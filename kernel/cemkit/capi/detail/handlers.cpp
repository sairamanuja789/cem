#include "cemkit/capi/detail/handlers.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cemkit/capi/cemkit.h"
#include "cemkit/core/fidelity.hpp"
#include "cemkit/core/provenance.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/sha256.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/geometry/occt/occt_backend.hpp"
#include "cemkit/geometry/port/backend.hpp"
#include "cemkit/physics/air.hpp"
#include "cemkit/product/products.hpp"
#include "cemkit/product/registry.hpp"
#include "cemkit/spec/catalogue.hpp"
#include "cemkit/spec/compiler.hpp"

namespace cemkit::capi::detail {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace physics = cemkit::physics;
namespace port = cemkit::geometry::port;
using product::air_of;
using product::member;
using product::number;
using product::text;

core::Length length_of(double v) { return v * isq::length[si::metre]; }

// Every compiled-in product's families and operations (ADR-015 rule 5), through the generated
// registration entry point: built per call, so the ABI keeps no state and names no product.
struct Products {
  std::shared_ptr<const product::Registry> registry;
  product::Operations operations;
};

Products products() {
  auto registry = std::make_shared<product::Registry>();
  product::Operations operations;
  if (auto ok = product::register_products(*registry, operations); !ok) {
    throw std::runtime_error{"product registration failed: " + core::describe(ok.error())};
  }
  return Products{.registry = std::move(registry), .operations = std::move(operations)};
}

// --- response encoding ------------------------------------------------------------------------

Json versions(const Products& p) {
  Json models = Json::object();
  for (const auto& model : p.operations.models()) {
    models[model.name] = core::to_string(model.version);
  }
  const auto air = physics::air_model();
  models[air.name] = core::to_string(air.version);
  Json plugins = Json::object();
  for (const auto& id : p.registry->ids()) {
    if (auto family = p.registry->get(id)) {
      plugins[id] = core::to_string(family->get().plugin().version);
    }
  }
  return Json{{"abi_version", std::to_string(CEMKIT_ABI_VERSION_MAJOR) + "." +
                                  std::to_string(CEMKIT_ABI_VERSION_MINOR) + "." +
                                  std::to_string(CEMKIT_ABI_VERSION_PATCH)},
              {"kernel_version", std::string{core::kernel_version()}},
              {"models", models},
              {"plugins", plugins}};
}

template <class R, class U>
Json result_json(const R& r, U unit) {
  if (!r) {
    return Json{{"error", error_json(r.error())}};
  }
  return product::labelled_json(*r, unit);
}

// One L0 case: the platform's air functions here, every other function from the operation a
// product registered under that name (ADR-015 rule 5).
Json evaluate_case(const Json& c, const product::Operations& operations) {
  const std::string fn = text(c, "function");
  [[maybe_unused]] const Json& args = member(c, "args");
  const physics::Air air = air_of(c, "air");
  if (fn == "ideal_gas_density") {
    const auto r = physics::ideal_gas_density(
        core::units::absolute_zero_pressure + number(args, "pressure") * isq::pressure[si::pascal],
        mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(number(args, "temperature")));
    return result_json(r, si::kilogram / mp_units::cubic(si::metre));
  }
  if (fn == "speed_of_sound") {
    return result_json(physics::speed_of_sound(air), si::metre / si::second);
  }
  const product::Operation* operation = operations.find(fn);
  if (operation == nullptr) {
    throw BadRequest{"function", "unknown function: " + fn};
  }
  auto r = (*operation)(c);
  if (!r) {
    return Json{{"error", error_json(r.error())}};
  }
  return std::move(*r);
}

// --- spec -------------------------------------------------------------------------------------

// `legacy` lists the qualifier names the family declares: each is reported on every field (null
// where absent), as ABI 0.2.0 did for "pressure_kind" (ADR-015 rule 6); "qualifier" is the
// product-neutral form.
Json field_json(const spec::Field& f, const std::vector<std::string>& legacy) {
  Json out{{"provenance", std::string{core::to_string(f.provenance)}},
           {"provisional", f.provisional}};
  const auto put = [&out](const char* key, const auto& optional) {
    out[key] = optional ? Json(*optional) : Json(nullptr);
  };
  put("original_value", f.original_value);
  put("original_unit", f.original_unit);
  put("si_value", f.si_value);
  put("si_unit", f.si_unit);
  put("text_value", f.text_value);
  put("confidence", f.confidence);
  put("note", f.note);
  for (const auto& name : legacy) {
    out[name] = f.qualifier && f.qualifier->name == name ? Json(f.qualifier->value) : Json(nullptr);
  }
  out["qualifier"] = f.qualifier ? Json{{"name", f.qualifier->name}, {"value", f.qualifier->value}}
                                 : Json(nullptr);
  if (f.tolerance) {
    const auto& t = *f.tolerance;
    out["tolerance"] =
        Json{{"type", t.type == spec::Tolerance::Type::relative ? "relative" : "absolute"},
             {"minus", t.minus},
             {"plus", t.plus},
             {"original_minus", t.original_minus},
             {"original_plus", t.original_plus},
             {"unit", t.unit}};
  } else {
    out["tolerance"] = nullptr;
  }
  return out;
}

// The qualifier names a family's catalogue declares, in declaration order.
std::vector<std::string> qualifier_names(const Products& p, std::string_view family) {
  std::vector<std::string> names;
  auto f = p.registry->get(family);
  if (!f) {
    return names;
  }
  std::set<std::string> seen;
  for (const auto& decl : f->get().field_catalogue().fields) {
    if (decl.qualifier && seen.insert(decl.qualifier->report_as).second) {
      names.push_back(decl.qualifier->report_as);
    }
  }
  return names;
}

Json spec_json(const spec::Spec& s, const std::vector<std::string>& legacy) {
  Json fields = Json::object();
  for (const auto& [path, field] : s.fields()) {  // std::map: sorted, deterministic
    fields[path] = field_json(field, legacy);
  }
  Json questions = Json::array();
  for (const auto& q : s.questions()) {
    questions.push_back(Json{{"field", q.field}, {"unit", q.unit}, {"reason", q.reason}});
  }
  Json conflicts = Json::array();
  for (const auto& c : s.conflicts()) {
    conflicts.push_back(Json{{"field", c.field},
                             {"winning", std::string{core::to_string(c.winning)}},
                             {"overridden", std::string{core::to_string(c.overridden)}}});
  }
  Json parent = nullptr;
  if (s.parent()) {
    parent = Json{{"spec_id", s.parent()->spec_id}, {"revision", s.parent()->revision}};
  }
  return Json{{"schema_version", s.schema_version()},
              {"spec_id", s.spec_id()},
              {"revision", s.revision()},
              {"parent", parent},
              {"family", s.family()},
              {"title", s.title()},
              {"fields", fields},
              {"questions", questions},
              {"conflicts", conflicts},
              {"has_unresolved_essential_unknowns", s.has_unresolved_essential_unknowns()}};
}

// --- geometry ---------------------------------------------------------------------------------

port::TestSolidParams solid_params_of(const Json& request) {
  port::TestSolidParams params{.hub_radius = length_of(number(request, "hub_radius")),
                               .hub_length = length_of(number(request, "hub_length")),
                               .sections = {}};
  const Json& sections = member(request, "sections");
  if (!sections.is_array()) {
    throw BadRequest{"sections", "must be an array: sections"};
  }
  for (const auto& s : sections) {
    params.sections.push_back(
        port::PlateSection{.radius = length_of(number(s, "radius")),
                           .chord = length_of(number(s, "chord")),
                           .thickness = length_of(number(s, "thickness")),
                           .stagger = number(s, "stagger") * isq::angular_measure[si::radian]});
  }
  return params;
}

// RFC 4648 base64 (standard alphabet, with padding), for export bytes in a JSON response.
std::string base64(std::string_view bytes) {
  static constexpr std::string_view k_alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const auto n = (static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16U) |
                   (static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1])) << 8U) |
                   static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 2]));
    out += k_alphabet[(n >> 18U) & 63U];
    out += k_alphabet[(n >> 12U) & 63U];
    out += k_alphabet[(n >> 6U) & 63U];
    out += k_alphabet[n & 63U];
  }
  if (i < bytes.size()) {
    auto n = static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16U;
    const bool two = i + 1 < bytes.size();
    if (two) {
      n |= static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1])) << 8U;
    }
    out += k_alphabet[(n >> 18U) & 63U];
    out += k_alphabet[(n >> 12U) & 63U];
    out += two ? k_alphabet[(n >> 6U) & 63U] : '=';
    out += '=';
  }
  return out;
}

port::ExportRequest export_request_of(const Json& e) {
  const std::string format = text(e, "format");
  if (format == "step") {
    return port::ExportRequest{.format = port::ExportFormat::step, .tessellation = std::nullopt};
  }
  if (format == "stl") {
    return port::ExportRequest{.format = port::ExportFormat::stl,
                               .tessellation = port::Tessellation{
                                   .linear_deflection = length_of(number(e, "linear_deflection")),
                                   .angular_deflection = number(e, "angular_deflection") *
                                                         isq::angular_measure[si::radian]}};
  }
  throw BadRequest{"format", "unknown export format: " + format};
}

core::Result<spec::Spec> compile_spec(const Json& request, const Products& p) {
  bool autonomous = false;
  if (request.contains("autonomous_mode")) {
    if (!request["autonomous_mode"].is_boolean()) {
      throw BadRequest{"autonomous_mode", "must be a boolean: autonomous_mode"};
    }
    autonomous = request["autonomous_mode"].get<bool>();
  }
  // The spec compiler takes nlohmann::json (unordered keys); the conversion keeps every value.
  const nlohmann::json document = member(request, "spec");
  const spec::SpecCompiler compiler{product::compiler_options(p.registry, autonomous)};
  return compiler.compile(document);
}

// SEL-004 for a compiled spec: the family's feasibility gate (ADR-015 rule 4; docs/capi.md).
core::Result<Json> feasibility_from_spec(const Json& request) {
  const Products p = products();
  auto compiled = compile_spec(request, p);
  if (!compiled) {
    return std::unexpected(compiled.error());
  }
  const spec::Spec& s = *compiled;
  auto family = p.registry->get(s.family());
  if (!family) {
    return std::unexpected(family.error());
  }
  auto outcome = family->get().feasibility(s);
  if (!outcome) {
    return std::unexpected(outcome.error());
  }
  Json out = versions(p);
  out["spec"] = Json{{"spec_id", s.spec_id()}, {"revision", s.revision()}, {"family", s.family()}};
  out["inputs"] = std::move(outcome->inputs);
  out["report"] = std::move(outcome->report);
  return out;
}

}  // namespace

Json error_json(const core::Error& error) { return product::error_json(error); }

core::Result<Json> spec_compile(const Json& request) {
  const Products p = products();
  auto compiled = compile_spec(request, p);
  if (!compiled) {
    return std::unexpected(compiled.error());
  }
  Json out = versions(p);
  out["spec"] = spec_json(*compiled, qualifier_names(p, compiled->family()));
  return out;
}

// Without a spec: the product operation "check_feasibility" on the request's duty, air and range.
core::Result<Json> feasibility(const Json& request) {
  if (request.contains("spec")) {
    return feasibility_from_spec(request);
  }
  const Products p = products();
  const product::Operation* operation = p.operations.find("check_feasibility");
  if (operation == nullptr) {
    return core::fail(core::ErrorCode::not_implemented, "no product offers check_feasibility",
                      "function", {{"capability", "check_feasibility"}});
  }
  Json c{{"function", "check_feasibility"}, {"args", request}};
  if (request.contains("air")) {
    c["air"] = request["air"];
  }
  if (request.contains("range")) {
    c["range"] = request["range"];
  }
  auto r = (*operation)(c);
  if (!r) {
    return std::unexpected(r.error());
  }
  Json out = versions(p);
  out["report"] = std::move((*r)["report"]);
  return out;
}

core::Result<Json> l0_batch(const Json& request) {
  const Json& cases = member(request, "cases");
  if (!cases.is_array()) {
    throw BadRequest{"cases", "must be an array: cases"};
  }
  const Products p = products();
  Json results = Json::array();
  results.get_ref<Json::array_t&>().reserve(cases.size());
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const Json& c = cases[i];
    Json row = Json{{"id", c.contains("id") ? c["id"] : Json(i)}};
    try {
      row.update(evaluate_case(c, p.operations));
    } catch (const BadRequest& bad) {
      throw BadRequest{"cases[" + std::to_string(i) + "]." + bad.subject(), bad.what()};
    }
    results.push_back(std::move(row));
  }
  Json out = versions(p);
  out["results"] = std::move(results);
  return out;
}

core::Result<Json> geometry_smoke(const Json& request) {
  const port::TestSolidParams params = solid_params_of(request);
  std::vector<port::ExportRequest> exports;
  if (request.contains("exports")) {
    if (!request["exports"].is_array()) {
      throw BadRequest{"exports", "must be an array: exports"};
    }
    for (const auto& e : request["exports"]) {
      exports.push_back(export_request_of(e));
    }
  }
  const geometry::occt::OcctBackend backend;
  auto solid = backend.build_test_solid(params);
  if (!solid) {
    return std::unexpected(solid.error());
  }
  auto validity = backend.check_validity(**solid);
  auto topology = backend.topology(**solid);
  auto mass = backend.mass_properties(**solid);
  if (!validity) {
    return std::unexpected(validity.error());
  }
  if (!topology) {
    return std::unexpected(topology.error());
  }
  if (!mass) {
    return std::unexpected(mass.error());
  }
  bool include_data = false;
  if (request.contains("include_data")) {
    if (!request["include_data"].is_boolean()) {
      throw BadRequest{"include_data", "must be a boolean: include_data"};
    }
    include_data = request["include_data"].get<bool>();
  }
  Json files = Json::array();
  for (const auto& e : exports) {
    auto bytes = backend.export_bytes(**solid, e);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    Json file{{"format", std::string{port::extension(e.format)}},
              {"sha256", core::sha256_hex(*bytes)},
              {"size_bytes", bytes->size()}};
    if (include_data) {
      file["data_base64"] = base64(*bytes);
    }
    files.push_back(std::move(file));
  }
  const auto& v = *validity;
  const auto& t = *topology;
  const auto& m = *mass;
  Json out = versions(products());
  out["backend"] = std::string{backend.name()};
  out["validity"] = Json{{"ok", v.ok()},
                         {"brep_valid", v.brep_valid},
                         {"closed", v.closed},
                         {"manifold", v.manifold},
                         {"self_intersection_free", v.self_intersection_free},
                         {"free_edges", v.free_edges},
                         {"non_manifold_edges", v.non_manifold_edges}};
  out["topology"] = Json{{"solids", t.solids},
                         {"shells", t.shells},
                         {"faces", t.faces},
                         {"edges", t.edges},
                         {"vertices", t.vertices}};
  out["mass_properties"] =
      Json{{"volume", m.volume.numerical_value_in(mp_units::cubic(si::metre))},
           {"area", m.area.numerical_value_in(mp_units::square(si::metre))},
           {"centroid", Json::array({m.centroid[0].numerical_value_in(si::metre),
                                     m.centroid[1].numerical_value_in(si::metre),
                                     m.centroid[2].numerical_value_in(si::metre)})}};
  out["exports"] = std::move(files);
  return out;
}

}  // namespace cemkit::capi::detail

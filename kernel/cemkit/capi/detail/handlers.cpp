#include "cemkit/capi/detail/handlers.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cstddef>
#include <memory>
#include <optional>
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
#include "cemkit/product/registry.hpp"
#include "cemkit/spec/compiler.hpp"
#include "products/fans/common/feasibility.hpp"
#include "products/fans/common/l0.hpp"
#include "products/fans/register.hpp"

namespace cemkit::capi::detail {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace fans = cemkit::fans;
namespace physics = cemkit::physics;
namespace port = cemkit::geometry::port;

// --- request decoding (every number is in its coherent SI unit) -----------------------------

const Json& member(const Json& object, const char* key) {
  if (!object.is_object() || !object.contains(key)) {
    throw BadRequest{key, std::string{"missing key: "} + key};
  }
  return object[key];
}

double number(const Json& object, const char* key) {
  const Json& value = member(object, key);
  if (!value.is_number()) {
    throw BadRequest{key, std::string{"must be a number: "} + key};
  }
  return value.get<double>();
}

std::string text(const Json& object, const char* key) {
  const Json& value = member(object, key);
  if (!value.is_string()) {
    throw BadRequest{key, std::string{"must be a string: "} + key};
  }
  return value.get<std::string>();
}

std::optional<double> optional_number(const Json& object, const char* key) {
  if (!object.contains(key) || object[key].is_null()) {
    return std::nullopt;
  }
  return number(object, key);
}

core::AngularVelocity omega_of(double v) {
  return v * isq::angular_velocity[si::radian / si::second];
}
core::Length length_of(double v) { return v * isq::length[si::metre]; }
core::VolumeFlowRate flow_of(double v) {
  return v * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
}
fans::FanTotalPressure total_of(double v) { return v * fans::fan_total_pressure[si::pascal]; }

// The request's air: each property given in the request, the rest from the AX-005 default air
// with the 1976 constants (physics::default_air), so no constant is repeated outside the kernel.
physics::Air air_of(const Json& object, const char* key) {
  physics::Air air = physics::default_air();
  if (!object.contains(key)) {
    return air;
  }
  const Json& a = object[key];
  if (!a.is_object()) {
    throw BadRequest{key, std::string{"must be an object: "} + key};
  }
  if (auto v = optional_number(a, "density")) {
    air.density = *v * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)];
  }
  if (auto v = optional_number(a, "temperature")) {
    air.temperature = mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(*v);
  }
  if (auto v = optional_number(a, "pressure")) {
    air.pressure = core::units::absolute_zero_pressure + *v * isq::pressure[si::pascal];
  }
  if (auto v = optional_number(a, "gamma")) {
    air.gamma = *v * isq::ratio_of_specific_heat_capacities[mp_units::one];
  }
  if (auto v = optional_number(a, "gas_constant")) {
    air.gas_constant = *v * isq::specific_gas_constant[si::joule / (si::kilogram * si::kelvin)];
  }
  return air;
}

// The registry of every compiled-in family (ADR-009): built per call, so the ABI keeps no state.
std::shared_ptr<const product::Registry> family_registry() {
  auto registry = std::make_shared<product::Registry>();
  if (auto ok = fans::register_fan_families(*registry); !ok) {
    throw std::runtime_error{"family registration failed: " + core::describe(ok.error())};
  }
  return registry;
}

fans::FamilyRange range_of(const Json& r) {
  return fans::FamilyRange{.family = text(r, "family"),
                           .specific_speed_min = optional_number(r, "specific_speed_min"),
                           .specific_speed_max = optional_number(r, "specific_speed_max"),
                           .source = text(r, "source")};
}

// --- response encoding ------------------------------------------------------------------------

Json versions() {
  Json models = Json::object();
  for (const auto& model : {fans::l0_model(), fans::feasibility_model(), physics::air_model()}) {
    models[model.name] = core::to_string(model.version);
  }
  Json plugins = Json::object();
  const auto registry = family_registry();
  for (const auto& id : registry->ids()) {
    if (auto family = registry->get(id)) {
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

template <class Q, class U>
Json labelled_json(const core::Labelled<Q>& l, U unit) {
  return Json{{"value", l.value().numerical_value_in(unit)},
              {"fidelity", std::string{core::to_string(l.fidelity())}},
              {"fidelity_label", std::string{core::report_label(l.fidelity())}},
              {"model", core::to_string(l.model())}};
}

template <class R, class U>
Json result_json(const R& r, U unit) {
  if (!r) {
    return Json{{"error", error_json(r.error())}};
  }
  return labelled_json(*r, unit);
}

Json duty_json(const fans::LabelledDuty& d) {
  return Json{{"flow", labelled_json(d.flow, mp_units::cubic(si::metre) / si::second)},
              {"fan_total_pressure", labelled_json(d.pressure, si::pascal)}};
}

Json report_json(const fans::FeasibilityReport& report) {
  Json checks = Json::array();
  for (const auto& c : report.checks) {
    checks.push_back(Json{
        {"limit", c.limit},
        {"status", std::string{fans::to_string(c.status)}},
        {"message", c.message},
        {"violation", c.violation ? error_json(*c.violation) : Json(nullptr)},
        {"nearest_feasible", c.nearest_feasible ? duty_json(*c.nearest_feasible) : Json(nullptr)}});
  }
  Json ws = nullptr;
  if (report.specific_speed) {
    ws = labelled_json(*report.specific_speed, mp_units::one);
  }
  return Json{{"verdict", std::string{fans::to_string(report.verdict())}},
              {"specific_speed", ws},
              {"checks", checks}};
}

// The feasibility gate for {"flow", "fan_total_pressure", ["omega"], ["d_tip"]} with air and range.
core::Result<fans::FeasibilityReport> run_feasibility(const Json& args, const physics::Air& air,
                                                      const Json& range) {
  std::optional<core::AngularVelocity> omega;
  std::optional<core::Length> d_tip;
  if (auto w = optional_number(args, "omega")) {
    omega = omega_of(*w);
  }
  if (auto d = optional_number(args, "d_tip")) {
    d_tip = length_of(*d);
  }
  const fans::Duty duty{.flow = flow_of(number(args, "flow")),
                        .pressure = total_of(number(args, "fan_total_pressure"))};
  return fans::check_feasibility(duty, air, range_of(range), omega, d_tip);
}

Json evaluate_case(const Json& c) {
  const std::string fn = text(c, "function");
  const Json& args = member(c, "args");
  const physics::Air air = air_of(c, "air");
  const auto pa = si::pascal;
  const auto ms = si::metre / si::second;
  if (fn == "tip_speed") {
    return result_json(
        fans::tip_speed(omega_of(number(args, "omega")), length_of(number(args, "d_tip"))), ms);
  }
  if (fn == "max_fan_total_pressure") {
    return result_json(fans::max_fan_total_pressure(air), pa);
  }
  if (fn == "max_tip_speed") {
    return result_json(fans::max_tip_speed(air), ms);
  }
  if (fn == "flow_coefficient") {
    return result_json(
        fans::flow_coefficient(flow_of(number(args, "flow")), length_of(number(args, "d_tip")),
                               omega_of(number(args, "omega")), air),
        mp_units::one);
  }
  if (fn == "pressure_coefficient") {
    return result_json(fans::pressure_coefficient(total_of(number(args, "fan_total_pressure")),
                                                  length_of(number(args, "d_tip")),
                                                  omega_of(number(args, "omega")), air),
                       mp_units::one);
  }
  if (fn == "specific_speed") {
    return result_json(
        fans::specific_speed(omega_of(number(args, "omega")), flow_of(number(args, "flow")),
                             total_of(number(args, "fan_total_pressure")), air),
        mp_units::one);
  }
  if (fn == "specific_diameter") {
    return result_json(
        fans::specific_diameter(length_of(number(args, "d_tip")), flow_of(number(args, "flow")),
                                total_of(number(args, "fan_total_pressure")), air),
        mp_units::one);
  }
  if (fn == "conventional_dynamic_pressure") {
    return result_json(fans::conventional_dynamic_pressure(flow_of(number(args, "flow")),
                                                           length_of(number(args, "d_duct")), air),
                       pa);
  }
  if (fn == "fan_total_from_static") {
    const auto r = fans::fan_total_from_static(
        number(args, "fan_static_pressure") * fans::fan_static_pressure[si::pascal],
        flow_of(number(args, "flow")), length_of(number(args, "d_duct")), air);
    if (!r) {
      return Json{{"error", error_json(r.error())}};
    }
    Json out = labelled_json(r->value, pa);
    out["provenance"] = std::string{core::to_string(r->provenance)};
    out["rule"] = std::string{r->rule};
    return out;
  }
  if (fn == "scale_fan_laws") {
    const Json& p = member(args, "point");
    const fans::FanPoint point{.omega = omega_of(number(p, "omega")),
                               .d_tip = length_of(number(p, "d_tip")),
                               .flow = flow_of(number(p, "flow")),
                               .pressure = total_of(number(p, "fan_total_pressure")),
                               .power = number(p, "power") * isq::power[si::watt],
                               .air = air};
    const physics::Air air_new = c.contains("air_new") ? air_of(c, "air_new") : air;
    const auto r = fans::scale_fan_laws(point, omega_of(number(args, "omega")),
                                        length_of(number(args, "d_tip")), air_new);
    if (!r) {
      return Json{{"error", error_json(r.error())}};
    }
    return Json{
        {"value",
         {{"flow", r->flow.value().numerical_value_in(mp_units::cubic(si::metre) / si::second)},
          {"fan_total_pressure", r->pressure.value().numerical_value_in(pa)},
          {"power", r->power.value().numerical_value_in(si::watt)}}},
        {"fidelity", std::string{core::to_string(r->flow.fidelity())}},
        {"fidelity_label", std::string{core::report_label(r->flow.fidelity())}},
        {"model", core::to_string(r->flow.model())}};
  }
  if (fn == "ideal_gas_density") {
    const auto r = physics::ideal_gas_density(
        core::units::absolute_zero_pressure + number(args, "pressure") * isq::pressure[si::pascal],
        mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(number(args, "temperature")));
    return result_json(r, si::kilogram / mp_units::cubic(si::metre));
  }
  if (fn == "speed_of_sound") {
    return result_json(physics::speed_of_sound(air), ms);
  }
  if (fn == "check_feasibility") {
    const auto r = run_feasibility(args, air, member(c, "range"));
    if (!r) {
      return Json{{"error", error_json(r.error())}};
    }
    return Json{{"report", report_json(*r)}};
  }
  throw BadRequest{"function", "unknown function: " + fn};
}

// --- spec -------------------------------------------------------------------------------------

Json field_json(const spec::Field& f) {
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
  put("pressure_kind", f.pressure_kind);
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

Json spec_json(const spec::Spec& s) {
  Json fields = Json::object();
  for (const auto& [path, field] : s.fields()) {  // std::map: sorted, deterministic
    fields[path] = field_json(field);
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

}  // namespace

Json error_json(const core::Error& error) {
  Json details = Json::object();
  for (const auto& [key, value] : error.details()) {
    details[key] = value;
  }
  return Json{{"code", std::string{core::to_string(error.code())}},
              {"subject", error.subject()},
              {"message", error.message()},
              {"details", details},
              {"describe", core::describe(error)}};
}

core::Result<Json> spec_compile(const Json& request) {
  bool autonomous = false;
  if (request.contains("autonomous_mode")) {
    if (!request["autonomous_mode"].is_boolean()) {
      throw BadRequest{"autonomous_mode", "must be a boolean: autonomous_mode"};
    }
    autonomous = request["autonomous_mode"].get<bool>();
  }
  // The spec compiler takes nlohmann::json (unordered keys); the conversion keeps every value.
  const nlohmann::json document = member(request, "spec");
  const spec::SpecCompiler compiler{
      spec::CompilerOptions{.autonomous_mode = autonomous,
                            .essential_resolver = product::essential_field_resolver(family_registry()),
                            .default_resolver = {}}};
  auto compiled = compiler.compile(document);
  if (!compiled) {
    return std::unexpected(compiled.error());
  }
  Json out = versions();
  out["spec"] = spec_json(*compiled);
  return out;
}

core::Result<Json> feasibility(const Json& request) {
  const auto r = run_feasibility(request, air_of(request, "air"), member(request, "range"));
  if (!r) {
    return std::unexpected(r.error());
  }
  Json out = versions();
  out["report"] = report_json(*r);
  return out;
}

core::Result<Json> l0_batch(const Json& request) {
  const Json& cases = member(request, "cases");
  if (!cases.is_array()) {
    throw BadRequest{"cases", "must be an array: cases"};
  }
  Json results = Json::array();
  results.get_ref<Json::array_t&>().reserve(cases.size());
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const Json& c = cases[i];
    Json row = Json{{"id", c.contains("id") ? c["id"] : Json(i)}};
    try {
      row.update(evaluate_case(c));
    } catch (const BadRequest& bad) {
      throw BadRequest{"cases[" + std::to_string(i) + "]." + bad.subject(), bad.what()};
    }
    results.push_back(std::move(row));
  }
  Json out = versions();
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
  Json out = versions();
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

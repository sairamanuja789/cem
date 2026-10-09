#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <nlohmann/json.hpp>

#include "cemkit/core/error.hpp"
#include "cemkit/spec/compiler.hpp"

using Catch::Matchers::WithinRel;
using json = nlohmann::json;
using namespace cemkit::spec;

namespace {

// A missing optional becomes NaN, which never matches an expected number, so a CHECK on
// value_or(k_nan) still fails when the value is absent (repo pattern, see core tests).
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();

// Returns the field or an empty Field. Callers REQUIRE presence first.
Field field_or_empty(const Spec& spec, std::string_view path) {
  return spec.field(path).value_or(Field{});
}

json base_valid_spec() {
  return json{
      {"schema_version", "1.0.0"},
      {"spec_id", "axial-120"},
      {"revision", 1},
      {"parent", nullptr},
      {"family", "fans.axial_ducted"},
      {"title", "120 mm ducted axial fan"},
      {"air",
       {{"density", {{"value", 1.18}, {"unit", "kg/m3"}, {"provenance", "default"}}},
        {"temperature", {{"value", 298.15}, {"unit", "K"}, {"provenance", "default"}}}}},
      {"manufacturing",
       {{"process", {{"value", "FDM"}, {"provenance", "user"}}},
        {"material",
         {{"value", "PETG"},
          {"provenance", "default"},
          {"provisional", true},
          {"note", "coupon tests needed"}}}}},
      {"envelope",
       {{"width", {{"value", 120.0}, {"unit", "mm"}, {"provenance", "default"}}},
        {"height", {{"value", 120.0}, {"unit", "mm"}, {"provenance", "default"}}},
        {"depth_min", {{"value", 25.0}, {"unit", "mm"}, {"provenance", "default"}}},
        {"depth_max", {{"value", 40.0}, {"unit", "mm"}, {"provenance", "default"}}}}},
      {"objectives",
       json::array(
           {json{{"quantity", "total_to_static_efficiency"},
                 {"sense", "maximize"},
                 {"provenance", "derived"}},
            json{{"quantity", "tip_speed"}, {"sense", "minimize"}, {"provenance", "derived"}},
            json{{"quantity", "rotor_mass"}, {"sense", "minimize"}, {"provenance", "derived"}}})},
      {"product",
       {{"duty",
         {{"flow",
           {{"value", 0.05},
            {"unit", "m3/s"},
            {"provenance", "user"},
            {"tolerance", {{"relative", 0.05}}}}},
          {"pressure",
           {{"value", 150.0},
            {"unit", "Pa"},
            {"kind", "fan_static"},
            {"provenance", "user"},
            {"tolerance", {{"minus", 0.0}, {"plus", 15.0}}}}}}},
        {"nominal_size", {{"value", 120.0}, {"unit", "mm"}, {"provenance", "user"}}},
        {"size_reference", {{"value", "duct_inner_diameter"}, {"provenance", "default"}}},
        {"rotational_speed", {{"value", 2000.0}, {"unit", "rpm"}, {"provenance", "default"}}},
        {"wall_thickness_min", {{"value", 0.8}, {"unit", "mm"}, {"provenance", "default"}}},
        {"tip_clearance_min", {{"value", 0.5}, {"unit", "mm"}, {"provenance", "default"}}},
        {"pressure_margin", {{"value", 0.10}, {"unit", "1"}, {"provenance", "derived"}}},
        {"safety_factor",
         {{"factor", {{"value", 2.0}, {"unit", "1"}, {"provenance", "default"}}},
          {"at_speed_ratio", {{"value", 1.2}, {"unit", "1"}, {"provenance", "default"}}}}},
        {"scope",
         {{"value", json::array({"rotor", "hub", "blades", "duct"})}, {"provenance", "user"}}},
        {"motor",
         {{"bore_diameter", {{"value", nullptr}, {"unit", "mm"}, {"provenance", "unknown"}}},
          {"speed_min", {{"value", nullptr}, {"unit", "rpm"}, {"provenance", "unknown"}}},
          {"speed_max", {{"value", nullptr}, {"unit", "rpm"}, {"provenance", "unknown"}}}}}}}};
}

}  // namespace

TEST_CASE("compiler accepts valid JSON structured spec", "[IN-001]") {
  SpecCompiler compiler;
  const json doc = base_valid_spec();
  const auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  CHECK(res->spec_id() == "axial-120");
  CHECK(res->revision() == 1);
  CHECK(res->family() == "fans.axial_ducted");
}

TEST_CASE("compiler rejects bare numbers without field object or missing provenance",
          "[SPEC-001]") {
  SpecCompiler compiler;

  // Bare number instead of field object
  json doc1 = base_valid_spec();
  doc1["envelope"]["width"] = 120.0;
  auto res1 = compiler.compile(doc1);
  REQUIRE(!res1.has_value());
  CHECK(res1.error().code() == cemkit::core::ErrorCode::spec_rejected);
  CHECK(res1.error().subject() == "envelope.width");

  // Missing provenance
  json doc2 = base_valid_spec();
  doc2["envelope"]["width"] = json{{"value", 120.0}, {"unit", "mm"}};
  auto res2 = compiler.compile(doc2);
  REQUIRE(!res2.has_value());
  CHECK(res2.error().code() == cemkit::core::ErrorCode::spec_rejected);
  CHECK(res2.error().subject() == "envelope.width");
}

TEST_CASE("inputs are converted to SI keeping original value and unit", "[SPEC-002]") {
  SpecCompiler compiler;
  const auto res = compiler.compile(base_valid_spec());
  REQUIRE(res.has_value());

  REQUIRE(res->field("envelope.width").has_value());
  const Field width = field_or_empty(*res, "envelope.width");
  REQUIRE(width.si_value.has_value());
  CHECK(width.original_value == 120.0);
  CHECK(width.original_unit == "mm");
  CHECK_THAT(width.si_value.value_or(k_nan), WithinRel(0.12, 1e-12));
  CHECK(width.si_unit == "m");

  REQUIRE(res->field("product.rotational_speed").has_value());
  const Field rpm = field_or_empty(*res, "product.rotational_speed");
  REQUIRE(rpm.si_value.has_value());
  CHECK(rpm.original_value == 2000.0);
  CHECK(rpm.original_unit == "rpm");
  CHECK_THAT(rpm.si_value.value_or(k_nan), WithinRel(209.43951023931953, 1e-12));
  CHECK(rpm.si_unit == "rad/s");
}

TEST_CASE("dimensionally inconsistent input is rejected naming the field", "[SPEC-003]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  doc["product"]["duty"]["pressure"]["unit"] = "m3/s";
  const auto res = compiler.compile(doc);
  REQUIRE(!res.has_value());
  CHECK(res.error().code() == cemkit::core::ErrorCode::unit_mismatch);
  CHECK(res.error().subject() == "product.duty.pressure");
}

TEST_CASE("pressure semantics: accept only fan_total and fan_static, reject static-to-static",
          "[SPEC-004]") {
  SpecCompiler compiler;

  // Missing kind
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["pressure"].erase("kind");
    const auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.pressure");
  }

  // static_to_static rejected
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["pressure"]["kind"] = "static_to_static";
    const auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.pressure");
  }

  // total_to_static rejected as pressure type
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["pressure"]["kind"] = "total_to_static";
    const auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.pressure");
    CHECK(res.error().message().find("efficiency type") != std::string::npos);
  }

  // fan_total accepted
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["pressure"]["kind"] = "fan_total";
    const auto res = compiler.compile(doc);
    REQUIRE(res.has_value());
    REQUIRE(res->field("product.duty.pressure").has_value());
    CHECK(field_or_empty(*res, "product.duty.pressure").pressure_kind == "fan_total");
  }
}

TEST_CASE("precedence rule: user > image > derived > default", "[SPEC-005]") {
  // Our compiler resolves precedence: user overrides image/derived/default
  SpecCompiler compiler;
  json doc = base_valid_spec();
  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());

  // Derive update with user overriding default
  json updates = json::object();
  updates["envelope"] = json{{"width", {{"value", 140.0}, {"unit", "mm"}, {"provenance", "user"}}}};
  auto derived = res->derive_new_revision(updates);
  REQUIRE(derived.has_value());
  REQUIRE(derived->field("envelope.width").has_value());
  const Field env_w = field_or_empty(*derived, "envelope.width");
  REQUIRE(env_w.si_value.has_value());
  CHECK(env_w.provenance == cemkit::core::Provenance::user);
  CHECK_THAT(env_w.si_value.value_or(k_nan), WithinRel(0.14, 1e-12));
  // Higher rank wins; the conflict records the winner and the overridden default.
  REQUIRE(derived->conflicts().size() == 1);
  CHECK(derived->conflicts()[0].field == "envelope.width");
  CHECK(derived->conflicts()[0].winning == cemkit::core::Provenance::user);
  CHECK(derived->conflicts()[0].overridden == cemkit::core::Provenance::default_value);
}

TEST_CASE("essential fields are declared by family plugin stub", "[SPEC-006]") {
  const auto fields = default_essential_fields("fans.axial_ducted");
  CHECK(fields.size() >= 2);
  bool has_flow = false;
  bool has_pressure = false;
  for (const auto& f : fields) {
    if (f == "product.duty.flow") {
      has_flow = true;
    }
    if (f == "product.duty.pressure") {
      has_pressure = true;
    }
  }
  CHECK(has_flow);
  CHECK(has_pressure);
}

TEST_CASE("spec with unresolved essential unknowns cannot start campaign unless autonomous",
          "[SPEC-007]") {
  json doc = base_valid_spec();
  doc["product"]["duty"]["flow"] =
      json{{"value", nullptr}, {"unit", "m3/s"}, {"provenance", "unknown"}};
  doc["product"]["duty"]["pressure"] =
      json{{"value", nullptr}, {"unit", "Pa"}, {"provenance", "unknown"}};

  // Normal mode: has unresolved essential unknowns
  SpecCompiler normal_compiler(CompilerOptions{.autonomous_mode = false});
  auto res1 = normal_compiler.compile(doc);
  REQUIRE(res1.has_value());
  CHECK(res1->has_unresolved_essential_unknowns());
  CHECK(res1->questions().size() == 2);

  // Autonomous mode without default resolver still has unresolved unknowns
  SpecCompiler auto_compiler_no_defaults(CompilerOptions{.autonomous_mode = true});
  auto res2 = auto_compiler_no_defaults.compile(doc);
  REQUIRE(res2.has_value());
  CHECK(res2->has_unresolved_essential_unknowns());
  CHECK(res2->questions().size() == 2);
}

TEST_CASE("compiler produces questions for essential unknowns", "[SPEC-008]") {
  json doc = base_valid_spec();
  doc["product"]["duty"]["flow"] =
      json{{"value", nullptr}, {"unit", "m3/s"}, {"provenance", "unknown"}};
  doc["product"]["duty"]["pressure"] =
      json{{"value", nullptr}, {"unit", "Pa"}, {"provenance", "unknown"}};

  SpecCompiler compiler;
  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  REQUIRE(res->questions().size() == 2);
  CHECK(res->questions()[0].field == "product.duty.flow");
  CHECK(!res->questions()[0].reason.empty());
  CHECK(res->questions()[1].field == "product.duty.pressure");
  CHECK(!res->questions()[1].reason.empty());
}

TEST_CASE("specs are immutable and versioned with parent link", "[SPEC-009]") {
  SpecCompiler compiler;
  auto res = compiler.compile(base_valid_spec());
  REQUIRE(res.has_value());
  CHECK(res->revision() == 1);
  CHECK(!res->parent().has_value());

  json mods = json::object();
  mods["title"] = "Updated axial fan title";
  auto rev2 = res->derive_new_revision(mods);
  REQUIRE(rev2.has_value());
  CHECK(rev2->revision() == 2);
  REQUIRE(rev2->parent().has_value());
  const SpecRef parent = rev2->parent().value_or(SpecRef{.spec_id = "", .revision = 0});
  CHECK(parent.spec_id == res->spec_id());
  CHECK(parent.revision == 1);
  CHECK(rev2->title() == "Updated axial fan title");
}

TEST_CASE("physically contradictory requirements are rejected", "[SPEC-010]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  // Duty point: Q = 0.05 m3/s, pressure = 1500 Pa -> air power P_air = 0.05 * 1500 = 75 W
  doc["product"]["duty"]["pressure"]["value"] = 1500.0;
  // State an envelope/motor power limit of 20 W
  doc["product"]["power_limit"] = json{{"value", 20.0}, {"unit", "W"}, {"provenance", "user"}};

  auto res = compiler.compile(doc);
  REQUIRE(!res.has_value());
  CHECK(res.error().code() == cemkit::core::ErrorCode::infeasible_requirement);
  CHECK(res.error().subject() == "product.power_limit");
}

TEST_CASE("known duty point flow and pressure must carry tolerances", "[SPEC-011]") {
  SpecCompiler compiler;

  // Missing flow tolerance
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["flow"].erase("tolerance");
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.flow");
  }

  // Missing pressure tolerance
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["pressure"].erase("tolerance");
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.pressure");
  }
}

TEST_CASE("SpecCompiler::compile_json handles valid and malformed JSON", "[SPEC-001]") {
  SpecCompiler compiler;
  const json doc = base_valid_spec();
  const auto valid_res = compiler.compile_json(doc.dump());
  REQUIRE(valid_res.has_value());

  const auto invalid_res = compiler.compile_json("{malformed json string");
  REQUIRE(!invalid_res.has_value());
  CHECK(invalid_res.error().code() == cemkit::core::ErrorCode::invalid_input);
}

TEST_CASE("top-level spec validation errors", "[SPEC-001]") {
  SpecCompiler compiler;

  // Not a JSON object
  const auto non_obj = compiler.compile(json::array());
  REQUIRE(!non_obj.has_value());
  CHECK(non_obj.error().code() == cemkit::core::ErrorCode::spec_rejected);

  // Missing schema_version
  {
    json d = base_valid_spec();
    d.erase("schema_version");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().subject() == "schema_version");
  }

  // Missing spec_id
  {
    json d = base_valid_spec();
    d.erase("spec_id");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().subject() == "spec_id");
  }

  // Missing revision
  {
    json d = base_valid_spec();
    d.erase("revision");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().subject() == "revision");
  }

  // Missing family
  {
    json d = base_valid_spec();
    d.erase("family");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().subject() == "family");
  }
}

TEST_CASE("field validation error branches", "[SPEC-004]") {
  SpecCompiler compiler;

  // Field is a primitive instead of object
  {
    json d = base_valid_spec();
    d["air"]["density"] = 1.18;
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Missing provenance
  {
    json d = base_valid_spec();
    d["envelope"]["width"].erase("provenance");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Invalid provenance string
  {
    json d = base_valid_spec();
    d["envelope"]["width"]["provenance"] = "invalid_prov";
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Image field without confidence
  {
    json d = base_valid_spec();
    d["envelope"]["width"]["provenance"] = "image";
    d["envelope"]["width"].erase("confidence");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Image field confidence out of [0, 1]
  {
    json d = base_valid_spec();
    d["envelope"]["width"]["provenance"] = "image";
    d["envelope"]["width"]["confidence"] = 1.5;
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Non-image field with confidence
  {
    json d = base_valid_spec();
    d["envelope"]["width"]["provenance"] = "user";
    d["envelope"]["width"]["confidence"] = 0.9;
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Missing value key
  {
    json d = base_valid_spec();
    d["envelope"]["width"].erase("value");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Unknown field with non-null value
  {
    json d = base_valid_spec();
    d["product"]["rotational_speed"]["provenance"] = "unknown";
    d["product"]["rotational_speed"]["value"] = 2000.0;
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Unknown field carrying tolerance
  {
    json d = base_valid_spec();
    d["product"]["rotational_speed"]["provenance"] = "unknown";
    d["product"]["rotational_speed"]["value"] = nullptr;
    d["product"]["rotational_speed"]["tolerance"] = json{{"relative", 0.05}};
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Known field with null value
  {
    json d = base_valid_spec();
    d["product"]["rotational_speed"]["provenance"] = "user";
    d["product"]["rotational_speed"]["value"] = nullptr;
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Numeric field with non-numeric value
  {
    json d = base_valid_spec();
    d["product"]["rotational_speed"]["value"] = "two thousand";
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Numeric field missing unit
  {
    json d = base_valid_spec();
    d["product"]["rotational_speed"].erase("unit");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }
}

TEST_CASE("pressure kind validation error paths", "[SPEC-004]") {
  SpecCompiler compiler;

  // Pressure missing kind
  {
    json d = base_valid_spec();
    d["product"]["duty"]["pressure"].erase("kind");
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Pressure invalid kind
  {
    json d = base_valid_spec();
    d["product"]["duty"]["pressure"]["kind"] = "unknown_pressure_kind";
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }
}

TEST_CASE("tolerance validation error paths", "[SPEC-011]") {
  SpecCompiler compiler;

  // Relative tolerance <= 0
  {
    json d = base_valid_spec();
    d["product"]["duty"]["flow"]["tolerance"]["relative"] = -0.05;
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Absolute tolerance negative bounds
  {
    json d = base_valid_spec();
    d["product"]["duty"]["pressure"]["tolerance"] = json{{"minus", -10.0}, {"plus", 15.0}};
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }

  // Malformed tolerance object
  {
    json d = base_valid_spec();
    d["product"]["duty"]["flow"]["tolerance"] = json::object();
    const auto res = compiler.compile(d);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  }
}

TEST_CASE("Spec helpers and derivation", "[SPEC-005]") {
  SpecCompiler compiler;
  const json base_doc = base_valid_spec();
  const auto res = compiler.compile(base_doc);
  REQUIRE(res.has_value());

  CHECK(res->to_json() == base_doc);

  const auto other_essentials = default_essential_fields("other.unsupported_family");
  CHECK(other_essentials.empty());

  // Derive revision with invalid modification that fails compilation
  const json bad_mod = json{{"family", 12345}};
  const auto fail_rev = res->derive_new_revision(bad_mod);
  REQUIRE(!fail_rev.has_value());

  // Derive revision with provenance override tracking
  json override_mod = json::object();
  override_mod["envelope"] =
      json{{"width", json{{"value", 140.0}, {"unit", "mm"}, {"provenance", "user"}}}};
  const auto ok_rev = res->derive_new_revision(override_mod);
  REQUIRE(ok_rev.has_value());
  CHECK(!ok_rev->conflicts().empty());
}

TEST_CASE("SPEC-005: lower rank patch loses and records conflict", "[SPEC-005]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  // Base has nominal_size with provenance user (120 mm)
  doc["product"]["nominal_size"] = json{{"value", 120.0}, {"unit", "mm"}, {"provenance", "user"}};
  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());

  // Patch with default provenance (100 mm)
  json patch = json::object();
  patch["product"] =
      json{{"nominal_size", {{"value", 100.0}, {"unit", "mm"}, {"provenance", "default"}}}};

  auto rev2 = res->derive_new_revision(patch);
  REQUIRE(rev2.has_value());
  REQUIRE(rev2->field("product.nominal_size").has_value());
  const Field sz = field_or_empty(*rev2, "product.nominal_size");
  REQUIRE(sz.si_value.has_value());
  CHECK_THAT(sz.si_value.value_or(k_nan), WithinRel(0.12, 1e-12));
  CHECK(sz.original_value == 120.0);
  CHECK(sz.provenance == cemkit::core::Provenance::user);

  REQUIRE(rev2->conflicts().size() == 1);
  CHECK(rev2->conflicts()[0].field == "product.nominal_size");
  CHECK(rev2->conflicts()[0].winning == cemkit::core::Provenance::user);
  CHECK(rev2->conflicts()[0].overridden == cemkit::core::Provenance::default_value);
}

TEST_CASE("SPEC-006: compiler calls injected essential resolver", "[SPEC-006]") {
  bool resolver_called = false;
  CompilerOptions opts;
  opts.essential_resolver = [&](std::string_view family) -> std::vector<std::string> {
    resolver_called = true;
    CHECK(family == "fans.axial_ducted");
    return {"envelope.width"};
  };
  SpecCompiler compiler(opts);
  json doc = base_valid_spec();
  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  CHECK(resolver_called);
}

TEST_CASE(
    "SPEC-007: autonomous mode with injected default resolver applies default value with "
    "provisional",
    "[SPEC-007]") {
  CompilerOptions opts;
  opts.autonomous_mode = true;
  opts.default_resolver = [](std::string_view /*family*/,
                             std::string_view path) -> std::optional<Field> {
    if (path == "product.duty.flow") {
      Field f;
      f.path = std::string(path);
      f.original_value = 0.04;
      f.original_unit = "m3/s";
      f.si_value = 0.04;
      f.si_unit = "m3/s";
      f.note = "test fixture source: injected by this test, not engineering data";
      f.tolerance = Tolerance{.type = Tolerance::Type::relative,
                              .minus = 0.05,
                              .plus = 0.05,
                              .original_minus = 0.05,
                              .original_plus = 0.05,
                              .unit = ""};
      return f;
    }
    return std::nullopt;
  };
  SpecCompiler compiler(opts);

  json doc = base_valid_spec();
  doc["product"]["duty"]["flow"] =
      json{{"value", nullptr}, {"unit", "m3/s"}, {"provenance", "unknown"}};
  // pressure has no default in our resolver, so it will remain unknown question
  doc["product"]["duty"]["pressure"] =
      json{{"value", nullptr}, {"unit", "Pa"}, {"provenance", "unknown"}};

  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  REQUIRE(res->field("product.duty.flow").has_value());
  const Field flow_f = field_or_empty(*res, "product.duty.flow");
  REQUIRE(flow_f.si_value.has_value());
  CHECK(flow_f.si_value.value_or(k_nan) == 0.04);
  CHECK(flow_f.provenance == cemkit::core::Provenance::default_value);
  CHECK(flow_f.provisional);

  // Pressure had no default, so questions exists and unresolved flag is true
  CHECK(res->has_unresolved_essential_unknowns());
  REQUIRE(res->questions().size() == 1);
  CHECK(res->questions()[0].field == "product.duty.pressure");
}

TEST_CASE(
    "SPEC-007: autonomous mode without defaults when product.duty is deleted generates questions "
    "and keeps unresolved true",
    "[SPEC-007]") {
  CompilerOptions opts;
  opts.autonomous_mode = true;
  // default_resolver is empty (no defaults)
  SpecCompiler compiler(opts);

  json doc = base_valid_spec();
  doc["product"].erase("duty");

  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  CHECK(res->has_unresolved_essential_unknowns());
  REQUIRE(res->questions().size() == 2);
  CHECK(res->questions()[0].field == "product.duty.flow");
  CHECK(res->questions()[1].field == "product.duty.pressure");
}

TEST_CASE("SPEC-008: Question::unit has coherent SI unit and text essential field is known",
          "[SPEC-008]") {
  CompilerOptions opts;
  opts.essential_resolver = [](std::string_view) -> std::vector<std::string> {
    return {"product.duty.flow", "product.duty.pressure", "manufacturing.process"};
  };
  SpecCompiler compiler(opts);

  json doc = base_valid_spec();
  doc["product"]["duty"]["flow"] =
      json{{"value", nullptr}, {"unit", "m3/s"}, {"provenance", "unknown"}};
  doc["product"]["duty"]["pressure"] =
      json{{"value", nullptr}, {"unit", "Pa"}, {"provenance", "unknown"}};
  // manufacturing.process is text field ("FDM") in base_valid_spec()

  auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  // manufacturing.process has text_value and is known, so only flow and pressure produce questions
  REQUIRE(res->questions().size() == 2);
  CHECK(res->questions()[0].field == "product.duty.flow");
  CHECK(res->questions()[0].unit == "m3/s");
  CHECK(res->questions()[1].field == "product.duty.pressure");
  CHECK(res->questions()[1].unit == "Pa");
}

TEST_CASE("SPEC-009: derive_new_revision leaves base spec completely unchanged", "[SPEC-009]") {
  SpecCompiler compiler;
  auto base_res = compiler.compile(base_valid_spec());
  REQUIRE(base_res.has_value());

  const auto orig_rev = base_res->revision();
  const auto orig_parent = base_res->parent();
  const auto orig_doc = base_res->raw_document();

  json patch = json::object();
  patch["title"] = "New Revision Title";
  auto rev2 = base_res->derive_new_revision(patch);
  REQUIRE(rev2.has_value());

  CHECK(base_res->revision() == orig_rev);
  CHECK(base_res->parent() == orig_parent);
  CHECK(base_res->raw_document() == orig_doc);
  CHECK(base_res->title() == "120 mm ducted axial fan");
}

TEST_CASE("exceptions do not escape from derive_new_revision or empty essential_resolver",
          "[SPEC-001]") {
  // derive_new_revision with non-object json
  SpecCompiler compiler;
  auto res = compiler.compile(base_valid_spec());
  REQUIRE(res.has_value());

  auto fail_arr = res->derive_new_revision(json::array({1, 2, 3}));
  REQUIRE(!fail_arr.has_value());
  CHECK(fail_arr.error().code() == cemkit::core::ErrorCode::invalid_input);
  CHECK(fail_arr.error().subject() == "modifications");

  // empty essential_resolver
  CompilerOptions bad_opts;
  bad_opts.essential_resolver = nullptr;
  SpecCompiler bad_compiler(bad_opts);
  auto empty_res = bad_compiler.compile(base_valid_spec());
  REQUIRE(!empty_res.has_value());
  CHECK(empty_res.error().code() == cemkit::core::ErrorCode::invalid_input);
  CHECK(empty_res.error().subject() == "essential_resolver");
}

TEST_CASE("derive_new_revision preserves CompilerOptions", "[SPEC-009]") {
  CompilerOptions opts;
  opts.autonomous_mode = true;
  opts.essential_resolver = [](std::string_view) -> std::vector<std::string> {
    return {"envelope.width"};
  };
  SpecCompiler compiler(opts);
  auto res = compiler.compile(base_valid_spec());
  REQUIRE(res.has_value());
  CHECK(res->compiler_options().autonomous_mode == true);

  json patch = json::object();
  patch["title"] = "Rev 2";
  auto rev2 = res->derive_new_revision(patch);
  REQUIRE(rev2.has_value());
  CHECK(rev2->compiler_options().autonomous_mode == true);
}

TEST_CASE("revision number and parent validation", "[SPEC-001]") {
  SpecCompiler compiler;

  // revision 0
  {
    json doc = base_valid_spec();
    doc["revision"] = 0;
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(res.error().subject() == "revision");
  }

  // revision -1
  {
    json doc = base_valid_spec();
    doc["revision"] = -1;
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(res.error().subject() == "revision");
  }

  // revision UINT32_MAX
  {
    json doc = base_valid_spec();
    doc["revision"] = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(res.error().subject() == "revision");
  }

  // malformed parent: non-object
  {
    json doc = base_valid_spec();
    doc["revision"] = 2;
    doc["parent"] = "parent_string";
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(res.error().subject() == "parent");
  }

  // malformed parent: parent.revision >= revision
  {
    json doc = base_valid_spec();
    doc["revision"] = 2;
    doc["parent"] = json{{"spec_id", "axial-120"}, {"revision", 2}};
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(res.error().subject() == "parent");
  }

  // derive_new_revision overflow check
  {
    json doc = base_valid_spec();
    doc["revision"] = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max() - 1);
    auto res = compiler.compile(doc);
    REQUIRE(res.has_value());
    json patch = json::object();
    patch["title"] = "Overflow";
    auto overflow_res = res->derive_new_revision(patch);
    REQUIRE(!overflow_res.has_value());
    CHECK(overflow_res.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(overflow_res.error().subject() == "revision");
  }
}

TEST_CASE("offset units and value range checks", "[SPEC-003]") {
  SpecCompiler compiler;

  // relative tolerance on degC is rejected
  {
    json doc = base_valid_spec();
    doc["air"]["temperature"]["unit"] = "degC";
    doc["air"]["temperature"]["value"] = 25.0;
    doc["air"]["temperature"]["tolerance"] = json{{"relative", 0.10}};
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "air.temperature");
  }

  // Temperature < 0 K
  {
    json doc = base_valid_spec();
    doc["air"]["temperature"]["unit"] = "degC";
    doc["air"]["temperature"]["value"] = -300.0;  // below absolute zero
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "air.temperature");
  }

  // Non-positive flow
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["flow"]["value"] = 0.0;
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.flow");
  }

  // Non-positive density
  {
    json doc = base_valid_spec();
    doc["air"]["density"]["value"] = -1.0;
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "air.density");
  }

  // Non-positive dynamic viscosity
  {
    json doc = base_valid_spec();
    doc["air"]["dynamic_viscosity"] =
        json{{"value", 0.0}, {"unit", "Pa*s"}, {"provenance", "default"}};
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "air.dynamic_viscosity");
  }
}

TEST_CASE("unknown fields and misspelt paths are rejected naming the field", "[SPEC-001]") {
  SpecCompiler compiler;

  // Bare top-level power_limit: 20
  {
    json doc = base_valid_spec();
    doc["power_limit"] = 20;
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "power_limit");
  }

  // Misspelt path product.duty.flw
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["flw"] = json{{"value", 0.05},
                                         {"unit", "m3/s"},
                                         {"provenance", "user"},
                                         {"tolerance", {{"relative", 0.05}}}};
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "product.duty.flw");
  }

  // Unknown top-level field
  {
    json doc = base_valid_spec();
    doc["unknown_section"] = json{{"key", "value"}};
    auto res = compiler.compile(doc);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
    CHECK(res.error().subject() == "unknown_section");
  }
}

TEST_CASE("SPEC-010: contradiction check examines all stated power limits", "[SPEC-010]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  // Q = 0.05 m3/s, p = 1500 Pa -> P_air = 75 W
  doc["product"]["duty"]["pressure"]["value"] = 1500.0;
  // product.power_limit is generous (1000 W)
  doc["product"]["power_limit"] = json{{"value", 1000.0}, {"unit", "W"}, {"provenance", "user"}};
  // product.motor.power_limit is tight (20 W)
  doc["product"]["motor"]["power_limit"] =
      json{{"value", 20.0}, {"unit", "W"}, {"provenance", "user"}};

  auto res = compiler.compile(doc);
  REQUIRE(!res.has_value());
  CHECK(res.error().code() == cemkit::core::ErrorCode::infeasible_requirement);
  CHECK(res.error().subject() == "product.motor.power_limit");
}

TEST_CASE("SPEC-007: an uncited default from the resolver is not applied", "[SPEC-007]") {
  CompilerOptions opts;
  opts.autonomous_mode = true;
  opts.default_resolver = [](std::string_view, std::string_view path) -> std::optional<Field> {
    Field f;
    f.path = std::string(path);
    f.si_value = 0.04;
    f.si_unit = "m3/s";  // no note: no citation
    return f;
  };
  SpecCompiler compiler(opts);
  json doc = base_valid_spec();
  doc["product"].erase("duty");
  const auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  CHECK(!res->field("product.duty.flow").has_value());
  CHECK(res->has_unresolved_essential_unknowns());
  CHECK(res->questions().size() == 2);
}

TEST_CASE("SPEC-008: questions carry a specific reason per essential field", "[SPEC-008]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  doc["product"].erase("duty");
  const auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  REQUIRE(res->questions().size() == 2);
  CHECK(res->questions()[0].reason.find("flow") != std::string::npos);
  CHECK(res->questions()[0].reason.find("AX-003") != std::string::npos);
  CHECK(res->questions()[1].reason.find("pressure") != std::string::npos);
  CHECK(res->questions()[1].reason.find("AX-004") != std::string::npos);
}

TEST_CASE("derive_new_revision keeps the injected essential resolver", "[SPEC-009]") {
  CompilerOptions opts;
  opts.essential_resolver = [](std::string_view) -> std::vector<std::string> {
    return {"product.motor.bore_diameter"};
  };
  SpecCompiler compiler(opts);
  const auto res = compiler.compile(base_valid_spec());
  REQUIRE(res.has_value());
  REQUIRE(res->questions().size() == 1);

  json patch = json::object();
  patch["title"] = "Rev 2";
  const auto rev2 = res->derive_new_revision(patch);
  REQUIRE(rev2.has_value());
  // A default-constructed compiler would ask about duty flow and pressure instead.
  REQUIRE(rev2->questions().size() == 1);
  CHECK(rev2->questions()[0].field == "product.motor.bore_diameter");
  CHECK(rev2->questions()[0].unit == "m");
}

TEST_CASE("a text field carrying a unit is rejected naming the field", "[SPEC-001]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  doc["manufacturing"]["process"]["unit"] = "mm";
  const auto res = compiler.compile(doc);
  REQUIRE(!res.has_value());
  CHECK(res.error().code() == cemkit::core::ErrorCode::spec_rejected);
  CHECK(res.error().subject() == "manufacturing.process");
}

TEST_CASE("invalid UTF-8 in a text array does not throw", "[SPEC-001]") {
  SpecCompiler compiler;
  json doc = base_valid_spec();
  doc["product"]["scope"]["value"] = json::array({std::string("rotor\xff")});
  const auto res = compiler.compile(doc);
  REQUIRE(res.has_value());
  CHECK(res->field("product.scope").has_value());
}

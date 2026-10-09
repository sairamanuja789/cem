#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include "cemkit/core/error.hpp"
#include "cemkit/spec/compiler.hpp"

using Catch::Matchers::WithinRel;
using json = nlohmann::json;
using namespace cemkit::spec;

namespace {

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

  const auto width = res->field("envelope.width");
  REQUIRE(width.has_value());
  if (width && width->si_value) {
    CHECK(width->original_value == 120.0);
    CHECK(width->original_unit == "mm");
    CHECK_THAT(*width->si_value, WithinRel(0.12, 1e-12));
    CHECK(width->si_unit == "m");
  }

  const auto rpm = res->field("product.rotational_speed");
  REQUIRE(rpm.has_value());
  if (rpm && rpm->si_value) {
    CHECK(rpm->original_value == 2000.0);
    CHECK(rpm->original_unit == "rpm");
    CHECK_THAT(*rpm->si_value, WithinRel(209.43951023931953, 1e-12));
    CHECK(rpm->si_unit == "rad/s");
  }
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
  }

  // fan_total accepted
  {
    json doc = base_valid_spec();
    doc["product"]["duty"]["pressure"]["kind"] = "fan_total";
    const auto res = compiler.compile(doc);
    REQUIRE(res.has_value());
    const auto p = res->field("product.duty.pressure");
    REQUIRE(p.has_value());
    if (p) {
      CHECK(p->pressure_kind == "fan_total");
    }
  }
}

TEST_CASE("precedence rule: user > image > derived > default", "[SPEC-005]") {
  // Check provenance precedence ranking
  CHECK(cemkit::core::Provenance::user < cemkit::core::Provenance::default_value);  // enum values
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
  if (derived) {
    const auto env_w = derived->field("envelope.width");
    REQUIRE(env_w.has_value());
    if (env_w && env_w->si_value) {
      CHECK(env_w->provenance == cemkit::core::Provenance::user);
      CHECK(*env_w->si_value == 0.14);
    }
    CHECK(!derived->conflicts().empty());
  }
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

  // Autonomous mode resolves unknowns using provisional defaults if available
  SpecCompiler auto_compiler(CompilerOptions{.autonomous_mode = true});
  auto res2 = auto_compiler.compile(doc);
  REQUIRE(res2.has_value());
  if (res2) {
    const auto flow_f = res2->field("product.duty.flow");
    const auto press_f = res2->field("product.duty.pressure");
    REQUIRE(flow_f.has_value());
    REQUIRE(press_f.has_value());
    if (flow_f && press_f) {
      CHECK(flow_f->provisional);
      CHECK(press_f->provisional);
    }
  }
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
  if (rev2) {
    CHECK(rev2->revision() == 2);
    REQUIRE(rev2->parent().has_value());
    if (rev2->parent()) {
      CHECK(rev2->parent()->spec_id == res->spec_id());
      CHECK(rev2->parent()->revision == 1);
    }
    CHECK(rev2->title() == "Updated axial fan title");
  }
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

TEST_CASE("pressure kind validation error paths", "[SPEC-007]") {
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
  if (ok_rev) {
    CHECK(!ok_rev->conflicts().empty());
  }
}

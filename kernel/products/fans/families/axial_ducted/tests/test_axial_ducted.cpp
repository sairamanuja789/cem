#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

#include "cemkit/product/registry.hpp"
#include "cemkit/spec/compiler.hpp"
#include "products/fans/families/axial_ducted/axial_ducted.hpp"

namespace axial = cemkit::fans::axial_ducted;
namespace fans = cemkit::fans;
namespace core = cemkit::core;
namespace si = mp_units::si;
namespace isq = mp_units::isq;

namespace {

// The examples/axial_120.yaml sizing fields: AX-001 (120 mm duct) and AX-009 (0.5 mm clearance).
nlohmann::json sized_spec() {
  using nlohmann::json;
  return json{
      {"schema_version", "1.0.0"},
      {"spec_id", "axial-d6"},
      {"revision", 1},
      {"family", "fans.axial_ducted"},
      {"product",
       {{"nominal_size", {{"value", 120}, {"unit", "mm"}, {"provenance", "user"}}},
        {"size_reference",
         {{"value", "duct_inner_diameter"}, {"provenance", "default"}, {"provisional", true}}},
        {"tip_clearance_min",
         {{"value", 0.5}, {"unit", "mm"}, {"provenance", "default"}, {"provisional", true}}}}}};
}

cemkit::spec::Spec compile(const nlohmann::json& doc) {
  auto registry = std::make_shared<cemkit::product::Registry>();
  REQUIRE(axial::register_family(*registry).has_value());
  const cemkit::spec::SpecCompiler compiler{cemkit::spec::CompilerOptions{
      .autonomous_mode = false,
      .essential_resolver = cemkit::product::essential_field_resolver(registry),
      .default_resolver = {}}};
  auto spec = compiler.compile(doc);
  REQUIRE(spec.has_value());
  return *spec;
}

fans::Duty l0_001_duty() {
  return fans::Duty{
      .flow = 0.05 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 150.0 * fans::fan_total_pressure[si::pascal]};
}

const auto k_omega = 209.43951023931953 * isq::angular_velocity[si::radian / si::second];

// The derived tip diameter, after REQUIREs that it exists. clang-tidy cannot see through REQUIRE,
// so the value is read with value_or: a missing value becomes NaN with no rule and fails every
// CHECK on it.
axial::DerivedTipDiameter require_tip(
    const core::Result<std::optional<axial::DerivedTipDiameter>>& tip) {
  REQUIRE(tip.has_value());
  REQUIRE(tip->has_value());
  return tip.value_or(std::nullopt)
      .value_or(axial::DerivedTipDiameter{
          .value = core::Length{std::numeric_limits<double>::quiet_NaN() * isq::length[si::metre]},
          .provenance = core::Provenance::unknown,
          .provisional = false,
          .rule = {},
          .from = {}});
}

}  // namespace

TEST_CASE("fans.axial_ducted registers and declares the ADR-003 D8 essential fields",
          "[FAM-001][SPEC-006]") {
  cemkit::product::Registry registry;
  REQUIRE(axial::register_family(registry).has_value());
  REQUIRE(registry.contains("fans.axial_ducted"));
  const auto fields = registry.essential_fields("fans.axial_ducted");
  REQUIRE(fields.has_value());
  CHECK(*fields == std::vector<std::string>{"product.duty.flow", "product.duty.pressure"});

  const auto family = registry.get("fans.axial_ducted");
  REQUIRE(family.has_value());
  const auto& f = family->get();
  CHECK(core::to_string(f.plugin()) == "fans.axial_ducted@0.1.0");
  CHECK(f.parameter_space().size() == 0);    // nothing invented
  CHECK(f.feasible_range().limits.empty());  // no cited range (HI-003)
}

TEST_CASE("the spec compiler asks fans.axial_ducted for its essential fields", "[SPEC-006]") {
  auto registry = std::make_shared<cemkit::product::Registry>();
  REQUIRE(axial::register_family(*registry).has_value());
  const cemkit::spec::SpecCompiler compiler{cemkit::spec::CompilerOptions{
      .autonomous_mode = false,
      .essential_resolver = cemkit::product::essential_field_resolver(registry),
      .default_resolver = {}}};
  const nlohmann::json doc{{"schema_version", "1.0.0"},
                           {"spec_id", "axial"},
                           {"revision", 1},
                           {"family", "fans.axial_ducted"}};
  const auto spec = compiler.compile(doc);
  REQUIRE(spec.has_value());
  REQUIRE(spec->questions().size() == 2);
  CHECK(spec->questions()[0].field == "product.duty.flow");
  CHECK(spec->questions()[1].field == "product.duty.pressure");
}

TEST_CASE("the family's feasibility hook reports range unsourced", "[SEL-002][SEL-004]") {
  CHECK(!axial::specific_speed_range().sourced());
  const fans::Duty duty{
      .flow = 0.05 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 150.0 * fans::fan_total_pressure[si::pascal]};
  const auto report =
      axial::check_feasibility(duty, cemkit::physics::default_air(),
                               209.43951023931953 * isq::angular_velocity[si::radian / si::second],
                               0.119 * isq::length[si::metre]);
  REQUIRE(report.has_value());
  CHECK(report->verdict() == fans::Verdict::unconfirmed);
  REQUIRE(report->checks.size() == 3);
  CHECK(report->checks[2].status == fans::CheckStatus::range_unsourced);
}

TEST_CASE("every other family operation is not implemented, never a number", "[FAM-001]") {
  const auto family = axial::make_family();
  REQUIRE(family.has_value());
  const auto& f = **family;
  const cemkit::spec::Spec spec;
  const cemkit::product::Design design;
  const cemkit::product::Evaluation evaluation;
  const auto expect = [](const auto& result) {
    REQUIRE(!result.has_value());
    CHECK(result.error().code() == core::ErrorCode::not_implemented);
    CHECK(result.error().subject() == "fans.axial_ducted");
  };
  expect(f.initial_design(spec));
  expect(f.evaluate_l1(spec, design));
  expect(f.check_constraints(spec, design, evaluation));
  expect(f.geometry_recipe(design));
  expect(f.simulation_case(spec, design));
}

TEST_CASE("D6: the rotor tip diameter is the duct diameter minus twice the spec's tip clearance",
          "[AX-001][AX-009][SEL-004]") {
  const auto d = require_tip(axial::rotor_tip_diameter(compile(sized_spec())));
  // 0.120 m - 2 x 0.0005 m = 0.119 m, the d_tip of the verified hand calculation l0_001.
  CHECK_THAT(d.value.numerical_value_in(si::metre), Catch::Matchers::WithinRel(0.119, 1e-12));
  CHECK(d.provenance == core::Provenance::default_value);
  CHECK(d.provisional);
  CHECK(d.rule == axial::k_tip_from_duct_rule);
  CHECK(d.from == std::vector<std::string>{"product.nominal_size", "product.size_reference",
                                           "product.tip_clearance_min"});
}

TEST_CASE("D6: feasibility computes the tip-speed check from the spec-derived tip diameter",
          "[AX-001][AX-009][SEL-004]") {
  const auto tip = require_tip(axial::rotor_tip_diameter(compile(sized_spec())));
  const auto report =
      axial::check_feasibility(l0_001_duty(), cemkit::physics::default_air(), k_omega, tip.value);
  REQUIRE(report.has_value());
  REQUIRE(report->checks.size() == 3);
  CHECK(report->checks[1].limit == "incompressible_tip_speed");
  CHECK(report->checks[1].status == fans::CheckStatus::pass);  // l0_001: U_tip = 12.46 m/s
  CHECK(report->verdict() == fans::Verdict::unconfirmed);      // the range is still UNSOURCED
}

TEST_CASE("D6: without a stated clearance or duct size reference the tip check is not computable",
          "[AX-001][AX-009][SEL-004]") {
  auto no_clearance = sized_spec();
  no_clearance["product"].erase("tip_clearance_min");
  auto unknown_clearance = sized_spec();
  unknown_clearance["product"]["tip_clearance_min"] = {
      {"value", nullptr}, {"unit", "mm"}, {"provenance", "unknown"}};
  auto frame = sized_spec();
  frame["product"]["size_reference"]["value"] = "frame";  // owner to confirm (AX-001 open item)
  auto no_reference = sized_spec();
  no_reference["product"].erase("size_reference");
  auto no_size = sized_spec();
  no_size["product"].erase("nominal_size");

  for (const auto& doc : {no_clearance, unknown_clearance, frame, no_reference, no_size}) {
    const auto tip = axial::rotor_tip_diameter(compile(doc));
    REQUIRE(tip.has_value());
    CHECK(!tip->has_value());
    const auto report =
        axial::check_feasibility(l0_001_duty(), cemkit::physics::default_air(), k_omega,
                                 tip->has_value() ? std::optional{(*tip)->value} : std::nullopt);
    REQUIRE(report.has_value());
    CHECK(report->checks[1].status == fans::CheckStatus::not_computable);
  }
}

TEST_CASE("D6: a clearance of half the duct diameter or more is rejected, never a number",
          "[AX-001][AX-009]") {
  for (const double clearance_mm : {60.0, 75.0, -0.5}) {
    auto doc = sized_spec();
    doc["product"]["tip_clearance_min"]["value"] = clearance_mm;
    const auto tip = axial::rotor_tip_diameter(compile(doc));
    REQUIRE(!tip.has_value());
    CHECK(tip.error().code() == core::ErrorCode::spec_rejected);
    CHECK(tip.error().subject() == "product.tip_clearance_min");
  }
}

TEST_CASE("a stated rotor tip diameter is used as given, without a clearance",
          "[AX-001][SEL-004]") {
  auto doc = sized_spec();
  doc["product"]["size_reference"] = {{"value", "rotor_tip_diameter"}, {"provenance", "user"}};
  doc["product"].erase("tip_clearance_min");
  const auto tip = require_tip(axial::rotor_tip_diameter(compile(doc)));
  CHECK_THAT(tip.value.numerical_value_in(si::metre), Catch::Matchers::WithinRel(0.12, 1e-12));
  CHECK(tip.provenance == core::Provenance::user);
  CHECK(!tip.provisional);
  CHECK(tip.rule == axial::k_tip_stated_rule);
}

TEST_CASE("D6: a non-positive nominal size is rejected on its own field", "[AX-001]") {
  for (const double size_mm : {0.0, -120.0}) {
    auto doc = sized_spec();
    doc["product"]["nominal_size"]["value"] = size_mm;
    const auto tip = axial::rotor_tip_diameter(compile(doc));
    REQUIRE(!tip.has_value());
    CHECK(tip.error().code() == core::ErrorCode::spec_rejected);
    CHECK(tip.error().subject() == "product.nominal_size");
  }
}

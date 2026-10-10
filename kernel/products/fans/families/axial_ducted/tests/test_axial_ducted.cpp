#include "products/fans/families/axial_ducted/axial_ducted.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nlohmann/json.hpp>

#include "cemkit/product/registry.hpp"
#include "cemkit/spec/compiler.hpp"

namespace axial = cemkit::fans::axial_ducted;
namespace fans = cemkit::fans;
namespace core = cemkit::core;
namespace si = mp_units::si;
namespace isq = mp_units::isq;

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
  CHECK(f.parameter_space().size() == 0);   // nothing invented
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
  const auto report = axial::check_feasibility(
      duty, cemkit::physics::default_air(),
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

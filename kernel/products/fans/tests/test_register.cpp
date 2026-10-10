// SEL-004, REL-002 (owner decision D4): the fan product's family feasibility hook dispatch.
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "cemkit/product/registry.hpp"
#include "products/fans/register.hpp"

namespace core = cemkit::core;
namespace fans = cemkit::fans;
namespace si = mp_units::si;

TEST_CASE("a family without a feasibility hook is not_implemented and names the capability",
          "[SEL-004][REL-002]") {
  const fans::Duty duty{
      .flow = 0.05 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 150.0 * fans::fan_total_pressure[si::pascal]};
  const auto report =
      fans::check_family_feasibility("fans.no_hook", duty, cemkit::physics::default_air(), {}, {});
  REQUIRE(!report.has_value());
  CHECK(report.error().code() == core::ErrorCode::not_implemented);
  CHECK(report.error().subject() == "family");
  const auto& details = report.error().details();
  REQUIRE(details.contains("capability"));
  CHECK(details.at("capability") == "fans.no_hook.feasibility");
}

TEST_CASE("fans.axial_ducted has a feasibility hook", "[SEL-004]") {
  const fans::Duty duty{
      .flow = 0.05 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 150.0 * fans::fan_total_pressure[si::pascal]};
  CHECK(fans::check_family_feasibility("fans.axial_ducted", duty, cemkit::physics::default_air(),
                                       {}, {})
            .has_value());
}

TEST_CASE("every fan family's pressure fields require the ISO 5801 pressure kind",
          "[SPEC-004][FAM-001]") {
  // ADR-015 moved SPEC-004 into each family's catalogue; this guards against a fan family that
  // declares a pressure field without it (physics review of M1).
  cemkit::product::Registry registry;
  REQUIRE(fans::register_fan_families(registry).has_value());
  REQUIRE(registry.size() > 0);
  for (const auto& id : registry.ids()) {
    INFO("family " << id);
    const auto family = registry.get(id);
    REQUIRE(family.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIRE above
    for (const auto& field : family->get().field_catalogue().fields) {
      if (field.kind != cemkit::spec::QuantityKind::pressure) {
        continue;
      }
      INFO("field " << field.path);
      REQUIRE(field.qualifier.has_value());
      const auto rule = field.qualifier.value_or(cemkit::spec::QualifierRule{});
      CHECK(rule.member == "kind");
      CHECK(rule.report_as == "pressure_kind");
      CHECK(rule.accepted == std::vector<std::string>{"fan_total", "fan_static"});
    }
  }
}

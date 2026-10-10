// SEL-004, REL-002 (owner decision D4): the fan product's family feasibility hook dispatch.
#include "products/fans/register.hpp"

#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>

namespace core = cemkit::core;
namespace fans = cemkit::fans;
namespace si = mp_units::si;

TEST_CASE("a family without a feasibility hook is not_implemented and names the capability",
          "[SEL-004][REL-002]") {
  const fans::Duty duty{
      .flow = 0.05 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 150.0 * fans::fan_total_pressure[si::pascal]};
  const auto report = fans::check_family_feasibility("fans.no_hook", duty,
                                                     cemkit::physics::default_air(), {}, {});
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

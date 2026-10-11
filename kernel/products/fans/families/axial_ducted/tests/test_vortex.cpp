// fans.axial.vortex@0.1.0 (PHY-001, PHY-005, PHY-007): the kernel port on the l1_003 hand
// calculation (tests/hand_calcs/proposed/l1_003_vortex_exponent.yaml): n = -1 reproduces the free
// vortex, n = 0 at a 28 mm hub, and the refusals. TRIAL inputs.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>

#include "products/fans/families/axial_ducted/free_vortex.hpp"
#include "products/fans/families/axial_ducted/vortex.hpp"

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using Catch::Matchers::WithinRel;
using cemkit::core::ErrorCode;
using cemkit::fans::axial_ducted::FreeVortexInputs;

namespace {
cemkit::core::Length len(double v) { return v * isq::length[si::metre]; }

FreeVortexInputs trial(double r_hub) {
  return FreeVortexInputs{
      .flow =
          0.028 * cemkit::core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 21.6 * cemkit::fans::fan_total_pressure[si::pascal],
      .omega = 209.43951023931953 * isq::angular_velocity[si::radian / si::second],
      .r_tip = len(0.0595),
      .r_hub = len(r_hub),
      .chord_hub = len(0.020),
      .chord_tip = len(0.016),
      .thickness = len(0.0012),
      .stations = 3,
      .embed_depth = len(0.003)};
}

double rad(cemkit::core::Angle a) { return a.numerical_value_in(si::radian); }
double mps(cemkit::core::Speed v) { return v.numerical_value_in(si::metre / si::second); }
}  // namespace

TEST_CASE("n = -1 reproduces the free vortex", "[PHY-005]") {
  const auto air = cemkit::physics::default_air();
  const auto general = cemkit::fans::axial_ducted::vortex_design(trial(0.020), -1.0, air);
  const auto free = cemkit::fans::axial_ducted::free_vortex_design(trial(0.020), air);
  REQUIRE(general.has_value());
  REQUIRE(free.has_value());
  CHECK(cemkit::core::to_string(general->model) == "fans.axial.vortex@0.1.0");
  CHECK_THAT(general->swirl_constant, WithinRel(0.08740034163012558, 1e-12));
  for (std::size_t i = 0; i < free->stations.size(); ++i) {
    CHECK_THAT(rad(general->stations[i].camber_angle),
               WithinRel(rad(free->stations[i].camber_angle), 1e-10));
    CHECK_THAT(mps(general->stations[i].outlet_axial_velocity), WithinRel(2.83820613427152, 1e-12));
  }
}

TEST_CASE("n = 0 at a 28 mm hub matches l1_003 case B", "[PHY-005]") {
  const auto d =
      cemkit::fans::axial_ducted::vortex_design(trial(0.028), 0.0, cemkit::physics::default_air());
  REQUIRE(d.has_value());
  CHECK_THAT(d->swirl_constant, WithinRel(1.816663947148875, 1e-5));
  const auto& hub = d->stations[1];
  CHECK_THAT(mps(hub.outlet_axial_velocity), WithinRel(0.9712083313850651, 1e-5));
  CHECK_THAT(rad(hub.camber_angle), WithinRel(-0.2684311888289601, 1e-5));  // negative camber
  CHECK_THAT(hub.reaction.numerical_value_in(mp_units::one), WithinRel(1.2915862150994055, 1e-5));
  CHECK_THAT(mps(d->stations[3].outlet_axial_velocity), WithinRel(4.46522831720305, 1e-5));
}

TEST_CASE("an exponent outside [-1, 1] or a duty needing reverse flow is refused", "[PHY-007]") {
  for (const double n : {-1.5, 1.5, std::nan("")}) {
    const auto d =
        cemkit::fans::axial_ducted::vortex_design(trial(0.028), n, cemkit::physics::default_air());
    REQUIRE_FALSE(d.has_value());
    CHECK(d.error().code() == ErrorCode::out_of_validity);
    CHECK(d.error().subject() == "vortex_exponent");
  }
  const auto reverse =
      cemkit::fans::axial_ducted::vortex_design(trial(0.020), 0.0, cemkit::physics::default_air());
  REQUIRE_FALSE(reverse.has_value());
  CHECK(reverse.error().subject() == "vortex_exponent");
}

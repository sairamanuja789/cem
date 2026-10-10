// fans.axial.mean_line@0.1.0 (PHY-001, PHY-002): the kernel port on the l1_001 hand calculation
// (tests/hand_calcs/proposed/l1_001_mean_line_velocity_triangles.yaml) and validity. Inputs are
// illustrative, not requirements.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>

#include "products/fans/families/axial_ducted/mean_line.hpp"

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using Catch::Matchers::WithinRel;
using cemkit::core::ErrorCode;

namespace {
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();

auto run(double dp, double r_hub) {
  return cemkit::fans::axial_ducted::mean_line(
      0.05 * cemkit::core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      dp * cemkit::fans::fan_total_pressure[si::pascal],
      209.43951023931953 * isq::angular_velocity[si::radian / si::second],
      0.0595 * isq::length[si::metre], r_hub * isq::length[si::metre],
      cemkit::physics::default_air());
}
}  // namespace

TEST_CASE("l1_001: loss-free mean-line triangles at 150 Pa and 40 Pa", "[PHY-001][PHY-005]") {
  const auto a = run(150.0, 0.020);
  REQUIRE(a.has_value());
  const auto ms = si::metre / si::second;
  CHECK_THAT(a ? a->outlet_swirl.numerical_value_in(ms) : k_nan,
             WithinRel(13.674257287372987, 1e-12));
  CHECK_THAT(a ? a->relative_outlet_angle.numerical_value_in(si::radian) : k_nan,
             WithinRel(-0.712465255054917, 1e-12));
  CHECK_THAT(a ? a->work_coefficient.numerical_value_in(mp_units::one) : k_nan,
             WithinRel(1.4709511239086828, 1e-12));
  CHECK((a.has_value() && a->fidelity == cemkit::core::Fidelity::l1_predicted));
  const auto b = run(40.0, 0.020);
  REQUIRE(b.has_value());
  CHECK_THAT(b ? b->relative_outlet_angle.numerical_value_in(si::radian) : k_nan,
             WithinRel(0.8396004263421646, 1e-12));
  CHECK_THAT(b ? b->de_haller_ratio.numerical_value_in(mp_units::one) : k_nan,
             WithinRel(0.716836931197381, 1e-12));
}

TEST_CASE("outside validity the mean line is an error, never a number", "[PHY-002]") {
  const auto hub = run(40.0, 0.0595);
  REQUIRE(!hub.has_value());
  CHECK(hub.error().code() == ErrorCode::out_of_validity);
  CHECK(hub.error().subject() == "r_hub");
  const auto dp = run(5000.0, 0.020);
  REQUIRE(!dp.has_value());
  CHECK(dp.error().subject() == "fan_total_pressure");
}

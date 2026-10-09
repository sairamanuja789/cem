#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>

#include "cemkit/core/error.hpp"
#include "products/fans/common/l0.hpp"

// Hand calculations tests/hand_calcs/verified/l0_001..l0_009 and proposed/l0_010, in the kernel.
// The full agreement with the Python reference (all hand calculations plus 10 000 random states)
// is ctest fans_l0_crosscheck.

using Catch::Matchers::WithinRel;
using cemkit::core::describe;
using cemkit::core::ErrorCode;
using cemkit::core::Fidelity;
namespace fans = cemkit::fans;
namespace physics = cemkit::physics;
namespace core = cemkit::core;
namespace si = mp_units::si;
namespace isq = mp_units::isq;

namespace {

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_inf = std::numeric_limits<double>::infinity();
constexpr double k_omega = 209.43951023931953;  // 2000 rpm

core::AngularVelocity rad_s(double v) { return v * isq::angular_velocity[si::radian / si::second]; }
core::Length m(double v) { return v * isq::length[si::metre]; }
core::VolumeFlowRate m3_s(double v) {
  return v * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
}
fans::FanTotalPressure total(double v) { return v * fans::fan_total_pressure[si::pascal]; }
fans::FanStaticPressure fan_static(double v) { return v * fans::fan_static_pressure[si::pascal]; }
core::Power watt(double v) { return v * isq::power[si::watt]; }

const physics::Air k_air = physics::default_air();

template <class L>
double number(const L& labelled) {
  using Q = decltype(labelled.value());
  return labelled.value().numerical_value_in(Q::unit);
}

// The value of a labelled result, NaN when it is an error (so a CHECK on it fails).
template <class R>
double value_or_nan(const R& result) {
  return result.has_value() ? number(*result) : k_nan;
}

template <class R>
void check_l0(const R& result) {
  REQUIRE(result.has_value());
  CHECK(result->fidelity() == Fidelity::l0_predicted);
  CHECK(core::to_string(result->model()) == "fans.l0@1.0.0");
}

}  // namespace

TEST_CASE("l0_001: tip speed, flow and pressure coefficients", "[SEL-001][PHY-005][PHY-004]") {
  const auto u = fans::tip_speed(rad_s(k_omega), m(0.119));
  check_l0(u);
  CHECK_THAT(value_or_nan(u), WithinRel(12.461650859239512, 1e-9));

  const auto phi = fans::flow_coefficient(m3_s(0.05), m(0.119), rad_s(k_omega), k_air);
  check_l0(phi);
  CHECK_THAT(value_or_nan(phi), WithinRel(0.36075355610599763, 1e-9));

  const auto psi = fans::pressure_coefficient(total(150.0), m(0.119), rad_s(k_omega), k_air);
  check_l0(psi);
  CHECK_THAT(value_or_nan(psi), WithinRel(1.6371485533454382, 1e-9));
}

TEST_CASE("l0_002: specific speed and specific diameter", "[SEL-001][PHY-005]") {
  const auto ws = fans::specific_speed(rad_s(k_omega), m3_s(0.05), total(150.0), k_air);
  check_l0(ws);
  CHECK_THAT(value_or_nan(ws), WithinRel(1.2370483575363835, 1e-9));

  const auto ds = fans::specific_diameter(m(0.119), m3_s(0.05), total(150.0), k_air);
  check_l0(ds);
  CHECK_THAT(value_or_nan(ds), WithinRel(1.7869576997691965, 1e-9));
}

TEST_CASE("l0_003 and l0_004: fan laws", "[PHY-005]") {
  const fans::FanPoint point{.omega = rad_s(k_omega),
                             .d_tip = m(0.119),
                             .flow = m3_s(0.05),
                             .pressure = total(150.0),
                             .power = watt(15.0),
                             .air = k_air};
  const auto doubled = fans::scale_fan_laws(point, rad_s(2.0 * k_omega), m(0.119), k_air);
  REQUIRE(doubled.has_value());
  CHECK_THAT(number(doubled->flow), WithinRel(0.1, 1e-12));
  CHECK_THAT(number(doubled->pressure), WithinRel(600.0, 1e-12));
  CHECK_THAT(number(doubled->power), WithinRel(120.0, 1e-12));
  CHECK(doubled->power.fidelity() == Fidelity::l0_predicted);
  CHECK(doubled->pressure.model() == fans::l0_model());

  const auto larger = fans::scale_fan_laws(point, rad_s(k_omega), m(0.1785), k_air);
  REQUIRE(larger.has_value());
  CHECK_THAT(number(larger->flow), WithinRel(0.16875, 1e-12));
  CHECK_THAT(number(larger->pressure), WithinRel(337.5, 1e-12));
  CHECK_THAT(number(larger->power), WithinRel(113.90625, 1e-12));
}

TEST_CASE("l0_005 and l0_010: static to total, free delivery, typed dynamic pressure",
          "[SPEC-004][PHY-005]") {
  const auto converted = fans::fan_total_from_static(fan_static(120.0), m3_s(0.05), m(0.12), k_air);
  REQUIRE(converted.has_value());
  CHECK_THAT(number(converted->value), WithinRel(131.53153903336792, 1e-9));
  CHECK(converted->provenance == core::Provenance::derived);
  CHECK(converted->rule == fans::k_dynamic_pressure_rule);
  CHECK(converted->value.fidelity() == Fidelity::l0_predicted);

  const auto dynamic = fans::conventional_dynamic_pressure(m3_s(0.05), m(0.12), k_air);
  check_l0(dynamic);
  CHECK_THAT(value_or_nan(dynamic), WithinRel(11.53153903336792, 1e-9));

  // Free delivery, with 0 and -0 (accepted as value >= 0): the total is the dynamic pressure.
  for (const double zero : {0.0, -0.0}) {
    const auto free = fans::fan_total_from_static(fan_static(zero), m3_s(0.05), m(0.12), k_air);
    REQUIRE(free.has_value());
    CHECK(number(free->value) == value_or_nan(dynamic));
  }
}

TEST_CASE("l0_006 and l0_007: beyond the incompressible limits", "[PHY-003][PHY-005]") {
  const auto p = fans::specific_speed(rad_s(k_omega), m3_s(0.05), total(1500.0), k_air);
  REQUIRE(!p.has_value());
  CHECK(describe(p.error()) ==
        "out_of_validity at fan_total_pressure: fan total pressure exceeds the incompressible "
        "limit EPSILON gamma p1 (ADR-003 D3) (bounds=(0, 1418.55], model=fans.l0@1.0.0, "
        "value=1500)");

  const auto u = fans::flow_coefficient(m3_s(1.0), m(0.5), rad_s(200.0), k_air);
  REQUIRE(!u.has_value());
  CHECK(describe(u.error()) ==
        "out_of_validity at tip_speed: blade tip-speed Mach number exceeds the incompressible "
        "limit M^2/2 <= EPSILON (ADR-003 D3) (bounds=(0, 48.952798245486484], "
        "model=fans.l0@1.0.0, value=50)");

  const auto limit = fans::max_fan_total_pressure(k_air);
  check_l0(limit);
  CHECK(value_or_nan(limit) == 1418.55);
  const auto tip = fans::max_tip_speed(k_air);
  check_l0(tip);
  CHECK(value_or_nan(tip) == 48.952798245486484);
}

TEST_CASE("l0_009: non-positive input", "[PHY-003][PHY-005]") {
  const auto r = fans::specific_speed(rad_s(k_omega), m3_s(0.0), total(150.0), k_air);
  REQUIRE(!r.has_value());
  CHECK(r.error().code() == ErrorCode::out_of_validity);
  CHECK(r.error().subject() == "flow");
  CHECK(r.error().details() ==
        core::ErrorDetails{{"bounds", "(0, inf)"}, {"model", "fans.l0@1.0.0"}, {"value", "0"}});
}

TEST_CASE("every L0 function rejects non-positive or non-finite inputs", "[PHY-003]") {
  for (const double bad : {0.0, -1.0, k_nan, k_inf}) {
    CHECK(fans::tip_speed(rad_s(bad), m(0.1)).error().subject() == "omega");
    CHECK(fans::tip_speed(rad_s(100.0), m(bad)).error().subject() == "d_tip");
    CHECK(fans::flow_coefficient(m3_s(bad), m(0.1), rad_s(100.0), k_air).error().subject() ==
          "flow");
    CHECK(fans::pressure_coefficient(total(bad), m(0.1), rad_s(100.0), k_air).error().subject() ==
          "fan_total_pressure");
    CHECK(fans::specific_speed(rad_s(bad), m3_s(0.05), total(150.0), k_air).error().subject() ==
          "omega");
    CHECK(fans::specific_diameter(m(bad), m3_s(0.05), total(150.0), k_air).error().subject() ==
          "d_tip");
    CHECK(fans::conventional_dynamic_pressure(m3_s(0.05), m(bad), k_air).error().subject() ==
          "d_duct");
    CHECK(fans::fan_total_from_static(fan_static(100.0), m3_s(bad), m(0.12), k_air)
              .error()
              .subject() == "flow");

    auto air = k_air;
    air.density = bad * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)];
    CHECK(fans::specific_speed(rad_s(100.0), m3_s(0.05), total(150.0), air).error().subject() ==
          "air.density");
    CHECK(fans::max_fan_total_pressure(air).error().subject() == "air.density");
    CHECK(fans::max_tip_speed(air).error().subject() == "air.density");
    CHECK(fans::check_air(air).error().subject() == "air.density");
  }
}

TEST_CASE("static pressure and converted totals outside validity", "[PHY-003][SPEC-004]") {
  for (const double bad : {-1e-300, -1.0, k_nan, k_inf}) {
    const auto r = fans::fan_total_from_static(fan_static(bad), m3_s(0.05), m(0.12), k_air);
    REQUIRE(!r.has_value());
    CHECK(r.error().subject() == "fan_static_pressure");
    CHECK(r.error().details().at("bounds") == "[0, inf)");
  }
  // A converted total of 0 (the dynamic pressure underflows) is rejected, not returned.
  const auto zero = fans::fan_total_from_static(fan_static(0.0), m3_s(1e-300), m(0.12), k_air);
  REQUIRE(!zero.has_value());
  CHECK(zero.error().subject() == "fan_total_pressure");
  CHECK(zero.error().details().at("bounds") == "(0, inf)");
  // A converted total beyond the limit: 1410 + 11.53 Pa > 1418.55 Pa.
  const auto high = fans::fan_total_from_static(fan_static(1410.0), m3_s(0.05), m(0.12), k_air);
  REQUIRE(!high.has_value());
  CHECK(high.error().subject() == "fan_total_pressure");
  CHECK(high.error().details().at("bounds") == "(0, 1418.55]");
  // A dynamic pressure beyond the limit (outlet M^2/2 > EPSILON): 88 m/s through 0.12 m.
  const auto fast = fans::conventional_dynamic_pressure(m3_s(1.0), m(0.12), k_air);
  REQUIRE(!fast.has_value());
  CHECK(fast.error().subject() == "fan_dynamic_pressure");
  CHECK(fast.error().details().at("bounds") == "(0, 1418.55]");
}

TEST_CASE("fan laws need both points inside the validity range", "[PHY-003]") {
  const fans::FanPoint point{.omega = rad_s(k_omega),
                             .d_tip = m(0.119),
                             .flow = m3_s(0.05),
                             .pressure = total(150.0),
                             .power = watt(15.0),
                             .air = k_air};
  // Speed x 4 gives 2400 Pa, beyond 1418.55 Pa.
  const auto beyond = fans::scale_fan_laws(point, rad_s(4.0 * k_omega), m(0.119), k_air);
  REQUIRE(!beyond.has_value());
  CHECK(beyond.error().subject() == "fan_total_pressure");

  const auto bad_target = fans::scale_fan_laws(point, rad_s(-1.0), m(0.119), k_air);
  REQUIRE(!bad_target.has_value());
  CHECK(bad_target.error().subject() == "omega_new");
  const auto bad_d = fans::scale_fan_laws(point, rad_s(k_omega), m(0.0), k_air);
  REQUIRE(!bad_d.has_value());
  CHECK(bad_d.error().subject() == "d_tip_new");

  auto bad_point = point;
  bad_point.power = watt(0.0);
  const auto r = fans::scale_fan_laws(bad_point, rad_s(k_omega), m(0.119), k_air);
  REQUIRE(!r.has_value());
  CHECK(r.error().subject() == "power");

  auto bad_air = k_air;
  bad_air.gamma = 0.0 * isq::ratio_of_specific_heat_capacities[mp_units::one];
  const auto ra = fans::scale_fan_laws(point, rad_s(k_omega), m(0.119), bad_air);
  REQUIRE(!ra.has_value());
  CHECK(ra.error().subject() == "air.gamma");
}

TEST_CASE("the exposed limit checks match the L0 functions", "[PHY-003]") {
  CHECK(fans::check_air(k_air).has_value());
  CHECK(fans::check_pressure_limit(total(1418.55), k_air).has_value());
  const auto above = fans::check_pressure_limit(total(1418.56), k_air);
  REQUIRE(!above.has_value());
  CHECK(above.error().subject() == "fan_total_pressure");
  // 200 rad/s x 0.5 m / 2 = 50 m/s > 48.95 m/s (l0_007).
  const auto tip = fans::check_tip_speed_limit(rad_s(200.0), m(0.5), k_air);
  REQUIRE(!tip.has_value());
  CHECK(tip.error().subject() == "tip_speed");
  CHECK(fans::check_tip_speed_limit(rad_s(k_omega), m(0.119), k_air).has_value());
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>

#include "cemkit/core/error.hpp"
#include "cemkit/physics/air.hpp"

using Catch::Matchers::WithinRel;
using cemkit::core::Fidelity;
namespace physics = cemkit::physics;
namespace si = mp_units::si;
namespace isq = mp_units::isq;

namespace {
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();

cemkit::core::AbsolutePressure pa(double v) {
  return cemkit::core::units::absolute_zero_pressure + v * isq::pressure[si::pascal];
}
cemkit::core::AbsoluteTemperature kelvin(double v) {
  return mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(v);
}
}  // namespace

TEST_CASE("the gas constant is computed from the 1976 constants", "[PHY-002]") {
  const double r =
      physics::gas_constant_air().numerical_value_in(si::joule / (si::kilogram * si::kelvin));
  CHECK(r == physics::k_universal_gas_constant / physics::k_molar_mass_air);
  CHECK_THAT(r, WithinRel(287.0530720, 1e-9));
  CHECK(cemkit::core::to_string(physics::air_model()) == "platform.air@1.0.0");
}

TEST_CASE("default air has the AX-005 and 1976 values", "[PHY-002]") {
  const auto a = physics::to_si(physics::default_air());
  CHECK(a.density == 1.18);
  CHECK(a.temperature == 298.15);
  CHECK(a.pressure == 101325.0);
  CHECK(a.gamma == 1.40);
  CHECK(a.gas_constant == physics::k_universal_gas_constant / physics::k_molar_mass_air);
}

TEST_CASE("ideal-gas density at the defaults (hand calc l0_008)", "[PHY-005]") {
  const auto rho = physics::ideal_gas_density(pa(101325.0), kelvin(298.15));
  REQUIRE(rho.has_value());
  const double value = rho->value().numerical_value_in(si::kilogram / mp_units::cubic(si::metre));
  CHECK_THAT(value, WithinRel(1.1839124828745407, 1e-9));
  // ADR-003 D2: within 0.5 % of the AX-005 density.
  CHECK(std::abs(value - 1.18) / 1.18 <= 0.005);
  CHECK(rho->fidelity() == Fidelity::l0_predicted);
  CHECK(rho->model() == physics::air_model());
}

TEST_CASE("speed of sound at the defaults (l0_007 working)", "[PHY-005]") {
  const auto a = physics::speed_of_sound(physics::default_air());
  REQUIRE(a.has_value());
  CHECK_THAT(a->value().numerical_value_in(si::metre / si::second), WithinRel(346.1485560, 1e-9));
  CHECK(a->fidelity() == Fidelity::l0_predicted);
}

TEST_CASE("air functions reject non-positive or non-finite inputs", "[PHY-003]") {
  for (const double bad : {0.0, -1.0, k_nan}) {
    const auto rho = physics::ideal_gas_density(pa(bad), kelvin(298.15));
    REQUIRE(!rho.has_value());
    CHECK(rho.error().subject() == "pressure");
    CHECK(rho.error().details().at("model") == "platform.air@1.0.0");

    const auto rho_t = physics::ideal_gas_density(pa(101325.0), kelvin(bad));
    REQUIRE(!rho_t.has_value());
    CHECK(rho_t.error().subject() == "temperature");

    const auto rho_r = physics::ideal_gas_density(
        pa(101325.0), kelvin(298.15),
        bad * isq::specific_gas_constant[si::joule / (si::kilogram * si::kelvin)]);
    REQUIRE(!rho_r.has_value());
    CHECK(rho_r.error().subject() == "gas_constant");

    auto air = physics::default_air();
    air.gamma = bad * isq::ratio_of_specific_heat_capacities[mp_units::one];
    const auto sound = physics::speed_of_sound(air);
    REQUIRE(!sound.has_value());
    CHECK(sound.error().subject() == "gamma");

    air = physics::default_air();
    air.gas_constant = bad * isq::specific_gas_constant[si::joule / (si::kilogram * si::kelvin)];
    REQUIRE(!physics::speed_of_sound(air).has_value());
    CHECK(physics::speed_of_sound(air).error().subject() == "gas_constant");

    air = physics::default_air();
    air.temperature = kelvin(bad);
    REQUIRE(!physics::speed_of_sound(air).has_value());
    CHECK(physics::speed_of_sound(air).error().subject() == "temperature");
  }
}

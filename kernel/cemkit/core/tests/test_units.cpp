#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <concepts>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/units.hpp"

namespace isq = mp_units::isq;
namespace si = mp_units::si;
namespace units = cemkit::core::units;
using namespace cemkit::core;  // NOLINT(google-build-using-namespace): test file
using Catch::Matchers::WithinRel;

// kernel/testing/compile_fail/ proves these rejections with real builds; the static checks keep the
// same rules visible next to the runtime tests.
static_assert(!std::convertible_to<Length, Pressure>);
static_assert(!std::convertible_to<double, Pressure>);
static_assert(!std::convertible_to<Angle, Ratio>);
static_assert(!std::convertible_to<Ratio, Angle>);
static_assert(!std::convertible_to<Torque, mp_units::quantity<isq::energy[si::joule], double>>);
static_assert(!std::convertible_to<AbsolutePressure, Pressure>);
static_assert(!std::convertible_to<AbsoluteTemperature, TemperatureDifference>);

TEST_CASE("quantities are stored in SI units", "[COR-001]") {
  const Length diameter = 120.0 * isq::length[si::milli<si::metre>];
  CHECK_THAT(diameter.numerical_value_in(si::metre), WithinRel(0.12, 1e-15));

  const Pressure rise = 1.5 * isq::pressure[si::kilo<si::pascal>];
  CHECK(rise.numerical_value_in(si::pascal) == 1500.0);

  const VolumeFlowRate flow = 3.0 * units::volume_flow_rate[mp_units::cubic(si::metre) / si::hour];
  CHECK_THAT(flow.numerical_value_in(mp_units::cubic(si::metre) / si::second),
             WithinRel(3.0 / 3600.0, 1e-15));

  const Area area = 2.0 * isq::area[mp_units::square(si::metre)];
  const Power power = 3.0 * isq::power[si::watt];
  const Density density = 1.2 * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)];
  const AngularVelocity omega = 300.0 * isq::angular_velocity[si::radian / si::second];
  const Speed speed = 10.0 * isq::speed[si::metre / si::second];
  const Mass mass = 250.0 * isq::mass[si::gram];
  const Torque torque = 0.02 * isq::torque[si::newton * si::metre];
  const DynamicViscosity mu = 1.8e-5 * isq::dynamic_viscosity[si::pascal * si::second];
  const Angle angle = 0.5 * isq::angular_measure[si::radian];
  const Ratio ratio = 0.5 * mp_units::one;
  CHECK(area.numerical_value_in(mp_units::square(si::metre)) == 2.0);
  CHECK(power.numerical_value_in(si::watt) == 3.0);
  CHECK(density.numerical_value_in(si::kilogram / mp_units::cubic(si::metre)) == 1.2);
  CHECK(omega.numerical_value_in(si::radian / si::second) == 300.0);
  CHECK(speed.numerical_value_in(si::metre / si::second) == 10.0);
  CHECK(mass.numerical_value_in(si::kilogram) == 0.25);
  CHECK(torque.numerical_value_in(si::newton * si::metre) == 0.02);
  CHECK(mu.numerical_value_in(si::pascal * si::second) == 1.8e-5);
  CHECK(angle.numerical_value_in(si::radian) == 0.5);
  CHECK(ratio.numerical_value_in(mp_units::one) == 0.5);
}

TEST_CASE("absolute pressure is a point; differences of points are pressures", "[COR-001]") {
  const AbsolutePressure inlet =
      units::absolute_zero_pressure + 101325.0 * isq::pressure[si::pascal];
  const AbsolutePressure outlet = inlet + 250.0 * isq::pressure[si::pascal];
  const Pressure rise = outlet - inlet;
  CHECK(rise.numerical_value_in(si::pascal) == 250.0);
  CHECK(outlet.quantity_from(units::absolute_zero_pressure).numerical_value_in(si::pascal) ==
        101575.0);
}

TEST_CASE("absolute temperature is a point measured from absolute zero", "[COR-001]") {
  const AbsoluteTemperature room =
      mp_units::point<isq::thermodynamic_temperature[si::degree_Celsius]>(25.0);
  CHECK_THAT(room.quantity_from(si::absolute_zero).numerical_value_in(si::kelvin),
             WithinRel(298.15, 1e-12));

  const AbsoluteTemperature warmer =
      room + mp_units::delta<isq::thermodynamic_temperature[si::kelvin]>(5.0);
  const TemperatureDifference rise = warmer - room;
  CHECK_THAT(rise.numerical_value_in(si::kelvin), WithinRel(5.0, 1e-12));
}

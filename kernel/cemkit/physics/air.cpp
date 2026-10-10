#include "cemkit/physics/air.hpp"

#include <cmath>

#include "cemkit/physics/validity.hpp"

namespace cemkit::physics {

namespace si = mp_units::si;
namespace isq = mp_units::isq;

core::ModelId air_model() {
  return core::ModelId{.name = "platform.air", .version = {.major = 1, .minor = 0, .patch = 0}};
}

SpecificGasConstant gas_constant_air() {
  return (k_universal_gas_constant / k_molar_mass_air) *
         isq::specific_gas_constant[si::joule / (si::kilogram * si::kelvin)];
}

Air default_air() {
  return Air{
      .density = k_default_density * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)],
      .temperature =
          mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(k_default_temperature),
      .pressure =
          core::units::absolute_zero_pressure + k_sea_level_pressure * isq::pressure[si::pascal],
      .gamma = k_gamma_air * isq::ratio_of_specific_heat_capacities[mp_units::one],
      .gas_constant = gas_constant_air(),
  };
}

AirSi to_si(const Air& air) {
  return AirSi{
      .density = air.density.numerical_value_in(si::kilogram / mp_units::cubic(si::metre)),
      .temperature =
          air.temperature.quantity_from(si::absolute_zero).numerical_value_in(si::kelvin),
      .pressure = air.pressure.quantity_from(core::units::absolute_zero_pressure)
                      .numerical_value_in(si::pascal),
      .gamma = air.gamma.numerical_value_in(mp_units::one),
      .gas_constant = air.gas_constant.numerical_value_in(si::joule / (si::kilogram * si::kelvin)),
  };
}

core::Result<core::Labelled<core::Density>> ideal_gas_density(core::AbsolutePressure pressure,
                                                              core::AbsoluteTemperature temperature,
                                                              SpecificGasConstant gas_constant) {
  // SI numbers: Pa, K, J/(kg K); the result is in kg/m3.
  const double p =
      pressure.quantity_from(core::units::absolute_zero_pressure).numerical_value_in(si::pascal);
  const double t = temperature.quantity_from(si::absolute_zero).numerical_value_in(si::kelvin);
  const double r = gas_constant.numerical_value_in(si::joule / (si::kilogram * si::kelvin));
  const auto model = air_model();
  if (auto ok = require_positive("pressure", p, model); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = require_positive("temperature", t, model); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = require_positive("gas_constant", r, model); !ok) {
    return std::unexpected(ok.error());
  }
  // Operation order of the reference: p / (R T).
  const double rho = p / (r * t);
  return core::Labelled<core::Density>::l0_predicted(
      rho * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)], model);
}

core::Result<core::Labelled<core::Speed>> speed_of_sound(const Air& air) {
  const AirSi a = to_si(air);
  const auto model = air_model();
  if (auto ok = require_positive("gamma", a.gamma, model); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = require_positive("gas_constant", a.gas_constant, model); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = require_positive("temperature", a.temperature, model); !ok) {
    return std::unexpected(ok.error());
  }
  // Operation order of the reference: sqrt(gamma R T) = sqrt((gamma R) T).
  const double speed = std::sqrt(a.gamma * a.gas_constant * a.temperature);
  return core::Labelled<core::Speed>::l0_predicted(speed * isq::speed[si::metre / si::second],
                                                   model);
}

}  // namespace cemkit::physics

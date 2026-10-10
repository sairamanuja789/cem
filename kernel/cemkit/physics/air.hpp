#pragma once

// Air properties for the L0 models (ADR-003 D2; PHY-001 to PHY-005). Model platform.air@1.0.0.
// Equations, values and page references: docs/models/platform/air-properties.md. Python reference:
// python/cemkit/reference/platform/air.py (the two must agree; T08 cross-check).
//
// Assumptions: air is a calorically perfect ideal gas; no humidity correction; no viscosity at L0.

#include <mp-units/framework.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"

namespace cemkit::physics {

using SpecificGasConstant =
    mp_units::quantity<mp_units::isq::specific_gas_constant
                           [mp_units::si::joule / (mp_units::si::kilogram * mp_units::si::kelvin)],
                       double>;
using RatioOfSpecificHeats =
    mp_units::quantity<mp_units::isq::ratio_of_specific_heat_capacities[mp_units::one], double>;

// U.S. Standard Atmosphere, 1976 (NOAA, NASA, USAF; NOAA-S/T 76-1562; NASA NTRS 19770009539).
// Page references: docs/models/platform/air-properties.md.
// R*, J/(kmol K): p. 3 (Table 2 A on p. 2 misprints the exponent as 10^-3).
inline constexpr double k_universal_gas_constant = 8.31432e3;
// M0, kg/kmol, sea-level mean molar mass: section 1.2.4, eq. (21) with table 3, p. 9.
inline constexpr double k_molar_mass_air = 28.9644;
// P0, Pa: Table 2 B, p. 2.
inline constexpr double k_sea_level_pressure = 101325.0;
// gamma, dimensionless: Table 2 B, p. 2, and p. 4.
inline constexpr double k_gamma_air = 1.40;
// Requirements AX-005 (the spec's own defaults, provenance "default").
inline constexpr double k_default_density = 1.18;        // kg/m3
inline constexpr double k_default_temperature = 298.15;  // K

// The air state the L0 models use: density for the similarity formulas; pressure, temperature,
// gamma and gas constant for the incompressibility check (ADR-003 D3).
struct Air {
  core::Density density;
  core::AbsoluteTemperature temperature;  // T1
  core::AbsolutePressure pressure;        // p1
  RatioOfSpecificHeats gamma;
  SpecificGasConstant gas_constant;
};

// "platform.air@1.0.0"
[[nodiscard]] core::ModelId air_model();

// R = R*/M0, computed from the two 1976 constants, never typed as a rounded value.
[[nodiscard]] SpecificGasConstant gas_constant_air();

// AX-005 defaults with the U.S. Standard Atmosphere 1976 constants.
[[nodiscard]] Air default_air();

// rho = p / (R T). Valid for finite, strictly positive p, T and R (subjects "pressure",
// "temperature", "gas_constant").
[[nodiscard]] core::Result<core::Labelled<core::Density>> ideal_gas_density(
    core::AbsolutePressure pressure, core::AbsoluteTemperature temperature,
    SpecificGasConstant gas_constant = gas_constant_air());

// a = sqrt(gamma R T). Valid for finite, strictly positive gamma, R and T (subjects "gamma",
// "gas_constant", "temperature").
[[nodiscard]] core::Result<core::Labelled<core::Speed>> speed_of_sound(const Air& air);

// SI numerical values of an Air, for the physics functions that unwrap it (raw doubles only after
// this explicit conversion to the coherent SI unit of each field).
struct AirSi {
  double density;       // kg/m3
  double temperature;   // K
  double pressure;      // Pa
  double gamma;         // 1
  double gas_constant;  // J/(kg K)
};
[[nodiscard]] AirSi to_si(const Air& air);

}  // namespace cemkit::physics

#pragma once

// Quantity kinds and origins that mp-units' ISQ does not name itself (COR-001). The double-backed
// SI aliases used throughout the kernel are in quantity.hpp. Conventions:
// docs/models/platform/units.md.

#include <mp-units/framework.h>
#include <mp-units/systems/isq.h>

namespace cemkit::core::units {

// Volume flow rate, q_V = dV/dt (ISO 80000-4 lists it; mp-units 2.5's ISQ does not).
QUANTITY_SPEC(volume_flow_rate, mp_units::isq::volume / mp_units::isq::time);

// Origin of absolute pressure. Absolute pressures are points measured from it; the difference of
// two absolute pressures is a pressure (a quantity), and two absolute pressures cannot be added.
// NOLINTNEXTLINE(readability-identifier-naming): mp-units origin naming, like si::absolute_zero
inline constexpr struct absolute_zero_pressure final
    : mp_units::absolute_point_origin<mp_units::isq::pressure> {
} absolute_zero_pressure;

}  // namespace cemkit::core::units

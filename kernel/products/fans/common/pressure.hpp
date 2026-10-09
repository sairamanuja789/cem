#pragma once

// Fan total pressure and fan static pressure as distinct quantity kinds (COR-001, SPEC-004). Both
// are pressure differences in pascal. Neither converts to the other, even explicitly, and neither
// converts implicitly to or from a plain pressure. Explicit construction from a plain pressure
// (FanTotalPressure{p}) compiles; only the spec compiler may use it, where the user's stated type
// is recorded. Definitions (ISO 5801:2017): docs/models/fans/pressure-kinds.md. The fan dynamic
// pressure (conventional, from the mean outlet velocity) is a third distinct kind. The only
// conversion between the kinds is l0.hpp's fan_total_from_static, which records its rule.

#include <mp-units/framework.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

namespace cemkit::fans {

QUANTITY_SPEC(fan_total_pressure, mp_units::isq::pressure, mp_units::is_kind);
QUANTITY_SPEC(fan_static_pressure, mp_units::isq::pressure, mp_units::is_kind);
QUANTITY_SPEC(fan_dynamic_pressure, mp_units::isq::pressure, mp_units::is_kind);

using FanTotalPressure = mp_units::quantity<fan_total_pressure[mp_units::si::pascal], double>;
using FanStaticPressure = mp_units::quantity<fan_static_pressure[mp_units::si::pascal], double>;
using FanDynamicPressure = mp_units::quantity<fan_dynamic_pressure[mp_units::si::pascal], double>;

}  // namespace cemkit::fans

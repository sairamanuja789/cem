#pragma once

// Double-backed SI quantity types used by every kernel interface (COR-001). Values are stored in
// the coherent SI unit named in each alias; assigning a value in another unit of the same kind
// converts it. Distinct kinds (angle and ratio, torque and energy) do not convert into each other.
// Absolute pressure and absolute temperature are points: their differences are quantities, and two
// points cannot be added. Conventions: docs/models/platform/units.md.

#include <mp-units/framework.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/units.hpp"

namespace cemkit::core {

namespace detail {
namespace isq = mp_units::isq;
namespace si = mp_units::si;
using mp_units::cubic;
using mp_units::square;
}  // namespace detail

using Length = mp_units::quantity<detail::isq::length[detail::si::metre], double>;
using Area = mp_units::quantity<detail::isq::area[detail::square(detail::si::metre)], double>;
using VolumeFlowRate = mp_units::quantity<
    units::volume_flow_rate[detail::cubic(detail::si::metre) / detail::si::second], double>;
using Pressure = mp_units::quantity<detail::isq::pressure[detail::si::pascal], double>;
using AbsolutePressure = mp_units::quantity_point<detail::isq::pressure[detail::si::pascal],
                                                  units::absolute_zero_pressure, double>;
using Power = mp_units::quantity<detail::isq::power[detail::si::watt], double>;
using Density = mp_units::quantity<
    detail::isq::mass_density[detail::si::kilogram / detail::cubic(detail::si::metre)], double>;
using AngularVelocity =
    mp_units::quantity<detail::isq::angular_velocity[detail::si::radian / detail::si::second],
                       double>;
using Speed =
    mp_units::quantity<detail::isq::speed[detail::si::metre / detail::si::second], double>;
using Mass = mp_units::quantity<detail::isq::mass[detail::si::kilogram], double>;
using Torque =
    mp_units::quantity<detail::isq::torque[detail::si::newton * detail::si::metre], double>;
using AbsoluteTemperature =
    mp_units::quantity_point<detail::isq::thermodynamic_temperature[detail::si::kelvin],
                             detail::si::absolute_zero, double>;
using TemperatureDifference =
    mp_units::quantity<detail::isq::thermodynamic_temperature[detail::si::kelvin], double>;
using DynamicViscosity =
    mp_units::quantity<detail::isq::dynamic_viscosity[detail::si::pascal * detail::si::second],
                       double>;
using Angle = mp_units::quantity<detail::isq::angular_measure[detail::si::radian], double>;
using Ratio = mp_units::quantity<mp_units::dimensionless[mp_units::one], double>;

}  // namespace cemkit::core

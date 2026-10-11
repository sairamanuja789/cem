#include "cemkit/simulation/port/case.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cmath>
#include <string>

#include "cemkit/core/error.hpp"

namespace cemkit::simulation::port {

namespace {

namespace si = mp_units::si;

double m(core::Length l) { return l.numerical_value_in(si::metre); }

core::Status bad(const std::string& subject, const std::string& message) {
  return core::fail(core::ErrorCode::invalid_input, message, subject);
}

bool positive(double v) { return std::isfinite(v) && v > 0.0; }

}  // namespace

core::Status validate(const RotorSectorCase& c) {
  if (c.sectors < 2) {
    return bad("sectors", "a rotor sector case needs at least 2 sectors");
  }
  for (const auto& [subject, value] :
       {std::pair{"hub_radius", m(c.hub_radius)}, std::pair{"duct_radius", m(c.duct_radius)},
        std::pair{"rotor_half_length", m(c.rotor_half_length)},
        std::pair{"cell_size", m(c.cell_size)},
        std::pair{"inlet_mixing_length", m(c.inlet_mixing_length)},
        std::pair{"omega", c.omega.numerical_value_in(si::radian / si::second)},
        std::pair{"flow", c.flow.numerical_value_in(mp_units::cubic(si::metre) / si::second)},
        std::pair{"dynamic_viscosity",
                  c.dynamic_viscosity.numerical_value_in(si::pascal * si::second)},
        std::pair{"density",
                  c.density.numerical_value_in(si::kilogram / mp_units::cubic(si::metre))},
        std::pair{"inlet_turbulence_intensity", c.inlet_turbulence_intensity}}) {
    if (!positive(value)) {
      return bad(subject, std::string{"must be finite and positive: "} + subject);
    }
  }
  if (!(c.inlet_turbulence_intensity < 1.0)) {
    return bad("inlet_turbulence_intensity", "a turbulence intensity is a fraction below 1");
  }
  if (!(m(c.hub_radius) < m(c.duct_radius))) {
    return bad("hub_radius", "the hub must be inside the duct");
  }
  const double z_in = m(c.z_inlet);
  const double z_out = m(c.z_outlet);
  const double half = m(c.rotor_half_length);
  if (!std::isfinite(z_in) || !std::isfinite(z_out) || z_in >= -half || half >= z_out) {
    return bad("z_inlet", "the inlet must be upstream and the outlet downstream of the rotor zone");
  }
  if (!(m(c.cell_size) < m(c.duct_radius) - m(c.hub_radius))) {
    return bad("cell_size", "the cell size must be less than the annulus height");
  }
  if (c.blade_refinement < 0 || c.rotor_refinement < 0 || c.rotor_refinement > c.blade_refinement) {
    return bad("blade_refinement",
               "refinement levels must be non-negative, the blade's at least the zone's");
  }
  if (c.processes == 0) {
    return bad("processes", "at least one solver process");
  }
  if (c.iterations == 0) {
    return bad("iterations", "at least one iteration");
  }
  if (c.blade_stl_mm.empty()) {
    return bad("blade_stl_mm", "the blade surface is empty");
  }
  return {};
}

}  // namespace cemkit::simulation::port

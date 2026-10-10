// Simulation port (SIM-001): a rotor sector case is validated before any writer sees it.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>

#include "cemkit/simulation/port/case.hpp"

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using cemkit::simulation::port::RotorSectorCase;

namespace {
RotorSectorCase good() {
  const auto len = [](double v) { return v * isq::length[si::metre]; };
  return RotorSectorCase{
      .sectors = 7,
      .hub_radius = len(0.02),
      .duct_radius = len(0.06),
      .z_inlet = len(-0.06),
      .z_outlet = len(0.12),
      .rotor_half_length = len(0.0125),
      .cell_size = len(0.0025),
      .blade_refinement = 3,
      .rotor_refinement = 1,
      .omega = 209.4 * isq::angular_velocity[si::radian / si::second],
      .flow = 0.028 *
              cemkit::core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .dynamic_viscosity = 1.8e-5 * isq::dynamic_viscosity[si::pascal * si::second],
      .density = 1.18 * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)],
      .inlet_turbulence_intensity = 0.05,
      .inlet_mixing_length = len(0.004),
      .iterations = 100,
      .blade_stl_mm = {1, 2, 3}};
}
}  // namespace

TEST_CASE("a valid rotor sector case passes; each bad field is named", "[SIM-001]") {
  CHECK(cemkit::simulation::port::validate(good()).has_value());
  auto c = good();
  c.hub_radius = c.duct_radius;
  CHECK(cemkit::simulation::port::validate(c).error().subject() == "hub_radius");
  c = good();
  c.z_inlet = -0.01 * isq::length[si::metre];
  CHECK(cemkit::simulation::port::validate(c).error().subject() == "z_inlet");
  c = good();
  c.rotor_refinement = 4;
  CHECK(cemkit::simulation::port::validate(c).error().subject() == "blade_refinement");
  c = good();
  c.blade_stl_mm.clear();
  CHECK(cemkit::simulation::port::validate(c).error().subject() == "blade_stl_mm");
  c = good();
  c.sectors = 1;
  CHECK(cemkit::simulation::port::validate(c).error().subject() == "sectors");
}

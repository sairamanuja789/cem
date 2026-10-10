// OpenFOAM rotor sector template (SIM-001, TRUST-004): deterministic files from the case data, the
// rotating zone ending on block faces, the passage flow, the rotation sense, and thresholds that
// name their sources.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "cemkit/simulation/openfoam/writer.hpp"

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using cemkit::simulation::port::RotorSectorCase;

namespace {
RotorSectorCase trial() {
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
      .omega = 209.43951023931953 * isq::angular_velocity[si::radian / si::second],
      .flow = 0.028 *
              cemkit::core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .dynamic_viscosity = 1.8e-5 * isq::dynamic_viscosity[si::pascal * si::second],
      .density = 1.18 * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)],
      .inlet_turbulence_intensity = 0.05,
      .inlet_mixing_length = len(0.004),
      .iterations = 1500,
      .blade_stl_mm = {1, 2, 3}};
}
bool has(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}
}  // namespace

TEST_CASE("the rotor sector case is deterministic and states its template", "[SIM-001]") {
  const cemkit::simulation::openfoam::OpenFoamWriter writer;
  const auto a = writer.rotor_sector(trial());
  const auto b = writer.rotor_sector(trial());
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(a->text == b->text);
  CHECK(cemkit::core::to_string(a->case_template) == "cemkit.openfoam.rotor_sector_mrf@0.1.0");
  CHECK(a->binary.at("constant/triSurface/blade_mm.stl") == std::vector<std::uint8_t>{1, 2, 3});
  REQUIRE(a->steps.size() == 7);
  CHECK(a->steps.back().argv == std::vector<std::string>{"simpleFoam"});
}

TEST_CASE("blocks end on the rotating zone; passage flow and rotation sense", "[SIM-001][ADR-016]") {
  const auto files = cemkit::simulation::openfoam::OpenFoamWriter{}.rotor_sector(trial());
  REQUIRE(files.has_value());
  const std::string& mesh = files->text.at("system/blockMeshDict");
  // Three axial blocks: 47.5 mm, 25 mm and 107.5 mm at 2.5 mm cells -> 19, 10 and 43 cells.
  CHECK(has(mesh, "(16 14 19)"));
  CHECK(has(mesh, "(16 14 10)"));
  CHECK(has(mesh, "(16 14 43)"));
  CHECK(has(mesh, "cyclicAMI"));
  CHECK(has(files->text.at("system/topoSetDict"), "p1 (0 0 -0.0125); p2 (0 0 0.0125)"));
  CHECK(has(files->text.at("0/U"), "volumetricFlowRate 0.004;"));  // 0.028 / 7
  // Counter-clockwise viewed from the inlet = about -z (flow along +z).
  CHECK(has(files->text.at("constant/MRFProperties"), "axis (0 0 -1); omega 209.43951023931953;"));
  CHECK(has(files->text.at("constant/transportProperties"), "nu 1.5254237288135594e-05;"));
}

TEST_CASE("the template's trust thresholds name their sources", "[TRUST-004]") {
  const auto files = cemkit::simulation::openfoam::OpenFoamWriter{}.rotor_sector(trial());
  REQUIRE(files.has_value());
  const auto& t = files->thresholds;
  CHECK(t.mass_imbalance == 0.005);
  CHECK(t.flat_fraction == 0.2);
  CHECK(t.residuals.size() == 6);
  CHECK(t.sources.size() == 4);
  CHECK(has(t.sources[0], "UNSOURCED"));
}

TEST_CASE("more than one process decomposes the case and runs the solver under MPI", "[SIM-002]") {
  auto c = trial();
  c.processes = 4;
  const auto files = cemkit::simulation::openfoam::OpenFoamWriter{}.rotor_sector(c);
  REQUIRE(files.has_value());
  CHECK(has(files->text.at("system/decomposeParDict"), "numberOfSubdomains 4;"));
  REQUIRE(files->steps.size() == 8);
  CHECK(files->steps.back().argv ==
        std::vector<std::string>{"mpirun", "-np", "4", "simpleFoam", "-parallel"});
}

TEST_CASE("an invalid case is rejected before any file is written", "[SIM-001]") {
  auto c = trial();
  c.cell_size = 0.05 * isq::length[si::metre];
  const auto r = cemkit::simulation::openfoam::OpenFoamWriter{}.rotor_sector(c);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().subject() == "cell_size");
}

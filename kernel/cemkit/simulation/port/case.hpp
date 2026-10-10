#pragma once
// Simulation port (ADR-009, ADR-015): case definitions a product hands to a solver case writer,
// and the writer interface. A product describes WHAT to simulate in solver-neutral SI terms; an
// adapter (simulation/openfoam) turns that into solver files from a versioned template (SIM-001).
// The kernel never runs a solver: it returns files and the command steps to run them (CLAUDE.md
// rule 1); orchestration runs them under resource limits (SIM-002).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"

namespace cemkit::simulation::port {

// One blade passage of a ducted axial rotor in an annular duct (hub extended through the domain),
// steady, with a rotating (MRF) zone over the rotor's axial extent. Flow along +z; the rotor turns
// counter-clockwise viewed from the inlet (about -z) at `omega`. The blade surface is a closed
// triangulated solid (STL, millimetres) of ONE blade centred on +x; the passage spans
// +-pi/sectors about +x with rotationally periodic sides.
struct RotorSectorCase {
  std::size_t sectors = 0;  // blade count
  core::Length hub_radius;
  core::Length duct_radius;
  core::Length z_inlet;
  core::Length z_outlet;
  core::Length rotor_half_length;  // the rotating zone is |z| <= rotor_half_length
  core::Length cell_size;          // background cell size; the mesh study varies only this
  int blade_refinement = 0;        // refinement levels at the blade surface
  int rotor_refinement = 0;        // refinement levels inside the rotating zone
  core::AngularVelocity omega;
  core::VolumeFlowRate flow;  // through the whole annulus (the passage gets flow / sectors)
  core::DynamicViscosity dynamic_viscosity;
  core::Density density;  // for the force monitor (the solver is kinematic)
  double inlet_turbulence_intensity = 0.0;
  core::Length inlet_mixing_length;
  std::size_t iterations = 0;
  std::size_t processes = 1;  // solver processes (MPI ranks when above 1)
  std::vector<std::uint8_t> blade_stl_mm;
};

// A command to run in the case directory, in order.
struct Step {
  std::string name;
  std::vector<std::string> argv;

  bool operator==(const Step&) const = default;
};

// TRUST-004: thresholds are per quantity and per versioned case template.
struct TrustThresholds {
  std::map<std::string, double> residuals;  // final initial residual per solved field
  double mass_imbalance = 0.0;              // |Q_in - Q_out| / |Q_in|
  double flat_fraction = 0.0;               // monitors are checked over this last fraction
  double flatness = 0.0;                    // allowed relative range there
  // A monitor's range is divided by max(|mean|, its scale here): a pressure rise near zero would
  // otherwise make any noise look large. Monitors not listed use |mean| alone.
  std::map<std::string, double> monitor_scales;
  std::vector<std::string> sources;  // where each threshold comes from (or UNSOURCED)

  bool operator==(const TrustThresholds&) const = default;
};

struct CaseFiles {
  core::ModelId case_template;
  std::map<std::string, std::string> text;                  // relative path -> content
  std::map<std::string, std::vector<std::uint8_t>> binary;  // relative path -> bytes
  std::vector<Step> steps;
  TrustThresholds thresholds;
  // What each monitor is: name -> description (the runner reads postProcessing/<name>).
  std::map<std::string, std::string> monitors;
};

class CaseWriter {
 public:
  CaseWriter() = default;
  CaseWriter(const CaseWriter&) = delete;
  CaseWriter& operator=(const CaseWriter&) = delete;
  CaseWriter(CaseWriter&&) = delete;
  CaseWriter& operator=(CaseWriter&&) = delete;
  virtual ~CaseWriter() = default;

  [[nodiscard]] virtual std::string solver() const = 0;
  // invalid_input naming the first bad field, or the case files.
  [[nodiscard]] virtual core::Result<CaseFiles> rotor_sector(const RotorSectorCase& c) const = 0;
};

// invalid_input naming the first field outside its domain (non-positive sizes, hub not inside the
// duct, inlet not upstream of the rotor zone, ...). Shared by every writer.
[[nodiscard]] core::Status validate(const RotorSectorCase& c);

}  // namespace cemkit::simulation::port

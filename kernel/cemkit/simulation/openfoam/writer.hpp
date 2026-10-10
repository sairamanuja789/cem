#pragma once
// OpenFOAM v2512 case writer (simulation adapter, ADR-006, ADR-009): template
// cemkit.openfoam.rotor_sector_mrf@0.2.0 (docs/models/platform/openfoam-rotor-sector.md).
// One blade passage: blockMesh annular sector in three axial blocks (upstream, rotor, downstream)
// with rotationally periodic cyclicAMI sides, snappyHexMesh refinement at the blade, a topoSet
// rotating zone that ends on block faces, simpleFoam with MRF and k-omega SST, and function
// objects for flow, area- and mass-weighted pressures, torque and residuals. Deterministic text.

#include <string>

#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/simulation/port/case.hpp"

namespace cemkit::simulation::openfoam {

[[nodiscard]] core::ModelId rotor_sector_template();

class OpenFoamWriter final : public port::CaseWriter {
 public:
  [[nodiscard]] std::string solver() const override;
  [[nodiscard]] core::Result<port::CaseFiles> rotor_sector(
      const port::RotorSectorCase& c) const override;
};

}  // namespace cemkit::simulation::openfoam

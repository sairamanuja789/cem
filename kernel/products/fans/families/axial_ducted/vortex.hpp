// General vortex blade design with simple radial equilibrium, loss-free (L1). Model
// fans.axial.vortex@0.1.0; equations, assumptions and sources: docs/models/fans/axial-vortex.md,
// ADR-018. Port of python/cemkit/reference/fans/axial/vortex.py (PHY-005: same algorithm, same
// order of operations).
#pragma once

#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/physics/air.hpp"
#include "products/fans/families/axial_ducted/free_vortex.hpp"

namespace cemkit::fans::axial_ducted {

[[nodiscard]] core::ModelId vortex_model();

// c_theta2 = K r^n (c_theta1 = 0) with n in [-1, 1]; c_x2(r) from simple radial equilibrium,
// scaled to the flow by continuity; K from the duty (mass-averaged loss-free total-pressure rise).
[[nodiscard]] core::Result<FreeVortexDesign> vortex_design(const FreeVortexInputs& in,
                                                           double vortex_exponent,
                                                           const physics::Air& air);

}  // namespace cemkit::fans::axial_ducted

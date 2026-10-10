#pragma once
// fans.axial.free_vortex@0.1.0 (L1 predicted): loss-free free-vortex blade sections of the axial
// rotor from hub to tip. Equations, conventions and sources: docs/models/fans/axial-free-vortex.md;
// Python reference python/cemkit/reference/fans/axial/free_vortex.py (PHY-005).
//
// c_theta2 = dp_t / (rho omega r) at every station (free vortex, no inlet swirl); beta1, beta2 from
// the triangles; circular-arc section with zero incidence and zero deviation (no deviation rule is
// sourced, UNSOURCED): camber angle theta = beta1 - beta2, stagger from the axis
// xi = (beta1 + beta2) / 2, camber height h = (c / 2) tan(theta / 4). Geometry recipe stagger is
// -xi (flow along +z, counter-clockwise from the inlet, ADR-016). The embedded root station inside
// the hub repeats the hub section.

#include <cstddef>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/fidelity.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/physics/air.hpp"
#include "products/fans/common/l0.hpp"
#include "products/fans/families/axial_ducted/mean_line.hpp"

namespace cemkit::fans::axial_ducted {

[[nodiscard]] core::ModelId free_vortex_model();

struct FreeVortexStation {
  core::Length radius;
  core::Speed blade_speed;            // U = omega r
  core::Speed outlet_swirl;           // c_theta2
  core::Angle relative_inlet_angle;   // beta1
  core::Angle relative_outlet_angle;  // beta2
  core::Angle camber_angle;           // theta
  core::Angle stagger_from_axis;      // xi
  core::Angle recipe_stagger;         // -xi
  core::Length chord;
  core::Length camber;  // h
  core::Length thickness;
  core::Ratio de_haller_ratio;  // W2 / W1, reported only (no sourced limit)
  core::Ratio reaction;         // 1 - c_theta2 / (2 U)
  bool embedded = false;
};

struct FreeVortexInputs {
  core::VolumeFlowRate flow;
  FanTotalPressure pressure;
  core::AngularVelocity omega;
  core::Length r_tip;
  core::Length r_hub;
  core::Length chord_hub;
  core::Length chord_tip;
  core::Length thickness;
  std::size_t stations = 0;
  core::Length embed_depth;
};

// Every quantity is L1 predicted by free_vortex_model().
struct FreeVortexDesign {
  MeanLine mean_line;
  core::Speed axial_velocity;
  std::vector<FreeVortexStation> stations;  // embedded root first, tip last
  core::Fidelity fidelity;
  core::ModelId model;
};

[[nodiscard]] core::Result<FreeVortexDesign> free_vortex_design(const FreeVortexInputs& in,
                                                                const physics::Air& air);

}  // namespace cemkit::fans::axial_ducted

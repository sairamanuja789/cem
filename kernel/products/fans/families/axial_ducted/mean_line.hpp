#pragma once

// fans.axial_ducted L1: the rotor's loss-free mean-line velocity triangles (model
// fans.axial.mean_line@0.1.0; PHY-001, PHY-002, PHY-005). Reference:
// python/cemkit/reference/fans/axial/mean_line.py; model page docs/models/fans/axial-mean-line.md.
//
// Euler's turbomachinery equation (conservation of angular momentum) with no inlet swirl:
// dp_t = rho U c_theta2 at the mean radius r_m = sqrt((r_tip^2 + r_hub^2) / 2) (equal-area split),
// uniform axial velocity c_x = Q / (pi (r_tip^2 - r_hub^2)). Loss-free: no efficiency or loss is
// sourced (UNSOURCED), so the swirl is the least that delivers dp_t and every output is ideal.
// The input fan total pressure is taken as the rotor's ideal Euler rise, which includes the outlet
// swirl dynamic pressure; for a rotor without a stator this overstates the delivered fan total
// pressure (docs/models/fans/axial-mean-line.md). Angles are from the axial direction. Validity:
// inputs finite and positive, r_hub < r_tip, and ADR-003 D3 (dp_t <= EPSILON gamma p1, omega r_tip
// within the tip-Mach limit); outside it an out_of_validity error, never a number.

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/physics/air.hpp"
#include "products/fans/common/pressure.hpp"

namespace cemkit::fans::axial_ducted {

[[nodiscard]] core::ModelId mean_line_model();

// Every quantity is L1 predicted by mean_line_model() (fidelity carried by the struct).
struct MeanLine {
  core::Area annulus_area;
  core::Length mean_radius;
  core::Speed blade_speed;            // U at r_m
  core::Speed axial_velocity;         // c_x
  core::Speed outlet_swirl;           // c_theta2
  core::Angle relative_inlet_angle;   // beta1
  core::Angle relative_outlet_angle;  // beta2
  core::Angle absolute_outlet_angle;  // alpha2
  core::Speed relative_inlet_speed;   // W1
  core::Speed relative_outlet_speed;  // W2
  core::Ratio flow_coefficient;       // phi_m = c_x / U
  core::Ratio work_coefficient;       // psi_E = c_theta2 / U
  core::Ratio de_haller_ratio;        // W2 / W1, reported only (no sourced limit)
  core::Fidelity fidelity;
  core::ModelId model;
};

[[nodiscard]] core::Result<MeanLine> mean_line(core::VolumeFlowRate flow, FanTotalPressure pressure,
                                               core::AngularVelocity omega, core::Length r_tip,
                                               core::Length r_hub, const physics::Air& air);

}  // namespace cemkit::fans::axial_ducted

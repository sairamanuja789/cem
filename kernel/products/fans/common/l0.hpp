#pragma once

// L0 fan similarity (T08; SEL-001, PHY-001 to PHY-005). Model fans.l0@1.0.0. Port of the Python
// reference python/cemkit/reference/fans/common/l0.py; the two must agree (ctest
// fans_l0_crosscheck). Formulas: docs/architecture.md section 6. Decisions: ADR-003 D1, D3, D4, D6.
// Equations, conventions, validity and sources: docs/models/fans/l0-similarity.md.
//
// Conventions: D is the rotor tip diameter and U = omega D / 2 (ADR-003 D4); the duct diameter
// enters only the dynamic pressure. psi, omega_s and delta_s take fan total pressure only; a fan
// static pressure does not convert to it (compile error), see pressure.hpp (ADR-003 D1).
//
// Assumptions: geometrically similar fans in the same Reynolds-number regime (recorded, not
// checked: L0 has no viscosity); incompressible flow within k_epsilon (ADR-003 D3; linearised
// isentropic relations, NACA Report 1135 (1953), p. 616, eqs. (34) and (45)).
//
// Validity: every input finite and strictly positive (the fan static pressure may be 0, free
// delivery), Delta p_t / (gamma p1) <= k_epsilon and M_tip^2 / 2 <= k_epsilon, each applied where
// its inputs are available. Outside, a function returns out_of_validity, never a number. Checks
// run in a fixed order: inputs in argument order, air, pressure limit, tip-Mach limit.
//
// Every value is returned labelled L0 predicted with model fans.l0@1.0.0 (COR-003, PHY-004).

#include <string_view>

#include "cemkit/core/provenance.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/physics/air.hpp"
#include "products/fans/common/pressure.hpp"

namespace cemkit::fans {

// Project tolerance for the incompressibility criteria. A project decision recorded in ADR-003 D3,
// not a standard value.
inline constexpr double k_epsilon = 0.01;

inline constexpr std::string_view k_dynamic_pressure_rule =
    "ISO 5801: fan total = fan static + conventional fan dynamic pressure 1/2 rho (Q/A_out)^2, "
    "A_out = pi D_duct^2 / 4, Mach factor 1 within ADR-003 D3";

// "fans.l0@1.0.0"
[[nodiscard]] core::ModelId l0_model();

template <class Q>
using L0Result = core::Result<core::Labelled<Q>>;

// One operating point of one fan, for the fan laws.
struct FanPoint {
  core::AngularVelocity omega;
  core::Length d_tip;
  core::VolumeFlowRate flow;
  FanTotalPressure pressure;
  core::Power power;  // shaft power
  physics::Air air;
};

// A fan point produced by the fan laws: the scaled flow, pressure and power are L0 predicted.
struct ScaledFanPoint {
  core::AngularVelocity omega;
  core::Length d_tip;
  core::Labelled<core::VolumeFlowRate> flow;
  core::Labelled<FanTotalPressure> pressure;
  core::Labelled<core::Power> power;
  physics::Air air;
};

// A fan total pressure computed from a fan static pressure: provenance "derived", with its rule.
struct DerivedFanTotalPressure {
  core::Labelled<FanTotalPressure> value;
  core::Provenance provenance{core::Provenance::derived};
  std::string_view rule{k_dynamic_pressure_rule};
};

// The model's own validity checks, exposed for the feasibility gate (same subjects and texts as the
// functions below). The limit checks assume inputs and air that already passed.
[[nodiscard]] core::Status check_air(const physics::Air& air);
[[nodiscard]] core::Status check_pressure_limit(FanTotalPressure pressure, const physics::Air& air);
[[nodiscard]] core::Status check_tip_speed_limit(core::AngularVelocity omega, core::Length d_tip,
                                                 const physics::Air& air);

// k_epsilon gamma p1, the largest fan total pressure inside the incompressible range.
[[nodiscard]] L0Result<FanTotalPressure> max_fan_total_pressure(const physics::Air& air);

// sqrt(2 k_epsilon) a1, the largest blade tip speed with M_tip^2 / 2 <= k_epsilon.
[[nodiscard]] L0Result<core::Speed> max_tip_speed(const physics::Air& air);

// U = omega D / 2.
[[nodiscard]] L0Result<core::Speed> tip_speed(core::AngularVelocity omega, core::Length d_tip);

// phi = Q / ((pi/4) D^2 U).
[[nodiscard]] L0Result<core::Ratio> flow_coefficient(core::VolumeFlowRate flow, core::Length d_tip,
                                                     core::AngularVelocity omega,
                                                     const physics::Air& air);

// psi = 2 Delta p_t / (rho U^2).
[[nodiscard]] L0Result<core::Ratio> pressure_coefficient(FanTotalPressure pressure,
                                                         core::Length d_tip,
                                                         core::AngularVelocity omega,
                                                         const physics::Air& air);

// omega_s = omega sqrt(Q) / (Delta p_t / rho)^(3/4).
[[nodiscard]] L0Result<core::Ratio> specific_speed(core::AngularVelocity omega,
                                                   core::VolumeFlowRate flow,
                                                   FanTotalPressure pressure,
                                                   const physics::Air& air);

// delta_s = D (Delta p_t / rho)^(1/4) / sqrt(Q).
[[nodiscard]] L0Result<core::Ratio> specific_diameter(core::Length d_tip, core::VolumeFlowRate flow,
                                                      FanTotalPressure pressure,
                                                      const physics::Air& air);

// p_d = 1/2 rho (Q / A_out)^2, A_out = pi D_duct^2 / 4 (ISO 5801 conventional fan dynamic
// pressure, Mach factor 1). The pressure limit applies to p_d too: p_d / (gamma p1) = M_out^2 / 2
// (NACA Report 1135, eq. (31b)), with the outlet static pressure ~ p1 and rho ~ p1/(R T1) (within
// the 0.5 % bound of ADR-003 D2).
[[nodiscard]] L0Result<FanDynamicPressure> conventional_dynamic_pressure(core::VolumeFlowRate flow,
                                                                         core::Length d_duct,
                                                                         const physics::Air& air);

// Delta p_t = Delta p_s + p_d (ADR-003 D1). Delta p_s >= 0 (free delivery is 0); the converted
// total must be strictly positive and inside the pressure limit.
[[nodiscard]] core::Result<DerivedFanTotalPressure> fan_total_from_static(
    FanStaticPressure pressure, core::VolumeFlowRate flow, core::Length d_duct,
    const physics::Air& air);

// Fan laws for a geometrically similar fan in the same Reynolds regime: Q ~ N D^3,
// Delta p ~ rho N^2 D^2, P ~ rho N^3 D^5. Both the given and the scaled point must be valid.
[[nodiscard]] core::Result<ScaledFanPoint> scale_fan_laws(const FanPoint& point,
                                                          core::AngularVelocity omega,
                                                          core::Length d_tip,
                                                          const physics::Air& air);

}  // namespace cemkit::fans

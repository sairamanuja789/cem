#pragma once

// L0 feasibility gate (T08; SEL-002, SEL-004, PHY-003). Model fans.feasibility@1.0.0. Rules:
// docs/models/fans/feasibility.md. Decisions: ADR-003 D3 (limits), ADR-011 (proposed: check order,
// "range unsourced" status, nearest-feasible-duty rule). Python reference:
// python/cemkit/reference/fans/common/feasibility.py (ctest fans_l0_crosscheck compares them).
//
// The gate runs before any geometry exists and reports one check per limit, in this order:
// 1. incompressible_pressure: Delta p_t <= k_epsilon gamma p1. Nearest feasible duty: same flow,
//    Delta p_t = k_epsilon gamma p1.
// 2. incompressible_tip_speed: U <= sqrt(2 k_epsilon) a1, when the speed and tip diameter are
//    given. A duty change cannot fix it, so there is no nearest feasible duty.
// 3. family_specific_speed_range: omega_s inside the family's cited range (SEL-002). A range
//    without a citation is reported as range_unsourced, never as a pass or a failure. Nearest
//    feasible duty: the closest duty in (ln Q, ln Delta p_t) whose omega_s is on the crossed bound.

#include <optional>
#include <string>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/physics/air.hpp"
#include "products/fans/common/pressure.hpp"

namespace cemkit::fans {

// Citation text that marks a range as having no verified source (CLAUDE.md rule 7).
inline constexpr std::string_view k_unsourced = "UNSOURCED";

// A family's feasible specific-speed range, from data/fans/family_ranges.yaml. source is the
// citation, or k_unsourced; an unsourced range has no bounds.
struct FamilyRange {
  std::string family;
  // omega_s = omega sqrt(Q) / (Delta p_t / rho)^(3/4) with omega in rad/s, Q in m3/s, Delta p_t
  // in Pa (fan total), rho in kg/m3. Cordier sigma or rpm-based specific speeds (n_q, N_s) are
  // different numbers: convert them explicitly before entering them.
  std::optional<double> specific_speed_min;
  std::optional<double> specific_speed_max;
  std::string source;

  [[nodiscard]] bool sourced() const noexcept {
    return source != k_unsourced && !source.empty() && specific_speed_min.has_value() &&
           specific_speed_max.has_value();
  }
};

struct Duty {
  core::VolumeFlowRate flow;
  FanTotalPressure pressure;
};

// A duty the gate proposes: computed numbers, so labelled L0 predicted, fans.feasibility@1.0.0.
struct LabelledDuty {
  core::Labelled<core::VolumeFlowRate> flow;
  core::Labelled<FanTotalPressure> pressure;
};

enum class CheckStatus { pass, violated, range_unsourced, not_computable };

// The gate's overall result. confirmed: every check passed. unconfirmed: none violated, but at
// least one could not be applied (range_unsourced or not_computable). infeasible: a check was
// violated. An unconfirmed duty is never reported as feasible (ADR-011).
enum class Verdict { infeasible, unconfirmed, confirmed };

[[nodiscard]] std::string_view to_string(Verdict verdict) noexcept;

[[nodiscard]] std::string_view to_string(CheckStatus status) noexcept;

struct FeasibilityCheck {
  std::string limit;
  CheckStatus status{CheckStatus::not_computable};
  std::string message;
  std::optional<core::Error> violation;
  // The nearest duty that satisfies this one limit; it is not checked against the other limits.
  std::optional<LabelledDuty> nearest_feasible;
};

struct FeasibilityReport {
  std::vector<FeasibilityCheck> checks;
  // L0 predicted, model fans.l0@1.0.0; absent when it cannot be computed.
  std::optional<core::Labelled<core::Ratio>> specific_speed;

  [[nodiscard]] Verdict verdict() const noexcept;
};

// "fans.feasibility@1.0.0"
[[nodiscard]] core::ModelId feasibility_model();

// Runs the L0 checks for one duty (SEL-004). Invalid inputs return out_of_validity (subjects as in
// l0.hpp); a sourced range with non-finite bounds or not 0 < min <= max returns invalid_input with
// subject "family_range".
[[nodiscard]] core::Result<FeasibilityReport> check_feasibility(
    const Duty& duty, const physics::Air& air, const FamilyRange& range,
    std::optional<core::AngularVelocity> omega = std::nullopt,
    std::optional<core::Length> d_tip = std::nullopt);

}  // namespace cemkit::fans

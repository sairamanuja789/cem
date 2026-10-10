#pragma once

// fans.axial_ducted: the 120 mm ducted axial fan family (T14; FAM-001, SPEC-006, SEL-002,
// SEL-004). Minimal registration until the axial L1 milestone:
// - essential fields: product.duty.flow and product.duty.pressure (ADR-003 D8, AX-003, AX-004);
// - feasibility: the T08 L0 gate with this family's specific-speed range, which is UNSOURCED
//   (data/fans/family_ranges.yaml, HI-003): the gate reports "range unsourced";
// - no parameter space, defaults or ranges are invented; initial design, L1 evaluation,
//   constraints, geometry recipe and simulation case return not_implemented (HI-014).

#include <memory>
#include <optional>
#include <string_view>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/physics/air.hpp"
#include "cemkit/product/family.hpp"
#include "cemkit/product/registry.hpp"
#include "products/fans/common/feasibility.hpp"

namespace cemkit::fans::axial_ducted {

inline constexpr std::string_view k_family_id = "fans.axial_ducted";

[[nodiscard]] core::Result<std::unique_ptr<const product::Family>> make_family();

// The family's registration function (ADR-009).
[[nodiscard]] core::Status register_family(product::Registry& registry);

// The family's specific-speed range for the T08 gate: UNSOURCED, no bounds (CLAUDE.md rule 7).
[[nodiscard]] FamilyRange specific_speed_range();

// The T08 L0 feasibility gate for this family (SEL-004).
[[nodiscard]] core::Result<FeasibilityReport> check_feasibility(
    const Duty& duty, const physics::Air& air,
    std::optional<core::AngularVelocity> omega = std::nullopt,
    std::optional<core::Length> d_tip = std::nullopt);

}  // namespace cemkit::fans::axial_ducted

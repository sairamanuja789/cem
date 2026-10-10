#pragma once

// fans.axial_ducted: the 120 mm ducted axial fan family (T14; FAM-001, SPEC-006, SEL-002,
// SEL-004). Minimal registration until the axial L1 milestone:
// - essential fields: product.duty.flow and product.duty.pressure (ADR-003 D8, AX-003, AX-004);
// - feasibility: the T08 L0 gate with this family's specific-speed range, which is UNSOURCED
//   (data/fans/family_ranges.yaml, HI-003): the gate reports "range unsourced";
// - no parameter space, defaults or ranges are invented; initial design, L1 evaluation,
//   constraints, geometry recipe and simulation case return not_implemented (HI-014);
// - rotor tip diameter from the spec (owner decision D6, AX-001, AX-009): provisional default.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/physics/air.hpp"
#include "cemkit/product/family.hpp"
#include "cemkit/product/registry.hpp"
#include "cemkit/spec/spec.hpp"
#include "products/fans/common/feasibility.hpp"

namespace cemkit::fans::axial_ducted {

inline constexpr std::string_view k_family_id = "fans.axial_ducted";

[[nodiscard]] core::Result<std::unique_ptr<const product::Family>> make_family();

// The family's registration function (ADR-009).
[[nodiscard]] core::Status register_family(product::Registry& registry);

// The family's specific-speed range for the T08 gate: UNSOURCED, no bounds (CLAUDE.md rule 7).
[[nodiscard]] FamilyRange specific_speed_range();

// A rotor tip diameter derived from the spec, with where it came from (owner decision D6).
struct DerivedTipDiameter {
  core::Length value;
  core::Provenance provenance{core::Provenance::default_value};
  bool provisional{true};
  std::string_view rule;
  std::vector<std::string> from;  // the spec fields it was computed from
};

inline constexpr std::string_view k_tip_from_duct_rule =
    "D_tip = D_duct - 2 c_tip (owner decision D6, 2026-10-10; AX-001, AX-009)";
inline constexpr std::string_view k_tip_stated_rule = "D_tip = nominal_size (AX-001, stated)";

// The rotor tip diameter for the feasibility gate's tip-speed check (owner decision D6,
// provisional; open item: owner to confirm frame vs rotor diameter, AX-001):
// - product.size_reference = duct_inner_diameter: D_tip = product.nominal_size
//   - 2 x product.tip_clearance_min, provenance default, provisional;
// - product.size_reference = rotor_tip_diameter: D_tip = product.nominal_size, with that field's
//   provenance (provisional if either field is);
// - frame or unknown size reference, or an unknown nominal size or tip clearance: nullopt, so
//   the tip-speed check stays not computable. No clearance is ever assumed.
// A non-positive nominal size is rejected (spec_rejected, product.nominal_size); a negative
// clearance or one of half the duct diameter or more is rejected (product.tip_clearance_min).
[[nodiscard]] core::Result<std::optional<DerivedTipDiameter>> rotor_tip_diameter(
    const spec::Spec& spec);

// The T08 L0 feasibility gate for this family (SEL-004).
[[nodiscard]] core::Result<FeasibilityReport> check_feasibility(
    const Duty& duty, const physics::Air& air,
    std::optional<core::AngularVelocity> omega = std::nullopt,
    std::optional<core::Length> d_tip = std::nullopt);

}  // namespace cemkit::fans::axial_ducted

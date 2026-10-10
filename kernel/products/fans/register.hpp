#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/physics/air.hpp"
#include "cemkit/product/registry.hpp"
#include "cemkit/spec/spec.hpp"
#include "products/fans/common/feasibility.hpp"

namespace cemkit::fans {

// Registers every fan family, in a fixed order (ADR-009). Called once by whoever owns the registry
// (the C ABI at start-up; tests).
[[nodiscard]] core::Status register_fan_families(product::Registry& registry);

// SEL-004: the L0 feasibility hook of the fan family `family`, so that callers (the C ABI) never
// name a family (FAM-002). not_implemented with subject "family" if the family has no hook.
[[nodiscard]] core::Result<FeasibilityReport> check_family_feasibility(
    std::string_view family, const Duty& duty, const physics::Air& air,
    std::optional<core::AngularVelocity> omega, std::optional<core::Length> d_tip);

// A length a family derives from a compiled spec, with where it came from (owner decision D6).
struct SpecDerivedLength {
  core::Length value;
  core::Provenance provenance{core::Provenance::default_value};
  bool provisional{true};
  std::string rule;
  std::vector<std::string> from;  // the spec fields it was computed from
};

// AX-001, AX-009 (owner decision D6, provisional): the rotor tip diameter the family derives from
// the spec, for the feasibility gate's tip-speed check, so that callers (the C ABI) never name a
// family (FAM-002). nullopt if the family derives none from this spec (the tip-speed check then
// stays not computable); a spec_rejected error names an inconsistent field.
[[nodiscard]] core::Result<std::optional<SpecDerivedLength>> family_rotor_tip_diameter(
    std::string_view family, const spec::Spec& spec);

}  // namespace cemkit::fans

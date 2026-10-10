#pragma once

#include <optional>
#include <string_view>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/physics/air.hpp"
#include "cemkit/product/registry.hpp"
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

}  // namespace cemkit::fans

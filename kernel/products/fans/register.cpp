#include "products/fans/register.hpp"

#include <string>

#include "products/fans/families/axial_ducted/axial_ducted.hpp"

namespace cemkit::fans {

// One call per folder under families/, in this fixed order (ADR-009).
// kernel/testing/family_registry fails if a folder under families/ is not registered.
core::Status register_fan_families(product::Registry& registry) {
  return axial_ducted::register_family(registry);
}

core::Result<FeasibilityReport> check_family_feasibility(std::string_view family, const Duty& duty,
                                                         const physics::Air& air,
                                                         std::optional<core::AngularVelocity> omega,
                                                         std::optional<core::Length> d_tip) {
  if (family == axial_ducted::k_family_id) {
    return axial_ducted::check_feasibility(duty, air, omega, d_tip);
  }
  return core::fail(core::ErrorCode::not_implemented, "no feasibility hook for this family",
                    "family", {{"family", std::string{family}}});
}

}  // namespace cemkit::fans

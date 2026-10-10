#include "products/fans/register.hpp"

#include "products/fans/families/axial_ducted/axial_ducted.hpp"

namespace cemkit::fans {

// One call per folder under families/, in this fixed order (ADR-009).
// kernel/testing/family_registry fails if a folder under families/ is not registered.
core::Status register_fan_families(product::Registry& registry) {
  return axial_ducted::register_family(registry);
}

}  // namespace cemkit::fans

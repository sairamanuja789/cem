#include "products/fans/register.hpp"

namespace cemkit::fans {

// One call per folder under families/, in this fixed order (ADR-009). No fan family exists yet; the
// first one (axial_ducted) adds its call here. kernel/testing/family_registry fails if a folder
// under families/ is not registered.
core::Status register_fan_families(product::Registry& /*registry*/) { return {}; }

}  // namespace cemkit::fans

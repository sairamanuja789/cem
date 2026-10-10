#pragma once

#include "cemkit/core/result.hpp"
#include "cemkit/product/registry.hpp"

namespace cemkit::fans {

// Registers every fan family, in a fixed order (ADR-009). Called once by whoever owns the registry
// (the C ABI at start-up; tests).
[[nodiscard]] core::Status register_fan_families(product::Registry& registry);

}  // namespace cemkit::fans

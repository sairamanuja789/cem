#pragma once

#include "cemkit/core/result.hpp"
#include "cemkit/product/operations.hpp"

namespace cemkit::fans {

// The fan L0 functions and the L0 feasibility gate as named operations for the C ABI's
// cemkit_l0_batch and cemkit_feasibility (PERF-002, PHY-005, SEL-004; ADR-015 rule 5), and the
// models they use (PHY-004). Case formats: docs/capi.md.
[[nodiscard]] core::Status register_operations(product::Operations& operations);

}  // namespace cemkit::fans

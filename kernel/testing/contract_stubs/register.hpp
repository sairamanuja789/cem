#pragma once

#include "cemkit/core/result.hpp"
#include "cemkit/product/registry.hpp"

namespace cemkit::testing::contract_stubs {

// Registers every contract stub family in a fixed order (ADR-009). Test fixtures (ADR-015).
[[nodiscard]] core::Status register_contract_stub_families(product::Registry& registry);

}  // namespace cemkit::testing::contract_stubs

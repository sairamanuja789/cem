#pragma once

#include "cemkit/core/result.hpp"
#include "cemkit/product/registry.hpp"

namespace cemkit::testing::stub_product {

// Registers every stub family in a fixed order (ADR-009), like products/fans/register.cpp.
[[nodiscard]] core::Status register_stub_product_families(product::Registry& registry);

}  // namespace cemkit::testing::stub_product

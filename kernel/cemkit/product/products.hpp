#pragma once

#include "cemkit/core/result.hpp"
#include "cemkit/product/operations.hpp"
#include "cemkit/product/registry.hpp"

namespace cemkit::product {

// Registers every compiled-in product: for each kernel/products/<product>/, in folder-name order,
// <product>::register_product(registry, operations). Defined in a source file generated at
// configure time (kernel/cmake/cemkit.cmake, cemkit_add_products) into the cemkit_products target,
// so no platform source names a product (ADR-015 rule 5).
[[nodiscard]] core::Status register_products(Registry& registry, Operations& operations);

}  // namespace cemkit::product

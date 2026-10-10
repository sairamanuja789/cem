#pragma once

#include <memory>
#include <string_view>

#include "cemkit/core/result.hpp"
#include "cemkit/product/family.hpp"
#include "cemkit/product/registry.hpp"

namespace cemkit::testing::contract_stubs::stub_ceiling {

// A ceiling-fan stub: an air-delivery duty and an input power; no pressure rise, no duct.
// Every field, list and message here is a TEST PARAMETER, not engineering data.
inline constexpr std::string_view k_family_id = "contract_stubs.stub_ceiling";

[[nodiscard]] core::Result<std::unique_ptr<const product::Family>> make_family();
[[nodiscard]] core::Status register_family(product::Registry& registry);

}  // namespace cemkit::testing::contract_stubs::stub_ceiling

#pragma once

#include <memory>

#include "cemkit/core/result.hpp"
#include "cemkit/product/family.hpp"
#include "cemkit/product/registry.hpp"

namespace cemkit::testing::stub_product::stub_family {

// A family plugin used only by tests (FAM-001, FAM-002, FAM-003, SPEC-006). Every number, field
// list, bound and default in it is a TEST PARAMETER chosen to exercise the interface. None of it is
// engineering data, and every source string says so and is marked UNSOURCED.
inline constexpr std::string_view k_family_id = "stub_product.stub_family";

[[nodiscard]] core::Result<std::unique_ptr<const product::Family>> make_family();

// The family's registration function (ADR-009): adds one StubFamily to the registry.
[[nodiscard]] core::Status register_family(product::Registry& registry);

}  // namespace cemkit::testing::stub_product::stub_family

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/geometry/port/backend.hpp"
#include "cemkit/product/json_io.hpp"
#include "cemkit/simulation/port/case.hpp"

namespace cemkit::product {

// A named operation a product offers through the C ABI's batch and feasibility calls (ADR-015 rule
// 5), so the C ABI never names a product. It takes one case ({"function", "args", "air", ...};
// docs/capi.md) and returns the case's result members or the model's classified error; a
// malformed case throws BadRequest.
// What the caller (the C ABI) lends an operation: the geometry backend it must build with, so that
// products never depend on an engine (ADR-009).
struct OperationContext {
  const geometry::port::GeometryBackend& geometry;
  // The solver case writer the C ABI lends (M5); null where no writer is linked.
  const simulation::port::CaseWriter* case_writer = nullptr;
};

using Operation =
    std::function<core::Result<Json>(const Json& case_, const OperationContext& context)>;

// The operations and model identities the compiled-in products register (ADR-015 rule 5). Like the
// Registry it is owned by the caller; there is no global table.
class Operations {
 public:
  // invalid_input if the name is empty or already taken, or the operation is empty.
  [[nodiscard]] core::Status add(std::string name, Operation operation);
  // nullptr if no product registered this name.
  [[nodiscard]] const Operation* find(std::string_view name) const noexcept;
  // A model the products' operations use, reported with every response (PHY-004); invalid_input if
  // its name is empty or already listed.
  [[nodiscard]] core::Status add_model(core::ModelId model);
  [[nodiscard]] const std::vector<core::ModelId>& models() const noexcept { return models_; }

 private:
  std::vector<std::pair<std::string, Operation>> operations_;
  std::vector<core::ModelId> models_;
};

}  // namespace cemkit::product

#include "cemkit/product/operations.hpp"

#include <algorithm>
#include <utility>

namespace cemkit::product {

core::Status Operations::add(std::string name, Operation operation) {
  if (name.empty() || !operation || find(name) != nullptr) {
    return core::fail(core::ErrorCode::invalid_input,
                      "an operation needs a unique, non-empty name and a body", "operation",
                      {{"operation", name}});
  }
  operations_.emplace_back(std::move(name), std::move(operation));
  return {};
}

const Operation* Operations::find(std::string_view name) const noexcept {
  const auto it =
      std::ranges::find_if(operations_, [name](const auto& op) { return op.first == name; });
  return it == operations_.end() ? nullptr : &it->second;
}

core::Status Operations::add_model(core::ModelId model) {
  const bool taken = std::ranges::any_of(
      models_, [&model](const core::ModelId& m) { return m.name == model.name; });
  if (model.name.empty() || taken) {
    return core::fail(core::ErrorCode::invalid_input, "a model needs a unique, non-empty name",
                      "model", {{"model", model.name}});
  }
  models_.push_back(std::move(model));
  return {};
}

}  // namespace cemkit::product

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"
#include "cemkit/product/family.hpp"
#include "cemkit/spec/essential.hpp"

namespace cemkit::product {

// The set of compiled-in family plugins (FAM-001, FAM-002). Registration is static in the sense of
// compile-time linked, and explicit (ADR-009): each family exposes `register_family(Registry&)`,
// each product's register.cpp calls its families in a fixed order, and the caller owns the
// registry. There is no global registry and no static self-registration.
class Registry {
 public:
  // Adds a family. invalid_input if the pointer is empty, the id is not "<product>.<family>"
  // (lower-case letters, digits and '_', one '.'), the id is already registered, or an essential
  // field path is empty or listed twice, or a feasible-range limit has no side set, a non-finite
  // or inverted bound, or no source.
  [[nodiscard]] core::Status add(std::unique_ptr<const Family> family);

  // spec_rejected with subject "family" if no family has this id.
  [[nodiscard]] core::Result<std::reference_wrapper<const Family>> get(std::string_view id) const;
  [[nodiscard]] bool contains(std::string_view id) const noexcept;

  // Family ids in registration order.
  [[nodiscard]] std::vector<std::string> ids() const;
  [[nodiscard]] std::size_t size() const noexcept { return families_.size(); }

  // SPEC-006: the essential fields the family plugin declares; spec_rejected naming "family" if
  // the family is not registered.
  [[nodiscard]] core::Result<std::vector<std::string>> essential_fields(std::string_view id) const;

 private:
  std::vector<std::unique_ptr<const Family>> families_;
};

// The spec compiler's essential-field resolver backed by a registry (SPEC-006): the compiler asks
// the family plugin, not a global list. The resolver shares ownership of the registry, so a
// compiled spec that keeps its options (SPEC-009) can never outlive it.
[[nodiscard]] spec::EssentialFieldResolver essential_field_resolver(
    std::shared_ptr<const Registry> registry);

}  // namespace cemkit::product

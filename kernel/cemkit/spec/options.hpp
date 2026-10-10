#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"
#include "cemkit/spec/catalogue.hpp"
#include "cemkit/spec/essential.hpp"
#include "cemkit/spec/field.hpp"

namespace cemkit::spec {

class Spec;

// The family's own contradiction rules for a compiled spec (SPEC-010, ADR-015): an error rejects
// the spec. The platform has none of its own.
using SpecCheckResolver = std::function<core::Result<void>(const Spec& spec)>;

using DefaultFieldResolver =
    std::function<std::optional<Field>(std::string_view family, std::string_view path)>;

struct CompilerOptions {
  bool autonomous_mode{false};
  // Required: compile() fails with invalid_input while it is empty (no built-in list, SPEC-006).
  EssentialFieldResolver essential_resolver{};
  DefaultFieldResolver default_resolver{};
  // Required: compile() fails with invalid_input while it is empty (no built-in field list,
  // ADR-015). product::compiler_options() supplies registry-backed resolvers.
  FieldCatalogueResolver catalogue_resolver{};
  // Optional: no family rules are applied while it is empty.
  SpecCheckResolver spec_checks{};
};

}  // namespace cemkit::spec

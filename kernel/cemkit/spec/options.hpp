#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "cemkit/spec/essential.hpp"
#include "cemkit/spec/field.hpp"

namespace cemkit::spec {

using DefaultFieldResolver =
    std::function<std::optional<Field>(std::string_view family, std::string_view path)>;

struct CompilerOptions {
  bool autonomous_mode{false};
  // Required: compile() fails with invalid_input while it is empty (no built-in list, SPEC-006).
  EssentialFieldResolver essential_resolver{};
  DefaultFieldResolver default_resolver{};
};

}  // namespace cemkit::spec

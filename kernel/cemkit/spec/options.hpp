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
  EssentialFieldResolver essential_resolver{default_essential_fields};
  DefaultFieldResolver default_resolver{};
};

}  // namespace cemkit::spec

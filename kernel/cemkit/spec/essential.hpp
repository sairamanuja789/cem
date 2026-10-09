#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cemkit::spec {

// Declares essential fields for a family (SPEC-006). Stubbed until T09.
using EssentialFieldResolver = std::function<std::vector<std::string>(std::string_view family)>;

[[nodiscard]] std::vector<std::string> default_essential_fields(std::string_view family);

}  // namespace cemkit::spec

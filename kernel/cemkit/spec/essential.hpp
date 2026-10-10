#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"

namespace cemkit::spec {

// Declares the essential fields of a family (SPEC-006): the compiler asks the family plugin, not a
// global list. The platform has no built-in list; product::essential_field_resolver() supplies one
// backed by the family registry (ADR-012). An error (for example an unknown family) rejects the
// spec with that error.
using EssentialFieldResolver =
    std::function<core::Result<std::vector<std::string>>(std::string_view family)>;

}  // namespace cemkit::spec

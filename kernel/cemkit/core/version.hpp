#pragma once

#include <string_view>

namespace cemkit::core {

// Kernel version (semantic versioning, MAINT-005); taken from the CMake project version.
[[nodiscard]] std::string_view kernel_version() noexcept;

}  // namespace cemkit::core

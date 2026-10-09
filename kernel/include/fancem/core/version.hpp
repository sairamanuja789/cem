#pragma once

#include <string_view>

namespace fancem::core {

// Kernel version (semantic versioning, MAINT-005); taken from the CMake project version.
[[nodiscard]] std::string_view kernel_version() noexcept;

}  // namespace fancem::core

#pragma once

#include <compare>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "cemkit/core/error.hpp"

namespace cemkit::core {

// Kernel version (semantic versioning, MAINT-005); taken from the CMake project version. No commit
// hash or build time is compiled in, so identical sources give identical binaries.
[[nodiscard]] std::string_view kernel_version() noexcept;

// MAJOR.MINOR.PATCH (semver.org 2.0.0 core form; pre-release and build suffixes are not used).
struct SemVer {
  std::uint32_t major;
  std::uint32_t minor;
  std::uint32_t patch;

  auto operator<=>(const SemVer&) const = default;
};

// Strict parse: three decimal numbers without leading zeros, each fitting in 32 bits. Anything else
// is invalid_input with subject "version".
[[nodiscard]] std::expected<SemVer, Error> parse_semver(std::string_view text);
[[nodiscard]] std::string to_string(SemVer version);

// Identity of the model (or measurement procedure) that produced a value (PHY-004): changing an
// equation bumps the version.
struct ModelId {
  std::string name;
  SemVer version;

  auto operator<=>(const ModelId&) const = default;
};

// "<name>@<major>.<minor>.<patch>"
[[nodiscard]] std::string to_string(const ModelId& model);

}  // namespace cemkit::core

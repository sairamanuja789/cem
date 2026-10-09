#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cemkit::core {

// Provenance classes of spec fields (requirements section 2). Stored and exchanged by name.
// Precedence between classes is applied by the spec compiler, not here.
enum class Provenance : std::uint8_t {
  user = 0,
  image = 1,
  derived = 2,
  default_value = 3,  // name "default"; `default` is a C++ keyword
  unknown = 4,
};

struct ProvenanceInfo {
  Provenance provenance;
  std::string_view name;
};

inline constexpr std::array k_provenance_classes{
    ProvenanceInfo{Provenance::user, "user"},
    ProvenanceInfo{Provenance::image, "image"},
    ProvenanceInfo{Provenance::derived, "derived"},
    ProvenanceInfo{Provenance::default_value, "default"},
    ProvenanceInfo{Provenance::unknown, "unknown"},
};

[[nodiscard]] constexpr std::string_view to_string(Provenance provenance) noexcept {
  for (const auto& info : k_provenance_classes) {
    if (info.provenance == provenance) {
      return info.name;
    }
  }
  return {};
}

[[nodiscard]] constexpr std::optional<Provenance> parse_provenance(std::string_view name) noexcept {
  for (const auto& info : k_provenance_classes) {
    if (info.name == name) {
      return info.provenance;
    }
  }
  return std::nullopt;
}

}  // namespace cemkit::core

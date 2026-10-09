#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cemkit::core {

// Fidelity labels (requirements section 2, COR-003, REP-001). Stored and exchanged by name; the
// report label is the wording used in reports. What each level claims, and the uncertainty
// convention for L2 verified and L3 validated: docs/models/platform/fidelity-and-provenance.md.
enum class Fidelity : std::uint8_t {
  l0_predicted = 0,
  l1_predicted = 1,
  l2_simulated = 2,
  l2_verified = 3,
  l3_validated = 4,
};

struct FidelityInfo {
  Fidelity level;
  std::string_view name;
  std::string_view report_label;
  bool requires_uncertainty;
};

inline constexpr std::array k_fidelity_levels{
    FidelityInfo{Fidelity::l0_predicted, "l0_predicted", "L0 predicted", false},
    FidelityInfo{Fidelity::l1_predicted, "l1_predicted", "L1 predicted", false},
    FidelityInfo{Fidelity::l2_simulated, "l2_simulated", "L2 simulated", false},
    FidelityInfo{Fidelity::l2_verified, "l2_verified", "L2 verified", true},
    FidelityInfo{Fidelity::l3_validated, "l3_validated", "L3 validated", true},
};

namespace detail {
[[nodiscard]] constexpr const FidelityInfo* find_fidelity(Fidelity level) noexcept {
  for (const auto& info : k_fidelity_levels) {
    if (info.level == level) {
      return &info;
    }
  }
  return nullptr;
}
}  // namespace detail

[[nodiscard]] constexpr std::string_view to_string(Fidelity level) noexcept {
  const auto* info = detail::find_fidelity(level);
  return info != nullptr ? info->name : std::string_view{};
}

[[nodiscard]] constexpr std::string_view report_label(Fidelity level) noexcept {
  const auto* info = detail::find_fidelity(level);
  return info != nullptr ? info->report_label : std::string_view{};
}

[[nodiscard]] constexpr bool requires_uncertainty(Fidelity level) noexcept {
  const auto* info = detail::find_fidelity(level);
  return info != nullptr && info->requires_uncertainty;
}

[[nodiscard]] constexpr std::optional<Fidelity> parse_fidelity(std::string_view name) noexcept {
  for (const auto& info : k_fidelity_levels) {
    if (info.name == name) {
      return info.level;
    }
  }
  return std::nullopt;
}

}  // namespace cemkit::core

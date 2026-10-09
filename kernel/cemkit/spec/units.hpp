#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "cemkit/core/result.hpp"

namespace cemkit::spec {

enum class QuantityKind {
  length,
  volume_flow_rate,
  pressure,
  angular_velocity,
  power,
  density,
  temperature,
  dynamic_viscosity,
  ratio,
};

[[nodiscard]] std::string_view to_string(QuantityKind kind) noexcept;
[[nodiscard]] std::optional<QuantityKind> parse_quantity_kind(std::string_view name) noexcept;

[[nodiscard]] bool is_known_unit(std::string_view unit) noexcept;
[[nodiscard]] std::optional<QuantityKind> kind_of_unit(std::string_view unit) noexcept;
[[nodiscard]] std::string_view coherent_si_unit(QuantityKind kind) noexcept;

// Converts value in `unit` to coherent SI unit for `kind` (SPEC-002, SPEC-003).
// If unit is not known at all: returns unknown_unit error with field named in subject.
// If unit is known but has wrong dimension: returns unit_mismatch error with field named in
// subject.
[[nodiscard]] core::Result<double> convert_to_si(QuantityKind kind, std::string_view unit,
                                                 double value, std::string_view field_name = {});

// Converts tolerance value (delta or difference). For offset units like degC, delta does not add
// 273.15.
[[nodiscard]] core::Result<double> convert_tolerance_to_si(QuantityKind kind, std::string_view unit,
                                                           double value,
                                                           std::string_view field_name = {});

}  // namespace cemkit::spec

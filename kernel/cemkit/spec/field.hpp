#pragma once

#include <optional>
#include <string>

#include "cemkit/core/provenance.hpp"

namespace cemkit::spec {

struct Tolerance {
  enum class Type { absolute, relative };
  Type type{Type::absolute};
  double minus{0.0};  // in SI for absolute, or ratio for relative
  double plus{0.0};   // in SI for absolute, or ratio for relative
  double original_minus{0.0};
  double original_plus{0.0};
  std::string unit;  // original unit for absolute tolerance

  bool operator==(const Tolerance&) const = default;
};

struct Field {
  std::string path;
  std::optional<double> original_value;
  std::optional<std::string> original_unit;
  std::optional<double> si_value;
  std::optional<std::string> si_unit;
  std::optional<std::string> text_value;  // for text fields like process, material
  core::Provenance provenance{core::Provenance::unknown};
  std::optional<double> confidence;
  bool provisional{false};
  std::optional<Tolerance> tolerance;
  std::optional<std::string> note;
  std::optional<std::string> pressure_kind;  // "fan_total" or "fan_static"

  bool operator==(const Field&) const = default;
};

}  // namespace cemkit::spec

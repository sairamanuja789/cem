#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"
#include "cemkit/spec/units.hpp"

namespace cemkit::product {

// The literal a source string starts with when no verified source is known (CLAUDE.md rule 7). Such
// values are allowed but reported by ParameterSpace::unsourced_defaults().
inline constexpr std::string_view k_unsourced = "UNSOURCED";

// A parameter's default value, in the coherent SI unit of the parameter's kind, with its source.
// A default without a source is rejected; "UNSOURCED..." is accepted and reported.
struct ParameterDefault {
  double si_value{0.0};
  std::string source;

  bool operator==(const ParameterDefault&) const = default;
};

// One named design parameter (FAM-003). Bounds and default are in the coherent SI unit of `kind`
// (spec::coherent_si_unit); the unit is therefore published with the parameter, not stored apart.
// Raw doubles are used here, as in spec::Field, because a parameter space is heterogeneous: every
// value is paired with its kind and converted to an mp-units quantity where it is used.
struct Parameter {
  std::string name;
  spec::QuantityKind kind{spec::QuantityKind::ratio};
  double lower{0.0};
  double upper{0.0};
  std::optional<ParameterDefault> default_value;

  [[nodiscard]] std::string_view unit() const noexcept { return spec::coherent_si_unit(kind); }

  bool operator==(const Parameter&) const = default;
};

// A value for one parameter, in the coherent SI unit of that parameter's kind.
struct ParameterValue {
  std::string name;
  double si_value{0.0};

  bool operator==(const ParameterValue&) const = default;
};

// A candidate design: one value per parameter, in the order of the parameter space.
struct Design {
  std::vector<ParameterValue> values;

  bool operator==(const Design&) const = default;
};

// The published parameters of a family (FAM-003): what an optimizer reads bounds from. Immutable
// once created; parameter order is the order given to create() and is kept everywhere.
class ParameterSpace {
 public:
  // Validates every parameter: a non-empty name used once; finite bounds with lower < upper; a
  // default, if any, finite, within the bounds and with a non-empty source. Failures are
  // invalid_input with the parameter name as subject.
  [[nodiscard]] static core::Result<ParameterSpace> create(std::vector<Parameter> parameters);

  [[nodiscard]] const std::vector<Parameter>& parameters() const noexcept { return parameters_; }
  [[nodiscard]] std::size_t size() const noexcept { return parameters_.size(); }
  [[nodiscard]] std::optional<Parameter> find(std::string_view name) const;

  // Names of parameters whose default is marked UNSOURCED, in parameter order (rule 7: reported).
  [[nodiscard]] std::vector<std::string> unsourced_defaults() const;

  // A design must give every parameter exactly once, in parameter order, with a finite value inside
  // the bounds. Failures are invalid_input (malformed design) or out_of_validity (value outside the
  // bounds), with the parameter name as subject.
  [[nodiscard]] core::Status check(const Design& design) const;

  // The design made of every parameter's default; invalid_input naming the first parameter that
  // has no default.
  [[nodiscard]] core::Result<Design> default_design() const;

  bool operator==(const ParameterSpace&) const = default;

 private:
  explicit ParameterSpace(std::vector<Parameter> parameters) : parameters_{std::move(parameters)} {}

  std::vector<Parameter> parameters_;
};

}  // namespace cemkit::product

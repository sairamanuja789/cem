#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/fidelity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/product/parameter_space.hpp"
#include "cemkit/spec/spec.hpp"
#include "cemkit/spec/units.hpp"

namespace cemkit::product {

// One computed quantity of a design (rule 5: it carries its fidelity label and model identity).
// The value is in the coherent SI unit of `kind`.
struct Metric {
  std::string name;
  spec::QuantityKind kind{spec::QuantityKind::ratio};
  double si_value{0.0};
  // No default: every metric states its label (rule 5); omitting it fails to compile
  // (-Wmissing-field-initializers under -Werror).
  core::Fidelity fidelity;
  core::ModelId model;

  bool operator==(const Metric&) const = default;
};

// Result of a family's L1 evaluation of one design.
struct Evaluation {
  std::vector<Metric> metrics;

  bool operator==(const Evaluation&) const = default;
};

// One evaluated constraint: `value` compared with `limit`, both in the coherent SI unit of `kind`.
// `source` cites where the limit comes from; "UNSOURCED..." when no verified source is known.
struct ConstraintResult {
  enum class Sense { at_most, at_least };

  std::string name;
  spec::QuantityKind kind{spec::QuantityKind::ratio};
  Sense sense{Sense::at_most};
  double value{0.0};
  double limit{0.0};
  core::Fidelity fidelity;  // no default, as for Metric
  std::string source;

  [[nodiscard]] bool satisfied() const noexcept {
    return sense == Sense::at_most ? value <= limit : value >= limit;
  }

  bool operator==(const ConstraintResult&) const = default;
};

// One bound of a family's feasible range (for example a specific-speed band, SEL-002), in the
// coherent SI unit of `kind`. At least one side is set. `source` cites the bound, or starts with
// "UNSOURCED" (CLAUDE.md rule 7), in which case users of the range report "range unsourced".
struct RangeLimit {
  std::string quantity;
  spec::QuantityKind kind{spec::QuantityKind::ratio};
  std::optional<double> lower;
  std::optional<double> upper;
  std::string source;

  bool operator==(const RangeLimit&) const = default;
};

struct FeasibleRange {
  std::vector<RangeLimit> limits;

  bool operator==(const FeasibleRange&) const = default;
};

// What the geometry backend is asked to build for a design. Provisional (ADR-012): a named,
// versioned recipe with named SI values, until the geometry port (T10) defines typed operations.
struct GeometryRecipe {
  core::ModelId recipe;
  std::vector<ParameterValue> values;

  bool operator==(const GeometryRecipe&) const = default;
};

// What the simulation layer is asked to run for a design. Provisional (ADR-012) in the same way,
// until the simulation port defines case types.
struct SimulationCase {
  core::ModelId case_template;
  std::vector<ParameterValue> settings;

  bool operator==(const SimulationCase&) const = default;
};

// A product family plugin (FAM-001). Every family implements this one interface; the platform and
// the orchestration only ever see it. Implementations are stateless after construction, and every
// operation is pure and deterministic: the same inputs give the same outputs.
//
// Identity: id() is "<product>.<family>", the product and family folder names (for example
// "fans.axial_ducted"); it is also the value of a spec's "family" field.
class Family {
 public:
  Family() = default;
  Family(const Family&) = delete;
  Family& operator=(const Family&) = delete;
  Family(Family&&) = delete;
  Family& operator=(Family&&) = delete;
  virtual ~Family() = default;

  [[nodiscard]] virtual std::string_view id() const noexcept = 0;
  // Plugin name and semantic version, recorded with every result (PHY-004).
  [[nodiscard]] virtual core::ModelId plugin() const = 0;

  // FAM-003: named parameters with units, bounds and defaults; the optimizer reads bounds here.
  [[nodiscard]] virtual const ParameterSpace& parameter_space() const noexcept = 0;
  // SPEC-006: spec field paths that must be known before a campaign can start.
  [[nodiscard]] virtual std::vector<std::string> essential_fields() const = 0;
  // Duty range the family is fit for (SEL-002).
  [[nodiscard]] virtual FeasibleRange feasible_range() const = 0;

  [[nodiscard]] virtual core::Result<Design> initial_design(const spec::Spec& spec) const = 0;
  [[nodiscard]] virtual core::Result<Evaluation> evaluate_l1(const spec::Spec& spec,
                                                             const Design& design) const = 0;
  [[nodiscard]] virtual core::Result<std::vector<ConstraintResult>> check_constraints(
      const spec::Spec& spec, const Design& design, const Evaluation& evaluation) const = 0;
  [[nodiscard]] virtual core::Result<GeometryRecipe> geometry_recipe(
      const Design& design) const = 0;
  [[nodiscard]] virtual core::Result<SimulationCase> simulation_case(
      const spec::Spec& spec, const Design& design) const = 0;
};

}  // namespace cemkit::product

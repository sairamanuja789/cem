#pragma once

#include <mp-units/framework.h>

#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "cemkit/core/error.hpp"
#include "cemkit/core/fidelity.hpp"
#include "cemkit/core/version.hpp"

namespace cemkit::core {

template <class T>
using Result = std::expected<T, Error>;
using Status = std::expected<void, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message,
                                                 std::string subject = {},
                                                 ErrorDetails details = {}) {
  return std::unexpected<Error>{std::in_place, code, std::move(message), std::move(subject),
                                std::move(details)};
}

// A reported quantity with its fidelity label and the identity of the model that produced it
// (COR-003, PHY-004). Only the factories create one, one per fidelity level; L2 verified and
// L3 validated require an uncertainty. There is no arithmetic: unwrap with value(), compute, and
// label the result again with the model that did the computation.
//
// The uncertainty is a half-width in the value's units. Its basis is implied by the label:
// L2 verified = fine-grid GCI (Celik et al. 2008); L3 validated = expanded uncertainty with
// coverage factor k = 2, about 95 % coverage (JCGM 100:2008). See
// docs/models/platform/fidelity-and-provenance.md.
template <mp_units::Quantity Q>
class Labelled {
 public:
  Labelled() = delete;

  [[nodiscard]] static Result<Labelled> l0_predicted(Q value, ModelId model) {
    return make(value, Fidelity::l0_predicted, std::move(model), std::nullopt);
  }
  [[nodiscard]] static Result<Labelled> l1_predicted(Q value, ModelId model) {
    return make(value, Fidelity::l1_predicted, std::move(model), std::nullopt);
  }
  [[nodiscard]] static Result<Labelled> l2_simulated(Q value, ModelId model) {
    return make(value, Fidelity::l2_simulated, std::move(model), std::nullopt);
  }
  [[nodiscard]] static Result<Labelled> l2_verified(Q value, Q uncertainty, ModelId model) {
    return make(value, Fidelity::l2_verified, std::move(model), uncertainty);
  }
  [[nodiscard]] static Result<Labelled> l3_validated(Q value, Q uncertainty, ModelId model) {
    return make(value, Fidelity::l3_validated, std::move(model), uncertainty);
  }

  [[nodiscard]] Q value() const noexcept { return value_; }
  [[nodiscard]] Fidelity fidelity() const noexcept { return fidelity_; }
  [[nodiscard]] const ModelId& model() const noexcept { return model_; }
  // Present exactly for L2 verified and L3 validated.
  [[nodiscard]] std::optional<Q> uncertainty() const noexcept { return uncertainty_; }

  // Written out rather than defaulted: comparing std::optional<Q> members makes libstdc++ 13 try
  // its mixed optional == U overloads, and mp-units' representation concept then recurses (GCC 13
  // error "satisfaction of atomic constraint depends on itself"). Unwrapping avoids those
  // overloads.
  friend bool operator==(const Labelled& a, const Labelled& b) {
    if (a.uncertainty_.has_value() != b.uncertainty_.has_value()) {
      return false;
    }
    return a.value_ == b.value_ && a.fidelity_ == b.fidelity_ && a.model_ == b.model_ &&
           (!a.uncertainty_.has_value() || *a.uncertainty_ == *b.uncertainty_);
  }

 private:
  Labelled(Q value, Fidelity fidelity, ModelId model, std::optional<Q> uncertainty)
      : value_{value}, fidelity_{fidelity}, model_{std::move(model)}, uncertainty_{uncertainty} {}

  static Result<Labelled> make(Q value, Fidelity fidelity, ModelId model,
                               std::optional<Q> uncertainty) {
    if (model.name.empty()) {
      return fail(ErrorCode::invalid_input, "a labelled value needs the name of its model",
                  "model");
    }
    const double number = value.numerical_value_in(Q::unit);
    if (!std::isfinite(number)) {
      return fail(ErrorCode::invalid_input, "a labelled value must be finite", "value",
                  {{"value", format_number(number)}});
    }
    if (uncertainty) {
      const double half_width = uncertainty->numerical_value_in(Q::unit);
      if (!std::isfinite(half_width) || half_width < 0.0) {
        return fail(ErrorCode::invalid_input,
                    "an uncertainty must be a finite, non-negative half-width", "uncertainty",
                    {{"value", format_number(half_width)}});
      }
    }
    return Labelled{value, fidelity, std::move(model), uncertainty};
  }

  Q value_;
  Fidelity fidelity_;
  ModelId model_;
  std::optional<Q> uncertainty_;
};

}  // namespace cemkit::core

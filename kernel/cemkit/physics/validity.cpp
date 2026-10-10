#include "cemkit/physics/validity.hpp"

#include <cmath>
#include <string>

#include "cemkit/core/error.hpp"

namespace cemkit::physics {

namespace {

core::Status out_of_validity(std::string_view subject, std::string_view message, double value,
                             std::string bounds, const core::ModelId& model) {
  return core::fail(core::ErrorCode::out_of_validity, std::string{message}, std::string{subject},
                    {{"bounds", std::move(bounds)},
                     {"model", core::to_string(model)},
                     {"value", core::format_number(value)}});
}

}  // namespace

core::Status require_positive(std::string_view subject, double value, const core::ModelId& model) {
  if (std::isfinite(value) && value > 0.0) {
    return {};
  }
  return out_of_validity(subject, "input must be finite and strictly positive", value,
                         std::string{k_positive_bounds}, model);
}

core::Status require_non_negative(std::string_view subject, double value,
                                  const core::ModelId& model) {
  if (std::isfinite(value) && value >= 0.0) {
    return {};
  }
  return out_of_validity(subject, "input must be finite and non-negative", value,
                         std::string{k_non_negative_bounds}, model);
}

core::Status require_at_most(std::string_view subject, double value, double limit,
                             const core::ModelId& model, std::string_view message) {
  if (value <= limit) {
    return {};
  }
  return out_of_validity(subject, message, value, "(0, " + core::format_number(limit) + "]", model);
}

}  // namespace cemkit::physics

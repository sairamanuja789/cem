#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cemkit::core {

// Failure codes (REL-002). Append-only: a code is never renamed, renumbered or reused; new codes go
// at the end with the next integer. Stored data and the JSON interface use the name; the integer is
// used only across the C ABI, and 0 is never a valid code.
// docs/models/platform/error-codes.md and schemas/cemkit/v1/error-codes.json are generated from
// k_error_codes by scripts/gen_error_codes.py; never edit them by hand.
enum class ErrorCode : std::uint16_t {
  spec_rejected = 1,
  infeasible_requirement = 2,
  out_of_validity = 3,
  geometry_failed = 4,
  mesh_failed = 5,
  sim_untrusted = 6,
  resource_exceeded = 7,
  invalid_input = 8,
  unknown_unit = 9,
  unit_mismatch = 10,
  internal_error = 11,
};

struct ErrorCodeInfo {
  ErrorCode code;
  std::string_view name;
  std::string_view meaning;
};

inline constexpr std::array k_error_codes{
    ErrorCodeInfo{ErrorCode::spec_rejected, "spec_rejected",
                  "The spec failed validation and was not compiled."},
    ErrorCodeInfo{ErrorCode::infeasible_requirement, "infeasible_requirement",
                  "The stated requirements cannot be met together, or not by the selected product "
                  "family."},
    ErrorCodeInfo{ErrorCode::out_of_validity, "out_of_validity",
                  "A model was asked for a value outside its stated validity range; no number is "
                  "returned."},
    ErrorCodeInfo{ErrorCode::geometry_failed, "geometry_failed",
                  "Geometry generation failed or produced an invalid solid."},
    ErrorCodeInfo{ErrorCode::mesh_failed, "mesh_failed",
                  "Meshing failed or the mesh did not pass its quality checks."},
    ErrorCodeInfo{ErrorCode::sim_untrusted, "sim_untrusted",
                  "A simulation ran but its result did not pass the trust gate."},
    ErrorCodeInfo{ErrorCode::resource_exceeded, "resource_exceeded",
                  "A job exceeded its memory, time or disk budget."},
    ErrorCodeInfo{ErrorCode::invalid_input, "invalid_input",
                  "An argument was malformed or outside its domain (for example NaN, a negative "
                  "uncertainty or a malformed version string)."},
    ErrorCodeInfo{ErrorCode::unknown_unit, "unknown_unit",
                  "A unit in the input is not one the spec compiler recognises."},
    ErrorCodeInfo{ErrorCode::unit_mismatch, "unit_mismatch",
                  "A value's unit has the wrong dimension for the field it was given for."},
    ErrorCodeInfo{ErrorCode::internal_error, "internal_error",
                  "A defect in cemkit itself; it should be reported and fixed."},
};

namespace detail {
consteval bool error_table_is_dense() {
  for (std::size_t i = 0; i < k_error_codes.size(); ++i) {
    if (std::to_underlying(k_error_codes.at(i).code) != i + 1) {
      return false;
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (k_error_codes.at(j).name == k_error_codes.at(i).name) {
        return false;
      }
    }
  }
  return true;
}
}  // namespace detail
static_assert(detail::error_table_is_dense(),
              "k_error_codes must list codes 1..N in order, each name once");

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  for (const auto& info : k_error_codes) {
    if (info.code == code) {
      return info.name;
    }
  }
  return {};
}

[[nodiscard]] constexpr std::optional<ErrorCode> parse_error_code(std::string_view name) noexcept {
  for (const auto& info : k_error_codes) {
    if (info.name == name) {
      return info.code;
    }
  }
  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<ErrorCode> error_code_from_int(std::uint16_t value) noexcept {
  for (const auto& info : k_error_codes) {
    if (std::to_underlying(info.code) == value) {
      return info.code;
    }
  }
  return std::nullopt;
}

// Key/value context of an error, sorted by key so that output is deterministic. Conventional keys:
// value, unit, bounds, model. Numbers are written with format_number().
using ErrorDetails = std::map<std::string, std::string, std::less<>>;

// A classified failure: code, human-readable message, subject (the spec field path or model
// parameter concerned; may be empty) and details. Messages must be deterministic: no addresses, no
// locale-dependent formatting.
class Error {
 public:
  Error(ErrorCode code, std::string message, std::string subject = {}, ErrorDetails details = {})
      : code_{code},
        message_{std::move(message)},
        subject_{std::move(subject)},
        details_{std::move(details)} {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] const std::string& subject() const noexcept { return subject_; }
  [[nodiscard]] const ErrorDetails& details() const noexcept { return details_; }

  bool operator==(const Error&) const = default;

 private:
  ErrorCode code_;
  std::string message_;
  std::string subject_;
  ErrorDetails details_;
};

// "<code> at <subject>: <message> (<key>=<value>, ...)"; the subject and details parts are omitted
// when empty.
[[nodiscard]] std::string describe(const Error& error);

// Shortest decimal text that round-trips to the same double, independent of the locale ("nan",
// "inf" and "-inf" for non-finite values).
[[nodiscard]] std::string format_number(double value);

}  // namespace cemkit::core

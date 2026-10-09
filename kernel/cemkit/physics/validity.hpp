#pragma once

// Validity checks shared by the physics models (PHY-002, PHY-003; ADR-003 D3, D6). A failed check
// is an out_of_validity error: subject = the input or quantity concerned; details value, bounds and
// model, numbers written with core::format_number. The Python mirror is
// python/cemkit/reference/validity.py; the texts must stay byte-identical (T08 cross-check).
//
// The checks act on SI numerical values: a caller unwraps a quantity in its coherent SI unit first
// (numerical_value_in(<unit>)), so the bounds text is in that unit.

#include <string_view>

#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"

namespace cemkit::physics {

inline constexpr std::string_view k_positive_bounds = "(0, inf)";
inline constexpr std::string_view k_non_negative_bounds = "[0, inf)";

// OK if value is finite and strictly positive.
[[nodiscard]] core::Status require_positive(std::string_view subject, double value,
                                            const core::ModelId& model);

// OK if value is finite and >= 0 (so -0.0 is accepted, as 0.0).
[[nodiscard]] core::Status require_non_negative(std::string_view subject, double value,
                                                const core::ModelId& model);

// OK if value <= limit; otherwise bounds "(0, <limit>]" and the given message.
[[nodiscard]] core::Status require_at_most(std::string_view subject, double value, double limit,
                                           const core::ModelId& model, std::string_view message);

}  // namespace cemkit::physics

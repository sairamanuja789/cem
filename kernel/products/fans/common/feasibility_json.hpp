#pragma once

// JSON encoding of the fan L0 feasibility gate for the C ABI responses (docs/capi.md; ADR-015
// rule 5: moved here from the C ABI so that it never names a product).

#include "cemkit/physics/air.hpp"
#include "cemkit/product/json_io.hpp"
#include "products/fans/common/feasibility.hpp"

namespace cemkit::fans {

[[nodiscard]] product::Json duty_json(const LabelledDuty& duty);
[[nodiscard]] product::Json report_json(const FeasibilityReport& report);

// The family range of a request: {"family", "specific_speed_min", "specific_speed_max", "source"}.
[[nodiscard]] FamilyRange range_of(const product::Json& range);

// The feasibility gate for {"flow", "fan_total_pressure", ["omega"], ["d_tip"]} with air and range.
// A malformed request throws product::BadRequest.
[[nodiscard]] core::Result<FeasibilityReport> run_feasibility(const product::Json& args,
                                                              const physics::Air& air,
                                                              const product::Json& range);

}  // namespace cemkit::fans

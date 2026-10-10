#pragma once

// Request handlers behind the C ABI (private to cemkit/capi). Each takes the parsed request and
// returns the response document or the kernel's classified error. Request and response formats:
// docs/capi.md. A malformed request (a missing key, a wrong JSON type) throws BadRequest, which
// cemkit.cpp turns into invalid_input at the boundary; nothing else here throws by design.

#include <nlohmann/json.hpp>
#include <string>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/product/json_io.hpp"

namespace cemkit::capi::detail {

using Json = product::Json;

// A request that does not have the documented shape; subject names the offending key.
using BadRequest = product::BadRequest;

[[nodiscard]] Json error_json(const core::Error& error);

[[nodiscard]] core::Result<Json> spec_compile(const Json& request);
[[nodiscard]] core::Result<Json> feasibility(const Json& request);
[[nodiscard]] core::Result<Json> l0_batch(const Json& request);
[[nodiscard]] core::Result<Json> geometry_smoke(const Json& request);
[[nodiscard]] core::Result<Json> product_operation(const Json& request);

}  // namespace cemkit::capi::detail

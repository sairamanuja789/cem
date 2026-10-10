#pragma once

// Request handlers behind the C ABI (private to cemkit/capi). Each takes the parsed request and
// returns the response document or the kernel's classified error. Request and response formats:
// docs/capi.md. A malformed request (a missing key, a wrong JSON type) throws BadRequest, which
// cemkit.cpp turns into invalid_input at the boundary; nothing else here throws by design.

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"

namespace cemkit::capi::detail {

using Json = nlohmann::ordered_json;

// A request that does not have the documented shape; subject names the offending key.
class BadRequest : public std::runtime_error {
 public:
  BadRequest(std::string subject, const std::string& message)
      : std::runtime_error{message}, subject_{std::move(subject)} {}
  [[nodiscard]] const std::string& subject() const noexcept { return subject_; }

 private:
  std::string subject_;
};

[[nodiscard]] Json error_json(const core::Error& error);

[[nodiscard]] core::Result<Json> spec_compile(const Json& request);
[[nodiscard]] core::Result<Json> feasibility(const Json& request);
[[nodiscard]] core::Result<Json> l0_batch(const Json& request);
[[nodiscard]] core::Result<Json> geometry_smoke(const Json& request);

}  // namespace cemkit::capi::detail

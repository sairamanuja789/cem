#pragma once

// JSON request reading and response writing shared by the C ABI and the products' operations
// (ADR-015 rule 5). Every number is in its coherent SI unit (docs/capi.md). A malformed request (a
// missing key, a wrong JSON type) throws BadRequest, which only the C ABI catches and turns into
// invalid_input at the boundary; nothing else here throws by design.

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

#include "cemkit/core/error.hpp"
#include "cemkit/core/fidelity.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/version.hpp"
#include "cemkit/physics/air.hpp"

namespace cemkit::product {

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

[[nodiscard]] const Json& member(const Json& object, const char* key);
[[nodiscard]] double number(const Json& object, const char* key);
[[nodiscard]] std::string text(const Json& object, const char* key);
[[nodiscard]] std::optional<double> optional_number(const Json& object, const char* key);

// The request's air: each property given in the request, the rest from the AX-005 default air
// with the 1976 constants (physics::default_air), so no constant is repeated outside the kernel.
[[nodiscard]] physics::Air air_of(const Json& object, const char* key);

[[nodiscard]] Json error_json(const core::Error& error);

template <class Q, class U>
[[nodiscard]] Json labelled_json(const core::Labelled<Q>& l, U unit) {
  return Json{{"value", l.value().numerical_value_in(unit)},
              {"fidelity", std::string{core::to_string(l.fidelity())}},
              {"fidelity_label", std::string{core::report_label(l.fidelity())}},
              {"model", core::to_string(l.model())}};
}

}  // namespace cemkit::product

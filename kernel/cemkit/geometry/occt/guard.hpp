#pragma once

// The exception boundary of the OCCT adapter (kernel/CLAUDE.md, ADR-004): OCCT reports many
// failures by throwing Standard_Failure subclasses. Every OCCT call in this module runs inside
// guarded(), which turns any exception into a geometry_failed Error, so none escapes the backend.

#include <Standard_Failure.hxx>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"

namespace cemkit::geometry::occt {

namespace detail {
[[nodiscard]] inline std::string message_of(const char* text) {
  return text == nullptr ? "" : text;
}
}  // namespace detail

// Runs `body` (which returns a core::Result<T> or core::Status) and returns its result, or
// geometry_failed with subject `operation` if it throws. Details: "exception" (the OCCT exception
// type, "std::exception" or "unknown") and "what" (the exception message, when there is one).
// noexcept: if building the Error itself fails (out of memory), the process terminates rather than
// letting an exception cross the boundary.
template <class Body>
[[nodiscard]] std::invoke_result_t<Body&> guarded(std::string_view operation,
                                                  Body&& body) noexcept {
  const auto failed = [operation](std::string type, std::string what) {
    core::ErrorDetails details{{"exception", std::move(type)}};
    if (!what.empty()) {
      details.emplace("what", std::move(what));
    }
    return core::fail(core::ErrorCode::geometry_failed,
                      "OpenCascade raised an exception during " + std::string{operation},
                      std::string{operation}, std::move(details));
  };
  try {
    return body();
  } catch (const Standard_Failure& failure) {
    return failed(detail::message_of(failure.ExceptionType()), detail::message_of(failure.what()));
  } catch (const std::exception& exception) {
    return failed("std::exception", detail::message_of(exception.what()));
  } catch (...) {
    return failed("unknown", {});
  }
}

}  // namespace cemkit::geometry::occt

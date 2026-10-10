// C ABI implementation (cemkit.h). Every entry point catches everything: no exception crosses the
// boundary. Strings handed to the caller are malloc'ed copies released by cemkit_free; this file is
// the one place in the kernel that owns raw memory, because a C caller cannot hold a C++ object.
#include "cemkit/capi/cemkit.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <string_view>

#include "cemkit/capi/detail/handlers.hpp"
#include "cemkit/core/error.hpp"
#include "cemkit/core/version.hpp"

namespace {

using cemkit::capi::detail::BadRequest;
using cemkit::capi::detail::Json;
using Handler = cemkit::core::Result<Json> (*)(const Json&);

// A malloc'ed, NUL-terminated copy of text, or nullptr when memory is exhausted.
char* to_c_string(const std::string& text) noexcept {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): C ABI ownership
  auto* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out != nullptr) {
    std::memcpy(out, text.c_str(), text.size() + 1);
  }
  return out;
}

cemkit_status emit_error(const cemkit::core::Error& error, cemkit_status status,
                         char** out_json) noexcept {
  try {
    *out_json = to_c_string(Json{{"error", cemkit::capi::detail::error_json(error)}}.dump());
  } catch (...) {
    *out_json = nullptr;
  }
  return status;
}

// Parses the request, runs the handler and writes the response; never throws.
cemkit_status call(Handler handler, const char* request_json, char** out_json) noexcept {
  if (out_json == nullptr) {
    return CEMKIT_INVALID_ARGUMENT;
  }
  *out_json = nullptr;
  if (request_json == nullptr) {
    return CEMKIT_INVALID_ARGUMENT;
  }
  try {
    const Json request = Json::parse(std::string_view{request_json}, nullptr, false);
    if (request.is_discarded() || !request.is_object()) {
      return emit_error(cemkit::core::Error{cemkit::core::ErrorCode::invalid_input,
                                            "request must be a JSON object", "request"},
                        CEMKIT_FAILED, out_json);
    }
    auto response = handler(request);
    if (!response) {
      return emit_error(response.error(), CEMKIT_FAILED, out_json);
    }
    *out_json = to_c_string(response->dump());
    return *out_json != nullptr ? CEMKIT_OK : CEMKIT_INTERNAL_ERROR;
  } catch (const BadRequest& bad) {
    return emit_error(
        cemkit::core::Error{cemkit::core::ErrorCode::invalid_input, bad.what(), bad.subject()},
        CEMKIT_FAILED, out_json);
  } catch (const std::bad_alloc&) {
    return CEMKIT_INTERNAL_ERROR;
  } catch (const std::exception& e) {
    return emit_error(
        cemkit::core::Error{cemkit::core::ErrorCode::internal_error, e.what(), "capi"},
        CEMKIT_INTERNAL_ERROR, out_json);
  } catch (...) {
    return CEMKIT_INTERNAL_ERROR;
  }
}

}  // namespace

extern "C" {

void cemkit_abi_version(uint32_t* major, uint32_t* minor, uint32_t* patch) {
  if (major != nullptr) {
    *major = CEMKIT_ABI_VERSION_MAJOR;
  }
  if (minor != nullptr) {
    *minor = CEMKIT_ABI_VERSION_MINOR;
  }
  if (patch != nullptr) {
    *patch = CEMKIT_ABI_VERSION_PATCH;
  }
}

const char* cemkit_kernel_version(void) { return cemkit::core::kernel_version().data(); }

cemkit_status cemkit_spec_compile(const char* request_json, char** out_json) {
  return call(&cemkit::capi::detail::spec_compile, request_json, out_json);
}

cemkit_status cemkit_feasibility(const char* request_json, char** out_json) {
  return call(&cemkit::capi::detail::feasibility, request_json, out_json);
}

cemkit_status cemkit_l0_batch(const char* request_json, char** out_json) {
  return call(&cemkit::capi::detail::l0_batch, request_json, out_json);
}

cemkit_status cemkit_geometry_smoke(const char* request_json, char** out_json) {
  return call(&cemkit::capi::detail::geometry_smoke, request_json, out_json);
}

void cemkit_free(char* json) {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): C ABI ownership
  std::free(json);
}

}  // extern "C"

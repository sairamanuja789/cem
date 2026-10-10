// cemkit._kernel: the Python binding of the C ABI (T11; PERF-002; ADR-012). Batch-only and
// logic-free: every function takes one JSON request and returns (status, JSON response) from the
// matching cemkit_* call; the GIL is released while the kernel runs. python/cemkit/kernel.py is the
// typed Python API on top of it.
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "cemkit/capi/cemkit.h"

namespace nb = nanobind;

namespace {

using Entry = cemkit_status (*)(const char*, char**);

std::tuple<int, std::string> invoke(Entry entry, const std::string& request) {
  char* out = nullptr;
  cemkit_status status = CEMKIT_INTERNAL_ERROR;
  {
    const nb::gil_scoped_release release;
    status = entry(request.c_str(), &out);
  }
  std::string body = out != nullptr ? std::string{out} : std::string{};
  cemkit_free(out);
  return {static_cast<int>(status), std::move(body)};
}

}  // namespace

NB_MODULE(_kernel, m) {
  m.doc() = "cemkit kernel C ABI binding: JSON in, (status, JSON) out (docs/capi.md)";
  m.def("abi_version", [] {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    cemkit_abi_version(&major, &minor, &patch);
    return std::make_tuple(major, minor, patch);
  });
  m.def("kernel_version", [] { return std::string{cemkit_kernel_version()}; });
  m.def("spec_compile", [](const std::string& r) { return invoke(&cemkit_spec_compile, r); });
  m.def("feasibility", [](const std::string& r) { return invoke(&cemkit_feasibility, r); });
  m.def("l0_batch", [](const std::string& r) { return invoke(&cemkit_l0_batch, r); });
  m.def("geometry_smoke", [](const std::string& r) { return invoke(&cemkit_geometry_smoke, r); });
}

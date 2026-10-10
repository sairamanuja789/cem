#pragma once

// JSON encoding of geometry results for the C ABI and product operations (GEO-001, GEO-002,
// GEO-004; docs/capi.md): export requests in, a solid's validity, topology, mass properties and
// export hashes out. Lengths in m, angles in rad. A malformed request throws BadRequest.

#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"
#include "cemkit/geometry/port/backend.hpp"
#include "cemkit/product/json_io.hpp"

namespace cemkit::product {

// RFC 4648 base64 (standard alphabet, with padding), for export bytes in a JSON response.
[[nodiscard]] std::string base64(std::string_view bytes);

// {"format": "step"} or {"format": "stl", "linear_deflection": m, "angular_deflection": rad}.
[[nodiscard]] geometry::port::ExportRequest export_request_of(const Json& e);

// The optional "exports" array and "include_data" flag of a request.
struct ExportOptions {
  std::vector<geometry::port::ExportRequest> exports;
  bool include_data = false;
};
[[nodiscard]] ExportOptions export_options_of(const Json& request);

// {"validity", "topology", "mass_properties", "exports"} of one solid; an engine error fails.
[[nodiscard]] core::Result<Json> solid_json(const geometry::port::GeometryBackend& backend,
                                            const geometry::port::Solid& solid,
                                            const ExportOptions& options);

}  // namespace cemkit::product

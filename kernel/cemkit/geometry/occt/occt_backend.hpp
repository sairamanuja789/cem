#pragma once

// OpenCascade (OCCT 8.0) implementation of the geometry port (ADR-004). The only module that
// includes OCCT headers. Every OCCT call runs inside guarded(): no exception leaves this backend.

#include <memory>
#include <string>
#include <string_view>

#include "cemkit/core/result.hpp"
#include "cemkit/geometry/port/backend.hpp"

namespace cemkit::geometry::occt {

// Name reported by OcctBackend::name() and by the solids it builds.
inline constexpr std::string_view k_backend_name = "occt";

// Time stamp written into every STEP header in place of the wall-clock time, so that the same solid
// always exports to the same bytes and therefore the same content-hash name (GEO-001, GEO-004).
// A fixed marker, not a date anyone should read.
inline constexpr std::string_view k_step_time_stamp = "1970-01-01T00:00:00";

// Id and name of every PRODUCT entity in an exported STEP file, replacing OCCT's per-process
// counter ("Open CASCADE STEP translator 8.0 <n>") for the same reason.
inline constexpr std::string_view k_step_product_name = "cemkit solid";

class OcctBackend final : public port::GeometryBackend {
 public:
  [[nodiscard]] std::string_view name() const noexcept override { return k_backend_name; }

  [[nodiscard]] core::Result<std::unique_ptr<port::Solid>> build_test_solid(
      const port::TestSolidParams& params) const override;
  [[nodiscard]] core::Result<port::ValidityReport> check_validity(
      const port::Solid& solid) const override;
  [[nodiscard]] core::Result<port::Topology> topology(const port::Solid& solid) const override;
  [[nodiscard]] core::Result<port::MassProperties> mass_properties(
      const port::Solid& solid) const override;
  [[nodiscard]] core::Result<std::string> export_bytes(
      const port::Solid& solid, const port::ExportRequest& request) const override;
};

}  // namespace cemkit::geometry::occt

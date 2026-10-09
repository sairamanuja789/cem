#pragma once

// Geometry port (ADR-004, ADR-009): the backend-neutral interface every geometry engine implements.
// Nothing here names an engine; kernel/cemkit/geometry/occt/ is the OpenCascade implementation.
//
// GEO-001: a backend builds geometry deterministically from parameters; the same parameters give
// the
//          same volume, area and topology.
// GEO-002: every solid a backend returns has passed the validity checks (closed, manifold, no
//          self-intersection); a failure is reported as geometry_failed with the reason.
// GEO-004: exports are STEP (editable CAD) and STL (printing), stored under content-hash names.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"

namespace cemkit::geometry::port {

// One cross-section of the test plate: a rectangle in the plane normal to the radial (+x) axis,
// centred on that axis at `radius`, `chord` long and `thickness` thick, turned by `stagger` about
// the radial axis (stagger 0: chord along the hub axis +z, thickness along +y).
struct PlateSection {
  core::Length radius;
  core::Length chord;
  core::Length thickness;
  core::Angle stagger;
};

// Parameters of the T10 test solid: a cylindrical hub on the z axis, centred on the origin, fused
// with a plate lofted through `sections` (at least 2, radii strictly increasing). The plate is
// meant to start inside the hub; a plate that does not meet the hub is rejected because the result
// is not one solid. These are geometry test parameters only, not a fan design.
struct TestSolidParams {
  core::Length hub_radius;
  core::Length hub_length;
  std::vector<PlateSection> sections;
};

// Number of distinct sub-shapes of each type (a sub-shape shared by several parents counts once).
struct Topology {
  std::size_t solids = 0;
  std::size_t shells = 0;
  std::size_t faces = 0;
  std::size_t edges = 0;
  std::size_t vertices = 0;

  bool operator==(const Topology&) const = default;
};

// Geometric properties of a solid: enclosed volume, total boundary area, and the centroid of the
// volume (x, y, z in the backend's global frame). Geometry, not a physics prediction: no fidelity
// label is attached.
struct MassProperties {
  core::Volume volume;
  core::Area area;
  std::array<core::Length, 3> centroid;
};

// Result of the GEO-002 checks.
//   brep_valid:             the engine's own topology and geometry check passes;
//   closed:                 every edge bounds faces on both sides (free_edges == 0);
//   manifold:               no edge is used by more than two faces (non_manifold_edges == 0);
//   self_intersection_free: no two sub-shapes intersect other than at shared boundaries.
struct ValidityReport {
  bool brep_valid = false;
  bool closed = false;
  bool manifold = false;
  bool self_intersection_free = false;
  std::size_t free_edges = 0;
  std::size_t non_manifold_edges = 0;

  [[nodiscard]] bool ok() const noexcept {
    return brep_valid && closed && manifold && self_intersection_free;
  }
};

// geometry_failed naming every failed check, or success if the report is ok().
[[nodiscard]] core::Status require_valid(const ValidityReport& report, std::string_view subject);

enum class ExportFormat : std::uint8_t { step, stl };

// File extension without the dot: "step" or "stl".
[[nodiscard]] std::string_view extension(ExportFormat format) noexcept;

// Triangulation tolerances for STL export: the largest distance between a triangle and the exact
// surface, and the largest angle between neighbouring triangle normals. Chosen by the caller (there
// is no default: the right value depends on the part and the printer).
struct Tessellation {
  core::Length linear_deflection;
  core::Angle angular_deflection;
};

// STL requires a tessellation; STEP ignores it.
struct ExportRequest {
  ExportFormat format = ExportFormat::step;
  std::optional<Tessellation> tessellation;
};

// A file written by export_to_directory(): <directory>/<sha256>.<extension>.
struct ExportedFile {
  std::filesystem::path path;
  std::string sha256;
  std::uintmax_t size_bytes = 0;
};

// A solid owned by the backend that built it. Opaque: only that backend can read it.
class Solid {
 public:
  Solid(const Solid&) = delete;
  Solid& operator=(const Solid&) = delete;
  Solid(Solid&&) = delete;
  Solid& operator=(Solid&&) = delete;
  virtual ~Solid() = default;

  // Name of the backend that built this solid.
  [[nodiscard]] virtual std::string_view backend() const noexcept = 0;

 protected:
  Solid() = default;
};

// A geometry engine. Every operation is deterministic, returns an Error instead of throwing, and
// leaves the solid unchanged. A Solid from another backend is rejected with invalid_input.
class GeometryBackend {
 public:
  GeometryBackend() = default;
  GeometryBackend(const GeometryBackend&) = delete;
  GeometryBackend& operator=(const GeometryBackend&) = delete;
  GeometryBackend(GeometryBackend&&) = delete;
  GeometryBackend& operator=(GeometryBackend&&) = delete;
  virtual ~GeometryBackend() = default;

  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // GEO-001, GEO-002: the T10 test solid, or geometry_failed with the reason (invalid parameters,
  // an engine failure, a result that is not one solid, or a failed validity check).
  [[nodiscard]] virtual core::Result<std::unique_ptr<Solid>> build_test_solid(
      const TestSolidParams& params) const = 0;

  [[nodiscard]] virtual core::Result<ValidityReport> check_validity(const Solid& solid) const = 0;
  [[nodiscard]] virtual core::Result<Topology> topology(const Solid& solid) const = 0;
  [[nodiscard]] virtual core::Result<MassProperties> mass_properties(const Solid& solid) const = 0;

  // GEO-004: the exported file's bytes. Identical solids and requests give identical bytes.
  [[nodiscard]] virtual core::Result<std::string> export_bytes(
      const Solid& solid, const ExportRequest& request) const = 0;
};

// GEO-004: exports `solid` and writes it to <directory>/<sha256 of the bytes>.<extension>. The
// directory must exist. The file is written under a temporary name and renamed into place, so a
// file with a content-hash name is always complete; writing the same content again is harmless.
[[nodiscard]] core::Result<ExportedFile> export_to_directory(
    const GeometryBackend& backend, const Solid& solid, const ExportRequest& request,
    const std::filesystem::path& directory);

}  // namespace cemkit::geometry::port

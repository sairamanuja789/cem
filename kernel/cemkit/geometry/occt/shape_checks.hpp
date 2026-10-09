#pragma once

// Shape-level checks and measurements used by OcctBackend (ADR-004). They take an OCCT shape, so
// only this module and its tests can call them. Lengths inside OCCT are millimetres (ADR-004); the
// results here are converted to SI quantities. None of these functions catches OCCT exceptions:
// callers wrap them in guarded() (guard.hpp).

#include <TopoDS_Shape.hxx>

#include "cemkit/geometry/port/backend.hpp"

namespace cemkit::geometry::occt {

// GEO-002: OCCT's B-rep check (BRepCheck_Analyzer), closedness and manifoldness from the number of
// faces that use each edge, and self-intersection (BOPAlgo_ArgumentAnalyzer, self-interference
// mode, OCCT default fuzzy value).
[[nodiscard]] port::ValidityReport check_shape(const TopoDS_Shape& shape);

// GEO-001: number of distinct solids, shells, faces, edges and vertices.
[[nodiscard]] port::Topology count_topology(const TopoDS_Shape& shape);

// GEO-001: volume, boundary area and volume centroid (BRepGProp), converted from OCCT millimetres.
[[nodiscard]] port::MassProperties measure(const TopoDS_Shape& shape);

}  // namespace cemkit::geometry::occt

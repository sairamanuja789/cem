#include "cemkit/geometry/occt/shape_checks.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <cstddef>
#include <gp_Pnt.hxx>
#include <vector>

#include "cemkit/geometry/port/backend.hpp"

namespace cemkit::geometry::occt {

namespace {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

std::size_t count_distinct(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
  ShapeMap map;
  TopExp::MapShapes(shape, type, map);
  return static_cast<std::size_t>(map.Extent());
}

}  // namespace

port::ValidityReport check_shape(const TopoDS_Shape& shape) {
  port::ValidityReport report;

  const BRepCheck_Analyzer analyzer{shape};
  report.brep_valid = analyzer.IsValid();

  // Face uses of every distinct edge. A seam edge (the closing edge of a periodic face such as a
  // cylinder) appears twice in its one face and is counted twice, as it bounds the face on both
  // sides. Degenerated edges (a cone apex) bound nothing and are skipped. A closed manifold
  // boundary uses every other edge exactly twice: once means a free edge (open), more than twice
  // means a non-manifold edge, and an edge in no face at all is a dangling edge (counted as free).
  ShapeMap edges;
  TopExp::MapShapes(shape, TopAbs_EDGE, edges);
  std::vector<int> uses(static_cast<std::size_t>(edges.Extent()), 0);
  for (TopExp_Explorer face{shape, TopAbs_FACE}; face.More(); face.Next()) {
    for (TopExp_Explorer edge{face.Current(), TopAbs_EDGE}; edge.More(); edge.Next()) {
      const int index = edges.FindIndex(edge.Current());
      ++uses.at(static_cast<std::size_t>(index - 1));
    }
  }
  for (int index = 1; index <= edges.Extent(); ++index) {
    if (BRep_Tool::Degenerated(TopoDS::Edge(edges.FindKey(index)))) {
      continue;
    }
    const int count = uses.at(static_cast<std::size_t>(index - 1));
    if (count < 2) {
      ++report.free_edges;
    } else if (count > 2) {
      ++report.non_manifold_edges;
    }
  }
  // A shape without faces bounds no volume: not closed, even with no edges at all.
  report.closed = report.free_edges == 0 && count_distinct(shape, TopAbs_FACE) > 0;
  report.manifold = report.non_manifold_edges == 0;

  BOPAlgo_ArgumentAnalyzer self_check;
  self_check.SetShape1(shape);
  self_check.SelfInterMode() = true;
  self_check.SetRunParallel(false);
  self_check.Perform();
  report.self_intersection_free = !self_check.HasFaulty();
  return report;
}

port::Topology count_topology(const TopoDS_Shape& shape) {
  return port::Topology{.solids = count_distinct(shape, TopAbs_SOLID),
                        .shells = count_distinct(shape, TopAbs_SHELL),
                        .faces = count_distinct(shape, TopAbs_FACE),
                        .edges = count_distinct(shape, TopAbs_EDGE),
                        .vertices = count_distinct(shape, TopAbs_VERTEX)};
}

port::MassProperties measure(const TopoDS_Shape& shape) {
  namespace si = mp_units::si;
  namespace isq = mp_units::isq;
  constexpr auto k_mm = si::milli<si::metre>;

  GProp_GProps volume_props;
  BRepGProp::VolumeProperties(shape, volume_props);
  GProp_GProps surface_props;
  BRepGProp::SurfaceProperties(shape, surface_props);
  const gp_Pnt centre = volume_props.CentreOfMass();

  // OCCT lengths are millimetres (ADR-004); the quantities convert them to SI.
  return port::MassProperties{
      .volume = volume_props.Mass() * isq::volume[mp_units::cubic(k_mm)],
      .area = surface_props.Mass() * isq::area[mp_units::square(k_mm)],
      .centroid = {centre.X() * isq::length[k_mm], centre.Y() * isq::length[k_mm],
                   centre.Z() * isq::length[k_mm]}};
}

}  // namespace cemkit::geometry::occt

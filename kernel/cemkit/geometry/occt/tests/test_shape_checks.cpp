#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <sstream>
#include <string>

#include "cemkit/geometry/occt/occt_backend.hpp"
#include "cemkit/geometry/occt/shape_checks.hpp"
#include "cemkit/geometry/port/backend.hpp"

namespace occt = cemkit::geometry::occt;
namespace port = cemkit::geometry::port;
namespace si = mp_units::si;
namespace isq = mp_units::isq;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr auto k_mm = si::milli<si::metre>;

// Box dimensions and positions below are arbitrary test values in OCCT millimetres.
TopoDS_Shape box(double x, double y, double z, double dx, double dy, double dz) {
  return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape();
}

}  // namespace

TEST_CASE("a box passes every validity check", "[GEO-002]") {
  const auto report = occt::check_shape(box(0, 0, 0, 10, 20, 30));
  CHECK(report.brep_valid);
  CHECK(report.closed);
  CHECK(report.manifold);
  CHECK(report.self_intersection_free);
  CHECK(report.ok());
}

TEST_CASE("an open face is not closed", "[GEO-002]") {
  const TopoDS_Shape face = BRepBuilderAPI_MakeFace(gp_Pln(), 0.0, 10.0, 0.0, 10.0).Shape();
  const auto report = occt::check_shape(face);
  CHECK_FALSE(report.closed);
  CHECK(report.free_edges == 4);
  CHECK_FALSE(report.ok());
}

TEST_CASE("an empty shape is not closed", "[GEO-002]") {
  TopoDS_Compound empty;
  BRep_Builder builder;
  builder.MakeCompound(empty);
  CHECK_FALSE(occt::check_shape(empty).closed);
}

TEST_CASE("two boxes sharing only an edge are not manifold", "[GEO-002]") {
  // The boxes touch along the line x = 10, y = 10; the fuse shares that edge between four faces.
  BRepAlgoAPI_Fuse fuse(box(0, 0, 0, 10, 10, 10), box(10, 10, 0, 10, 10, 10));
  REQUIRE(fuse.IsDone());
  const auto report = occt::check_shape(fuse.Shape());
  CHECK_FALSE(report.manifold);
  CHECK(report.non_manifold_edges >= 1);
  CHECK_FALSE(report.ok());
}

TEST_CASE("overlapping solids in one shape are self-intersecting", "[GEO-002]") {
  TopoDS_Compound compound;
  BRep_Builder builder;
  builder.MakeCompound(compound);
  builder.Add(compound, box(0, 0, 0, 10, 10, 10));
  builder.Add(compound, box(5, 5, 5, 10, 10, 10));
  const auto report = occt::check_shape(compound);
  CHECK_FALSE(report.self_intersection_free);
  CHECK_FALSE(report.ok());
}

TEST_CASE("topology counts distinct sub-shapes", "[GEO-001]") {
  const auto topology = occt::count_topology(box(0, 0, 0, 1, 2, 3));
  CHECK(topology ==
        port::Topology{.solids = 1, .shells = 1, .faces = 6, .edges = 12, .vertices = 8});
}

TEST_CASE("mass properties of a box match the hand calculation in SI units", "[GEO-001]") {
  // 10 x 20 x 30 mm box at (1, 2, 3) mm: V = 6000 mm^3 = 6e-6 m^3;
  // A = 2 (10*20 + 20*30 + 10*30) = 2200 mm^2 = 2.2e-3 m^2; centroid (6, 12, 18) mm.
  const auto props = occt::measure(box(1, 2, 3, 10, 20, 30));
  CHECK_THAT(props.volume.numerical_value_in(mp_units::cubic(si::metre)), WithinRel(6e-6, 1e-12));
  CHECK_THAT(props.area.numerical_value_in(mp_units::square(si::metre)), WithinRel(2.2e-3, 1e-12));
  CHECK_THAT(props.centroid.at(0).numerical_value_in(k_mm), WithinRel(6.0, 1e-12));
  CHECK_THAT(props.centroid.at(1).numerical_value_in(k_mm), WithinRel(12.0, 1e-12));
  CHECK_THAT(props.centroid.at(2).numerical_value_in(k_mm), WithinRel(18.0, 1e-12));
}

TEST_CASE("the exported STEP file reads back as the same solid", "[GEO-004]") {
  const occt::OcctBackend backend;
  const auto params =
      port::TestSolidParams{.hub_radius = 15.0 * isq::length[k_mm],
                            .hub_length = 20.0 * isq::length[k_mm],
                            .sections = {{.radius = 10.0 * isq::length[k_mm],
                                          .chord = 18.0 * isq::length[k_mm],
                                          .thickness = 2.0 * isq::length[k_mm],
                                          .stagger = 0.3 * isq::angular_measure[si::radian]},
                                         {.radius = 30.0 * isq::length[k_mm],
                                          .chord = 15.0 * isq::length[k_mm],
                                          .thickness = 1.5 * isq::length[k_mm],
                                          .stagger = 0.5 * isq::angular_measure[si::radian]},
                                         {.radius = 50.0 * isq::length[k_mm],
                                          .chord = 12.0 * isq::length[k_mm],
                                          .thickness = 1.2 * isq::length[k_mm],
                                          .stagger = 0.7 * isq::angular_measure[si::radian]}}};
  const auto solid = backend.build_test_solid(params);
  REQUIRE(solid.has_value());
  const auto original = backend.mass_properties(**solid);
  const auto original_topology = backend.topology(**solid);
  const auto bytes = backend.export_bytes(**solid, port::ExportRequest{});
  REQUIRE(original.has_value());
  REQUIRE(original_topology.has_value());
  REQUIRE(bytes.has_value());

  std::istringstream in{*bytes};
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("test_solid.step", in) == IFSelect_RetDone);
  REQUIRE(reader.TransferRoots() > 0);
  const TopoDS_Shape read_back = reader.OneShape();

  // The STEP file is in millimetres, as is OCCT's model space, so values come back unscaled.
  const auto props = occt::measure(read_back);
  const double volume = original->volume.numerical_value_in(mp_units::cubic(si::metre));
  CHECK_THAT(props.volume.numerical_value_in(mp_units::cubic(si::metre)), WithinRel(volume, 1e-6));
  CHECK(occt::count_topology(read_back).faces == original_topology->faces);
  CHECK(occt::check_shape(read_back).ok());
}

#include "cemkit/geometry/occt/occt_backend.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <APIHeaderSection_MakeHeader.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <GeomAbs_Shape.hxx>
#include <Geom_BSplineCurve.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_HArray1.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <StepBasic_Product.hxx>
#include <StepData_StepModel.hxx>
#include <StlAPI_Writer.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <cmath>
#include <cstddef>
#include <format>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <iostream>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/geometry/occt/guard.hpp"
#include "cemkit/geometry/occt/shape_checks.hpp"

namespace cemkit::geometry::occt {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
constexpr auto k_mm = si::milli<si::metre>;

// Route OCCT messages through Message::DefaultMessenger() to std::clog at debug level.
// Standard output (stdout) must never receive OCCT output so CLI JSON remains clean (D5, HI-013).
class OcctLogPrinter final : public Message_Printer {
 public:
  DEFINE_STANDARD_RTTI_INLINE(OcctLogPrinter, Message_Printer)

  OcctLogPrinter() { SetTraceLevel(Message_Info); }

 protected:
  void send(const TCollection_AsciiString& theString,
            const Message_Gravity /*theGravity*/) const override {
    const char* str = theString.ToCString();
    if (str != nullptr && str[0] != '\0' && str[0] != '\n') {
      std::clog << str;
    }
  }
};

void init_occt_messenger() {
  static const bool initialized = []() {
    const occ::handle<Message_Messenger>& messenger = Message::DefaultMessenger();
    if (!messenger.IsNull()) {
      messenger->ChangePrinters().Clear();
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the OCCT handle takes ownership
      messenger->AddPrinter(new OcctLogPrinter());
    }
    return true;
  }();
  (void)initialized;
}

class OcctSolid final : public port::Solid {
 public:
  explicit OcctSolid(TopoDS_Shape shape) : shape_{std::move(shape)} {}
  [[nodiscard]] std::string_view backend() const noexcept override { return k_backend_name; }
  [[nodiscard]] const TopoDS_Shape& shape() const noexcept { return shape_; }

 private:
  TopoDS_Shape shape_;
};

core::Result<const TopoDS_Shape*> shape_of(const port::Solid& solid) {
  const auto* own = dynamic_cast<const OcctSolid*>(&solid);
  if (own == nullptr) {
    return core::fail(core::ErrorCode::invalid_input, "solid was not built by this backend",
                      "solid", {{"backend", std::string{solid.backend()}}, {"expected", "occt"}});
  }
  return &own->shape();
}

// Geometry parameters are checked before OCCT sees them. A bad geometry parameter is a geometry
// failure of this candidate (GEO-002: failures are recorded as geometry_failed), named by its field
// path. A bad export request is the caller's error (invalid_input).
core::Status require_positive(double value, const std::string& subject,
                              core::ErrorCode code = core::ErrorCode::geometry_failed) {
  if (!std::isfinite(value)) {
    return core::fail(code, subject + " must be finite", subject,
                      {{"value", core::format_number(value)}});
  }
  if (value <= 0.0) {
    return core::fail(code, subject + " must be positive", subject,
                      {{"value", core::format_number(value)}});
  }
  return {};
}

core::Status require_finite(double value, const std::string& subject) {
  if (!std::isfinite(value)) {
    return core::fail(core::ErrorCode::geometry_failed, subject + " must be finite", subject,
                      {{"value", core::format_number(value)}});
  }
  return {};
}

// Parameters in OCCT model units: millimetres and radians (ADR-004).
struct SectionMm {
  double radius;
  double chord;
  double thickness;
  double stagger;
};

core::Status validate(const port::TestSolidParams& params) {
  if (auto ok = require_positive(params.hub_radius.numerical_value_in(k_mm), "hub_radius"); !ok) {
    return ok;
  }
  if (auto ok = require_positive(params.hub_length.numerical_value_in(k_mm), "hub_length"); !ok) {
    return ok;
  }
  if (params.sections.size() < 2) {
    return core::fail(core::ErrorCode::geometry_failed, "the plate needs at least 2 sections",
                      "sections", {{"count", std::to_string(params.sections.size())}});
  }
  for (std::size_t i = 0; i < params.sections.size(); ++i) {
    const auto& section = params.sections.at(i);
    const std::string prefix = std::format("sections[{}].", i);
    for (const auto& [value, field] :
         {std::pair{section.radius.numerical_value_in(k_mm), "radius"},
          std::pair{section.chord.numerical_value_in(k_mm), "chord"},
          std::pair{section.thickness.numerical_value_in(k_mm), "thickness"}}) {
      if (auto ok = require_positive(value, prefix + field); !ok) {
        return ok;
      }
    }
    if (auto ok =
            require_finite(section.stagger.numerical_value_in(si::radian), prefix + "stagger");
        !ok) {
      return ok;
    }
    if (i > 0 && !(section.radius > params.sections.at(i - 1).radius)) {
      return core::fail(
          core::ErrorCode::geometry_failed, "section radii must be strictly increasing",
          prefix + "radius",
          {{"value", core::format_number(section.radius.numerical_value_in(k_mm))},
           {"previous",
            core::format_number(params.sections.at(i - 1).radius.numerical_value_in(k_mm))},
           {"unit", "mm"}});
    }
  }
  return {};
}

// The section rectangle at x = radius in the y-z plane: chord along (0, -sin s, cos s), thickness
// along (0, cos s, sin s), for stagger s.
core::Result<TopoDS_Wire> section_wire(const SectionMm& s, std::size_t index) {
  const double half_chord = s.chord / 2.0;
  const double half_thickness = s.thickness / 2.0;
  const double sin_s = std::sin(s.stagger);
  const double cos_s = std::cos(s.stagger);
  const auto corner = [&](double along_chord, double along_thickness) {
    return gp_Pnt(s.radius, (-along_chord * sin_s) + (along_thickness * cos_s),
                  (along_chord * cos_s) + (along_thickness * sin_s));
  };
  BRepBuilderAPI_MakePolygon polygon(
      corner(-half_chord, -half_thickness), corner(half_chord, -half_thickness),
      corner(half_chord, half_thickness), corner(-half_chord, half_thickness), true);
  if (!polygon.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "section outline could not be built",
                      std::format("sections[{}]", index));
  }
  return polygon.Wire();
}

std::string boolean_errors(const BRepAlgoAPI_Fuse& fuse) {
  std::ostringstream text;
  fuse.DumpErrors(text);
  return text.str();
}

core::Result<std::unique_ptr<port::Solid>> build(const port::TestSolidParams& params) {
  const double hub_radius = params.hub_radius.numerical_value_in(k_mm);
  const double hub_length = params.hub_length.numerical_value_in(k_mm);

  // Hub: cylinder on the z axis, centred on the origin. Its x direction is -x so that the seam of
  // the cylindrical face lies opposite the plate rather than through the fuse region.
  const gp_Ax2 hub_axes(gp_Pnt(0.0, 0.0, -hub_length / 2.0), gp_Dir(0.0, 0.0, 1.0),
                        gp_Dir(-1.0, 0.0, 0.0));
  BRepPrimAPI_MakeCylinder hub(hub_axes, hub_radius, hub_length);
  hub.Build();
  if (!hub.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "hub cylinder could not be built", "hub");
  }

  // Plate: a smooth (not ruled) solid loft through the section rectangles.
  BRepOffsetAPI_ThruSections loft(/*isSolid=*/true, /*ruled=*/false);
  for (std::size_t i = 0; i < params.sections.size(); ++i) {
    const auto& section = params.sections.at(i);
    auto wire = section_wire(SectionMm{.radius = section.radius.numerical_value_in(k_mm),
                                       .chord = section.chord.numerical_value_in(k_mm),
                                       .thickness = section.thickness.numerical_value_in(k_mm),
                                       .stagger = section.stagger.numerical_value_in(si::radian)},
                             i);
    if (!wire) {
      return std::unexpected{std::move(wire).error()};
    }
    loft.AddWire(*wire);
  }
  loft.Build();
  if (!loft.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "plate loft failed", "plate");
  }

  // Fuse, serially so that the result does not depend on thread scheduling (GEO-001).
  BRepAlgoAPI_Fuse fuse;
  NCollection_List<TopoDS_Shape> arguments;
  arguments.Append(hub.Shape());
  NCollection_List<TopoDS_Shape> tools;
  tools.Append(loft.Shape());
  fuse.SetArguments(arguments);
  fuse.SetTools(tools);
  fuse.SetRunParallel(false);
  fuse.Build();
  if (!fuse.IsDone() || fuse.HasErrors()) {
    return core::fail(core::ErrorCode::geometry_failed, "fusing the hub and the plate failed",
                      "test_solid", {{"occt_errors", boolean_errors(fuse)}});
  }

  const TopoDS_Shape fused = fuse.Shape();
  const auto topology = count_topology(fused);
  if (topology.solids != 1) {
    return core::fail(core::ErrorCode::geometry_failed,
                      "hub and plate are not one solid after the fuse", "test_solid",
                      {{"solids", std::to_string(topology.solids)}});
  }
  TopExp_Explorer solid{fused, TopAbs_SOLID};
  TopoDS_Shape result = solid.Current();

  // GEO-002: only valid solids leave the backend.
  if (auto valid = port::require_valid(check_shape(result), "test_solid"); !valid) {
    return std::unexpected{std::move(valid).error()};
  }
  return std::make_unique<OcctSolid>(std::move(result));
}

core::Status validate(const port::Tessellation& tessellation) {
  if (auto ok = require_positive(tessellation.linear_deflection.numerical_value_in(k_mm),
                                 "tessellation.linear_deflection", core::ErrorCode::invalid_input);
      !ok) {
    return ok;
  }
  return require_positive(tessellation.angular_deflection.numerical_value_in(si::radian),
                          "tessellation.angular_deflection", core::ErrorCode::invalid_input);
}

core::Result<std::string> step_bytes(const TopoDS_Shape& shape) {
  STEPControl_Writer writer;
  if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
    return core::fail(core::ErrorCode::geometry_failed, "STEP translation failed", "export");
  }
  // Make the bytes depend only on the solid (GEO-001, GEO-004). OCCT writes the wall-clock time
  // into the header, and names each PRODUCT with a counter kept in its process-wide STEP actor, so
  // the second export in a process would otherwise differ from the first.
  APIHeaderSection_MakeHeader header(writer.Model());
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the OCCT handle takes ownership
  header.SetTimeStamp(new TCollection_HAsciiString(std::string{k_step_time_stamp}.c_str()));
  const occ::handle<StepData_StepModel> model = writer.Model();
  for (int i = 1; i <= model->NbEntities(); ++i) {
    const auto product = occ::down_cast<StepBasic_Product>(model->Value(i));
    if (!product.IsNull()) {
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the OCCT handle takes ownership
      const occ::handle<TCollection_HAsciiString> name =
          new TCollection_HAsciiString(std::string{k_step_product_name}.c_str());
      product->SetId(name);
      product->SetName(name);
    }
  }
  std::ostringstream out;
  if (writer.WriteStream(out) != IFSelect_RetDone) {
    return core::fail(core::ErrorCode::geometry_failed, "STEP writing failed", "export");
  }
  return std::move(out).str();
}

core::Result<std::string> stl_bytes(const TopoDS_Shape& shape,
                                    const port::Tessellation& tessellation) {
  // Mesh a copy: BRepMesh stores the triangulation in the shape, and the solid must not change.
  BRepBuilderAPI_Copy copier(shape, /*copyGeom=*/true, /*copyMesh=*/false);
  const TopoDS_Shape copy = copier.Shape();

  IMeshTools_Parameters mesh_params;
  mesh_params.Deflection = tessellation.linear_deflection.numerical_value_in(k_mm);
  mesh_params.Angle = tessellation.angular_deflection.numerical_value_in(si::radian);
  mesh_params.Relative = false;
  mesh_params.InParallel = false;  // GEO-001: no dependence on thread scheduling
  BRepMesh_IncrementalMesh mesher(copy, mesh_params);
  if (!mesher.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "triangulation for STL failed", "export");
  }

  StlAPI_Writer writer;
  writer.ASCIIMode() = false;  // binary STL: compact and free of locale-dependent number text
  std::ostringstream out;
  if (!writer.Write(copy, out)) {
    return core::fail(core::ErrorCode::geometry_failed, "STL writing failed", "export");
  }
  return std::move(out).str();
}

// --- construction operations (M2) -------------------------------------------------------------

constexpr double k_spline_tolerance_mm = 1e-6;  // interpolation tolerance in model units (mm)

core::Result<gp_Pnt> point_mm(const port::Point3& p, const std::string& subject) {
  const double x = p.x.numerical_value_in(k_mm);
  const double y = p.y.numerical_value_in(k_mm);
  const double z = p.z.numerical_value_in(k_mm);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    return core::fail(core::ErrorCode::geometry_failed, subject + " must be finite", subject);
  }
  return gp_Pnt(x, y, z);
}

// An interpolating B-spline edge through the points (a straight edge for two points).
core::Result<TopoDS_Edge> spline_edge(const std::vector<port::Point3>& points,
                                      const std::string& subject) {
  if (points.size() < 2) {
    return core::fail(core::ErrorCode::geometry_failed, subject + " needs at least 2 points",
                      subject, {{"count", std::to_string(points.size())}});
  }
  NCollection_Array1<gp_Pnt> local(1, static_cast<int>(points.size()));
  for (std::size_t i = 0; i < points.size(); ++i) {
    auto p = point_mm(points.at(i), std::format("{}[{}]", subject, i));
    if (!p) {
      return std::unexpected{std::move(p).error()};
    }
    if (i > 0 && p->Distance(local.Value(static_cast<int>(i))) <= k_spline_tolerance_mm) {
      return core::fail(core::ErrorCode::geometry_failed, "consecutive points coincide",
                        std::format("{}[{}]", subject, i));
    }
    local.SetValue(static_cast<int>(i) + 1, *p);
  }
  const Handle(NCollection_HArray1<gp_Pnt>) array = new NCollection_HArray1<gp_Pnt>(local);
  GeomAPI_Interpolate interpolate(array, /*periodic=*/false, k_spline_tolerance_mm);
  interpolate.Perform();
  if (!interpolate.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "spline interpolation failed", subject);
  }
  BRepBuilderAPI_MakeEdge edge(interpolate.Curve());
  if (!edge.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "spline edge could not be built", subject);
  }
  return edge.Edge();
}

// The closed wire of a SplineSection: upper, trailing line, lower (reversed), leading line; an end
// line is left out where the two curves already meet.
core::Result<TopoDS_Wire> section_wire(const port::SplineSection& section, std::size_t index) {
  const std::string prefix = std::format("sections[{}].", index);
  auto upper = spline_edge(section.upper, prefix + "upper");
  if (!upper) {
    return std::unexpected{std::move(upper).error()};
  }
  auto lower = spline_edge(section.lower, prefix + "lower");
  if (!lower) {
    return std::unexpected{std::move(lower).error()};
  }
  BRepBuilderAPI_MakeWire wire;
  wire.Add(*upper);
  const auto join = [&](const port::Point3& a, const port::Point3& b) -> core::Status {
    auto pa = point_mm(a, prefix + "end");
    auto pb = point_mm(b, prefix + "end");
    if (!pa || !pb) {
      return core::fail(core::ErrorCode::geometry_failed, "section end point is not finite",
                        prefix + "end");
    }
    if (pa->Distance(*pb) > k_spline_tolerance_mm) {
      wire.Add(BRepBuilderAPI_MakeEdge(*pa, *pb).Edge());
    }
    return {};
  };
  if (auto ok = join(section.upper.back(), section.lower.back()); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  wire.Add(*lower);
  if (auto ok = join(section.lower.front(), section.upper.front()); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  if (!wire.IsDone() || !wire.Wire().Closed()) {
    return core::fail(core::ErrorCode::geometry_failed, "section outline is not a closed wire",
                      std::format("sections[{}]", index));
  }
  return wire.Wire();
}

// GEO-002: the shape's only solid, checked, or geometry_failed naming `subject`.
core::Result<std::unique_ptr<port::Solid>> one_valid_solid(const TopoDS_Shape& shape,
                                                           const std::string& subject) {
  const auto topology = count_topology(shape);
  if (topology.solids != 1) {
    return core::fail(core::ErrorCode::geometry_failed, "the result is not one solid", subject,
                      {{"solids", std::to_string(topology.solids)}});
  }
  TopExp_Explorer solid{shape, TopAbs_SOLID};
  TopoDS_Shape result = solid.Current();
  if (auto valid = port::require_valid(check_shape(result), subject); !valid) {
    return std::unexpected{std::move(valid).error()};
  }
  return std::make_unique<OcctSolid>(std::move(result));
}

template <class Boolean>
core::Result<std::unique_ptr<port::Solid>> run_boolean(const TopoDS_Shape& a,
                                                       NCollection_List<TopoDS_Shape> tools,
                                                       const std::string& subject) {
  Boolean op;
  NCollection_List<TopoDS_Shape> arguments;
  arguments.Append(a);
  op.SetArguments(arguments);
  op.SetTools(tools);
  op.SetRunParallel(false);  // serial, so the result does not depend on scheduling (GEO-001)
  op.Build();
  if (!op.IsDone() || op.HasErrors()) {
    std::ostringstream text;
    op.DumpErrors(text);
    return core::fail(core::ErrorCode::geometry_failed, "boolean operation failed", subject,
                      {{"occt_errors", text.str()}});
  }
  return one_valid_solid(op.Shape(), subject);
}

core::Result<std::unique_ptr<port::Solid>> build_loft(
    const std::vector<port::SplineSection>& sections) {
  if (sections.size() < 2) {
    return core::fail(core::ErrorCode::geometry_failed, "a loft needs at least 2 sections",
                      "sections", {{"count", std::to_string(sections.size())}});
  }
  BRepOffsetAPI_ThruSections loft(/*isSolid=*/true, /*ruled=*/false);
  for (std::size_t i = 0; i < sections.size(); ++i) {
    auto wire = section_wire(sections.at(i), i);
    if (!wire) {
      return std::unexpected{std::move(wire).error()};
    }
    loft.AddWire(*wire);
  }
  loft.Build();
  if (!loft.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "loft failed", "loft");
  }
  return one_valid_solid(loft.Shape(), "loft");
}

core::Result<std::unique_ptr<port::Solid>> build_revolve(const port::RevolveProfile& profile) {
  if (profile.rz.size() < 3) {
    return core::fail(core::ErrorCode::geometry_failed, "a profile needs at least 3 points",
                      "profile", {{"count", std::to_string(profile.rz.size())}});
  }
  BRepBuilderAPI_MakePolygon polygon;
  for (std::size_t i = 0; i < profile.rz.size(); ++i) {
    const double r = profile.rz.at(i)[0].numerical_value_in(k_mm);
    const double z = profile.rz.at(i)[1].numerical_value_in(k_mm);
    if (!std::isfinite(r) || !std::isfinite(z) || r < 0.0) {
      return core::fail(core::ErrorCode::geometry_failed,
                        "profile points must be finite with r >= 0", std::format("profile[{}]", i));
    }
    polygon.Add(gp_Pnt(r, 0.0, z));
  }
  polygon.Close();
  if (!polygon.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "profile polygon could not be built",
                      "profile");
  }
  BRepBuilderAPI_MakeFace face(polygon.Wire(), /*OnlyPlane=*/true);
  if (!face.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "profile is not a planar face", "profile");
  }
  BRepPrimAPI_MakeRevol revol(face.Face(), gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
                              2.0 * std::numbers::pi);
  revol.Build();
  if (!revol.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "revolution failed", "profile");
  }
  return one_valid_solid(revol.Shape(), "revolve");
}

core::Result<std::unique_ptr<port::Solid>> build_pattern(const TopoDS_Shape& base,
                                                         const TopoDS_Shape& item,
                                                         std::size_t count) {
  if (count < 1) {
    return core::fail(core::ErrorCode::geometry_failed, "a pattern needs at least 1 copy", "count");
  }
  NCollection_List<TopoDS_Shape> tools;
  for (std::size_t k = 0; k < count; ++k) {
    gp_Trsf rotation;
    rotation.SetRotation(
        gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
        2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(count));
    BRepBuilderAPI_Transform copy(item, rotation, /*Copy=*/true);
    if (!copy.IsDone()) {
      return core::fail(core::ErrorCode::geometry_failed, "pattern copy failed",
                        std::format("copy[{}]", k));
    }
    tools.Append(copy.Shape());
  }
  return run_boolean<BRepAlgoAPI_Fuse>(base, tools, "pattern");
}

// The sharp, open edges of `shape` whose sampled points all lie within the rule's tolerance of the
// cylinder (EdgesOnCylinder). `shape` must be a private copy: edge regularity is encoded on it.
// Smooth edges (tangent-continuous neighbouring faces, such as a cylinder's seam or a face split by
// a boolean) and closed edges are not feature edges and are never selected.
std::vector<TopoDS_Edge> select_edges(const TopoDS_Shape& shape,
                                      const port::EdgesOnCylinder& rule) {
  constexpr int k_samples = 9;
  constexpr double k_smooth_angle = 1e-4;  // rad: neighbouring faces closer than this are smooth
  const double radius = rule.radius.numerical_value_in(k_mm);
  const double tolerance = rule.tolerance.numerical_value_in(k_mm);
  BRepLib::EncodeRegularity(shape, k_smooth_angle);
  NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>
      edge_faces;
  TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edge_faces);
  std::vector<TopoDS_Edge> selected;
  for (int i = 1; i <= edge_faces.Extent(); ++i) {
    const TopoDS_Edge& edge = TopoDS::Edge(edge_faces.FindKey(i));
    if (BRep_Tool::Degenerated(edge) || BRep_Tool::IsClosed(edge)) {
      continue;
    }
    const auto& faces = edge_faces.FindFromIndex(i);
    // In a valid solid an edge bounded by fewer than two distinct faces is a seam.
    bool smooth = faces.Size() < 2;
    if (faces.Size() == 2) {
      const TopoDS_Face& f1 = TopoDS::Face(faces.First());
      const TopoDS_Face& f2 = TopoDS::Face(faces.Last());
      smooth = f1.IsSame(f2) || (BRep_Tool::HasContinuity(edge, f1, f2) &&
                                 BRep_Tool::Continuity(edge, f1, f2) != GeomAbs_C0);
    }
    if (smooth) {
      continue;
    }
    const BRepAdaptor_Curve curve(edge);
    bool on = true;
    for (int s = 0; s < k_samples && on; ++s) {
      const double t = curve.FirstParameter() + ((curve.LastParameter() - curve.FirstParameter()) *
                                                 static_cast<double>(s) / (k_samples - 1));
      const gp_Pnt p = curve.Value(t);
      on = std::abs(std::hypot(p.X(), p.Y()) - radius) <= tolerance;
    }
    if (on) {
      selected.push_back(edge);
    }
  }
  return selected;
}

// A deep copy of a shape, so that encoding regularity never touches a solid the caller holds.
TopoDS_Shape private_copy(const TopoDS_Shape& shape) {
  BRepBuilderAPI_Copy copy(shape, /*copyGeom=*/true, /*copyMesh=*/false);
  return copy.Shape();
}

core::Status validate(const port::EdgesOnCylinder& rule) {
  if (auto ok = require_positive(rule.radius.numerical_value_in(k_mm), "rule.radius"); !ok) {
    return ok;
  }
  return require_positive(rule.tolerance.numerical_value_in(k_mm), "rule.tolerance");
}

core::Result<std::unique_ptr<port::Solid>> build_fillet(const TopoDS_Shape& shape,
                                                        const port::EdgesOnCylinder& rule,
                                                        double radius_mm) {
  const TopoDS_Shape copy = private_copy(shape);
  const auto edges = select_edges(copy, rule);
  if (edges.empty()) {
    return core::fail(core::ErrorCode::geometry_failed, "the edge rule selects no edge", "rule");
  }
  BRepFilletAPI_MakeFillet fillet(copy);
  for (const auto& edge : edges) {
    fillet.Add(radius_mm, edge);
  }
  fillet.Build();
  if (!fillet.IsDone()) {
    return core::fail(core::ErrorCode::geometry_failed, "fillet failed", "fillet",
                      {{"edges", std::to_string(edges.size())}});
  }
  return one_valid_solid(fillet.Shape(), "fillet");
}

}  // namespace

OcctBackend::OcctBackend() { init_occt_messenger(); }

core::Result<std::unique_ptr<port::Solid>> OcctBackend::build_test_solid(
    const port::TestSolidParams& params) const {
  if (auto ok = validate(params); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  return guarded("build_test_solid", [&] { return build(params); });
}

core::Result<port::ValidityReport> OcctBackend::check_validity(const port::Solid& solid) const {
  const auto shape = shape_of(solid);
  if (!shape) {
    return std::unexpected{shape.error()};
  }
  return guarded("check_validity",
                 [&] { return core::Result<port::ValidityReport>{check_shape(**shape)}; });
}

core::Result<port::Topology> OcctBackend::topology(const port::Solid& solid) const {
  const auto shape = shape_of(solid);
  if (!shape) {
    return std::unexpected{shape.error()};
  }
  return guarded("topology", [&] { return core::Result<port::Topology>{count_topology(**shape)}; });
}

core::Result<port::MassProperties> OcctBackend::mass_properties(const port::Solid& solid) const {
  const auto shape = shape_of(solid);
  if (!shape) {
    return std::unexpected{shape.error()};
  }
  return guarded("mass_properties",
                 [&] { return core::Result<port::MassProperties>{measure(**shape)}; });
}

core::Result<std::unique_ptr<port::Solid>> OcctBackend::loft(
    const std::vector<port::SplineSection>& sections) const {
  return guarded("loft", [&] { return build_loft(sections); });
}

core::Result<std::unique_ptr<port::Solid>> OcctBackend::revolve(
    const port::RevolveProfile& profile) const {
  return guarded("revolve", [&] { return build_revolve(profile); });
}

core::Result<std::unique_ptr<port::Solid>> OcctBackend::fuse_circular_pattern(
    const port::Solid& base, const port::Solid& item, std::size_t count) const {
  const auto b = shape_of(base);
  const auto i = shape_of(item);
  if (!b) {
    return std::unexpected{b.error()};
  }
  if (!i) {
    return std::unexpected{i.error()};
  }
  return guarded("fuse_circular_pattern", [&] { return build_pattern(**b, **i, count); });
}

core::Result<std::unique_ptr<port::Solid>> OcctBackend::fuse(const port::Solid& a,
                                                             const port::Solid& b) const {
  const auto sa = shape_of(a);
  const auto sb = shape_of(b);
  if (!sa) {
    return std::unexpected{sa.error()};
  }
  if (!sb) {
    return std::unexpected{sb.error()};
  }
  return guarded("fuse", [&] {
    NCollection_List<TopoDS_Shape> tools;
    tools.Append(**sb);
    return run_boolean<BRepAlgoAPI_Fuse>(**sa, tools, "fuse");
  });
}

core::Result<std::unique_ptr<port::Solid>> OcctBackend::cut(const port::Solid& a,
                                                            const port::Solid& b) const {
  const auto sa = shape_of(a);
  const auto sb = shape_of(b);
  if (!sa) {
    return std::unexpected{sa.error()};
  }
  if (!sb) {
    return std::unexpected{sb.error()};
  }
  return guarded("cut", [&] {
    NCollection_List<TopoDS_Shape> tools;
    tools.Append(**sb);
    return run_boolean<BRepAlgoAPI_Cut>(**sa, tools, "cut");
  });
}

core::Result<port::EdgeSelection> OcctBackend::select(const port::Solid& solid,
                                                      const port::EdgesOnCylinder& rule) const {
  const auto shape = shape_of(solid);
  if (!shape) {
    return std::unexpected{shape.error()};
  }
  if (auto ok = validate(rule); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  return guarded("select", [&] {
    const auto edges = select_edges(private_copy(**shape), rule);
    double length = 0.0;
    for (const auto& edge : edges) {
      GProp_GProps props;
      BRepGProp::LinearProperties(edge, props);
      length += props.Mass();
    }
    return core::Result<port::EdgeSelection>{
        port::EdgeSelection{.count = edges.size(), .length = length * isq::length[k_mm]}};
  });
}

core::Result<std::unique_ptr<port::Solid>> OcctBackend::fillet(const port::Solid& solid,
                                                               const port::EdgesOnCylinder& rule,
                                                               core::Length radius) const {
  const auto shape = shape_of(solid);
  if (!shape) {
    return std::unexpected{shape.error()};
  }
  if (auto ok = validate(rule); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  const double radius_mm = radius.numerical_value_in(k_mm);
  if (auto ok = require_positive(radius_mm, "radius"); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  return guarded("fillet", [&] { return build_fillet(**shape, rule, radius_mm); });
}

core::Result<std::string> OcctBackend::export_bytes(const port::Solid& solid,
                                                    const port::ExportRequest& request) const {
  const auto shape = shape_of(solid);
  if (!shape) {
    return std::unexpected{shape.error()};
  }
  switch (request.format) {
    case port::ExportFormat::step:
      return guarded("export_step", [&] { return step_bytes(**shape); });
    case port::ExportFormat::stl: {
      if (!request.tessellation) {
        return core::fail(core::ErrorCode::invalid_input, "STL export needs a tessellation",
                          "tessellation");
      }
      if (auto ok = validate(*request.tessellation); !ok) {
        return std::unexpected{std::move(ok).error()};
      }
      return guarded("export_stl", [&] { return stl_bytes(**shape, *request.tessellation); });
    }
  }
  return core::fail(core::ErrorCode::invalid_input, "unknown export format", "format");
}

}  // namespace cemkit::geometry::occt

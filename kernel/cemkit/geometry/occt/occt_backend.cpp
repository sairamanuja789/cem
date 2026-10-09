#include "cemkit/geometry/occt/occt_backend.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <APIHeaderSection_MakeHeader.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IMeshTools_Parameters.hxx>
#include <NCollection_List.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <StepBasic_Product.hxx>
#include <StepData_StepModel.hxx>
#include <StlAPI_Writer.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <cmath>
#include <cstddef>
#include <format>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/geometry/occt/guard.hpp"
#include "cemkit/geometry/occt/shape_checks.hpp"

namespace cemkit::geometry::occt {

namespace {

namespace si = mp_units::si;
constexpr auto k_mm = si::milli<si::metre>;

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

}  // namespace

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

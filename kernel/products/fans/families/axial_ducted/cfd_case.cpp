// "rotor_cfd_case" (M5; SIM-001, SIM-003): the solver cases of one designed rotor at several
// operating points, written by the case writer the C ABI lends (OperationContext::case_writer).
// The family states WHAT to simulate (one blade passage of its geometry in the duct, its speed and
// flows); the writer's versioned template states HOW. Nothing here runs a solver.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/product/geometry_json.hpp"
#include "cemkit/product/json_io.hpp"
#include "cemkit/product/operations.hpp"
#include "cemkit/simulation/port/case.hpp"
#include "products/fans/families/axial_ducted/geometry.hpp"

namespace cemkit::fans::axial_ducted {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace port = geometry::port;
using product::Json;
using product::number;

core::Length len(double v) { return v * isq::length[si::metre]; }

// args: geometry_args (ducted_fan_geometry args), flows (m3/s, the operating points), omega
// (rad/s), density (kg/m3), dynamic_viscosity (Pa s), mesh {cell_size (m), blade_refinement,
// rotor_refinement, iterations}, inlet {turbulence_intensity (1), mixing_length (m)}, domain
// {upstream, downstream (m, from the rotor zone ends)}, blade_stl {linear_deflection (m),
// angular_deflection (rad)}.
core::Result<Json> rotor_cfd_case(const Json& c, const product::OperationContext& context) {
  if (context.case_writer == nullptr) {
    return core::fail(core::ErrorCode::not_implemented, "no solver case writer is linked",
                      "case_writer");
  }
  const Json& a = product::member(c, "args");
  const auto params = fan_geometry_params_of(product::member(a, "geometry_args"));
  auto g = build_fan_geometry(context.geometry, params);
  if (!g) {
    return std::unexpected(g.error());
  }
  const Json& tess = product::member(a, "blade_stl");
  auto stl = context.geometry.export_bytes(
      *g->blade,
      port::ExportRequest{
          .format = port::ExportFormat::stl,
          .tessellation = port::Tessellation{
              .linear_deflection = len(number(tess, "linear_deflection")),
              .angular_deflection =
                  number(tess, "angular_deflection") * isq::angular_measure[si::radian]}});
  if (!stl) {
    return std::unexpected(stl.error());
  }
  const Json& mesh = product::member(a, "mesh");
  const Json& inlet = product::member(a, "inlet");
  const Json& domain = product::member(a, "domain");
  const Json& flows = product::member(a, "flows");
  const Json& refine_blade = product::member(mesh, "blade_refinement");
  const Json& refine_zone = product::member(mesh, "rotor_refinement");
  const Json& iterations = product::member(mesh, "iterations");
  if (!flows.is_array() || flows.empty() || !refine_blade.is_number_integer() ||
      !refine_zone.is_number_integer() || !iterations.is_number_integer() ||
      iterations.get<long long>() <= 0) {
    throw product::BadRequest{"args", "flows must be a non-empty array and the mesh counts "
                                      "integers: args"};
  }
  // The rotating zone spans the hub (the rotor's axial extent, geometry.cpp centres it on z = 0).
  const double half = params.hub_length.numerical_value_in(si::metre) / 2.0;
  simulation::port::RotorSectorCase base{
      .sectors = params.blade_count,
      .hub_radius = params.hub_radius,
      .duct_radius = params.duct_inner_radius,
      .z_inlet = len(-half - number(domain, "upstream")),
      .z_outlet = len(half + number(domain, "downstream")),
      .rotor_half_length = len(half),
      .cell_size = len(number(mesh, "cell_size")),
      .blade_refinement = refine_blade.get<int>(),
      .rotor_refinement = refine_zone.get<int>(),
      .omega = number(a, "omega") * isq::angular_velocity[si::radian / si::second],
      .flow = 0.0 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .dynamic_viscosity =
          number(a, "dynamic_viscosity") * isq::dynamic_viscosity[si::pascal * si::second],
      .density = number(a, "density") * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)],
      .inlet_turbulence_intensity = number(inlet, "turbulence_intensity"),
      .inlet_mixing_length = len(number(inlet, "mixing_length")),
      .iterations = static_cast<std::size_t>(iterations.get<long long>()),
      .processes = mesh.contains("processes")
                       ? static_cast<std::size_t>(product::number(mesh, "processes"))
                       : std::size_t{1},
      .blade_stl_mm = std::vector<std::uint8_t>(stl->begin(), stl->end())};
  Json cases = Json::array();
  Json thresholds;
  Json monitors;
  std::string case_template;
  for (const auto& f : flows) {
    if (!f.is_number()) {
      throw product::BadRequest{"flows", "each flow must be a number (m3/s): flows"};
    }
    simulation::port::RotorSectorCase one = base;
    one.flow = f.get<double>() *
               core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
    auto files = context.case_writer->rotor_sector(one);
    if (!files) {
      return std::unexpected(files.error());
    }
    Json text = Json::object();
    for (const auto& [path, content] : files->text) {
      text[path] = content;
    }
    Json binary = Json::object();
    for (const auto& [path, bytes] : files->binary) {
      binary[path] = product::base64(std::string_view{
          reinterpret_cast<const char*>(bytes.data()), bytes.size()});  // NOLINT
    }
    Json steps = Json::array();
    for (const auto& s : files->steps) {
      steps.push_back(Json{{"name", s.name}, {"argv", s.argv}});
    }
    cases.push_back(Json{{"flow", f.get<double>()},
                         {"files", std::move(text)},
                         {"binary_files", std::move(binary)},
                         {"steps", std::move(steps)}});
    const auto& t = files->thresholds;
    thresholds = Json{{"residuals", t.residuals},
                      {"mass_imbalance", t.mass_imbalance},
                      {"flat_fraction", t.flat_fraction},
                      {"flatness", t.flatness},
                      {"monitor_scales", t.monitor_scales},
                      {"sources", t.sources}};
    monitors = Json(files->monitors);
    case_template = core::to_string(files->case_template);
  }
  return Json{{"solver", context.case_writer->solver()},
              {"case_template", case_template},
              {"sectors", params.blade_count},
              {"thresholds", std::move(thresholds)},
              {"monitors", std::move(monitors)},
              {"cases", std::move(cases)}};
}

// "rotor_cfd_metrics" (SIM-004, TRUST-001): the integrated quantities of one solved case, from the
// final values of its monitors (kinematic pressures, one passage). Only a CONVERGED or VERIFIED
// result gives numbers (TRUST-004); any other status is sim_untrusted, never a number. CONVERGED is
// L2 simulated; VERIFIED is L2 verified and needs each quantity's discretisation uncertainty (the
// mesh study's GCI times the fine-mesh value). Definitions (docs/models/fans/axial-cfd.md):
//   Q = N Q_outlet; dp_s = rho (p_out - p_in) (area-averaged static); dp_t = rho (pt_out - pt_in)
//   (flow-weighted total, outlet swirl included); T = N M_z (fluid moment on the rotor about +z;
//   the rotor turns about -z, so T > 0 absorbs power); P = T omega; eta_t = Q dp_t / P (rotor
//   total-pressure efficiency, swirl credited); eta_s = Q dp_s / P (static-pressure efficiency).
core::Result<Json> metrics_op(const Json& c, const product::OperationContext& /*context*/) {
  return rotor_cfd_metrics(product::member(c, "args"));
}

}  // namespace

core::Result<Json> rotor_cfd_metrics(const Json& a) {
  const std::string status = product::text(a, "status");
  if (status != "CONVERGED" && status != "VERIFIED") {
    return core::fail(core::ErrorCode::sim_untrusted,
                      "the result did not pass the trust gate: " + status, "status");
  }
  const auto& sectors_json = product::member(a, "sectors");
  if (!sectors_json.is_number_integer() || sectors_json.get<long long>() <= 0) {
    throw product::BadRequest{"sectors", "must be a positive integer: sectors"};
  }
  const auto n = static_cast<double>(sectors_json.get<long long>());
  const double rho = number(a, "density");
  const double omega = number(a, "omega");
  const double q = n * number(a, "outlet_flow");
  const double dps = rho * (number(a, "p_out") - number(a, "p_in"));
  const double dpt = rho * (number(a, "pt_out") - number(a, "pt_in"));
  const double torque = n * number(a, "moment_z");
  const double power = torque * omega;
  const auto model = core::ModelId{.name = "fans.axial.cfd_metrics",
                                   .version = {.major = 0, .minor = 1, .patch = 0}};
  const auto fidelity = status == "VERIFIED" ? core::Fidelity::l2_verified
                                             : core::Fidelity::l2_simulated;
  const Json* uncertainty = a.contains("uncertainty") ? &a.at("uncertainty") : nullptr;
  if (fidelity == core::Fidelity::l2_verified && uncertainty == nullptr) {
    throw product::BadRequest{"uncertainty", "a VERIFIED result needs its uncertainties"};
  }
  const auto value = [&](const char* key, double v, const char* unit) {
    Json out{{"value", v},
             {"unit", unit},
             {"fidelity", std::string{core::to_string(fidelity)}},
             {"fidelity_label", std::string{core::report_label(fidelity)}},
             {"model", core::to_string(model)}};
    if (uncertainty != nullptr && uncertainty->contains(key)) {
      out["uncertainty"] = uncertainty->at(key);
    }
    return out;
  };
  Json out{{"flow", value("flow", q, "m3/s")},
           {"static_pressure_rise", value("static_pressure_rise", dps, "Pa")},
           {"total_pressure_rise", value("total_pressure_rise", dpt, "Pa")},
           {"torque", value("torque", torque, "N m")},
           {"shaft_power", value("shaft_power", power, "W")},
           {"definitions",
            "static rise: area-averaged inlet to outlet; total rise: flow-weighted, outlet swirl "
            "included (rotor without stator); total-pressure efficiency Q dp_t / P; "
            "static-pressure efficiency Q dp_s / P"}};
  if (power > 0.0) {
    out["total_pressure_efficiency"] =
        value("total_pressure_efficiency", q * dpt / power, "1");
    out["static_pressure_efficiency"] =
        value("static_pressure_efficiency", q * dps / power, "1");
  } else {
    out["note"] = "the rotor absorbs no power at this point; no efficiency is defined";
  }
  return out;
}

core::Status register_cfd_operation(product::Operations& operations) {
  if (auto ok = operations.add("rotor_cfd_case", rotor_cfd_case); !ok) {
    return ok;
  }
  return operations.add("rotor_cfd_metrics", metrics_op);
}

}  // namespace cemkit::fans::axial_ducted

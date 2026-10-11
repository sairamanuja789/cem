// "rotor_cfd_case" (M5; SIM-001, SIM-003): the solver cases of one designed rotor at several
// operating points, written by the case writer the C ABI lends (OperationContext::case_writer).
// The family states WHAT to simulate (one blade passage of its geometry in the duct, its speed and
// flows); the writer's versioned template states HOW. Nothing here runs a solver.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
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
  if (auto ok = blade_fits_passage(params); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  const Json& tess = product::member(a, "blade_stl");
  auto stl = context.geometry.export_bytes(
      *g->blade,
      port::ExportRequest{.format = port::ExportFormat::stl,
                          .tessellation = port::Tessellation{
                              .linear_deflection = len(number(tess, "linear_deflection")),
                              .angular_deflection = number(tess, "angular_deflection") *
                                                    isq::angular_measure[si::radian]}});
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
    throw product::BadRequest{"args",
                              "flows must be a non-empty array and the mesh counts "
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
      .density =
          number(a, "density") * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)],
      .inlet_turbulence_intensity = number(inlet, "turbulence_intensity"),
      .inlet_mixing_length = len(number(inlet, "mixing_length")),
      .iterations = static_cast<std::size_t>(iterations.get<long long>()),
      .processes = std::size_t{1},
      .blade_stl_mm = std::vector<std::uint8_t>(stl->begin(), stl->end())};
  if (mesh.contains("processes")) {
    const Json& p = mesh.at("processes");
    if (!p.is_number_integer() || p.get<long long>() <= 0) {
      throw product::BadRequest{"processes", "must be a positive integer: processes"};
    }
    base.processes = static_cast<std::size_t>(p.get<long long>());
  }
  Json cases = Json::array();
  Json thresholds;
  Json monitors;
  std::string case_template;
  for (const auto& f : flows) {
    if (!f.is_number()) {
      throw product::BadRequest{"flows", "each flow must be a number (m3/s): flows"};
    }
    simulation::port::RotorSectorCase one = base;
    one.flow =
        f.get<double>() * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
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
      binary[path] = product::base64(
          std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});  // NOLINT
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
    thresholds = Json{{"residuals", t.residuals},           {"mass_imbalance", t.mass_imbalance},
                      {"flat_fraction", t.flat_fraction},   {"flatness", t.flatness},
                      {"monitor_scales", t.monitor_scales}, {"sources", t.sources}};
    monitors = Json(files->monitors);
    case_template = core::to_string(files->case_template);
  }
  return Json{{"solver", context.case_writer->solver()}, {"case_template", case_template},
              {"sectors", params.blade_count},           {"thresholds", std::move(thresholds)},
              {"monitors", std::move(monitors)},         {"cases", std::move(cases)}};
}

// "rotor_cfd_metrics" (SIM-004, TRUST-001): the integrated quantities of one solved case, from the
// final values of its monitors (kinematic pressures, one passage). Only a CONVERGED or VERIFIED
// result gives numbers (TRUST-004); any other status is sim_untrusted, never a number. The status
// is the orchestration trust gate's (python/cemkit/simulation/trust_gate.py); this operation
// cannot re-check it. A quantity is L2 verified only when it carries its own discretisation
// uncertainty (mesh study, TRUST-002); every other quantity of a VERIFIED case stays L2 simulated.
// Definitions (docs/models/fans/axial-cfd.md; pressure types after ISO 5801, requirements
// section 2):
//   Q = N Q_outlet;
//   dp_t = rho (pt_out - pt_in): rotor total pressure rise, both totals volume-flow-weighted,
//     outlet swirl included (a rotor without a stator does not deliver it);
//   dp_ts = rho (p_out - pt_in): total-to-static rise, area-averaged outlet static minus inlet
//     total (the ISO 5801 fan static pressure form p_s2 - p_t1, in this rotor domain);
//   T = N M_z, the fluid moment on the rotating surfaces about +z (the rotor turns about -z, so
//     T > 0 absorbs power); P = T omega;
//   eta_t = Q dp_t / P (total efficiency), eta_ts = Q dp_ts / P (total-to-static efficiency),
//     reported only where the rise is positive.
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
  const double dpt = rho * (number(a, "pt_out") - number(a, "pt_in"));
  const double dpts = rho * (number(a, "p_out") - number(a, "pt_in"));
  const double torque = n * number(a, "moment_z");
  const double power = torque * omega;
  const auto model = core::ModelId{.name = "fans.axial.cfd_metrics",
                                   .version = {.major = 0, .minor = 2, .patch = 0}};
  const Json* uncertainty = a.contains("uncertainty") ? &a.at("uncertainty") : nullptr;
  if (status == "VERIFIED" && uncertainty == nullptr) {
    throw product::BadRequest{"uncertainty", "a VERIFIED result needs its uncertainties"};
  }
  const auto value = [&](const char* key, double v, const char* unit) {
    const bool verified =
        status == "VERIFIED" && uncertainty != nullptr && uncertainty->contains(key);
    const auto fidelity = verified ? core::Fidelity::l2_verified : core::Fidelity::l2_simulated;
    Json out{{"value", v},
             {"unit", unit},
             {"fidelity", std::string{core::to_string(fidelity)}},
             {"fidelity_label", std::string{core::report_label(fidelity)}},
             {"model", core::to_string(model)}};
    if (verified) {
      out["uncertainty"] = uncertainty->at(key);
    }
    return out;
  };
  Json out{{"flow", value("flow", q, "m3/s")},
           {"total_pressure_rise", value("total_pressure_rise", dpt, "Pa")},
           {"total_to_static_pressure_rise", value("total_to_static_pressure_rise", dpts, "Pa")},
           {"torque", value("torque", torque, "N m")},
           {"shaft_power", value("shaft_power", power, "W")},
           {"definitions",
            "total rise: volume-flow-weighted totals, outlet swirl included (rotor without "
            "stator); total-to-static rise: outlet area-averaged static minus inlet total; "
            "efficiencies Q dp / P, reported only for a positive rise; rotor-domain quantities, "
            "not an ISO 5801 test arrangement"}};
  Json notes = Json::array();
  if (!(power > 0.0)) {
    notes.push_back("the rotor absorbs no power at this point; no efficiency is defined");
  } else {
    if (dpt > 0.0) {
      out["total_efficiency"] = value("total_efficiency", q * dpt / power, "1");
    } else {
      notes.push_back("total pressure rise not positive: no total efficiency");
    }
    if (dpts > 0.0) {
      out["total_to_static_efficiency"] =
          value("total_to_static_efficiency", q * dpts / power, "1");
    } else {
      notes.push_back("total-to-static rise not positive: no total-to-static efficiency");
    }
  }
  if (!notes.empty()) {
    out["notes"] = std::move(notes);
  }
  return out;
}

core::Status blade_fits_passage(const FanGeometryParams& params) {
  // The passage holds exactly one blade: every section point must lie strictly inside +-pi/N about
  // +x (sections at or outside the hub radius), or the periodic sides would cut the blade.
  if (params.blade_count == 0) {
    return core::fail(core::ErrorCode::geometry_failed, "no blades", "blade_count");
  }
  const double half_passage = std::numbers::pi / static_cast<double>(params.blade_count);
  for (std::size_t i = 0; i < params.stations.size(); ++i) {
    // A section inside the hub (the embedded root) is not in the fluid domain.
    if (params.stations[i].radius < params.hub_radius) {
      continue;
    }
    const auto section = blade_section(params.stations[i], 64);
    for (const auto* curve : {&section.upper, &section.lower}) {
      for (const auto& pt : *curve) {
        const double angle =
            std::atan2(pt.y.numerical_value_in(si::metre), pt.x.numerical_value_in(si::metre));
        if (!(std::abs(angle) < half_passage)) {
          return core::fail(core::ErrorCode::geometry_failed,
                            "the blade is wider than its passage (pi/N about the blade axis)",
                            "stations[" + std::to_string(i) + "]");
        }
      }
    }
  }
  return {};
}

core::Status register_cfd_operation(product::Operations& operations) {
  if (auto ok = operations.add("rotor_cfd_case", rotor_cfd_case); !ok) {
    return ok;
  }
  return operations.add("rotor_cfd_metrics", metrics_op);
}

}  // namespace cemkit::fans::axial_ducted

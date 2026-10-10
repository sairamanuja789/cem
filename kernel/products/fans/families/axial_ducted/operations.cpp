#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <string>

#include "cemkit/product/json_io.hpp"
#include "products/fans/families/axial_ducted/free_vortex.hpp"
#include "products/fans/families/axial_ducted/geometry.hpp"
#include "products/fans/families/axial_ducted/mean_line.hpp"

namespace cemkit::fans::axial_ducted {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using product::Json;
using product::number;

// "mean_line" (PHY-005 cross-check through cemkit_l0_batch): args flow (m3/s), fan_total_pressure
// (Pa), omega (rad/s), r_tip and r_hub (m); air as for the L0 functions.
core::Result<Json> mean_line_case(const Json& c, const product::OperationContext& /*context*/) {
  const Json& a = product::member(c, "args");
  const auto r = mean_line(
      number(a, "flow") * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      number(a, "fan_total_pressure") * fan_total_pressure[si::pascal],
      number(a, "omega") * isq::angular_velocity[si::radian / si::second],
      number(a, "r_tip") * isq::length[si::metre], number(a, "r_hub") * isq::length[si::metre],
      product::air_of(c, "air"));
  if (!r) {
    return std::unexpected(r.error());
  }
  const auto ms = si::metre / si::second;
  return Json{{"value",
               {{"annulus_area", r->annulus_area.numerical_value_in(mp_units::square(si::metre))},
                {"mean_radius", r->mean_radius.numerical_value_in(si::metre)},
                {"blade_speed", r->blade_speed.numerical_value_in(ms)},
                {"axial_velocity", r->axial_velocity.numerical_value_in(ms)},
                {"outlet_swirl", r->outlet_swirl.numerical_value_in(ms)},
                {"relative_inlet_angle", r->relative_inlet_angle.numerical_value_in(si::radian)},
                {"relative_outlet_angle", r->relative_outlet_angle.numerical_value_in(si::radian)},
                {"absolute_outlet_angle", r->absolute_outlet_angle.numerical_value_in(si::radian)},
                {"relative_inlet_speed", r->relative_inlet_speed.numerical_value_in(ms)},
                {"relative_outlet_speed", r->relative_outlet_speed.numerical_value_in(ms)},
                {"flow_coefficient", r->flow_coefficient.numerical_value_in(mp_units::one)},
                {"work_coefficient", r->work_coefficient.numerical_value_in(mp_units::one)},
                {"de_haller_ratio", r->de_haller_ratio.numerical_value_in(mp_units::one)}}},
              {"fidelity", std::string{core::to_string(r->fidelity)}},
              {"fidelity_label", std::string{core::report_label(r->fidelity)}},
              {"model", core::to_string(r->model)}};
}

// "free_vortex_design" (PHY-005 cross-check through cemkit_l0_batch; the design step of
// cemkit design fan): args flow (m3/s), fan_total_pressure (Pa), omega (rad/s), r_tip, r_hub,
// chord_hub, chord_tip, thickness, embed_depth (m) and stations; air as for the L0 functions. With
// an optional "geometry" member (blade_count, hub_length, duct_inner_radius, duct_wall,
// duct_length, m) the value also carries "geometry_args", the ducted_fan_geometry request args
// that build these sections.
core::Result<Json> free_vortex_case(const Json& c, const product::OperationContext& /*context*/) {
  const Json& a = product::member(c, "args");
  const auto length = [&](const char* key) { return number(a, key) * isq::length[si::metre]; };
  const Json& count = product::member(a, "stations");
  if (!count.is_number_unsigned()) {
    throw product::BadRequest{"stations", "must be a non-negative integer: stations"};
  }
  const FreeVortexInputs in{
      .flow = number(a, "flow") *
              core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = number(a, "fan_total_pressure") * fan_total_pressure[si::pascal],
      .omega = number(a, "omega") * isq::angular_velocity[si::radian / si::second],
      .r_tip = length("r_tip"),
      .r_hub = length("r_hub"),
      .chord_hub = length("chord_hub"),
      .chord_tip = length("chord_tip"),
      .thickness = length("thickness"),
      .stations = count.get<std::size_t>(),
      .embed_depth = length("embed_depth")};
  const auto d = free_vortex_design(in, product::air_of(c, "air"));
  if (!d) {
    return std::unexpected(d.error());
  }
  const auto ms = si::metre / si::second;
  const auto metres = [](core::Length l) { return l.numerical_value_in(si::metre); };
  const auto rad = [](core::Angle x) { return x.numerical_value_in(si::radian); };
  const auto one = [](core::Ratio x) { return x.numerical_value_in(mp_units::one); };
  Json stations = Json::array();
  Json sections = Json::array();
  for (const auto& s : d->stations) {
    stations.push_back(Json{{"radius", metres(s.radius)},
                            {"blade_speed", s.blade_speed.numerical_value_in(ms)},
                            {"outlet_swirl", s.outlet_swirl.numerical_value_in(ms)},
                            {"relative_inlet_angle", rad(s.relative_inlet_angle)},
                            {"relative_outlet_angle", rad(s.relative_outlet_angle)},
                            {"camber_angle", rad(s.camber_angle)},
                            {"stagger_from_axis", rad(s.stagger_from_axis)},
                            {"recipe_stagger", rad(s.recipe_stagger)},
                            {"chord", metres(s.chord)},
                            {"camber", metres(s.camber)},
                            {"thickness", metres(s.thickness)},
                            {"de_haller_ratio", one(s.de_haller_ratio)},
                            {"reaction", one(s.reaction)},
                            {"embedded", s.embedded}});
    sections.push_back(Json{{"radius", metres(s.radius)},
                            {"chord", metres(s.chord)},
                            {"stagger", rad(s.recipe_stagger)},
                            {"camber", metres(s.camber)},
                            {"thickness", metres(s.thickness)}});
  }
  const MeanLine& ml = d->mean_line;
  Json value{{"axial_velocity", d->axial_velocity.numerical_value_in(ms)},
             {"mean_line",
              {{"mean_radius", metres(ml.mean_radius)},
               {"blade_speed", ml.blade_speed.numerical_value_in(ms)},
               {"flow_coefficient", one(ml.flow_coefficient)},
               {"work_coefficient", one(ml.work_coefficient)},
               {"de_haller_ratio", one(ml.de_haller_ratio)}}},
             {"stations", std::move(stations)}};
  if (a.contains("geometry")) {
    const Json& g = a.at("geometry");
    Json args{{"blade_count", product::member(g, "blade_count")},
              {"hub_radius", metres(in.r_hub)},
              {"hub_length", number(g, "hub_length")},
              {"duct_inner_radius", number(g, "duct_inner_radius")},
              {"duct_wall", number(g, "duct_wall")},
              {"duct_length", number(g, "duct_length")},
              {"stations", std::move(sections)}};
    value["geometry_args"] = std::move(args);
  }
  return Json{{"value", std::move(value)},
              {"fidelity", std::string{core::to_string(d->fidelity)}},
              {"fidelity_label", std::string{core::report_label(d->fidelity)}},
              {"model", core::to_string(d->model)}};
}

}  // namespace

core::Status register_operations(product::Operations& operations) {
  if (auto ok = register_geometry_operation(operations); !ok) {
    return ok;
  }
  if (auto ok = operations.add("mean_line", mean_line_case); !ok) {
    return ok;
  }
  if (auto ok = operations.add("free_vortex_design", free_vortex_case); !ok) {
    return ok;
  }
  if (auto ok = operations.add_model(mean_line_model()); !ok) {
    return ok;
  }
  return operations.add_model(free_vortex_model());
}

}  // namespace cemkit::fans::axial_ducted

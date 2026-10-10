#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <string>

#include "cemkit/product/json_io.hpp"
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

}  // namespace

core::Status register_operations(product::Operations& operations) {
  if (auto ok = register_geometry_operation(operations); !ok) {
    return ok;
  }
  if (auto ok = operations.add("mean_line", mean_line_case); !ok) {
    return ok;
  }
  return operations.add_model(mean_line_model());
}

}  // namespace cemkit::fans::axial_ducted

#include "products/fans/operations.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <string>
#include <utility>
#include <vector>

#include "products/fans/common/feasibility_json.hpp"
#include "products/fans/common/l0.hpp"

namespace cemkit::fans {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using product::Json;
using product::member;
using product::number;

core::AngularVelocity omega_of(double v) {
  return v * isq::angular_velocity[si::radian / si::second];
}
core::Length length_of(double v) { return v * isq::length[si::metre]; }
core::VolumeFlowRate flow_of(double v) {
  return v * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
}
FanTotalPressure total_of(double v) { return v * fan_total_pressure[si::pascal]; }

template <class R, class U>
core::Result<Json> labelled(const R& r, U unit) {
  if (!r) {
    return std::unexpected(r.error());
  }
  return product::labelled_json(*r, unit);
}

const Json& args_of(const Json& c) { return member(c, "args"); }

physics::Air air(const Json& c) { return product::air_of(c, "air"); }

}  // namespace

core::Status register_operations(product::Operations& operations) {
  const auto pa = si::pascal;
  const auto ms = si::metre / si::second;
  const auto one = mp_units::one;
  const std::vector<std::pair<const char*, product::Operation>> table{
      {"tip_speed",
       [ms](const Json& c, const product::OperationContext& /*context*/) {
         const Json& a = args_of(c);
         return labelled(tip_speed(omega_of(number(a, "omega")), length_of(number(a, "d_tip"))),
                         ms);
       }},
      {"max_fan_total_pressure",
       [pa](const Json& c, const product::OperationContext& /*context*/) {
         return labelled(max_fan_total_pressure(air(c)), pa);
       }},
      {"max_tip_speed",
       [ms](const Json& c, const product::OperationContext& /*context*/) {
         return labelled(max_tip_speed(air(c)), ms);
       }},
      {"flow_coefficient",
       [one](const Json& c, const product::OperationContext& /*context*/) {
         const Json& a = args_of(c);
         return labelled(flow_coefficient(flow_of(number(a, "flow")), length_of(number(a, "d_tip")),
                                          omega_of(number(a, "omega")), air(c)),
                         one);
       }},
      {"pressure_coefficient",
       [one](const Json& c, const product::OperationContext& /*context*/) {
         const Json& a = args_of(c);
         return labelled(pressure_coefficient(total_of(number(a, "fan_total_pressure")),
                                              length_of(number(a, "d_tip")),
                                              omega_of(number(a, "omega")), air(c)),
                         one);
       }},
      {"specific_speed",
       [one](const Json& c, const product::OperationContext& /*context*/) {
         const Json& a = args_of(c);
         return labelled(specific_speed(omega_of(number(a, "omega")), flow_of(number(a, "flow")),
                                        total_of(number(a, "fan_total_pressure")), air(c)),
                         one);
       }},
      {"specific_diameter",
       [one](const Json& c, const product::OperationContext& /*context*/) {
         const Json& a = args_of(c);
         return labelled(
             specific_diameter(length_of(number(a, "d_tip")), flow_of(number(a, "flow")),
                               total_of(number(a, "fan_total_pressure")), air(c)),
             one);
       }},
      {"conventional_dynamic_pressure",
       [pa](const Json& c, const product::OperationContext& /*context*/) {
         const Json& a = args_of(c);
         return labelled(conventional_dynamic_pressure(flow_of(number(a, "flow")),
                                                       length_of(number(a, "d_duct")), air(c)),
                         pa);
       }},
      {"fan_total_from_static",
       [pa](const Json& c, const product::OperationContext& /*context*/) -> core::Result<Json> {
         const Json& a = args_of(c);
         const auto r = fan_total_from_static(
             number(a, "fan_static_pressure") * fan_static_pressure[si::pascal],
             flow_of(number(a, "flow")), length_of(number(a, "d_duct")), air(c));
         if (!r) {
           return std::unexpected(r.error());
         }
         Json out = product::labelled_json(r->value, pa);
         out["provenance"] = std::string{core::to_string(r->provenance)};
         out["rule"] = std::string{r->rule};
         return out;
       }},
      {"scale_fan_laws",
       [pa](const Json& c, const product::OperationContext& /*context*/) -> core::Result<Json> {
         const Json& a = args_of(c);
         const physics::Air air_old = air(c);
         const Json& p = member(a, "point");
         const FanPoint point{.omega = omega_of(number(p, "omega")),
                              .d_tip = length_of(number(p, "d_tip")),
                              .flow = flow_of(number(p, "flow")),
                              .pressure = total_of(number(p, "fan_total_pressure")),
                              .power = number(p, "power") * isq::power[si::watt],
                              .air = air_old};
         const physics::Air air_new =
             c.contains("air_new") ? product::air_of(c, "air_new") : air_old;
         const auto r = scale_fan_laws(point, omega_of(number(a, "omega")),
                                       length_of(number(a, "d_tip")), air_new);
         if (!r) {
           return std::unexpected(r.error());
         }
         return Json{{"value",
                      {{"flow", r->flow.value().numerical_value_in(mp_units::cubic(si::metre) /
                                                                   si::second)},
                       {"fan_total_pressure", r->pressure.value().numerical_value_in(pa)},
                       {"power", r->power.value().numerical_value_in(si::watt)}}},
                     {"fidelity", std::string{core::to_string(r->flow.fidelity())}},
                     {"fidelity_label", std::string{core::report_label(r->flow.fidelity())}},
                     {"model", core::to_string(r->flow.model())}};
       }},
      {"check_feasibility",
       [](const Json& c, const product::OperationContext& /*context*/) -> core::Result<Json> {
         const auto r = run_feasibility(args_of(c), air(c), member(c, "range"));
         if (!r) {
           return std::unexpected(r.error());
         }
         return Json{{"report", report_json(*r)}};
       }},
  };
  for (const auto& [name, operation] : table) {
    if (auto ok = operations.add(name, operation); !ok) {
      return ok;
    }
  }
  for (auto model : {l0_model(), feasibility_model()}) {
    if (auto ok = operations.add_model(std::move(model)); !ok) {
      return ok;
    }
  }
  return {};
}

}  // namespace cemkit::fans

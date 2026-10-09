// Test-data tool for the PHY-005 cross-check (ctest fans_l0_crosscheck, scripts/crosscheck_l0.py).
// Reads a JSON array of cases on stdin, evaluates each with the kernel and writes a JSON array of
// results on stdout, in the same order. A case is
//   {"id": str, "function": str, "args": {...}, "air": {...}, ["air_new": {...}], ["range": {...}]}
// with every number in its coherent SI unit. A result is {"id", "value": number | object,
// "fidelity", "model"[, "provenance", "rule"]}, {"id", "report": {...}} or
// {"id", "error": {"code", "subject", "message", "details", "describe"}}.
#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "cemkit/core/error.hpp"
#include "cemkit/core/fidelity.hpp"
#include "cemkit/physics/air.hpp"
#include "products/fans/common/feasibility.hpp"
#include "products/fans/common/l0.hpp"

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace fans = cemkit::fans;
namespace core = cemkit::core;
namespace physics = cemkit::physics;
using json = nlohmann::ordered_json;

double num(const json& args, const char* key) { return args.at(key).get<double>(); }

core::AngularVelocity omega_of(double v) {
  return v * isq::angular_velocity[si::radian / si::second];
}
core::Length length_of(double v) { return v * isq::length[si::metre]; }
core::VolumeFlowRate flow_of(double v) {
  return v * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
}
fans::FanTotalPressure total_of(double v) { return v * fans::fan_total_pressure[si::pascal]; }

physics::Air air_of(const json& a) {
  return physics::Air{
      .density = num(a, "density") * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)],
      .temperature =
          mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(num(a, "temperature")),
      .pressure =
          core::units::absolute_zero_pressure + num(a, "pressure") * isq::pressure[si::pascal],
      .gamma = num(a, "gamma") * isq::ratio_of_specific_heat_capacities[mp_units::one],
      .gas_constant = num(a, "gas_constant") *
                      isq::specific_gas_constant[si::joule / (si::kilogram * si::kelvin)],
  };
}

json error_json(const core::Error& e) {
  json details = json::object();
  for (const auto& [key, value] : e.details()) {
    details[key] = value;
  }
  return json{{"code", std::string{core::to_string(e.code())}},
              {"subject", e.subject()},
              {"message", e.message()},
              {"details", details},
              {"describe", core::describe(e)}};
}

template <class Q, class U>
json labelled_json(const core::Labelled<Q>& l, U unit) {
  return json{{"value", l.value().numerical_value_in(unit)},
              {"fidelity", std::string{core::to_string(l.fidelity())}},
              {"model", core::to_string(l.model())}};
}

template <class R, class U>
json result_json(const R& r, U unit) {
  if (!r) {
    return json{{"error", error_json(r.error())}};
  }
  return labelled_json(*r, unit);
}

json duty_json(const fans::LabelledDuty& d) {
  return json{{"flow", labelled_json(d.flow, mp_units::cubic(si::metre) / si::second)},
              {"fan_total_pressure", labelled_json(d.pressure, si::pascal)}};
}

json report_json(const fans::FeasibilityReport& report) {
  json checks = json::array();
  for (const auto& c : report.checks) {
    checks.push_back(json{
        {"limit", c.limit},
        {"status", std::string{fans::to_string(c.status)}},
        {"message", c.message},
        {"violation", c.violation ? error_json(*c.violation) : json(nullptr)},
        {"nearest_feasible", c.nearest_feasible ? duty_json(*c.nearest_feasible) : json(nullptr)}});
  }
  json ws = nullptr;
  if (report.specific_speed) {
    ws = labelled_json(*report.specific_speed, mp_units::one);
  }
  return json{{"verdict", std::string{fans::to_string(report.verdict())}},
              {"specific_speed", ws},
              {"checks", checks}};
}

json evaluate(const json& c) {
  const std::string fn = c.at("function").get<std::string>();
  const json& args = c.at("args");
  const physics::Air air = air_of(c.at("air"));
  const auto pa = si::pascal;
  const auto ms = si::metre / si::second;
  if (fn == "tip_speed") {
    return result_json(fans::tip_speed(omega_of(num(args, "omega")), length_of(num(args, "d_tip"))),
                       ms);
  }
  if (fn == "max_fan_total_pressure") {
    return result_json(fans::max_fan_total_pressure(air), pa);
  }
  if (fn == "max_tip_speed") {
    return result_json(fans::max_tip_speed(air), ms);
  }
  if (fn == "flow_coefficient") {
    return result_json(
        fans::flow_coefficient(flow_of(num(args, "flow")), length_of(num(args, "d_tip")),
                               omega_of(num(args, "omega")), air),
        mp_units::one);
  }
  if (fn == "pressure_coefficient") {
    return result_json(fans::pressure_coefficient(total_of(num(args, "fan_total_pressure")),
                                                  length_of(num(args, "d_tip")),
                                                  omega_of(num(args, "omega")), air),
                       mp_units::one);
  }
  if (fn == "specific_speed") {
    return result_json(
        fans::specific_speed(omega_of(num(args, "omega")), flow_of(num(args, "flow")),
                             total_of(num(args, "fan_total_pressure")), air),
        mp_units::one);
  }
  if (fn == "specific_diameter") {
    return result_json(
        fans::specific_diameter(length_of(num(args, "d_tip")), flow_of(num(args, "flow")),
                                total_of(num(args, "fan_total_pressure")), air),
        mp_units::one);
  }
  if (fn == "conventional_dynamic_pressure") {
    return result_json(fans::conventional_dynamic_pressure(flow_of(num(args, "flow")),
                                                           length_of(num(args, "d_duct")), air),
                       pa);
  }
  if (fn == "fan_total_from_static") {
    const auto r = fans::fan_total_from_static(
        num(args, "fan_static_pressure") * fans::fan_static_pressure[si::pascal],
        flow_of(num(args, "flow")), length_of(num(args, "d_duct")), air);
    if (!r) {
      return json{{"error", error_json(r.error())}};
    }
    json out = labelled_json(r->value, pa);
    out["provenance"] = std::string{core::to_string(r->provenance)};
    out["rule"] = std::string{r->rule};
    return out;
  }
  if (fn == "scale_fan_laws") {
    const json& p = args.at("point");
    const fans::FanPoint point{.omega = omega_of(num(p, "omega")),
                               .d_tip = length_of(num(p, "d_tip")),
                               .flow = flow_of(num(p, "flow")),
                               .pressure = total_of(num(p, "fan_total_pressure")),
                               .power = num(p, "power") * isq::power[si::watt],
                               .air = air};
    const physics::Air air_new = c.contains("air_new") ? air_of(c.at("air_new")) : air;
    const auto r = fans::scale_fan_laws(point, omega_of(num(args, "omega")),
                                        length_of(num(args, "d_tip")), air_new);
    if (!r) {
      return json{{"error", error_json(r.error())}};
    }
    return json{
        {"value",
         {{"flow", r->flow.value().numerical_value_in(mp_units::cubic(si::metre) / si::second)},
          {"fan_total_pressure", r->pressure.value().numerical_value_in(pa)},
          {"power", r->power.value().numerical_value_in(si::watt)}}},
        {"fidelity", std::string{core::to_string(r->flow.fidelity())}},
        {"model", core::to_string(r->flow.model())}};
  }
  if (fn == "ideal_gas_density") {
    const auto r = physics::ideal_gas_density(
        core::units::absolute_zero_pressure + num(args, "pressure") * isq::pressure[si::pascal],
        mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(num(args, "temperature")));
    return result_json(r, si::kilogram / mp_units::cubic(si::metre));
  }
  if (fn == "speed_of_sound") {
    return result_json(physics::speed_of_sound(air), ms);
  }
  if (fn == "check_feasibility") {
    const json& rj = c.at("range");
    fans::FamilyRange range{.family = rj.at("family").get<std::string>(),
                            .specific_speed_min = std::nullopt,
                            .specific_speed_max = std::nullopt,
                            .source = rj.at("source").get<std::string>()};
    if (!rj.at("specific_speed_min").is_null()) {
      range.specific_speed_min = rj.at("specific_speed_min").get<double>();
    }
    if (!rj.at("specific_speed_max").is_null()) {
      range.specific_speed_max = rj.at("specific_speed_max").get<double>();
    }
    std::optional<core::AngularVelocity> omega;
    std::optional<core::Length> d_tip;
    if (args.contains("omega")) {
      omega = omega_of(num(args, "omega"));
    }
    if (args.contains("d_tip")) {
      d_tip = length_of(num(args, "d_tip"));
    }
    const fans::Duty duty{.flow = flow_of(num(args, "flow")),
                          .pressure = total_of(num(args, "fan_total_pressure"))};
    const auto r = fans::check_feasibility(duty, air, range, omega, d_tip);
    if (!r) {
      return json{{"error", error_json(r.error())}};
    }
    return json{{"report", report_json(*r)}};
  }
  throw std::invalid_argument("unknown function: " + fn);
}

}  // namespace

int main() try {
  const json cases = json::parse(std::cin);
  json results = json::array();
  for (const auto& c : cases) {
    json out = evaluate(c);
    json row = json{{"id", c.at("id")}};
    row.update(out);
    results.push_back(std::move(row));
  }
  std::cout << results.dump() << '\n';
  return 0;
} catch (const std::exception& error) {
  std::cerr << "l0_crosscheck: " << error.what() << '\n';
  return 1;
}

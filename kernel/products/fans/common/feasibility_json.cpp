#include "products/fans/common/feasibility_json.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <optional>
#include <string>

namespace cemkit::fans {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using product::Json;

namespace {

// A labelled quantity with the words and unit the report prints it with (ADR-015: product
// wording lives in the product, so the CLI stays product-neutral). Additive members.
Json with_text(Json quantity, const char* label, const char* unit) {
  quantity["label"] = label;
  quantity["unit"] = unit;
  return quantity;
}

}  // namespace

product::Json duty_json(const LabelledDuty& duty) {
  return Json{
      {"flow", with_text(product::labelled_json(duty.flow, mp_units::cubic(si::metre) / si::second),
                         "flow", "m3/s")},
      {"fan_total_pressure",
       with_text(product::labelled_json(duty.pressure, si::pascal), "fan total pressure", "Pa")}};
}

product::Json report_json(const FeasibilityReport& report) {
  Json checks = Json::array();
  for (const auto& c : report.checks) {
    checks.push_back(Json{
        {"limit", c.limit},
        {"status", std::string{to_string(c.status)}},
        {"message", c.message},
        {"violation", c.violation ? product::error_json(*c.violation) : Json(nullptr)},
        {"nearest_feasible", c.nearest_feasible ? duty_json(*c.nearest_feasible) : Json(nullptr)}});
  }
  Json ws = nullptr;
  if (report.specific_speed) {
    ws = with_text(product::labelled_json(*report.specific_speed, mp_units::one), "specific speed",
                   "(1)");
  }
  return Json{{"verdict", std::string{to_string(report.verdict())}},
              {"specific_speed", ws},
              {"checks", checks}};
}

FamilyRange range_of(const product::Json& r) {
  return FamilyRange{.family = product::text(r, "family"),
                     .specific_speed_min = product::optional_number(r, "specific_speed_min"),
                     .specific_speed_max = product::optional_number(r, "specific_speed_max"),
                     .source = product::text(r, "source")};
}

core::Result<FeasibilityReport> run_feasibility(const product::Json& args, const physics::Air& air,
                                                const product::Json& range) {
  std::optional<core::AngularVelocity> omega;
  std::optional<core::Length> d_tip;
  if (auto w = product::optional_number(args, "omega")) {
    omega = *w * isq::angular_velocity[si::radian / si::second];
  }
  if (auto d = product::optional_number(args, "d_tip")) {
    d_tip = *d * isq::length[si::metre];
  }
  const Duty duty{
      .flow = product::number(args, "flow") *
              core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = product::number(args, "fan_total_pressure") * fan_total_pressure[si::pascal]};
  return check_feasibility(duty, air, range_of(range), omega, d_tip);
}

}  // namespace cemkit::fans

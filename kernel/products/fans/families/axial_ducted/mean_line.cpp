#include "products/fans/families/axial_ducted/mean_line.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cmath>
#include <initializer_list>
#include <numbers>
#include <string>
#include <utility>

#include "cemkit/core/error.hpp"
#include "cemkit/physics/validity.hpp"
#include "products/fans/common/l0.hpp"

namespace cemkit::fans::axial_ducted {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;

constexpr core::SemVer k_version{.major = 0, .minor = 1, .patch = 0};

}  // namespace

core::ModelId mean_line_model() { return {.name = "fans.axial.mean_line", .version = k_version}; }

core::Result<MeanLine> mean_line(core::VolumeFlowRate flow, FanTotalPressure pressure,
                                 core::AngularVelocity omega, core::Length r_tip,
                                 core::Length r_hub, const physics::Air& air) {
  const auto model = mean_line_model();
  const double q = flow.numerical_value_in(mp_units::cubic(si::metre) / si::second);
  const double dp = pressure.numerical_value_in(si::pascal);
  const double w = omega.numerical_value_in(si::radian / si::second);
  const double rt = r_tip.numerical_value_in(si::metre);
  const double rh = r_hub.numerical_value_in(si::metre);
  const physics::AirSi a = physics::to_si(air);
  for (const auto& [subject, value] : std::initializer_list<std::pair<std::string_view, double>>{
           {"flow", q},
           {"fan_total_pressure", dp},
           {"omega", w},
           {"r_tip", rt},
           {"r_hub", rh},
           {"air.density", a.density},
           {"air.temperature", a.temperature},
           {"air.pressure", a.pressure},
           {"air.gamma", a.gamma},
           {"air.gas_constant", a.gas_constant}}) {
    if (auto ok = physics::require_positive(subject, value, model); !ok) {
      return std::unexpected{std::move(ok).error()};
    }
  }
  if (!(rh < rt)) {
    return core::fail(core::ErrorCode::out_of_validity,
                      "the hub radius must be less than the tip radius", "r_hub",
                      {{"value", core::format_number(rh)},
                       {"bounds", "(0, " + core::format_number(rt) + ")"},
                       {"model", core::to_string(model)}});
  }
  // The same operation order as the reference (PHY-005).
  if (auto ok = physics::require_at_most(
          "fan_total_pressure", dp, k_epsilon * a.gamma * a.pressure, model,
          "fan total pressure exceeds the incompressible limit EPSILON gamma p1 (ADR-003 D3)");
      !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  if (auto ok = physics::require_at_most(
          "tip_speed", w * rt,
          std::sqrt(2.0 * k_epsilon) * std::sqrt(a.gamma * a.gas_constant * a.temperature), model,
          "blade tip-speed Mach number exceeds the incompressible limit M^2/2 <= EPSILON "
          "(ADR-003 D3)");
      !ok) {
    return std::unexpected{std::move(ok).error()};
  }

  const double area = std::numbers::pi * ((rt * rt) - (rh * rh));
  const double r_m = std::sqrt(((rt * rt) + (rh * rh)) / 2.0);
  const double u = w * r_m;
  const double c_x = q / area;
  const double c_theta2 = dp / (a.density * u);  // Euler, no inlet swirl, loss-free
  const double w_theta2 = u - c_theta2;
  const double w1 = std::sqrt((c_x * c_x) + (u * u));
  const double w2 = std::sqrt((c_x * c_x) + (w_theta2 * w_theta2));
  const auto speed = [](double v) { return v * isq::speed[si::metre / si::second]; };
  const auto angle = [](double v) { return v * isq::angular_measure[si::radian]; };
  const auto ratio = [](double v) { return v * mp_units::one; };
  return MeanLine{.annulus_area = area * isq::area[mp_units::square(si::metre)],
                  .mean_radius = r_m * isq::length[si::metre],
                  .blade_speed = speed(u),
                  .axial_velocity = speed(c_x),
                  .outlet_swirl = speed(c_theta2),
                  .relative_inlet_angle = angle(std::atan2(u, c_x)),
                  .relative_outlet_angle = angle(std::atan2(w_theta2, c_x)),
                  .absolute_outlet_angle = angle(std::atan2(c_theta2, c_x)),
                  .relative_inlet_speed = speed(w1),
                  .relative_outlet_speed = speed(w2),
                  .flow_coefficient = ratio(c_x / u),
                  .work_coefficient = ratio(c_theta2 / u),
                  .de_haller_ratio = ratio(w2 / w1),
                  .fidelity = core::Fidelity::l1_predicted,
                  .model = model};
}

}  // namespace cemkit::fans::axial_ducted

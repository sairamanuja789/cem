#include "products/fans/common/l0.hpp"

#include <cmath>
#include <initializer_list>
#include <numbers>
#include <utility>

#include "cemkit/physics/validity.hpp"

namespace cemkit::fans {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using physics::AirSi;

// Each function unwraps its quantities into their coherent SI unit (rad/s, m, m3/s, Pa, W) and
// computes in double in exactly the operation order of the Python reference, so the two agree to
// the last bit wherever the C library does (PHY-005). The results are wrapped again at the end.
// Squares are x * x on both sides (compilers turn std::pow(x, 2.0) into x * x, while Python's
// x**2 calls the C library's pow); other powers call std::pow, as the reference calls pow.
double si_value(core::AngularVelocity v) { return v.numerical_value_in(si::radian / si::second); }
double si_value(core::Length v) { return v.numerical_value_in(si::metre); }
double si_value(core::VolumeFlowRate v) {
  return v.numerical_value_in(mp_units::cubic(si::metre) / si::second);
}
double si_value(FanTotalPressure v) { return v.numerical_value_in(si::pascal); }
double si_value(FanStaticPressure v) { return v.numerical_value_in(si::pascal); }
double si_value(core::Power v) { return v.numerical_value_in(si::watt); }

using Input = std::pair<std::string_view, double>;

core::Status positive(std::initializer_list<Input> inputs) {
  const auto model = l0_model();
  for (const auto& [subject, value] : inputs) {
    if (auto ok = physics::require_positive(subject, value, model); !ok) {
      return ok;
    }
  }
  return {};
}

core::Status air_valid(const AirSi& a) {
  return positive({{"air.density", a.density},
                   {"air.temperature", a.temperature},
                   {"air.pressure", a.pressure},
                   {"air.gamma", a.gamma},
                   {"air.gas_constant", a.gas_constant}});
}

// Operation order (k_epsilon * gamma) * p1 fixes the exact bounds text (reference: _max_pressure).
double max_pressure(const AirSi& a) { return k_epsilon * a.gamma * a.pressure; }

// sqrt(2 k_epsilon) * sqrt(gamma R T1), in this order (reference: _max_tip). Only called with air
// that air_valid accepted.
double max_tip(const AirSi& a) {
  return std::sqrt(2.0 * k_epsilon) * std::sqrt(a.gamma * a.gas_constant * a.temperature);
}

core::Status pressure_valid(double pressure, const AirSi& a) {
  return physics::require_at_most(
      "fan_total_pressure", pressure, max_pressure(a), l0_model(),
      "fan total pressure exceeds the incompressible limit EPSILON gamma p1 (ADR-003 D3)");
}

core::Status tip_valid(double omega, double d_tip, const AirSi& a) {
  return physics::require_at_most(
      "tip_speed", omega * d_tip / 2.0, max_tip(a), l0_model(),
      "blade tip-speed Mach number exceeds the incompressible limit M^2/2 <= EPSILON "
      "(ADR-003 D3)");
}

// The first failed check, in order.
core::Status first(std::initializer_list<core::Status> checks) {
  for (const auto& check : checks) {
    if (!check) {
      return check;
    }
  }
  return {};
}

// Reference: _dynamic_pa. area = pi D^2 / 4; 1/2 rho (Q / area)^2.
double dynamic_pa(double flow, double d_duct, const AirSi& a) {
  const double area = std::numbers::pi * (d_duct * d_duct) / 4.0;
  const double velocity = flow / area;
  return 0.5 * a.density * (velocity * velocity);
}

core::Status point_valid(double omega, double d_tip, double flow, double pressure, double power,
                         const AirSi& a) {
  return first({positive({{"omega", omega},
                          {"d_tip", d_tip},
                          {"flow", flow},
                          {"fan_total_pressure", pressure},
                          {"power", power}}),
                air_valid(a), pressure_valid(pressure, a), tip_valid(omega, d_tip, a)});
}

L0Result<core::Ratio> ratio(double value) {
  return core::Labelled<core::Ratio>::l0_predicted(value * mp_units::one, l0_model());
}

L0Result<core::Speed> speed(double value) {
  return core::Labelled<core::Speed>::l0_predicted(value * isq::speed[si::metre / si::second],
                                                   l0_model());
}

L0Result<FanTotalPressure> total_pressure(double value) {
  return core::Labelled<FanTotalPressure>::l0_predicted(value * fan_total_pressure[si::pascal],
                                                        l0_model());
}

}  // namespace

core::ModelId l0_model() {
  return core::ModelId{.name = "fans.l0", .version = {.major = 1, .minor = 0, .patch = 0}};
}

core::Status check_air(const physics::Air& air) { return air_valid(physics::to_si(air)); }

core::Status check_pressure_limit(FanTotalPressure pressure, const physics::Air& air) {
  return pressure_valid(si_value(pressure), physics::to_si(air));
}

core::Status check_tip_speed_limit(core::AngularVelocity omega, core::Length d_tip,
                                   const physics::Air& air) {
  return tip_valid(si_value(omega), si_value(d_tip), physics::to_si(air));
}

L0Result<FanTotalPressure> max_fan_total_pressure(const physics::Air& air) {
  const AirSi a = physics::to_si(air);
  if (auto ok = air_valid(a); !ok) {
    return std::unexpected(ok.error());
  }
  return total_pressure(max_pressure(a));
}

L0Result<core::Speed> max_tip_speed(const physics::Air& air) {
  const AirSi a = physics::to_si(air);
  if (auto ok = air_valid(a); !ok) {
    return std::unexpected(ok.error());
  }
  return speed(max_tip(a));
}

L0Result<core::Speed> tip_speed(core::AngularVelocity omega, core::Length d_tip) {
  const double w = si_value(omega);
  const double d = si_value(d_tip);
  if (auto ok = positive({{"omega", w}, {"d_tip", d}}); !ok) {
    return std::unexpected(ok.error());
  }
  return speed(w * d / 2.0);
}

L0Result<core::Ratio> flow_coefficient(core::VolumeFlowRate flow, core::Length d_tip,
                                       core::AngularVelocity omega, const physics::Air& air) {
  const double q = si_value(flow);
  const double d = si_value(d_tip);
  const double w = si_value(omega);
  const AirSi a = physics::to_si(air);
  if (auto ok = first(
          {positive({{"flow", q}, {"d_tip", d}, {"omega", w}}), air_valid(a), tip_valid(w, d, a)});
      !ok) {
    return std::unexpected(ok.error());
  }
  const double u = w * d / 2.0;
  return ratio(q / (std::numbers::pi / 4.0 * (d * d) * u));
}

L0Result<core::Ratio> pressure_coefficient(FanTotalPressure pressure, core::Length d_tip,
                                           core::AngularVelocity omega, const physics::Air& air) {
  const double p = si_value(pressure);
  const double d = si_value(d_tip);
  const double w = si_value(omega);
  const AirSi a = physics::to_si(air);
  if (auto ok = first({positive({{"fan_total_pressure", p}, {"d_tip", d}, {"omega", w}}),
                       air_valid(a), pressure_valid(p, a), tip_valid(w, d, a)});
      !ok) {
    return std::unexpected(ok.error());
  }
  const double u = w * d / 2.0;
  return ratio(2.0 * p / (a.density * (u * u)));
}

L0Result<core::Ratio> specific_speed(core::AngularVelocity omega, core::VolumeFlowRate flow,
                                     FanTotalPressure pressure, const physics::Air& air) {
  const double w = si_value(omega);
  const double q = si_value(flow);
  const double p = si_value(pressure);
  const AirSi a = physics::to_si(air);
  if (auto ok = first({positive({{"omega", w}, {"flow", q}, {"fan_total_pressure", p}}),
                       air_valid(a), pressure_valid(p, a)});
      !ok) {
    return std::unexpected(ok.error());
  }
  return ratio(w * std::sqrt(q) / std::pow(p / a.density, 0.75));
}

L0Result<core::Ratio> specific_diameter(core::Length d_tip, core::VolumeFlowRate flow,
                                        FanTotalPressure pressure, const physics::Air& air) {
  const double d = si_value(d_tip);
  const double q = si_value(flow);
  const double p = si_value(pressure);
  const AirSi a = physics::to_si(air);
  if (auto ok = first({positive({{"d_tip", d}, {"flow", q}, {"fan_total_pressure", p}}),
                       air_valid(a), pressure_valid(p, a)});
      !ok) {
    return std::unexpected(ok.error());
  }
  return ratio(d * std::pow(p / a.density, 0.25) / std::sqrt(q));
}

L0Result<FanDynamicPressure> conventional_dynamic_pressure(core::VolumeFlowRate flow,
                                                           core::Length d_duct,
                                                           const physics::Air& air) {
  const double q = si_value(flow);
  const double d = si_value(d_duct);
  const AirSi a = physics::to_si(air);
  if (auto ok = first({positive({{"flow", q}, {"d_duct", d}}), air_valid(a)}); !ok) {
    return std::unexpected(ok.error());
  }
  const double dynamic = dynamic_pa(q, d, a);
  if (auto ok = physics::require_at_most(
          "fan_dynamic_pressure", dynamic, max_pressure(a), l0_model(),
          "fan dynamic pressure exceeds the incompressible limit EPSILON gamma p1 (ADR-003 D3)");
      !ok) {
    return std::unexpected(ok.error());
  }
  return core::Labelled<FanDynamicPressure>::l0_predicted(
      dynamic * fan_dynamic_pressure[si::pascal], l0_model());
}

core::Result<DerivedFanTotalPressure> fan_total_from_static(FanStaticPressure pressure,
                                                            core::VolumeFlowRate flow,
                                                            core::Length d_duct,
                                                            const physics::Air& air) {
  const double ps = si_value(pressure);
  const double q = si_value(flow);
  const double d = si_value(d_duct);
  const AirSi a = physics::to_si(air);
  if (auto ok = first({physics::require_non_negative("fan_static_pressure", ps, l0_model()),
                       positive({{"flow", q}, {"d_duct", d}}), air_valid(a)});
      !ok) {
    return std::unexpected(ok.error());
  }
  const double total = ps + dynamic_pa(q, d, a);
  // At free delivery a tiny flow can underflow the dynamic pressure to 0; a total of 0 is outside
  // (0, limit], so it is rejected rather than returned.
  if (auto ok = first({physics::require_positive("fan_total_pressure", total, l0_model()),
                       pressure_valid(total, a)});
      !ok) {
    return std::unexpected(ok.error());
  }
  auto labelled = total_pressure(total);
  if (!labelled) {
    return std::unexpected(labelled.error());
  }
  return DerivedFanTotalPressure{.value = *labelled};
}

core::Result<ScaledFanPoint> scale_fan_laws(const FanPoint& point, core::AngularVelocity omega,
                                            core::Length d_tip, const physics::Air& air) {
  const double w1 = si_value(point.omega);
  const double d1 = si_value(point.d_tip);
  const double q1 = si_value(point.flow);
  const double p1 = si_value(point.pressure);
  const double pw1 = si_value(point.power);
  const AirSi a1 = physics::to_si(point.air);
  const double w2 = si_value(omega);
  const double d2 = si_value(d_tip);
  const AirSi a2 = physics::to_si(air);
  if (auto ok = first({point_valid(w1, d1, q1, p1, pw1, a1),
                       positive({{"omega_new", w2}, {"d_tip_new", d2}}), air_valid(a2)});
      !ok) {
    return std::unexpected(ok.error());
  }
  const double n = w2 / w1;
  const double d = d2 / d1;
  const double r = a2.density / a1.density;
  const double q2 = q1 * n * std::pow(d, 3.0);
  const double p2 = p1 * r * (n * n) * (d * d);
  const double pw2 = pw1 * r * std::pow(n, 3.0) * std::pow(d, 5.0);
  if (auto ok = point_valid(w2, d2, q2, p2, pw2, a2); !ok) {
    return std::unexpected(ok.error());
  }
  auto flow = core::Labelled<core::VolumeFlowRate>::l0_predicted(
      q2 * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second], l0_model());
  auto pressure = total_pressure(p2);
  auto power = core::Labelled<core::Power>::l0_predicted(pw2 * isq::power[si::watt], l0_model());
  if (!flow || !pressure || !power) {
    // Unreachable: point_valid accepted finite values.
    return core::fail(core::ErrorCode::internal_error, "scaled fan point is not finite",
                      "scale_fan_laws");
  }
  return ScaledFanPoint{.omega = omega,
                        .d_tip = d_tip,
                        .flow = *flow,
                        .pressure = *pressure,
                        .power = *power,
                        .air = air};
}

}  // namespace cemkit::fans

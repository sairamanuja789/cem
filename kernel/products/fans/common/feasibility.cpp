#include "products/fans/common/feasibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "cemkit/physics/validity.hpp"
#include "products/fans/common/l0.hpp"

namespace cemkit::fans {

namespace {

namespace si = mp_units::si;

// omega_s ~ Q^(1/2) Delta p^(-3/4): the exponents of the log-space rule (ADR-011). Exact in binary.
constexpr double k_flow_exponent = 0.5;
constexpr double k_pressure_exponent = 0.75;
constexpr double k_norm =
    (k_flow_exponent * k_flow_exponent) + (k_pressure_exponent * k_pressure_exponent);  // 0.8125
// Last-ulp steps allowed to land the rounded nearest duty inside the range (ADR-011).
constexpr int k_max_nudges = 8;

double flow_si(core::VolumeFlowRate q) {
  return q.numerical_value_in(mp_units::cubic(si::metre) / si::second);
}
double pressure_si(FanTotalPressure p) { return p.numerical_value_in(si::pascal); }

core::VolumeFlowRate flow_of(double q) {
  return q * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
}
FanTotalPressure pressure_of(double p) { return p * fan_total_pressure[si::pascal]; }

core::Status inputs_valid(const Duty& duty, const physics::Air& air,
                          const std::optional<core::AngularVelocity>& omega,
                          const std::optional<core::Length>& d_tip) {
  const auto model = l0_model();
  if (auto ok = physics::require_positive("flow", flow_si(duty.flow), model); !ok) {
    return ok;
  }
  if (auto ok = physics::require_positive("fan_total_pressure", pressure_si(duty.pressure), model);
      !ok) {
    return ok;
  }
  if (omega) {
    if (auto ok = physics::require_positive(
            "omega", omega->numerical_value_in(si::radian / si::second), model);
        !ok) {
      return ok;
    }
  }
  if (d_tip) {
    if (auto ok = physics::require_positive("d_tip", d_tip->numerical_value_in(si::metre), model);
        !ok) {
      return ok;
    }
  }
  return check_air(air);
}

core::Status range_valid(const FamilyRange& range) {
  if (!range.specific_speed_min || !range.specific_speed_max) {
    return {};
  }
  const double lo = range.specific_speed_min.value_or(0.0);
  const double hi = range.specific_speed_max.value_or(0.0);
  if (std::isfinite(lo) && std::isfinite(hi) && 0.0 < lo && lo <= hi) {
    return {};
  }
  return core::fail(core::ErrorCode::invalid_input,
                    "a family specific-speed range needs finite bounds with 0 < min <= max",
                    "family_range",
                    {{"family", range.family}, {"model", core::to_string(feasibility_model())}});
}

bool within(double value, double lo, double hi) { return lo <= value && value <= hi; }

std::optional<double> omega_s(core::AngularVelocity omega, double flow, double pressure,
                              const physics::Air& air) {
  auto value = specific_speed(omega, flow_of(flow), pressure_of(pressure), air);
  if (!value) {
    return std::nullopt;
  }
  return value->value().numerical_value_in(mp_units::one);
}

// The duty closest to the given one in (ln Q, ln Delta p) whose omega_s is the crossed range bound
// (ADR-011); nullopt if rounding cannot land it inside, or it breaks the pressure limit.
std::optional<Duty> nearest_in_range(const Duty& duty, core::AngularVelocity omega,
                                     const physics::Air& air, double value, double lo, double hi) {
  const double target = value > hi ? hi : lo;
  const double c = std::log(target / value);
  const double flow = flow_si(duty.flow) * std::exp(c * k_flow_exponent / k_norm);
  double pressure = pressure_si(duty.pressure) * std::exp(-c * k_pressure_exponent / k_norm);
  for (int i = 0; i <= k_max_nudges; ++i) {
    const auto ws = omega_s(omega, flow, pressure, air);
    if (!ws) {
      return std::nullopt;
    }
    if (within(*ws, lo, hi)) {
      return Duty{.flow = flow_of(flow), .pressure = pressure_of(pressure)};
    }
    // omega_s falls as the pressure rises: step the pressure by one ulp towards the range.
    pressure = std::nextafter(pressure, *ws > hi ? std::numeric_limits<double>::infinity() : 0.0);
  }
  return std::nullopt;
}

std::optional<LabelledDuty> labelled(const Duty& duty) {
  auto flow = core::Labelled<core::VolumeFlowRate>::l0_predicted(duty.flow, feasibility_model());
  auto pressure =
      core::Labelled<FanTotalPressure>::l0_predicted(duty.pressure, feasibility_model());
  if (!flow || !pressure) {
    return std::nullopt;  // not finite: no duty is offered
  }
  return LabelledDuty{.flow = *flow, .pressure = *pressure};
}

}  // namespace

std::string_view to_string(CheckStatus status) noexcept {
  switch (status) {
    case CheckStatus::pass:
      return "pass";
    case CheckStatus::violated:
      return "violated";
    case CheckStatus::range_unsourced:
      return "range_unsourced";
    case CheckStatus::not_computable:
      return "not_computable";
  }
  return {};
}

Verdict FeasibilityReport::verdict() const noexcept {
  const auto is = [](CheckStatus status) {
    return [status](const FeasibilityCheck& check) { return check.status == status; };
  };
  if (std::ranges::any_of(checks, is(CheckStatus::violated))) {
    return Verdict::infeasible;
  }
  if (std::ranges::all_of(checks, is(CheckStatus::pass))) {
    return Verdict::confirmed;
  }
  return Verdict::unconfirmed;
}

std::string_view to_string(Verdict verdict) noexcept {
  switch (verdict) {
    case Verdict::infeasible:
      return "infeasible";
    case Verdict::unconfirmed:
      return "unconfirmed";
    case Verdict::confirmed:
      return "confirmed";
  }
  return {};
}

core::ModelId feasibility_model() {
  return core::ModelId{.name = "fans.feasibility", .version = {.major = 1, .minor = 0, .patch = 0}};
}

core::Result<FeasibilityReport> check_feasibility(const Duty& duty, const physics::Air& air,
                                                  const FamilyRange& range,
                                                  std::optional<core::AngularVelocity> omega,
                                                  std::optional<core::Length> d_tip) {
  if (auto ok = inputs_valid(duty, air, omega, d_tip); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = range_valid(range); !ok) {
    return std::unexpected(ok.error());
  }
  FeasibilityReport report;

  // 1. Pressure limit, with the same error text as the L0 functions.
  const auto pressure_check = check_pressure_limit(duty.pressure, air);
  const bool pressure_ok = pressure_check.has_value();
  if (pressure_ok) {
    report.checks.push_back(FeasibilityCheck{.limit = "incompressible_pressure",
                                             .status = CheckStatus::pass,
                                             .message = "within EPSILON gamma p1",
                                             .violation = std::nullopt,
                                             .nearest_feasible = std::nullopt});
  } else {
    // The limit itself, from the L0 model (air was checked above, so it has a value).
    const auto limit = max_fan_total_pressure(air);
    std::optional<LabelledDuty> at_limit;
    if (limit) {
      at_limit = labelled(Duty{.flow = duty.flow, .pressure = limit->value()});
    }
    report.checks.push_back(FeasibilityCheck{.limit = "incompressible_pressure",
                                             .status = CheckStatus::violated,
                                             .message = pressure_check.error().message(),
                                             .violation = pressure_check.error(),
                                             .nearest_feasible = at_limit});
  }

  // 2. Tip-speed limit.
  if (!omega || !d_tip) {
    report.checks.push_back(
        FeasibilityCheck{.limit = "incompressible_tip_speed",
                         .status = CheckStatus::not_computable,
                         .message = "needs the rotational speed and the rotor tip diameter",
                         .violation = std::nullopt,
                         .nearest_feasible = std::nullopt});
  } else {
    const auto tip_check = check_tip_speed_limit(*omega, *d_tip, air);
    if (tip_check) {
      report.checks.push_back(FeasibilityCheck{.limit = "incompressible_tip_speed",
                                               .status = CheckStatus::pass,
                                               .message = "within sqrt(2 EPSILON) a1",
                                               .violation = std::nullopt,
                                               .nearest_feasible = std::nullopt});
    } else {
      report.checks.push_back(FeasibilityCheck{.limit = "incompressible_tip_speed",
                                               .status = CheckStatus::violated,
                                               .message = tip_check.error().message(),
                                               .violation = tip_check.error(),
                                               .nearest_feasible = std::nullopt});
    }
  }

  // 3. Family specific-speed range.
  std::optional<double> ws;
  if (omega && pressure_ok) {
    auto value = specific_speed(*omega, duty.flow, duty.pressure, air);
    if (value) {
      ws = value->value().numerical_value_in(mp_units::one);
      report.specific_speed = *value;
    }
  }
  if (!range.sourced()) {
    report.checks.push_back(FeasibilityCheck{
        .limit = "family_specific_speed_range",
        .status = CheckStatus::range_unsourced,
        .message = "range unsourced: no cited specific-speed range for " + range.family,
        .violation = std::nullopt,
        .nearest_feasible = std::nullopt});
  } else if (!ws || !omega) {
    report.checks.push_back(
        FeasibilityCheck{.limit = "family_specific_speed_range",
                         .status = CheckStatus::not_computable,
                         .message = !omega ? "needs the rotational speed"
                                           : "specific speed is out of validity at this pressure",
                         .violation = std::nullopt,
                         .nearest_feasible = std::nullopt});
  } else {
    const double lo = range.specific_speed_min.value_or(0.0);
    const double hi = range.specific_speed_max.value_or(0.0);
    const double value = ws.value_or(0.0);
    if (within(value, lo, hi)) {
      report.checks.push_back(FeasibilityCheck{.limit = "family_specific_speed_range",
                                               .status = CheckStatus::pass,
                                               .message = "inside " + range.source,
                                               .violation = std::nullopt,
                                               .nearest_feasible = std::nullopt});
    } else {
      core::Error violation{
          core::ErrorCode::infeasible_requirement,
          "specific speed is outside the family's feasible range",
          "specific_speed",
          {{"bounds", "[" + core::format_number(lo) + ", " + core::format_number(hi) + "]"},
           {"family", range.family},
           {"model", core::to_string(feasibility_model())},
           {"value", core::format_number(value)}}};
      auto nearest = nearest_in_range(duty, *omega, air, value, lo, hi);
      std::optional<LabelledDuty> nearest_labelled;
      if (nearest) {
        nearest_labelled = labelled(*nearest);
      }
      std::string message = violation.message();
      report.checks.push_back(FeasibilityCheck{.limit = "family_specific_speed_range",
                                               .status = CheckStatus::violated,
                                               .message = std::move(message),
                                               .violation = std::move(violation),
                                               .nearest_feasible = nearest_labelled});
    }
  }
  return report;
}

}  // namespace cemkit::fans

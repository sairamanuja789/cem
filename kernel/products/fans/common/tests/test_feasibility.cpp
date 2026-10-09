#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <limits>
#include <string>
#include <vector>

#include "cemkit/core/error.hpp"
#include "products/fans/common/feasibility.hpp"
#include "products/fans/common/l0.hpp"

using Catch::Matchers::WithinRel;
namespace fans = cemkit::fans;
namespace physics = cemkit::physics;
namespace core = cemkit::core;
namespace si = mp_units::si;
namespace isq = mp_units::isq;
using fans::CheckStatus;

namespace {

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_omega = 209.43951023931953;
constexpr double k_ws = 1.2370483575363835;  // l0_002
// Test fixture only, not engineering data: bounds around l0_002's omega_s.
const std::string k_fixture = "test fixture, not engineering data";

core::AngularVelocity rad_s(double v) { return v * isq::angular_velocity[si::radian / si::second]; }
core::Length m(double v) { return v * isq::length[si::metre]; }
core::VolumeFlowRate m3_s(double v) {
  return v * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
}
fans::FanTotalPressure total(double v) { return v * fans::fan_total_pressure[si::pascal]; }
double flow_of(const fans::Duty& d) {
  return d.flow.numerical_value_in(mp_units::cubic(si::metre) / si::second);
}
double pressure_of(const fans::Duty& d) { return d.pressure.numerical_value_in(si::pascal); }

const physics::Air k_air = physics::default_air();
const fans::Duty k_duty{.flow = m3_s(0.05), .pressure = total(150.0)};
const fans::FamilyRange k_unsourced{.family = "fans.axial_ducted",
                                    .specific_speed_min = std::nullopt,
                                    .specific_speed_max = std::nullopt,
                                    .source = std::string{fans::k_unsourced}};

fans::FamilyRange sourced(double lo, double hi) {
  return fans::FamilyRange{.family = "fans.axial_ducted",
                           .specific_speed_min = lo,
                           .specific_speed_max = hi,
                           .source = k_fixture};
}

std::vector<std::pair<std::string, CheckStatus>> statuses(const fans::FeasibilityReport& r) {
  std::vector<std::pair<std::string, CheckStatus>> out;
  out.reserve(r.checks.size());
  for (const auto& c : r.checks) {
    out.emplace_back(c.limit, c.status);
  }
  return out;
}

// The numbers of a proposed duty, or k_duty when absent (callers REQUIRE presence first).
fans::Duty unlabel(const std::optional<fans::LabelledDuty>& d) {
  return d
      .transform([](const fans::LabelledDuty& x) {
        return fans::Duty{.flow = x.flow.value(), .pressure = x.pressure.value()};
      })
      .value_or(k_duty);
}

// Proposed duties are labelled L0 predicted by the gate's model (rule 5).
bool labelled_by_gate(const std::optional<fans::LabelledDuty>& d) {
  return d
      .transform([](const fans::LabelledDuty& x) {
        return x.flow.fidelity() == core::Fidelity::l0_predicted &&
               x.pressure.fidelity() == core::Fidelity::l0_predicted &&
               x.flow.model() == fans::feasibility_model() &&
               x.pressure.model() == fans::feasibility_model();
      })
      .value_or(false);
}

// omega_s of a report, NaN when absent (so a CHECK on it fails).
double ws_value(const fans::FeasibilityReport& r) {
  return r.specific_speed
      .transform([](const auto& l) { return l.value().numerical_value_in(mp_units::one); })
      .value_or(k_nan);
}

}  // namespace

TEST_CASE("an unsourced family range is reported as range unsourced", "[SEL-002]") {
  const auto r = fans::check_feasibility(k_duty, k_air, k_unsourced, rad_s(k_omega), m(0.119));
  REQUIRE(r.has_value());
  REQUIRE(r->checks.size() == 3);
  CHECK(r->checks[2].status == CheckStatus::range_unsourced);
  CHECK(r->checks[2].message ==
        "range unsourced: no cited specific-speed range for fans.axial_ducted");
  CHECK(r->verdict() == fans::Verdict::unconfirmed);  // never "feasible" (ADR-011)
  REQUIRE(r->specific_speed.has_value());
  CHECK(r->specific_speed.transform([](const auto& l) { return l.fidelity(); }) ==
        core::Fidelity::l0_predicted);
  CHECK(r->specific_speed.transform([](const auto& l) { return l.model(); }) == fans::l0_model());
  CHECK(!k_unsourced.sourced());
  CHECK(sourced(1.0, 2.0).sourced());
}

TEST_CASE("a duty inside every limit passes", "[SEL-004]") {
  const auto r =
      fans::check_feasibility(k_duty, k_air, sourced(1.0, 1.5), rad_s(k_omega), m(0.119));
  REQUIRE(r.has_value());
  CHECK(statuses(*r) == std::vector<std::pair<std::string, CheckStatus>>{
                            {"incompressible_pressure", CheckStatus::pass},
                            {"incompressible_tip_speed", CheckStatus::pass},
                            {"family_specific_speed_range", CheckStatus::pass}});
  CHECK(r->verdict() == fans::Verdict::confirmed);
  REQUIRE(r->specific_speed.has_value());
  CHECK_THAT(ws_value(*r), WithinRel(k_ws, 1e-12));
}

TEST_CASE("pressure beyond the limit names the limit and the nearest feasible duty", "[SEL-004]") {
  const fans::Duty duty{.flow = m3_s(0.05), .pressure = total(1500.0)};
  const auto r = fans::check_feasibility(duty, k_air, k_unsourced, rad_s(k_omega), m(0.119));
  REQUIRE(r.has_value());
  const auto& first = r->checks.at(0);
  CHECK(first.limit == "incompressible_pressure");
  CHECK(first.status == CheckStatus::violated);
  REQUIRE(first.violation.has_value());
  CHECK(first.violation.value_or(core::Error{core::ErrorCode::internal_error, ""})
            .details()
            .at("bounds") == "(0, 1418.55]");
  REQUIRE(first.nearest_feasible.has_value());
  const auto nearest = unlabel(first.nearest_feasible);
  CHECK(labelled_by_gate(first.nearest_feasible));
  CHECK(flow_of(nearest) == 0.05);
  CHECK(pressure_of(nearest) == 1418.55);
  CHECK(r->verdict() == fans::Verdict::infeasible);
  CHECK(!r->specific_speed.has_value());
}

TEST_CASE("tip speed beyond the limit has no nearest feasible duty", "[SEL-004]") {
  const auto r = fans::check_feasibility(k_duty, k_air, k_unsourced, rad_s(200.0), m(0.5));
  REQUIRE(r.has_value());
  const auto& tip = r->checks.at(1);
  CHECK(tip.status == CheckStatus::violated);
  CHECK(tip.violation.has_value());
  CHECK(!tip.nearest_feasible.has_value());
  CHECK(r->verdict() == fans::Verdict::infeasible);
}

TEST_CASE("specific speed outside a sourced range gives a nearest duty inside it", "[SEL-004]") {
  for (const auto& [lo, hi] : {std::pair{1.3, 2.0}, std::pair{0.5, 1.1}}) {
    const auto r =
        fans::check_feasibility(k_duty, k_air, sourced(lo, hi), rad_s(k_omega), m(0.119));
    REQUIRE(r.has_value());
    const auto& last = r->checks.at(2);
    CHECK(last.status == CheckStatus::violated);
    REQUIRE(last.violation.has_value());
    const auto violation =
        last.violation.value_or(core::Error{core::ErrorCode::internal_error, ""});
    CHECK(violation.code() == core::ErrorCode::infeasible_requirement);
    CHECK(violation.subject() == "specific_speed");
    CHECK(violation.details().at("bounds") ==
          "[" + core::format_number(lo) + ", " + core::format_number(hi) + "]");
    REQUIRE(last.nearest_feasible.has_value());
    const auto nearest = unlabel(last.nearest_feasible);
    CHECK(labelled_by_gate(last.nearest_feasible));
    const auto ws = fans::specific_speed(rad_s(k_omega), nearest.flow, nearest.pressure, k_air);
    REQUIRE(ws.has_value());
    const double value = ws->value().numerical_value_in(mp_units::one);
    CHECK(lo <= value);
    CHECK(value <= hi);
    CHECK_THAT(value, WithinRel(lo > k_ws ? lo : hi, 1e-12));
    CHECK(r->verdict() == fans::Verdict::infeasible);
  }
}

TEST_CASE("a nearest duty that would break the pressure limit is not offered", "[SEL-004]") {
  // omega_s far above the range: the nearest duty needs more than 1418.55 Pa.
  const fans::Duty duty{.flow = m3_s(0.05), .pressure = total(1000.0)};
  const auto r =
      fans::check_feasibility(duty, k_air, sourced(0.01, 0.02), rad_s(k_omega), m(0.119));
  REQUIRE(r.has_value());
  CHECK(r->checks.at(2).status == CheckStatus::violated);
  CHECK(!r->checks.at(2).nearest_feasible.has_value());
}

TEST_CASE("speed-dependent checks are not computable without the speed", "[SEL-004]") {
  const auto r = fans::check_feasibility(k_duty, k_air, sourced(1.0, 1.5));
  REQUIRE(r.has_value());
  CHECK(statuses(*r) == std::vector<std::pair<std::string, CheckStatus>>{
                            {"incompressible_pressure", CheckStatus::pass},
                            {"incompressible_tip_speed", CheckStatus::not_computable},
                            {"family_specific_speed_range", CheckStatus::not_computable}});
  CHECK(r->checks.at(2).message == "needs the rotational speed");
  CHECK(r->verdict() == fans::Verdict::unconfirmed);

  const fans::Duty high{.flow = m3_s(0.05), .pressure = total(1500.0)};
  const auto rh = fans::check_feasibility(high, k_air, sourced(1.0, 1.5), rad_s(k_omega));
  REQUIRE(rh.has_value());
  CHECK(rh->checks.at(2).message == "specific speed is out of validity at this pressure");
}

TEST_CASE("invalid inputs and malformed ranges are errors, not reports", "[PHY-003][SEL-002]") {
  for (const double bad : {0.0, -1.0, k_nan}) {
    const fans::Duty duty{.flow = m3_s(bad), .pressure = total(150.0)};
    const auto r = fans::check_feasibility(duty, k_air, k_unsourced);
    REQUIRE(!r.has_value());
    CHECK(r.error().code() == core::ErrorCode::out_of_validity);
    CHECK(r.error().subject() == "flow");
    CHECK(fans::check_feasibility(k_duty, k_air, k_unsourced, rad_s(bad)).error().subject() ==
          "omega");
    CHECK(fans::check_feasibility(k_duty, k_air, k_unsourced, rad_s(100.0), m(bad))
              .error()
              .subject() == "d_tip");
    const fans::Duty bad_p{.flow = m3_s(0.05), .pressure = total(bad)};
    CHECK(fans::check_feasibility(bad_p, k_air, k_unsourced).error().subject() ==
          "fan_total_pressure");
  }
  auto air = k_air;
  air.gamma = -1.0 * isq::ratio_of_specific_heat_capacities[mp_units::one];
  CHECK(fans::check_feasibility(k_duty, air, k_unsourced).error().subject() == "air.gamma");

  for (const auto& [lo, hi] : {std::pair{2.0, 1.0}, std::pair{0.0, 1.0}, std::pair{1.0, k_nan}}) {
    const auto r = fans::check_feasibility(k_duty, k_air, sourced(lo, hi), rad_s(k_omega));
    REQUIRE(!r.has_value());
    CHECK(r.error().code() == core::ErrorCode::invalid_input);
    CHECK(r.error().subject() == "family_range");
  }
}

TEST_CASE("check status names", "[SEL-004]") {
  CHECK(fans::to_string(CheckStatus::pass) == "pass");
  CHECK(fans::to_string(CheckStatus::violated) == "violated");
  CHECK(fans::to_string(CheckStatus::range_unsourced) == "range_unsourced");
  CHECK(fans::to_string(CheckStatus::not_computable) == "not_computable");
  CHECK(core::to_string(fans::feasibility_model()) == "fans.feasibility@1.0.0");
  CHECK(fans::to_string(fans::Verdict::infeasible) == "infeasible");
  CHECK(fans::to_string(fans::Verdict::unconfirmed) == "unconfirmed");
  CHECK(fans::to_string(fans::Verdict::confirmed) == "confirmed");
}

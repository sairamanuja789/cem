#include "products/fans/families/axial_ducted/vortex.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/fidelity.hpp"

namespace cemkit::fans::axial_ducted {
namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;

constexpr core::SemVer k_version{.major = 0, .minor = 1, .patch = 0};
constexpr std::size_t k_intervals = 200;  // Simpson intervals (numerical choice, as the reference)
constexpr int k_max_doublings = 200;
constexpr double k_duty_tolerance = 1e-9;  // numerical guard on the final rise, as the reference

double m(core::Length l) { return l.numerical_value_in(si::metre); }
core::Length len(double v) { return v * isq::length[si::metre]; }
core::Speed speed(double v) { return v * isq::speed[si::metre / si::second]; }
core::Angle angle(double v) { return v * isq::angular_measure[si::radian]; }
core::Ratio ratio(double v) { return v * mp_units::one; }

// Adjacent doubles (lo, hi) with f false at lo and true at hi, from such a bracket.
template <class F>
std::pair<double, double> bisect(F&& f, double lo, double hi) {
  while (true) {
    const double mid = 0.5 * (lo + hi);
    if (mid <= lo || mid >= hi) {
      return {lo, hi};
    }
    if (f(mid)) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
}

class Equilibrium {
 public:
  Equilibrium(double n, double omega, double r_hub, double r_tip)
      : n_{n}, omega_{omega}, r_hub_{r_hub} {
    const double h = (r_tip - r_hub) / static_cast<double>(k_intervals);
    for (std::size_t j = 0; j <= k_intervals; ++j) {
      const double r = r_hub + (static_cast<double>(j) * h);
      double weight = 2.0;  // composite Simpson: 1, 4, 2, 4, ..., 4, 1
      if (j == 0 || j == k_intervals) {
        weight = 1.0;
      } else if (j % 2 != 0) {
        weight = 4.0;
      }
      nodes_.push_back(r);
      weights_.push_back((h / 3.0) * weight);
      a_.push_back(work_term(r));
      b_.push_back(swirl_term(r));
    }
  }

  [[nodiscard]] double work_term(double r) const {
    return 2.0 * omega_ * (std::pow(r, n_ + 1.0) - std::pow(r_hub_, n_ + 1.0));
  }

  [[nodiscard]] double swirl_term(double r) const {
    const double x = std::log(r / r_hub_);
    const double integral =
        n_ == 0.0 ? x : std::pow(r_hub_, 2.0 * n_) * std::expm1(2.0 * n_ * x) / (2.0 * n_);
    return 2.0 * (n_ + 1.0) * integral;
  }

  [[nodiscard]] static double cx2_squared(double a_hub, double k, double a, double b) {
    return a_hub + (k * a) - (k * k * b);
  }

  [[nodiscard]] double min_hub(double k) const {
    double worst = -std::numeric_limits<double>::infinity();
    for (std::size_t j = 0; j < nodes_.size(); ++j) {
      worst = std::max(worst, (k * k * b_[j]) - (k * a_[j]));
    }
    return std::max(worst, 0.0);
  }

  [[nodiscard]] double flow(double a_hub, double k) const {
    double total = 0.0;
    for (std::size_t j = 0; j < nodes_.size(); ++j) {
      total += weights_[j] * 2.0 * std::numbers::pi * nodes_[j] *
               std::sqrt(std::max(cx2_squared(a_hub, k, a_[j], b_[j]), 0.0));
    }
    return total;
  }

  [[nodiscard]] double work_flow(double a_hub, double k) const {
    double total = 0.0;
    for (std::size_t j = 0; j < nodes_.size(); ++j) {
      const double c = std::sqrt(std::max(cx2_squared(a_hub, k, a_[j], b_[j]), 0.0));
      total += weights_[j] * 2.0 * std::numbers::pi * nodes_[j] * std::pow(nodes_[j], n_ + 1.0) * c;
    }
    return omega_ * k * total;
  }

 private:
  double n_;
  double omega_;
  double r_hub_;
  std::vector<double> nodes_;
  std::vector<double> weights_;
  std::vector<double> a_;
  std::vector<double> b_;
};

std::unexpected<core::Error> invalid(const std::string& message, double value,
                                     const std::string& bounds) {
  return core::fail(core::ErrorCode::out_of_validity, message, "vortex_exponent",
                    {{"value", core::format_number(value)},
                     {"bounds", bounds},
                     {"model", core::to_string(vortex_model())}});
}

}  // namespace

core::ModelId vortex_model() { return {.name = "fans.axial.vortex", .version = k_version}; }

core::Result<FreeVortexDesign> vortex_design(const FreeVortexInputs& in, double vortex_exponent,
                                             const physics::Air& air) {
  auto line = mean_line(in.flow, in.pressure, in.omega, in.r_tip, in.r_hub, air);
  if (!line) {
    return std::unexpected{std::move(line).error()};
  }
  const auto model = vortex_model();
  if (auto ok = check_sections(in, model); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  const double n = vortex_exponent;
  if (!std::isfinite(n) || n < -1.0 || n > 1.0) {
    return invalid("the vortex exponent must be in [-1, 1]", n, "[-1, 1]");
  }
  const double rh = m(in.r_hub);
  const double rt = m(in.r_tip);
  const double ch = m(in.chord_hub);
  const double ct = m(in.chord_tip);
  const double t = m(in.thickness);
  const double embed = m(in.embed_depth);
  const double w = in.omega.numerical_value_in(si::radian / si::second);
  const double dp = in.pressure.numerical_value_in(si::pascal);
  const double q = in.flow.numerical_value_in(mp_units::cubic(si::metre) / si::second);
  const double rho = physics::to_si(air).density;
  const double c_x1 = line->axial_velocity.numerical_value_in(si::metre / si::second);
  const Equilibrium eq{n, w, rh, rt};

  const auto hub_for = [&](double k) -> std::optional<double> {
    const double lo = eq.min_hub(k);
    if (eq.flow(lo, k) >= q) {
      return std::nullopt;  // continuity needs reverse flow somewhere behind the rotor
    }
    double hi = lo + (c_x1 * c_x1);
    for (int i = 0; i < k_max_doublings; ++i) {
      if (eq.flow(hi, k) >= q) {
        return bisect([&](double x) { return eq.flow(x, k) >= q; }, lo, hi).second;
      }
      hi = lo + (2.0 * (hi - lo));
    }
    return std::nullopt;
  };
  const auto rise = [&](double k) -> std::optional<double> {
    const auto a_hub = hub_for(k);
    if (!a_hub) {
      return std::nullopt;
    }
    return rho * eq.work_flow(*a_hub, k) / q;
  };
  const auto reverse = [&] {
    return invalid(
        "simple radial equilibrium needs reverse flow behind the rotor to reach this duty at "
        "this vortex exponent",
        n, "c_x2^2 > 0 at every radius");
  };

  // Bracket K exactly as the reference does.
  double k_lo = 0.0;
  const double r_mid = 0.5 * (rh + rt);
  double k_hi = dp / (rho * w * std::pow(r_mid, n + 1.0));
  bool bracketed = false;
  for (int i = 0; i < k_max_doublings; ++i) {
    auto p = rise(k_hi);
    if (!p) {
      k_hi = bisect([&](double x) { return !rise(x).has_value(); }, k_lo, k_hi).first;
      p = rise(k_hi);
      if (!p || *p < dp) {
        return reverse();
      }
      bracketed = true;
      break;
    }
    if (*p >= dp) {
      bracketed = true;
      break;
    }
    k_lo = k_hi;
    k_hi = 2.0 * k_hi;
  }
  if (!bracketed) {
    return reverse();
  }
  const double k = bisect(
                       [&](double x) {
                         const auto p = rise(x);
                         return !p || *p >= dp;
                       },
                       k_lo, k_hi)
                       .second;
  const auto a_hub = hub_for(k);
  if (!a_hub) {
    return reverse();
  }
  const double achieved = rho * eq.work_flow(*a_hub, k) / q;
  if (!(std::abs((achieved / dp) - 1.0) <= k_duty_tolerance)) {
    return invalid("the duty search did not converge (the rise is not monotonic in K here)", n,
                   "rise within " + core::format_number(k_duty_tolerance) + " of the duty; got " +
                       core::format_number(achieved) + " Pa");
  }

  std::vector<FreeVortexStation> stations;
  stations.reserve(in.stations + 1);
  for (std::size_t i = 0; i < in.stations; ++i) {
    const double f = static_cast<double>(i) / static_cast<double>(in.stations - 1);
    const double r = rh + (f * (rt - rh));
    const double chord = ch + (f * (ct - ch));
    const double c_x2_sq = Equilibrium::cx2_squared(*a_hub, k, eq.work_term(r), eq.swirl_term(r));
    if (!(c_x2_sq > 0.0)) {
      return reverse();
    }
    const double c_x2 = std::sqrt(c_x2_sq);
    const double u = w * r;
    const double c_theta2 = k * std::pow(r, n);
    const double w_theta2 = u - c_theta2;
    const double beta1 = std::atan2(u, c_x1);
    const double beta2 = std::atan2(w_theta2, c_x2);
    const double theta = beta1 - beta2;
    const double xi = (beta1 + beta2) / 2.0;
    const double w1 = std::sqrt((c_x1 * c_x1) + (u * u));
    const double w2 = std::sqrt((c_x2 * c_x2) + (w_theta2 * w_theta2));
    const double reaction =
        1.0 - (((c_x2 * c_x2) - (c_x1 * c_x1) + (c_theta2 * c_theta2)) / (2.0 * u * c_theta2));
    stations.push_back(FreeVortexStation{.radius = len(r),
                                         .blade_speed = speed(u),
                                         .outlet_swirl = speed(c_theta2),
                                         .relative_inlet_angle = angle(beta1),
                                         .relative_outlet_angle = angle(beta2),
                                         .camber_angle = angle(theta),
                                         .stagger_from_axis = angle(xi),
                                         .recipe_stagger = angle(-xi),
                                         .chord = len(chord),
                                         .camber = len((chord / 2.0) * std::tan(theta / 4.0)),
                                         .thickness = len(t),
                                         .de_haller_ratio = ratio(w2 / w1),
                                         .reaction = ratio(reaction),
                                         .embedded = false,
                                         .outlet_axial_velocity = speed(c_x2)});
  }
  FreeVortexStation root = stations.front();
  root.radius = len(rh - embed);
  root.embedded = true;
  stations.insert(stations.begin(), root);
  return FreeVortexDesign{.mean_line = *line,
                          .axial_velocity = line->axial_velocity,
                          .vortex_exponent = n,
                          .swirl_constant = k,
                          .hub_axial_velocity_squared = *a_hub,
                          .stations = std::move(stations),
                          .fidelity = core::Fidelity::l1_predicted,
                          .model = model};
}

}  // namespace cemkit::fans::axial_ducted

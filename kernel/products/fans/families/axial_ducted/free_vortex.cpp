#include "products/fans/families/axial_ducted/free_vortex.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "cemkit/core/result.hpp"
#include "cemkit/physics/validity.hpp"

namespace cemkit::fans::axial_ducted {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;

constexpr core::SemVer k_version{.major = 0, .minor = 1, .patch = 0};

double m(core::Length l) { return l.numerical_value_in(si::metre); }
core::Length len(double v) { return v * isq::length[si::metre]; }
core::Speed speed(double v) { return v * isq::speed[si::metre / si::second]; }
core::Angle angle(double v) { return v * isq::angular_measure[si::radian]; }
core::Ratio ratio(double v) { return v * mp_units::one; }

std::unexpected<core::Error> out_of_validity(const std::string& message, const std::string& subject,
                                             const std::string& value, const std::string& bounds) {
  return core::fail(
      core::ErrorCode::out_of_validity, message, subject,
      {{"value", value}, {"bounds", bounds}, {"model", core::to_string(free_vortex_model())}});
}

}  // namespace

core::ModelId free_vortex_model() {
  return {.name = "fans.axial.free_vortex", .version = k_version};
}

core::Result<FreeVortexDesign> free_vortex_design(const FreeVortexInputs& in,
                                                  const physics::Air& air) {
  auto line = mean_line(in.flow, in.pressure, in.omega, in.r_tip, in.r_hub, air);
  if (!line) {
    return std::unexpected{std::move(line).error()};
  }
  const auto model = free_vortex_model();
  const double ch = m(in.chord_hub);
  const double ct = m(in.chord_tip);
  const double t = m(in.thickness);
  const double embed = m(in.embed_depth);
  const double rh = m(in.r_hub);
  const double rt = m(in.r_tip);
  // The same check order as the reference (PHY-005).
  for (const auto& [subject, value] : std::initializer_list<std::pair<std::string_view, double>>{
           {"chord_hub", ch}, {"chord_tip", ct}, {"thickness", t}, {"embed_depth", embed}}) {
    if (auto ok = physics::require_positive(subject, value, model); !ok) {
      return std::unexpected{std::move(ok).error()};
    }
  }
  if (in.stations < 3) {
    return out_of_validity("at least 3 blade stations are needed", "stations",
                           std::to_string(in.stations), "[3, inf)");
  }
  if (!(embed < rh)) {
    return out_of_validity("the embed depth must be less than the hub radius", "embed_depth",
                           core::format_number(embed), "(0, " + core::format_number(rh) + ")");
  }
  if (t >= ch || t >= ct) {  // all three are finite and positive here
    return out_of_validity("the blade thickness must be less than every chord", "thickness",
                           core::format_number(t),
                           "(0, " + core::format_number(std::min(ch, ct)) + ")");
  }

  const double w = in.omega.numerical_value_in(si::radian / si::second);
  const double dp = in.pressure.numerical_value_in(si::pascal);
  const double rho = physics::to_si(air).density;
  const double c_x = line->axial_velocity.numerical_value_in(si::metre / si::second);
  std::vector<FreeVortexStation> stations;
  stations.reserve(in.stations + 1);
  for (std::size_t i = 0; i < in.stations; ++i) {
    const double f = static_cast<double>(i) / static_cast<double>(in.stations - 1);
    const double r = rh + (f * (rt - rh));
    const double chord = ch + (f * (ct - ch));
    const double u = w * r;
    const double c_theta2 = dp / (rho * w * r);  // free vortex: r c_theta2 constant
    const double w_theta2 = u - c_theta2;
    const double beta1 = std::atan2(u, c_x);
    const double beta2 = std::atan2(w_theta2, c_x);
    const double theta = beta1 - beta2;
    const double xi = (beta1 + beta2) / 2.0;
    const double w1 = std::sqrt((c_x * c_x) + (u * u));
    const double w2 = std::sqrt((c_x * c_x) + (w_theta2 * w_theta2));
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
                                         .reaction = ratio(1.0 - (c_theta2 / (2.0 * u))),
                                         .embedded = false});
  }
  FreeVortexStation root = stations.front();
  root.radius = len(rh - embed);
  root.embedded = true;
  stations.insert(stations.begin(), root);
  return FreeVortexDesign{.mean_line = *line,
                          .axial_velocity = line->axial_velocity,
                          .stations = std::move(stations),
                          .fidelity = core::Fidelity::l1_predicted,
                          .model = model};
}

}  // namespace cemkit::fans::axial_ducted

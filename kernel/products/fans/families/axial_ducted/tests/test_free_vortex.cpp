// fans.axial.free_vortex@0.1.0 (PHY-001, PHY-005): the kernel port on the l1_002 hand calculation
// (tests/hand_calcs/proposed/l1_002_free_vortex_sections.yaml), validity, and the recipe sign
// convention: the built section's end tangents follow beta1 and beta2 (ADR-016). TRIAL inputs.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>

#include "products/fans/families/axial_ducted/free_vortex.hpp"
#include "products/fans/families/axial_ducted/geometry.hpp"

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using cemkit::core::ErrorCode;
using cemkit::fans::axial_ducted::FreeVortexInputs;

namespace {
cemkit::core::Length len(double v) { return v * isq::length[si::metre]; }

FreeVortexInputs trial(std::size_t stations = 3, double embed = 0.003) {
  return FreeVortexInputs{
      .flow =
          0.028 * cemkit::core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second],
      .pressure = 21.6 * cemkit::fans::fan_total_pressure[si::pascal],
      .omega = 209.43951023931953 * isq::angular_velocity[si::radian / si::second],
      .r_tip = len(0.0595),
      .r_hub = len(0.020),
      .chord_hub = len(0.020),
      .chord_tip = len(0.016),
      .thickness = len(0.0012),
      .stations = stations,
      .embed_depth = len(embed)};
}

double rad(cemkit::core::Angle a) { return a.numerical_value_in(si::radian); }
double m(cemkit::core::Length l) { return l.numerical_value_in(si::metre); }
}  // namespace

TEST_CASE("l1_002: free-vortex sections at hub, mid and tip", "[PHY-001][PHY-005]") {
  const auto d =
      cemkit::fans::axial_ducted::free_vortex_design(trial(), cemkit::physics::default_air());
  REQUIRE(d.has_value());
  REQUIRE(d->stations.size() == 4);
  const auto& root = d->stations[0];
  const auto& hub = d->stations[1];
  const auto& tip = d->stations[3];
  CHECK(root.embedded);
  CHECK_THAT(m(root.radius), WithinRel(0.017, 1e-12));
  CHECK_THAT(rad(hub.relative_outlet_angle), WithinRel(-0.0637660452291582, 1e-12));
  CHECK_THAT(rad(hub.camber_angle), WithinRel(1.0390480079855122, 1e-12));
  CHECK_THAT(m(hub.camber), WithinRel(0.0026576671733706658, 1e-12));
  CHECK_THAT(hub.de_haller_ratio.numerical_value_in(mp_units::one),
             WithinRel(0.5620769960178094, 1e-12));
  CHECK_THAT(rad(tip.recipe_stagger), WithinRel(-1.3324930626545326, 1e-12));
  CHECK_THAT(m(tip.camber), WithinRel(5.747262264778856e-05, 1e-12));
  CHECK(d->fidelity == cemkit::core::Fidelity::l1_predicted);
}

TEST_CASE("free vortex: outside validity is an error, never a number", "[PHY-003]") {
  const auto air = cemkit::physics::default_air();
  const auto few = cemkit::fans::axial_ducted::free_vortex_design(trial(2), air);
  REQUIRE_FALSE(few.has_value());
  CHECK((few.error().code() == ErrorCode::out_of_validity && few.error().subject() == "stations"));
  const auto deep = cemkit::fans::axial_ducted::free_vortex_design(trial(3, 0.020), air);
  REQUIRE_FALSE(deep.has_value());
  CHECK(deep.error().subject() == "embed_depth");
  auto thick = trial();
  thick.thickness = len(0.017);
  const auto t = cemkit::fans::axial_ducted::free_vortex_design(thick, air);
  REQUIRE_FALSE(t.has_value());
  CHECK(t.error().subject() == "thickness");
  auto nan_chord = trial();
  nan_chord.chord_tip = len(std::nan(""));
  const auto c = cemkit::fans::axial_ducted::free_vortex_design(nan_chord, air);
  REQUIRE_FALSE(c.has_value());
  CHECK(c.error().subject() == "chord_tip");
  auto bad_hub = trial();
  bad_hub.r_hub = len(0.0595);  // through the mean-line validity
  const auto h = cemkit::fans::axial_ducted::free_vortex_design(bad_hub, air);
  REQUIRE_FALSE(h.has_value());
  CHECK(h.error().subject() == "r_hub");
}

TEST_CASE("the built section's end tangents follow beta1 and beta2", "[ADR-016][GEO-001]") {
  // Flow along +z, counter-clockwise from the inlet: the relative flow at a blade on +x moves
  // towards +y, at an angle beta from +z. The camber line is the mid-line of the two surfaces.
  const auto d =
      cemkit::fans::axial_ducted::free_vortex_design(trial(5), cemkit::physics::default_air());
  REQUIRE(d.has_value());
  for (const auto& s : d->stations) {
    constexpr std::size_t n = 2001;
    const auto section = cemkit::fans::axial_ducted::blade_section({.radius = s.radius,
                                                                    .chord = s.chord,
                                                                    .stagger = s.recipe_stagger,
                                                                    .camber = s.camber,
                                                                    .thickness = s.thickness},
                                                                   n);
    const auto mid = [&](std::size_t i, auto member) {
      return (m(section.upper[i].*member) + m(section.lower[i].*member)) / 2.0;
    };
    using P = cemkit::geometry::port::Point3;
    const double le = std::atan2(mid(1, &P::y) - mid(0, &P::y), mid(1, &P::z) - mid(0, &P::z));
    const double te =
        std::atan2(mid(n - 1, &P::y) - mid(n - 2, &P::y), mid(n - 1, &P::z) - mid(n - 2, &P::z));
    CHECK_THAT(le, WithinAbs(rad(s.relative_inlet_angle), 2e-3));
    CHECK_THAT(te, WithinAbs(rad(s.relative_outlet_angle), 2e-3));
  }
}

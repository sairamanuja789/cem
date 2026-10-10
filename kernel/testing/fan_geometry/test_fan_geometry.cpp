// The fans.axial_ducted geometry recipe on the OpenCascade backend (ADR-015 M3; GEO-001, GEO-002,
// AX-009). Every dimension here is a GEOMETRY TEST PARAMETER, not a design and not sourced data;
// nothing about performance is checked or implied.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <numbers>

#include "cemkit/geometry/occt/occt_backend.hpp"
#include "products/fans/families/axial_ducted/geometry.hpp"

namespace port = cemkit::geometry::port;
namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace fan = cemkit::fans::axial_ducted;
using Catch::Matchers::WithinRel;
using cemkit::core::ErrorCode;

namespace {

constexpr auto k_mm = si::milli<si::metre>;
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();

cemkit::core::Length mm(double v) { return v * isq::length[k_mm]; }
cemkit::core::Angle deg(double v) {
  return (v * std::numbers::pi / 180.0) * isq::angular_measure[si::radian];
}

fan::FanGeometryParams test_params(std::size_t blades) {
  return fan::FanGeometryParams{.blade_count = blades,
                                .hub_radius = mm(20.0),
                                .hub_length = mm(30.0),
                                .stations = {{.radius = mm(17.0),
                                              .chord = mm(20.0),
                                              .stagger = deg(45.0),
                                              .camber = mm(1.5),
                                              .thickness = mm(1.2)},
                                             {.radius = mm(38.0),
                                              .chord = mm(18.0),
                                              .stagger = deg(58.0),
                                              .camber = mm(1.5),
                                              .thickness = mm(1.2)},
                                             {.radius = mm(59.5),
                                              .chord = mm(16.0),
                                              .stagger = deg(66.0),
                                              .camber = mm(1.2),
                                              .thickness = mm(1.2)}},
                                .duct_inner_radius = mm(60.0),
                                .duct_wall = mm(2.0),
                                .duct_length = mm(30.0),
                                .root_fillet = std::nullopt};
}

double volume_mm3(const port::GeometryBackend& b, const port::Solid& s) {
  const auto m = b.mass_properties(s);
  REQUIRE(m.has_value());
  return m.has_value() ? m->volume.numerical_value_in(mp_units::cubic(k_mm)) : k_nan;
}

}  // namespace

TEST_CASE("the recipe builds a valid rotor and duct from stated parameters",
          "[GEO-001][GEO-002][AX-009]") {
  const cemkit::geometry::occt::OcctBackend b;
  const auto g = fan::build_fan_geometry(b, test_params(7));
  REQUIRE(g.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE above
  CHECK(b.check_validity(*g->rotor).value_or(port::ValidityReport{}).ok());
  CHECK(b.check_validity(*g->duct).value_or(port::ValidityReport{}).ok());
  CHECK_THAT(g->tip_clearance.numerical_value_in(k_mm), WithinRel(0.5, 1e-12));
  // The duct is an annulus: closed-form volume.
  CHECK_THAT(volume_mm3(b, *g->duct),
             WithinRel(std::numbers::pi * ((62.0 * 62.0) - (60.0 * 60.0)) * 30.0, 1e-6));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("the same parameters give the same solid; each blade adds the same volume", "[GEO-001]") {
  const cemkit::geometry::occt::OcctBackend b;
  const auto five = fan::build_fan_geometry(b, test_params(5));
  const auto five_again = fan::build_fan_geometry(b, test_params(5));
  const auto six = fan::build_fan_geometry(b, test_params(6));
  const auto seven = fan::build_fan_geometry(b, test_params(7));
  REQUIRE((five && five_again && six && seven));
  // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE above
  CHECK(volume_mm3(b, *five->rotor) == volume_mm3(b, *five_again->rotor));
  const double per_blade_6 = volume_mm3(b, *six->rotor) - volume_mm3(b, *five->rotor);
  const double per_blade_7 = volume_mm3(b, *seven->rotor) - volume_mm3(b, *six->rotor);
  CHECK(per_blade_6 > 0.0);
  CHECK_THAT(per_blade_7, WithinRel(per_blade_6, 1e-6));  // blades do not overlap outside the hub
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("a root fillet on the rule-selected junction edges keeps one valid solid", "[GEO-002]") {
  const cemkit::geometry::occt::OcctBackend b;
  auto params = test_params(5);
  params.root_fillet = mm(0.4);
  const auto plain = fan::build_fan_geometry(b, test_params(5));
  const auto filleted = fan::build_fan_geometry(b, params);
  REQUIRE((plain && filleted));
  // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE above
  CHECK(b.check_validity(*filleted->rotor).value_or(port::ValidityReport{}).ok());
  CHECK(volume_mm3(b, *filleted->rotor) > volume_mm3(b, *plain->rotor));
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("a flat-plate section is symmetric; a cambered one bows towards +y at stagger 0",
          "[GEO-001]") {
  const fan::BladeStation flat{.radius = mm(30.0),
                               .chord = mm(20.0),
                               .stagger = deg(0.0),
                               .camber = mm(0.0),
                               .thickness = mm(1.0)};
  const auto s = fan::blade_section(flat, 5);
  CHECK_THAT(s.upper[2].y.numerical_value_in(k_mm), WithinRel(0.5, 1e-12));
  CHECK_THAT(s.lower[2].y.numerical_value_in(k_mm), WithinRel(-0.5, 1e-12));
  auto cambered = flat;
  cambered.camber = mm(2.0);
  const auto c = fan::blade_section(cambered, 5);
  // Mid-chord: camber height plus half the thickness; the ends stay on the chord line.
  CHECK_THAT(c.upper[2].y.numerical_value_in(k_mm), WithinRel(2.5, 1e-12));
  CHECK(std::abs(((c.upper[0].y + c.lower[0].y) / 2.0).numerical_value_in(k_mm)) < 1e-12);
}

TEST_CASE("invalid parameters are geometry_failed naming the parameter", "[GEO-002][AX-009]") {
  const cemkit::geometry::occt::OcctBackend b;
  const auto subject = [&](const fan::FanGeometryParams& p) {
    const auto r = fan::build_fan_geometry(b, p);
    REQUIRE(!r.has_value());
    CHECK(r.error().code() == ErrorCode::geometry_failed);
    return r.error().subject();
  };
  auto p = test_params(5);
  p.stations.back().radius = mm(60.0);  // no tip clearance
  CHECK(subject(p) == "stations[2].radius");
  p = test_params(5);
  p.stations.front().radius = mm(21.0);  // root outside the hub
  CHECK(subject(p) == "stations[0].radius");
  p = test_params(0);
  CHECK(subject(p) == "blade_count");
  p = test_params(5);
  p.stations[1].camber = mm(9.0);  // camber >= chord / 2
  CHECK(subject(p) == "stations[1].camber");
  p = test_params(5);
  p.hub_length = mm(10.0);  // the root reaches beyond the hub ends
  CHECK(subject(p) == "stations[0]");
}

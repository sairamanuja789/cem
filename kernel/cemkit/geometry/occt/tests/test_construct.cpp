// M2 construction operations (ADR-015 rule 7; GEO-001, GEO-002): loft through spline sections,
// revolve, circular pattern, booleans, rule-based edge selection and fillet. Volumes are checked
// against closed-form values; every dimension is a test parameter, not a design.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/geometry/occt/occt_backend.hpp"
#include "cemkit/geometry/port/backend.hpp"

namespace port = cemkit::geometry::port;
namespace si = mp_units::si;
namespace isq = mp_units::isq;
using Catch::Matchers::WithinRel;
using cemkit::core::ErrorCode;
using cemkit::geometry::occt::OcctBackend;

namespace {

constexpr auto k_mm = si::milli<si::metre>;
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_rel = 1e-6;

cemkit::core::Length mm(double v) { return v * isq::length[k_mm]; }

port::Point3 p(double x, double y, double z) { return {.x = mm(x), .y = mm(y), .z = mm(z)}; }

double volume_mm3(const OcctBackend& b, const port::Solid& s) {
  const auto m = b.mass_properties(s);
  REQUIRE(m.has_value());
  return m.has_value() ? m->volume.numerical_value_in(mp_units::cubic(k_mm)) : k_nan;
}

// A rectangle 2 x 6 mm in the plane x = const (y in [-1, 1], z in [-3, 3]) as a SplineSection.
port::SplineSection rect_at(double x) {
  return {.upper = {p(x, 1.0, -3.0), p(x, 1.0, 3.0)}, .lower = {p(x, -1.0, -3.0), p(x, -1.0, 3.0)}};
}

port::RevolveProfile cylinder_profile(double r, double z0, double z1) {
  return {.rz = {{mm(0.0), mm(z0)}, {mm(r), mm(z0)}, {mm(r), mm(z1)}, {mm(0.0), mm(z1)}}};
}

std::unique_ptr<port::Solid> require_solid(cemkit::core::Result<std::unique_ptr<port::Solid>> r) {
  REQUIRE(r.has_value());
  return r.has_value() ? std::move(*r) : nullptr;
}

}  // namespace

TEST_CASE("a revolved annulus has the closed-form volume", "[GEO-001][GEO-002]") {
  const OcctBackend b;
  const port::RevolveProfile annulus{
      .rz = {{mm(10.0), mm(0.0)}, {mm(12.0), mm(0.0)}, {mm(12.0), mm(20.0)}, {mm(10.0), mm(20.0)}}};
  const auto solid = require_solid(b.revolve(annulus));
  CHECK_THAT(volume_mm3(b, *solid), WithinRel(std::numbers::pi * (144.0 - 100.0) * 20.0, k_rel));
}

TEST_CASE("a loft through identical sections is a prism", "[GEO-001][GEO-002]") {
  const OcctBackend b;
  const auto solid = require_solid(b.loft({rect_at(5.0), rect_at(30.0)}));
  CHECK_THAT(volume_mm3(b, *solid), WithinRel(25.0 * 2.0 * 6.0, k_rel));
}

TEST_CASE("spline sections loft deterministically into a valid solid", "[GEO-001][GEO-002]") {
  const OcctBackend b;
  // A cambered section: upper and lower arcs through 5 points each, blunt ends.
  const auto section = [](double x, double camber) {
    port::SplineSection s;
    for (int i = 0; i <= 4; ++i) {
      const double z = -6.0 + (3.0 * i);
      const double y = camber * (1.0 - ((z / 6.0) * (z / 6.0)));
      s.upper.push_back(p(x, y + 0.5, z));
      s.lower.push_back(p(x, y - 0.5, z));
    }
    return s;
  };
  const auto a = require_solid(b.loft({section(5.0, 1.0), section(20.0, 2.0), section(40.0, 1.5)}));
  const auto c = require_solid(b.loft({section(5.0, 1.0), section(20.0, 2.0), section(40.0, 1.5)}));
  CHECK(volume_mm3(b, *a) == volume_mm3(b, *c));
  CHECK(b.topology(*a).value_or(port::Topology{}) == b.topology(*c).value_or(port::Topology{}));
  CHECK(b.check_validity(*a).value_or(port::ValidityReport{}).ok());
}

TEST_CASE("a hub fused with a circular pattern of blades has the closed-form volume",
          "[GEO-001][GEO-002]") {
  const OcctBackend b;
  const auto hub = require_solid(b.revolve(cylinder_profile(10.0, -5.0, 5.0)));
  const auto blade = require_solid(b.loft({rect_at(5.0), rect_at(30.0)}));
  const auto rotor = require_solid(b.fuse_circular_pattern(*hub, *blade, 4));
  // The blade part inside the hub: 6 * integral over |y| <= 1 of (sqrt(100 - y^2) - 5) dy.
  const double inside = 6.0 * (std::sqrt(99.0) + (100.0 * std::asin(0.1)) - 10.0);
  const double expected = (std::numbers::pi * 100.0 * 10.0) + (4.0 * (300.0 - inside));
  CHECK_THAT(volume_mm3(b, *rotor), WithinRel(expected, k_rel));

  SECTION("the junction edges are selected by rule, not by index") {
    // Per blade: two straight lines 6 mm long (side planes y = +-1) and two arcs of
    // 10 * 2 asin(1/10) mm (top and bottom planes) on the hub. The hub's end circles and the
    // cylinder seam are not selected. The count is engine topology (the seam splits one blade's
    // arcs); the total length is geometry.
    const port::EdgesOnCylinder rule{.radius = mm(10.0), .tolerance = mm(1e-3)};
    const double per_blade = (2.0 * 6.0) + (2.0 * 10.0 * 2.0 * std::asin(0.1));
    for (const std::size_t n : {std::size_t{3}, std::size_t{4}}) {
      const auto r = require_solid(b.fuse_circular_pattern(*hub, *blade, n));
      const auto sel = b.select(*r, rule);
      REQUIRE(sel.has_value());
      CHECK_THAT(sel.has_value() ? sel->length.numerical_value_in(k_mm) : k_nan,
                 WithinRel(static_cast<double>(n) * per_blade, k_rel));
      CHECK(sel.value_or(port::EdgeSelection{}).count >= 4 * n);
    }
  }

  SECTION("filleting the junction edges keeps one valid solid and adds material") {
    const port::EdgesOnCylinder rule{.radius = mm(10.0), .tolerance = mm(1e-3)};
    const auto filleted = require_solid(b.fillet(*rotor, rule, mm(0.5)));
    CHECK(volume_mm3(b, *filleted) > volume_mm3(b, *rotor));
    CHECK(b.check_validity(*filleted).value_or(port::ValidityReport{}).ok());
  }
}

TEST_CASE("fuse and cut give the closed-form volumes", "[GEO-001][GEO-002]") {
  const OcctBackend b;
  const auto outer = require_solid(b.revolve(cylinder_profile(10.0, 0.0, 10.0)));
  const auto inner = require_solid(b.revolve(cylinder_profile(4.0, -1.0, 11.0)));
  const auto tube = require_solid(b.cut(*outer, *inner));
  CHECK_THAT(volume_mm3(b, *tube), WithinRel(std::numbers::pi * (100.0 - 16.0) * 10.0, k_rel));
  const auto cap = require_solid(b.revolve(cylinder_profile(10.0, 9.0, 12.0)));
  const auto fused = require_solid(b.fuse(*outer, *cap));
  CHECK_THAT(volume_mm3(b, *fused), WithinRel(std::numbers::pi * 100.0 * 12.0, k_rel));
}

TEST_CASE("invalid construction inputs are geometry_failed naming the input", "[GEO-002]") {
  const OcctBackend b;
  const auto code = [](const auto& r) {
    REQUIRE(!r.has_value());
    return r.error().code();
  };
  CHECK(code(b.loft({rect_at(5.0)})) == ErrorCode::geometry_failed);
  port::SplineSection one_point = rect_at(5.0);
  one_point.upper.resize(1);
  const auto bad = b.loft({one_point, rect_at(9.0)});
  CHECK(code(bad) == ErrorCode::geometry_failed);
  CHECK(bad.error().subject() == "sections[0].upper");
  CHECK(code(b.revolve({.rz = {{mm(-1.0), mm(0.0)}, {mm(2.0), mm(0.0)}, {mm(2.0), mm(1.0)}}})) ==
        ErrorCode::geometry_failed);
  const auto hub = require_solid(b.revolve(cylinder_profile(10.0, -5.0, 5.0)));
  const auto far = require_solid(b.loft({rect_at(40.0), rect_at(50.0)}));
  CHECK(code(b.fuse_circular_pattern(*hub, *far, 2)) == ErrorCode::geometry_failed);
  CHECK(code(b.fuse_circular_pattern(*hub, *far, 0)) == ErrorCode::geometry_failed);
  const port::EdgesOnCylinder nothing{.radius = mm(77.0), .tolerance = mm(1e-3)};
  CHECK(code(b.fillet(*hub, nothing, mm(0.5))) == ErrorCode::geometry_failed);
}

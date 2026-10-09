#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <type_traits>

#include "cemkit/core/quantity.hpp"
#include "products/fans/common/pressure.hpp"

namespace si = mp_units::si;
using cemkit::core::Pressure;
using cemkit::fans::FanStaticPressure;
using cemkit::fans::FanTotalPressure;

// Fan total and fan static pressure are distinct kinds (COR-001): neither converts to the other,
// even explicitly, and neither converts implicitly to or from a plain pressure. tests/compile_fail/
// proves the same rules with real builds.
static_assert(!std::convertible_to<FanStaticPressure, FanTotalPressure>);
static_assert(!std::convertible_to<FanTotalPressure, FanStaticPressure>);
static_assert(!std::is_constructible_v<FanTotalPressure, FanStaticPressure>);
static_assert(!std::is_constructible_v<FanStaticPressure, FanTotalPressure>);
static_assert(!std::convertible_to<Pressure, FanTotalPressure>);
static_assert(!std::convertible_to<Pressure, FanStaticPressure>);
static_assert(!std::convertible_to<FanTotalPressure, Pressure>);
static_assert(!std::convertible_to<FanStaticPressure, Pressure>);

// mp-units allows *explicit* construction of a child kind from its parent, so a plain pressure can
// be declared total or static with FanTotalPressure{p}. Only the spec compiler may do this, where
// the user's stated type is recorded (SPEC-004; docs/models/fans/pressure-kinds.md). Pinned here so
// a change in mp-units behaviour is noticed.
static_assert(std::is_constructible_v<FanTotalPressure, Pressure>);
static_assert(std::is_constructible_v<FanStaticPressure, Pressure>);

TEST_CASE("fan pressures are stored in pascal and keep their kind", "[COR-001]") {
  const FanTotalPressure total = 0.25 * cemkit::fans::fan_total_pressure[si::kilo<si::pascal>];
  const FanStaticPressure fan_static = 180.0 * cemkit::fans::fan_static_pressure[si::pascal];
  CHECK(total.numerical_value_in(si::pascal) == 250.0);
  CHECK(fan_static.numerical_value_in(si::pascal) == 180.0);

  const FanTotalPressure doubled = total + total;
  const FanStaticPressure margin =
      fan_static - 30.0 * cemkit::fans::fan_static_pressure[si::pascal];
  CHECK(doubled.numerical_value_in(si::pascal) == 500.0);
  CHECK(margin.numerical_value_in(si::pascal) == 150.0);
}

// [COR-001] Fan total and fan static pressure must not be added to each other.
// Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined.
// See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"
#include "products/fans/common/pressure.hpp"

namespace {

[[maybe_unused]] void probe() {
  namespace si = mp_units::si;
  const cemkit::fans::FanTotalPressure total = 100.0 * cemkit::fans::fan_total_pressure[si::pascal];
  const cemkit::fans::FanStaticPressure fan_static =
      80.0 * cemkit::fans::fan_static_pressure[si::pascal];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const auto sum = total + fan_static;
#else
  [[maybe_unused]] const auto sum = total + total;
  (void)fan_static;
#endif
}

}  // namespace

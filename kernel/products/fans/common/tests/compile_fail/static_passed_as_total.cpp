// [COR-001] A fan static pressure must not be passed where a fan total pressure is required.
// Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined.
// See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"
#include "products/fans/common/pressure.hpp"

namespace {
double takes_total(cemkit::fans::FanTotalPressure p) {
  return p.numerical_value_in(mp_units::si::pascal);
}
}  // namespace

namespace {

[[maybe_unused]] void probe() {
  namespace si = mp_units::si;
  const cemkit::fans::FanTotalPressure total = 100.0 * cemkit::fans::fan_total_pressure[si::pascal];
  const cemkit::fans::FanStaticPressure fan_static =
      80.0 * cemkit::fans::fan_static_pressure[si::pascal];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const double value = takes_total(fan_static);
  (void)total;
#else
  [[maybe_unused]] const double value = takes_total(total);
  (void)fan_static;
#endif
}

}  // namespace

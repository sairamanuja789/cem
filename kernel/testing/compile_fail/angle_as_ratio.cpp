// [COR-001] An angle must not be usable as a dimensionless ratio.
// Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined.
// See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"

namespace {

[[maybe_unused]] void probe() {
  namespace isq = mp_units::isq;
  namespace si = mp_units::si;
  const cemkit::core::Angle angle = 0.5 * isq::angular_measure[si::radian];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const cemkit::core::Ratio ratio = angle;
#else
  [[maybe_unused]] const cemkit::core::Ratio ratio = 0.5 * mp_units::one;
  (void)angle;
#endif
}

}  // namespace

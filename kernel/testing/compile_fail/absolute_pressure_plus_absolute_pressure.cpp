// [COR-001] Two absolute pressures (points) must not be addable; only their difference is a
// quantity. Builds cleanly as written (control); must fail to build with
// CEMKIT_EXPECT_COMPILE_ERROR defined. See cemkit_add_compile_fail_test() in
// kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"

namespace {

[[maybe_unused]] void probe() {
  namespace isq = mp_units::isq;
  namespace si = mp_units::si;
  const cemkit::core::AbsolutePressure a =
      cemkit::core::units::absolute_zero_pressure + 101325.0 * isq::pressure[si::pascal];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const auto sum = a + a;
#else
  [[maybe_unused]] const cemkit::core::Pressure difference = a - a;
#endif
}

}  // namespace

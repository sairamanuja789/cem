// [COR-001] A length must not be assignable to a pressure.
// Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined.
// See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"

namespace {

[[maybe_unused]] void probe() {
  namespace isq = mp_units::isq;
  namespace si = mp_units::si;
  const cemkit::core::Pressure p = 100.0 * isq::pressure[si::pascal];
  const cemkit::core::Length d = 0.12 * isq::length[si::metre];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const cemkit::core::Pressure copy = d;
  (void)p;
#else
  [[maybe_unused]] const cemkit::core::Pressure copy = p;
  (void)d;
#endif
}

}  // namespace

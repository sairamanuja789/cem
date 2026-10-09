// [COR-001] A torque (N m) must not be usable as an energy (J).
// Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined.
// See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"

namespace {

[[maybe_unused]] void probe() {
  namespace isq = mp_units::isq;
  namespace si = mp_units::si;
  using Energy = mp_units::quantity<isq::energy[si::joule], double>;
  const cemkit::core::Torque torque = 1.0 * isq::torque[si::newton * si::metre];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const Energy energy = torque;
#else
  [[maybe_unused]] const Energy energy = 1.0 * isq::energy[si::joule];
  (void)torque;
#endif
}

}  // namespace

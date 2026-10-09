// [COR-001] Adding a pressure to a volume flow rate must not compile.
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
  const cemkit::core::VolumeFlowRate q =
      1.0 * cemkit::core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const auto sum = p + q;
#else
  [[maybe_unused]] const auto sum = p + p;
  (void)q;
#endif
}

}  // namespace

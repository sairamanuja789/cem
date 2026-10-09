// [COR-003] Arithmetic on labelled values must not compile; unwrap with value() and relabel the
// result. Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR
// defined. See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"

namespace {

[[maybe_unused]] void probe() {
  namespace isq = mp_units::isq;
  namespace si = mp_units::si;
  const cemkit::core::Pressure p = 100.0 * isq::pressure[si::pascal];
  const cemkit::core::ModelId model{"probe", cemkit::core::SemVer{1, 0, 0}};
  const auto a = cemkit::core::Labelled<cemkit::core::Pressure>::l1_predicted(p, model).value();
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const auto sum = a + a;
#else
  [[maybe_unused]] const auto sum = a.value() + a.value();
#endif
}

}  // namespace

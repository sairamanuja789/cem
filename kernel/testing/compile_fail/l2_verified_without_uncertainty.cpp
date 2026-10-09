// [COR-003] An L2-verified value must not be constructible without its uncertainty.
// Builds cleanly as written (control); must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined.
// See cemkit_add_compile_fail_test() in kernel/cmake/cemkit.cmake.
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
#ifdef CEMKIT_EXPECT_COMPILE_ERROR
  [[maybe_unused]] const auto verified =
      cemkit::core::Labelled<cemkit::core::Pressure>::l2_verified(p, model);
#else
  [[maybe_unused]] const auto verified =
      cemkit::core::Labelled<cemkit::core::Pressure>::l2_verified(
          p, 4.0 * isq::pressure[si::pascal], model);
#endif
}

}  // namespace

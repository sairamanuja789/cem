// [COR-003] A labelled value must not be constructible without a fidelity label.
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
  [[maybe_unused]] const cemkit::core::Labelled<cemkit::core::Pressure> labelled{p, model};
#else
  [[maybe_unused]] const auto labelled =
      cemkit::core::Labelled<cemkit::core::Pressure>::l1_predicted(p, model);
#endif
}

}  // namespace

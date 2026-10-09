#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "cemkit/core/version.hpp"

TEST_CASE("kernel reports the project version", "[MAINT-001]") {
  CHECK(cemkit::core::kernel_version() == std::string_view{CEMKIT_EXPECTED_VERSION});
}

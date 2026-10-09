#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "fancem/core/version.hpp"

TEST_CASE("kernel reports the project version", "[MAINT-001]") {
  CHECK(fancem::core::kernel_version() == std::string_view{FANCEM_EXPECTED_VERSION});
}

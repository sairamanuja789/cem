#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "cemkit/core/error.hpp"
#include "cemkit/core/version.hpp"

using cemkit::core::ModelId;
using cemkit::core::SemVer;

TEST_CASE("kernel reports the project version", "[MAINT-001][MAINT-005]") {
  CHECK(cemkit::core::kernel_version() == std::string_view{CEMKIT_EXPECTED_VERSION});
  CHECK(cemkit::core::parse_semver(cemkit::core::kernel_version()).has_value());
}

TEST_CASE("semantic versions parse strictly", "[PHY-004]") {
  CHECK(cemkit::core::parse_semver("0.0.0") == SemVer{0, 0, 0});
  CHECK(cemkit::core::parse_semver("1.20.3") == SemVer{1, 20, 3});
  CHECK(cemkit::core::parse_semver("4294967295.0.0") == SemVer{4294967295U, 0, 0});

  for (const std::string_view bad :
       {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03", "v1.2.3", "1.2.3-rc1",
        "1.2.3+build", " 1.2.3", "1.2.3 ", "1..3", "-1.2.3", "4294967296.0.0", "1.2.x"}) {
    const auto parsed = cemkit::core::parse_semver(bad);
    CAPTURE(bad);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code() == cemkit::core::ErrorCode::invalid_input);
    CHECK(parsed.error().subject() == "version");
  }
}

TEST_CASE("semantic versions compare by major, minor, patch", "[PHY-004]") {
  CHECK(SemVer{1, 0, 0} > SemVer{0, 9, 9});
  CHECK(SemVer{1, 2, 0} > SemVer{1, 1, 9});
  CHECK(SemVer{1, 2, 4} > SemVer{1, 2, 3});
  CHECK(SemVer{1, 2, 3} == SemVer{1, 2, 3});
  CHECK(cemkit::core::to_string(SemVer{1, 20, 3}) == "1.20.3");
}

TEST_CASE("a model identity is its name and semantic version", "[PHY-004]") {
  const ModelId a{.name = "axial_ducted.l1", .version = SemVer{.major = 1, .minor = 2, .patch = 0}};
  const ModelId b{.name = "axial_ducted.l1", .version = SemVer{.major = 1, .minor = 3, .patch = 0}};
  CHECK(cemkit::core::to_string(a) == "axial_ducted.l1@1.2.0");
  CHECK(a != b);
  CHECK(a < b);
  CHECK(a ==
        ModelId{.name = "axial_ducted.l1", .version = SemVer{.major = 1, .minor = 2, .patch = 0}});
}

// Toolchain tests (MAINT-001): every preset must compile and run these, so a compiler or standard
// library regression (for example <expected> being invisible to Clang 18, see ADR-007) shows up at
// once.
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <format>
#include <nlohmann/json.hpp>
#include <string>

namespace {

std::expected<int, std::string> halve(int value) {
  if (value % 2 != 0) {
    return std::unexpected{std::format("{} is odd", value)};
  }
  return value / 2;
}

}  // namespace

TEST_CASE("std::expected compiles and behaves under this compiler and standard library",
          "[MAINT-001]") {
  const auto ok = halve(10);
  REQUIRE(ok.has_value());
  CHECK(*ok == 5);

  const auto bad = halve(7);
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == "7 is odd");
}

TEST_CASE("mp-units quantities compile and convert", "[MAINT-001][REPRO-002]") {
  using namespace mp_units;
  using namespace mp_units::si::unit_symbols;
  const quantity length = 120. * mm;
  CHECK(length.in(m).numerical_value_in(m) == 0.12);
}

TEST_CASE("nlohmann-json parses", "[MAINT-001][REPRO-002]") {
  const auto doc = nlohmann::json::parse(R"({"unit":"Pa"})");
  CHECK(doc.at("unit").get<std::string>() == "Pa");
}

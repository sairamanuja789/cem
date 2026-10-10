#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"

using cemkit::core::Error;
using cemkit::core::ErrorCode;

TEST_CASE("error codes keep their fixed integer and name", "[REL-002]") {
  // Append-only list: this table must only ever grow at the end. Renaming, renumbering or
  // reusing a code breaks stored data and this test.
  constexpr std::array<std::pair<std::uint16_t, std::string_view>, 12> k_fixed{{
      {1, "spec_rejected"},
      {2, "infeasible_requirement"},
      {3, "out_of_validity"},
      {4, "geometry_failed"},
      {5, "mesh_failed"},
      {6, "sim_untrusted"},
      {7, "resource_exceeded"},
      {8, "invalid_input"},
      {9, "unknown_unit"},
      {10, "unit_mismatch"},
      {11, "internal_error"},
      {12, "not_implemented"},
  }};
  REQUIRE(cemkit::core::k_error_codes.size() == k_fixed.size());
  for (std::size_t i = 0; i < k_fixed.size(); ++i) {
    const auto& info = cemkit::core::k_error_codes.at(i);
    CHECK(std::to_underlying(info.code) == k_fixed.at(i).first);
    CHECK(info.name == k_fixed.at(i).second);
    CHECK_FALSE(info.meaning.empty());
  }
}

TEST_CASE("error code names and integers round-trip", "[REL-002]") {
  for (const auto& info : cemkit::core::k_error_codes) {
    CHECK(cemkit::core::to_string(info.code) == info.name);
    CHECK(cemkit::core::parse_error_code(info.name) == info.code);
    CHECK(cemkit::core::error_code_from_int(std::to_underlying(info.code)) == info.code);
  }
  CHECK_FALSE(cemkit::core::parse_error_code("failure_cluster").has_value());
  CHECK_FALSE(cemkit::core::parse_error_code("").has_value());
  CHECK_FALSE(cemkit::core::error_code_from_int(0).has_value());
  CHECK_FALSE(cemkit::core::error_code_from_int(13).has_value());  // first unused code
  CHECK(cemkit::core::to_string(static_cast<ErrorCode>(0)).empty());
}

TEST_CASE("an error carries code, message, subject and sorted details", "[REL-002]") {
  const Error error{
      ErrorCode::out_of_validity,
      "tip speed above the model's range",
      "rotor.tip_speed",
      {{"value", "120"}, {"bounds", "[0, 100]"}, {"unit", "m/s"}, {"model", "x@1.0.0"}}};
  CHECK(error.code() == ErrorCode::out_of_validity);
  CHECK(error.message() == "tip speed above the model's range");
  CHECK(error.subject() == "rotor.tip_speed");
  REQUIRE(error.details().size() == 4);
  CHECK(error.details().begin()->first == "bounds");
  CHECK(cemkit::core::describe(error) ==
        "out_of_validity at rotor.tip_speed: tip speed above the model's range "
        "(bounds=[0, 100], model=x@1.0.0, unit=m/s, value=120)");
}

TEST_CASE("an error without subject or details describes itself plainly", "[REL-002]") {
  const Error error{ErrorCode::internal_error, "unreachable branch"};
  CHECK(error.subject().empty());
  CHECK(error.details().empty());
  CHECK(cemkit::core::describe(error) == "internal_error: unreachable branch");
  CHECK(error == Error{ErrorCode::internal_error, "unreachable branch"});
  CHECK_FALSE(error == Error{ErrorCode::invalid_input, "unreachable branch"});
}

TEST_CASE("numbers in messages are locale-independent and round-trip", "[REL-002]") {
  using cemkit::core::format_number;
  CHECK(format_number(101325.0) == "101325");
  CHECK(format_number(0.1) == "0.1");
  CHECK(format_number(-2.5) == "-2.5");
  CHECK(format_number(1e-7) == "1e-07");
  CHECK(format_number(std::numeric_limits<double>::quiet_NaN()) == "nan");
  CHECK(format_number(std::numeric_limits<double>::infinity()) == "inf");
  CHECK(format_number(-std::numeric_limits<double>::infinity()) == "-inf");
}

TEST_CASE("fail() builds the unexpected side of a Result", "[REL-002]") {
  const cemkit::core::Result<int> result =
      cemkit::core::fail(ErrorCode::invalid_input, "negative count", "count", {{"value", "-1"}});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code() == ErrorCode::invalid_input);
  CHECK(result.error().details().at("value") == "-1");

  const cemkit::core::Status ok{};
  CHECK(ok.has_value());
}

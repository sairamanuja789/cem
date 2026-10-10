// ADR-015 rule 5: the operations table through which the C ABI reaches product operations.
#include <catch2/catch_test_macros.hpp>

#include "cemkit/product/operations.hpp"

using cemkit::core::ErrorCode;
using cemkit::product::Json;
using cemkit::product::Operations;

TEST_CASE("operations are found by name and names are unique", "[FAM-001]") {
  Operations ops;
  REQUIRE(ops.add("echo", [](const Json& c) -> cemkit::core::Result<Json> { return c; }));
  const auto* echo = ops.find("echo");
  REQUIRE(echo != nullptr);
  CHECK((*echo)(Json{{"a", 1}}).value_or(Json{}) == Json{{"a", 1}});
  CHECK(ops.find("missing") == nullptr);
  const auto dup = ops.add("echo", [](const Json& c) -> cemkit::core::Result<Json> { return c; });
  REQUIRE(!dup.has_value());
  CHECK(dup.error().code() == ErrorCode::invalid_input);
  CHECK(!ops.add("", [](const Json& c) -> cemkit::core::Result<Json> { return c; }).has_value());
  CHECK(!ops.add("empty", {}).has_value());
}

TEST_CASE("models are listed once in registration order", "[PHY-004]") {
  Operations ops;
  REQUIRE(ops.add_model({.name = "m.one", .version = {.major = 1, .minor = 0, .patch = 0}}));
  REQUIRE(ops.add_model({.name = "m.two", .version = {.major = 1, .minor = 0, .patch = 0}}));
  CHECK(!ops.add_model({.name = "m.one", .version = {.major = 2, .minor = 0, .patch = 0}})
             .has_value());
  REQUIRE(ops.models().size() == 2);
  CHECK(ops.models()[1].name == "m.two");
}

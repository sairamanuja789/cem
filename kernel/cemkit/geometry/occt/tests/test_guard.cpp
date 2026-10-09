#include <BRepBuilderAPI_MakePolygon.hxx>
#include <Standard_ConstructionError.hxx>
#include <catch2/catch_test_macros.hpp>
#include <gp_Pnt.hxx>
#include <stdexcept>
#include <string>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/geometry/occt/guard.hpp"

using cemkit::core::ErrorCode;
using cemkit::core::Result;
using cemkit::geometry::occt::guarded;

TEST_CASE("guarded passes a result through unchanged", "[GEO-002]") {
  const auto value = guarded("op", [] { return Result<int>{42}; });
  REQUIRE(value.has_value());
  CHECK(*value == 42);

  const auto error = guarded("op", []() -> Result<int> {
    return cemkit::core::fail(ErrorCode::invalid_input, "bad", "x");
  });
  REQUIRE_FALSE(error.has_value());
  CHECK(error.error().code() == ErrorCode::invalid_input);
}

TEST_CASE("an OCCT exception becomes geometry_failed with its type and message", "[GEO-002]") {
  const auto result = guarded("loft", []() -> Result<int> {
    throw Standard_ConstructionError("sections are not compatible");
  });
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code() == ErrorCode::geometry_failed);
  CHECK(result.error().subject() == "loft");
  CHECK(result.error().message() == "OpenCascade raised an exception during loft");
  CHECK(result.error().details().at("exception") == "Standard_ConstructionError");
  CHECK(result.error().details().at("what") == "sections are not compatible");
}

TEST_CASE("an exception thrown by OCCT itself is caught", "[GEO-002]") {
  // A polygon with one vertex is not done; asking for its wire makes OCCT throw StdFail_NotDone.
  const auto result = guarded("polygon", []() -> cemkit::core::Status {
    BRepBuilderAPI_MakePolygon polygon;
    polygon.Add(gp_Pnt(0.0, 0.0, 0.0));
    static_cast<void>(polygon.Wire());
    return {};
  });
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code() == ErrorCode::geometry_failed);
  CHECK(result.error().details().at("exception") == "StdFail_NotDone");
}

TEST_CASE("standard and unknown exceptions are caught too", "[GEO-002]") {
  const auto standard =
      guarded("op", []() -> Result<int> { throw std::runtime_error("out of range"); });
  REQUIRE_FALSE(standard.has_value());
  CHECK(standard.error().code() == ErrorCode::geometry_failed);
  CHECK(standard.error().details().at("exception") == "std::exception");
  CHECK(standard.error().details().at("what") == "out of range");

  const auto unknown = guarded("op", []() -> Result<int> { throw 7; });
  REQUIRE_FALSE(unknown.has_value());
  CHECK(unknown.error().code() == ErrorCode::geometry_failed);
  CHECK(unknown.error().details().at("exception") == "unknown");
  CHECK_FALSE(unknown.error().details().contains("what"));
}

#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "cemkit/core/error.hpp"
#include "cemkit/physics/validity.hpp"

using cemkit::core::describe;
using cemkit::core::ModelId;
using cemkit::physics::require_at_most;
using cemkit::physics::require_non_negative;
using cemkit::physics::require_positive;

namespace {
const ModelId k_model{.name = "test.model", .version = {.major = 1, .minor = 2, .patch = 3}};
constexpr double k_inf = std::numeric_limits<double>::infinity();
constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
}  // namespace

TEST_CASE("require_positive accepts finite positive values only", "[PHY-003]") {
  CHECK(require_positive("x", 1e-300, k_model).has_value());
  for (const double bad : {0.0, -0.0, -1.0, k_inf, -k_inf, k_nan}) {
    const auto res = require_positive("x", bad, k_model);
    REQUIRE(!res.has_value());
    CHECK(res.error().code() == cemkit::core::ErrorCode::out_of_validity);
    CHECK(res.error().details().at("bounds") == "(0, inf)");
  }
  // Byte-identical to python/cemkit/reference/validity.py (ADR-003 D6).
  CHECK(describe(require_positive("flow", 0.0, k_model).error()) ==
        "out_of_validity at flow: input must be finite and strictly positive "
        "(bounds=(0, inf), model=test.model@1.2.3, value=0)");
  CHECK(require_positive("flow", -0.0, k_model).error().details().at("value") == "-0");
}

TEST_CASE("require_non_negative accepts zero and negative zero", "[PHY-003]") {
  CHECK(require_non_negative("p", 0.0, k_model).has_value());
  CHECK(require_non_negative("p", -0.0, k_model).has_value());
  CHECK(require_non_negative("p", 5.0, k_model).has_value());
  for (const double bad : {-1e-300, -1.0, k_inf, k_nan}) {
    const auto res = require_non_negative("p", bad, k_model);
    REQUIRE(!res.has_value());
    CHECK(res.error().details().at("bounds") == "[0, inf)");
    CHECK(res.error().message() == "input must be finite and non-negative");
  }
}

TEST_CASE("require_at_most writes the limit into the bounds", "[PHY-003]") {
  CHECK(require_at_most("p", 1418.55, 1418.55, k_model, "too high").has_value());
  const auto res = require_at_most("p", 1500.0, 1418.55, k_model, "too high");
  REQUIRE(!res.has_value());
  CHECK(describe(res.error()) ==
        "out_of_validity at p: too high (bounds=(0, 1418.55], model=test.model@1.2.3, value=1500)");
  CHECK(!require_at_most("p", k_nan, 1.0, k_model, "m").has_value());
}

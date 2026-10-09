#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <limits>
#include <type_traits>

#include "cemkit/core/error.hpp"
#include "cemkit/core/fidelity.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/version.hpp"

namespace isq = mp_units::isq;
namespace si = mp_units::si;
using cemkit::core::ErrorCode;
using cemkit::core::Fidelity;
using cemkit::core::Labelled;
using cemkit::core::ModelId;
using cemkit::core::Pressure;
using cemkit::core::SemVer;

namespace {
template <class T>
concept Addable = requires(T a, T b) { a + b; };
template <class T>
concept Scalable = requires(T a) { a * 2.0; };

const ModelId k_model{.name = "axial_ducted.l1",
                      .version = SemVer{.major = 1, .minor = 0, .patch = 0}};

Pressure pa(double value) { return value * isq::pressure[si::pascal]; }
}  // namespace

// The label is part of the type (COR-003): no default value, no unlabelled construction, no
// arithmetic, and no silent conversion to or from the bare quantity. kernel/testing/compile_fail/
// proves the same rules with real builds.
static_assert(!std::is_default_constructible_v<Labelled<Pressure>>);
static_assert(!std::is_constructible_v<Labelled<Pressure>, Pressure>);
static_assert(!std::is_constructible_v<Labelled<Pressure>, Pressure, ModelId>);
static_assert(!std::convertible_to<Labelled<Pressure>, Pressure>);
static_assert(!Addable<Labelled<Pressure>>);
static_assert(!Scalable<Labelled<Pressure>>);
static_assert(std::is_copy_constructible_v<Labelled<Pressure>>);

TEST_CASE("each factory sets its fidelity label and model identity", "[COR-003][PHY-004]") {
  const auto l0 = Labelled<Pressure>::l0_predicted(pa(100.0), k_model);
  const auto l1 = Labelled<Pressure>::l1_predicted(pa(100.0), k_model);
  const auto l2 = Labelled<Pressure>::l2_simulated(pa(100.0), k_model);
  REQUIRE(l0.has_value());
  REQUIRE(l1.has_value());
  REQUIRE(l2.has_value());
  CHECK(l0->fidelity() == Fidelity::l0_predicted);
  CHECK(l1->fidelity() == Fidelity::l1_predicted);
  CHECK(l2->fidelity() == Fidelity::l2_simulated);
  CHECK(l1->value() == pa(100.0));
  CHECK(l1->model() == k_model);
  CHECK_FALSE(l0->uncertainty().has_value());
  CHECK_FALSE(l1->uncertainty().has_value());
  CHECK_FALSE(l2->uncertainty().has_value());
}

TEST_CASE("L2 verified and L3 validated values carry their uncertainty", "[COR-003]") {
  const auto verified = Labelled<Pressure>::l2_verified(pa(100.0), pa(4.0), k_model);
  const auto validated = Labelled<Pressure>::l3_validated(
      pa(100.0), 0.006 * isq::pressure[si::kilo<si::pascal>], k_model);
  REQUIRE(verified.has_value());
  REQUIRE(validated.has_value());
  CHECK(verified->fidelity() == Fidelity::l2_verified);
  CHECK(validated->fidelity() == Fidelity::l3_validated);
  // optional<Q> == Q makes mp-units probe optional as a representation type and GCC 13 reports a
  // recursive constraint, so unwrap first. value_or with an impossible half-width keeps
  // clang-tidy's optional-access check satisfied; REQUIRE already proved presence.
  REQUIRE(verified->uncertainty().has_value());
  REQUIRE(validated->uncertainty().has_value());
  CHECK(verified->uncertainty().value_or(pa(-1.0)) == pa(4.0));
  CHECK(validated->uncertainty().value_or(pa(-1.0)).numerical_value_in(si::pascal) == 6.0);

  const auto zero = Labelled<Pressure>::l2_verified(pa(100.0), pa(0.0), k_model);
  CHECK(zero.has_value());
}

TEST_CASE("labelled values compare by value, label, model and uncertainty", "[COR-003]") {
  const auto a = Labelled<Pressure>::l1_predicted(pa(100.0), k_model);
  const auto b = Labelled<Pressure>::l1_predicted(pa(100.0), k_model);
  const auto c = Labelled<Pressure>::l0_predicted(pa(100.0), k_model);
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  REQUIRE(c.has_value());
  CHECK(*a == *b);
  CHECK_FALSE(*a == *c);

  const auto v1 = Labelled<Pressure>::l2_verified(pa(100.0), pa(4.0), k_model);
  const auto v2 = Labelled<Pressure>::l2_verified(pa(100.0), pa(5.0), k_model);
  const auto s = Labelled<Pressure>::l2_simulated(pa(100.0), k_model);
  REQUIRE(v1.has_value());
  REQUIRE(v2.has_value());
  REQUIRE(s.has_value());
  CHECK(*v1 == *v1);
  CHECK_FALSE(*v1 == *v2);
  CHECK_FALSE(*v1 == *s);
}

TEST_CASE("labelled values reject non-finite values and bad uncertainties", "[COR-003]") {
  constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double k_inf = std::numeric_limits<double>::infinity();

  const auto nan_value = Labelled<Pressure>::l1_predicted(pa(k_nan), k_model);
  REQUIRE_FALSE(nan_value.has_value());
  CHECK(nan_value.error().code() == ErrorCode::invalid_input);
  CHECK(nan_value.error().subject() == "value");
  CHECK(nan_value.error().details().at("value") == "nan");

  const auto inf_value = Labelled<Pressure>::l2_verified(pa(k_inf), pa(1.0), k_model);
  REQUIRE_FALSE(inf_value.has_value());
  CHECK(inf_value.error().subject() == "value");

  const auto negative = Labelled<Pressure>::l2_verified(pa(100.0), pa(-1.0), k_model);
  REQUIRE_FALSE(negative.has_value());
  CHECK(negative.error().code() == ErrorCode::invalid_input);
  CHECK(negative.error().subject() == "uncertainty");
  CHECK(negative.error().details().at("value") == "-1");

  const auto nan_uncertainty = Labelled<Pressure>::l3_validated(pa(100.0), pa(k_nan), k_model);
  REQUIRE_FALSE(nan_uncertainty.has_value());
  CHECK(nan_uncertainty.error().subject() == "uncertainty");
}

TEST_CASE("labelled values need the name of the producing model", "[PHY-004]") {
  const auto unnamed = Labelled<Pressure>::l1_predicted(
      pa(100.0), ModelId{.name = "", .version = SemVer{.major = 1, .minor = 0, .patch = 0}});
  REQUIRE_FALSE(unnamed.has_value());
  CHECK(unnamed.error().code() == ErrorCode::invalid_input);
  CHECK(unnamed.error().subject() == "model");
}

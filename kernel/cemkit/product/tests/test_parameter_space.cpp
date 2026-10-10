// FAM-003: parameter spaces publish named parameters with units, bounds and defaults.
// Every number here is a test value with no engineering meaning.
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <vector>

#include "cemkit/product/parameter_space.hpp"

using cemkit::core::ErrorCode;
using cemkit::product::Design;
using cemkit::product::Parameter;
using cemkit::product::ParameterDefault;
using cemkit::product::ParameterSpace;
using cemkit::product::ParameterValue;
using cemkit::spec::QuantityKind;

namespace {

constexpr double k_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double k_inf = std::numeric_limits<double>::infinity();
const std::string k_test_source = "test value; not engineering data";

std::vector<Parameter> two_parameters() {
  return {
      Parameter{.name = "a_length",
                .kind = QuantityKind::length,
                .lower = 1.0,
                .upper = 2.0,
                .default_value = ParameterDefault{.si_value = 1.5, .source = k_test_source}},
      Parameter{.name = "b_ratio",
                .kind = QuantityKind::ratio,
                .lower = 0.0,
                .upper = 1.0,
                .default_value = std::nullopt},
  };
}

ErrorCode create_error(std::vector<Parameter> parameters, std::string* subject = nullptr) {
  const auto space = ParameterSpace::create(std::move(parameters));
  REQUIRE(!space.has_value());
  if (subject != nullptr) {
    *subject = space.error().subject();
  }
  return space.error().code();
}

}  // namespace

TEST_CASE("a parameter space publishes names, units, bounds and defaults in order", "[FAM-003]") {
  const auto space = ParameterSpace::create(two_parameters());
  REQUIRE(space.has_value());
  REQUIRE(space->size() == 2);
  const auto& first = space->parameters().at(0);
  CHECK(first.name == "a_length");
  CHECK(first.unit() == "m");
  CHECK(first.lower == 1.0);
  CHECK(first.upper == 2.0);
  REQUIRE(first.default_value.has_value());
  CHECK(first.default_value.value_or(ParameterDefault{}).si_value == 1.5);
  CHECK(first.default_value.value_or(ParameterDefault{}).source == k_test_source);
  const auto& second = space->parameters().at(1);
  CHECK(second.name == "b_ratio");
  CHECK(second.unit() == "1");
  CHECK(!second.default_value.has_value());

  CHECK(space->find("b_ratio") == second);
  CHECK(!space->find("missing").has_value());
  CHECK(space->unsourced_defaults().empty());
}

TEST_CASE("an empty parameter space is valid", "[FAM-003]") {
  const auto space = ParameterSpace::create({});
  REQUIRE(space.has_value());
  CHECK(space->size() == 0);
  CHECK(space->check(Design{}).has_value());
}

TEST_CASE("parameter spaces reject malformed parameters, naming the parameter", "[FAM-003]") {
  std::string subject;
  auto params = two_parameters();

  SECTION("empty name") {
    params[1].name.clear();
    CHECK(create_error(params, &subject) == ErrorCode::invalid_input);
    CHECK(subject == "parameter");
  }
  SECTION("duplicate name") {
    params[1].name = "a_length";
    CHECK(create_error(params, &subject) == ErrorCode::invalid_input);
    CHECK(subject == "a_length");
  }
  SECTION("lower equal to upper") {
    params[1].upper = params[1].lower;
    CHECK(create_error(params, &subject) == ErrorCode::invalid_input);
    CHECK(subject == "b_ratio");
  }
  SECTION("lower above upper") {
    params[1].lower = 2.0;
    CHECK(create_error(params, &subject) == ErrorCode::invalid_input);
  }
  SECTION("non-finite bounds") {
    params[1].upper = k_inf;
    CHECK(create_error(params) == ErrorCode::invalid_input);
    params[1].upper = 1.0;
    params[1].lower = k_nan;
    CHECK(create_error(params) == ErrorCode::invalid_input);
  }
  SECTION("default outside the bounds") {
    params[0].default_value = ParameterDefault{.si_value = 2.5, .source = k_test_source};
    CHECK(create_error(params, &subject) == ErrorCode::invalid_input);
    CHECK(subject == "a_length");
  }
  SECTION("default not finite") {
    params[0].default_value = ParameterDefault{.si_value = k_nan, .source = k_test_source};
    CHECK(create_error(params) == ErrorCode::invalid_input);
  }
  SECTION("default without a source (CLAUDE.md rule 7)") {
    params[0].default_value = ParameterDefault{.si_value = 1.5, .source = ""};
    CHECK(create_error(params, &subject) == ErrorCode::invalid_input);
    CHECK(subject == "a_length");
  }
}

TEST_CASE("UNSOURCED defaults are accepted and reported", "[FAM-003]") {
  auto params = two_parameters();
  params[1].default_value =
      ParameterDefault{.si_value = 0.5, .source = "UNSOURCED: owner to supply"};
  const auto space = ParameterSpace::create(params);
  REQUIRE(space.has_value());
  CHECK(space->unsourced_defaults() == std::vector<std::string>{"b_ratio"});
}

TEST_CASE("designs are checked against the parameter space", "[FAM-003]") {
  const auto space = ParameterSpace::create(two_parameters());
  REQUIRE(space.has_value());
  Design design{.values = {ParameterValue{.name = "a_length", .si_value = 1.0},
                           ParameterValue{.name = "b_ratio", .si_value = 1.0}}};
  CHECK(space->check(design).has_value());  // bounds are inclusive

  SECTION("missing value") {
    design.values.pop_back();
    const auto status = space->check(design);
    REQUIRE(!status.has_value());
    CHECK(status.error().code() == ErrorCode::invalid_input);
    CHECK(status.error().subject() == "design");
  }
  SECTION("wrong order") {
    std::swap(design.values[0], design.values[1]);
    const auto status = space->check(design);
    REQUIRE(!status.has_value());
    CHECK(status.error().subject() == "a_length");
  }
  SECTION("non-finite value") {
    design.values[1].si_value = k_nan;
    const auto status = space->check(design);
    REQUIRE(!status.has_value());
    CHECK(status.error().code() == ErrorCode::invalid_input);
    CHECK(status.error().subject() == "b_ratio");
  }
  SECTION("value outside the bounds") {
    design.values[0].si_value = 2.0001;
    const auto status = space->check(design);
    REQUIRE(!status.has_value());
    CHECK(status.error().code() == ErrorCode::out_of_validity);
    CHECK(status.error().subject() == "a_length");
    CHECK(status.error().details().at("unit") == "m");
  }
}

TEST_CASE("the default design needs a default for every parameter", "[FAM-003]") {
  auto params = two_parameters();
  const auto partial = ParameterSpace::create(params);
  REQUIRE(partial.has_value());
  const auto missing = partial->default_design();
  REQUIRE(!missing.has_value());
  CHECK(missing.error().subject() == "b_ratio");

  params[1].default_value = ParameterDefault{.si_value = 0.25, .source = k_test_source};
  const auto full = ParameterSpace::create(params);
  REQUIRE(full.has_value());
  const auto design = full->default_design();
  REQUIRE(design.has_value());
  CHECK(design->values == std::vector<ParameterValue>{{.name = "a_length", .si_value = 1.5},
                                                      {.name = "b_ratio", .si_value = 0.25}});
  CHECK(full->check(*design).has_value());
}

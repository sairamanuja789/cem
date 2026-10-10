// FAM-001, FAM-003: the stub family implements the whole plugin interface. All values are test
// parameters of the stub, not engineering data.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "cemkit/product/registry.hpp"
#include "testing/stub_product/families/stub_family/stub_family.hpp"

using cemkit::core::ErrorCode;
using cemkit::core::Fidelity;
using namespace cemkit::product;
namespace stub = cemkit::testing::stub_product::stub_family;

namespace {

std::unique_ptr<const Family> make() {
  auto family = stub::make_family();
  REQUIRE(family.has_value());
  return std::move(*family);
}

}  // namespace

TEST_CASE("the stub family registers through register_family", "[FAM-001][FAM-002]") {
  Registry registry;
  REQUIRE(stub::register_family(registry).has_value());
  CHECK(registry.ids() == std::vector<std::string>{std::string(stub::k_family_id)});
  // Registering twice is a defect the registry reports.
  CHECK(!stub::register_family(registry).has_value());
}

TEST_CASE("the stub family publishes parameters with units, bounds and defaults", "[FAM-003]") {
  const auto family = make();
  CHECK(family->id() == "stub_product.stub_family");
  CHECK(cemkit::core::to_string(family->plugin()) == "stub_product.stub_family@0.1.0");
  const auto& params = family->parameter_space().parameters();
  REQUIRE(params.size() == 2);
  CHECK(params[0].name == "stub_ratio");
  CHECK(params[0].unit() == "1");
  CHECK(params[1].name == "stub_length");
  CHECK(params[1].unit() == "m");
  for (const auto& p : params) {
    CHECK(p.lower < p.upper);
    CHECK(p.default_value.has_value());
  }
  // Every stub value is a test parameter and is reported as unsourced.
  CHECK(family->parameter_space().unsourced_defaults() ==
        std::vector<std::string>{"stub_ratio", "stub_length"});
  const auto range = family->feasible_range();
  REQUIRE(range.limits.size() == 1);
  CHECK(range.limits[0].source.starts_with(k_unsourced));
}

TEST_CASE("the stub family runs every interface operation", "[FAM-001]") {
  const auto family = make();
  const cemkit::spec::Spec spec;

  const auto design = family->initial_design(spec);
  REQUIRE(design.has_value());
  CHECK(family->parameter_space().check(*design).has_value());

  const auto evaluation = family->evaluate_l1(spec, *design);
  REQUIRE(evaluation.has_value());
  REQUIRE(evaluation->metrics.size() == 1);
  CHECK(evaluation->metrics[0].fidelity == Fidelity::l1_predicted);
  CHECK(evaluation->metrics[0].model == family->plugin());
  CHECK(evaluation->metrics[0].si_value == design->values[0].si_value);

  const auto constraints = family->check_constraints(spec, *design, *evaluation);
  REQUIRE(constraints.has_value());
  REQUIRE(constraints->size() == 1);
  CHECK(constraints->front().satisfied());
  CHECK(constraints->front().source.starts_with(k_unsourced));

  const auto recipe = family->geometry_recipe(*design);
  REQUIRE(recipe.has_value());
  CHECK(recipe->values == design->values);

  const auto sim = family->simulation_case(spec, *design);
  REQUIRE(sim.has_value());
  CHECK(sim->settings == design->values);

  // Same inputs, same outputs.
  CHECK(family->evaluate_l1(spec, *design) == evaluation);
}

TEST_CASE("the stub family rejects designs outside its parameter space", "[FAM-001][FAM-003]") {
  const auto family = make();
  const cemkit::spec::Spec spec;
  auto design = family->initial_design(spec);
  REQUIRE(design.has_value());
  design->values[0].si_value = 2.0;

  const auto evaluation = family->evaluate_l1(spec, *design);
  REQUIRE(!evaluation.has_value());
  CHECK(evaluation.error().code() == ErrorCode::out_of_validity);
  CHECK(evaluation.error().subject() == "stub_ratio");
  CHECK(!family->check_constraints(spec, *design, Evaluation{}).has_value());
  CHECK(!family->geometry_recipe(*design).has_value());
  CHECK(!family->simulation_case(spec, *design).has_value());

  design->values[0].si_value = 0.5;
  const auto empty = family->check_constraints(spec, *design, Evaluation{});
  REQUIRE(!empty.has_value());
  CHECK(empty.error().subject() == "evaluation");
}

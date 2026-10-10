// FAM-001, FAM-002, SPEC-006: the family registry and the registry-backed essential-field resolver.
// The family here is a local test double; every value is a test value with no engineering meaning.
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cemkit/product/registry.hpp"

using cemkit::core::ErrorCode;
using cemkit::core::ModelId;
using cemkit::core::Result;
using namespace cemkit::product;

namespace {

class TestFamily final : public Family {
 public:
  TestFamily(std::string id, std::vector<std::string> essentials, FeasibleRange range = {})
      : id_{std::move(id)}, essentials_{std::move(essentials)}, range_{std::move(range)} {}

  std::string_view id() const noexcept override { return id_; }
  ModelId plugin() const override {
    return {.name = id_, .version = {.major = 0, .minor = 1, .patch = 0}};
  }
  const ParameterSpace& parameter_space() const noexcept override { return space_; }
  std::vector<std::string> essential_fields() const override { return essentials_; }
  FeasibleRange feasible_range() const override { return range_; }
  Result<Design> initial_design(const cemkit::spec::Spec& /*spec*/) const override {
    return Design{};
  }
  Result<Evaluation> evaluate_l1(const cemkit::spec::Spec& /*spec*/,
                                 const Design& /*design*/) const override {
    return Evaluation{};
  }
  Result<std::vector<ConstraintResult>> check_constraints(
      const cemkit::spec::Spec& /*spec*/, const Design& /*design*/,
      const Evaluation& /*evaluation*/) const override {
    return std::vector<ConstraintResult>{};
  }
  Result<GeometryRecipe> geometry_recipe(const Design& /*design*/) const override {
    return GeometryRecipe{};
  }
  Result<SimulationCase> simulation_case(const cemkit::spec::Spec& /*spec*/,
                                         const Design& /*design*/) const override {
    return SimulationCase{};
  }

 private:
  std::string id_;
  std::vector<std::string> essentials_;
  FeasibleRange range_;
  ParameterSpace space_{ParameterSpace::create({}).value()};
};

std::unique_ptr<const Family> family(std::string id, std::vector<std::string> essentials = {
                                                         "product.nominal_size"}) {
  return std::make_unique<TestFamily>(std::move(id), std::move(essentials));
}

ErrorCode add_error(Registry& registry, std::unique_ptr<const Family> f) {
  const auto status = registry.add(std::move(f));
  REQUIRE(!status.has_value());
  CHECK(status.error().subject() == "family");
  return status.error().code();
}

}  // namespace

TEST_CASE("the registry keeps families in registration order", "[FAM-001][FAM-002]") {
  Registry registry;
  CHECK(registry.size() == 0);
  REQUIRE(registry.add(family("zeta.one")).has_value());
  REQUIRE(registry.add(family("alpha.two_2")).has_value());
  CHECK(registry.ids() == std::vector<std::string>{"zeta.one", "alpha.two_2"});
  CHECK(registry.size() == 2);
  CHECK(registry.contains("alpha.two_2"));
  CHECK(!registry.contains("alpha.two"));

  const auto found = registry.get("zeta.one");
  REQUIRE(found.has_value());
  CHECK(found->get().id() == "zeta.one");
}

TEST_CASE("the registry rejects malformed or duplicate families", "[FAM-001][FAM-002]") {
  Registry registry;
  REQUIRE(registry.add(family("p.f")).has_value());
  CHECK(add_error(registry, nullptr) == ErrorCode::invalid_input);
  CHECK(add_error(registry, family("p.f")) == ErrorCode::invalid_input);
  for (const auto* bad : {"", "nodot", ".f", "p.", "p.f.g", "P.f", "p.f-g", "p f.g"}) {
    INFO(bad);
    CHECK(add_error(registry, family(bad)) == ErrorCode::invalid_input);
  }
  CHECK(add_error(registry, family("p.g", {"product.nominal_size", "product.nominal_size"})) ==
        ErrorCode::invalid_input);
  CHECK(add_error(registry, family("p.h", {""})) == ErrorCode::invalid_input);
  CHECK(registry.ids() == std::vector<std::string>{"p.f"});
}

TEST_CASE("the registry rejects malformed feasible ranges", "[FAM-001]") {
  Registry registry;
  const auto with_limit = [](std::string id, RangeLimit limit) {
    return std::make_unique<TestFamily>(std::move(id),
                                        std::vector<std::string>{"product.nominal_size"},
                                        FeasibleRange{.limits = {std::move(limit)}});
  };
  const RangeLimit good{.quantity = "q",
                        .kind = cemkit::spec::QuantityKind::ratio,
                        .lower = 0.0,
                        .upper = std::nullopt,
                        .source = "test value"};
  REQUIRE(registry.add(with_limit("p.ok", good)).has_value());

  auto no_side = good;
  no_side.lower = std::nullopt;
  CHECK(add_error(registry, with_limit("p.a", no_side)) == ErrorCode::invalid_input);
  auto inverted = good;
  inverted.upper = -1.0;
  CHECK(add_error(registry, with_limit("p.b", inverted)) == ErrorCode::invalid_input);
  auto not_finite = good;
  not_finite.lower = std::numeric_limits<double>::infinity();
  CHECK(add_error(registry, with_limit("p.c", not_finite)) == ErrorCode::invalid_input);
  auto unsourced = good;
  unsourced.source.clear();
  CHECK(add_error(registry, with_limit("p.d", unsourced)) == ErrorCode::invalid_input);
  CHECK(registry.ids() == std::vector<std::string>{"p.ok"});
}

TEST_CASE("an unknown family is rejected naming the family field", "[FAM-001][SPEC-006]") {
  Registry registry;
  REQUIRE(registry.add(family("p.f")).has_value());
  const auto got = registry.get("p.g");
  REQUIRE(!got.has_value());
  CHECK(got.error().code() == ErrorCode::spec_rejected);
  CHECK(got.error().subject() == "family");
  CHECK(got.error().details().at("family") == "p.g");

  const auto essentials = registry.essential_fields("p.g");
  REQUIRE(!essentials.has_value());
  CHECK(essentials.error().code() == ErrorCode::spec_rejected);
}

TEST_CASE("the essential-field resolver asks the registered plugin", "[SPEC-006]") {
  auto registry = std::make_shared<Registry>();
  REQUIRE(registry->add(family("p.f", {"product.rotational_speed", "product.nominal_size"}))
              .has_value());
  const auto resolver = essential_field_resolver(registry);
  const auto fields = resolver("p.f");
  REQUIRE(fields.has_value());
  CHECK(*fields == std::vector<std::string>{"product.rotational_speed", "product.nominal_size"});

  const auto unknown = resolver("p.g");
  REQUIRE(!unknown.has_value());
  CHECK(unknown.error().subject() == "family");

  const auto no_registry = essential_field_resolver(nullptr)("p.f");
  REQUIRE(!no_registry.has_value());
  CHECK(no_registry.error().code() == ErrorCode::internal_error);
}

TEST_CASE("constraint results report whether they hold", "[FAM-001]") {
  ConstraintResult c{.name = "c",
                     .kind = cemkit::spec::QuantityKind::ratio,
                     .sense = ConstraintResult::Sense::at_most,
                     .value = 1.0,
                     .limit = 1.0,
                     .fidelity = cemkit::core::Fidelity::l1_predicted,
                     .source = "test value"};
  CHECK(c.satisfied());
  c.value = 1.5;
  CHECK(!c.satisfied());
  c.sense = ConstraintResult::Sense::at_least;
  CHECK(c.satisfied());
  c.value = 0.5;
  CHECK(!c.satisfied());
}

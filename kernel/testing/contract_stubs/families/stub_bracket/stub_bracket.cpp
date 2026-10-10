#include "testing/contract_stubs/families/stub_bracket/stub_bracket.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cemkit::testing::contract_stubs::stub_bracket {

namespace {

using spec::FieldDecl;
using spec::QuantityKind;

constexpr core::SemVer k_version{.major = 0, .minor = 1, .patch = 0};

// D4: the fixture has no hook for this operation; the capability detail names it.
core::Error not_implemented(std::string_view operation) {
  return core::Error{core::ErrorCode::not_implemented,
                     std::string{operation} + " is not implemented for the test fixture " +
                         std::string{k_family_id},
                     std::string{k_family_id},
                     {{"capability", std::string{k_family_id} + "." + std::string{operation}}}};
}

class Stub final : public product::Family {
 public:
  explicit Stub(product::ParameterSpace space) : space_{std::move(space)} {}

  [[nodiscard]] std::string_view id() const noexcept override { return k_family_id; }
  [[nodiscard]] core::ModelId plugin() const override {
    return {.name = std::string(k_family_id), .version = k_version};
  }
  [[nodiscard]] const product::ParameterSpace& parameter_space() const noexcept override {
    return space_;
  }
  [[nodiscard]] std::vector<std::string> essential_fields() const override {
    return {"product.load.surface_pressure", "product.allowable_deflection"};
  }
  [[nodiscard]] product::FeasibleRange feasible_range() const override { return {}; }
  [[nodiscard]] spec::FieldCatalogue field_catalogue() const override {
    return {.title = "contract_stubs.stub_bracket (test fixture)",
            .description = "Test parameters only; not engineering data.",
            .fields = {FieldDecl{.path = "product.load.surface_pressure",
                                 .kind = QuantityKind::pressure,
                                 .required = true},
                       FieldDecl{.path = "product.allowable_deflection",
                                 .kind = QuantityKind::length,
                                 .required = true},
                       FieldDecl{.path = "product.thickness_max", .kind = QuantityKind::length},
                       FieldDecl{.path = "product.support", .allowed_values = {"fixed", "pinned"}}},
            .containers = {}};
  }
  [[nodiscard]] core::Status spec_checks(const spec::Spec& /*spec*/) const override { return {}; }
  [[nodiscard]] core::Result<product::FeasibilityOutcome> feasibility(
      const spec::Spec& /*spec*/) const override {
    return std::unexpected(not_implemented("feasibility"));
  }
  [[nodiscard]] core::Result<product::Design> initial_design(
      const spec::Spec& /*spec*/) const override {
    return std::unexpected(not_implemented("initial_design"));
  }
  [[nodiscard]] core::Result<product::Evaluation> evaluate_l1(
      const spec::Spec& /*spec*/, const product::Design& /*design*/) const override {
    return std::unexpected(not_implemented("evaluate_l1"));
  }
  [[nodiscard]] core::Result<std::vector<product::ConstraintResult>> check_constraints(
      const spec::Spec& /*spec*/, const product::Design& /*design*/,
      const product::Evaluation& /*evaluation*/) const override {
    return std::unexpected(not_implemented("check_constraints"));
  }
  [[nodiscard]] core::Result<product::GeometryRecipe> geometry_recipe(
      const product::Design& /*design*/) const override {
    return std::unexpected(not_implemented("geometry_recipe"));
  }
  [[nodiscard]] core::Result<product::SimulationCase> simulation_case(
      const spec::Spec& /*spec*/, const product::Design& /*design*/) const override {
    return std::unexpected(not_implemented("simulation_case"));
  }

 private:
  product::ParameterSpace space_;
};

}  // namespace

core::Result<std::unique_ptr<const product::Family>> make_family() {
  return product::ParameterSpace::create({}).transform([](product::ParameterSpace space) {
    return std::unique_ptr<const product::Family>{std::make_unique<Stub>(std::move(space))};
  });
}

core::Status register_family(product::Registry& registry) {
  auto family = make_family();
  if (!family) {
    return std::unexpected(std::move(family.error()));
  }
  return registry.add(std::move(*family));
}

}  // namespace cemkit::testing::contract_stubs::stub_bracket

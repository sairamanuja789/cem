#include "testing/stub_product/families/stub_family/stub_family.hpp"

#include <string>
#include <utility>
#include <vector>

namespace cemkit::testing::stub_product::stub_family {

namespace {

using spec::QuantityKind;

// Every value below is a test parameter, not engineering data (CLAUDE.md rule 7: marked UNSOURCED
// so that anything reading it reports it as such).
constexpr std::string_view k_test_source =
    "UNSOURCED: stub plugin test parameter, not engineering data";
constexpr core::SemVer k_version{.major = 0, .minor = 1, .patch = 0};

core::Result<product::ParameterSpace> make_space() {
  return product::ParameterSpace::create({
      product::Parameter{
          .name = "stub_ratio",
          .kind = QuantityKind::ratio,
          .lower = 0.0,
          .upper = 1.0,
          .default_value =
              product::ParameterDefault{.si_value = 0.5, .source = std::string(k_test_source)}},
      product::Parameter{
          .name = "stub_length",
          .kind = QuantityKind::length,
          .lower = 0.0,
          .upper = 1.0,
          .default_value =
              product::ParameterDefault{.si_value = 0.25, .source = std::string(k_test_source)}},
  });
}

class StubFamily final : public product::Family {
 public:
  explicit StubFamily(product::ParameterSpace space) : space_{std::move(space)} {}

  [[nodiscard]] std::string_view id() const noexcept override { return k_family_id; }
  [[nodiscard]] core::ModelId plugin() const override {
    return {.name = std::string(k_family_id), .version = k_version};
  }
  [[nodiscard]] const product::ParameterSpace& parameter_space() const noexcept override {
    return space_;
  }
  [[nodiscard]] std::vector<std::string> essential_fields() const override {
    // Test parameters: deliberately different from any real family's list, so a test can tell
    // that the compiler asked this plugin.
    return {"product.nominal_size", "product.rotational_speed"};
  }
  [[nodiscard]] product::FeasibleRange feasible_range() const override {
    return {.limits = {product::RangeLimit{.quantity = "stub_ratio",
                                           .kind = QuantityKind::ratio,
                                           .lower = 0.0,
                                           .upper = 1.0,
                                           .source = std::string(k_test_source)}}};
  }
  [[nodiscard]] core::Result<product::Design> initial_design(
      const spec::Spec& /*spec*/) const override {
    return space_.default_design();
  }
  [[nodiscard]] core::Result<product::Evaluation> evaluate_l1(
      const spec::Spec& /*spec*/, const product::Design& design) const override {
    if (auto status = space_.check(design); !status) {
      return std::unexpected(std::move(status.error()));
    }
    // A test "model": echoes stub_ratio as an L1 metric.
    return product::Evaluation{
        .metrics = {product::Metric{.name = "stub_ratio_echo",
                                    .kind = QuantityKind::ratio,
                                    .si_value = design.values.front().si_value,
                                    .fidelity = core::Fidelity::l1_predicted,
                                    .model = plugin()}}};
  }
  [[nodiscard]] core::Result<std::vector<product::ConstraintResult>> check_constraints(
      const spec::Spec& /*spec*/, const product::Design& design,
      const product::Evaluation& evaluation) const override {
    if (auto status = space_.check(design); !status) {
      return std::unexpected(std::move(status.error()));
    }
    if (evaluation.metrics.empty()) {
      return core::fail(core::ErrorCode::invalid_input, "evaluation has no metrics", "evaluation");
    }
    return std::vector<product::ConstraintResult>{
        product::ConstraintResult{.name = "stub_ratio_echo_limit",
                                  .kind = QuantityKind::ratio,
                                  .sense = product::ConstraintResult::Sense::at_most,
                                  .value = evaluation.metrics.front().si_value,
                                  .limit = 0.75,
                                  .fidelity = evaluation.metrics.front().fidelity,
                                  .source = std::string(k_test_source)}};
  }
  [[nodiscard]] core::Result<product::GeometryRecipe> geometry_recipe(
      const product::Design& design) const override {
    if (auto status = space_.check(design); !status) {
      return std::unexpected(std::move(status.error()));
    }
    return product::GeometryRecipe{
        .recipe = {.name = "stub_product.stub_recipe", .version = k_version},
        .values = design.values};
  }
  [[nodiscard]] core::Result<product::SimulationCase> simulation_case(
      const spec::Spec& /*spec*/, const product::Design& design) const override {
    if (auto status = space_.check(design); !status) {
      return std::unexpected(std::move(status.error()));
    }
    return product::SimulationCase{
        .case_template = {.name = "stub_product.stub_case", .version = k_version},
        .settings = design.values};
  }

 private:
  product::ParameterSpace space_;
};

}  // namespace

core::Result<std::unique_ptr<const product::Family>> make_family() {
  return make_space().transform([](product::ParameterSpace space) {
    return std::unique_ptr<const product::Family>{std::make_unique<StubFamily>(std::move(space))};
  });
}

core::Status register_family(product::Registry& registry) {
  auto family = make_family();
  if (!family) {
    return std::unexpected(std::move(family.error()));
  }
  return registry.add(std::move(*family));
}

}  // namespace cemkit::testing::stub_product::stub_family

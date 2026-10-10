#include "products/fans/families/axial_ducted/axial_ducted.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <string>
#include <utility>
#include <vector>

namespace cemkit::fans::axial_ducted {

namespace {

constexpr core::SemVer k_version{.major = 0, .minor = 1, .patch = 0};

// D4: the spec declares fans.axial_ducted; this release lacks the named hook of that family
// (axial L1 milestone, HI-014). The capability detail names it.
core::Error not_implemented(std::string_view operation) {
  return core::Error{core::ErrorCode::not_implemented,
                     std::string{operation} +
                         " is not implemented for fans.axial_ducted yet "
                         "(axial L1 milestone)",
                     std::string{k_family_id},
                     {{"capability", std::string{k_family_id} + "." + std::string{operation}}}};
}

class AxialDucted final : public product::Family {
 public:
  explicit AxialDucted(product::ParameterSpace space) : space_{std::move(space)} {}

  [[nodiscard]] std::string_view id() const noexcept override { return k_family_id; }
  [[nodiscard]] core::ModelId plugin() const override {
    return {.name = std::string(k_family_id), .version = k_version};
  }
  [[nodiscard]] const product::ParameterSpace& parameter_space() const noexcept override {
    return space_;  // empty: no design parameters until the axial L1 milestone
  }
  [[nodiscard]] std::vector<std::string> essential_fields() const override {
    // ADR-003 D8 (proposed), requirements AX-003 and AX-004.
    return {"product.duty.flow", "product.duty.pressure"};
  }
  [[nodiscard]] product::FeasibleRange feasible_range() const override {
    // No cited range exists (HI-003); the registry accepts only bounded, sourced limits, so none
    // is declared here. specific_speed_range() gives the gate its UNSOURCED range.
    return {};
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
    return std::unique_ptr<const product::Family>{std::make_unique<AxialDucted>(std::move(space))};
  });
}

core::Status register_family(product::Registry& registry) {
  auto family = make_family();
  if (!family) {
    return std::unexpected(std::move(family.error()));
  }
  return registry.add(std::move(*family));
}

FamilyRange specific_speed_range() {
  return FamilyRange{.family = std::string{k_family_id},
                     .specific_speed_min = std::nullopt,
                     .specific_speed_max = std::nullopt,
                     .source = std::string{k_unsourced}};
}

core::Result<std::optional<DerivedTipDiameter>> rotor_tip_diameter(const spec::Spec& spec) {
  const auto reference = spec.field("product.size_reference");
  const std::string kind = reference ? reference->text_value.value_or("") : "";
  const bool from_duct = kind == "duct_inner_diameter";
  if (!from_duct && kind != "rotor_tip_diameter") {
    return std::nullopt;  // frame or unknown: not decided by the owner (AX-001 open item)
  }
  const auto size = spec.field("product.nominal_size");
  if (!size || !size->si_value) {
    return std::nullopt;
  }
  if (!(*size->si_value > 0.0)) {
    return core::fail(core::ErrorCode::spec_rejected, "the nominal size must be positive",
                      "product.nominal_size");
  }
  const core::Length nominal = *size->si_value * mp_units::isq::length[mp_units::si::metre];
  if (!from_duct) {
    // The user stated the rotor tip diameter itself: nothing is derived or assumed.
    return DerivedTipDiameter{.value = nominal,
                              .provenance = size->provenance,
                              .provisional = size->provisional || reference->provisional,
                              .rule = k_tip_stated_rule,
                              .from = {"product.nominal_size", "product.size_reference"}};
  }
  const auto clearance = spec.field("product.tip_clearance_min");
  if (!clearance || !clearance->si_value) {
    return std::nullopt;  // never assume a clearance (D6)
  }
  const core::Length c_tip = *clearance->si_value * mp_units::isq::length[mp_units::si::metre];
  const core::Length d_tip = nominal - 2.0 * c_tip;
  if (!(c_tip.numerical_value_in(mp_units::si::metre) >= 0.0) ||
      !(d_tip.numerical_value_in(mp_units::si::metre) > 0.0)) {
    return core::fail(core::ErrorCode::spec_rejected,
                      "the tip clearance must be non-negative and less than half the duct inner "
                      "diameter (D_tip = D_duct - 2 c_tip > 0)",
                      "product.tip_clearance_min");
  }
  return DerivedTipDiameter{
      .value = d_tip,
      .provenance = core::Provenance::default_value,
      .provisional = true,
      .rule = k_tip_from_duct_rule,
      .from = {"product.nominal_size", "product.size_reference", "product.tip_clearance_min"}};
}

core::Result<FeasibilityReport> check_feasibility(const Duty& duty, const physics::Air& air,
                                                  std::optional<core::AngularVelocity> omega,
                                                  std::optional<core::Length> d_tip) {
  return fans::check_feasibility(duty, air, specific_speed_range(), omega, d_tip);
}

}  // namespace cemkit::fans::axial_ducted

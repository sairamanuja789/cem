#include "products/fans/families/axial_ducted/axial_ducted.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "products/fans/common/feasibility_json.hpp"
#include "products/fans/common/l0.hpp"

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

namespace si = mp_units::si;
namespace isq = mp_units::isq;
using product::Json;
using spec::FieldDecl;
using spec::QuantityKind;

FieldDecl quantity(std::string path, QuantityKind kind, bool required = false,
                   std::string description = {}) {
  return FieldDecl{.path = std::move(path),
                   .kind = kind,
                   .required = required,
                   .description = std::move(description)};
}

// ADR-015 rule 3: the fields of a fans.axial_ducted spec (requirements section 7), formerly the
// compiler's built-in table and the hand-written schemas/products/fans/v1/spec.schema.json.
spec::FieldCatalogue catalogue() {
  // SPEC-004: a pressure states its ISO 5801 kind (docs/models/fans/pressure-kinds.md).
  const spec::QualifierRule pressure_kind{
      .member = "kind",
      .report_as = "pressure_kind",
      .accepted = {"fan_total", "fan_static"},
      .rejected = {{.values = {"static_to_static", "static-to-static"},
                    .message = "static-to-static pressure is rejected (ISO 5801 requires "
                               "fan_total or fan_static)"},
                   {.values = {"total_to_static", "total-to-static"},
                    .message = "'total-to-static' is an efficiency type, not a pressure type "
                               "(ISO 5801)"}},
      .missing_message = "pressure must state its kind (fan_total or fan_static)",
      .unknown_prefix = "unknown pressure kind: ",
      .unknown_suffix = " (must be fan_total or fan_static)",
      .description =
          "A fan pressure (ISO 5801; docs/models/fans/pressure-kinds.md). fan_total = p_t2 - p_t1; "
          "fan_static = p_s2 - p_t1 (total-to-static). Any other kind, including "
          "static-to-static, is rejected; a known value must state its kind (SPEC-004)."};
  const std::string duty_tolerance = "duty point must carry a tolerance";  // SPEC-011

  FieldDecl flow = quantity("product.duty.flow", QuantityKind::volume_flow_rate, true);
  flow.tolerance_required = duty_tolerance;
  flow.question = "Essential duty point flow rate (AX-003).";
  flow.comment = "SPEC-011: a known duty flow carries a tolerance.";

  FieldDecl pressure = quantity("product.duty.pressure", QuantityKind::pressure, true);
  pressure.tolerance_required = duty_tolerance;
  pressure.qualifier = pressure_kind;
  pressure.question = "Essential duty point pressure rise and kind (AX-004).";
  pressure.comment = "SPEC-011: a known duty pressure carries a tolerance.";

  FieldDecl nominal_size = quantity("product.nominal_size", QuantityKind::length, true);
  nominal_size.question = "Essential outer geometry envelope / nominal size (AX-001).";

  FieldDecl size_reference{.path = "product.size_reference",
                           .allowed_values = {"frame", "duct_inner_diameter", "rotor_tip_diameter"},
                           .required = true,
                           .description = "What the nominal size measures (AX-001)."};

  FieldDecl speed = quantity("product.rotational_speed", QuantityKind::angular_velocity, true);
  speed.question = "Essential design operating rotational speed (AX-002).";

  FieldDecl scope{.path = "product.scope",
                  .text_list = true,
                  .allowed_values = {"rotor", "hub", "blades", "duct"},
                  .description = "Parts to design (AX-010)."};

  return spec::FieldCatalogue{
      .title = "fan spec extension (fans.axial_ducted)",
      .description =
          "The 'product' block of a fans.axial_ducted spec (requirements section 7). Field objects "
          "come from the platform spec schema. Pressures state their ISO 5801 kind (SPEC-004); a "
          "known duty point carries tolerances (SPEC-011).",
      .fields = {flow, pressure, nominal_size, size_reference, speed,
                 quantity("product.wall_thickness_min", QuantityKind::length),
                 quantity("product.tip_clearance_min", QuantityKind::length),
                 quantity("product.pressure_margin", QuantityKind::ratio, false,
                          "Required margin on the duty pressure at the duty flow (AX-012)."),
                 quantity("product.safety_factor.factor", QuantityKind::ratio, true),
                 quantity("product.safety_factor.at_speed_ratio", QuantityKind::ratio, true), scope,
                 quantity(
                     "product.power_limit", QuantityKind::power, false,
                     "A stated power limit; the air power Q x p of the duty point must not exceed "
                     "it (SPEC-010)."),
                 quantity("product.motor.bore_diameter", QuantityKind::length),
                 quantity("product.motor.speed_min", QuantityKind::angular_velocity),
                 quantity("product.motor.speed_max", QuantityKind::angular_velocity),
                 quantity("product.motor.power_limit", QuantityKind::power, false,
                          "The motor's power limit (SPEC-010).")},
      .containers = {
          {.path = "product.duty",
           .required = true,
           .description =
               "Duty point (AX-003, AX-004). Essential: must be known before a campaign."},
          {.path = "product.safety_factor",
           .description =
               "Structural safety factor at a stated fraction of design speed (AX-014)."},
          {.path = "product.motor",
           .description = "Motor interface; the motor itself is not designed (AX-010)."}}};
}

// SPEC-010 (moved from the spec compiler, ADR-015 rule 4): the air power Q x p of a known duty
// point must not exceed any stated power limit.
core::Status check_air_power(const spec::Spec& spec) {
  const auto flow_f = spec.field("product.duty.flow");
  const auto pressure_f = spec.field("product.duty.pressure");
  if (!flow_f || !flow_f->si_value || !pressure_f || !pressure_f->si_value) {
    return {};
  }
  const double q = *flow_f->si_value;
  const double p = *pressure_f->si_value;
  const bool positive = q > 0.0 && p > 0.0;
  if (!positive) {
    return {};
  }
  const double air_power = q * p;
  // Every limit present is checked: product, motor and the platform's top-level one.
  const std::array power_limit_paths = {std::string_view{"product.power_limit"},
                                        std::string_view{"product.motor.power_limit"},
                                        std::string_view{"power_limit"}};
  for (const auto path : power_limit_paths) {
    const auto limit_f = spec.field(path);
    if (limit_f && limit_f->si_value.has_value()) {
      const double power_limit = *limit_f->si_value;
      if (air_power > power_limit) {
        return core::fail(core::ErrorCode::infeasible_requirement,
                          "required air power exceeds stated power limit", limit_f->path,
                          {{"air_power", core::format_number(air_power)},
                           {"power_limit", core::format_number(power_limit)}});
      }
    }
  }
  return {};
}

std::optional<double> si_of(const spec::Spec& s, std::string_view path) {
  const auto f = s.field(path);
  return f ? f->si_value : std::nullopt;
}

Json input_json(double value, const char* unit, const char* from) {
  return Json{{"value", value}, {"unit", unit}, {"from", from}};
}

// SEL-004 for a compiled spec (moved from the C ABI, ADR-015 rule 4; docs/capi.md): the duty point
// and air from the spec. Fan static pressure is converted to fan total with the duct diameter,
// which is product.nominal_size when product.size_reference is duct_inner_diameter (AX-001). The
// rotor tip diameter comes from rotor_tip_diameter (owner decision D6, provisional); without one
// the tip-speed check is not computable.
core::Result<product::FeasibilityOutcome> spec_feasibility(const spec::Spec& s) {
  const auto flow = si_of(s, "product.duty.flow");
  if (!flow) {
    return core::fail(core::ErrorCode::spec_rejected,
                      "the duty point flow is unknown: answer the spec's questions first",
                      "product.duty.flow");
  }
  const auto pressure_field = s.field("product.duty.pressure");
  const auto pressure = si_of(s, "product.duty.pressure");
  if (!pressure_field || !pressure) {
    return core::fail(core::ErrorCode::spec_rejected,
                      "the duty point pressure is unknown: answer the spec's questions first",
                      "product.duty.pressure");
  }
  physics::Air air = physics::default_air();
  if (auto rho = si_of(s, "air.density")) {
    air.density = *rho * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)];
  }
  if (auto t = si_of(s, "air.temperature")) {
    air.temperature = mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(*t);
  }
  const auto flow_q =
      *flow * core::units::volume_flow_rate[mp_units::cubic(si::metre) / si::second];

  Json inputs = Json::object();
  inputs["flow"] = input_json(*flow, "m3/s", "product.duty.flow");
  double total = *pressure;
  const bool fan_static =
      pressure_field->qualifier.has_value() && pressure_field->qualifier->value == "fan_static";
  if (fan_static) {
    const auto reference = s.field("product.size_reference");
    const auto d_duct = si_of(s, "product.nominal_size");
    const bool duct = reference && reference->text_value.value_or("") == "duct_inner_diameter";
    if (!duct || !d_duct) {
      return core::fail(core::ErrorCode::spec_rejected,
                        "fan static pressure needs the duct diameter: product.size_reference must "
                        "be duct_inner_diameter and product.nominal_size known",
                        "product.size_reference");
    }
    const auto converted = fan_total_from_static(*pressure * fan_static_pressure[si::pascal],
                                                 flow_q, *d_duct * isq::length[si::metre], air);
    if (!converted) {
      return std::unexpected(converted.error());
    }
    total = converted->value.value().numerical_value_in(si::pascal);
    Json derived = product::labelled_json(converted->value, si::pascal);
    derived["unit"] = "Pa";
    derived["provenance"] = std::string{core::to_string(converted->provenance)};
    derived["rule"] = std::string{converted->rule};
    derived["from"] =
        Json::array({"product.duty.pressure", "product.duty.flow", "product.nominal_size"});
    inputs["fan_total_pressure"] = std::move(derived);
  } else {
    inputs["fan_total_pressure"] = input_json(*pressure, "Pa", "product.duty.pressure");
  }
  std::optional<core::AngularVelocity> omega;
  if (auto w = si_of(s, "product.rotational_speed")) {
    omega = *w * isq::angular_velocity[si::radian / si::second];
    inputs["omega"] = input_json(*w, "rad/s", "product.rotational_speed");
  }

  const auto tip = rotor_tip_diameter(s);
  if (!tip) {
    return std::unexpected(tip.error());
  }
  std::optional<core::Length> d_tip;
  if (*tip) {
    const auto& t = **tip;
    d_tip = t.value;
    inputs["d_tip"] = Json{{"value", t.value.numerical_value_in(si::metre)},
                           {"unit", "m"},
                           {"provenance", std::string{core::to_string(t.provenance)}},
                           {"provisional", t.provisional},
                           {"rule", t.rule},
                           {"from", t.from}};
  }

  const Duty duty{.flow = flow_q, .pressure = total * fan_total_pressure[si::pascal]};
  const auto report = check_feasibility(duty, air, omega, d_tip);
  if (!report) {
    return std::unexpected(report.error());
  }
  return product::FeasibilityOutcome{.inputs = std::move(inputs), .report = report_json(*report)};
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
  [[nodiscard]] spec::FieldCatalogue field_catalogue() const override { return catalogue(); }
  [[nodiscard]] core::Status spec_checks(const spec::Spec& spec) const override {
    return check_air_power(spec);
  }
  [[nodiscard]] core::Result<product::FeasibilityOutcome> feasibility(
      const spec::Spec& spec) const override {
    return spec_feasibility(spec);
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
    return DerivedTipDiameter{
        .value = nominal,
        .provenance = size->provenance,
        // reference is set here (kind came from it); checked for clang-tidy.
        .provisional = size->provisional || (reference.has_value() && reference->provisional),
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

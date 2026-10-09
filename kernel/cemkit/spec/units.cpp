#include "cemkit/spec/units.hpp"

#include <array>
#include <numbers>
#include <string>

namespace cemkit::spec {

namespace {

struct UnitDefinition {
  std::string_view unit;
  QuantityKind kind;
  double factor;
  double offset;  // for temperature (value_si = (value * factor) + offset)
};

inline constexpr std::array k_unit_table{
    // Length
    UnitDefinition{.unit = "m", .kind = QuantityKind::length, .factor = 1.0, .offset = 0.0},
    UnitDefinition{.unit = "mm", .kind = QuantityKind::length, .factor = 0.001, .offset = 0.0},
    UnitDefinition{.unit = "cm", .kind = QuantityKind::length, .factor = 0.01, .offset = 0.0},
    UnitDefinition{.unit = "in", .kind = QuantityKind::length, .factor = 0.0254, .offset = 0.0},

    // Angular velocity
    UnitDefinition{
        .unit = "rad/s", .kind = QuantityKind::angular_velocity, .factor = 1.0, .offset = 0.0},
    UnitDefinition{.unit = "rpm",
                   .kind = QuantityKind::angular_velocity,
                   .factor = std::numbers::pi / 30.0,
                   .offset = 0.0},

    // Volume flow rate
    UnitDefinition{
        .unit = "m3/s", .kind = QuantityKind::volume_flow_rate, .factor = 1.0, .offset = 0.0},
    UnitDefinition{.unit = "m3/min",
                   .kind = QuantityKind::volume_flow_rate,
                   .factor = 1.0 / 60.0,
                   .offset = 0.0},
    UnitDefinition{.unit = "m3/h",
                   .kind = QuantityKind::volume_flow_rate,
                   .factor = 1.0 / 3600.0,
                   .offset = 0.0},
    UnitDefinition{
        .unit = "L/s", .kind = QuantityKind::volume_flow_rate, .factor = 0.001, .offset = 0.0},
    UnitDefinition{.unit = "CFM",
                   .kind = QuantityKind::volume_flow_rate,
                   .factor = 0.028316846592 / 60.0,
                   .offset = 0.0},

    // Pressure
    UnitDefinition{.unit = "Pa", .kind = QuantityKind::pressure, .factor = 1.0, .offset = 0.0},
    UnitDefinition{.unit = "kPa", .kind = QuantityKind::pressure, .factor = 1000.0, .offset = 0.0},
    UnitDefinition{
        .unit = "mmH2O", .kind = QuantityKind::pressure, .factor = 9.80665, .offset = 0.0},
    UnitDefinition{
        .unit = "mmH₂O", .kind = QuantityKind::pressure, .factor = 9.80665, .offset = 0.0},
    UnitDefinition{
        .unit = "inH2O", .kind = QuantityKind::pressure, .factor = 249.08891, .offset = 0.0},
    UnitDefinition{
        .unit = "inH₂O", .kind = QuantityKind::pressure, .factor = 249.08891, .offset = 0.0},

    // Power
    UnitDefinition{.unit = "W", .kind = QuantityKind::power, .factor = 1.0, .offset = 0.0},
    UnitDefinition{.unit = "kW", .kind = QuantityKind::power, .factor = 1000.0, .offset = 0.0},

    // Density
    UnitDefinition{.unit = "kg/m3", .kind = QuantityKind::density, .factor = 1.0, .offset = 0.0},

    // Temperature
    UnitDefinition{.unit = "K", .kind = QuantityKind::temperature, .factor = 1.0, .offset = 0.0},
    UnitDefinition{
        .unit = "degC", .kind = QuantityKind::temperature, .factor = 1.0, .offset = 273.15},

    // Dynamic viscosity
    UnitDefinition{
        .unit = "Pa*s", .kind = QuantityKind::dynamic_viscosity, .factor = 1.0, .offset = 0.0},
    UnitDefinition{
        .unit = "mPa*s", .kind = QuantityKind::dynamic_viscosity, .factor = 0.001, .offset = 0.0},

    // Ratio / dimensionless
    UnitDefinition{.unit = "1", .kind = QuantityKind::ratio, .factor = 1.0, .offset = 0.0},
    UnitDefinition{.unit = "%", .kind = QuantityKind::ratio, .factor = 0.01, .offset = 0.0},
};

const UnitDefinition* find_unit(std::string_view unit) noexcept {
  for (const auto& def : k_unit_table) {
    if (def.unit == unit) {
      return &def;
    }
  }
  return nullptr;
}

}  // namespace

std::string_view to_string(QuantityKind kind) noexcept {
  switch (kind) {
    case QuantityKind::length:
      return "length";
    case QuantityKind::volume_flow_rate:
      return "volume_flow_rate";
    case QuantityKind::pressure:
      return "pressure";
    case QuantityKind::angular_velocity:
      return "angular_velocity";
    case QuantityKind::power:
      return "power";
    case QuantityKind::density:
      return "density";
    case QuantityKind::temperature:
      return "temperature";
    case QuantityKind::dynamic_viscosity:
      return "dynamic_viscosity";
    case QuantityKind::ratio:
      return "ratio";
  }
  return "unknown";
}

std::optional<QuantityKind> parse_quantity_kind(std::string_view name) noexcept {
  if (name == "length") {
    return QuantityKind::length;
  }
  if (name == "volume_flow_rate") {
    return QuantityKind::volume_flow_rate;
  }
  if (name == "pressure") {
    return QuantityKind::pressure;
  }
  if (name == "angular_velocity") {
    return QuantityKind::angular_velocity;
  }
  if (name == "power") {
    return QuantityKind::power;
  }
  if (name == "density") {
    return QuantityKind::density;
  }
  if (name == "temperature") {
    return QuantityKind::temperature;
  }
  if (name == "dynamic_viscosity") {
    return QuantityKind::dynamic_viscosity;
  }
  if (name == "ratio") {
    return QuantityKind::ratio;
  }
  return std::nullopt;
}

bool is_known_unit(std::string_view unit) noexcept { return find_unit(unit) != nullptr; }

std::optional<QuantityKind> kind_of_unit(std::string_view unit) noexcept {
  const auto* def = find_unit(unit);
  if (def != nullptr) {
    return def->kind;
  }
  return std::nullopt;
}

std::string_view coherent_si_unit(QuantityKind kind) noexcept {
  switch (kind) {
    case QuantityKind::length:
      return "m";
    case QuantityKind::volume_flow_rate:
      return "m3/s";
    case QuantityKind::pressure:
      return "Pa";
    case QuantityKind::angular_velocity:
      return "rad/s";
    case QuantityKind::power:
      return "W";
    case QuantityKind::density:
      return "kg/m3";
    case QuantityKind::temperature:
      return "K";
    case QuantityKind::dynamic_viscosity:
      return "Pa*s";
    case QuantityKind::ratio:
      return "1";
  }
  return "";
}

core::Result<double> convert_to_si(QuantityKind kind, std::string_view unit, double value,
                                   std::string_view field_name) {
  const auto* def = find_unit(unit);
  if (def == nullptr) {
    return core::fail(core::ErrorCode::unknown_unit, "unknown unit: " + std::string(unit),
                      std::string(field_name), {{"unit", std::string(unit)}});
  }
  if (def->kind != kind) {
    return core::fail(
        core::ErrorCode::unit_mismatch,
        "unit " + std::string(unit) + " has dimension " + std::string(to_string(def->kind)) +
            ", expected " + std::string(to_string(kind)),
        std::string(field_name),
        {{"expected_kind", std::string(to_string(kind))}, {"unit", std::string(unit)}});
  }
  return (value * def->factor) + def->offset;
}

core::Result<double> convert_tolerance_to_si(QuantityKind kind, std::string_view unit, double value,
                                             std::string_view field_name) {
  const auto* def = find_unit(unit);
  if (def == nullptr) {
    return core::fail(core::ErrorCode::unknown_unit, "unknown unit: " + std::string(unit),
                      std::string(field_name), {{"unit", std::string(unit)}});
  }
  if (def->kind != kind) {
    return core::fail(
        core::ErrorCode::unit_mismatch,
        "unit " + std::string(unit) + " has dimension " + std::string(to_string(def->kind)) +
            ", expected " + std::string(to_string(kind)),
        std::string(field_name),
        {{"expected_kind", std::string(to_string(kind))}, {"unit", std::string(unit)}});
  }
  // Tolerances are deltas: offset is never added!
  return value * def->factor;
}

}  // namespace cemkit::spec

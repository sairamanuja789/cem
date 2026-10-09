#include "cemkit/spec/units.hpp"

#include <numbers>
#include <string>

namespace cemkit::spec {

namespace {

struct UnitDefinition {
  std::string_view unit;
  QuantityKind kind;
  double factor;
  double offset;  // for temperature (value_si = value * factor + offset)
};

constexpr UnitDefinition k_unit_table[] = {
    // Length
    {"m", QuantityKind::length, 1.0, 0.0},
    {"mm", QuantityKind::length, 0.001, 0.0},
    {"cm", QuantityKind::length, 0.01, 0.0},
    {"in", QuantityKind::length, 0.0254, 0.0},

    // Angular velocity
    {"rad/s", QuantityKind::angular_velocity, 1.0, 0.0},
    {"rpm", QuantityKind::angular_velocity, std::numbers::pi / 30.0, 0.0},

    // Volume flow rate
    {"m3/s", QuantityKind::volume_flow_rate, 1.0, 0.0},
    {"m3/min", QuantityKind::volume_flow_rate, 1.0 / 60.0, 0.0},
    {"m3/h", QuantityKind::volume_flow_rate, 1.0 / 3600.0, 0.0},
    {"L/s", QuantityKind::volume_flow_rate, 0.001, 0.0},
    {"CFM", QuantityKind::volume_flow_rate, 0.028316846592 / 60.0, 0.0},

    // Pressure
    {"Pa", QuantityKind::pressure, 1.0, 0.0},
    {"kPa", QuantityKind::pressure, 1000.0, 0.0},
    {"mmH2O", QuantityKind::pressure, 9.80665, 0.0},
    {"mmH₂O", QuantityKind::pressure, 9.80665, 0.0},
    {"inH2O", QuantityKind::pressure, 249.08891, 0.0},
    {"inH₂O", QuantityKind::pressure, 249.08891, 0.0},

    // Power
    {"W", QuantityKind::power, 1.0, 0.0},
    {"kW", QuantityKind::power, 1000.0, 0.0},

    // Density
    {"kg/m3", QuantityKind::density, 1.0, 0.0},

    // Temperature
    {"K", QuantityKind::temperature, 1.0, 0.0},
    {"degC", QuantityKind::temperature, 1.0, 273.15},

    // Dynamic viscosity
    {"Pa*s", QuantityKind::dynamic_viscosity, 1.0, 0.0},
    {"mPa*s", QuantityKind::dynamic_viscosity, 0.001, 0.0},

    // Ratio / dimensionless
    {"1", QuantityKind::ratio, 1.0, 0.0},
    {"%", QuantityKind::ratio, 0.01, 0.0},
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
  if (name == "length") return QuantityKind::length;
  if (name == "volume_flow_rate") return QuantityKind::volume_flow_rate;
  if (name == "pressure") return QuantityKind::pressure;
  if (name == "angular_velocity") return QuantityKind::angular_velocity;
  if (name == "power") return QuantityKind::power;
  if (name == "density") return QuantityKind::density;
  if (name == "temperature") return QuantityKind::temperature;
  if (name == "dynamic_viscosity") return QuantityKind::dynamic_viscosity;
  if (name == "ratio") return QuantityKind::ratio;
  return std::nullopt;
}

bool is_known_unit(std::string_view unit) noexcept {
  return find_unit(unit) != nullptr;
}

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
    return core::fail(core::ErrorCode::unknown_unit,
                      "unknown unit: " + std::string(unit),
                      std::string(field_name),
                      {{"unit", std::string(unit)}});
  }
  if (def->kind != kind) {
    return core::fail(core::ErrorCode::unit_mismatch,
                      "unit " + std::string(unit) + " has dimension " +
                          std::string(to_string(def->kind)) + ", expected " +
                          std::string(to_string(kind)),
                      std::string(field_name),
                      {{"expected_kind", std::string(to_string(kind))},
                       {"unit", std::string(unit)}});
  }
  return value * def->factor + def->offset;
}

core::Result<double> convert_tolerance_to_si(QuantityKind kind, std::string_view unit, double value,
                                             std::string_view field_name) {
  const auto* def = find_unit(unit);
  if (def == nullptr) {
    return core::fail(core::ErrorCode::unknown_unit,
                      "unknown unit: " + std::string(unit),
                      std::string(field_name),
                      {{"unit", std::string(unit)}});
  }
  if (def->kind != kind) {
    return core::fail(core::ErrorCode::unit_mismatch,
                      "unit " + std::string(unit) + " has dimension " +
                          std::string(to_string(def->kind)) + ", expected " +
                          std::string(to_string(kind)),
                      std::string(field_name),
                      {{"expected_kind", std::string(to_string(kind))},
                       {"unit", std::string(unit)}});
  }
  // Tolerances are deltas: offset is never added!
  return value * def->factor;
}

}  // namespace cemkit::spec

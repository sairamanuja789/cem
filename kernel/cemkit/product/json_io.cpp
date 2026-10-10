#include "cemkit/product/json_io.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

namespace cemkit::product {

namespace si = mp_units::si;
namespace isq = mp_units::isq;

const Json& member(const Json& object, const char* key) {
  if (!object.is_object() || !object.contains(key)) {
    throw BadRequest{key, std::string{"missing key: "} + key};
  }
  return object[key];
}

double number(const Json& object, const char* key) {
  const Json& value = member(object, key);
  if (!value.is_number()) {
    throw BadRequest{key, std::string{"must be a number: "} + key};
  }
  return value.get<double>();
}

std::string text(const Json& object, const char* key) {
  const Json& value = member(object, key);
  if (!value.is_string()) {
    throw BadRequest{key, std::string{"must be a string: "} + key};
  }
  return value.get<std::string>();
}

std::optional<double> optional_number(const Json& object, const char* key) {
  if (!object.contains(key) || object[key].is_null()) {
    return std::nullopt;
  }
  return number(object, key);
}

physics::Air air_of(const Json& object, const char* key) {
  physics::Air air = physics::default_air();
  if (!object.contains(key)) {
    return air;
  }
  const Json& a = object[key];
  if (!a.is_object()) {
    throw BadRequest{key, std::string{"must be an object: "} + key};
  }
  if (auto v = optional_number(a, "density")) {
    air.density = *v * isq::mass_density[si::kilogram / mp_units::cubic(si::metre)];
  }
  if (auto v = optional_number(a, "temperature")) {
    air.temperature = mp_units::point<isq::thermodynamic_temperature[si::kelvin]>(*v);
  }
  if (auto v = optional_number(a, "pressure")) {
    air.pressure = core::units::absolute_zero_pressure + *v * isq::pressure[si::pascal];
  }
  if (auto v = optional_number(a, "gamma")) {
    air.gamma = *v * isq::ratio_of_specific_heat_capacities[mp_units::one];
  }
  if (auto v = optional_number(a, "gas_constant")) {
    air.gas_constant = *v * isq::specific_gas_constant[si::joule / (si::kilogram * si::kelvin)];
  }
  return air;
}

Json error_json(const core::Error& error) {
  Json details = Json::object();
  for (const auto& [key, value] : error.details()) {
    details[key] = value;
  }
  return Json{{"code", std::string{core::to_string(error.code())}},
              {"subject", error.subject()},
              {"message", error.message()},
              {"details", details},
              {"describe", core::describe(error)}};
}

}  // namespace cemkit::product

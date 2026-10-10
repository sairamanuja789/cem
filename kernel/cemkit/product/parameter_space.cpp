#include "cemkit/product/parameter_space.hpp"

#include <cmath>
#include <set>
#include <utility>

namespace cemkit::product {

namespace {

core::ErrorDetails bounds_details(const Parameter& p) {
  return {
      {"bounds", "[" + core::format_number(p.lower) + ", " + core::format_number(p.upper) + "]"},
      {"unit", std::string(p.unit())}};
}

core::Status check_parameter(const Parameter& p) {
  if (!std::isfinite(p.lower) || !std::isfinite(p.upper) || !(p.lower < p.upper)) {
    return core::fail(core::ErrorCode::invalid_input,
                      "parameter bounds must be finite with lower < upper", p.name,
                      bounds_details(p));
  }
  if (p.default_value) {
    const auto& d = *p.default_value;
    if (!std::isfinite(d.si_value) || d.si_value < p.lower || d.si_value > p.upper) {
      auto details = bounds_details(p);
      details.emplace("value", core::format_number(d.si_value));
      return core::fail(core::ErrorCode::invalid_input,
                        "parameter default must be finite and inside the bounds", p.name,
                        std::move(details));
    }
    if (d.source.empty()) {
      return core::fail(core::ErrorCode::invalid_input,
                        "parameter default needs a source (or UNSOURCED)", p.name);
    }
  }
  return {};
}

}  // namespace

core::Result<ParameterSpace> ParameterSpace::create(std::vector<Parameter> parameters) {
  std::set<std::string, std::less<>> seen;
  for (const auto& p : parameters) {
    if (p.name.empty()) {
      return core::fail(core::ErrorCode::invalid_input, "parameter name must not be empty",
                        "parameter");
    }
    if (!seen.insert(p.name).second) {
      return core::fail(core::ErrorCode::invalid_input, "parameter name is used twice", p.name);
    }
    if (auto status = check_parameter(p); !status) {
      return std::unexpected(std::move(status.error()));
    }
  }
  return ParameterSpace{std::move(parameters)};
}

std::optional<Parameter> ParameterSpace::find(std::string_view name) const {
  for (const auto& p : parameters_) {
    if (p.name == name) {
      return p;
    }
  }
  return std::nullopt;
}

std::vector<std::string> ParameterSpace::unsourced_defaults() const {
  std::vector<std::string> names;
  for (const auto& p : parameters_) {
    if (p.default_value && p.default_value->source.starts_with(k_unsourced)) {
      names.push_back(p.name);
    }
  }
  return names;
}

core::Status ParameterSpace::check(const Design& design) const {
  if (design.values.size() != parameters_.size()) {
    return core::fail(core::ErrorCode::invalid_input,
                      "design must give one value per parameter, in parameter order", "design",
                      {{"expected", std::to_string(parameters_.size())},
                       {"given", std::to_string(design.values.size())}});
  }
  for (std::size_t i = 0; i < parameters_.size(); ++i) {
    const auto& p = parameters_[i];
    const auto& v = design.values[i];
    if (v.name != p.name) {
      return core::fail(core::ErrorCode::invalid_input,
                        "design value is not for the parameter at this position", p.name,
                        {{"given", v.name}});
    }
    if (!std::isfinite(v.si_value)) {
      return core::fail(core::ErrorCode::invalid_input, "design value must be finite", p.name,
                        {{"value", core::format_number(v.si_value)}});
    }
    if (v.si_value < p.lower || v.si_value > p.upper) {
      auto details = bounds_details(p);
      details.emplace("value", core::format_number(v.si_value));
      return core::fail(core::ErrorCode::out_of_validity,
                        "design value is outside the parameter bounds", p.name, std::move(details));
    }
  }
  return {};
}

core::Result<Design> ParameterSpace::default_design() const {
  Design design;
  design.values.reserve(parameters_.size());
  for (const auto& p : parameters_) {
    if (!p.default_value) {
      return core::fail(core::ErrorCode::invalid_input, "parameter has no default", p.name);
    }
    design.values.push_back(ParameterValue{.name = p.name, .si_value = p.default_value->si_value});
  }
  return design;
}

}  // namespace cemkit::product

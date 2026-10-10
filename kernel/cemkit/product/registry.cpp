#include "cemkit/product/registry.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace cemkit::product {

namespace {

bool is_name_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// "<product>.<family>": two non-empty parts of [a-z0-9_], separated by exactly one '.'.
bool is_valid_id(std::string_view id) noexcept {
  const auto dot = id.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == id.size()) {
    return false;
  }
  const auto product = id.substr(0, dot);
  const auto family = id.substr(dot + 1);
  return std::ranges::all_of(product, is_name_char) && std::ranges::all_of(family, is_name_char);
}

}  // namespace

core::Status Registry::add(std::unique_ptr<const Family> family) {
  if (!family) {
    return core::fail(core::ErrorCode::invalid_input, "cannot register an empty family", "family");
  }
  const std::string id{family->id()};
  if (!is_valid_id(id)) {
    return core::fail(core::ErrorCode::invalid_input,
                      "family id must be <product>.<family> in lower-case letters, digits and _",
                      "family", {{"family", id}});
  }
  if (contains(id)) {
    return core::fail(core::ErrorCode::invalid_input, "family is already registered", "family",
                      {{"family", id}});
  }
  std::set<std::string, std::less<>> seen;
  for (const auto& path : family->essential_fields()) {
    if (path.empty() || !seen.insert(path).second) {
      return core::fail(core::ErrorCode::invalid_input,
                        "essential field paths must be non-empty and listed once", "family",
                        {{"family", id}, {"field", path}});
    }
  }
  for (const auto& limit : family->feasible_range().limits) {
    const bool finite = (!limit.lower || std::isfinite(*limit.lower)) &&
                        (!limit.upper || std::isfinite(*limit.upper));
    const bool ordered = !limit.lower || !limit.upper || *limit.lower <= *limit.upper;
    if ((!limit.lower && !limit.upper) || !finite || !ordered || limit.source.empty()) {
      return core::fail(core::ErrorCode::invalid_input,
                        "a feasible-range limit needs a finite side, ordered bounds and a source",
                        "family", {{"family", id}, {"quantity", limit.quantity}});
    }
  }
  families_.push_back(std::move(family));
  return {};
}

core::Result<std::reference_wrapper<const Family>> Registry::get(std::string_view id) const {
  const auto it = std::ranges::find_if(families_, [id](const auto& f) { return f->id() == id; });
  if (it == families_.end()) {
    return core::fail(core::ErrorCode::spec_rejected,
                      "unknown family: no registered plugin has this id", "family",
                      {{"family", std::string(id)}});
  }
  return std::cref(**it);
}

bool Registry::contains(std::string_view id) const noexcept {
  return std::ranges::any_of(families_, [id](const auto& f) { return f->id() == id; });
}

std::vector<std::string> Registry::ids() const {
  std::vector<std::string> out;
  out.reserve(families_.size());
  for (const auto& f : families_) {
    out.emplace_back(f->id());
  }
  return out;
}

core::Result<std::vector<std::string>> Registry::essential_fields(std::string_view id) const {
  return get(id).transform([](const Family& f) { return f.essential_fields(); });
}

spec::EssentialFieldResolver essential_field_resolver(std::shared_ptr<const Registry> registry) {
  return [registry = std::move(registry)](
             std::string_view family) -> core::Result<std::vector<std::string>> {
    if (!registry) {
      return core::fail(core::ErrorCode::internal_error,
                        "essential-field resolver has no family registry", "family");
    }
    return registry->essential_fields(family);
  };
}

}  // namespace cemkit::product

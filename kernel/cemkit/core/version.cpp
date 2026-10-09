#include "cemkit/core/version.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace cemkit::core {

namespace {

std::optional<std::uint32_t> parse_component(std::string_view text) {
  if (text.empty() || (text.size() > 1 && text.front() == '0')) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (status != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::string_view kernel_version() noexcept { return CEMKIT_KERNEL_VERSION; }

std::expected<SemVer, Error> parse_semver(std::string_view text) {
  std::array<std::uint32_t, 3> parts{};
  std::string_view rest = text;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const bool last = i + 1 == parts.size();
    const std::size_t dot = rest.find('.');
    if (last != (dot == std::string_view::npos)) {
      return std::unexpected{Error{ErrorCode::invalid_input,
                                   "a version must be MAJOR.MINOR.PATCH",
                                   "version",
                                   {{"value", std::string{text}}}}};
    }
    const auto component = parse_component(rest.substr(0, dot));
    if (!component) {
      return std::unexpected{Error{ErrorCode::invalid_input,
                                   "each version component must be a decimal number without "
                                   "leading zeros that fits in 32 bits",
                                   "version",
                                   {{"value", std::string{text}}}}};
    }
    parts.at(i) = *component;
    rest = last ? std::string_view{} : rest.substr(dot + 1);
  }
  return SemVer{.major = parts[0], .minor = parts[1], .patch = parts[2]};
}

std::string to_string(SemVer version) {
  return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' +
         std::to_string(version.patch);
}

std::string to_string(const ModelId& model) { return model.name + '@' + to_string(model.version); }

}  // namespace cemkit::core

#include "cemkit/core/error.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace cemkit::core {

std::string describe(const Error& error) {
  std::string text{to_string(error.code())};
  if (!error.subject().empty()) {
    text += " at ";
    text += error.subject();
  }
  text += ": ";
  text += error.message();
  if (!error.details().empty()) {
    text += " (";
    const char* separator = "";
    for (const auto& [key, value] : error.details()) {
      text += separator;
      text += key;
      text += '=';
      text += value;
      separator = ", ";
    }
    text += ')';
  }
  return text;
}

std::string format_number(double value) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value > 0.0 ? "inf" : "-inf";
  }
  // 32 characters hold the longest shortest-round-trip form of a double
  // ("-2.2250738585072014e-308").
  std::array<char, 32> buffer{};
  const auto [end, status] = std::to_chars(buffer.begin(), buffer.end(), value);
  if (status != std::errc{}) {
    return "?";  // unreachable with a 32-character buffer
  }
  return {buffer.begin(), end};
}

}  // namespace cemkit::core

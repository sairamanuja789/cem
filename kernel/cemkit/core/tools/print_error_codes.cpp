// Prints k_error_codes as JSON on stdout (REL-002). scripts/gen_error_codes.py renders
// docs/models/platform/error-codes.md and schemas/cemkit/v1/error-codes.json from this output, so
// the kernel table stays the single source.
#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "cemkit/core/error.hpp"

int main() try {
  auto codes = nlohmann::ordered_json::array();
  for (const auto& info : cemkit::core::k_error_codes) {
    codes.push_back({{"code", std::to_underlying(info.code)},
                     {"name", std::string{info.name}},
                     {"meaning", std::string{info.meaning}}});
  }
  std::cout << codes.dump(2) << '\n';
  return 0;
} catch (const std::exception& error) {
  std::cerr << "print_error_codes: " << error.what() << '\n';
  return 1;
}

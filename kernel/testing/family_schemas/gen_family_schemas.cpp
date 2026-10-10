// ADR-015 rule 3: writes or checks schemas/products/<product>/v1/families/<family>.schema.json for
// every compiled-in product family, generated from Family::field_catalogue() (the single source of
// truth). Usage: gen_family_schemas <repo root> [--write]. Without --write it exits 1 if a
// committed file is missing or differs (the ctest family_schemas_up_to_date);
// scripts/gen_family_schemas.sh runs it with --write.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "cemkit/product/products.hpp"
#include "cemkit/spec/catalogue.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: gen_family_schemas <repo root> [--write]\n";
    return 2;
  }
  const fs::path root{argv[1]};
  const bool write = argc > 2 && std::string_view{argv[2]} == "--write";
  cemkit::product::Registry registry;
  cemkit::product::Operations operations;
  if (auto ok = cemkit::product::register_products(registry, operations); !ok) {
    std::cerr << "registration failed: " << ok.error().message() << "\n";
    return 2;
  }
  int stale = 0;
  for (const auto& id : registry.ids()) {
    const auto family = registry.get(id);
    if (!family) {
      return 2;
    }
    const auto dot = id.find('.');
    const fs::path file = root / "schemas" / "products" / id.substr(0, dot) / "v1" / "families" /
                          (id.substr(dot + 1) + ".schema.json");
    const std::string text =
        cemkit::spec::family_schema(id, family->get().field_catalogue()).dump(2) + "\n";
    if (write) {
      fs::create_directories(file.parent_path());
      std::ofstream{file, std::ios::binary} << text;
      std::cout << "wrote " << file.string() << "\n";
      continue;
    }
    std::ifstream in{file, std::ios::binary};
    std::stringstream committed;
    committed << in.rdbuf();
    if (!in || committed.str() != text) {
      std::cerr << "stale or missing: " << file.string()
                << " (run scripts/gen_family_schemas.sh and commit the result)\n";
      ++stale;
    }
  }
  return stale == 0 ? 0 : 1;
}

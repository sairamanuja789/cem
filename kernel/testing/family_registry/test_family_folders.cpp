// FAM-002, UC-09 (ADR-009): every family folder is in the registry. A family folder added without
// its line in the product's register.cpp fails here, so a forgotten registration cannot go
// unnoticed.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "cemkit/product/registry.hpp"
#include "products/fans/register.hpp"
#include "testing/stub_product/register.hpp"

namespace fs = std::filesystem;
using cemkit::product::Registry;

namespace {

// "<product>.<family>" for every directory <root>/<product>/families/<family>/, sorted.
std::vector<std::string> family_folders(const fs::path& root) {
  std::vector<std::string> ids;
  for (const auto& product : fs::directory_iterator(root)) {
    const auto families = product.path() / "families";
    if (!product.is_directory() || !fs::is_directory(families)) {
      continue;
    }
    for (const auto& family : fs::directory_iterator(families)) {
      if (family.is_directory()) {
        ids.push_back(product.path().filename().string() + "." + family.path().filename().string());
      }
    }
  }
  std::ranges::sort(ids);
  return ids;
}

std::vector<std::string> missing_from(const std::vector<std::string>& folders,
                                      const Registry& registry) {
  std::vector<std::string> missing;
  std::ranges::copy_if(folders, std::back_inserter(missing),
                       [&registry](const std::string& id) { return !registry.contains(id); });
  return missing;
}

// The registry the products build: every product's registration function, called once.
Registry all_products() {
  Registry registry;
  REQUIRE(cemkit::fans::register_fan_families(registry).has_value());
  REQUIRE(cemkit::testing::stub_product::register_stub_product_families(registry).has_value());
  return registry;
}

}  // namespace

TEST_CASE("every folder under products/*/families/ is registered", "[FAM-002][UC-09]") {
  const fs::path kernel{CEMKIT_KERNEL_ROOT};
  REQUIRE(fs::is_directory(kernel / "products"));
  REQUIRE(fs::is_directory(kernel / "testing"));
  const auto registry = all_products();

  auto folders = family_folders(kernel / "products");
  const auto stub_folders = family_folders(kernel / "testing");
  // The stub proves the check runs on at least one real folder.
  REQUIRE(std::ranges::find(stub_folders, "stub_product.stub_family") != stub_folders.end());
  folders.insert(folders.end(), stub_folders.begin(), stub_folders.end());

  CHECK(missing_from(folders, registry).empty());

  // And every registered family has its folder: nothing is registered from elsewhere.
  std::ranges::sort(folders);
  auto ids = registry.ids();
  std::ranges::sort(ids);
  CHECK(ids == folders);
}

TEST_CASE("a family folder missing from the registry is reported", "[FAM-002]") {
  const auto registry = all_products();
  const std::vector<std::string> folders{"stub_product.stub_family", "stub_product.forgotten"};
  CHECK(missing_from(folders, registry) == std::vector<std::string>{"stub_product.forgotten"});
}

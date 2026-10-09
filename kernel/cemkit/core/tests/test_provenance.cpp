#include <catch2/catch_test_macros.hpp>

#include "cemkit/core/provenance.hpp"

using cemkit::core::Provenance;

TEST_CASE("the five provenance classes have fixed names", "[COR-003]") {
  CHECK(cemkit::core::k_provenance_classes.size() == 5);
  CHECK(cemkit::core::to_string(Provenance::user) == "user");
  CHECK(cemkit::core::to_string(Provenance::image) == "image");
  CHECK(cemkit::core::to_string(Provenance::derived) == "derived");
  CHECK(cemkit::core::to_string(Provenance::default_value) == "default");
  CHECK(cemkit::core::to_string(Provenance::unknown) == "unknown");
}

TEST_CASE("provenance names round-trip and unknown names are rejected", "[COR-003]") {
  for (const auto& info : cemkit::core::k_provenance_classes) {
    CHECK(cemkit::core::parse_provenance(info.name) == info.provenance);
  }
  CHECK_FALSE(cemkit::core::parse_provenance("default_value").has_value());
  CHECK_FALSE(cemkit::core::parse_provenance("User").has_value());
  CHECK(cemkit::core::to_string(static_cast<Provenance>(99)).empty());
}

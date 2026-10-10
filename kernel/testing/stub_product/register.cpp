#include "testing/stub_product/register.hpp"

#include "testing/stub_product/families/stub_family/stub_family.hpp"

namespace cemkit::testing::stub_product {

core::Status register_stub_product_families(product::Registry& registry) {
  return stub_family::register_family(registry);
}

}  // namespace cemkit::testing::stub_product

#include "testing/contract_stubs/register.hpp"

#include "testing/contract_stubs/families/stub_bracket/stub_bracket.hpp"
#include "testing/contract_stubs/families/stub_ceiling/stub_ceiling.hpp"

namespace cemkit::testing::contract_stubs {

core::Status register_contract_stub_families(product::Registry& registry) {
  if (auto ok = stub_bracket::register_family(registry); !ok) {
    return ok;
  }
  return stub_ceiling::register_family(registry);
}

}  // namespace cemkit::testing::contract_stubs

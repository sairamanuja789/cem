# Two deliberately unlike stub families (ADR-015 M1): test fixtures, not products and not users of
# any platform feature: a structural bracket that shares no physics with fans (loads, deflection,
# material) and a ceiling fan with an air-delivery duty and no pressure. Laid out like a real
# product; families are found by folder discovery.
cemkit_add_families(contract_stub_family_targets)
cemkit_add_module(
  PATH testing/contract_stubs
  SOURCES register.cpp
  DEPS cemkit_product ${contract_stub_family_targets}
)

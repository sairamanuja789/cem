# ADR-012: Family plugin interface, registry-backed essential fields, family folder discovery

- Status: proposed (needs owner review)
- Date: 2026-10-10
- Requirements affected: FAM-001, FAM-002, FAM-003, SPEC-006, UC-09

## Context
T09 builds the family plugin system (`kernel/cemkit/product/`). ADR-009 already fixes explicit
registration (`register_family(Registry&)`, one call per family in the product's `register.cpp`,
no static self-registration) and the CI test for unregistered family folders. Work was autonomous,
so the choices ADR-009 leaves open are recorded here for the owner:
1. the shape of the interface, while the geometry and simulation ports do not exist yet;
2. how the spec compiler gets essential fields (SPEC-006: "the compiler asks the plugin, not a
   global list"), given that T07 left a global stub list naming `fans.axial_ducted` in the platform;
3. how parameter values carry units (FAM-003);
4. how FAM-002 ("no changes outside its plugin folder") is met alongside ADR-009's register line.

## Options considered
1. Essential fields: keep T07's global list as the compiler's default and add a registry resolver
   beside it — no churn, but the platform keeps a product name and a global list (SPEC-006 forbids
   it); or remove the list and make the resolver required — chosen.
2. Unknown family: an empty essential list (spec compiles with no essential checks) — silent; or
   reject the spec naming `family` — chosen.
3. Parameter units: one mp-units type per parameter — a parameter space is heterogeneous and read by
   the optimizer by name, so it cannot be one typed container; or SI `double` paired with
   `spec::QuantityKind`, the same convention as `spec::Field` — chosen.
4. Family build and registration: a CMake-generated list of register calls from folder names (no
   edit outside the folder at all, but it replaces ADR-009's explicit line); or CMake folder
   discovery for the build plus ADR-009's explicit register line — chosen.

## Decision
- **Interface** (`family.hpp`): `id()` is `"<product>.<family>"` — the product and family folder
  names and the spec's `family` value; `plugin()` (name + semver, PHY-004); `parameter_space()`;
  `essential_fields()`; `feasible_range()` (named bounds with a source or `UNSOURCED`);
  `initial_design`, `evaluate_l1` (metrics carry a fidelity label and model id), `check_constraints`
  (value, limit, sense, source), `geometry_recipe`, `simulation_case`. All pure and deterministic,
  returning `Result`. `GeometryRecipe` and `SimulationCase` are provisional (a versioned name plus
  named SI values) until the geometry port (T10) and the simulation port define typed contracts.
- **Registry** (`registry.hpp`): owned by its caller, no global instance; registration order kept;
  rejects malformed ids, duplicates and malformed essential lists. Registration functions return
  `core::Status`.
- **Essential fields** (SPEC-006): `spec::EssentialFieldResolver` returns
  `Result<vector<string>>`. The spec module has no built-in list; `CompilerOptions` has no default
  resolver, and `compile()` fails with `invalid_input` without one. `product::essential_field_resolver
  (shared_ptr<const Registry>)` supplies it; an unregistered family is `spec_rejected` naming
  `family`. The resolver shares ownership of the registry because a compiled spec keeps its options
  for `derive_new_revision` (SPEC-009).
- **Parameters** (FAM-003): name, `spec::QuantityKind` (the unit is its coherent SI unit), finite
  bounds with lower < upper, optional default inside the bounds with a mandatory source; sources
  starting with `UNSOURCED` are allowed and reported by `unsourced_defaults()` (CLAUDE.md rule 7).
- **Folders** (FAM-002, UC-09): `cemkit_add_families()` adds every `families/*/` folder of a
  product in sorted order; `cemkit_add_family()` makes a family's build file name nothing outside
  its folder. Registration stays ADR-009's explicit call in the product's `register.cpp`. The stub
  is laid out as a product: `kernel/testing/stub_product/families/stub_family/`.
- **Checks**: `kernel/testing/family_registry` fails if a folder under `products/*/families/` or
  `testing/*/families/` is not registered, or a registered family has no folder;
  `tests/python/test_family_isolation.py` fails if the platform or any build file names a family,
  or if anything other than its product's `register.cpp` (and tests) does.

## Consequences
- FAM-002 is met as ADR-009 reads it: a new family is its folder, its tests, and the include plus
  call in its product's `register.cpp`; nothing else. A literal "no edit outside the folder" would
  need the generated register list of option 4 (HI-021).
- Until `products/fans/families/axial_ducted/` exists and declares ADR-003's essential fields
  (`product.duty.flow`, `product.duty.pressure`), a `fans.axial_ducted` spec compiled with a
  registry-backed resolver is rejected as an unknown family (HI-022). Callers that relied on the
  T07 default (none in this branch) must now pass a resolver.
- Typed geometry and simulation contracts replace the provisional structs when their ports land;
  that changes `family.hpp` and every family, and is cheap only while the stub is the sole family.

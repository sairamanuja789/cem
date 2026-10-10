# ADR-015: A product-neutral platform core with family-owned specs, and the geometry contract

- Status: accepted (owner, 2026-10-10)
- Date: 2026-10-10
- Requirements affected: FAM-001, FAM-002, FAM-003, SPEC-001, SPEC-006, GEO-001, MAINT-005

## Context
The platform (cemkit) is meant to extend beyond fans; v1 scope (the 120 mm ducted axial fan) is
unchanged. An audit of main at 3c61342 found the layering of ADR-009 sound but found fan and
pressure-rise assumptions inside platform code:
1. `kernel/cemkit/spec/compiler.cpp` `expected_field_kind()` hardcodes product field paths
   (`product.duty.flow`, `product.duty.pressure`, `product.power_limit`, `product.motor.*`, ...).
2. `kernel/cemkit/spec/spec.cpp` checks air power Q·Δp against the power limit (fan physics).
3. `kernel/cemkit/capi/detail/handlers.cpp` calls `fans::` code directly, and
   `scripts/check_boundaries.py` exempts `capi` from every dependency rule, so the gate cannot see it.
4. `python/cemkit/kernel.py` and `cli/app.py` assume a flow/pressure duty point.
5. The geometry port offers only `build_test_solid`; `GeometryRecipe` and `SimulationCase` are
   untyped placeholders (ADR-012).

## Options considered
1. Domain-independent platform with plugins, grown only where two real users exist — one core,
   gated boundaries; costs one task now.
2. A small common framework plus independent applications per product — fast at first, then
   provenance, store and claims are duplicated.
3. A monolithic CEM that grows per family — fastest now, accumulates product conditionals.

## Decision
Option 1, at its minimum: the core names no product, each family owns its fields, checks and
feasibility report, and new shared abstractions need two real users.

Rules:
1. **Two-user rule.** No shared abstraction (geometry operation, domain library, solver contract)
   until two real users need it. Test fixtures (stub families) prove platform paths but are not users
   and never justify a new feature.
2. **Physics are libraries, not plugins.** Domain physics are pure functions with validity ranges and
   never include geometry or adapter code. No `kernel/domains/` directory until a second family needs
   the same model; until then models stay in the product's `common/`.
3. **One source of truth for family fields.** `Family::field_catalogue()` (C++) declares each field's
   path, quantity kind, requirement and sign rule. It generates
   `schemas/products/<p>/v1/families/<f>.schema.json`; a test fails if the committed schema differs
   from the generated one (the same pattern as `schemas/cemkit/v1/error-codes.json`). The spec
   compiler validates against the catalogue; `expected_field_kind()` is removed.
4. **Family-owned checks.** `Family::spec_checks(spec)` holds contradiction rules (the Q·Δp check
   moves to `fans.axial_ducted`); `Family::feasibility(spec)` returns a family-defined report rendered
   by a shared formatter.
5. **The C ABI dispatches through the Registry** and a generated registration entry point only. The
   `capi` exemption in `check_boundaries.py` is removed; a planted-violation test proves the rule fires.
6. **Compatibility.** `fans.axial_ducted` spec paths, CLI commands and outputs, and C ABI 0.2.0
   behaviour stay the same, shown by golden JSON diffs in CI. The ABI minor version may rise only for
   additive JSON fields. Existing tests keep their assertions.
7. **Geometry contract (designed now, built in M2).** Typed port operations, added as the axial
   recipe needs them: an interpolated curve / wire through given points (blade sections need it),
   profile, extrude, revolve, loft, boolean, pattern, fillet, assembly placement. Recipes select edges
   and faces by geometric rule (for example the blade–hub intersection edges), never by index; this is
   M2's main risk and is tested under parameter changes. The axial recipe drives which operations
   exist.

Sequencing: M1 is time-boxed to one task — field catalogue, spec checks, family feasibility report,
C ABI dispatch, strict boundaries, two stub families in `kernel/testing/stub_product/`
(`stub_bracket`, deliberately non-fan; `stub_ceiling`, air delivery and no pressure), a family
contract suite over all registered families, and golden JSON diffs. Then work returns to the axial
family (L1 model and blade recipe, blocked on the owner's airfoil decision).

## Consequences
- A new family is added without editing platform code; FAM-002 plus the contract suite enforce it.
- The bracket stub exposes any fan assumption left in the core, because it shares no physics.
- Writing a family now means writing its catalogue; its JSON schema is generated, not hand-written.
- Edge/face selection by rule is harder than by index and must be designed with the first fillet.
- Revisit when a second real family is accepted into scope (requires a requirements change by the
  owner) or when a second user for a domain model appears.

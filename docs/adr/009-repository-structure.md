# ADR-009: Repository structure — a product-agnostic platform (cemkit) with product domains

- Status: accepted
- Date: 2026-10-09
- Requirements affected: FAM-001, FAM-002, UC-09, MAINT-003, MAINT-004, MAINT-005, REL-002 (spec v1.2)

## Context
The first layout (requirements section 8, v1.1) was fan-specific throughout: `libfancem`, `python/fancem`,
`kernel/include` and `kernel/src` split by language role, one plugin folder. Later products (other fan
families, then other machines) and later engines (another CAD kernel, CFD solver or optimizer) would have
forced edits across unrelated modules. Changing the layout is cheap now (one kernel module exists) and
grows with every task.

## Options considered
1. Keep the v1.1 layout and refactor when a second product arrives — cheapest now, most expensive later.
2. A product-agnostic platform with product domains, ports and adapters for external engines, one CMake
   target per module and enforced dependency rules.

## Decision
Option 2, as specified in `docs/repository-structure.md`. Platform name **cemkit** (`cem` is taken on PyPI):
C++ namespace `cemkit`, Python package `cemkit`, CMake targets `cemkit_*`, C header `cemkit.h`, CLI `cemkit`.

- `kernel/` is the include root; namespaces follow the path with `products/` and `common/` dropped
  (`cemkit::core`, `cemkit::fans`, `cemkit::fans::axial_ducted`); `detail/` headers are private.
- One CMake target per module via `cemkit_add_module()`; module tests live in `<module>/tests/`.
- **Registration is explicit**: each family exposes `register_family(Registry&)`, each product's
  `register.cpp` calls its families in a fixed order, and the C ABI calls each product's registration once.
  Static self-registration was rejected: self-registering objects in static libraries are dropped by the
  linker when nothing references them, leaving an empty registry. Whole-archive linking was rejected because
  it behaves differently across toolchains. A CI test (T09) fails if a folder under `products/*/families/`
  is missing from the registry.
- Dependency rules are enforced by `scripts/check_boundaries.py` in `scripts/check.sh`: an `#include` scan,
  the CMake link graph (`cmake --graphviz`) and a Python import scan. No new dependencies.
- MAINT-003's coverage scope (spec v1.2): platform core, spec and physics, plus `products/*/common` and each
  family's L1 model (`gcovr.cfg`, `scripts/lib/coverage.sh`).
- A wrapper type for fan pressure kinds, if mp-units cannot express them, would be ADR-010.

## Consequences
- Adding a family = its folder + one line in its product's `register.cpp`; adding an engine = an adapter.
- Every module boundary is checked on every change; a wrong dependency fails CI, not code review.
- The container names follow the platform name: images `cemkit-ci` and `cemkit-dev`, `/opt/cemkit`,
  `CEMKIT_TARGET`. The protected hook's bypass variable stays `FANCEM_ALLOW_PROTECTED` until a maintainer
  edits `.claude/hooks/protect_files.sh`.
- The Python virtual environment is shared between host tools (the edit hook's ruff) and the container;
  they can recreate `.venv` for each other. Harmless, but slow; revisit if it becomes a nuisance.

# ADR-008: CI execution

- Status: accepted
- Date: 2026-10-09
- Requirements affected: MAINT-001, MAINT-002, MAINT-003, SEC-001, REPRO-002

## Context
CI must run `scripts/check.sh` inside the pinned toolchain. The full dev image is about 4 GB because of
OpenFOAM, CalculiX and Gmsh, none of which the T02 checks need. Private container storage for a multi-GB image
is likely to cost money, and there is no nightly CFD yet.

## Options considered
1. Push the dev image to a registry — storage cost, extra credentials, not needed yet.
2. Build the full dev image in every CI run — slow and wasteful.
3. Add a `ci` Docker target without the solvers, built in CI with BuildKit's GitHub Actions cache.

## Decision
Option 3. Dockerfile stages are `base`, `toolchain`, `ci` (toolchain only) and `dev` (toolchain plus solvers).
CI builds `ci`. No registry; revisit at M3 when nightly CFD needs the solver image.

## Workflow rules
- Job named `check` (branch protection depends on the name).
- Runner `ubuntu-24.04`, never `ubuntu-latest`.
- Every action pinned by full commit SHA.
- `permissions: contents: read`, a job timeout, and concurrency that cancels superseded runs.
- vcpkg binary cache keyed on `vcpkg.json`, the baseline, the triplet and the compiler.
- Triggers: `pull_request` and push to `main`.

## Consequences
- CI never exercises OpenFOAM, CalculiX or Gmsh; those checks stay local until M3.
- Sanitizer builds use many Actions minutes; caching keeps this manageable. Check the plan's included
  minutes for private repositories.
- A change to the toolchain layers rebuilds the `ci` image once per cache miss.

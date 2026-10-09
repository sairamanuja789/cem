# Fan CEM — instructions for Claude Code

A Computational Engineering Model (CEM) that turns a reference image plus stated
requirements into a fan design whose performance is computed, checked and traceable.
First product: 120 mm ducted axial fan. Later: wall, pedestal, ceiling, centrifugal fans.

## Sources of truth — read the relevant parts before every task
- `docs/requirements.md` — the requirements spec. Requirement IDs (e.g. SPEC-004) are binding.
- `docs/architecture.md` — the architecture plan.
- `docs/build-plan.md` — ordered tasks. Work on exactly one task at a time.
- `docs/adr/` — architecture decision records. Do not contradict an accepted ADR.

If code and the spec disagree, the spec wins. If the spec is ambiguous, stop and ask.

## Architecture rules (never violate)
1. Layers: kernel (C++23, `kernel/`) ← bindings (nanobind + C ABI, `bindings/`) ← orchestration
   (Python, `python/cemkit/`). Solvers (OpenFOAM, CalculiX, Gmsh) are external processes.
   The kernel never calls Python, the network, or a solver.
2. Placement: code that defines engineering truth or runs once per candidate goes in the kernel.
   Code that talks to people, files, services or schedules work goes in Python.
3. The Python–kernel boundary is coarse: a spec and a batch of candidates go in, fidelity-labelled
   results come out. No fine-grained calls into kernel objects from Python.
4. SI units inside. Strong unit types (mp-units) in the kernel. Unit conversion happens only in the
   spec compiler, and the original value + unit is recorded.
5. Every reported quantity carries a fidelity label (L0, L1, L2 simulated, L2 verified, L3 validated).
6. Physics functions are pure, deterministic, and return `std::expected<T, Error>`. Outside a model's
   validity range they return an out-of-validity error, never a number.
7. Never invent engineering constants, coefficients, limits or ranges. Every empirical value needs a
   cited source in the code comment and in `docs/models/`. If no source is known, mark it `UNSOURCED`
   and make the system report it. Ask the user for the source.
8. Simulation results that have not passed the trust gate are never ranked or reported as numbers.
9. Never write "optimal", "perfect", "validated" or "certified" in outputs or reports unless the
   evidence required by the spec exists (REP-004).
10. LLM output is untrusted data: validate against a schema, never execute it or use it as a path.

## How to work
- Use `/task <ID>` (see `.claude/commands/task.md`). Restate the task's requirement IDs and acceptance
  criteria, propose a plan, and wait for approval before editing files.
- Tests first. Write failing tests that encode the acceptance criteria, then implement.
- Physics: write the Python reference in `python/cemkit/reference/` first, then port to C++.
  Both must agree to floating-point tolerance.
- Never weaken, skip, delete or loosen a test to make it pass. If a test looks wrong, stop and explain.
- Protected files (enforced by a hook): `docs/requirements.md`, `tests/hand_calcs/verified/`,
  `.claude/settings.json`, `.claude/hooks/`. Propose changes in your reply instead.
- New hand calculations go to `tests/hand_calcs/proposed/` with the full working shown.
  A human checks them and moves them to `verified/`.
- No new dependencies without asking (LIFE-002).
- Small diffs. One logical change per commit. Commit messages start with requirement IDs:
  `SPEC-004: reject pressure without a stated type`.
- Run the physics-reviewer subagent after any change to physics, units, plugins or reference models.
- Before saying a task is done, run `scripts/check.sh` and report the real output, including failures.

## Commands (created in T01–T02; keep this list current)
- `scripts/dev.sh` — open a shell in the pinned dev container
- `scripts/check.sh` — everything CI runs: builds, tests, sanitizers, lint, types
- `cmake --preset gcc-debug && cmake --build --preset gcc-debug -j 6 && ctest --preset gcc-debug`
- `cmake --preset clang-asan && cmake --build --preset clang-asan -j 6 && ctest --preset clang-asan`
- `uv run pytest`
- `uv run ruff check . && uv run mypy --strict python/`

Machine: 16 GB RAM. Never build C++ with more than `-j 6`; OpenCascade builds can run out of memory.

## Repository map
Full layout and dependency rules: `docs/repository-structure.md` (ADR-009). Platform name: cemkit.
- `kernel/cemkit/` platform modules (core, spec, physics, product, geometry/mesh/simulation ports and
  adapters, capi); `kernel/products/<product>/` product domains (fans first); `kernel/testing/` shared tests
- `bindings/python/` nanobind module `cemkit._kernel`
- `python/cemkit/` CLI, orchestration, store, intake, reporting, reference implementations; product glue
  in `python/cemkit/products/<product>/`
- `schemas/` JSON Schemas (single source of truth for spec, candidate, result)
- `data/` sourced engineering data (e.g. family specific-speed ranges), each value with a citation
- `docs/` requirements, architecture, build plan, ADRs, model pages
- `tests/` Python and integration tests; `tests/hand_calcs/` human-verified expected values

## Definition of done for any task
- All acceptance criteria in the task are met and demonstrated.
- `scripts/check.sh` passes with no new warnings.
- Tests reference the requirement IDs they verify (Catch2 tags `[SPEC-004]`, pytest `@pytest.mark.req("SPEC-004")`).
- `docs/models/` updated for any physics change; ADR written for any architectural choice.

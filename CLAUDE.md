# CEM platform — instructions for Claude Code

A Computational Engineering Model (CEM) platform that turns a reference image plus stated
requirements into a design whose performance is computed, checked and traceable.
First product: fans — 120 mm ducted axial fan. Later: wall, pedestal, ceiling, centrifugal fans.
The platform core names no product (ADR-015).

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

## Platform and engineering design rules (ADR-015; engine research 2026-10-10)
Layering (ADR-015):
- The platform core (`kernel/cemkit/`, platform Python) names no product or family. Families own
  their fields (`Family::field_catalogue()`, which generates the family JSON schema), spec checks,
  feasibility report, physics, recipes and simulation cases.
- Two-user rule: no shared abstraction (geometry operation, domain library, solver contract) until
  two real users need it. Stub families are test fixtures, not users.
- Physics are pure-function libraries, not plugins, and never include geometry or adapter code: they
  take numbers (chord, radius), never shapes. No `kernel/domains/` until a second family needs a model.
- Abstraction layers, depending downward only: math primitives → quantities/units → geometry (via the
  port) → governing equations and constitutive models → numerical methods / solver adapters →
  constraints and objectives → product design algorithms → verification and validation.

Concepts adopted from NX / Simcenter / ANSYS (public product concepts, not copied implementations;
not cited sources):
- Separate stages with explicit hand-offs: geometry → mesh → solve → post-process. Each stage gets
  typed inputs, never another stage's internals.
- History = rebuild: a geometry recipe is an ordered list of typed port operations; regenerating it
  from parameters is the feature history. No feature tree, no CAD constraint solver — recipes compute
  positions explicitly from engineering parameters.
- Select edges and faces by geometric rule (e.g. blade–hub intersection edges), never by index, and
  attach named groups that carry through to meshing and boundary conditions.
- Stale-aware pipeline (like a project schematic): each stage is keyed by a content hash of its
  inputs (recipe id@version, parameter values, backend version); reuse when unchanged, invalidate
  when any input changes. Stop at the first question, invalid input, out-of-validity or infeasible
  result.
- Multi-fidelity exploration: screen with L0/L1, explore with design of experiments plus surrogates,
  run L2 only on finalists. Rank only at the fidelity actually reached (rule 8).
- Verification as in solver verification manuals: compare against analytic cases; use manufactured
  solutions and mesh-convergence studies for solver adapters.
- Do not build: a geometric constraint solver, a PDE/CFD/FEA solver, adjoint sensitivities or
  reduced-order models, until a measured bottleneck justifies them.

Mathematics and numerics:
- Prefer established libraries over hand-written numerics (new dependency → ask, LIFE-002): OCCT for
  B-splines, lofts, booleans, fillets, meshing and mass properties; external CalculiX, OpenFOAM and
  Gmsh for field problems; Eigen or SciPy only when a real user appears.
- Small, justified internal numerics only: frames and rigid transforms at the port boundary,
  bracketed root finding (Brent) for scalar implicit relations, Gauss–Legendre spanwise integration.
- Conditioning: optimise in dimensionless, scaled variables (φ, ψ, ωₛ for fans); QR rather than
  normal equations for least squares; re-orthonormalise composed rotations; integrate in r/R.
- Every iterative method has a tolerance and an iteration cap; non-convergence is an error, never a
  number.
- Optimiser choice follows the problem: gradient-based (SQP, analytic gradients, multi-start) for
  smooth cheap L0/L1; enumeration or evolutionary for discrete choices; Pareto (NSGA-II type) for
  several objectives; Bayesian or surrogate search when L2 cost dominates.

Physics and verification:
- Keep fundamental laws (conservation, Euler turbomachine equation, actuator-disk momentum,
  equilibrium, linear elasticity, energy balance) separate from empirical correlations (losses,
  deviation, low-Re section data, Nusselt/friction correlations, allowables). Every correlation is
  cited or `UNSOURCED`.
- Every model records: id@version, typed inputs and outputs, method (closed form | iterative with
  tolerance | solver with settings), validity predicates with sources, verification evidence, and a
  fidelity-labelled output. No universal `Model` base class.
- Errors stay distinct: missing input (question), invalid input, out of validity, infeasible,
  non-convergence, not implemented.
- A geometrically valid solid proves nothing physical: geometry validity and performance are separate
  checks and are reported separately.
- Verification kinds stay distinct in reports: code verification (hand calcs, Python ↔ C++),
  numerical verification (convergence, mesh independence), model validation (published measured
  data), physical validation (our rig, L3).
- Never claim a performance improvement without a baseline, a benchmark and a measurement; until
  measured it is a hypothesis.
- Known applicability gap: `scale_fan_laws` assumes the same Reynolds regime but does not check it;
  size scaling at low Reynolds numbers (120 mm fans) is where it is least valid.

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
- `ctest --preset gcc-debug -R fans_l0_crosscheck` — kernel L0 vs the Python reference (PHY-005)
- `cmake --build --preset release -j 6` also builds `cemkit._kernel` into `python/cemkit/` (needed by
  `uv run pytest`); `uv build --wheel` builds the scikit-build-core wheel (ADR-012)
- `PYTHONPATH=python uv run python scripts/bench_l0_batch.py` — batch API benchmark (docs/capi.md)
- `scripts/cemkit <command>` — the CLI (`spec compile`, `spec questions`, `feasibility`,
  `geometry smoke`, `runs show`, `runs reproduce`; docs/cli.md)

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

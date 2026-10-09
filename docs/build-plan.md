# Fan CEM — Build plan for the CEM base

Each task is sized for one Claude Code session. Run it with `/task T04` (for example).
Tasks are in dependency order; T03 can run in parallel with T02–T05.

ADR numbering: 000 hardware budget (T03) · 001 language and stack · 002 v1 scope and validation target ·
003 requirement semantics (002 and 003 are defined in `docs/architecture.md` section 12) ·
004 OpenCascade backend (T10) · 005 job resource limits (T13) · 006 toolchain pins and OpenFOAM version (T01) ·
007 compilers, standard library and warnings (T01/T02) · 008 CI execution (T02) ·
009 repository structure (docs/repository-structure.md) · 010 reserved: wrapper type for pressure kinds, if needed.
Requirement IDs refer to `docs/requirements.md`. "Human" marks steps Claude Code cannot do for you.

The base is done when every box in **T15** is ticked. The ducted axial L1 model, CFD, optimizer
and vision intake come after the base, in a later plan.

---

## T00 — Setup (human)

- Install Ubuntu 24.04, Git, Docker, `jq`, and Claude Code.
- Create the repository `fan-cem/` and copy this kit into it.
- Run `chmod +x .claude/hooks/*.sh` (hooks must be executable).
- Export the Claude Doc "Fan CEM — Requirements Specification v1" as Markdown to `docs/requirements.md`.
- Export the Claude Doc "Fan CEM — Production-Grade Architecture & Implementation Plan" as Markdown to `docs/architecture.md`.
- `git init`, first commit, push to a private GitHub or GitLab repository.
- In Claude Code, run `/memory` and confirm the root `CLAUDE.md` is loaded.

**Done when:** `claude` starts in the repo, `/task` is listed, and editing `docs/requirements.md` through Claude is blocked.

---

## T01 — Pinned development container

**Requirements:** REPRO-002, PORT-001, LIFE-001

**Deliverables**
- `docker/Dockerfile`: Ubuntu 24.04; GCC 13 and Clang 20 (+ clang-format, clang-tidy, libclang-rt, llvm for the sanitizer symbolizer); CMake ≥ 3.28; Ninja;
  ccache; vcpkg (pinned commit); uv; Python 3.13 (uv-managed); OpenFOAM v2512 (final release, official
  apt repository, 2512.0-2); CalculiX; Gmsh 4.15.2 (pip wheel, installed once); jq. Named stages
  `base`, `toolchain`, `ci` (toolchain only, no solvers; ADR-008) and `dev` (toolchain plus solvers); OpenFOAM in its own layer. Base image pinned by digest; Ubuntu apt
  packages installed from one fixed Ubuntu snapshot timestamp.
- One pins file read by the Dockerfile and checked by `scripts/versions.sh` (no pin lives anywhere else;
  T02 reuses the Gmsh install rather than pinning a second copy).
- `scripts/dev.sh` (start a shell in the container with the repo mounted), `scripts/versions.sh`
  (print every tool version), `docs/toolchain.md` (generated version table; log each snapshot refresh here).

**Done when**
- `docker build` succeeds from a clean cache.
- `scripts/versions.sh` prints all versions and they match the Dockerfile pins.
- The image digest is recorded in `docs/toolchain.md`.

**Notes:** OpenFOAM is v2512 (see `docs/adr/006-toolchain-pins-openfoam-version.md`): v2606's apt packages
are release-candidate only. Upgrading to v2606 is a deliberate later change. Pin rule: nothing published in
the last 14 days at pin time. Verify Gmsh headless inside the container (`gmsh -version` and
`python -c "import gmsh; gmsh.initialize()"`), adding only system libraries proven missing.
Report the final image size.

---

## T02 — Build skeleton, quality gates and CI

**Requirements:** MAINT-001, MAINT-002, MAINT-003, SEC-001, REPRO-002
**Decisions:** ADR-007 (compilers, standard library, warnings), ADR-008 (CI execution)

**Deliverables**
- `CMakeLists.txt` (minimum 3.28, Ninja, `CMAKE_CXX_STANDARD 23` required, extensions off,
  `compile_commands.json`, ccache launcher, vcpkg toolchain) and `CMakePresets.json` with presets
  `gcc-debug`, `clang-debug`, `clang-asan` (`-fsanitize=address,undefined -fno-omit-frame-pointer
  -fno-sanitize-recover=all`, halt on error), `gcc-coverage` (`--coverage`, for gcovr) and `release`
  (GCC, `-O2`, tests run). GCC 13 and Clang 20, both on libstdc++ 13.
- Warning flags on project targets only (third-party headers as SYSTEM): `-Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough`. A flag that proves unworkable is
  removed only with a written reason in ADR-007.
- `vcpkg.json` with `builtin-baseline` equal to `VCPKG_COMMIT` in `docker/pins.env`: mp-units, nlohmann-json,
  catch2. (OpenCascade is added in T10.) If the pinned port of mp-units fails with GCC 13 or Clang 20,
  stop and report; feature options may change, the version may not.
- Empty kernel library with one trivial Catch2 test; a toolchain test compiling a `std::expected` example under
  every preset; `.clang-format` (100 columns) and `.clang-tidy` (bugprone, performance, modernize, selected
  cppcoreguidelines, readability without magic-numbers; every disabled check commented; WarningsAsErrors;
  project sources only).
- `pyproject.toml` + `uv.lock`: `requires-python ">=3.13,<3.14"`; pytest, pytest-cov, hypothesis,
  jsonschema, typer, ruff, mypy, gcovr; `uv sync --locked`; hatchling backend for now (T11 moves to
  scikit-build-core); package `python/cemkit` with `py.typed`. Ruff line length 100, rules E, F, W, I, B, UP,
  SIM, RUF, `ruff format --check`; mypy strict; pytest `--strict-markers --strict-config`, warnings as
  errors, marker `req` registered. Reuse the Gmsh install from T01; no second pin.
- Gates that prove they work (negative tests tagged with their requirement ID): an unused variable fails to
  compile under the project flags; under `clang-asan` a deliberate heap-buffer overflow is caught; gitleaks
  detects a fake secret built by concatenation at test runtime in a temp directory (never commit a
  secret-like string).
- Coverage (MAINT-003): gcovr reports on the MAINT-003 scope (`kernel/cemkit/{core,spec,physics}`, `kernel/products/*/common`, each family's L1 model) with a 90% floor; the floor
  applies once those directories contain code. In T02, show that the report runs. No trivial code to game it.
- `scripts/check.sh`: refuses to run outside the container; runs every stage even after a failure; prints a
  pass/fail summary; exits non-zero if anything failed. Stages: all presets (configure, build, ctest),
  clang-tidy, clang-format check, ruff, ruff format check, mypy, pytest with coverage, gcovr report, gitleaks,
  the negative tests, and the REPRO-002 check (vcpkg baseline equals the pinned SHA; `uv lock --check`).
- `.github/workflows/ci.yml`: job named `check`; runner `ubuntu-24.04`; every action pinned by full commit SHA;
  `permissions: contents: read`; a timeout; concurrency cancelling superseded runs; builds the `ci` image
  target (no OpenFOAM or CalculiX) with BuildKit's GitHub Actions cache; caches vcpkg binaries keyed on
  `vcpkg.json`, baseline, triplet and compiler; triggers `pull_request` and push to `main`.
  No registry until M3 (nightly CFD needs the solver image).

**Done when:** PR 1 with T02 goes green and is merged. PR 2 adds a deliberate warning, CI goes red, the run
link is recorded in the T02 report, and PR 2 is closed unmerged.

---

## T03 — Hardware budget measurement (human + Claude)

**Requirements:** ASM-001, RES-001, RES-002

**Deliverables**
- `scripts/measure_openfoam_memory.sh`: runs an OpenFOAM MRF tutorial at 3 mesh sizes and records cells,
  peak RAM (`/usr/bin/time -v`) and wall time on 4 MPI ranks.
- `docs/adr/000-hardware-budget.md` with the measured RAM per million cells and the chosen job memory cap.

**Done when:** the ADR contains measured numbers from your laptop, not estimates.

---

## T04 — Core types

**Requirements:** COR-001, COR-003, PHY-004

**Deliverables**
- `kernel/cemkit/core/`: `units.hpp` (mp-units aliases for the SI quantities used),
  `fidelity.hpp` (L0, L1, L2 simulated, L2 verified, L3 validated), `provenance.hpp`
  (user, image, derived, default, unknown), `error.hpp` (`Error`, error-code enum),
  `result.hpp` (`std::expected` aliases; labelled value = quantity + fidelity + model version),
  `version.hpp`.
- Compile-fail tests in `kernel/testing/compile_fail/` (e.g. adding a pressure to a flow rate); fan pressure
  kinds and their compile-fail tests in `kernel/products/fans/common/`.

**Done when:** unit and compile-fail tests pass under all presets; the error-code list is documented
in `docs/models/platform/error-codes.md` and `schemas/cemkit/v1/error-codes.json` for reuse by Python.

---

## T05 — JSON Schemas and examples

**Requirements:** SPEC-001, SPEC-004, SPEC-009, MAINT-005

**Deliverables**
- `schemas/cemkit/v1/spec.schema.json` (v1.0, ADR-009 layout): field object = value, unit, tolerance?, provenance, confidence?
  (image only), note?; shared fields from requirements section 5; family-specific extension block.
- `schemas/cemkit/v1/candidate.schema.json`, `schemas/cemkit/v1/result.schema.json`; the fan block in
  `schemas/products/fans/v1/spec.schema.json`.
- Pressure inputs are fan total or fan static pressure as defined in ISO 5801 (fan static = outlet static
  minus inlet total, i.e. total-to-static by construction); the schema rejects static-to-static.
- `examples/axial_120.yaml` matching requirements section 7 (unknown duty point left as unknown).
- Python tests validating examples and rejecting malformed ones (pressure without type, bare numbers).

**Done when:** a human has reviewed the schemas (they are the contract for everything else).

---

## T06 — Python reference implementation of L0 physics

**Requirements:** PHY-001, PHY-002, PHY-005, SEL-001

**Prerequisite:** `docs/architecture.md` must be in the repo (human, T00). The specific-speed and
specific-diameter formulas come from its section 6. Do not start T06 without it, and do not write the
formulas from memory.

**Deliverables**
- `python/cemkit/reference/` (mirroring the kernel modules: generic air properties under platform physics,
  fan laws and coefficients under `fans/common`), `l0.py`: air properties at the default state, fan laws, flow coefficient,
  pressure coefficient, specific speed, specific diameter. Each function documents assumptions,
  validity range and source.
- `docs/models/fans/l0-similarity.md` (and `docs/models/platform/air-properties.md`): equations, conventions, sources.
- `tests/hand_calcs/proposed/l0_*.yaml`: at least 5 hand-calculated cases with the working shown.

**Human step:** check the proposed hand calculations and move correct ones to `tests/hand_calcs/verified/`.

**Done when:** reference functions pass all verified hand calculations and Hypothesis property tests
(e.g. fan-law scaling is exact; doubling speed doubles flow at fixed diameter).

---

## T07 — Spec compiler (C++)

**Requirements:** SPEC-001 to SPEC-010, IN-001

**Deliverables**
- `kernel/cemkit/spec/`: parse JSON (nlohmann), validate against the
  schema rules, convert to SI with the original value and unit recorded, unit table
  (mm, m, rpm, rad/s, m³/s, m³/h, m³/min, CFM, Pa, mmH₂O, inH₂O, W), pressure-type rule,
  precedence user > image > derived > default, essential-field check (asks the family plugin;
  stubbed until T09), question generation, immutable versioning with parent link,
  contradiction checks (start with: required air power exceeds a stated power limit).
- Pressure semantics: accept only fan total or fan static pressure per ISO 5801; reject static-to-static
  with a message naming the field. "Total-to-static" is an efficiency type only, not a pressure type.

**Done when:** each SPEC requirement has at least one tagged passing test; error messages name the field.

---

## T08 — L0 physics in the kernel and feasibility gate

**Requirements:** SEL-001, SEL-002, SEL-004, PHY-001 to PHY-005 (PHY-005 for L0 only)

**Scope note:** the base has only L0 models. PHY-005 is covered here for L0 (Python reference vs kernel).
PERF-001 is written for the ducted axial L1 model and is **deferred to the axial L1 milestone**; do not
build an L1 model in T08.

**Deliverables**
- `kernel/cemkit/physics/` (generic: air properties) and `kernel/products/fans/common/` (fan laws, coefficients,
  specific speed and diameter, feasibility): port of T06 using mp-units and
  `std::expected`; out-of-range inputs return out-of-validity errors.
- `data/fans/family_ranges.yaml`: specific-speed ranges per family, each with a citation. Values without a
  verified source stay `UNSOURCED`, and the feasibility gate reports "range unsourced" for them.
  **Do not invent ranges.**
- Feasibility check returning the violated limit and the nearest feasible duty where computable.

**Human step:** supply sources for the family ranges (for example a Cordier diagram dataset from a fan textbook).

**Done when:** kernel and reference agree to floating-point tolerance on all verified cases and on
10,000 random in-range inputs; the physics-reviewer subagent approves.

---

## T09 — Family plugin system

**Requirements:** FAM-001, FAM-002, FAM-003, SPEC-006, UC-09

**Deliverables**
- `kernel/cemkit/product/family.hpp` (interface: parameter space, initial design, L1 evaluation,
  constraints, geometry recipe, simulation case, essential fields, feasible range),
  `registry.hpp` (static registration), `parameter_space.hpp` (named parameters with units, bounds, defaults).
- `kernel/testing/stub_product/` and `stub_family/`: a minimal product and family used only by tests,
  registered explicitly (`register_family(Registry&)`); a CI test fails if any folder under
  `products/*/families/` is missing from the registry.
- CI check that the stub builds and registers with no edits outside its folder.

**Done when:** the spec compiler asks the stub plugin for essential fields; FAM-002 test passes.

---

## T10 — Geometry backend (OpenCascade 8.0)

**Requirements:** GEO-001, GEO-002, GEO-004

**Deliverables**
- Add OpenCascade 8.0.x: use the vcpkg port if it already offers 8.0.x, otherwise build OCCT 8.0.1 from
  source inside the container (limit to `-j 6`). Record the choice in `docs/adr/004-opencascade-backend.md`.
- `kernel/cemkit/geometry/port/backend.hpp` (interface), `kernel/cemkit/geometry/occt/occt_backend.cpp`:
  build a test solid (cylinder hub plus a lofted plate from 3 sections), validity checks
  (closed, manifold, no self-intersection), mass properties, STEP and STL export,
  content-hash file names. All OCCT exceptions converted to `Error`.

**Done when:** the same parameters give identical volume, area and topology across reruns; an invalid
input produces `geometry_failed` with a reason; exports open in FreeCAD (human check once).

---

## T11 — C ABI and Python bindings

**Requirements:** PERF-002, MAINT-005, PHY-005

**Deliverables**
- `kernel/cemkit/capi/cemkit.h` + implementation: ABI version, spec compile, feasibility, batch L0 evaluation,
  geometry smoke; JSON in and out; `cemkit_free`; no exceptions cross the boundary.
- `bindings/python/`: nanobind module `cemkit._kernel` built with scikit-build-core (the Python project
  moves from hatchling to scikit-build-core here).
- Python tests comparing kernel batch results with the reference on 10,000 random inputs; benchmark.

**Done when:** the batch API evaluates 10,000 inputs with one Python call; results match the reference.

---

## T12 — Store

**Requirements:** STORE-001 to STORE-004, REL-001, STORE-002

**Deliverables**
- `python/cemkit/store/`: SQLite in WAL mode; tables for specs, candidates, jobs, results, failures,
  artifacts, runs; plain-SQL migrations with a small migrator; content-addressed artifact directory;
  run-metadata capture (kernel, plugin and model versions, git commit, container digest, input hash).
- Append-only enforcement (no UPDATE of result rows; status changes are new rows or a separate table).

**Done when:** migration up from empty works; killing the process mid-write leaves a consistent database;
duplicate artifacts are stored once.

---

## T13 — Job queue and worker

**Requirements:** ORC-001 to ORC-003, RES-001, REL-002, REL-003, OBS-001, SIM-002

**Deliverables**
- `python/cemkit/orchestration/`: durable job states (queued, running, done, failed) with leases and
  heartbeats; one heavy job at a time by default; retries with a recorded reason; failure codes shared
  with the kernel; resource limits for child processes (memory, cores, wall time) using cgroups via
  `systemd-run --user --scope` or `docker run` limits — decide in `docs/adr/005-job-resource-limits.md`.
- Structured JSON logs with peak RAM, CPU and wall time.
- Failure-injection tests: killed worker, out-of-memory child, timeout, disk full (simulated).

**Done when:** killing the worker mid-job and restarting resumes with no lost or duplicated completed jobs.

---

## T14 — CLI

**Requirements:** IN-001, UC-01 to UC-03, UC-08, REP-001, REP-004

**Deliverables**
- `python/cemkit/cli/` (Typer): `cemkit spec compile <file>`, `cemkit spec questions <spec-id>`,
  `cemkit feasibility <spec-id>`, `cemkit geometry smoke`, `cemkit runs show <run-id>`,
  `cemkit runs reproduce <run-id>`.
- Output always shows units and fidelity labels; a banned-claims check runs on all text output.

**Done when:** each command has an integration test and works inside the container.

---

## T15 — Base acceptance (human review)

Run every check from requirements section 8 and record the results in `docs/base-release-report.md`.

- [ ] Clean clone builds and passes all tests with one command inside the container
- [ ] CI: GCC and Clang with -Werror, clang-tidy, ASan/UBSan, ruff, mypy --strict, coverage ≥ 90% on core, spec, physics
- [ ] A unit error fails to compile (negative compile test)
- [ ] `cemkit spec compile examples/axial_120.yaml` gives a versioned spec with provenance on every field and the open questions
- [ ] A pressure without a type is rejected with a message naming the field
- [ ] `cemkit feasibility` matches the Python reference and rejects an impossible duty with the violated limit
- [ ] The stub family registers with no edits outside its folder
- [ ] `cemkit geometry smoke` builds a valid solid and exports STEP and STL with content-hash names
- [ ] Killing the worker mid-job and restarting loses and duplicates nothing
- [ ] Every run record contains versions, git commit, container digest and input hash
- [ ] ADR-001 to ADR-003 merged

---

## How to run a session well

1. Start a fresh session per task (`/clear`), then `/task T0x`.
2. Read the plan Claude proposes before approving; push back on anything that skips tests or invents data.
3. Review every diff yourself, especially physics and units. Claude writes the code; you own correctness.
4. Commit after each green task. Never merge with a failing `scripts/check.sh`.
5. When Claude asks for an engineering source or a decision, answer it in an ADR or a spec amendment,
   not only in chat, so later sessions see it.

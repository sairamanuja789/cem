# Fan CEM — Build plan for the CEM base

Each task is sized for one Claude Code session. Run it with `/task T04` (for example).
Tasks are in dependency order; T03 can run in parallel with T02–T05.

ADR numbering: 000 hardware budget (T03) · 001 language and stack · 002 v1 scope and validation target ·
003 requirement semantics (002 and 003 are defined in `docs/architecture.md` section 12) ·
004 OpenCascade backend (T10) · 005 job resource limits (T13) · 006 toolchain pins and OpenFOAM version (T01).
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
- `docker/Dockerfile`: Ubuntu 24.04; GCC 13 and Clang 18 (+ clang-format, clang-tidy); CMake ≥ 3.28; Ninja;
  ccache; vcpkg (pinned commit); uv; Python 3.13 (uv-managed); OpenFOAM v2512 (final release, official
  apt repository, 2512.0-2); CalculiX; Gmsh 4.15.2 (pip wheel, installed once); jq. Named stages
  `toolchain`, `solvers`, `dev`; OpenFOAM in its own layer. Base image pinned by digest; Ubuntu apt
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

**Deliverables**
- `CMakeLists.txt`, `CMakePresets.json` with presets `gcc-debug`, `clang-debug`, `clang-asan`
  (AddressSanitizer + UBSan), `release`; `-std=c++23 -Wall -Wextra -Wpedantic -Werror`.
- `vcpkg.json` with a pinned `builtin-baseline`: mp-units, nlohmann-json, catch2.
  (OpenCascade is added in T10.)
- Empty `libfancem` with one trivial Catch2 test; `.clang-format`, `.clang-tidy`.
- `pyproject.toml` + `uv.lock`: pytest, hypothesis, jsonschema, typer, ruff, mypy; pytest marker `req` registered.
- `scripts/check.sh` running: both compilers, the sanitizer preset, ctest, pytest, ruff, mypy --strict,
  coverage report, and a secret scan.
- `.github/workflows/ci.yml` running `scripts/check.sh` inside the T01 container.

**Done when:** CI is green on a pull request; a deliberately introduced warning fails the build.

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
- `kernel/include/fancem/core/`: `units.hpp` (mp-units aliases for the SI quantities used),
  `fidelity.hpp` (L0, L1, L2 simulated, L2 verified, L3 validated), `provenance.hpp`
  (user, image, derived, default, unknown), `error.hpp` (`Error`, error-code enum),
  `result.hpp` (`std::expected` aliases; labelled value = quantity + fidelity + model version),
  `version.hpp`.
- Compile-fail tests in `kernel/tests/compile_fail/` (e.g. adding a pressure to a flow rate).

**Done when:** unit and compile-fail tests pass under all presets; the error-code list is documented
in `docs/models/error-codes.md` for reuse by Python.

---

## T05 — JSON Schemas and examples

**Requirements:** SPEC-001, SPEC-004, SPEC-009, MAINT-005

**Deliverables**
- `schemas/spec.schema.json` (v1.0): field object = value, unit, tolerance?, provenance, confidence?
  (image only), note?; shared fields from requirements section 5; family-specific extension block.
- `schemas/candidate.schema.json`, `schemas/result.schema.json`.
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
- `python/fancem/reference/l0.py`: air properties at the default state, fan laws, flow coefficient,
  pressure coefficient, specific speed, specific diameter. Each function documents assumptions,
  validity range and source.
- `docs/models/l0-similarity.md`: equations, conventions, sources.
- `tests/hand_calcs/proposed/l0_*.yaml`: at least 5 hand-calculated cases with the working shown.

**Human step:** check the proposed hand calculations and move correct ones to `tests/hand_calcs/verified/`.

**Done when:** reference functions pass all verified hand calculations and Hypothesis property tests
(e.g. fan-law scaling is exact; doubling speed doubles flow at fixed diameter).

---

## T07 — Spec compiler (C++)

**Requirements:** SPEC-001 to SPEC-010, IN-001

**Deliverables**
- `kernel/include/fancem/spec/` and `kernel/src/spec/`: parse JSON (nlohmann), validate against the
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
- `kernel/include/fancem/physics/` and `kernel/src/physics/`: port of T06 using mp-units and
  `std::expected`; out-of-range inputs return out-of-validity errors.
- `data/family_ranges.yaml`: specific-speed ranges per family, each with a citation. Values without a
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
- `kernel/include/fancem/family/plugin.hpp` (interface: parameter space, initial design, L1 evaluation,
  constraints, geometry recipe, simulation case, essential fields, feasible range),
  `registry.hpp` (static registration), `parameter_space.hpp` (named parameters with units, bounds, defaults).
- `kernel/plugins/stub_family/`: a minimal family used only by tests.
- CI check that the stub builds and registers with no edits outside its folder.

**Done when:** the spec compiler asks the stub plugin for essential fields; FAM-002 test passes.

---

## T10 — Geometry backend (OpenCascade 8.0)

**Requirements:** GEO-001, GEO-002, GEO-004

**Deliverables**
- Add OpenCascade 8.0.x: use the vcpkg port if it already offers 8.0.x, otherwise build OCCT 8.0.1 from
  source inside the container (limit to `-j 6`). Record the choice in `docs/adr/004-opencascade-backend.md`.
- `kernel/include/fancem/geometry/backend.hpp` (interface), `kernel/src/geometry/occt_backend.cpp`:
  build a test solid (cylinder hub plus a lofted plate from 3 sections), validity checks
  (closed, manifold, no self-intersection), mass properties, STEP and STL export,
  content-hash file names. All OCCT exceptions converted to `Error`.

**Done when:** the same parameters give identical volume, area and topology across reruns; an invalid
input produces `geometry_failed` with a reason; exports open in FreeCAD (human check once).

---

## T11 — C ABI and Python bindings

**Requirements:** PERF-002, MAINT-005, PHY-005

**Deliverables**
- `kernel/capi/fancem.h` + implementation: ABI version, spec compile, feasibility, batch L0 evaluation,
  geometry smoke; JSON in and out; `fancem_free`; no exceptions cross the boundary.
- `bindings/python/`: nanobind module `fancem._kernel` built with scikit-build-core.
- Python tests comparing kernel batch results with the reference on 10,000 random inputs; benchmark.

**Done when:** the batch API evaluates 10,000 inputs with one Python call; results match the reference.

---

## T12 — Store

**Requirements:** STORE-001 to STORE-004, REL-001, STORE-002

**Deliverables**
- `python/fancem/store/`: SQLite in WAL mode; tables for specs, candidates, jobs, results, failures,
  artifacts, runs; plain-SQL migrations with a small migrator; content-addressed artifact directory;
  run-metadata capture (kernel, plugin and model versions, git commit, container digest, input hash).
- Append-only enforcement (no UPDATE of result rows; status changes are new rows or a separate table).

**Done when:** migration up from empty works; killing the process mid-write leaves a consistent database;
duplicate artifacts are stored once.

---

## T13 — Job queue and worker

**Requirements:** ORC-001 to ORC-003, RES-001, REL-002, REL-003, OBS-001, SIM-002

**Deliverables**
- `python/fancem/orchestration/`: durable job states (queued, running, done, failed) with leases and
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
- `python/fancem/cli.py` (Typer): `fancem spec compile <file>`, `fancem spec questions <spec-id>`,
  `fancem feasibility <spec-id>`, `fancem geometry smoke`, `fancem runs show <run-id>`,
  `fancem runs reproduce <run-id>`.
- Output always shows units and fidelity labels; a banned-claims check runs on all text output.

**Done when:** each command has an integration test and works inside the container.

---

## T15 — Base acceptance (human review)

Run every check from requirements section 8 and record the results in `docs/base-release-report.md`.

- [ ] Clean clone builds and passes all tests with one command inside the container
- [ ] CI: GCC and Clang with -Werror, clang-tidy, ASan/UBSan, ruff, mypy --strict, coverage ≥ 90% on core, spec, physics
- [ ] A unit error fails to compile (negative compile test)
- [ ] `fancem spec compile examples/axial_120.yaml` gives a versioned spec with provenance on every field and the open questions
- [ ] A pressure without a type is rejected with a message naming the field
- [ ] `fancem feasibility` matches the Python reference and rejects an impossible duty with the violated limit
- [ ] The stub family registers with no edits outside its folder
- [ ] `fancem geometry smoke` builds a valid solid and exports STEP and STL with content-hash names
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

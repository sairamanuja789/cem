# Base release report (T15)

- Date: 2026-10-10
- Commit under test: `b286c060e228a0520912b4d9e1a10cbe3f51ef49` (main after PR #19)
- Where: a fresh clone (`git clone`, no build trees or caches carried over), checks run inside the
  pinned dev container (`scripts/dev.sh`, image `sha256:06de9d23b3355baa16bc63215498593b907aedb8f2943e3e857d383fe41f2b2d`)
- CLI: `scripts/cemkit` (runs `python -m cemkit.cli` with `python/` on the path, ADR-013). The
  `cemkit` script itself is installed only by the wheel.
- Scope: the T15 checklist in `docs/build-plan.md` (requirements section 8 acceptance criteria).

## Summary

| # | Check | Result |
|---|-------|--------|
| 1 | Clean clone builds and passes all tests with one command inside the container | PASS (see note: run in parts because the disk ran out) |
| 2 | CI: GCC and Clang with -Werror, clang-tidy, ASan/UBSan, ruff, mypy --strict, coverage ≥ 90% on core, spec, physics | PASS |
| 3 | A unit error fails to compile (negative compile test) | PASS |
| 4 | `cemkit spec compile examples/axial_120.yaml` gives a versioned spec with provenance on every field and the open questions | PASS |
| 5 | A pressure without a type is rejected with a message naming the field | PASS |
| 6 | `cemkit feasibility` matches the Python reference and rejects an impossible duty with the violated limit | PASS |
| 7 | The stub family registers with no edits outside its folder (FAM-002) | PASS |
| 8 | `cemkit geometry smoke` builds a valid solid and exports STEP and STL with content-hash names | PASS |
| 9 | Killing the worker mid-job and restarting loses and duplicates nothing | PASS |
| 10 | Every run record contains versions, git commit, container digest and input hash | PASS |
| 11 | ADR-001 to ADR-003 merged | FAIL: ADR-001 and ADR-002 do not exist |

10 PASS, 1 FAIL.

## Evidence

### 1. Clean clone, one command (`scripts/check.sh`)

`scripts/check.sh` was started on the fresh clone with an empty vcpkg binary cache. The host disk
filled up during the gcc-coverage stage (`No space left on device` while vcpkg installed catch2;
an environment failure, not a code failure). After freeing space (deleting only this clone's vcpkg
buildtrees and packages), the stages the first run had not reached were run with the same commands
in the same container, deleting each build tree when it was no longer needed. Two stages
(clang-tidy and the clang-asan half of the negative tests) were left out of that second part by
mistake and were run in a third part after the disk was freed. Every stage of `scripts/check.sh` has
therefore passed on this commit, but not in a single invocation.

Part 1 (one `scripts/check.sh` invocation):

| Stage | Result |
|---|---|
| toolchain pins | PASS: `versions.sh: all pins match` (vcpkg commit 9e593bb1, snapshot 20260920T000000Z) |
| REPRO-002 | PASS: vcpkg baseline matches the pin; `uv lock --check` and `uv sync --locked` clean; OpenCascade 8.0.0 built from source in 9.2 min |
| gcc-debug | PASS: 209/209 tests |
| clang-debug | PASS: 209/209 tests |
| clang-asan | PASS: 210/210 tests |
| gcc-coverage | not completed: disk full |

Part 2 (same commands, remaining stages):

| Stage | Result |
|---|---|
| gcc-coverage | PASS: 209/209 tests |
| gcovr (MAINT-003) | PASS: lines 92.3% (1288/1395), functions 99.4% (180/181), branches 51.8% (1812/3498); floor `--fail-under-line 90` |
| release | PASS: 209/209 tests |
| boundaries (ADR-009) | PASS: 12 kernel modules, 0 violations |
| clang-format | PASS |
| ruff check | PASS: `All checks passed!` |
| ruff format | PASS: 94 files already formatted |
| mypy --strict | PASS: no issues in 54 source files |
| pytest + coverage | PASS: 372 passed, 94% Python coverage |
| gitleaks (SEC-001) | PASS: no leaks found (directory and git history) |
| negative tests (gcc) | PASS: gcc-debug `negative_*` 1/1, `tests/python/test_gitleaks.py` 2 passed |
| wheel (ADR-013) | PASS: wheel contains `cemkit/_kernel.cpython-313-x86_64-linux-gnu.so` and `cemkit/kernel.py` |

Part 3 (stages left out of part 2):

| Stage | Result |
|---|---|
| clang-tidy | PASS: `run-clang-tidy -p build/clang-debug` over 50 kernel sources, 0 diagnostics in project code, exit 0 |
| negative tests (clang-asan) | PASS: 2/2 (`negative_unused_variable_fails_to_compile`, `negative_heap_overflow_is_caught`) |

### 2. CI

The `ci` workflow passed on this commit (push to main, run
https://github.com/sairamanuja789/cem/actions/runs/38062823904) and on the PR #19 head `3c025bf`.
Locally, the same gates passed in item 1: GCC 13 and Clang 20 builds with warnings as errors
(ADR-007), clang-tidy, ASan/UBSan (clang-asan), ruff, mypy --strict, and line coverage of
kernel/cemkit/{core,spec,physics} and products/*/common at 92.3% (floor 90%).

### 3. Negative compile tests

`ctest --test-dir build/gcc-debug -R 'compile_fail|^negative_'`: 100% tests passed, 0 tests failed
out of 25 (24 `compile_fail.*` control/rejected pairs, labels COR-001 and COR-003, among them
`compile_fail.fans_static_passed_as_total.rejected`, `compile_fail.angle_as_ratio.rejected`,
`compile_fail.labelled_without_fidelity.rejected`; plus `negative_unused_variable_fails_to_compile`).
Each `.control` compiles and each `.rejected` fails to compile.

### 4. `cemkit spec compile examples/axial_120.yaml`

Exit 0. Output (abridged):

```text
spec axial-120 revision 1 (family fans.axial_ducted): compiled, run 1
fields (value as given = SI value; provenance; inputs carry no fidelity label):
  air.density: 1.18 kg/m3 = 1.18 kg/m3  [default]
  envelope.depth_max: 40 mm = 0.04 m  [default, provisional]
  manufacturing.process: FDM  [user]
  product.duty.flow: unknown  [unknown]
  product.duty.pressure: unknown  [unknown]
  product.nominal_size: 120 mm = 0.12 m  [user]
  product.rotational_speed: 2000 rpm = 209.43951023931953 rad/s  [default, provisional]
  ... (22 fields, each with provenance)
open questions (2):
  product.duty.flow [m3/s]: Essential duty point flow rate (AX-003).
  product.duty.pressure [Pa]: Essential duty point pressure rise and kind (AX-004).
not runnable yet: answer the essential questions (SPEC-007)
```

The spec is recorded with id, revision and sha256 (`axial-120 revision 1 sha256 4d54b390…`, item 10).

### 5. Pressure without a type

The example with a duty point of 120 Pa and the `kind` key removed:

```text
rejected: spec_rejected at product.duty.pressure: pressure must state its kind (fan_total or fan_static)
```

Exit 1.

### 6. Feasibility

Parity with the Python reference: `pytest tests/python/test_cli.py -k feasibility
tests/python/test_kernel_bindings.py tests/python/test_reference_feasibility.py`: 20 passed
(includes `test_feasibility_matches_the_reference_and_reports_range_unsourced` and
`test_one_call_evaluates_10000_states_and_matches_the_reference`).

Feasible duty (0.05 m3/s, 120 Pa fan static), exit 0:

```text
  fan_total_pressure: 131.53153903336792 Pa [L0 predicted, fans.l0@1.0.0] derived: ISO 5801: ...
specific speed: 1.3651572974796444 (1) [L0 predicted, fans.l0@1.0.0]
  incompressible_pressure: pass: within EPSILON gamma p1
  incompressible_tip_speed: pass: within sqrt(2 EPSILON) a1
  family_specific_speed_range: range_unsourced: range unsourced: no cited specific-speed range for fans.axial_ducted
verdict: unconfirmed: no limit violated, but at least one check could not be applied
```

Impossible duty (0.05 m3/s, 2000 Pa fan total), exit 2:

```text
  incompressible_pressure: violated: fan total pressure exceeds the incompressible limit EPSILON gamma p1 (ADR-003 D3)
    limit violated: out_of_validity at fan_total_pressure: ... (bounds=(0, 1418.55], model=fans.l0@1.0.0, value=2000)
    nearest feasible duty for this limit: flow 0.05 m3/s [L0 predicted, fans.feasibility@1.0.0], fan total pressure 1418.55 Pa [L0 predicted, fans.feasibility@1.0.0]
verdict: infeasible: a limit is violated
```

### 7. Stub family (FAM-002)

`ctest -R 'stub|family'` in gcc-debug: all passed, exit 0 (stub_product 3 tests, stub_family 4,
family_registry 1, product registry 1, among others). `pytest tests/python/test_family_isolation.py`:
4 passed (`test_a_family_needs_no_edit_outside_its_folder`, and a negative case showing that a
family named by the platform or a build file is caught).

### 8. Geometry smoke

`cemkit geometry smoke --out <dir>`, exit 0:

```text
geometry smoke (occt backend, test parameters): run 6
validity: ok (closed True, manifold True, self-intersection free True)
topology: 1 solid, 8 faces, 15 edges
  b245ad86d7b13225ea0f749fa3374c0e973e4aec7ebd7857253902dd6bd173c8.step  32550 bytes
  f63adecc311aab335702beb9c488dd5b9f36e5f5fd0be1716ccd82832613bdc5.stl  24084 bytes
```

`sha256sum` of each file equals its name. Both files are byte-identical to the copies already in
`/home/sai/Desktop/cem-geometry/` from the earlier run with the same parameters, so the export is
reproducible. `cemkit runs reproduce 6`: `reproduced, identical output`.

### 9. Worker kill

`pytest tests/python/test_orchestration.py -k "kill or dies"`: 6 passed, including
`test_killing_the_worker_mid_job_loses_and_duplicates_no_completed_job` (SIGKILL of the worker
mid-job, restart, every job done exactly once) and
`test_a_worker_that_dies_while_publishing_is_resumed_without_rerunning`.

### 10. Run records

`cemkit runs show 1` … `6`. Every run has all fields filled (`unknown fields: none`), for example:

```text
run 1 recorded 2026-10-10T16:24:25.795469+00:00
  kernel version: 0.0.0
  plugin versions: {"fans.axial_ducted":"0.1.0"}
  model versions: {"fans.feasibility":"1.0.0","fans.l0":"1.0.0","platform.air":"1.0.0"}
  git commit: b286c060e228a0520912b4d9e1a10cbe3f51ef49
  container digest: sha256:06de9d23b3355baa16bc63215498593b907aedb8f2943e3e857d383fe41f2b2d
  input hash: 79b0250cbc671a2db6e4c84ef7fb2d0c9f335512700f43e4c7b30f971d6bd980
```

### 11. ADRs

| ADR | Status |
|---|---|
| ADR-000 hardware budget | accepted |
| ADR-001 core language and stack | **does not exist** |
| ADR-002 v1 scope and validation target | **does not exist** |
| ADR-003 requirement semantics | accepted (D1–D7); D8 proposed |
| ADR-004 OpenCascade backend | proposed |
| ADR-005 job resource limits | proposed |
| ADR-006 toolchain pins, OpenFOAM version | accepted (OpenFOAM version); pin values proposed |
| ADR-007 compilers, stdlib, warnings | accepted |
| ADR-008 CI execution | accepted |
| ADR-009 repository structure | accepted |
| ADR-010 store schema | proposed |
| ADR-011 feasibility gate | proposed |
| ADR-012 family plugin interface | proposed |
| ADR-013 C ABI and Python binding | proposed |
| ADR-014 CLI, runs and claims | proposed |

ADR-001 and ADR-002 are defined in `docs/architecture.md` section 12 and the language decision is in
requirements section 3, but neither ADR file was ever written. Item 11 therefore fails.

## Open items

1. Write ADR-001 (core language and stack: C++23 kernel with Python orchestration, per requirements
   section 3) and ADR-002 (v1 scope and validation target, per architecture section 12). Both need
   the owner's decisions; this closes item 11.
2. The duty point (AX-003, AX-004) and motor bore and speed range are still unknown in
   `examples/axial_120.yaml`. The spec is not runnable until they are answered (SPEC-007).
3. The ducted axial specific-speed range is UNSOURCED (`data/fans/family_ranges.yaml`), so every
   feasibility verdict is at best `unconfirmed`. A cited source is needed.
4. Kernel version is recorded as `0.0.0` in run records. Set a real version before the first release.
5. A full `scripts/check.sh` from an empty cache needs more free disk than the 1.6 GB this host had
   (OpenCascade from source plus five build trees). Keep several GB free before running it in one go.
6. Kernel branch coverage is 51.8%. Only line coverage has a floor (MAINT-003); recorded for information.

## Proposed ADRs awaiting owner review

- ADR-003 D8: spec compiler semantics and unit conversions
- ADR-004: OpenCascade backend
- ADR-005: job resource limits
- ADR-006: the pin values (the OpenFOAM version is accepted)
- ADR-010: store schema
- ADR-011: feasibility gate
- ADR-012: family plugin interface
- ADR-013: C ABI and Python binding
- ADR-014: CLI, runs and claims

## Owner sign-off

Reviewed and accepted by: ______________________  Date: ____________

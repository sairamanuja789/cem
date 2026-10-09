# T02 report: build skeleton, quality gates and CI

Requirements: MAINT-001, MAINT-002, MAINT-003, SEC-001, REPRO-002. Decisions: ADR-007, ADR-008.

## Done-when evidence

| Criterion | Result | Link |
|---|---|---|
| PR 1 (T02) green and merged | `check` passed all 16 stages in about 6 minutes; merged by rebase | [PR 1](https://github.com/sairamanuja789/cem/pull/1), [run 37918624124](https://github.com/sairamanuja789/cem/actions/runs/37918624124) |
| PR 2 with a deliberate warning goes red | `check` failed: `error: unused variable 'deliberately_unused' [-Werror,-Wunused-variable]` in `kernel/src/core/version.cpp`, under GCC and Clang; PR closed unmerged, branch deleted | [PR 2](https://github.com/sairamanuja789/cem/pull/2), [run 37919358357](https://github.com/sairamanuja789/cem/actions/runs/37919358357) |

## Stages in `scripts/check.sh`

Toolchain pins, REPRO-002, presets `gcc-debug`, `clang-debug`, `clang-asan`, `gcc-coverage`, `release`,
clang-tidy, clang-format, ruff check, ruff format, mypy --strict, pytest with coverage, gcovr (MAINT-003),
gitleaks (SEC-001), negative tests.

## Gates shown to work

- An unused variable fails to compile under the project flags (negative test, GCC and Clang).
- A deliberate heap-buffer overflow is caught under `clang-asan`.
- gitleaks detects a fake secret built at runtime, and is quiet on a clean file.
- The coverage floor applies when only some of `kernel/src/{core,spec,physics}` exist. The regression test fails on the old logic.

## Findings during T02

- Clang 18 cannot see libstdc++ 13's `<expected>`; Clang 19 compiles it but is unsupported by mp-units 2.5.0; Clang 20.1.2 passes everything (ADR-007).
- vcpkg needs writable build, download and package directories outside `/opt/vcpkg`.
- The gitleaks test's fake key was constant-folded into a `.pyc`; it is now assembled at runtime.
- The coverage floor was silently skipped when some covered directories were missing (`find` under `pipefail`).
- The sanitizer negative test needs `abort_on_error=0` for that one test.

## Open

- Branch protection on `main` requiring the `check` status (maintainer step).
- No registry: the `ci` image is rebuilt with the Actions cache; revisit at M3 (ADR-008).
- Check the plan's included Actions minutes for private repositories (the repo is currently public).

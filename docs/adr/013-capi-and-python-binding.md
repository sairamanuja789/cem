# ADR-013: C ABI shape and the Python binding build

- Status: proposed (needs owner review)
- Date: 2026-10-10
- Requirements affected: PERF-002, MAINT-005, PHY-005, LIFE-002

## Context
T11 adds the C ABI (`kernel/cemkit/capi/cemkit.h`) and the nanobind module `cemkit._kernel`, and
moves the Python build from hatchling to scikit-build-core (both pre-approved in the build plan).
kernel/CLAUDE.md fixes the ABI rules (extern "C", JSON in and out, `cemkit_free`, status codes, an
ABI version). Open points: the status codes, the batch format, ownership of returned strings, and
how the dev workflow gets the compiled module without breaking `uv run` on the host (the protected
edit hook runs `uv run ruff` outside the container, and `.venv` and `build/` are shared with it).

## Options considered
1. Dev install of the module: (a) uv builds the project (scikit-build-core editable) on
   `uv sync`/`uv run`; (b) `tool.uv.package = false`; the `release` CMake preset builds the module
   into `python/cemkit/` and tests import it from there; wheels still use scikit-build-core.
   (a) makes every host `uv run` try to compile the kernel (no vcpkg on the host: it fails), and
   host and container fight over the shared build tree. (b) keeps `uv run` pure Python.
2. Batch format: (a) a list of cases, each naming its function, with errors per case; (b) columnar
   arrays per quantity. (b) needs numpy for speed (a new dependency); (a) covers every L0 function
   and the gate with one entry point and matches the PHY-005 cross-check.
3. Returned strings: (a) malloc'ed copies released by `cemkit_free`; (b) caller-provided buffers.
   (b) needs a size round trip for every call.

## Decision
Option 1b, 2a and 3a: status codes `CEMKIT_OK`, `CEMKIT_FAILED` (classified error in the JSON),
`CEMKIT_INVALID_ARGUMENT`, `CEMKIT_INTERNAL_ERROR`; ABI 0.1.0; malformed requests become
`invalid_input` naming the key; nanobind 3.1.0 and scikit-build-core 1.0.3 pinned exactly (nanobind
also in the locked dev group for the preset build); the wheel build limits Ninja with a job pool
(6, or `CEMKIT_JOBS`).

## Consequences
- `uv run pytest` needs the `release` preset to have run (check.sh does; a missing module fails
  the binding tests loudly, nothing is skipped).
- check.sh gains a "wheel (ADR-013)" stage that builds the scikit-build-core wheel and checks it
  contains `cemkit/_kernel*.so`; it recompiles the kernel in `build/python/`.
- The JSON batch costs about 4 µs per case for encoding and decoding in Python on top of about
  9 µs in the C ABI (docs/capi.md). If L1 populations need less, add a columnar call (MINOR bump).
- Scripts that import `cemkit` run with `PYTHONPATH=python` (the ctest sets it; `scripts/cemkit`
  sets it; `tests/python/conftest.py` exports it so that child processes started by tests, such as
  the T13 orchestration fakes, can import cemkit).

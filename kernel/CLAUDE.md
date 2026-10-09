# Kernel rules (C++23, cemkit)

## Language subset
Compile with `-std=c++23`, but use only features that both GCC 13 and Clang 20 implement (both on libstdc++ 13; ADR-007):
concepts, ranges, `std::expected`, `std::span`, `std::format`, `std::optional`, `std::variant`,
designated initializers, `constexpr`/`consteval`.
Do not use: modules, `std::print`, `std::mdspan`, deducing `this`, coroutines. CI builds with both compilers. Clang 18 is not used: it cannot see libstdc++ 13's `<expected>`.

## Error handling
- Physics and spec functions return `std::expected<T, cemkit::core::Error>`.
- `Error` carries a code (enum shared with Python failure codes), a message and context fields.
- No exceptions in kernel logic. OpenCascade exceptions are caught at the geometry backend
  boundary and converted to `Error`. Nothing may throw across the C ABI.

## Units and data
- Use mp-units quantities for every physical value. Raw `double` only inside a function body
  after an explicit conversion to a named unit, with a comment explaining why.
- Results carry a fidelity label and model version (see `core/fidelity.hpp`, `core/version.hpp`).

## Determinism
- No randomness without an explicit seed parameter. No clock reads in kernel logic.
- Do not iterate `std::unordered_map` to produce output. Fix the order of floating-point reductions.

## Style
- `#pragma once`, `.hpp` / `.cpp`; `kernel/` is the include root (`"cemkit/core/units.hpp"`,
  `"products/fans/common/pressure.hpp"`). Namespaces follow the path with `products/` and `common/` dropped:
  `cemkit::core`, `cemkit::spec`, `cemkit::fans`, `cemkit::fans::axial_ducted`.
- One CMake target per module via `cemkit_add_module()`; module tests in `<module>/tests/`; headers in a
  module's `detail/` are private. Dependency rules: `docs/repository-structure.md`, checked by
  `scripts/check_boundaries.py`.
- Families register explicitly (`register_family(Registry&)`, called from the product's `register.cpp`);
  never static self-registration.
- Types `PascalCase`, functions and variables `snake_case`, constants `k_snake_case`.
- No raw `new`/`delete`, no owning raw pointers, no global mutable state.
- Every empirical constant: source citation comment + entry in `docs/models/`.

## C ABI (`kernel/cemkit/capi/cemkit.h`)
- `extern "C"`, JSON strings in and out, explicit `cemkit_free` for returned buffers,
  status codes instead of exceptions, an ABI version function. Semantic versioning (MAINT-005).

## Tests
- Catch2 v3. Tag each test with requirement IDs: `TEST_CASE("...", "[SPEC-004]")`.
- Negative compile tests (e.g. adding Pa to m^3/s must not compile) live in `kernel/testing/compile_fail/` (product-specific ones in the product's
  `tests/compile_fail/`), each with a paired control build that must compile.

# Kernel rules (C++23, libfancem)

## Language subset
Compile with `-std=c++23`, but use only features that both GCC 13 and Clang 20 implement (both on libstdc++ 13; ADR-007):
concepts, ranges, `std::expected`, `std::span`, `std::format`, `std::optional`, `std::variant`,
designated initializers, `constexpr`/`consteval`.
Do not use: modules, `std::print`, `std::mdspan`, deducing `this`, coroutines. CI builds with both compilers. Clang 18 is not used: it cannot see libstdc++ 13's `<expected>`.

## Error handling
- Physics and spec functions return `std::expected<T, fancem::Error>`.
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
- `#pragma once`, `.hpp` / `.cpp`, namespaces `fancem::core`, `fancem::spec`, `fancem::physics`, ...
- Types `PascalCase`, functions and variables `snake_case`, constants `k_snake_case`.
- No raw `new`/`delete`, no owning raw pointers, no global mutable state.
- Every empirical constant: source citation comment + entry in `docs/models/`.

## C ABI (`kernel/capi/fancem.h`)
- `extern "C"`, JSON strings in and out, explicit `fancem_free` for returned buffers,
  status codes instead of exceptions, an ABI version function. Semantic versioning (MAINT-005).

## Tests
- Catch2 v3. Tag each test with requirement IDs: `TEST_CASE("...", "[SPEC-004]")`.
- Negative compile tests (e.g. adding Pa to m^3/s must not compile) live in `kernel/tests/compile_fail/`.

# ADR-007: Compilers, standard library and warning flags

- Status: accepted
- Date: 2026-10-09
- Requirements affected: MAINT-001, COR-001, COR-002, LIFE-002, REPRO-002

## Context
The kernel is C++23 and returns `std::expected` from physics and spec functions (architecture rule 6), and
uses mp-units for strong unit types (COR-001). Findings on Ubuntu 24.04, verified 2026-10-09:
- **Clang 18** defines `__cpp_concepts` as 201907 while libstdc++ 13's `<expected>` needs 202002, so
  `std::expected` is invisible (`no member named 'expected' in namespace 'std'`). libstdc++ 14 did not help.
  Clang 18 with libc++ 18 compiled, but needs OpenCascade built twice and splits the C++ ABI under the bindings.
- **Clang 19** fixes `<expected>` but cannot compile the pinned mp-units 2.5.0: `quantity_point.h:120: type
  constraint differs in template redeclaration`. No option helped (`-std=c++20/23/26`,
  `MP_UNITS_API_NATURAL_UNITS=0`, `MP_UNITS_API_NO_CRTP=1`; the port has no feature options). mp-units' own
  compiler-support page lists Clang "16+ && !19" and attributes it to an unfixable compiler bug
  (https://mpusz.github.io/mp-units/HEAD/getting_started/cpp_compiler_support/). Upgrading mp-units is
  therefore not a way out.
- **Clang 20.1.2** (`1:20.1.2-0ubuntu1~24.04.3`, noble-updates and noble-security, pinned by the Ubuntu
  snapshot) passed: the mp-units reproducer (0 errors), a `std::expected` and `std::format` example under
  `-Wall -Wextra -Wpedantic -Werror`, clang-tidy-20 parsing an mp-units translation unit (0 errors),
  AddressSanitizer catching a deliberate heap-buffer overflow, and LeakSanitizer working in the container.

## Options considered
1. Clang 18 + libc++ 18 — two standard libraries, a second OpenCascade build.
2. Clang 18 or 19 + libstdc++ — `std::expected` unavailable (18) or mp-units unusable (19).
3. GCC 13 only, with GCC's ASan and UBSan — the fallback if Clang 20 had failed.
4. GCC 13 and Clang 20, both on libstdc++ 13.

## Decision
Build with GCC 13 and Clang 20 (`1:20.1.2-0ubuntu1~24.04.3`), both on libstdc++ 13. No libc++.
clang-tidy and clang-format are the version 20 tools.

## Warning flags
On project targets only, with third-party headers included as SYSTEM:
`-Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
-Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wformat=2
-Wimplicit-fallthrough`.
A flag that proves unworkable is removed only with a written reason appended here:

| Flag removed | Date | Reason |
|---|---|---|
| (none) | | |

## Consequences
- One standard library for both compilers; OpenCascade is built once against libstdc++.
- Every preset compiles a `std::expected` example and an mp-units example (toolchain test).
- Before raising mp-units or Clang, check mp-units' compiler-support table first.

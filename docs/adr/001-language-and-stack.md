# ADR-001: Core language and stack: C++23 kernel, Python orchestration, solvers as processes

- Status: proposed (needs owner review). This records a decision already made in
  `docs/requirements.md` section 3; it becomes accepted when the owner accepts this ADR.
- Date: 2026-10-10
- Requirements affected: all (layering); MAINT-001 to MAINT-005, PERF-002, PHY-005

## Context

Every module, test and interface depends on the implementation language, so it has to be decided
first (`docs/architecture.md` section 12). The earlier architecture plan recommended a
Python-first stack. Requirements section 3 replaced that recommendation after comparing C++23,
Rust, C#/.NET, Python and Julia against the production pattern of CAE systems: a compiled
kernel driven from a higher-level language.

## Decision

1. **Kernel:** C++23, limited to features both GCC 13 and Clang 20 implement (ADR-007), in
   `kernel/`. Strong unit types use mp-units. Physics functions are pure, deterministic and
   return `std::expected<T, Error>`.
2. **Orchestration:** Python 3.13 in `python/cemkit/`. It talks to people, files, services and
   the job schedule.
3. **Boundary:** nanobind plus a C ABI (`bindings/`, ADR-013). The boundary is coarse: a spec and
   a batch of candidates go in, fidelity-labelled results come out. The kernel never calls
   Python, the network or a solver.
4. **Solvers:** OpenFOAM, CalculiX and Gmsh run as external processes through the orchestration
   runner (ADR-005).
5. **Placement rule:** code that defines engineering truth or runs once per candidate goes in the
   kernel. Code that talks to people, files or services, or schedules work, goes in Python.
6. **Reference first:** each model is written first in Python (`python/cemkit/reference/`),
   checked against hand calculations, then ported to C++. The two must agree to floating-point
   tolerance.

## Alternatives considered

The comparison table is in requirements section 3:
- Rust has no mature B-rep CAD kernel.
- C#/.NET needs wrappers and has three-year LTS cycles.
- Pure Python is slow in per-candidate loops.
- Julia has a thin CAD ecosystem.

## Consequences

- **Cost:** kernel work is slower to write and carries memory-safety risk. This is contained by
  a small kernel, `-Werror`, clang-tidy, ASan/UBSan in CI and the Python reference (ADR-007,
  ADR-008).
- **Evidence it works:** T04–T14 are implemented this way. In T08 the kernel agreed bit for bit
  with the Python reference on all verified hand calculations and on 10,000 random inputs. In
  T11 the batch C ABI call agreed exactly as well.
- **Reuse:** the kernel can later serve a C#, web or desktop front end unchanged.

# Fan pressure kinds (COR-001, SPEC-004)

Code: `kernel/products/fans/common/pressure.hpp` (`cemkit::fans`).

## Definitions

These follow the ISO 5801:2017 terms (*Fans — Performance testing using standardized airways*) and
the conventions in requirements section 2. The clause numbers and the standard's symbols have not
been checked against the standard yet, so they are not cited here.

- **Fan total pressure** (ISO 5801: *fan pressure*): the stagnation pressure at the fan outlet minus
  the stagnation pressure at the fan inlet, p_t2 − p_t1. Here "p_t" (requirements section 2) means
  the ISO 5801 stagnation pressure, which includes the Mach-factor correction of the dynamic part.
- **Fan static pressure**: the fan pressure minus the fan dynamic pressure at the outlet.
  Equivalently, p_s2 − p_t1: the outlet static pressure minus the inlet stagnation pressure. It is
  total-to-static by construction. The ISO 5801 fan dynamic pressure is a conventional value,
  worked out from the mass flow, the mean outlet density and the fan outlet area (with the Mach
  factor). It is not the local dynamic pressure in a measuring plane.

Both are pressure differences in pascal, but a requirement or a rating means one of them, not just
"a pressure". Binding rules (SPEC-004, requirements section 2):

- a pressure without a stated type is rejected;
- static-to-static pressure rise (p_s2 − p_s1) is rejected as an input. No type exists for it.

## Types

| Alias | Quantity spec | Unit |
| --- | --- | --- |
| `FanTotalPressure` | `fan_total_pressure` (kind, child of `isq::pressure`) | Pa |
| `FanStaticPressure` | `fan_static_pressure` (kind, child of `isq::pressure`) | Pa |

Each is its own kind (`mp_units::is_kind`). Values of the same kind add and subtract normally.
What converts:

| From → to | Implicit | Explicit (`T{x}`, `quantity_cast`) |
| --- | --- | --- |
| fan static ↔ fan total | no | no |
| fan kind → plain `Pressure` | no | not used |
| plain `Pressure` → fan kind | no | **compiles** |

mp-units allows explicit construction of a child kind from its parent. A plain pressure can
therefore be declared total or static with `FanTotalPressure{p}`. Only the spec compiler may do
this, at the point where it records the type the user stated. Physics code receives fan pressures
already typed. The unit tests pin this behaviour, so a change in mp-units is noticed. A mechanical
check that confines the explicit conversion to `kernel/cemkit/spec/` is a follow-up for the
spec-compiler task.

## Conversion between the kinds

- **Python reference (T06):** `fan_total_from_static` in
  `python/cemkit/reference/fans/common/l0.py` adds the conventional fan dynamic pressure,
  Δp_t = Δp_s + ½ ρ (Q / A_out)² with A_out = π D_duct² / 4 (ADR-003 D1). Its result has
  provenance **derived**. Δp_s ≥ 0 is accepted, so free delivery (Δp_s = 0) is valid.
- **Assumptions:** ρ₂ ≈ ρ₁ and the Mach factor is 1. Both hold inside the incompressible range of
  ADR-003 D3. Equations and validity are in `docs/models/fans/l0-similarity.md`.
- **C++ kernel:** the conversion and a typed `fan_dynamic_pressure` follow with the L0 port in T08.

## Evidence

- Unit tests: `kernel/products/fans/common/tests/test_pressure.cpp`.
- Compile-fail tests: `kernel/products/fans/common/tests/compile_fail/`. Each case has a control
  build. The cases are:
  - static passed where total is required;
  - total + static;
  - plain pressure used as total by implicit conversion.

# ADR-003: Requirement semantics: pressure basis, air constants, L0 validity

- Status: accepted (user decisions, recorded 2026-10-10)
- Date: 2026-10-10
- Requirements affected: SPEC-004, SEL-001, PHY-001, PHY-002, PHY-003, PHY-005, AX-005

## Context

T06 implements the L0 physics in `docs/architecture.md` section 6: the fan laws, the flow
coefficient φ, the pressure coefficient ψ, the specific speed ω_s and the specific diameter δ_s.
Three questions of meaning had to be settled first: which pressure enters the formulas, which air
constants are used and where they come from, and where the L0 models stop being valid. The user
answered these in an earlier session, but the answers never reached the repo, so a later session
asked again. This ADR records them so they are asked only once. Later semantics decisions are
added here.

## Decisions

### D1. Pressure basis (SPEC-004, SEL-001)

- ψ, ω_s and δ_s take **fan total pressure only**. Fan static pressure is a distinct type, so
  passing it is a **type error** (mypy in Python, a compile error in C++), not a runtime
  out-of-validity result.
- **Static-to-total conversion is part of T06.** It is an explicit, separate function. Under the
  ISO 5801 definitions of fan total, static and dynamic pressure, with the conventional fan
  dynamic pressure taken from the mean outlet velocity:

  Δp_t = Δp_s + ½ ρ (Q / A_out)², with A_out = π D_duct² / 4

  The result carries provenance **derived**. ISO 5801's Mach factor on the dynamic pressure is
  taken as 1 inside the incompressibility range of D3.
- **No static-pressure variant of ω_s.** The Cordier diagram is built on total pressure. If T08
  finds a dataset published only on a static basis, converting it is T08's decision, with its own
  ADR.

### D2. Air properties and constants (AX-005)

| Quantity | Value | Source |
| --- | --- | --- |
| Density ρ (default) | 1.18 kg/m³ | Requirements AX-005 and section 2 (the spec's own default) |
| Temperature T₁ (default) | 298.15 K | Requirements AX-005 |
| Ambient pressure p₁ | 101 325 Pa | U.S. Standard Atmosphere, 1976 (NOAA/NASA/USAF, NOAA-S/T 76-1562): sea-level pressure P₀ |
| Gas constant of air R | R*/M₀ = 8.31432×10³ / 28.9644 = 287.0531 J/(kg·K) | Same document: universal gas constant R* and sea-level mean molar mass M₀. **R is computed in code from these two constants, never typed as a rounded value** |
| Ratio of specific heats γ | 1.40 | Same document |

- **Page and table numbers** in the 1976 document were read from the NASA NTRS scan on 2026-10-10
  and are given in
  `docs/models/platform/air-properties.md`. Note that Table 2 A misprints R* as × 10⁻³; the text
  on p. 3 gives the correct × 10³.
- **Consistency check:** the ideal-gas density at the defaults, p₁/(R T₁) = 1.1839 kg/m³, must be
  within 0.5 % of 1.18 kg/m³. It is 0.33 % today, and a test enforces the 0.5 % bound.
- **No viscosity in T06.** Nothing in L0 needs it. It arrives with the Reynolds number in the L1
  model.

### D3. Validity of the L0 models (PHY-002, PHY-003)

Every L0 function returns out-of-validity, never a number, unless both of these hold:

1. **Every input is finite and strictly positive.**
2. **The flow is incompressible within the project tolerance ε = 0.01.** Each criterion is applied
   wherever its inputs are available:
   - Δp_t / (γ p₁) ≤ ε. This is the isentropic density change from the pressure rise.
   - M_tip² / 2 ≤ ε, where M_tip = U_tip / √(γ R T₁). This is the stagnation density change at the
     blade tip.

The relations themselves are the standard isentropic-flow results: Δρ/ρ ≈ Δp/(γp) from p/ρ^γ =
const, and ρ₀/ρ ≈ 1 + M²/2. They are cited to NACA Report 1135 (1953), p. 616, eqs. (34) and
(45). That report is public domain and its equations were read from the scan. It replaces the
Anderson textbook citation, whose chapter and equation numbers could not be verified
(`docs/models/fans/l0-similarity.md`).
**ε = 0.01 is a project decision, not a standard value.**

At the default state the limits are:
- Δp_t ≤ 1 418.55 Pa;
- M_tip ≤ 0.1414, so U_tip ≤ 48.95 m/s with a₁ = 346.15 m/s.

The pressure criterion also bounds the outlet Mach number used in D1, because the dynamic pressure
is part of Δp_t.

**Recorded assumption, not checked:** the fan laws hold only for geometrically similar fans in
the same Reynolds-number regime. L0 has no viscosity to check this.

### D4. Diameters and tip speed

- φ, ψ and δ_s use the **rotor tip diameter** D, and U = ω D / 2.
- The **duct diameter** enters only the static-to-total conversion of D1.

### D5. Family boundaries

The Cordier boundaries (family selection, SEL-002) stay in T08, as cited data. T06 only computes
ω_s and δ_s.

### D6. Out-of-validity results mirror the kernel error contract (REL-002)

A Python reference function returns either its value or an error with the same shape as the
kernel's `cemkit::core::Error`:
- code `out_of_validity`, taken from `schemas/cemkit/v1/error-codes.json`;
- a subject;
- a message;
- details `value`, `bounds` and `model`, with numbers formatted exactly as the kernel's
  `format_number` formats them.

T08 then compares the C++ and Python results field for field.

### D7. Tests of the L0 reference

- **Invariance tests.** These check that ω_s, δ_s, φ and ψ are unchanged under fan-law scaling,
  and that the fan-law ratios are exact. They generate only states that are valid on **both
  sides** of the scaling.
- **Out-of-validity tests.** Separate tests check that states outside the limits return
  out-of-validity.
- **Hand calculations.** In addition to the planned cases, these include:
  - a static-to-total conversion case;
  - one case beyond each incompressibility limit;
  - the ideal-gas consistency check.

## Consequences

- **Type split in Python.** The Python reference gets distinct fan total and fan static pressure
  types, as the kernel already has. A test runs mypy to prove that passing a static pressure where
  a total pressure is required fails type checking.
- **Small fans stay inside the range.** The 120 mm fan at 2000 rpm has U_tip ≈ 12.6 m/s, well
  inside the limits. Only unusually high speeds or pressures fall outside, and those return
  out-of-validity instead of a number.
- **Revisit ε** if L1 or L2 results show compressibility effects matter at a smaller tolerance,
  or if a product needs higher tip speeds. Any change is recorded here.

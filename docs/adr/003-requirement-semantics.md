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
- **Free delivery is accepted** (user decision, 2026-10-10). The conversion takes Δp_s ≥ 0, so
  the free-delivery rating point Δp_s = 0 gives Δp_t = ½ ρ (Q / A_out)². Negative fan static
  pressure, beyond free delivery, stays out of validity until a product needs it; extending the
  range is recorded here.
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

1. **Every input is finite and strictly positive.** The one exception is the fan static pressure
   in the conversion of D1, which is finite and ≥ 0 (bounds `[0, inf)`), so that free delivery
   is valid.
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
- **Rotor tip diameter from the spec (owner decision D6, 2026-10-10; provisional default).** The
  120 mm of AX-001 is the duct inner diameter, and D = 120 mm − 2 × tip clearance, with the
  clearance taken from the spec (`product.tip_clearance_min`, AX-009). If no clearance is stated,
  D stays unknown and the tip-speed check stays not computable. The derived D has provenance
  `default` and is provisional until the owner confirms frame vs rotor diameter (HI-023).

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
  - a static-to-total conversion case, and a free-delivery case (Δp_s = 0);
  - one case beyond each incompressibility limit;
  - the ideal-gas consistency check.

### D8. Spec compiler semantics and unit conversions (T07: SPEC-001 to SPEC-011, IN-001)

**Status of D8: proposed (needs owner review).** D1–D7 are the owner's accepted decisions; D8 was
written during autonomous work on T07 and is not accepted until the owner reviews it
(`docs/open-items.md`). Decision on inH₂O basis (owner decision D3, 2026-10-10): inH₂O is taken as
the **conventional** inch of water: 0.0254 m × 1000 kg/m³ × 9.80665 m/s² = 249.08891 Pa. mmH₂O is
9.80665 Pa (both NIST SP 811 Sec. B.8). A temperature-specific basis must be stated explicitly in
the input. If the spec schema or compiler has no way to state a temperature-specific basis, any such
units (e.g. `inH2O@60F`) are rejected with `spec_rejected` naming the field, rather than guessing.

- **Unit conversion factors** (non-SI factors: NIST SP 811 (2008), Sec. B.8):
  - Length: 1 mm = 0.001 m, 1 cm = 0.01 m, 1 in = 0.0254 m (exact).
  - Angular velocity: 1 rpm = π/30 rad/s.
  - Volume flow rate: 1 m³/min = 1/60 m³/s, 1 m³/h = 1/3600 m³/s, 1 L/s = 0.001 m³/s,
    1 CFM = (0.3048)³ / 60 m³/s = 0.0004719474432 m³/s.
  - Pressure: 1 kPa = 1000 Pa, 1 mmH₂O = 9.80665 Pa (conventional; standard gravity g_n = 9.80665 m/s² exactly),
    1 inH₂O = 25.4 × 9.80665 Pa = 249.08891 Pa. Both ASCII ("mmH2O", "inH2O") and Unicode
    ("mmH₂O", "inH₂O") spellings are accepted.
  - Power: 1 kW = 1000 W.
  - Temperature: K is absolute; °C converts as T + 273.15 K, but tolerance bands in °C convert
    with scale 1.0 (difference of points).
  - Dimensionless / ratio: 1 % = 0.01, 1 = 1.0.
  - Viscosity: 1 Pa·s = 1.0 Pa·s, 1 mPa·s = 0.001 Pa·s.
- **Provenance precedence:** user (rank 4) > image (rank 3) > derived (rank 2) > default (rank 1) > unknown (rank 0).
  Deterministic; conflicts and overridden provenance are recorded on the compiled spec.
- **Pressure semantics:** accept only `fan_total` and `fan_static`. Reject missing kind, reject
  `static_to_static` (and `static-to-static`), and reject `total_to_static` explaining that it is
  an efficiency type only, not a pressure type. All rejections name the field.
- **Contradiction check:** Required air power P_air = Q · Δp > P_limit (or motor/shaft limit) is
  rejected as a physical contradiction (`infeasible_requirement`), naming the field.
- **Essential unknown fields:** Stubbed until T09; for `fans.axial_ducted`, essential fields are
  `product.duty.flow` and `product.duty.pressure`. Every essential unknown produces a question
  naming the field, unit and why it matters. A campaign cannot start with unresolved essential
  unknowns unless autonomous mode is set and a default exists (marked provisional).
- **Defaults** come only from the injected family default resolver. The compiler has no built-in
  default values. A resolver default is applied only when it carries a value in the field's
  coherent SI unit and a non-empty source note (citation); otherwise the field stays an essential
  unknown with a question. Applied defaults get provenance `default` and are provisional.
- **Not covered by the kernel compiler yet:** SPEC-011's operating range / system curve (only a
  single duty point with tolerances is compiled; HI-006) and IN-001's YAML input (the kernel reads
  JSON; YAML is parsed in Python orchestration; HI-007).

## Consequences

- **Type split in Python.** The Python reference gets distinct fan total and fan static pressure
  types, as the kernel already has. A test runs mypy to prove that passing a static pressure where
  a total pressure is required fails type checking.
- **Small fans stay inside the range.** The 120 mm fan at 2000 rpm has U_tip ≈ 12.6 m/s, well
  inside the limits. Only unusually high speeds or pressures fall outside, and those return
  out-of-validity instead of a number.
- **Revisit ε** if L1 or L2 results show compressibility effects matter at a smaller tolerance,
  or if a product needs higher tip speeds. Any change is recorded here.

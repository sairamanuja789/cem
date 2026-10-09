# L0 fan similarity: fan laws, coefficients, specific speed and diameter

Kernel: `kernel/products/fans/common/l0.hpp` (model `fans.l0@1.0.0`; every result labelled L0
predicted with the model ID). Reference: `python/cemkit/reference/fans/common/l0.py`. The
feasibility gate built on these functions: `docs/models/fans/feasibility.md`.

Formulas: `docs/architecture.md` section 6. Decisions: ADR-003 (D1 pressure basis, D3 validity,
D4 diameters, D6 errors). Requirements: SEL-001, PHY-001, PHY-002, PHY-003, PHY-005.

## Conventions

- **Units:** SI throughout.
- **Diameter and tip speed:** D is the rotor tip diameter, and U = ω D / 2. The duct diameter
  enters only the static-to-total conversion.
- **Pressure basis:** Δp_t is fan total pressure (ISO 5801; `docs/models/fans/pressure-kinds.md`).
  ψ, ω_s and δ_s accept only a `FanTotalPressure`. Passing a `FanStaticPressure` is a type error,
  which a test proves by running mypy.

## Equations

| Function | Equation | Source |
| --- | --- | --- |
| `tip_speed` | U = ω D / 2 | Definition (ADR-003 D4) |
| `flow_coefficient` | φ = Q / ((π/4) D² U) | Architecture §6 |
| `pressure_coefficient` | ψ = 2 Δp_t / (ρ U²) | Architecture §6 |
| `specific_speed` | ω_s = ω √Q / (Δp_t/ρ)^(3/4) | Architecture §6 |
| `specific_diameter` | δ_s = D (Δp_t/ρ)^(1/4) / √Q | Architecture §6 |
| `scale_fan_laws` | Q ∝ N D³, Δp ∝ ρ N² D², P ∝ ρ N³ D⁵ | Architecture §6 |
| `conventional_dynamic_pressure` | p_d = ½ ρ (Q / A_out)², A_out = π D_duct² / 4 | ISO 5801 definitions (ADR-003 D1) |
| `fan_total_from_static` | Δp_t = Δp_s + ½ ρ (Q / A_out)², A_out = π D_duct² / 4 | ISO 5801 definitions (ADR-003 D1) |

`fan_total_from_static` returns its result with provenance **derived** and records the rule it
applied. It accepts Δp_s = 0, the free-delivery rating point, where Δp_t is the dynamic pressure
alone (ADR-003 D1). Negative static pressure is out of validity. The converted total must also be
strictly positive: at free delivery a vanishingly small flow underflows the dynamic pressure to 0,
and that returns `out_of_validity` with subject `fan_total_pressure`, checked before the pressure
limit.

ISO 5801's conventional fan dynamic pressure is built from the mass flow, the mean *outlet*
density and the fan outlet area, with a Mach factor. Here it is simplified, and every result is
inside the incompressible range below, where these simplifications hold:
- ρ₂ ≈ ρ₁ (outlet density equals inlet density);
- Q is a single volume flow, the same at inlet and outlet;
- the Mach factor is 1.

## Validity (PHY-002, PHY-003)

A function returns an `out_of_validity` error, never a number, unless both of these hold:

1. **Every input is finite and strictly positive.** The bounds are written `(0, inf)`. The one
   exception is the fan static pressure of `fan_total_from_static`, which is finite and ≥ 0,
   written `[0, inf)`, so that free delivery is valid.
2. **The flow is incompressible within ε = 0.01.** ε is a project decision (ADR-003 D3), not a
   standard value. Each criterion applies where its inputs are available:

   | Criterion | Meaning | Limit at the default state |
   | --- | --- | --- |
   | Δp_t / (γ p₁) ≤ ε | isentropic density change from the pressure rise | Δp_t ≤ 1 418.55 Pa |
   | M_tip² / 2 ≤ ε, M_tip = U / √(γ R T₁) | stagnation density change at the blade tip | U ≤ 48.953 m/s (M_tip ≤ 0.1414) |

   M_tip is the **blade tip-speed Mach number** (U only), as ADR-003 D3 decides. It is not the
   relative Mach number √(U² + c_x²)/a, which is about 6 % higher at φ ≈ 0.36.

   | Function | Positivity | Pressure limit | Tip-Mach limit |
   | --- | :---: | :---: | :---: |
   | `tip_speed` | ✓ | – | – |
   | `flow_coefficient` | ✓ | – | ✓ |
   | `pressure_coefficient` | ✓ | ✓ | ✓ |
   | `specific_speed` | ✓ | ✓ | – (no D) |
   | `specific_diameter` | ✓ | ✓ | – (no ω) |
   | `conventional_dynamic_pressure` | ✓ | ✓, on p_d (outlet M²/2 ≤ ε) | – |
   | `fan_total_from_static` | ✓ | ✓, on the result | – |
   | `scale_fan_laws` | ✓ | ✓, both points | ✓, both points |

   The pressure criterion on a converted total pressure also bounds the outlet Mach number,
   because the dynamic pressure is part of Δp_t. `conventional_dynamic_pressure` applies the same
   limit to p_d alone: by eq. (31b) below, p_d / (γ p₁) = M_out² / 2, so p_d ≤ ε γ p₁ is the
   stagnation criterion at the outlet Mach number (with p₁ for the outlet static pressure, which
   is within ε of it in this range, and with ρ ≈ p₁/(R T₁): the AX-005 density 1.18 kg/m³ is
   0.33 % below the ideal-gas value, inside the 0.5 % bound of ADR-003 D2, so the p_d limit
   corresponds to V ≤ 49.03 m/s instead of √(2ε) a₁ = 48.95 m/s).

**Source of the criteria.** These are the linearised isentropic relations of a perfect gas, from
Ames Research Staff, *Equations, Tables, and Charts for Compressible Flow*, NACA Report 1135
(1953), p. 616. NASA NTRS document 19930091059, public domain. Read from the scan on 2026-10-10.

| Relation | Source equation | Linearised form |
| --- | --- | --- |
| p/ρ^γ = const = p_t/ρ_t^γ | (34) [isen, perf] | Δρ/ρ ≈ Δp/(γ p), the pressure criterion |
| ρ/ρ_t = (1 + (γ−1)/2 · M²)^(−1/(γ−1)) | (45) [isen, perf] | ρ_t/ρ ≈ 1 + M²/2, the tip-Mach criterion |
| a = √(γ R T) | (29b) [therm perf] | speed of sound |
| q = ½ ρV² = (γ/2) p M² | (31a), (31b) | outlet Mach bounded by the pressure criterion |

NACA Report 1135 replaces the earlier Anderson citation, whose chapter and equation numbers could
not be verified.

**Recorded assumption, not checked:** the fan laws hold for geometrically similar fans in the same
Reynolds-number regime. L0 has no viscosity, so it cannot check this.

**Error shape (ADR-003 D6).** Errors use the kernel's shape:
- code `out_of_validity`;
- subject, the input or quantity concerned: `flow`, `fan_total_pressure`, `tip_speed`,
  `air.density`, …;
- details `value`, `bounds` and `model`, with numbers written exactly as the kernel's
  `format_number` writes them.

A test checks the Python formatter against libstdc++'s `std::to_chars` on 622 values.

## Order of checks

Checks run in a fixed order, so the reported error is deterministic:

1. inputs, in argument order;
2. air properties;
3. the pressure limit;
4. the tip-Mach limit.

`scale_fan_laws` checks the given point first, then the target speed and diameter, then the scaled
point.

## Kernel port (T08)

- Each function unwraps its mp-units quantities into the coherent SI unit, computes in `double`
  in the reference's operation order, and wraps the result as `core::Labelled<Q>` (L0 predicted,
  `fans.l0@1.0.0`). Limits keep the reference's order: `(ε γ) p₁` and `√(2ε) · √(γ R T₁)`.
- Squares are written `x * x` in both languages. A release build turns `std::pow(x, 2.0)` into
  `x * x`, while Python's `x**2` calls the C library's `pow`, which is not always correctly rounded;
  the two then differed in the last bit on 3 of 130 018 cross-check cases. Higher powers call `pow`
  on both sides. This changed rounding only, not an equation, so the model version stays 1.0.0.
- Errors carry the same subject, message and details as the reference, so `describe()` is byte
  identical. The static pressure check is `value >= 0.0`, so −0.0 is accepted like 0.0.
- `conventional_dynamic_pressure` returns the distinct kind `FanDynamicPressure`. (The function
  is not called `fan_dynamic_pressure` because that name is the quantity kind.)
- `check_air`, `check_pressure_limit` and `check_tip_speed_limit` expose the validity checks to
  the feasibility gate with the same texts.

## Evidence

- Hand calculations `tests/hand_calcs/verified/l0_001` … `l0_009`, checked by a human, and
  `tests/hand_calcs/proposed/l0_010` (free delivery), awaiting that check. All show the working.
- Hypothesis properties in `tests/python/test_reference_l0.py`:
  - the fan-law ratios are exact;
  - doubling the speed doubles the flow at a fixed diameter;
  - φ, ψ, ω_s and δ_s are invariant under fan-law scaling.

  These draw only states valid on both sides of the scaling. Separate tests check that pressure,
  tip speed or scaling beyond the limits, and non-positive or non-finite inputs, return
  `out_of_validity`.
- Kernel unit tests `kernel/products/fans/common/tests/test_l0.cpp` (the hand calculations again,
  and every validity branch) and `kernel/cemkit/physics/tests/`.
- **Kernel vs reference (PHY-005):** ctest `fans_l0_crosscheck` runs `scripts/crosscheck_l0.py`.
  The script builds cases from every hand calculation in `verified/` and `proposed/`, a few edge
  cases (Δp_s = −0.0, an underflowing total, limits), 10 000 seeded random states inside the
  validity range (seed 20261010), each evaluated by every L0 function, both air functions and the
  feasibility gate, and 1 000 further states with one input pushed outside the range. The kernel
  side is a small test-data tool, `kernel/products/fans/common/tests/l0_crosscheck.cpp`, that reads
  the cases as JSON and writes the kernel results as JSON; the bindings arrive in T11. Numbers must
  agree to a relative 1e-12, labels, model IDs and provenance exactly, and errors field for field
  with a byte-identical `describe()`. On 2026-10-10 (gcc-debug and release) the largest relative
  difference over 149 451 numbers was 0 (bitwise agreement), and 16 125 errors matched.

## Not here

- **Cordier boundaries** (family selection, SEL-002): `data/fans/family_ranges.yaml`. No range
  has a verified source yet, so all are `UNSOURCED` and the gate reports "range unsourced"
  (HI-003).
- **Reynolds-number checks and viscosity:** the L1 model.

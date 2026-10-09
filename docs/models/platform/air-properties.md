# Air properties (platform physics, L0)

Kernel: `kernel/cemkit/physics/air.hpp` (model `platform.air@1.0.0`, results labelled L0 predicted).
Reference: `python/cemkit/reference/platform/air.py`. The two agree bit for bit on every hand
calculation and 10 000 random states (ctest `fans_l0_crosscheck`, T08). Decisions: ADR-003 D2.

## Values

| Symbol | Value | Unit | Source |
| --- | --- | --- | --- |
| ρ (default) | 1.18 | kg/m³ | Requirements AX-005 and section 2 (the spec's default air) |
| T₁ (default) | 298.15 | K | Requirements AX-005 |
| p₁ | 101 325 | Pa | USSA 1976, Table 2 B (Category II constants), p. 2: P₀ = 1.013250 × 10⁵ N/m²; discussed on p. 3 |
| R* | 8.31432 × 10³ | J/(kmol·K) | USSA 1976, p. 3, discussion of the Category I constants (see the erratum below) |
| M₀ | 28.9644 | kg/kmol | USSA 1976, section 1.2.4 "Mean molecular weight", eq. (21) with table 3, p. 9 |
| R | R*/M₀ = 287.0531 | J/(kg·K) | Computed in code from R* and M₀, never typed as a rounded value |
| γ | 1.40 | – | USSA 1976, Table 2 B (Category II constants), p. 2 ("1.40 (dimensionless)"); p. 4 ("an adopted value γ = 1.400") |

**Document:** *U.S. Standard Atmosphere, 1976*. National Oceanic and Atmospheric Administration,
National Aeronautics and Space Administration and United States Air Force. NOAA-S/T 76-1562, U.S.
Government Printing Office, Washington, D.C., October 1976. Public domain. NASA NTRS document
19770009539. Page numbers are those printed on the document.

The page references were read from the NTRS scan on 2026-10-10:
- **R\*:** Table 2 A on p. 2 prints R* as "8.31432 × 10⁻³ N·m/(kmol·K)". That is a misprint of the
  exponent, recorded in the errata sheet appended to the NTRS PDF. The correct value,
  8.31432 × 10³, is stated in the text on p. 3, so that is the citation.
- **M₀:** not in Table 2. It follows from the sea-level composition in table 3:
  Σ F_i M_i = 28.96443. Section 1.2.4 states "M₀ is found to be 28.9644 kg/kmol".

## Functions

| Function | Equation | Validity |
| --- | --- | --- |
| `default_air()` | The values above | – |
| `ideal_gas_density(p, T, R)` | ρ = p / (R T) | p, T, R finite and > 0 |
| `speed_of_sound(air)` | a = √(γ R T) | γ, R, T finite and > 0 |

**Assumptions:**
- Air is a calorically perfect ideal gas.
- There is no humidity correction.
- There is no viscosity at L0. Viscosity arrives with the Reynolds number in the L1 model, with
  its own cited source.

## Consistency check

The ideal-gas density at the defaults is p₁ / (R T₁) = 101 325 / (287.0531 × 298.15) =
1.18391 kg/m³. That is 0.33 % above the spec's 1.18 kg/m³. The tolerance is 0.5 % (ADR-003 D2), and
`tests/python/test_reference_l0.py` and hand calculation `l0_008` enforce it.

## Speed of sound at the default state

a₁ = √(1.40 × 287.0531 × 298.15) = 346.149 m/s. It is used by the tip-Mach limit in
`docs/models/fans/l0-similarity.md`.

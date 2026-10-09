# Air properties (platform physics, L0)

Reference: `python/cemkit/reference/platform/air.py` (model `platform.air@1.0.0`). The kernel port
follows in T08. Decisions: ADR-003 D2.

## Values

| Symbol | Value | Unit | Source |
| --- | --- | --- | --- |
| ρ (default) | 1.18 | kg/m³ | Requirements AX-005 and section 2 (the spec's default air) |
| T₁ (default) | 298.15 | K | Requirements AX-005 |
| p₁ | 101 325 | Pa | U.S. Standard Atmosphere, 1976: sea-level pressure P₀ |
| R* | 8.31432 × 10³ | J/(kmol·K) | U.S. Standard Atmosphere, 1976: universal gas constant |
| M₀ | 28.9644 | kg/kmol | U.S. Standard Atmosphere, 1976: sea-level mean molar mass of air |
| R | R*/M₀ = 287.0531 | J/(kg·K) | Computed in code from R* and M₀, never typed as a rounded value |
| γ | 1.40 | – | U.S. Standard Atmosphere, 1976: ratio of specific heats of air |

**Document:** *U.S. Standard Atmosphere, 1976*. National Oceanic and Atmospheric Administration,
National Aeronautics and Space Administration and United States Air Force. NOAA-S/T 76-1562, U.S.
Government Printing Office, Washington, D.C., October 1976. Public domain.

**To confirm (user):** the section and table numbers for R*, M₀, P₀ and γ are not given here yet.
They are to be read from the document when the hand calculations are verified, and added to this
page. Until then the constants are cited to the document as a whole.

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

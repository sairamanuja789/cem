# L1 axial rotor mean line: loss-free velocity triangles

Kernel: `kernel/products/fans/families/axial_ducted/mean_line.hpp` (model
`fans.axial.mean_line@0.1.0`; every output **L1 predicted**). Reference:
`python/cemkit/reference/fans/axial/mean_line.py`. Cross-check: `tests/python/test_reference_mean_line.py`
(seeded random duties and the hand calculation through `cemkit_l0_batch`, function `mean_line`).
Hand calculation: `tests/hand_calcs/proposed/l1_001_mean_line_velocity_triangles.yaml` (awaiting
owner check). Requirements: PHY-001, PHY-002, PHY-005.

## Fundamental law

Euler's turbomachinery equation, from conservation of angular momentum through the rotor, for
incompressible flow along one stream surface:

    Δp_t = ρ U (c_θ2 − c_θ1)

Source: Dixon, S. L. and Hall, C. A., *Fluid Mechanics and Thermodynamics of Turbomachinery*,
7th ed., Butterworth-Heinemann, 2014, chapter 1 (Euler work equation) and chapter 5 (axial-flow
velocity diagrams). **Edition, chapter and equation numbers to be confirmed by the owner.** The law
is not empirical; no coefficient is taken from the source.

## Equations

| Quantity | Equation | Kind |
| --- | --- | --- |
| Annulus area | A = π (r_t² − r_h²) | geometry |
| Mean radius | r_m = √((r_t² + r_h²)/2) (splits A into two equal areas) | definition |
| Blade speed | U = ω r_m | kinematics |
| Axial velocity | c_x = Q / A (uniform) | continuity |
| Outlet swirl | c_θ2 = Δp_t / (ρ U) (c_θ1 = 0, loss-free) | Euler |
| Relative inlet angle | β1 = atan2(U, c_x) | triangle |
| Relative outlet angle | β2 = atan2(U − c_θ2, c_x) | triangle |
| Absolute outlet angle | α2 = atan2(c_θ2, c_x) | triangle |
| Relative speeds | W1 = √(c_x² + U²), W2 = √(c_x² + (U − c_θ2)²) | triangle |
| Flow coefficient | φ_m = c_x / U | definition |
| Work coefficient | ψ_E = c_θ2 / U = Δp_t / (ρ U²) | definition |
| de Haller ratio | W2 / W1 | definition; **reported only** |

Angles are measured from the axial direction. The relative tangential velocity is w_θ = U − c_θ, so
β is positive when the relative flow points against the direction of rotation; β2 < 0 means the
relative flow leaves turned past the axial direction.

## Assumptions

- **The input fan total pressure is taken as the rotor's ideal Euler total-pressure rise.** Euler's
  rise includes the dynamic pressure of the outlet swirl, ½ρc_θ2². ISO 5801 fan total pressure uses
  the conventional outlet dynamic pressure from the mean axial velocity and does not credit swirl.
  For a rotor without a stator, equating the two counts the swirl as useful and so **overstates the
  fan total pressure delivered**. At the l1_001 illustrative duties ½ρc_θ2² is 110.3 Pa of 150 Pa
  (case A) and 7.8 Pa of 40 Pa (case B). Recovering or discounting the swirl (a stator, or an
  efficiency) needs data that is not sourced (`UNSOURCED`); it is recorded here, not applied.

- No inlet swirl (c_θ1 = 0); uniform axial velocity, equal at rotor inlet and outlet.
- One mean radius represents the rotor; no radial equilibrium or spanwise work distribution.
- **Loss-free.** The swirl computed is the least that can deliver Δp_t. A real rotor needs more
  (hydraulic efficiency below 1). No efficiency, loss, deviation or blockage value is sourced
  (`UNSOURCED`), so none is applied and every output is the ideal value. These are needed before
  blade angles can be set from this model (step 5 of the plan) and await the owner's source choice
  (the airfoil data decision).
- Any limit on the de Haller ratio or on ψ_E is empirical and unsourced: none is applied.

## Validity

Every input finite and strictly positive, so r_h > 0 (a hubless rotor is outside the model); r_h < r_t; and the incompressibility limits of ADR-003 D3:
Δp_t ≤ ε γ p₁ and M_tip²/2 ≤ ε with U_tip = ω r_t. Outside them the model returns
`out_of_validity`, never a number.

## What the hand calculation shows

At the illustrative duty Q = 0.05 m³/s, Δp_t = 150 Pa, 2000 rpm, r_t = 59.5 mm, r_h = 20 mm the
loss-free swirl (13.7 m/s) already exceeds the mean blade speed (9.3 m/s): ψ_E = 1.47 and β2 < 0.
The model reports these numbers and applies no limit. At 40 Pa, ψ_E = 0.39 and β2 = 48.1°.

# fans.axial.free_vortex@0.1.0: free-vortex blade sections (L1 predicted)

Kernel: `kernel/products/fans/families/axial_ducted/free_vortex.{hpp,cpp}` (product operation
`free_vortex_design`). Reference: `python/cemkit/reference/fans/axial/free_vortex.py` (PHY-005,
agreement 1e-12). Hand calculation: `tests/hand_calcs/proposed/l1_002_free_vortex_sections.yaml`.
CLI: `cemkit design fan <spec-id> <params>` (geometry built by the M3 recipe).

## Fundamental law

Euler's turbomachinery equation at each radius, incompressible, no inlet swirl:
Δp_t = ρ U c_θ2 with U = ω r. Requiring the same Δp_t at every radius gives the free vortex
r c_θ2 = Δp_t / (ρ ω) = constant (Dixon & Hall, *Fluid Mechanics and Thermodynamics of
Turbomachinery*, 7th ed., 2014, ch. 6; chapter and equation numbers to be confirmed by the owner).

## Equations (station radius r, chord c)

| Quantity | Equation | Kind |
| --- | --- | --- |
| Axial velocity | c_x = Q / (π (r_t² − r_h²)), uniform | continuity (mean line) |
| Outlet swirl | c_θ2 = Δp_t / (ρ ω r) | Euler, free vortex |
| Relative angles | β1 = atan2(ω r, c_x), β2 = atan2(ω r − c_θ2, c_x) | triangle |
| Camber angle | θ = β1 − β2 | zero incidence and deviation |
| Stagger (from axis) | ξ = (β1 + β2)/2 | circular arc symmetric about mid-chord |
| Camber height | h = (c/2) tan(θ/4) | circular-arc geometry, R = c/(2 sin(θ/2)) |
| de Haller ratio | W2/W1 | reported only |
| Reaction | R = 1 − c_θ2/(2U) | Dixon & Hall ch. 5 (c_x constant, c_θ1 = 0) |

Stations are equally spaced from r_h to r_t; the chord varies linearly from `chord_hub` to
`chord_tip`; the thickness is constant. An extra root station `embed_depth` inside the hub repeats the
hub section so the blade fuses with the hub (geometry only).

## Assumptions (recorded, not checked)

- All of fans.axial.mean_line's (axial-mean-line.md), including defect D1: Δp_t is the ideal Euler
  rise, which includes swirl dynamic pressure that a rotor without a stator does not deliver.
- Loss-free (UNSOURCED efficiency): the swirl is the least that gives Δp_t.
- Zero incidence and zero deviation: the blade metal angles equal the flow angles. Real cascades
  deviate (Carter's or Howell's rules); no deviation rule is sourced, so none is applied (UNSOURCED).
- Free vortex: no radial equilibrium correction is needed for this swirl distribution.
- Chords, thickness, hub radius and station count are stated inputs (placeholders in
  `examples/fan_design_trial.yaml`), not sizing results: no solidity or loading rule is sourced.

## Sign convention (ADR-016)

Flow along +z, rotation counter-clockwise viewed from the inlet. At the blade on +x the relative
flow moves towards +y at angle β from +z. The geometry recipe's stagger is −ξ and the camber
positive (bows towards +y at stagger 0). `test_free_vortex.cpp` checks that the built section's end
tangents follow β1 and β2.

## Validity

That of fans.axial.mean_line (positive inputs, r_h < r_t, ADR-003 D3 incompressibility), plus
positive chords, thickness and embed depth, at least 3 stations, embed_depth < r_h and thickness below
every chord. Outside it: `out_of_validity`, never a number.

## Known behaviour

At the TRIAL duty with the placeholder 40 mm hub the free vortex loads the hub heavily (β2 < 0,
θ ≈ 60°, W2/W1 ≈ 0.56) while the tip is almost flat. The model reports this and applies no limit: a
de Haller limit needs a cited source (HI-026). A larger hub, more blades or a different vortex
distribution change this; none is chosen here.

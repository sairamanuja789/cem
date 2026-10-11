# ADR-018: Vortex exponent definition and the general vortex model

- Status: proposed (needs owner review)
- Date: 2026-10-11

## Context

docs/architecture.md section 5 listed the design vortex exponent as "0 (free vortex) to 1
(forced)", which is not the textbook convention. The trial review (HI-031) asked for the textbook
form and for radial equilibrium once the axial velocity is no longer uniform.

## Decision

1. **Definition:** c_θ2(r) = K rⁿ with c_θ1 = 0 (no inlet guide vanes). n = −1 is the free vortex,
   n = 0 constant swirl, n = 1 the forced vortex. Search range [−1, 1]. Source: Dixon & Hall, 7th
   ed., three-dimensional flows in axial turbomachines (pages to be confirmed by the owner).
2. **Axial velocity:** for n ≠ −1, c_x2(r) follows from simple radial equilibrium behind the rotor
   with loss-free Euler work and a uniform inlet, scaled to the duty flow by continuity. K is set so
   the mass-averaged loss-free total-pressure rise equals the duty
   (docs/models/fans/axial-vortex.md).
3. **A separate model,** `fans.axial.vortex@0.1.0` (operation `vortex_design`). The free vortex
   (`fans.axial.free_vortex@0.1.0`, l1_002) stays unchanged; `cemkit design fan` uses the general
   model only when the design parameters state `vortex_exponent`.
4. **K is a plain double in SI** in the kernel: its unit, m^(1−n)/s, depends on n, so no fixed
   mp-units type exists. A = c_x2² at the hub is also stored as a double (m²/s²).
5. **No silent repair:** a duty that needs reverse flow behind the rotor is `out_of_validity`; a
   section that needs negative camber is refused by the geometry recipe.

## Consequences

- docs/architecture.md section 5 now reads "−1 (free vortex) to 1 (forced vortex)".
- Hand calculation l1_003 covers n = −1 (reproduces l1_002) and n = 0; the owner moves it to
  `verified/` after checking.
- On the TRIAL duty, at the hub sizes examined (28 mm for n from −1 to 1, 20 mm for n = 0), n > −1
  does not unload the hub (model page, findings): the next levers are hub size, solidity and the
  measured duty (HI-025, HI-029).

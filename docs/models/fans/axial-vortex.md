# fans.axial.vortex@0.1.0: general vortex blade sections with simple radial equilibrium (L1 predicted)

Kernel: `kernel/products/fans/families/axial_ducted/vortex.{hpp,cpp}` (product operation
`vortex_design`). Reference: `python/cemkit/reference/fans/axial/vortex.py` (PHY-005, agreement
1e-9 on random designs). Hand calculation: `tests/hand_calcs/proposed/l1_003_vortex_exponent.yaml`.
CLI: `cemkit design fan` with `vortex_exponent` in the design parameters; `cemkit optimize fan`
search variable `vortex_exponent` (bounds [-1, 1]). Decision: ADR-018 (HI-031).

## Fundamental laws

- Euler's turbomachinery equation, incompressible, loss-free, no inlet swirl:
  h02 − h01 = U c_θ2 with U = ω r.
- Swirl distribution c_θ2 = K rⁿ: n = −1 free vortex (r c_θ2 constant, equal work at every radius),
  n = 0 constant swirl, n = 1 forced vortex (solid-body rotation).
- Simple radial equilibrium behind the rotor, uniform inlet total enthalpy:
  d(c_x2²/2)/dr = d(ω K r^(n+1))/dr − (c_θ2/r) d(r c_θ2)/dr.

Source: Dixon & Hall, *Fluid Mechanics and Thermodynamics of Turbomachinery*, 7th ed., 2014,
three-dimensional flows in axial turbomachines (general vortex design, simple radial equilibrium);
reaction as defined in the axial-compressor chapter. **Chapter and page numbers to be confirmed by
the owner (HI-031).**

## Solution

Integrating from the hub with A = c_x2²(r_hub):

c_x2²(r) = A + 2ωK (r^(n+1) − r_hub^(n+1)) − 2(n+1)K² I(r),
I(r) = r_hub^(2n) expm1(2n ln(r/r_hub)) / (2n), or ln(r/r_hub) for n = 0.

Two conditions fix A and K:

| Condition | Equation |
| --- | --- |
| Continuity | Q = ∫ 2π r c_x2 dr (hub to tip) |
| Duty | Δp_t = ρ ω K ∫ r^(n+1) c_x2 2π r dr / Q (mass-averaged loss-free Euler rise) |

Numerics (choices, not engineering constants): composite Simpson's rule on 200 intervals; A by
bisection for each K, K by bisection, each run until the bracket cannot shrink. The kernel repeats
the reference's algorithm and order of operations. For n = −1 the equilibrium terms vanish, so
c_x2 = c_x1 and K = Δp_t/(ρω): the free vortex (l1_003 case A, agreement 1e-10).

Sections at each station: β1 = atan2(U, c_x1), β2 = atan2(U − c_θ2, c_x2), θ = β1 − β2,
ξ = (β1 + β2)/2, h = (c/2) tan(θ/4) (zero incidence and zero deviation, UNSOURCED, HI-027),
de Haller ratio W2/W1, reaction R = 1 − (c_x2² − c_x1² + c_θ2²)/(2 U c_θ2).

## Validity

That of `fans.axial.free_vortex@0.1.0` (mean line and section checks), plus:

| Check | Range | Source |
| --- | --- | --- |
| Vortex exponent n | [−1, 1] | docs/architecture.md section 5 search range (corrected wording, ADR-018) |
| Axial velocity behind the rotor | c_x2² > 0 at every quadrature node and station | simple radial equilibrium has no meaning with reverse flow |

When the duty can only be reached with reverse flow somewhere behind the rotor, the model returns
`out_of_validity` (subject `vortex_exponent`), never a number.

## Findings on the TRIAL duty (l1_003)

Moving work outward (n > −1) removes swirl at the hub, but radial equilibrium then lowers the hub
axial velocity behind the rotor. With a 28 mm hub, the hub de Haller ratio is 0.633 (n = −1), 0.614
(n = −0.5) and 0.622 (n = 0). At n = 0 the hub section needs **negative camber** (θ = −15.4°) and
reaction 1.29, which the geometry recipe refuses (camber must be in [0, c/2)). With a 20 mm hub,
n = 0 reaches only about 12 Pa before reverse flow. Raising n alone therefore does not unload the
hub at this duty; a larger hub, more solidity, a lower duty or a different speed are the levers.

## Fidelity and limits

L1 predicted. Loss-free; zero deviation (HI-027); no blade blockage; simple radial equilibrium
(no streamline curvature or radial velocity); uniform inlet. Not validated (no rig data, AX-017).

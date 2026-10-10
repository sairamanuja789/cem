# ADR-016: v1 rotor configuration and trial duty point

Status: accepted (owner decisions, 2026-10-11)

## Context

Step 5 of the build order (connect the L1 mean line to the geometry recipe) needs the duty point,
the hub diameter, the blade count, the rotation sense, the depth and the blade section type. The L1
model (fans.axial.mean_line@0.1.0, `l1_001`) showed that the illustrative duty used so far, 150 Pa
at 0.05 m³/s and 2000 rpm, needs outlet swirl above the blade speed even with no losses
(ψ_E = c_θ2/U_m ≈ 1.47). On the tip-speed pressure coefficient ψ = 2Δp_t/(ρU_tip²),
U_tip = π × 0.12 m × 2000/60 ≈ 12.6 m/s gives ψ ≈ 300/(1.18 × 12.6²) ≈ 1.6, beyond the range of
axial machines; a stator does not close a gap that size.

## Decision

1. **No stator in v1.** The frame struts are not a designed stator. A stator stays a later option.
2. **Blade count is a design variable**, bounds 3–11 (spec); the initial classical design uses 7.
3. **Depth 25 mm** (within AX-015).
4. **Rotation counter-clockwise viewed from the inlet.** This sets only the blade handedness.
5. **Cambered-plate sections**, pending the owner's choice of data source for deviation and losses
   (those stay `UNSOURCED` until then).
6. **Hub diameter from the motor:** set from a commercial 120 mm fan motor that the owner will
   measure; until then it stays unknown (`product.motor.bore_diameter`).
7. **The 150 Pa example duty is retired** for design work. It remains only as a test input where
   tests exercise warnings or large loadings.
8. **Trial duty until the real one is supplied:** 0.028 m³/s at 18 Pa fan static,
   provenance `default`, `provisional: true`, labelled TRIAL everywhere
   (`examples/axial_120_trial.yaml`). It exercises the loop; it is not a requirement, and no
   result computed from it is reported as meeting a requirement.
9. The real duty will be "match or beat a measured commercial 120 mm reference fan at one
   operating point" (ADR-002, to be written), measured on the owner's rig.

M5 (simulation) and M6 (optimisation) do not start until the step-5 inputs (duty, hub, data source)
are supplied. After step 5 the next milestone is printing and measuring the initial design.

## Consequences

- `examples/axial_120.yaml` keeps the duty unknown (the v1 spec as stated); the trial variant is a
  separate file so the golden outputs and the unknown-duty questions stay unchanged.
- The ψ ≈ 1.6 finding is recorded as HI-024 in `docs/open-items.md`. The typical axial range of ψ
  must be cited from the owner's chosen textbook before any limit is coded (CLAUDE.md rule 7).

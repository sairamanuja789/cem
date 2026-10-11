# ADR-019: Provisional design duty from public manufacturer data

- Status: proposed (needs owner review; the TRIAL duty of ADR-016 stays in force until accepted)
- Date: 2026-10-12
- Requirements affected: SPEC-004, AX-003, AX-004, REP-002, REP-004

## Context

- **The current duty is likely unreachable.** The TRIAL duty (ADR-016: 0.028 m³/s at 18 Pa fan
  static, 2000 rpm, 120 mm) has produced no trusted candidate (HI-029). Public data now suggests it is
  out of reach for this class of fan.
- **No physical reference fan:** HI-025 stays open.
- **Public data** (`data/reference_fans/`):
  - **ebm-papst 4412 F** (2900 rpm): a vector-extracted curve of "Δp_f" against flow. The shutoff is
    54.6 Pa, zero pressure at 173 m³/h, and an inflection near 45–60 CFM. **The pressure type is
    undefined** in the datasheet (HI-032).
  - **Noctua NF-F12 industrialPPC-2000** (2000 rpm): maximum ratings only, 121.8 m³/h and 38.6 Pa,
    at different operating points. No curve. The values come from retailer listings, as the primary
    page has not yet been read (HI-033).
- **Fan laws** (`scale_fan_laws`, docs/models/fans/l0-similarity.md: Q ∝ N, Δp ∝ N² at fixed
  diameter and density) map the ebm-papst curve to 2000 rpm:
  - shutoff ≈ 26 Pa;
  - zero pressure ≈ 0.033 m³/s;
  - at the TRIAL flow 0.028 m³/s, about 7.2 Pa (all inferred), against the TRIAL target of 18 Pa.
  - The scaling assumes geometric similarity and ignores Reynolds-number effects, and the 4412 F is
    a different fan from ours.

## Options considered

1. **Keep the TRIAL duty.** Pros: no change. Cons: it is evidently out of reach for a rotor-only
   120 mm fan at 2000 rpm, so it drives fruitless searches.
2. **A provisional duty inferred from the scaled public curve,** near its best region past the stall
   inflection. Pros: realistic and traceable. Cons: inferred from a different fan, with an
   undefined pressure type.
3. **Wait for a measured reference fan.** Pros: best evidence. Cons: blocks design work indefinitely.

## Decision (proposed)

Option 2:
- **Duty:** **Q = 0.020 m³/s at 12 Pa, 2000 rpm, 120 mm**, provenance `inferred, provisional`,
  labelled **PROVISIONAL (public data)** everywhere, as TRIAL is today.
- **Derivation:** the ebm-papst points at 60 CFM (25.67 Pa) and 65 CFM (24.58 Pa), scaled to
  2000 rpm, give 0.0195–0.0212 m³/s at 12.2–11.7 Pa.
- **Pressure type:** the target is entered as **fan static only if** HI-032 shows that the
  ebm-papst Δp_f is the static pressure. If it is fan total pressure, the target is entered as fan
  total.
- **Spec:** until HI-032 is resolved, the spec carries the value with the pressure type marked
  unconfirmed. The design commands refuse a duty whose pressure type is not stated (SPEC-004), so
  **this ADR cannot be applied to the spec before HI-032.**

## Consequences

- **The searches** get a target that a 120 mm fan at 2000 rpm can plausibly reach. Every report
  says it is inferred from a different fan and is not a requirement, a measurement or
  manufacturer-equivalent.
- **To revisit:**
  - when the Δp_f definition is found (HI-032);
  - when the Noctua primary page is read (HI-033);
  - when a physical reference fan is measured (HI-025), which supersedes this ADR.
- **No validation claim** follows from this ADR (REP-004).

# Trade study plan: rotor only vs rotor + stator (ADR-016 reassessment)

Status: **plan only.**
- No stator is implemented.
- Rotor-only stays the v1 baseline unless this study, completed and documented, justifies a change
  (owner decision 2026-10-12).
- Noctua's geometry and vane count are not used.

## Question

Can a rotor-only 120 mm fan at 2000 rpm reach the design duty at acceptable efficiency, or does an
outlet guide vane (stator) row earn its cost in parts, length and noise risk?

## Configurations (same duty, envelope, speed and blade design method)

1. **R (rotor only):** the current baseline.
2. **R+S (rotor plus our own stator):**
   - the vane count is a design variable, chosen to avoid coupling with the rotor blade count (the
     rule and its source are to be cited when used);
   - the vanes are cambered to remove the rotor exit swirl;
   - they must fit inside the 25 mm depth.

## Metrics

| Metric | Source of the number |
| --- | --- |
| Fan static pressure at the duty flow (ISO 5801 total-to-static) | L2 CFD (trusted only) |
| η_ts at the duty | L2 CFD (trusted only) |
| Exit swirl dynamic pressure lost (R) or recovered (R+S) | L1, then L2 |
| Stall margin (flow at peak pressure relative to the duty) | L2 sweep |
| Axial length, part count, printability, assembly | geometry and process rules |
| Blade–vane interaction risk (noise proxy) | rule check only; no acoustic model in v1 |

## Method

1. **L1:** add a stator row to the mean line, loss-free first. Losses come only after HI-027
   (Carter, Lieblein) is sourced; the Python reference comes first, with hand calcs and a physics
   review.
2. **L2:**
   - extend the CFD template with a stationary stator zone;
   - the rotor/stator interface (mixing plane or frozen rotor) needs its own ADR before code;
   - use the same trust gate and a mesh study for both configurations.
3. **Decision rule** (the owner sets the threshold): change the baseline only if trusted CFD shows R
   cannot meet the duty, or R+S gains more than the threshold in static pressure or η_ts, with
   acceptable parts and printability.

## Prerequisites (blockers)

- **B1:** the duty and its pressure definition (ADR-019, HI-032).
- **B3:** the FAN-01 workflow check (ADR-020) passed, so CFD comparisons are credible.
- **The interface ADR** for the stator zone.

## Effort estimate

- L1 stator: about 1 day.
- CFD template extension: 2–3 days.
- Runs: 2–4 days of machine time.

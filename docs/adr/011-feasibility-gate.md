# ADR-011: L0 feasibility gate: check order, unsourced ranges, nearest feasible duty

- Status: proposed (needs owner review)
- Date: 2026-10-10
- Requirements affected: SEL-002, SEL-004, PHY-003, PHY-005

## Context
T08 builds the feasibility gate (`kernel/products/fans/common/feasibility.hpp`). SEL-004 asks for
the violated limit and the nearest feasible duty; SEL-002 asks for family specific-speed ranges
from a cited source. ADR-003 D3 fixes the limits, but three points were left open, and work was
autonomous, so they are recorded here for the owner:
1. what the gate does when a family's range has no verified source (all of them today, HI-003);
2. which duty counts as "nearest" for a specific-speed range;
3. the order of the checks and what each reports when it cannot be applied.

## Options considered
1. Unsourced range: (a) fail the duty; (b) pass it silently; (c) report `range_unsourced`, neither
   pass nor fail, so the duty is unconfirmed. (a) blocks every campaign until sources exist;
   (b) hides missing evidence and breaks CLAUDE.md rule 7.
2. Nearest duty for ω_s: (a) hold Q, move Δp_t; (b) hold Δp_t, move Q; (c) smallest change in
   (ln Q, ln Δp_t). (a) and (b) each favour one requirement arbitrarily; (c) is symmetric and
   scale-free.
3. Order: inputs, pressure limit, tip-speed limit, family range, each reported (not stopping at
   the first failure), so a report lists every violated limit.
4. Overall result: (a) a boolean "feasible" = no violation; (b) a three-way verdict infeasible /
   unconfirmed / confirmed. (a) would call every duty "feasible" while all ranges are unsourced,
   and downstream code reads the flag, not the message.

## Decision
Report an unsourced range as `range_unsourced` (option 1c), give the nearest ω_s duty by the
smallest log-space change (option 2c, with up to 8 one-ulp pressure steps to absorb rounding, and
none if the result breaks the pressure limit), run all checks in the fixed order of option 3, and
return the three-way verdict of option 4b. Proposed nearest duties are labelled L0 predicted with
model `fans.feasibility@1.0.0` and hold for their own limit only.

## Consequences
- Every duty is "unconfirmed" until HI-003 supplies cited ranges; reports must say "range
  unsourced", never "feasible" without qualification.
- If the owner prefers 2a or 2b, only `nearest_in_range` in the kernel and the reference change,
  plus the model version (`fans.feasibility` 1.0.0 → 2.0.0).
- A tip-speed pressure limit (ψ_max) and specific-diameter ranges need cited data first; they are
  not checked.

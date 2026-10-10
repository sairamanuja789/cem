# L0 feasibility gate (SEL-002, SEL-004)

Kernel: `kernel/products/fans/common/feasibility.hpp` (model `fans.feasibility@1.0.0`).
Reference: `python/cemkit/reference/fans/common/feasibility.py`. The two are compared by ctest
`fans_l0_crosscheck` (see `l0-similarity.md`, Evidence). Decisions: ADR-003 D3 (the limits) and
ADR-011 (**proposed, needs owner review**: check order, the "range unsourced" status, the
nearest-feasible-duty rule).

The gate runs before any geometry exists (SEL-004). It takes a duty (Q, Δp_t, fan total pressure),
the air state, the family's specific-speed range and, if known, the rotational speed ω and rotor
tip diameter D. Invalid inputs return `out_of_validity` (same subjects as the L0 functions); a
sourced range with non-finite bounds or not 0 < min ≤ max returns `invalid_input` with subject
`family_range`. Otherwise it returns a report with one check per limit, always in this order.

| # | Limit | Rule | Status when it cannot be applied | Nearest feasible duty |
| --- | --- | --- | --- | --- |
| 1 | `incompressible_pressure` | Δp_t ≤ ε γ p₁ (ADR-003 D3) | – | same Q, Δp_t = ε γ p₁ |
| 2 | `incompressible_tip_speed` | U = ω D / 2 ≤ √(2ε) a₁ (ADR-003 D3) | `not_computable` without ω and D | none: a duty change cannot fix the tip speed |
| 3 | `family_specific_speed_range` | ω_s inside the family's cited range | `range_unsourced` without a cited range; `not_computable` without ω or when check 1 failed | log-space rule below |

Each check has a status (`pass`, `violated`, `range_unsourced`, `not_computable`), a message and,
when violated, the error that states the limit (code, subject, details `value`, `bounds`, `model`).
Checks 1 and 2 reuse the L0 error exactly (code `out_of_validity`, model `fans.l0@1.0.0`). Check 3
uses code `infeasible_requirement`, subject `specific_speed`, details `bounds = [min, max]`,
`family`, `model = fans.feasibility@1.0.0` and `value`. The report also carries ω_s, labelled L0
predicted, when it can be computed.

`verdict()` is `infeasible` when a check is `violated`, `confirmed` when every check passed, and
`unconfirmed` otherwise (a `range_unsourced` or `not_computable` check). A `range_unsourced` check
neither passes nor fails the duty, and reports must say "range unsourced". There is deliberately no
"feasible" flag: an unconfirmed duty must not read as feasible.

A proposed nearest feasible duty is computed, so its flow and pressure are labelled L0 predicted
with model `fans.feasibility@1.0.0`. It is nearest for **its own limit only**: it is not checked
against the other limits (for example the duty at the pressure limit may still fall outside the
family range, which is not computable while the pressure limit is violated).

## Rotor tip diameter from the spec (owner decision D6, provisional)

For `fans.axial_ducted`, `axial_ducted::rotor_tip_diameter(spec)` gives check 2 its D (ADR-003 D4):

- `product.size_reference` = `duct_inner_diameter`: D = `product.nominal_size` − 2 ×
  `product.tip_clearance_min`. With `examples/axial_120.yaml` (AX-001 120 mm, AX-009 0.5 mm) this is
  0.119 m, the D of the verified hand calculation `l0_001`. Provenance `default`, provisional.
- `product.size_reference` = `rotor_tip_diameter`: D = `product.nominal_size` as stated, with that
  field's provenance; no clearance is involved.
- A `frame` or unknown size reference, or an unknown nominal size or tip clearance:
  no D, so check 2 stays `not_computable`. No clearance is ever assumed.
- A non-positive nominal size is `spec_rejected` on `product.nominal_size`; a clearance below zero
  or of half the duct diameter or more is `spec_rejected` on `product.tip_clearance_min`.

Open item HI-023: the owner confirms frame vs rotor diameter (AX-001).

## Family ranges (SEL-002)

`data/fans/family_ranges.yaml` lists ω_s ranges per family, with ω in rad/s and SI Q, Δp_t, ρ.
Cordier σ or rpm-based specific speeds (n_q, N_s) are different numbers and must be converted
explicitly before entry. Each needs a citation from a primary
source; a range without one is `UNSOURCED` with null bounds, and a test
(`tests/python/test_reference_feasibility.py`) fails if an unsourced entry carries numbers. Today
every family is `UNSOURCED` (open item HI-003): the gate reports "range unsourced" for all of them.
The kernel takes ranges as data (`FamilyRange`); it does not read the YAML file. Loading it is the
orchestration layer's job (T11 onwards).

## Nearest feasible duty for the specific-speed range (ADR-011, proposed)

ω_s = ω √Q / (Δp_t/ρ)^(3/4), so at fixed ω and ρ, ln ω_s changes by ½ a − ¾ b when ln Q changes by a
and ln Δp_t by b. The nearest feasible duty is the one with the smallest a² + b² that puts ω_s on
the crossed bound ω_s*:

  c = ln(ω_s* / ω_s),  a = c · ½ / 0.8125,  b = −c · ¾ / 0.8125  (0.8125 = ½² + ¾²)

  Q' = Q eᵃ, Δp_t' = Δp_t eᵇ.

Rounding can leave ω_s(Q', Δp_t') a few ulps outside the range; Δp_t' is then stepped by one ulp at
a time towards the range, at most 8 times. If the duty is still outside, or Δp_t' breaks check 1,
no nearest duty is given. This is a metric choice, not a physical law; the owner may prefer
holding Q fixed (ADR-011).

## Not here

- A pressure-rise limit from the tip speed (Δp_t ≤ ψ_max ρ U² / 2) needs a cited ψ_max per family;
  none is sourced, so it is not checked.
- Specific-diameter ranges (Cordier line): the same sourcing rule applies; none are listed yet.

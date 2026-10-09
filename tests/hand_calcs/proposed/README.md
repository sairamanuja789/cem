# Proposed hand calculations

Each file shows its working. A human checks the arithmetic, then moves the file to `../verified/`
(protected). `tests/python/test_reference_l0.py` runs every file in both folders. Files in
`verified/` are the acceptance evidence. Files in `proposed/` must also pass, so a slip in either
the code or the working is caught early.

File format:
- `checks`: a list of calls into `cemkit.reference`.
- `args`: SI inputs. `fan_total_pressure` and `fan_static_pressure` are in Pa and wrapped in
  their types.
- `air`: optional overrides of `default_air()`.
- `expected` with `rel_tol`: the expected numeric result.
- `expected_error`: the kernel-shaped error (ADR-003 D6) instead of a number.

Illustrative inputs: the v1 fan's duty point is still unknown (AX-003, AX-004). The flow,
pressure and power values below are therefore round illustrative numbers, not requirements.
D_tip = 0.119 m is 120 mm − 2 × 0.5 mm, using the AX-001 duct size and the AX-009 tip clearance.
ω = 2000 rpm (AX-002).

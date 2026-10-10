# cemkit C ABI and Python binding (T11)

Header: `kernel/cemkit/capi/cemkit.h`. Python: `cemkit.kernel` (typed API) over the nanobind
module `cemkit._kernel` (`bindings/python/module.cpp`). Decisions: ADR-013 (proposed).
Requirements: PERF-002 (batches in one call), MAINT-005 (semantic versioning), PHY-005.

## Conventions

- JSON (UTF-8) in and out. Every number is in its coherent SI unit (m, m³/s, Pa, rad/s, W, kg/m³,
  K, J/(kg·K), rad). Unit conversion happens only in the spec compiler.
- Every call returns a `cemkit_status`:

  | Status | Meaning | `*out_json` |
  | --- | --- | --- |
  | `CEMKIT_OK` (0) | result | response document |
  | `CEMKIT_FAILED` (1) | the kernel's classified error | `{"error": {...}}` |
  | `CEMKIT_INVALID_ARGUMENT` (2) | a NULL pointer | untouched or NULL |
  | `CEMKIT_INTERNAL_ERROR` (3) | a defect or out of memory | error or NULL |

  An error object is `{"code", "subject", "message", "details", "describe"}`, the shape of
  `cemkit::core::Error` and `cemkit.errors.Error`. A request that is not the documented shape is
  `invalid_input` with the offending key as subject (in a batch: `cases[<i>].<key>`).
- Strings returned by the library are released with `cemkit_free`. No exception crosses the ABI;
  every function is re-entrant and stateless.
- Every OK response carries `abi_version`, `kernel_version`, `models` ({model: version}) and
  `plugins` ({family: plugin version}), for the run metadata (STORE-002).
- A labelled value is `{"value", "fidelity", "fidelity_label", "model"}`; `fidelity_label` is the
  report wording ("L0 predicted").
- Python: `cemkit.kernel` raises `KernelError` (with `.error`) on `CEMKIT_FAILED` and
  `KernelInternalError` otherwise. The GIL is released while the kernel runs.

## Versioning (MAINT-005)

`CEMKIT_ABI_VERSION_{MAJOR,MINOR,PATCH}` = 0.2.0, returned by `cemkit_abi_version()` (0.2.0, T14:
responses gained `models`, `plugins` and `fidelity_label`, air keys became optional, geometry smoke
gained `include_data`). A breaking
change to a signature or to a JSON format bumps MAJOR; an added function or key bumps MINOR.
`cemkit.kernel.ABI_REQUIRED` = (0, 2): the loaded library must have the same MAJOR and at least
that MINOR (`cemkit.kernel.check_abi()`).

## Calls

### `cemkit_l0_batch` / `kernel.l0_batch(cases)` (PERF-002)

Request `{"cases": [case, ...]}`, response `{"results": [row, ...]}` in case order. A case:

```json
{"id": "any JSON value", "function": "<name>", "args": {...},
 "air": {"density", "temperature", "pressure", "gamma", "gas_constant"},
 "air_new": {...}, "range": {...}}
```

`air` is optional, and so is each of its keys (the kernel's default air, AX-005 with the 1976
constants, fills the rest); `air_new` is the target air of
`scale_fan_laws`; `range` is the family range of `check_feasibility`. Functions and `args`:

| function | args |
| --- | --- |
| `tip_speed` | `omega`, `d_tip` |
| `max_fan_total_pressure`, `max_tip_speed`, `speed_of_sound` | – |
| `flow_coefficient` | `flow`, `d_tip`, `omega` |
| `pressure_coefficient` | `fan_total_pressure`, `d_tip`, `omega` |
| `specific_speed` | `omega`, `flow`, `fan_total_pressure` |
| `specific_diameter` | `d_tip`, `flow`, `fan_total_pressure` |
| `conventional_dynamic_pressure` | `flow`, `d_duct` |
| `fan_total_from_static` | `fan_static_pressure`, `flow`, `d_duct` |
| `scale_fan_laws` | `point` {`omega`, `d_tip`, `flow`, `fan_total_pressure`, `power`}, `omega`, `d_tip` |
| `ideal_gas_density` | `pressure`, `temperature` |
| `check_feasibility` | `flow`, `fan_total_pressure`, optional `omega`, `d_tip` |

A row is `{"id", "value", "fidelity", "model"}` (plus `provenance` and `rule` for
`fan_total_from_static`; `value` is an object for `scale_fan_laws`), `{"id", "report"}` for
`check_feasibility`, or `{"id", "error"}` when that case is out of validity. A failing case does not
fail the batch. `id` defaults to the case's position.

### `cemkit_feasibility` / `kernel.feasibility(request)` (SEL-004)

Request: `{"flow", "fan_total_pressure", ["omega"], ["d_tip"], ["air"], "range": {"family",
"specific_speed_min", "specific_speed_max", "source"}}`. Response `report`: `verdict`
(`infeasible`, `unconfirmed`, `confirmed`), `specific_speed` (labelled or null) and `checks`
(`limit`, `status`, `message`, `violation`, `nearest_feasible` with labelled `flow` and
`fan_total_pressure`). Rules: `docs/models/fans/feasibility.md`.

Spec form (UC-03): `{"spec": <spec document>}`. The kernel compiles the spec and takes the duty
from `product.duty.flow` and `product.duty.pressure`, the speed from `product.rotational_speed`
and the air density and temperature from `air.*` (the rest of the air from the kernel's defaults).
A fan static pressure is converted to fan total (ADR-003 D1) with the duct diameter
`product.nominal_size`, which requires `product.size_reference` = `duct_inner_diameter` (AX-001);
the converted value is reported under `inputs.fan_total_pressure`, labelled and `derived`. The
rotor tip diameter comes from the family (owner decision D6, provisional; `fans.axial_ducted`:
D_tip = `product.nominal_size` − 2 × `product.tip_clearance_min` for a duct inner diameter) and is
reported under `inputs.d_tip` with its unit, provenance (`default`), `provisional`, `rule` and the
spec fields it came from; with a speed it makes the tip-speed check computable. Without it (frame
or unknown size reference, unknown clearance) the tip-speed check is `not_computable`. The family's own
feasibility hook supplies the range (`fans.axial_ducted`: `UNSOURCED`). An unknown duty point is
`spec_rejected` naming the field. The response adds `spec` (`spec_id`, `revision`, `family`) and
`inputs` (each value with its unit and the spec field it came from).

### `cemkit_spec_compile` / `kernel.spec_compile(spec, autonomous_mode)` (SPEC-001..011)

Request `{"spec": <spec document>, "autonomous_mode": false}`. Essential fields come from the
family registry (`register_fan_families`); an unknown family is `spec_rejected` with subject
`family`. Response `spec`: `schema_version`,
`spec_id`, `revision`, `parent`, `family`, `title`, `fields` (by path: `provenance`, `provisional`,
`original_value`, `original_unit`, `si_value`, `si_unit`, `text_value`, `confidence`, `note`,
`pressure_kind`, `tolerance`), `questions`, `conflicts`, `has_unresolved_essential_unknowns`.

### `cemkit_geometry_smoke` / `kernel.geometry_smoke(request)` (GEO-001, GEO-002, GEO-004)

Request: `{"hub_radius", "hub_length", "sections": [{"radius", "chord", "thickness", "stagger"}],
"exports": [{"format": "step"} | {"format": "stl", "linear_deflection", "angular_deflection"}],
"include_data": false}`. Response: `backend`, `validity`, `topology`, `mass_properties` (`volume`,
`area`, `centroid`) and `exports` (`format`, `sha256`, `size_bytes`, and `data_base64` when
`include_data` is true). No file is written.

## Building and testing

- The `release` preset builds `cemkit._kernel` and copies it into `python/cemkit/` (gitignored);
  `scripts/check.sh` builds it before pytest. `tests/python/test_kernel_bindings.py` sends the
  whole cross-check population (hand calculations, edge cases, 10 000 in-range and 1 000
  out-of-range random states: 143 020 cases) in one call and compares it with the reference.
- Wheels: `uv build --wheel` (scikit-build-core; check.sh stage "wheel (ADR-013)").
- Benchmark: `PYTHONPATH=python uv run python scripts/bench_l0_batch.py`. On 2026-10-10 in the dev
  container (release build), 143 020 cases: JSON encode 394 ms, kernel call 1 224 ms (8.6 µs per
  case including JSON parse and serialisation), JSON decode 150 ms.

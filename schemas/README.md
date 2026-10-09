# cemkit JSON Schemas

These schemas are the contract between people, the Python orchestration layer and the C++ kernel
(ADR-009). They are JSON Schema 2020-12. Each schema is identified by a URN `$id`, and documents
refer to each other by those URNs, not by file paths.

| File | `$id` | What it describes |
| --- | --- | --- |
| `cemkit/v1/spec.schema.json` | `urn:cemkit:schema:v1:spec` | A requirement set (spec) and the shared field objects |
| `cemkit/v1/candidate.schema.json` | `urn:cemkit:schema:v1:candidate` | One design candidate |
| `cemkit/v1/result.schema.json` | `urn:cemkit:schema:v1:result` | The outcome of evaluating a candidate |
| `cemkit/v1/error-codes.json` | `urn:cemkit:schema:v1:error-codes` | Failure codes. **Generated**; never edit (see below) |
| `products/fans/v1/spec.schema.json` | `urn:cemkit:schema:products:fans:v1:spec` | The `product` block of a `fans.axial_ducted` spec |

Example: `examples/axial_120.yaml`, the v1 product from requirements section 7. Tests:
`tests/python/test_schemas.py`.

## Loading and validating (`python/cemkit/schemas.py`)

- **Loading.** Always load documents with `load_json` or `load_yaml`. JSON Schema cannot reject
  NaN or infinity, and Python's parsers accept them (NaN even passes `"minimum": 0`). These two
  loaders reject them.
- **Validating a spec.** A spec is valid when both of these hold:
  1. the platform schema accepts the whole document;
  2. the product schema of its `family` accepts the `product` block.

  `SchemaSet.spec_errors` runs both checks.
- **Families.** Each product schema lists the families it covers in `x-cemkit-families`. Adding a
  family therefore touches only its own schema file (FAM-002).
- **References.** The platform schemas never reference product schemas (ADR-009); a test enforces
  this. Product schemas may reference platform definitions.

## Field objects (SPEC-001)

Every input value is an object; a bare number is invalid.

| Member | Required | Meaning |
| --- | --- | --- |
| `value` | yes | A number (or a string for text fields), or `null` when the provenance is `unknown`. |
| `unit` | numeric fields | One of the unit strings accepted for that quantity, listed below. |
| `provenance` | yes | `user`, `image`, `derived`, `default` or `unknown` (requirements section 2). |
| `tolerance` | no | Either `{"minus": a, "plus": b}` in the field's own unit, or `{"relative": r}` (see below). Not allowed when the value is unknown. |
| `confidence` | when the provenance is `image` | A number from 0 to 1. Not allowed for any other provenance (IN-003, IN-004). |
| `provisional` | no | `true` for a default or assumption that still has to be confirmed (SPEC-007). |
| `note` | no | Free text. |

Accepted unit strings, all ASCII:

| Quantity | Units |
| --- | --- |
| length | `m`, `mm`, `cm`, `in` |
| volume flow rate | `m3/s`, `m3/min`, `m3/h`, `L/s`, `CFM` |
| pressure | `Pa`, `kPa`, `inH2O`, `mmH2O` |
| angular velocity | `rad/s`, `rpm` |
| density | `kg/m3` |
| temperature | `K`, `degC` |
| dynamic viscosity | `Pa*s` |
| ratio | `1`, `%` |

Tolerances:

- `relative` is a fraction of |value|: 0.05 means 5 %, even when the field's unit is `%`.
- An absolute band `{minus, plus}` is a difference. For an offset unit such as `degC` it converts
  with the scale factor only, never the offset.
- A zero-width band is allowed and means the value is exact as stated.

Sign rules that follow from the definitions:

- These must be strictly positive: lengths, densities, viscosities, volume flow rates, speeds and
  temperatures in K.
- Ratios must be non-negative.
- A temperature in `degC` is checked after conversion by the spec compiler.

Candidates and results hold kernel values, which are SI. Their `unit` must be one of the coherent
SI strings in `$defs/si_unit` of the spec schema (`m`, `m3/s`, `Pa`, `rad/s`, `N*m`, `W`, `1`, ...).

The schema only names the units. Converting them to SI is the job of the spec compiler (T07), which
records the original value and unit (SPEC-002). Its conversion factors need cited sources: the
water-column units `inH2O` and `mmH2O` in particular depend on a stated reference water
temperature and gravity.

## Fan-specific rules

- **Pressure (SPEC-004).** A known pressure must carry `kind`: `fan_total` (p_t2 − p_t1) or
  `fan_static` (p_s2 − p_t1, which is total-to-static), per ISO 5801. Any other kind is rejected,
  including static-to-static. Definitions are in `docs/models/fans/pressure-kinds.md`.
- **Pressure kinds in every schema.** Any field that accepts pressure units must declare `kind`; a
  test checks every schema.
- **Unknown pressure.** An unknown pressure may omit its kind, or state it before the value is
  known.
- **The spec compiler (T07) must never default `kind`.** Requirements section 7 says static is the
  *expected* type for a ducted fan, but expected is not stated.
- **Duty point (SPEC-011).** A known duty flow and a known duty pressure must each carry a tolerance.

## Results (COR-003, REL-002)

A result points to an exact spec revision and has one of two forms:

- **ok**: the result carries `metrics`. Each metric is a labelled value with `value`, `unit`,
  `fidelity` (the stored name, such as `l2_verified`) and `model` (`{name, version}`).
  `uncertainty` is required for `l2_verified` and `l3_validated` and forbidden for every other
  label.
- **failed**: the result carries exactly one `failure` whose `code` is from `error-codes.json`.

An ok result needs at least one metric, and a candidate needs at least one parameter.

## Open points

- Requirements section 2 defines provenance `default` as "taken from the documented defaults
  table". That table does not exist yet: the example's `default` values cite section 7 instead. The
  spec compiler task (T07) should add it under `data/` with sources.

## Versioning (MAINT-005, SPEC-009)

- The directory name carries the major version (`v1`). Every document states the exact version in
  `schema_version`. The v1 schemas accept `1.x.y` only.
- **Minor version**: backwards-compatible additions, such as a new optional member or a new
  accepted unit.
- **Major version**: any change that can reject a document that was valid before, such as a new
  required member, a removed unit or a narrowed range. It goes in a new directory (`v2`), and the
  old one is kept.
- **Spec revisions.** A spec is identified by `spec_id` plus `revision`. Specs are immutable: an
  edit creates `revision + 1`, with `parent` pointing to the revision it was edited from. Revision
  1 of a new spec has `parent: null`. Candidates and results always name the exact
  `{spec_id, revision}`. The store (T12) enforces immutability.
- **Error codes.** `cemkit/v1/error-codes.json` is generated from `kernel/cemkit/core/error.hpp` by
  `scripts/gen_error_codes.py`. A ctest fails if it drifts.

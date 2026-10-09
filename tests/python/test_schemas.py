"""T05: the JSON Schemas accept the v1 example and reject malformed documents.

Requirements: SPEC-001 (field objects, no bare numbers), SPEC-004 (typed pressure, no
static-to-static), SPEC-009 (versioned specs, results point to an exact spec revision), SPEC-011
(duty points carry tolerances), IN-004 (image fields carry a confidence), COR-003 (labelled
results), REL-002 (failure codes), MAINT-005 (schema semver).

A spec is valid when the platform schema accepts the document and the family's schema accepts its
"product" block (the platform never references product schemas; ADR-009).
"""

import copy
import json
from collections.abc import Callable
from pathlib import Path
from typing import Any

import jsonschema
import pytest

from cemkit import schemas as cs

ROOT = Path(__file__).resolve().parents[2]
SCHEMAS = ROOT / "schemas"
EXAMPLE = ROOT / "examples" / "axial_120.yaml"
SPEC = cs.SPEC
CANDIDATE = cs.CANDIDATE
RESULT = cs.RESULT

Doc = dict[str, Any]

SET = cs.load_schemas()
ALL = SET.schemas


def _errors(schema_id: str, document: Any) -> list[str]:
    return SET.errors(schema_id, document)


def spec_errors(document: Doc) -> list[str]:
    return SET.spec_errors(document)


def example() -> Doc:
    loaded = cs.load_yaml(EXAMPLE.read_text(encoding="utf-8"))
    assert isinstance(loaded, dict)
    return loaded


def edited(change: Callable[[Doc], None]) -> Doc:
    document = copy.deepcopy(example())
    change(document)
    return document


KNOWN_FLOW = {"value": 0.05, "unit": "m3/s", "provenance": "user", "tolerance": {"relative": 0.05}}
KNOWN_PRESSURE = {
    "kind": "fan_static",
    "value": 150.0,
    "unit": "Pa",
    "provenance": "user",
    "tolerance": {"minus": 0.0, "plus": 15.0},
}


def with_duty(flow: Doc, pressure: Doc) -> Doc:
    def change(d: Doc) -> None:
        d["product"]["duty"] = {"flow": flow, "pressure": pressure}

    return edited(change)


# --- the schemas themselves ---------------------------------------------------------------------


@pytest.mark.req("MAINT-005")
def test_every_schema_is_valid_2020_12_with_a_unique_urn_id() -> None:
    paths = sorted(SCHEMAS.rglob("*.json"))
    assert len(paths) == len(ALL) >= 5
    for path in paths:
        schema = json.loads(path.read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator.check_schema(schema)
        assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
        assert schema["$id"].startswith("urn:cemkit:schema:")


@pytest.mark.req("MAINT-005")
def test_platform_schemas_never_reference_product_schemas() -> None:
    def refs(node: Any) -> list[str]:
        if isinstance(node, dict):
            found = [node["$ref"]] if isinstance(node.get("$ref"), str) else []
            return found + [r for value in node.values() for r in refs(value)]
        if isinstance(node, list):
            return [r for item in node for r in refs(item)]
        return []

    platform = [s for uri, s in ALL.items() if not uri.startswith("urn:cemkit:schema:products:")]
    assert platform
    for schema in platform:
        for ref in refs(schema):
            assert ref.startswith(("#", "urn:cemkit:schema:v1:")), (schema["$id"], ref)


# --- the v1 example -----------------------------------------------------------------------------


@pytest.mark.req("SPEC-001")
@pytest.mark.req("SPEC-009")
def test_the_axial_120_example_is_valid() -> None:
    assert spec_errors(example()) == []


@pytest.mark.req("SPEC-001")
def test_the_example_leaves_the_duty_point_unknown() -> None:
    duty = example()["product"]["duty"]
    assert duty["flow"]["value"] is None and duty["flow"]["provenance"] == "unknown"
    assert duty["pressure"]["value"] is None and duty["pressure"]["provenance"] == "unknown"


@pytest.mark.req("SPEC-004")
@pytest.mark.req("SPEC-011")
def test_a_known_typed_duty_point_with_tolerances_is_valid() -> None:
    assert spec_errors(with_duty(KNOWN_FLOW, KNOWN_PRESSURE)) == []
    total = {**KNOWN_PRESSURE, "kind": "fan_total"}
    assert spec_errors(with_duty(KNOWN_FLOW, total)) == []


# --- field objects (SPEC-001, IN-004) -----------------------------------------------------------


def _set_density(field: Any) -> Callable[[Doc], None]:
    def change(d: Doc) -> None:
        d["air"]["density"] = field

    return change


@pytest.mark.req("SPEC-001")
@pytest.mark.parametrize(
    ("field", "why"),
    [
        (1.18, "bare number"),
        ({"value": 1.18, "unit": "kg/m3"}, "no provenance"),
        ({"value": 1.18, "provenance": "user"}, "no unit"),
        ({"value": 1.18, "unit": "kg/m3", "provenance": "guess"}, "unknown provenance class"),
        ({"value": 1.18, "unit": "lb/ft3", "provenance": "user"}, "unit not accepted for density"),
        ({"value": 1.18, "unit": "Pa", "provenance": "user"}, "unit of another quantity"),
        ({"value": None, "unit": "kg/m3", "provenance": "user"}, "known field without a value"),
        ({"value": 1.18, "unit": "kg/m3", "provenance": "unknown"}, "unknown field with a value"),
        ({"value": "1.18", "unit": "kg/m3", "provenance": "user"}, "number as a string"),
        ({"value": 1.18, "unit": "kg/m3", "provenance": "user", "extra": 1}, "extra property"),
        (
            {
                "value": 1.18,
                "unit": "kg/m3",
                "provenance": "user",
                "tolerance": {"relative": 0.1, "plus": 1},
            },
            "two tolerance forms",
        ),
        (
            {
                "value": 1.18,
                "unit": "kg/m3",
                "provenance": "user",
                "tolerance": {"minus": -1, "plus": 1},
            },
            "negative tolerance",
        ),
        (
            {
                "value": None,
                "unit": "kg/m3",
                "provenance": "unknown",
                "tolerance": {"relative": 0.1},
            },
            "tolerance on an unknown value",
        ),
    ],
)
def test_malformed_field_objects_are_rejected(field: Any, why: str) -> None:
    assert spec_errors(edited(_set_density(field))) != [], why


@pytest.mark.req("IN-004")
def test_image_fields_need_a_confidence_and_only_image_fields_may_have_one() -> None:
    image = {"value": 1.18, "unit": "kg/m3", "provenance": "image", "confidence": 0.7}
    assert spec_errors(edited(_set_density(image))) == []
    without = {"value": 1.18, "unit": "kg/m3", "provenance": "image"}
    assert spec_errors(edited(_set_density(without))) != []
    too_high = {**image, "confidence": 1.5}
    assert spec_errors(edited(_set_density(too_high))) != []
    user = {"value": 1.18, "unit": "kg/m3", "provenance": "user", "confidence": 0.9}
    assert spec_errors(edited(_set_density(user))) != []


# --- typed pressure and duty tolerances (SPEC-004, SPEC-011) ------------------------------------


@pytest.mark.req("SPEC-004")
@pytest.mark.parametrize(
    "pressure",
    [
        {k: v for k, v in KNOWN_PRESSURE.items() if k != "kind"},
        {**KNOWN_PRESSURE, "kind": "static_to_static"},
        {**KNOWN_PRESSURE, "kind": "static"},
        {**KNOWN_PRESSURE, "kind": "total"},
        {**KNOWN_PRESSURE, "unit": "m3/s"},
    ],
    ids=["no kind", "static-to-static", "bare static", "bare total", "flow unit"],
)
def test_untyped_or_static_to_static_pressure_is_rejected(pressure: Doc) -> None:
    assert spec_errors(with_duty(KNOWN_FLOW, pressure)) != []


@pytest.mark.req("SPEC-011")
def test_a_known_duty_point_without_a_tolerance_is_rejected() -> None:
    flow = {k: v for k, v in KNOWN_FLOW.items() if k != "tolerance"}
    pressure = {k: v for k, v in KNOWN_PRESSURE.items() if k != "tolerance"}
    assert spec_errors(with_duty(flow, KNOWN_PRESSURE)) != []
    assert spec_errors(with_duty(KNOWN_FLOW, pressure)) != []


# --- envelope and versioning (SPEC-009, MAINT-005) ----------------------------------------------


@pytest.mark.req("SPEC-009")
@pytest.mark.parametrize(
    "change",
    [
        lambda d: d.pop("revision"),
        lambda d: d.update(revision=0),
        lambda d: d.pop("parent"),
        lambda d: d.update(parent={"spec_id": "axial-120"}),
        lambda d: d.update(schema_version="2.0.0"),
        lambda d: d.update(schema_version="1.0"),
        lambda d: d.update(spec_id="Axial 120"),
        lambda d: d.update(family="axial"),
        lambda d: d.pop("product"),
        lambda d: d.update(unexpected=True),
        lambda d: d["objectives"].append(
            {"quantity": "noise", "sense": "lower", "provenance": "user"}
        ),
    ],
    ids=[
        "no revision",
        "revision 0",
        "no parent",
        "parent without revision",
        "major 2",
        "not semver",
        "bad spec id",
        "bad family",
        "no product",
        "extra top-level key",
        "bad objective sense",
    ],
)
def test_malformed_spec_envelopes_are_rejected(change: Callable[[Doc], None]) -> None:
    assert spec_errors(edited(change)) != []


@pytest.mark.req("SPEC-009")
def test_a_revision_links_to_its_parent() -> None:
    def change(d: Doc) -> None:
        d.update(revision=2, parent={"spec_id": "axial-120", "revision": 1})

    assert spec_errors(edited(change)) == []


@pytest.mark.req("SPEC-001")
def test_unknown_family_and_bad_product_block_are_rejected() -> None:
    assert spec_errors(edited(lambda d: d.update(family="fans.mystery"))) != []
    assert (
        spec_errors(
            edited(lambda d: d["product"].update(scope={"value": ["motor"], "provenance": "user"}))
        )
        != []
    )
    assert spec_errors(edited(lambda d: d["product"].pop("duty"))) != []


# --- candidates ---------------------------------------------------------------------------------

CANDIDATE_DOC: Doc = {
    "schema_version": "1.0.0",
    "candidate_id": "c-000001",
    "spec": {"spec_id": "axial-120", "revision": 1},
    "family": "fans.axial_ducted",
    "parameters": {
        "tip_diameter": {"value": 0.119, "unit": "m"},
        "blade_count": {"value": 7, "unit": "1"},
    },
}


@pytest.mark.req("SPEC-009")
def test_a_candidate_points_to_an_exact_spec_revision() -> None:
    assert _errors(CANDIDATE, CANDIDATE_DOC) == []
    no_revision = copy.deepcopy(CANDIDATE_DOC)
    no_revision["spec"].pop("revision")
    assert _errors(CANDIDATE, no_revision) != []
    bare = copy.deepcopy(CANDIDATE_DOC)
    bare["parameters"]["tip_diameter"] = 0.119
    assert _errors(CANDIDATE, bare) != []


# --- results (COR-003, REL-002, SPEC-009) -------------------------------------------------------

MODEL = {"name": "fans.axial_ducted.l1", "version": "1.0.0"}


def _result(metrics: Doc | None = None, failure: Doc | None = None) -> Doc:
    document: Doc = {
        "schema_version": "1.0.0",
        "candidate_id": "c-000001",
        "spec": {"spec_id": "axial-120", "revision": 1},
        "kernel_version": "0.1.0",
        "status": "failed" if failure else "ok",
    }
    if metrics is not None:
        document["metrics"] = metrics
    if failure is not None:
        document["failure"] = failure
    return document


def _metric(fidelity: str, uncertainty: float | None = None) -> Doc:
    metric: Doc = {"value": 150.0, "unit": "Pa", "fidelity": fidelity, "model": MODEL}
    if uncertainty is not None:
        metric["uncertainty"] = uncertainty
    return metric


@pytest.mark.req("COR-003")
def test_results_carry_labelled_metrics() -> None:
    metrics = {
        "fan_static_pressure": _metric("l1_predicted"),
        "fan_total_pressure": _metric("l2_verified", 4.0),
        "shaft_power": _metric("l3_validated", 0.2),
    }
    assert _errors(RESULT, _result(metrics)) == []


@pytest.mark.req("COR-003")
@pytest.mark.parametrize(
    "metric",
    [
        _metric("l2_verified"),
        _metric("l3_validated"),
        _metric("l1_predicted", 1.0),
        _metric("l2_verified", -1.0),
        _metric("L2 verified", 1.0),
        {"value": 150.0, "unit": "Pa", "fidelity": "l1_predicted"},
        {
            "value": 150.0,
            "unit": "Pa",
            "fidelity": "l1_predicted",
            "model": {"name": "x", "version": "1.0"},
        },
        {"value": 150.0, "fidelity": "l1_predicted", "model": MODEL},
        150.0,
    ],
    ids=[
        "verified without uncertainty",
        "validated without uncertainty",
        "uncertainty below L2 verified",
        "negative uncertainty",
        "report label instead of name",
        "no model",
        "model version not semver",
        "no unit",
        "bare number",
    ],
)
def test_malformed_metrics_are_rejected(metric: Any) -> None:
    assert _errors(RESULT, _result({"p": metric})) != []


@pytest.mark.req("REL-002")
def test_failed_results_carry_a_known_error_code_and_no_metrics() -> None:
    failure = {
        "code": "out_of_validity",
        "message": "tip speed above range",
        "subject": "tip_speed",
        "details": {"bounds": "[0, 100]"},
    }
    assert _errors(RESULT, _result(failure=failure)) == []
    assert _errors(RESULT, _result(failure={**failure, "code": "failure_cluster"})) != []
    assert _errors(RESULT, _result(metrics={"p": _metric("l1_predicted")}, failure=failure)) != []
    missing = _result()
    missing["status"] = "failed"
    assert _errors(RESULT, missing) != []
    no_spec = _result(metrics={"p": _metric("l1_predicted")})
    no_spec.pop("spec")
    assert _errors(RESULT, no_spec) != []


# --- reviewer findings: values the schemas alone cannot stop ---------------------------------


@pytest.mark.req("SPEC-001")
@pytest.mark.req("COR-003")
@pytest.mark.parametrize(
    "text",
    ['{"value": NaN}', '{"value": Infinity}', '{"value": -Infinity}', '{"value": 1e999}'],
)
def test_json_loader_rejects_non_finite_numbers(text: str) -> None:
    with pytest.raises(cs.DocumentError, match="non-finite"):
        cs.load_json(text)


@pytest.mark.req("SPEC-001")
@pytest.mark.parametrize("literal", [".nan", ".inf", "-.inf", ".NaN"])
def test_yaml_loader_rejects_non_finite_numbers(literal: str) -> None:
    with pytest.raises(cs.DocumentError, match="non-finite"):
        cs.load_yaml(f"air:\n  density: {{value: {literal}, unit: kg/m3, provenance: user}}\n")


@pytest.mark.req("SPEC-001")
def test_loaders_report_malformed_text_as_document_errors() -> None:
    with pytest.raises(cs.DocumentError, match="invalid JSON"):
        cs.load_json("{")
    with pytest.raises(cs.DocumentError, match="invalid YAML"):
        cs.load_yaml("a: [")
    assert cs.load_json('{"a": [1.5, {"b": 2}]}') == {"a": [1.5, {"b": 2}]}


@pytest.mark.req("SPEC-001")
def test_schema_loading_rejects_duplicate_ids_and_families(tmp_path: Path) -> None:
    first = {"$schema": "x", "$id": "urn:a", "x-cemkit-families": ["p.f"]}
    (tmp_path / "a.json").write_text(json.dumps(first))
    (tmp_path / "b.json").write_text(json.dumps({**first, "$id": "urn:b"}))
    with pytest.raises(cs.DocumentError, match="already declared"):
        cs.load_schemas(tmp_path)
    (tmp_path / "b.json").write_text(json.dumps({"$schema": "x", "$id": "urn:a"}))
    with pytest.raises(cs.DocumentError, match="duplicate"):
        cs.load_schemas(tmp_path)


@pytest.mark.req("SPEC-001")
def test_each_family_has_exactly_one_product_schema() -> None:
    assert dict(SET.families) == {"fans.axial_ducted": "urn:cemkit:schema:products:fans:v1:spec"}
    assert SET.spec_errors([]) != []


@pytest.mark.req("SPEC-004")
def test_every_pressure_field_in_every_schema_carries_a_kind() -> None:
    """A field using pressure units must also declare the ISO 5801 kind (reviewer finding)."""

    def walk(node: Any, where: str) -> list[str]:
        missing: list[str] = []
        if isinstance(node, dict):
            props = node.get("properties", {})
            unit = props.get("unit", {}) if isinstance(props, dict) else {}
            pressure = isinstance(unit, dict) and str(unit.get("$ref", "")).endswith(
                "/pressure_unit"
            )
            if pressure and "kind" not in props:
                missing.append(where)
            for key, value in node.items():
                missing += walk(value, f"{where}/{key}")
        elif isinstance(node, list):
            for index, item in enumerate(node):
                missing += walk(item, f"{where}/{index}")
        return missing

    uses = 0
    for uri, schema in ALL.items():
        text = json.dumps(schema)
        uses += text.count("/pressure_unit")
        assert walk(schema, uri) == []
    assert uses >= 1


@pytest.mark.req("SPEC-001")
@pytest.mark.parametrize(
    ("section", "name", "field"),
    [
        ("air", "density", {"value": -1.18, "unit": "kg/m3", "provenance": "user"}),
        ("air", "density", {"value": 0, "unit": "kg/m3", "provenance": "user"}),
        ("air", "temperature", {"value": -5, "unit": "K", "provenance": "user"}),
        ("air", "dynamic_viscosity", {"value": -1e-5, "unit": "Pa*s", "provenance": "user"}),
        ("envelope", "width", {"value": -120, "unit": "mm", "provenance": "user"}),
    ],
)
def test_values_that_are_positive_by_definition_reject_zero_and_negatives(
    section: str, name: str, field: Doc
) -> None:
    def change(d: Doc) -> None:
        d[section][name] = field

    assert spec_errors(edited(change)) != []


@pytest.mark.req("SPEC-001")
def test_a_celsius_temperature_may_be_negative_and_a_ratio_may_be_zero() -> None:
    def change(d: Doc) -> None:
        d["air"]["temperature"] = {"value": -5, "unit": "degC", "provenance": "user"}
        d["product"]["pressure_margin"] = {"value": 0, "unit": "1", "provenance": "user"}

    assert spec_errors(edited(change)) == []

    def negative_ratio(d: Doc) -> None:
        d["product"]["pressure_margin"] = {"value": -0.1, "unit": "1", "provenance": "user"}

    assert spec_errors(edited(negative_ratio)) != []


@pytest.mark.req("SPEC-001")
def test_negative_duty_flow_and_nominal_size_are_rejected() -> None:
    assert spec_errors(with_duty({**KNOWN_FLOW, "value": -0.05}, KNOWN_PRESSURE)) != []
    assert spec_errors(edited(lambda d: d["product"]["nominal_size"].update(value=-120))) != []


@pytest.mark.req("SPEC-004")
def test_an_unknown_pressure_may_already_state_its_kind() -> None:
    pressure = {"kind": "fan_static", "value": None, "unit": "Pa", "provenance": "unknown"}
    flow = {"value": None, "unit": "m3/s", "provenance": "unknown"}
    assert spec_errors(with_duty(flow, pressure)) == []


@pytest.mark.req("SPEC-001")
@pytest.mark.parametrize(
    ("quantity", "ok"),
    [
        ("efficiency", False),
        ("static_efficiency", False),
        ("total_efficiency", True),
        ("total_to_static_efficiency", True),
        ("overall_efficiency", True),
        ("tip_speed", True),
    ],
)
def test_efficiency_objectives_must_state_their_type(quantity: str, ok: bool) -> None:
    def change(d: Doc) -> None:
        d["objectives"] = [{"quantity": quantity, "sense": "maximize", "provenance": "user"}]

    assert (spec_errors(edited(change)) == []) is ok


@pytest.mark.req("COR-003")
def test_ok_results_need_at_least_one_metric_and_candidates_a_parameter() -> None:
    assert _errors(RESULT, _result(metrics={})) != []
    empty = copy.deepcopy(CANDIDATE_DOC)
    empty["parameters"] = {}
    assert _errors(CANDIDATE, empty) != []


@pytest.mark.req("COR-003")
@pytest.mark.parametrize("unit", ["mm", "banana", "kPa", "rpm", ""])
def test_candidate_and_result_units_are_coherent_si(unit: str) -> None:
    candidate = copy.deepcopy(CANDIDATE_DOC)
    candidate["parameters"]["tip_diameter"]["unit"] = unit
    assert _errors(CANDIDATE, candidate) != []
    metric = {**_metric("l1_predicted"), "unit": unit}
    assert _errors(RESULT, _result({"p": metric})) != []

"""ADR-015 compatibility: the C ABI responses and CLI output for fans.axial_ducted stay the same.

Each case sends a fixed request through the C ABI (cemkit._kernel, raw JSON) or the CLI and
compares the response with a file under tests/python/data/golden/, captured from main before the
ADR-015 refactor. A difference fails, except additive JSON members (ADR-015 rule 6: the ABI minor
version may rise for additive fields only). To record new golden files after an approved change,
run with CEMKIT_GOLDEN_UPDATE=1 and review the diff.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

import pytest
from typer.testing import CliRunner

from cemkit import _kernel
from cemkit import schemas as cs
from cemkit.cli import app as cli
from cemkit.reference.crosscheck import build_cases, edge_cases

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "tests" / "python" / "data" / "golden"
EXAMPLE = ROOT / "examples" / "axial_120.yaml"
UPDATE = os.environ.get("CEMKIT_GOLDEN_UPDATE") == "1"


def example() -> dict[str, Any]:
    loaded = cs.load_yaml(EXAMPLE.read_text(encoding="utf-8"))
    assert isinstance(loaded, dict)
    return loaded


def with_duty(pressure: float, kind: str) -> dict[str, Any]:
    doc = example()
    doc["revision"] = 2
    doc["parent"] = {"spec_id": doc["spec_id"], "revision": 1}
    doc["product"]["duty"] = {
        "flow": {
            "value": 0.05,
            "unit": "m3/s",
            "provenance": "user",
            "tolerance": {"relative": 0.05},
        },
        "pressure": {
            "value": pressure,
            "unit": "Pa",
            "kind": kind,
            "provenance": "user",
            "tolerance": {"minus": 0.0, "plus": 10.0},
        },
    }
    return doc


def without_pressure_kind() -> dict[str, Any]:
    doc = with_duty(150.0, "fan_total")
    del doc["product"]["duty"]["pressure"]["kind"]
    return doc


def power_limited() -> dict[str, Any]:
    doc = with_duty(150.0, "fan_total")
    doc["product"]["power_limit"] = {"value": 1.0, "unit": "W", "provenance": "user"}
    return doc


def l0_cases() -> list[dict[str, Any]]:
    # Seeded random states and the fixed edge cases; hand-calculation cases are left out so that a
    # proposed hand calculation moving to verified/ does not change the golden file.
    cases, _, fixed = build_cases(40)
    return cases[fixed:] + edge_cases()


SMOKE = cli.SMOKE_REQUEST
RAW_RANGE = {
    "family": "fans.axial_ducted",
    "specific_speed_min": None,
    "specific_speed_max": None,
    "source": "UNSOURCED",
}

ABI_CASES: dict[str, tuple[str, Any]] = {
    "spec_compile_example": ("spec_compile", lambda: {"spec": example()}),
    "spec_compile_autonomous": (
        "spec_compile",
        lambda: {"spec": example(), "autonomous_mode": True},
    ),
    "spec_compile_duty_static": ("spec_compile", lambda: {"spec": with_duty(150.0, "fan_static")}),
    "spec_compile_no_pressure_kind": ("spec_compile", lambda: {"spec": without_pressure_kind()}),
    "spec_compile_power_limit": ("spec_compile", lambda: {"spec": power_limited()}),
    "spec_compile_rejected_kind": (
        "spec_compile",
        lambda: {"spec": with_duty(150.0, "static_to_static")},
    ),
    "feasibility_spec_example": ("feasibility", lambda: {"spec": example()}),
    "feasibility_spec_static": ("feasibility", lambda: {"spec": with_duty(150.0, "fan_static")}),
    "feasibility_spec_total": ("feasibility", lambda: {"spec": with_duty(150.0, "fan_total")}),
    "feasibility_spec_impossible": (
        "feasibility",
        lambda: {"spec": with_duty(5000.0, "fan_total")},
    ),
    "feasibility_raw": (
        "feasibility",
        lambda: {
            "flow": 0.05,
            "fan_total_pressure": 150.0,
            "omega": 209.43951023931953,
            "d_tip": 0.119,
            "range": RAW_RANGE,
        },
    ),
    "l0_batch": ("l0_batch", lambda: {"cases": l0_cases()}),
    "l0_batch_bad_request": (
        "l0_batch",
        lambda: {"cases": [{"function": "tip_speed", "args": {"omega": "fast", "d_tip": 0.1}}]},
    ),
    "l0_batch_unknown_function": (
        "l0_batch",
        lambda: {"cases": [{"function": "no_such_function", "args": {}}]},
    ),
    "geometry_smoke": ("geometry_smoke", lambda: SMOKE),
}


def is_superset(actual: Any, expected: Any, where: str = "$") -> list[str]:
    """Differences between actual and expected, allowing extra object members in actual."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return [f"{where}: expected an object"]
        diffs: list[str] = []
        for key, value in expected.items():
            if key not in actual:
                diffs.append(f"{where}.{key}: missing")
            else:
                diffs += is_superset(actual[key], value, f"{where}.{key}")
        return diffs
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            return [f"{where}: expected a list of {len(expected)}"]
        diffs = []
        for i, (a, e) in enumerate(zip(actual, expected, strict=True)):
            diffs += is_superset(a, e, f"{where}[{i}]")
        return diffs
    return [] if actual == expected else [f"{where}: {actual!r} != {expected!r}"]


def check_golden(name: str, actual: Any) -> None:
    path = GOLDEN / f"{name}.json"
    if UPDATE:
        GOLDEN.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(actual, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        return
    expected = json.loads(path.read_text(encoding="utf-8"))
    diffs = is_superset(actual, expected)
    assert not diffs, f"{name}: {len(diffs)} difference(s): {diffs[:10]}"


@pytest.mark.req("MAINT-005")
@pytest.mark.req("FAM-001")
@pytest.mark.parametrize("name", sorted(ABI_CASES))
def test_c_abi_response_is_unchanged(name: str) -> None:
    entry, request = ABI_CASES[name]
    status, body = getattr(_kernel, entry)(json.dumps(request()))
    check_golden(f"abi_{name}", {"status": status, "body": json.loads(body)})


SPEC_ID = "axial-120"  # examples/axial_120.yaml

# Each CLI case is a sequence of commands against one fresh store; every command's exit code and
# output are compared.
CLI_CASES: dict[str, list[list[str]]] = {
    "spec_compile": [["spec", "compile", "{spec}"], ["spec", "questions", SPEC_ID]],
    "feasibility_example": [["spec", "compile", "{spec}"], ["feasibility", SPEC_ID]],
    "feasibility_static": [
        ["spec", "compile", "{spec}"],
        ["spec", "compile", "{static}"],
        ["feasibility", SPEC_ID],
    ],
    "geometry_smoke_json": [["geometry", "smoke", "--json", "--out", "{out}"]],
    "geometry_smoke_text": [["geometry", "smoke"]],
}


@pytest.mark.req("MAINT-005")
@pytest.mark.req("FAM-001")
@pytest.mark.parametrize("name", sorted(CLI_CASES))
def test_cli_output_is_unchanged(name: str, tmp_path: Path) -> None:
    spec = tmp_path / "spec.json"
    spec.write_text(json.dumps(example()), encoding="utf-8")
    static = tmp_path / "static.json"
    static.write_text(json.dumps(with_duty(150.0, "fan_static")), encoding="utf-8")
    out = tmp_path / "out"
    runs = []
    for command in CLI_CASES[name]:
        args = [a.format(spec=spec, static=static, out=out) for a in command]
        result = CliRunner().invoke(cli.app, [*args, "--store", str(tmp_path / "store")])
        text = result.output.replace(str(tmp_path), "<TMP>")
        runs.append(
            {"command": command, "exit_code": result.exit_code, "output": text.splitlines()}
        )
    check_golden(f"cli_{name}", runs)

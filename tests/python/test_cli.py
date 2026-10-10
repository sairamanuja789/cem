"""T14: the cemkit CLI (IN-001, UC-01 to UC-03, UC-08, REP-001, REP-004).

Each command runs in-process through Typer's CliRunner against a temporary store, calling the real
kernel through cemkit._kernel (built by the release preset). scripts/cemkit runs the same app inside
the container.
"""

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest
import yaml
from typer.testing import CliRunner

from cemkit import schemas as cs
from cemkit.cli import app as cli
from cemkit.reference.fans.common import l0
from cemkit.reference.fans.common.feasibility import (
    UNSOURCED,
    Duty,
    FamilyRange,
    Report,
    check_feasibility,
)
from cemkit.reference.fans.common.l0 import FanStaticPressure, FanTotalPressure
from cemkit.reference.platform.air import default_air
from cemkit.reporting.claims import BannedClaim, banned_claims, check_claims
from cemkit.store import open_store

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "axial_120.yaml"
RANGES = ROOT / "data" / "fans" / "family_ranges.yaml"
runner = CliRunner()
# Owner decision D6 (provisional): D_tip = D_duct - 2 c_tip with the example's nominal_size 120 mm
# (duct_inner_diameter) and tip_clearance_min 0.5 mm (AX-001, AX-009).
D6_TIP = 0.12 - 2.0 * 0.0005


def invoke(store: Path, *args: str) -> Any:
    return runner.invoke(cli.app, [*args, "--store", str(store)])


def example() -> dict[str, Any]:
    loaded = cs.load_yaml(EXAMPLE.read_text(encoding="utf-8"))
    assert isinstance(loaded, dict)
    return loaded


def with_duty(revision: int, pressure: float, kind: str) -> dict[str, Any]:
    doc = example()
    doc["revision"] = revision
    doc["parent"] = {"spec_id": "axial-120", "revision": revision - 1}
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


def write(tmp: Path, name: str, doc: dict[str, Any]) -> Path:
    path = tmp / name
    path.write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")
    return path


@pytest.mark.req("IN-001")
@pytest.mark.req("UC-01")
@pytest.mark.req("SPEC-008")
def test_spec_compile_records_the_example_with_provenance_and_questions(tmp_path: Path) -> None:
    store = tmp_path / "store"
    result = invoke(store, "spec", "compile", str(EXAMPLE))
    assert result.exit_code == 0, result.output
    out = result.output
    assert "spec axial-120 revision 1 (family fans.axial_ducted): compiled, run 1" in out
    assert "  product.nominal_size: 120 mm = 0.12 m  [user]" in out
    assert (
        "  product.rotational_speed: 2000 rpm = 209.43951023931953 rad/s  [default, provisional]"
        in out
    )
    assert "  product.duty.flow: unknown  [unknown]" in out
    assert "open questions (2):" in out
    assert "  product.duty.flow [m3/s]: Essential duty point flow rate (AX-003)." in out
    assert "  product.duty.pressure [Pa]:" in out
    assert "not runnable yet" in out
    with open_store(store) as opened:
        assert [r.revision for r in opened.spec_records("axial-120")] == [1]


@pytest.mark.req("SPEC-004")
def test_a_pressure_without_a_type_is_rejected_naming_the_field(tmp_path: Path) -> None:
    doc = with_duty(2, 120.0, "fan_static")
    del doc["product"]["duty"]["pressure"]["kind"]
    result = invoke(tmp_path / "store", "spec", "compile", str(write(tmp_path, "s.yaml", doc)))
    assert result.exit_code == 1
    assert "rejected: spec_rejected at product.duty.pressure" in result.output


@pytest.mark.req("IN-001")
def test_spec_compile_reads_json_and_refuses_other_files(tmp_path: Path) -> None:
    json_path = tmp_path / "s.json"
    json_path.write_text(json.dumps(example()), encoding="utf-8")
    assert invoke(tmp_path / "store", "spec", "compile", str(json_path)).exit_code == 0
    other = tmp_path / "s.txt"
    other.write_text("{}", encoding="utf-8")
    result = invoke(tmp_path / "store", "spec", "compile", str(other))
    assert result.exit_code == 1 and "expected a .json, .yaml or .yml file" in result.output


@pytest.mark.req("UC-02")
def test_spec_questions_for_a_recorded_spec(tmp_path: Path) -> None:
    store = tmp_path / "store"
    assert invoke(store, "spec", "compile", str(EXAMPLE)).exit_code == 0
    result = invoke(store, "spec", "questions", "axial-120")
    assert result.exit_code == 0
    assert "spec axial-120 revision 1:\nopen questions (2):" in result.output
    missing = invoke(store, "spec", "questions", "nope")
    assert missing.exit_code == 1 and "no recorded spec nope" in missing.output


@pytest.mark.req("UC-03")
@pytest.mark.req("SEL-004")
@pytest.mark.req("PHY-005")
def test_feasibility_matches_the_reference_and_reports_range_unsourced(tmp_path: Path) -> None:
    store = tmp_path / "store"
    spec_file = write(tmp_path, "rev2.yaml", with_duty(2, 120.0, "fan_static"))
    assert invoke(store, "spec", "compile", str(spec_file)).exit_code == 0
    result = invoke(store, "feasibility", "axial-120")
    assert result.exit_code == 0, result.output
    out = result.output

    # The reference: static to total through the 0.12 m duct, then the gate (range UNSOURCED, tip
    # diameter from owner decision D6), with the spec's air (AX-005 defaults).
    air = default_air()
    total = l0.fan_total_from_static(FanStaticPressure(120.0), 0.05, 0.12, air)
    assert isinstance(total, l0.Derived)
    report = check_feasibility(
        Duty(0.05, total.value),
        air,
        FamilyRange("fans.axial_ducted", None, None, UNSOURCED),
        omega=209.43951023931953,
        d_tip=D6_TIP,
    )
    assert isinstance(report, Report) and report.specific_speed is not None
    numbers = {
        m.group(1): float(m.group(2)) for m in re.finditer(r"(\w+): (\S+) [^\s\[]+ \[L0", out)
    }
    assert numbers["fan_total_pressure"] == pytest.approx(total.value.pa, rel=1e-12)
    assert numbers["speed"] == pytest.approx(report.specific_speed, rel=1e-12)
    assert "[L0 predicted, fans.l0@1.0.0]" in out
    statuses = [(c.limit, c.status) for c in report.checks]
    for limit, status in statuses:
        assert f"  {limit}: {status}:" in out
    assert "range unsourced: no cited specific-speed range for fans.axial_ducted" in out
    assert f"verdict: {report.verdict}" in out


@pytest.mark.req("SEL-004")
@pytest.mark.req("AX-001")
@pytest.mark.req("AX-009")
def test_feasibility_of_the_example_computes_the_tip_speed_check(tmp_path: Path) -> None:
    # examples/axial_120.yaml with a duty point (its speed, 2000 rpm, is already stated): the tip
    # diameter comes from the spec by owner decision D6, so the tip-speed check is computed.
    store = tmp_path / "store"
    spec_file = write(tmp_path, "rev2.yaml", with_duty(2, 150.0, "fan_total"))
    assert invoke(store, "spec", "compile", str(spec_file)).exit_code == 0
    result = invoke(store, "feasibility", "axial-120")
    assert result.exit_code == 0, result.output
    out = result.output
    tip = re.search(r"  d_tip: (\S+) m  \[default, provisional\] D_tip = D_duct - 2 c_tip ", out)
    assert tip is not None, out
    assert float(tip.group(1)) == pytest.approx(D6_TIP, rel=1e-12)
    assert "(from product.nominal_size, product.size_reference, product.tip_clearance_min)" in out
    report = check_feasibility(
        Duty(0.05, FanTotalPressure(150.0)),
        default_air(),
        FamilyRange("fans.axial_ducted", None, None, UNSOURCED),
        omega=209.43951023931953,
        d_tip=D6_TIP,
    )
    assert isinstance(report, Report)
    tip_check = next(c for c in report.checks if c.limit == "incompressible_tip_speed")
    assert tip_check.status == "pass"
    assert f"  incompressible_tip_speed: pass: {tip_check.message}" in out
    assert "needs the rotational speed and the rotor tip diameter" not in out


@pytest.mark.req("SEL-004")
def test_feasibility_rejects_an_impossible_duty_with_the_violated_limit(tmp_path: Path) -> None:
    store = tmp_path / "store"
    spec_file = write(tmp_path, "rev2.yaml", with_duty(2, 1500.0, "fan_total"))
    assert invoke(store, "spec", "compile", str(spec_file)).exit_code == 0
    result = invoke(store, "feasibility", "axial-120")
    assert result.exit_code == 2
    out = result.output
    assert "  incompressible_pressure: violated:" in out
    assert "limit violated: out_of_validity at fan_total_pressure" in out
    assert "bounds=(0, 1418.55]" in out
    assert "nearest feasible duty for this limit: flow 0.05 m3/s [L0 predicted, " in out
    assert "fan total pressure 1418.55 Pa [L0 predicted, fans.feasibility@1.0.0]" in out
    assert "verdict: infeasible" in out


@pytest.mark.req("UC-03")
def test_feasibility_of_a_spec_without_a_duty_point_names_the_field(tmp_path: Path) -> None:
    store = tmp_path / "store"
    assert invoke(store, "spec", "compile", str(EXAMPLE)).exit_code == 0
    result = invoke(store, "feasibility", "axial-120")
    assert result.exit_code == 1
    assert "rejected: spec_rejected at product.duty.flow" in result.output


@pytest.mark.req("SEL-002")
def test_the_data_file_and_the_family_hook_agree_the_range_is_unsourced() -> None:
    entries = yaml.safe_load(RANGES.read_text(encoding="utf-8"))["families"]
    axial = next(e for e in entries if e["family"] == "fans.axial_ducted")
    assert axial["source"] == UNSOURCED  # the kernel hook reports "range unsourced" (test above)


@pytest.mark.req("GEO-001")
@pytest.mark.req("GEO-002")
@pytest.mark.req("GEO-004")
def test_geometry_smoke_exports_step_and_stl_with_content_hash_names(tmp_path: Path) -> None:
    store, out_dir = tmp_path / "store", tmp_path / "exports"
    result = invoke(store, "geometry", "smoke", "--out", str(out_dir))
    assert result.exit_code == 0, result.output
    assert "validity: ok" in result.output
    assert "no fidelity label" in result.output
    names = re.findall(r"  ([0-9a-f]{64}\.(?:step|stl))  \d+ bytes", result.output)
    assert sorted(n.rsplit(".", 1)[1] for n in names) == ["step", "stl"]
    for name in names:
        data = (out_dir / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == name.split(".")[0]
    with open_store(store) as opened:
        stored = {a.name for a in opened.run_artifacts(opened.run(1))}
    assert set(names) <= stored and {"command.json", "output.json"} <= stored


@pytest.mark.req("GEO-004")
@pytest.mark.req("IN-001")
def test_geometry_smoke_json_stdout_is_only_json(tmp_path: Path) -> None:
    # Owner decision D5: OCCT messages never reach stdout. A child process captures the real file
    # descriptor 1, so a print from C++ (which CliRunner would not see) fails this test.
    environment = {**os.environ, "PYTHONPATH": str(ROOT / "python")}
    child = subprocess.run(
        [
            sys.executable,
            *("-m", "cemkit.cli", "geometry", "smoke", "--json"),
            *("--store", str(tmp_path / "store")),
        ],
        capture_output=True,
        text=True,
        env=environment,
        check=False,
    )
    assert child.returncode == 0, child.stderr
    document = json.loads(child.stdout)  # the whole of stdout is one JSON document
    assert child.stdout == json.dumps(document, sort_keys=True) + "\n"
    assert document["command"] == "geometry smoke"
    assert document["output"]["validity"]["ok"] is True
    assert sorted(f["name"].rsplit(".", 1)[1] for f in document["files"]) == ["step", "stl"]


@pytest.mark.req("UC-08")
@pytest.mark.req("STORE-002")
def test_runs_show_and_reproduce(tmp_path: Path) -> None:
    store = tmp_path / "store"
    assert invoke(store, "spec", "compile", str(EXAMPLE)).exit_code == 0
    shown = invoke(store, "runs", "show", "1")
    assert shown.exit_code == 0
    out = shown.output
    assert re.search(r"  kernel version: \d+\.\d+\.\d+", out)
    assert '"fans.axial_ducted":"0.1.0"' in out and '"fans.l0":"1.0.0"' in out
    assert re.search(r"  input hash: [0-9a-f]{64}", out)
    assert "  axial-120 revision 1 sha256 " in out
    assert "  command.json " in out and "  output.json " in out
    reproduced = invoke(store, "runs", "reproduce", "1")
    assert reproduced.exit_code == 0, reproduced.output
    assert "run 1 (spec compile): reproduced, identical output" in reproduced.output
    assert invoke(store, "runs", "show", "99").exit_code == 1


@pytest.mark.req("UC-08")
def test_reproduce_reports_a_changed_output(tmp_path: Path) -> None:
    store = tmp_path / "store"
    assert invoke(store, "geometry", "smoke").exit_code == 0
    assert invoke(store, "runs", "reproduce", "1").exit_code == 0
    # A run whose recorded output differs from what the same command gives now.
    with open_store(store) as opened:
        run2 = opened.record_run(opened.run(1).metadata)
        command = {"command": "geometry smoke", "inputs": cli.SMOKE_REQUEST}
        opened.record_artifact(run2, json.dumps(command).encode(), "command.json")
        opened.record_artifact(run2, b'{"backend":"other"}', "output.json")
    result = invoke(store, "runs", "reproduce", str(run2.run_pk))
    assert result.exit_code == 1
    assert "output differs in" in result.output
    assert "versions unchanged: the difference is not explained by them" in result.output


@pytest.mark.req("REP-004")
def test_banned_claims_are_refused_in_all_output(tmp_path: Path) -> None:
    assert banned_claims("An Optimal and PERFECT design, validated.") == [
        "optimal",
        "perfect",
        "validated",
    ]
    assert banned_claims("certified") == ["certified"]
    assert banned_claims("L3 validated", l3_evidence=True) == []
    assert banned_claims("L3 validated") == ["validated"]
    assert banned_claims("suboptimally imperfect") == []  # whole words only
    with pytest.raises(BannedClaim):
        check_claims("optimal")
    # Text quoted from a spec is checked too: a material named with a banned claim is refused and
    # nothing of the output is printed (exit 3).
    doc = example()
    doc["manufacturing"]["material"]["value"] = "certified PETG"
    result = invoke(tmp_path / "store", "spec", "compile", str(write(tmp_path, "b.yaml", doc)))
    assert result.exit_code == 3
    assert "banned claims ['certified']" in result.output
    assert "PETG" not in result.output

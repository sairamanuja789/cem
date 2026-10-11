"""M5 and M6 plumbing end to end (SIM-001 to SIM-004, TRUST-001 to TRUST-004, OPT-005, OPT-006):
the real kernel writes the cases and a stand-in for OpenFOAM writes synthetic monitors (CI has no
OpenFOAM), so these tests check the wiring and labels, not any fan's performance."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
from typer.testing import CliRunner

from cemkit import schemas as cs
from cemkit.cli import app as cli

ROOT = Path(__file__).resolve().parents[2]
TRIAL_SPEC = ROOT / "examples" / "axial_120_trial.yaml"
TRIAL_DESIGN = ROOT / "examples" / "fan_design_trial.yaml"
CFD = ROOT / "examples" / "cfd_trial.yaml"


def invoke(store: Path, *args: str) -> Any:
    return CliRunner().invoke(cli.app, [*args, "--store", str(store)])


def small_cfd(tmp_path: Path) -> Path:
    settings = cs.load_yaml(CFD.read_text(encoding="utf-8"))
    assert isinstance(settings, dict)
    settings["cores"] = 1
    path = tmp_path / "cfd.json"
    path.write_text(json.dumps(settings), encoding="utf-8")
    return path


@pytest.mark.req("SIM-003")
@pytest.mark.req("TRUST-002")
@pytest.mark.usefixtures("fake_openfoam")
def test_simulate_fan_runs_three_points_and_a_mesh_study(tmp_path: Path) -> None:
    store = tmp_path / "store"
    assert invoke(store, "spec", "compile", str(TRIAL_SPEC)).exit_code == 0
    assert invoke(store, "design", "fan", "axial-120-trial", str(TRIAL_DESIGN)).exit_code == 0
    result = invoke(
        store, "simulate", "fan", "2", str(small_cfd(tmp_path)), "--workdir", str(tmp_path / "w")
    )
    assert result.exit_code == 0, result.output
    out = result.output
    for name in ("medium_q080", "medium_q100", "medium_q120", "coarse_q100", "fine_q100"):
        assert f"{name}: " in out
    assert "[L2 simulated, fans.axial.cfd_metrics@0.2.0]" in out
    # Identical synthetic monitors on all meshes: a converged study, so the fine case is verified.
    assert "fine_q100: VERIFIED" in out
    assert "[L2 verified, fans.axial.cfd_metrics@0.2.0]" in out
    assert "UNSOURCED" in out  # the thresholds' sources are printed
    case = tmp_path / "w" / "design_2" / "medium_q100"
    assert "volumetricFlowRate 0.004;" in (case / "0" / "U").read_text(encoding="utf-8")


@pytest.mark.req("OPT-005")
@pytest.mark.req("OPT-006")
@pytest.mark.usefixtures("fake_openfoam")
def test_optimize_fan_labels_the_campaign_exploratory_and_respects_the_budget(
    tmp_path: Path,
) -> None:
    store = tmp_path / "store"
    assert invoke(store, "spec", "compile", str(TRIAL_SPEC)).exit_code == 0
    search = tmp_path / "search.json"
    search.write_text(
        json.dumps(
            {
                "variables": {"r_hub": [0.024, 0.028]},
                "objective": "total_to_static_efficiency",
                "max_candidates": 1,
            }
        ),
        encoding="utf-8",
    )
    cfd = small_cfd(tmp_path)
    over = invoke(
        store, "optimize", "fan", "axial-120-trial", str(TRIAL_DESIGN), str(search), str(cfd)
    )
    assert over.exit_code != 0 and "OPT-005" in over.output
    search.write_text(
        json.dumps(
            {
                "variables": {"r_hub": [0.024, 0.028]},
                "objective": "total_to_static_efficiency",
                "max_candidates": 2,
            }
        ),
        encoding="utf-8",
    )
    result = invoke(
        store,
        "optimize",
        "fan",
        "axial-120-trial",
        str(TRIAL_DESIGN),
        str(search),
        str(cfd),
        "--workdir",
        str(tmp_path / "o"),
    )
    assert result.exit_code == 0, result.output
    assert "exploratory" in result.output
    # The synthetic monitors give a 0.94 Pa total-to-static rise, below the 18 Pa fan static duty.
    assert "nothing is ranked" in result.output


@pytest.mark.req("REP-002")
@pytest.mark.req("REP-003")
@pytest.mark.req("REP-004")
@pytest.mark.usefixtures("fake_openfoam")
def test_report_fan_writes_a_package_with_provisional_inputs_first(tmp_path: Path) -> None:
    store = tmp_path / "store"
    assert invoke(store, "spec", "compile", str(TRIAL_SPEC)).exit_code == 0
    assert invoke(store, "design", "fan", "axial-120-trial", str(TRIAL_DESIGN)).exit_code == 0
    sim = invoke(
        store, "simulate", "fan", "2", str(small_cfd(tmp_path)), "--workdir", str(tmp_path / "w")
    )
    assert sim.exit_code == 0, sim.output
    out = tmp_path / "package"
    result = invoke(store, "report", "fan", "3", "--out", str(out))
    assert result.exit_code == 0, result.output
    report = (out / "report.md").read_text(encoding="utf-8")
    assert report.index("provisional inputs") < report.index("## 2. L0 feasibility")
    assert "`product.duty.flow`" in report and "TRIAL" in report
    assert "[L2 simulated, fans.axial.cfd_metrics@0.2.0]" in report
    assert "Not L3" in report
    assert len(list((out / "files").iterdir())) == 4  # rotor and duct, STEP and STL
    assert {p.name for p in (out / "previews").iterdir()} == {
        "fan_iso.png",
        "fan_axial.png",
        "fan_side.png",
    }
    assert (out / "previews" / "fan_iso.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

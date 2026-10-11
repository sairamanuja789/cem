"""SIM-002, TRUST-001 to TRUST-004: write, run (under limits), parse and assess a case end to end,
with a stand-in for the OpenFOAM environment that writes synthetic monitors (CI has no OpenFOAM)."""

from __future__ import annotations

from pathlib import Path

import pytest

from cemkit.orchestration.limits import ResourceLimits
from cemkit.simulation import study
from cemkit.simulation.runners import openfoam

CASE = {
    "files": {"system/controlDict": "x\n", "0/U": "y\n"},
    "binary_files": {"constant/triSurface/blade_mm.stl": "AAEC"},
    "steps": [
        {"name": "checkMesh", "argv": ["checkMesh"]},
        {"name": "simpleFoam", "argv": ["simpleFoam"]},
    ],
}
THRESHOLDS = {
    "residuals": {"p": 1e-4},
    "mass_imbalance": 0.005,
    "flat_fraction": 0.2,
    "flatness": 0.01,
}


@pytest.mark.req("SIM-002")
@pytest.mark.req("TRUST-004")
@pytest.mark.usefixtures("fake_openfoam")
def test_a_case_is_written_run_parsed_and_assessed(tmp_path: Path) -> None:
    limits = ResourceLimits(cores=1, wall_time_s=60.0)
    r = study.run_case("q100", tmp_path / "case", CASE, study.thresholds_of(THRESHOLDS), limits)
    assert (tmp_path / "case" / "constant/triSurface/blade_mm.stl").read_bytes() == b"\x00\x01\x02"
    assert r.assessment.status == "CONVERGED", r.assessment.failed
    assert r.cells == 1000
    assert r.finals["moment_z"] == pytest.approx(0.000175)
    assert r.finals["inlet_flow"] == pytest.approx(0.004)  # inwards flow made positive
    assert r.finals["pt_out"] - r.finals["pt_in"] == pytest.approx(1.0)


@pytest.mark.req("TRUST-003")
@pytest.mark.usefixtures("fake_openfoam")
def test_a_residual_above_its_threshold_is_unconverged_and_recorded(tmp_path: Path) -> None:
    strict = {**THRESHOLDS, "residuals": {"p": 1e-12}}
    limits = ResourceLimits(cores=1, wall_time_s=60.0)
    r = study.run_case("q100", tmp_path / "case", CASE, study.thresholds_of(strict), limits)
    assert r.assessment.status == "UNCONVERGED"
    assert [c.name for c in r.assessment.failed] == ["residual_p"]


@pytest.mark.req("SIM-001")
def test_case_files_cannot_escape_the_case_directory(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        openfoam.write_case(tmp_path / "case", {"files": {"../evil": "x"}})


@pytest.mark.req("TRUST-004")
def test_mesh_failures_are_read_from_the_checkmesh_log(tmp_path: Path) -> None:
    (tmp_path / "log.checkMesh").write_text(
        "    Max skewness = 1.2 OK.\n"
        " ***Max skewness = 7.7328652, 4 highly skew faces detected which may impair\n"
        "Failed 1 mesh checks.\n",
        encoding="utf-8",
    )
    assert openfoam.mesh_failures(tmp_path) == (
        "Max skewness = 7.7328652, 4 highly skew faces detected which may impair",
    )
    assert openfoam.mesh_failures(tmp_path / "missing") == ("no checkMesh log",)

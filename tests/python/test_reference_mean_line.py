"""L1 axial mean line (fans.axial.mean_line@0.1.0, PHY-001, PHY-005): the Python reference checked
against hand calculations and against the identities it must satisfy. Inputs are illustrative."""

from __future__ import annotations

import math
from dataclasses import asdict
from pathlib import Path
from typing import Any

import pytest
import yaml
from hypothesis import given
from hypothesis import strategies as st

from cemkit.errors import Error
from cemkit.reference.fans.axial.mean_line import MeanLine, mean_line
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import default_air

HAND_CALCS = Path(__file__).resolve().parents[1] / "hand_calcs"


def _checks() -> list[Any]:
    checks = []
    for folder in ("verified", "proposed"):
        for path in sorted((HAND_CALCS / folder).glob("l1_*.yaml")):
            doc = yaml.safe_load(path.read_text(encoding="utf-8"))
            checks += [pytest.param(c, id=f"{doc['id']}:{i}") for i, c in enumerate(doc["checks"])]
    return checks


def _run(args: dict[str, Any]) -> MeanLine | Error:
    a = dict(args)
    return mean_line(
        flow=a["flow"],
        pressure=FanTotalPressure(a["fan_total_pressure"]),
        omega=a["omega"],
        r_tip=a["r_tip"],
        r_hub=a["r_hub"],
        air=default_air(),
    )


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _checks())
def test_hand_calculations(check: dict[str, Any]) -> None:
    result = _run(check["args"])
    if "expected_error" in check:
        assert isinstance(result, Error)
        assert result.code == check["expected_error"]["code"]
        assert result.subject == check["expected_error"]["subject"]
        return
    assert isinstance(result, MeanLine), result
    values = asdict(result)
    for key, expected in check["expected"].items():
        assert values[key] == pytest.approx(expected, rel=check["rel_tol"]), key


@pytest.mark.req("PHY-005")
def test_there_is_at_least_one_l1_hand_calculation() -> None:
    assert len(_checks()) >= 3


@st.composite
def duties(draw: st.DrawFn) -> dict[str, float]:
    r_tip = draw(st.floats(0.02, 0.3))
    return {
        "flow": draw(st.floats(1e-3, 2.0)),
        "fan_total_pressure": draw(st.floats(1.0, 1000.0)),
        "omega": draw(st.floats(10.0, 40.0 / r_tip)),  # tip speed under the D3 limit
        "r_tip": r_tip,
        "r_hub": r_tip * draw(st.floats(0.1, 0.9)),
    }


@pytest.mark.req("PHY-001")
@given(duties())
def test_velocity_triangles_satisfy_euler_and_close(d: dict[str, float]) -> None:
    r = _run(d)
    assert isinstance(r, MeanLine)
    rho = default_air().density
    # Euler: rho U c_theta2 = dp_t (loss-free, no inlet swirl).
    assert rho * r.blade_speed * r.outlet_swirl == pytest.approx(d["fan_total_pressure"], rel=1e-12)
    # Triangle closure: tan(beta1) - tan(beta2) = c_theta2 / c_x.
    assert math.tan(r.relative_inlet_angle) - math.tan(r.relative_outlet_angle) == pytest.approx(
        r.outlet_swirl / r.axial_velocity, rel=1e-9, abs=1e-9
    )
    # Continuity: c_x A = Q; equal-area mean radius.
    assert r.axial_velocity * r.annulus_area == pytest.approx(d["flow"], rel=1e-12)
    inner = math.pi * (r.mean_radius**2 - d["r_hub"] ** 2)
    assert inner == pytest.approx(r.annulus_area / 2.0, rel=1e-9)
    assert r.work_coefficient == pytest.approx(
        d["fan_total_pressure"] / (rho * r.blade_speed**2), rel=1e-12
    )


@pytest.mark.req("PHY-002")
@pytest.mark.parametrize(
    ("field", "value", "subject"),
    [
        ("flow", 0.0, "flow"),
        ("fan_total_pressure", -1.0, "fan_total_pressure"),
        ("omega", math.inf, "omega"),
        ("r_hub", 0.07, "r_hub"),
        ("fan_total_pressure", 5000.0, "fan_total_pressure"),
        ("omega", 2000.0, "tip_speed"),
    ],
)
def test_outside_validity_is_an_error_never_a_number(
    field: str, value: float, subject: str
) -> None:
    d = {"flow": 0.05, "fan_total_pressure": 40.0, "omega": 209.4, "r_tip": 0.0595, "r_hub": 0.02}
    d[field] = value
    r = _run(d)
    assert isinstance(r, Error)
    assert r.code == "out_of_validity"
    assert r.subject == subject


# --- PHY-005: the kernel port agrees with the reference ---------------------------------------


@pytest.mark.req("PHY-005")
@given(duties())
def test_kernel_mean_line_agrees_with_the_reference(d: dict[str, float]) -> None:
    from cemkit import kernel

    expected = _run(d)
    assert isinstance(expected, MeanLine)
    (row,) = kernel.l0_batch([{"function": "mean_line", "args": d}])
    assert row["fidelity"] == "l1_predicted"
    assert row["model"] == "fans.axial.mean_line@0.1.0"
    for key, value in asdict(expected).items():
        assert row["value"][key] == pytest.approx(value, rel=1e-12, abs=1e-15), key


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _checks())
def test_kernel_mean_line_matches_the_hand_calculations(check: dict[str, Any]) -> None:
    from cemkit import kernel

    (row,) = kernel.l0_batch([{"function": "mean_line", "args": check["args"]}])
    if "expected_error" in check:
        assert row["error"]["code"] == check["expected_error"]["code"]
        assert row["error"]["subject"] == check["expected_error"]["subject"]
        return
    for key, expected in check["expected"].items():
        assert row["value"][key] == pytest.approx(expected, rel=check["rel_tol"]), key

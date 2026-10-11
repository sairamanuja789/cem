"""L1 free-vortex blade design (fans.axial.free_vortex@0.1.0, PHY-001, PHY-005): the Python
reference against hand calculations and identities, and the kernel port against the reference.
Inputs are illustrative or TRIAL values (ADR-016), not requirements."""

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
from cemkit.reference.fans.axial.free_vortex import FreeVortexDesign, free_vortex_design
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import default_air

HAND_CALCS = Path(__file__).resolve().parents[1] / "hand_calcs"


def _checks() -> list[Any]:
    checks = []
    for folder in ("verified", "proposed"):
        for path in sorted((HAND_CALCS / folder).glob("l1_*.yaml")):
            doc = yaml.safe_load(path.read_text(encoding="utf-8"))
            checks += [
                pytest.param(c, id=f"{doc['id']}:{i}")
                for i, c in enumerate(doc["checks"])
                if c["function"] == "free_vortex_design"
            ]
    return checks


def _run(a: dict[str, Any]) -> FreeVortexDesign | Error:
    return free_vortex_design(
        flow=a["flow"],
        pressure=FanTotalPressure(a["fan_total_pressure"]),
        omega=a["omega"],
        r_tip=a["r_tip"],
        r_hub=a["r_hub"],
        air=default_air(),
        chord_hub=a["chord_hub"],
        chord_tip=a["chord_tip"],
        thickness=a["thickness"],
        stations=a["stations"],
        embed_depth=a["embed_depth"],
    )


def _as_dict(d: FreeVortexDesign) -> dict[str, Any]:
    return {"axial_velocity": d.axial_velocity, "stations": [asdict(s) for s in d.stations]}


def _compare(actual: dict[str, Any], check: dict[str, Any]) -> None:
    expected = check["expected"]
    assert actual["axial_velocity"] == pytest.approx(expected["axial_velocity"], rel=1e-9)
    assert len(actual["stations"]) == len(expected["stations"])
    for got, want in zip(actual["stations"], expected["stations"], strict=True):
        for key, value in want.items():
            if isinstance(value, bool):
                assert got[key] is value, key
            else:
                assert got[key] == pytest.approx(value, rel=check["rel_tol"]), key


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _checks())
def test_hand_calculations(check: dict[str, Any]) -> None:
    result = _run(check["args"])
    if "expected_error" in check:
        assert isinstance(result, Error)
        assert (result.code, result.subject) == (
            check["expected_error"]["code"],
            check["expected_error"]["subject"],
        )
        return
    assert isinstance(result, FreeVortexDesign), result
    _compare(_as_dict(result), check)


@pytest.mark.req("PHY-005")
def test_there_are_free_vortex_hand_calculations() -> None:
    assert len(_checks()) >= 3


@st.composite
def designs(draw: st.DrawFn) -> dict[str, Any]:
    r_tip = draw(st.floats(0.03, 0.3))
    r_hub = r_tip * draw(st.floats(0.15, 0.8))
    chord_hub = draw(st.floats(0.005, 0.1))
    chord_tip = draw(st.floats(0.005, 0.1))
    return {
        "flow": draw(st.floats(1e-3, 2.0)),
        "fan_total_pressure": draw(st.floats(1.0, 500.0)),
        "omega": draw(st.floats(10.0, 40.0 / r_tip)),
        "r_tip": r_tip,
        "r_hub": r_hub,
        "chord_hub": chord_hub,
        "chord_tip": chord_tip,
        "thickness": min(chord_hub, chord_tip) * draw(st.floats(0.01, 0.5)),
        "stations": draw(st.integers(3, 9)),
        "embed_depth": r_hub * draw(st.floats(0.01, 0.9)),
    }


@pytest.mark.req("PHY-001")
@given(designs())
def test_each_station_satisfies_euler_and_the_arc_geometry(a: dict[str, Any]) -> None:
    d = _run(a)
    assert isinstance(d, FreeVortexDesign)
    rho = default_air().density
    root, *stations = d.stations
    assert root.embedded and root.radius == pytest.approx(a["r_hub"] - a["embed_depth"])
    assert stations[0].radius == pytest.approx(a["r_hub"])
    assert stations[-1].radius == pytest.approx(a["r_tip"])
    for s in stations:
        # Euler at every radius gives the same total-pressure rise (free vortex).
        assert rho * s.blade_speed * s.outlet_swirl == pytest.approx(a["fan_total_pressure"])
        assert s.camber_angle == pytest.approx(s.relative_inlet_angle - s.relative_outlet_angle)
        assert s.camber_angle > 0.0
        # The arc's end tangents make theta/2 with its chord: R = c / (2 sin(theta/2)).
        big_r = s.chord / (2.0 * math.sin(s.camber_angle / 2.0))
        # 1 - cos(x) = 2 sin^2(x / 2), without cancellation at small camber angles
        assert s.camber == pytest.approx(big_r * 2.0 * math.sin(s.camber_angle / 4.0) ** 2)
        assert s.recipe_stagger == pytest.approx(-s.stagger_from_axis)


# --- PHY-005: the kernel port agrees with the reference ---------------------------------------


@pytest.mark.req("PHY-005")
@given(designs())
def test_kernel_free_vortex_agrees_with_the_reference(a: dict[str, Any]) -> None:
    from cemkit import kernel

    expected = _run(a)
    assert isinstance(expected, FreeVortexDesign)
    (row,) = kernel.l0_batch([{"function": "free_vortex_design", "args": a}])
    assert row["fidelity"] == "l1_predicted"
    assert row["model"] == "fans.axial.free_vortex@0.1.0"
    want = _as_dict(expected)
    assert row["value"]["axial_velocity"] == pytest.approx(want["axial_velocity"], rel=1e-12)
    for got, station in zip(row["value"]["stations"], want["stations"], strict=True):
        for key, value in station.items():
            assert got[key] == pytest.approx(value, rel=1e-12, abs=1e-15), key


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _checks())
def test_kernel_free_vortex_matches_the_hand_calculations(check: dict[str, Any]) -> None:
    from cemkit import kernel

    (row,) = kernel.l0_batch([{"function": "free_vortex_design", "args": check["args"]}])
    if "expected_error" in check:
        assert (row["error"]["code"], row["error"]["subject"]) == (
            check["expected_error"]["code"],
            check["expected_error"]["subject"],
        )
        return
    _compare(row["value"], check)

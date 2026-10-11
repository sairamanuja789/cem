"""PHY-001, PHY-005, PHY-007: the general vortex reference (fans.axial.vortex@0.1.0) against hand
calculations, the free vortex at n = -1 and its own continuity and duty conditions, and the kernel
port against the reference."""

from __future__ import annotations

import math
from collections.abc import Callable
from dataclasses import asdict
from pathlib import Path
from typing import Any

import pytest
import yaml
from hypothesis import given, settings
from hypothesis import strategies as st

from cemkit.errors import Error
from cemkit.reference.fans.axial.free_vortex import FreeVortexDesign, free_vortex_design
from cemkit.reference.fans.axial.vortex import VortexDesign, vortex_design
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import default_air

HAND_CALCS = Path(__file__).resolve().parents[1] / "hand_calcs"
TRIAL = {
    "flow": 0.028,
    "fan_total_pressure": 21.6,
    "omega": 209.43951023931953,
    "r_tip": 0.0595,
    "r_hub": 0.028,
    "chord_hub": 0.020,
    "chord_tip": 0.016,
    "thickness": 0.0012,
    "stations": 5,
    "embed_depth": 0.003,
}


def _checks() -> list[Any]:
    checks = []
    for folder in ("verified", "proposed"):
        for path in sorted((HAND_CALCS / folder).glob("l1_*.yaml")):
            doc = yaml.safe_load(path.read_text(encoding="utf-8"))
            checks += [
                pytest.param(c, id=f"{doc['id']}:{i}")
                for i, c in enumerate(doc["checks"])
                if c["function"] == "vortex_design"
            ]
    return checks


def _free(a: dict[str, Any]) -> FreeVortexDesign | Error:
    b = {k: v for k, v in a.items() if k != "vortex_exponent"}
    return free_vortex_design(
        flow=b["flow"],
        pressure=FanTotalPressure(b["fan_total_pressure"]),
        omega=b["omega"],
        r_tip=b["r_tip"],
        r_hub=b["r_hub"],
        air=default_air(),
        chord_hub=b["chord_hub"],
        chord_tip=b["chord_tip"],
        thickness=b["thickness"],
        stations=b["stations"],
        embed_depth=b["embed_depth"],
    )


def _run(a: dict[str, Any]) -> VortexDesign | Error:
    return vortex_design(
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
        vortex_exponent=a["vortex_exponent"],
    )


def _as_dict(d: VortexDesign) -> dict[str, Any]:
    return {
        "axial_velocity": d.axial_velocity,
        "swirl_constant": d.swirl_constant,
        "hub_axial_velocity_squared": d.hub_axial_velocity_squared,
        "stations": [asdict(s) for s in d.stations],
    }


def _compare(actual: dict[str, Any], check: dict[str, Any]) -> None:
    expected = check["expected"]
    tol = check["rel_tol"]
    for key in ("axial_velocity", "swirl_constant"):
        assert actual[key] == pytest.approx(expected[key], rel=tol), key
    assert len(actual["stations"]) == len(expected["stations"])
    for got, want in zip(actual["stations"], expected["stations"], strict=True):
        for key, value in want.items():
            if isinstance(value, bool):
                assert got[key] is value, key
            else:
                assert got[key] == pytest.approx(value, rel=tol), key


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
    assert isinstance(result, VortexDesign), result
    _compare(_as_dict(result), check)


@pytest.mark.req("PHY-005")
def test_there_are_vortex_hand_calculations_for_free_and_constant_swirl() -> None:
    exponents = {c.values[0]["args"]["vortex_exponent"] for c in _checks()}
    assert {-1.0, 0.0} <= exponents


@st.composite
def designs(draw: st.DrawFn) -> dict[str, Any]:
    r_tip = draw(st.floats(0.03, 0.3))
    r_hub = r_tip * draw(st.floats(0.3, 0.6))
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
        "stations": draw(st.integers(3, 7)),
        "embed_depth": r_hub * draw(st.floats(0.01, 0.9)),
        "vortex_exponent": draw(st.floats(-1.0, 1.0)),
    }


@pytest.mark.req("PHY-001")
@settings(max_examples=30, deadline=None)
@given(designs())
def test_n_minus_one_is_the_free_vortex(a: dict[str, Any]) -> None:
    a = {**a, "vortex_exponent": -1.0}
    free = _free(a)
    general = _run(a)
    if isinstance(free, Error):
        assert isinstance(general, Error) and general.code == free.code
        return
    assert isinstance(general, VortexDesign), general
    for g, f in zip(general.stations, free.stations, strict=True):
        for key, value in asdict(f).items():
            if isinstance(value, bool):
                assert getattr(g, key) is value
            else:
                assert getattr(g, key) == pytest.approx(value, rel=1e-10, abs=1e-15), key


@pytest.mark.req("PHY-001")
@pytest.mark.parametrize("n", [-1.0, -0.5, 0.0])
def test_the_solution_meets_continuity_the_duty_and_radial_equilibrium(n: float) -> None:
    """An independent check on a fine trapezoid grid: the solved c_x2(r) carries the flow, its
    mass-averaged Euler rise is the duty, and it satisfies the equilibrium ODE."""
    d = _run({**TRIAL, "vortex_exponent": n})
    assert isinstance(d, VortexDesign), d
    w, rh, rt, k, a_hub = (
        TRIAL["omega"],
        TRIAL["r_hub"],
        TRIAL["r_tip"],
        d.swirl_constant,
        d.hub_axial_velocity_squared,
    )

    def cx2(r: float) -> float:
        work = 2.0 * w * k * (r ** (n + 1.0) - rh ** (n + 1.0))
        i = math.log(r / rh) if n == 0.0 else (r ** (2.0 * n) - rh ** (2.0 * n)) / (2.0 * n)
        return math.sqrt(a_hub + work - 2.0 * (n + 1.0) * k * k * i)

    m = 20000
    h = (rt - rh) / m
    rs = [rh + j * h for j in range(m + 1)]

    def trap(f: Callable[[float], float]) -> float:
        return h * (sum(f(r) for r in rs) - 0.5 * (f(rs[0]) + f(rs[-1])))

    flow = trap(lambda r: 2.0 * math.pi * r * cx2(r))
    rise = 1.18 * w * k * trap(lambda r: 2.0 * math.pi * r * r ** (n + 1.0) * cx2(r)) / flow
    assert flow == pytest.approx(TRIAL["flow"], rel=1e-6)
    assert rise == pytest.approx(TRIAL["fan_total_pressure"], rel=1e-6)
    # d(c_x2^2/2)/dr = omega d(r c_theta)/dr - (c_theta/r) d(r c_theta)/dr, by central differences
    r, e = 0.04, 1e-6
    lhs = (cx2(r + e) ** 2 - cx2(r - e) ** 2) / (4.0 * e)
    rct = (n + 1.0) * k * r**n  # d(r c_theta)/dr with c_theta = K r^n
    rhs = w * rct - (k * r**n / r) * rct
    assert lhs == pytest.approx(rhs, rel=1e-5)
    for s in d.stations[1:]:
        assert s.outlet_axial_velocity == pytest.approx(cx2(s.radius), rel=1e-12)
        assert s.outlet_swirl == pytest.approx(k * s.radius**n, rel=1e-12)


@pytest.mark.req("PHY-007")
def test_out_of_range_exponents_and_reverse_flow_are_refused() -> None:
    for n in (-1.5, 1.5, math.nan):
        result = _run({**TRIAL, "vortex_exponent": n})
        assert isinstance(result, Error) and result.subject == "vortex_exponent"
    reverse = _run({**TRIAL, "r_hub": 0.020, "vortex_exponent": 1.0})
    assert isinstance(reverse, Error) and "reverse flow" in reverse.message


# --- PHY-005: the kernel port agrees with the reference ---------------------------------------


@pytest.mark.req("PHY-005")
@settings(max_examples=40, deadline=None)
@given(designs())
def test_kernel_vortex_agrees_with_the_reference(a: dict[str, Any]) -> None:
    from cemkit import kernel

    expected = _run(a)
    (row,) = kernel.l0_batch([{"function": "vortex_design", "args": a}])
    if isinstance(expected, Error):
        assert (row["error"]["code"], row["error"]["subject"]) == (expected.code, expected.subject)
        return
    assert row["model"] == "fans.axial.vortex@0.1.0"
    want = _as_dict(expected)
    for key in ("axial_velocity", "swirl_constant", "hub_axial_velocity_squared"):
        assert row["value"][key] == pytest.approx(want[key], rel=1e-9, abs=1e-15), key
    for got, station in zip(row["value"]["stations"], want["stations"], strict=True):
        for key, value in station.items():
            assert got[key] == pytest.approx(value, rel=1e-9, abs=1e-12), key


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _checks())
def test_kernel_vortex_matches_the_hand_calculations(check: dict[str, Any]) -> None:
    from cemkit import kernel

    (row,) = kernel.l0_batch([{"function": "vortex_design", "args": check["args"]}])
    if "expected_error" in check:
        assert (row["error"]["code"], row["error"]["subject"]) == (
            check["expected_error"]["code"],
            check["expected_error"]["subject"],
        )
        return
    _compare(row["value"], check)

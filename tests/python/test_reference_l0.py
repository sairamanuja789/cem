"""T06: the L0 reference (PHY-001, PHY-002, PHY-003, PHY-005, SEL-001; ADR-003).

- Hand calculations: every file in tests/hand_calcs/verified/ (the acceptance evidence) and in
  tests/hand_calcs/proposed/ (checked too, so that slips are caught before review).
- Hypothesis properties (ADR-003 D7): invariance and exact-ratio tests draw only states valid on
  both sides of the scaling; separate tests check that states outside the limits return
  out_of_validity.
- Typing (ADR-003 D1): passing a fan static pressure where a fan total pressure is required fails
  mypy, with a control that passes.
"""

import math
from pathlib import Path
from typing import Any

import pytest
import yaml
from hypothesis import assume, given
from hypothesis import strategies as st
from mypy import api as mypy_api

from cemkit.errors import Error
from cemkit.reference.fans.common import l0
from cemkit.reference.fans.common.l0 import FanPoint, FanStaticPressure, FanTotalPressure
from cemkit.reference.platform import air as air_mod
from cemkit.reference.platform.air import Air, default_air

ROOT = Path(__file__).resolve().parents[2]
HAND_CALCS = ROOT / "tests" / "hand_calcs"
AIR = default_air()
_U_MAX = l0.max_tip_speed(AIR)
_P_MAX = l0.max_fan_total_pressure(AIR)
assert isinstance(_U_MAX, float) and isinstance(_P_MAX, float)
U_MAX: float = _U_MAX
P_MAX: float = _P_MAX

# --- hand calculations ---------------------------------------------------------------------------


def _hand_calc_checks(folder: str) -> list[Any]:
    params = []
    for path in sorted((HAND_CALCS / folder).glob("l0_*.yaml")):
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        for index, check in enumerate(doc["checks"]):
            params.append(pytest.param(check, id=f"{folder}/{path.stem}[{index}]"))
    return params


def _air(overrides: dict[str, float] | None) -> Air:
    return Air(**{**AIR.__dict__, **(overrides or {})})


def _wrap(args: dict[str, Any]) -> dict[str, Any]:
    out = dict(args)
    if "fan_total_pressure" in out:
        out["pressure"] = FanTotalPressure(out.pop("fan_total_pressure"))
    if "fan_static_pressure" in out:
        out["pressure"] = FanStaticPressure(out.pop("fan_static_pressure"))
    return out


def _call(check: dict[str, Any]) -> Any:
    name = check["function"]
    args = dict(check["args"])
    air = _air(check.get("air"))
    if name == "tip_speed":
        return l0.tip_speed(**args)
    if name == "ideal_gas_density":
        return air_mod.ideal_gas_density(**args)
    if name == "scale_fan_laws":
        point = FanPoint(air=air, **_wrap(args.pop("point")))
        return l0.scale_fan_laws(point, air=air, **args)
    return getattr(l0, name)(air=air, **_wrap(args))


def _numbers(result: Any) -> Any:
    if isinstance(result, l0.FanDynamicPressure):
        return result.pa
    if isinstance(result, l0.Derived):
        assert result.provenance == "derived"
        return result.value.pa
    if isinstance(result, FanPoint):
        return {
            "flow": result.flow,
            "fan_total_pressure": result.pressure.pa,
            "power": result.power,
        }
    return result


def _assert_check(check: dict[str, Any]) -> None:
    result = _call(check)
    if "expected_error" in check:
        want = check["expected_error"]
        assert isinstance(result, Error), result
        assert result.code == want["code"]
        assert result.subject == want["subject"]
        assert dict(result.details) == want["details"]
        return
    assert not isinstance(result, Error), result
    got, want = _numbers(result), check["expected"]
    if isinstance(want, dict):
        assert set(got) == set(want)
        for key in want:
            assert math.isclose(got[key], want[key], rel_tol=check["rel_tol"]), key
    else:
        assert math.isclose(got, want, rel_tol=check["rel_tol"])


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _hand_calc_checks("verified"))
def test_verified_hand_calculations(check: dict[str, Any]) -> None:
    _assert_check(check)


@pytest.mark.req("PHY-005")
@pytest.mark.parametrize("check", _hand_calc_checks("proposed"))
def test_proposed_hand_calculations_agree_with_the_reference(check: dict[str, Any]) -> None:
    _assert_check(check)


@pytest.mark.req("PHY-005")
def test_there_are_at_least_five_l0_hand_calculations() -> None:
    files = list((HAND_CALCS / "verified").glob("l0_*.yaml")) + list(
        (HAND_CALCS / "proposed").glob("l0_*.yaml")
    )
    assert len(files) >= 5


# --- air ----------------------------------------------------------------------------------------


@pytest.mark.req("PHY-005")
def test_gas_constant_is_computed_from_the_1976_constants_not_typed() -> None:
    assert air_mod.GAS_CONSTANT_AIR == air_mod.UNIVERSAL_GAS_CONSTANT / air_mod.MOLAR_MASS_AIR
    assert math.isclose(air_mod.GAS_CONSTANT_AIR, 287.0530720, rel_tol=1e-9)


@pytest.mark.req("PHY-005")
def test_ideal_gas_density_at_the_defaults_is_within_half_a_percent_of_ax_005() -> None:
    rho = air_mod.ideal_gas_density(AIR.pressure, AIR.temperature)
    assert isinstance(rho, float)
    assert abs(rho - air_mod.DEFAULT_DENSITY) / air_mod.DEFAULT_DENSITY <= 0.005


@pytest.mark.req("PHY-003")
@pytest.mark.parametrize("bad", [0.0, -1.0, math.nan, math.inf])
def test_air_functions_reject_non_positive_or_non_finite_inputs(bad: float) -> None:
    result = air_mod.ideal_gas_density(bad, 298.15)
    assert isinstance(result, Error) and result.subject == "pressure"
    sound = air_mod.speed_of_sound(_air({"temperature": bad}))
    assert isinstance(sound, Error) and sound.subject == "temperature"


# --- valid states -------------------------------------------------------------------------------

# Points are drawn with U <= 0.45 U_max and dp_t <= 0.2 p_max, and scaled by speed and diameter
# ratios in [0.7, 1.4] and a density ratio in [0.8, 1.2]. Then U grows by at most 1.96 and dp_t by
# at most 4.6, so the scaled point is valid too: valid on both sides by construction (ADR-003 D7).
ratios = st.floats(min_value=0.7, max_value=1.4)


@st.composite
def fan_points(draw: st.DrawFn) -> FanPoint:
    d = draw(st.floats(min_value=0.05, max_value=2.0))
    u = draw(st.floats(min_value=1.0, max_value=0.45 * U_MAX))
    phi = draw(st.floats(min_value=0.05, max_value=1.0))
    psi = draw(st.floats(min_value=0.05, max_value=2.0))
    dp = psi * AIR.density * u**2 / 2.0
    assume(dp <= 0.2 * P_MAX)
    flow = phi * math.pi / 4.0 * d**2 * u
    power = draw(st.floats(min_value=0.1, max_value=1.0e4))
    return FanPoint(2.0 * u / d, d, flow, FanTotalPressure(dp), power, AIR)


def _scaled(point: FanPoint, n: float, d: float, r: float) -> FanPoint:
    air = _air({"density": AIR.density * r})
    result = l0.scale_fan_laws(point, point.omega * n, point.d_tip * d, air)
    assert isinstance(result, FanPoint), result
    return result


def _value(result: float | Error) -> float:
    assert isinstance(result, float), result
    return result


def _coefficients(p: FanPoint) -> tuple[float, float, float, float]:
    return (
        _value(l0.flow_coefficient(p.flow, p.d_tip, p.omega, p.air)),
        _value(l0.pressure_coefficient(p.pressure, p.d_tip, p.omega, p.air)),
        _value(l0.specific_speed(p.omega, p.flow, p.pressure, p.air)),
        _value(l0.specific_diameter(p.d_tip, p.flow, p.pressure, p.air)),
    )


@pytest.mark.req("PHY-005")
@given(fan_points(), ratios, ratios, st.floats(min_value=0.8, max_value=1.2))
def test_fan_law_ratios_are_exact(point: FanPoint, n: float, d: float, r: float) -> None:
    s = _scaled(point, n, d, r)
    rel = 1e-12
    assert math.isclose(s.flow / point.flow, n * d**3, rel_tol=rel)
    assert math.isclose(s.pressure.pa / point.pressure.pa, r * n**2 * d**2, rel_tol=rel)
    assert math.isclose(s.power / point.power, r * n**3 * d**5, rel_tol=rel)


@pytest.mark.req("PHY-005")
@given(fan_points())
def test_doubling_speed_doubles_flow_at_fixed_diameter(point: FanPoint) -> None:
    result = l0.scale_fan_laws(point, 2.0 * point.omega, point.d_tip, point.air)
    assert isinstance(result, FanPoint), result
    assert math.isclose(result.flow, 2.0 * point.flow, rel_tol=1e-12)
    assert math.isclose(result.pressure.pa, 4.0 * point.pressure.pa, rel_tol=1e-12)
    assert math.isclose(result.power, 8.0 * point.power, rel_tol=1e-12)


@pytest.mark.req("SEL-001")
@pytest.mark.req("PHY-005")
@given(fan_points(), ratios, ratios, st.floats(min_value=0.8, max_value=1.2))
def test_coefficients_and_specific_values_are_invariant_under_fan_law_scaling(
    point: FanPoint, n: float, d: float, r: float
) -> None:
    before = _coefficients(point)
    after = _coefficients(_scaled(point, n, d, r))
    for b, a in zip(before, after, strict=True):
        assert math.isclose(a, b, rel_tol=1e-10)


@pytest.mark.req("SPEC-004")
@given(
    st.floats(min_value=0.0, max_value=500.0),  # 0 is free delivery (ADR-003 D1)
    st.floats(min_value=0.001, max_value=0.2),
    st.floats(min_value=0.1, max_value=1.0),
)
def test_static_to_total_adds_the_conventional_dynamic_pressure(
    static: float, flow: float, d_duct: float
) -> None:
    result = l0.fan_total_from_static(FanStaticPressure(static), flow, d_duct, AIR)
    velocity = flow / (math.pi * d_duct**2 / 4.0)
    dynamic = 0.5 * AIR.density * velocity**2
    if static + dynamic > P_MAX:
        assert isinstance(result, Error) and result.subject == "fan_total_pressure"
        return
    assert isinstance(result, l0.Derived)
    assert result.provenance == "derived"
    assert result.value.pa > static
    # Compare the total, not total - static: the difference loses digits to cancellation when the
    # dynamic pressure is tiny next to the static pressure.
    assert math.isclose(result.value.pa, static + dynamic, rel_tol=1e-12)


@pytest.mark.req("SPEC-004")
def test_free_delivery_converts_to_the_dynamic_pressure_alone() -> None:
    # ADR-003 D1: fan static pressure 0 is the free-delivery rating point; the total is exactly
    # the conventional fan dynamic pressure.
    # Exact == because this copies the reference's operation order; the C++ port (T08) must keep
    # that order or compare with a tolerance. -0.0 is the same value as 0.0 and is accepted.
    flow, d_duct = 0.05, 0.12
    velocity = flow / (math.pi * (d_duct * d_duct) / 4.0)
    for zero in (0.0, -0.0):
        result = l0.fan_total_from_static(FanStaticPressure(zero), flow, d_duct, AIR)
        assert isinstance(result, l0.Derived)
        assert result.value.pa == 0.5 * AIR.density * (velocity * velocity)


@pytest.mark.req("SPEC-004")
def test_fan_dynamic_pressure_is_typed_and_equals_the_free_delivery_total() -> None:
    # l0_010: Q = 0.05 m3/s, D_duct = 0.12 m gives p_d = 11.53153903 Pa, the free-delivery total.
    dynamic = l0.conventional_dynamic_pressure(0.05, 0.12, AIR)
    assert isinstance(dynamic, l0.FanDynamicPressure)
    total = l0.fan_total_from_static(FanStaticPressure(0.0), 0.05, 0.12, AIR)
    assert isinstance(total, l0.Derived)
    assert dynamic.pa == total.value.pa
    assert math.isclose(dynamic.pa, 11.53153903336792, rel_tol=1e-9)


@pytest.mark.req("PHY-003")
def test_fan_dynamic_pressure_outside_validity() -> None:
    zero_flow = l0.conventional_dynamic_pressure(0.0, 0.12, AIR)
    assert isinstance(zero_flow, Error) and zero_flow.subject == "flow"
    bad_duct = l0.conventional_dynamic_pressure(0.05, -0.12, AIR)
    assert isinstance(bad_duct, Error) and bad_duct.subject == "d_duct"
    bad_air = l0.conventional_dynamic_pressure(0.05, 0.12, _air({"gamma": 0.0}))
    assert isinstance(bad_air, Error) and bad_air.subject == "air.gamma"
    # 1/2 rho v^2 > EPSILON gamma p1 needs v > 49 m/s: Q = 1 m3/s through D = 0.12 m is 88 m/s.
    fast = l0.conventional_dynamic_pressure(1.0, 0.12, AIR)
    assert isinstance(fast, Error)
    assert (fast.code, fast.subject) == ("out_of_validity", "fan_dynamic_pressure")
    assert fast.detail("bounds") == "(0, 1418.55]"


@pytest.mark.req("PHY-003")
def test_free_delivery_with_an_underflowing_dynamic_pressure_is_out_of_validity() -> None:
    # Q = 1e-300 m3/s squares to 0, so the total would be 0, outside (0, limit].
    result = l0.fan_total_from_static(FanStaticPressure(0.0), 1e-300, 0.12, AIR)
    assert isinstance(result, Error)
    assert (result.code, result.subject) == ("out_of_validity", "fan_total_pressure")
    assert result.detail("bounds") == "(0, inf)"


@pytest.mark.req("PHY-003")
@pytest.mark.parametrize("bad", [-1e-300, -1.0, math.nan, math.inf, -math.inf])
def test_negative_or_non_finite_static_pressure_is_out_of_validity(bad: float) -> None:
    result = l0.fan_total_from_static(FanStaticPressure(bad), 0.05, 0.12, AIR)
    assert isinstance(result, Error)
    assert (result.code, result.subject) == ("out_of_validity", "fan_static_pressure")
    assert result.detail("bounds") == "[0, inf)"
    assert result.detail("model") == l0.MODEL


# --- outside the limits -------------------------------------------------------------------------


@pytest.mark.req("PHY-003")
@given(st.floats(min_value=1.0 + 1e-9, max_value=20.0))
def test_pressure_beyond_the_limit_is_out_of_validity(factor: float) -> None:
    pressure = FanTotalPressure(P_MAX * factor)
    assume(pressure.pa > P_MAX)
    for result in (
        l0.specific_speed(200.0, 0.05, pressure, AIR),
        l0.specific_diameter(0.119, 0.05, pressure, AIR),
        l0.pressure_coefficient(pressure, 0.119, 200.0, AIR),
    ):
        assert isinstance(result, Error)
        assert (result.code, result.subject) == ("out_of_validity", "fan_total_pressure")


@pytest.mark.req("PHY-003")
@given(st.floats(min_value=1.0 + 1e-9, max_value=20.0))
def test_tip_speed_beyond_the_limit_is_out_of_validity(factor: float) -> None:
    d = 0.5
    omega = 2.0 * U_MAX * factor / d
    assume(omega * d / 2.0 > U_MAX)
    for result in (
        l0.flow_coefficient(0.05, d, omega, AIR),
        l0.pressure_coefficient(FanTotalPressure(100.0), d, omega, AIR),
    ):
        assert isinstance(result, Error)
        assert (result.code, result.subject) == ("out_of_validity", "tip_speed")


@pytest.mark.req("PHY-003")
@given(fan_points(), st.floats(min_value=3.0, max_value=10.0))
def test_scaling_out_of_the_valid_region_is_out_of_validity(point: FanPoint, n: float) -> None:
    # Speed x n (n >= 3) at fixed D: tip speed grows by n, pressure by n^2. Keep only draws where
    # at least one of them leaves the valid range; those must be out_of_validity.
    scaled_u = point.omega * n * point.d_tip / 2.0
    scaled_p = point.pressure.pa * n**2
    assume(scaled_u > U_MAX or scaled_p > P_MAX)
    result = l0.scale_fan_laws(point, point.omega * n, point.d_tip, point.air)
    assert isinstance(result, Error)
    assert result.code == "out_of_validity"
    assert result.subject in {"tip_speed", "fan_total_pressure"}


@pytest.mark.req("PHY-003")
@pytest.mark.parametrize("bad", [0.0, -1.0, math.nan, math.inf, -math.inf])
def test_non_positive_or_non_finite_inputs_are_out_of_validity(bad: float) -> None:
    cases: list[tuple[float | Error | l0.Derived[FanTotalPressure] | FanPoint, str]] = [
        (l0.tip_speed(bad, 0.1), "omega"),
        (l0.flow_coefficient(bad, 0.1, 100.0, AIR), "flow"),
        (l0.pressure_coefficient(FanTotalPressure(bad), 0.1, 100.0, AIR), "fan_total_pressure"),
        (
            l0.specific_speed(100.0, 0.05, FanTotalPressure(100.0), _air({"density": bad})),
            "air.density",
        ),
        (l0.specific_diameter(bad, 0.05, FanTotalPressure(100.0), AIR), "d_tip"),
        (l0.fan_total_from_static(FanStaticPressure(100.0), 0.05, bad, AIR), "d_duct"),
    ]
    for result, subject in cases:
        assert isinstance(result, Error), subject
        assert (result.code, result.subject) == ("out_of_validity", subject)
        assert result.detail("bounds") == "(0, inf)"
        assert result.detail("model") == l0.MODEL


@pytest.mark.req("PHY-003")
def test_a_converted_total_pressure_beyond_the_limit_is_out_of_validity() -> None:
    # 1400 Pa static is inside the limit; adding 1/2 rho v^2 at v = 10 m/s (59 Pa) is not.
    d_duct = 0.12
    flow = 10.0 * math.pi * d_duct**2 / 4.0
    result = l0.fan_total_from_static(FanStaticPressure(1400.0), flow, d_duct, AIR)
    assert isinstance(result, Error)
    assert (result.code, result.subject) == ("out_of_validity", "fan_total_pressure")
    assert result.detail("bounds") == "(0, 1418.55]"


@pytest.mark.req("PHY-003")
def test_limits_need_valid_air() -> None:
    for limit in (l0.max_tip_speed, l0.max_fan_total_pressure):
        result = limit(_air({"temperature": -1.0}))
        assert isinstance(result, Error) and result.subject == "air.temperature"
    sound = air_mod.speed_of_sound(AIR)
    assert isinstance(sound, float)
    assert math.isclose(sound, 346.1485559744042, rel_tol=1e-12)
    assert math.isclose(U_MAX, math.sqrt(0.02) * sound, rel_tol=1e-15)


@pytest.mark.req("PHY-003")
def test_scaling_rejects_an_invalid_target() -> None:
    point = FanPoint(200.0, 0.119, 0.05, FanTotalPressure(150.0), 15.0, AIR)
    result = l0.scale_fan_laws(point, -1.0, 0.119, AIR)
    assert isinstance(result, Error) and result.subject == "omega_new"


# --- typing (ADR-003 D1) ------------------------------------------------------------------------

TYPE_PROBE = """
from cemkit.reference.fans.common.l0 import FanStaticPressure, FanTotalPressure, specific_speed
from cemkit.reference.platform.air import default_air
specific_speed(200.0, 0.05, {pressure}, default_air())
"""


def _mypy(tmp_path: Path, pressure: str) -> tuple[str, int]:
    probe = tmp_path / "probe.py"
    probe.write_text(TYPE_PROBE.format(pressure=pressure))
    stdout, _, status = mypy_api.run(
        ["--strict", "--no-incremental", "--cache-dir", str(tmp_path / ".mypy"), str(probe)]
    )
    return stdout, status


@pytest.mark.req("SPEC-004")
def test_static_pressure_where_total_is_required_is_a_type_error(tmp_path: Path) -> None:
    out, status = _mypy(tmp_path, "FanTotalPressure(150.0)")
    assert status == 0, out
    out, status = _mypy(tmp_path, "FanStaticPressure(150.0)")
    assert status == 1
    assert 'incompatible type "FanStaticPressure"' in out

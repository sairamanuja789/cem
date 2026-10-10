"""Kernel vs reference cross-check for the L0 models and the feasibility gate (PHY-005; T08, T11).

Test-only code, like the rest of cemkit.reference. It builds the cases (every hand calculation in
tests/hand_calcs/verified/ and proposed/, edge cases, seeded random states) in the JSON form the
kernel's batch API takes (C ABI cemkit_l0_batch, Python cemkit.kernel.l0_batch), evaluates them
with the Python reference and compares the kernel's results:
- numbers to a relative tolerance of REL_TOL (floating-point tolerance);
- fidelity label, model ID, provenance and rule exactly;
- errors field for field, and their describe() text byte for byte (ADR-003 D6).

Used by scripts/crosscheck_l0.py (ctest fans_l0_crosscheck, through the C ABI) and by
tests/python/test_kernel_bindings.py (through the Python binding, one call for all cases).
"""

from __future__ import annotations

import copy
import math
import random
from pathlib import Path
from typing import Any

import yaml

from cemkit.errors import Error, describe
from cemkit.reference.fans.common import feasibility as gate
from cemkit.reference.fans.common import l0
from cemkit.reference.fans.common.l0 import FanPoint, FanStaticPressure, FanTotalPressure
from cemkit.reference.platform import air as air_mod
from cemkit.reference.platform.air import Air, default_air

ROOT = Path(__file__).resolve().parents[3]
HAND_CALCS = ROOT / "tests" / "hand_calcs"
SEED = 20261010
REL_TOL = 1e-12
FIXTURE_SOURCE = "test fixture, not engineering data"
AIR_FUNCTIONS = {"ideal_gas_density", "speed_of_sound"}

Case = dict[str, Any]


def _air_json(air: Air) -> dict[str, float]:
    return {
        "density": air.density,
        "temperature": air.temperature,
        "pressure": air.pressure,
        "gamma": air.gamma,
        "gas_constant": air.gas_constant,
    }


def _air(data: dict[str, float]) -> Air:
    return Air(**data)


# --- the reference side ----------------------------------------------------------------------


def _total(args: dict[str, Any]) -> FanTotalPressure:
    return FanTotalPressure(float(args["fan_total_pressure"]))


def _family_range(data: dict[str, Any]) -> gate.FamilyRange:
    return gate.FamilyRange(
        data["family"], data["specific_speed_min"], data["specific_speed_max"], data["source"]
    )


def reference(case: Case) -> Any:
    """Evaluates one case with the Python reference."""
    fn: str = case["function"]
    a: dict[str, Any] = case["args"]
    air = _air(case["air"])
    if fn == "tip_speed":
        return l0.tip_speed(a["omega"], a["d_tip"])
    if fn == "max_fan_total_pressure":
        return l0.max_fan_total_pressure(air)
    if fn == "max_tip_speed":
        return l0.max_tip_speed(air)
    if fn == "flow_coefficient":
        return l0.flow_coefficient(a["flow"], a["d_tip"], a["omega"], air)
    if fn == "pressure_coefficient":
        return l0.pressure_coefficient(_total(a), a["d_tip"], a["omega"], air)
    if fn == "specific_speed":
        return l0.specific_speed(a["omega"], a["flow"], _total(a), air)
    if fn == "specific_diameter":
        return l0.specific_diameter(a["d_tip"], a["flow"], _total(a), air)
    if fn == "conventional_dynamic_pressure":
        return l0.conventional_dynamic_pressure(a["flow"], a["d_duct"], air)
    if fn == "fan_total_from_static":
        return l0.fan_total_from_static(
            FanStaticPressure(a["fan_static_pressure"]), a["flow"], a["d_duct"], air
        )
    if fn == "scale_fan_laws":
        p = a["point"]
        point = FanPoint(p["omega"], p["d_tip"], p["flow"], _total(p), p["power"], air)
        return l0.scale_fan_laws(
            point,
            a["omega"],
            a["d_tip"],
            _air(case["air_new"] if "air_new" in case else case["air"]),
        )
    if fn == "ideal_gas_density":
        return air_mod.ideal_gas_density(a["pressure"], a["temperature"])
    if fn == "speed_of_sound":
        return air_mod.speed_of_sound(air)
    if fn == "check_feasibility":
        duty = gate.Duty(a["flow"], _total(a))
        return gate.check_feasibility(
            duty, air, _family_range(case["range"]), a.get("omega"), a.get("d_tip")
        )
    raise ValueError(f"unknown function {fn}")


# --- comparison ------------------------------------------------------------------------------


def _error_json(err: Error) -> dict[str, Any]:
    return {
        "code": err.code,
        "subject": err.subject,
        "message": err.message,
        "details": dict(err.details),
        "describe": describe(err),
    }


class Comparison:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.numbers = 0
        self.errors = 0
        self.max_rel = 0.0

    def fail(self, case_id: str, text: str) -> None:
        self.failures.append(f"{case_id}: {text}")

    def number(self, case_id: str, what: str, want: float, got: Any) -> None:
        self.numbers += 1
        if not isinstance(got, int | float):
            self.fail(case_id, f"{what}: expected {want!r}, kernel gave {got!r}")
            return
        got_f = float(got)
        if want == got_f:
            return
        rel = abs(want - got_f) / max(abs(want), abs(got_f))
        self.max_rel = max(self.max_rel, rel)
        if not math.isclose(want, got_f, rel_tol=REL_TOL, abs_tol=0.0):
            self.fail(case_id, f"{what}: reference {want!r}, kernel {got_f!r} (rel {rel:.3g})")

    def error(self, case_id: str, what: str, want: Error, got: Any) -> None:
        self.errors += 1
        expected = _error_json(want)
        if got != expected:
            self.fail(case_id, f"{what}: reference error {expected}, kernel {got}")

    def label(self, case_id: str, got: dict[str, Any], model: str) -> None:
        if got.get("fidelity") != "l0_predicted" or got.get("model") != model:
            self.fail(case_id, f"label: {got.get('fidelity')} {got.get('model')}, want {model}")


def _compare_report(cmp: Comparison, cid: str, want: gate.Report, got: dict[str, Any]) -> None:
    if got.get("verdict") != want.verdict:
        cmp.fail(cid, f"verdict: reference {want.verdict}, kernel {got.get('verdict')}")
    ws = got.get("specific_speed")
    if want.specific_speed is None:
        if ws is not None:
            cmp.fail(cid, f"specific_speed: reference none, kernel {ws}")
    elif not isinstance(ws, dict):
        cmp.fail(cid, "specific_speed missing in kernel report")
    else:
        cmp.number(cid, "specific_speed", want.specific_speed, ws.get("value"))
        cmp.label(cid, ws, l0.MODEL)
    checks = got.get("checks", [])
    if len(checks) != len(want.checks):
        cmp.fail(cid, f"checks: reference {len(want.checks)}, kernel {len(checks)}")
        return
    for w, g in zip(want.checks, checks, strict=True):
        tag = f"check {w.limit}"
        for key, value in (("limit", w.limit), ("status", w.status), ("message", w.message)):
            if g.get(key) != value:
                cmp.fail(cid, f"{tag} {key}: reference {value!r}, kernel {g.get(key)!r}")
        if w.violation is None:
            if g.get("violation") is not None:
                cmp.fail(cid, f"{tag}: kernel has a violation the reference has not")
        else:
            cmp.error(cid, f"{tag} violation", w.violation, g.get("violation"))
        nearest = g.get("nearest_feasible")
        if w.nearest_feasible is None:
            if nearest is not None:
                cmp.fail(cid, f"{tag}: kernel has a nearest duty the reference has not")
        elif not isinstance(nearest, dict):
            cmp.fail(cid, f"{tag}: kernel has no nearest duty")
        else:
            flow, pressure = nearest.get("flow", {}), nearest.get("fan_total_pressure", {})
            cmp.number(cid, f"{tag} nearest flow", w.nearest_feasible.flow, flow.get("value"))
            cmp.number(
                cid,
                f"{tag} nearest pressure",
                w.nearest_feasible.pressure.pa,
                pressure.get("value"),
            )
            cmp.label(cid, flow, gate.MODEL)
            cmp.label(cid, pressure, gate.MODEL)


def compare(cmp: Comparison, case: Case, want: Any, got: dict[str, Any]) -> None:
    cid = case["id"]
    if got.get("id") != cid:
        cmp.fail(cid, f"result order: kernel row is {got.get('id')}")
        return
    if isinstance(want, Error):
        cmp.error(cid, "error", want, got.get("error"))
        return
    if "error" in got:
        cmp.fail(cid, f"reference returned a value, kernel an error: {got['error']['describe']}")
        return
    model = air_mod.MODEL if case["function"] in AIR_FUNCTIONS else l0.MODEL
    if isinstance(want, gate.Report):
        report = got.get("report")
        if not isinstance(report, dict):
            cmp.fail(cid, "kernel gave no report")
            return
        _compare_report(cmp, cid, want, report)
        return
    cmp.label(cid, got, model)
    value = got.get("value")
    if isinstance(want, l0.Derived):
        if got.get("provenance") != "derived" or got.get("rule") != want.rule:
            cmp.fail(cid, f"derived: {got.get('provenance')} / {got.get('rule')}")
        cmp.number(cid, "value", want.value.pa, value)
    elif isinstance(want, l0.FanDynamicPressure | FanTotalPressure):
        cmp.number(cid, "value", want.pa, value)
    elif isinstance(want, FanPoint):
        if not isinstance(value, dict):
            cmp.fail(cid, f"kernel gave {value!r} for a fan point")
            return
        cmp.number(cid, "flow", want.flow, value.get("flow"))
        cmp.number(cid, "fan_total_pressure", want.pressure.pa, value.get("fan_total_pressure"))
        cmp.number(cid, "power", want.power, value.get("power"))
    else:
        cmp.number(cid, "value", float(want), value)


# --- cases -----------------------------------------------------------------------------------


def hand_calc_cases() -> list[Case]:
    cases: list[Case] = []
    base = _air_json(default_air())
    for folder in ("verified", "proposed"):
        for path in sorted((HAND_CALCS / folder).glob("l0_*.yaml")):
            doc = yaml.safe_load(path.read_text(encoding="utf-8"))
            for index, check in enumerate(doc["checks"]):
                cases.append(
                    {
                        "id": f"{folder}/{path.stem}[{index}]",
                        "function": check["function"],
                        "args": check["args"],
                        "air": {**base, **(check.get("air") or {})},
                    }
                )
    return cases


def _state_cases(rng: random.Random, index: int, outside: bool) -> list[Case]:
    """Every L0 function and the gate at one random state, inside the validity range."""
    air = Air(
        density=rng.uniform(0.9, 1.4),
        temperature=rng.uniform(250.0, 330.0),
        pressure=rng.uniform(80_000.0, 105_000.0),
        gamma=rng.uniform(1.30, 1.45),
        gas_constant=air_mod.GAS_CONSTANT_AIR,
    )
    u_max = l0.max_tip_speed(air)
    p_max = l0.max_fan_total_pressure(air)
    assert isinstance(u_max, float) and isinstance(p_max, float)
    d_tip = math.exp(rng.uniform(math.log(0.02), math.log(2.0)))
    u = rng.uniform(0.02, 0.98) * u_max
    omega = 2.0 * u / d_tip
    p_t = rng.uniform(0.01, 0.98) * p_max
    flow = rng.uniform(0.02, 1.0) * math.pi / 4.0 * d_tip**2 * u
    d_duct = d_tip * rng.uniform(1.0, 1.1)
    p_s = rng.uniform(0.0, 0.5) * p_max
    power = rng.uniform(0.1, 1.0e4)
    air_new = Air(
        density=air.density * rng.uniform(0.8, 1.2),
        temperature=air.temperature,
        pressure=air.pressure,
        gamma=air.gamma,
        gas_constant=air.gas_constant,
    )
    ws = l0.specific_speed(omega, flow, FanTotalPressure(p_t), air)
    assert isinstance(ws, float)
    kind = index % 4
    family: dict[str, Any]
    if kind == 0:
        family = {
            "family": "fans.axial_ducted",
            "specific_speed_min": None,
            "specific_speed_max": None,
            "source": gate.UNSOURCED,
        }
    else:
        lo = ws * rng.uniform(0.5, 1.5)
        family = {
            "family": "fans.test_family",
            "specific_speed_min": lo,
            "specific_speed_max": lo * rng.uniform(1.0, 2.0),
            "source": FIXTURE_SOURCE,
        }
    gate_args: dict[str, float] = {"flow": flow, "fan_total_pressure": p_t}
    if kind != 3:
        gate_args["omega"] = omega
        gate_args["d_tip"] = d_tip
    point = {
        "omega": omega,
        "d_tip": d_tip,
        "flow": flow,
        "fan_total_pressure": p_t,
        "power": power,
    }
    calls: list[tuple[str, dict[str, Any]]] = [
        ("tip_speed", {"omega": omega, "d_tip": d_tip}),
        ("max_fan_total_pressure", {}),
        ("max_tip_speed", {}),
        ("flow_coefficient", {"flow": flow, "d_tip": d_tip, "omega": omega}),
        ("pressure_coefficient", {"fan_total_pressure": p_t, "d_tip": d_tip, "omega": omega}),
        ("specific_speed", {"omega": omega, "flow": flow, "fan_total_pressure": p_t}),
        ("specific_diameter", {"d_tip": d_tip, "flow": flow, "fan_total_pressure": p_t}),
        ("conventional_dynamic_pressure", {"flow": flow, "d_duct": d_duct}),
        ("fan_total_from_static", {"fan_static_pressure": p_s, "flow": flow, "d_duct": d_duct}),
        (
            "scale_fan_laws",
            {
                "point": point,
                "omega": omega * rng.uniform(0.7, 1.4),
                "d_tip": d_tip * rng.uniform(0.7, 1.4),
            },
        ),
        ("ideal_gas_density", {"pressure": air.pressure, "temperature": air.temperature}),
        ("speed_of_sound", {}),
        ("check_feasibility", gate_args),
    ]
    if outside:
        calls = [(fn, _push_outside(rng, args, p_max)) for fn, args in calls]
    cases: list[Case] = []
    for fn, args in calls:
        case: Case = {
            "id": f"random/{index}/{fn}",
            "function": fn,
            "args": args,
            "air": _air_json(air),
        }
        if fn == "scale_fan_laws":
            case["air_new"] = _air_json(air_new)
        if fn == "check_feasibility":
            case["range"] = family
        cases.append(case)
    return cases


def _push_outside(rng: random.Random, args: dict[str, Any], p_max: float) -> dict[str, Any]:
    """One input made invalid: zero, negative or (for pressures) beyond the limit."""
    if not args:
        return args
    out = copy.deepcopy(args)
    key = rng.choice(sorted(k for k in out if k != "point"))
    if key in ("fan_total_pressure", "fan_static_pressure") and rng.random() < 0.5:
        out[key] = p_max * rng.uniform(1.0001, 3.0)
    else:
        out[key] = rng.choice([0.0, -0.0, -1.0, -1e-300])
    return out


def edge_cases() -> list[Case]:
    """Fixed cases the carry-over notes name: -0.0 static pressure and an underflowing total."""
    air = _air_json(default_air())
    rows: list[tuple[str, dict[str, Any]]] = [
        ("fan_total_from_static", {"fan_static_pressure": -0.0, "flow": 0.05, "d_duct": 0.12}),
        ("fan_total_from_static", {"fan_static_pressure": 0.0, "flow": 1e-300, "d_duct": 0.12}),
        ("fan_total_from_static", {"fan_static_pressure": -1e-300, "flow": 0.05, "d_duct": 0.12}),
        ("conventional_dynamic_pressure", {"flow": 1.0, "d_duct": 0.12}),
        ("pressure_coefficient", {"fan_total_pressure": 1418.55, "d_tip": 0.119, "omega": 200.0}),
    ]
    return [
        {"id": f"edge/{i}/{fn}", "function": fn, "args": args, "air": air}
        for i, (fn, args) in enumerate(rows)
    ]


def build_cases(states: int, seed: int = SEED) -> tuple[list[Case], int, int]:
    """Hand-calculation and edge cases, `states` random states inside the validity range and
    states // 10 with one input pushed outside it. Returns the cases, the number of out-of-range
    states and the number of hand-calculation and edge cases (they come first)."""
    rng = random.Random(seed)
    cases = hand_calc_cases() + edge_cases()
    fixed = len(cases)
    outside = states // 10
    for index in range(states + outside):
        cases.extend(_state_cases(rng, index, outside=index >= states))
    return cases, outside, fixed


def compare_all(cases: list[Case], results: list[dict[str, Any]]) -> Comparison:
    """Compares the kernel's results, in case order, with the reference."""
    cmp = Comparison()
    if len(results) != len(cases):
        cmp.fail("all", f"kernel returned {len(results)} results for {len(cases)} cases")
    for case, got in zip(cases, results, strict=False):
        compare(cmp, case, reference(case), got)
    return cmp

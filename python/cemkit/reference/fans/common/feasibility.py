"""Reference implementation: L0 feasibility gate (T08; SEL-002, SEL-004, PHY-003, PHY-005).

Independent reference for the kernel's gate in kernel/products/fans/common/feasibility.hpp; used
only by tests. Rules: docs/models/fans/feasibility.md. Decisions: ADR-003 D3 (limits) and ADR-011
(proposed: check order, the "range unsourced" status and the nearest-feasible-duty rule).

The gate runs before any geometry exists. It returns a report with one check per limit, in a fixed
order:
1. incompressible_pressure: Delta p_t <= EPSILON gamma p1. Nearest feasible duty: same flow,
   Delta p_t = EPSILON gamma p1.
2. incompressible_tip_speed: U = omega D / 2 <= sqrt(2 EPSILON) a1, when omega and D are given.
   A duty change cannot fix it, so there is no nearest feasible duty.
3. family_specific_speed_range: omega_s inside the family's cited range. A range without a
   citation is reported as "range unsourced", never as a pass or a failure. Nearest feasible duty:
   the duty closest in log space (ADR-011).
Invalid inputs return an out_of_validity Error instead of a report.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Literal

from cemkit.errors import Error, error, format_number
from cemkit.reference.fans.common import l0
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import Air

MODEL = "fans.feasibility@1.0.0"
UNSOURCED = "UNSOURCED"

# omega_s ~ Q^(1/2) Delta p^(-3/4): the exponents of the log-space rule (ADR-011). Exact in binary.
_FLOW_EXPONENT = 0.5
_PRESSURE_EXPONENT = 0.75
_NORM = _FLOW_EXPONENT**2 + _PRESSURE_EXPONENT**2  # 0.8125
# Last-ulp steps allowed to land the rounded nearest duty inside the range (ADR-011).
_MAX_NUDGES = 8

Status = Literal["pass", "violated", "range_unsourced", "not_computable"]
Verdict = Literal["infeasible", "unconfirmed", "confirmed"]


@dataclass(frozen=True)
class FamilyRange:
    """A family's feasible specific-speed range, from data/fans/family_ranges.yaml (SEL-002).
    source is the citation, or UNSOURCED; an unsourced range has no bounds."""

    family: str
    specific_speed_min: float | None
    specific_speed_max: float | None
    source: str

    @property
    def sourced(self) -> bool:
        return (
            self.source != UNSOURCED
            and self.source != ""
            and self.specific_speed_min is not None
            and self.specific_speed_max is not None
        )


@dataclass(frozen=True)
class Duty:
    flow: float  # m3/s
    pressure: FanTotalPressure


@dataclass(frozen=True)
class Check:
    limit: str
    status: Status
    message: str
    violation: Error | None = None
    # The nearest duty for this one limit, not checked against the others. Its numbers are
    # computed, so the kernel labels them L0 predicted, model MODEL.
    nearest_feasible: Duty | None = None


@dataclass(frozen=True)
class Report:
    checks: tuple[Check, ...]
    specific_speed: float | None  # L0 predicted, model fans.l0@1.0.0

    @property
    def verdict(self) -> Verdict:
        """infeasible if a check is violated; confirmed if every check passed; otherwise
        unconfirmed (a range_unsourced or not_computable check). An unconfirmed duty is never
        reported as feasible (ADR-011)."""
        if any(c.status == "violated" for c in self.checks):
            return "infeasible"
        if all(c.status == "pass" for c in self.checks):
            return "confirmed"
        return "unconfirmed"


def _range_error(family_range: FamilyRange) -> Error | None:
    lo, hi = family_range.specific_speed_min, family_range.specific_speed_max
    if lo is None or hi is None:
        return None
    if math.isfinite(lo) and math.isfinite(hi) and 0.0 < lo <= hi:
        return None
    return error(
        "invalid_input",
        "a family specific-speed range needs finite bounds with 0 < min <= max",
        "family_range",
        family=family_range.family,
        model=MODEL,
    )


def _inputs_error(duty: Duty, air: Air, omega: float | None, d_tip: float | None) -> Error | None:
    inputs = [("flow", duty.flow), ("fan_total_pressure", duty.pressure.pa)]
    if omega is not None:
        inputs.append(("omega", omega))
    if d_tip is not None:
        inputs.append(("d_tip", d_tip))
    return l0._positive(*inputs) or l0._air_valid(air)


def _within(value: float, lo: float, hi: float) -> bool:
    return lo <= value <= hi


def _nearest_in_range(
    duty: Duty, omega: float, air: Air, omega_s: float, lo: float, hi: float
) -> Duty | None:
    """The duty closest to the given one in (ln Q, ln Delta p) whose omega_s is the nearest
    range bound (ADR-011). None if rounding cannot land it inside, or it breaks the pressure
    limit."""
    target = hi if omega_s > hi else lo
    c = math.log(target / omega_s)
    flow = duty.flow * math.exp(c * _FLOW_EXPONENT / _NORM)
    pressure = duty.pressure.pa * math.exp(-c * _PRESSURE_EXPONENT / _NORM)
    for _ in range(_MAX_NUDGES + 1):
        value = l0.specific_speed(omega, flow, FanTotalPressure(pressure), air)
        if isinstance(value, Error):
            return None
        if _within(value, lo, hi):
            return Duty(flow, FanTotalPressure(pressure))
        # omega_s falls as the pressure rises: step the pressure by one ulp towards the range.
        pressure = math.nextafter(pressure, math.inf if value > hi else 0.0)
    return None


def check_feasibility(
    duty: Duty,
    air: Air,
    family_range: FamilyRange,
    omega: float | None = None,
    d_tip: float | None = None,
) -> Report | Error:
    """Runs the L0 feasibility checks for one duty (SEL-004)."""
    failure = _inputs_error(duty, air, omega, d_tip) or _range_error(family_range)
    if failure is not None:
        return failure

    checks: list[Check] = []

    pressure_failure = l0._pressure_valid(duty.pressure, air)
    if pressure_failure is None:
        checks.append(Check("incompressible_pressure", "pass", "within EPSILON gamma p1"))
    else:
        at_limit = Duty(duty.flow, FanTotalPressure(l0._max_pressure(air)))
        checks.append(
            Check(
                "incompressible_pressure",
                "violated",
                pressure_failure.message,
                pressure_failure,
                at_limit,
            )
        )

    if omega is None or d_tip is None:
        checks.append(
            Check(
                "incompressible_tip_speed",
                "not_computable",
                "needs the rotational speed and the rotor tip diameter",
            )
        )
    else:
        tip_failure = l0._tip_valid(omega, d_tip, air)
        if tip_failure is None:
            checks.append(Check("incompressible_tip_speed", "pass", "within sqrt(2 EPSILON) a1"))
        else:
            checks.append(
                Check("incompressible_tip_speed", "violated", tip_failure.message, tip_failure)
            )

    specific_speed: float | None = None
    if omega is not None and pressure_failure is None:
        value = l0.specific_speed(omega, duty.flow, duty.pressure, air)
        assert not isinstance(value, Error)  # inputs and pressure limit already checked
        specific_speed = value

    lo, hi = family_range.specific_speed_min, family_range.specific_speed_max
    if not family_range.sourced or lo is None or hi is None:
        checks.append(
            Check(
                "family_specific_speed_range",
                "range_unsourced",
                f"range unsourced: no cited specific-speed range for {family_range.family}",
            )
        )
    elif specific_speed is None or omega is None:
        reason = (
            "needs the rotational speed"
            if omega is None
            else "specific speed is out of validity at this pressure"
        )
        checks.append(Check("family_specific_speed_range", "not_computable", reason))
    elif _within(specific_speed, lo, hi):
        checks.append(Check("family_specific_speed_range", "pass", f"inside {family_range.source}"))
    else:
        violation = error(
            "infeasible_requirement",
            "specific speed is outside the family's feasible range",
            "specific_speed",
            value=format_number(specific_speed),
            bounds=f"[{format_number(lo)}, {format_number(hi)}]",
            family=family_range.family,
            model=MODEL,
        )
        nearest = _nearest_in_range(duty, omega, air, specific_speed, lo, hi)
        checks.append(
            Check(
                "family_specific_speed_range",
                "violated",
                violation.message,
                violation,
                nearest,
            )
        )

    return Report(tuple(checks), specific_speed)

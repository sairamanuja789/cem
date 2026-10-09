"""Reference implementation: L0 fan similarity (T06; PHY-001, PHY-002, PHY-005, SEL-001).

Formulas: docs/architecture.md section 6 (fan laws, flow and pressure coefficients, specific speed
and specific diameter). Decisions: docs/adr/003-requirement-semantics.md. Equations, conventions
and sources: docs/models/fans/l0-similarity.md. Independent reference for the kernel; used only by
tests.

Conventions:
- SI units throughout: m, m3/s, Pa, rad/s, kg/m3, W.
- D is the rotor tip diameter and U = omega D / 2 (ADR-003 D4). The duct diameter enters only
  fan_total_from_static.
- psi, omega_s and delta_s take fan total pressure only. Fan static pressure is a distinct type,
  so passing it is a type error (ADR-003 D1).

Assumptions:
- Geometrically similar fans in the same Reynolds-number regime (fan laws). This is recorded,
  not checked: L0 has no viscosity.
- Incompressible flow within the project tolerance EPSILON (ADR-003 D3).

Validity: every input is finite and strictly positive, Delta p_t / (gamma p1) <= EPSILON and
M_tip^2 / 2 <= EPSILON, each applied where its inputs are available. Outside these limits a
function returns an out_of_validity Error, never a number.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Literal

from cemkit.errors import Error
from cemkit.reference.platform.air import Air
from cemkit.reference.validity import require_at_most, require_positive

MODEL = "fans.l0@1.0.0"

# Project tolerance for the incompressibility criteria. A project decision recorded in ADR-003 D3,
# not a standard value.
EPSILON = 0.01


@dataclass(frozen=True)
class FanTotalPressure:
    """Fan total pressure p_t2 - p_t1 (ISO 5801), Pa."""

    pa: float


@dataclass(frozen=True)
class FanStaticPressure:
    """Fan static pressure p_s2 - p_t1 (ISO 5801), Pa."""

    pa: float


@dataclass(frozen=True)
class Derived[T]:
    """A value computed from other fields by a documented rule (provenance class "derived")."""

    value: T
    rule: str
    provenance: Literal["derived"] = "derived"


@dataclass(frozen=True)
class FanPoint:
    """One operating point of one fan, for the fan laws."""

    omega: float  # rad/s
    d_tip: float  # m
    flow: float  # m3/s
    pressure: FanTotalPressure
    power: float  # W, shaft power
    air: Air


# --- validity --------------------------------------------------------------------------------


def _positive(*inputs: tuple[str, float]) -> Error | None:
    for subject, value in inputs:
        failure = require_positive(subject, value, MODEL)
        if failure is not None:
            return failure
    return None


def _air_valid(air: Air) -> Error | None:
    return _positive(
        ("air.density", air.density),
        ("air.temperature", air.temperature),
        ("air.pressure", air.pressure),
        ("air.gamma", air.gamma),
        ("air.gas_constant", air.gas_constant),
    )


def _max_pressure(air: Air) -> float:
    # Operation order (EPSILON * gamma) * p1 fixes the exact bounds text; the kernel port must
    # use the same order (T08).
    return EPSILON * air.gamma * air.pressure


def _max_tip(air: Air) -> float:
    # sqrt(2 EPSILON) * sqrt(gamma R T1), in this order (T08 must match). Only called with air
    # that _air_valid accepted, so the square roots are of positive finite numbers.
    return math.sqrt(2.0 * EPSILON) * math.sqrt(air.gamma * air.gas_constant * air.temperature)


def max_fan_total_pressure(air: Air) -> float | Error:
    """EPSILON gamma p1: the largest fan total pressure inside the incompressible range."""
    failure = _air_valid(air)
    return failure if failure is not None else _max_pressure(air)


def max_tip_speed(air: Air) -> float | Error:
    """sqrt(2 EPSILON) a1: the largest blade tip speed with M_tip^2 / 2 <= EPSILON."""
    failure = _air_valid(air)
    return failure if failure is not None else _max_tip(air)


def _pressure_valid(pressure: FanTotalPressure, air: Air) -> Error | None:
    return require_at_most(
        "fan_total_pressure",
        pressure.pa,
        _max_pressure(air),
        MODEL,
        "fan total pressure exceeds the incompressible limit EPSILON gamma p1 (ADR-003 D3)",
    )


def _tip_valid(omega: float, d_tip: float, air: Air) -> Error | None:
    return require_at_most(
        "tip_speed",
        omega * d_tip / 2.0,
        _max_tip(air),
        MODEL,
        "blade tip-speed Mach number exceeds the incompressible limit M^2/2 <= EPSILON "
        "(ADR-003 D3)",
    )


def _first(*checks: Error | None) -> Error | None:
    return next((c for c in checks if c is not None), None)


# --- L0 models -------------------------------------------------------------------------------


def tip_speed(omega: float, d_tip: float) -> float | Error:
    """U = omega D / 2."""
    failure = _positive(("omega", omega), ("d_tip", d_tip))
    return failure if failure is not None else omega * d_tip / 2.0


def flow_coefficient(flow: float, d_tip: float, omega: float, air: Air) -> float | Error:
    """phi = Q / ((pi/4) D^2 U)."""
    failure = _first(
        _positive(("flow", flow), ("d_tip", d_tip), ("omega", omega)),
        _air_valid(air),
    )
    failure = failure or _tip_valid(omega, d_tip, air)
    if failure is not None:
        return failure
    u = omega * d_tip / 2.0
    return flow / (math.pi / 4.0 * d_tip**2 * u)


def pressure_coefficient(
    pressure: FanTotalPressure, d_tip: float, omega: float, air: Air
) -> float | Error:
    """psi = 2 Delta p_t / (rho U^2)."""
    failure = _first(
        _positive(("fan_total_pressure", pressure.pa), ("d_tip", d_tip), ("omega", omega)),
        _air_valid(air),
    )
    failure = failure or _first(_pressure_valid(pressure, air), _tip_valid(omega, d_tip, air))
    if failure is not None:
        return failure
    u = omega * d_tip / 2.0
    return 2.0 * pressure.pa / (air.density * u**2)


def specific_speed(
    omega: float, flow: float, pressure: FanTotalPressure, air: Air
) -> float | Error:
    """omega_s = omega sqrt(Q) / (Delta p_t / rho)^(3/4)."""
    failure = _first(
        _positive(("omega", omega), ("flow", flow), ("fan_total_pressure", pressure.pa)),
        _air_valid(air),
    )
    failure = failure or _pressure_valid(pressure, air)
    if failure is not None:
        return failure
    return omega * math.sqrt(flow) / math.pow(pressure.pa / air.density, 0.75)


def specific_diameter(
    d_tip: float, flow: float, pressure: FanTotalPressure, air: Air
) -> float | Error:
    """delta_s = D (Delta p_t / rho)^(1/4) / sqrt(Q)."""
    failure = _first(
        _positive(("d_tip", d_tip), ("flow", flow), ("fan_total_pressure", pressure.pa)),
        _air_valid(air),
    )
    failure = failure or _pressure_valid(pressure, air)
    if failure is not None:
        return failure
    return d_tip * math.pow(pressure.pa / air.density, 0.25) / math.sqrt(flow)


DYNAMIC_PRESSURE_RULE = (
    "ISO 5801: fan total = fan static + conventional fan dynamic pressure 1/2 rho (Q/A_out)^2, "
    "A_out = pi D_duct^2 / 4, Mach factor 1 within ADR-003 D3"
)


def fan_total_from_static(
    pressure: FanStaticPressure, flow: float, d_duct: float, air: Air
) -> Derived[FanTotalPressure] | Error:
    """Delta p_t = Delta p_s + 1/2 rho (Q / A_out)^2, A_out = pi D_duct^2 / 4 (ADR-003 D1).

    The result has provenance "derived". The pressure criterion of ADR-003 D3 is applied to the
    result; it also bounds the outlet Mach number, because the dynamic pressure is part of it.
    """
    failure = _first(
        _positive(("fan_static_pressure", pressure.pa), ("flow", flow), ("d_duct", d_duct)),
        _air_valid(air),
    )
    if failure is not None:
        return failure
    area = math.pi * d_duct**2 / 4.0
    total = FanTotalPressure(pressure.pa + 0.5 * air.density * (flow / area) ** 2)
    failure = _pressure_valid(total, air)
    if failure is not None:
        return failure
    return Derived(value=total, rule=DYNAMIC_PRESSURE_RULE)


def _point_valid(point: FanPoint) -> Error | None:
    return _first(
        _positive(
            ("omega", point.omega),
            ("d_tip", point.d_tip),
            ("flow", point.flow),
            ("fan_total_pressure", point.pressure.pa),
            ("power", point.power),
        ),
        _air_valid(point.air),
    ) or _first(
        _pressure_valid(point.pressure, point.air), _tip_valid(point.omega, point.d_tip, point.air)
    )


def scale_fan_laws(point: FanPoint, omega: float, d_tip: float, air: Air) -> FanPoint | Error:
    """Fan laws for a geometrically similar fan at the same Reynolds regime:
    Q ~ N D^3, Delta p ~ rho N^2 D^2, P ~ rho N^3 D^5.

    Both the given point and the scaled point must be inside the validity range.
    """
    failure = _point_valid(point) or _first(
        _positive(("omega_new", omega), ("d_tip_new", d_tip)), _air_valid(air)
    )
    if failure is not None:
        return failure
    n = omega / point.omega
    d = d_tip / point.d_tip
    r = air.density / point.air.density
    scaled = FanPoint(
        omega=omega,
        d_tip=d_tip,
        flow=point.flow * n * d**3,
        pressure=FanTotalPressure(point.pressure.pa * r * n**2 * d**2),
        power=point.power * r * n**3 * d**5,
        air=air,
    )
    failure = _point_valid(scaled)
    return failure if failure is not None else scaled

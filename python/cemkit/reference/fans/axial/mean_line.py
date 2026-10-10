"""Axial fan rotor mean-line velocity triangles, loss-free (L1; independent kernel reference).

Model fans.axial.mean_line@0.1.0. Equations, assumptions and sources:
docs/models/fans/axial-mean-line.md. The C++ kernel port must agree to floating-point tolerance
(PHY-005).

Fundamental law: Euler's turbomachinery equation (conservation of angular momentum through the
rotor) for incompressible flow at one radius, dp_t = rho U (c_theta2 - c_theta1). Source: Dixon,
S. L. and Hall, C. A., Fluid Mechanics and Thermodynamics of Turbomachinery, 7th ed., Butterworth-
Heinemann, 2014: chapter 1 (Euler work equation) and chapter 5 (axial-flow velocity diagrams).
Edition, chapter and equation numbers to be confirmed by the owner.

Assumptions (recorded, not checked):
- the input fan total pressure is taken as the rotor's ideal Euler total-pressure rise, which
  includes the outlet swirl dynamic pressure 1/2 rho c_theta2^2; for a rotor without a stator this
  overstates the delivered fan total pressure (ISO 5801 does not credit swirl);
- no inlet swirl (c_theta1 = 0);
- uniform axial velocity, the same at rotor inlet and outlet;
- one mean radius r_m = sqrt((r_tip^2 + r_hub^2) / 2), which splits the annulus into two equal
  areas (a definition, not data);
- loss-free: the swirl computed is the least that delivers dp_t. Real rotors need more swirl
  (a hydraulic efficiency below 1); no efficiency or loss value is sourced (UNSOURCED), so none is
  applied and every output is the ideal value.
Validity: every input finite and strictly positive, r_hub < r_tip, and the incompressibility
limits of ADR-003 D3 (dp_t <= EPSILON gamma p1; tip Mach M^2/2 <= EPSILON, U_tip = omega r_tip).
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from cemkit.errors import Error, error, format_number
from cemkit.reference.fans.common import l0
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import Air
from cemkit.reference.validity import require_at_most, require_positive

MODEL = "fans.axial.mean_line@0.1.0"


@dataclass(frozen=True)
class MeanLine:
    """Loss-free mean-line velocity triangles. Angles in rad from the axial direction."""

    annulus_area: float  # m2
    mean_radius: float  # m
    blade_speed: float  # U at r_m, m/s
    axial_velocity: float  # c_x, m/s
    outlet_swirl: float  # c_theta2, m/s
    relative_inlet_angle: float  # beta1
    relative_outlet_angle: float  # beta2
    absolute_outlet_angle: float  # alpha2
    relative_inlet_speed: float  # W1, m/s
    relative_outlet_speed: float  # W2, m/s
    flow_coefficient: float  # phi_m = c_x / U
    work_coefficient: float  # psi_E = c_theta2 / U = dp_t / (rho U^2)
    de_haller_ratio: float  # W2 / W1 (reported only: any limit on it is empirical, UNSOURCED)


def _positive(*inputs: tuple[str, float]) -> Error | None:
    for subject, value in inputs:
        failure = require_positive(subject, value, MODEL)
        if failure is not None:
            return failure
    return None


def mean_line(
    flow: float,
    pressure: FanTotalPressure,
    omega: float,
    r_tip: float,
    r_hub: float,
    air: Air,
) -> MeanLine | Error:
    """The rotor's loss-free mean-line velocity triangles for a duty (flow, fan total pressure)."""
    failure = _positive(
        ("flow", flow),
        ("fan_total_pressure", pressure.pa),
        ("omega", omega),
        ("r_tip", r_tip),
        ("r_hub", r_hub),
        ("air.density", air.density),
        ("air.temperature", air.temperature),
        ("air.pressure", air.pressure),
        ("air.gamma", air.gamma),
        ("air.gas_constant", air.gas_constant),
    )
    if failure is not None:
        return failure
    if not r_hub < r_tip:
        return error(
            "out_of_validity",
            "the hub radius must be less than the tip radius",
            "r_hub",
            value=format_number(r_hub),
            bounds=f"(0, {format_number(r_tip)})",
            model=MODEL,
        )
    limits = (
        require_at_most(
            "fan_total_pressure",
            pressure.pa,
            l0.EPSILON * air.gamma * air.pressure,
            MODEL,
            "fan total pressure exceeds the incompressible limit EPSILON gamma p1 (ADR-003 D3)",
        ),
        require_at_most(
            "tip_speed",
            omega * r_tip,
            math.sqrt(2.0 * l0.EPSILON) * math.sqrt(air.gamma * air.gas_constant * air.temperature),
            MODEL,
            "blade tip-speed Mach number exceeds the incompressible limit M^2/2 <= EPSILON "
            "(ADR-003 D3)",
        ),
    )
    failure = next((f for f in limits if f is not None), None)
    if failure is not None:
        return failure

    # Squares as x * x, never x**2, so that the kernel port can match bit for bit (l0.py note).
    area = math.pi * ((r_tip * r_tip) - (r_hub * r_hub))
    r_m = math.sqrt(((r_tip * r_tip) + (r_hub * r_hub)) / 2.0)
    u = omega * r_m
    c_x = flow / area
    c_theta2 = pressure.pa / (air.density * u)  # Euler, c_theta1 = 0, loss-free
    w_theta2 = u - c_theta2
    w1 = math.sqrt((c_x * c_x) + (u * u))
    w2 = math.sqrt((c_x * c_x) + (w_theta2 * w_theta2))
    return MeanLine(
        annulus_area=area,
        mean_radius=r_m,
        blade_speed=u,
        axial_velocity=c_x,
        outlet_swirl=c_theta2,
        relative_inlet_angle=math.atan2(u, c_x),
        relative_outlet_angle=math.atan2(w_theta2, c_x),
        absolute_outlet_angle=math.atan2(c_theta2, c_x),
        relative_inlet_speed=w1,
        relative_outlet_speed=w2,
        flow_coefficient=c_x / u,
        work_coefficient=c_theta2 / u,
        de_haller_ratio=w2 / w1,
    )

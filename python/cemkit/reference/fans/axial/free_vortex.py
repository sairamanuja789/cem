"""Free-vortex blade design of an axial rotor, loss-free (L1; independent kernel reference).

Model fans.axial.free_vortex@0.1.0. Equations, assumptions, sign conventions and sources:
docs/models/fans/axial-free-vortex.md. The C++ kernel port must agree to floating-point tolerance
(PHY-005).

At each blade station of radius r, Euler's equation with no inlet swirl and the same total-pressure
rise at every radius gives the free-vortex swirl c_theta2 = dp_t / (rho omega r), so r c_theta2 is
constant (Dixon and Hall, 7th ed., ch. 6, free-vortex flow; to be confirmed by the owner). The
velocity triangles give the relative flow angles beta1 and beta2 (from the axial direction). A
circular-arc camber line whose end tangents follow those angles (zero incidence, zero deviation:
no deviation rule is sourced, UNSOURCED) has camber angle theta = beta1 - beta2 and stagger
xi = (beta1 + beta2) / 2; its height above the chord is h = (c / 2) tan(theta / 4) (geometry of a
circular arc). Degree of reaction R = 1 - c_theta2 / (2 U) (c_theta1 = 0, constant c_x).

Geometry recipe convention (geometry.hpp): flow along +z, rotation counter-clockwise viewed from
the inlet (ADR-016), so the recipe's stagger is -xi and the camber is positive. The embedded root
station inside the hub repeats the hub section.

Validity: that of fans.axial.mean_line (mean_line.py), plus strictly positive chords, thickness and
embed depth, 3 <= stations, embed_depth < r_hub, and thickness < chord at every station.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from cemkit.errors import Error, error, format_number
from cemkit.reference.fans.axial.mean_line import MeanLine, mean_line
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import Air
from cemkit.reference.validity import require_positive

MODEL = "fans.axial.free_vortex@0.1.0"


@dataclass(frozen=True)
class Station:
    """One blade station: loss-free velocity triangle and circular-arc section (SI, rad)."""

    radius: float
    blade_speed: float  # U = omega r
    outlet_swirl: float  # c_theta2 = dp_t / (rho omega r)
    relative_inlet_angle: float  # beta1
    relative_outlet_angle: float  # beta2
    camber_angle: float  # theta = beta1 - beta2
    stagger_from_axis: float  # xi = (beta1 + beta2) / 2
    recipe_stagger: float  # -xi (geometry recipe convention)
    chord: float
    camber: float  # h = (c / 2) tan(theta / 4)
    thickness: float
    de_haller_ratio: float  # W2 / W1 (reported only: a limit on it is empirical, UNSOURCED)
    reaction: float  # R = 1 - c_theta2 / (2 U)
    embedded: bool  # the root station inside the hub (repeats the hub section)
    outlet_axial_velocity: float  # c_x2 (= c_x for the free vortex)


@dataclass(frozen=True)
class FreeVortexDesign:
    mean_line: MeanLine
    axial_velocity: float
    stations: tuple[Station, ...]  # root (embedded) first, tip last


def _section(
    r: float,
    omega: float,
    c_x: float,
    pressure: float,
    density: float,
    chord: float,
    thickness: float,
) -> tuple[float, ...]:
    u = omega * r
    c_theta2 = pressure / (density * omega * r)
    w_theta2 = u - c_theta2
    beta1 = math.atan2(u, c_x)
    beta2 = math.atan2(w_theta2, c_x)
    theta = beta1 - beta2
    xi = (beta1 + beta2) / 2.0
    w1 = math.sqrt((c_x * c_x) + (u * u))
    w2 = math.sqrt((c_x * c_x) + (w_theta2 * w_theta2))
    camber = (chord / 2.0) * math.tan(theta / 4.0)
    reaction = 1.0 - (c_theta2 / (2.0 * u))
    return (u, c_theta2, beta1, beta2, theta, xi, camber, w2 / w1, reaction, chord, thickness)


def check_sections(
    chord_hub: float,
    chord_tip: float,
    thickness: float,
    stations: int,
    embed_depth: float,
    r_hub: float,
    model: str,
) -> Error | None:
    """The section validity checks shared by the free and general vortex models (fixed order,
    PHY-005)."""
    for subject, value in (
        ("chord_hub", chord_hub),
        ("chord_tip", chord_tip),
        ("thickness", thickness),
        ("embed_depth", embed_depth),
    ):
        failure = require_positive(subject, value, model)
        if failure is not None:
            return failure
    if stations < 3:
        return error(
            "out_of_validity",
            "at least 3 blade stations are needed",
            "stations",
            value=str(stations),
            bounds="[3, inf)",
            model=model,
        )
    if not embed_depth < r_hub:
        return error(
            "out_of_validity",
            "the embed depth must be less than the hub radius",
            "embed_depth",
            value=format_number(embed_depth),
            bounds=f"(0, {format_number(r_hub)})",
            model=model,
        )
    if not (thickness < chord_hub and thickness < chord_tip):
        return error(
            "out_of_validity",
            "the blade thickness must be less than every chord",
            "thickness",
            value=format_number(thickness),
            bounds=f"(0, {format_number(min(chord_hub, chord_tip))})",
            model=model,
        )
    return None


def free_vortex_design(
    flow: float,
    pressure: FanTotalPressure,
    omega: float,
    r_tip: float,
    r_hub: float,
    air: Air,
    chord_hub: float,
    chord_tip: float,
    thickness: float,
    stations: int,
    embed_depth: float,
) -> FreeVortexDesign | Error:
    """Loss-free free-vortex blade sections from hub to tip, plus an embedded root station."""
    line = mean_line(flow, pressure, omega, r_tip, r_hub, air)
    if isinstance(line, Error):
        return line
    failure = check_sections(chord_hub, chord_tip, thickness, stations, embed_depth, r_hub, MODEL)
    if failure is not None:
        return failure

    c_x = line.axial_velocity
    out: list[Station] = []
    for i in range(stations):
        f = i / (stations - 1)
        r = r_hub + (f * (r_tip - r_hub))
        chord = chord_hub + (f * (chord_tip - chord_hub))
        s = _section(r, omega, c_x, pressure.pa, air.density, chord, thickness)
        out.append(
            Station(
                radius=r,
                blade_speed=s[0],
                outlet_swirl=s[1],
                relative_inlet_angle=s[2],
                relative_outlet_angle=s[3],
                camber_angle=s[4],
                stagger_from_axis=s[5],
                recipe_stagger=-s[5],
                chord=chord,
                camber=s[6],
                thickness=thickness,
                de_haller_ratio=s[7],
                reaction=s[8],
                embedded=False,
                outlet_axial_velocity=c_x,
            )
        )
    hub = out[0]
    root = Station(**{**hub.__dict__, "radius": r_hub - embed_depth, "embedded": True})
    return FreeVortexDesign(mean_line=line, axial_velocity=c_x, stations=(root, *out))

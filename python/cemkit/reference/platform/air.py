"""Reference implementation: air properties for the L0 models (ADR-003 D2; PHY-005).

Independent Python reference for the kernel's platform physics. Used only by tests; production
physics lives in the C++ kernel. Sources and conventions: docs/models/platform/air-properties.md.

Assumptions: air is a calorically perfect ideal gas; no viscosity at L0.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from cemkit.errors import Error
from cemkit.reference.validity import require_positive

MODEL = "platform.air@1.0.0"

# U.S. Standard Atmosphere, 1976 (NOAA, NASA, USAF; NOAA-S/T 76-1562; NTRS 19770009539).
# Page references: docs/models/platform/air-properties.md.
UNIVERSAL_GAS_CONSTANT = 8.31432e3  # R*, J/(kmol K); p. 3 (Table 2 A on p. 2 misprints 10^-3)
MOLAR_MASS_AIR = 28.9644  # M0, kg/kmol, sea-level mean molar mass; section 1.2.4, eq. (21), p. 9
SEA_LEVEL_PRESSURE = 101325.0  # P0, Pa; Table 2 B, p. 2
GAMMA = 1.40  # ratio of specific heats of air; Table 2 B, p. 2, and p. 4
GAS_CONSTANT_AIR = UNIVERSAL_GAS_CONSTANT / MOLAR_MASS_AIR  # R = R*/M0, J/(kg K); never rounded

# Requirements AX-005 (the spec's own defaults, provenance "default").
DEFAULT_DENSITY = 1.18  # kg/m3
DEFAULT_TEMPERATURE = 298.15  # K


@dataclass(frozen=True)
class Air:
    """The air state the L0 models use: density for the similarity formulas; pressure,
    temperature, gamma and gas constant for the incompressibility check (ADR-003 D3)."""

    density: float  # kg/m3
    temperature: float  # K, T1
    pressure: float  # Pa, p1
    gamma: float
    gas_constant: float  # J/(kg K)


def default_air() -> Air:
    """AX-005 defaults with the U.S. Standard Atmosphere 1976 constants."""
    return Air(
        density=DEFAULT_DENSITY,
        temperature=DEFAULT_TEMPERATURE,
        pressure=SEA_LEVEL_PRESSURE,
        gamma=GAMMA,
        gas_constant=GAS_CONSTANT_AIR,
    )


def ideal_gas_density(
    pressure: float, temperature: float, gas_constant: float = GAS_CONSTANT_AIR
) -> float | Error:
    """rho = p / (R T). Valid for finite, strictly positive inputs."""
    for subject, value in (
        ("pressure", pressure),
        ("temperature", temperature),
        ("gas_constant", gas_constant),
    ):
        failure = require_positive(subject, value, MODEL)
        if failure is not None:
            return failure
    return pressure / (gas_constant * temperature)


def speed_of_sound(air: Air) -> float | Error:
    """a = sqrt(gamma R T), the ideal-gas speed of sound at the air's temperature."""
    for subject, value in (
        ("gamma", air.gamma),
        ("gas_constant", air.gas_constant),
        ("temperature", air.temperature),
    ):
        failure = require_positive(subject, value, MODEL)
        if failure is not None:
            return failure
    return math.sqrt(air.gamma * air.gas_constant * air.temperature)

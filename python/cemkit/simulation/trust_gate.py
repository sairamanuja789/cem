"""Result-trust gate (TRUST-001, TRUST-003, TRUST-004).

Each simulation result gets exactly one status: VERIFIED, CONVERGED, UNCONVERGED, MESH_INVALID,
RESOURCE_LIMIT or SOLVER_FAILED. Only CONVERGED and VERIFIED may be ranked or reported as numbers;
only VERIFIED may be labelled L2 verified (TRUST-002: a three-level mesh study, mesh_study.py).
Thresholds come from the versioned case template (TRUST-004: per quantity and per template), never
from here; every failed check is recorded with its value and threshold (TRUST-003).
"""

from __future__ import annotations

import math
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field
from typing import Literal

Status = Literal[
    "VERIFIED", "CONVERGED", "UNCONVERGED", "MESH_INVALID", "RESOURCE_LIMIT", "SOLVER_FAILED"
]
RANKABLE: frozenset[str] = frozenset({"CONVERGED", "VERIFIED"})


@dataclass(frozen=True)
class Thresholds:
    """From the case template: residuals (final initial residual per field), the inlet-outlet
    mass imbalance (fraction) and the monitor flatness (relative range over the last
    `flat_fraction` of iterations)."""

    residuals: Mapping[str, float]
    mass_imbalance: float
    flat_fraction: float
    flatness: float
    monitor_scales: Mapping[str, float] = field(default_factory=dict)


@dataclass(frozen=True)
class Check:
    name: str
    passed: bool
    value: float | None
    threshold: float | None
    message: str


@dataclass(frozen=True)
class Assessment:
    status: Status
    checks: tuple[Check, ...] = field(default_factory=tuple)

    @property
    def failed(self) -> tuple[Check, ...]:
        return tuple(c for c in self.checks if not c.passed)


def relative_range(values: Sequence[float], fraction: float, scale: float = 0.0) -> float:
    """(max - min) / max(|mean|, scale) over the last `fraction` of the values (inf when both
    are 0). The scale keeps a monitor whose mean is near zero from looking unsteady."""
    n = max(2, math.ceil(len(values) * fraction))
    tail = values[-n:]
    mean = sum(tail) / len(tail)
    spread = max(tail) - min(tail)
    denominator = max(abs(mean), scale)
    return math.inf if denominator == 0.0 else spread / denominator


def assess(
    *,
    solver_failed: bool,
    resource_limit: str | None,
    mesh_ok: bool,
    residuals: Mapping[str, float],
    inflow: float,
    outflow: float,
    monitors: Mapping[str, Sequence[float]],
    thresholds: Thresholds,
) -> Assessment:
    """The status of one run. The order is fixed: resource limit, solver failure, mesh, then the
    convergence checks (all recorded)."""
    if resource_limit is not None:
        return Assessment(
            "RESOURCE_LIMIT",
            (Check("resource_limit", False, None, None, f"limit exceeded: {resource_limit}"),),
        )
    if solver_failed:
        return Assessment(
            "SOLVER_FAILED", (Check("solver", False, None, None, "a solver step failed"),)
        )
    if not mesh_ok:
        return Assessment(
            "MESH_INVALID", (Check("mesh_quality", False, None, None, "checkMesh did not pass"),)
        )
    checks: list[Check] = []
    for name, limit in sorted(thresholds.residuals.items()):
        value = residuals.get(name)
        ok = value is not None and math.isfinite(value) and value <= limit
        checks.append(Check(f"residual_{name}", ok, value, limit, "final initial residual"))
    imbalance = abs(inflow - outflow) / abs(inflow) if inflow != 0.0 else math.inf
    checks.append(
        Check(
            "mass_imbalance",
            imbalance <= thresholds.mass_imbalance,
            imbalance,
            thresholds.mass_imbalance,
            "|Q_in - Q_out| / |Q_in|",
        )
    )
    for name, values in sorted(monitors.items()):
        scale = thresholds.monitor_scales.get(name, 0.0)
        spread = (
            relative_range(values, thresholds.flat_fraction, scale)
            if len(values) >= 2
            else math.inf
        )
        checks.append(
            Check(
                f"flat_{name}",
                spread <= thresholds.flatness,
                spread,
                thresholds.flatness,
                f"relative range over the last {thresholds.flat_fraction:.0%} of iterations",
            )
        )
    status: Status = "CONVERGED" if all(c.passed for c in checks) else "UNCONVERGED"
    return Assessment(status, tuple(checks))


def verified(statuses: Sequence[Status], gci: Sequence[float | None], limit: float) -> bool:
    """TRUST-002: all three mesh levels CONVERGED and every studied quantity's GCI below `limit`."""
    return (
        len(statuses) == 3
        and all(s == "CONVERGED" for s in statuses)
        and len(gci) > 0
        and all(g is not None and g < limit for g in gci)
    )

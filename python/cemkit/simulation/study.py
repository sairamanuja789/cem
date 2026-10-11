"""Runs solver cases the kernel wrote and assesses each one (SIM-002, TRUST-001 to TRUST-004).

Product-neutral: a case is the kernel's {"files", "binary_files", "steps"} and the template's
"thresholds"; the monitors read here are the rotor-sector template's (pressures, flows, moment,
residuals). The CLI runs one case at a time with MPI ranks inside the case (ADR-017 decision 3:
the runner pins each child to the first N CPUs, so concurrent cases would share CPUs).
"""

from __future__ import annotations

from collections.abc import Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cemkit.orchestration.limits import ResourceLimits
from cemkit.simulation import trust_gate
from cemkit.simulation.runners import openfoam

Json = Any


@dataclass(frozen=True)
class CaseResult:
    name: str
    directory: Path
    assessment: trust_gate.Assessment
    finals: dict[str, float]  # last value of each monitor (kinematic pressures, one passage)
    cells: int | None
    wall_seconds: float
    peak_rss_bytes: int


def thresholds_of(t: Json) -> trust_gate.Thresholds:
    return trust_gate.Thresholds(
        residuals=dict(t["residuals"]),
        mass_imbalance=float(t["mass_imbalance"]),
        flat_fraction=float(t["flat_fraction"]),
        flatness=float(t["flatness"]),
        monitor_scales={k: float(v) for k, v in t.get("monitor_scales", {}).items()},
    )


def run_case(
    name: str,
    directory: Path,
    case: Json,
    thresholds: trust_gate.Thresholds,
    limits: ResourceLimits,
) -> CaseResult:
    openfoam.write_case(directory, case)
    steps = openfoam.run_steps(directory, case["steps"], limits)
    last = steps[-1].outcome
    wall = sum(s.outcome.wall_seconds for s in steps)
    peak = max(s.outcome.peak_rss_bytes for s in steps)
    if last.exceeded is not None:
        a = trust_gate.assess(
            solver_failed=False,
            resource_limit=last.exceeded,
            mesh_ok=False,
            residuals={},
            inflow=1.0,
            outflow=1.0,
            monitors={},
            thresholds=thresholds,
        )
        return CaseResult(name, directory, a, {}, openfoam.cell_count(directory), wall, peak)
    failed = last.returncode != 0 or len(steps) < len(case["steps"])
    mesh_ok = openfoam.mesh_ok(directory)
    finals: dict[str, float] = {}
    monitors: dict[str, list[float]] = {}
    residuals: dict[str, float] = {}
    inflow = outflow = 0.0
    if not failed and mesh_ok:
        try:
            flows_in = openfoam.series(directory, "inletFlow")
            flows_out = openfoam.series(directory, "outletFlow")
            p_in = openfoam.series(directory, "pIn")
            p_out = openfoam.series(directory, "pOut")
            pt_in = openfoam.series(directory, "ptIn")
            pt_out = openfoam.series(directory, "ptOut")
            moment = openfoam.series(directory, "forces", 3, "moment.dat")
            residuals = openfoam.final_initial_residuals(directory)
        except (OSError, ValueError, IndexError, StopIteration):
            failed = True
        else:
            inflow, outflow = -flows_in[-1], flows_out[-1]
            rise = [o - i for o, i in zip(pt_out, pt_in, strict=False)]
            monitors = {"moment_z": moment, "total_pressure_rise": rise}
            finals = {
                "inlet_flow": inflow,
                "outlet_flow": outflow,
                "p_in": p_in[-1],
                "p_out": p_out[-1],
                "pt_in": pt_in[-1],
                "pt_out": pt_out[-1],
                "moment_z": moment[-1],
            }
    a = trust_gate.assess(
        solver_failed=failed,
        resource_limit=None,
        mesh_ok=mesh_ok,
        mesh_failures=() if mesh_ok else openfoam.mesh_failures(directory),
        residuals=residuals,
        inflow=inflow,
        outflow=outflow,
        monitors=monitors,
        thresholds=thresholds,
    )
    return CaseResult(name, directory, a, finals, openfoam.cell_count(directory), wall, peak)


def run_cases(
    jobs: Sequence[tuple[str, Path, Json]],
    thresholds: trust_gate.Thresholds,
    limits: ResourceLimits,
    concurrent: int,
) -> list[CaseResult]:
    """Runs (name, directory, case) jobs, `concurrent` at a time; results in job order."""
    with ThreadPoolExecutor(max_workers=max(1, concurrent)) as pool:
        futures = [pool.submit(run_case, n, d, c, thresholds, limits) for n, d, c in jobs]
        return [f.result() for f in futures]

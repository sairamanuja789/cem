"""OpenFOAM runner: runs a written case under resource limits (SIM-002) and reads its monitors.

The kernel writes the case (kernel/cemkit/simulation/openfoam, SIM-001); this module only writes
those files to a directory, runs the listed commands one by one through
cemkit.orchestration.runner.run_limited inside the OpenFOAM environment, and parses the function
object outputs (postProcessing/<name>/0/*.dat). It decides nothing about trust (trust_gate.py).
"""

from __future__ import annotations

import base64
import os
import shutil
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cemkit.orchestration.limits import ResourceLimits
from cemkit.orchestration.runner import ProcessOutcome, run_limited

Json = Any
OPENFOAM = "openfoam2512"  # docker/pins.env OPENFOAM_VERSION (ADR-006)
WRAPPER_ENV = "CEMKIT_OPENFOAM_WRAPPER"  # tests only: a stand-in for the OpenFOAM environment


@dataclass(frozen=True)
class StepOutcome:
    name: str
    outcome: ProcessOutcome
    log: Path


def write_case(directory: Path, case: Json) -> None:
    """Writes the kernel's case files ({"files": {path: text}, "binary_files": {path: base64}})."""
    if directory.exists():
        shutil.rmtree(directory)
    for rel, text in case["files"].items():
        path = _inside(directory, rel)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    for rel, data in case.get("binary_files", {}).items():
        path = _inside(directory, rel)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(base64.b64decode(data))


def _inside(directory: Path, rel: str) -> Path:
    path = (directory / rel).resolve()
    if directory.resolve() not in path.parents:
        raise ValueError(f"case file outside the case directory: {rel!r}")
    return path


def run_steps(directory: Path, steps: Sequence[Json], limits: ResourceLimits) -> list[StepOutcome]:
    """Runs each step ({"name", "argv"}) in the OpenFOAM environment; stops at the first failure."""
    outcomes: list[StepOutcome] = []
    for step in steps:
        log = directory / f"log.{step['name']}"
        command = " ".join(_quote(a) for a in step["argv"])
        outcome = run_limited(
            [os.environ.get(WRAPPER_ENV, OPENFOAM), "-c", command],
            limits,
            cwd=directory,
            stdout=log,
            stderr=directory / f"log.{step['name']}.err",
        )
        outcomes.append(StepOutcome(step["name"], outcome, log))
        if outcome.exceeded is not None or outcome.returncode != 0:
            break
    return outcomes


def _quote(arg: str) -> str:
    if arg and all(c.isalnum() or c in "-_./=:" for c in arg):
        return arg
    return "'" + arg.replace("'", "'\"'\"'") + "'"


def read_dat(path: Path) -> tuple[list[str], list[list[str]]]:
    """Columns (from the last comment line) and rows of a function-object .dat file."""
    columns: list[str] = []
    rows: list[list[str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            fields = line[1:].split()
            if fields and fields[0] == "Time":
                columns = fields
        elif line.strip():
            rows.append(line.split())
    return columns, rows


def series(case: Path, function: str, column: int = 1, file: str | None = None) -> list[float]:
    """One numeric column of a function object's output, one value per iteration."""
    folder = case / "postProcessing" / function / "0"
    path = folder / file if file else next(iter(sorted(folder.glob("*.dat"))))
    _, rows = read_dat(path)
    return [float(r[column]) for r in rows]


def final_initial_residuals(case: Path) -> dict[str, float]:
    """The initial residual of each solved field at the last iteration (solverInfo)."""
    columns, rows = read_dat(case / "postProcessing" / "residuals" / "0" / "solverInfo.dat")
    last = rows[-1]
    return {
        name.removesuffix("_initial"): float(last[i])
        for i, name in enumerate(columns)
        if name.endswith("_initial")
    }


def mesh_ok(case: Path) -> bool:
    log = case / "log.checkMesh"
    return log.exists() and "Mesh OK." in log.read_text(encoding="utf-8", errors="replace")


def cell_count(case: Path) -> int | None:
    log = case / "log.checkMesh"
    if not log.exists():
        return None
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip().startswith("cells:"):
            return int(line.split()[1])
    return None

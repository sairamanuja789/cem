"""Fake child processes and job handlers for the orchestration tests (REL-003: fakes in CI).

Run as a script, this file is the child process: `python fake_job.py <mode> [args...]`.
Imported, it provides FakeHandler, which turns a job payload into a command for one of the modes.

Modes:
  ok <out> <seconds> <log> <tag>   append <tag> to <log>, sleep, write <out>/result.txt
  flaky <out> <marker>            exit 2 the first time (creates <marker>), succeed the second time
  exit <code> <message>           print <message> to stderr and exit with <code>
  alloc <step_mb> <max_mb>        allocate and touch memory step by step (for the memory limit)
  sleep <seconds>                 sleep (for the wall-time limit)
  oom_kernel <events>             add 1 to oom_kill in a fake memory.events, then SIGKILL itself
  killed                          SIGKILL itself (killed from outside, not by the runner)
  affinity <out>                  write the number of CPUs it may run on to <out>/cpus.txt
"""

from __future__ import annotations

import os
import signal
import sys
import time
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from cemkit.orchestration import Command, JobFailure, ProcessOutcome, ResourceLimits

FAKE = Path(__file__).resolve()
MIB = 1024 * 1024


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main(argv: list[str]) -> int:
    mode, args = argv[1], argv[2:]
    if mode == "ok":
        out, seconds, log, tag = Path(args[0]), float(args[1]), Path(args[2]), args[3]
        with log.open("a", encoding="utf-8") as stream:
            stream.write(tag + "\n")
        time.sleep(seconds)
        _write(out / "result.txt", f"ok {tag}\n")
        print(f"done {tag}")
        return 0
    if mode == "flaky":
        out, marker = Path(args[0]), Path(args[1])
        if not marker.exists():
            marker.write_text("failed once\n", encoding="utf-8")
            print("residuals diverged", file=sys.stderr)
            return 2
        _write(out / "result.txt", "ok\n")
        return 0
    if mode == "exit":
        print(args[1], file=sys.stderr)
        return int(args[0])
    if mode == "alloc":
        step, limit = int(args[0]) * MIB, int(args[1]) * MIB
        held: list[bytes] = []
        while len(held) * step < limit:
            held.append(b"x" * step)
            time.sleep(0.02)
        return 0
    if mode == "sleep":
        time.sleep(float(args[0]))
        return 0
    if mode == "oom_kernel":
        events = Path(args[0])
        lines = events.read_text(encoding="utf-8").splitlines()
        bumped = [
            f"oom_kill {int(line.split()[1]) + 1}" if line.startswith("oom_kill ") else line
            for line in lines
        ]
        events.write_text("\n".join(bumped) + "\n", encoding="utf-8")
        os.kill(os.getpid(), signal.SIGKILL)
    if mode == "killed":
        os.kill(os.getpid(), signal.SIGKILL)
    if mode == "affinity":
        _write(Path(args[0]) / "cpus.txt", str(len(os.sched_getaffinity(0))))
        return 0
    raise SystemExit(f"unknown mode {mode}")


class FakeHandler:
    """Runs fake_job.py; a non-zero exit is classified with this handler's failure code."""

    def __init__(self, kind: str, failure_code: str) -> None:
        self.kind = kind
        self._code = failure_code

    def command(self, payload: Mapping[str, Any], workdir: Path) -> Command:
        out = str(workdir / "out")
        args = [out if a == "{out}" else str(a) for a in payload.get("args", [])]
        limits = ResourceLimits(
            cores=int(payload.get("cores", 1)),
            wall_time_s=float(payload.get("wall_s", 60.0)),
            memory_bytes=int(payload.get("memory_mb", 512)) * MIB,
        )
        return Command((sys.executable, str(FAKE), str(payload["mode"]), *args), limits)

    def check(self, outcome: ProcessOutcome, workdir: Path) -> JobFailure | None:
        if outcome.returncode == 0:
            return None
        stderr = (workdir / "stderr.log").read_text(encoding="utf-8").strip()
        return JobFailure(self._code, f"{self.kind} exited with {outcome.returncode}: {stderr}")


def handlers() -> list[FakeHandler]:
    return [
        FakeHandler("fake_mesh", "mesh_failed"),
        FakeHandler("fake_solver", "sim_untrusted"),
    ]


if __name__ == "__main__":
    sys.exit(main(sys.argv))

"""Runs one external process under memory, core and wall-time limits (SIM-002, RES-001, OBS-001).

This is the only place in cemkit that starts processes (python/CLAUDE.md). How the limits are
enforced, and why, is ADR-005:

- cores:     the child is pinned to the first N CPUs this process may use (sched_setaffinity);
             threads and children it starts inherit the set.
- memory:    the runner polls the resident memory of the child's whole process group (it starts
             its own session) and kills the group when the sum exceeds the limit. If the container
             has a cgroup memory limit, a kernel OOM kill is also recognised, from memory.events.
- wall time: the group is killed when the limit has passed.
- the child gets PR_SET_PDEATHSIG = SIGKILL, so it dies with the worker instead of running on
  unsupervised after a worker crash.

Peak memory is the larger of the polled group peak and the kernel's ru_maxrss for the child.
"""

from __future__ import annotations

import contextlib
import ctypes
import os
import signal
import subprocess
import time
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from functools import partial
from pathlib import Path
from typing import Final

from cemkit.orchestration.limits import ResourceLimits

DEFAULT_OOM_EVENTS: Final = Path("/sys/fs/cgroup/memory.events")
_PAGE_BYTES: Final = os.sysconf("SC_PAGE_SIZE")
_PR_SET_PDEATHSIG: Final = 1
_LIBC: Final = ctypes.CDLL(None, use_errno=True)

MEMORY: Final = "memory"
KERNEL_OOM: Final = "kernel_oom"
WALL_TIME: Final = "wall_time"


@dataclass(frozen=True, slots=True)
class ProcessOutcome:
    returncode: int | None
    """Exit status if the child exited; None if a signal ended it."""
    signal: int | None
    exceeded: str | None
    """MEMORY, KERNEL_OOM or WALL_TIME if a limit ended the child, else None."""
    peak_rss_bytes: int
    cpu_seconds: float
    wall_seconds: float


def pick_cpus(cores: int) -> frozenset[int]:
    available = sorted(os.sched_getaffinity(0))
    if cores > len(available):
        raise ValueError(f"{cores} cores requested but only {len(available)} are available")
    return frozenset(available[:cores])


def _child_setup(cpus: frozenset[int], parent: int) -> None:  # pragma: no cover - runs in child
    os.sched_setaffinity(0, cpus)
    _LIBC.prctl(_PR_SET_PDEATHSIG, signal.SIGKILL, 0, 0, 0)
    if os.getppid() != parent:  # the parent died before prctl took effect
        os._exit(1)


def _group_rss(group: int) -> int:
    """Resident memory of every process in a process group, in bytes."""
    total = 0
    for entry in os.scandir("/proc"):
        if not entry.name.isdigit():
            continue
        try:
            text = Path("/proc", entry.name, "stat").read_bytes()
        except OSError:
            continue
        fields = text[text.rindex(b")") + 2 :].split()
        if int(fields[2]) == group:  # field 5, pgrp
            total += int(fields[21]) * _PAGE_BYTES  # field 24, rss in pages
    return total


def _oom_kills(path: Path | None) -> int | None:
    if path is None:
        return None
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    for line in lines:
        name, _, value = line.partition(" ")
        if name == "oom_kill":
            return int(value)
    return None


def _kill_group(group: int) -> None:
    with contextlib.suppress(ProcessLookupError):
        os.killpg(group, signal.SIGKILL)


def run_limited(
    argv: Sequence[str],
    limits: ResourceLimits,
    *,
    cwd: Path,
    stdout: Path,
    stderr: Path,
    env: Mapping[str, str] | None = None,
    on_tick: Callable[[], None] | None = None,
    tick_seconds: float = 1.0,
    poll_seconds: float = 0.02,
    oom_events: Path | None = DEFAULT_OOM_EVENTS,
) -> ProcessOutcome:
    """Run `argv` to completion under `limits`; stdout and stderr go to files.

    `on_tick` is called about every `tick_seconds` while the child runs (the worker renews its
    lease there). If it raises, the child's process group is killed and the exception propagates.
    """
    cpus = pick_cpus(limits.cores)
    oom_before = _oom_kills(oom_events)
    started = time.monotonic()
    with stdout.open("wb") as out, stderr.open("wb") as err:
        process = subprocess.Popen(
            list(argv),
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=out,
            stderr=err,
            env=None if env is None else {**os.environ, **env},
            start_new_session=True,
            preexec_fn=partial(_child_setup, cpus, os.getpid()),
        )
    pid = process.pid
    peak = 0
    exceeded: str | None = None
    next_tick = started + tick_seconds
    try:
        while True:
            reaped, status, usage = os.wait4(pid, os.WNOHANG)
            if reaped == pid:
                break
            peak = max(peak, _group_rss(pid))
            now = time.monotonic()
            if exceeded is None:
                if peak > limits.memory_bytes:
                    exceeded = MEMORY
                elif now - started > limits.wall_time_s:
                    exceeded = WALL_TIME
                if exceeded is not None:
                    _kill_group(pid)
            if on_tick is not None and now >= next_tick:
                next_tick = now + tick_seconds
                on_tick()
            time.sleep(poll_seconds)
    except BaseException:
        _kill_group(pid)
        _, status, _ = os.wait4(pid, 0)
        process.returncode = os.waitstatus_to_exitcode(status)
        raise
    wall = time.monotonic() - started
    process.returncode = os.waitstatus_to_exitcode(status)
    _kill_group(pid)  # anything the child left behind in its group
    signal_number = os.WTERMSIG(status) if os.WIFSIGNALED(status) else None
    returncode = os.WEXITSTATUS(status) if os.WIFEXITED(status) else None
    if exceeded is None and signal_number == signal.SIGKILL and oom_before is not None:
        oom_after = _oom_kills(oom_events)
        if oom_after is not None and oom_after > oom_before:
            exceeded = KERNEL_OOM
    return ProcessOutcome(
        returncode=returncode,
        signal=signal_number,
        exceeded=exceeded,
        peak_rss_bytes=max(peak, usage.ru_maxrss * 1024),
        cpu_seconds=usage.ru_utime + usage.ru_stime,
        wall_seconds=wall,
    )

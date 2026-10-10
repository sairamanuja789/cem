"""Resource limits for child processes (RES-001, SIM-002)."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Final

JOB_MEMORY_CAP_BYTES: Final = 12 * 1024**3
"""ADR-000 (accepted 2026-10-10): 12 GiB cap on all memory of a job, leaving at least 3 GB of the
16 GB target laptop for the OS (RES-001). A job may ask for less, never for more."""


@dataclass(frozen=True, slots=True)
class ResourceLimits:
    """Limits the runner enforces on one child process and everything it starts.

    cores and wall_time_s have no default: they depend on the job (for example the MPI rank count
    and the case size), and there is no sourced general value for them.
    """

    cores: int
    wall_time_s: float
    memory_bytes: int = JOB_MEMORY_CAP_BYTES

    def __post_init__(self) -> None:
        if isinstance(self.cores, bool) or not isinstance(self.cores, int) or self.cores < 1:
            raise ValueError(f"cores must be a positive integer, got {self.cores!r}")
        if not math.isfinite(self.wall_time_s) or self.wall_time_s <= 0:
            raise ValueError(f"wall_time_s must be positive and finite, got {self.wall_time_s!r}")
        if not isinstance(self.memory_bytes, int) or self.memory_bytes <= 0:
            raise ValueError(f"memory_bytes must be a positive integer, got {self.memory_bytes!r}")
        if self.memory_bytes > JOB_MEMORY_CAP_BYTES:
            raise ValueError(
                f"RES-001: memory_bytes {self.memory_bytes} is above the job memory cap "
                f"{JOB_MEMORY_CAP_BYTES} (ADR-000)"
            )

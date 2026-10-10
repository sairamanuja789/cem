"""Typed Python API of the C++ kernel (T11; PERF-002, MAINT-005; ADR-013).

The only way Python reaches the kernel: every function sends one JSON request through the
nanobind module cemkit._kernel (a thin wrapper of the C ABI, kernel/cemkit/capi/cemkit.h) and
decodes the response. Batches go in one call (PERF-002): never loop over candidates calling the
kernel one at a time. Request and response formats: docs/capi.md. Numbers are in coherent SI units.

A classified kernel failure (cemkit_status CEMKIT_FAILED) raises KernelError carrying the kernel's
Error (same shape as cemkit.errors.Error). A failed case inside a batch is not raised: its result
row carries its own "error".
"""

from __future__ import annotations

import json
from collections.abc import Callable, Sequence
from typing import Any

from cemkit import _kernel
from cemkit.errors import Error, error

# The C ABI MAJOR.MINOR this package was written against (MAINT-005): the library must have the
# same MAJOR and at least this MINOR.
ABI_REQUIRED = (0, 1)

CEMKIT_OK = 0
CEMKIT_FAILED = 1

Json = dict[str, Any]


class KernelError(Exception):
    """A request the kernel rejected with a classified error (cemkit_status CEMKIT_FAILED)."""

    def __init__(self, err: Error) -> None:
        super().__init__(err.message)
        self.error = err


class KernelInternalError(Exception):
    """The kernel returned CEMKIT_INVALID_ARGUMENT or CEMKIT_INTERNAL_ERROR: a defect."""


def abi_version() -> tuple[int, int, int]:
    return _kernel.abi_version()


def kernel_version() -> str:
    return _kernel.kernel_version()


def check_abi() -> None:
    """Raises KernelInternalError unless the loaded kernel's ABI is compatible (MAINT-005)."""
    major, minor, _ = abi_version()
    if major != ABI_REQUIRED[0] or minor < ABI_REQUIRED[1]:
        raise KernelInternalError(
            f"kernel ABI {major}.{minor} is not compatible with {ABI_REQUIRED}"
        )


def _error_of(body: Json) -> Error:
    e = body["error"]
    return error(e["code"], e["message"], e["subject"], **e["details"])


def _call(entry: Callable[[str], tuple[int, str]], request: Json) -> Json:
    status, text = entry(json.dumps(request))
    if status == CEMKIT_OK:
        result: Json = json.loads(text)
        return result
    if status == CEMKIT_FAILED:
        raise KernelError(_error_of(json.loads(text)))
    raise KernelInternalError(f"kernel status {status}: {text}")


def l0_batch(cases: Sequence[Json]) -> list[Json]:
    """Evaluates every L0 case in one kernel call; one result per case, in order."""
    results: list[Json] = _call(_kernel.l0_batch, {"cases": list(cases)})["results"]
    return results


def feasibility(request: Json) -> Json:
    """The L0 feasibility gate report for one duty (SEL-004)."""
    report: Json = _call(_kernel.feasibility, request)["report"]
    return report


def spec_compile(spec: Json, autonomous_mode: bool = False) -> Json:
    """The compiled spec: fields in SI with original values, questions, conflicts."""
    compiled: Json = _call(
        _kernel.spec_compile, {"spec": spec, "autonomous_mode": autonomous_mode}
    )["spec"]
    return compiled


def geometry_smoke(request: Json) -> Json:
    """Builds the geometry test solid and reports validity, topology, mass properties and export
    hashes (GEO-001, GEO-002, GEO-004). Writes no files."""
    return _call(_kernel.geometry_smoke, request)

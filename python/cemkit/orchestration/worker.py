"""The worker: claims jobs from the store, runs them under limits, publishes their outputs (T13).

One attempt of a job (ORC-004):

1. claim       the store leases the oldest runnable job to this worker (status running). Heavy jobs
               start only while fewer than max_heavy_jobs are active (ORC-002).
2. run         the handler turns the payload into a command; the runner runs it in
               work/job-<pk>/attempt-<n>/ under its limits and renews the lease while it runs.
3. classify    a limit hit is resource_exceeded (SIM-002); a signal the runner did not send is
               internal_error; otherwise the handler decides (for example mesh_failed or
               sim_untrusted). A failure ends the attempt; the store requeues it while retries
               remain (ORC-003) and records the reason.
4. manifest    the outputs (work/.../out/**), the logs and the measured usage are listed with
               their sha256 in manifest.json, written atomically, before the job is marked
               publishing.
5. publish     each file goes into the content-addressed artifact store, then one transaction
               links them, records the attempt as done and marks the job done.

Reconciliation (ORC-004, no blind reruns): before each claim the worker looks for leases that
expired. A job lost while running gets a lost attempt (internal_error) and is retried if its budget
allows. A job lost while publishing is resumed from its manifest, without rerunning the child, if
every listed file still matches its hash; otherwise it is treated like a lost running job.

Every finished attempt is logged as a JSON line with peak RAM, CPU time and wall time (OBS-001).
"""

from __future__ import annotations

import errno
import hashlib
import json
import os
import re
import tempfile
import time
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from functools import partial
from pathlib import Path
from typing import Any, Protocol

from cemkit.logging import JsonLogger
from cemkit.orchestration.limits import ResourceLimits
from cemkit.orchestration.runner import (
    DEFAULT_OOM_EVENTS,
    KERNEL_OOM,
    MEMORY,
    WALL_TIME,
    ProcessOutcome,
    run_limited,
)
from cemkit.store import (
    Claim,
    DocumentRejected,
    Lease,
    LeaseLost,
    PublishedFile,
    Store,
    Usage,
    capture_run_metadata,
)
from cemkit.store.sqlite.backend import DEFAULT_MAX_HEAVY_JOBS

_WORKER_ID = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}")
_CHUNK = 1 << 20


@dataclass(frozen=True, slots=True)
class WorkerConfig:
    """Worker settings. The timing defaults are software settings, not engineering values:
    a lease is renewed every heartbeat_seconds and lost after lease_seconds without renewal."""

    worker_id: str
    lease_seconds: float = 30.0
    heartbeat_seconds: float = 5.0
    max_heavy_jobs: int = DEFAULT_MAX_HEAVY_JOBS
    poll_seconds: float = 0.5
    oom_events: Path | None = DEFAULT_OOM_EVENTS

    def __post_init__(self) -> None:
        if not _WORKER_ID.fullmatch(self.worker_id):
            raise ValueError(f"worker id {self.worker_id!r} must match {_WORKER_ID.pattern}")
        if not 0 < self.heartbeat_seconds < self.lease_seconds:
            raise ValueError("need 0 < heartbeat_seconds < lease_seconds")
        if self.poll_seconds <= 0 or self.max_heavy_jobs < 0:
            raise ValueError("poll_seconds must be positive and max_heavy_jobs non-negative")


@dataclass(frozen=True, slots=True)
class Command:
    argv: tuple[str, ...]
    limits: ResourceLimits
    env: Mapping[str, str] | None = None
    """Added to the worker's environment for the child."""


@dataclass(frozen=True, slots=True)
class JobFailure:
    code: str
    """A REL-002 code from schemas/cemkit/v1/error-codes.json."""
    message: str


class JobHandler(Protocol):
    """Turns a job payload into a command and judges a finished child (one handler per kind)."""

    @property
    def kind(self) -> str: ...

    def command(self, payload: Mapping[str, Any], workdir: Path) -> Command: ...

    def check(self, outcome: ProcessOutcome, workdir: Path) -> JobFailure | None:
        """None if the child succeeded. Called only for a child that exited by itself."""
        ...


class _ManifestInvalid(Exception):
    pass


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(_CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _limit_failure(outcome: ProcessOutcome, limits: ResourceLimits) -> JobFailure | None:
    if outcome.exceeded == MEMORY:
        return JobFailure(
            "resource_exceeded",
            f"memory limit exceeded: peak {outcome.peak_rss_bytes} bytes, limit "
            f"{limits.memory_bytes} bytes",
        )
    if outcome.exceeded == KERNEL_OOM:
        return JobFailure(
            "resource_exceeded",
            "killed by the kernel out-of-memory killer (cgroup memory.events oom_kill increased)",
        )
    if outcome.exceeded == WALL_TIME:
        return JobFailure(
            "resource_exceeded",
            f"wall time limit exceeded: ran {outcome.wall_seconds:.1f} s, limit "
            f"{limits.wall_time_s} s",
        )
    if outcome.signal is not None:
        return JobFailure(
            "internal_error", f"child killed by signal {outcome.signal}, not by the runner"
        )
    return None


class Worker:
    def __init__(
        self,
        store: Store,
        config: WorkerConfig,
        handlers: Iterable[JobHandler],
        *,
        clock: Callable[[], float] = time.time,
        logger: JsonLogger | None = None,
    ) -> None:
        self._store = store
        self._config = config
        self._clock = clock
        self._handlers: dict[str, JobHandler] = {}
        for handler in handlers:
            if handler.kind in self._handlers:
                raise ValueError(f"two handlers for job kind {handler.kind!r}")
            self._handlers[handler.kind] = handler
        self._kinds = tuple(sorted(self._handlers))
        self._run = store.record_run(
            capture_run_metadata({"worker_id": config.worker_id, "kinds": list(self._kinds)})
        )
        self._log = logger or JsonLogger(store.root / "logs" / f"worker-{config.worker_id}.jsonl")
        self._log.emit(
            "worker_started",
            worker_id=config.worker_id,
            kinds=list(self._kinds),
            run_pk=self._run.run_pk,
            unknown_metadata=list(self._run.metadata.unknown_fields()),
        )

    # --- loop --------------------------------------------------------------------------------

    def run_once(self) -> bool:
        """Reconcile expired leases, then claim and run one job. False if nothing was claimable."""
        self._reconcile()
        claim = self._store.claim_job(
            self._run,
            self._config.worker_id,
            self._kinds,
            now=self._clock(),
            lease_seconds=self._config.lease_seconds,
            max_heavy=self._config.max_heavy_jobs,
        )
        if claim is None:
            return False
        self._log.emit(
            "job_claimed",
            worker_id=self._config.worker_id,
            job_pk=claim.job.job_pk,
            kind=claim.spec.kind,
            attempt=claim.lease.attempt,
            resource_class=claim.spec.resource_class,
            input_hash=claim.spec.input_hash,
        )
        self._execute(claim)
        return True

    def run_until_idle(self, timeout: float | None = None) -> None:
        """Run jobs until none of this worker's kinds is queued, running or publishing."""
        deadline = None if timeout is None else time.monotonic() + timeout
        while True:
            if self.run_once():
                continue
            if self._store.unfinished_jobs(self._kinds) == 0:
                self._log.emit("worker_idle", worker_id=self._config.worker_id)
                return
            if deadline is not None and time.monotonic() > deadline:
                raise TimeoutError(f"worker {self._config.worker_id}: jobs still unfinished")
            time.sleep(self._config.poll_seconds)

    # --- one attempt -------------------------------------------------------------------------

    def _workdir(self, lease: Lease) -> Path:
        return self._store.root / "work" / f"job-{lease.job_pk}" / f"attempt-{lease.attempt}"

    def _heartbeat(self, lease: Lease) -> None:
        self._store.heartbeat(
            self._run, lease, now=self._clock(), lease_seconds=self._config.lease_seconds
        )

    def _execute(self, claim: Claim) -> None:
        lease, kind = claim.lease, claim.spec.kind
        workdir = self._workdir(lease)
        (workdir / "out").mkdir(parents=True, exist_ok=True)
        try:
            command = self._handlers[kind].command(claim.spec.payload(), workdir)
        except (KeyError, TypeError, ValueError) as error:
            self._fail(lease, kind, JobFailure("invalid_input", f"bad job payload: {error}"))
            return
        try:
            outcome = run_limited(
                command.argv,
                command.limits,
                cwd=workdir,
                stdout=workdir / "stdout.log",
                stderr=workdir / "stderr.log",
                env=command.env,
                on_tick=partial(self._heartbeat, lease),
                tick_seconds=self._config.heartbeat_seconds,
                oom_events=self._config.oom_events,
            )
        except LeaseLost as error:
            self._lease_lost(lease, error)
            return
        usage = Usage(outcome.peak_rss_bytes, outcome.cpu_seconds, outcome.wall_seconds)
        failure = _limit_failure(outcome, command.limits) or self._handlers[kind].check(
            outcome, workdir
        )
        if failure is not None:
            self._fail(lease, kind, failure, usage)
            return
        try:
            digest = self._write_manifest(lease, workdir, usage)
            self._store.mark_publishing(self._run, lease, f"manifest {digest}")
        except OSError as error:
            self._fail(lease, kind, self._io_failure(error), usage)
            return
        except LeaseLost as error:
            self._lease_lost(lease, error)
            return
        self._publish_or_fail(lease, kind, workdir, usage)

    def _io_failure(self, error: OSError) -> JobFailure:
        if error.errno == errno.ENOSPC:
            return JobFailure("resource_exceeded", f"disk full while publishing outputs: {error}")
        return JobFailure("internal_error", f"publishing outputs failed: {error}")

    def _fail(
        self, lease: Lease, kind: str, failure: JobFailure, usage: Usage | None = None
    ) -> None:
        usage = usage or Usage()
        try:
            try:
                status = self._store.fail_attempt(
                    self._run, lease, failure.code, failure.message, usage
                )
            except DocumentRejected:
                failure = JobFailure(
                    "internal_error",
                    f"the {kind} handler returned an undefined failure code {failure.code!r}: "
                    f"{failure.message}",
                )
                status = self._store.fail_attempt(
                    self._run, lease, failure.code, failure.message, usage
                )
        except LeaseLost as error:
            self._lease_lost(lease, error)
            return
        self._finished(lease, kind, "failed", failure.code, usage, status)

    def _finished(
        self, lease: Lease, kind: str, outcome: str, code: str | None, usage: Usage, status: str
    ) -> None:
        self._log.emit(
            "attempt_finished",
            worker_id=self._config.worker_id,
            job_pk=lease.job_pk,
            kind=kind,
            attempt=lease.attempt,
            outcome=outcome,
            code=code,
            job_status=status,
            peak_rss_bytes=usage.peak_rss_bytes,
            cpu_seconds=usage.cpu_seconds,
            wall_seconds=usage.wall_seconds,
        )

    def _lease_lost(self, lease: Lease, error: Exception) -> None:
        self._log.emit(
            "lease_lost",
            worker_id=self._config.worker_id,
            job_pk=lease.job_pk,
            attempt=lease.attempt,
            reason=str(error),
        )

    # --- manifest and publishing ---------------------------------------------------------------

    def _write_manifest(self, lease: Lease, workdir: Path, usage: Usage) -> str:
        files: list[dict[str, Any]] = []
        out = workdir / "out"
        for path in sorted(out.rglob("*")):
            if path.is_file() and not path.is_symlink():  # never publish what a link points to
                name = path.relative_to(out).as_posix()
                files.append(self._entry(workdir, f"out/{name}", name, "output"))
        for name in ("stdout.log", "stderr.log"):
            files.append(self._entry(workdir, name, name, "log"))
        manifest = {
            "job_pk": lease.job_pk,
            "attempt": lease.attempt,
            "usage": {
                "peak_rss_bytes": usage.peak_rss_bytes,
                "cpu_seconds": usage.cpu_seconds,
                "wall_seconds": usage.wall_seconds,
            },
            "files": files,
        }
        text = json.dumps(manifest, sort_keys=True, indent=1, allow_nan=False) + "\n"
        descriptor, temporary = tempfile.mkstemp(dir=workdir, prefix="manifest-")
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                stream.write(text)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, workdir / "manifest.json")
        finally:
            Path(temporary).unlink(missing_ok=True)
        return hashlib.sha256(text.encode("utf-8")).hexdigest()

    @staticmethod
    def _entry(workdir: Path, relative: str, name: str, role: str) -> dict[str, Any]:
        path = workdir / relative
        return {
            "name": name,
            "path": relative,
            "role": role,
            "sha256": _sha256_file(path),
            "size_bytes": path.stat().st_size,
        }

    def _verified_manifest(self, lease: Lease, workdir: Path) -> dict[str, Any]:
        """The manifest of this attempt, if it is complete and every file matches its hash."""
        try:
            manifest = json.loads((workdir / "manifest.json").read_text(encoding="utf-8"))
            if (manifest["job_pk"], manifest["attempt"]) != (lease.job_pk, lease.attempt):
                raise _ManifestInvalid("the manifest belongs to another attempt")
            for entry in manifest["files"]:
                relative = Path(entry["path"])
                if relative.is_absolute() or ".." in relative.parts:
                    raise _ManifestInvalid(f"path {entry['path']!r} leaves the work directory")
                if _sha256_file(workdir / relative) != entry["sha256"]:
                    raise _ManifestInvalid(f"{entry['path']} no longer matches its sha256")
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise _ManifestInvalid(f"manifest unreadable: {error}") from error
        assert isinstance(manifest, dict)
        return manifest

    def _publish(self, lease: Lease, workdir: Path) -> Usage:
        manifest = self._verified_manifest(lease, workdir)
        artifacts = self._store.artifacts
        published: list[PublishedFile] = []
        for entry in manifest["files"]:
            stored = artifacts.put_file(workdir / entry["path"])
            if stored.sha256 != entry["sha256"]:
                raise _ManifestInvalid(f"{entry['path']} changed while it was published")
            published.append(
                PublishedFile(entry["name"], stored.sha256, stored.size_bytes, entry["role"])
            )
        stored = artifacts.put_file(workdir / "manifest.json")
        published.append(
            PublishedFile(
                "manifest.json", stored.sha256, stored.size_bytes, "manifest", "application/json"
            )
        )
        usage = Usage(**manifest["usage"])
        self._store.complete_job(self._run, lease, usage, published)
        return usage

    def _publish_or_fail(self, lease: Lease, kind: str, workdir: Path, usage: Usage) -> None:
        try:
            usage = self._publish(lease, workdir)
        except _ManifestInvalid as error:
            self._fail(lease, kind, JobFailure("internal_error", str(error)), usage)
        except OSError as error:
            self._fail(lease, kind, self._io_failure(error), usage)
        except LeaseLost as error:
            self._lease_lost(lease, error)
        else:
            self._finished(lease, kind, "done", None, usage, "done")

    # --- reconciliation ------------------------------------------------------------------------

    def _reconcile(self) -> None:
        for expired in self._store.expired_leases(now=self._clock()):
            lease, kind = expired.lease, expired.spec.kind
            reason = (
                f"worker {lease.worker_id} stopped renewing its lease while the job was "
                f"{expired.status}"
            )
            try:
                if expired.status == "publishing":
                    workdir = self._workdir(lease)
                    try:
                        manifest = self._verified_manifest(lease, workdir)
                    except _ManifestInvalid as error:
                        reason = f"{reason}; outputs not resumable: {error}"
                    else:
                        adopted = self._store.adopt_lease(
                            self._run,
                            lease,
                            self._config.worker_id,
                            now=self._clock(),
                            lease_seconds=self._config.lease_seconds,
                        )
                        self._log.emit(
                            "job_adopted",
                            worker_id=self._config.worker_id,
                            job_pk=lease.job_pk,
                            attempt=lease.attempt,
                            reason=reason,
                        )
                        self._publish_or_fail(adopted, kind, workdir, Usage(**manifest["usage"]))
                        continue
                status = self._store.abandon_lease(
                    self._run, lease, now=self._clock(), reason=reason
                )
            except LeaseLost:
                continue  # another worker reconciled it first
            self._log.emit(
                "job_reconciled",
                worker_id=self._config.worker_id,
                job_pk=lease.job_pk,
                attempt=lease.attempt,
                job_status=status,
                reason=reason,
            )

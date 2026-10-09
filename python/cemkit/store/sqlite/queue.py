"""Job-queue operations on the SQLite store (T13; ORC-001..004).

Every function here runs on a connection that is already inside one write transaction
(SqliteStore._write, BEGIN IMMEDIATE), so a claim, a hand-over or a completion is atomic and two
workers can never both win the same job.

State model, all derived from append-only rows:
- status       newest job_events row: queued -> running -> publishing -> done, or -> failed;
               a failed or lost attempt with retries left goes back to queued (ORC-003).
- lease        newest job_leases row of the job. It is current while no job_attempts row ends it.
- expiry       the later of the lease's expires_at and its newest heartbeat's expires_at.
"""

from __future__ import annotations

import json
import sqlite3
from collections.abc import Sequence
from datetime import UTC, datetime

from cemkit.store.errors import LeaseLost, StoreError
from cemkit.store.records import (
    Claim,
    ExpiredLease,
    JobRecord,
    JobSpec,
    Lease,
    PublishedFile,
    Usage,
)

ACTIVE: tuple[str, str] = ("running", "publishing")

_SPEC = (
    "SELECT j.job_pk, j.kind, s.resource_class, s.max_retries, j.input_hash, s.payload"
    " FROM jobs AS j JOIN job_specs AS s ON s.job_pk = j.job_pk WHERE j.job_pk = ?"
)
_LEASE = (
    "SELECT lease_pk, job_pk, worker_id, attempt, acquired_at, expires_at FROM job_leases"
    " WHERE lease_pk = ?"
)


def _recorded_at() -> str:
    return datetime.now(UTC).isoformat(timespec="microseconds")


def spec(db: sqlite3.Connection, job_pk: int) -> JobSpec:
    row = db.execute(_SPEC, (job_pk,)).fetchone()
    if row is None:
        raise StoreError(f"job {job_pk} has no job spec (it was not enqueued for a worker)")
    return JobSpec(*row)


def status(db: sqlite3.Connection, job_pk: int) -> str | None:
    row = db.execute("SELECT status FROM job_status WHERE job_pk = ?", (job_pk,)).fetchone()
    return None if row is None else str(row[0])


def _event(db: sqlite3.Connection, run_pk: int, job_pk: int, new: str, reason: str) -> None:
    db.execute(
        "INSERT INTO job_events (job_pk, run_pk, status, reason, recorded_at)"
        " VALUES (?, ?, ?, ?, ?)",
        (job_pk, run_pk, new, reason, _recorded_at()),
    )


def _insert_lease(
    db: sqlite3.Connection,
    run_pk: int,
    job_pk: int,
    worker_id: str,
    attempt: int,
    now: float,
    lease_seconds: float,
) -> Lease:
    expires_at = now + lease_seconds
    cursor = db.execute(
        "INSERT INTO job_leases (job_pk, run_pk, worker_id, attempt, acquired_at, expires_at,"
        " recorded_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
        (job_pk, run_pk, worker_id, attempt, now, expires_at, _recorded_at()),
    )
    assert cursor.lastrowid is not None
    return Lease(cursor.lastrowid, job_pk, worker_id, attempt, now, expires_at)


def enqueue(
    db: sqlite3.Connection,
    run_pk: int,
    kind: str,
    payload_json: str,
    input_hash: str,
    resource_class: str,
    max_retries: int,
    candidate_pk: int | None,
) -> JobSpec:
    """A job, its spec and its first status (queued), in the caller's transaction."""
    recorded_at = _recorded_at()
    cursor = db.execute(
        "INSERT INTO jobs (run_pk, kind, candidate_pk, input_hash, recorded_at)"
        " VALUES (?, ?, ?, ?, ?)",
        (run_pk, kind, candidate_pk, input_hash, recorded_at),
    )
    assert cursor.lastrowid is not None
    job_pk = cursor.lastrowid
    db.execute(
        "INSERT INTO job_specs (job_pk, run_pk, resource_class, max_retries, payload, recorded_at)"
        " VALUES (?, ?, ?, ?, ?, ?)",
        (job_pk, run_pk, resource_class, max_retries, payload_json, recorded_at),
    )
    _event(db, run_pk, job_pk, "queued", "enqueued")
    return spec(db, job_pk)


def claim(
    db: sqlite3.Connection,
    run_pk: int,
    worker_id: str,
    kinds: Sequence[str],
    now: float,
    lease_seconds: float,
    max_heavy: int,
) -> Claim | None:
    """The oldest queued job of one of `kinds` that may start now, leased to `worker_id`.

    A heavy job may start only while fewer than `max_heavy` heavy jobs are running or publishing
    (ORC-002). Light jobs are not limited here; each worker runs one job at a time.
    """
    if not kinds:
        return None
    heavy_active = db.execute(
        "SELECT count(*) FROM job_status AS js JOIN job_specs AS s ON s.job_pk = js.job_pk"
        " WHERE js.status IN ('running', 'publishing') AND s.resource_class = 'heavy'"
    ).fetchone()[0]
    marks = ", ".join("?" for _ in kinds)
    queued = db.execute(
        "SELECT js.job_pk FROM job_status AS js JOIN jobs AS j ON j.job_pk = js.job_pk"
        f" JOIN job_specs AS s ON s.job_pk = js.job_pk WHERE js.status = 'queued' AND j.kind IN"
        f" ({marks}) AND (s.resource_class = 'light' OR ? < ?) ORDER BY js.job_pk LIMIT 1",
        (*kinds, heavy_active, max_heavy),
    ).fetchone()
    if queued is None:
        return None
    job_pk = int(queued[0])
    attempt = 1 + int(
        db.execute(
            "SELECT coalesce(max(attempt), 0) FROM job_leases WHERE job_pk = ?", (job_pk,)
        ).fetchone()[0]
    )
    lease = _insert_lease(db, run_pk, job_pk, worker_id, attempt, now, lease_seconds)
    _event(db, run_pk, job_pk, "running", f"attempt {attempt} started by worker {worker_id}")
    row = db.execute(
        "SELECT job_pk, run_pk, kind, candidate_pk, input_hash, recorded_at FROM jobs"
        " WHERE job_pk = ?",
        (job_pk,),
    ).fetchone()
    return Claim(JobRecord(*row), spec(db, job_pk), lease)


def expiry(db: sqlite3.Connection, lease: Lease) -> float:
    row = db.execute(
        "SELECT max(expires_at) FROM job_heartbeats WHERE lease_pk = ?", (lease.lease_pk,)
    ).fetchone()
    return lease.expires_at if row[0] is None else max(lease.expires_at, float(row[0]))


def require_current(db: sqlite3.Connection, lease: Lease) -> str:
    """The job's status, if `lease` still holds the job; otherwise LeaseLost."""
    latest = db.execute(
        "SELECT max(lease_pk) FROM job_leases WHERE job_pk = ?", (lease.job_pk,)
    ).fetchone()[0]
    ended = db.execute(
        "SELECT 1 FROM job_attempts WHERE lease_pk = ?", (lease.lease_pk,)
    ).fetchone()
    current = status(db, lease.job_pk)
    if latest != lease.lease_pk or ended is not None or current not in ACTIVE:
        raise LeaseLost(f"lease {lease.lease_pk} on job {lease.job_pk} is no longer current")
    assert current is not None
    return current


def heartbeat(
    db: sqlite3.Connection, run_pk: int, lease: Lease, now: float, lease_seconds: float
) -> float:
    require_current(db, lease)
    expires_at = now + lease_seconds
    db.execute(
        "INSERT INTO job_heartbeats (lease_pk, run_pk, at, expires_at, recorded_at)"
        " VALUES (?, ?, ?, ?, ?)",
        (lease.lease_pk, run_pk, now, expires_at, _recorded_at()),
    )
    return expires_at


def mark_publishing(db: sqlite3.Connection, run_pk: int, lease: Lease, reason: str) -> None:
    if require_current(db, lease) != "running":
        raise StoreError(f"job {lease.job_pk} is not running")
    _event(db, run_pk, lease.job_pk, "publishing", reason)


def _attempt(
    db: sqlite3.Connection,
    run_pk: int,
    lease: Lease,
    outcome: str,
    code: str | None,
    reason: str | None,
    usage: Usage,
) -> None:
    db.execute(
        "INSERT INTO job_attempts (lease_pk, job_pk, run_pk, outcome, code, reason,"
        " peak_rss_bytes, cpu_seconds, wall_seconds, recorded_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            lease.lease_pk,
            lease.job_pk,
            run_pk,
            outcome,
            code,
            reason,
            usage.peak_rss_bytes,
            usage.cpu_seconds,
            usage.wall_seconds,
            _recorded_at(),
        ),
    )


def end_attempt(
    db: sqlite3.Connection,
    run_pk: int,
    lease: Lease,
    outcome: str,
    code: str,
    reason: str,
    usage: Usage,
) -> str:
    """Record a failed or lost attempt and its failure; requeue it if retries remain (ORC-003).

    Returns the job's new status, queued or failed.
    """
    require_current(db, lease)
    _attempt(db, run_pk, lease, outcome, code, reason, usage)
    candidate_pk = db.execute(
        "SELECT candidate_pk FROM jobs WHERE job_pk = ?", (lease.job_pk,)
    ).fetchone()[0]
    details = json.dumps(
        {"attempt": str(lease.attempt), "outcome": outcome, "worker": lease.worker_id},
        sort_keys=True,
        separators=(",", ":"),
    )
    db.execute(
        "INSERT INTO failures (run_pk, job_pk, candidate_pk, code, message, details, recorded_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)",
        (run_pk, lease.job_pk, candidate_pk, code, reason, details, _recorded_at()),
    )
    budget = spec(db, lease.job_pk).max_retries
    if lease.attempt <= budget:
        new = "queued"
        why = f"retry {lease.attempt} of {budget} after attempt {lease.attempt}: {code}: {reason}"
    else:
        new = "failed"
        why = f"{code}: {reason} (attempt {lease.attempt}; no retries left of {budget})"
    _event(db, run_pk, lease.job_pk, new, why)
    return new


def complete(
    db: sqlite3.Connection,
    run_pk: int,
    lease: Lease,
    usage: Usage,
    files: Sequence[PublishedFile],
) -> None:
    """Link the published files, end the attempt as done and mark the job done, atomically."""
    if require_current(db, lease) != "publishing":
        raise StoreError(f"job {lease.job_pk} must be publishing to complete")
    for item in files:
        recorded_at = _recorded_at()
        cursor = db.execute(
            "INSERT INTO artifacts (run_pk, sha256, size_bytes, name, media_type, recorded_at)"
            " VALUES (?, ?, ?, ?, ?, ?)",
            (run_pk, item.sha256, item.size_bytes, item.name, item.media_type, recorded_at),
        )
        db.execute(
            "INSERT INTO job_artifacts (job_pk, artifact_pk, run_pk, role, recorded_at)"
            " VALUES (?, ?, ?, ?, ?)",
            (lease.job_pk, cursor.lastrowid, run_pk, item.role, recorded_at),
        )
    _attempt(db, run_pk, lease, "done", None, None, usage)
    _event(db, run_pk, lease.job_pk, "done", f"attempt {lease.attempt} published")


def expired(db: sqlite3.Connection, now: float) -> tuple[ExpiredLease, ...]:
    """Running or publishing jobs whose current lease expired before `now`."""
    found: list[ExpiredLease] = []
    rows = db.execute(
        "SELECT js.job_pk, js.status, max(l.lease_pk) FROM job_status AS js"
        " JOIN job_leases AS l ON l.job_pk = js.job_pk"
        " WHERE js.status IN ('running', 'publishing') GROUP BY js.job_pk ORDER BY js.job_pk"
    ).fetchall()
    for job_pk, current, lease_pk in rows:
        lease = Lease(*db.execute(_LEASE, (lease_pk,)).fetchone())
        until = expiry(db, lease)
        if until < now:
            found.append(ExpiredLease(lease, spec(db, job_pk), str(current), until))
    return tuple(found)


def require_expired(db: sqlite3.Connection, lease: Lease, now: float) -> str:
    current = require_current(db, lease)
    if expiry(db, lease) >= now:
        raise LeaseLost(f"lease {lease.lease_pk} on job {lease.job_pk} has not expired")
    return current


def adopt(
    db: sqlite3.Connection,
    run_pk: int,
    lease: Lease,
    worker_id: str,
    now: float,
    lease_seconds: float,
) -> Lease:
    """Hand an expired publishing job to `worker_id` for the same attempt (no rerun; ORC-004)."""
    if require_expired(db, lease, now) != "publishing":
        raise StoreError(f"job {lease.job_pk} is not publishing; it cannot be adopted")
    reason = f"worker {lease.worker_id} lost while publishing; worker {worker_id} resumed it"
    _attempt(db, run_pk, lease, "handed_over", None, reason, Usage())
    new = _insert_lease(db, run_pk, lease.job_pk, worker_id, lease.attempt, now, lease_seconds)
    _event(db, run_pk, lease.job_pk, "publishing", reason)
    return new

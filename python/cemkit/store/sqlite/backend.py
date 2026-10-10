"""SQLite implementation of the store port (STORE-001..004, REL-001).

A store is a directory:

    store.sqlite3   the database (WAL mode; see migrate.connect)
    artifacts/      the content-addressed files (see cemkit.store.artifacts)

Documents (specs, candidates, results) are checked against the JSON Schemas in schemas/ before
they are written, and stored as canonical JSON with their sha256. A spec revision or a candidate
that already exists may be recorded again only with identical content; the existing record is then
returned. Different content under the same identity is refused (immutability).
"""

from __future__ import annotations

import json
import sqlite3
from collections.abc import Iterable, Iterator, Mapping, Sequence
from contextlib import contextmanager
from datetime import UTC, datetime
from functools import cache
from pathlib import Path
from types import TracebackType
from typing import Any, Final, Self

from cemkit import schemas
from cemkit.store.artifacts import ArtifactStore
from cemkit.store.errors import DocumentRejected, StoreError
from cemkit.store.provenance import canonical_json, sha256_hex
from cemkit.store.records import (
    ARTIFACT_ROLES,
    RESOURCE_CLASSES,
    SHA256_HEX,
    ArtifactRecord,
    AttemptRecord,
    CandidateRecord,
    Claim,
    ExpiredLease,
    FailureRecord,
    JobEvent,
    JobRecord,
    JobSpec,
    Lease,
    PublishedFile,
    ResultRecord,
    Run,
    RunMetadata,
    SpecRecord,
    Usage,
    versions_from_json,
)
from cemkit.store.sqlite import migrate, queue

DATABASE_NAME: Final = "store.sqlite3"
ARTIFACTS_DIRECTORY: Final = "artifacts"
JOB_STATUSES: Final = ("queued", "running", "publishing", "done", "failed")  # ORC-004
ERROR_CODES: Final = "urn:cemkit:schema:v1:error-codes"
DEFAULT_MAX_RETRIES: Final = 1  # ORC-003: "Default 1 retry"
DEFAULT_MAX_HEAVY_JOBS: Final = 1  # ORC-002: heavy jobs one at a time by default
# A worker that stops renewing its lease has no failure code of its own in error-codes.json; the
# lost attempt is recorded as internal_error with the reason (ADR-005, owner review).
LOST_WORKER_CODE: Final = "internal_error"
_PRAGMAS: Final = frozenset({"journal_mode", "foreign_keys", "synchronous"})

_RUN = (
    "SELECT run_pk, recorded_at, kernel_version, plugin_versions, model_versions, git_commit,"
    " container_digest, input_hash FROM runs"
)
_SPEC = "SELECT spec_pk, run_pk, spec_id, revision, sha256, document, recorded_at FROM specs"
_CANDIDATE = (
    "SELECT candidate_pk, run_pk, spec_pk, candidate_id, sha256, document, recorded_at"
    " FROM candidates"
)
_EVENT = "SELECT event_pk, job_pk, run_pk, status, reason, recorded_at FROM job_events"
_RESULT = (
    "SELECT result_pk, run_pk, candidate_pk, job_pk, status, sha256, document, recorded_at"
    " FROM results"
)
_ARTIFACT = (
    "SELECT artifact_pk, run_pk, sha256, size_bytes, name, media_type, recorded_at FROM artifacts"
)


@cache
def _schemas() -> schemas.SchemaSet:
    return schemas.load_schemas()


def _now() -> str:
    return datetime.now(UTC).isoformat(timespec="microseconds")


def _canonical(kind: str, document: Any) -> tuple[str, str]:
    try:
        text = canonical_json(document)
    except (TypeError, ValueError) as error:
        raise DocumentRejected(f"{kind}: not storable as JSON: {error}") from error
    return text, sha256_hex(text)


def _require_valid(kind: str, errors: list[str]) -> None:
    if errors:
        raise DocumentRejected(f"{kind} rejected by its schema: " + "; ".join(errors[:5]))


def _run(row: sqlite3.Row | tuple[Any, ...]) -> Run:
    pk, recorded_at, kernel, plugins, models, commit, digest, hashed = row
    metadata = RunMetadata(
        kernel_version=kernel,
        plugin_versions=versions_from_json(plugins),
        model_versions=versions_from_json(models),
        git_commit=commit,
        container_digest=digest,
        input_hash=hashed,
    )
    return Run(run_pk=pk, recorded_at=recorded_at, metadata=metadata)


def open_store(root: Path) -> SqliteStore:
    """Create or open the store under `root`, migrating the database to the latest version."""
    root.mkdir(parents=True, exist_ok=True)
    migrate.migrate(root / DATABASE_NAME)
    return SqliteStore(root)


class SqliteStore:
    def __init__(self, root: Path) -> None:
        self._root = root
        self._database = root / DATABASE_NAME
        latest = len(migrate.load_migrations())
        if migrate.current_version(self._database) != latest:
            raise StoreError(f"{self._database} is not at schema version {latest}; use open_store")
        self._artifacts = ArtifactStore(root / ARTIFACTS_DIRECTORY)
        self._connection: sqlite3.Connection | None = migrate.connect(self._database)

    # --- lifecycle ---------------------------------------------------------------------------

    def __enter__(self) -> Self:
        return self

    def __exit__(
        self,
        kind: type[BaseException] | None,
        error: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.close()

    def close(self) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None

    @property
    def root(self) -> Path:
        return self._root

    @property
    def database(self) -> Path:
        return self._database

    @property
    def artifacts(self) -> ArtifactStore:
        return self._artifacts

    def pragma(self, name: str) -> Any:
        if name not in _PRAGMAS:
            raise StoreError(f"pragma {name!r} is not readable through the store")
        return self._db.execute(f"PRAGMA {name}").fetchone()[0]

    @property
    def _db(self) -> sqlite3.Connection:
        if self._connection is None:
            raise StoreError("the store is closed")
        return self._connection

    @contextmanager
    def _write(self) -> Iterator[sqlite3.Connection]:
        """One transaction; a constraint failure (unknown run, bad status, ...) is a StoreError."""
        try:
            with migrate.transaction(self._db) as connection:
                yield connection
        except sqlite3.IntegrityError as error:
            raise StoreError(f"the database refused the write: {error}") from error

    # --- writes ------------------------------------------------------------------------------

    def record_run(self, metadata: RunMetadata) -> Run:
        recorded_at = _now()
        with self._write() as db:
            cursor = db.execute(
                "INSERT INTO runs (recorded_at, kernel_version, plugin_versions, model_versions,"
                " git_commit, container_digest, input_hash) VALUES (?, ?, ?, ?, ?, ?, ?)",
                (
                    recorded_at,
                    metadata.kernel_version,
                    metadata.plugin_versions_json(),
                    metadata.model_versions_json(),
                    metadata.git_commit,
                    metadata.container_digest,
                    metadata.input_hash,
                ),
            )
        assert cursor.lastrowid is not None
        return Run(run_pk=cursor.lastrowid, recorded_at=recorded_at, metadata=metadata)

    def record_spec(self, run: Run, document: Mapping[str, Any]) -> SpecRecord:
        text, digest = _canonical("spec", document)
        _require_valid("spec", _schemas().spec_errors(document))
        spec_id, revision = document["spec_id"], document["revision"]
        with self._write() as db:
            existing = db.execute(
                f"{_SPEC} WHERE spec_id = ? AND revision = ?", (spec_id, revision)
            ).fetchone()
            if existing is not None:
                record = SpecRecord(*existing)
                if record.sha256 != digest:
                    raise DocumentRejected(
                        f"spec {spec_id} revision {revision} is immutable and already recorded "
                        "with different content; record a new revision instead"
                    )
                return record
            recorded_at = _now()
            cursor = db.execute(
                "INSERT INTO specs (run_pk, spec_id, revision, sha256, document, recorded_at)"
                " VALUES (?, ?, ?, ?, ?, ?)",
                (run.run_pk, spec_id, revision, digest, text, recorded_at),
            )
        assert cursor.lastrowid is not None
        return SpecRecord(
            cursor.lastrowid, run.run_pk, spec_id, revision, digest, text, recorded_at
        )

    def record_candidates(
        self, run: Run, documents: Iterable[Mapping[str, Any]]
    ) -> tuple[CandidateRecord, ...]:
        """Record a batch of candidates in one transaction: all of them or none."""
        records: list[CandidateRecord] = []
        with self._write() as db:
            for document in documents:
                records.append(self._insert_candidate(db, run, document))
        return tuple(records)

    def _insert_candidate(
        self, db: sqlite3.Connection, run: Run, document: Mapping[str, Any]
    ) -> CandidateRecord:
        text, digest = _canonical("candidate", document)
        _require_valid("candidate", _schemas().errors(schemas.CANDIDATE, document))
        candidate_id = document["candidate_id"]
        spec_pk = self._spec_pk(db, document["spec"])
        existing = db.execute(
            f"{_CANDIDATE} WHERE spec_pk = ? AND candidate_id = ?", (spec_pk, candidate_id)
        ).fetchone()
        if existing is not None:
            record = CandidateRecord(*existing)
            if record.sha256 != digest:
                raise DocumentRejected(
                    f"candidate {candidate_id} is immutable and already recorded with different "
                    "content"
                )
            return record
        recorded_at = _now()
        cursor = db.execute(
            "INSERT INTO candidates (run_pk, spec_pk, candidate_id, sha256, document, recorded_at)"
            " VALUES (?, ?, ?, ?, ?, ?)",
            (run.run_pk, spec_pk, candidate_id, digest, text, recorded_at),
        )
        assert cursor.lastrowid is not None
        return CandidateRecord(
            cursor.lastrowid, run.run_pk, spec_pk, candidate_id, digest, text, recorded_at
        )

    @staticmethod
    def _spec_pk(db: sqlite3.Connection, ref: Mapping[str, Any]) -> int:
        row = db.execute(
            "SELECT spec_pk FROM specs WHERE spec_id = ? AND revision = ?",
            (ref["spec_id"], ref["revision"]),
        ).fetchone()
        if row is None:
            raise DocumentRejected(
                f"spec {ref['spec_id']} revision {ref['revision']} is not recorded in this store"
            )
        return int(row[0])

    def create_job(
        self, run: Run, kind: str, input_hash: str, candidate: CandidateRecord | None = None
    ) -> JobRecord:
        """Record a job and its first status, queued, in one transaction."""
        if not kind:
            raise StoreError("a job needs a kind")
        if not SHA256_HEX.fullmatch(input_hash):
            raise StoreError(f"a job's input hash must be sha256 hex, got {input_hash!r}")
        recorded_at = _now()
        candidate_pk = None if candidate is None else candidate.candidate_pk
        with self._write() as db:
            cursor = db.execute(
                "INSERT INTO jobs (run_pk, kind, candidate_pk, input_hash, recorded_at)"
                " VALUES (?, ?, ?, ?, ?)",
                (run.run_pk, kind, candidate_pk, input_hash, recorded_at),
            )
            assert cursor.lastrowid is not None
            job = JobRecord(
                cursor.lastrowid, run.run_pk, kind, candidate_pk, input_hash, recorded_at
            )
            self._insert_event(db, run, job, "queued", None)
        return job

    def record_job_event(
        self, run: Run, job: JobRecord, status: str, reason: str | None = None
    ) -> JobEvent:
        """Append a status change. Which transitions are allowed is the job runner's rule (T13)."""
        with self._write() as db:
            return self._insert_event(db, run, job, status, reason)

    @staticmethod
    def _insert_event(
        db: sqlite3.Connection, run: Run, job: JobRecord, status: str, reason: str | None
    ) -> JobEvent:
        if status not in JOB_STATUSES:
            raise StoreError(f"job status {status!r} is not one of {JOB_STATUSES}")
        recorded_at = _now()
        cursor = db.execute(
            "INSERT INTO job_events (job_pk, run_pk, status, reason, recorded_at)"
            " VALUES (?, ?, ?, ?, ?)",
            (job.job_pk, run.run_pk, status, reason, recorded_at),
        )
        assert cursor.lastrowid is not None
        return JobEvent(cursor.lastrowid, job.job_pk, run.run_pk, status, reason, recorded_at)

    def record_result(
        self, run: Run, document: Mapping[str, Any], job: JobRecord | None = None
    ) -> ResultRecord:
        text, digest = _canonical("result", document)
        _require_valid("result", _schemas().errors(schemas.RESULT, document))
        job_pk = None if job is None else job.job_pk
        recorded_at = _now()
        with self._write() as db:
            spec_pk = self._spec_pk(db, document["spec"])
            row = db.execute(
                "SELECT candidate_pk FROM candidates WHERE spec_pk = ? AND candidate_id = ?",
                (spec_pk, document["candidate_id"]),
            ).fetchone()
            if row is None:
                raise DocumentRejected(
                    f"result names candidate {document['candidate_id']}, which is not recorded"
                )
            candidate_pk = int(row[0])
            status = document["status"]
            cursor = db.execute(
                "INSERT INTO results (run_pk, candidate_pk, job_pk, status, sha256, document,"
                " recorded_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
                (run.run_pk, candidate_pk, job_pk, status, digest, text, recorded_at),
            )
        assert cursor.lastrowid is not None
        return ResultRecord(
            cursor.lastrowid, run.run_pk, candidate_pk, job_pk, status, digest, text, recorded_at
        )

    def record_failure(
        self,
        run: Run,
        code: str,
        message: str,
        *,
        job: JobRecord | None = None,
        candidate: CandidateRecord | None = None,
        details: Mapping[str, str] | None = None,
    ) -> FailureRecord:
        """Record a failure classified by a REL-002 code from schemas/cemkit/v1/error-codes.json."""
        _require_valid("failure code", _schemas().errors(ERROR_CODES, code))
        if not message:
            raise DocumentRejected("a failure needs a message")
        details_json, _ = _canonical("failure details", dict(details or {}))
        if not all(isinstance(v, str) for v in (details or {}).values()):
            raise DocumentRejected("failure details must map names to text")
        job_pk = None if job is None else job.job_pk
        candidate_pk = None if candidate is None else candidate.candidate_pk
        recorded_at = _now()
        with self._write() as db:
            cursor = db.execute(
                "INSERT INTO failures (run_pk, job_pk, candidate_pk, code, message, details,"
                " recorded_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
                (run.run_pk, job_pk, candidate_pk, code, message, details_json, recorded_at),
            )
        assert cursor.lastrowid is not None
        return FailureRecord(
            cursor.lastrowid,
            run.run_pk,
            job_pk,
            candidate_pk,
            code,
            message,
            details_json,
            recorded_at,
        )

    def record_artifact(
        self, run: Run, source: bytes | Path, name: str, media_type: str | None = None
    ) -> ArtifactRecord:
        """Store the file (once per content) and then record this use of it.

        The file is published before the row is written, so a row never names a missing file; a
        crash in between leaves only an unreferenced file, which is harmless.
        """
        if not name:
            raise StoreError("an artifact needs a name")
        stored = (
            self._artifacts.put_bytes(source)
            if isinstance(source, bytes)
            else self._artifacts.put_file(source)
        )
        recorded_at = _now()
        with self._write() as db:
            cursor = db.execute(
                "INSERT INTO artifacts (run_pk, sha256, size_bytes, name, media_type, recorded_at)"
                " VALUES (?, ?, ?, ?, ?, ?)",
                (run.run_pk, stored.sha256, stored.size_bytes, name, media_type, recorded_at),
            )
        assert cursor.lastrowid is not None
        return ArtifactRecord(
            cursor.lastrowid,
            run.run_pk,
            stored.sha256,
            stored.size_bytes,
            name,
            media_type,
            recorded_at,
        )

    # --- reads -------------------------------------------------------------------------------

    def runs(self) -> tuple[Run, ...]:
        return tuple(_run(row) for row in self._db.execute(f"{_RUN} ORDER BY run_pk"))

    def job_events(self, job: JobRecord) -> tuple[JobEvent, ...]:
        rows = self._db.execute(f"{_EVENT} WHERE job_pk = ? ORDER BY event_pk", (job.job_pk,))
        return tuple(JobEvent(*row) for row in rows)

    def job_status(self, job: JobRecord) -> str:
        """The status in the job's most recent event."""
        events = self.job_events(job)
        if not events:
            raise StoreError(f"job {job.job_pk} has no status events")
        return events[-1].status

    def results(self, candidate: CandidateRecord) -> tuple[ResultRecord, ...]:
        rows = self._db.execute(
            f"{_RESULT} WHERE candidate_pk = ? ORDER BY result_pk", (candidate.candidate_pk,)
        )
        return tuple(ResultRecord(*row) for row in rows)

    def artifact_records(self) -> tuple[ArtifactRecord, ...]:
        return tuple(ArtifactRecord(*row) for row in self._db.execute(f"{_ARTIFACT} ORDER BY 1"))

    def run(self, run_pk: int) -> Run:
        """The run with this key; StoreError if there is none."""
        row = self._db.execute(f"{_RUN} WHERE run_pk = ?", (run_pk,)).fetchone()
        if row is None:
            raise StoreError(f"no run {run_pk}")
        return _run(row)

    def spec_records(self, spec_id: str) -> tuple[SpecRecord, ...]:
        """Every recorded revision of a spec, oldest first (empty if the spec is unknown)."""
        rows = self._db.execute(f"{_SPEC} WHERE spec_id = ? ORDER BY revision", (spec_id,))
        return tuple(SpecRecord(*row) for row in rows)

    def run_specs(self, run: Run) -> tuple[SpecRecord, ...]:
        rows = self._db.execute(f"{_SPEC} WHERE run_pk = ? ORDER BY spec_pk", (run.run_pk,))
        return tuple(SpecRecord(*row) for row in rows)

    def run_artifacts(self, run: Run) -> tuple[ArtifactRecord, ...]:
        rows = self._db.execute(
            f"{_ARTIFACT} WHERE run_pk = ? ORDER BY artifact_pk", (run.run_pk,)
        )
        return tuple(ArtifactRecord(*row) for row in rows)

    # --- job queue (T13; ORC-001..004) -------------------------------------------------------

    def enqueue_job(
        self,
        run: Run,
        kind: str,
        payload: Mapping[str, Any],
        *,
        resource_class: str = "light",
        max_retries: int = DEFAULT_MAX_RETRIES,
        candidate: CandidateRecord | None = None,
    ) -> JobSpec:
        """Record a job a worker will run; its input hash is the hash of its kind and payload."""
        if not kind:
            raise StoreError("a job needs a kind")
        if resource_class not in RESOURCE_CLASSES:
            raise StoreError(f"resource class {resource_class!r} is not one of {RESOURCE_CLASSES}")
        if isinstance(max_retries, bool) or not isinstance(max_retries, int) or max_retries < 0:
            raise StoreError(f"max_retries must be a non-negative integer, got {max_retries!r}")
        payload_json, _ = _canonical("job payload", dict(payload))
        hashed = sha256_hex(canonical_json({"kind": kind, "payload": json.loads(payload_json)}))
        candidate_pk = None if candidate is None else candidate.candidate_pk
        with self._write() as db:
            return queue.enqueue(
                db,
                run.run_pk,
                kind,
                payload_json,
                hashed,
                resource_class,
                max_retries,
                candidate_pk,
            )

    def claim_job(
        self,
        run: Run,
        worker_id: str,
        kinds: Sequence[str],
        *,
        now: float,
        lease_seconds: float,
        max_heavy: int = DEFAULT_MAX_HEAVY_JOBS,
    ) -> Claim | None:
        if not worker_id:
            raise StoreError("a worker needs an id")
        if lease_seconds <= 0 or max_heavy < 0:
            raise StoreError("lease_seconds must be positive and max_heavy non-negative")
        with self._write() as db:
            return queue.claim(db, run.run_pk, worker_id, kinds, now, lease_seconds, max_heavy)

    def heartbeat(self, run: Run, lease: Lease, *, now: float, lease_seconds: float) -> float:
        """Extend the lease; returns the new expiry. Raises LeaseLost if it is no longer held."""
        with self._write() as db:
            return queue.heartbeat(db, run.run_pk, lease, now, lease_seconds)

    def mark_publishing(self, run: Run, lease: Lease, reason: str) -> None:
        with self._write() as db:
            queue.mark_publishing(db, run.run_pk, lease, reason)

    def fail_attempt(
        self, run: Run, lease: Lease, code: str, reason: str, usage: Usage | None = None
    ) -> str:
        """End the attempt as failed with a REL-002 code; returns the new status (queued/failed)."""
        _require_valid("failure code", _schemas().errors(ERROR_CODES, code))
        if not reason:
            raise StoreError("a failed attempt needs a reason")
        with self._write() as db:
            return queue.end_attempt(
                db, run.run_pk, lease, "failed", code, reason, usage or Usage()
            )

    def complete_job(
        self, run: Run, lease: Lease, usage: Usage, files: Sequence[PublishedFile]
    ) -> None:
        """Mark a publishing job done with its published files. Each file must be stored already."""
        for item in files:
            if item.role not in ARTIFACT_ROLES or not item.name:
                raise StoreError(f"bad published file {item!r}")
            if not self._artifacts.contains(item.sha256):
                raise StoreError(f"artifact {item.sha256} is not in the artifact store")
        with self._write() as db:
            queue.complete(db, run.run_pk, lease, usage, files)

    def expired_leases(self, *, now: float) -> tuple[ExpiredLease, ...]:
        with self._write() as db:
            return queue.expired(db, now)

    def abandon_lease(self, run: Run, lease: Lease, *, now: float, reason: str) -> str:
        """End an expired attempt as lost (its worker died); requeue it if retries remain."""
        with self._write() as db:
            queue.require_expired(db, lease, now)
            return queue.end_attempt(
                db, run.run_pk, lease, "lost", LOST_WORKER_CODE, reason, Usage()
            )

    def adopt_lease(
        self, run: Run, lease: Lease, worker_id: str, *, now: float, lease_seconds: float
    ) -> Lease:
        with self._write() as db:
            return queue.adopt(db, run.run_pk, lease, worker_id, now, lease_seconds)

    def job_spec(self, job_pk: int) -> JobSpec:
        return queue.spec(self._db, job_pk)

    def job_statuses(self) -> dict[int, str]:
        rows = self._db.execute("SELECT job_pk, status FROM job_status ORDER BY job_pk")
        return {int(pk): str(value) for pk, value in rows}

    def attempts(self, job_pk: int) -> tuple[AttemptRecord, ...]:
        rows = self._db.execute(
            "SELECT a.attempt_pk, a.lease_pk, a.job_pk, a.run_pk, l.attempt, a.outcome, a.code,"
            " a.reason, a.peak_rss_bytes, a.cpu_seconds, a.wall_seconds, a.recorded_at"
            " FROM job_attempts AS a JOIN job_leases AS l ON l.lease_pk = a.lease_pk"
            " WHERE a.job_pk = ? ORDER BY a.attempt_pk",
            (job_pk,),
        )
        return tuple(
            AttemptRecord(
                attempt_pk=row[0],
                lease_pk=row[1],
                job_pk=row[2],
                run_pk=row[3],
                attempt=row[4],
                outcome=row[5],
                code=row[6],
                reason=row[7],
                usage=Usage(row[8], row[9], row[10]),
                recorded_at=row[11],
            )
            for row in rows
        )

    def failures(self, job_pk: int) -> tuple[FailureRecord, ...]:
        rows = self._db.execute(
            "SELECT failure_pk, run_pk, job_pk, candidate_pk, code, message, details, recorded_at"
            " FROM failures WHERE job_pk = ? ORDER BY failure_pk",
            (job_pk,),
        )
        return tuple(FailureRecord(*row) for row in rows)

    def job_artifacts(self, job_pk: int) -> tuple[tuple[str, ArtifactRecord], ...]:
        """(role, artifact) for every file the job published."""
        rows = self._db.execute(
            "SELECT ja.role, a.artifact_pk, a.run_pk, a.sha256, a.size_bytes, a.name,"
            " a.media_type, a.recorded_at FROM job_artifacts AS ja"
            " JOIN artifacts AS a ON a.artifact_pk = ja.artifact_pk"
            " WHERE ja.job_pk = ? ORDER BY ja.link_pk",
            (job_pk,),
        )
        return tuple((str(row[0]), ArtifactRecord(*row[1:])) for row in rows)

    def unfinished_jobs(self, kinds: Sequence[str]) -> int:
        """Jobs of these kinds that are queued, running or publishing."""
        if not kinds:
            return 0
        marks = ", ".join("?" for _ in kinds)
        row = self._db.execute(
            "SELECT count(*) FROM job_status AS js JOIN jobs AS j ON j.job_pk = js.job_pk"
            f" WHERE js.status IN ('queued', 'running', 'publishing') AND j.kind IN ({marks})",
            tuple(kinds),
        ).fetchone()
        return int(row[0])

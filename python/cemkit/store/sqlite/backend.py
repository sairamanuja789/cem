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

import sqlite3
from collections.abc import Iterable, Iterator, Mapping
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
    SHA256_HEX,
    ArtifactRecord,
    CandidateRecord,
    FailureRecord,
    JobEvent,
    JobRecord,
    ResultRecord,
    Run,
    RunMetadata,
    SpecRecord,
    versions_from_json,
)
from cemkit.store.sqlite import migrate

DATABASE_NAME: Final = "store.sqlite3"
ARTIFACTS_DIRECTORY: Final = "artifacts"
JOB_STATUSES: Final = ("queued", "running", "publishing", "done", "failed")  # ORC-004
ERROR_CODES: Final = "urn:cemkit:schema:v1:error-codes"
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

"""The store interface (ADR-009: code above the store depends on this, not on SQLite).

Every write is one transaction: it is recorded completely or not at all (REL-001). Nothing is ever
updated or deleted (STORE-001); a job's status changes by appending a JobEvent.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any, Protocol

from cemkit.store.artifacts import ArtifactStore
from cemkit.store.records import (
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
)


class Store(Protocol):
    @property
    def root(self) -> Path: ...

    @property
    def artifacts(self) -> ArtifactStore: ...

    def record_run(self, metadata: RunMetadata) -> Run: ...

    def record_spec(self, run: Run, document: Mapping[str, Any]) -> SpecRecord: ...

    def record_candidates(
        self, run: Run, documents: Iterable[Mapping[str, Any]]
    ) -> tuple[CandidateRecord, ...]: ...

    def create_job(
        self, run: Run, kind: str, input_hash: str, candidate: CandidateRecord | None = None
    ) -> JobRecord: ...

    def record_job_event(
        self, run: Run, job: JobRecord, status: str, reason: str | None = None
    ) -> JobEvent: ...

    def record_result(
        self, run: Run, document: Mapping[str, Any], job: JobRecord | None = None
    ) -> ResultRecord: ...

    def record_failure(
        self,
        run: Run,
        code: str,
        message: str,
        *,
        job: JobRecord | None = None,
        candidate: CandidateRecord | None = None,
        details: Mapping[str, str] | None = None,
    ) -> FailureRecord: ...

    def record_artifact(
        self, run: Run, source: bytes | Path, name: str, media_type: str | None = None
    ) -> ArtifactRecord: ...

    def runs(self) -> tuple[Run, ...]: ...

    def job_events(self, job: JobRecord) -> tuple[JobEvent, ...]: ...

    def job_status(self, job: JobRecord) -> str: ...

    def results(self, candidate: CandidateRecord) -> tuple[ResultRecord, ...]: ...

    def artifact_records(self) -> tuple[ArtifactRecord, ...]: ...

    def run(self, run_pk: int) -> Run: ...

    def spec_records(self, spec_id: str) -> tuple[SpecRecord, ...]: ...

    def run_specs(self, run: Run) -> tuple[SpecRecord, ...]: ...

    def run_artifacts(self, run: Run) -> tuple[ArtifactRecord, ...]: ...

    def close(self) -> None: ...

    # --- job queue (T13): every call is one transaction; see cemkit.store.sqlite.queue ---------

    def enqueue_job(
        self,
        run: Run,
        kind: str,
        payload: Mapping[str, Any],
        *,
        resource_class: str = ...,
        max_retries: int = ...,
        candidate: CandidateRecord | None = None,
    ) -> JobSpec: ...

    def claim_job(
        self,
        run: Run,
        worker_id: str,
        kinds: Sequence[str],
        *,
        now: float,
        lease_seconds: float,
        max_heavy: int = ...,
    ) -> Claim | None: ...

    def heartbeat(self, run: Run, lease: Lease, *, now: float, lease_seconds: float) -> float: ...

    def mark_publishing(self, run: Run, lease: Lease, reason: str) -> None: ...

    def fail_attempt(
        self, run: Run, lease: Lease, code: str, reason: str, usage: Usage | None = None
    ) -> str: ...

    def complete_job(
        self, run: Run, lease: Lease, usage: Usage, files: Sequence[PublishedFile]
    ) -> None: ...

    def expired_leases(self, *, now: float) -> tuple[ExpiredLease, ...]: ...

    def abandon_lease(self, run: Run, lease: Lease, *, now: float, reason: str) -> str: ...

    def adopt_lease(
        self, run: Run, lease: Lease, worker_id: str, *, now: float, lease_seconds: float
    ) -> Lease: ...

    def unfinished_jobs(self, kinds: Sequence[str]) -> int: ...

    def job_spec(self, job_pk: int) -> JobSpec: ...

    def job_statuses(self) -> dict[int, str]: ...

    def attempts(self, job_pk: int) -> tuple[AttemptRecord, ...]: ...

    def failures(self, job_pk: int) -> tuple[FailureRecord, ...]: ...

    def job_artifacts(self, job_pk: int) -> tuple[tuple[str, ArtifactRecord], ...]: ...

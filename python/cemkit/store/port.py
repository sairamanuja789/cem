"""The store interface (ADR-009: code above the store depends on this, not on SQLite).

Every write is one transaction: it is recorded completely or not at all (REL-001). Nothing is ever
updated or deleted (STORE-001); a job's status changes by appending a JobEvent.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import Any, Protocol

from cemkit.store.artifacts import ArtifactStore
from cemkit.store.records import (
    ArtifactRecord,
    CandidateRecord,
    FailureRecord,
    JobEvent,
    JobRecord,
    ResultRecord,
    Run,
    RunMetadata,
    SpecRecord,
)


class Store(Protocol):
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

    def close(self) -> None: ...

"""The provenance store (T12): specs, candidates, jobs, results, failures, artifacts; append-only.

All database access in cemkit goes through this package (python/CLAUDE.md). See docs/store.md.
"""

from __future__ import annotations

from cemkit.store.artifacts import ArtifactStore, StoredArtifact
from cemkit.store.errors import ArtifactCorrupt, DocumentRejected, MigrationError, StoreError
from cemkit.store.port import Store
from cemkit.store.provenance import (
    CONTAINER_DIGEST_ENV,
    capture_run_metadata,
    input_hash,
    read_git_commit,
)
from cemkit.store.records import (
    UNKNOWN,
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
from cemkit.store.sqlite import SqliteStore, open_store

__all__ = [
    "CONTAINER_DIGEST_ENV",
    "UNKNOWN",
    "ArtifactCorrupt",
    "ArtifactRecord",
    "ArtifactStore",
    "CandidateRecord",
    "DocumentRejected",
    "FailureRecord",
    "JobEvent",
    "JobRecord",
    "MigrationError",
    "ResultRecord",
    "Run",
    "RunMetadata",
    "SpecRecord",
    "SqliteStore",
    "Store",
    "StoreError",
    "StoredArtifact",
    "capture_run_metadata",
    "input_hash",
    "open_store",
    "read_git_commit",
]

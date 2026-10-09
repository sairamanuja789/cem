"""Immutable records returned by the store (STORE-001, STORE-002).

Records are frozen dataclasses. Documents are kept as canonical JSON text, so a record can never be
changed through a nested dict; `document()` returns a fresh parsed copy each time.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, fields
from typing import Any, Final, Literal

UNKNOWN: Final = "unknown"
"""The value recorded when a STORE-002 field cannot be determined. Never NULL, never omitted."""

type Unknown = Literal["unknown"]
type Versions = tuple[tuple[str, str], ...]
"""(name, semantic version) pairs, sorted by name. An empty tuple means "none used"."""

SEMVER = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
SHA256_HEX = re.compile(r"[0-9a-f]{64}")
GIT_COMMIT = re.compile(r"[0-9a-f]{40}|[0-9a-f]{64}")
CONTAINER_DIGEST = re.compile(r"sha256:[0-9a-f]{64}")


def _check(name: str, value: str, pattern: re.Pattern[str], allow_unknown: bool = True) -> None:
    if allow_unknown and value == UNKNOWN:
        return
    if not isinstance(value, str) or not pattern.fullmatch(value):
        raise ValueError(f"{name}: {value!r} does not match {pattern.pattern}")


def _check_versions(name: str, value: Versions | Unknown) -> None:
    if value == UNKNOWN:
        return
    if not isinstance(value, tuple):
        raise ValueError(f"{name}: expected a tuple of (name, version) pairs or {UNKNOWN!r}")
    names = [pair[0] for pair in value]
    if names != sorted(set(names)):
        raise ValueError(f"{name}: names must be unique and sorted")
    for item, version in value:
        if not item:
            raise ValueError(f"{name}: empty name")
        _check(f"{name}[{item}]", version, SEMVER, allow_unknown=False)


def versions_to_json(value: Versions | Unknown) -> str:
    if value == UNKNOWN:
        return UNKNOWN
    return json.dumps(dict(value), sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def versions_from_json(text: str) -> Versions | Unknown:
    if text == UNKNOWN:
        return UNKNOWN
    loaded = json.loads(text)
    return tuple(sorted((str(k), str(v)) for k, v in loaded.items()))


@dataclass(frozen=True, slots=True)
class RunMetadata:
    """What STORE-002 requires to reproduce a run. Every record in the store points to one run."""

    kernel_version: str
    plugin_versions: Versions | Unknown
    model_versions: Versions | Unknown
    git_commit: str
    container_digest: str
    input_hash: str

    def __post_init__(self) -> None:
        _check("kernel_version", self.kernel_version, SEMVER)
        _check_versions("plugin_versions", self.plugin_versions)
        _check_versions("model_versions", self.model_versions)
        _check("git_commit", self.git_commit, GIT_COMMIT)
        _check("container_digest", self.container_digest, CONTAINER_DIGEST)
        _check("input_hash", self.input_hash, SHA256_HEX, allow_unknown=False)

    def plugin_versions_json(self) -> str:
        return versions_to_json(self.plugin_versions)

    def model_versions_json(self) -> str:
        return versions_to_json(self.model_versions)

    def unknown_fields(self) -> tuple[str, ...]:
        """Names of the fields recorded as unknown, in declaration order (for reports)."""
        return tuple(f.name for f in fields(self) if getattr(self, f.name) == UNKNOWN)


@dataclass(frozen=True, slots=True)
class Run:
    run_pk: int
    recorded_at: str
    metadata: RunMetadata


@dataclass(frozen=True, slots=True)
class SpecRecord:
    spec_pk: int
    run_pk: int
    spec_id: str
    revision: int
    sha256: str
    document_json: str
    recorded_at: str

    def document(self) -> Any:
        return json.loads(self.document_json)


@dataclass(frozen=True, slots=True)
class CandidateRecord:
    candidate_pk: int
    run_pk: int
    spec_pk: int
    candidate_id: str
    sha256: str
    document_json: str
    recorded_at: str

    def document(self) -> Any:
        return json.loads(self.document_json)


@dataclass(frozen=True, slots=True)
class JobRecord:
    job_pk: int
    run_pk: int
    kind: str
    candidate_pk: int | None
    input_hash: str
    recorded_at: str


@dataclass(frozen=True, slots=True)
class JobEvent:
    event_pk: int
    job_pk: int
    run_pk: int
    status: str
    reason: str | None
    recorded_at: str


@dataclass(frozen=True, slots=True)
class ResultRecord:
    result_pk: int
    run_pk: int
    candidate_pk: int
    job_pk: int | None
    status: str
    sha256: str
    document_json: str
    recorded_at: str

    def document(self) -> Any:
        return json.loads(self.document_json)


@dataclass(frozen=True, slots=True)
class FailureRecord:
    failure_pk: int
    run_pk: int
    job_pk: int | None
    candidate_pk: int | None
    code: str
    message: str
    details_json: str
    recorded_at: str

    def details(self) -> dict[str, str]:
        loaded: dict[str, str] = json.loads(self.details_json)
        return loaded


@dataclass(frozen=True, slots=True)
class ArtifactRecord:
    artifact_pk: int
    run_pk: int
    sha256: str
    size_bytes: int
    name: str
    media_type: str | None
    recorded_at: str

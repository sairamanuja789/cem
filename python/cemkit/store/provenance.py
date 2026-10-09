"""Run-metadata capture (STORE-002) and canonical hashing.

What is captured, and where it comes from:

| Field              | Source                                                                    |
| ------------------ | ------------------------------------------------------------------------- |
| kernel_version     | passed by the caller (the kernel binding reports it; T11)                 |
| plugin_versions    | passed by the caller, {plugin name: semver}                               |
| model_versions     | passed by the caller, {model name: semver} (PHY-004)                      |
| git_commit         | read from the repository's .git directory (HEAD, loose refs, packed-refs) |
| container_digest   | the CEMKIT_CONTAINER_DIGEST environment variable, set by scripts/dev.sh   |
| input_hash         | sha256 of the canonical JSON of the run's inputs (or of raw bytes)        |

Anything that cannot be determined is recorded as the text "unknown" (UNKNOWN), never dropped. To
say "unknown" a caller passes None; passing the string "unknown" is refused so that a typo cannot
look like a real version. An empty mapping means "no plugins/models used", which is known.

git_commit is HEAD only: uncommitted changes in the working tree are not detected (reading the index
would need git itself, and only the orchestration runner may start processes). See docs/store.md.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from cemkit.store.records import GIT_COMMIT, UNKNOWN, RunMetadata, Unknown, Versions

CONTAINER_DIGEST_ENV = "CEMKIT_CONTAINER_DIGEST"
REPO_ROOT = Path(__file__).resolve().parents[3]
_REF = re.compile(r"refs/[A-Za-z0-9._/-]+")


def canonical_json(document: Any) -> str:
    """Sorted keys, no whitespace, UTF-8 text. Raises ValueError for NaN or infinity."""
    return json.dumps(
        document, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
    )


def sha256_hex(data: str | bytes) -> str:
    return hashlib.sha256(data.encode("utf-8") if isinstance(data, str) else data).hexdigest()


def input_hash(inputs: Any) -> str:
    """sha256 of raw bytes, or of the canonical JSON of any JSON-compatible value."""
    if isinstance(inputs, bytes):
        return sha256_hex(inputs)
    return sha256_hex(canonical_json(inputs))


def _read(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeDecodeError):
        return None


def _git_dir(repo_root: Path) -> Path | None:
    dot_git = repo_root / ".git"
    if dot_git.is_dir():
        return dot_git
    text = _read(dot_git) if dot_git.is_file() else None
    if text is None or not text.startswith("gitdir:"):
        return None
    target = Path(text.removeprefix("gitdir:").strip())
    return target if target.is_absolute() else (repo_root / target).resolve()


def read_git_commit(repo_root: Path) -> str | None:
    """The commit HEAD points to, or None if it cannot be read. Never starts a process."""
    git_dir = _git_dir(repo_root)
    head = _read(git_dir / "HEAD") if git_dir is not None else None
    if git_dir is None or head is None:
        return None
    if GIT_COMMIT.fullmatch(head):
        return head
    ref = head.removeprefix("ref:").strip()
    if not head.startswith("ref:") or not _REF.fullmatch(ref) or ".." in ref:
        return None
    common = _read(git_dir / "commondir")
    common_dir = (git_dir / common).resolve() if common else git_dir
    for directory in (git_dir, common_dir):
        loose = _read(directory / ref)
        if loose is not None:
            return loose if GIT_COMMIT.fullmatch(loose) else None
    for line in (_read(common_dir / "packed-refs") or "").splitlines():
        commit, _, name = line.partition(" ")
        if name == ref and GIT_COMMIT.fullmatch(commit):
            return commit
    return None


def _versions(name: str, value: Mapping[str, str] | None) -> Versions | Unknown:
    if value is None:
        return UNKNOWN
    if any(v == UNKNOWN for v in value.values()):
        raise ValueError(f"{name}: pass None for unknown versions, not the string {UNKNOWN!r}")
    return tuple(sorted(value.items()))


def capture_run_metadata(
    inputs: Any,
    *,
    kernel_version: str | None = None,
    plugin_versions: Mapping[str, str] | None = None,
    model_versions: Mapping[str, str] | None = None,
    environ: Mapping[str, str] | None = None,
    repo_root: Path | None = None,
) -> RunMetadata:
    """Collect the STORE-002 fields for a run whose inputs are `inputs`.

    Raises ValueError if a value is present but malformed (for example a container digest that is
    not "sha256:<64 hex>"): a wrong value is an error, only a missing one is "unknown".
    """
    if kernel_version == UNKNOWN:
        raise ValueError(f"kernel_version: pass None for unknown, not the string {UNKNOWN!r}")
    env = os.environ if environ is None else environ
    digest = env.get(CONTAINER_DIGEST_ENV, "").strip()
    commit = read_git_commit(REPO_ROOT if repo_root is None else repo_root)
    return RunMetadata(
        kernel_version=UNKNOWN if kernel_version is None else kernel_version,
        plugin_versions=_versions("plugin_versions", plugin_versions),
        model_versions=_versions("model_versions", model_versions),
        git_commit=UNKNOWN if commit is None else commit,
        container_digest=digest or UNKNOWN,
        input_hash=input_hash(inputs),
    )

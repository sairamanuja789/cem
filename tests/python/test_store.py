"""T12: the provenance store (python/cemkit/store/).

Requirements: STORE-001 (everything recorded, append-only), STORE-002 (every record carries kernel,
plugin and model versions, git commit, container digest and input hash), STORE-003 (artifacts named
by content hash, identical files stored once), STORE-004 (versioned schema, forward migrations),
REL-001 (no crash in the orchestration layer corrupts the store).

The append-only tests open a raw sqlite3 connection on purpose: they show that the database itself
refuses the change, not only the store API.
"""

from __future__ import annotations

import copy
import os
import signal
import sqlite3
import subprocess
import sys
import textwrap
import time
from collections import Counter
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest

from cemkit import schemas as cs
from cemkit.store import (
    UNKNOWN,
    ArtifactCorrupt,
    ArtifactStore,
    DocumentRejected,
    MigrationError,
    RunMetadata,
    SqliteStore,
    StoreError,
    capture_run_metadata,
    input_hash,
    open_store,
    read_git_commit,
)
from cemkit.store.sqlite import migrate as mig

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "examples" / "axial_120.yaml"
RECORD_TABLES = (
    "specs",
    "candidates",
    "jobs",
    "job_events",
    "results",
    "failures",
    "artifacts",
)
ALL_TABLES = ("runs", *RECORD_TABLES)
DIGEST = "sha256:" + "ab" * 32
COMMIT = "0123456789abcdef0123456789abcdef01234567"

Doc = dict[str, Any]


def spec_doc() -> Doc:
    loaded = cs.load_yaml(EXAMPLE.read_text(encoding="utf-8"))
    assert isinstance(loaded, dict)
    return loaded


def candidate_doc(candidate_id: str = "c-000001") -> Doc:
    return {
        "schema_version": "1.0.0",
        "candidate_id": candidate_id,
        "spec": {"spec_id": "axial-120", "revision": 1},
        "family": "fans.axial_ducted",
        "parameters": {
            "tip_diameter": {"value": 0.119, "unit": "m"},
            "blade_count": {"value": 7, "unit": "1"},
        },
    }


def result_doc(candidate_id: str = "c-000001", failed: bool = False) -> Doc:
    document: Doc = {
        "schema_version": "1.0.0",
        "candidate_id": candidate_id,
        "spec": {"spec_id": "axial-120", "revision": 1},
        "kernel_version": "0.1.0",
        "status": "failed" if failed else "ok",
    }
    if failed:
        document["failure"] = {"code": "out_of_validity", "message": "tip speed above range"}
    else:
        document["metrics"] = {
            "fan_static_pressure": {
                "value": 150.0,
                "unit": "Pa",
                "fidelity": "l1_predicted",
                "model": {"name": "fans.axial_ducted.l1", "version": "1.0.0"},
            }
        }
    return document


def metadata(**overrides: Any) -> RunMetadata:
    arguments: dict[str, Any] = {
        "kernel_version": "0.1.0",
        "plugin_versions": {"fans.axial_ducted": "0.1.0"},
        "model_versions": {"fans.axial_ducted.l1": "1.0.0"},
        "environ": {"CEMKIT_CONTAINER_DIGEST": DIGEST},
    }
    arguments.update(overrides)
    return capture_run_metadata({"spec": "axial-120"}, **arguments)


@pytest.fixture
def store(tmp_path: Path) -> Iterator[SqliteStore]:
    with open_store(tmp_path / "store") as opened:
        yield opened


def raw(path: Path) -> sqlite3.Connection:
    return sqlite3.connect(path, autocommit=True)


def schema_dump(path: Path) -> list[tuple[str, str, str]]:
    with raw(path) as connection:
        rows = connection.execute(
            "SELECT type, name, sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY type, name"
        ).fetchall()
    connection.close()
    return [(str(t), str(n), str(s)) for t, n, s in rows]


# --- STORE-004: forward migrations ---------------------------------------------------------------


@pytest.mark.req("STORE-004")
def test_migration_from_empty_creates_every_table_and_records_the_version(tmp_path: Path) -> None:
    database = tmp_path / "s.sqlite3"
    migrations = mig.load_migrations()
    assert [m.version for m in migrations] == list(range(1, len(migrations) + 1))
    assert mig.migrate(database) == tuple(m.version for m in migrations)
    assert mig.current_version(database) == len(migrations)
    tables = {name for kind, name, _ in schema_dump(database) if kind == "table"}
    assert set(ALL_TABLES) | {"schema_version"} <= tables
    with raw(database) as connection:
        recorded = connection.execute("SELECT version, sha256 FROM schema_version").fetchall()
    connection.close()
    assert recorded == [(m.version, m.sha256) for m in migrations]


@pytest.mark.req("STORE-004")
def test_migration_is_idempotent(tmp_path: Path) -> None:
    database = tmp_path / "s.sqlite3"
    mig.migrate(database)
    before = schema_dump(database)
    assert mig.migrate(database) == ()
    assert mig.migrate(database) == ()
    assert schema_dump(database) == before
    with open_store(tmp_path / "root") as first:
        first.record_run(metadata())
    with open_store(tmp_path / "root") as second:
        assert len(second.runs()) == 1


@pytest.mark.req("STORE-004")
def test_an_edited_applied_migration_is_refused(tmp_path: Path) -> None:
    database = tmp_path / "s.sqlite3"
    mig.migrate(database)
    edited = list(mig.load_migrations())
    edited[0] = mig.Migration(edited[0].version, edited[0].name, edited[0].sql + "\n-- edit\n")
    with pytest.raises(MigrationError, match="changed after it was applied"):
        mig.migrate(database, tuple(edited))


@pytest.mark.req("STORE-004")
def test_a_database_newer_than_the_code_is_refused(tmp_path: Path) -> None:
    database = tmp_path / "s.sqlite3"
    mig.migrate(database)
    older = mig.load_migrations()[:-1]
    if not older:
        # Only one migration exists: simulate the newer database by adding one more.
        extra = mig.Migration(2, "extra", "CREATE TABLE extra (x INTEGER) STRICT;")
        mig.migrate(database, (*mig.load_migrations(), extra))
        older = mig.load_migrations()
    with pytest.raises(MigrationError, match="newer than this code"):
        mig.migrate(database, older)


@pytest.mark.req("STORE-004")
def test_a_failing_migration_leaves_the_previous_version(tmp_path: Path) -> None:
    database = tmp_path / "s.sqlite3"
    mig.migrate(database)
    version = mig.current_version(database)
    broken = mig.Migration(
        version + 1,
        "broken",
        "CREATE TABLE half (x INTEGER) STRICT; INSERT INTO nowhere VALUES (1);",
    )
    with pytest.raises(sqlite3.OperationalError):
        mig.migrate(database, (*mig.load_migrations(), broken))
    assert mig.current_version(database) == version
    assert "half" not in {name for _, name, _ in schema_dump(database)}


@pytest.mark.req("STORE-004")
@pytest.mark.parametrize(
    "names",
    [("0001_a.sql", "0003_b.sql"), ("0002_a.sql",), ("1_a.sql",), ("0001_A.sql",)],
    ids=["gap", "not from one", "unpadded", "upper case"],
)
def test_migration_files_must_be_numbered_from_one_without_gaps(
    tmp_path: Path, names: tuple[str, ...]
) -> None:
    for name in names:
        (tmp_path / name).write_text("SELECT 1;", encoding="utf-8")
    with pytest.raises(MigrationError):
        mig.load_migrations(tmp_path)


@pytest.mark.req("STORE-004")
def test_the_schema_version_table_is_append_only(tmp_path: Path) -> None:
    database = tmp_path / "s.sqlite3"
    mig.migrate(database)
    with raw(database) as connection:
        for statement in ("UPDATE schema_version SET sha256 = 'x'", "DELETE FROM schema_version"):
            with pytest.raises(sqlite3.DatabaseError, match="append-only"):
                connection.execute(statement)
    connection.close()


@pytest.mark.req("REL-001")
def test_the_store_runs_in_wal_mode_with_foreign_keys(store: SqliteStore) -> None:
    assert store.pragma("journal_mode") == "wal"
    assert store.pragma("foreign_keys") == 1


# --- STORE-001: append-only ----------------------------------------------------------------------


def populated(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    store.record_spec(run, spec_doc())
    (candidate,) = store.record_candidates(run, [candidate_doc()])
    job = store.create_job(run, "l1_evaluation", input_hash("job"), candidate)
    store.record_result(run, result_doc(), job)
    store.record_failure(run, "mesh_failed", "no valid mesh", job=job, candidate=candidate)
    store.record_artifact(run, b"solid blade", "blade.step", "model/step")


@pytest.mark.req("STORE-001")
@pytest.mark.parametrize("table", ALL_TABLES)
def test_rows_cannot_be_updated_or_deleted(store: SqliteStore, table: str) -> None:
    populated(store)
    with raw(store.database) as connection:
        assert connection.execute(f"SELECT count(*) FROM {table}").fetchone()[0] >= 1
        column = connection.execute(f"SELECT name FROM pragma_table_info('{table}')").fetchone()[0]
        with pytest.raises(sqlite3.DatabaseError, match="append-only"):
            connection.execute(f"UPDATE {table} SET {column} = {column}")
        with pytest.raises(sqlite3.DatabaseError, match="append-only"):
            connection.execute(f"DELETE FROM {table}")
        with pytest.raises(sqlite3.DatabaseError, match="append-only"):
            connection.execute(f"INSERT OR REPLACE INTO {table} SELECT * FROM {table}")
    connection.close()


@pytest.mark.req("STORE-001")
def test_result_rows_cannot_be_updated_or_deleted(store: SqliteStore) -> None:
    populated(store)
    with raw(store.database) as connection:
        for statement in (
            "UPDATE results SET status = 'failed'",
            "UPDATE results SET document = '{}' WHERE result_pk = 1",
            "DELETE FROM results",
            "DELETE FROM results WHERE result_pk = 1",
        ):
            with pytest.raises(sqlite3.DatabaseError, match="append-only"):
                connection.execute(statement)
        assert connection.execute("SELECT status FROM results").fetchall() == [("ok",)]
    connection.close()


@pytest.mark.req("STORE-001")
def test_job_status_changes_are_new_rows(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    job = store.create_job(run, "cfd", input_hash({"case": 1}))
    assert store.job_status(job) == "queued"
    store.record_job_event(run, job, "running")
    store.record_job_event(run, job, "failed", reason="solver diverged")
    assert store.job_status(job) == "failed"
    events = store.job_events(job)
    assert [e.status for e in events] == ["queued", "running", "failed"]
    assert events[-1].reason == "solver diverged"
    assert all(e.job_pk == job.job_pk and e.run_pk == run.run_pk for e in events)
    with pytest.raises(StoreError):
        store.record_job_event(run, job, "exploded")


@pytest.mark.req("STORE-001")
def test_every_kind_of_record_is_stored_and_read_back(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    spec = store.record_spec(run, spec_doc())
    assert (spec.spec_id, spec.revision) == ("axial-120", 1)
    assert spec.document() == spec_doc()
    candidates = store.record_candidates(run, [candidate_doc("c-1"), candidate_doc("c-2")])
    assert [c.candidate_id for c in candidates] == ["c-1", "c-2"]
    assert all(c.spec_pk == spec.spec_pk for c in candidates)
    ok = store.record_result(run, result_doc("c-1"))
    failed = store.record_result(run, result_doc("c-1", failed=True))
    assert (ok.status, failed.status) == ("ok", "failed")
    assert store.results(candidates[0]) == (ok, failed)
    assert store.results(candidates[1]) == ()
    failure = store.record_failure(run, "sim_untrusted", "diverged", details={"step": "12"})
    assert failure.details() == {"step": "12"}
    assert (failure.job_pk, failure.candidate_pk) == (None, None)
    artifact = store.record_artifact(run, b"mesh", "mesh.msh")
    assert store.artifacts.read_bytes(artifact.sha256) == b"mesh"
    assert store.runs() == (run,)


@pytest.mark.req("STORE-001")
def test_records_are_immutable(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    with pytest.raises(AttributeError):
        run.run_pk = 5  # type: ignore[misc]
    with pytest.raises(AttributeError):
        run.metadata.git_commit = COMMIT  # type: ignore[misc]


@pytest.mark.req("STORE-001")
def test_a_spec_revision_is_immutable(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    first = store.record_spec(run, spec_doc())
    assert store.record_spec(run, spec_doc()) == first
    changed = spec_doc()
    changed["title"] = "another fan"
    with pytest.raises(DocumentRejected, match="immutable"):
        store.record_spec(run, changed)


@pytest.mark.req("STORE-001")
def test_a_candidate_is_immutable(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    store.record_spec(run, spec_doc())
    (first,) = store.record_candidates(run, [candidate_doc()])
    assert store.record_candidates(run, [candidate_doc()]) == (first,)
    changed = candidate_doc()
    changed["parameters"]["blade_count"]["value"] = 9
    with pytest.raises(DocumentRejected, match="immutable"):
        store.record_candidates(run, [changed])


@pytest.mark.req("STORE-001")
@pytest.mark.parametrize(
    "action",
    [
        "spec_invalid",
        "spec_nan",
        "candidate_invalid",
        "candidate_without_spec",
        "result_invalid",
        "result_without_candidate",
        "failure_code",
        "failure_message",
        "job_kind",
        "job_hash",
    ],
)
def test_invalid_records_are_rejected_and_nothing_is_written(
    store: SqliteStore, action: str
) -> None:
    run = store.record_run(metadata())
    store.record_spec(run, spec_doc())
    store.record_candidates(run, [candidate_doc()])
    before = {table: count(store.database, table) for table in ALL_TABLES}
    with pytest.raises(StoreError):
        match action:
            case "spec_invalid":
                broken = spec_doc()
                broken.pop("family")
                store.record_spec(run, broken)
            case "spec_nan":
                broken = spec_doc()
                broken["revision"] = 2
                broken["air"]["density"]["value"] = float("nan")
                store.record_spec(run, broken)
            case "candidate_invalid":
                store.record_candidates(run, [candidate_doc("c-2"), {"candidate_id": "c-3"}])
            case "candidate_without_spec":
                orphan = candidate_doc("c-4")
                orphan["spec"]["revision"] = 7
                store.record_candidates(run, [orphan])
            case "result_invalid":
                store.record_result(run, {**result_doc(), "status": "maybe"})
            case "result_without_candidate":
                store.record_result(run, result_doc("c-unknown"))
            case "failure_code":
                store.record_failure(run, "failure_cluster", "not a code")
            case "failure_message":
                store.record_failure(run, "mesh_failed", "")
            case "job_kind":
                store.create_job(run, "", input_hash("x"))
            case "job_hash":
                store.create_job(run, "cfd", "not-a-hash")
    assert {table: count(store.database, table) for table in ALL_TABLES} == before


def count(database: Path, table: str, where: str = "1") -> int:
    with raw(database) as connection:
        value = connection.execute(f"SELECT count(*) FROM {table} WHERE {where}").fetchone()[0]
    connection.close()
    assert isinstance(value, int)
    return value


# --- STORE-002: run metadata ---------------------------------------------------------------------

STORE_002_COLUMNS = (
    "kernel_version",
    "plugin_versions",
    "model_versions",
    "git_commit",
    "container_digest",
    "input_hash",
)


@pytest.mark.req("STORE-002")
def test_every_run_record_carries_the_store_002_fields(store: SqliteStore) -> None:
    store.record_run(metadata())
    store.record_run(capture_run_metadata({"x": 1}, environ={}, repo_root=Path("/nonexistent")))
    with raw(store.database) as connection:
        columns = {row[1]: row for row in connection.execute("PRAGMA table_info(runs)")}
        rows = connection.execute(f"SELECT {', '.join(STORE_002_COLUMNS)} FROM runs").fetchall()
    connection.close()
    for column in STORE_002_COLUMNS:
        assert columns[column][3] == 1, f"{column} must be NOT NULL"
    assert len(rows) == 2
    for row in rows:
        assert all(isinstance(value, str) and value for value in row)
    assert rows[1][:5] == (UNKNOWN,) * 5


@pytest.mark.req("STORE-002")
@pytest.mark.parametrize("table", RECORD_TABLES)
def test_every_record_references_its_run(store: SqliteStore, table: str) -> None:
    populated(store)
    with raw(store.database) as connection:
        info = {row[1]: row for row in connection.execute(f"PRAGMA table_info({table})")}
        keys = connection.execute(f"PRAGMA foreign_key_list({table})").fetchall()
        orphans = connection.execute(
            f"SELECT count(*) FROM {table} WHERE run_pk NOT IN (SELECT run_pk FROM runs)"
        ).fetchone()[0]
    connection.close()
    assert info["run_pk"][3] == 1, "run_pk must be NOT NULL"
    assert any(k[2] == "runs" and k[3] == "run_pk" for k in keys)
    assert orphans == 0


@pytest.mark.req("STORE-002")
@pytest.mark.parametrize("column", STORE_002_COLUMNS)
def test_the_database_refuses_a_run_without_a_store_002_field(
    store: SqliteStore, column: str
) -> None:
    good = {
        "recorded_at": "2026-10-10T00:00:00+00:00",
        "kernel_version": "0.1.0",
        "plugin_versions": "{}",
        "model_versions": "{}",
        "git_commit": COMMIT,
        "container_digest": DIGEST,
        "input_hash": "0" * 64,
    }
    names = ", ".join(good)
    marks = ", ".join("?" for _ in good)
    with raw(store.database) as connection:
        for bad in (None, ""):
            values = {**good, column: bad}
            with pytest.raises(sqlite3.IntegrityError):
                connection.execute(
                    f"INSERT INTO runs ({names}) VALUES ({marks})", tuple(values.values())
                )
    connection.close()


@pytest.mark.req("STORE-002")
def test_known_values_are_captured(tmp_path: Path) -> None:
    repo = tmp_path / "repo"
    (repo / ".git").mkdir(parents=True)
    (repo / ".git" / "HEAD").write_text(f"{COMMIT}\n", encoding="utf-8")
    meta = capture_run_metadata(
        {"b": 2, "a": [1.5, "x"]},
        kernel_version="0.1.0",
        plugin_versions={"fans.axial_ducted": "0.2.0", "fans.centrifugal": "0.1.0"},
        model_versions={},
        environ={"CEMKIT_CONTAINER_DIGEST": DIGEST},
        repo_root=repo,
    )
    assert meta.kernel_version == "0.1.0"
    assert meta.plugin_versions == (("fans.axial_ducted", "0.2.0"), ("fans.centrifugal", "0.1.0"))
    assert meta.model_versions == ()  # no models used: known and empty, not unknown
    assert meta.git_commit == COMMIT
    assert meta.container_digest == DIGEST
    assert meta.input_hash == input_hash({"a": [1.5, "x"], "b": 2})


@pytest.mark.req("STORE-002")
def test_values_that_cannot_be_determined_are_recorded_as_unknown(tmp_path: Path) -> None:
    meta = capture_run_metadata({}, environ={"CEMKIT_CONTAINER_DIGEST": ""}, repo_root=tmp_path)
    assert meta.kernel_version == UNKNOWN
    assert meta.plugin_versions == UNKNOWN
    assert meta.model_versions == UNKNOWN
    assert meta.git_commit == UNKNOWN
    assert meta.container_digest == UNKNOWN
    assert meta.unknown_fields() == (
        "kernel_version",
        "plugin_versions",
        "model_versions",
        "git_commit",
        "container_digest",
    )
    assert len(meta.input_hash) == 64


@pytest.mark.req("STORE-002")
def test_the_container_digest_comes_from_the_environment_by_default(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("CEMKIT_CONTAINER_DIGEST", DIGEST)
    assert capture_run_metadata({}).container_digest == DIGEST
    monkeypatch.delenv("CEMKIT_CONTAINER_DIGEST")
    assert capture_run_metadata({}).container_digest == UNKNOWN


@pytest.mark.req("STORE-002")
@pytest.mark.parametrize(
    "overrides",
    [
        {"environ": {"CEMKIT_CONTAINER_DIGEST": "latest"}},
        {"kernel_version": ""},
        {"kernel_version": "unknown"},
        {"plugin_versions": {"": "1.0.0"}},
        {"model_versions": {"m": ""}},
    ],
    ids=["digest", "empty kernel", "kernel spelled unknown", "empty plugin", "empty model"],
)
def test_malformed_metadata_is_rejected(overrides: dict[str, Any]) -> None:
    with pytest.raises(ValueError):
        metadata(**overrides)


@pytest.mark.req("STORE-002")
def test_run_metadata_round_trips_through_the_store(store: SqliteStore) -> None:
    known = metadata()
    unknown = capture_run_metadata({}, environ={}, repo_root=Path("/nonexistent"))
    stored = (store.record_run(known), store.record_run(unknown))
    assert tuple(r.metadata for r in store.runs()) == (known, unknown)
    assert store.runs() == stored


@pytest.mark.req("STORE-002")
def test_the_input_hash_is_canonical() -> None:
    assert input_hash({"a": 1, "b": [1, 2]}) == input_hash({"b": [1, 2], "a": 1})
    assert input_hash({"a": 1}) != input_hash({"a": 2})
    assert input_hash(b"\x00raw") == input_hash(b"\x00raw")
    with pytest.raises(ValueError):
        input_hash({"a": float("inf")})


def write_git(repo: Path, head: str, files: dict[str, str] | None = None) -> Path:
    git = repo / ".git"
    git.mkdir(parents=True)
    (git / "HEAD").write_text(head, encoding="utf-8")
    for name, text in (files or {}).items():
        (git / name).parent.mkdir(parents=True, exist_ok=True)
        (git / name).write_text(text, encoding="utf-8")
    return git


@pytest.mark.req("STORE-002")
def test_git_commit_is_read_from_a_branch_ref(tmp_path: Path) -> None:
    write_git(tmp_path, "ref: refs/heads/main\n", {"refs/heads/main": COMMIT + "\n"})
    assert read_git_commit(tmp_path) == COMMIT


@pytest.mark.req("STORE-002")
def test_git_commit_is_read_from_packed_refs(tmp_path: Path) -> None:
    other = "f" * 40
    packed = (
        f"# pack-refs with: peeled\n{other} refs/heads/other\n{COMMIT} refs/heads/main\n^{other}\n"
    )
    write_git(tmp_path, "ref: refs/heads/main\n", {"packed-refs": packed})
    assert read_git_commit(tmp_path) == COMMIT


@pytest.mark.req("STORE-002")
def test_git_commit_is_read_through_a_gitdir_file_and_commondir(tmp_path: Path) -> None:
    main = write_git(tmp_path / "main", COMMIT, {"refs/heads/feature": "e" * 40})
    worktree_git = main / "worktrees" / "wt"
    worktree_git.mkdir(parents=True)
    (worktree_git / "HEAD").write_text("ref: refs/heads/feature\n", encoding="utf-8")
    (worktree_git / "commondir").write_text("../..\n", encoding="utf-8")
    wt = tmp_path / "wt"
    wt.mkdir()
    (wt / ".git").write_text(f"gitdir: {worktree_git}\n", encoding="utf-8")
    assert read_git_commit(wt) == "e" * 40


@pytest.mark.req("STORE-002")
@pytest.mark.parametrize(
    "head, files",
    [
        ("ref: refs/heads/missing\n", {}),
        ("not a commit\n", {}),
        ("ref: refs/heads/main\n", {"refs/heads/main": "garbage\n"}),
        ("ref: ../../etc/passwd\n", {}),
    ],
    ids=["missing ref", "garbage head", "garbage ref", "ref outside refs/"],
)
def test_an_unreadable_git_commit_is_none(tmp_path: Path, head: str, files: dict[str, str]) -> None:
    write_git(tmp_path, head, files)
    assert read_git_commit(tmp_path) is None
    assert read_git_commit(tmp_path / "no-repo") is None


# --- STORE-003: content-addressed artifacts ------------------------------------------------------


def stored_files(root: Path) -> list[Path]:
    return sorted(p for p in (root / "sha256").rglob("*") if p.is_file())


@pytest.mark.req("STORE-003")
def test_duplicate_artifacts_are_stored_once(tmp_path: Path) -> None:
    artifacts = ArtifactStore(tmp_path / "a")
    first = artifacts.put_bytes(b"same content")
    second = artifacts.put_bytes(b"same content")
    source = tmp_path / "copy.bin"
    source.write_bytes(b"same content")
    third = artifacts.put_file(source)
    assert first.sha256 == second.sha256 == third.sha256
    assert (first.created, second.created, third.created) == (True, False, False)
    assert stored_files(tmp_path / "a") == [artifacts.path_of(first.sha256)]
    assert list((tmp_path / "a" / "tmp").iterdir()) == []


@pytest.mark.req("STORE-003")
def test_artifacts_are_named_by_their_sha256(tmp_path: Path) -> None:
    import hashlib

    artifacts = ArtifactStore(tmp_path)
    data = bytes(range(256)) * 1000
    stored = artifacts.put_bytes(data)
    assert stored.sha256 == hashlib.sha256(data).hexdigest()
    assert stored.size_bytes == len(data)
    path = artifacts.path_of(stored.sha256)
    assert path.name == stored.sha256
    assert path.parent.name == stored.sha256[:2]
    assert artifacts.read_bytes(stored.sha256) == data
    assert artifacts.contains(stored.sha256)
    assert not artifacts.contains("0" * 64)


@pytest.mark.req("STORE-003")
def test_duplicate_artifacts_across_runs_share_one_file(store: SqliteStore) -> None:
    first_run = store.record_run(metadata())
    second_run = store.record_run(metadata(kernel_version="0.2.0"))
    a = store.record_artifact(first_run, b"identical", "a.stl", "model/stl")
    b = store.record_artifact(second_run, b"identical", "b.stl")
    source = store.root / "c.stl"
    source.write_bytes(b"identical")
    c = store.record_artifact(second_run, source, "c.stl")
    assert a.sha256 == b.sha256 == c.sha256
    assert (a.run_pk, b.run_pk) == (first_run.run_pk, second_run.run_pk)
    assert (a.name, b.name, a.media_type, b.media_type) == ("a.stl", "b.stl", "model/stl", None)
    assert len(stored_files(store.root / "artifacts")) == 1
    assert count(store.database, "artifacts") == 3


@pytest.mark.req("STORE-003")
def test_a_corrupted_artifact_is_detected(tmp_path: Path) -> None:
    artifacts = ArtifactStore(tmp_path)
    stored = artifacts.put_bytes(b"original")
    path = artifacts.path_of(stored.sha256)
    path.chmod(0o644)
    path.write_bytes(b"tampered")
    with pytest.raises(ArtifactCorrupt):
        artifacts.read_bytes(stored.sha256)


@pytest.mark.req("STORE-003")
@pytest.mark.parametrize("digest", ["../../etc/passwd", "A" * 64, "0" * 63, "g" * 64, ""])
def test_artifact_names_must_be_sha256_hex(tmp_path: Path, digest: str) -> None:
    with pytest.raises(ValueError):
        ArtifactStore(tmp_path).path_of(digest)


@pytest.mark.req("STORE-003")
def test_stored_artifact_files_are_read_only(tmp_path: Path) -> None:
    artifacts = ArtifactStore(tmp_path)
    path = artifacts.path_of(artifacts.put_bytes(b"x").sha256)
    assert path.stat().st_mode & 0o222 == 0


@pytest.mark.req("STORE-003")
def test_a_failed_artifact_write_leaves_no_temporary_file(tmp_path: Path) -> None:
    artifacts = ArtifactStore(tmp_path)
    with pytest.raises(FileNotFoundError):
        artifacts.put_file(tmp_path / "missing.bin")
    assert list((tmp_path / "tmp").iterdir()) == []
    assert stored_files(tmp_path) == []


# --- REL-001: a killed process leaves a consistent database ---------------------------------------

CHILD_SETUP = """
import sys, time
from pathlib import Path
from cemkit import schemas as cs
from cemkit.store import capture_run_metadata, input_hash, open_store

root = Path(sys.argv[1])
tag = sys.argv[2]
store = open_store(root)
run = store.record_run(capture_run_metadata({"tag": tag}, environ={}))
spec = cs.load_yaml(Path(sys.argv[3]).read_text(encoding="utf-8"))
store.record_spec(run, spec)

def candidate(candidate_id):
    return {
        "schema_version": "1.0.0",
        "candidate_id": candidate_id,
        "spec": {"spec_id": "axial-120", "revision": 1},
        "family": "fans.axial_ducted",
        "parameters": {"tip_diameter": {"value": 0.119, "unit": "m"}},
    }
"""

BATCH = 100


def run_child(root: Path, tag: str, body: str) -> subprocess.Popen[str]:
    script = CHILD_SETUP + textwrap.dedent(body)
    environment = {**os.environ, "PYTHONPATH": str(ROOT / "python")}
    return subprocess.Popen(
        [sys.executable, "-c", script, str(root), tag, str(EXAMPLE)],
        stdout=subprocess.PIPE,
        text=True,
        env=environment,
    )


def wait_for(child: subprocess.Popen[str], line: str) -> None:
    assert child.stdout is not None
    received = child.stdout.readline().strip()
    assert received == line, f"child printed {received!r}, return code {child.poll()}"


def kill(child: subprocess.Popen[str]) -> None:
    child.send_signal(signal.SIGKILL)
    assert child.wait(timeout=30) == -signal.SIGKILL
    assert child.stdout is not None
    child.stdout.close()


def assert_consistent(database: Path) -> None:
    with raw(database) as connection:
        assert connection.execute("PRAGMA integrity_check").fetchall() == [("ok",)]
        connection.execute("PRAGMA foreign_keys = ON")
        assert connection.execute("PRAGMA foreign_key_check").fetchall() == []
        jobs_without_events = connection.execute(
            "SELECT count(*) FROM jobs WHERE job_pk NOT IN (SELECT job_pk FROM job_events)"
        ).fetchone()[0]
    connection.close()
    assert jobs_without_events == 0


@pytest.mark.req("REL-001")
def test_a_kill_in_the_middle_of_a_transaction_writes_nothing_from_it(tmp_path: Path) -> None:
    root = tmp_path / "store"
    child = run_child(
        root,
        "mid",
        f"""
        store.record_candidates(run, [candidate(f"done-{{i}}") for i in range({BATCH})])

        def slow():
            for i in range({BATCH}):
                if i == {BATCH // 2}:
                    print("mid", flush=True)
                    time.sleep(120)
                yield candidate(f"partial-{{i}}")

        store.record_candidates(run, slow())
        """,
    )
    wait_for(child, "mid")
    kill(child)
    assert_consistent(root / "store.sqlite3")
    assert count(root / "store.sqlite3", "candidates", "candidate_id LIKE 'done-%'") == BATCH
    assert count(root / "store.sqlite3", "candidates", "candidate_id LIKE 'partial-%'") == 0
    with open_store(root) as reopened:  # the next process can carry on writing
        assert len(reopened.runs()) == 1
        reopened.record_run(metadata())


@pytest.mark.req("REL-001")
@pytest.mark.parametrize("delay", [0.1, 0.25, 0.5])
def test_a_kill_at_any_time_during_a_write_loop_leaves_whole_batches(
    tmp_path: Path, delay: float
) -> None:
    root = tmp_path / "store"
    for round_number in range(3):
        child = run_child(
            root,
            f"r{round_number}",
            f"""
            print("ready", flush=True)
            batch = 0
            while True:
                docs = [candidate(f"r{round_number}-b{{batch}}-{{i}}") for i in range({BATCH})]
                (first, *_) = store.record_candidates(run, docs)
                job = store.create_job(run, "l1_evaluation", input_hash(batch), first)
                store.record_job_event(run, job, "running")
                store.record_artifact(run, f"artifact {{batch}}".encode(), f"a{{batch}}.txt")
                batch += 1
            """,
        )
        wait_for(child, "ready")
        time.sleep(delay + 0.1 * round_number)
        kill(child)
        database = root / "store.sqlite3"
        assert_consistent(database)
        with raw(database) as connection:
            ids = [row[0] for row in connection.execute("SELECT candidate_id FROM candidates")]
        connection.close()
        batches = Counter(candidate_id.rsplit("-", 1)[0] for candidate_id in ids)
        assert set(batches.values()) <= {BATCH}, "a batch was written partially"
    assert count(root / "store.sqlite3", "candidates") > 0, "the child never committed a batch"
    with open_store(root) as reopened:
        assert len(reopened.runs()) == 3
        for artifact in reopened.artifact_records():
            assert reopened.artifacts.read_bytes(artifact.sha256)


# --- the store API -------------------------------------------------------------------------------


@pytest.mark.req("STORE-001")
def test_open_store_lays_out_the_root(tmp_path: Path) -> None:
    with open_store(tmp_path / "new") as opened:
        assert opened.database == tmp_path / "new" / "store.sqlite3"
        assert opened.artifacts.root == tmp_path / "new" / "artifacts"
    with pytest.raises(StoreError):
        opened.runs()  # closed


@pytest.mark.req("STORE-001")
def test_a_record_for_a_run_that_does_not_exist_is_rejected(store: SqliteStore) -> None:
    run = store.record_run(metadata())
    ghost = copy.replace(run, run_pk=run.run_pk + 1)
    with pytest.raises(StoreError):
        store.record_spec(ghost, spec_doc())
    assert count(store.database, "specs") == 0


@pytest.mark.req("STORE-002")
def test_versions_serialise_canonically_or_as_unknown() -> None:
    meta = metadata()
    assert meta.plugin_versions_json() == '{"fans.axial_ducted":"0.1.0"}'
    unknown = copy.replace(meta, plugin_versions=UNKNOWN)
    assert unknown.plugin_versions_json() == UNKNOWN

"""T13: durable job queue, worker, resource limits and JSON logs (python/cemkit/orchestration/).

Requirements: ORC-001 (durable jobs; a crash loses no completed work), ORC-002 (heavy jobs one at a
time by default, configurable), ORC-003 (retries with a recorded reason, default 1), ORC-004 (job
state machine, leases, publish with a manifest, reconcile after a crash), RES-001 (memory cap),
SIM-002 (memory, core and wall-time limits; exceeding one records resource_exceeded), REL-002
(every failure has a defined code), REL-003 (failure injection with fakes: mesh failure, solver
divergence, out-of-memory, disk full, killed process), OBS-001 (JSON-line logs with peak RAM, CPU
time and wall time), STORE-001/STORE-002 for the new tables.
"""

from __future__ import annotations

import errno
import io
import json
import os
import signal
import sqlite3
import subprocess
import sys
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest
from fake_job import FAKE, MIB, handlers

from cemkit import schemas as cs
from cemkit.logging import JsonLogger
from cemkit.orchestration import (
    JOB_MEMORY_CAP_BYTES,
    ResourceLimits,
    Worker,
    WorkerConfig,
    run_limited,
)
from cemkit.store import (
    ArtifactStore,
    LeaseLost,
    Run,
    SqliteStore,
    StoreError,
    capture_run_metadata,
    open_store,
)

TESTS = Path(__file__).resolve().parent
ERROR_CODES = {
    entry["const"]
    for entry in cs.load_schemas().schemas["urn:cemkit:schema:v1:error-codes"]["oneOf"]
}
NEW_TABLES = ("job_specs", "job_leases", "job_heartbeats", "job_attempts", "job_artifacts")


@pytest.fixture
def store(tmp_path: Path) -> Iterator[SqliteStore]:
    with open_store(tmp_path / "store") as opened:
        yield opened


@pytest.fixture
def run(store: SqliteStore) -> Run:
    return store.record_run(capture_run_metadata({"test": "orchestration"}, environ={}))


@pytest.fixture
def oom_events(tmp_path: Path) -> Path:
    path = tmp_path / "memory.events"
    path.write_text("low 0\nhigh 0\nmax 0\noom 0\noom_kill 0\n", encoding="utf-8")
    return path


def config(oom_events: Path, **overrides: Any) -> WorkerConfig:
    values: dict[str, Any] = {
        "worker_id": "w1",
        "lease_seconds": 5.0,
        "heartbeat_seconds": 0.2,
        "poll_seconds": 0.05,
        "oom_events": oom_events,
    }
    values.update(overrides)
    return WorkerConfig(**values)


def worker(store: SqliteStore, oom_events: Path, **overrides: Any) -> Worker:
    return Worker(store, config(oom_events, **overrides), handlers())


def ok_payload(log: Path, tag: str, seconds: float = 0.0) -> dict[str, Any]:
    return {"mode": "ok", "args": ["{out}", seconds, str(log), tag]}


def events(store: SqliteStore, job_pk: int) -> list[tuple[str, str | None]]:
    with sqlite3.connect(store.database, autocommit=True) as connection:
        rows = connection.execute(
            "SELECT status, reason FROM job_events WHERE job_pk = ? ORDER BY event_pk", (job_pk,)
        ).fetchall()
    connection.close()
    return [(str(s), r) for s, r in rows]


def log_lines(store: SqliteStore, worker_id: str = "w1") -> list[dict[str, Any]]:
    path = store.root / "logs" / f"worker-{worker_id}.jsonl"
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


# --- ORC-004: the state machine and publishing ---------------------------------------------------


@pytest.mark.req("ORC-004")
def test_a_job_goes_queued_running_publishing_done_and_publishes_a_manifest(
    store: SqliteStore, run: Run, oom_events: Path, tmp_path: Path
) -> None:
    spec = store.enqueue_job(run, "fake_solver", ok_payload(tmp_path / "log", "a"))
    assert store.job_statuses() == {spec.job_pk: "queued"}
    assert worker(store, oom_events).run_once()
    assert [s for s, _ in events(store, spec.job_pk)] == ["queued", "running", "publishing", "done"]
    (attempt,) = store.attempts(spec.job_pk)
    assert (attempt.attempt, attempt.outcome, attempt.code) == (1, "done", None)
    published = {(role, a.name): a for role, a in store.job_artifacts(spec.job_pk)}
    assert set(published) == {
        ("output", "result.txt"),
        ("log", "stdout.log"),
        ("log", "stderr.log"),
        ("manifest", "manifest.json"),
    }
    assert store.artifacts.read_bytes(published["output", "result.txt"].sha256) == b"ok a\n"
    manifest = json.loads(store.artifacts.read_bytes(published["manifest", "manifest.json"].sha256))
    assert manifest["job_pk"] == spec.job_pk and manifest["attempt"] == 1
    listed = {entry["name"]: entry["sha256"] for entry in manifest["files"]}
    assert listed["result.txt"] == published["output", "result.txt"].sha256
    assert len(spec.input_hash) == 64


@pytest.mark.req("ORC-004")
def test_the_input_hash_depends_on_kind_and_payload(store: SqliteStore, run: Run) -> None:
    a = store.enqueue_job(run, "fake_solver", {"mode": "sleep", "args": [1]})
    b = store.enqueue_job(run, "fake_solver", {"args": [1], "mode": "sleep"})
    c = store.enqueue_job(run, "fake_mesh", {"mode": "sleep", "args": [1]})
    assert a.input_hash == b.input_hash != c.input_hash


@pytest.mark.req("ORC-004")
def test_a_worker_that_dies_while_publishing_is_resumed_without_rerunning(
    store: SqliteStore, run: Run, oom_events: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    log = tmp_path / "log"
    spec = store.enqueue_job(run, "fake_solver", ok_payload(log, "once"))
    first = worker(store, oom_events, lease_seconds=0.5)

    def crash(*_: Any, **__: Any) -> None:
        raise KeyboardInterrupt("simulated crash during publishing")

    monkeypatch.setattr(first, "_publish", crash)
    with pytest.raises(KeyboardInterrupt):
        first.run_once()
    assert store.job_statuses()[spec.job_pk] == "publishing"
    monkeypatch.undo()
    time.sleep(0.6)  # the first worker's lease expires
    second = worker(store, oom_events, worker_id="w2")
    second.run_until_idle(timeout=30)
    assert store.job_statuses()[spec.job_pk] == "done"
    assert log.read_text(encoding="utf-8").splitlines() == ["once"], "the child ran twice"
    outcomes = [(a.attempt, a.outcome) for a in store.attempts(spec.job_pk)]
    assert outcomes == [(1, "handed_over"), (1, "done")]
    done = store.attempts(spec.job_pk)[-1]
    assert done.usage.wall_seconds is not None, "usage survives the hand-over via the manifest"


@pytest.mark.req("ORC-004")
def test_a_tampered_output_is_not_adopted(
    store: SqliteStore, run: Run, oom_events: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    spec = store.enqueue_job(run, "fake_solver", ok_payload(tmp_path / "log", "t"), max_retries=0)
    first = worker(store, oom_events, lease_seconds=0.5)

    def crash(*_: Any, **__: Any) -> None:
        raise KeyboardInterrupt

    monkeypatch.setattr(first, "_publish", crash)
    with pytest.raises(KeyboardInterrupt):
        first.run_once()
    monkeypatch.undo()
    (store.root / "work" / f"job-{spec.job_pk}" / "attempt-1" / "out" / "result.txt").write_text(
        "tampered", encoding="utf-8"
    )
    time.sleep(0.6)
    worker(store, oom_events, worker_id="w2").run_until_idle(timeout=30)
    assert store.job_statuses()[spec.job_pk] == "failed"
    assert [a.outcome for a in store.attempts(spec.job_pk)] == ["lost"]


@pytest.mark.req("ORC-004")
def test_a_lease_that_was_reconciled_is_lost(store: SqliteStore, run: Run) -> None:
    store.enqueue_job(run, "k", {"x": 1})
    claim = store.claim_job(run, "w1", ["k"], now=100.0, lease_seconds=1.0)
    assert claim is not None
    assert store.heartbeat(run, claim.lease, now=100.5, lease_seconds=1.0) == 101.5
    assert store.expired_leases(now=101.0) == ()
    (expired,) = store.expired_leases(now=102.0)
    assert expired.lease == claim.lease and expired.status == "running"
    with pytest.raises(LeaseLost):
        store.abandon_lease(run, claim.lease, now=101.0, reason="too early")
    assert store.abandon_lease(run, claim.lease, now=102.0, reason="worker lost") == "queued"
    with pytest.raises(LeaseLost):
        store.heartbeat(run, claim.lease, now=102.1, lease_seconds=1.0)
    with pytest.raises(LeaseLost):
        store.complete_job(run, claim.lease, store.attempts(claim.job.job_pk)[0].usage, [])
    with pytest.raises(StoreError):
        store.adopt_lease(run, claim.lease, "w2", now=103.0, lease_seconds=1.0)


@pytest.mark.req("ORC-004")
def test_the_runner_stops_the_child_when_the_lease_is_lost(
    store: SqliteStore, run: Run, oom_events: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    spec = store.enqueue_job(run, "fake_solver", {"mode": "sleep", "args": [30]})
    w = worker(store, oom_events, lease_seconds=0.3, heartbeat_seconds=0.1)
    original = store.heartbeat

    def reconcile_then_heartbeat(r: Run, lease: Any, *, now: float, lease_seconds: float) -> float:
        store.abandon_lease(run, lease, now=now + 10.0, reason="reconciled by another worker")
        return original(r, lease, now=now, lease_seconds=lease_seconds)

    monkeypatch.setattr(store, "heartbeat", reconcile_then_heartbeat)
    started = time.monotonic()
    assert w.run_once()
    assert time.monotonic() - started < 10
    assert [a.outcome for a in store.attempts(spec.job_pk)] == ["lost"]
    assert any(line["event"] == "lease_lost" for line in log_lines(store))


# --- ORC-001: killing the worker mid-job ---------------------------------------------------------

WORKER_SCRIPT = """
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[3])
from fake_job import handlers
from cemkit.orchestration import Worker, WorkerConfig
from cemkit.store import open_store

store = open_store(Path(sys.argv[1]))
settings = WorkerConfig(worker_id=sys.argv[2], lease_seconds=1.0, heartbeat_seconds=0.2,
                        poll_seconds=0.05)
Worker(store, settings, handlers()).run_until_idle(timeout=120)
"""


def start_worker(root: Path, worker_id: str) -> subprocess.Popen[bytes]:
    return subprocess.Popen(
        [sys.executable, "-c", WORKER_SCRIPT, str(root), worker_id, str(TESTS)],
        stdout=subprocess.DEVNULL,
    )


@pytest.mark.req("ORC-001")
@pytest.mark.req("REL-003")
def test_killing_the_worker_mid_job_loses_and_duplicates_no_completed_job(
    store: SqliteStore, run: Run, tmp_path: Path
) -> None:
    log = tmp_path / "executions.log"
    jobs = [
        store.enqueue_job(run, "fake_solver", ok_payload(log, f"job{i}", 0.3)).job_pk
        for i in range(8)
    ]
    first = start_worker(store.root, "first")
    deadline = time.monotonic() + 60
    while True:
        statuses = store.job_statuses()
        done = [j for j in jobs if statuses[j] == "done"]
        running = [j for j in jobs if statuses[j] == "running"]
        if len(done) >= 2 and running:
            break
        assert time.monotonic() < deadline, "the worker made no progress"
        time.sleep(0.02)
    first.send_signal(signal.SIGKILL)
    assert first.wait(timeout=30) == -signal.SIGKILL
    done_before = {j for j, s in store.job_statuses().items() if s == "done"}
    assert len(done_before) >= 2

    second = start_worker(store.root, "second")
    assert second.wait(timeout=120) == 0
    statuses = store.job_statuses()
    assert all(statuses[j] == "done" for j in jobs), statuses
    executions = log.read_text(encoding="utf-8").splitlines()
    for index, job_pk in enumerate(jobs):
        outcomes = [a.outcome for a in store.attempts(job_pk)]
        assert outcomes.count("done") == 1, (job_pk, outcomes)
        assert [s for s, _ in events(store, job_pk)].count("done") == 1
        manifests = [a for role, a in store.job_artifacts(job_pk) if role == "manifest"]
        assert len(manifests) == 1
        if job_pk in done_before:
            assert outcomes == ["done"], "a completed job was touched again"
            assert executions.count(f"job{index}") == 1, "a completed job was run again"
    interrupted = [j for j in jobs if j not in done_before and len(store.attempts(j)) > 1]
    assert interrupted, "no job was interrupted by the kill"
    for job_pk in interrupted:
        assert store.attempts(job_pk)[0].outcome in {"lost", "handed_over"}
    with sqlite3.connect(store.database, autocommit=True) as connection:
        assert connection.execute("PRAGMA integrity_check").fetchall() == [("ok",)]
    connection.close()


# --- ORC-002: heavy jobs one at a time ---------------------------------------------------------


@pytest.mark.req("ORC-002")
def test_heavy_jobs_run_one_at_a_time_by_default(store: SqliteStore, run: Run) -> None:
    heavy = [store.enqueue_job(run, "k", {"n": i}, resource_class="heavy") for i in range(2)]
    light = store.enqueue_job(run, "k", {"n": "light"})
    first = store.claim_job(run, "a", ["k"], now=0.0, lease_seconds=60.0)
    assert first is not None and first.job.job_pk == heavy[0].job_pk
    second = store.claim_job(run, "b", ["k"], now=0.0, lease_seconds=60.0)
    assert second is not None and second.job.job_pk == light.job_pk, "the light job goes ahead"
    assert store.claim_job(run, "c", ["k"], now=0.0, lease_seconds=60.0) is None
    store.mark_publishing(run, first.lease, "outputs written")
    assert store.claim_job(run, "c", ["k"], now=0.0, lease_seconds=60.0) is None
    store.fail_attempt(run, first.lease, "mesh_failed", "bad mesh")
    third = store.claim_job(run, "c", ["k"], now=0.0, lease_seconds=60.0)
    assert third is not None and third.spec.resource_class == "heavy"


@pytest.mark.req("ORC-002")
def test_the_heavy_job_limit_is_configurable(store: SqliteStore, run: Run) -> None:
    for i in range(3):
        store.enqueue_job(run, "k", {"n": i}, resource_class="heavy")
    claims = [
        store.claim_job(run, f"w{i}", ["k"], now=0.0, lease_seconds=60.0, max_heavy=2)
        for i in range(3)
    ]
    assert [c is not None for c in claims] == [True, True, False]
    assert WorkerConfig(worker_id="w").max_heavy_jobs == 1


@pytest.mark.req("ORC-002")
def test_a_worker_only_claims_kinds_it_handles(store: SqliteStore, run: Run) -> None:
    store.enqueue_job(run, "other", {})
    assert store.claim_job(run, "w", ["k"], now=0.0, lease_seconds=1.0) is None
    assert store.claim_job(run, "w", [], now=0.0, lease_seconds=1.0) is None


# --- ORC-003: retries ------------------------------------------------------------------------


@pytest.mark.req("ORC-003")
@pytest.mark.req("REL-003")
def test_solver_divergence_is_retried_once_by_default_then_fails(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    spec = store.enqueue_job(run, "fake_solver", {"mode": "exit", "args": [2, "diverged"]})
    assert spec.max_retries == 1
    worker(store, oom_events).run_until_idle(timeout=30)
    history = events(store, spec.job_pk)
    assert [s for s, _ in history] == ["queued", "running", "queued", "running", "failed"]
    retry_reason = history[2][1]
    assert retry_reason is not None and "retry 1 of 1" in retry_reason
    assert "sim_untrusted" in retry_reason and "diverged" in retry_reason
    assert [(a.attempt, a.outcome, a.code) for a in store.attempts(spec.job_pk)] == [
        (1, "failed", "sim_untrusted"),
        (2, "failed", "sim_untrusted"),
    ]
    failures = store.failures(spec.job_pk)
    assert [f.code for f in failures] == ["sim_untrusted", "sim_untrusted"]
    assert failures[0].details()["attempt"] == "1"


@pytest.mark.req("ORC-003")
def test_a_retry_can_succeed(
    store: SqliteStore, run: Run, oom_events: Path, tmp_path: Path
) -> None:
    payload = {"mode": "flaky", "args": ["{out}", str(tmp_path / "marker")]}
    spec = store.enqueue_job(run, "fake_solver", payload)
    worker(store, oom_events).run_until_idle(timeout=30)
    assert store.job_statuses()[spec.job_pk] == "done"
    assert [a.outcome for a in store.attempts(spec.job_pk)] == ["failed", "done"]


@pytest.mark.req("ORC-003")
@pytest.mark.parametrize("retries", [0, 2])
def test_the_retry_count_is_configurable(
    store: SqliteStore, run: Run, oom_events: Path, retries: int
) -> None:
    payload = {"mode": "exit", "args": [3, "no cells"]}
    spec = store.enqueue_job(run, "fake_mesh", payload, max_retries=retries)
    worker(store, oom_events).run_until_idle(timeout=30)
    assert len(store.attempts(spec.job_pk)) == retries + 1
    assert store.job_statuses()[spec.job_pk] == "failed"


@pytest.mark.req("ORC-003")
@pytest.mark.parametrize(
    "arguments",
    [
        {"kind": ""},
        {"resource_class": "medium"},
        {"max_retries": -1},
        {"max_retries": True},
        {"payload": {"x": float("nan")}},
    ],
)
def test_bad_jobs_are_refused(store: SqliteStore, run: Run, arguments: dict[str, Any]) -> None:
    values: dict[str, Any] = {"kind": "k", "payload": {}}
    values.update(arguments)
    kind, payload = values.pop("kind"), values.pop("payload")
    with pytest.raises(StoreError):
        store.enqueue_job(run, kind, payload, **values)


# --- REL-002 / REL-003: failure classification -------------------------------------------------


@pytest.mark.req("REL-002")
@pytest.mark.req("REL-003")
def test_a_mesh_failure_is_recorded_as_mesh_failed(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    payload = {"mode": "exit", "args": [3, "negative cell volume"]}
    spec = store.enqueue_job(run, "fake_mesh", payload, max_retries=0)
    worker(store, oom_events).run_until_idle(timeout=30)
    (failure,) = store.failures(spec.job_pk)
    assert failure.code == "mesh_failed"
    assert "negative cell volume" in failure.message


@pytest.mark.req("REL-002")
@pytest.mark.req("REL-003")
def test_a_child_killed_from_outside_is_an_internal_error(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    spec = store.enqueue_job(run, "fake_solver", {"mode": "killed"}, max_retries=0)
    worker(store, oom_events).run_until_idle(timeout=30)
    (failure,) = store.failures(spec.job_pk)
    assert failure.code == "internal_error"
    assert "signal 9" in failure.message


@pytest.mark.req("REL-002")
def test_an_unknown_failure_code_is_refused(store: SqliteStore, run: Run) -> None:
    store.enqueue_job(run, "k", {})
    claim = store.claim_job(run, "w", ["k"], now=0.0, lease_seconds=60.0)
    assert claim is not None
    with pytest.raises(StoreError):
        store.fail_attempt(run, claim.lease, "solver_exploded", "not a code")
    with pytest.raises(StoreError):
        store.fail_attempt(run, claim.lease, "mesh_failed", "")


@pytest.mark.req("REL-002")
def test_every_recorded_failure_uses_a_defined_code(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    for payload in (
        {"mode": "exit", "args": [2, "x"]},
        {"mode": "killed"},
        {"mode": "sleep", "args": [30], "wall_s": 0.2},
    ):
        store.enqueue_job(run, "fake_solver", payload, max_retries=0)
    worker(store, oom_events).run_until_idle(timeout=60)
    codes = {f.code for job in store.job_statuses() for f in store.failures(job)}
    assert codes and codes <= ERROR_CODES


# --- SIM-002 / RES-001: resource limits ----------------------------------------------------------


@pytest.mark.req("SIM-002")
@pytest.mark.req("REL-003")
def test_exceeding_the_memory_limit_kills_the_child_and_records_resource_exceeded(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    payload = {"mode": "alloc", "args": [16, 1024], "memory_mb": 96}
    spec = store.enqueue_job(run, "fake_solver", payload, max_retries=0)
    started = time.monotonic()
    worker(store, oom_events).run_until_idle(timeout=60)
    assert time.monotonic() - started < 30
    (attempt,) = store.attempts(spec.job_pk)
    assert attempt.code == "resource_exceeded"
    assert attempt.reason is not None and "memory" in attempt.reason
    assert attempt.usage.peak_rss_bytes is not None and attempt.usage.peak_rss_bytes > 96 * MIB
    assert attempt.usage.peak_rss_bytes < 1024 * MIB


@pytest.mark.req("SIM-002")
@pytest.mark.req("REL-003")
def test_a_kernel_oom_kill_is_recorded_as_resource_exceeded(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    spec = store.enqueue_job(
        run, "fake_solver", {"mode": "oom_kernel", "args": [str(oom_events)]}, max_retries=0
    )
    worker(store, oom_events).run_until_idle(timeout=30)
    (failure,) = store.failures(spec.job_pk)
    assert failure.code == "resource_exceeded"
    assert "out-of-memory" in failure.message


@pytest.mark.req("SIM-002")
@pytest.mark.req("REL-003")
def test_exceeding_the_wall_time_kills_the_child_and_records_resource_exceeded(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    payload = {"mode": "sleep", "args": [60], "wall_s": 0.5}
    spec = store.enqueue_job(run, "fake_solver", payload, max_retries=0)
    started = time.monotonic()
    worker(store, oom_events).run_until_idle(timeout=60)
    assert time.monotonic() - started < 15
    (attempt,) = store.attempts(spec.job_pk)
    assert attempt.code == "resource_exceeded"
    assert attempt.reason is not None and "wall time" in attempt.reason
    assert attempt.usage.wall_seconds is not None and 0.5 <= attempt.usage.wall_seconds < 10


@pytest.mark.req("SIM-002")
def test_the_child_runs_on_the_requested_number_of_cores(tmp_path: Path) -> None:
    if len(os.sched_getaffinity(0)) < 2:
        pytest.skip("needs at least two CPUs")
    out = tmp_path / "out"
    outcome = run_limited(
        (sys.executable, str(FAKE), "affinity", str(out)),
        ResourceLimits(cores=2, wall_time_s=30),
        cwd=tmp_path,
        stdout=tmp_path / "o",
        stderr=tmp_path / "e",
    )
    assert outcome.returncode == 0
    assert (out / "cpus.txt").read_text(encoding="utf-8") == "2"


@pytest.mark.req("SIM-002")
def test_the_runner_measures_cpu_and_peak_memory(tmp_path: Path) -> None:
    outcome = run_limited(
        (sys.executable, "-c", "x = b'x' * (64 << 20); sum(range(3_000_000))"),
        ResourceLimits(cores=1, wall_time_s=30),
        cwd=tmp_path,
        stdout=tmp_path / "o",
        stderr=tmp_path / "e",
    )
    assert (outcome.returncode, outcome.signal, outcome.exceeded) == (0, None, None)
    assert outcome.peak_rss_bytes >= 64 * MIB
    assert outcome.cpu_seconds > 0
    assert outcome.wall_seconds > 0


@pytest.mark.req("RES-001")
def test_the_memory_cap_is_the_adr_000_value_and_cannot_be_raised() -> None:
    assert JOB_MEMORY_CAP_BYTES == 12 * 1024**3
    assert ResourceLimits(cores=1, wall_time_s=1).memory_bytes == JOB_MEMORY_CAP_BYTES
    with pytest.raises(ValueError, match="RES-001"):
        ResourceLimits(cores=1, wall_time_s=1, memory_bytes=JOB_MEMORY_CAP_BYTES + 1)


@pytest.mark.req("SIM-002")
@pytest.mark.parametrize(
    "arguments",
    [
        {"cores": 0, "wall_time_s": 1},
        {"cores": 1, "wall_time_s": 0},
        {"cores": 1, "wall_time_s": float("inf")},
        {"cores": 1, "wall_time_s": 1, "memory_bytes": 0},
    ],
)
def test_limits_must_be_positive_and_finite(arguments: dict[str, Any]) -> None:
    with pytest.raises(ValueError):
        ResourceLimits(**arguments)


@pytest.mark.req("SIM-002")
def test_more_cores_than_available_are_refused(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="cores"):
        run_limited(
            ("true",),
            ResourceLimits(cores=len(os.sched_getaffinity(0)) + 1, wall_time_s=1),
            cwd=tmp_path,
            stdout=tmp_path / "o",
            stderr=tmp_path / "e",
        )


# --- REL-003: disk full ----------------------------------------------------------------------


@pytest.mark.req("REL-003")
def test_a_full_disk_while_publishing_records_resource_exceeded(
    store: SqliteStore,
    run: Run,
    oom_events: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def full(self: ArtifactStore, source: Path) -> Any:
        raise OSError(errno.ENOSPC, "No space left on device")

    monkeypatch.setattr(ArtifactStore, "put_file", full)
    spec = store.enqueue_job(run, "fake_solver", ok_payload(tmp_path / "log", "d"))
    worker(store, oom_events).run_until_idle(timeout=30)
    assert store.job_statuses()[spec.job_pk] == "failed"
    attempts = store.attempts(spec.job_pk)
    assert [(a.outcome, a.code) for a in attempts] == [("failed", "resource_exceeded")] * 2
    assert attempts[0].reason is not None and "disk full" in attempts[0].reason
    assert store.job_artifacts(spec.job_pk) == ()


@pytest.mark.req("REL-003")
def test_another_publishing_error_is_an_internal_error(
    store: SqliteStore,
    run: Run,
    oom_events: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def broken(self: ArtifactStore, source: Path) -> Any:
        raise OSError(errno.EIO, "Input/output error")

    monkeypatch.setattr(ArtifactStore, "put_file", broken)
    payload = ok_payload(tmp_path / "log", "e")
    spec = store.enqueue_job(run, "fake_solver", payload, max_retries=0)
    worker(store, oom_events).run_until_idle(timeout=30)
    (failure,) = store.failures(spec.job_pk)
    assert failure.code == "internal_error"


# --- OBS-001: structured logs ------------------------------------------------------------------


@pytest.mark.req("OBS-001")
def test_every_attempt_logs_peak_ram_cpu_and_wall_time_as_json_lines(
    store: SqliteStore, run: Run, oom_events: Path, tmp_path: Path
) -> None:
    store.enqueue_job(run, "fake_solver", ok_payload(tmp_path / "log", "a"))
    store.enqueue_job(run, "fake_mesh", {"mode": "exit", "args": [3, "x"]}, max_retries=0)
    worker(store, oom_events).run_until_idle(timeout=30)
    finished = [line for line in log_lines(store) if line["event"] == "attempt_finished"]
    assert len(finished) == 2
    for line in finished:
        assert line["worker_id"] == "w1"
        assert isinstance(line["peak_rss_bytes"], int) and line["peak_rss_bytes"] > 0
        assert isinstance(line["cpu_seconds"], float) and line["cpu_seconds"] >= 0
        assert isinstance(line["wall_seconds"], float) and line["wall_seconds"] > 0
        assert line["outcome"] in {"done", "failed"}
        assert {"time", "job_pk", "kind", "attempt", "code"} <= set(line)
    assert {line["code"] for line in finished} == {None, "mesh_failed"}


@pytest.mark.req("OBS-001")
def test_the_json_logger_writes_one_sorted_object_per_line(tmp_path: Path) -> None:
    path = tmp_path / "logs" / "x.jsonl"
    logger = JsonLogger(path)
    logger.emit("first", b=1, a="text")
    logger.emit("second", value=None)
    lines = path.read_text(encoding="utf-8").splitlines()
    assert [json.loads(line)["event"] for line in lines] == ["first", "second"]
    assert lines[0].index('"a"') < lines[0].index('"b"')
    with pytest.raises(ValueError):
        JsonLogger(path).emit("bad", value=float("nan"))
    stream = io.StringIO()
    JsonLogger(stream=stream).emit("to a stream", n=1)
    assert json.loads(stream.getvalue())["n"] == 1


# --- the worker --------------------------------------------------------------------------------


def test_worker_ids_are_safe_file_names(oom_events: Path) -> None:
    for bad in ("", "../x", "a/b", "x" * 65):
        with pytest.raises(ValueError):
            WorkerConfig(worker_id=bad)
    with pytest.raises(ValueError):
        WorkerConfig(worker_id="w", lease_seconds=1.0, heartbeat_seconds=1.0)


def test_run_until_idle_times_out_while_another_worker_holds_a_job(
    store: SqliteStore, run: Run, oom_events: Path
) -> None:
    store.enqueue_job(run, "fake_solver", {"mode": "sleep", "args": [0]})
    assert store.claim_job(run, "other", ["fake_solver"], now=time.time(), lease_seconds=60)
    with pytest.raises(TimeoutError):
        worker(store, oom_events).run_until_idle(timeout=0.3)


def test_a_worker_with_no_work_returns_at_once(store: SqliteStore, oom_events: Path) -> None:
    w = worker(store, oom_events)
    assert not w.run_once()
    w.run_until_idle(timeout=1)
    assert log_lines(store)[0]["event"] == "worker_started"


def test_two_handlers_for_one_kind_are_refused(store: SqliteStore, oom_events: Path) -> None:
    with pytest.raises(ValueError):
        Worker(store, config(oom_events), [*handlers(), handlers()[0]])


# --- STORE-001 / STORE-002 for the new tables ----------------------------------------------------


@pytest.mark.req("STORE-001")
@pytest.mark.req("STORE-002")
@pytest.mark.parametrize("table", NEW_TABLES)
def test_queue_tables_are_append_only_and_reference_their_run(
    store: SqliteStore, run: Run, oom_events: Path, tmp_path: Path, table: str
) -> None:
    store.enqueue_job(run, "fake_solver", ok_payload(tmp_path / "log", "a", 0.5))
    worker(store, oom_events).run_until_idle(timeout=30)
    with sqlite3.connect(store.database, autocommit=True) as connection:
        assert connection.execute(f"SELECT count(*) FROM {table}").fetchone()[0] >= 1
        info = {row[1]: row for row in connection.execute(f"PRAGMA table_info({table})")}
        assert info["run_pk"][3] == 1
        column = next(iter(info))
        for statement in (
            f"UPDATE {table} SET {column} = {column}",
            f"DELETE FROM {table}",
            f"INSERT OR REPLACE INTO {table} SELECT * FROM {table}",
        ):
            with pytest.raises(sqlite3.DatabaseError, match="append-only"):
                connection.execute(statement)
    connection.close()

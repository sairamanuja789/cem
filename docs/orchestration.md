# Job queue and worker

`python/cemkit/orchestration/` runs jobs from the store (ORC-001 to ORC-004, RES-001, SIM-002,
REL-002, REL-003, OBS-001). How resource limits are enforced, and why: [ADR-005](adr/005-job-resource-limits.md).
The store tables are described in [store.md](store.md).

```python
from cemkit.orchestration import Worker, WorkerConfig

spec = store.enqueue_job(run, "openfoam", payload, resource_class="heavy")  # max_retries=1
Worker(store, WorkerConfig(worker_id="w1"), handlers).run_until_idle()
```

A **handler** exists for each job kind. It turns the job's payload into a `Command`: an argv plus
`ResourceLimits(cores, wall_time_s, memory_bytes ≤ 12 GiB)`. After the child exits, the handler
classifies the result with a REL-002 code. Only the runner (`orchestration/runner.py`) starts
processes.

## States (ORC-004)

```text
queued ──claim──▶ running ──outputs + manifest──▶ publishing ──files linked──▶ done
   ▲                 │                                │
   └── retry left ───┴── failed / lost attempt ───────┘──▶ failed (no retry left)
```

- Each change is a new `job_events` row. The current status is the newest row.
- A **lease** gives one worker one attempt. The worker renews it with heartbeats while the child
  runs. An attempt ends with exactly one `job_attempts` row:
  - `done`, `failed` or `lost`, with peak RSS, CPU time and wall time;
  - or `handed_over`.
- **Retries (ORC-003):** a failed or lost attempt is requeued while `attempt ≤ max_retries`
  (default 1). The retry reason is in the event's `reason` and in a `failures` row.
- **Heavy jobs (ORC-002):** a heavy job starts only while fewer than `max_heavy_jobs` (default 1)
  heavy jobs are running or publishing. A light job may go ahead of a waiting heavy job.

## Publishing and crash recovery

1. The child writes its outputs to `work/job-<pk>/attempt-<n>/out/`.
2. The worker writes `manifest.json` atomically. It lists every output and log file with its
   sha256 and size, plus the measured usage. Only then is the job marked `publishing`.
3. Each file goes into the content-addressed artifact store. One transaction then:
   - links the files to the job (`job_artifacts`: output, log, manifest);
   - records the attempt as `done`;
   - marks the job `done`.

Before every claim, a worker reconciles leases that expired:

- **Expired while running:** the attempt is recorded as `lost` and retried if the budget allows.
  The child cannot still be running, because `PR_SET_PDEATHSIG` killed it with its worker.
- **Expired while publishing:** if every file still matches the manifest, another worker takes the
  job over (`handed_over`) and finishes publishing **without rerunning the child**. Otherwise the
  job is handled like an attempt lost while running.

Every step is one transaction, and a lease that is no longer current is refused (`LeaseLost`). A
completed job is therefore never run or recorded twice. `test_killing_the_worker_mid_job_…` shows
this with a real SIGKILL of the worker process.

## Failure codes (REL-002)

| Event | Code |
| --- | --- |
| Memory, wall-time limit, kernel OOM kill | `resource_exceeded` |
| Disk full while publishing | `resource_exceeded` |
| Mesh failed (handler) | `mesh_failed` |
| Solver diverged (handler) | `sim_untrusted` |
| Bad payload | `invalid_input` |
| Child killed by a signal the runner did not send; worker lost; other publishing error | `internal_error` |

## Logs (OBS-001)

Each worker appends JSON lines to `<store>/logs/worker-<id>.jsonl` (`cemkit/logging.py`).
The events are `worker_started`, `job_claimed`, `attempt_finished`, `job_reconciled`,
`job_adopted`, `lease_lost` and `worker_idle`. Each `attempt_finished` line carries:

- `peak_rss_bytes`, `cpu_seconds` and `wall_seconds`;
- `outcome`, `code` and the new `job_status`.

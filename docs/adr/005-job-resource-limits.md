# ADR-005: Job resource limits

- Status: proposed (needs owner review)
- Date: 2026-10-10
- Requirements affected: RES-001, SIM-002, ORC-002, REL-003, OBS-001

## Context
Every solver run needs enforced memory, core and wall-time limits; exceeding one kills the job and
records `resource_exceeded` (SIM-002). All job memory is capped at 12 GiB (ADR-000, RES-001). The
build plan asks for a choice between `systemd-run --user --scope` and `docker run` limits.

Facts that constrain the choice:
- The orchestration runs inside the pinned dev container (`scripts/dev.sh`), like every other tool.
- Inside that container there is no systemd user manager, so `systemd-run --user` cannot be used.
  There is also no Docker socket, and mounting it would give the container root on the host.
- The container sees cgroup v2 `memory.events` read-only (checked: `oom_kill 0`), but it cannot
  create child cgroups, so it cannot cap one job by itself.
- ADR-000 measured OpenFOAM in a fresh container per solver run, reading cgroup `memory.peak`. Its
  conservative estimator is the sum of per-rank peak RSS.

## Options considered
1. **`systemd-run --user --scope -p MemoryMax=… -p AllowedCPUs=… -p RuntimeMaxSec=…` per job.** A
   real kernel cap per job, but it works only on the host, outside the pinned toolchain, and needs a
   user systemd session. Rejected for now.
2. **`docker run --memory --memory-swap --cpus` per job.** A real kernel cap per job, and it matches
   how ADR-000 measured memory. But the orchestrator would have to run on the host, or get the Docker
   socket (root-equivalent) inside the container. Rejected for now.
3. **Enforce the limits in the runner, inside the container,** and add a kernel backstop for the
   whole worker container:
   - cores: `sched_setaffinity` to N CPUs, inherited by threads and child processes;
   - memory: poll the resident memory of the job's whole process group (the job runs in its own
     session) and kill the group above the limit;
   - wall time: kill the group when the limit has passed;
   - children get `PR_SET_PDEATHSIG = SIGKILL`, so they die with a crashed worker.
   - backstop: start the worker container with `docker run --memory=12g --memory-swap=12g`. A kernel
     OOM kill in that cgroup is recognised from `memory.events` and also recorded as
     `resource_exceeded`.

## Decision
Option 3. It works where the orchestration already runs, and it can be tested in CI with fakes. The
per-job kernel cap of options 1 and 2 is kept as the upgrade path for M3, when real solver jobs run.

## Consequences
- Memory is polled about every 20 ms, so an allocation faster than that can overshoot the limit for
  one interval. Until the container backstop is applied, nothing in the kernel stops such a burst.
  **Not done in this change:** adding `--memory=12g --memory-swap=12g` to the worker's container
  start. Applying it to `scripts/dev.sh` as a whole would also cap the C++ builds, so it needs either
  a separate worker entry point or the owner's agreement.
- The polled sum counts shared pages once per process, so it can overstate the true use. This errs
  on the safe side and matches ADR-000's rank-sum estimator.
- `cores` is CPU pinning, not a CPU quota: the job may use those cores fully.
- The worker records peak RSS, CPU time and wall time for every attempt (OBS-001). Peak RSS is the
  larger of the polled group peak and the kernel's `ru_maxrss` for the child.
- Failure codes come from the fixed list in `error-codes.json`, which is generated from the kernel
  and cannot be extended here. Some mappings therefore need the owner's agreement:
  - a limit hit, a kernel OOM kill, or a full disk while publishing → `resource_exceeded`;
  - a child killed by a signal the runner did not send → `internal_error`;
  - a worker that stopped renewing its lease → the attempt is recorded as `lost` with
    `internal_error`, and it uses one retry, so a job that keeps crashing workers cannot loop
    forever.
- Revisit at M3, with the first real OpenFOAM job: measure the overshoot, and decide whether to move
  heavy jobs to option 2 (per-job containers) with a host-side runner.

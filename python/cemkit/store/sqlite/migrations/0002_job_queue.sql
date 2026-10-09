-- 0002_job_queue: durable job queue for the worker (T13; ORC-001..004, OBS-001, STORE-001).
--
-- Forward-only; never edit after merge (see 0001_initial.sql and docs/store.md).
--
-- A job's status is the newest row in job_events (0001). This migration adds what the worker needs:
--   job_specs       what to run: resource class (light or heavy), retry budget, payload (1:1 with jobs)
--   job_leases      one row per attempt start or hand-over: which worker holds the job until when
--   job_heartbeats  lease extensions while the child process runs
--   job_attempts    how a lease ended: done, failed, lost (worker died) or handed_over (another worker
--                   resumed publishing); with the resources used (peak RAM, CPU time, wall time)
--   job_artifacts   the artifacts a job published (outputs, manifest, logs)
-- Every table is append-only, like the tables in 0001.

CREATE TABLE job_specs (
    job_pk         INTEGER PRIMARY KEY REFERENCES jobs (job_pk),
    run_pk         INTEGER NOT NULL REFERENCES runs (run_pk),
    resource_class TEXT NOT NULL CHECK (resource_class IN ('light', 'heavy')),
    max_retries    INTEGER NOT NULL CHECK (max_retries >= 0),
    payload        TEXT NOT NULL CHECK (json_valid(payload) AND json_type(payload) = 'object'),
    recorded_at    TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;

CREATE TABLE job_leases (
    lease_pk    INTEGER PRIMARY KEY,
    job_pk      INTEGER NOT NULL REFERENCES jobs (job_pk),
    run_pk      INTEGER NOT NULL REFERENCES runs (run_pk),
    worker_id   TEXT NOT NULL CHECK (length(worker_id) > 0),
    attempt     INTEGER NOT NULL CHECK (attempt >= 1),
    acquired_at REAL NOT NULL,
    expires_at  REAL NOT NULL CHECK (expires_at > acquired_at),
    recorded_at TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;
CREATE INDEX job_leases_by_job ON job_leases (job_pk, lease_pk);

CREATE TABLE job_heartbeats (
    heartbeat_pk INTEGER PRIMARY KEY,
    lease_pk     INTEGER NOT NULL REFERENCES job_leases (lease_pk),
    run_pk       INTEGER NOT NULL REFERENCES runs (run_pk),
    at           REAL NOT NULL,
    expires_at   REAL NOT NULL CHECK (expires_at > at),
    recorded_at  TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;
CREATE INDEX job_heartbeats_by_lease ON job_heartbeats (lease_pk, heartbeat_pk);

CREATE TABLE job_attempts (
    attempt_pk     INTEGER PRIMARY KEY,
    lease_pk       INTEGER NOT NULL UNIQUE REFERENCES job_leases (lease_pk),
    job_pk         INTEGER NOT NULL REFERENCES jobs (job_pk),
    run_pk         INTEGER NOT NULL REFERENCES runs (run_pk),
    outcome        TEXT NOT NULL CHECK (outcome IN ('done', 'failed', 'lost', 'handed_over')),
    code           TEXT,
    reason         TEXT,
    peak_rss_bytes INTEGER CHECK (peak_rss_bytes IS NULL OR peak_rss_bytes >= 0),
    cpu_seconds    REAL CHECK (cpu_seconds IS NULL OR cpu_seconds >= 0),
    wall_seconds   REAL CHECK (wall_seconds IS NULL OR wall_seconds >= 0),
    recorded_at    TEXT NOT NULL CHECK (length(recorded_at) > 0),
    CHECK ((outcome IN ('failed', 'lost')) = (code IS NOT NULL))
) STRICT;
CREATE INDEX job_attempts_by_job ON job_attempts (job_pk, attempt_pk);

CREATE TABLE job_artifacts (
    link_pk     INTEGER PRIMARY KEY,
    job_pk      INTEGER NOT NULL REFERENCES jobs (job_pk),
    artifact_pk INTEGER NOT NULL UNIQUE REFERENCES artifacts (artifact_pk),
    run_pk      INTEGER NOT NULL REFERENCES runs (run_pk),
    role        TEXT NOT NULL CHECK (role IN ('output', 'manifest', 'log')),
    recorded_at TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;
CREATE INDEX job_artifacts_by_job ON job_artifacts (job_pk, link_pk);

-- The current status of every job: its newest event.
CREATE VIEW job_status AS
SELECT e.job_pk, e.status
FROM job_events AS e
WHERE e.event_pk = (SELECT max(event_pk) FROM job_events WHERE job_pk = e.job_pk);

-- --- append-only triggers (STORE-001) ------------------------------------------------------------

CREATE TRIGGER job_specs_no_update BEFORE UPDATE ON job_specs
BEGIN SELECT RAISE(ABORT, 'append-only: job_specs rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER job_specs_no_delete BEFORE DELETE ON job_specs
BEGIN SELECT RAISE(ABORT, 'append-only: job_specs rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER job_specs_no_replace BEFORE INSERT ON job_specs
WHEN EXISTS (SELECT 1 FROM job_specs WHERE job_pk = NEW.job_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: job_specs rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER job_leases_no_update BEFORE UPDATE ON job_leases
BEGIN SELECT RAISE(ABORT, 'append-only: job_leases rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER job_leases_no_delete BEFORE DELETE ON job_leases
BEGIN SELECT RAISE(ABORT, 'append-only: job_leases rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER job_leases_no_replace BEFORE INSERT ON job_leases
WHEN EXISTS (SELECT 1 FROM job_leases WHERE lease_pk = NEW.lease_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: job_leases rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER job_heartbeats_no_update BEFORE UPDATE ON job_heartbeats
BEGIN SELECT RAISE(ABORT, 'append-only: job_heartbeats rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER job_heartbeats_no_delete BEFORE DELETE ON job_heartbeats
BEGIN SELECT RAISE(ABORT, 'append-only: job_heartbeats rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER job_heartbeats_no_replace BEFORE INSERT ON job_heartbeats
WHEN EXISTS (SELECT 1 FROM job_heartbeats WHERE heartbeat_pk = NEW.heartbeat_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: job_heartbeats rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER job_attempts_no_update BEFORE UPDATE ON job_attempts
BEGIN SELECT RAISE(ABORT, 'append-only: job_attempts rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER job_attempts_no_delete BEFORE DELETE ON job_attempts
BEGIN SELECT RAISE(ABORT, 'append-only: job_attempts rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER job_attempts_no_replace BEFORE INSERT ON job_attempts
WHEN EXISTS (SELECT 1 FROM job_attempts WHERE attempt_pk = NEW.attempt_pk
             OR lease_pk = NEW.lease_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: job_attempts rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER job_artifacts_no_update BEFORE UPDATE ON job_artifacts
BEGIN SELECT RAISE(ABORT, 'append-only: job_artifacts rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER job_artifacts_no_delete BEFORE DELETE ON job_artifacts
BEGIN SELECT RAISE(ABORT, 'append-only: job_artifacts rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER job_artifacts_no_replace BEFORE INSERT ON job_artifacts
WHEN EXISTS (SELECT 1 FROM job_artifacts WHERE link_pk = NEW.link_pk
             OR artifact_pk = NEW.artifact_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: job_artifacts rows cannot be replaced (STORE-001)'); END;

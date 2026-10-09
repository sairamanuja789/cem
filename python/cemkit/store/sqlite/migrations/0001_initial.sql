-- 0001_initial: the provenance store (T12; STORE-001, STORE-002, STORE-003, STORE-004, REL-001).
--
-- Forward-only. Never edit this file after it is merged: the migrator stores its sha256 in
-- schema_version and refuses to run if an applied migration has changed (STORE-004). A schema
-- change is a new file, 0002_<name>.sql. See docs/store.md.
--
-- Append-only (STORE-001): every table below has three triggers that abort
--   * any UPDATE,
--   * any DELETE,
--   * an INSERT whose key already exists (this stops INSERT OR REPLACE, which would otherwise
--     delete the old row without firing the DELETE trigger).
-- Job status changes are new rows in job_events, never an update of jobs.
--
-- STORE-002: runs holds the reproduction metadata. Every other table has run_pk NOT NULL, so every
-- record reaches its kernel, plugin and model versions, git commit, container digest and input hash
-- through its run. A value that could not be determined is the text 'unknown', never NULL.

CREATE TABLE runs (
    run_pk           INTEGER PRIMARY KEY,
    recorded_at      TEXT NOT NULL CHECK (length(recorded_at) > 0),
    kernel_version   TEXT NOT NULL CHECK (length(kernel_version) > 0),
    plugin_versions  TEXT NOT NULL CHECK (
        CASE WHEN plugin_versions = 'unknown' THEN 1
             WHEN json_valid(plugin_versions) THEN json_type(plugin_versions) = 'object'
             ELSE 0 END),
    model_versions   TEXT NOT NULL CHECK (
        CASE WHEN model_versions = 'unknown' THEN 1
             WHEN json_valid(model_versions) THEN json_type(model_versions) = 'object'
             ELSE 0 END),
    git_commit       TEXT NOT NULL CHECK (length(git_commit) > 0),
    container_digest TEXT NOT NULL CHECK (length(container_digest) > 0),
    input_hash       TEXT NOT NULL CHECK (length(input_hash) = 64 AND input_hash NOT GLOB '*[^0-9a-f]*')
) STRICT;

-- One row per spec revision. A revision is immutable (schemas/README.md, SPEC-009).
CREATE TABLE specs (
    spec_pk     INTEGER PRIMARY KEY,
    run_pk      INTEGER NOT NULL REFERENCES runs (run_pk),
    spec_id     TEXT NOT NULL CHECK (length(spec_id) > 0),
    revision    INTEGER NOT NULL CHECK (revision >= 1),
    sha256      TEXT NOT NULL CHECK (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'),
    document    TEXT NOT NULL CHECK (json_valid(document)),
    recorded_at TEXT NOT NULL CHECK (length(recorded_at) > 0),
    UNIQUE (spec_id, revision)
) STRICT;

CREATE TABLE candidates (
    candidate_pk INTEGER PRIMARY KEY,
    run_pk       INTEGER NOT NULL REFERENCES runs (run_pk),
    spec_pk      INTEGER NOT NULL REFERENCES specs (spec_pk),
    candidate_id TEXT NOT NULL CHECK (length(candidate_id) > 0),
    sha256       TEXT NOT NULL CHECK (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'),
    document     TEXT NOT NULL CHECK (json_valid(document)),
    recorded_at  TEXT NOT NULL CHECK (length(recorded_at) > 0),
    UNIQUE (spec_pk, candidate_id)
) STRICT;

CREATE TABLE jobs (
    job_pk       INTEGER PRIMARY KEY,
    run_pk       INTEGER NOT NULL REFERENCES runs (run_pk),
    kind         TEXT NOT NULL CHECK (length(kind) > 0),
    candidate_pk INTEGER REFERENCES candidates (candidate_pk),
    input_hash   TEXT NOT NULL CHECK (length(input_hash) = 64 AND input_hash NOT GLOB '*[^0-9a-f]*'),
    recorded_at  TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;

-- Job status history. The current status is the row with the highest event_pk. The states are the
-- ones ORC-004 names; the allowed transitions belong to the job runner (T13), not to the store.
CREATE TABLE job_events (
    event_pk    INTEGER PRIMARY KEY,
    job_pk      INTEGER NOT NULL REFERENCES jobs (job_pk),
    run_pk      INTEGER NOT NULL REFERENCES runs (run_pk),
    status      TEXT NOT NULL CHECK (status IN ('queued', 'running', 'publishing', 'done', 'failed')),
    reason      TEXT,
    recorded_at TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;
CREATE INDEX job_events_by_job ON job_events (job_pk, event_pk);

CREATE TABLE results (
    result_pk    INTEGER PRIMARY KEY,
    run_pk       INTEGER NOT NULL REFERENCES runs (run_pk),
    candidate_pk INTEGER NOT NULL REFERENCES candidates (candidate_pk),
    job_pk       INTEGER REFERENCES jobs (job_pk),
    status       TEXT NOT NULL CHECK (status IN ('ok', 'failed')),
    sha256       TEXT NOT NULL CHECK (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'),
    document     TEXT NOT NULL CHECK (json_valid(document)),
    recorded_at  TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;
CREATE INDEX results_by_candidate ON results (candidate_pk, result_pk);

-- Failures of jobs or of the orchestration itself, classified by a REL-002 code. A failed
-- evaluation result is a results row with status 'failed'.
CREATE TABLE failures (
    failure_pk   INTEGER PRIMARY KEY,
    run_pk       INTEGER NOT NULL REFERENCES runs (run_pk),
    job_pk       INTEGER REFERENCES jobs (job_pk),
    candidate_pk INTEGER REFERENCES candidates (candidate_pk),
    code         TEXT NOT NULL CHECK (length(code) > 0),
    message      TEXT NOT NULL CHECK (length(message) > 0),
    details      TEXT NOT NULL CHECK (json_valid(details) AND json_type(details) = 'object'),
    recorded_at  TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;

-- One row per use of an artifact. The file itself is stored once, under its sha256, in the
-- content-addressed directory (STORE-003); many rows may name the same sha256.
CREATE TABLE artifacts (
    artifact_pk INTEGER PRIMARY KEY,
    run_pk      INTEGER NOT NULL REFERENCES runs (run_pk),
    sha256      TEXT NOT NULL CHECK (length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'),
    size_bytes  INTEGER NOT NULL CHECK (size_bytes >= 0),
    name        TEXT NOT NULL CHECK (length(name) > 0),
    media_type  TEXT,
    recorded_at TEXT NOT NULL CHECK (length(recorded_at) > 0)
) STRICT;
CREATE INDEX artifacts_by_sha256 ON artifacts (sha256);

-- --- append-only triggers (STORE-001) ------------------------------------------------------------

CREATE TRIGGER runs_no_update BEFORE UPDATE ON runs
BEGIN SELECT RAISE(ABORT, 'append-only: runs rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER runs_no_delete BEFORE DELETE ON runs
BEGIN SELECT RAISE(ABORT, 'append-only: runs rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER runs_no_replace BEFORE INSERT ON runs
WHEN EXISTS (SELECT 1 FROM runs WHERE run_pk = NEW.run_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: runs rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER specs_no_update BEFORE UPDATE ON specs
BEGIN SELECT RAISE(ABORT, 'append-only: specs rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER specs_no_delete BEFORE DELETE ON specs
BEGIN SELECT RAISE(ABORT, 'append-only: specs rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER specs_no_replace BEFORE INSERT ON specs
WHEN EXISTS (SELECT 1 FROM specs WHERE spec_pk = NEW.spec_pk
             OR (spec_id = NEW.spec_id AND revision = NEW.revision))
BEGIN SELECT RAISE(ABORT, 'append-only: specs rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER candidates_no_update BEFORE UPDATE ON candidates
BEGIN SELECT RAISE(ABORT, 'append-only: candidates rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER candidates_no_delete BEFORE DELETE ON candidates
BEGIN SELECT RAISE(ABORT, 'append-only: candidates rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER candidates_no_replace BEFORE INSERT ON candidates
WHEN EXISTS (SELECT 1 FROM candidates WHERE candidate_pk = NEW.candidate_pk
             OR (spec_pk = NEW.spec_pk AND candidate_id = NEW.candidate_id))
BEGIN SELECT RAISE(ABORT, 'append-only: candidates rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER jobs_no_update BEFORE UPDATE ON jobs
BEGIN SELECT RAISE(ABORT, 'append-only: jobs rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER jobs_no_delete BEFORE DELETE ON jobs
BEGIN SELECT RAISE(ABORT, 'append-only: jobs rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER jobs_no_replace BEFORE INSERT ON jobs
WHEN EXISTS (SELECT 1 FROM jobs WHERE job_pk = NEW.job_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: jobs rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER job_events_no_update BEFORE UPDATE ON job_events
BEGIN SELECT RAISE(ABORT, 'append-only: job_events rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER job_events_no_delete BEFORE DELETE ON job_events
BEGIN SELECT RAISE(ABORT, 'append-only: job_events rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER job_events_no_replace BEFORE INSERT ON job_events
WHEN EXISTS (SELECT 1 FROM job_events WHERE event_pk = NEW.event_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: job_events rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER results_no_update BEFORE UPDATE ON results
BEGIN SELECT RAISE(ABORT, 'append-only: results rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER results_no_delete BEFORE DELETE ON results
BEGIN SELECT RAISE(ABORT, 'append-only: results rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER results_no_replace BEFORE INSERT ON results
WHEN EXISTS (SELECT 1 FROM results WHERE result_pk = NEW.result_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: results rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER failures_no_update BEFORE UPDATE ON failures
BEGIN SELECT RAISE(ABORT, 'append-only: failures rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER failures_no_delete BEFORE DELETE ON failures
BEGIN SELECT RAISE(ABORT, 'append-only: failures rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER failures_no_replace BEFORE INSERT ON failures
WHEN EXISTS (SELECT 1 FROM failures WHERE failure_pk = NEW.failure_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: failures rows cannot be replaced (STORE-001)'); END;

CREATE TRIGGER artifacts_no_update BEFORE UPDATE ON artifacts
BEGIN SELECT RAISE(ABORT, 'append-only: artifacts rows cannot be updated (STORE-001)'); END;
CREATE TRIGGER artifacts_no_delete BEFORE DELETE ON artifacts
BEGIN SELECT RAISE(ABORT, 'append-only: artifacts rows cannot be deleted (STORE-001)'); END;
CREATE TRIGGER artifacts_no_replace BEFORE INSERT ON artifacts
WHEN EXISTS (SELECT 1 FROM artifacts WHERE artifact_pk = NEW.artifact_pk)
BEGIN SELECT RAISE(ABORT, 'append-only: artifacts rows cannot be replaced (STORE-001)'); END;

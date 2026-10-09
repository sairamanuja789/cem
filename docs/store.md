# Provenance store

`python/cemkit/store/` records every spec, candidate, job, result, failure and artifact, with what is
needed to reproduce it (STORE-001 to STORE-004, REL-001). All database access in cemkit goes through
this package. Decisions and their reasons: [ADR-010](adr/010-store-schema.md).

```python
from cemkit.store import capture_run_metadata, input_hash, open_store

with open_store(Path("runs/store")) as store:  # creates and migrates if needed
    run = store.record_run(
        capture_run_metadata(
            spec, kernel_version="0.1.0", plugin_versions={...}, model_versions={...}
        )
    )
    store.record_spec(run, spec)
    (candidate, *_) = store.record_candidates(run, candidates)  # one transaction for the batch
    job = store.create_job(run, "l1_evaluation", input_hash(batch), candidate)
    store.record_job_event(run, job, "running")
    store.record_result(run, result, job)
    store.record_artifact(run, Path("blade.step"), "blade.step", "model/step")
```

## Layout on disk

```text
<root>/store.sqlite3               SQLite database, WAL mode
<root>/artifacts/sha256/ab/abcd…   artifact files, read-only, named by their sha256
<root>/artifacts/tmp/              files being written
```

## Schema (migration 0001)

| Table | One row per | Key columns |
| --- | --- | --- |
| `runs` | run of the orchestration | STORE-002 fields: `kernel_version`, `plugin_versions`, `model_versions`, `git_commit`, `container_digest`, `input_hash` |
| `specs` | spec revision | `spec_id`, `revision` (unique), `sha256`, `document` |
| `candidates` | candidate of a spec revision | `spec_pk`, `candidate_id` (unique together), `sha256`, `document` |
| `jobs` | job | `kind`, `candidate_pk` (optional), `input_hash` |
| `job_events` | status change of a job | `job_pk`, `status` (`queued`, `running`, `publishing`, `done`, `failed`; ORC-004), `reason` |
| `results` | evaluation result | `candidate_pk`, `job_pk` (optional), `status` (`ok` or `failed`), `sha256`, `document` |
| `failures` | classified failure | `code` (REL-002, from `schemas/cemkit/v1/error-codes.json`), `message`, `details` |
| `artifacts` | use of an artifact | `sha256`, `size_bytes`, `name`, `media_type` |
| `schema_version` | applied migration | `version`, `name`, `sha256` of the file, `applied_at` |

Every table except `runs` and `schema_version` has `run_pk NOT NULL REFERENCES runs`, so every record
reaches the STORE-002 fields of the run that wrote it. Every row also has `recorded_at` (UTC, ISO 8601).

### STORE-002 fields

| Field | Source | When it cannot be determined |
| --- | --- | --- |
| `kernel_version` | passed by the caller (from the kernel binding) | `unknown` |
| `plugin_versions` | passed by the caller, `{name: semver}` as canonical JSON | `unknown` |
| `model_versions` | passed by the caller, `{name: semver}` as canonical JSON | `unknown` |
| `git_commit` | `.git/HEAD`, loose refs, `packed-refs` (no process is started) | `unknown` |
| `container_digest` | `CEMKIT_CONTAINER_DIGEST`, set by `scripts/dev.sh` to the image ID | `unknown` |
| `input_hash` | sha256 of the canonical JSON of the run's inputs | always known |

`unknown` is stored as that text, never as NULL. A present but malformed value (a digest that is not
`sha256:<64 hex>`, a version that is not semver) is an error, not `unknown`. `git_commit` is HEAD only:
uncommitted changes are not detected.

### Documents

Specs, candidates and results are checked against `schemas/` before they are written and stored as
canonical JSON (sorted keys, no whitespace, no NaN or infinity) with its sha256. A spec revision or a
candidate that is already stored may be recorded again only with identical content; the existing record
is returned. A result must name a candidate that is already stored.

## Append-only rules (STORE-001)

- Nothing is updated or deleted. Each table has three triggers that abort with
  `append-only: …`: on UPDATE, on DELETE, and on an INSERT whose key already exists (this blocks
  `INSERT OR REPLACE`, which would otherwise delete a row silently).
- A job's status changes by appending a `job_events` row; `store.job_status(job)` is the newest one.
  Which transitions are allowed is decided by the job runner (T13), not the store.
- A corrected spec is a new revision. A corrected result is a new result row.
- Records returned by the store are frozen dataclasses; documents are kept as JSON text and
  `record.document()` returns a fresh copy.

## Crash safety (REL-001)

- WAL mode, `synchronous = FULL`, foreign keys on.
- Each state change is one `BEGIN IMMEDIATE … COMMIT` transaction. A batch of candidates is one
  transaction; creating a job writes the job and its `queued` event in one transaction.
- A process killed at any point leaves a database that passes `PRAGMA integrity_check` and contains
  only whole transactions (tested by killing a child process with SIGKILL during writes).
- Artifact files are written to `tmp/`, fsynced, made read-only and moved into place with
  `os.replace`. The database row is written after the file is in place, so a row never names a missing
  file. `read_bytes` checks the content against its sha256.

## Migrations (STORE-004)

Schema changes are made only by migration files in `python/cemkit/store/sqlite/migrations/`.

1. Add a new file `NNNN_<name>.sql`, numbered one above the highest existing file (lower-case name,
   digits and underscores). Plain SQL only; no `BEGIN` or `COMMIT` (the migrator wraps each file in a
   transaction).
2. Add the append-only triggers for any new table.
3. Never edit or delete a merged migration: the migrator stores each file's sha256 in
   `schema_version` and refuses to run if an applied file has changed.
4. Add a test, and update the schema table above.

`open_store` migrates automatically; `cemkit.store.sqlite.migrate.migrate(path)` does it explicitly.
Running it again applies nothing. A failing migration is rolled back and leaves the previous version.
A database newer than the code is refused. There is no downgrade.

## Not yet covered

- STORE-005, the design revision chain (`store/lineage.py`), is a later task.
- Nightly backup of the store (architecture.md, "Data safety").
- A sweep for unreferenced artifact files and stray files in `tmp/`.

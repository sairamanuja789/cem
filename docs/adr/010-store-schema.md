# ADR-010: Provenance store schema and run metadata

- Status: proposed (needs owner review)
- Date: 2026-10-10
- Requirements affected: STORE-001, STORE-002, STORE-003, STORE-004, REL-001, ORC-001, ORC-004, SPEC-009

## Context
T12 builds the provenance store (`python/cemkit/store/`). The architecture already fixes SQLite in WAL
mode with a content-addressed artifact directory (architecture.md, "Storage and provenance"), and the
layout in ADR-009 (`store/port.py`, `store/sqlite/`, `store/artifacts.py`). These questions were still
open:

1. How "every record shall include" the STORE-002 fields is met without repeating six columns on every row.
2. What "container digest" means when there is no registry (ADR-008 builds images locally).
3. How the git commit is read when only the orchestration runner may start processes (python/CLAUDE.md).
4. How strict append-only is, and where job status lives.
5. How durable a commit is.

## Options considered
1. **STORE-002 fields on every table.** Simple to query, but six columns repeated on seven tables and
   easy to get out of step.
2. **A `runs` table with the STORE-002 fields, and `run_pk NOT NULL REFERENCES runs` on every other
   table.** One place per run; the database refuses a record without a run.

For the container digest: (a) a registry `RepoDigest` — none exists until a registry is used; (b) the
local image ID (`docker image inspect --format '{{.Id}}'`, the sha256 of the image config), which
identifies the exact image built from `docker/pins.env`.

For the git commit: (a) run `git rev-parse` — not allowed outside the runner; (b) read `.git/HEAD`,
loose refs and `packed-refs` directly (worktrees through `gitdir:` and `commondir`).

## Decision
- **Run metadata:** option 2. `runs` holds kernel version, plugin versions, model versions, git commit,
  container digest and input hash, each `NOT NULL`. Every other table has `run_pk NOT NULL` with a
  foreign key to `runs`. A value that cannot be determined is the text `unknown`; it is never NULL and
  never dropped. Callers say "unknown" by passing `None`; passing the string `"unknown"` is refused, so a
  typo cannot pass for a version. An empty plugin or model mapping means "none used", which is known.
  Versions are semantic versions (PHY-004).
- **Container digest:** the local image ID, passed into the container by `scripts/dev.sh` as
  `CEMKIT_CONTAINER_DIGEST`. Format `sha256:<64 hex>`; a malformed value is an error.
- **Git commit:** read from `.git` without starting a process. It is HEAD only: uncommitted changes are
  not detected.
- **Append-only:** every table, including `schema_version`, has triggers that abort UPDATE, DELETE and
  an INSERT whose key already exists (which would otherwise let `INSERT OR REPLACE` delete a row without
  firing the DELETE trigger). Job status changes are rows in `job_events`; the current status is the
  newest event. The store accepts the five ORC-004 states; the allowed transitions are the job
  runner's rule (T13).
- **Immutability of documents:** specs, candidates and results are checked against `schemas/` before
  they are written and stored as canonical JSON with their sha256. A spec revision or a candidate may be
  recorded again only with identical content (the existing record is returned).
- **Durability:** `PRAGMA synchronous = FULL` and one `BEGIN IMMEDIATE` transaction per state change,
  so a returned commit survives power loss (ORC-001), not only a process crash.
- **Artifacts:** one file per content (sha256), written to `tmp/`, fsynced, made read-only and moved in
  with `os.replace`; one `artifacts` row per use. The file is published before its row is written.
- **Migrations:** numbered plain-SQL files; the migrator stores each file's sha256 and refuses an edited
  applied migration or a database newer than the code.

## Consequences
- Every record reaches its reproduction metadata through one join; a report can list a run's unknown
  fields (`RunMetadata.unknown_fields()`).
- The kernel, plugin and model versions stay `unknown` until the kernel binding (T11) reports them and
  the orchestration (T13) passes them in. Reports must show them as unknown, not hide them.
- A dirty working tree is not recorded. Revisit if reproductions fail for that reason: the runner
  could record `git status --porcelain` output as an artifact.
- The image ID is local to the machine that built the image. When images are pushed to a registry,
  record the registry digest as well (new migration).
- Append-only is enforced against the application and against accidental SQL. A local user can still
  drop a trigger; protection against that needs backups (architecture.md, "Data safety"), not SQLite.
- A crash between publishing an artifact file and writing its row leaves an unreferenced file. This is
  harmless; a later sweep may remove such files and stray files in `tmp/`.
- Adding a job state or any column is a new migration file.
- STORE-005 (design revision chain, `store/lineage.py`) is not part of T12.

"""Forward-only SQL migrations (STORE-004) and connection settings (REL-001) for the SQLite store.

Migrations are plain SQL files in migrations/, named NNNN_<name>.sql and numbered from 0001 without
gaps. Each is applied in its own transaction together with its row in schema_version (version, name,
sha256 of the file, time applied); SQLite DDL is transactional, so a failing migration leaves the
previous version intact. Running the migrator again applies nothing.

The migrator refuses to run if an applied migration's file has changed since (its sha256 differs)
or if the database is newer than the code. There is no downgrade: a mistake is fixed by a new
migration.
"""

from __future__ import annotations

import re
import sqlite3
from collections.abc import Iterator
from contextlib import closing, contextmanager
from dataclasses import dataclass
from datetime import UTC, datetime
from importlib.resources import files
from importlib.resources.abc import Traversable
from pathlib import Path

from cemkit.store.errors import MigrationError, StoreError
from cemkit.store.provenance import sha256_hex

_FILE = re.compile(r"(\d{4})_([a-z0-9_]+)\.sql")

# Bookkeeping owned by the migrator itself, not by a migration. Append-only like every other table.
_BOOTSTRAP = """
CREATE TABLE IF NOT EXISTS schema_version (
    version    INTEGER PRIMARY KEY CHECK (version >= 1),
    name       TEXT NOT NULL,
    sha256     TEXT NOT NULL CHECK (length(sha256) = 64),
    applied_at TEXT NOT NULL
) STRICT;
CREATE TRIGGER IF NOT EXISTS schema_version_no_update BEFORE UPDATE ON schema_version
BEGIN SELECT RAISE(ABORT, 'append-only: schema_version rows cannot be updated (STORE-004)'); END;
CREATE TRIGGER IF NOT EXISTS schema_version_no_delete BEFORE DELETE ON schema_version
BEGIN SELECT RAISE(ABORT, 'append-only: schema_version rows cannot be deleted (STORE-004)'); END;
CREATE TRIGGER IF NOT EXISTS schema_version_no_replace BEFORE INSERT ON schema_version
WHEN EXISTS (SELECT 1 FROM schema_version WHERE version = NEW.version)
BEGIN SELECT RAISE(ABORT, 'append-only: schema_version rows cannot be replaced (STORE-004)'); END;
"""


@dataclass(frozen=True, slots=True)
class Migration:
    version: int
    name: str
    sql: str

    @property
    def sha256(self) -> str:
        return sha256_hex(self.sql)


def load_migrations(directory: Traversable | Path | None = None) -> tuple[Migration, ...]:
    """Every migration file, in order. Raises MigrationError for a bad name, gap or duplicate."""
    source = files("cemkit.store.sqlite") / "migrations" if directory is None else directory
    found: list[Migration] = []
    for entry in source.iterdir():
        if not entry.name.endswith(".sql"):
            continue
        match = _FILE.fullmatch(entry.name)
        if match is None:
            raise MigrationError(f"migration file {entry.name!r} is not named NNNN_<name>.sql")
        sql = entry.read_text(encoding="utf-8")
        found.append(Migration(int(match.group(1)), match.group(2), sql))
    found.sort(key=lambda m: m.version)
    versions = [m.version for m in found]
    if versions != list(range(1, len(found) + 1)):
        raise MigrationError(f"migrations must be numbered 1, 2, 3, ... without gaps: {versions}")
    return tuple(found)


def connect(database: Path) -> sqlite3.Connection:
    """A connection in WAL mode with foreign keys on and full fsync on commit (REL-001, ORC-001).

    autocommit=True means Python issues no implicit BEGIN or COMMIT; every write goes through
    transaction(), which takes the write lock up front with BEGIN IMMEDIATE.
    """
    connection = sqlite3.connect(database, autocommit=True)
    try:
        mode = connection.execute("PRAGMA journal_mode = WAL").fetchone()[0]
        if mode != "wal":
            raise StoreError(f"{database}: could not switch to WAL mode (got {mode!r})")
        # FULL: a commit is on disk before it returns, so power loss cannot lose completed work.
        connection.execute("PRAGMA synchronous = FULL")
        connection.execute("PRAGMA foreign_keys = ON")
    except BaseException:
        connection.close()
        raise
    return connection


@contextmanager
def transaction(connection: sqlite3.Connection) -> Iterator[sqlite3.Connection]:
    """One write transaction: committed on success, rolled back on any exception."""
    connection.execute("BEGIN IMMEDIATE")
    try:
        yield connection
    except BaseException:
        if connection.in_transaction:
            connection.execute("ROLLBACK")
        raise
    connection.execute("COMMIT")


def _applied(connection: sqlite3.Connection) -> dict[int, str]:
    rows = connection.execute("SELECT version, sha256 FROM schema_version").fetchall()
    return {int(version): str(digest) for version, digest in rows}


def _check(applied: dict[int, str], migrations: tuple[Migration, ...]) -> None:
    known = {m.version: m for m in migrations}
    for version, digest in sorted(applied.items()):
        if version not in known:
            raise MigrationError(
                f"database schema version {max(applied)} is newer than this code "
                f"(knows up to {len(migrations)})"
            )
        if known[version].sha256 != digest:
            raise MigrationError(
                f"migration {version} ({known[version].name}) changed after it was applied; "
                "write a new migration instead of editing an applied one"
            )


def current_version(database: Path) -> int:
    """The highest applied migration, 0 for an empty database."""
    with closing(sqlite3.connect(database, autocommit=True)) as connection:
        exists = connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'schema_version'"
        ).fetchone()
        if exists is None:
            return 0
        value = connection.execute("SELECT max(version) FROM schema_version").fetchone()[0]
        return int(value or 0)


def migrate(database: Path, migrations: tuple[Migration, ...] | None = None) -> tuple[int, ...]:
    """Bring the database up to the latest migration. Returns the versions applied by this call."""
    plan = load_migrations() if migrations is None else migrations
    applied_now: list[int] = []
    with closing(connect(database)) as connection:
        with transaction(connection):
            connection.executescript(_BOOTSTRAP)
            _check(_applied(connection), plan)
        for migration in plan:
            with transaction(connection):
                applied = _applied(connection)  # re-read under the write lock
                _check(applied, plan)
                if migration.version in applied:
                    continue
                connection.executescript(migration.sql)
                connection.execute(
                    "INSERT INTO schema_version (version, name, sha256, applied_at) "
                    "VALUES (?, ?, ?, ?)",
                    (
                        migration.version,
                        migration.name,
                        migration.sha256,
                        datetime.now(UTC).isoformat(timespec="microseconds"),
                    ),
                )
            applied_now.append(migration.version)
    return tuple(applied_now)

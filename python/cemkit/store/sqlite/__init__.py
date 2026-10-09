"""SQLite backend of the store: WAL mode, forward SQL migrations, append-only triggers."""

from __future__ import annotations

from typing import TYPE_CHECKING

from cemkit.store.sqlite.backend import DATABASE_NAME, SqliteStore, open_store

if TYPE_CHECKING:
    from cemkit.store.port import Store

    _conforms: type[Store] = SqliteStore

__all__ = ["DATABASE_NAME", "SqliteStore", "open_store"]

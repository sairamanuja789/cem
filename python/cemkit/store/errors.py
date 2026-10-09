"""Exceptions raised by the provenance store."""

from __future__ import annotations


class StoreError(Exception):
    """The store refused an operation or is not usable."""


class DocumentRejected(StoreError):
    """A document failed its schema, broke immutability, or names a record that does not exist."""


class MigrationError(StoreError):
    """The migration files or the database schema version are not consistent (STORE-004)."""


class ArtifactCorrupt(StoreError):
    """A stored artifact no longer matches the sha256 it is named by (STORE-003)."""


class LeaseLost(StoreError):
    """The lease is no longer current: the attempt ended, or another worker reconciled the job.

    The worker holding it must stop the job's child process and record nothing more for it.
    """

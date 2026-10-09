"""Content-addressed artifact directory (STORE-003).

Layout under the root:

    sha256/<first two hex digits>/<64 hex digits>   the files, read-only, named by their sha256
    tmp/                                            files being written

A file is written to tmp/, flushed and fsynced, made read-only, then moved into place with
os.replace, which is atomic on one filesystem. A reader therefore never sees a partial file, and a
crash leaves at most a stray file in tmp/. Identical content gets the same name, so it is stored
once. Callers record the artifact in the database only after the file is in place, so a database
row never names a missing file.

Names given to artifacts by people or by an LLM are labels in the database, never paths: a path is
only ever built from a checked sha256.
"""

from __future__ import annotations

import hashlib
import os
import re
import tempfile
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

from cemkit.store.errors import ArtifactCorrupt

_SHA256_HEX = re.compile(r"[0-9a-f]{64}")
_CHUNK = 1 << 20


@dataclass(frozen=True, slots=True)
class StoredArtifact:
    sha256: str
    size_bytes: int
    created: bool
    """False if an identical file was already stored."""


def _fsync_directory(directory: Path) -> None:
    descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


class ArtifactStore:
    def __init__(self, root: Path) -> None:
        self._root = root
        self._objects = root / "sha256"
        self._tmp = root / "tmp"
        self._objects.mkdir(parents=True, exist_ok=True)
        self._tmp.mkdir(parents=True, exist_ok=True)

    @property
    def root(self) -> Path:
        return self._root

    def path_of(self, digest: str) -> Path:
        if not isinstance(digest, str) or not _SHA256_HEX.fullmatch(digest):
            raise ValueError(f"not a lower-case sha256 hex digest: {digest!r}")
        return self._objects / digest[:2] / digest

    def contains(self, digest: str) -> bool:
        return self.path_of(digest).is_file()

    def put_bytes(self, data: bytes) -> StoredArtifact:
        return self._put((data,))

    def put_file(self, source: Path) -> StoredArtifact:
        with source.open("rb") as stream:
            return self._put(iter(lambda: stream.read(_CHUNK), b""))

    def read_bytes(self, digest: str) -> bytes:
        """The artifact's content, checked against its name."""
        data = self.path_of(digest).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ArtifactCorrupt(f"artifact {digest} does not match its sha256")
        return data

    def _put(self, chunks: Iterable[bytes]) -> StoredArtifact:
        descriptor, name = tempfile.mkstemp(dir=self._tmp, prefix="put-")
        temporary = Path(name)
        try:
            digest = hashlib.sha256()
            size = 0
            with os.fdopen(descriptor, "wb") as out:
                for chunk in chunks:
                    digest.update(chunk)
                    out.write(chunk)
                    size += len(chunk)
                out.flush()
                os.fsync(out.fileno())
            hexdigest = digest.hexdigest()
            target = self.path_of(hexdigest)
            if target.is_file():
                return StoredArtifact(hexdigest, size, created=False)
            target.parent.mkdir(exist_ok=True)
            temporary.chmod(0o444)
            os.replace(temporary, target)
            _fsync_directory(target.parent)
            _fsync_directory(self._objects)
            return StoredArtifact(hexdigest, size, created=True)
        finally:
            temporary.unlink(missing_ok=True)

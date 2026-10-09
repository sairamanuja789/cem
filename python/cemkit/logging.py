"""Structured logs: one JSON object per line (OBS-001).

Every line has "time" (UTC, ISO 8601) and "event", plus the caller's fields, with sorted keys.
NaN and infinity are refused, like everywhere else in cemkit. Never pass secrets or the environment
(SEC-001).
"""

from __future__ import annotations

import json
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, TextIO


class JsonLogger:
    """Appends JSON lines to a file (created with its directory) or writes them to a stream.

    A file is opened for each line, so no handle is held between lines and a crash loses at most
    the line being written.
    """

    def __init__(self, path: Path | None = None, stream: TextIO | None = None) -> None:
        self._path = path
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)
        self._stream: TextIO = stream or sys.stderr

    def emit(self, event: str, **fields: Any) -> None:
        record = {"time": datetime.now(UTC).isoformat(timespec="microseconds"), "event": event}
        record.update(fields)
        line = json.dumps(
            record, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False
        )
        if self._path is None:
            self._stream.write(line + "\n")
            self._stream.flush()
            return
        with self._path.open("a", encoding="utf-8") as stream:
            stream.write(line + "\n")

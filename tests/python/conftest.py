"""Shared pytest setup.

The dev venv does not install cemkit (ADR-013: `tool.uv.package = false`); pytest finds it through
`pythonpath = ["python"]`, which only affects this process. Tests that start child Python processes
(the T13 orchestration fakes and workers) need cemkit too, so the session exports PYTHONPATH for
them, as scripts/cemkit does for the CLI.
"""

import os
from pathlib import Path

PYTHON_ROOT = str(Path(__file__).resolve().parents[2] / "python")
_existing = os.environ.get("PYTHONPATH", "")
if PYTHON_ROOT not in _existing.split(os.pathsep):
    os.environ["PYTHONPATH"] = PYTHON_ROOT + (os.pathsep + _existing if _existing else "")

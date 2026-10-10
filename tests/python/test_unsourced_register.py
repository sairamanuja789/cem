"""CLAUDE.md rule 7: the committed UNSOURCED register lists every marked line (never stale)."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.req("PHY-002")
def test_the_unsourced_register_is_up_to_date() -> None:
    spec = importlib.util.spec_from_file_location(
        "gen_unsourced_register", ROOT / "scripts" / "gen_unsourced_register.py"
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    committed = (ROOT / "docs" / "unsourced.md").read_text(encoding="utf-8")
    assert committed == module.render(), (
        "docs/unsourced.md is stale: run uv run python scripts/gen_unsourced_register.py"
    )

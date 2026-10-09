"""REL-002: scripts/gen_error_codes.py renders the error-code files from the kernel table.

The kernel table (kernel/cemkit/core/error.hpp) is the single source. These tests use a fake table
tool so they run without a kernel build; the ctest `error_codes_generated_files_are_current` runs
the real tool against the committed files.
"""

import json
import stat
import sys
from pathlib import Path
from typing import Any

import jsonschema
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import gen_error_codes as gen

CODES = [
    {"code": 1, "name": "spec_rejected", "meaning": "The spec failed validation."},
    {"code": 2, "name": "sim_untrusted", "meaning": "A simulation did not pass the trust gate."},
    {"code": 3, "name": "mesh_failed", "meaning": "Meshing failed."},
    {"code": 4, "name": "resource_exceeded", "meaning": "A job exceeded its budget."},
]


def _fake_tool(tmp_path: Path, codes: list[dict[str, Any]], name: str = "tool") -> Path:
    directory = tmp_path / name
    directory.mkdir()
    data = directory / "codes.json"
    data.write_text(json.dumps(codes))
    tool = directory / "print_error_codes"
    tool.write_text(f"#!/bin/sh\ncat '{data}'\n")
    tool.chmod(tool.stat().st_mode | stat.S_IXUSR)
    return tool


@pytest.mark.req("REL-002")
def test_schema_is_valid_2020_12_and_accepts_only_known_names() -> None:
    schema = json.loads(gen.render_schema(gen.validate(CODES)))
    jsonschema.Draft202012Validator.check_schema(schema)
    validator = jsonschema.Draft202012Validator(schema)
    assert validator.is_valid("sim_untrusted")
    assert not validator.is_valid("failure_cluster")
    assert not validator.is_valid(2)
    assert [(e["const"], e["x-cemkit-code"]) for e in schema["oneOf"]] == [
        ("spec_rejected", 1),
        ("sim_untrusted", 2),
        ("mesh_failed", 3),
        ("resource_exceeded", 4),
    ]


@pytest.mark.req("REL-002")
def test_markdown_lists_every_code_and_the_trust_004_mapping() -> None:
    text = gen.render_markdown(gen.validate(CODES))
    assert "Do not edit" in text
    assert "| 2 | `sim_untrusted` | A simulation did not pass the trust gate. |" in text
    assert "| UNCONVERGED | `sim_untrusted` |" in text
    assert "| MESH\\_INVALID | `mesh_failed` |" in text
    assert "| RESOURCE\\_LIMIT | `resource_exceeded` |" in text


@pytest.mark.req("REL-002")
@pytest.mark.parametrize(
    ("codes", "reason"),
    [
        ([], "no error codes"),
        ([{"code": 2, "name": "a", "meaning": "m"}], "numbered 1"),
        (
            [{"code": 1, "name": "a", "meaning": "m"}, {"code": 3, "name": "b", "meaning": "m"}],
            "1..2",
        ),
        (
            [{"code": 1, "name": "a", "meaning": "m"}, {"code": 2, "name": "a", "meaning": "m"}],
            "twice",
        ),
        ([{"code": 1, "name": "Bad-Name", "meaning": "m"}], "snake_case"),
        ([{"code": 1, "name": "a", "meaning": ""}], "meaning"),
        ([{"code": 1, "name": "a"}], "fields"),
    ],
)
def test_malformed_tables_are_rejected(codes: list[dict[str, Any]], reason: str) -> None:
    with pytest.raises(gen.TableError, match=reason):
        gen.validate(codes)


@pytest.mark.req("REL-002")
def test_trust_004_mapping_must_target_existing_codes() -> None:
    with pytest.raises(gen.TableError, match="sim_untrusted"):
        gen.render_markdown(gen.validate([{"code": 1, "name": "mesh_failed", "meaning": "m"}]))


@pytest.mark.req("REL-002")
def test_write_then_check_passes_and_drift_is_detected(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    tool = _fake_tool(tmp_path, CODES)
    root = tmp_path / "repo"
    assert gen.main(["--tool", str(tool), "--root", str(root)]) == 0
    assert gen.main(["--tool", str(tool), "--root", str(root), "--check"]) == 0

    (root / gen.MARKDOWN).write_text("hand edit\n")
    assert gen.main(["--tool", str(tool), "--root", str(root), "--check"]) == 1
    assert str(gen.MARKDOWN) in capsys.readouterr().out

    grown = _fake_tool(
        tmp_path, [*CODES, {"code": 5, "name": "new_code", "meaning": "New."}], name="grown"
    )
    assert gen.main(["--tool", str(grown), "--root", str(root)]) == 0
    assert gen.main(["--tool", str(tool), "--root", str(root), "--check"]) == 1


@pytest.mark.req("REL-002")
def test_check_reports_missing_files(tmp_path: Path) -> None:
    tool = _fake_tool(tmp_path, CODES)
    assert gen.main(["--tool", str(tool), "--root", str(tmp_path / "empty"), "--check"]) == 1


@pytest.mark.req("REL-002")
def test_committed_files_match_the_committed_markdown_and_schema_pair() -> None:
    root = Path(__file__).resolve().parents[2]
    schema = json.loads((root / gen.SCHEMA).read_text())
    codes = [
        {"code": e["x-cemkit-code"], "name": e["const"], "meaning": e["description"]}
        for e in schema["oneOf"]
    ]
    table = gen.validate(codes)
    assert (root / gen.SCHEMA).read_text() == gen.render_schema(table)
    assert (root / gen.MARKDOWN).read_text() == gen.render_markdown(table)

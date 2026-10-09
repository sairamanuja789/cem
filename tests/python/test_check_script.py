"""Regression tests for scripts/check.sh helpers (MAINT-003).

Bug fixed in T02: the coverage floor was silently skipped when some covered directories did not
exist yet, because a failing `find` under `pipefail` was read as "no code".
Scope (spec v1.2): kernel/cemkit/{core,spec,physics}, products/*/common and each family's L1 model.
"""

import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]


def _floor_args(root: Path) -> str:
    result = subprocess.run(
        [
            "bash",
            "-c",
            f'. "{REPO}/scripts/lib/coverage.sh"; set -o pipefail; coverage_floor_args "$1"',
            "_",
            str(root),
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def _write(root: Path, relative: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("int f() { return 0; }\n")


@pytest.mark.req("MAINT-003")
def test_floor_applies_when_only_core_exists_and_spec_and_physics_are_missing(
    tmp_path: Path,
) -> None:
    _write(tmp_path, "kernel/cemkit/core/version.cpp")
    assert _floor_args(tmp_path) == "--fail-under-line 90"


@pytest.mark.req("MAINT-003")
@pytest.mark.parametrize(
    "source",
    [
        "kernel/cemkit/core/x.cpp",
        "kernel/cemkit/spec/x.cpp",
        "kernel/cemkit/physics/x.cpp",
        "kernel/products/fans/common/pressure.cpp",
        "kernel/products/fans/families/axial_ducted/l1_model.cpp",
    ],
)
def test_floor_applies_when_any_covered_module_has_code(tmp_path: Path, source: str) -> None:
    _write(tmp_path, source)
    assert _floor_args(tmp_path) == "--fail-under-line 90"


@pytest.mark.req("MAINT-003")
@pytest.mark.parametrize(
    "source",
    [
        "kernel/cemkit/geometry/occt/occt_backend.cpp",
        "kernel/cemkit/core/tests/test_version.cpp",
        "kernel/products/fans/families/axial_ducted/geometry_recipe.cpp",
        "kernel/testing/toolchain/test_toolchain.cpp",
    ],
)
def test_floor_ignores_code_outside_the_covered_scope(tmp_path: Path, source: str) -> None:
    _write(tmp_path, source)
    assert _floor_args(tmp_path) == ""


@pytest.mark.req("MAINT-003")
def test_no_source_tree_at_all_gives_no_floor_and_no_crash(tmp_path: Path) -> None:
    assert _floor_args(tmp_path) == ""

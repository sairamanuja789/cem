"""FAM-002, UC-09: a family folder builds and registers without edits outside it (ADR-009, ADR-012).

The ctest suites build the stub family and register it (kernel/testing/stub_product,
kernel/testing/family_registry). This check covers the other half: no file outside the family
folder had to change for it. For every family folder (products/*/families/* and the stub under
testing/*/families/*):
  - the platform (kernel/cemkit, kernel/cmake, kernel/CMakeLists.txt) never names it;
  - no build file names it: the product's CMakeLists.txt finds families by folder discovery;
  - outside its folder and outside tests (tests/ folders, test_* files), only its product's
    register.cpp names it (the one registration call ADR-009 asks for, plus the include of the
    family header).
"""

import re
from pathlib import Path

import pytest

KERNEL = Path(__file__).resolve().parents[2] / "kernel"
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h"}


def family_folders(kernel: Path) -> list[Path]:
    """Every <area>/<product>/families/<family>/ folder, sorted."""
    folders = [
        *kernel.glob("products/*/families/*"),
        *kernel.glob("testing/*/families/*"),
    ]
    return sorted(f for f in folders if f.is_dir())


def mentions(path: Path, name: str) -> bool:
    return re.search(rf"\b{re.escape(name)}\b", path.read_text(encoding="utf-8")) is not None


def kernel_files(kernel: Path) -> list[Path]:
    return sorted(
        p
        for p in kernel.rglob("*")
        if p.is_file() and (p.suffix in SOURCE_SUFFIXES or p.name == "CMakeLists.txt")
    )


def is_test(path: Path, kernel: Path) -> bool:
    return "tests" in path.relative_to(kernel).parts or path.name.startswith("test_")


def outside_mentions(kernel: Path, folder: Path) -> list[str]:
    """Non-test kernel files outside the family folder that name the family."""
    name = folder.name
    return [
        p.relative_to(kernel).as_posix()
        for p in kernel_files(kernel)
        if folder not in p.parents and not is_test(p, kernel) and mentions(p, name)
    ]


@pytest.mark.req("FAM-002")
def test_the_stub_family_is_among_the_checked_folders() -> None:
    names = [f.relative_to(KERNEL).as_posix() for f in family_folders(KERNEL)]
    assert "testing/stub_product/families/stub_family" in names


@pytest.mark.req("FAM-002")
@pytest.mark.req("UC-09")
@pytest.mark.parametrize(
    "folder", family_folders(KERNEL), ids=lambda f: f.relative_to(KERNEL).as_posix()
)
def test_a_family_needs_no_edit_outside_its_folder(folder: Path) -> None:
    assert (folder / "CMakeLists.txt").is_file(), "a family folder builds itself"
    assert any(mentions(p, "register_family") for p in folder.glob("*.hpp")), (
        "a family exposes register_family(Registry&)"
    )
    product_register = (folder.parent.parent / "register.cpp").relative_to(KERNEL).as_posix()
    assert outside_mentions(KERNEL, folder) == [product_register]


@pytest.mark.req("FAM-002")
def test_a_family_named_by_the_platform_or_a_build_file_is_caught(tmp_path: Path) -> None:
    kernel = tmp_path / "kernel"
    family = kernel / "products" / "p" / "families" / "f_one"
    family.mkdir(parents=True)
    (family / "CMakeLists.txt").write_text("cemkit_add_family(SOURCES f_one.cpp)\n")
    (family / "f_one.hpp").write_text("void register_family();\n")
    (kernel / "products" / "p" / "register.cpp").write_text('#include "f_one.hpp"\n')
    (kernel / "products" / "p" / "CMakeLists.txt").write_text("cemkit_add_families(t)\n")
    tests = kernel / "products" / "p" / "tests"
    tests.mkdir()
    (tests / "test_p.cpp").write_text("// f_one in a test is fine\n")
    assert family_folders(kernel) == [family]
    assert outside_mentions(kernel, family) == ["products/p/register.cpp"]

    (kernel / "products" / "p" / "CMakeLists.txt").write_text("add_subdirectory(families/f_one)\n")
    platform = kernel / "cemkit" / "spec"
    platform.mkdir(parents=True)
    (platform / "spec.cpp").write_text('auto x = "p.f_one";\n')
    assert outside_mentions(kernel, family) == [
        "cemkit/spec/spec.cpp",
        "products/p/CMakeLists.txt",
        "products/p/register.cpp",
    ]

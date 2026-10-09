"""ADR-009 dependency rules: scripts/check_boundaries.py must reject every broken rule (FAM-002).

Each test builds a small synthetic tree or graph that breaks exactly one rule, plus clean controls.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import check_boundaries as cb


def _tree(root: Path, files: dict[str, str]) -> Path:
    kernel = root / "kernel"
    for relative, text in files.items():
        path = kernel / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return kernel


@pytest.mark.req("FAM-002")
@pytest.mark.parametrize(
    ("path", "module"),
    [
        ("cemkit/core/version.cpp", "cemkit/core"),
        ("cemkit/core/tests/test_version.cpp", "cemkit/core"),
        ("cemkit/core/detail/x.hpp", "cemkit/core"),
        ("cemkit/geometry/port/backend.hpp", "cemkit/geometry/port"),
        ("cemkit/geometry/occt/occt_backend.cpp", "cemkit/geometry/occt"),
        ("products/fans/common/pressure.hpp", "products/fans/common"),
        ("products/fans/families/axial_ducted/l1_model.cpp", "products/fans/families/axial_ducted"),
        ("products/fans/register.cpp", "products/fans"),
        ("testing/toolchain/test_toolchain.cpp", "testing/toolchain"),
    ],
)
def test_module_of_maps_paths_to_modules(path: str, module: str) -> None:
    assert cb.module_of(path) == module


@pytest.mark.req("FAM-002")
def test_target_name_rule_matches_cmake() -> None:
    assert cb.target_of("cemkit/core") == "cemkit_core"
    assert cb.target_of("products/fans/common") == "cemkit_fans_common"
    assert (
        cb.target_of("products/fans/families/axial_ducted") == "cemkit_fans_families_axial_ducted"
    )


ALLOWED = [
    ("cemkit/spec", "cemkit/core"),
    ("cemkit/physics", "cemkit/core"),
    ("cemkit/geometry/port", "cemkit/core"),
    ("cemkit/geometry/occt", "cemkit/geometry/port"),
    ("cemkit/product", "cemkit/geometry/port"),
    ("products/fans/common", "cemkit/product"),
    ("products/fans/families/axial_ducted", "products/fans/common"),
    ("products/fans", "products/fans/families/axial_ducted"),
    ("cemkit/capi", "products/fans"),
    ("cemkit/capi", "cemkit/geometry/occt"),
]
FORBIDDEN = [
    ("cemkit/core", "cemkit/spec"),
    ("cemkit/spec", "cemkit/physics"),
    ("cemkit/spec", "products/fans/common"),
    ("cemkit/geometry/port", "cemkit/geometry/occt"),
    ("cemkit/geometry/occt", "cemkit/mesh/port"),
    ("cemkit/product", "cemkit/geometry/occt"),
    ("cemkit/product", "products/fans/common"),
    ("products/fans/common", "products/fans/families/axial_ducted"),
    ("products/fans/common", "cemkit/capi"),
    ("products/fans/families/axial_ducted", "products/fans/families/ceiling"),
    ("products/fans/families/axial_ducted", "products/pumps/common"),
    ("products/fans/common", "cemkit/geometry/occt"),
    ("cemkit/core", "testing/toolchain"),
]


@pytest.mark.req("FAM-002")
@pytest.mark.parametrize(("src", "dst"), ALLOWED)
def test_allowed_dependencies(src: str, dst: str) -> None:
    assert cb.allowed(src, dst)


@pytest.mark.req("FAM-002")
@pytest.mark.parametrize(("src", "dst"), FORBIDDEN)
def test_forbidden_dependencies(src: str, dst: str) -> None:
    assert not cb.allowed(src, dst)


@pytest.mark.req("FAM-002")
def test_include_scan_accepts_a_clean_tree(tmp_path: Path) -> None:
    kernel = _tree(
        tmp_path,
        {
            "cemkit/core/units.hpp": "#pragma once\n#include <mp-units/systems/si.h>\n",
            "cemkit/spec/field.hpp": '#include "cemkit/core/units.hpp"\n',
            "cemkit/core/tests/t.cpp": (
                '#include "cemkit/core/units.hpp"\n#include "testing/helpers/h.hpp"\n'
            ),
            "testing/helpers/h.hpp": "#pragma once\n",
        },
    )
    assert cb.scan_includes(kernel) == []


@pytest.mark.req("FAM-002")
def test_include_scan_rejects_a_platform_module_including_a_product(tmp_path: Path) -> None:
    kernel = _tree(
        tmp_path,
        {
            "cemkit/spec/field.hpp": '#include "products/fans/common/pressure.hpp"\n',
            "products/fans/common/pressure.hpp": "#pragma once\n",
        },
    )
    violations = cb.scan_includes(kernel)
    assert len(violations) == 1
    assert "cemkit/spec" in violations[0] and "products/fans/common" in violations[0]


@pytest.mark.req("FAM-002")
def test_include_scan_rejects_another_modules_detail_header(tmp_path: Path) -> None:
    kernel = _tree(
        tmp_path,
        {
            "cemkit/core/detail/impl.hpp": "#pragma once\n",
            "cemkit/core/units.hpp": '#include "cemkit/core/detail/impl.hpp"\n',
            "cemkit/spec/field.hpp": '#include "cemkit/core/detail/impl.hpp"\n',
        },
    )
    violations = cb.scan_includes(kernel)
    assert len(violations) == 1
    assert "detail" in violations[0] and "cemkit/spec" in violations[0]


@pytest.mark.req("FAM-002")
def test_include_scan_rejects_production_code_including_testing(tmp_path: Path) -> None:
    kernel = _tree(
        tmp_path,
        {
            "testing/helpers/h.hpp": "#pragma once\n",
            "cemkit/core/units.hpp": '#include "testing/helpers/h.hpp"\n',
        },
    )
    assert len(cb.scan_includes(kernel)) == 1


DOT = """digraph "cemkit" {
"node0" [ label = "cemkit_core", shape = octagon ];
"node1" [ label = "cemkit_spec", shape = octagon ];
"node2" [ label = "mp-units::mp-units", shape = octagon ];
"node3" [ label = "cemkit_core_tests", shape = egg ];
"node1" -> "node0"  // cemkit_spec -> cemkit_core
"node0" -> "node2"  // cemkit_core -> mp-units::mp-units
"node3" -> "node0"  // cemkit_core_tests -> cemkit_core
EDGE
}
"""
MANIFEST = "cemkit/core=cemkit_core\ncemkit/spec=cemkit_spec\n"


@pytest.mark.req("FAM-002")
def test_link_graph_accepts_allowed_edges() -> None:
    assert cb.check_link_graph(DOT.replace("EDGE", ""), MANIFEST) == []


@pytest.mark.req("FAM-002")
def test_link_graph_rejects_core_linking_spec() -> None:
    dot = DOT.replace("EDGE", '"node0" -> "node1"  // cemkit_core -> cemkit_spec')
    violations = cb.check_link_graph(dot, MANIFEST)
    assert len(violations) == 1
    assert "cemkit_core" in violations[0] and "cemkit_spec" in violations[0]


@pytest.mark.req("FAM-002")
def test_python_platform_must_not_import_products(tmp_path: Path) -> None:
    pkg = tmp_path / "cemkit"
    (pkg / "store").mkdir(parents=True)
    (pkg / "products" / "fans").mkdir(parents=True)
    (pkg / "store" / "ok.py").write_text("import json\nfrom cemkit.store import port\n")
    (pkg / "store" / "bad.py").write_text("from cemkit.products.fans import cli\n")
    (pkg / "products" / "fans" / "glue.py").write_text("from cemkit.store import port\n")
    violations = cb.scan_python_imports(pkg)
    assert len(violations) == 1
    assert "bad.py" in violations[0]

"""ADR-009 dependency rules for the kernel and the Python orchestration package.

Three checks, run by scripts/check.sh:
  1. include scan: every quoted #include under kernel/ must respect the module rules, and a module's
     detail/ headers are private to it;
  2. link graph: every link between module targets (from `cmake --graphviz`) must respect the same
     rules;
  3. Python imports: the cemkit platform package must never import cemkit.products.*.

Usage:
  check_boundaries.py --kernel kernel --python python/cemkit \
      --manifest build/<preset>/cemkit_modules.txt --dot build/<preset>/graph/cemkit.dot
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

PLATFORM_ROOT = ("core", "spec", "physics", "product", "capi")
ENGINE_AREAS = ("geometry", "mesh", "simulation")
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def module_of(path: str) -> str | None:
    """Module that owns a path relative to kernel/ (tests/ and detail/ belong to their module)."""
    parts = path.split("/")
    if parts[0] == "cemkit" and len(parts) >= 2:
        if parts[1] in PLATFORM_ROOT:
            return f"cemkit/{parts[1]}"
        if parts[1] in ENGINE_AREAS and len(parts) >= 3:
            return f"cemkit/{parts[1]}/{parts[2]}"
        return None
    if parts[0] == "products" and len(parts) >= 3:
        product = parts[1]
        if parts[2] == "common":
            return f"products/{product}/common"
        if parts[2] == "families" and len(parts) >= 4:
            return f"products/{product}/families/{parts[3]}"
        return f"products/{product}"
    if parts[0] == "testing" and len(parts) >= 2:
        return f"testing/{parts[1]}"
    return None


def target_of(module: str) -> str:
    """CMake target name for a module; the same rule as cemkit_module_target() in kernel/cmake."""
    rest = re.sub(r"^(cemkit|products)/", "", module)
    return "cemkit_" + rest.replace("/", "_")


def _kind(module: str) -> str:
    parts = module.split("/")
    if parts[0] == "testing":
        return "testing"
    if parts[0] == "products":
        if len(parts) == 2:
            return "product_register"
        return "product_common" if parts[2] == "common" else "family"
    if parts[1] in ENGINE_AREAS:
        return "port" if parts[2] == "port" else "adapter"
    return parts[1]  # core, spec, physics, product, capi


def _product(module: str) -> str:
    return module.split("/")[1]


def allowed(src: str, dst: str) -> bool:
    """True if module src may depend on module dst (docs/repository-structure.md, section 3)."""
    if src == dst:
        return True
    s, d = _kind(src), _kind(dst)
    if s == "testing":
        return True
    if d == "testing":
        return False
    platform_basics = {"core", "spec", "physics", "port"}
    if s == "core":
        return False
    if s in {"spec", "physics", "port"}:
        return d == "core"
    if s == "adapter":
        own_port = "/".join(src.split("/")[:2]) + "/port"
        return d == "core" or dst == own_port
    if s == "product":
        return d in platform_basics
    if s == "capi":
        return True
    same_product = _product(src) == _product(dst) if d in {"product_common", "family"} else False
    if s == "product_common":
        return d in platform_basics | {"product"}
    if s == "family":
        return d in platform_basics | {"product"} or (d == "product_common" and same_product)
    if s == "product_register":
        return d in platform_basics | {"product"} or (
            d in {"product_common", "family"} and same_product
        )
    return False


def _is_test_file(relative: str) -> bool:
    return "/tests/" in relative or relative.startswith("testing/")


def scan_includes(kernel: Path) -> list[str]:
    """Violations of the include rules under kernel/ (sorted, deterministic)."""
    violations: list[str] = []
    for file in sorted(kernel.rglob("*")):
        if file.suffix not in {".hpp", ".cpp", ".h"} or not file.is_file():
            continue
        relative = file.relative_to(kernel).as_posix()
        src = module_of(relative)
        if src is None:
            continue
        for included in INCLUDE.findall(file.read_text(encoding="utf-8")):
            dst = module_of(included)
            if dst is None:
                continue
            where = f"{relative}: includes {included}"
            if "/detail/" in included and dst != src:
                violations.append(f"{where} ({src} may not use the private detail/ of {dst})")
            elif _is_test_file(relative) and _kind(dst) == "testing":
                continue  # module tests may use shared test helpers
            elif not allowed(src, dst):
                violations.append(f"{where} ({src} may not depend on {dst})")
    return violations


NODE = re.compile(r'^\s*"(node\d+)"\s*\[\s*label\s*=\s*"([^"]+)"', re.MULTILINE)
EDGE = re.compile(r'^\s*"(node\d+)"\s*->\s*"(node\d+)"', re.MULTILINE)


def check_link_graph(dot: str, manifest: str) -> list[str]:
    """Violations of the module rules in a `cmake --graphviz` link graph."""
    module_by_target = {
        target: path
        for path, target in (line.split("=", 1) for line in manifest.splitlines() if "=" in line)
    }
    label = dict(NODE.findall(dot))
    violations: list[str] = []
    for a, b in EDGE.findall(dot):
        src_t, dst_t = label.get(a, ""), label.get(b, "")
        src, dst = module_by_target.get(src_t), module_by_target.get(dst_t)
        if src is None or dst is None:
            continue  # test executables, interface helpers and third-party targets
        if not allowed(src, dst):
            violations.append(f"{src_t} links {dst_t} ({src} may not depend on {dst})")
    return sorted(set(violations))


def scan_python_imports(package: Path) -> list[str]:
    """Platform modules of the cemkit package must never import cemkit.products.*."""
    violations: list[str] = []
    products = package / "products"
    for file in sorted(package.rglob("*.py")):
        if products in file.parents:
            continue
        tree = ast.parse(file.read_text(encoding="utf-8"), filename=str(file))
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                names = [node.module]
            else:
                continue
            for name in names:
                if name == "cemkit.products" or name.startswith("cemkit.products."):
                    rel = file.relative_to(package.parent).as_posix()
                    violations.append(f"{rel}:{node.lineno}: imports {name}")
    return violations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--python", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--dot", type=Path, required=True)
    args = parser.parse_args(argv)

    violations = scan_includes(args.kernel)
    violations += check_link_graph(args.dot.read_text(), args.manifest.read_text())
    violations += scan_python_imports(args.python)
    for violation in violations:
        print(f"boundary violation: {violation}")
    modules = len(args.manifest.read_text().split())
    print(f"check_boundaries: {modules} kernel module(s), {len(violations)} violation(s)")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())

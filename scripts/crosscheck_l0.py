"""PHY-005 cross-check through the C ABI: kernel L0 models and feasibility gate vs the reference.

Builds the cases with cemkit.reference.crosscheck (every hand calculation, edge cases, --cases
random states inside the validity range and --cases / 10 outside it), sends them in one request
to the kernel tool kernel/cemkit/capi/tests/l0_crosscheck.cpp (which calls cemkit_l0_batch), and
compares the results with the Python reference. The Python binding runs the same comparison in
tests/python/test_kernel_bindings.py.

Usage (ctest fans_l0_crosscheck runs this):
  uv run python scripts/crosscheck_l0.py --tool <build>/kernel/cemkit/capi/cemkit_l0_crosscheck \
      --cases 10000
Exit status 1 on any disagreement.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from cemkit.reference.crosscheck import REL_TOL, SEED, build_cases, compare_all


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tool", required=True, type=Path)
    parser.add_argument("--cases", type=int, default=10_000, help="random in-range states")
    args = parser.parse_args()

    cases, outside, fixed = build_cases(args.cases)
    run = subprocess.run(
        [str(args.tool)],
        input=json.dumps({"cases": cases}),
        capture_output=True,
        text=True,
        check=False,
    )
    if run.returncode != 0:
        print(f"crosscheck_l0: kernel tool failed: {run.stderr}", file=sys.stderr)
        return 1
    cmp = compare_all(cases, json.loads(run.stdout)["results"])
    print(
        f"crosscheck_l0: {len(cases)} cases ({fixed} hand-calculation and "
        f"edge cases, {args.cases} in-range and {outside} out-of-range random states, seed "
        f"{SEED}); {cmp.numbers} numbers within rel {REL_TOL} (max rel difference "
        f"{cmp.max_rel:.3g}); {cmp.errors} errors compared byte for byte"
    )
    if cmp.failures:
        print(f"crosscheck_l0: {len(cmp.failures)} disagreements", file=sys.stderr)
        for line in cmp.failures[:20]:
            print(f"  {line}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

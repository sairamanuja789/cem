"""Benchmark of the batch L0 API through the Python binding (T11; PERF-002).

Times one cemkit.kernel.l0_batch call on the cross-check population (hand calculations, edge
cases, --states random in-range states and states/10 out-of-range), split into JSON encoding, the
kernel call (C ABI: parse, evaluate, serialise) and JSON decoding. Prints one line per part. Not a
pass/fail check: PERF-001 (the 1 ms L1 budget) belongs to the axial L1 milestone.

Usage (after the release preset built python/cemkit/_kernel*.so):
  PYTHONPATH=python uv run python scripts/bench_l0_batch.py --states 10000 --repeat 5
"""

from __future__ import annotations

import argparse
import json
import statistics
import time

from cemkit import _kernel
from cemkit.reference.crosscheck import build_cases


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--states", type=int, default=10_000)
    parser.add_argument("--repeat", type=int, default=5)
    args = parser.parse_args()

    cases, _, _ = build_cases(args.states)
    encode, call, decode = [], [], []
    for _ in range(args.repeat):
        t0 = time.perf_counter()
        request = json.dumps({"cases": cases})
        t1 = time.perf_counter()
        status, body = _kernel.l0_batch(request)
        t2 = time.perf_counter()
        results = json.loads(body)["results"]
        t3 = time.perf_counter()
        assert status == 0 and len(results) == len(cases)
        encode.append(t1 - t0)
        call.append(t2 - t1)
        decode.append(t3 - t2)
    n = len(cases)
    for name, times in (("json encode", encode), ("kernel call", call), ("json decode", decode)):
        best = min(times)
        print(
            f"{name:12s}: median {statistics.median(times) * 1e3:7.1f} ms, best "
            f"{best * 1e3:7.1f} ms, {best / n * 1e6:6.2f} us/case ({n} cases, {args.repeat} runs)"
        )


if __name__ == "__main__":
    main()

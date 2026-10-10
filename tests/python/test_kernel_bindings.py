"""T11: the Python binding cemkit._kernel and its typed API cemkit.kernel (PERF-002, MAINT-005,
PHY-005; ADR-013).

The module is built by the "release" CMake preset into python/cemkit/ (scripts/check.sh builds it
before pytest). The batch test sends every hand calculation, edge case and 10 000 random in-range
states (plus 1 000 out-of-range) to the kernel in ONE call and compares each result with the Python
reference: numbers to a relative 1e-12, labels exactly, error texts byte for byte.
"""

import json
import time
from typing import Any

import pytest

from cemkit import _kernel, kernel
from cemkit.reference.crosscheck import REL_TOL, build_cases, compare_all


@pytest.mark.req("MAINT-005")
def test_abi_version_is_compatible() -> None:
    major, minor, _ = kernel.abi_version()
    assert (major, minor) >= kernel.ABI_REQUIRED
    kernel.check_abi()
    assert kernel.kernel_version().count(".") == 2


@pytest.mark.req("PERF-002")
@pytest.mark.req("PHY-005")
def test_one_call_evaluates_10000_states_and_matches_the_reference(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    cases, outside, _ = build_cases(10_000)
    assert outside == 1_000
    calls: list[int] = []
    real = _kernel.l0_batch

    def counted(request: str) -> tuple[int, str]:
        calls.append(1)
        return real(request)

    monkeypatch.setattr(_kernel, "l0_batch", counted)
    results = kernel.l0_batch(cases)
    assert len(calls) == 1  # PERF-002: the whole population in one call
    cmp = compare_all(cases, results)
    assert not cmp.failures, cmp.failures[:10]
    assert cmp.numbers > 100_000 and cmp.errors > 10_000
    assert cmp.max_rel <= REL_TOL


@pytest.mark.req("SEL-004")
def test_feasibility_reports_range_unsourced() -> None:
    report = kernel.feasibility(
        {
            "flow": 0.05,
            "fan_total_pressure": 150.0,
            "omega": 209.43951023931953,
            "d_tip": 0.119,
            "range": {
                "family": "fans.axial_ducted",
                "specific_speed_min": None,
                "specific_speed_max": None,
                "source": "UNSOURCED",
            },
        }
    )
    assert report["verdict"] == "unconfirmed"
    assert report["checks"][2]["message"].startswith("range unsourced")


@pytest.mark.req("SPEC-002")
def test_spec_compile_and_classified_errors() -> None:
    spec: dict[str, Any] = {
        "schema_version": "1.0.0",
        "spec_id": "py-binding",
        "revision": 1,
        "family": "fans.axial_ducted",
        "product": {"nominal_size": {"value": 120.0, "unit": "mm", "provenance": "user"}},
    }
    compiled = kernel.spec_compile(spec)
    assert compiled["fields"]["product.nominal_size"]["si_value"] == 0.12
    assert compiled["has_unresolved_essential_unknowns"] is True

    spec["product"]["nominal_size"]["unit"] = "Pa"
    with pytest.raises(kernel.KernelError) as raised:
        kernel.spec_compile(spec)
    assert raised.value.error.code == "unit_mismatch"
    assert raised.value.error.subject == "product.nominal_size"


@pytest.mark.req("MAINT-005")
def test_malformed_requests_are_status_codes_not_crashes() -> None:
    status, body = _kernel.l0_batch("{not json")
    assert status == kernel.CEMKIT_FAILED
    assert json.loads(body)["error"]["code"] == "invalid_input"
    with pytest.raises(kernel.KernelError) as raised:
        kernel.l0_batch([{"function": "tip_speed", "args": {"omega": "fast", "d_tip": 0.1}}])
    assert raised.value.error.subject == "cases[0].omega"


@pytest.mark.req("GEO-001")
@pytest.mark.req("GEO-004")
def test_geometry_smoke_step_and_stl_are_deterministic() -> None:
    # Arbitrary geometry test parameters (not a fan design), as in the T10 OCCT tests, in metres.
    request = {
        "hub_radius": 0.015,
        "hub_length": 0.020,
        "sections": [
            {"radius": 0.010, "chord": 0.018, "thickness": 0.002, "stagger": 0.3},
            {"radius": 0.030, "chord": 0.015, "thickness": 0.0015, "stagger": 0.5},
            {"radius": 0.050, "chord": 0.012, "thickness": 0.0012, "stagger": 0.7},
        ],
        "exports": [
            {"format": "step"},
            {"format": "stl", "linear_deflection": 5e-5, "angular_deflection": 0.2},
        ],
    }
    first = kernel.geometry_smoke(request)
    assert first["validity"]["ok"] is True
    assert [e["format"] for e in first["exports"]] == ["step", "stl"]
    assert kernel.geometry_smoke(request) == first


@pytest.mark.req("PERF-002")
def test_batch_benchmark_reports_throughput(capsys: pytest.CaptureFixture[str]) -> None:
    # Benchmark, not a pass/fail limit (PERF-001 is deferred to the axial L1 milestone): the time
    # for one 10 000-state batch is printed; scripts/bench_l0_batch.py gives the full breakdown.
    cases, _, _ = build_cases(10_000)
    start = time.perf_counter()
    results = kernel.l0_batch(cases)
    elapsed = time.perf_counter() - start
    assert len(results) == len(cases)
    with capsys.disabled():
        print(f"\nl0_batch: {len(cases)} cases in {elapsed * 1e3:.0f} ms (one call)")

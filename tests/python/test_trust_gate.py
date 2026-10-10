"""TRUST-001, TRUST-003, TRUST-004: the trust gate on synthetic monitors (docs/architecture.md)."""

from __future__ import annotations

import pytest

from cemkit.simulation.trust_gate import RANKABLE, Thresholds, assess, verified

T = Thresholds(
    residuals={"p": 1e-4, "Ux": 1e-4}, mass_imbalance=0.005, flat_fraction=0.2, flatness=0.01
)
FLAT = [10.0] * 100
GOOD = {"p": 1e-5, "Ux": 2e-5}


def run(**kw: object) -> str:
    args: dict[str, object] = dict(
        solver_failed=False,
        resource_limit=None,
        mesh_ok=True,
        residuals=GOOD,
        inflow=1.0,
        outflow=0.999,
        monitors={"torque": FLAT},
        thresholds=T,
    )
    args.update(kw)
    return assess(**args).status  # type: ignore[arg-type]


@pytest.mark.req("TRUST-004")
def test_every_failure_mode_gets_exactly_one_status() -> None:
    assert run() == "CONVERGED"
    assert run(resource_limit="memory") == "RESOURCE_LIMIT"
    assert run(solver_failed=True) == "SOLVER_FAILED"
    assert run(mesh_ok=False) == "MESH_INVALID"
    assert run(residuals={"p": 1e-3, "Ux": 1e-5}) == "UNCONVERGED"
    assert run(outflow=0.99) == "UNCONVERGED"  # 1% imbalance
    assert run(monitors={"torque": [10.0] * 80 + [10.0, 11.0] * 10}) == "UNCONVERGED"
    assert run(residuals={"Ux": 1e-5}) == "UNCONVERGED"  # a missing residual never passes


@pytest.mark.req("TRUST-001")
def test_only_converged_and_verified_are_rankable() -> None:
    assert {"CONVERGED", "VERIFIED"} == RANKABLE


@pytest.mark.req("TRUST-003")
def test_every_failed_check_is_recorded_with_value_and_threshold() -> None:
    a = assess(
        solver_failed=False,
        resource_limit=None,
        mesh_ok=True,
        residuals={"p": 1e-3, "Ux": 1e-5},
        inflow=1.0,
        outflow=0.98,
        monitors={"torque": FLAT},
        thresholds=T,
    )
    failed = {c.name: c for c in a.failed}
    assert set(failed) == {"residual_p", "mass_imbalance"}
    assert failed["residual_p"].value == 1e-3 and failed["residual_p"].threshold == 1e-4
    assert failed["mass_imbalance"].value == pytest.approx(0.02)


@pytest.mark.req("TRUST-002")
def test_verified_needs_three_converged_levels_and_every_gci_below_the_limit() -> None:
    assert verified(["CONVERGED"] * 3, [0.01, 0.03], 0.05)
    assert not verified(["CONVERGED"] * 3, [0.01, 0.06], 0.05)
    assert not verified(["CONVERGED", "UNCONVERGED", "CONVERGED"], [0.01], 0.05)
    assert not verified(["CONVERGED"] * 3, [None], 0.05)
    assert not verified(["CONVERGED"] * 2, [0.01], 0.05)


@pytest.mark.req("TRUST-004")
def test_a_monitor_near_zero_is_judged_against_its_scale() -> None:
    near_zero = [0.001] * 80 + [0.001, -0.001] * 10  # noise of 0.002 around a mean of ~0
    scaled = Thresholds(
        residuals={},
        mass_imbalance=0.005,
        flat_fraction=0.2,
        flatness=0.01,
        monitor_scales={"rise": 1.0},
    )
    unscaled = Thresholds(residuals={}, mass_imbalance=0.005, flat_fraction=0.2, flatness=0.01)
    common = dict(
        solver_failed=False,
        resource_limit=None,
        mesh_ok=True,
        residuals={},
        inflow=1.0,
        outflow=1.0,
        monitors={"rise": near_zero},
    )
    assert assess(**common, thresholds=scaled).status == "CONVERGED"  # type: ignore[arg-type]
    assert assess(**common, thresholds=unscaled).status == "UNCONVERGED"  # type: ignore[arg-type]

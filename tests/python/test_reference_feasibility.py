"""T08: the L0 feasibility gate reference and the family-range data (SEL-002, SEL-004, PHY-003).

The kernel gate is compared with this reference by scripts/crosscheck_l0.py (ctest
fans_l0_crosscheck). These tests pin the reference's own behaviour and the data-file rules.
"""

import math
from pathlib import Path
from typing import Any

import pytest
import yaml

from cemkit.errors import Error, format_number
from cemkit.reference.fans.common import l0
from cemkit.reference.fans.common.feasibility import (
    UNSOURCED,
    Duty,
    FamilyRange,
    Report,
    check_feasibility,
)
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import default_air

ROOT = Path(__file__).resolve().parents[2]
RANGES = ROOT / "data" / "fans" / "family_ranges.yaml"
AIR = default_air()
OMEGA = 209.43951023931953  # 2000 rpm
DUTY = Duty(0.05, FanTotalPressure(150.0))  # omega_s = 1.2370483575363835 (l0_002)
UNSOURCED_RANGE = FamilyRange("fans.axial_ducted", None, None, UNSOURCED)
# Test fixture only, not engineering data: bounds chosen around l0_002's omega_s.
TEST_SOURCE = "test fixture, not engineering data"


def _report(result: Report | Error) -> Report:
    assert isinstance(result, Report), result
    return result


def _statuses(report: Report) -> list[tuple[str, str]]:
    return [(c.limit, c.status) for c in report.checks]


def _load_ranges() -> list[dict[str, Any]]:
    doc = yaml.safe_load(RANGES.read_text(encoding="utf-8"))
    assert doc["schema_version"] == 1
    families: list[dict[str, Any]] = doc["families"]
    return families


@pytest.mark.req("SEL-002")
def test_family_ranges_are_cited_or_unsourced_with_no_bounds() -> None:
    families = _load_ranges()
    assert families, "at least the first product family is listed"
    for entry in families:
        assert set(entry) == {"family", "specific_speed_min", "specific_speed_max", "source"}
        lo, hi, source = entry["specific_speed_min"], entry["specific_speed_max"], entry["source"]
        assert isinstance(source, str) and source.strip()
        if source == UNSOURCED:
            # Never a number without a source (CLAUDE.md rule 7).
            assert lo is None and hi is None, entry["family"]
        else:
            assert isinstance(lo, float) and isinstance(hi, float), entry["family"]
            assert 0.0 < lo <= hi


@pytest.mark.req("SEL-002")
def test_the_gate_reports_range_unsourced_for_the_data_file() -> None:
    for entry in _load_ranges():
        family_range = FamilyRange(
            entry["family"],
            entry["specific_speed_min"],
            entry["specific_speed_max"],
            entry["source"],
        )
        if family_range.sourced:
            continue
        report = _report(check_feasibility(DUTY, AIR, family_range, OMEGA, 0.119))
        last = report.checks[-1]
        assert (last.limit, last.status) == ("family_specific_speed_range", "range_unsourced")
        assert last.message.startswith("range unsourced")
        assert report.verdict == "unconfirmed"  # never "feasible" (ADR-011)


@pytest.mark.req("SEL-004")
def test_a_duty_inside_every_limit_passes() -> None:
    family_range = FamilyRange("fans.axial_ducted", 1.0, 1.5, TEST_SOURCE)
    report = _report(check_feasibility(DUTY, AIR, family_range, OMEGA, 0.119))
    assert _statuses(report) == [
        ("incompressible_pressure", "pass"),
        ("incompressible_tip_speed", "pass"),
        ("family_specific_speed_range", "pass"),
    ]
    assert report.verdict == "confirmed"
    assert report.specific_speed == pytest.approx(1.2370483575363835, rel=1e-12)


@pytest.mark.req("SEL-004")
def test_pressure_beyond_the_limit_names_the_limit_and_the_nearest_duty() -> None:
    duty = Duty(0.05, FanTotalPressure(1500.0))
    report = _report(check_feasibility(duty, AIR, UNSOURCED_RANGE, OMEGA, 0.119))
    first = report.checks[0]
    assert (first.limit, first.status) == ("incompressible_pressure", "violated")
    assert first.violation is not None
    assert first.violation.detail("bounds") == "(0, 1418.55]"
    assert first.nearest_feasible == Duty(0.05, FanTotalPressure(1418.55))
    assert report.verdict == "infeasible"
    assert report.specific_speed is None


@pytest.mark.req("SEL-004")
def test_tip_speed_beyond_the_limit_has_no_nearest_duty() -> None:
    report = _report(check_feasibility(DUTY, AIR, UNSOURCED_RANGE, 200.0, 0.5))
    tip = report.checks[1]
    assert (tip.limit, tip.status) == ("incompressible_tip_speed", "violated")
    assert tip.violation is not None and tip.violation.subject == "tip_speed"
    assert tip.nearest_feasible is None
    assert report.verdict == "infeasible"


@pytest.mark.req("SEL-004")
@pytest.mark.parametrize(("lo", "hi"), [(1.3, 2.0), (0.5, 1.1)])
def test_specific_speed_outside_a_sourced_range_gives_a_nearest_duty_inside(
    lo: float, hi: float
) -> None:
    family_range = FamilyRange("fans.axial_ducted", lo, hi, TEST_SOURCE)
    report = _report(check_feasibility(DUTY, AIR, family_range, OMEGA, 0.119))
    last = report.checks[-1]
    assert (last.limit, last.status) == ("family_specific_speed_range", "violated")
    assert last.violation is not None
    assert last.violation.code == "infeasible_requirement"
    assert last.violation.detail("bounds") == f"[{format_number(lo)}, {format_number(hi)}]"
    nearest = last.nearest_feasible
    assert nearest is not None
    value = l0.specific_speed(OMEGA, nearest.flow, nearest.pressure, AIR)
    assert isinstance(value, float) and lo <= value <= hi
    # The nearest duty sits on the bound the duty crossed, up to rounding.
    crossed = lo if lo > 1.2370483575363835 else hi
    assert math.isclose(value, crossed, rel_tol=1e-12)
    assert report.verdict == "infeasible"


@pytest.mark.req("SEL-004")
def test_missing_speed_makes_speed_dependent_checks_not_computable() -> None:
    family_range = FamilyRange("fans.axial_ducted", 1.0, 1.5, TEST_SOURCE)
    report = _report(check_feasibility(DUTY, AIR, family_range))
    assert _statuses(report) == [
        ("incompressible_pressure", "pass"),
        ("incompressible_tip_speed", "not_computable"),
        ("family_specific_speed_range", "not_computable"),
    ]
    assert report.specific_speed is None
    assert report.verdict == "unconfirmed"


@pytest.mark.req("PHY-003")
@pytest.mark.parametrize("bad", [0.0, -1.0, math.nan, math.inf])
def test_invalid_inputs_return_out_of_validity(bad: float) -> None:
    result = check_feasibility(Duty(bad, FanTotalPressure(150.0)), AIR, UNSOURCED_RANGE)
    assert isinstance(result, Error)
    assert (result.code, result.subject) == ("out_of_validity", "flow")


@pytest.mark.req("SEL-002")
@pytest.mark.parametrize(("lo", "hi"), [(2.0, 1.0), (0.0, 1.0), (1.0, math.inf)])
def test_a_malformed_sourced_range_is_invalid_input(lo: float, hi: float) -> None:
    result = check_feasibility(DUTY, AIR, FamilyRange("x", lo, hi, TEST_SOURCE), OMEGA)
    assert isinstance(result, Error)
    assert (result.code, result.subject) == ("invalid_input", "family_range")

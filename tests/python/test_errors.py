"""The Python error mirror matches the kernel's error contract (REL-002, ADR-003 D6)."""

from pathlib import Path

import pytest

from cemkit.errors import ERROR_CODES, Error, describe, error, format_number

DATA = Path(__file__).resolve().parent / "data" / "to_chars_libstdcxx13.tsv"


def _to_chars_cases() -> list[tuple[float, str]]:
    cases = []
    for line in DATA.read_text(encoding="utf-8").splitlines():
        if line.startswith("#"):
            continue
        hex_value, text = line.split("\t")
        cases.append((float.fromhex(hex_value), text))
    return cases


@pytest.mark.req("REL-002")
def test_format_number_matches_libstdcxx_to_chars_on_622_values() -> None:
    cases = _to_chars_cases()
    assert len(cases) == 622
    mismatches = [(v, format_number(v), t) for v, t in cases if format_number(v) != t]
    assert mismatches == []


@pytest.mark.req("REL-002")
def test_format_number_matches_the_kernel_unit_test_cases() -> None:
    # Same cases as kernel/cemkit/core/tests/test_error.cpp.
    assert format_number(101325.0) == "101325"
    assert format_number(0.1) == "0.1"
    assert format_number(-2.5) == "-2.5"
    assert format_number(1e-7) == "1e-07"
    assert format_number(float("nan")) == "nan"
    assert format_number(float("inf")) == "inf"
    assert format_number(float("-inf")) == "-inf"


@pytest.mark.req("REL-002")
def test_errors_use_known_codes_and_sorted_details() -> None:
    assert "out_of_validity" in ERROR_CODES and len(ERROR_CODES) == 12
    err = error(
        "out_of_validity",
        "tip speed above the model's range",
        "rotor.tip_speed",
        value="120",
        bounds="[0, 100]",
        unit="m/s",
        model="x@1.0.0",
    )
    assert err.details[0] == ("bounds", "[0, 100]")
    assert err.detail("value") == "120"
    # Same text as the kernel test in test_error.cpp.
    assert describe(err) == (
        "out_of_validity at rotor.tip_speed: tip speed above the model's range "
        "(bounds=[0, 100], model=x@1.0.0, unit=m/s, value=120)"
    )
    assert describe(error("internal_error", "unreachable branch")) == (
        "internal_error: unreachable branch"
    )


@pytest.mark.req("REL-002")
def test_unknown_codes_and_unsorted_details_are_rejected() -> None:
    with pytest.raises(ValueError, match="unknown error code"):
        error("failure_cluster", "x")
    with pytest.raises(ValueError, match="sorted"):
        Error("invalid_input", "x", "", (("value", "1"), ("bounds", "2")))
    with pytest.raises(ValueError, match="sorted"):
        Error("invalid_input", "x", "", (("a", "1"), ("a", "2")))

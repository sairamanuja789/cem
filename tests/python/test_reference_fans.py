"""PHY-002, REP-002: public reference-fan data carries provenance, keeps unknowns explicit and is
internally consistent (data/fans/reference_fans/; ADR-019, ADR-020)."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import pytest
import yaml

DATA = Path(__file__).resolve().parents[2] / "data" / "fans" / "reference_fans"
FILES = sorted(DATA.glob("*.yaml"))


def _load(path: Path) -> dict[str, Any]:
    doc = yaml.safe_load(path.read_text(encoding="utf-8"))
    assert isinstance(doc, dict)
    return doc


def _walk(node: Any) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    if isinstance(node, dict):
        out.append(node)
        for value in node.values():
            out += _walk(value)
    elif isinstance(node, list):
        for value in node:
            out += _walk(value)
    return out


@pytest.mark.req("PHY-002")
def test_there_is_reference_data_for_the_three_public_fans() -> None:
    assert {p.stem for p in FILES} >= {
        "ebmpapst_4412f",
        "noctua_nf_f12_ippc2000",
        "fan01_eaa_benchmark",
    }


@pytest.mark.req("PHY-002")
@pytest.mark.parametrize("path", FILES, ids=[p.stem for p in FILES])
def test_every_file_records_its_source(path: Path) -> None:
    doc = _load(path)
    assert doc["schema_version"] == 1
    source = doc["document"]
    assert source["url"].startswith("https://")
    assert source["accessed"]
    # A document that was downloaded is pinned by hash; one that could not be read says so.
    hashed = "sha256" in source or "files" in source
    assert hashed or source.get("primary_page_read") is False


@pytest.mark.req("REP-002")
@pytest.mark.parametrize("path", FILES, ids=[p.stem for p in FILES])
def test_unknown_values_are_null_never_a_number(path: Path) -> None:
    for node in _walk(_load(path)):
        if node.get("status") == "UNKNOWN":
            assert node.get("value") is None, node


@pytest.mark.req("REP-002")
def test_the_ebmpapst_pressure_type_is_an_inferred_fan_static_with_evidence() -> None:
    """HI-032: accepted as INFERRED (not manufacturer-confirmed); the evidence is pinned by hash."""
    curve = _load(DATA / "ebmpapst_4412f.yaml")["curve"]
    definition = curve["pressure_definition"]
    assert (definition["value"], definition["status"]) == ("fan_static", "INFERRED")
    assert len(definition["evidence"]["sha256"]) == 64
    assert definition["uncertainty"]
    flows = [q for q, _ in curve["points_cfm_pa"]]
    pressures = [p for _, p in curve["points_cfm_pa"]]
    assert flows == sorted(flows)
    assert pressures == sorted(pressures, reverse=True)


@pytest.mark.req("PHY-002")
def test_the_fan01_power_column_is_watts() -> None:
    """The file names it P_shaft_in_Nm; Q dp_ts / P reproduces the published 53 % at 1.4 m3/s only
    if the values are watts (a recorded discrepancy)."""
    char = _load(DATA / "fan01_eaa_benchmark.yaml")["characteristic"]
    assert char["columns"] == ["flow_m3_per_s", "delta_p_ts_pa", "shaft_power_w"]
    points = char["points"]
    assert len(points) == 20
    assert [q for q, _, _ in points] == sorted(q for q, _, _ in points)
    q, dp, power = min(points, key=lambda r: abs(r[0] - 1.4))
    assert q * dp / power == pytest.approx(0.53, abs=0.005)

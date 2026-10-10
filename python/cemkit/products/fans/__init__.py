"""Fans product glue: the wording of the fans feasibility report (ADR-015)."""

from __future__ import annotations

from cemkit.products import FeasibilityText

FEASIBILITY_TEXT = FeasibilityText(
    nearest_duty=(("flow", "flow", "m3/s"), ("fan_total_pressure", "fan total pressure", "Pa")),
    report_quantities=(("specific_speed", "specific speed", "(1)"),),
)

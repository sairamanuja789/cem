"""Validity checks shared by the reference models (PHY-002, PHY-003, ADR-003 D3, D6).

A failed check returns an out_of_validity Error shaped like the kernel's: subject = the input or
quantity concerned, details value, bounds and model, numbers written with format_number.
"""

from __future__ import annotations

import math

from cemkit.errors import Error, error, format_number

POSITIVE_BOUNDS = "(0, inf)"


def require_positive(subject: str, value: float, model: str) -> Error | None:
    """None if value is finite and strictly positive, else out_of_validity."""
    if math.isfinite(value) and value > 0.0:
        return None
    return error(
        "out_of_validity",
        "input must be finite and strictly positive",
        subject,
        value=format_number(value),
        bounds=POSITIVE_BOUNDS,
        model=model,
    )


def require_at_most(
    subject: str, value: float, limit: float, model: str, message: str
) -> Error | None:
    """None if value <= limit, else out_of_validity with bounds (0, limit]."""
    if value <= limit:
        return None
    return error(
        "out_of_validity",
        message,
        subject,
        value=format_number(value),
        bounds=f"(0, {format_number(limit)}]",
        model=model,
    )

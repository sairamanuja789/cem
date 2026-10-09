"""Python mirror of the kernel's error contract (REL-002, ADR-003 D6).

An Error has the same shape as cemkit::core::Error (kernel/cemkit/core/error.hpp): a failure code
from schemas/cemkit/v1/error-codes.json, a message, a subject and key/value details sorted by key.
format_number and describe produce the same text as the kernel's functions of the same name, so
results from the Python reference and the C++ kernel can be compared field for field (T08).
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path

ERROR_CODES_FILE = (
    Path(__file__).resolve().parents[2] / "schemas" / "cemkit" / "v1" / "error-codes.json"
)


def _load_codes() -> frozenset[str]:
    schema = json.loads(ERROR_CODES_FILE.read_text(encoding="utf-8"))
    return frozenset(str(entry["const"]) for entry in schema["oneOf"])


ERROR_CODES = _load_codes()


@dataclass(frozen=True)
class Error:
    """A classified failure, shaped like cemkit::core::Error. Build it with error()."""

    code: str
    message: str
    subject: str
    details: tuple[tuple[str, str], ...]

    def __post_init__(self) -> None:
        if self.code not in ERROR_CODES:
            raise ValueError(f"unknown error code {self.code!r}")
        if list(self.details) != sorted(self.details) or len({k for k, _ in self.details}) != len(
            self.details
        ):
            raise ValueError("details must be sorted by key, each key once")

    def detail(self, key: str) -> str:
        return dict(self.details)[key]


def error(code: str, message: str, subject: str = "", **details: str) -> Error:
    """Error with its details sorted by key, as the kernel's std::map keeps them."""
    return Error(code, message, subject, tuple(sorted(details.items())))


def describe(err: Error) -> str:
    """'<code> at <subject>: <message> (<key>=<value>, ...)', as the kernel's describe()."""
    text = err.code
    if err.subject:
        text += f" at {err.subject}"
    text += f": {err.message}"
    if err.details:
        text += " (" + ", ".join(f"{k}={v}" for k, v in err.details) + ")"
    return text


def format_number(value: float) -> str:
    """The text std::to_chars(first, last, double) produces: the shortest decimal that reads back
    to the same double, in fixed or scientific notation, whichever is shorter (fixed on a tie);
    "nan", "inf" and "-inf" for non-finite values."""
    if math.isnan(value):
        return "nan"
    if math.isinf(value):
        return "inf" if value > 0 else "-inf"
    # repr() gives the shortest round-trip digits; Decimal exposes them without trailing zeros.
    sign, digit_tuple, exponent = Decimal(repr(value)).normalize().as_tuple()
    assert isinstance(exponent, int)
    digits = "".join(map(str, digit_tuple))
    minus = "-" if sign else ""
    n = len(digits)

    if exponent >= 0:
        # An integral value in fixed notation is written exactly, as libstdc++ does (for example
        # 1.2345678901234568e20 -> "123456789012345683968"); same length as the zero-padded digits.
        fixed = str(abs(int(value)))
    elif -exponent < n:
        fixed = digits[: n + exponent] + "." + digits[n + exponent :]
    else:
        fixed = "0." + "0" * (-exponent - n) + digits

    sci_exponent = exponent + n - 1
    mantissa = digits[0] + ("." + digits[1:] if n > 1 else "")
    scientific = f"{mantissa}e{'-' if sci_exponent < 0 else '+'}{abs(sci_exponent):02d}"

    return minus + (fixed if len(fixed) <= len(scientific) else scientific)

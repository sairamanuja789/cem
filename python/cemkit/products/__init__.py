"""Product glue for orchestration (docs/repository-structure.md): the words a product uses for
its quantities, so the CLI stays product-neutral (ADR-015)."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class FeasibilityText:
    """How a product's feasibility report names its quantities: (response key, label, unit)."""

    nearest_duty: tuple[tuple[str, str, str], ...]
    report_quantities: tuple[tuple[str, str, str], ...]


def feasibility_text(family: str) -> FeasibilityText:
    """The text of the product that owns `family` ("<product>.<family>")."""
    from cemkit.products import fans

    products = {"fans": fans.FEASIBILITY_TEXT}
    return products[family.split(".", 1)[0]]

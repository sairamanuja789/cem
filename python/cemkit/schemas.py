"""Loading and validating cemkit documents against the JSON Schemas in schemas/ (T05).

Two rules the schemas cannot express are enforced here:

- Non-finite numbers are rejected. JSON Schema has no keyword for this, and Python's json module
  and PyYAML both parse NaN and infinity (NaN even passes "minimum": 0). Every document must
  therefore be loaded with load_json() or load_yaml().
- A spec is validated in two steps: first the platform schema, then the product schema of its
  family. Each product schema lists the families it covers in "x-cemkit-families", so adding a
  family touches only its own schema file (FAM-002). The platform schema never references product
  schemas (ADR-009).
"""

from __future__ import annotations

import json
import math
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import jsonschema
import yaml
from referencing import Registry, Resource

SCHEMA_ROOT = Path(__file__).resolve().parents[2] / "schemas"
SPEC = "urn:cemkit:schema:v1:spec"
CANDIDATE = "urn:cemkit:schema:v1:candidate"
RESULT = "urn:cemkit:schema:v1:result"
FAMILIES_KEY = "x-cemkit-families"


class DocumentError(ValueError):
    """A document could not be loaded (malformed text or a non-finite number)."""


def _reject_constant(name: str) -> float:
    raise DocumentError(f"non-finite number {name} is not allowed")


def check_finite(document: Any, path: str = "$") -> None:
    """Raise DocumentError if any number in the document is NaN or infinite."""
    if isinstance(document, float) and not math.isfinite(document):
        raise DocumentError(f"{path}: non-finite number {document!r} is not allowed")
    if isinstance(document, Mapping):
        for key, value in document.items():
            check_finite(value, f"{path}.{key}")
    elif isinstance(document, list):
        for index, item in enumerate(document):
            check_finite(item, f"{path}[{index}]")


def load_json(text: str) -> Any:
    """Parse JSON, rejecting NaN, Infinity and overflowing literals such as 1e999."""
    try:
        document = json.loads(text, parse_constant=_reject_constant)
    except json.JSONDecodeError as error:
        raise DocumentError(f"invalid JSON: {error}") from error
    check_finite(document)
    return document


def load_yaml(text: str) -> Any:
    """Parse YAML (safe loader only), rejecting .nan and .inf."""
    try:
        document = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise DocumentError(f"invalid YAML: {error}") from error
    check_finite(document)
    return document


@dataclass(frozen=True)
class SchemaSet:
    """All schemas under one root, indexed by $id, plus the family -> product schema map."""

    schemas: Mapping[str, Mapping[str, Any]]
    families: Mapping[str, str]
    registry: Registry[Any]

    def errors(self, schema_id: str, document: Any) -> list[str]:
        validator = jsonschema.Draft202012Validator(self.schemas[schema_id], registry=self.registry)
        return [_describe(e) for e in validator.iter_errors(document)]

    def spec_errors(self, document: Any) -> list[str]:
        """Platform schema, then the product schema of the document's family."""
        errors = self.errors(SPEC, document)
        if not isinstance(document, Mapping):
            return errors
        family = document.get("family")
        product = document.get("product")
        schema_id = self.families.get(family) if isinstance(family, str) else None
        if schema_id is None:
            errors.append(f"$.family: no product schema declares family {family!r}")
        elif isinstance(product, Mapping):
            errors += [f"product: {e}" for e in self.errors(schema_id, product)]
        return errors


def _describe(error: jsonschema.ValidationError) -> str:
    return f"{error.json_path}: {error.message}"


def _schema_files(root: Path) -> Iterable[Path]:
    return sorted(p for p in root.rglob("*.json") if p.is_file())


def load_schemas(root: Path = SCHEMA_ROOT) -> SchemaSet:
    schemas: dict[str, Mapping[str, Any]] = {}
    families: dict[str, str] = {}
    for path in _schema_files(root):
        schema = load_json(path.read_text(encoding="utf-8"))
        schema_id = schema["$id"]
        if schema_id in schemas:
            raise DocumentError(f"{path}: duplicate $id {schema_id}")
        schemas[schema_id] = schema
        for family in schema.get(FAMILIES_KEY, []):
            if family in families:
                raise DocumentError(f"{path}: family {family} already declared")
            families[family] = schema_id
    registry: Registry[Any] = Registry().with_resources(
        (uri, Resource.from_contents(schema)) for uri, schema in schemas.items()
    )
    return SchemaSet(schemas=schemas, families=families, registry=registry)

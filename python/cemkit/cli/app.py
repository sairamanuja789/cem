"""cemkit command-line interface (T14; IN-001, UC-01 to UC-03, UC-08, REP-001, REP-004).

Commands (docs/cli.md):
  cemkit spec compile <file>          compile a JSON or YAML spec; record it; list its questions
  cemkit spec questions <spec-id>     the open questions of a recorded spec
  cemkit feasibility <spec-id>        the L0 feasibility gate for a recorded spec
  cemkit geometry smoke               build the geometry test solid; export STEP and STL
  cemkit runs show <run-id>           a run's metadata, specs and artifacts
  cemkit runs reproduce <run-id>      re-run a recorded command and compare its output

Rules:
- Every number printed carries its unit, and every computed number its fidelity label and model
  (REP-001). Spec values are inputs, shown with their provenance instead.
- All text output passes the banned-claims check (REP-004) before it is printed.
- The kernel is reached only through cemkit.kernel (one call per command, PERF-002); all storage
  through cemkit.store. Commands that compute something record a run (STORE-002) with the command
  and its output as artifacts, so `runs reproduce` can re-run it (UC-08).

Exit codes: 0 done; 1 rejected input, unknown id or failed reproduction; 2 infeasible duty;
3 output refused by the banned-claims check.
"""

from __future__ import annotations

import base64
import json
import os
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Annotated, Any

import typer

from cemkit import kernel
from cemkit import schemas as cs
from cemkit.errors import describe, format_number
from cemkit.reporting.claims import BannedClaim, check_claims
from cemkit.store import (
    DocumentRejected,
    SpecRecord,
    SqliteStore,
    StoreError,
    capture_run_metadata,
    open_store,
)
from cemkit.store.provenance import canonical_json

STORE_ENV = "CEMKIT_STORE"
DEFAULT_STORE = ".cemkit-store"

Json = dict[str, Any]

# Geometry TEST parameters for `geometry smoke` (not a fan design and not sourced data): the
# arbitrary test solid of the T10 OCCT tests (a 30 mm hub with a lofted plate) and its arbitrary
# test tessellation (0.05 mm chordal deviation, 0.2 rad between normals), in SI units.
SMOKE_REQUEST: Json = {
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
MEDIA_TYPES = {"step": "model/step", "stl": "model/stl"}

app = typer.Typer(no_args_is_help=True, add_completion=False, help="cemkit CEM platform")
spec_app = typer.Typer(no_args_is_help=True, help="Specs: compile and questions")
runs_app = typer.Typer(no_args_is_help=True, help="Runs: show and reproduce")
geometry_app = typer.Typer(no_args_is_help=True, help="Geometry checks")
app.add_typer(spec_app, name="spec")
app.add_typer(runs_app, name="runs")
app.add_typer(geometry_app, name="geometry")


class Failure(Exception):
    """A user-facing failure: printed to stderr with its exit code."""

    def __init__(self, message: str, code: int = 1) -> None:
        super().__init__(message)
        self.code = code


# --- output --------------------------------------------------------------------------------------


def emit(lines: list[str], *, err: bool = False) -> None:
    """Prints lines after the banned-claims check (REP-004)."""
    typer.echo(check_claims("\n".join(lines)), err=err)


def num(value: float) -> str:
    return format_number(value)


def labelled(value: Json, unit: str) -> str:
    """A computed value: number, unit, fidelity label and model (REP-001)."""
    return f"{num(value['value'])} {unit} [{value['fidelity_label']}, {value['model']}]"


@contextmanager
def store_at(path: Path) -> Iterator[SqliteStore]:
    path.mkdir(parents=True, exist_ok=True)
    with open_store(path) as store:
        yield store


def run_guarded(body: Callable[[], None]) -> None:
    """Runs a command body, turning failures into stderr text and exit codes."""
    try:
        body()
    except Failure as failure:
        typer.echo(check_claims(f"error: {failure}"), err=True)
        raise typer.Exit(failure.code) from None
    except kernel.KernelError as rejected:
        typer.echo(check_claims(f"rejected: {describe(rejected.error)}"), err=True)
        raise typer.Exit(1) from None
    except BannedClaim as banned:
        typer.echo(str(banned), err=True)
        raise typer.Exit(3) from None


# --- runs ----------------------------------------------------------------------------------------


def record_run(
    store: SqliteStore, command: str, inputs: Json, output: Json, spec: Json | None = None
) -> int:
    """Records a run (STORE-002) with its command and output as artifacts; returns the run id."""
    versions = kernel.versions()
    command_doc = {"command": command, "inputs": inputs}
    metadata = capture_run_metadata(
        command_doc,
        kernel_version=versions["kernel_version"],
        plugin_versions=versions["plugins"],
        model_versions=versions["models"],
    )
    run = store.record_run(metadata)
    if spec is not None:
        try:
            store.record_spec(run, spec)
        except DocumentRejected as rejected:
            raise Failure(f"the store rejected the spec: {rejected}") from None
    store.record_artifact(
        run, canonical_json(command_doc).encode(), "command.json", "application/json"
    )
    store.record_artifact(run, canonical_json(output).encode(), "output.json", "application/json")
    return run.run_pk


def without_versions(response: Json) -> Json:
    """The response without the version keys: what `runs reproduce` compares."""
    skip = {"abi_version", "kernel_version", "models", "plugins"}
    return {k: v for k, v in response.items() if k not in skip}


# --- spec ----------------------------------------------------------------------------------------


def load_spec_file(path: Path) -> Json:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise Failure(f"cannot read {path}: {error.strerror}") from None
    try:
        if path.suffix in {".yaml", ".yml"}:
            document = cs.load_yaml(text)
        elif path.suffix == ".json":
            document = cs.load_json(text)
        else:
            raise Failure(f"{path}: expected a .json, .yaml or .yml file")
    except cs.DocumentError as error:
        raise Failure(f"{path}: {error}") from None
    if not isinstance(document, dict):
        raise Failure(f"{path}: a spec must be a mapping")
    return document


def stored_spec(store: SqliteStore, spec_id: str, revision: int | None) -> SpecRecord:
    records = store.spec_records(spec_id)
    if revision is not None:
        records = tuple(r for r in records if r.revision == revision)
    if not records:
        which = spec_id if revision is None else f"{spec_id} revision {revision}"
        raise Failure(f"no recorded spec {which} (record it with `cemkit spec compile`)")
    return records[-1]


def field_line(path: str, field: Json) -> str:
    if field["text_value"] is not None:
        value = field["text_value"]
    elif field["si_value"] is not None:
        given = f"{num(field['original_value'])} {field['original_unit']}"
        value = f"{given} = {num(field['si_value'])} {field['si_unit']}"
        if field["pressure_kind"]:
            value += f" ({field['pressure_kind']})"
    else:
        value = "unknown"
    flags = field["provenance"] + (", provisional" if field["provisional"] else "")
    return f"  {path}: {value}  [{flags}]"


def question_lines(spec: Json) -> list[str]:
    questions = spec["questions"]
    lines = [f"open questions ({len(questions)}):"]
    lines += [f"  {q['field']} [{q['unit'] or 'text'}]: {q['reason']}" for q in questions]
    if not questions:
        lines = ["open questions: none"]
    return lines


@spec_app.command("compile")
def spec_compile(
    file: Annotated[Path, typer.Argument(help="Spec as JSON or YAML (IN-001)")],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    autonomous: Annotated[bool, typer.Option(help="Autonomous mode (SPEC-007)")] = False,
) -> None:
    """Compile a spec, record it and list its open questions (UC-01)."""

    def body() -> None:
        document = load_spec_file(file)
        compiled = kernel.spec_compile(document, autonomous_mode=autonomous)
        with store_at(store) as opened:
            run_id = record_run(
                opened,
                "spec compile",
                {"spec": document, "autonomous_mode": autonomous},
                compiled,
                spec=document,
            )
        lines = [
            f"spec {compiled['spec_id']} revision {compiled['revision']} "
            f"(family {compiled['family']}): compiled, run {run_id}",
            "fields (value as given = SI value; provenance; inputs carry no fidelity label):",
        ]
        lines += [field_line(path, f) for path, f in compiled["fields"].items()]
        lines += question_lines(compiled)
        if compiled["has_unresolved_essential_unknowns"]:
            lines.append("not runnable yet: answer the essential questions (SPEC-007)")
        emit(lines)

    run_guarded(body)


@spec_app.command("questions")
def spec_questions(
    spec_id: Annotated[str, typer.Argument(help="Recorded spec id")],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    revision: Annotated[int | None, typer.Option(help="Revision (default: latest)")] = None,
) -> None:
    """The open questions of a recorded spec (UC-02)."""

    def body() -> None:
        with store_at(store) as opened:
            record = stored_spec(opened, spec_id, revision)
        compiled = kernel.spec_compile(record.document())
        emit([f"spec {spec_id} revision {record.revision}:", *question_lines(compiled)])

    run_guarded(body)


# --- feasibility ---------------------------------------------------------------------------------

INPUT_UNITS = {"flow": "m3/s", "fan_total_pressure": "Pa", "omega": "rad/s"}


def input_line(name: str, value: Json) -> str:
    if "fidelity" in value:  # computed by the kernel (for example a converted pressure)
        return f"  {name}: {labelled(value, value['unit'])} derived: {value['rule']}"
    if "rule" in value:  # taken from the spec by a stated rule (for example D6's tip diameter)
        flags = value["provenance"] + (", provisional" if value["provisional"] else "")
        sources = ", ".join(value["from"])
        return (
            f"  {name}: {num(value['value'])} {value['unit']}  [{flags}] {value['rule']}"
            f"  (from {sources})"
        )
    return f"  {name}: {num(value['value'])} {value['unit']}  (from {value['from']})"


def check_lines(check: Json) -> list[str]:
    lines = [f"  {check['limit']}: {check['status']}: {check['message']}"]
    if check["violation"] is not None:
        lines.append(f"    limit violated: {check['violation']['describe']}")
    nearest = check["nearest_feasible"]
    if nearest is not None:
        lines.append(
            "    nearest feasible duty for this limit: flow "
            f"{labelled(nearest['flow'], 'm3/s')}, fan total pressure "
            f"{labelled(nearest['fan_total_pressure'], 'Pa')}"
        )
    elif check["status"] == "violated":
        lines.append("    nearest feasible duty: not computable for this limit")
    return lines


VERDICT_TEXT = {
    "confirmed": "confirmed: every L0 check passed",
    "unconfirmed": "unconfirmed: no limit violated, but at least one check could not be applied",
    "infeasible": "infeasible: a limit is violated",
}


@app.command("feasibility")
def feasibility(
    spec_id: Annotated[str, typer.Argument(help="Recorded spec id")],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    revision: Annotated[int | None, typer.Option(help="Revision (default: latest)")] = None,
) -> None:
    """The L0 feasibility gate for a recorded spec (UC-03, SEL-004)."""

    def body() -> None:
        with store_at(store) as opened:
            record = stored_spec(opened, spec_id, revision)
            response = kernel.feasibility_for_spec(record.document())
            run_id = record_run(
                opened, "feasibility", {"spec": record.document()}, without_versions(response)
            )
        report = response["report"]
        lines = [f"feasibility of {spec_id} revision {record.revision}: run {run_id}", "inputs:"]
        lines += [input_line(name, value) for name, value in response["inputs"].items()]
        if report["specific_speed"] is not None:
            lines.append(f"specific speed: {labelled(report['specific_speed'], '(1)')}")
        lines.append("checks:")
        for check in report["checks"]:
            lines += check_lines(check)
        lines.append(f"verdict: {VERDICT_TEXT[report['verdict']]}")
        emit(lines)
        if report["verdict"] == "infeasible":
            raise typer.Exit(2)

    run_guarded(body)


# --- geometry ------------------------------------------------------------------------------------


def write_named(directory: Path, name: str, data: bytes) -> Path:
    """Writes data to directory/name through a temporary file and a rename (complete or absent)."""
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / name
    temporary = directory / f".{name}.tmp"
    temporary.write_bytes(data)
    os.replace(temporary, target)
    return target


@geometry_app.command("smoke")
def geometry_smoke(
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    out: Annotated[
        Path | None, typer.Option(help="Also write <sha256>.<ext> files to this directory")
    ] = None,
    as_json: Annotated[
        bool, typer.Option("--json", help="Print one JSON document on stdout instead of text")
    ] = False,
) -> None:
    """Build the geometry test solid and export STEP and STL (GEO-001, GEO-002, GEO-004)."""

    def body() -> None:
        response = kernel.geometry_smoke({**SMOKE_REQUEST, "include_data": True})
        files = [(e, base64.b64decode(e.pop("data_base64"))) for e in response["exports"]]
        output = without_versions(response)
        with store_at(store) as opened:
            run_id = record_run(opened, "geometry smoke", SMOKE_REQUEST, output)
            run = opened.run(run_id)
            paths = []
            for export, data in files:
                name = f"{export['sha256']}.{export['format']}"
                opened.record_artifact(run, data, name, MEDIA_TYPES[export["format"]])
                where = write_named(out, name, data) if out is not None else None
                paths.append((name, export["size_bytes"], where))
        if as_json:
            # D5: stdout carries this one JSON document and nothing else (OCCT logs elsewhere).
            document = {
                "command": "geometry smoke",
                "run": run_id,
                "output": output,
                "files": [
                    {"name": n, "size_bytes": size, "path": str(where) if where else None}
                    for n, size, where in paths
                ],
            }
            emit([json.dumps(document, sort_keys=True)])
            return
        v, t, m = response["validity"], response["topology"], response["mass_properties"]
        centroid = ", ".join(num(c) for c in m["centroid"])
        lines = [
            f"geometry smoke ({response['backend']} backend, test parameters): run {run_id}",
            f"validity: {'ok' if v['ok'] else 'FAILED'} (closed {v['closed']}, manifold "
            f"{v['manifold']}, self-intersection free {v['self_intersection_free']})",
            f"topology: {t['solids']} solid, {t['faces']} faces, {t['edges']} edges",
            "mass properties (geometry, not a prediction: no fidelity label): volume "
            f"{num(m['volume'])} m3, area {num(m['area'])} m2, centroid ({centroid}) m",
            "exports (content-hash names, stored as artifacts):",
        ]
        for name, size, where in paths:
            lines.append(f"  {name}  {size} bytes" + (f"  -> {where}" if where else ""))
        emit(lines)

    run_guarded(body)


# --- runs ----------------------------------------------------------------------------------------


def command_of(store: SqliteStore, run_id: int) -> tuple[Json, Json]:
    run = store.run(run_id)
    by_name = {a.name: a for a in store.run_artifacts(run)}
    if "command.json" not in by_name or "output.json" not in by_name:
        raise Failure(f"run {run_id} has no recorded command to reproduce")
    command = cs.load_json(store.artifacts.read_bytes(by_name["command.json"].sha256).decode())
    output = cs.load_json(store.artifacts.read_bytes(by_name["output.json"].sha256).decode())
    return command, output


@runs_app.command("show")
def runs_show(
    run_id: Annotated[int, typer.Argument(help="Run id")],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
) -> None:
    """A run's metadata (STORE-002), specs and artifacts (UC-08)."""

    def body() -> None:
        with store_at(store) as opened:
            try:
                run = opened.run(run_id)
            except StoreError:
                raise Failure(f"no run {run_id}") from None
            specs = opened.run_specs(run)
            artifacts = opened.run_artifacts(run)
        md = run.metadata
        unknown = md.unknown_fields()
        lines = [
            f"run {run.run_pk} recorded {run.recorded_at}",
            f"  kernel version: {md.kernel_version}",
            f"  plugin versions: {md.plugin_versions_json()}",
            f"  model versions: {md.model_versions_json()}",
            f"  git commit: {md.git_commit}",
            f"  container digest: {md.container_digest}",
            f"  input hash: {md.input_hash}",
            f"  unknown fields: {', '.join(unknown) if unknown else 'none'}",
            "specs:",
            *[f"  {s.spec_id} revision {s.revision} sha256 {s.sha256}" for s in specs],
            "artifacts:",
            *[f"  {a.name}  {a.size_bytes} bytes  sha256 {a.sha256}" for a in artifacts],
        ]
        emit(lines)

    run_guarded(body)


def recompute(command: Json) -> Json:
    name, inputs = command["command"], command["inputs"]
    if name == "spec compile":
        return kernel.spec_compile(inputs["spec"], autonomous_mode=inputs["autonomous_mode"])
    if name == "feasibility":
        return without_versions(kernel.feasibility_for_spec(inputs["spec"]))
    if name == "geometry smoke":
        response = without_versions(kernel.geometry_smoke(inputs))
        return response
    raise Failure(f"cannot reproduce command {name!r}")


@runs_app.command("reproduce")
def runs_reproduce(
    run_id: Annotated[int, typer.Argument(help="Run id")],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
) -> None:
    """Re-run a recorded command and compare its output (UC-08)."""

    def body() -> None:
        with store_at(store) as opened:
            try:
                run = opened.run(run_id)
            except StoreError:
                raise Failure(f"no run {run_id}") from None
            command, recorded = command_of(opened, run_id)
        again = recompute(command)
        same = canonical_json(again) == canonical_json(recorded)
        versions = kernel.versions()
        changed = [
            f"  {label}: recorded {old}, now {new}"
            for label, old, new in (
                ("kernel version", run.metadata.kernel_version, versions["kernel_version"]),
                (
                    "model versions",
                    run.metadata.model_versions_json(),
                    canonical_json(versions["models"]),
                ),
                (
                    "plugin versions",
                    run.metadata.plugin_versions_json(),
                    canonical_json(versions["plugins"]),
                ),
            )
            if old != new
        ]
        if same:
            emit([f"run {run_id} ({command['command']}): reproduced, identical output", *changed])
            return
        keys = sorted(k for k in set(again) | set(recorded) if again.get(k) != recorded.get(k))
        emit(
            [
                f"run {run_id} ({command['command']}): output differs in {', '.join(keys)}",
                *(changed or ["  versions unchanged: the difference is not explained by them"]),
            ],
            err=True,
        )
        raise typer.Exit(1)

    run_guarded(body)


def main() -> None:
    app()

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
import math
import os
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Annotated, Any

import typer

from cemkit import kernel
from cemkit import schemas as cs
from cemkit.errors import describe, format_number
from cemkit.orchestration.limits import ResourceLimits
from cemkit.reporting.claims import BannedClaim, check_claims
from cemkit.reporting.render import write_previews
from cemkit.simulation import study, trust_gate
from cemkit.simulation.mesh_study import cell_size, mesh_study
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
design_app = typer.Typer(no_args_is_help=True, help="L1 designs with geometry")
simulate_app = typer.Typer(no_args_is_help=True, help="L2 simulation of a recorded design")
optimize_app = typer.Typer(no_args_is_help=True, help="Design searches (exploratory)")
report_app = typer.Typer(no_args_is_help=True, help="Design packages")
app.add_typer(spec_app, name="spec")
app.add_typer(runs_app, name="runs")
app.add_typer(geometry_app, name="geometry")
app.add_typer(design_app, name="design")
app.add_typer(simulate_app, name="simulate")
app.add_typer(optimize_app, name="optimize")
app.add_typer(report_app, name="report")


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


# Test tessellation for STL exports when the parameter file states none (the same arbitrary test
# values as geometry smoke: 0.05 mm chordal deviation, 0.2 rad between normals).
FAN_EXPORTS: list[Json] = [
    {"format": "step"},
    {"format": "stl", "linear_deflection": 5e-5, "angular_deflection": 0.2},
]


def fan_output(response: Json) -> tuple[Json, list[tuple[str, Json, bytes]]]:
    """The recorded output (no versions, no file bytes) and each part's exported files."""
    files: list[tuple[str, Json, bytes]] = []
    for part in response["result"]["parts"]:
        for export in part["exports"]:
            data = export.pop("data_base64", None)
            if data is not None:
                files.append((part["name"], export, base64.b64decode(data)))
    return without_versions(response), files


@geometry_app.command("fan")
def geometry_fan(
    params: Annotated[
        Path, typer.Argument(help="Fan geometry parameters (JSON or YAML, m and rad)")
    ],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    out: Annotated[
        Path | None, typer.Option(help="Also write <part>-<sha256>.<ext> files to this directory")
    ] = None,
    as_json: Annotated[
        bool, typer.Option("--json", help="Print one JSON document on stdout instead of text")
    ] = False,
) -> None:
    """Build a ducted fan's rotor and duct from stated parameters and export STEP and STL
    (ADR-015 M3; GEO-001, GEO-002, GEO-004). Geometry only: no performance is computed."""

    def body() -> None:
        loaded = load_spec_file(params)
        exports = loaded.pop("exports", FAN_EXPORTS)
        request = {"operation": "ducted_fan_geometry", "args": loaded, "exports": exports}
        response = kernel.product_operation({**request, "include_data": True})
        output, files = fan_output(response)
        with store_at(store) as opened:
            run_id = record_run(opened, "geometry fan", request, output)
            run = opened.run(run_id)
            paths = []
            for part, export, data in files:
                name = f"{part}-{export['sha256']}.{export['format']}"
                opened.record_artifact(run, data, name, MEDIA_TYPES[export["format"]])
                where = write_named(out, name, data) if out is not None else None
                paths.append((name, export["size_bytes"], where))
        if as_json:
            document = {
                "command": "geometry fan",
                "run": run_id,
                "output": output,
                "files": [
                    {"name": n, "size_bytes": size, "path": str(where) if where else None}
                    for n, size, where in paths
                ],
            }
            emit([json.dumps(document, sort_keys=True)])
            return
        result = response["result"]
        lines = [
            f"geometry fan ({response['backend']} backend): run {run_id}",
            f"basis: {result['basis']}",
            f"tip clearance: {num(result['tip_clearance'])} m (stated geometry)",
        ]
        for part in result["parts"]:
            v, t, m = part["validity"], part["topology"], part["mass_properties"]
            lines.append(
                f"{part['name']}: validity {'ok' if v['ok'] else 'FAILED'}, {t['faces']} faces, "
                f"volume {num(m['volume'])} m3 (geometry, no fidelity label)"
            )
        lines.append("exports (content-hash names, stored as artifacts):")
        for name, size, where in paths:
            lines.append(f"  {name}  {size} bytes" + (f"  -> {where}" if where else ""))
        emit(lines)

    run_guarded(body)


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
        qualifier = field.get("qualifier")
        if qualifier:
            value += f" ({qualifier['value']})"
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
        # The product names its quantities (label, unit); the CLI only prints them (ADR-015).
        duty = ", ".join(f"{q['label']} {labelled(q, q['unit'])}" for q in nearest.values())
        lines.append(f"    nearest feasible duty for this limit: {duty}")
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
        for value in report.values():
            if isinstance(value, dict) and "label" in value:
                lines.append(f"{value['label']}: {labelled(value, value['unit'])}")
        lines.append("checks:")
        for check in report["checks"]:
            lines += check_lines(check)
        evaluated = ("pass", "violated")
        skipped = sum(check["status"] not in evaluated for check in report["checks"])
        lines.append(
            f"checks not evaluated: {skipped} of {len(report['checks'])} "
            "(a check that was not evaluated never counts as a pass)"
        )
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


# --- design --------------------------------------------------------------------------------------

DESIGN_PARAMS = (
    "r_hub",
    "chord_hub",
    "chord_tip",
    "thickness",
    "stations",
    "embed_depth",
    "blade_count",
    "hub_length",
    "duct_wall",
    "duct_length",
)


def si_field(compiled: Json, path: str) -> float | None:
    field = compiled["fields"].get(path)
    value = None if field is None else field["si_value"]
    return None if value is None else float(value)


def design_request(spec: Json, params: Json) -> tuple[Json, Json]:
    """The L0 feasibility response of the spec and the free_vortex_design request built from it and
    the stated design parameters (every length in m). Units come from the kernel's spec compiler
    only (CLAUDE.md rule 4)."""
    missing = [k for k in DESIGN_PARAMS if k not in params]
    if missing:
        raise Failure(f"the design parameters lack: {', '.join(missing)}")
    feasibility = kernel.feasibility_for_spec(spec)
    verdict = feasibility["report"]["verdict"]
    if verdict == "infeasible":
        raise Failure("the L0 feasibility gate rejects this duty; no design is attempted")
    inputs = feasibility["inputs"]
    for name in ("flow", "fan_total_pressure", "omega", "d_tip"):
        if name not in inputs:
            raise Failure(f"the spec gives no {name}; run cemkit feasibility for the reason")
    compiled = kernel.spec_compile(spec)
    clearance = si_field(compiled, "product.tip_clearance_min")
    if clearance is None:
        raise Failure("the spec gives no product.tip_clearance_min")
    r_tip = inputs["d_tip"]["value"] / 2.0
    air = {
        key: value
        for key, value in (
            ("density", si_field(compiled, "air.density")),
            ("temperature", si_field(compiled, "air.temperature")),
        )
        if value is not None
    }
    args = {
        "flow": inputs["flow"]["value"],
        "fan_total_pressure": inputs["fan_total_pressure"]["value"],
        "omega": inputs["omega"]["value"],
        "r_tip": r_tip,
        **{k: params[k] for k in DESIGN_PARAMS[:6]},
        **({"camber_scale": params["camber_scale"]} if "camber_scale" in params else {}),
        "geometry": {
            "blade_count": params["blade_count"],
            "hub_length": params["hub_length"],
            "duct_inner_radius": r_tip + clearance,
            "duct_wall": params["duct_wall"],
            "duct_length": params["duct_length"],
        },
    }
    return feasibility, {"operation": "free_vortex_design", "args": args, "air": air}


def design_fan_output(
    spec: Json, params: Json, exports: list[Json]
) -> tuple[Json, list[tuple[str, Json, bytes]]]:
    """The recorded output of cemkit design fan (no versions, no file bytes) and the files."""
    feasibility, request = design_request(spec, params)
    design = kernel.product_operation(request)
    geometry = kernel.product_operation(
        {
            "operation": "ducted_fan_geometry",
            "args": design["result"]["value"]["geometry_args"],
            "exports": exports,
            "include_data": True,
        }
    )
    geometry_output, files = fan_output(geometry)
    output = {
        "feasibility": without_versions(feasibility),
        "design": {**without_versions(design), "camber_scale": params.get("camber_scale", 1.0)},
        "geometry": geometry_output,
    }
    return output, files


def design_lines(output: Json) -> list[str]:
    design = output["design"]["result"]
    value = design["value"]
    label = f"[{design['fidelity_label']}, {design['model']}]"
    ml = value["mean_line"]
    lines = [
        f"design: free-vortex blade sections {label}",
        "  loss-free, zero incidence and zero deviation (no loss or deviation data is sourced:",
        "  UNSOURCED); every angle in degrees from the axial direction",
        f"  axial velocity: {num(value['axial_velocity'])} m/s; mean line: flow coefficient "
        f"{num(ml['flow_coefficient'])}, work coefficient {num(ml['work_coefficient'])}, "
        f"de Haller {num(ml['de_haller_ratio'])}",
        "  radius_mm  beta1  beta2  camber  stagger  chord_mm  camber_mm  de_Haller  reaction",
    ]
    for s in value["stations"]:
        if s["embedded"]:
            continue
        lines.append(
            f"  {s['radius'] * 1e3:9.2f} {math.degrees(s['relative_inlet_angle']):6.2f} "
            f"{math.degrees(s['relative_outlet_angle']):6.2f} "
            f"{math.degrees(s['camber_angle']):7.2f} "
            f"{math.degrees(s['stagger_from_axis']):8.2f} {s['chord'] * 1e3:9.2f} "
            f"{s['camber'] * 1e3:10.3f} {s['de_haller_ratio']:10.3f} {s['reaction']:9.3f}"
        )
    camber_scale = output["design"].get("camber_scale", 1.0)
    if camber_scale != 1.0:
        lines.append(
            f"  note: the built sections use camber_scale {num(camber_scale)}; the L1 stations "
            "above keep the zero-deviation camber and do not describe the built blade"
        )
    if any(s["relative_outlet_angle"] < 0.0 for s in value["stations"]):
        lines.append(
            "  note: beta2 < 0 at some stations (the relative flow is turned past axial); no limit"
        )
        lines.append("  is applied because a blade-loading limit needs a cited source (HI-026)")
    return lines


@design_app.command("fan")
def design_fan(
    spec_id: Annotated[str, typer.Argument(help="Recorded spec id (duty, speed, size)")],
    params: Annotated[
        Path, typer.Argument(help="Design parameters (JSON or YAML, m): hub, chords, counts")
    ],
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    revision: Annotated[int | None, typer.Option(help="Revision (default: latest)")] = None,
    out: Annotated[
        Path | None, typer.Option(help="Also write <part>-<sha256>.<ext> files to this directory")
    ] = None,
) -> None:
    """L1 free-vortex design of a ducted axial rotor from a recorded spec and stated design
    parameters, built as STEP and STL (PHY-001, GEO-001, GEO-002, ADR-016). Loss-free: no
    efficiency, loss or deviation is applied, and nothing here is simulated or measured."""

    def body() -> None:
        loaded = load_spec_file(params)
        exports = loaded.pop("exports", FAN_EXPORTS)
        with store_at(store) as opened:
            record = stored_spec(opened, spec_id, revision)
            spec = record.document()
            output, files = design_fan_output(spec, loaded, exports)
            inputs = {"spec": spec, "params": loaded, "exports": exports}
            run_id = record_run(opened, "design fan", inputs, output)
            run = opened.run(run_id)
            paths = []
            for part, export, data in files:
                name = f"{part}-{export['sha256']}.{export['format']}"
                opened.record_artifact(run, data, name, MEDIA_TYPES[export["format"]])
                where = write_named(out, name, data) if out is not None else None
                paths.append((name, export["size_bytes"], where))
        lines = [f"design fan of {spec_id} revision {record.revision}: run {run_id}"]
        lines += [input_line(n, v) for n, v in output["feasibility"]["inputs"].items()]
        lines.append(f"L0 feasibility: {VERDICT_TEXT[output['feasibility']['report']['verdict']]}")
        lines += design_lines(output)
        for part in output["geometry"]["result"]["parts"]:
            v, m = part["validity"], part["mass_properties"]
            lines.append(
                f"{part['name']}: validity {'ok' if v['ok'] else 'FAILED'}, volume "
                f"{num(m['volume'])} m3 (geometry, no fidelity label)"
            )
        lines.append("exports (content-hash names, stored as artifacts):")
        for name, size, where in paths:
            lines.append(f"  {name}  {size} bytes" + (f"  -> {where}" if where else ""))
        emit(lines)

    run_guarded(body)


# --- simulate ------------------------------------------------------------------------------------

CFD_SETTINGS = (
    "flow_factors",
    "domain",
    "inlet",
    "blade_stl",
    "mesh_levels",
    "blade_refinement",
    "rotor_refinement",
    "iterations",
    "cores",
    "wall_time_s",
    "gci_limit",
)
STUDY_QUANTITIES = (
    "total_pressure_rise",
    "total_to_static_pressure_rise",
    "torque",
    "total_efficiency",
    "total_to_static_efficiency",
)


def cfd_air(spec: Json) -> tuple[float, float]:
    """Density and dynamic viscosity (SI) from the compiled spec; a viscosity the spec does not
    state comes from the kernel's Sutherland model at the spec temperature."""
    compiled = kernel.spec_compile(spec)
    density = si_field(compiled, "air.density")
    temperature = si_field(compiled, "air.temperature")
    viscosity = si_field(compiled, "air.dynamic_viscosity")
    if density is None or temperature is None:
        raise Failure("the spec gives no air density or temperature")
    if viscosity is None:
        (row,) = kernel.l0_batch(
            [{"function": "dynamic_viscosity", "args": {"temperature": temperature}}]
        )
        if "error" in row:
            raise Failure(f"no viscosity: {row['error']['message']}")
        viscosity = float(row["value"])
    return density, viscosity


def simulate_fan_run(
    command: Json,
    output: Json,
    settings: Json,
    workdir: Path,
    plan: list[tuple[str, float]] | None = None,
) -> tuple[Json, list[study.CaseResult]]:
    """Writes, runs and assesses the CFD cases of a recorded design (M5). The default plan runs
    every flow factor on the medium mesh and the duty point on all three meshes (TRUST-002); a
    screening plan (for example [("medium", 1.0)]) runs only its cases and has no mesh study."""
    missing = [k for k in CFD_SETTINGS if k not in settings]
    if missing:
        raise Failure(f"the CFD settings lack: {', '.join(missing)}")
    geometry_args = output["design"]["result"]["value"]["geometry_args"]
    inputs = output["feasibility"]["inputs"]
    flow, omega = inputs["flow"]["value"], inputs["omega"]["value"]
    density, viscosity = cfd_air(command["inputs"]["spec"])
    levels = settings["mesh_levels"]
    factors = [float(f) for f in settings["flow_factors"]]
    if 1.0 not in factors or len(factors) < 3:
        raise Failure("SIM-003: at least 3 flow factors, one of them 1.0 (the duty)")
    full = plan is None
    if plan is None:
        plan = [("medium", f) for f in factors] + [("coarse", 1.0), ("fine", 1.0)]
    limits = ResourceLimits(
        cores=int(settings["cores"]), wall_time_s=float(settings["wall_time_s"])
    )
    jobs: list[tuple[str, Path, Json]] = []
    response: Json = {}
    for level in ("coarse", "medium", "fine"):
        level_flows = [flow * f for lv, f in plan if lv == level]
        if not level_flows:
            continue
        response = kernel.product_operation(
            {
                "operation": "rotor_cfd_case",
                "args": {
                    "geometry_args": geometry_args,
                    "flows": level_flows,
                    "omega": omega,
                    "density": density,
                    "dynamic_viscosity": viscosity,
                    "mesh": {
                        "cell_size": levels[level],
                        "blade_refinement": settings["blade_refinement"],
                        "rotor_refinement": settings["rotor_refinement"],
                        "iterations": settings["iterations"],
                        "processes": settings["cores"],
                    },
                    "inlet": settings["inlet"],
                    "domain": settings["domain"],
                    "blade_stl": settings["blade_stl"],
                },
            }
        )
        result = response["result"]
        for case in result["cases"]:
            factor = case["flow"] / flow
            name = f"{level}_q{round(factor * 100):03d}"
            jobs.append((name, workdir / name, case))
    thresholds = study.thresholds_of(result["thresholds"])
    results = study.run_cases(jobs, thresholds, limits, concurrent=1)
    sectors = result["sectors"]
    domain_volume = (
        math.pi
        * (geometry_args["duct_inner_radius"] ** 2 - geometry_args["hub_radius"] ** 2)
        * (
            geometry_args["hub_length"]
            + settings["domain"]["upstream"]
            + settings["domain"]["downstream"]
        )
        / sectors
    )
    by_name = {r.name: r for r in results}
    duty = [by_name[f"{lv}_q100"] for lv in ("fine", "medium", "coarse")] if full else []
    statuses = [r.assessment.status for r in duty]
    study_out: Json = {"levels": ["fine", "medium", "coarse"], "statuses": statuses}
    gcis: list[float | None] = []
    if full and all(s == "CONVERGED" for s in statuses) and all(r.cells for r in duty):
        sizes = tuple(cell_size(domain_volume, int(r.cells or 0)) for r in duty)
        per_quantity: Json = {}
        metrics = [cfd_metrics(r, sectors, omega, density, "CONVERGED") for r in duty]
        for q in STUDY_QUANTITIES:
            if not all(q in m for m in metrics):
                per_quantity[q] = {
                    "convergence": "not studied",
                    "gci_fine": None,
                    "reason": "not defined on every mesh (rise not positive)",
                }
                gcis.append(None)
                continue
            values = [m[q]["value"] for m in metrics]
            ms = mesh_study((sizes[0], sizes[1], sizes[2]), (values[0], values[1], values[2]))
            gcis.append(ms.gci_fine)
            per_quantity[q] = {
                "values_fine_medium_coarse": values,
                "convergence": ms.convergence,
                "order": ms.order,
                "gci_fine": ms.gci_fine,
                "refinement_ratios": list(ms.refinement_ratios),
            }
        study_out["cell_sizes"] = list(sizes)
        study_out["quantities"] = per_quantity
    is_verified = trust_gate.verified(statuses, gcis, float(settings["gci_limit"]))
    study_out["verified"] = is_verified
    study_out["gci_limit"] = settings["gci_limit"]
    cases_out = []
    for r in results:
        status = r.assessment.status
        if r.name == "fine_q100" and is_verified:
            status = "VERIFIED"
        entry: Json = {
            "name": r.name,
            "status": status,
            "cells": r.cells,
            "wall_seconds": r.wall_seconds,
            "peak_rss_bytes": r.peak_rss_bytes,
            "failed_checks": [
                {"check": c.name, "value": c.value, "threshold": c.threshold, "message": c.message}
                for c in r.assessment.failed
            ],
        }
        if status in trust_gate.RANKABLE:
            uncertainty = None
            if status == "VERIFIED":
                q = study_out["quantities"]
                uncertainty = {
                    k: abs(q[k]["values_fine_medium_coarse"][0]) * q[k]["gci_fine"]
                    for k in STUDY_QUANTITIES
                }
                uncertainty["shaft_power"] = uncertainty["torque"] * omega
            entry["metrics"] = cfd_metrics(r, sectors, omega, density, status, uncertainty)
        cases_out.append(entry)
    out = {
        "solver": response["result"]["solver"],
        "case_template": response["result"]["case_template"],
        "thresholds": response["result"]["thresholds"],
        "air": {"density": density, "dynamic_viscosity": viscosity},
        "settings": settings,
        "cases": cases_out,
        "mesh_study": study_out,
    }
    return out, results


def cfd_metrics(
    r: study.CaseResult,
    sectors: int,
    omega: float,
    density: float,
    status: str,
    uncertainty: Json | None = None,
) -> Json:
    args: Json = {
        "status": status,
        "sectors": sectors,
        "omega": omega,
        "density": density,
        **r.finals,
    }
    if uncertainty is not None:
        args["uncertainty"] = uncertainty
    response = kernel.product_operation({"operation": "rotor_cfd_metrics", "args": args})
    metrics: Json = response["result"]
    return metrics


@simulate_app.command("fan")
def simulate_fan(
    design_run: Annotated[int, typer.Argument(help="Run id of a recorded `design fan`")],
    settings_file: Annotated[Path, typer.Argument(help="CFD settings (YAML or JSON, SI)")],
    workdir: Annotated[Path, typer.Option(help="Directory for the solver cases")] = Path(
        "build/cfd"
    ),
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
) -> None:
    """L2 CFD of a recorded design (M5; SIM-001 to SIM-004, TRUST-001 to TRUST-004): one blade
    passage with OpenFOAM, every flow factor on the medium mesh and a three-mesh study at the duty.
    Numbers are printed only for results that pass the trust gate."""

    def body() -> None:
        settings = load_spec_file(settings_file)
        with store_at(store) as opened:
            try:
                command, output = command_of(opened, design_run)
            except StoreError:
                raise Failure(f"no run {design_run}") from None
            if command["command"] != "design fan":
                raise Failure(f"run {design_run} is not a design fan run")
            out, _ = simulate_fan_run(command, output, settings, workdir / f"design_{design_run}")
            out["design_run"] = design_run
            run_id = record_run(
                opened, "simulate fan", {"design_run": design_run, "settings": settings}, out
            )
        lines = [
            f"simulate fan of design run {design_run}: run {run_id}",
            f"  solver {out['solver']}, template {out['case_template']}; cases in {workdir}",
            "  trust thresholds: " + "; ".join(out["thresholds"]["sources"]),
        ]
        for case in out["cases"]:
            head = f"  {case['name']}: {case['status']} ({case['cells']} cells, "
            head += f"{case['wall_seconds']:.0f} s)"
            lines.append(head)
            for failed in case["failed_checks"]:
                lines.append(
                    f"    failed {failed['check']}: {failed['value']} (limit {failed['threshold']})"
                )
            metrics = case.get("metrics")
            if metrics:
                for key in (
                    "flow",
                    "total_pressure_rise",
                    "total_to_static_pressure_rise",
                    "torque",
                    "shaft_power",
                    "total_efficiency",
                    "total_to_static_efficiency",
                ):
                    if key in metrics:
                        m = metrics[key]
                        extra = f" +- {num(m['uncertainty'])}" if "uncertainty" in m else ""
                        lines.append(
                            f"    {key}: {num(m['value'])}{extra} {m['unit']} "
                            f"[{m['fidelity_label']}, {m['model']}]"
                        )
        ms = out["mesh_study"]
        lines.append(f"mesh study at the duty (fine, medium, coarse): {', '.join(ms['statuses'])}")
        for q, d in ms.get("quantities", {}).items():
            gci = "none" if d["gci_fine"] is None else f"{d['gci_fine']:.2%}"
            lines.append(f"  {q}: {d['convergence']}, GCI_fine {gci}")
        lines.append(
            "  L2 verified (TRUST-002)"
            if ms["verified"]
            else f"  not verified: needs three CONVERGED meshes and GCI < {ms['gci_limit']:.0%}"
        )
        emit(lines)

    run_guarded(body)


# --- optimize ------------------------------------------------------------------------------------

SEARCH_SETTINGS = ("variables", "objective", "max_candidates")
SEARCH_VARIABLES = ("r_hub", "blade_count", "chord_scale", "chord_tip", "camber_scale")


def candidates_of(base: Json, variables: Json, limit: int) -> list[Json]:
    """The full factorial of the stated variable values (OPT-003: deterministic, no seed needed),
    applied to the base design parameters; chord_scale multiplies both chords."""
    unknown = [k for k in variables if k not in SEARCH_VARIABLES]
    if unknown:
        raise Failure(f"unknown search variables: {', '.join(unknown)}")
    names = sorted(variables)
    combos: list[dict[str, Any]] = [{}]
    for name in names:
        combos = [{**c, name: v} for c in combos for v in variables[name]]
    if len(combos) > limit:
        raise Failure(f"OPT-005: {len(combos)} candidates exceed max_candidates {limit}")
    out = []
    for combo in combos:
        params = dict(base)
        scale = float(combo.get("chord_scale", 1.0))
        params["chord_hub"] = base["chord_hub"] * scale
        params["chord_tip"] = base["chord_tip"] * scale
        for key in ("r_hub", "blade_count", "camber_scale", "chord_tip"):
            if key in combo:
                params[key] = combo[key]
        out.append({"variables": combo, "params": params})
    return out


@optimize_app.command("fan")
def optimize_fan(
    spec_id: Annotated[str, typer.Argument(help="Recorded spec id")],
    params: Annotated[Path, typer.Argument(help="Base design parameters (YAML or JSON, m)")],
    search: Annotated[Path, typer.Argument(help="Search settings: variables, objective, budget")],
    cfd: Annotated[Path, typer.Argument(help="CFD settings (as for simulate fan)")],
    workdir: Annotated[Path, typer.Option(help="Directory for the solver cases")] = Path(
        "build/optimize"
    ),
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
    revision: Annotated[int | None, typer.Option(help="Revision (default: latest)")] = None,
) -> None:
    """Exploratory CFD-in-the-loop search (M6; OPT-001 to OPT-006, TRUST-001): every candidate is
    designed by L1 (free vortex), built, and simulated at the duty on the medium mesh; only
    CONVERGED candidates that meet the duty's total pressure are ranked, by the objective; the best
    gets the full study. Exploratory: the L1 model has not passed the reference-fan benchmark."""

    def body() -> None:
        base = load_spec_file(params)
        settings = load_spec_file(search)
        cfd_settings = load_spec_file(cfd)
        missing = [k for k in SEARCH_SETTINGS if k not in settings]
        if missing:
            raise Failure(f"the search settings lack: {', '.join(missing)}")
        objective = settings["objective"]
        if objective != "total_to_static_efficiency":
            raise Failure("the only objective is total_to_static_efficiency (AX-011, maximised)")
        with store_at(store) as opened:
            record = stored_spec(opened, spec_id, revision)
            spec = record.document()
            duty = kernel.spec_compile(spec)["fields"].get("product.duty.pressure") or {}
            kind = (duty.get("qualifier") or {}).get("value")
            if kind != "fan_static" or duty.get("si_value") is None:
                raise Failure(
                    "the duty constraint needs a known fan_static duty pressure (ISO 5801 "
                    "p_s2 - p_t1, compared with the CFD total-to-static rise)"
                )
            required = float(duty["si_value"])
            rows: list[Json] = []
            for i, cand in enumerate(
                candidates_of(base, settings["variables"], int(settings["max_candidates"]))
            ):
                row: Json = {"candidate": i, "variables": cand["variables"]}
                try:
                    output, _ = design_fan_output(spec, cand["params"], [])
                except (Failure, kernel.KernelError) as e:
                    row["status"] = "infeasible"
                    row["reason"] = str(e)
                    rows.append(row)
                    continue
                command = {"command": "design fan", "inputs": {"spec": spec}}
                out, _ = simulate_fan_run(
                    command, output, cfd_settings, workdir / f"candidate_{i:02d}", [("medium", 1.0)]
                )
                case = out["cases"][0]
                row["status"] = case["status"]
                row["failed_checks"] = case["failed_checks"]
                if "metrics" in case:
                    row["metrics"] = {
                        k: case["metrics"][k]["value"]
                        for k in ("total_to_static_pressure_rise", "shaft_power", "flow")
                        if k in case["metrics"]
                    }
                    if objective in case["metrics"]:
                        row["metrics"][objective] = case["metrics"][objective]["value"]
                row["required_fan_static_pressure"] = required
                row["meets_duty"] = bool(
                    "metrics" in row and row["metrics"]["total_to_static_pressure_rise"] >= required
                )
                row["params"] = cand["params"]
                rows.append(row)
            ranked = sorted(
                (r for r in rows if r["status"] in trust_gate.RANKABLE and r["meets_duty"]),
                key=lambda r: -r["metrics"].get(objective, -math.inf),
            )
            best: Json | None = None
            if ranked:
                winner = ranked[0]
                output, _ = design_fan_output(spec, winner["params"], [])
                command = {"command": "design fan", "inputs": {"spec": spec}}
                full, _ = simulate_fan_run(
                    command, output, cfd_settings, workdir / f"best_{winner['candidate']:02d}"
                )
                best = {"candidate": winner["candidate"], "study": full}
            result: Json = {
                "label": "exploratory (OPT-006: the L1 model has not passed the benchmark)",
                "objective": objective,
                "candidates": rows,
                "ranked": [r["candidate"] for r in ranked],
                "best": best,
            }
            run_id = record_run(
                opened,
                "optimize fan",
                {"spec": spec, "params": base, "search": settings, "cfd": cfd_settings},
                result,
            )
        lines = [
            f"optimize fan of {spec_id}: run {run_id} [{result['label']}]",
            f"  objective: maximise {objective}; constraint: CFD total-to-static rise >= the "
            f"duty's fan static pressure ({num(required)} Pa)",
        ]
        for r in rows:
            m = r.get("metrics", {})
            text = ", ".join(f"{k} {num(v)}" for k, v in m.items())
            lines.append(
                f"  #{r['candidate']:02d} {r['variables']}: {r['status']}"
                + (f" ({text})" if text else "")
                + (" meets duty" if r.get("meets_duty") else "")
            )
        if best is None:
            lines.append("  no candidate is both trusted and meets the duty: nothing is ranked")
        else:
            lines.append(f"  best: #{best['candidate']:02d}; full study below")
            for case in best["study"]["cases"]:
                lines.append(f"    {case['name']}: {case['status']}")
            lines.append(f"    verified: {best['study']['mesh_study']['verified']}")
        emit(lines)

    run_guarded(body)


# --- report --------------------------------------------------------------------------------------


def metric_text(m: Json) -> str:
    extra = f" ± {num(m['uncertainty'])}" if "uncertainty" in m else ""
    return f"{num(m['value'])}{extra} {m['unit']} [{m['fidelity_label']}, {m['model']}]"


def report_sources(store: SqliteStore, run_id: int) -> tuple[Json, Json, Json | None, Json | None]:
    """(spec, design params, simulate output or None, optimize output or None) of a design,
    simulate or optimize run."""
    command, output = command_of(store, run_id)
    name = command["command"]
    if name == "design fan":
        return command["inputs"]["spec"], command["inputs"]["params"], None, None
    if name == "simulate fan":
        design_command, _ = command_of(store, int(command["inputs"]["design_run"]))
        return (
            design_command["inputs"]["spec"],
            design_command["inputs"]["params"],
            output,
            None,
        )
    if name == "optimize fan":
        best = output["best"]
        params = command["inputs"]["params"]
        if best is not None:
            params = next(
                c["params"] for c in output["candidates"] if c["candidate"] == best["candidate"]
            )
        sim = best["study"] if best is not None else None
        return command["inputs"]["spec"], params, sim, output
    raise Failure(f"run {run_id} ({name}) is not a design, simulate or optimize run")


def report_lines(
    spec: Json,
    design: Json,
    files: list[str],
    previews: list[str],
    sim: Json | None,
    opt: Json | None,
) -> list[str]:
    compiled = kernel.spec_compile(spec)
    lines = [f"# Design package: {spec.get('title', spec['spec_id'])}", ""]
    provisional = [
        (path, f)
        for path, f in sorted(compiled["fields"].items())
        if f["provisional"] or f["provenance"] in ("default", "unknown")
    ]
    lines += [
        "## 1. Read this first: provisional inputs and unknowns (REP-002)",
        "",
        "Every value below is a default, a placeholder or unknown. Results that depend on them are",
        "not a design for production; replace them with measured or stated values first.",
        "",
        "| Field | Value | Provenance | Note |",
        "| --- | --- | --- | --- |",
    ]
    for path, f in provisional:
        value = f["text_value"] if f["text_value"] is not None else f["original_value"]
        unit = f["original_unit"] or ""
        flags = f["provenance"] + (", provisional" if f["provisional"] else "")
        note = (f["note"] or "").replace("|", "/")
        lines.append(f"| `{path}` | {value} {unit} | {flags} | {note} |")
    lines += [
        "",
        "Design parameters (all placeholders unless stated otherwise):",
        "",
        "| Parameter | Value |",
        "| --- | --- |",
    ]
    lines += [f"| `{k}` | {v} |" for k, v in sorted(design["params"].items())]
    lines += [
        "",
        "Values without a cited source are listed in `docs/unsourced.md`; owner actions in",
        "`docs/open-items.md`.",
        "",
        "## 2. L0 feasibility",
        "",
    ]
    feas = design["output"]["feasibility"]
    lines += [f"- {input_line(n, v).strip()}" for n, v in feas["inputs"].items()]
    lines += [
        f"- check {c['limit']}: {c['status']}: {c['message']}" for c in feas["report"]["checks"]
    ]
    lines += [f"- verdict: {VERDICT_TEXT[feas['report']['verdict']]}", "", "## 3. L1 design", ""]
    lines += design_lines(design["output"])
    lines += ["", "## 4. Geometry (no fidelity label: geometry implies no performance)", ""]
    for part in design["output"]["geometry"]["result"]["parts"]:
        v, m = part["validity"], part["mass_properties"]
        lines.append(
            f"- {part['name']}: validity {'ok' if v['ok'] else 'FAILED'}, "
            f"volume {num(m['volume'])} m3"
        )
    lines += ["", "Files:", ""] + [f"- `files/{f}`" for f in files]
    lines += [""] + [f"![{Path(p).stem}](previews/{Path(p).name})" for p in previews]
    lines += ["", "## 5. L2 simulation (CFD)", ""]
    if sim is None:
        lines.append("Not simulated.")
    else:
        lines += [
            f"Solver {sim['solver']}, template {sim['case_template']} "
            "(docs/models/fans/axial-cfd.md). Trust thresholds:",
            "",
        ]
        lines += [f"- {src}" for src in sim["thresholds"]["sources"]]
        lines.append("")
        for case in sim["cases"]:
            lines.append(f"### {case['name']}: {case['status']} ({case['cells']} cells)")
            lines.append("")
            for f in case["failed_checks"]:
                lines.append(f"- failed {f['check']}: {f['value']} (limit {f['threshold']})")
            metrics = case.get("metrics")
            if metrics:
                for key in (
                    "flow",
                    "total_pressure_rise",
                    "total_to_static_pressure_rise",
                    "torque",
                    "shaft_power",
                    "total_efficiency",
                    "total_to_static_efficiency",
                ):
                    if key in metrics:
                        lines.append(f"- {key}: {metric_text(metrics[key])}")
                for note in metrics.get("notes", []):
                    lines.append(f"- note: {note}")
            elif case["status"] not in trust_gate.RANKABLE:
                lines.append("- no numbers: the result did not pass the trust gate (TRUST-001)")
            lines.append("")
        ms = sim["mesh_study"]
        lines.append(f"Mesh study at the duty (fine, medium, coarse): {', '.join(ms['statuses'])}")
        for q, d in ms.get("quantities", {}).items():
            gci = "none" if d["gci_fine"] is None else f"{d['gci_fine']:.2%}"
            lines.append(f"- {q}: {d['convergence']}, GCI_fine {gci}")
        lines.append(
            "- L2 verified (TRUST-002)" if ms["verified"] else "- not verified (TRUST-002)"
        )
    if opt is not None:
        lines += ["", f"## 6. Search: {opt['label']}", ""]
        lines += [
            "| # | Variables | Status | Total-to-static rise (Pa) | Efficiency | Meets duty |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
        for r in opt["candidates"]:
            m = r.get("metrics", {})
            rise = (
                num(m["total_to_static_pressure_rise"])
                if "total_to_static_pressure_rise" in m
                else ""
            )
            eff = num(m[opt["objective"]]) if opt["objective"] in m else ""
            lines.append(
                f"| {r['candidate']} | {r['variables']} | {r['status']} | {rise} | {eff} | "
                f"{'yes' if r.get('meets_duty') else 'no'} |"
            )
    lines += [
        "",
        "## 7. What this package is not",
        "",
        "- Not L3: nothing here has been compared with a rig measurement (AX-017).",
        "- The duty is a TRIAL value (ADR-016) unless the spec says otherwise.",
        "- L1 numbers are loss-free with zero deviation (UNSOURCED losses and deviation).",
    ]
    return lines


@report_app.command("fan")
def report_fan(
    run_id: Annotated[int, typer.Argument(help="A design fan, simulate fan or optimize fan run")],
    out: Annotated[Path, typer.Option(help="Directory for the package")] = Path("build/package"),
    store: Annotated[Path, typer.Option(envvar=STORE_ENV, help="Store directory")] = Path(
        DEFAULT_STORE
    ),
) -> None:
    """Design package (REP-001 to REP-004): provisional inputs first, L0, L1, geometry files and
    previews, L2 results with their trust status, and the search if any."""

    def body() -> None:
        with store_at(store) as opened:
            spec, params, sim, opt = report_sources(opened, run_id)
        output, files = design_fan_output(spec, params, FAN_EXPORTS)
        (out / "files").mkdir(parents=True, exist_ok=True)
        (out / "previews").mkdir(parents=True, exist_ok=True)
        names = []
        stls = []
        for part, export, data in files:
            name = f"{part}-{export['sha256'][:12]}.{export['format']}"
            write_named(out / "files", name, data)
            names.append(name)
            if export["format"] == "stl":
                stls.append(data)
        previews = [str(p) for p in write_previews(stls, out / "previews", "fan")]
        design = {"params": params, "output": output}
        text = "\n".join(report_lines(spec, design, names, previews, sim, opt)) + "\n"
        (out / "report.md").write_text(check_claims(text), encoding="utf-8")
        package = {"spec": spec, "design": design, "simulation": sim, "search": opt}
        (out / "data.json").write_text(
            json.dumps(package, indent=1, sort_keys=True), encoding="utf-8"
        )
        emit(
            [
                f"design package of run {run_id}: {out / 'report.md'}",
                *[f"  files/{n}" for n in names],
            ]
        )

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
    if name == "geometry fan":
        return fan_output(kernel.product_operation(inputs))[0]
    if name == "design fan":
        return design_fan_output(inputs["spec"], inputs["params"], inputs["exports"])[0]
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

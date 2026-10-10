# cemkit CLI (T14)

Code: `python/cemkit/cli/app.py` (Typer). Inside the container: `scripts/dev.sh scripts/cemkit …`
(`python -m cemkit.cli` with `python/` on the path; a wheel installs a `cemkit` script). The
release preset must have built `cemkit._kernel` (ADR-013). Decisions: ADR-014 (proposed).
Requirements: IN-001, UC-01 to UC-03, UC-08, REP-001, REP-004.

The store is `--store DIR`, else `$CEMKIT_STORE`, else `.cemkit-store/` in the current directory.

| Command | Does | Records a run |
| --- | --- | --- |
| `cemkit spec compile <file> [--autonomous]` | Loads JSON or YAML (`.json`, `.yaml`, `.yml`; NaN/inf refused), compiles it in the kernel, records the spec, prints every field (value as given = SI value, provenance) and the open questions | yes |
| `cemkit spec questions <spec-id> [--revision N]` | Recompiles a recorded spec and prints its open questions | no |
| `cemkit feasibility <spec-id> [--revision N]` | The kernel maps the spec's duty point, speed and air onto the L0 gate (docs/capi.md, spec form) and runs the family's hook; prints the inputs, ω_s, every check, violated limits, nearest feasible duties and the verdict | yes |
| `cemkit geometry smoke [--out DIR] [--json]` | Builds the geometry **test** solid (T10 test parameters), prints validity, topology and mass properties, exports STEP and STL, stores them as artifacts named `<sha256>.<ext>` (and writes them to `DIR`) | yes |
| `cemkit runs show <run-id>` | The run's STORE-002 metadata (unknown fields listed), specs and artifacts | no |
| `cemkit runs reproduce <run-id>` | Re-runs the recorded command from its recorded inputs and compares the output; on a difference it names the differing keys and the version changes, or says the versions do not explain it | no |

**Output rules.** Every computed number carries its unit, fidelity label and model
(`131.53 Pa [L0 predicted, fans.l0@1.0.0]`, REP-001). Spec values are inputs: they carry their
provenance instead. Geometry properties are not predictions and say so. All text, including text
quoted from a spec, passes the banned-claims check (`cemkit.reporting.claims`, REP-004) before it
is printed; a refusal prints nothing of the output and exits 3.

**Runs (STORE-002, UC-08).** A command that computes something records a run with the kernel,
model and plugin versions, git commit, container digest and input hash, plus two artifacts:
`command.json` (`{"command", "inputs"}`) and `output.json` (the kernel response without version
keys). `runs reproduce` compares a fresh output with `output.json` byte for byte (canonical JSON).

**Exit codes.** 0 done (an *unconfirmed* feasibility verdict is 0); 1 rejected input, unknown id or
a reproduction that differs; 2 infeasible duty; 3 output refused by the banned-claims check.

**Clean stdout.** OCCT messages go to the log, never to stdout (owner decision D5, HI-013
resolved). `geometry smoke --json` prints one JSON document (`command`, `run`, `output`, `files`)
and nothing else on stdout; `tests/python/test_cli.py` checks this in a child process.

**Known limitations.** `feasibility` checks the tip speed with the rotor tip diameter the family
derives from the spec (owner decision D6, provisional, HI-023; docs/capi.md); without one the check
is `not_computable`. `fans.axial_ducted` is a minimal plugin (HI-014).

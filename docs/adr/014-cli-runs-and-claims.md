# ADR-014: CLI run recording, reproduction and the banned-claims scope

- Status: proposed (needs owner review)
- Date: 2026-10-10
- Requirements affected: UC-03, UC-08, STORE-002, REP-001, REP-004

## Context
T14 adds the CLI. Four points were not fixed by the spec: how a command records what it did so
that `runs reproduce` can redo it; what "the same output" means; whether the banned-claims check
covers text quoted from a spec; and where the spec-to-gate mapping for `cemkit feasibility` lives.

## Options considered
1. Recording: (a) store results as result records (needs candidates, which do not exist at L0);
   (b) record a run with `command.json` and `output.json` artifacts.
2. Sameness: (a) byte-identical canonical JSON of the kernel response without version keys;
   (b) numeric tolerance. The kernel is deterministic (COR-002), so (a) is achievable and strict.
3. Claims scope: (a) only system-written text; (b) all printed text, including quoted spec text.
4. Spec-to-gate mapping: (a) Python glue; (b) the kernel (C ABI feasibility, spec form).

## Decision
1b, 2a, 3b and 4b: runs carry `command.json` and `output.json`; reproduction compares canonical
JSON and explains differences by version changes; every printed line is checked (a refusal prints
nothing, exit 3); the kernel maps the spec onto the gate (CLAUDE.md rule 2).

## Consequences
- A spec note or name containing a banned word cannot be printed until it is reworded.
- Commands that compute nothing (`spec questions`, `runs show`) record no run.
- When result records for candidates arrive (L1), `runs show` should list them too.

# Open Items (Human Acceptance / Actions Required)

This document tracks items that require human owner action, verification, or review.

| ID | Item | Origin | Notes / Action Required |
|---|---|---|---|
| HI-001 | Update `status: proposed` to `status: verified` in `tests/hand_calcs/verified/l0_001`..`l0_009` | T06 | Folder `tests/hand_calcs/verified/` is protected from AI modification. Owner must edit metadata in-place. |
| HI-002 | Review proposed hand calculation `tests/hand_calcs/proposed/l0_010_free_delivery.yaml` | T06 | Free delivery test case for fan static-to-total conversion. Move to `verified/` once checked. |
| HI-003 | Source Cordier specific-speed boundaries for axial fans | T08 | `data/fans/family_ranges.yaml` Cordier boundaries are marked `UNSOURCED` pending verified engineering literature citation. |
| HI-004 | Review ADR-003 D8 (spec compiler semantics and unit table) | T07 | Written during autonomous work; marked "proposed (needs owner review)" inside accepted ADR-003. Accept, amend or reject. |
| HI-005 | Decide the inH₂O basis | T07 | **Resolved (owner decision D3, 2026-10-10):** Conventional inH₂O = 249.08891 Pa and mmH₂O = 9.80665 Pa (NIST SP 811). Temperature-specific bases (e.g. inH2O@60F) must be explicitly stated in input; because current schema/compiler does not support them, they are rejected with `spec_rejected` naming the field. |
| HI-006 | Support operating range / system curve in kernel spec compiler (SPEC-011) | T07 | Single duty point is implemented in C++ kernel spec compiler; operating curve input is deferred to future revision. |
| HI-007 | Support YAML format directly in kernel spec compiler (IN-001) | T07 | JSON parser is implemented in kernel; YAML parsing is handled in Python orchestration layer. |
| HI-008 | Review ADR-011 (L0 feasibility gate: "range unsourced" status, nearest-feasible-duty rule, check order) | T08 | Proposed during autonomous work. Accept, amend or reject; the nearest-duty metric (log-space vs fixed flow) is the main choice. |
| HI-009 | Review proposed hand calculation `tests/hand_calcs/proposed/l0_011_conventional_dynamic_pressure.yaml` | T08 | Conventional fan dynamic pressure and its outlet-Mach limit. Move to `verified/` once checked. |
| HI-020 | Review ADR-012 (family plugin interface, required registry-backed essential-field resolver, unknown family rejected, parameter units as SI + kind, folder discovery) | T09 | Proposed during autonomous work. Accept, amend or reject. |
| HI-021 | Decide whether FAM-002 must hold literally (no edit at all outside the family folder) | T09 | Today a new family also adds its include and call to the product's `register.cpp`, as ADR-009 states. A literal reading needs a CMake-generated registration list (ADR-012 option 4). |
| HI-022 | `fans.axial_ducted` has no family plugin yet | T09 | The spec compiler no longer has a built-in essential list; until the axial_ducted family declares `product.duty.flow` and `product.duty.pressure` (ADR-003), a registry-backed compile rejects it as an unknown family. Code merged from other branches that used the T07 default `CompilerOptions` must pass a resolver. |

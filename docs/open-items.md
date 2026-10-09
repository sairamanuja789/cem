# Open Items (Human Acceptance / Actions Required)

This document tracks items that require human owner action, verification, or review.

| ID | Item | Origin | Notes / Action Required |
|---|---|---|---|
| HI-001 | Update `status: proposed` to `status: verified` in `tests/hand_calcs/verified/l0_001`..`l0_009` | T06 | Folder `tests/hand_calcs/verified/` is protected from AI modification. Owner must edit metadata in-place. |
| HI-002 | Review proposed hand calculation `tests/hand_calcs/proposed/l0_010_free_delivery.yaml` | T06 | Free delivery test case for fan static-to-total conversion. Move to `verified/` once checked. |
| HI-003 | Source Cordier specific-speed boundaries for axial fans | T08 | `data/fans/family_ranges.yaml` Cordier boundaries are marked `UNSOURCED` pending verified engineering literature citation. |
| HI-004 | Review ADR-003 D8 (spec compiler semantics and unit table) | T07 | Written during autonomous work; marked "proposed (needs owner review)" inside accepted ADR-003. Accept, amend or reject. |
| HI-005 | Decide the inH₂O basis | T07 | Conventional 249.08891 Pa is used. Datasheets at 60 °F (248.84 Pa) or 39.2 °F (249.082 Pa) differ by up to 0.1 %. Decide whether a reference temperature must be stated. |

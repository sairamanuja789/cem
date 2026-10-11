# FAN-01 benchmark: dataset permissions and geometry completeness

Data: `data/reference_fans/fan01_eaa_benchmark.yaml`. Decision on use: ADR-020 (proposed). Status:
**preparation only**: no CFD case exists yet.

## Permissions

| Item | Finding |
| --- | --- |
| Dataset | "FAN 01 Benchmark: Low pressure Axial Fan in a short Duct", Zenodo, v5 (2026-05-05), doi:10.5281/zenodo.20037409 |
| Licence | **CC BY 4.0**: sharing and adaptation allowed, including commercially, with attribution and an indication of changes |
| Our use | Benchmark comparison of the CFD workflow; repaired geometry derived from the IGES (an adaptation: the changes are recorded in the provenance file) |
| Attribution | The text in the data file's `attribution` field must appear wherever FAN-01 data or derived geometry is shown |
| Stored in this repository | Metadata, hashes and the 20-point characteristic (CC BY 4.0 allows it, with attribution). **Not** the CAD or the PDF (size); those are kept read-only outside the repository with SHA-256 sums |

## Geometry completeness (`cad_model.igs`, one-off FreeCAD 1.1.1 inspection, outside the kernel)

| Check | Result |
| --- | --- |
| Contents | 3864 loose B-spline faces, no shells or solids; rotor plus parts of the test setup (plates, posts, duct pieces) spanning about 2.6 × 1.4 × 2.7 m |
| Rotor location | Axis along Y at X = −400, Z = 1515 mm; rotor surfaces at Y ≈ −406 to −307 mm |
| Blade count | **9** (9-fold symmetric face groups; consistent with the dataset figures) |
| Tip radius | **247.50 mm** (max vertex radius) |
| Hub | Radius 118–127 mm; **open cylinder** (no end caps) |
| Blade–hub junction | **Not trimmed or fused**: open loops at the blade roots (r ≈ 119–122 mm) |
| Sewing (tolerance 0.01 mm) | One 65-face open shell (blades and hub), one 5-face shell and 5 loose faces near the axis |
| Tip gap | **UNKNOWN**: duct surface samples in the rotor span reach r = 247.17 mm, inside the tip radius, which is inconsistent (HI-034) |
| Parasolid `cad_model.x_t` | Not inspected (no reader in the toolchain; adding one is a dependency decision) |
| Verdict | **Incomplete for CFD as delivered.** Usable after the repairs listed in ADR-020 (trim or fuse the blade roots, close the hub, Boolean fuse, then verify) |

## Recorded discrepancies

- The power column is named `P_shaft_in_Nm`, but its values are watts (they reproduce η_ts = 53 % at
  1.4 m³/s).
- The rotational speed is not in the data files; 1500 rpm is inferred from the 225 Hz blade-passing
  frequency with 9 blades.
- The test uncertainty is not stated; the round-robin study's figures are not yet extracted.
- The tip gap is undetermined (see above).

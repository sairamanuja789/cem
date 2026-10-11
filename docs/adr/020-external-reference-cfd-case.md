# ADR-020: External-reference CFD case (FAN-01) for CFD workflow credibility

- Status: accepted (owner, 2026-10-12), with the provisional ±10 % pressure target
- Date: 2026-10-12
- Requirements affected: SIM-001, SIM-003, SIM-004, TRUST-001 to TRUST-004, REP-001, REP-004, AX-017

## Context

**Gate 1** (docs/architecture.md: "first fan validated (L1 and L2 compared with rig data)") needs rig
data for *our* fan; none exists yet (HI-025). Before relying on CFD for design, the owner asked for
a check of the CFD workflow against an openly licensed research fan with measured performance.

- **The case:** FAN-01 (EAA benchmark; Zenodo doi:10.5281/zenodo.20037409, v5, CC BY 4.0;
  `data/fans/reference_fans/fan01_eaa_benchmark.yaml`).
- **Why it fits:**
  - a 9-blade rotor with no stator, in a 500 mm short duct;
  - ISO 5801 total-to-static characteristic plus shaft power;
  - the same pressure definition our metrics report (`total_to_static_pressure_rise`).
- **Gaps:**
  - The geometry is open surfaces (IGES): the blades are not fused to the hub, and the hub is open.
  - The tip gap is unknown.
  - The speed (1500 rpm) is inferred.
  - The test uncertainty is not stated.
  - The chord Reynolds number is about 10× or more above our fan's.

Our CFD template (`cemkit.openfoam.rotor_sector_mrf@0.3.0`) builds the blade from our own L1
sections. It can't take an external rotor.

## Options considered

1. **New case type for an external rotor solid** in the simulation port:
   - one blade passage, MRF, the same solver settings, trust gate and metrics;
   - the rotor comes from a provenance-recorded STEP, not from L1.
   - Pros: it reuses the trusted pipeline, so it is a fair workflow check.
   - Cons: a new port type and a geometry import path.
2. **Hand-built OpenFOAM case outside cemkit.** Pros: quick. Cons: it doesn't test *our* workflow,
   so it defeats the purpose.
3. **Wait for a physical fan.** Pros: it addresses our Reynolds number. Cons: it blocks all CFD
   credibility work until hardware exists.

## Decision

Option 1: an **external-reference rotor case**. It is a separate case type with its own template
version, and it keeps the trust gate, metrics, labels and reports unchanged.

1. **Geometry route:**
   - The one-off conversion runs outside the kernel: FreeCAD from the IGES to a closed rotor solid.
     The originals and scripts are kept with SHA-256 hashes (`~/Desktop/cem-geometry/fan01/`).
   - Repair steps: trim or extend the blade roots into the hub; close the hub to the extended-hub
     convention (ADR-017); Boolean fuse.
   - Each step is recorded in a provenance file.
   - No IGES or Parasolid reader is added to the pinned OCCT (LIFE-002).
2. **The kernel takes STEP/STL only** and checks the solid before any case is written:
   - validity;
   - blade count 9;
   - tip radius 247.50 mm ± 0.05 mm;
   - the solid fits inside the stated duct radius.
3. **Domain:**
   - one 40° passage, cyclic sides;
   - the duct radius and the inlet and outlet extensions follow the test setup as far as the dataset
     documents it.
   - Undocumented items stay labelled: the nozzle and diffuser shape, and the tip gap (HI-034).
4. **Operating points:** the measured flows at and around the design point (1.2, 1.3, 1.4, 1.5 and
   1.6 m³/s) at the inferred 1500 rpm. Optionally one point in the stall plateau (1.0 m³/s), expected
   to be refused by the trust gate.
5. **Labels:**
   - CFD results are L2 simulated, or L2 verified after the mesh study (TRUST-002), for *FAN-01*.
   - Comparison with the measured curve is reported as "benchmark comparison". It is **not**
     validation of our fan, does not close Gate 1, and earns no L3 label (REP-004).

## Acceptance criteria (accepted 2026-10-12)

**A. The case is fit to compare:**
- the trust gate gives CONVERGED or VERIFIED at the compared points;
- a three-mesh GCI study at 1.4 m³/s (TRUST-002) is reported per quantity.

**B. The comparison is honest:**
- for every compared point, report the simulated Δp_ts, η_ts and shaft power next to the measured
  values;
- show both the difference and the combined uncertainty band: the numerical uncertainty (GCI) and
  the experimental uncertainty, which is **UNKNOWN** until the round-robin figures are extracted;
- show the assumption list: inferred speed, unknown tip gap, simplified nozzle and diffuser, MRF,
  and k-ω SST.

**C. Credibility statement:** the workflow is called *credible for this case* only if all three hold:
1. criterion A passes;
2. the simulated Δp_ts at 1.4 m³/s lies within **±10 %** (provisional owner target) of the measured value;
3. the curve trend over 1.2–1.6 m³/s has the same sign of slope.

**D. Otherwise:** the discrepancy is reported with candidate causes and nothing is tuned to fit.
Calibration needs separate, recorded data (PHY-006, holdout rules).

## Consequences

- **Product-neutral platform (owner, 2026-10-12):**
  - the generic simulation port carries only what any rotating-sector case needs;
  - FAN-01 data (`data/fans/reference_fans/`), the external-rotor case writer's fan-specific inputs
    and all fan design searches live in the fans application module (`kernel/products/fans/`,
    `python/cemkit/products/fans/` where applicable), never in the platform.

- **Easier:**
  - a first evidence-based statement about our CFD workflow;
  - a reusable path for other open reference fans (USI7, if its terms allow).
- **Harder:**
  - a new port type and template to maintain;
  - geometry repair is manual and must be redone if the dataset changes.
- **Revisit when:** the round-robin uncertainty is extracted, the tip gap and speed are confirmed
  (HI-034), or a physical fan is available. Gate 1 stays blocked throughout.

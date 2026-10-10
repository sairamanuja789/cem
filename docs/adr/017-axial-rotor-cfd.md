# ADR-017: Axial rotor CFD case, trust thresholds and where each part runs

- Status: proposed (needs owner review)
- Date: 2026-10-11

## Context

M5 needs L2 results for the step-5 design: OpenFOAM cases written from templates plus design data
(SIM-001), run under resource limits (SIM-002) at three or more operating points (SIM-003), with
flow, pressures, torque, power and stated efficiencies (SIM-004), and a trust gate with per-template
thresholds (TRUST-001 to TRUST-004). docs/architecture.md plans one blade passage with MRF and k-ω
SST, 0.5–2 M cells, a few MPI ranks.

## Decision

1. **Case placement follows the geometry pattern.** `kernel/cemkit/simulation/port` defines the
   solver-neutral `RotorSectorCase` and the `CaseWriter` interface; `simulation/openfoam` implements
   it (template `cemkit.openfoam.rotor_sector_mrf@0.2.0`); the C ABI lends the writer to product
   operations through `OperationContext::case_writer`, as it lends the geometry backend. The family
   only states what to simulate (`rotor_cfd_case`). The kernel writes files and command steps and
   never runs a solver.
2. **Domain:** one passage, rotationally periodic `cyclicAMI` sides, the hub extended through the
   domain, MRF over the hub length, stationary duct wall. The motor, frame and struts are not
   modelled (stated in every result's documentation).
3. **Running:** Python writes and runs the steps through `run_limited` (ADR-005), one case at a time
   with MPI ranks = `cores` (≤ 6 on this machine), because the runner pins children to the first N
   CPUs and concurrent single-core cases would share one CPU.
4. **Trust gate and mesh study in Python** (`python/cemkit/simulation/trust_gate.py`,
   `mesh_study.py`), as docs/repository-structure.md (ADR-009) places them. The metrics that turn
   monitors into engineering quantities are a kernel family operation (`rotor_cfd_metrics`), which
   refuses any status other than CONVERGED or VERIFIED (`sim_untrusted`).
5. **Thresholds (template 0.1.0, provisional):** residuals ≤ 1e-4 (UNSOURCED), mass imbalance
   ≤ 0.5% and flatness over the last 20% (docs/architecture.md, proposed), flatness ≤ 1% relative
   range (UNSOURCED). Every assessment prints these sources.
6. **Air viscosity:** Sutherland's law from the U.S. Standard Atmosphere 1976 (eq. 51) as a new
   model `platform.air.viscosity@1.0.0`, used when the spec states none.

## Consequences

- A new solver adds a writer under `kernel/cemkit/simulation/<solver>/`; families do not change.
- CI has no OpenFOAM (the CI image); runner tests use a stand-in wrapper
  (`CEMKIT_OPENFOAM_WRAPPER`) that writes synthetic monitors. Real runs happen in the dev image.
- The owner must accept or replace the UNSOURCED thresholds before any L2 result supports a
  decision (HI-028).

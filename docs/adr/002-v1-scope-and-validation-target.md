# ADR-002: v1 scope and validation target

- Status: proposed (needs owner review). Parts of the decision are still open (below); the owner
  supplies them before the first design campaign.
- Date: 2026-10-10
- Requirements affected: section 1 (scope), section 8 (acceptance), UC-07, SPEC-011, L3 fidelity

## Context

"Done" for v1 and what counts as evidence have to be defined before significant work
(`docs/architecture.md` section 12). Requirements section 1 sets the scope. Requirements section
7 (product requirements for v1): the first product is a 3D-printed ducted axial fan of 120 mm nominal size,
whose duty point "is still unknown and must be supplied before the first campaign".

## Decision

1. **Product:** one family in v1, a 3D-printed ducted axial fan of 120 mm nominal size.
2. **Out of scope for v1** (requirements section 1):
   - motors and electronics, and bearings;
   - acoustic prediction in dB;
   - transient CFD;
   - cross-flow and bladeless fans;
   - cloud deployment and multi-user operation.
3. **Validation target:** a design is evidenced at L3 only by rig measurement with stated
   uncertainty (UC-07), compared against its predictions at one concrete duty point.
4. **Reference fans:** four commercial 120 mm fans are measured on the same rig, to check the rig
   and the models.
5. **Rig commitment:** a test rig is built, with a flow nozzle, a pressure sensor, a tachometer
   and a power meter (architecture section 12).

## Open points (owner)

- **The duty point:** flow and pressure, with pressure kind and tolerances (SPEC-011). This is
  not yet supplied, and it blocks the first campaign.
- **The four reference fans:** not yet chosen.
- **The rig:** the parts list and the measurement standard. Rig design is outside this ADR.
- **Whether v1 claims L3 for one design or for several holdout fans.** The architecture plan
  mentions holdout fans per family.

## Consequences

- **The base release (T00–T15) is the platform only.** Its geometry is a test solid, and the
  ducted axial plugin is minimal: blade design, the geometry recipe and L1 evaluation return
  `not_implemented` (HI-014).
- **Reports:** no v1 report may call a design "validated" until the L3 evidence above exists
  (REP-004).

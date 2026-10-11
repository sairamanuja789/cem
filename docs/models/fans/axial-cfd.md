# Axial rotor CFD (L2): template cemkit.openfoam.rotor_sector_mrf@0.3.0

Kernel: case definition `kernel/cemkit/simulation/port/case.hpp` (`RotorSectorCase`), OpenFOAM
writer `kernel/cemkit/simulation/openfoam/writer.cpp`, family operations `rotor_cfd_case` and
`rotor_cfd_metrics` (`kernel/products/fans/families/axial_ducted/cfd_case.cpp`). Orchestration:
`python/cemkit/simulation/` (runner, trust gate, mesh study) and `cemkit simulate fan`.
Decision record: ADR-017.

## What is solved

Steady incompressible RANS (OpenFOAM v2512 `simpleFoam`, k-ω SST, wall functions) in **one blade
passage** of the rotor in its duct: a 2π/N annular sector with rotationally periodic `cyclicAMI`
sides, a moving reference frame (MRF) over the rotor's axial extent |z| ≤ L_hub/2, flow along +z,
rotation counter-clockwise viewed from the inlet (about −z, ADR-016). The duct wall is stationary;
the blade and the hub inside the MRF zone rotate.

| Item | Setting | Basis |
| --- | --- | --- |
| Domain | r_hub ≤ r ≤ r_duct, upstream and downstream lengths from the CFD settings | modelling choice |
| Hub | extended through the whole domain (an annular duct) | simplification: the motor, frame and struts are not modelled |
| Mesh | blockMesh sector in three axial blocks (zone ends on block faces), snappyHexMesh refinement at the blade and in the zone, no prism layers | modelling choice |
| Inlet | flow rate (Q/N), turbulence intensity and mixing length from the settings | inflow turbulence UNSOURCED |
| Outlet | p = 0 (kinematic), inletOutlet for the rest | standard |
| Air | ρ and ν = μ/ρ: μ from the spec or Sutherland (platform.air.viscosity@1.0.0) | USSA 1976 |
| Iterations | fixed count from the settings; no early stop, so the flatness check sees the whole tail | modelling choice |

## Monitors and metrics (rotor_cfd_metrics, model fans.axial.cfd_metrics@0.1.0)

Per passage, final iteration: inlet and outlet flow, area-averaged static pressure,
volume-flow-weighted total pressure (Σφ p_t / Σφ), and the moment about +z on the **rotating
surfaces only**: the blade and the hub patch inside the rotating zone (`hubRotor`; the upstream and
downstream hub patches are stationary walls and are excluded). With N passages:

| Quantity | Definition |
| --- | --- |
| Flow | Q = N Q_outlet |
| Total pressure rise | Δp_t = ρ (p̄t_out − p̄t_in), both flow-weighted; the outlet swirl is included (a rotor without a stator does not deliver it as static pressure) |
| Total-to-static rise | Δp_ts = ρ (p̄_out − p̄t_in): outlet area-averaged static minus inlet total, the form of ISO 5801 fan static pressure p_s2 − p_t1 (requirements section 2), in this rotor domain |
| Torque, power | T = N M_z (the rotor turns about −z, so T > 0 absorbs power), P = T ω |
| Total efficiency | η_t = Q Δp_t / P, only for Δp_t > 0 |
| Total-to-static efficiency | η_ts = Q Δp_ts / P, only for Δp_ts > 0 (AX-011's efficiency type) |

The geometry recipe keeps every blade section inside |z| ≤ L_hub/2 (it rejects a root reaching past
the hub's end faces), so the whole blade lies in the rotating zone.

These are rotor-domain quantities, not ISO 5801 test results: the domain has no inlet bell, frame
or the standard's downstream duct. The outlet holds a uniform static pressure only about 0.9 D
downstream while the flow still swirls, which biases p̄_out (a modelling error the GCI does not
measure).

## Trust (TRUST-001 to TRUST-004)

Each case gets exactly one status. Thresholds belong to the template version:

| Check | Threshold | Source |
| --- | --- | --- |
| Final initial residual, every field | ≤ 1e-4 | project decision (ADR-017, HI-028) |
| Mass imbalance | ≤ 0.5% | project decision (ADR-017, HI-028) |
| Torque and total-pressure-rise monitors | relative range ≤ 1% over the last 20% of iterations | project decision (ADR-017, HI-028) |
| Mesh | `checkMesh -meshQuality`: "Mesh OK", or skewness is the only generic failure and no face is in error under meshQualityDict (internal skewness 4, boundary 20) | OpenFOAM v2512 `etc/caseDicts/mesh/generation/meshQualityDict.cfg`; accepting generic boundary skewness above 4 on the meshQualityDict metric is a project decision (ADR-017) |

Numbers are reported only for CONVERGED (L2 simulated) and VERIFIED (L2 verified) results;
any other status is `sim_untrusted`. VERIFIED needs a three-mesh study at the duty point with every
studied quantity's GCI (Celik et al. 2008, `mesh_study.py`) below TRUST-002's limit (5%, proposed):
both pressure rises, the torque and both efficiencies. Only quantities with their own uncertainty
(GCI_fine × |φ_fine|; shaft power from the torque's times ω) are labelled L2 verified; the flow,
imposed by the inlet condition, stays L2 simulated.

## What L2 here does not mean

L2 says the equations were solved acceptably for this geometry and these assumptions. It is not
L3: no rig measurement has compared it (AX-017). The extended hub, the missing frame, wall
functions at chord Reynolds numbers of order 10⁴ and the UNSOURCED inflow turbulence are modelling
errors the GCI does not measure.

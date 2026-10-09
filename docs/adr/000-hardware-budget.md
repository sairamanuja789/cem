# ADR-000: Hardware budget: OpenFOAM memory per million cells and the job memory cap

- Status: accepted (user, 2026-10-10)
- Date: 2026-10-10
- Requirements affected: ASM-001, RES-001, RES-002, CON-001

## Context

RES-001 requires every heavy job to be capped below physical RAM, leaving at least 3 GB for the OS.
The requirement proposes a 12 GB cap on 16 GB. RES-002 requires CFD memory to be estimated from the
cell count before meshing, using a measured RAM-per-cell figure. ASM-001 assumes that a
single-passage OpenFOAM MRF case fits under that cap at mesh-study resolution. This ADR records the
measurement.

**Machine (CON-001):**
- CPU: 12th Gen Intel Core i5-12450H, 12 logical CPUs.
- RAM: 16 088 576 KiB = 15.34 GiB.
- Kernel: Linux 7.0.0-34.
- Power: on mains.
- Image: dev image `sha256:06de9d23…2b2d`, OpenFOAM v2512 (ADR-006).
- Commit: `23e58a9`.
- Run: 2026-10-09T18:34:29Z, by the user.

**Case.** The script is `scripts/measure_openfoam_memory.sh`. It uses the OpenFOAM tutorial
`incompressible/simpleFoam/rotatingCylinders` (MRF). OpenFOAM v2512 has no 3D incompressible MRF
tutorial, so the case is made 3D, with the user's approval:
- n cells in z;
- front and back patches changed from empty to symmetry;
- k-ω SST instead of laminar, the planned fan setup;
- 32 n³ hexahedral cells;
- 100 SIMPLE iterations, 4 MPI ranks, scotch decomposition.

**Measurement.** Every figure is the peak during the run.
- **Rank sum:** each rank runs under `/usr/bin/time -v`, and the four peak RSS values are summed.
  This is an upper bound: the ranks need not peak together, and each rank's RSS also counts the
  shared OpenFOAM libraries once per rank.
- **Container:** each solver run starts in a fresh container, and the cgroup v2 `memory.peak` is
  read at the end. This covers every process, MPI included. It also counts page cache, and it
  counts shared pages once.
- **Meshing:** blockMesh and decomposePar run in their own container and are measured separately.

## Measured results

| n | Cells | Solver, rank sum (MiB) | Solver, container (MiB) | blockMesh (MiB) | decomposePar (MiB) | Solver wall, 100 it. (s) | s / iteration |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20 | 256 000 | 613 | 453 | 239 | 183 | 16.5 | 0.165 |
| 25 | 500 000 | 1 045 | 879 | 440 | 325 | 38.1 | 0.381 |
| 32 | 1 048 576 | 1 811 | 1 622 | 892 | 643 | 82.0 | 0.820 |

These figures are rounded from `results.csv`, given below.

Least-squares fit of memory = a + b × (cells / 10⁶), recomputed from the raw KiB values:

| Measure | b: MiB per million cells | a: fixed MiB | Largest residual (MiB) |
| --- | ---: | ---: | ---: |
| Solver, rank sum | 1 492 | 259 | 41 |
| Solver, container | 1 455 | 110 | 42 |
| blockMesh | 824 | 28 | 0.2 |
| decomposePar | 580 | 35 | 0.1 |

The two solver measures agree on the per-cell cost to within 3 %. They differ mainly in the fixed
part, because the rank sum counts the shared libraries four times. Memory per million cells falls
as the case grows (2 393 → 1 727 MiB by rank sum) because the fixed part is spread over more cells.

Raw `results.csv`, with all memory values in KiB:

```
n,cells,blockMesh_maxrss_kib,decomposePar_maxrss_kib,ranks,rank_maxrss_sum_kib,rank_maxrss_max_kib,solver_cgroup_peak_kib,solver_wall_s,iterations
20,256000,244740,187760,4,627296,157372,463916,16.49,100
25,500000,450840,332348,4,1070588,268956,900452,38.09,100
32,1048576,913144,658000,4,1854092,468396,1661024,82.03,100
```

## Options considered

1. **Cap of 12 GiB (12 288 MiB).** This leaves 3.34 GiB (3.59 GB) for the OS, which meets RES-001's
   3 GB minimum. It matches the requirement's proposal.
2. **A lower cap, for example 11 GiB.** This leaves more room for a desktop session and a browser
   during runs, but allows about 0.7 M fewer cells.
3. **No fixed cap, using free memory at job start.** Rejected: results would depend on what else
   is running, which breaks reproducibility and RES-001.

## Decision

The job memory cap is **12 GiB** for all job memory, enforced by the runner (RES-001). The RES-002
estimator is the more conservative solver fit:

**memory ≈ 259 MiB + 1 492 MiB × (cells / 10⁶)**

With this estimator, the largest solver case under the cap is about **8.0 M cells** on 4 ranks.
The container fit gives 8.4 M.

## Consequences

- **ASM-001 is supported on the tutorial, not yet confirmed.** A single-passage fan mesh study up
  to about 8 M cells on the finest grid fits under the cap. ASM-001 is confirmed only when the
  first fan case (M3) is measured: snappyHexMesh meshes, cyclic patches and a polyhedral cell mix
  can cost more per cell than these hexahedra.
- **Cases above 1.05 M cells are extrapolations.** Above that, the estimator extrapolates beyond
  the measured range. The runner should record such estimates as extrapolated, and the M3 fan case
  should be measured at its own sizes, including one above 2 M cells.
- **Meshing is not the limit here.** blockMesh needs about 0.8 GiB per million cells, below the
  solver. snappyHexMesh has not been measured and may need more than the solver. It must be
  measured with the first fan case before RES-002 also covers meshing.
- **Speed.** About 0.8 s per SIMPLE iteration at 1 M cells on 4 ranks, roughly linear in cell count.
  This is useful for wall-time limits (SIM-002), but it is not a performance requirement.
- **Revisit when:**
  - the first fan case exists (M3);
  - OpenFOAM changes version (ADR-006);
  - the rank count changes;
  - the machine changes.

  Rerun `scripts/measure_openfoam_memory.sh` and update this ADR with the new numbers.

## Extrapolation policy (user decision, 2026-10-10)

No extra margin is applied to estimates beyond the measured range. The runner refuses a case only
when the plain estimate exceeds the 12 GiB cap. Any estimate above 1.05 M cells is logged as
extrapolated. The policy is to be revisited when the first fan case (M3) is measured at larger
sizes.

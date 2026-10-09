# ADR-006: Toolchain pins and OpenFOAM version

- Status: accepted (OpenFOAM version); pin values below are proposed until T01 builds them
- Date: 2026-10-09
- Requirements affected: REPRO-002, LIFE-001, PORT-001, CON-002

## Context
REPRO-002 requires every dependency pinned to an exact version. The architecture plan named OpenFOAM
v2606. On 2026-10-09 the official OpenCFD apt repository for Ubuntu 24.04 (`noble`) offers v2606 only as
`2606.0~rc2-1`, a release candidate. The `opencfd/openfoam-run:2606` Docker image was last updated
2026-06-23, before the 2026-06-26 release announcement, and would make OpenCFD's base OS the base of the
whole image. The v1 plan uses only simpleFoam with MRF, k-omega SST, snappyHexMesh, cyclic patches and the
rotorDisk source, all of which exist in v2512. v2512 is available as a final release (`2512.0-2`).

## Options considered
1. OpenFOAM v2606 from the `opencfd/openfoam-run:2606` image — final release intended, but image
   predates the announcement (may be an RC) and replaces our pinned Ubuntu base.
2. OpenFOAM v2606 from apt (`2606.0~rc2-1`) — breaks the pin-final-releases rule for no v1 benefit.
3. OpenFOAM v2512 from the official apt repository (`2512.0-2`) in our own Ubuntu 24.04 image.
4. Build v2606 from source — hours of compile time on a 16 GB machine for no v1 benefit.

## Decision
Use OpenFOAM v2512 (`2512.0-2`, official apt repository) in an image built from `ubuntu:24.04` pinned
by digest, with Ubuntu packages installed from one fixed Ubuntu snapshot timestamp.

## Pin rules
- Nothing published in the last 14 days at pin time (cutoff for the 2026-10-09 lookup: 2026-09-25).
- All pins live in one pins file read by the Dockerfile and checked by `scripts/versions.sh`.
- Gmsh is installed once (pip wheel 4.15.2) and reused by the Python project in T02.
- The OpenFOAM apt repository (dl.openfoam.com) is not covered by the Ubuntu snapshot service, so its
  packages are pinned by exact version and checked by SHA256.
- Each snapshot refresh is a deliberate change and is logged in `docs/toolchain.md`.

## Consequences
- Moving to v2606 is a later, deliberate change: new pins, then a rerun of the CFD benchmark case once one
  exists (M3) to confirm results have not shifted. Revisit when final v2606 packages are published for noble.
- The architecture plan's "OpenFOAM v2606" wording is read as v2512 (see its status note).
- OpenFOAM packages are outside the snapshot service, so reproducibility there relies on exact versions
  and hashes, and on the repository keeping old versions.

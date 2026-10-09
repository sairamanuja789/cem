# ADR-004: OpenCascade 8.0 geometry backend

- Status: proposed
- Date: 2026-10-10
- Requirements affected: GEO-001, GEO-002, GEO-004, REPRO-002, LIFE-002, PORT-001

## Context
T10 adds the geometry engine behind the `GeometryBackend` port (architecture section 2: OCCT 8.0
through its C++ API, isolated behind an interface). The build plan pre-approves OpenCascade 8.0.x and
asks for the vcpkg port if it offers 8.0.x, otherwise a source build of 8.0.1 in the container.

Facts, checked 2026-10-10 in the pinned container (`cemkit-dev`):
- The pinned vcpkg baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d` (tag 2026.07.29) has the
  port `opencascade` **8.0.0, port-version 1**. It downloads OCCT tag `V8_0_0_p1` (the 8.0.0 hot-fix
  release) from GitHub with SHA512 `f150f73a…eabf669`; a manual download of that tag gave the same
  SHA512.
- The stock port builds every module except Draw, with OpenGL and X11 on Linux, and FreeType by
  default. The container has no OpenGL or X11 development headers (`/usr/include/GL`, `/usr/include/X11`
  are absent), so the stock port cannot build there without changing the shared Docker image.
- OCCT's CMake lets a build switch modules off and name extra toolkits
  (`BUILD_MODULE_<name>=OFF`, `BUILD_ADDITIONAL_TOOLKITS`); it then builds those toolkits plus their
  dependency closure. TKDESTEP (STEP) depends on TKXCAF, which depends on TKV3d and TKService from the
  Visualization module; those two build without any windowing, font or GL library when `USE_XLIB`,
  `USE_FREETYPE`, `USE_OPENGL` and `USE_GLES2` are off.
- The vcpkg Linux triplet builds ports with vcpkg's own Linux toolchain, not the project's chainload
  file, so one OCCT binary serves the GCC and Clang presets (both on libstdc++ 13, ADR-007).
- OCCT's CMake adds `-DNo_Exception` to release builds: the `*_Raise_if` precondition macros are
  compiled out, while explicit `throw` statements (for example `StdFail_NotDone`) remain.

## Options considered
1. **Stock vcpkg port.** No overlay, but it fails in the container (no GL or X11 headers), builds
   the whole Visualization and ApplicationFramework modules cemkit never uses, and builds Debug and
   Release.
2. **Stock port plus GL/X11 headers in the Docker image.** Changes the image shared with other work
   and adds packages only to compile code cemkit never calls.
3. **OCCT 8.0.1 from source in the Dockerfile.** 8.0.1 is not in the pinned baseline; a Dockerfile
   build adds a second pinning mechanism beside vcpkg and rebuilds with every image change.
4. **vcpkg overlay of the pinned port with a reduced toolkit set (chosen).** Same source tag and
   SHA512, same five patches; only build options change.

## Decision
Use OCCT **8.0.0 (tag `V8_0_0_p1`)** from an overlay of the pinned vcpkg port in
`cmake/vcpkg-ports/opencascade/`, enabled by `vcpkg-configuration.overlay-ports` in `vcpkg.json`.

The overlay differs from the pinned port only in:
- toolkits: FoundationClasses and ModelingData modules, plus `TKPrim TKOffset TKBO TKBool TKMesh
  TKShHealing TKDESTEP TKDESTL` and their dependency closure (which includes TKXSBase, TKXCAF, TKCAF,
  TKLCAF, TKCDF, TKVCAF, TKV3d, TKService, TKHLR, TKGeomAlgo, TKTopAlgo); no Draw, no OpenGL/GLES
  toolkits, no IGES/VRML/glTF/OBJ/PLY writers;
- options: `USE_FREETYPE`, `USE_FREEIMAGE`, `USE_OPENGL`, `USE_GLES2`, `USE_XLIB`, `USE_RAPIDJSON`,
  `USE_TBB`, `USE_VTK`, `USE_TK` all OFF;
- dependencies: no `opengl` port, no default features, so **OCCT pulls in no other library**
  (only the host build helpers `vcpkg-cmake` and `vcpkg-cmake-config`);
- release build only (`VCPKG_BUILD_TYPE release`); debug presets link the release libraries.

The pin is exact: the overlay fixes the tag and SHA512, so a baseline bump does not change OCCT.
Changing OCCT means editing the overlay (and this ADR).

8.0.1 was not used: the build plan prefers the vcpkg port when it offers 8.0.x, and 8.0.0 is the
version the pinned baseline offers. Moving to 8.0.1 later means changing the tag and SHA512 in the
overlay, after checking that the five patches still apply.

## Backend conventions (kernel/cemkit/geometry)
- **Port** (`geometry/port/backend.hpp`): engine-neutral types and the `GeometryBackend` interface.
  Solids are opaque, owned by the backend that built them; a solid from another backend is rejected
  with `invalid_input`.
- **Model units:** OCCT model space is in millimetres, OCCT's own default (`xstep.cascade.unit`),
  so STEP files are written in millimetres and STL files (which carry no unit) are in the millimetres
  slicers assume. Parameters arrive as mp-units quantities and are read with
  `numerical_value_in(mm)` at the adapter boundary; results return as SI quantities. No global OCCT
  setting is changed.
- **Exceptions:** every OCCT call runs inside `guarded()` (`occt/guard.hpp`), which turns
  `Standard_Failure`, `std::exception` or anything else into `geometry_failed` with the exception
  type and message in the details. `guarded()` is `noexcept`.
- **Determinism (GEO-001):** Boolean operations, the self-intersection check and meshing run
  serially (`SetRunParallel(false)`, `InParallel = false`). The STEP header time stamp is replaced by
  the fixed marker `1970-01-01T00:00:00`, so the same solid always gives the same STEP bytes. STL is
  binary (no locale-dependent number text). STL meshing works on a copy, so exporting never changes
  the solid.
- **Validity (GEO-002):** `BRepCheck_Analyzer`; closed and manifold from the number of faces that use
  each edge (exactly two everywhere; seam edges count twice, degenerated edges are skipped);
  self-intersection from `BOPAlgo_ArgumentAnalyzer` in self-interference mode with OCCT's default
  fuzzy value. `build_test_solid` returns only solids that pass; otherwise `geometry_failed` naming
  the failed checks.
- **Errors:** invalid geometry parameters (non-finite, non-positive, too few sections, radii not
  increasing) and engine failures are `geometry_failed` with the field path as subject (GEO-002).
  A bad export request (no tessellation for STL, a non-positive tolerance, a missing directory) is
  `invalid_input`.
- **Exports (GEO-004):** `export_to_directory` writes `<sha256>.step` or `<sha256>.stl` into a
  caller-given directory, through a `.partial` file renamed into place. The sha256 is computed in
  `cemkit/core/sha256.hpp` (FIPS 180-4; no new dependency) and matches Python's `hashlib.sha256`, so
  the store (STORE-003) can verify it.
- **Tessellation tolerances** for STL have no default: the caller chooses them. The values in the
  tests are arbitrary test values, as are all test-solid dimensions.
- Mass properties are geometry, not a physics prediction, and carry no fidelity label.

## Build cost
Measured on this machine (16 GB, shared), `VCPKG_MAX_CONCURRENCY=2`: about 28 minutes for the
OCCT port (vcpkg logs: configure started 02:36:49, install finished 03:04:47), giving static
libraries; the binary cache entry is about 61 MB in total for all ports.
vcpkg's post-build check warns "mismatching number of debug and release binaries"; this is the
intended effect of `VCPKG_BUILD_TYPE release` in the overlay and is not suppressed (suppressing it
would change the port's ABI hash and force a rebuild).
The binary cache (`.cache/vcpkg/binary`, keyed in CI on the overlay files too) makes later configures
take seconds.

## Consequences
- The container image is unchanged; CI needs no new system packages.
- CI's first run after this change builds OCCT once (cache miss); the CI job timeout is raised to
  allow it. Later runs restore it from the cache.
- cemkit cannot display geometry through OCCT (no Visualization module); it never needed to.
- Release-only OCCT has no debug symbols; stepping into OCCT needs a local debug build.
- The overlay must be re-checked when the vcpkg baseline moves (REPRO-002), even though it pins OCCT
  independently.
- Human check still open: the exported STEP and STL open in FreeCAD (build plan T10).
- Open (HI-009): under clang-asan, UBSan's alignment check fires in OCCT header code
  (`NCollection_TListNode<int>::delNode`) on nodes from `NCollection_IncAllocator`, which
  bump-allocates without alignment, during STL meshing. The sanitizer configuration is not changed
  until the owner decides.

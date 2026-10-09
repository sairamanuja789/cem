#!/usr/bin/env bash
# T03 (ASM-001, RES-001, RES-002): measure OpenFOAM memory per million cells on this machine.
#
# Case: the OpenFOAM v2512 tutorial incompressible/simpleFoam/rotatingCylinders (MRF), made 3D.
# It gets N cells in z, its front and back patches change from empty to symmetry, and its
# turbulence model changes from laminar to k-omega SST, the planned fan setup (approved
# 2026-10-09). Only the memory of a 3D hex MRF + SST case matters here, not the physics.
#
# Cells = 4 blocks x (4n) x (2n) x n = 32 n^3. The default sizes n = 20, 25, 32 give 256 000,
# 500 000 and 1 048 576 cells.
#
# Memory is measured two ways, both upper bounds:
#   per rank  : each MPI rank runs under /usr/bin/time -v, and the four peak RSS values are summed
#               (the ranks need not peak at the same moment);
#   container : each solver run starts in a fresh container, and the cgroup v2 memory.peak is read
#               at the end. It includes MPI helpers and page cache.
# Meshing (blockMesh, decomposePar) runs in its own container and is reported separately, because
# the solver is the step that sets the job memory cap.
#
# Run on the host, on mains power, with nothing else heavy running:
#   scripts/measure_openfoam_memory.sh [--sizes "20 25 32"] [--iterations 100] [--ranks 4]
#                                      [--out build/t03/<utc-time>] [--allow-battery]
# Writes <out>/results.csv and <out>/summary.md. Their numbers go into docs/adr/000-hardware-budget.md.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
self="scripts/measure_openfoam_memory.sh"

# ------------------------------------------------------------------------------------------------
# In-container phases
# ------------------------------------------------------------------------------------------------

load_openfoam() {
  local bashrc
  bashrc="$(ls /usr/lib/openfoam/openfoam*/etc/bashrc | head -1)"
  set +eu
  # shellcheck disable=SC1090
  . "$bashrc"
  set -eu
}

header() { # class object
  printf 'FoamFile\n{\n    version 2.0;\n    format ascii;\n    class %s;\n    object %s;\n}\n\n' "$1" "$2"
}

patch_or_die() { # file sed-expression expected-grep
  sed -i "$2" "$1"
  grep -q -- "$3" "$1" || { echo "measure: failed to patch $1 ($3)" >&2; exit 1; }
}

# The k-omega SST initial values below are starting values for this memory benchmark only; they are
# not engineering data and do not affect memory use.
write_turbulence_fields() {
  local f
  for f in k omega nut; do
    local dim value wall
    case "$f" in
      k)     dim="[0 2 -2 0 0 0 0]"; value=1e-3; wall="kqRWallFunction" ;;
      omega) dim="[0 0 -1 0 0 0 0]"; value=1;    wall="omegaWallFunction" ;;
      nut)   dim="[0 2 -1 0 0 0 0]"; value=0;    wall="nutkWallFunction" ;;
    esac
    {
      header volScalarField "$f"
      printf 'dimensions %s;\ninternalField uniform %s;\nboundaryField\n{\n' "$dim" "$value"
      printf '    "innerWall|outerWall" { type %s; value uniform %s; }\n' "$wall" "$value"
      printf '    frontAndBack { type symmetry; }\n}\n'
    } > "0/$f"
  done
}

prepare_case() { # dir n iterations ranks
  local dir="$1" n="$2" iterations="$3" ranks="$4"
  local tutorial="$FOAM_TUTORIALS/incompressible/simpleFoam/rotatingCylinders"
  rm -rf "$dir"
  cp -r "$tutorial" "$dir"
  cd "$dir"

  # Geometry: 3D, n-scaled resolution, symmetry front and back.
  patch_or_die system/blockMeshDict "s/^\(\s*nr\s\+\)40;/\1$((2 * n));/" "nr *$((2 * n));"
  patch_or_die system/blockMeshDict "s/^\(\s*ntheta\s\+\)40;/\1$((4 * n));/" "ntheta *$((4 * n));"
  patch_or_die system/blockMeshDict 's#(\$/geom/ntheta \$/geom/nr 1)#($/geom/ntheta $/geom/nr '"$n"')#' \
    "nr $n)"
  patch_or_die system/blockMeshDict 's/type\s\+empty;/type    symmetry;/' "type    symmetry;"
  for f in 0/U 0/p; do
    patch_or_die "$f" '/frontAndBack/,/}/ s/type\s\+empty;/type            symmetry;/' "symmetry;"
  done

  # Turbulence: k-omega SST, the planned fan setup.
  { header dictionary turbulenceProperties
    printf 'simulationType RAS;\nRAS\n{\n    RASModel kOmegaSST;\n    turbulence on;\n    printCoeffs on;\n}\n'
  } > constant/turbulenceProperties
  write_turbulence_fields

  # Schemes and solvers: the tutorial's, plus k and omega.
  { header dictionary fvSchemes
    cat <<'EOF'
ddtSchemes { default steadyState; }
gradSchemes { default Gauss linear; }
divSchemes
{
    default none;
    div(phi,U) bounded Gauss linearUpwind grad(U);
    div(phi,k) bounded Gauss upwind;
    div(phi,omega) bounded Gauss upwind;
    div((nuEff*dev2(T(grad(U))))) Gauss linear;
}
laplacianSchemes { default Gauss linear corrected; }
interpolationSchemes { default linear; }
snGradSchemes { default corrected; }
wallDist { method meshWave; }
EOF
  } > system/fvSchemes
  { header dictionary fvSolution
    cat <<'EOF'
solvers
{
    p { solver GAMG; tolerance 1e-10; relTol 0.1; smoother GaussSeidel; }
    "(U|k|omega)" { solver smoothSolver; smoother GaussSeidel; tolerance 1e-10; relTol 0.1; }
}
SIMPLE { nNonOrthogonalCorrectors 0; pRefCell 0; pRefValue 0; }
relaxationFactors
{
    fields { p 0.3; }
    equations { U 0.7; "(k|omega)" 0.7; }
}
EOF
  } > system/fvSolution

  # Fixed iteration count. Binary output, and no field writes during the run, so that page cache
  # inflates the container peak as little as possible.
  { header dictionary controlDict
    printf 'application simpleFoam;\nstartFrom startTime;\nstartTime 0;\nstopAt endTime;\n'
    printf 'endTime %s;\ndeltaT 1;\nwriteControl timeStep;\nwriteInterval 100000000;\n' "$iterations"
    printf 'writeFormat binary;\nwritePrecision 6;\nwriteCompression off;\ntimeFormat general;\n'
    printf 'timePrecision 6;\nrunTimeModifiable false;\n'
  } > system/controlDict
  { header dictionary decomposeParDict
    printf 'numberOfSubdomains %s;\nmethod scotch;\n' "$ranks"
  } > system/decomposeParDict
}

timed() { # label command... : runs under /usr/bin/time -v, log in log.<label>, times in time.<label>
  local label="$1"; shift
  /usr/bin/time -v -o "time.$label" "$@" > "log.$label" 2>&1
}

phase_mesh() { # dir n iterations ranks
  load_openfoam
  prepare_case "$@"
  timed blockMesh blockMesh
  timed decomposePar decomposePar -force
  grep -m1 -E '^\s*nCells:' log.blockMesh | awk '{print $2}' > cells
}

phase_solve() { # dir ranks
  load_openfoam
  cd "$1"
  local ranks="$2"
  # Docker's default seccomp profile blocks the cross-memory-attach copy OpenMPI's shared-memory
  # transport tries first; disable it rather than fail.
  export OMPI_MCA_btl_vader_single_copy_mechanism=none
  local start end
  start="$(date +%s.%N)"
  mpirun -np "$ranks" bash -c 'exec /usr/bin/time -v -o "time.rank-${OMPI_COMM_WORLD_RANK}" simpleFoam -parallel' \
    > log.simpleFoam 2>&1
  end="$(date +%s.%N)"
  echo "$start $end" | awk '{printf "%.2f\n", $2 - $1}' > solver_wall_s
  cat /sys/fs/cgroup/memory.peak > solver_cgroup_peak_bytes
  grep -q '^End' log.simpleFoam || { echo "measure: simpleFoam did not finish; see $1/log.simpleFoam" >&2; exit 1; }
}

case "${1:-}" in
  --phase-mesh) shift; phase_mesh "$@"; exit 0 ;;
  --phase-solve) shift; phase_solve "$@"; exit 0 ;;
esac

# ------------------------------------------------------------------------------------------------
# Host driver
# ------------------------------------------------------------------------------------------------

sizes="20 25 32"
iterations=100
ranks=4
out="build/t03/$(date -u +%Y%m%dT%H%M%SZ)"
allow_battery=0
while [ $# -gt 0 ]; do
  case "$1" in
    --sizes) sizes="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --ranks) ranks="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --allow-battery) allow_battery=1; shift ;;
    *) echo "usage: $self [--sizes \"20 25 32\"] [--iterations N] [--ranks N] [--out DIR] [--allow-battery]" >&2
       exit 2 ;;
  esac
done

mains="unknown"
for supply in /sys/class/power_supply/*; do
  if [ "$(cat "$supply/type" 2>/dev/null)" = "Mains" ]; then
    mains="$( [ "$(cat "$supply/online" 2>/dev/null)" = 1 ] && echo yes || echo no)"
  fi
done
if [ "$mains" != "yes" ] && [ "$allow_battery" -ne 1 ]; then
  echo "measure: not on mains power (mains=$mains). Plug in, or pass --allow-battery for a smoke run;" >&2
  echo "         battery runs are not valid for docs/adr/000-hardware-budget.md." >&2
  exit 2
fi

mkdir -p "$root/$out"
image="cemkit-dev:latest"
run_container() { # command...
  docker run --rm --user "$(id -u):$(id -g)" --shm-size=1g \
    -v "$root":/workspace -w /workspace -e HOME=/home/dev "$image" bash "$self" "$@"
}

{
  echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git_commit: $(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
  echo "logical_cpus: $(nproc)"
  echo "mem_total_kib: $(awk '/MemTotal/{print $2}' /proc/meminfo)"
  echo "kernel: $(uname -r)"
  echo "mains_power: $mains"
  echo "image: $image $(docker image inspect --format '{{.Id}}' "$image" 2>/dev/null)"
  echo "sizes_n: $sizes"
  echo "iterations: $iterations"
  echo "ranks: $ranks"
} > "$root/$out/machine.txt"

maxrss_kib() { awk -F': ' '/Maximum resident set size/{print $2}' "$1"; }
wall() { awk -F': ' '/Elapsed \(wall clock\)/{print $2}' "$1"; }

csv="$root/$out/results.csv"
echo "n,cells,blockMesh_maxrss_kib,decomposePar_maxrss_kib,ranks,rank_maxrss_sum_kib,rank_maxrss_max_kib,solver_cgroup_peak_kib,solver_wall_s,iterations" > "$csv"
for n in $sizes; do
  case_dir="$out/case-n$n"
  echo "measure: n=$n (about $((32 * n * n * n)) cells): meshing"
  run_container --phase-mesh "$case_dir" "$n" "$iterations" "$ranks"
  echo "measure: n=$n: solving on $ranks ranks, $iterations iterations"
  run_container --phase-solve "$case_dir" "$ranks"
  d="$root/$case_dir"
  sum=0; max=0
  for r in $(seq 0 $((ranks - 1))); do
    v="$(maxrss_kib "$d/time.rank-$r")"
    sum=$((sum + v)); [ "$v" -gt "$max" ] && max="$v"
  done
  echo "$n,$(cat "$d/cells"),$(maxrss_kib "$d/time.blockMesh"),$(maxrss_kib "$d/time.decomposePar"),$ranks,$sum,$max,$(( $(cat "$d/solver_cgroup_peak_bytes") / 1024 )),$(cat "$d/solver_wall_s"),$iterations" >> "$csv"
done

# Summary: per-size MiB per million cells, and a least-squares line memory = a + b * cells over
# the sizes (b is the marginal cost per million cells, a the fixed overhead).
awk -F, -v machine="$root/$out/machine.txt" '
  NR == 1 { next }
  { n++; c[n] = $2; r[n] = $6 / 1024; g[n] = $8 / 1024; w[n] = $9; it = $10
    bm[n] = $3 / 1024; dp[n] = $4 / 1024 }
  function fit(y, label,   i, sx, sy, sxx, sxy, b, a) {
    sx = sy = sxx = sxy = 0
    for (i = 1; i <= n; i++) { x = c[i] / 1e6; sx += x; sy += y[i]; sxx += x * x; sxy += x * y[i] }
    if (n < 2 || n * sxx - sx * sx == 0) { printf "| %s | n/a | n/a |\n", label; return }
    b = (n * sxy - sx * sy) / (n * sxx - sx * sx); a = (sy - b * sx) / n
    printf "| %s | %.0f | %.0f |\n", label, b, a
  }
  END {
    print "# OpenFOAM memory measurement (T03)\n"
    while ((getline line < machine) > 0) print "- " line
    print "\n| cells | solver, sum of rank peaks (MiB) | solver, container peak (MiB) | per M cells, rank sum (MiB) | per M cells, container (MiB) | blockMesh (MiB) | decomposePar (MiB) | solver wall (s) | s per iteration |"
    print "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
    for (i = 1; i <= n; i++)
      printf "| %d | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.1f | %.3f |\n", c[i], r[i], g[i], r[i] / (c[i] / 1e6), g[i] / (c[i] / 1e6), bm[i], dp[i], w[i], w[i] / it
    print "\nLeast-squares fit, memory = a + b x (cells / 1e6):\n"
    print "| measure | b: MiB per million cells | a: fixed MiB |"
    print "| --- | ---: | ---: |"
    fit(r, "sum of rank peaks"); fit(g, "container peak")
  }' "$csv" > "$root/$out/summary.md"

echo "measure: done. Results in $out/results.csv and $out/summary.md"
cat "$root/$out/summary.md"

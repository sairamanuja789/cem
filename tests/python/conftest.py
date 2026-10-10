"""Shared pytest setup.

The dev venv does not install cemkit (ADR-013: `tool.uv.package = false`); pytest finds it through
`pythonpath = ["python"]`, which only affects this process. Tests that start child Python processes
(the T13 orchestration fakes and workers) need cemkit too, so the session exports PYTHONPATH for
them, as scripts/cemkit does for the CLI.
"""

import os
import stat
from pathlib import Path

import pytest

PYTHON_ROOT = str(Path(__file__).resolve().parents[2] / "python")
_existing = os.environ.get("PYTHONPATH", "")
if PYTHON_ROOT not in _existing.split(os.pathsep):
    os.environ["PYTHONPATH"] = PYTHON_ROOT + (os.pathsep + _existing if _existing else "")


# A stand-in for "openfoam2512 -c <command>" (CI has no OpenFOAM): checkMesh says OK and the solver
# writes flat synthetic monitors of the rotor-sector template (tests of the M5/M6 plumbing only).
FAKE = r"""#!/bin/sh
# Stand-in for "openfoam2512 -c <command>": checkMesh says OK, the solver writes monitors.
case "$2" in
  checkMesh*)
    # Cells from the blockMesh blocks "(nr nt nz)", so each mesh level has its own count.
    n=$(grep -o "([0-9]* [0-9]* [0-9]*) simpleGrading" system/blockMeshDict 2>/dev/null \
      | awk '{gsub(/[()]/,""); s += $1*$2*$3} END {print (s ? s : 1000)}')
    echo "    cells:            $n"; echo "Mesh OK." ;;
  simpleFoam*|mpirun*)
    for f in inletFlow outletFlow pIn pOut ptIn ptOut; do mkdir -p postProcessing/$f/0; done
    mkdir -p postProcessing/forces/0 postProcessing/residuals/0
    printf '# Time v\n' > postProcessing/inletFlow/0/surfaceFieldValue.dat
    printf '# Time v\n' > postProcessing/outletFlow/0/surfaceFieldValue.dat
    printf '# Time v\n' > postProcessing/pIn/0/surfaceFieldValue.dat
    printf '# Time v\n' > postProcessing/pOut/0/surfaceFieldValue.dat
    printf '# Time v\n' > postProcessing/ptIn/0/surfaceFieldValue.dat
    printf '# Time v\n' > postProcessing/ptOut/0/surfaceFieldValue.dat
    printf '# Time tx ty tz\n' > postProcessing/forces/0/moment.dat
    r=postProcessing/residuals/0/solverInfo.dat
    printf '# Time Ux_initial Uy_initial Uz_initial' > $r
    printf ' p_initial k_initial omega_initial\n' >> $r
    i=1
    while [ $i -le 50 ]; do
      echo "$i -0.004" >> postProcessing/inletFlow/0/surfaceFieldValue.dat
      echo "$i 0.004" >> postProcessing/outletFlow/0/surfaceFieldValue.dat
      echo "$i 0.4" >> postProcessing/pIn/0/surfaceFieldValue.dat
      echo "$i 5.0" >> postProcessing/pOut/0/surfaceFieldValue.dat
      echo "$i 4.2" >> postProcessing/ptIn/0/surfaceFieldValue.dat
      echo "$i 5.2" >> postProcessing/ptOut/0/surfaceFieldValue.dat
      echo "$i 0 0 0.000175" >> postProcessing/forces/0/moment.dat
      echo "$i 1e-9 1e-9 1e-9 1e-9 1e-9 1e-9" >> $r
      i=$((i+1))
    done ;;
esac
exit 0
"""


@pytest.fixture
def fake_openfoam(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from cemkit.simulation.runners import openfoam

    wrapper = tmp_path / "fake-openfoam"
    wrapper.write_text(FAKE, encoding="utf-8")
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IEXEC)
    monkeypatch.setenv(openfoam.WRAPPER_ENV, str(wrapper))

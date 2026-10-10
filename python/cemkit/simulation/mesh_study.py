"""Three-level mesh study: grid convergence index (TRUST-002).

Procedure of Celik, I. B., Ghia, U., Roache, P. J., Freitas, C. J., Coleman, H. and Raad, P. E.,
"Procedure for Estimation and Reporting of Uncertainty Due to Discretization in CFD Applications",
ASME Journal of Fluids Engineering 130(7), 078001, 2008 (cited in docs/architecture.md), steps 1-5:

  h_i = (V / N_i)^(1/3) (3D representative cell size), fine 1, medium 2, coarse 3;
  r21 = h2 / h1, r32 = h3 / h2; eps21 = phi2 - phi1, eps32 = phi3 - phi2; s = sign(eps32 / eps21);
  p = |ln|eps32 / eps21| + q(p)| / ln r21, q(p) = ln((r21^p - s) / (r32^p - s)), by fixed-point
  iteration from q = 0;
  phi_ext21 = (r21^p phi1 - phi2) / (r21^p - 1);
  e_a21 = |(phi1 - phi2) / phi1|, e_ext21 = |(phi_ext21 - phi1) / phi_ext21|;
  GCI_fine21 = 1.25 e_a21 / (r21^p - 1) (Celik et al.: safety factor 1.25 for three grids).

The study is applicable only for monotonic convergence (s > 0); oscillatory (s < 0) or
non-converging cases are reported, never given a GCI. The acceptance limit is TRUST-002's
(start at 5%, proposed) and is passed in, not chosen here.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Literal

SAFETY_FACTOR = 1.25  # Celik et al. 2008, three-grid studies
MAX_ITERATIONS = 200  # fixed-point iterations for p (numerical setting, not a physical value)


@dataclass(frozen=True)
class MeshStudy:
    convergence: Literal["monotonic", "oscillatory", "divergent", "converged"]
    cell_sizes: tuple[float, float, float]  # h1 (fine), h2, h3 (m)
    refinement_ratios: tuple[float, float]  # r21, r32
    order: float | None  # apparent order p
    extrapolated: float | None  # phi_ext21
    approximate_error: float  # e_a21
    extrapolated_error: float | None  # e_ext21
    gci_fine: float | None  # GCI_fine21 (fraction, not percent)


def cell_size(volume: float, cells: int, dimensions: int = 3) -> float:
    """Representative cell size h = (V / N)^(1/d) of a d-dimensional mesh (V: volume or area)."""
    if not (volume > 0.0 and cells > 0 and dimensions in (2, 3)):
        raise ValueError("volume and cell count must be positive and dimensions 2 or 3")
    return float((volume / cells) ** (1.0 / dimensions))


def mesh_study(sizes: tuple[float, float, float], values: tuple[float, float, float]) -> MeshStudy:
    """GCI of one quantity from the fine, medium and coarse meshes (cell sizes h and values in
    that order)."""
    h1, h2, h3 = sizes
    if not (h1 < h2 < h3):
        raise ValueError("the meshes must be ordered fine, medium, coarse with distinct sizes")
    phi1, phi2, phi3 = values
    if not all(math.isfinite(v) for v in values) or phi1 == 0.0:
        raise ValueError("the values must be finite and the fine-mesh value non-zero")
    r21, r32 = h2 / h1, h3 / h2
    eps21, eps32 = phi2 - phi1, phi3 - phi2
    e_a21 = abs((phi1 - phi2) / phi1)
    sizes, ratios = (h1, h2, h3), (r21, r32)
    if eps21 == 0.0 and eps32 == 0.0:
        return MeshStudy("converged", sizes, ratios, None, phi1, 0.0, 0.0, 0.0)
    if eps21 == 0.0 or eps32 == 0.0:
        return MeshStudy("divergent", sizes, ratios, None, None, e_a21, None, None)
    ratio = eps32 / eps21
    if ratio < 0.0:
        return MeshStudy("oscillatory", sizes, ratios, None, None, e_a21, None, None)
    # Celik et al. take |ln|eps32/eps21| + q|; without the outer abs the sign shows whether the
    # error shrinks on refinement (p > 0) for any pair of ratios r21, r32.
    s = 1.0
    q = 0.0
    p = 0.0
    for _ in range(MAX_ITERATIONS):
        p_new = (math.log(ratio) + q) / math.log(r21)
        if not (math.isfinite(p_new) and p_new > 0.0):
            return MeshStudy("divergent", sizes, ratios, None, None, e_a21, None, None)
        q = math.log(((r21**p_new) - s) / ((r32**p_new) - s))
        if abs(p_new - p) <= 1e-12 * max(1.0, abs(p_new)):
            p = p_new
            break
        p = p_new
    else:
        return MeshStudy("divergent", sizes, ratios, None, None, e_a21, None, None)
    factor = (r21**p) - 1.0
    phi_ext = ((r21**p) * phi1 - phi2) / factor
    e_ext = abs((phi_ext - phi1) / phi_ext) if phi_ext != 0.0 else None
    gci = SAFETY_FACTOR * e_a21 / factor
    return MeshStudy("monotonic", sizes, ratios, p, phi_ext, e_a21, e_ext, gci)

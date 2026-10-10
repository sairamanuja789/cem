"""TRUST-002: the GCI procedure of Celik et al. (2008) reproduces the paper's worked example."""

from __future__ import annotations

import pytest

from cemkit.simulation.mesh_study import cell_size, mesh_study


@pytest.mark.req("TRUST-002")
def test_celik_2008_table_1_reattachment_length() -> None:
    # Celik et al., J. Fluids Eng. 130(7) 078001, 2008, Table 1 (2D, N = 18000, 8000, 4500):
    # p = 1.53, phi_ext21 = 6.1685, e_a21 = 1.5%, e_ext21 = 1.7%, GCI_fine21 = 2.2%.
    sizes = (cell_size(1.0, 18000, 2), cell_size(1.0, 8000, 2), cell_size(1.0, 4500, 2))
    study = mesh_study(sizes, (6.063, 5.972, 5.863))
    assert study.convergence == "monotonic"
    assert study.refinement_ratios == pytest.approx((1.5, 4.0 / 3.0))
    assert study.order == pytest.approx(1.53, abs=0.005)
    assert study.extrapolated == pytest.approx(6.1685, abs=5e-5)
    assert study.approximate_error == pytest.approx(0.015, abs=5e-4)
    assert study.extrapolated_error == pytest.approx(0.017, abs=5e-4)
    assert study.gci_fine == pytest.approx(0.022, abs=5e-4)


@pytest.mark.req("TRUST-002")
def test_oscillatory_and_divergent_studies_get_no_gci() -> None:
    sizes = (1.0, 1.5, 2.25)
    assert mesh_study(sizes, (1.0, 1.1, 1.0)).gci_fine is None  # oscillatory
    assert mesh_study(sizes, (1.0, 1.1, 1.15)).convergence == "divergent"  # error grows
    assert mesh_study(sizes, (2.0, 2.0, 2.0)).gci_fine == 0.0


@pytest.mark.req("TRUST-002")
def test_a_study_needs_three_ordered_meshes() -> None:
    with pytest.raises(ValueError):
        mesh_study((1.0, 1.0, 2.0), (1.0, 2.0, 3.0))
    with pytest.raises(ValueError):
        cell_size(1.0, 0)

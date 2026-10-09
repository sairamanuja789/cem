# Fidelity labels, uncertainty, provenance and model identity

Code: `kernel/cemkit/core/fidelity.hpp`, `result.hpp` (`Labelled`), `provenance.hpp`, `version.hpp`.
Requirements: COR-003, PHY-004, REP-001, requirements section 2.

## Fidelity labels

| Name (stored) | Report label | Claim (requirements section 2) | Uncertainty |
| --- | --- | --- | --- |
| `l0_predicted` | L0 predicted | Order-of-magnitude plausibility | none |
| `l1_predicted` | L1 predicted | Plausible within the model's stated validity range | none |
| `l2_simulated` | L2 simulated | CFD or FEA that passed the trust gate | none |
| `l2_verified` | L2 verified | L2 simulated plus a passing mesh study | required |
| `l3_validated` | L3 validated | Rig measurement with uncertainty | required |

## Labelled values

A reported quantity is a `Labelled<Q>`: the quantity, its fidelity label and the identity of the
model that produced it. The label is part of the type:

- there is no default constructor and no constructor without a label;
- there is one factory per level; `l2_verified` and `l3_validated` take an uncertainty argument;
- there is no arithmetic. To compute with a value, unwrap it with `value()` and label the result
  with the model that did the computation.

Every factory returns `Result<Labelled<Q>>` and rejects with `invalid_input`:

- an empty model name;
- a non-finite value;
- a non-finite or negative uncertainty.

## Uncertainty convention

The uncertainty is one number: a **half-width in the value's units**. The label implies the basis,
so no separate basis field is stored.

- **L2 verified**: the fine-grid discretisation uncertainty from the grid convergence index (GCI)
  procedure of Celik, I. B., Ghia, U., Roache, P. J., Freitas, C. J., Coleman, H. and Raad, P. E.
  (2008), "Procedure for Estimation and Reporting of Uncertainty Due to Discretization in CFD
  Applications", *Journal of Fluids Engineering* 130(7), 078001. Celik et al. report the fine-grid
  GCI as a relative value. It is stored here as an absolute half-width: GCI_fine × |fine-grid value|.
- **L3 validated**: the expanded measurement uncertainty U = k·u_c with coverage factor k = 2,
  corresponding to a coverage probability of about 95 %, per JCGM 100:2008 (GUM), *Evaluation of
  measurement data — Guide to the expression of uncertainty in measurement*.

Caveats for the implementations that produce these numbers:

- The relative GCI is undefined when the fine-grid value is close to zero, and the Celik et al.
  procedure does not apply to oscillatory convergence. In those cases the M3 implementation must
  return an error, not a number.
- The GCI band (safety factor 1.25 in Celik et al.) is not a GUM expanded uncertainty. L2 verified
  and L3 validated half-widths are therefore not directly comparable, and reports must not combine
  them as if they were.
- `Labelled` currently accepts any finite, non-negative half-width, including zero. It does not yet
  check that a mesh study (L2 verified) or a rig record (L3 validated) exists; that evidence link is
  planned for M3 and the rig work (TRUST-004, REP-004).

The arithmetic that produces these numbers (the mesh study, the rig uncertainty budget) belongs to
later tasks (M3 and the rig work). T04 only fixes what the stored number means.

## Model identity (PHY-004)

`ModelId` = name + semantic version (`MAJOR.MINOR.PATCH`, semver.org 2.0.0 core form, no pre-release
or build suffix). Changing an equation bumps the version. `parse_semver` is strict: it rejects
leading zeros, extra or missing components and values over 32 bits. Model identities compare by name
and then by version.

The kernel's own version comes from the CMake project version. No commit hash or build time is
compiled in, so identical sources give identical binaries.

## Provenance classes

Every spec field carries one class, stored by name: `user`, `image`, `derived`, `default`
(`Provenance::default_value` in C++) or `unknown`. Precedence between the classes (requirements
section 2) is applied by the spec compiler, not by this type.

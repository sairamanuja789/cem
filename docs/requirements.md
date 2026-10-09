# Fan CEM — Requirements Specification v1

Oct 9, 2026 · @Akshit

## 1. Purpose, scope and how to read this spec

This document states what the Fan CEM must do, how each requirement is to be interpreted, and how it will be verified. It is the contract for the first build (the CEM base) and for the first product (a 120 mm ducted axial fan); later fan families add their own requirement sections without changing the base.

**In scope:** a Computational Engineering Model that turns a reference image plus stated requirements into a fan design, with physics-based sizing, parametric geometry, simulation, optimization, validation evidence and a traceable design package.

**Out of scope for v1:** motors and electronics, bearings, acoustic prediction in dB, transient CFD, cross-flow and bladeless fans, cloud deployment, multi-user operation.

**Requirement wording** (following the usual convention in requirements engineering, e.g. ISO/IEC/IEEE 29148)

| Word | Meaning |
| --- | --- |
| shall | Mandatory. The release fails if it is not met |
| should | Expected. Deviation needs a written reason in the decision log |
| may | Optional. Allowed, not required |

**Requirement identifiers** take the form AREA-NNN (for example SPEC-004). An identifier is never reused, even if the requirement is deleted. Each requirement carries a priority: **M** (must, v1 base), **P** (product v1), **L** (later release).

**Verification methods**

| Code | Method | Example |
| --- | --- | --- |
| T | Automated test | Unit, property, regression or integration test in CI |
| A | Analysis | Hand calculation, mesh study, uncertainty analysis |
| I | Inspection | Code review, document review, schema check |
| D | Demonstration | Running the CLI end to end in front of a reviewer |

Every requirement maps to at least one test or check before its module is called done.

## 2. Definitions and interpretation rules

These definitions are binding: when any requirement uses one of these words, it means exactly this. Ambiguity found later is resolved by amending this section, not by reinterpreting code.

**Core terms**

| Term | Definition |
| --- | --- |
| CEM | Computational Engineering Model: software that encodes engineering knowledge and produces designs whose performance is computed, not drawn |
| Specification (spec) | A versioned, structured record of requirements, each field with value, unit, tolerance and provenance |
| Fan family | A class of fans sharing one design method: ducted axial, unducted axial, ceiling, centrifugal, mixed-flow, cross-flow, bladeless |
| Family plugin | The module that implements one family's parameters, sizing rules, L1 model, geometry recipe and simulation case |
| Candidate | One parameter vector for one family plugin version, evaluated against one spec |
| Duty point | The flow rate and pressure rise the fan must deliver at the stated speed and air conditions |
| Campaign | One run of the design loop for one spec, with its own budget, seed and records |
| Artifact | Any file the system produces (STEP, 3MF, mesh, solver case, log, report), named by the hash of its content |

**Fidelity levels.** Every reported number shall carry exactly one of these labels.

| Label | Produced by | Meaning |
| --- | --- | --- |
| L0 predicted | Similarity laws, feasibility rules | Order-of-magnitude plausibility |
| L1 predicted | Family analytical model | Plausible within the model's stated validity range |
| L2 simulated | CFD or FEA that passed the trust gate | The equations were solved correctly for this geometry |
| L2 verified | L2 simulated plus a passing mesh study | Numerical error is bounded and stated |
| L3 validated | Rig measurement with uncertainty | Matches physical reality within stated uncertainty |

A number may only be described with the words "validated" or "measured" if it has an L3 record.

**Provenance classes.** Every spec field shall carry one.

| Class | Source | Precedence |
| --- | --- | --- |
| user | Stated by the user in text or a form | Highest |
| image | Inferred from the reference image by the vision model, with confidence | Overridden by user |
| derived | Computed from other fields by a documented rule | Recomputed when inputs change |
| default | Taken from the documented defaults table | Overridden by user and image |
| unknown | No value | Must be resolved or explicitly allowed before a campaign starts |

**Physical conventions**

- All internal quantities are SI: m, kg, s, Pa, W, rad/s, K. Inputs in other units (mm, rpm, CFM, inH₂O, m³/min) are converted at the boundary and the original is recorded.
- Pressure rise shall always state its type, using the ISO 5801 fan-pressure definitions: fan total pressure (p\_t2 − p\_t1) or fan static pressure (p\_s2 − p\_t1, which is total-to-static by construction). Static-to-static pressure rise (p\_s2 − p\_s1) is rejected as an input. A pressure value without a type is invalid.
- Efficiency shall always state its type: total, total-to-static, or overall (including motor). A bare "efficiency" is invalid in reports.
- Default air: density 1.18 kg/m³ and temperature 298.15 K, recorded as provenance "default".
- Speed is stored as angular velocity in rad/s; rpm is a display unit only.

**Interpretation rules for the requirements themselves**

1. A requirement applies to every fan family unless it names a family.
2. A numeric limit includes its boundary ("at most 5%" means ≤ 5%).
3. A limit marked "proposed" is a starting value that may be changed only by a logged decision, never silently in code.
4. Where a requirement conflicts with physics (for example, a duty point no family can reach), the system shall report the conflict rather than satisfy the requirement approximately.

## 3. Implementation language decision (ADR-001)

**Decision: a C++23 engineering kernel (limited to features GCC 13 and Clang 20 both implement) with a Python orchestration layer, with solvers as separate processes.** This is how production CAE systems are built: none of them uses a single language. The compute kernel is compiled, and engineers drive it from a higher-level language.

**How production systems are actually layered**

| System | Compute kernel | Engineering / automation layer |
| --- | --- | --- |
| [LEAP 71 Noyron / PicoGK](https://github.com/leap71/PicoGK/blob/main/Documentation/README.md) | C++ runtime built on OpenVDB | C#; PicoGK's C# code calls the C++ runtime for the heavy lifting |
| [Ansys (PyAnsys)](https://mapdl.docs.pyansys.com/version/stable/getting_started/project.html) | Compiled solvers (MAPDL, Fluent) | Python, client–server over gRPC; PyMAPDL replaced an older CORBA interface |
| [Open CASCADE 8.0](https://dev.opencascade.org/content/open-cascade-technology-800-release) | C++17 baseline | Python through community bindings (OCP, build123d) |
| OpenFOAM | C++ | Case dictionaries and scripts |
| [Gmsh](https://audilab.bme.mcgill.ca/sw/gmsh.html) | C++ | APIs for C, C++, Python, Julia and Fortran |

So you are right that the heavy machinery is C++, but every major vendor also ships a high-level layer, and Ansys has chosen Python for it. The production pattern is two layers, not one.

**Languages compared for the CEM kernel**

| Language | Strengths here | Weaknesses here | Support horizon | Verdict |
| --- | --- | --- | --- | --- |
| C++23 (GCC 13 / Clang 20 subset) | Native to OCCT, OpenFOAM and Gmsh; fastest; huge CAE talent pool | Memory safety is on you; slower to write; build systems are complex | ISO standard with strong backward compatibility; code lives for decades | **Kernel** |
| Rust | Memory safe at C++ speed; excellent tooling | No mature B-rep CAD kernel; OCCT and solvers only through community FFI; small CAE ecosystem | Stable since 1.0 (2015); editions keep old code compiling | Credible kernel alternative later, not now |
| C# / .NET | Productive, fast; LEAP 71's choice | OCCT and solvers need wrappers; LTS releases get 3 years ([.NET 10 ends Nov 14, 2028](https://dotnet.microsoft.com/platform/support-policy/dotnet-core)), so upgrades every 2–3 years | 3-year LTS cycles | Not needed unless PicoGK becomes central |
| Python | Largest scientific and optimization ecosystem; Ansys's automation language | Slow in pure loops; dynamic typing | Each version gets 5 years ([3.14 supported to Oct 2030](https://endoflife.date/python)) | **Orchestration** |
| Julia | Excellent numerics | Thin CAD ecosystem; weaker deployment record | — | Not chosen |

**Placement rule.** If code defines engineering truth or runs once per candidate, it goes in the C++ kernel. If it talks to people, files, services or schedules work, it goes in Python. The Python–kernel boundary is coarse and data-oriented: a spec and a batch of candidates go in, fidelity-labelled results come out; Python never makes fine-grained calls into kernel objects.

&#91;embedded content: CEM software layers · kernel, bindings, orchestration, solvers\]

The kernel never calls Python; orchestration reaches it only through the bindings, so the kernel can later serve a C#, web or desktop front end unchanged.

**What this costs and how to contain it**

- Kernel work is slower than Python. Contain it by keeping the kernel small in the base release: units, spec, L0, one L1 model, geometry.
- C++ memory and undefined-behaviour bugs. Contain them with no raw owning pointers, value types, -Wall -Wextra -Werror, clang-tidy, AddressSanitizer and UBSan in CI, and fuzzing of the spec parser.
- Python first, then C++: each model is written first in Python, checked against hand calculations, then ported to the kernel. The Python version stays as the independent reference used in tests, and physics functions return std::expected (a value or a typed error such as out-of-validity) instead of throwing. The two must agree to floating-point tolerance, which catches transcription errors in equations.

This replaces the Python-first recommendation in the earlier architecture plan; that plan's other choices stand.

## 4. System context and use cases

The CEM runs on one Linux workstation, used by a design engineer, and talks to three kinds of external systems: a hosted vision model, local numerical solvers, and the physical world through a 3D printer and a test rig.

**Actors and external systems**

| Actor or system | Role | Trust level |
| --- | --- | --- |
| Design engineer | Provides image and requirements, answers questions, chooses among Pareto designs | Authoritative for requirements |
| Reviewer | Approves releases of models, plugins and designs | Authoritative for sign-off |
| Test technician | Prints parts, runs the rig, enters measurements | Authoritative for L3 data |
| Vision LLM service | Interprets image and free text into a draft spec | Untrusted; output is a proposal only |
| Solvers (OpenFOAM, CalculiX, Gmsh) | Mesh and solve cases written by the kernel | Trusted only through the trust gate |
| 3D printer and rig | Produce and measure physical parts | Source of validation data |

**Use cases**

| ID | Use case | Trigger | Successful outcome |
| --- | --- | --- | --- |
| UC-01 | Create a spec from image and text | Engineer submits a photo and a description | A versioned spec with every field's provenance, plus a list of open questions |
| UC-02 | Resolve open questions | Spec has essential unknowns | Engineer answers, or accepts listed provisional defaults; spec becomes runnable |
| UC-03 | Check feasibility and select family | Runnable spec | Chosen family with reasons, or a report that no family meets the duty and the nearest feasible duty |
| UC-04 | Size an initial design | Family selected | One classical design with L1 performance, geometry and exports |
| UC-05 | Run a design campaign | Engineer starts a campaign with a budget | Pareto set of feasible designs, finalists verified at L2, full campaign record |
| UC-06 | Choose and release a design | Campaign complete | Selected design package with fidelity-labelled report |
| UC-07 | Record a rig test | Printed part measured | L3 record linked to the design, with uncertainty |
| UC-08 | Reproduce a past result | Anyone, any time | Same inputs and versions regenerate the same outputs, or the difference is explained |
| UC-09 | Add a fan family | Developer writes a plugin | New family available without changes to kernel core, orchestration or store |

## 5. Functional requirements

Each requirement has an ID, a "shall" statement, its interpretation, a priority (M base, P product v1, L later) and a verification method (T test, A analysis, I inspection, D demonstration).

**Intake (IN)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| IN-001 | The system shall accept a requirement set as structured JSON or YAML | The structured path works with no LLM at all | M | T |
| IN-002 | The system shall accept free text and an optional reference image | Formats: JPEG, PNG; at most one image per spec in v1 | P | T |
| IN-003 | The vision adapter shall return only fields defined in the intake schema, each with a confidence between 0 and 1 | Any extra or malformed output is rejected, not repaired silently | P | T |
| IN-004 | Image-derived fields shall be tagged provenance "image" and never "user" | Even at confidence 1.0 | P | T |
| IN-005 | The system shall not derive absolute dimensions from an image unless the user supplies a reference dimension in the same image | Proportions may be inferred; sizes may not | P | T |
| IN-006 | Raw image, prompt, model identifier and response shall be stored with the spec | For audit and reproducibility of the interpretation | P | I |

**Requirement compiler (SPEC)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| SPEC-001 | Every field shall carry value, unit, tolerance (optional) and provenance | Bare numbers are invalid | M | T |
| SPEC-002 | Inputs shall be converted to SI at the boundary, keeping the original value and unit | e.g. 2000 rpm → 209.44 rad/s, original recorded | M | T |
| SPEC-003 | Dimensionally inconsistent inputs shall be rejected with a message naming the field | e.g. pressure given in m³/s | M | T |
| SPEC-004 | A pressure without a stated type (fan total or fan static, per ISO 5801) shall be rejected, and static-to-static pressure shall be rejected | See section 2 conventions | M | T |
| SPEC-005 | Precedence shall be user > image > derived > default | Applied deterministically; conflicts are recorded | M | T |
| SPEC-006 | Essential fields per family shall be declared by the family plugin | The compiler asks the plugin, not a global list | M | T |
| SPEC-007 | A spec with unresolved essential unknowns shall not start a campaign unless autonomous mode is set and a default exists | Defaults used this way are marked provisional and listed first in reports | M | T |
| SPEC-008 | The compiler shall produce a list of questions for every essential unknown | Each question names the field, unit and why it matters | M | T |
| SPEC-009 | Specs shall be immutable and versioned; an edit creates a new version linked to its parent | Results always point to an exact spec version | M | T |
| SPEC-010 | Physically contradictory requirements shall be reported, not averaged | e.g. flow and pressure demanding more power than the stated limit | M | T |

**Family selection and feasibility (SEL)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| SEL-001 | The system shall compute specific speed and specific diameter for every spec with a duty point | Formulas in the architecture plan, section 6 | M | T |
| SEL-002 | Each family plugin shall declare its feasible specific-speed range from a cited source | Ranges are data with a source, not constants in code | M | I |
| SEL-003 | When the image-indicated family differs from the physics-preferred family, the system shall report both with reasons and ask the user | Never silently switches family | P | T |
| SEL-004 | Infeasible duties shall be rejected before geometry is built, stating the limit violated and the nearest feasible duty | Takes milliseconds | M | T |

**Physics models (PHY)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| PHY-001 | Each model shall be a pure function of its inputs | No hidden state, no I/O, no randomness | M | T |
| PHY-002 | Each model shall declare assumptions, validity ranges and sources in code and in docs/models | Machine-readable ranges are checked at run time | M | I |
| PHY-003 | A model evaluated outside its validity range shall return an out-of-validity result, never a number | Optimizer treats it as not scored | M | T |
| PHY-004 | Each model shall carry a semantic version recorded with every result | Changing an equation bumps the version | M | I |
| PHY-005 | Each L0 and L1 model shall have an independent reference implementation used in tests | The two agree to floating-point tolerance | M | T |
| PHY-006 | Calibrated coefficients shall record the data that produced them | Holdout data never used | P | I |

**Family plugins (FAM)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| FAM-001 | Each family shall be implemented as a plugin behind one interface: parameter space, initial design, L1 evaluation, constraints, geometry recipe, simulation case | Interface defined in the base | M | I |
| FAM-002 | Adding a family shall not require changes outside its plugin folder and its tests | Verified by adding a stub family in CI | M | T |
| FAM-003 | Each plugin shall publish its parameters with units, bounds and defaults | Optimizer reads bounds from here | M | T |

**Geometry (GEO)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| GEO-001 | Geometry shall be built deterministically from parameters | Same parameters give the same volume, area and topology | M | T |
| GEO-002 | Every solid shall pass validity checks: closed, manifold, no self-intersection | Failures recorded as geometry\_failed | M | T |
| GEO-003 | Geometry shall be checked against envelope, minimum wall thickness and printability rules from the spec | Hard constraints | P | T |
| GEO-004 | The system shall export STEP (editable CAD) and 3MF or STL (printing) | Exports are content-addressed artifacts | M | T |
| GEO-005 | Fluid-domain geometry for CFD shall be generated from the same parameters as the part | No hand-made domains | P | T |

**Simulation and trust (SIM, TRUST)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| SIM-001 | Solver cases shall be generated entirely from templates plus candidate data | No manual edits; templates are versioned | P | T |
| SIM-002 | Each solver run shall execute with enforced memory, core and wall-time limits | Exceeding a limit kills the job and records resource\_exceeded | P | T |
| SIM-003 | Each CFD evaluation shall cover at least 3 operating points of the fan curve | Not a single point | P | T |
| SIM-004 | Extracted metrics shall include flow, static and total pressure rise, torque, shaft power and stated efficiency type | With units | P | T |
| TRUST-001 | No simulation result shall be used for ranking or reporting before it passes the trust gate | Gate checks residuals, mass balance, monitor stability, mesh quality | P | T |
| TRUST-002 | A result shall be labelled L2 verified only after a three-level mesh study with GCI below the agreed limit | Start at 5% (proposed) | P | A |
| TRUST-003 | Every gate failure shall record the specific failed checks | For debugging and statistics | P | T |

**Optimization (OPT)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| OPT-001 | Search shall be multi-objective with each objective in physical units | No weighted sum of incompatible units | P | I |
| OPT-002 | Meeting the duty point shall be a constraint, not an objective | Infeasible candidates never outrank feasible ones | P | T |
| OPT-003 | Each campaign shall record its random seed and reproduce identical candidates when rerun | Same versions required | M | T |
| OPT-004 | Promotion to L2 shall select a spread of Pareto points, excluding points near model validity limits | Default 3–5 finalists | P | T |
| OPT-005 | Campaigns shall stop on budget exhaustion or convergence criteria defined in the campaign config | Never run unbounded | M | T |

**Orchestration and store (ORC, STORE)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| ORC-001 | Jobs shall be durable: a crash or power loss shall not lose completed work | Resume from last completed job | M | T |
| ORC-002 | Heavy jobs (meshing, CFD, FEA) shall run one at a time by default on the target laptop | Configurable | M | T |
| ORC-003 | Failed jobs shall be retried at most the configured number of times with a recorded reason | Default 1 retry | M | T |
| STORE-001 | Every spec, candidate, job, result, failure and artifact shall be recorded | Append-only | M | T |
| STORE-002 | Every record shall include kernel version, plugin and model versions, git commit, container digest and input hash | Enough to reproduce | M | T |
| STORE-003 | Artifacts shall be named by content hash | Identical files stored once | M | T |
| STORE-004 | The store schema shall be versioned with forward migrations | No manual schema edits | M | T |

**Reporting and deliverables (REP)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| REP-001 | Every number in a report shall carry a fidelity label and unit | See section 2 | M | T |
| REP-002 | Reports shall list provisional assumptions and unknowns before results | First page | M | I |
| REP-003 | The design package shall include CAD, print files, parameters, materials, curves, simulation setup and results, constraint checks, campaign history and known limitations | Matches the original vision document | P | I |
| REP-004 | Reports shall never call a design optimal, perfect or validated without the evidence the label requires | Wording checked in tests against a banned-claims list | M | T |

**Added in v1.1 (architecture review, 9 October 2026)**

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| SPEC-011 | Duty-point flow and pressure shall carry tolerances. A required operating range or system curve may be given and shall become a set of constraint points | A duty point without a tolerance is rejected | M | T |
| PHY-007 | Each L1 model shall state its validity range in dimensionless terms, including chord Reynolds number, flow coefficient and blade-loading limits, and return out-of-validity outside it | Low-Reynolds operation of small fans is the expected case, not an edge case | P | T, I |
| OPT-006 | Campaign results shall be labelled exploratory until the L1 model version has passed the reference-fan benchmark and the L1–L2 ranking check; only then may a campaign recommend a design | Prevents the optimizer from exploiting an unbenchmarked model | P | T |
| OPT-007 | A stated power limit shall be a hard constraint at every required operating point | Not an objective | P | T |
| OPT-008 | At each required operating point the fan-curve slope shall be negative, and each family plugin shall define a stall-margin criterion with a cited source | Steady RANS near stall is not trusted; the rig measures stall | P | T, A |
| STORE-005 | Every result shall reference one design revision ID: a hash of spec version, plugin version, parameter vector, geometry recipe version and kernel version. Geometry, mesh, case and result records shall store that ID plus their own content hash, excluding timestamps (e.g. STEP header dates). Results whose chain does not match shall be rejected | Mesh records also store the domain decomposition | M | T |
| TRUST-004 | Each simulation result shall have exactly one status: VERIFIED, CONVERGED, UNCONVERGED, MESH\_INVALID, RESOURCE\_LIMIT or SOLVER\_FAILED. Only CONVERGED and VERIFIED may be ranked, and only VERIFIED may be labelled L2 verified | Thresholds are per quantity and per versioned case template | P | T |
| ORC-004 | Jobs shall follow a defined state machine (queued, running, publishing, done or failed) recording input hash, attempt count, resources, artifact references and outcome. Artifacts shall be written to a temporary location, verified, then published atomically with a manifest; on restart the runner shall reconcile incomplete jobs | No blind reruns after a crash | M | T |

## 6. Non-functional requirements

These define "production grade" in testable terms. Numeric targets marked (proposed) are measured on the target laptop in the base release and then fixed by a logged decision.

| ID | Requirement | Interpretation | Pri | Verify |
| --- | --- | --- | --- | --- |
| COR-001 | All kernel physics shall use strong unit types so that adding incompatible quantities fails to compile | C++ units library (mp-units); bypassing it needs a reviewed exception | M | I, T |
| COR-002 | Floating-point results of L1 models shall be identical across reruns on the same build | No uninitialized memory, no unordered reductions in deterministic paths | M | T |
| COR-003 | Every reported quantity shall carry its fidelity label end to end, from kernel to report | Label is part of the data type, not formatting. Values labelled L2 verified or L3 validated must also carry their uncertainty (numerical or measurement) | M | T |
| REPRO-001 | Any stored result shall be regenerable from its record using the same container image | Demonstrated monthly on a random sample | M | D |
| REPRO-002 | All dependencies shall be pinned to exact versions (C++ through vcpkg or Conan lock, Python through uv lock, solvers through the image digest) | No floating versions | M | I |
| PERF-001 | One L1 evaluation of a ducted axial candidate shall take at most 1 ms in the kernel (proposed) | Measured by a benchmark in CI | M | T |
| PERF-002 | The kernel shall evaluate candidates in batches through the Python binding without a per-candidate Python loop | Population-at-once API | M | T |
| PERF-003 | Spec compilation shall take at most 1 s (proposed) | Excluding LLM calls | M | T |
| PERF-004 | One geometry build with exports shall take at most 30 s (proposed) | Measured across random samples | P | T |
| RES-001 | Total memory used by a heavy job shall be capped below physical RAM, leaving at least 3 GB for the OS (proposed: 12 GB cap on 16 GB) | Enforced by the runner | M | T |
| RES-002 | The system shall estimate CFD memory from cell count before meshing and refuse cases above the cap | Uses the measured RAM-per-cell figure | P | T |
| REL-001 | No crash in the orchestration layer shall corrupt the store | SQLite in WAL mode, transactions per job state change | M | T |
| REL-002 | Every failure shall be classified with one of the defined failure codes | The controlled list is docs/models/platform/error-codes.md, generated from and tested against kernel/cemkit/core/error.hpp. Codes are append-only: never renamed, renumbered or reused. No uncategorized failures in reports | M | T |
| REL-003 | Failure-injection tests shall cover mesh failure, solver divergence, out-of-memory, disk full and killed process | Run in CI with fakes, nightly with real solvers | M | T |
| MAINT-001 | C++ code shall build with -Wall -Wextra -Werror under GCC and Clang, pass clang-tidy, and run tests under AddressSanitizer and UBSan | In CI on every change | M | T |
| MAINT-002 | Python code shall pass ruff and mypy --strict | In CI | M | T |
| MAINT-003 | Line coverage shall be at least 90% (proposed) on the platform core, spec and physics modules and on every product domain's physics (products/\*/common and each family's L1 model) | Coverage is a floor, not a goal | M | T |
| MAINT-004 | Every architectural decision shall be recorded as an ADR | context, options, decision, consequences | M | I |
| MAINT-005 | Public interfaces (C ABI, Python API, JSON schemas) shall follow semantic versioning | Breaking change = major version | M | I |
| SEC-001 | Secrets (LLM API keys) shall come only from the environment or a secret store, never from files in the repository or records | Checked by a secret scanner in CI | M | T |
| SEC-002 | LLM output shall be treated as untrusted data and validated against a schema before use | Never executed, never used as code or file paths | M | T |
| SEC-003 | Solver jobs shall run in the container without network access | Prevents accidental data egress | P | I |
| PORT-001 | The system shall run on Ubuntu 24.04 x86-64; other platforms are best-effort | Containerized solvers make this portable later | M | D |
| OBS-001 | Every job shall emit structured logs with peak RAM, CPU time and wall time | JSON lines | M | T |
| LIFE-001 | Each pinned dependency shall be within its vendor support window at release time, with a written upgrade plan before its end date | e.g. Python 3.14 until Oct 2030; Ubuntu 24.04 LTS | M | I |
| LIFE-002 | Kernel code shall depend only on ISO C++23 (the GCC 13 / Clang 20 subset), OCCT and a short, reviewed list of libraries | Limits long-term maintenance risk | M | I |

## 7. Product requirements for v1: 120 mm ducted axial fan

The first product is a 3D-printed ducted axial fan of 120 mm nominal size; its duty point is still unknown and must be supplied before the first campaign. Each row shows how the original wording is interpreted so the code has no room to guess.

| ID | Requirement | Value | Provenance | Interpretation |
| --- | --- | --- | --- | --- |
| AX-001 | Nominal size | 120 mm | user | Taken as the duct inner diameter; rotor tip diameter = 120 mm − 2 × tip clearance. **Confirm** (PC fans use 120 mm as the frame size) |
| AX-002 | Rotational speed | 2000 rpm (209.4 rad/s) | default, provisional | A starting assumption from the vision document, not a requirement; may become a design variable if the motor allows |
| AX-003 | Duty point flow | unknown | unknown | **Essential.** Must be given in m³/s, m³/h or CFM before a campaign |
| AX-004 | Duty point pressure | unknown | unknown | **Essential.** Must be given with its type; static pressure is the expected type for a ducted fan |
| AX-005 | Air conditions | 1.18 kg/m³, 298.15 K | default | Sea-level room air |
| AX-006 | Manufacturing | FDM 3D printing | user | Print orientation chosen by the geometry plugin and recorded |
| AX-007 | Material | PETG | default, provisional | Replace with the material actually used; strength values need coupon tests on your printer |
| AX-008 | Minimum wall thickness | 0.8 mm | default, provisional | Assumes a 0.4 mm nozzle and two perimeters |
| AX-009 | Tip clearance lower bound | 0.5 mm | default, provisional | Set by print accuracy and assembly; measured on the first print |
| AX-010 | Design scope | Rotor, hub, blades, duct | user | Motor mount is a simple bore of user-given diameter; motor not designed |
| AX-011 | Objectives | Total-to-static efficiency (max), tip speed (min), rotor mass (min) | derived from user's "balanced" goal | "Balanced" is interpreted as a Pareto front over these three; the user picks the final trade-off |
| AX-012 | Pressure capability | Constraint, not objective | derived | Must meet the duty pressure at the duty flow with a margin of 10% (proposed) |
| AX-013 | Noise | Ranking proxies only | derived | Tip speed and blade loading reported; no dB(A) claim in v1 |
| AX-014 | Structural safety factor | 2.0 at 120% of design speed (proposed) | default, provisional | Covers overspeed and printed-material scatter until coupon data exist |
| AX-015 | Envelope | 120 × 120 mm frame, depth 25–40 mm (proposed) | default, provisional | Based on common fan frame depths; confirm |
| AX-016 | Deliverables | STEP, 3MF, parameters, fan curve, efficiency, stress results, constraint checks, campaign history, limitations | user (vision document) | Every number fidelity-labelled |
| AX-017 | Validation | Rig measurement of the printed design and at least 2 reference fans | derived | Needed to label any number L3 validated |

**What the system must ask before the first campaign:** duty flow, duty pressure and its type, whether 120 mm is the frame or the rotor, the actual print material, and the motor's speed range and shaft or bore size.

## 8. CEM base (first build): scope, modules, acceptance criteria

The base is the production skeleton every fan family will stand on: units, specifications, provenance, feasibility, the plugin system, geometry plumbing, the store and the job runner. It contains no fan-specific physics beyond L0; the ducted axial L1 model is the next milestone, built on top of it.

> **v1.2:** the repository layout below is superseded by `docs/repository-structure.md`: a product-agnostic CEM platform (`cem`kit) with fans as the first product domain, ports and adapters for every external engine, and dependency rules enforced in CI. The module scope and acceptance criteria in this section still apply, under the new paths.

**Repository layout**

```text
fan-cem/
├── CMakeLists.txt, CMakePresets.json
├── vcpkg.json                         # C++ deps pinned by baseline: OCCT 8.0.x, mp-units, nlohmann-json, Catch2, nanobind
├── pyproject.toml, uv.lock            # Python deps pinned
├── schemas/                           # JSON Schema: single source of truth for spec, candidate, result
├── kernel/                            # libcemkit, C++23 mode
│   ├── include/cemkit/
│   │   ├── core/      units.hpp, quantity.hpp, fidelity.hpp, provenance.hpp, result.hpp, version.hpp
│   │   ├── spec/      spec.hpp, field.hpp, compiler.hpp, questions.hpp
│   │   ├── physics/   air.hpp, similarity.hpp, specific_speed.hpp
│   │   ├── family/    plugin.hpp, registry.hpp, parameter_space.hpp
│   │   └── geometry/  backend.hpp, checks.hpp, export.hpp
│   ├── src/                           # implementations, incl. occt_backend.cpp
│   ├── capi/cemkit.h                  # stable C ABI
│   ├── plugins/stub_family/           # proves FAM-002 in CI
│   └── tests/                         # Catch2 unit and property tests
├── bindings/python/                   # nanobind module cemkit._kernel (batch APIs)
├── python/cemkit/
│   ├── cli.py
│   ├── orchestration/  jobs.py, worker.py, limits.py
│   ├── store/          db.py, migrations/, artifacts.py
│   ├── reference/      # independent Python reference implementations (tests only)
│   └── logging.py
├── tests/              python/, integration/
├── docs/               adr/, models/
├── docker/Dockerfile               # Ubuntu 24.04, GCC 13, Clang 20, OpenFOAM v2512, CalculiX, Gmsh 4.15
└── .github/workflows/ci.yml        # build, test, sanitizers, lint, type check
```

**Modules in the base and the requirements they satisfy**

| Module | Language | Delivers | Requirements |
| --- | --- | --- | --- |
| core | C++ | Strong unit types, fidelity-labelled values, provenance, versioned result types | COR-001, COR-003, PHY-004 |
| spec | C++ | Spec data model, SI conversion, validation, precedence, questions, immutable versions | SPEC-001 to SPEC-010 |
| physics (L0) | C++ | Air properties, fan laws, specific speed and diameter, feasibility checks | SEL-001, SEL-004, PHY-001 to PHY-003 |
| family | C++ | Plugin interface, registry, parameter spaces, stub family | FAM-001 to FAM-003, SEL-002 |
| geometry | C++ | Backend interface, OCCT implementation, validity checks, STEP and STL export of a test solid | GEO-001, GEO-002, GEO-004 |
| capi + bindings | C, C++ | Stable C ABI and Python module with batch evaluation | PERF-002, MAINT-005 |
| store | Python | SQLite schema, migrations, content-addressed artifacts, run metadata | STORE-001 to STORE-004, REL-001 |
| orchestration | Python | Durable job queue, worker, resource limits, retries, failure codes | ORC-001 to ORC-003, RES-001, REL-002 |
| CLI | Python | `cemkit spec compile`, `cemkit spec questions`, `cemkit feasibility`, `cemkit geometry smoke`, `cemkit runs show` | IN-001, UC-01 to UC-03, UC-08 |
| reference | Python | Reference implementations of L0 formulas for cross-checking | PHY-005 |
| CI and container | — | Pinned toolchain, sanitizers, lint, type checks, coverage | MAINT-001 to MAINT-003, REPRO-002, SEC-001 |

**Out of the base (next milestones):** ducted axial L1 model and geometry recipe, CFD case writer and trust gate, optimizer, vision intake, full reports.

**Acceptance criteria for the base**

- [ ] Clean clone builds and passes all tests with one command inside the container
- [ ] CI runs GCC and Clang builds with -Werror, clang-tidy, ASan/UBSan, ruff, mypy --strict, and coverage of at least 90% on core, spec and physics
- [ ] A unit error (e.g. adding Pa to m³/s) fails to compile, shown by a negative compile test
- [ ] `cemkit spec compile examples/axial_120.yaml` produces a versioned spec with provenance on every field and lists the open questions from section 7
- [ ] A spec with a pressure lacking its type is rejected with a message naming the field
- [ ] `cemkit feasibility` returns specific speed and diameter matching the Python reference to floating-point tolerance, and rejects an impossible duty with the violated limit
- [ ] The stub family registers and runs without any edit outside its folder
- [ ] `cemkit geometry smoke` builds an OCCT solid, passes validity checks and exports STEP and STL with content-hash names
- [ ] Killing the worker mid-job and restarting resumes without losing or duplicating completed jobs
- [ ] Every run record contains kernel, plugin and model versions, git commit, container digest and input hash
- [ ] ADR-001 to ADR-003 merged

## 9. Constraints, assumptions and open questions

The constraints below are fixed for v1; the assumptions must each be tested, and the open questions block the first campaign (not the base build).

**Constraints**

| ID | Constraint |
| --- | --- |
| CON-001 | Target machine: Intel i5 12th gen laptop, 16 GB RAM, RTX 3050 6 GB, Ubuntu 24.04 |
| CON-002 | Open-source tools only for the core loop (OCCT LGPL with exception, OpenFOAM GPL, CalculiX GPL, Gmsh GPL); solvers run as separate processes so the kernel's own license stays your choice |
| CON-003 | No cloud infrastructure required; the vision model is the only hosted service, and the structured-input path works without it |
| CON-004 | One developer-scale team; no microservices |

**Assumptions to test**

| ID | Assumption | How it is tested | When |
| --- | --- | --- | --- |
| ASM-001 | OpenFOAM single-passage MRF fits under the 12 GB job cap at mesh-study resolution | Memory measurement on tutorials, then on the first fan | Base build week 1 |
| ASM-002 | OCCT lofts and fillets twisted blades reliably over the parameter range | Random-sample build test | Axial geometry milestone |
| ASM-003 | GCC 13 and Clang on Ubuntu 24.04 build mp-units and OCCT 8.0 together cleanly | Container build in CI | Base build week 1 |
| ASM-004 | The L1 axial model ranks designs like CFD does | Rank correlation on 10 designs | CFD milestone |
| ASM-005 | A vision model identifies fan type and blade count on your photos accurately enough | Labelled photo set | Vision milestone |

**Open questions**

- [ ] Is 120 mm the frame size or the rotor diameter?
- [ ] Duty point: target flow and pressure, with pressure type
- [ ] Print material, nozzle size and printer model
- [ ] Motor: speed range and mounting dimensions
- [ ] License for your own code: proprietary or open source (affects whether solvers may be linked or must stay separate processes; separate processes are assumed)
- [ ] Which hosted vision model and account to use for intake

* [ ] Low-Reynolds data: which published airfoil or cascade data covers chord Reynolds numbers of roughly 2–3 × 10⁴ (estimated for the 120 mm fan at 2000 rpm)? Needed before the L1 axial model (PHY-007)

**Sources** (as of 9 October 2026)

- [PicoGK documentation (LEAP 71)](https://github.com/leap71/PicoGK/blob/main/Documentation/README.md)
- [PyMAPDL project overview (Ansys)](https://mapdl.docs.pyansys.com/version/stable/getting_started/project.html)
- [Open CASCADE Technology 8.0.0 release](https://dev.opencascade.org/content/open-cascade-technology-800-release)
- [Open CASCADE Technology 8.0.1 release](https://github.com/Open-Cascade-SAS/OCCT/discussions/1415)
- [.NET support policy (Microsoft)](https://dotnet.microsoft.com/platform/support-policy/dotnet-core)
- [Python release support (endoflife.date)](https://endoflife.date/python)
- [Gmsh overview and APIs](https://audilab.bme.mcgill.ca/sw/gmsh.html)

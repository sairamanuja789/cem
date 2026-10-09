# Units and quantity types (COR-001)

Code: `kernel/cemkit/core/units.hpp`, `kernel/cemkit/core/quantity.hpp`. Library: mp-units 2.5.0
(ISQ quantity specs, SI units), every value stored as a `double`.

## Rules

- Every physical value in the kernel is a strong quantity type. Values are stored in the coherent SI
  unit of the alias; assigning a value given in another unit of the same kind (mm, kPa, m³/h)
  converts it. Conversion from user units (rpm, CFM, inH₂O) happens only in the spec compiler, which
  records the original value and unit.
- A raw `double` is never accepted where a quantity is expected.
- Quantities of different kinds do not mix, even when they share a dimension. This applies to angle
  and ratio, to torque (N·m) and energy (J), and to fan total and fan static pressure (see
  `docs/models/fans/pressure-kinds.md`).
- Absolute pressure and absolute temperature are *points* (`quantity_point`). The difference of two
  points is a quantity (`Pressure`, `TemperatureDifference`). A point plus a quantity is a point. Two
  points cannot be added.
- Temperature values in non-kelvin units are built with `mp_units::point<...>` (an absolute value)
  or `mp_units::delta<...>` (a difference). mp-units 2.5 deprecates `value * unit` for offset units
  such as °C, and the build treats deprecation warnings as errors.

## Types

| Alias | ISQ quantity | Stored unit | Kind |
| --- | --- | --- | --- |
| `Length` | `isq::length` | m | quantity |
| `Area` | `isq::area` | m² | quantity |
| `Volume` | `isq::volume` | m³ | quantity |
| `VolumeFlowRate` | `units::volume_flow_rate` = volume / time | m³/s | quantity |
| `Pressure` | `isq::pressure` | Pa | quantity (a pressure difference) |
| `AbsolutePressure` | `isq::pressure` from `units::absolute_zero_pressure` | Pa | point |
| `Power` | `isq::power` | W | quantity |
| `Density` | `isq::mass_density` | kg/m³ | quantity |
| `AngularVelocity` | `isq::angular_velocity` | rad/s | quantity |
| `Speed` | `isq::speed` | m/s | quantity |
| `Mass` | `isq::mass` | kg | quantity |
| `Torque` | `isq::torque` | N·m | quantity, not convertible to energy |
| `AbsoluteTemperature` | `isq::thermodynamic_temperature` from `si::absolute_zero` | K | point |
| `TemperatureDifference` | `isq::thermodynamic_temperature` | K | quantity |
| `DynamicViscosity` | `isq::dynamic_viscosity` | Pa·s | quantity |
| `Angle` | `isq::angular_measure` | rad | own kind, not a ratio |
| `Ratio` | `dimensionless` | 1 | quantity |

mp-units 2.5's ISQ has no volume flow rate, so `units::volume_flow_rate` defines it as volume / time
(ISO 80000-4 lists volume flow rate as a quantity).

CLAUDE.md rule 4 ("unit conversion happens only in the spec compiler") concerns the units a user
states (rpm, CFM, mm) and recording the original value. mp-units' own scaling between units of one
kind inside the kernel (kPa to Pa) is type-checked and exact in intent. That rule does not cover it.

## Writing equations

These points are from the physics review of T04, probed with GCC 13 and mp-units 2.5.

- Products whose result is a named quantity convert implicitly: T·ω → `Power`, ω·r → `Speed`,
  A·v → `VolumeFlowRate`, Δp·q_V → `Power`.
- Some textbook groupings need an explicit conversion, because ISQ gives the product a different
  quantity:
  - ½ρv² → `Pressure`;
  - ρvL/μ → `Ratio`;
  - ρ·q_V → mass flow rate.

  Write `Pressure{0.5 * rho * v * v}` or use `mp_units::quantity_cast`. The dimension is still
  checked.
- F·r does not become `Torque`: ISQ treats moment of force as a vector product. Torque is computed
  from its own definition.
- `AngularVelocity` is a vector quantity in ISQ; here it holds the magnitude of the shaft speed.
- Trigonometry on an `Angle` uses `mp_units::si::sin` and the related functions from
  `<mp-units/systems/si/math.h>`. An unqualified `sin(angle)` picks `std::sin` and fails to compile.

## Evidence

- Unit tests: `kernel/cemkit/core/tests/test_units.cpp`.
- Compile-fail tests: `kernel/testing/compile_fail/`. Each case has a control build that must
  compile and a guarded build that must not, under both GCC 13 and Clang 20. The cases are:
  - pressure + flow rate;
  - length assigned to pressure;
  - raw double used as pressure;
  - torque used as energy;
  - absolute pressure + absolute pressure;
  - angle used as ratio.

## Design notes

- mp-units expresses every distinction above directly (kinds via `is_kind`, absolute values via
  point origins). No wrapper types were needed, so the ADR-010 fallback was not used.
- Comparing a `std::optional<Q>` with anything through libstdc++ 13's mixed `optional == U` overloads
  makes mp-units' representation concept recurse; GCC 13 reports "satisfaction of atomic constraint
  depends on itself". Unwrap the optional before comparing (see `Labelled::operator==`).

## Spec compiler unit table (T07)

The spec compiler (`kernel/cemkit/spec/units.cpp`) converts user units to SI and records the
original value and unit (architecture rule 4). Decisions: ADR-003 D8 (proposed, needs owner review).

| Unit | SI factor | Source |
| --- | --- | --- |
| mm, cm, kPa, kW, mPa·s, L/s, % | SI prefixes / definitions | SI |
| in | 0.0254 m (exact) | NIST SP 811 (2008), Sec. B.8 |
| ft³/min (CFM) | 0.3048³ / 60 m³/s = 4.719474 × 10⁻⁴ m³/s | NIST SP 811 Sec. B.8: ft = 0.3048 m exact; ft³/min = 4.719 474 E−04 m³/s |
| m³/min, m³/h | 1/60, 1/3600 m³/s | time units |
| rpm | 2π/60 rad/s | NIST SP 811 Sec. B.8: 1.047 198 E−01 rad/s |
| mmH₂O (conventional) | 9.80665 Pa | NIST SP 811 Sec. B.8; g_n = 9.806 65 m/s² exactly (same document) |
| inH₂O (conventional) | 25.4 × 9.80665 = 249.08891 Pa | NIST SP 811 Sec. B.8: 2.490 889 E+02 Pa |
| °C | T + 273.15 K (differences: × 1) | SI definition of the Celsius scale |

**Open point (owner):** inH₂O at 60 °F is 248.84 Pa and at 39.2 °F is 249.082 Pa (NIST SP 811
Sec. B.8). The conventional value is used unless the owner decides a reference temperature must be
stated (`docs/open-items.md`).

Document: B. N. Taylor and A. Thompson, *Guide for the Use of the International System of Units
(SI)*, NIST Special Publication 811, 2008 edition; entries read from the NIST PDF on 2026-10-10.

"""General vortex blade design of an axial rotor with simple radial equilibrium, loss-free (L1;
independent kernel reference).

Model fans.axial.vortex@0.1.0. Equations, assumptions and sources: docs/models/fans/axial-vortex.md,
ADR-018. The C++ kernel port must agree to floating-point tolerance (PHY-005).

Swirl: c_theta1 = 0 (no inlet guide vanes) and c_theta2 = K r^n with the vortex exponent n in
[-1, 1]: n = -1 free vortex (r c_theta2 constant), n = 0 constant swirl, n = 1 forced vortex
(Dixon & Hall, 7th ed., three-dimensional flows in axial turbomachines, general vortex design;
pages to be confirmed by the owner, HI-031).

Axial velocity: the inlet is uniform, c_x1 = Q / (pi (r_tip^2 - r_hub^2)). Behind the rotor, simple
radial equilibrium with the loss-free Euler work h02 - h01 = U c_theta2 = omega K r^(n+1) and a
uniform inlet total enthalpy gives (same source)

    d(c_x2^2 / 2)/dr = d(omega K r^(n+1))/dr - (c_theta2 / r) d(r c_theta2)/dr,

so, with A = c_x2^2 at the hub,

    c_x2^2(r) = A + 2 omega K (r^(n+1) - r_hub^(n+1)) - 2 (n + 1) K^2 I(r),
    I(r) = integral from r_hub to r of s^(2n-1) ds = r_hub^(2n) expm1(2n ln(r / r_hub)) / (2n)
           (ln(r / r_hub) for n = 0).

A follows from continuity, Q = integral of 2 pi r c_x2 dr, and K from the duty: the mass-averaged
loss-free total-pressure rise rho omega (integral of r^(n+1) c_x2 2 pi r dr) / Q equals dp_t. Both
integrals use composite Simpson's rule on N_INTERVALS intervals; both unknowns are found by
bisection run until the interval cannot shrink (deterministic; the kernel port repeats it). For
n = -1 the equilibrium terms vanish, c_x2 = c_x1 and K = dp_t / (rho omega): the free vortex.

Sections: beta1 = atan2(U, c_x1), beta2 = atan2(U - c_theta2, c_x2), circular-arc camber with zero
incidence and zero deviation (UNSOURCED, HI-027) as in free_vortex.py; reaction
R = 1 - (c_x2^2 - c_x1^2 + c_theta2^2) / (2 U c_theta2) (rotor static over total enthalpy rise,
c_theta1 = 0; Dixon & Hall, reaction; reduces to 1 - c_theta2 / (2U) for c_x2 = c_x1).

Validity: that of free_vortex.py, plus -1 <= n <= 1 (the search range of docs/architecture.md
section 5) and c_x2^2 > 0 at every quadrature node (no reverse flow behind the rotor).
"""

from __future__ import annotations

import math
from collections.abc import Callable
from dataclasses import dataclass

from cemkit.errors import Error, error, format_number
from cemkit.reference.fans.axial.free_vortex import Station, check_sections
from cemkit.reference.fans.axial.mean_line import MeanLine, mean_line
from cemkit.reference.fans.common.l0 import FanTotalPressure
from cemkit.reference.platform.air import Air

MODEL = "fans.axial.vortex@0.1.0"
N_INTERVALS = 200  # Simpson intervals (even); a numerical choice, not an engineering constant
MAX_DOUBLINGS = 200


@dataclass(frozen=True)
class VortexDesign:
    mean_line: MeanLine
    axial_velocity: float  # c_x1, uniform inlet
    vortex_exponent: float  # n
    swirl_constant: float  # K, SI (m^(1-n)/s)
    hub_axial_velocity_squared: float  # A = c_x2^2 at the hub (m2/s2)
    stations: tuple[Station, ...]  # root (embedded) first, tip last


def _bisect(f: Callable[[float], bool], lo: float, hi: float) -> tuple[float, float]:
    """Adjacent doubles (lo, hi) with f false at lo and true at hi, from such a bracket."""
    while True:
        mid = 0.5 * (lo + hi)
        if not lo < mid < hi:
            return lo, hi
        if f(mid):
            hi = mid
        else:
            lo = mid


class _Equilibrium:
    def __init__(self, n: float, omega: float, r_hub: float, r_tip: float) -> None:
        self.n = n
        self.omega = omega
        self.r_hub = r_hub
        h = (r_tip - r_hub) / N_INTERVALS
        self.nodes = [r_hub + (j * h) for j in range(N_INTERVALS + 1)]
        self.weights = [
            (h / 3.0) * (1.0 if j in (0, N_INTERVALS) else (4.0 if j % 2 else 2.0))
            for j in range(N_INTERVALS + 1)
        ]
        self.a = [self.work_term(r) for r in self.nodes]
        self.b = [self.swirl_term(r) for r in self.nodes]

    def work_term(self, r: float) -> float:
        n = self.n
        return 2.0 * self.omega * (math.pow(r, n + 1.0) - math.pow(self.r_hub, n + 1.0))

    def swirl_term(self, r: float) -> float:
        n = self.n
        x = math.log(r / self.r_hub)
        integral = (
            x if n == 0.0 else math.pow(self.r_hub, 2.0 * n) * math.expm1(2.0 * n * x) / (2.0 * n)
        )
        return 2.0 * (n + 1.0) * integral

    def cx2_squared(self, a_hub: float, k: float, a: float, b: float) -> float:
        return a_hub + (k * a) - (k * k * b)

    def min_hub(self, k: float) -> float:
        """The smallest A that keeps c_x2^2 >= 0 at every node."""
        return max(max((k * k * b) - (k * a) for a, b in zip(self.a, self.b, strict=True)), 0.0)

    def flow(self, a_hub: float, k: float) -> float:
        total = 0.0
        for r, w, a, b in zip(self.nodes, self.weights, self.a, self.b, strict=True):
            total += w * 2.0 * math.pi * r * math.sqrt(max(self.cx2_squared(a_hub, k, a, b), 0.0))
        return total

    def work_flow(self, a_hub: float, k: float) -> float:
        """Integral of omega K r^(n+1) c_x2 2 pi r dr (m3/s times m2/s2)."""
        total = 0.0
        for r, w, a, b in zip(self.nodes, self.weights, self.a, self.b, strict=True):
            c = math.sqrt(max(self.cx2_squared(a_hub, k, a, b), 0.0))
            total += w * 2.0 * math.pi * r * math.pow(r, self.n + 1.0) * c
        return self.omega * k * total


def _invalid(message: str, subject: str, value: float, bounds: str) -> Error:
    return error(
        "out_of_validity",
        message,
        subject,
        value=format_number(value),
        bounds=bounds,
        model=MODEL,
    )


def vortex_design(
    flow: float,
    pressure: FanTotalPressure,
    omega: float,
    r_tip: float,
    r_hub: float,
    air: Air,
    chord_hub: float,
    chord_tip: float,
    thickness: float,
    stations: int,
    embed_depth: float,
    vortex_exponent: float,
) -> VortexDesign | Error:
    """Loss-free general-vortex blade sections from hub to tip, plus an embedded root station."""
    line = mean_line(flow, pressure, omega, r_tip, r_hub, air)
    if isinstance(line, Error):
        return line
    failure = check_sections(chord_hub, chord_tip, thickness, stations, embed_depth, r_hub, MODEL)
    if failure is not None:
        return failure
    n = vortex_exponent
    if not (math.isfinite(n) and -1.0 <= n <= 1.0):
        return _invalid("the vortex exponent must be in [-1, 1]", "vortex_exponent", n, "[-1, 1]")

    eq = _Equilibrium(n, omega, r_hub, r_tip)
    c_x1 = line.axial_velocity

    def hub_for(k: float) -> float | None:
        lo = eq.min_hub(k)
        if eq.flow(lo, k) >= flow:
            return None  # continuity needs reverse flow somewhere behind the rotor
        hi = lo + (c_x1 * c_x1)
        for _ in range(MAX_DOUBLINGS):
            if eq.flow(hi, k) >= flow:
                return _bisect(lambda x: eq.flow(x, k) >= flow, lo, hi)[1]
            hi = lo + (2.0 * (hi - lo))
        return None

    def rise(k: float) -> float | None:
        a_hub = hub_for(k)
        return None if a_hub is None else air.density * eq.work_flow(a_hub, k) / flow

    reverse = _invalid(
        "simple radial equilibrium needs reverse flow behind the rotor to reach this duty at this "
        "vortex exponent",
        "vortex_exponent",
        n,
        "c_x2^2 > 0 at every radius",
    )
    # Bracket K: the rise is 0 at K = 0 and grows with K until the equilibrium needs reverse flow.
    # When a trial K needs reverse flow, the largest K that does not is the last candidate.
    k_lo = 0.0
    r_mid = 0.5 * (r_hub + r_tip)
    k_hi = pressure.pa / (air.density * omega * math.pow(r_mid, n + 1.0))
    for _ in range(MAX_DOUBLINGS):
        p = rise(k_hi)
        if p is None:
            k_hi = _bisect(lambda x: rise(x) is None, k_lo, k_hi)[0]
            p = rise(k_hi)
            if p is None or p < pressure.pa:
                return reverse
            break
        if p >= pressure.pa:
            break
        k_lo, k_hi = k_hi, 2.0 * k_hi
    else:
        return reverse

    # A K that needs reverse flow counts as "too high"; the final K is checked again below.
    k = _bisect(lambda x: (p := rise(x)) is None or p >= pressure.pa, k_lo, k_hi)[1]
    a_hub = hub_for(k)
    if a_hub is None:
        return reverse

    out: list[Station] = []
    for i in range(stations):
        f = i / (stations - 1)
        r = r_hub + (f * (r_tip - r_hub))
        chord = chord_hub + (f * (chord_tip - chord_hub))
        c_x2_sq = eq.cx2_squared(a_hub, k, eq.work_term(r), eq.swirl_term(r))
        if not c_x2_sq > 0.0:
            return reverse
        c_x2 = math.sqrt(c_x2_sq)
        u = omega * r
        c_theta2 = k * math.pow(r, n)
        w_theta2 = u - c_theta2
        beta1 = math.atan2(u, c_x1)
        beta2 = math.atan2(w_theta2, c_x2)
        theta = beta1 - beta2
        xi = (beta1 + beta2) / 2.0
        w1 = math.sqrt((c_x1 * c_x1) + (u * u))
        w2 = math.sqrt((c_x2 * c_x2) + (w_theta2 * w_theta2))
        reaction = 1.0 - (
            ((c_x2 * c_x2) - (c_x1 * c_x1) + (c_theta2 * c_theta2)) / (2.0 * u * c_theta2)
        )
        out.append(
            Station(
                radius=r,
                blade_speed=u,
                outlet_swirl=c_theta2,
                relative_inlet_angle=beta1,
                relative_outlet_angle=beta2,
                camber_angle=theta,
                stagger_from_axis=xi,
                recipe_stagger=-xi,
                chord=chord,
                camber=(chord / 2.0) * math.tan(theta / 4.0),
                thickness=thickness,
                de_haller_ratio=w2 / w1,
                reaction=reaction,
                embedded=False,
                outlet_axial_velocity=c_x2,
            )
        )
    hub = out[0]
    root = Station(**{**hub.__dict__, "radius": r_hub - embed_depth, "embedded": True})
    return VortexDesign(
        mean_line=line,
        axial_velocity=c_x1,
        vortex_exponent=n,
        swirl_constant=k,
        hub_axial_velocity_squared=a_hub,
        stations=(root, *out),
    )

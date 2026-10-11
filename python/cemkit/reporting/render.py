"""Flat-shaded previews of binary STL parts as PNG (standard library only; REP-003 pictures).

A preview shows what was built; it carries no fidelity label and implies no performance.
"""

from __future__ import annotations

import math
import struct
import zlib
from collections.abc import Sequence
from pathlib import Path

Vec = tuple[float, float, float]
Tri = tuple[Vec, Vec, Vec]

VIEWS = {  # name: (yaw, pitch) in rad; the view looks along +y after rotation
    "iso": (math.radians(30.0), math.radians(-35.0)),
    "axial": (0.0, math.pi / 2.0),
    "side": (0.0, 0.0),
}


def load_stl(data: bytes) -> list[Tri]:
    """Triangles of a binary STL."""
    (n,) = struct.unpack_from("<I", data, 80)
    tris: list[Tri] = []
    for i in range(n):
        v = struct.unpack_from("<12f", data, 84 + i * 50)
        tris.append(((v[3], v[4], v[5]), (v[6], v[7], v[8]), (v[9], v[10], v[11])))
    return tris


def _rot(p: Vec, yaw: float, pitch: float) -> Vec:
    x, y, z = p
    c, s = math.cos(yaw), math.sin(yaw)
    x, y = c * x - s * y, s * x + c * y
    c, s = math.cos(pitch), math.sin(pitch)
    y, z = c * y - s * z, s * y + c * z
    return x, y, z


def _png(width: int, height: int, px: Sequence[int]) -> bytes:
    raw = b"".join(b"\x00" + bytes(px[r * width * 3 : (r + 1) * width * 3]) for r in range(height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def render(tris: Sequence[Tri], view: str, width: int = 520, height: int = 420) -> bytes:
    """A PNG of the triangles seen from `view` (iso, axial or side)."""
    yaw, pitch = VIEWS[view]
    pts = [tuple(_rot(p, yaw, pitch) for p in t) for t in tris]
    xs = [p[0] for t in pts for p in t]
    zs = [p[2] for t in pts for p in t]
    scale = 0.85 * min(width / ((max(xs) - min(xs)) or 1.0), height / ((max(zs) - min(zs)) or 1.0))
    cx, cz = (max(xs) + min(xs)) / 2.0, (max(zs) + min(zs)) / 2.0
    zbuf = [math.inf] * (width * height)
    px = [245] * (width * height * 3)
    light = (-0.4, -0.8, 0.45)
    norm = math.sqrt(sum(c * c for c in light))
    light = (light[0] / norm, light[1] / norm, light[2] / norm)
    for a, b, c in pts:
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
        shade = abs((n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / length)
        colour = (int(60 + 150 * shade), int(90 + 130 * shade), int(140 + 110 * shade))
        sc = [
            ((p[0] - cx) * scale + width / 2, height / 2 - (p[2] - cz) * scale, p[1])
            for p in (a, b, c)
        ]
        (x0, y0, d0), (x1, y1, d1), (x2, y2, d2) = sc
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-12:
            continue
        for yy in range(
            max(int(min(y0, y1, y2)), 0), min(int(max(y0, y1, y2)) + 1, height - 1) + 1
        ):
            for xx in range(
                max(int(min(x0, x1, x2)), 0), min(int(max(x0, x1, x2)) + 1, width - 1) + 1
            ):
                l0 = ((y1 - y2) * (xx - x2) + (x2 - x1) * (yy - y2)) / den
                l1 = ((y2 - y0) * (xx - x2) + (x0 - x2) * (yy - y2)) / den
                l2 = 1.0 - l0 - l1
                if l0 < 0 or l1 < 0 or l2 < 0:
                    continue
                d = l0 * d0 + l1 * d1 + l2 * d2
                k = yy * width + xx
                if d < zbuf[k]:
                    zbuf[k] = d
                    px[3 * k : 3 * k + 3] = colour
    return _png(width, height, px)


def write_previews(stl_parts: Sequence[bytes], directory: Path, prefix: str) -> list[Path]:
    """iso, axial and side previews of the parts together; returns the PNG paths."""
    tris = [t for data in stl_parts for t in load_stl(data)]
    out = []
    for view in VIEWS:
        path = directory / f"{prefix}_{view}.png"
        path.write_bytes(render(tris, view))
        out.append(path)
    return out

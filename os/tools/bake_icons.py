#!/usr/bin/env python3
"""Bake Phosphor Regular SVG icons into a compact C++ polygon table.

    python os/tools/bake_icons.py --fetch     # download/refresh os/icons/ph/*.svg
    python os/tools/bake_icons.py --analyze   # bbox / point statistics only
    python os/tools/bake_icons.py             # regenerate os/src/icons_ph_data.inc

Phosphor (c) Phosphor Icons, MIT.  Regular weight is fill-only (no <path stroke>),
so every glyph is a set of closed contours with no stroke_to_outline step.

Pipeline
  SVG d=  ->  M/L/H/V/C/S/Q/T/A/Z -> cubic beziers -> adaptive flatten (256 grid)
          ->  per-icon optical normalisation into the 24 px box (bbox centred,
              one global scale so the largest glyph spans 0.92 * 24)
          ->  Douglas-Peucker simplify -> 1/16 px fixed point -> C++ array.

Only the standard library is used; the runtime side has no dependencies at all.
"""
import argparse
import json
import math
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OS_DIR = os.path.dirname(HERE)
ROOT = os.path.dirname(OS_DIR)
PH_DIR = os.path.join(OS_DIR, "icons", "ph")
OUT_INC = os.path.join(OS_DIR, "src", "icons_ph_data.inc")

# Phosphor Icons 2.1.1, MIT (LICENSE copy: os/icons/ph/LICENSE)
PHOSPHOR_VERSION = "2.1.1"
PHOSPHOR_URL = ("https://unpkg.com/@phosphor-icons/core@" + PHOSPHOR_VERSION +
                "/assets/regular/%s.svg")

# our id -> Phosphor glyph id.  Order matches icon() in gfx.cpp.
ICONS = [
    ("music",      "music-notes"),
    ("folder",     "folder"),
    ("terminal",   "terminal-window"),
    ("globe",      "globe"),
    ("sliders",    "sliders"),
    ("note",       "file-text"),
    ("grid",       "squares-four"),
    ("wifi",       "wifi-high"),
    ("lock",       "lock"),
    ("power",      "power"),
    ("battery",    "battery-high"),
    ("sd",         "sim-card"),        # replacement: Phosphor has no SD glyph
    ("play",       "play"),
    ("pause",      "pause"),
    ("next",       "skip-forward"),
    ("prev",       "skip-back"),
    ("shuffle",    "shuffle"),
    ("repeat",     "repeat"),
    ("search",     "magnifying-glass"),
    ("plus",       "plus"),
    ("check",      "check"),
    ("close",      "x"),
    ("sun",        "sun"),
    ("volume",     "speaker-high"),
    ("keyboard",   "keyboard"),
    ("bluetooth",  "bluetooth"),
    ("sim",        "sim-card"),
    ("info",       "info"),
    ("back",       "arrow-left"),
    ("chevron",    "caret-right"),     # replacement: Phosphor chevron is lighter
    ("usb",        "usb"),
    ("clock",      "clock"),
    ("airplane",   "airplane"),
    ("storage",    "hard-drives"),
    ("shield",     "shield"),
    ("lab",        "flask"),           # replacement: Phosphor has no test tubes
    ("trash",      "trash"),
    ("restart",    "arrow-clockwise"),
    ("headphones", "headphones"),
]

GRID = 24.0            # device icon grid, px
FIXED = 16             # fixed point: 1/16 px on the 24 grid
OPTICAL_TRIM = 0.92    # largest glyph spans 0.92 * 24 = 22.08 px
FLATTEN_TOL = 0.20     # 256-grid units (~0.019 px on the 24 grid)
SIMPLIFY_TOL = 0.035   # 24-grid px
MIN_AREA = 0.8         # 256-grid units^2, drop hairline debris
COV_MAX = 256          # int16 samples per scanline in the renderer

CMD_RE = re.compile(r"[MmZzLlHhVvCcSsQqTtAa]")
NUM_RE = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
TOK_RE = re.compile(r"[MmZzLlHhVvCcSsQqTtAa]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


# ---------------------------------------------------------------- SVG fetching
def fetch_all():
    os.makedirs(PH_DIR, exist_ok=True)
    seen = {}
    for ours, ph in ICONS:
        if ph in seen:
            src = seen[ph]
        else:
            with urllib.request.urlopen(PHOSPHOR_URL % ph, timeout=30) as r:
                src = r.read().decode("utf-8")
            seen[ph] = src
        path = os.path.join(PH_DIR, ours + ".svg")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(src)
        print("fetched %-11s <- %s" % (ours, ph))

    lic = os.path.join(PH_DIR, "LICENSE")
    try:
        with urllib.request.urlopen(
                "https://unpkg.com/@phosphor-icons/core@" + PHOSPHOR_VERSION +
                "/LICENSE", timeout=30) as r:
            text = r.read().decode("utf-8")
        with open(lic, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("fetched LICENSE")
    except Exception as e:                                  # pragma: no cover
        print("WARN: licence download failed: %s" % e)

    meta = os.path.join(PH_DIR, "SOURCES.json")
    with open(meta, "w", encoding="utf-8", newline="\n") as f:
        json.dump({
            "package": "@phosphor-icons/core",
            "version": PHOSPHOR_VERSION,
            "weight": "regular",
            "license": "MIT",
            "icon": [{"ours": o, "phosphor": p, "url": PHOSPHOR_URL % p}
                     for o, p in ICONS],
        }, f, indent=2)
        f.write("\n")
    print("wrote %s" % os.path.relpath(meta, ROOT))


# ---------------------------------------------------------------- path parsing
def parse_path(d):
    """d= -> list of subpaths (list of (x, y)), flat, 256 grid, y down."""
    tok = TOK_RE.findall(d)
    i = 0
    subpaths = []
    cur = []            # current subpath points
    x = y = 0.0         # current point
    sx = sy = 0.0       # subpath start
    prev_cmd = ""
    prev_c2 = None       # last cubic control point (for S/s)
    prev_q = None       # last quad control point (for T/t)
    cur_x = cur_y = 0.0

    def num():
        nonlocal i
        v = float(tok[i])
        i += 1
        return v

    def flush():
        nonlocal cur
        if len(cur) >= 2:
            pts = cur
            if len(pts) > 2 and pts[0] == pts[-1]:
                pts = pts[:-1]
            if len(pts) >= 3:
                subpaths.append(pts)
        cur = []

    while i < len(tok):
        t = tok[i]
        if CMD_RE.match(t):
            cmd = t
            i += 1
        else:
            # implicit repeat: M->L, m->l, others repeat themselves
            if prev_cmd in ("M", "m"):
                cmd = "L" if prev_cmd == "M" else "l"
            elif prev_cmd in ("Z", "z", ""):
                break                      # stray number after close: stop
            else:
                cmd = prev_cmd
        rel = cmd.islower()
        c = cmd.upper()

        if c == "M":
            x = num(); y = num()
            if rel:
                x += cur_x; y += cur_y
            flush()
            cur = [(x, y)]
            sx, sy = x, y
            prev_c2 = prev_q = None
        elif c == "L":
            x = num(); y = num()
            if rel:
                x += cur_x; y += cur_y
            cur.append((x, y))
            prev_c2 = prev_q = None
        elif c == "H":
            x = num()
            if rel:
                x += cur_x
            cur.append((x, y))
            prev_c2 = prev_q = None
        elif c == "V":
            y = num()
            if rel:
                y += cur_y
            cur.append((x, y))
            prev_c2 = prev_q = None
        elif c == "C":
            x1 = num(); y1 = num(); x2 = num(); y2 = num(); x = num(); y = num()
            if rel:
                x1 += cur_x; y1 += cur_y; x2 += cur_x; y2 += cur_y
                x += cur_x; y += cur_y
            flatten_cubic(cur_x, cur_y, x1, y1, x2, y2, x, y, cur)
            prev_c2 = (x2, y2); prev_q = None
        elif c == "S":
            x2 = num(); y2 = num(); x = num(); y = num()
            if rel:
                x2 += cur_x; y2 += cur_y; x += cur_x; y += cur_y
            if prev_c2 is not None:
                x1 = 2 * cur_x - prev_c2[0]
                y1 = 2 * cur_y - prev_c2[1]
            else:
                x1, y1 = cur_x, cur_y
            flatten_cubic(cur_x, cur_y, x1, y1, x2, y2, x, y, cur)
            prev_c2 = (x2, y2); prev_q = None
        elif c == "Q":
            x1 = num(); y1 = num(); x = num(); y = num()
            if rel:
                x1 += cur_x; y1 += cur_y; x += cur_x; y += cur_y
            flatten_quad(cur_x, cur_y, x1, y1, x, y, cur)
            prev_q = (x1, y1); prev_c2 = None
        elif c == "T":
            x = num(); y = num()
            if rel:
                x += cur_x; y += cur_y
            if prev_q is not None:
                x1 = 2 * cur_x - prev_q[0]
                y1 = 2 * cur_y - prev_q[1]
            else:
                x1, y1 = cur_x, cur_y
            flatten_quad(cur_x, cur_y, x1, y1, x, y, cur)
            prev_q = (x1, y1); prev_c2 = None
        elif c == "A":
            rx = num(); ry = num(); phi = num(); laf = num(); sf = num()
            x = num(); y = num()
            if rel:
                x += cur_x; y += cur_y
            for seg in arc_to_cubics(cur_x, cur_y, rx, ry, phi, laf, sf, x, y):
                flatten_cubic(seg[0], seg[1], seg[2], seg[3], seg[4], seg[5],
                              seg[6], seg[7], cur)
            prev_c2 = prev_q = None
        elif c == "Z":
            if cur:
                cur.append((sx, sy))
            flush()
            x, y = sx, sy
            prev_c2 = prev_q = None
        cur_x, cur_y = x, y
        prev_cmd = cmd
    flush()
    return subpaths


def flatten_cubic(x0, y0, x1, y1, x2, y2, x3, y3, out, depth=0):
    # flatness: distance of the control points from the chord
    d1 = abs((x3 - x0) * (y0 - y1) - (x0 - x1) * (y3 - y0))
    d2 = abs((x3 - x0) * (y0 - y2) - (x0 - x2) * (y3 - y0))
    dd = math.hypot(x3 - x0, y3 - y0)
    if depth >= 18 or (dd > 1e-9 and (d1 + d2) / dd < FLATTEN_TOL * 2):
        out.append((x3, y3))
        return
    ax = (x0 + x1) * 0.5; ay = (y0 + y1) * 0.5
    bx = (x1 + x2) * 0.5; by = (y1 + y2) * 0.5
    cx = (x2 + x3) * 0.5; cy = (y2 + y3) * 0.5
    dx = (ax + bx) * 0.5; dy = (ay + by) * 0.5
    ex = (bx + cx) * 0.5; ey = (by + cy) * 0.5
    fx = (dx + ex) * 0.5; fy = (dy + ey) * 0.5
    flatten_cubic(x0, y0, ax, ay, dx, dy, fx, fy, out, depth + 1)
    flatten_cubic(fx, fy, ex, ey, cx, cy, x3, y3, out, depth + 1)


def flatten_quad(x0, y0, x1, y1, x2, y2, out):
    # exact: quad -> cubic
    c1 = (x0 + 2.0 * (x1 - x0) / 3.0, y0 + 2.0 * (y1 - y0) / 3.0)
    c2 = (x2 + 2.0 * (x1 - x2) / 3.0, y2 + 2.0 * (y1 - y2) / 3.0)
    flatten_cubic(x0, y0, c1[0], c1[1], c2[0], c2[1], x2, y2, out)


def arc_to_cubics(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """SVG endpoint arc -> list of (x0,y0, c1x,c1y, c2x,c2y, x1,y1) cubics."""
    if rx == 0 or ry == 0 or (x1 == x2 and y1 == y2):
        return [(x1, y1, x1, y1, x2, y2, x2, y2)]
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(phi % 360.0)
    cp, sp = math.cos(phi), math.sin(phi)
    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cp * dx2 + sp * dy2
    y1p = -sp * dx2 + cp * dy2
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx *= s; ry *= s
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp = co * rx * y1p / ry
    cyp = -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2.0
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2.0

    def ang(ux, uy, vx, vy):
        d = (ux * vx + uy * vy) / (math.hypot(ux, uy) * math.hypot(vx, vy))
        a = math.acos(max(-1.0, min(1.0, d)))
        return -a if ux * vy - uy * vx < 0 else a

    th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry,
              (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    elif sweep and dth < 0:
        dth += 2 * math.pi
    n = max(1, int(math.ceil(abs(dth) / (math.pi / 2.0))))
    d = dth / n
    k = 4.0 / 3.0 * math.tan(d / 4.0)
    segs = []
    a = th1
    for _ in range(n):
        c1 = (math.cos(a), math.sin(a))
        c2 = (math.cos(a + d), math.sin(a + d))
        p1 = (c1[0] - k * c1[1], c1[1] + k * c1[0])
        p2 = (c2[0] + k * c2[1], c2[1] - k * c2[0])

        def tr(p):
            return (cx + cp * rx * p[0] - sp * ry * p[1],
                    cy + sp * rx * p[0] + cp * ry * p[1])

        q1, q2, q3, q4 = tr(c1), tr(p1), tr(p2), tr(c2)
        if segs:
            segs.append((segs[-1][6], segs[-1][7], q2[0], q2[1], q3[0], q3[1],
                         q4[0], q4[1]))
        else:
            segs.append((x1, y1, q2[0], q2[1], q3[0], q3[1], q4[0], q4[1]))
        a += d
    segs[-1] = segs[-1][:6] + (x2, y2)
    return segs


# ---------------------------------------------------------------- geometry
def polygon_area(pts):
    a = 0.0
    n = len(pts)
    for i in range(n):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % n]
        a += x0 * y1 - x1 * y0
    return a * 0.5


def simplify(pts, tol):
    """Douglas-Peucker for a closed polygon."""
    if len(pts) < 3:
        return pts
    # rotate so the start is the point farthest from the centroid (stable)
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    start = max(range(len(pts)),
                key=lambda i: (pts[i][0] - cx) ** 2 + (pts[i][1] - cy) ** 2)
    pts = pts[start:] + pts[:start]
    # keep the ring closed for the algorithm, then drop the duplicate
    ring = pts + [pts[0]]
    keep = [False] * len(ring)
    keep[0] = keep[-1] = True

    def rdp(lo, hi):
        ax, ay = ring[lo]
        bx, by = ring[hi]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy)
        best, bi = -1.0, -1
        for i in range(lo + 1, hi):
            px, py = ring[i]
            if L < 1e-12:
                d = math.hypot(px - ax, py - ay)
            else:
                d = abs(dx * (ay - py) - (ax - px) * dy) / L
            if d > best:
                best, bi = d, i
        if best > tol and bi > 0:
            keep[bi] = True
            rdp(lo, bi)
            rdp(bi, hi)

    rdp(0, len(ring) - 1)
    out = [p for p, k in zip(ring, keep) if k]
    return out[:-1] if len(out) > 1 else out


def load_icon(name):
    path = os.path.join(PH_DIR, name + ".svg")
    with open(path, "r", encoding="utf-8") as f:
        svg = f.read()
    m = re.search(r'viewBox="([^"]+)"', svg)
    vb = [float(v) for v in re.split(r"[\s,]+", m.group(1).strip())] if m else \
        [0, 0, 256, 256]
    ds = re.findall(r'\sd="([^"]+)"', svg)
    if len(ds) != 1:
        raise SystemExit("%s: expected 1 path, got %d" % (name, len(ds)))
    subs = parse_path(ds[0])
    sx, sy = 256.0 / vb[2], 256.0 / vb[3]
    out = []
    for sub in subs:
        p = [((x - vb[0]) * sx, (y - vb[1]) * sy) for x, y in sub]
        if abs(polygon_area(p)) < MIN_AREA:
            continue
        out.append(p)
    return out


# ---------------------------------------------------------------- winding probe
def max_winding(subs, n=48):
    """How many contour layers pile up (2 means even-odd != nonzero)."""
    best = 0
    for j in range(n):
        y = (j + 0.5) * 256.0 / n
        xs = []
        for sub in subs:
            m = len(sub)
            for i in range(m):
                x0, y0 = sub[i]
                x1, y1 = sub[(i + 1) % m]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    t = (y - y0) / (y1 - y0)
                    xs.append((x0 + t * (x1 - x0), 1 if y1 > y0 else -1))
        xs.sort()
        w = 0
        for _, d in xs:
            w += d
            best = max(best, abs(w))
    return best


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true", help="download Phosphor SVGs")
    ap.add_argument("--analyze", action="store_true", help="print stats only")
    ap.add_argument("--out", default=OUT_INC)
    args = ap.parse_args()

    if args.fetch:
        fetch_all()

    icons = []
    for ours, ph in ICONS:
        subs = load_icon(ours)
        xs = [p[0] for s in subs for p in s]
        ys = [p[1] for s in subs for p in s]
        bb = (min(xs), min(ys), max(xs), max(ys))
        icons.append({
            "name": ours, "ph": ph, "subs": subs, "bb": bb,
            "w": bb[2] - bb[0], "h": bb[3] - bb[1],
            "winding": max_winding(subs),
        })

    maxdim = max(max(i["w"], i["h"]) for i in icons)
    scale = GRID * OPTICAL_TRIM / maxdim

    if args.analyze:
        print("%-11s %-14s %7s %7s %6s %8s %5s" %
              ("our id", "phosphor", "w", "h", "fill", "pts", "wind"))
        for i in icons:
            npts = sum(len(s) for s in i["subs"])
            print("%-11s %-14s %7.1f %7.1f %5.2f%% %8d %5d" %
                  (i["name"], i["ph"], i["w"], i["h"],
                   100.0 * (i["w"] * i["h"]) / (256.0 * 256.0), npts,
                   i["winding"]))
        print("\nmax bbox dim %.1f (256 grid) -> scale %.5f, box span %.2f px" %
              (maxdim, scale, maxdim * scale))
        print("median bbox dim %.1f -> %.2f px of 24" %
              (sorted(max(i["w"], i["h"]) for i in icons)[len(icons) // 2],
               sorted(max(i["w"], i["h"]) for i in icons)[len(icons) // 2] * scale))
        return

    # -------------------------------------------------- normalise + emit
    total_pts = total_polys = 0
    worst_dev = 0.0
    worst_name = ""
    lines = []
    tables = []
    for i, ic in enumerate(icons):
        cx = (ic["bb"][0] + ic["bb"][2]) / 2.0
        cy = (ic["bb"][1] + ic["bb"][3]) / 2.0
        polys = []
        src_area = 0.0
        for sub in ic["subs"]:
            src_area += abs(polygon_area(sub))
            p = [((x - cx) * scale + GRID / 2.0, (y - cy) * scale + GRID / 2.0)
                 for x, y in sub]
            p = simplify(p, SIMPLIFY_TOL)
            if len(p) < 3 or abs(polygon_area(p)) < 0.05:
                continue
            q = []
            for x, y in p:
                fx = int(round(x * FIXED))
                fy = int(round(y * FIXED))
                if q and q[-1] == (fx, fy):
                    continue
                q.append((fx, fy))
            if len(q) >= 3 and q[0] == q[-1]:
                q.pop()
            if len(q) < 3:
                continue
            polys.append(q)
        if not polys:
            raise SystemExit("%s: empty after bake" % ic["name"])
        if len(polys) > 255:
            raise SystemExit("%s: too many contours (%d)" % (ic["name"], len(polys)))
        # fidelity: the baked area should match the source area within a fraction
        # of a pixel; a big deviation means the flatten/simplify mangled a contour.
        baked_area = sum(abs(polygon_area(p)) for p in polys) / (FIXED * FIXED)
        ref_area = src_area * scale * scale
        dev = abs(baked_area - ref_area) / max(ref_area, 1e-9)
        if dev > worst_dev:
            worst_dev, worst_name = dev, ic["name"]
        flat = [c for p in polys for xy in p for c in xy]
        arr = "s_" + ic["name"] + "_pts"
        body = []
        for k in range(0, len(flat), 16):
            body.append("    " + ", ".join("%d" % v for v in flat[k:k + 16]) + ",")
        lines.append("static const int16_t %s[] = {\n%s\n};\n" %
                     (arr, "\n".join(body)))
        offs = []
        off = 0
        for p in polys:
            offs.append((arr, off, len(p)))
            off += len(p)
        tables.append((ic["name"], offs))
        total_pts += len([1 for p in polys for _ in p])
        total_polys += len(polys)

    parts = []
    parts.append("""// GENERATED by os/tools/bake_icons.py -- do not edit by hand.
// Phosphor Icons %s (MIT), Regular weight.  Sources: os/icons/ph/*.svg
// %d icons, %d contours, %d points, %d bytes of vertex data.
// Fixed point: 1/%d px on the 24 px icon grid, y down, no strokes (fill only).
""" % (PHOSPHOR_VERSION, len(icons), total_polys, total_pts,
       2 * total_pts, FIXED))
    parts.append("\n".join(lines))
    parts.append("// contour tables: flat-array offset (x,y pairs) and vertex count")
    for name, offs in tables:
        parts.append("static const IconPoly s_%s[] = {\n%s\n};\n" % (
            name, "\n".join("    { %s + %d, %d }," % (n, 2 * o, k)
                            for n, o, k in offs)))
    parts.append("static const IconEnt s_icons[] = {\n%s\n};\n" % "\n".join(
        '    { "%s", s_%s, %d },' % (n, n, len(o)) for n, o in tables))
    parts.append("static const int s_icon_count = %d;\n" % len(icons))

    text = "\n".join(parts)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s: %d icons, %d contours, %d points, %.1f KB" %
          (os.path.relpath(args.out, ROOT), len(icons), total_polys, total_pts,
           len(text) / 1024.0))
    print("area fidelity: worst %.2f%% (%s)" % (100.0 * worst_dev, worst_name))
    if worst_dev > 0.02:
        print("WARN: %s lost %.1f%% of its area in the bake" %
              (worst_name, 100.0 * worst_dev))


if __name__ == "__main__":
    main()

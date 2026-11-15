#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Gigstart2.jpg (a line drawing) -> firmware/gen/orgigs_head.h: the head as polylines for the welcome screen.

    python3 tools/gen_head_lines.py assets/gigstart2.jpg [preview.ppm]

Needs Pillow (only to read the JPEG). The ink is thresholded, thinned to one-pixel lines (Zhang-Suen),
traced into polylines, simplified (Ramer-Douglas-Peucker) and scaled into the 240 x 240 screen.
The header holds the points as bytes: per line its length, then x, y pairs. Lines are sorted top to
bottom, so the welcome screen draws the hair first and the chin last."""
import os
import sys

from PIL import Image, ImageFilter

H_OUT = 214          # height of the head on the screen (px)
WORK_H = 430         # the working grid: about 2 x the output, so thin lines survive
INK = int(os.environ.get("HEAD_INK", 225))   # grey below this is ink, after the lines were thickened
MIN_LEN = 5.0        # shorter lines (on screen, px) are dropped
EPS = 0.55           # RDP tolerance (on screen, px)


def load(path):
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    # the bounding box of the ink
    x0, y0, x1, y1 = w, h, 0, 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            if px[x, y] < 160:
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    pad = 8
    box = (max(0, x0 - pad), max(0, y0 - pad), min(w, x1 + pad), min(h, y1 + pad))
    im = im.crop(box).filter(ImageFilter.MinFilter(int(os.environ.get("HEAD_THICK", 5))))   # faint lines survive the scaling
    s = WORK_H / im.size[1]
    im = im.resize((max(1, round(im.size[0] * s)), WORK_H), Image.LANCZOS)
    return im


def binarise(im):
    w, h = im.size
    px = im.load()
    return [[1 if px[x, y] < INK else 0 for x in range(w)] for y in range(h)], w, h


def thin(g, w, h):
    """Zhang-Suen thinning, in place."""
    def nb(y, x):
        return [g[y - 1][x], g[y - 1][x + 1], g[y][x + 1], g[y + 1][x + 1],
                g[y + 1][x], g[y + 1][x - 1], g[y][x - 1], g[y - 1][x - 1]]
    changed = True
    while changed:
        changed = False
        for step in (0, 1):
            kill = []
            for y in range(1, h - 1):
                row = g[y]
                for x in range(1, w - 1):
                    if not row[x]:
                        continue
                    p = nb(y, x)
                    b = sum(p)
                    if b < 2 or b > 6:
                        continue
                    a = sum(1 for i in range(8) if p[i] == 0 and p[(i + 1) % 8] == 1)
                    if a != 1:
                        continue
                    if step == 0:
                        if p[0] * p[2] * p[4] or p[2] * p[4] * p[6]:
                            continue
                    else:
                        if p[0] * p[2] * p[6] or p[0] * p[4] * p[6]:
                            continue
                    kill.append((y, x))
            for y, x in kill:
                g[y][x] = 0
            changed = changed or bool(kill)


N8 = [(-1, 0), (0, 1), (1, 0), (0, -1), (-1, 1), (1, 1), (1, -1), (-1, -1)]   # straight first


def trace(g, w, h):
    """The skeleton as polylines: from every end point (and junction), then the closed loops."""
    def nbrs(y, x):
        return [(y + dy, x + dx) for dy, dx in N8
                if 0 <= y + dy < h and 0 <= x + dx < w and g[y + dy][x + dx]]
    used = set()
    lines = []

    def walk(start):
        line = [start]
        used.add(start)
        cur = start
        while True:
            nxt = [p for p in nbrs(*cur) if p not in used]
            if not nxt:
                break
            cur = nxt[0]
            used.add(cur)
            line.append(cur)
        return line

    pts = [(y, x) for y in range(h) for x in range(w) if g[y][x]]
    ends = [p for p in pts if len(nbrs(*p)) == 1]
    for p in ends + [p for p in pts if len(nbrs(*p)) > 2] + pts:
        if p in used:
            continue
        ln = walk(p)
        if len(ln) > 1:
            lines.append(ln)
    return lines


def rdp(pts, eps):
    if len(pts) < 3:
        return pts
    (ay, ax), (by, bx) = pts[0], pts[-1]
    dy, dx = by - ay, bx - ax
    n = (dx * dx + dy * dy) ** 0.5
    best, bi = -1.0, 0
    for i in range(1, len(pts) - 1):
        py, px = pts[i]
        d = abs(dx * (ay - py) - dy * (ax - px)) / n if n else ((py - ay) ** 2 + (px - ax) ** 2) ** 0.5
        if d > best:
            best, bi = d, i
    if best <= eps:
        return [pts[0], pts[-1]]
    return rdp(pts[:bi + 1], eps)[:-1] + rdp(pts[bi:], eps)


def length(pts):
    return sum(((pts[i][0] - pts[i - 1][0]) ** 2 + (pts[i][1] - pts[i - 1][1]) ** 2) ** 0.5 for i in range(1, len(pts)))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    im = load(sys.argv[1])
    g, w, h = binarise(im)
    thin(g, w, h)
    raw = trace(g, w, h)
    s = H_OUT / h
    ox = (240 - w * s) / 2
    oy = 240 - H_OUT - 6
    out = []
    for ln in raw:
        p = [(y * s + oy, x * s + ox) for y, x in ln]
        if length(p) < MIN_LEN:
            continue
        p = rdp(p, EPS)
        q = []
        for y, x in p:
            v = (max(0, min(239, round(x))), max(0, min(239, round(y))))
            if not q or q[-1] != v:
                q.append(v)
        if len(q) >= 2:
            out.append(q)
    out.sort(key=lambda q: min(y for _, y in q))
    data, nparts = [], 0
    for q in out:
        while q:                                   # at most 255 points a line
            part, q = q[:255], q[254:] if len(q) > 255 else []
            data.append(len(part))
            nparts += 1
            for x, y in part:
                data += [x, y]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    npts = sum(len(q) for q in out)
    lines = ["/* generated by tools/gen_head_lines.py from Gigstart2.jpg: do not edit */",
             "#define HEAD_NLINES %du" % nparts, "#define HEAD_NPTS %du" % npts,
             "static const uint8_t HEAD_LINES[%d] = {" % len(data)]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("%d" % v for v in data[i:i + 16]) + ",")
    lines.append("};")
    open(os.path.join(root, "firmware", "gen", "orgigs_head.h"), "w").write("\n".join(lines) + "\n")
    print("firmware/gen/orgigs_head.h: %d lines, %d points, %d bytes" % (len(out), npts, len(data)))
    if len(sys.argv) > 2:                         # a preview: Bresenham, as the firmware draws
        fb = [[0] * 240 for _ in range(240)]
        for q in out:
            for (x0, y0), (x1, y1) in zip(q, q[1:]):
                dx, dy = abs(x1 - x0), -abs(y1 - y0)
                sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
                e = dx + dy
                while True:
                    fb[y0][x0] = 1
                    if x0 == x1 and y0 == y1:
                        break
                    e2 = 2 * e
                    if e2 >= dy:
                        e += dy; x0 += sx
                    if e2 <= dx:
                        e += dx; y0 += sy
        with open(sys.argv[2], "wb") as f:
            f.write(b"P6 240 240 255\n")
            for row in fb:
                f.write(bytes(v for c in row for v in ((236, 230, 210) if c else (10, 10, 14))))


if __name__ == "__main__":
    main()

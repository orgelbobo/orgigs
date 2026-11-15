#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The OrGigs lettering (assets/orgigs_logo.jpg, cut from "Orgigs-Logo Idee.jpg") -> firmware/gen/orgigs_logo.h:
O r G i g s in their colours, for the Elite's welcome screen (on its black ground: 0 is not drawn, and the black G
gets a grey rim, colour 7).

    python3 tools/gen_logo.py assets/orgigs_logo.jpg [preview.png]
    python3 tools/gen_logo.py --cover assets/orgigs_logo.jpg docs/manual/img/cover-logo.png   (the manual's cover)

Needs Pillow (only to read the picture). Each pixel of the picture takes the nearest of the seven colours of the
template's palette; each screen pixel (the picture scaled to LOGO_W) the colour most of its picture pixels have (0 the ground, 1..6 the letters, so a letter's colour is also its number). The
header keeps it as runs, row after row: one byte each, colour in the top 3 bits, length - 1 in the low 5 (rows end
where their runs add up to LOGO_W). About 2 KB."""
import sys

from PIL import Image

LOGO_W = 216
# the template's palette (Orgigs logo beispiel.py): ground, red O, yellow r, black G, white i, brown g, green s
PAL = [(244, 241, 230), (155, 26, 16), (218, 154, 23), (0, 0, 0), (255, 255, 255), (96, 62, 41), (19, 86, 41),
       (120, 120, 128)]                         # 7: the G's rim (the screen's ground is black)
# what the picture's JPEG colours are, for telling them apart (measured; the red and the brown a little off the palette)
SEEN = [(243, 240, 225), (165, 32, 17), (220, 157, 16), (12, 12, 12), (254, 253, 254), (91, 59, 36), (37, 84, 38)]


def nearest(c):
    return min(range(len(SEEN)), key=lambda i: sum((a - b) ** 2 for a, b in zip(c, SEEN[i])))


def cover(src, dst):
    """the lettering for the manual's cover: the picture's size, its ground transparent, the G's rim (5 px) grey"""
    im = Image.open(src).convert("RGB")
    w, h = im.size
    px = im.load()
    cls = [[nearest(px[x, y]) for x in range(w)] for y in range(h)]
    rim = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if cls[y][x] == 3:
                for yy in range(max(0, y - 5), min(h, y + 6)):
                    for xx in range(max(0, x - 5), min(w, x + 6)):
                        if cls[yy][xx] == 0 and (yy - y) ** 2 + (xx - x) ** 2 <= 25:
                            rim[yy][xx] = True
    out = Image.new("RGBA", (w, h))
    out.putdata([(PAL[7] + (255,)) if rim[y][x] else (0, 0, 0, 0) if cls[y][x] == 0 else (PAL[cls[y][x]] + (255,))
                 for y in range(h) for x in range(w)])
    out = out.crop(out.getbbox())
    out.save(dst, optimize=True)
    print("cover %d x %d -> %s" % (out.size[0], out.size[1], dst))


def main():
    if sys.argv[1] == "--cover":
        cover(sys.argv[2], sys.argv[3])
        return
    src = sys.argv[1]
    im = Image.open(src).convert("RGB")
    if im.size[0] > 750:                        # (from 750 px: less of the JPEG's noise survives the vote)
        im = im.resize((750, round(750 * im.size[1] / im.size[0])), Image.LANCZOS)
    w, h = im.size
    px = im.load()
    x0, y0, x1, y1 = w, h, 0, 0                 # the letters' bounding box
    for y in range(h):
        for x in range(w):
            if nearest(px[x, y]) != 0:
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    cw, ch = x1 + 1 - x0, y1 + 1 - y0
    cls = [[nearest(px[x0 + x, y0 + y]) for x in range(cw)] for y in range(ch)]
    oh = round(LOGO_W * ch / cw)
    rows = []
    for oy in range(oh):                        # each screen pixel: the colour most of its picture pixels have
        ya, yb = oy * ch // oh, max(oy * ch // oh + 1, (oy + 1) * ch // oh)
        row = []
        for ox in range(LOGO_W):
            xa, xb = ox * cw // LOGO_W, max(ox * cw // LOGO_W + 1, (ox + 1) * cw // LOGO_W)
            n = [0] * len(PAL)
            for y in range(ya, yb):
                for x in range(xa, xb):
                    n[cls[y][x]] += 1
            row.append(max(range(len(PAL)), key=lambda i: n[i]))
        rows.append(row)
    for y in range(oh):                         # a lone pixel of another letter's colour (a JPEG fringe): the ground
        for x in range(LOGO_W):
            c = rows[y][x]
            nb = [rows[yy][xx] for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)) if 0 <= yy < oh and 0 <= xx < LOGO_W]
            if c and c not in nb:
                rows[y][x] = max(set(nb), key=nb.count)
    for y in range(oh):                         # 7: the ground round the black G, its rim (so it shows on black)
        for x in range(LOGO_W):
            if rows[y][x] == 0 and any(0 <= y + dy < oh and 0 <= x + dx < LOGO_W and rows[y + dy][x + dx] == 3
                                       for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                rows[y][x] = 7
    out = []
    for r in rows:
        x = 0
        while x < LOGO_W:
            c, n = r[x], 1
            while x + n < LOGO_W and r[x + n] == c and n < 32:
                n += 1
            out.append(c << 5 | (n - 1))
            x += n
    with open("firmware/gen/orgigs_logo.h", "w") as f:
        f.write("/* generated by tools/gen_logo.py from %s: the OrGigs lettering, runs (colour << 5 | length - 1) */\n"
                % src.split("/")[-1])
        f.write("#define LOGO_W %du\n#define LOGO_H %du\n" % (LOGO_W, oh))
        f.write("static const uint8_t LOGO_PAL[8][3] = {%s};\n" % ", ".join("{%d, %d, %d}" % c for c in PAL))
        f.write("static const uint8_t LOGO_RUNS[%d] = {\n" % len(out))
        for i in range(0, len(out), 24):
            f.write("    " + ", ".join(str(v) for v in out[i:i + 24]) + ",\n")
        f.write("};\n")
    print("logo %d x %d: %d runs, %d B" % (LOGO_W, oh, len(out), len(out)))
    if len(sys.argv) > 2:
        pv = Image.new("RGB", (LOGO_W, oh))
        pv.putdata([PAL[c] for r in rows for c in r])
        pv.resize((LOGO_W * 3, oh * 3), Image.NEAREST).save(sys.argv[2])


if __name__ == "__main__":
    main()

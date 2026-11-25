/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs 2's look, for both boards' screens (style C, docs/WISHLIST.md): smoked glass behind, realistic drawbars with
 * engraved numbers, lamps, knobs with a ring of lamps, the panel's buttons as keycaps. Everything blends onto the
 * canvas (src/gfx.c); positions and sizes in 1/16 px (Q4) where they need it. No FPU: integers only. */
#include "orgigs_look.h"

#define Q4(v) ((int32_t)((v) * 16))

static int32_t lk_sy;                  /* the canvas's screen row (the panel behind it is drawn from there) */
static int32_t lk_x0, lk_x1 = 9999;    /* lk_put draws only from lk_x0 to before lk_x1 (a part redrawn: a margin) */

/* a canvas at screen row sy, the glass behind its part between LOOK_Y0 and LOOK_Y1, bg elsewhere */
static void lk_begin(uint32_t w, uint32_t h, uint32_t sx, uint32_t sy, uint16_t bg)
{
    uint32_t y, x;
    cv_begin(w, h, bg);
    lk_sy = (int32_t)sy;
    for (y = 0; y < cv_h; y++) {
        uint32_t r = sy + y;
        const uint16_t *p;
        if (r < LOOK_Y0 || r >= LOOK_Y1) continue;
        p = LOOK_PANEL + (r - LOOK_Y0) * 240u + sx;
        for (x = 0; x < cv_w && sx + x < 240u; x++) cv_px[y * cv_w + x] = swap16(p[x]);
    }
}

/* colours: t / k in 1/256 */
static uint16_t lk_mix(uint16_t a, uint16_t b, uint32_t t)
{
    int32_t r = (int32_t)(a >> 11), g = (int32_t)(a >> 5 & 63u), bl = (int32_t)(a & 31u), tt = (int32_t)t;
    r += (((int32_t)(b >> 11) - r) * tt) >> 8;
    g += (((int32_t)(b >> 5 & 63u) - g) * tt) >> 8;
    bl += (((int32_t)(b & 31u) - bl) * tt) >> 8;
    return (uint16_t)((uint32_t)r << 11 | (uint32_t)g << 5 | (uint32_t)bl);
}
static uint16_t lk_mul(uint16_t c, uint32_t k)
{
    uint32_t r = (c >> 11) * k >> 8, g = (c >> 5 & 63u) * k >> 8, b = (c & 31u) * k >> 8;
    return (uint16_t)((r > 31u ? 31u : r) << 11 | (g > 63u ? 63u : g) << 5 | (b > 31u ? 31u : b));
}
#define LK_WHITE 0xFFFFu
#define LK_LIGHT(c, t) lk_mix(c, LK_WHITE, t)

/* a pixel of c over the canvas, a in 1/256 */
static void lk_put(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    if (x < lk_x0 || x >= lk_x1) return;
    if (a > 255u) a = 255u;
    cv_blend(x, y, c, (a * 15u + 128u) >> 8);
}
static void lk_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t a)
{
    int32_t i, j;
    if (a >= 255u) { cv_rect(x, y, w, h, c); return; }
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) lk_put(i, j, c, a);
}

static uint32_t lk_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* a disc around (cx, cy), radius r (all Q4), edges smoothed */
static void lk_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    int32_t x, y;
    for (y = (cy - r) / 16 - 1; y <= (cy + r) / 16 + 1; y++)
        for (x = (cx - r) / 16 - 1; x <= (cx + r) / 16 + 1; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy, cov;
            cov = r - (int32_t)lk_isqrt((uint32_t)(dx * dx + dy * dy)) + 8;
            if (cov <= 0) continue;
            lk_put(x, y, c, cov >= 16 ? a : a * (uint32_t)cov / 16u);
        }
}
/* a soft halo: falls off as (1 - d/r)^2 */
static void lk_glow(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    int32_t x, y;
    for (y = (cy - r) / 16; y <= (cy + r) / 16; y++)
        for (x = (cx - r) / 16; x <= (cx + r) / 16; x++) {
            int32_t dx = x * 16 + 8 - cx, dy = y * 16 + 8 - cy, d = (int32_t)lk_isqrt((uint32_t)(dx * dx + dy * dy)), f;
            if (d >= r) continue;
            f = (r - d) * 256 / r;
            lk_put(x, y, c, a * (uint32_t)(f * f >> 8) >> 8);
        }
}
/* a short line (up to ~40 px) of width w (Q4), ends rounded */
static void lk_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint16_t c)
{
    int32_t vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy, x, y;
    int32_t lo_x = (x0 < x1 ? x0 : x1) - w, hi_x = (x0 > x1 ? x0 : x1) + w;
    int32_t lo_y = (y0 < y1 ? y0 : y1) - w, hi_y = (y0 > y1 ? y0 : y1) + w;
    if (!l2) l2 = 1;
    for (y = lo_y / 16 - 1; y <= hi_y / 16 + 1; y++)
        for (x = lo_x / 16 - 1; x <= hi_x / 16 + 1; x++) {
            int32_t px = x * 16 + 8 - x0, py = y * 16 + 8 - y0, t = (px * vx + py * vy) * 64 / l2, ex, ey, d, cov;
            if (t < 0) t = 0;
            if (t > 64) t = 64;
            ex = px - vx * t / 64;
            ey = py - vy * t / 64;
            d = (int32_t)lk_isqrt((uint32_t)(ex * ex + ey * ey));
            cov = w / 2 - d + 8;
            if (cov <= 0) continue;
            lk_put(x, y, c, cov >= 16 ? 255u : (uint32_t)cov * 16u);
        }
}

/* a rounded rectangle, top colour to bottom colour, corner radius r px */
static void lk_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t top, uint16_t bot, uint32_t a)
{
    int32_t i, j;
    for (j = 0; j < h; j++) {
        uint16_t c = h > 1 ? lk_mix(top, bot, (uint32_t)(j * 256 / (h - 1))) : top;
        for (i = 0; i < w; i++) {
            int32_t dx = 0, dy = 0, cov = 16;
            if (i < r) dx = (r - i) * 16 - 8; else if (i >= w - r) dx = (i - (w - r)) * 16 + 8;
            if (j < r) dy = (r - j) * 16 - 8; else if (j >= h - r) dy = (j - (h - r)) * 16 + 8;
            if (dx && dy) {
                cov = r * 16 - (int32_t)lk_isqrt((uint32_t)(dx * dx + dy * dy)) + 8;
                if (cov <= 0) continue;
                if (cov > 16) cov = 16;
            }
            lk_put(x + i, y + j, c, a * (uint32_t)cov / 16u);
        }
    }
}

/* a point on the knobs' scale: 7 o'clock (v 0) to 5 o'clock (v 127), radius r (Q4) around (cx, cy) */
static void lk_arc_pt(int32_t cx, int32_t cy, int32_t r, uint32_t v, int32_t *x, int32_t *y)
{
    uint32_t ph = 0xA0000000u + v * (0xC0000000u / 127u);
    *x = cx + ((r * org_sin(ph)) >> 15);
    *y = cy - ((r * org_sin(ph + 0x40000000u)) >> 15);
}

/* ---- the parts */

/* a lamp: a bezel, the glass lit (a halo, a shine) or dark; r px */
static void lk_lamp(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t on)
{
    lk_disc(cx, cy, r + 24, RGB(70, 70, 76), 255);
    lk_disc(cx, cy, r, on ? c : lk_mul(c, 41), 255);
    if (on) {
        lk_glow(cx, cy, r * 26 / 10, c, 102);
        lk_disc(cx - r * 3 / 10, cy - r * 3 / 10, r * 45 / 100, LK_WHITE, 140);
    } else
        lk_disc(cx - r * 3 / 10, cy - r * 3 / 10, r * 3 / 10, LK_WHITE, 31);
}

/* a knob with a ring of n lamps around it (the value lit up to v of 127), radius ring (Q4), cap radius cap (Q4);
   lit: the knobs hold it (the lamps glow, the pointer bright) */
static void lk_knob(int32_t cx, int32_t cy, int32_t ring, int32_t cap, uint32_t n, uint32_t v, uint16_t c, uint32_t lit,
                    uint16_t ptr)
{
    uint32_t i;
    int32_t x0, y0, x1, y1, lr = ring > Q4(13) ? Q4(1.8) : Q4(1.4);
    for (i = 0; i < n; i++) {
        uint32_t on = v && i * 127u <= v * (n - 1u) + 1u;
        int32_t lx, ly;
        lk_arc_pt(cx, cy, ring, i * 127u / (n - 1u), &lx, &ly);
        if (on && lit) {
            lk_glow(lx, ly, lr * 25 / 10, c, 90);
            lk_disc(lx, ly, lr, LK_LIGHT(c, 77), 255);
        } else
            lk_disc(lx, ly, lr * 9 / 10, on ? lk_mul(c, 140) : lk_mul(c, 46), 255);
    }
    lk_disc(cx, cy, cap + 16, RGB(24, 24, 28), 255);
    lk_disc(cx, cy, cap, RGB(48, 48, 54), 255);
    lk_disc(cx - cap / 4, cy - cap / 3, cap / 2, LK_WHITE, 18);
    lk_arc_pt(cx, cy, cap / 4, v, &x0, &y0);
    lk_arc_pt(cx, cy, cap * 8 / 10, v, &x1, &y1);
    lk_line(x0, y0, x1, y1, Q4(1.5), ptr);
}

/* a keycap (the panel's buttons mirrored): amber when on */
static void lk_key(int32_t x, int32_t y, int32_t w, int32_t h, const char *s, uint32_t on, uint16_t txt)
{
    lk_rect(x + 1, y + h, w - 1, 2, C_BLACK, 128);
    lk_rrect(x, y, w, h, 4, on ? RGB(255, 210, 120) : RGB(58, 58, 66), on ? RGB(220, 140, 30) : RGB(30, 30, 36), 255);
    if (on) lk_rect(x + 3, y + 1, w - 6, 1, LK_WHITE, 70);
    cv_ctext(x + w / 2, y + (h - 16) / 2, w - 4, &FONT_S, s, on ? C_BLACK : txt);
}

/* a badge in the top bar: a rounded box behind the text; returns the end x */
static int32_t lk_badge(int32_t x, const char *s, uint16_t top, uint16_t bot, uint16_t txt)
{
    lk_rrect(x - 3, 3, text_w(&FONT_S, s) + 6, 18, 4, top, bot, 255);
    return cv_text(x, 4, &FONT_S, s, txt);
}

/* a 3 x 5 digit, engraved on a drawbar */
static const uint16_t LK_D35[10] = {075557, 026227, 071747, 071717, 055711, 074717, 074757, 071222, 075757, 075717};
static void lk_digit35(int32_t x, int32_t y, uint32_t d, uint16_t c, uint32_t a)
{
    uint32_t i;
    for (i = 0; i < 15u; i++)
        if (LK_D35[d % 10u] >> (14u - i) & 1u) lk_put(x + (int32_t)(i % 3u), y + (int32_t)(i / 3u), c, a);
}

/* a drawbar's round shaft, len px from row top, bw px wide: shaded across, a shine at a third */
static void lk_shaft(int32_t x, int32_t top, int32_t bw, int32_t len, uint16_t base)
{
    int32_t i;
    for (i = 0; i < bw; i++) {                                      /* one column at a time */
        uint32_t u = (uint32_t)(i * 64 / bw);
        cv_rect(x + i, top, 1, len, lk_mix(lk_mul(base, LOOK_SH[u]), RGB(255, 255, 250), LOOK_SP[u]));
    }
}
/* the cap at the shaft's end (row y), its shadow under it */
static void lk_cap(int32_t x, int32_t y, int32_t bw, uint16_t base)
{
    lk_rect(x - 2, y + 9, bw + 4, 4, C_BLACK, 115);
    lk_rrect(x - 2, y - 1, bw + 4, 11, 4, LK_LIGHT(base, 115), lk_mul(base, 140), 255);
    lk_rect(x, y, bw, 1, LK_WHITE, 90);
}

/* a drawbar pulled out to lvl (0..8) from row top: a round shaft with engraved grooves and numbers, a cap; bw px
   wide, each step 11 px */
static void lk_drawbar(int32_t x, int32_t top, int32_t bw, uint32_t lvl, uint16_t base)
{
    int32_t len = 8 + (int32_t)lvl * 11, slot = x + bw / 2, j;
    uint32_t light = (base >> 11) + (base >> 5 & 63u) / 2u > 40u;   /* light shafts: dark numbers */
    lk_rect(slot - 2, top, 4, 102, RGB(3, 3, 3), 255);              /* the slot */
    lk_rect(slot + 2, top, 1, 102, RGB(60, 55, 50), 150);
    lk_shaft(x, top, bw, len, base);
    for (j = 1; j <= (int32_t)lvl; j++) {                          /* engraved: a groove and the number */
        int32_t gy = top + 8 + j * 11 - 2;
        lk_rect(x + 1, gy, bw - 2, 1, C_BLACK, 140);
        lk_rect(x + 1, gy + 1, bw - 2, 1, LK_WHITE, 31);
        lk_digit35(x + bw / 2 - 1, gy - 8, (uint32_t)j, light ? RGB(40, 30, 20) : RGB(230, 220, 200), 217);
    }
    lk_cap(x, top + len, bw, base);
}

/* a small drawbar (the other manuals): a shaft lvl * 3 px long */
static void lk_minibar(int32_t x, int32_t y, uint32_t lvl, uint16_t base)
{
    int32_t i;
    lk_rect(x + 2, y, 1, 24, C_BLACK, 255);
    for (i = 0; i < 5; i++) cv_rect(x + i, y, 1, (int32_t)lvl * 3, lk_mul(base, LOOK_SH[(uint32_t)i * 64u / 5u] + 20u));
}

/* a lamp switch: the name over it, the setting under it (lit in its colour when on) */
static void lk_switch(int32_t x, int32_t y, int32_t w, uint16_t c, uint32_t on, const char *a, const char *b)
{
    cv_ctext(x + w / 2, y, w - 2, &FONT_S, a, on ? C_TXT : C_MUT);
    lk_lamp(Q4(x) + Q4(w) / 2, Q4(y + 26), Q4(6), c, on);
    cv_ctext(x + w / 2, y + 38, w - 2, &FONT_S, b, on ? c : C_MUT);
}

/* text from x, squeezed (up to 2 px a letter) to fit w; returns the end x */
static int32_t lk_ltext(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c)
{
    for (cv_sq = 0; cv_sq < 2u && text_w(&FONT_S, s) > w; cv_sq++)
        ;
    x = cv_text(x, y, &FONT_S, s, c);
    cv_sq = 0;
    return x;
}

/* the row the knobs hold: a warm glass strip behind it */
static void lk_focus(int32_t x, int32_t y, int32_t w, int32_t h)
{
    lk_rrect(x, y, w, h, 5, RGB(60, 50, 30), RGB(40, 34, 22), 140);
}

/* a lamp with a name under it (a row of choices: pads 1..n), centred on cx in a canvas row from y */
static void lk_choice(int32_t cx, int32_t y, int32_t w, const char *s, uint32_t on, uint16_t c, uint16_t off)
{
    lk_lamp(Q4(cx), Q4(y + 4), Q4(3.5), c, on);
    cv_ctext(cx, y + 9, w, &FONT_S, s, on ? c : off);
}

/* a pad's switch (the pads mirrored): a keycap lit in the pad's colour when on */
static void lk_pad(int32_t x, int32_t y, int32_t w, int32_t h, const char *s, uint32_t on, uint16_t c)
{
    lk_rect(x + 1, y + h, w - 1, 2, C_BLACK, 128);
    lk_rrect(x, y, w, h, 4, on ? LK_LIGHT(c, 90) : RGB(58, 58, 66), on ? lk_mul(c, 190) : RGB(30, 30, 36), 255);
    uint16_t t = on ? C_BLACK : C_TXT;
    uint32_t n = 0;
    while (s[n] && s[n] != '\n') n++;
    if (s[n] && n < 8u) {                       /* two lines: the caps are 9 px, so they fit a 25 px pad */
        char a[8];
        uint32_t i;
        for (i = 0; i < n; i++) a[i] = s[i];
        a[n] = 0;
        cv_ctext(x + w / 2, y + h / 2 - 15, w - 2, &FONT_S, a, t);
        cv_ctext(x + w / 2, y + h / 2 - 4, w - 2, &FONT_S, s + n + 1, t);
    } else {
        cv_ctext(x + w / 2, y + (h - 16) / 2, w - 2, &FONT_S, s, t);
    }
}

/* a piece of drawbar shaft, len px long (no cap): the small ones */
static void lk_drawbar_part(int32_t x, int32_t y, int32_t bw, int32_t len, uint16_t base) { lk_shaft(x, y, bw, len, base); }

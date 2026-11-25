/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs: the welcome screen (main loop), about 3.2 s. Included by orgigs_ui.c, the Elite's and the FM-1's.
 *
 *   0    .. 1100 ms  the head draws itself, line by line, hair first (Gigstart2.jpg as polylines)
 *   1100 .. 1400 ms  it rests
 *   1400 .. 2100 ms  the head sinks out of the screen; from the top, out of its hair, the nine drawbars of the
 *                    manual's cover grow down
 *   2100 .. 2500 ms  O r G i g s letter by letter, as the logo (Orgigs-Logo Idee.jpg; the black G with a grey rim)
 *   .. 3200 ms       hold
 * All the while (the Elite) the pads: a wave of drawbars pulled out runs from left to right and back, through the colours.
 *
 * Any key or button ends it at once (and does nothing else).
 *
 * The head is out of the release (ORGIGS_INTRO_HEAD=0, the default): the screen starts with the drawbars growing
 * down, 1400 ms sooner. ./build.sh --intro-head (or ORGIGS_CFLAGS=-DORGIGS_INTRO_HEAD=1) brings it back. */
#ifndef ORGIGS_INTRO_HEAD
#define ORGIGS_INTRO_HEAD 0
#endif
#if ORGIGS_INTRO_HEAD
#include "orgigs_head.h"
#endif
#include "orgigs_logo.h"

#define INTRO_MS 3200u
#define INTRO_DRAW 1100u                /* the head draws itself */
#define INTRO_FALL 1400u                /* then sinks */
#define INTRO_SKIP (ORGIGS_INTRO_HEAD ? 0u : INTRO_FALL)   /* (no head: its part skipped) */
#define LOGO_X ((240u - LOGO_W) / 2u)
#define LOGO_Y (240u - LOGO_H - 4u)

static struct {
    uint32_t on, t0, t_drawn;
} intro;

/* the cover's drawbars: length (px) and colour (short enough to end above the logo) */
static const uint8_t INTRO_LEN[9] = {80, 80, 80, 46, 24, 57, 16, 35, 69};

static uint16_t intro_bar_colour(uint32_t d)
{
    static const uint8_t K[9] = {0, 0, 1, 1, 2, 1, 2, 2, 3};
    return K[d] == 0 ? C_BROWN : K[d] == 1 ? C_IVORY : K[d] == 2 ? C_DARKB : C_HYELLOW;
}

static int32_t intro_smooth(int32_t p)          /* 0..1024 -> 0..1024, 3p^2 - 2p^3 */
{
    int32_t p2 = (p * p) >> 10;
    return (3 * p2) - ((2 * p2 * p) >> 10);
}

/* the logo's letters 1..n where they cross rows y0..y0+59 (on the black ground: the G with its grey rim) */
static void intro_logo(uint32_t n, int32_t y0)
{
    uint32_t i, x = 0, y = 0;
    uint16_t col[8];
    for (i = 0; i < 8u; i++) col[i] = RGB(LOGO_PAL[i][0], LOGO_PAL[i][1], LOGO_PAL[i][2]);
    for (i = 0; i < sizeof LOGO_RUNS && y < LOGO_H; i++) {
        uint32_t c = LOGO_RUNS[i] >> 5, len = (LOGO_RUNS[i] & 31u) + 1u, l = c == 7u ? 3u : c;   /* (7: the G's rim) */
        int32_t sy = (int32_t)(LOGO_Y + y);
        if (c && l <= n && sy >= y0 && sy < y0 + 60) cv_rect((int32_t)(LOGO_X + x), sy, (int32_t)len, 1, col[c]);
        x += len;
        if (x >= LOGO_W) { x = 0; y++; }
    }
}

#if ORGIGS_INTRO_HEAD
/* the head's lines, the first `npts` points of them, moved down by dy; only what crosses rows y0..y0+59 */
static void intro_head(uint32_t npts, int32_t dy, int32_t y0)
{
    const uint8_t *p = HEAD_LINES;
    uint32_t l, i, done = 0;
    for (l = 0; l < HEAD_NLINES && done < npts; l++) {
        uint32_t n = *p++;
        for (i = 1; i < n && done + i < npts; i++) {
            int32_t ya = p[2u * i - 1u] + dy, yb = p[2u * i + 1u] + dy;
            if ((ya < y0 && yb < y0) || (ya >= y0 + 60 && yb >= y0 + 60)) continue;
            cv_line(p[2u * i - 2u], ya, p[2u * i], yb, C_IVORY);
        }
        done += n;
        p += 2u * n;
    }
}
#endif

#if BOARD_SMK37
/* the pads: a wave of drawbars pulled out, left to right in the first half, back in the second; its colour runs
   once round the colour wheel. A column's level 0..8 lights its bottom pad (0..4), then the top one (5..8). */
static void intro_pads(uint32_t t)
{
    uint32_t half = INTRO_MS / 2u, c, hue = t * 1536u / INTRO_MS, f = hue & 255u, br = 8u - (st0.pads >> 3 & 7u);
    int32_t p = (int32_t)((t < half ? t : INTRO_MS - t) * 1024u / half), pos = intro_smooth(p > 1024 ? 1024 : p) * 7;   /* x1024 */
    uint32_t r, g, b;
    switch (hue >> 8) {                 /* the colour wheel: red, yellow, green, cyan, blue, magenta */
    case 0: r = 255; g = f; b = 0; break;
    case 1: r = 255 - f; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = f; break;
    case 3: r = 0; g = 255 - f; b = 255; break;
    case 4: r = f; g = 0; b = 255; break;
    default: r = 255; g = 0; b = 255 - f; break;
    }
    for (c = 0; c < 8u; c++) {
        int32_t d = (int32_t)c * 1024 - pos, lv;
        uint32_t lo, hi;
        if (d < 0) d = -d;
        lv = 8 - (d * 3 >> 10);                 /* 8 at the crest, 0 three columns away */
        if (lv < 0) lv = 0;
        lo = (uint32_t)(lv > 4 ? 4 : lv) * br;  /* x 32 = full */
        hi = (uint32_t)(lv > 4 ? lv - 4 : 0) * br;
        padled_set(c + 8u, (uint8_t)(r * lo >> 5), (uint8_t)(g * lo >> 5), (uint8_t)(b * lo >> 5));
        padled_set(c, (uint8_t)(r * hi >> 5), (uint8_t)(g * hi >> 5), (uint8_t)(b * hi >> 5));
    }
    padled_flush();
}
#endif

static void intro_frame(uint32_t t)
{
    uint32_t half, d;
#if ORGIGS_INTRO_HEAD
    uint32_t npts = t >= INTRO_DRAW ? HEAD_NPTS : (uint32_t)(intro_smooth((int32_t)(t * 1024u / INTRO_DRAW)) * HEAD_NPTS) >> 10;
#endif
    int32_t p = t < INTRO_FALL ? 0 : t >= INTRO_FALL + 700u ? 1024 : (int32_t)((t - INTRO_FALL) * 1024u / 700u);
#if ORGIGS_INTRO_HEAD
    int32_t head_dy = (p * p >> 10) * 250 >> 10;               /* falls, slowly first */
#endif
    int32_t grow = intro_smooth(p);
    uint32_t letters = t < INTRO_FALL + 700u ? 0u : (t - INTRO_FALL - 700u) / 65u;

#if BOARD_SMK37
    intro_pads((t - INTRO_SKIP) * INTRO_MS / (INTRO_MS - INTRO_SKIP));   /* (the wave: all of it, there and back) */
#endif
    for (half = 0; half < 4u; half++) {        /* four strips of 60 rows (the canvas holds 64) */
        cv_begin(240, 60, C_BG);
        cv_oy = -(int32_t)half * 60;
#if ORGIGS_INTRO_HEAD
        if (head_dy < 240) intro_head(npts, head_dy, (int32_t)half * 60);
#endif
        if (letters) intro_logo(letters, (int32_t)half * 60);       /* (a letter each 65 ms) */
        /* the drawbars, growing down from the top */
        if (grow)
            for (d = 0; d < 9u; d++) {
                int32_t x = 12 + (int32_t)d * 25, len = (INTRO_LEN[d] * grow) >> 10, g;
                uint16_t c = intro_bar_colour(d);
                lk_shaft(x, 0, 18, len, c);                              /* OrGigs 2: round, no numbers */
                for (g = len - 13; g > 0; g -= 11) lk_rect(x + 1, g, 16, 1, C_BLACK, 60);   /* faint grooves */
                lk_cap(x, len, 18, c);
            }
        cv_oy = 0;
        cv_blit(0, half * 60u);
    }
}

static void intro_start(void)
{
    intro.on = 1;
    intro.t0 = fm1_ms;
    intro.t_drawn = 0xFFFFFFFFu;
}

/* one main-loop pass while the intro runs: 1 = it still runs (the panel is not read) */
static uint32_t intro_step(void)
{
    uint32_t t, rel;
    if (!intro.on) return 0;
    t = fm1_ms - intro.t0;
    if (t >= INTRO_MS - INTRO_SKIP || fm1_in.notes || fm1_in.buttons) {
        intro.on = 0;
        (void)fm1_input_edges(&rel);            /* the press that ended it does nothing else */
        (void)fm1_input_note_edges();
        lcd_fill(0, 0, 240, 240, C_BG);
        drawn_view = 99;                        /* the UI draws everything again (the Elite's pads too: they differ) */
        ui_prev_keys = fm1_in.notes;            /* a held key does not start a note */
        return 0;
    }
    if (intro.t_drawn == 0xFFFFFFFFu || t - intro.t_drawn >= 30u) {
        intro.t_drawn = t;
        intro_frame(t + INTRO_SKIP);
    }
    return 1;
}

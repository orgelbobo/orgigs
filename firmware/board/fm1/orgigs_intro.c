/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: the welcome screen (main loop), about 3 s. Included by orgigs_ui.c.
 *
 *   0    .. 1100 ms  the head draws itself, line by line, hair first (Gigstart2.jpg as polylines)
 *   1100 .. 1400 ms  it rests
 *   1400 .. 2100 ms  the head sinks out of the screen; from the top, out of its hair, the nine drawbars of the
 *                    manual's cover grow down
 *   2100 .. 2500 ms  O r G i g s, letter by letter, 4x, in the tonewheel-organ colours; then FM-1
 *   .. 2950 ms       hold
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

#define INTRO_MS 2950u
#define INTRO_DRAW 1100u                /* the head draws itself */
#define INTRO_FALL 1400u                /* then sinks */
#define INTRO_SKIP (ORGIGS_INTRO_HEAD ? 0u : INTRO_FALL)   /* (no head: its part skipped) */

static struct {
    uint32_t on, t0, t_drawn;
} intro;

/* the cover's drawbars: length (px) and colour */
static const uint8_t INTRO_LEN[9] = {124, 124, 124, 72, 37, 89, 25, 55, 107};

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

/* one glyph of FONT_S at 4x, as solid blocks (the pixel look of the head); returns the advance */
static int32_t intro_glyph4(int32_t x, int32_t y, char ch, uint16_t c)
{
    uint32_t gi = glyph(&FONT_S, (uint8_t)ch), w = FONT_S.bw[gi], bpr = (w + 1u) / 2u, gx, gy;
    const uint8_t *gd = FONT_S.data + FONT_S.off[gi];
    for (gy = 0; gy < FONT_S.h; gy++)
        for (gx = 0; gx < w; gx++) {
            uint32_t v = gd[gy * bpr + gx / 2u];
            v = (gx & 1u) ? (v & 15u) : (v >> 4);
            if (v >= 8u) cv_rect(x + 4 * ((int32_t)gx - FONT_S.pad), y + 4 * (int32_t)gy, 4, 4, c);
        }
    return 4 * FONT_S.adv[gi];
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

static void intro_frame(uint32_t t)
{
    static const char WORD[] = "OrGigs";
    uint32_t half, i, d;
#if ORGIGS_INTRO_HEAD
    uint32_t npts = t >= INTRO_DRAW ? HEAD_NPTS : (uint32_t)(intro_smooth((int32_t)(t * 1024u / INTRO_DRAW)) * HEAD_NPTS) >> 10;
#endif
    int32_t p = t < INTRO_FALL ? 0 : t >= INTRO_FALL + 700u ? 1024 : (int32_t)((t - INTRO_FALL) * 1024u / 700u);
#if ORGIGS_INTRO_HEAD
    int32_t head_dy = (p * p >> 10) * 250 >> 10;               /* falls, slowly first */
#endif
    int32_t grow = intro_smooth(p);
    uint32_t letters = t < INTRO_FALL + 700u ? 0u : (t - INTRO_FALL - 700u) / 65u;
    int32_t word_w = 0;
    for (i = 0; i < 6u; i++) word_w += 4 * FONT_S.adv[glyph(&FONT_S, (uint8_t)WORD[i])];

    for (half = 0; half < 4u; half++) {        /* four strips of 60 rows (the canvas holds 64) */
        cv_begin(240, 60, C_BG);
        cv_oy = -(int32_t)half * 60;
#if ORGIGS_INTRO_HEAD
        if (head_dy < 240) intro_head(npts, head_dy, (int32_t)half * 60);
#endif
        /* the drawbars, growing down from the top */
        if (grow)
            for (d = 0; d < 9u; d++) {
                int32_t x = 12 + (int32_t)d * 25, len = (INTRO_LEN[d] * grow) >> 10;
                cv_rect(x, 0, 18, len, intro_bar_colour(d));
                cv_rect(x - 2, len, 22, 5, C_IVORY);                    /* the handle */
            }
        /* the name */
        if (letters) {
            int32_t x = 120 - word_w / 2;
            for (i = 0; i < 6u && i < letters; i++) {
                uint16_t c = i % 3u == 0u ? C_HGREEN : i % 3u == 1u ? C_HRED : C_HYELLOW;
                x += intro_glyph4(x, 136, WORD[i], c);
            }
            if (letters > 6u) cv_text(120 - text_w(&FONT_S, "FM-1") / 2, 212, &FONT_S, "FM-1", C_TXT);
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
        drawn_view = 99;                        /* the UI draws everything again */
        ui_prev_keys = fm1_in.notes;            /* a held key does not start a note */
        return 0;
    }
    if (intro.t_drawn == 0xFFFFFFFFu || t - intro.t_drawn >= 30u) {
        intro.t_drawn = t;
        intro_frame(t + INTRO_SKIP);
    }
    return 1;
}

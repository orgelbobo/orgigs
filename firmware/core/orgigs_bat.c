/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs: the battery, for any board (main loop). batt_raw() is the raw ADC of the battery divider (platform.c:
   the FM-1's PB1, the SMK-37's mux C 7; the host: T batt RAW). About half the cell's voltage against 3.3 V, so
   raw x BOARD_BAT_SCALE / 1023 mV (measured on the Elite: 635 off the cable, a nearly full cell, 647 charging;
   the FM-1 reads lower: 624 full off the cable, 628 on a charger, so its board.h sets 6810).
   The % follows a lithium cell's discharge curve; 0 % = 3.40 V is an estimate until a near-empty reading. Below
   300: no reading (the host build), nothing shown.

   Charging: the keyboard is on a USB host (a computer, the SEQTRAK) - its cable carries the power, the cell
   charges. A plain USB charger does not show (nothing talks to the keyboard there). The boards draw a bolt in the
   symbol then, and say no BATTERY LOW. */
#ifndef BOARD_BAT_SCALE
#define BOARD_BAT_SCALE 6600u
#endif

static struct { uint32_t acc, shown, warned, warn_at; } bat;
static const uint16_t BAT_MV[11] = {4150, 4100, 4000, 3900, 3800, 3750, 3700, 3650, 3600, 3500, 3400};
static const uint8_t BAT_PCT[11] = {100, 95, 85, 75, 60, 50, 40, 25, 15, 8, 0};
static uint32_t bat_ok(void) { return bat.acc >= 300u * 64u; }
static uint32_t bat_mv(void) { return bat.acc * BOARD_BAT_SCALE / (1023u * 64u); }
static uint32_t bat_charging(void) { return usb_host_up() != 0u; }
static uint32_t bat_pct(void)
{
    uint32_t mv = bat_mv(), i;
    if (mv >= BAT_MV[0]) return 100u;
    for (i = 1; i < 11u; i++)
        if (mv >= BAT_MV[i])
            return BAT_PCT[i] + (mv - BAT_MV[i]) * (BAT_PCT[i - 1] - BAT_PCT[i]) / (BAT_MV[i - 1] - BAT_MV[i]);
    return 0;
}
static char *bat_fmt(char *p)            /* "85% 4.05V", "--" */
{
    uint32_t mv = bat_mv();
    if (!bat_ok()) return cat(p, "--");
    p = cat(org_fmt(p, (int32_t)bat_pct(), 0), "% ");
    p = cat(org_fmt(p, (int32_t)(mv / 1000u), 0), ".");
    if ((mv % 1000u) / 10u < 10u) p = cat(p, "0");
    return cat(org_fmt(p, (int32_t)((mv % 1000u) / 10u), 0), "V");
}
/* smoothed; 1 when the symbol changes (the board redraws it); BATTERY LOW below 10 %, not while charging */
static uint32_t bat_tick(void)
{
    uint32_t raw = batt_raw(), pct, key;
    if (raw < 300u) { if (bat.acc) { bat.acc = 0; bat.shown = 0; return 1; } return 0; }
    bat.acc = bat.acc ? bat.acc + (uint32_t)(((int32_t)raw * 64 - (int32_t)bat.acc) / 256) : raw * 64u;
    pct = bat_pct();
    if (pct >= 15u || bat_charging()) bat.warned = 0;
    else if (pct < 10u && fm1_ms > 3000u && (!bat.warned || fm1_ms - bat.warn_at >= 300000u)) {
        char t[8];
        bat.warned = 1;
        bat.warn_at = fm1_ms;
        cat(org_fmt(t, (int32_t)pct, 0), "%");
        ctl_say("BATTERY LOW", t);
    }
    key = (pct * 12u + 50u) / 100u + (pct < 20u ? 16u : 0u) + (bat_charging() ? 64u : 0u) + 32u;
    if (key == bat.shown) return 0;
    bat.shown = key;
    return 1;
}

/* the bolt in the battery symbol, 8 x 11 from (x, y), yellow with a black rim (on the symbol's white fill) */
static void draw_bolt(int32_t x, int32_t y)
{
    static const uint8_t ROW[11][2] = {{4, 3}, {3, 3}, {3, 3}, {2, 3}, {1, 6}, {1, 6}, {3, 3}, {3, 2}, {2, 2}, {2, 2}, {1, 2}};
    uint32_t i;
    for (i = 0; i < 11u; i++) cv_rect(x + ROW[i][0] - 1, y + (int32_t)i - 1, ROW[i][1] + 2, 3, RGB(0, 0, 0));
    for (i = 0; i < 11u; i++) cv_rect(x + ROW[i][0], y + (int32_t)i, ROW[i][1], 1, RGB(244, 196, 36));
}

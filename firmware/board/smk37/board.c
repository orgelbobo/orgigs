/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs on the M-VAVE SMK-37 (Elite): the FM-1's panel and screens (board/fm1/orgigs_ui.c) with the SMK-37's own
 * controls on top. Bank PERFORMANCE (the default; more banks and layouts later):
 *
 *   K1..K8      drawbars 1..8 of the focused manual (on the drawbar screen; elsewhere K5..K8 are KNOB1..4 and
 *               K2..K4 SELECT, ALGORITHM, PRESETS as on the FM-1)
 *   fader 1     drawbar 9 (up 0, down 8)         fader 2  reverb send (of the manual)  fader 3  the MIDI out's sum       fader 4  volume
 *               (1..3 pick up: after a change they act once they reach the value)
 *   pads        the column under knob n: top pad drawbar n to 0, bottom pad to 8; their colours show the drawbars
 *               (layout A FULL DRAWBARS; SCALE + KNOB BANK: B DRAWBARS + PADS, the bottom pads drawbar n in / out, the
 *               top pads as the screen's strip: vibrato, PERC on/off, PERC 2F..3S, rotary slow/fast, stop, drive,
 *               delay, the effect; C KNOBS + 16 PADS: B's top pads, the bottom pads manual U L P, coupler, the
 *               the zone's FCCI, FCCO, MIDI OUT and INTERNAL SOUND; D SOUND DESIGN: B's top pads, the bottom pads
 *               the pages FX ROTARY EFFECT MASTER MIXER for the knobs (again: the drawbars), the manual U L P)
 *   ARP / PATCH drawbar 9 to 0 / to 8 (on the drawbar screen; lit: drawbar 9 at 0..3 / 4..8); SCALE + PATCH: panic
 *   FX (again: back) ARP / PATCH the pages: FX: K1..K4 the manual's drive, vibrato, delay and reverb sends, K5..K8
 *               the shared ones; pads 1-3 the manual, 4 its rotary. ROTARY: pads 1-4 the cabinet (122, 145 fixed,
 *               USER 1, 2 kept as edited), K1..K8 and SCALE + K1..K4 its values; BT + pad 3 / 4 copies to USER 1 / 2.
 *               EFFECT: pads 1-7 the insert effect (again: off), 9-11 its manual, K1..K8 its values. MASTER: pad 1 the
 *               EQ, pad 2 the compressor on the sum; K1..K4 EQ, K5..K8 compressor, SCALE + K1..K3 the rest
 *   MIXER (PARA; again: back) K1..K8 channels 1..8 / 9..16 (KNOB BANK), < > the parameter (volume, pan, reverb,
 *               delay); top pads mute (red), bottom pads solo (dark blue)
 *   CHORD       chord memory: hold, play a chord, let go: each key plays it transposed; a tap: off (again: on)
 *   NOTE RPT    vibrato / tremulant of the manual on/off       PARA  the MIDI mixer (again: back)
 *   KNOB BANK   the next manual (upper, lower, pedal)           < >   the drawbar list (on letting go); SCALE + < >:
 *               the model; < and > held together: K1 the drawbar list, K2 the model, K3 the layout (the pads: the
 *               cloner's channels until one of them turns)
 *   FADER BANK  the next layout A B1 B2 C D (elsewhere: the drawbars first). KNOB BANK + FADER BANK: HOME (upper,
 *               model 1, P1, the couplers off, layout A). Both banks on letting go. A model or layout chosen: its name big
 *   sustain     holds the keys let go (organ and MIDI), CC 64 out; its kind (open / closed) read at power-on
 *   pitch wheel bend (the BEND setting)                         mod wheel  rotary slow / fast (WAH: its pedal)
 *   37 keys     C3..C6 with velocity (to MIDI out; the organ has none); they only play, everywhere
 *   MIDI (GLOBE; again: back) ZONE CLONE YST HARMONIC CHORD, ARP / PATCH the previous / next screen, PARA the mixer;
 *               K1..K8 the screen's eight values; ZONE: its switches on pads 1..11 (2 FCCO, 11 FCCI); CHORD: pad 1 capture, pad 2 clear;
 *               YST: pad 16 the sound browser (K1 the category, K2 the sound; pad 16 or GLOBE: back)
 *   SCALE + pad setup 1..16 (top row 1..8), BT + pad: store it (held, the pads show the setups); CLONE: the pads
 *               are channels 1..16, HARMONIC: pads 1..9 the drawbars sent (in YST SCALE + pad is a track's mute)
 * The FM-1's names on the SMK's buttons (platform.c): ENV = CHORD, LFO = NOTE REPEAT, EDIT = PARA, SEL = SCALE,
 * GLO = GLOBE, HOME = PATCH, SAVE = BT. The button LEDs are on / off: the glow and the lights' DIM show dark.
 *
 * The pads' colours (the lights screen, SCALE + GLOBE: K1 the scheme, K3 the brightness), after the SMK-37 drawbar
 * LED study (Pad-Beleuchtung Idee): GLOW (red .. yellow, the default), SPLIT 4+4, SPECTRUM, SPEC WARM, FOOTAGE,
 * DUAL RES. GLOW, SPECTRUM and SPECTRUM WARM have the study's rows swapped: the bottom pad shows what is pulled out
 * (a drawbar at 8 = bottom pad red); FOOTAGE (the footage colour on top), SPLIT 4+4 and DUAL RES keep its rows. */
#include "smk_wheel.h"

static uint32_t smk_controls(void);
static uint32_t smk_buttons(uint32_t b);
static void smk_banks(void);
static void smk_leds(uint8_t *lv);
static uint32_t smk_key_taps(void);
static uint32_t smk_knob_light(uint32_t k, int32_t st);
#define smk_light_cells(top) smk_light_cells_((top)->lab, (top)->val, (top)->col)
static void smk_light_cells_(char lab[4][10], char val[4][10], uint16_t *col);

#include "orgigs_ui.c"

/* ---- the pads' colours: r, g, b and a brightness in % (the study's tables) */
typedef struct { uint8_t r, g, b, pct; } padc_t;
/* (as stored: 0 was GLOW before RED-GLOW came, the default; GLOW is 6 now) */
enum { PS_RED, PS_SPLIT, PS_SPECTRUM, PS_SPEC_WARM, PS_FOOTAGE, PS_DUAL, PS_GLOW, PS_N };
static const char *const PS_NAME[PS_N] = {"RED-GLOW", "SPLIT 4+4", "SPECTRUM", "SPECTRUM WARM", "FOOTAGE", "DUAL RES", "GLOW"};
static const char *const PS_TAG[PS_N] = {"RED", "SPLIT", "SPECTR", "WARM", "FOOT", "DUAL", "GLOW"};   /* (the cell is narrow) */
static const padc_t GLOW[9] = {
    {255, 255, 255, 0}, {255, 246, 224, 13}, {255, 238, 153, 25}, {255, 221, 68, 38}, {255, 187, 17, 50},
    {255, 144, 0, 63}, {255, 98, 0, 75}, {255, 48, 0, 88}, {255, 0, 0, 100}};
static const padc_t SPLIT_TOP[5] = {{0, 0, 0, 0}, {0, 85, 255, 42}, {0, 229, 255, 62}, {0, 230, 118, 82}, {255, 234, 0, 100}};
static const padc_t SPLIT_BOT[5] = {{0, 0, 0, 0}, {255, 179, 0, 50}, {255, 109, 0, 70}, {255, 42, 0, 88}, {255, 0, 34, 100}};
static const padc_t THERMAL[9] = {
    {0, 0, 0, 0}, {30, 64, 255, 35}, {0, 176, 255, 50}, {0, 229, 204, 65}, {0, 230, 118, 82},
    {174, 234, 0, 88}, {255, 234, 0, 92}, {255, 109, 0, 96}, {255, 0, 34, 100}};
static const padc_t FOOT_HEAD[8] = {   /* 16' 5 1/3' amber, 8' 4' 2' cold white, 2 2/3' 1 3/5' 1 1/3' violet */
    {255, 140, 0, 0}, {255, 140, 0, 0}, {232, 244, 255, 0}, {232, 244, 255, 0},
    {179, 71, 255, 0}, {232, 244, 255, 0}, {179, 71, 255, 0}, {179, 71, 255, 0}};

static padc_t pc(uint8_t r, uint8_t g, uint8_t b, uint32_t pct) { padc_t c = {r, g, b, (uint8_t)pct}; return c; }
static void pad_pair(uint32_t scheme, uint32_t col, uint32_t v, padc_t *top, padc_t *bot)
{
    *top = pc(0, 0, 0, 0);
    *bot = pc(0, 0, 0, 0);
    switch (scheme) {
    case PS_RED:                        /* one red over both, moving down as the drawbar is pulled (the sum: constant) */
        *top = pc(255, 0, 0, (8u - v) * 100u / 8u); *bot = pc(255, 0, 0, v * 100u / 8u); break;
    case PS_GLOW: *top = GLOW[v]; *bot = GLOW[8u - v]; break;
    case PS_SPLIT:
        if (v <= 4u) *top = SPLIT_TOP[v];
        else { *top = pc(255, 234, 0, 95); *bot = SPLIT_BOT[v - 4u]; }
        break;
    case PS_SPECTRUM: *top = THERMAL[v]; *bot = THERMAL[8u - v]; break;
    case PS_SPEC_WARM: *top = THERMAL[v]; *bot = pc(255, 243, 214, v * 100u / 8u); break;
    case PS_FOOTAGE:
        *top = FOOT_HEAD[col];
        top->pct = (uint8_t)(v ? 45u + v * 50u / 8u : 22u);
        *bot = THERMAL[v];
        break;
    default:                            /* PS_DUAL: the third (blue, green, red), then the step inside it */
        if (!v) break;
        *top = v <= 3u ? pc(0, 136, 255, 35u + v * 20u) : v <= 6u ? pc(0, 230, 118, 40u + (v - 3u) * 20u)
                                                                    : pc(255, 23, 68, v == 7u ? 80u : 100u);
        *bot = (v - 1u) % 3u == 0u ? pc(255, 240, 200, 45) : (v - 1u) % 3u == 1u ? pc(255, 234, 0, 78) : pc(0, 240, 255, 100);
        break;
    }
}

static uint32_t smk_pad_scheme(void) { return (st0.pads & 7u) < PS_N ? st0.pads & 7u : PS_RED; }
static uint32_t smk_pad_bright(void) { return 8u - (st0.pads >> 3 & 7u); }   /* 1..8 (8 = as the study) */

static void pad_out(uint32_t pad, padc_t c, uint32_t br)
{
    uint32_t s = c.pct * br;            /* % x 1..8: / 800 */
    padled_set(pad, (uint8_t)((c.r * s + 400u) / 800u), (uint8_t)((c.g * s + 400u) / 800u), (uint8_t)((c.b * s + 400u) / 800u));
}

static uint32_t smk_yst(void) { return ui.mode == MODE_MIDI && ui.ms == MS_YST; }   /* bank YST: its screen is up */
static uint32_t smk_midi(uint32_t ms) { return ui.mode == MODE_MIDI && ui.ms == ms; }

/* ---- bank YST, pads 12..15: 12 the action pad (pink): while held it pulls a CC up, as fast as it was struck (hard:
   60 ms to the top, soft: 3 s), let go: 0 again. The CC: the WHEELS page, K3 (default 102, the master effect's first
   parameter: VINYL BREAK's BREAK when the SEQTRAK's master effect is VINYL BREAK); 107..109 on the track's channel.
   13 STOP (white), 14 PLAY / PAUSE (green; blinks while paused), 15 REC (red; the SEQTRAK takes no record over MIDI:
   not yet). Bright: the state it is in */
static struct { uint8_t held, vel, sent, ch, cc; uint32_t at; int32_t v; } vb;
static void vb_down(uint32_t vel)
{
    uint32_t i = vb_idx();
    vb.held = 1; vb.vel = (uint8_t)vel; vb.cc = VB_CC[i]; vb.ch = i < VB_TRK ? 0u : (uint8_t)yst.cur;
    vb.v = 0; vb.sent = 0; vb.at = fm1_ms;
    ctl_say("ACTION PAD", VB_NAME[i]);
}
static void vb_tick(void)
{
    uint32_t dt = fm1_ms - vb.at, nv;
    if (!vb.held || dt < 8u) return;
    vb.at = fm1_ms;
    vb.v += (int32_t)(dt * (127u * 256u) / (60u + (127u - vb.vel) * 2940u / 126u));
    if (vb.v > 127 * 256) vb.v = 127 * 256;
    nv = (uint32_t)vb.v >> 8;
    if (nv != vb.sent) { vb.sent = (uint8_t)nv; ml_q(0xB0u | vb.ch, vb.cc, nv); }
}
static void vb_up(void)
{
    if (!vb.held) return;
    vb.held = 0; vb.sent = 0;
    ml_q(0xB0u | vb.ch, vb.cc, 0);
}
static void yst_transport(uint32_t pad) /* 12 stop, 13 play / pause, 14 rec */
{
    if (pad == 12u) { ml_q(0xFCu, 0, 0); yst.run = 0; ctl_say("SEQTRAK", "STOP"); }
    else if (pad == 13u) {
        if (yst.run == 1u) { yst.run = 2; ml_q(0xFCu, 0, 0); ctl_say("SEQTRAK", "PAUSE"); }
        else { ml_q(yst.run == 2u ? 0xFBu : 0xFAu, 0, 0); ctl_say("SEQTRAK", yst.run == 2u ? "CONTINUE" : "PLAY"); yst.run = 1; }
    } else ctl_say("REC", "NOT OVER MIDI (YET)");
    yst.gen++;
}
static uint32_t yst_blink(void) { return yst.run == 2u && (fm1_ms / 300u & 1u); }

static void smk_pads_yst(uint32_t br)  /* bank YST: pads 1..11 the SEQTRAK's tracks, coloured by type */
{
    static const padc_t TYPE_C[6] = {{60, 60, 60, 40}, {255, 120, 0, 70}, {0, 200, 255, 70}, {255, 120, 0, 70},
                                     {190, 60, 255, 70}, {40, 230, 80, 70}};
    uint32_t t;
    for (t = 0; t < 16u; t++) {
        padc_t c = pc(0, 0, 0, 0);
        if (t < YST_NTRK) {
            c = TYPE_C[yst.on && yst.t[t].type <= YT_SAMPLER ? yst.t[t].type : 0];   /* (grey: not read yet) */
            if (t == yst.cur) c = pc(255, 255, 255, 100);
            else if (yst.t[t].mute) c.pct = 8;
        } else if (t == 11u) c = pc(255, 40, 160, vb.held ? 25u + vb.sent * 75u / 127u : 15u);   /* the action pad */
        else if (t == 12u) c = pc(255, 255, 255, yst.run == 0u ? 80u : 12u);                     /* STOP */
        else if (t == 13u) c = pc(40, 230, 80, yst.run == 1u || yst_blink() ? 90u : 12u);        /* PLAY / PAUSE */
        else if (t == 14u) c = pc(255, 0, 0, 12u);                                              /* REC (not yet) */
        else if (t == 15u) c = pc(255, 150, 0, ysb.on ? 90u : 20u);   /* pad 16: the sound browser */
        pad_out(t, c, br);
    }
    padled_flush();
}

static void smk_pads_mask(uint32_t mask, uint32_t n, padc_t on, uint32_t br)   /* pads 1..n lit where the mask is */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) pad_out(i, i >= n ? pc(0, 0, 0, 0) : mask >> i & 1u ? on : pc(60, 60, 60, 12), br);
    padled_flush();
}

/* ---- the cloner's channels on the pads (the CLONE screen, or < + > held anywhere): green the channels cloned to
   (dim while the cloner is off), white the zone's own out channel, grey the rest. A pad: the channel in / out; the
   first one chosen switches the cloner on, the last one let go off (CHORD on the CLONE screen: on / off, the
   channels kept) */
static uint8_t lr_both, lr_turned;     /* < and > held together; K1..K3 turned meanwhile (the pads: the layout's) */
static void smk_pads_clone(uint32_t m, uint32_t br)
{
    const zone_t *z = &zn.z[m];
    uint32_t i;
    for (i = 0; i < 16u; i++)
        pad_out(i, i == z->out_ch ? pc(255, 255, 255, 60) : z->clone_mask >> i & 1u ? pc(40, 230, 80, z->flags & Z_CLONE ? 80u : 20u)
                   : pc(60, 60, 60, 12), br);
    padled_flush();
}
static void smk_clone_pad(uint32_t pad)
{
    zone_t *z = &zn.z[mls.focus];
    char t[24];
    if (pad == z->out_ch) { man_label(t, "OWN CHANNEL"); ctl_say_int(t, (int32_t)pad + 1, 0); return; }
    ml_release_all();
    z->clone_mask ^= (uint16_t)(1u << pad);
    if (z->clone_mask >> pad & 1u) z->flags |= Z_CLONE;     /* the first channel: the cloner on */
    else if (!z->clone_mask) z->flags &= (uint8_t)~Z_CLONE;
    ui.clone_cur = (uint8_t)pad;
    (void)man_label(t, z->clone_mask >> pad & 1u ? "CLONE ON CH" : "CLONE OFF CH");
    ctl_say_int(t, (int32_t)pad + 1, 0);
    touched();
}

/* ---- layout B (DRAWBARS + PADS): the top pads' live switches, in the order and colours of the screen's strip (VIB
   green, PERC red, LES yellow, DRV red, DLY yellow); bright = on, dim = off:
   1 vibrato on / off (held: K1 the type; the other models: the tremulant, K1 its depth)   2 PERC on / off (the other
   models: the coupler)   3 PERC 2F 2S 3F 3S 4F 4S in turn (held: K1 NORMAL / SOFT, K3 this one's decay)   4 rotary slow / fast (it pulses
   with the horn)   5 rotary stop   6 the manual's drive on / off (held: K6 its amount)   7 its delay send on / off (held: K7 the
   delay time, K8 the send)   8 the insert effect on / off (amber, the knobs' colour; held: K8 its MIX). Pads 1, 3, 6,
   7 and 8 act on letting go, unless a knob turned while held. The bottom pads: drawbar 1..8 in (0) / out (8). */
static void smk_db_set(uint32_t d, uint32_t v);
static uint8_t perc_last = PERC_3RD_FAST, fx_last = FXI_CHORUS;   /* what PERC and EFFECT switch back on */
static uint8_t drv_last[ORG_NMAN] = {48, 48, 48}, dly_last[ORG_NMAN] = {40, 40, 40};
static uint8_t live_held, live_turned;                          /* pad 1, 3, 6, 7 or 8 held (bit), a knob turned meanwhile */
static uint32_t live_pulse;                                     /* the rotary pad's level 0..7, every 30 ms */

static void smk_perc(uint32_t p)
{
    org_par.perc = (uint8_t)p;
    ctl_say(org_par.model == MODEL_B3 ? "PERCUSSION" : "PERC (WHEEL ONLY)", PERC_NAME[p]);
    touched();
}
static void smk_live_toggle(uint32_t pad)   /* pad 6: the manual's drive on / off; 7: its delay send; 8: the insert effect */
{
    char t[16];
    uint32_t m = mls.focus;
    if (pad == 5u) {
        if (org_par.drive[m]) { drv_last[m] = org_par.drive[m]; org_par.drive[m] = 0; } else org_par.drive[m] = drv_last[m];
        man_label(t, "DRIVE"); ctl_say_int(t, org_par.drive[m], 0); touched();
        return;
    }
    if (pad == 7u) {
        if (fxi_st.type) { fx_last = fxi_st.type; fxi_pick(fxi_st.type); }   /* (its pad again: off) */
        else fxi_pick(fx_last);
        return;
    }
    if (org_par.dsend[m]) { dly_last[m] = org_par.dsend[m]; org_par.dsend[m] = 0; } else org_par.dsend[m] = dly_last[m];
    man_label(t, "DELAY SEND"); ctl_say_int(t, org_par.dsend[m], 0); touched();
}
static void smk_live_up(uint32_t pad)   /* pads 1, 3, 6, 7, 8 act on letting go, unless a knob turned while held */
{
    uint32_t q;
    if (pad >= 8u || !(live_held >> pad & 1u)) return;
    live_held &= (uint8_t)~(1u << pad);
    if (live_turned) { if (!live_held) live_turned = 0; return; }
    if (pad >= 5u) { smk_live_toggle(pad); return; }
    if (pad == 0u) { ctl_vib_toggle(); return; }
    q = (org_par.perc ? org_par.perc : perc_last) % (ORG_NPERC_ALL - 1u) + 1u;   /* 2F 2S 3F 3S 4F 4S 2F .. */
    if (org_par.perc) smk_perc(q);
    else { perc_last = (uint8_t)q; ctl_say("PERC OFF, NEXT", PERC_NAME[q]); }
}
static uint32_t fxi_mix_k(void)          /* the insert effect's MIX: its knob, FXI_NP: none */
{
    uint32_t k;
    for (k = 0; k < FXI_NP; k++) {
        const char *g = FXI_PAR[fxi_st.type][k].tag;
        if (g && g[0] == 'M' && g[1] == 'I' && g[2] == 'X' && !g[3]) return k;
    }
    return FXI_NP;
}
static uint32_t smk_live_takes(uint32_t f)   /* the knob is a held pad's: K1, K3 (pads 1, 3), K6 (pad 6), K7 K8 (pad 7), K8 (pad 8) */
{
    if (f == 5u) return (live_held & 0x20u) != 0u;
    if (f == 0u || f == 2u) return (live_held & 5u) != 0u;
    if (f == 6u) return (live_held & 0x40u) != 0u;
    return f == 7u && (live_held & 0xC0u) != 0u;
}
static void smk_live_knob(uint32_t f, int32_t st)  /* K1 or K3 while pad 1 or 3 is held, K6 while pad 6, K7 / K8 while pad 7 or 8 */
{
    live_turned = 1;
    if (f == 5u) { ctl_drive(st); return; }                                          /* pad 6: the drive */
    if (f >= 6u) {
        if (live_held & 0x40u) { if (f == 6u) ctl_delay_time(st); else ctl_dsend(st); }   /* pad 7: time, send */
        else if (fxi_st.type == FXI_OFF) ctl_say("EFFECT OFF", "PAD 8: SWITCH IT ON");
        else if (fxi_mix_k() < FXI_NP) fxi_step(fxi_mix_k(), st);                       /* pad 8: its MIX */
        return;
    }
    if (f == 2u) { if (live_held & 4u) pnl_perc_decay(org_par.perc ? org_par.perc : perc_last, st); }   /* pad 3 + K3 */
    else if (live_held & 1u) ctl_vib(st);
    else if (org_par.perc_soft != (st < 0)) pnl_soft_toggle();   /* (left: soft, right: normal) */
}
static void smk_live_pad(uint32_t pad)
{
    switch (pad) {
    case 0: case 2: live_held |= (uint8_t)(1u << pad); if (pad == 2u && org_par.model != MODEL_B3) live_held &= (uint8_t)~4u; break;
    case 1:
        if (org_par.model != MODEL_B3) cpl_step();
        else smk_perc(org_par.perc ? PERC_OFF : perc_last);
        break;
    case 3: ctl_leslie_fast(); break;
    case 4: ctl_leslie_brake(); break;
    default: live_held |= (uint8_t)(1u << pad); break;   /* 6, 7, 8: on letting go (held: the knobs) */
    }
}
static void smk_db_flip(uint32_t d)     /* a bottom pad: the drawbar out to 8, or (anywhere out) in to 0 */
{
    uint32_t m = mls.focus;
    if ((m == MAN_PEDAL && d >= 4u) || d >= ORG_NDB) return;
    smk_db_set(d, org_par.drawbar[m][d] ? 0u : 8u);
}
/* ---- layout C (KNOBS + 16 PADS): the top pads as B; the bottom pads 9-11 the manual (upper, lower, pedal: bright the one
   chosen; purple: coupled, else blue), 12 the coupler on / off (purple; bright: two or more coupled; held + pad 9-11: that manual in / out of it,
   9-11 purple meanwhile), 13-16 its zone's FCCI (filter CC in), FCCO (filter CC out), MIDI OUT and INTERNAL SOUND, as on the
   ZONE screen (lit: the way is open, 13 and 15 grey, 14 and 16 orange) */
static void smk_fcci_toggle(void)       /* the focused zone's FCCI (also the ZONE screen's pad 11) */
{
    zone_t *z = &zn.z[mls.focus];
    ml_release_all();                   /* (notes the pedal holds: let go now, not never) */
    z->xf ^= ZX_FCCI;
    ctl_say("FILTER CC IN", z->xf & ZX_FCCI ? "ON" : "OFF");
    touched();
}
static void smk_mout_toggle(void)       /* the focused zone's MIDI OUT on / off (also the ZONE screen's pad 12) */
{
    zone_t *z = &zn.z[mls.focus];
    ml_release_all();                   /* (no note left hanging out there) */
    z->xf ^= ZX_MUTE;
    ctl_say("MIDI OUT", z->xf & ZX_MUTE ? "OFF" : "ON");
    touched();
}
static uint8_t c_cpl_held, c_cpl_used;  /* the coupler pad held (C: 12, B2: 4; the pad + 1), a manual pad tapped meanwhile */
static void smk_c_pad(uint32_t i, uint32_t pad)   /* C's bottom row, B2's top row: i 0..7 */
{
    zone_t *z = &zn.z[mls.focus];
    if (i < ORG_NMAN) { if (c_cpl_held) { c_cpl_used = 1; cpl_man(i); } else focus_set(i); }
    else if (i == 3u) { c_cpl_held = (uint8_t)(pad + 1u); c_cpl_used = 0; }    /* (on letting go: on / off) */
    else if (i == 4u) smk_fcci_toggle();
    else if (i == 5u) { flag_toggle(z, Z_NOCC, "FILTER CC OUT"); touched(); }
    else if (i == 6u) smk_mout_toggle();
    else {
        ml_release_all();
        z->flags ^= Z_NOINT;
        ctl_say("INTERNAL SOUND", z->flags & Z_NOINT ? "OFF" : "ON");
        touched();
    }
}
static padc_t smk_c_col(uint32_t i)
{
    const zone_t *z = &zn.z[mls.focus];
    uint32_t c = ml_cpl();
    if (i < ORG_NMAN) return c_cpl_held ? pc(170, 40, 255, (cpl_raw() ? cpl_raw() : cpl_mask) >> i & 1u ? 90u : 15u)
                                        : c >> i & 1u ? pc(170, 40, 255, i == mls.focus ? 90u : 15u)   /* coupled */
                                                      : pc(0, 90, 255, i == mls.focus ? 90u : 15u);
    if (i == 3u) return pc(170, 40, 255, c ? 90u : 15u);
    /* lit: it passes (filter out of the way, MIDI out on, internal sound on); 13, 15 grey, 14, 16 orange */
    if (i == 4u) return pc(200, 200, 200, z->xf & ZX_FCCI ? 15u : 90u);
    if (i == 5u) return pc(255, 110, 0, z->flags & Z_NOCC ? 15u : 90u);
    if (i == 6u) return pc(200, 200, 200, z->xf & ZX_MUTE ? 15u : 90u);
    return pc(255, 110, 0, z->flags & Z_NOINT ? 15u : 90u);
}

/* ---- layout D (SOUND DESIGN): the top pads as B; the bottom pads 9-13 the pages FX, ROTARY, EFFECT, MASTER, MIXER (the
   knobs on them; the page's pad again: back to the drawbars), 14-16 the manual (upper, lower, pedal) */
static uint32_t smk_d_here(void)        /* 1: D's pads are up (the drawbars or one of its pages, SCALE not held) */
{
    return pnl_st.lay == LAY_SOUND && (ui.mode == MODE_PLAY || ui.mode == MODE_FX || ui.mode == MODE_MIX);
}
static uint32_t smk_d_page(void)        /* the page up: 0..4, 5 = the drawbars */
{
    return ui.mode == MODE_FX ? ui.fxp : ui.mode == MODE_MIX ? 4u : 5u;
}
static void smk_d_pad(uint32_t i)
{
    static const uint8_t FXP_OF[4] = {FXP_FX, FXP_LESLIE, FXP_EFFECT, FXP_MASTER};
    static const char *const PG[5] = {"FX", "ROTARY", "EFFECT", "MASTER", "MIXER"};
    if (i >= 5u) { focus_set(i - 5u); return; }
    if (i == smk_d_page()) { set_mode(MODE_PLAY); ctl_say("PAGE", "DRAWBARS"); return; }
    if (i == 4u) set_mode(MODE_MIX);
    else { ui.fxp = FXP_OF[i]; set_mode(MODE_FX); }
    ctl_say("PAGE", PG[i]);
}
static padc_t smk_d_col(uint32_t i)
{
    if (i >= 5u) return pc(255, 150, 0, i - 5u == mls.focus ? 90u : 15u);
    return pc(232, 244, 255, i == smk_d_page() ? 90u : 12u);
}

static padc_t smk_live_col(uint32_t pad)   /* the screen's colours: on 90 %, off 15 % */
{
    static const padc_t COL[8] = {{52, 178, 84, 0}, {214, 56, 42, 0}, {214, 56, 42, 0}, {244, 196, 36, 0},
                                  {244, 196, 36, 0}, {214, 56, 42, 0}, {244, 196, 36, 0}, {255, 176, 48, 0}};
    uint32_t m = mls.focus, b3 = org_par.model == MODEL_B3, on;
    padc_t c = COL[pad & 7u];
    switch (pad) {
    case 0: on = dept_now() == DEPT_VINTAGE ? org_par.vib[m] != 0u : org_par.trem[m] != 0u; break;
    case 1: on = b3 ? org_par.perc != 0u : cpl_idx() != 0u; break;
    case 2: if (!b3) return pc(0, 0, 0, 0); on = org_par.perc != 0u; break;
    case 3:                             /* the speed is in the pulse; past the rotary: steady, dim */
        if (!org_par.les_on[m]) { c.pct = 15; return c; }
        c.pct = (uint8_t)(20u + live_pulse * 11u);
        return c;
    case 4: on = org_par.leslie == LESLIE_BRAKE; break;
    case 5: on = org_par.drive[m] != 0u; break;
    case 6: on = org_par.dsend[m] != 0u; break;
    default: on = fxi_st.type != 0u; break;
    }
    c.pct = on ? 90u : 15u;
    return c;
}
static uint32_t smk_live_hash(void)
{
    static uint32_t at;
    uint32_t m = mls.focus, h;
    if (fm1_ms - at >= 30u) {           /* the horn's angle: one pulse a turn */
        at = fm1_ms;
        live_pulse = (uint32_t)(org_sin(org_stat.horn_ph) + 32768) >> 13;
        if (live_pulse > 7u) live_pulse = 7u;
    }
    if (org_par.perc) perc_last = org_par.perc;
    if (fxi_st.type) fx_last = fxi_st.type;
    h = (((((org_par.perc * 8u + cpl_idx()) * 8u + org_par.vib[m]) * 131u + org_par.trem[m]) * 4u + org_par.leslie) * 2u
         + org_par.les_on[m]) * 8u + live_pulse;
    h = ((h * 2u + (org_par.drive[m] != 0u)) * 2u + (org_par.dsend[m] != 0u)) * 16u + fxi_st.type;
    return (h * 16u + org_par.model) * 4u + m;
}

static void smk_pads_show(void)         /* the focused manual's drawbars 1..8 on the 8 columns (the pedal: 1..4) */
{
    static uint32_t drawn;
    uint32_t m = mls.focus, col, n = m == MAN_PEDAL ? 4u : 8u, h = 0x9E37u, sch = smk_pad_scheme(), br = smk_pad_bright();
    uint32_t held = fm1_in.buttons >> BTN_SAVE & 1u ? 2u : ui.shift && !smk_yst() && ui.mode != MODE_MIX && ui.mode != MODE_FX ? 1u : 0u;
    if (lr_both && !lr_turned) {        /* < + > held: the cloner's channels (K1..K3 turned: the layout, as it now is) */
        h = 0xA8000000u ^ ((uint32_t)zn.z[m].clone_mask << 8 | zn.z[m].out_ch << 3 | (zn.z[m].flags & Z_CLONE ? 4u : 0u) | m) ^ br << 24;
        if (h != drawn) { drawn = h; smk_pads_clone(m, br); }
        return;
    }
    if (held) {                         /* SCALE held: the setups to recall (amber), BT held: to store (red); white: the
                                           one last recalled or stored */
        h = 0xC0000000u | (uint32_t)st0.setup << 12 | held << 8 | br;
        if (h != drawn) {
            uint32_t i;
            drawn = h;
            for (i = 0; i < 16u; i++)
                pad_out(i, i == st0.setup ? pc(255, 255, 255, 90) : held == 2u ? pc(255, 30, 0, 45) : pc(255, 150, 0, 45), br);
            padled_flush();
        }
        return;
    }
    if (smk_d_here()) {                 /* D: the live switches, the pages and the manual */
        uint32_t i;
        h = 0x86000000u ^ (smk_live_hash() * 0x01000193u) ^ (smk_d_page() << 24 | m << 20 | br);
        if (h != drawn) {
            drawn = h;
            for (i = 0; i < 8u; i++) { pad_out(i, smk_live_col(i), br); pad_out(i + 8u, smk_d_col(i), br); }
            padled_flush();
        }
        return;
    }
    if (ui.mode == MODE_FX) {           /* FX: pads 1-3 the manual (amber), 4 its rotary (yellow); ROTARY: the cabinets */
        uint32_t i;
        h = 0x82000000u | ui.fxp << 20 | les_st.type << 16 | m << 12 | org_par.les_on[m] << 8 | fxi_st.type << 4 |
            fxi_st.man << 3 | (br & 7u) | (uint32_t)(mst_st.eq_on + 2u * mst_st.comp_on) << 27;
        if (h != drawn) {
            drawn = h;
            for (i = 0; i < 16u; i++) {
                padc_t c = pc(0, 0, 0, 0);
                if (ui.fxp == FXP_MASTER) { if (i < 2u) c = (i ? mst_st.comp_on : mst_st.eq_on) ? pc(255, 150, 0, 90) : pc(60, 60, 60, 12); }
                else if (ui.fxp == FXP_EFFECT) {            /* the effects (dark: not yet), the manuals */
                    if (i < 7u) c = i + 1u == fxi_st.type ? pc(255, 150, 0, 90) : FXI_READY[i + 1u] ? pc(60, 60, 60, 12) : pc(0, 0, 0, 0);
                    else if (i >= 8u && i < 8u + ORG_NMAN) c = i - 8u == fxi_st.man ? pc(0, 140, 255, 80) : pc(60, 60, 60, 12);
                } else if (ui.fxp == FXP_LESLIE) { if (i < LES_NT) c = i == les_st.type ? pc(255, 150, 0, 90) : pc(60, 60, 60, 12); }
                else if (i < ORG_NMAN) c = i == m ? pc(255, 150, 0, 90) : pc(60, 60, 60, 12);
                else if (i == 3u) c = org_par.les_on[m] ? pc(255, 220, 0, 90) : pc(60, 60, 60, 12);
                pad_out(i, c, br);
            }
            padled_flush();
        }
        return;
    }
    if (ui.mode == MODE_MIX) {          /* MIXER: the page's mutes (red) on top, its solos (dark blue) below */
        uint32_t i, mu = 0, so = (uint32_t)(mx.solo >> (mx.page * 8u)) & 0xFFu;
        for (i = 0; i < 8u; i++) mu |= mx_muted(mx.page * 8u + i) << i;
        h = 0x84000000u | mu << 12 | so << 4 | mx.page << 3 | (br & 7u);
        if (h != drawn) {
            drawn = h;
            for (i = 0; i < 8u; i++) {
                pad_out(i, mu >> i & 1u ? pc(255, 0, 0, 90) : pc(60, 60, 60, 12), br);
                pad_out(i + 8u, so >> i & 1u ? pc(0, 0, 255, 70) : pc(60, 60, 60, 12), br);
            }
            padled_flush();
        }
        return;
    }
    if (smk_midi(MS_CLONE)) {           /* CLONE: the channels cloned to */
        h = 0xA0000000u ^ ((uint32_t)zn.z[m].clone_mask << 8 | zn.z[m].out_ch << 3 | (zn.z[m].flags & Z_CLONE ? 4u : 0u) | m) ^ br << 24;
        if (h != drawn) { drawn = h; smk_pads_clone(m, br); }
        return;
    }
    if (smk_midi(MS_HARM)) {            /* HARMONIC: the drawbars sent, red (not for the pedal) */
        h = 0xB0000000u | (m == MAN_PEDAL ? 0u : (uint32_t)zn.z[m].harm_mask) << 4 | br;
        if (h != drawn) { drawn = h; smk_pads_mask(m == MAN_PEDAL ? 0u : zn.z[m].harm_mask, 9u, pc(255, 0, 0, 100), br); }
        return;
    }
    if (smk_midi(MS_ZONE)) {            /* ZONE: its switches, green (the zone's) / blue (for all) / amber (learn) */
        uint32_t i;
        h = 0x90000000u | br;
        for (i = 0; i < ZSW_N; i++) h = h * 3u + zone_sw_on(i);
        if (h != drawn) {
            drawn = h;
            for (i = 0; i < 16u; i++)
                pad_out(i, i >= ZSW_N ? pc(0, 0, 0, 0) : !zone_sw_on(i) ? pc(60, 60, 60, 12)
                           : i == 4u ? pc(255, 150, 0, 90) : i >= 6u && i < 10u ? pc(0, 140, 255, 80) : pc(40, 230, 80, 80), br);
            padled_flush();
        }
        return;
    }
    if (smk_midi(MS_CHORD)) {           /* CHORD: pad 1 capture (amber, bright once a chord is in), pad 2 clear */
        h = 0x88000000u | (uint32_t)zn.z[m].chord_n << 4 | br;
        if (h != drawn) { drawn = h; smk_pads_mask(1u | (zn.z[m].chord_n ? 2u : 0u), 2u, pc(255, 150, 0, zn.z[m].chord_n ? 90u : 45u), br); }
        return;
    }
    if (smk_yst()) {
        h = (((((yst.gen * 31u + yst.cur) * 31u + br) * 2u + ysb.on) * 4u + yst.run) * 2u + yst_blink()) * 131u + vb.held * 128u + vb.sent;
        h |= 0x80000000u;
        if (h != drawn) { drawn = h; smk_pads_yst(br); }
        return;
    }
    for (col = 0; col < 8u; col++) h = h * 31u + (col < n ? org_par.drawbar[m][col] : 15u);
    if (pnl_st.lay != LAY_FULL) h = h * 0x01000193u ^ smk_live_hash() ^ (pnl_st.lay * 0x5A5Au + ((uint32_t)zn.z[m].flags << 24) + ((uint32_t)zn.z[m].xf << 14) + (c_cpl_held != 0u) * 0x40000000u + (cpl_mask | cpl_raw() << 3) * 0x2000u);
    h = ((h * 31u + sch) * 31u + br + 1u) & 0x7FFFFFFFu;
    if (h == drawn) return;
    drawn = h;
    for (col = 0; col < 8u; col++) {
        padc_t top, bot;
        if (col < n) pad_pair(sch, col, org_par.drawbar[m][col] > 8u ? 8u : org_par.drawbar[m][col], &top, &bot);
        else top = bot = pc(0, 0, 0, 0);
        if (sch == PS_GLOW || sch == PS_SPECTRUM || sch == PS_SPEC_WARM) {   /* the study's rows swapped: the */
            padc_t t = top; top = bot; bot = t;     /* bottom pad (to 8) shows the drawbar out: 8 = bottom red */
        }                                           /* (FOOTAGE keeps its footage colour on top, SPLIT and DUAL their rows) */
        pad_out(col, pnl_st.lay == LAY_FULL ? top : pnl_st.lay == LAY_PADS2 ? smk_c_col(col) : smk_live_col(col), br);
        pad_out(col + 8u, pnl_st.lay == LAY_KNOBS ? smk_c_col(col) : bot, br);
    }
    padled_flush();
}

/* ---- the drawbars by the SMK's controls */
static void smk_db_set(uint32_t d, uint32_t v)   /* drawbar d (0..8) of the focused manual to v */
{
    uint32_t m = mls.focus;
    if ((m == MAN_PEDAL && d >= 4u) || d >= ORG_NDB) return;
    if (org_par.drawbar[m][d] != v) ctl_drawbar(d, (int32_t)v - (int32_t)org_par.drawbar[m][d]);
}

static uint32_t smk_key_taps(void)      /* the outer keys' events (the held keys come from fm1_in.keys) */
{
    uint32_t taps = 0;
    int32_t e;
    while ((e = smk_extkey_take()) >= 0) (void)e;   /* (a tap shorter than a main-loop round of the outer keys is lost) */
    taps = fm1_input_note_edges();      /* the middle 27: bits 5.. */
    return taps << 5;
}

/* ---- bank YST: faders 1..3 the SEQTRAK's touch sliders FX LEVEL, HIGH PASS, REPEATER (its FX page 1: the master
   effect's first parameter, the master's high pass and beat repeat; CC on channel 1, after seqtrak-docs: 102, 105,
   106), fader 4 the volume as everywhere. (Cutoff, resonance, attack, decay: K1..K4) */
static void smk_yst_fader(uint32_t f, uint32_t v)
{
    static const uint8_t CC[3] = {102, 105, 106};
    static const char *const NM[3] = {"FX LEVEL", "HIGH PASS", "REPEATER"};
    ml_q(0xB0u, CC[f], v);
    ctl_say_int(NM[f], (int32_t)v, 0);
}

/* fader 3: the MIDI out's sum, every channel's volume (CC 7) times it (ml_mvol); sent again to the channels heard (the
   zones', the SEQTRAK's tracks) from the tick, at most every 20 ms, the last value always */
static uint8_t mv_want = 127u;
static uint32_t mv_at;
static void mv_tick(void)
{
    uint32_t ch, heard;
    if (mv_want == ml_mvol || fm1_ms - mv_at < 20u) return;
    mv_at = fm1_ms;
    ml_mvol = mv_want;
    heard = ml_used_channels();
    for (ch = 0; ch < 16u; ch++) {
        if (mx_yst(ch)) { uint32_t v = yst.t[ch].v[YP_VOL]; if (v != YP_UNKNOWN) yst_cc(ch, 7u, ml_vol_out(v)); }
        else if (heard >> ch & 1u && !(mx.off >> ch & 1u)) ml_mix_send(ch);
    }
}

/* ---- the faders 1..3 pick up: after their job changed (the manual, the YST screen and back) or their value did
   (a preset, a setup, a knob), a fader does nothing until it reaches the value (or passes it); meanwhile the message
   line says which way. Fader 1 drawbar 9 (up 0, down 8), 2 the manual's reverb send, 3 the MIDI out's sum. (The YST
   screen's faders send the SEQTRAK's CCs as they are: their values are not known. Fader 4, the volume: platform.c) */
static uint8_t fad_pick[3], fad_set[3] = {0xFF, 0xFF, 0xFF}, fad_job[3];
static uint32_t fad_db(uint32_t v) { return ((127u - v) * 8u + 63u) / 127u; }   /* fader 1 -> drawbar 9 */
static int32_t fad_target(uint32_t f)  /* the value now, in the fader's units (fader 1: the drawbar's), -1: none */
{
    uint32_t m = mls.focus;
    if (f == 0u) return m == MAN_PEDAL ? -1 : (int32_t)org_par.drawbar[m][8];
    return f == 1u ? (int32_t)org_par.rsend[m] : (int32_t)mv_want;
}
static void fad_move(uint32_t f, uint32_t prev, uint32_t v)
{
    static const char *const NM[3] = {"DRAWBAR 9", "REVERB SEND", "MIDI OUT VOLUME"};
    uint32_t job = f == 2u ? 1u : 1u + mls.focus;
    int32_t cur = fad_target(f), a = f ? (int32_t)prev : (int32_t)fad_db(prev), b = f ? (int32_t)v : (int32_t)fad_db(v);
    char t[32];
    if (cur < 0) return;
    if (fad_job[f] != job || fad_set[f] != (uint32_t)cur) { fad_pick[f] = 0; fad_job[f] = (uint8_t)job; }
    if (!fad_pick[f]) {
        if ((a - cur) * (b - cur) > 0 && (b - cur > 1 || cur - b > 1 || f == 0u)) {   /* not there yet */
            cat(cat(t, NM[f]), (f == 0u) == (b > cur) ? ": FADER UP TO" : ": FADER DOWN TO");
            ctl_say_int(t, cur, 0);
            return;
        }
        fad_pick[f] = 1;
    }
    fad_set[f] = (uint8_t)b;
    if (f == 0u) smk_db_set(8, (uint32_t)b);
    else if (f == 1u) { org_par.rsend[mls.focus] = (uint8_t)b; ctl_say_int("REVERB SEND", b, 0); touched(); }
    else { mv_want = (uint8_t)b; ctl_say_int("MIDI OUT VOLUME", b, 0); }
}

/* the mod wheel: raw -> 0..127. The Elite's reads backwards (down: high) over a part of the range only, and no two
   alike: the travel is learned, the lowest and highest reading seen (from a narrow guess on), a few counts at
   either end the stop */
static uint32_t smk_mod_val(uint32_t raw)
{
    static int32_t lo = 440, hi = 570;
    int32_t r = (int32_t)raw, v;
    if (r < lo) lo = r;
    if (r > hi) hi = r;
    v = (hi - 6 - r) * 127 / (hi - lo - 12);
    return v < 0 ? 0u : v > 127 ? 127u : (uint32_t)v;
}

static void smk_master_pickup(void)     /* fader 4: the volume everywhere (hal: smk_master_hold) */
{
    smk_master_hold = 0;                /* (now everywhere the volume, YST too) */
}

/* ---- the MIDI area: 8 knobs, the zone's switches on the pads, ARP / PATCH the screens */
static void smk_zone_pad(uint32_t pad)  /* pads 1..5 the zone's switches, 6..10 midi_switch's second ones, 11 the
                                           zone's FILTER CC IN, 12 its MIDI OUT, 13 SUS IN */
{
    uint32_t sh = ui.shift;
    if (pad == 10u) { smk_fcci_toggle(); return; }
    if (pad == 11u) { smk_mout_toggle(); return; }
    if (pad == 12u) {                   /* SUS IN: a sustain coming in on the zone's channel holds the keys too */
        zone_t *z = &zn.z[mls.focus];
        z->xf ^= ZX_SUSK;
        if (!(z->xf & ZX_SUSK)) ml_key_sus(KSUS_IN, 0);
        ctl_say("SUSTAIN IN HOLDS KEYS", z->xf & ZX_SUSK ? "ON" : "OFF");
        touched();
        return;
    }
    if (pad > 12u) return;
    ui.shift = pad >= 5u;
    midi_switch(pad % 5u);
    ui.shift = sh;
}
static void smk_knob_midi(uint32_t f, int32_t st)   /* K1..K8 on a MIDI screen (knob_midi's two rows side by side) */
{
    uint32_t sh = ui.shift;
    if (ui.ms == MS_YST && ysb.on && f < 2u) { ysb_knob(f, st); return; }   /* the browser: K1 category, K2 sound */
    if (ui.ms == MS_YST) {              /* cutoff resonance attack decay volume pan reverb delay; SCALE + K1..K4 FM */
        yst_step(yst.cur, ui.shift && f < 4u ? YP_ALG + f : YP_CUTOFF + f, st);
        return;
    }
    if (ui.ms == MS_ZONE && sh && f == 5u) { ui.shift = 0; knob_midi(4u, st); }   /* SCALE + K6: the ORGAN OCTAVE */
    else if (ui.ms == MS_ZONE) { ui.shift = f & 1u; knob_midi(f >> 1, st); }   /* in out, lowest highest, transp octave, vel bend */
    else if (ui.ms == MS_CLONE && f < 2u) { ui.shift = 0; knob_midi(f ? 0u : 1u, st); }   /* K1 the octave, K2 the channel */
    else if (ui.ms == MS_HARM && f < 2u) { ui.shift = 0; knob_midi(f, st); }               /* K1 the channels, K2 what is sent */
    ui.shift = sh;
}
static void smk_midi_page(int32_t st)   /* ARP / PATCH: the previous / next MIDI screen */
{
    ui.ms = (ui.ms + MS_N + (uint32_t)st) % MS_N;
    mls.learn = 0;
    set_mode(MODE_MIDI);
    if (ui.ms == MS_YST) yst_follow_zone();
    ctl_say("MIDI", MS_NAME[ui.ms]);
}

/* ---- the chord memory (PERFORMANCE): hold CHORD and play a chord; let go: every key plays it, transposed;
   a short press without keys: off (and on again with the chord kept) */
static void smk_chord_mem(void)
{
    static uint8_t held, was_on;
    static uint64_t got;
    zone_t *z = &zn.z[mls.focus];
    uint32_t now = fm1_in.buttons >> BTN_ENV & 1u;
    if (now && !held) {
        if (ui.mode != MODE_PLAY) return;
        held = 1;
        got = 0;
        was_on = z->flags & Z_CHORD ? 1u : 0u;
        if (was_on) { ml_release_all(); z->flags &= (uint8_t)~Z_CHORD; }   /* (the new chord sounds plain) */
        return;
    }
    if (now) { got |= fm1_in.keys; return; }
    if (!held) return;
    held = 0;
    if (got) {                                              /* the chord: intervals over its lowest key */
        uint32_t k, low = 0, n = 0;
        while (!(got >> low & 1u)) low++;
        for (k = low; k < BOARD_NKEYS && n < ML_MAXCHORD; k++) if (got >> k & 1u) z->chord[n++] = (int8_t)(k - low);
        z->chord_n = (uint8_t)n;
        ml_release_all();
        z->flags |= Z_CHORD;
        ctl_say_int("CHORD MEMORY NOTES", (int32_t)n, 0);
    } else if (!was_on && z->chord_n) {
        ml_release_all();
        z->flags |= Z_CHORD;
        ctl_say("CHORD MEMORY", "ON");
    } else ctl_say("CHORD MEMORY", "OFF");
    touched();
}

/* ---- < >: on letting go (the list; SCALE + < >: the model; in YST the track's sound); both held together:
   K1 the drawbar list, K2 the model, K3 the panel layout (and no step when they are let go) */
static uint32_t smk_lr(void)            /* 1 = both are held */
{
    static uint8_t prev, both;
    uint32_t now = (fm1_in.buttons >> BTN_LEFT & 1u) | (fm1_in.buttons >> BTN_RIGHT & 1u) << 1, up = prev & ~now;
    prev = (uint8_t)now;
    if (now == 3u) both = 1;
    if (up && !both) {
        int32_t st = up & 2u ? 1 : -1;
        if (ui.mode == MODE_MIX) {                      /* the mixer: the parameter on the knobs */
            mx.par = (uint8_t)((mx.par + MXP_N + (uint32_t)st) % MXP_N);
            ctl_say("MIXER", MXP_NAME[mx.par]);
        } else if (smk_yst()) yst_browse_kit(st);
        else if (ui.shift) ctl_model_step(st);
        else ctl_preset_step(st);
    }
    if (!now) both = 0;
    if (now != 3u) lr_turned = 0;
    lr_both = now == 3u;
    return now == 3u;
}

/* ---- the pedal socket (raw 0..1023). A sustain pedal: it holds the keys let go, inside and out, and sends CC 64 on
   the focused zone's channels; either kind (up is how it rests at power-on: plugged in, not pressed). An expression
   pedal: the swell (the expression, CC 11 out on the same channels), its travel learned as the mod wheel's. PEDAL on
   the WHEELS page (pnl_st.ped): AUTO takes an expression pedal for one when it rests between the ends at power-on or
   stays there for 0.3 s (a sustain pedal only passes), until power-off; SUSTAIN, SWELL fixed */
static uint16_t ped_rest = 0xFFFFu;
static uint8_t ped_down;
static void smk_sustain(uint32_t down)
{
    uint32_t ch, m = zn.z[mls.focus].flags & Z_NOCC ? 0u : zone_ctl_mask(&zn.z[mls.focus], 0);
    ml_key_sus(KSUS_PANEL, down);       /* (a sustain coming in may still hold them: SUS IN) */
    for (ch = 0; ch < 16u; ch++) if (m >> ch & 1u) ml_q(0xB0u | ch, 64, down ? 127u : 0u);
}
#define PED_MID_LO 150u                 /* AUTO: between these, an expression pedal (a sustain pedal reads its ends) */
#define PED_MID_HI 900u
#define PED_MIN 40u                     /* the swell closed: 10 % of the level (the expression squares: 40^2 / 127^2) */
static void smk_swell(uint32_t v)       /* v: the pedal's travel, 0..127 */
{
    uint32_t e = PED_MIN + (v * (127u - PED_MIN) + 63u) / 127u, ch;
    uint32_t m = zn.z[mls.focus].flags & Z_NOCC ? 0u : zone_ctl_mask(&zn.z[mls.focus], 0);
    org_par.expression = (uint8_t)e;
    org_par.roller = (uint8_t)v;        /* (the romantic organ's crescendo roller: the pedal's whole travel) */
    for (ch = 0; ch < 16u; ch++) if (m >> ch & 1u) ml_q(0xB0u | ch, 11, e);
    ctl_say_int(org_par.model == MODEL_ROMANTIC ? "CRESCENDO %" : "SWELL %", (int32_t)(v * 100u / 127u), 0);
}
static void smk_swell_tick(uint32_t r)
{
    uint32_t a, v;
    ped.acc = (uint16_t)(ped.acc + ((int32_t)r * 16 - ped.acc) / 4);   /* x16, smoothed */
    a = ped.acc / 16u;
    if (a < ped.lo) ped.lo = (uint16_t)a;
    if (a > ped.hi) ped.hi = (uint16_t)a;
    if (ped.hi < ped.lo + 200u) return; /* (the travel not learned yet: once all the way) */
    v = a < ped.lo + 6u ? 0u : (a - ped.lo - 6u) * 127u / (ped.hi - ped.lo - 12u);
    if (v > 127u) v = 127u;
    if (ped.swell != 0xFFu && v + 2u > ped.swell && v < ped.swell + 2u && !(v != ped.swell && (v == 0u || v == 127u))) return;
    ped.swell = (uint8_t)v;             /* (+-1 dead band, the ends reachable) */
    smk_swell(v);
}
static void smk_pedal_mode(void)        /* PEDAL changed: sustain up, the swell open, its travel and rest taken anew */
{
    if (ped_down) { ped_down = 0; smk_sustain(0); }
    if (ped.swell != 0xFFu) { ped.swell = 0xFFu; smk_swell(127); }
    ped_rest = (uint16_t)fm1_in.pedal;
    ped.lo = 0xFFFFu; ped.hi = 0; ped.acc = (uint16_t)(fm1_in.pedal * 16u);
    ped.mid_at = fm1_ms;
}
static void smk_sustain_tick(void)
{
    uint32_t r = fm1_in.pedal, d, now;
    if (fm1_ms < 1500u) return;         /* (once the ADC has settled) */
    if (ped_rest == 0xFFFFu) {
        ped_rest = (uint16_t)r; ped.acc = (uint16_t)(r * 16u); ped.mid_at = fm1_ms;
        ped.expr = r >= PED_MID_LO - 90u && r <= PED_MID_HI + 50u;   /* resting between the ends: 60..950 */
        return;
    }
    if (pnl_st.ped == PJ_AUTO && !ped.expr) {
        if (r < PED_MID_LO || r > PED_MID_HI) ped.mid_at = fm1_ms;
        else if (fm1_ms - ped.mid_at > 300u) {
            ped.expr = 1;
            if (ped_down) { ped_down = 0; smk_sustain(0); }
            ctl_say("PEDAL", "EXPRESSION: THE SWELL");
        }
    }
    if (ped_swell()) { smk_swell_tick(r); return; }
    d = r > ped_rest ? r - ped_rest : ped_rest - r;
    now = ped_down ? d > 250u : d > 400u;   /* (a gap: no flutter) */
    if (now != ped_down) { ped_down = (uint8_t)now; smk_sustain(now); }
}

static void smk_knob_fx(uint32_t f, int32_t st)
{
    uint32_t sh = ui.shift;
    if (ui.fxp == FXP_LESLIE) { les_step(f, st); return; }
    if (ui.fxp == FXP_EFFECT) { fxi_step(f, st); return; }
    if (ui.fxp == FXP_MASTER) { mst_step(ui.shift ? (f < 3u ? MSP_LFREQ + f : MSP_N) : f, st); return; }   /* SCALE + K1..K3 */
    if (ui.fxp == FXP_WHEELS) { whl_knob(f, st); return; }
    ui.shift = f >= 4u;                                     /* (knob_fx: SCALE = the shared ones) */
    knob_fx(f & 3u, st);
    ui.shift = sh;
}

static uint32_t smk_controls(void)      /* pads, faders, wheels; on the drawbars the 8 knobs: 1 = they were taken */
{
    static uint8_t fad_last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    static int32_t bend_last;
    static uint32_t mod_last = 0xFFFFu;
    static smk_pw_t pw;
    uint32_t f, prev, play = ui.mode == MODE_PLAY;
    int32_t e, st;
    smk_chord_mem();
    smk_banks();
    while ((e = smk_pad_take()) >= 0) {                     /* a pad: top row to 0, bottom row to 8 */
        uint32_t pad = (uint32_t)e >> 8 & 15u;
        if (!(e & 0xFF)) {                                  /* let go: B's and C's pads 1 and 3 */
            if (pnl_st.lay != LAY_FULL) smk_live_up(pad);
            if (c_cpl_held && pad + 1u == c_cpl_held) { c_cpl_held = 0; if (!c_cpl_used) cpl_toggle(); }
            if (pad == 11u) vb_up();                        /* (the action pad, wherever we are now) */
            continue;
        }
        if (lr_both && !lr_turned) smk_clone_pad(pad);    /* < + > held: the cloner's channels (K1..K3 turned: the layout's pads) */
        else if (fm1_in.buttons >> BTN_SAVE & 1u) {         /* BT + pad: store setup (ROTARY: pads 3, 4 copy) */
            ui.save_used = 1;
            if (ui.mode == MODE_FX && ui.fxp == FXP_LESLIE) { if (pad == 2u || pad == 3u) les_copy(pad - 2u); }
            else { ctl_setup_store(pad); pnl_setup_store(pad); }
        }
        else if (smk_yst()) {                               /* YST: a track; SCALE + pad: its mute */
            if (pad < YST_NTRK) { if (ui.shift) yst_mute(pad); else yst_select(pad); }
            else if (pad == 11u) vb_down((uint32_t)e & 0xFFu);   /* pad 12: the action pad */
            else if (pad < 15u) yst_transport(pad);         /* 13..15: STOP PLAY / PAUSE REC */
            else if (pad == 15u) ysb_open(!ysb.on);         /* pad 16: the sound browser */
        } else if (smk_d_here() && !ui.shift) {             /* D: the top pads switch, the bottom ones the pages */
            if (pad & 8u) smk_d_pad(pad & 7u); else smk_live_pad(pad);
        } else if (ui.mode == MODE_FX) {                    /* FX: pads 1-3 the manual, 4 its rotary; ROTARY: 1-4 */
            if (ui.fxp == FXP_MASTER) { if (pad < 2u) mst_toggle(pad); }
            else if (ui.fxp == FXP_EFFECT) { if (pad < 7u) fxi_pick(pad + 1u); else if (pad >= 8u && pad < 8u + ORG_NMAN) fxi_man(pad - 8u); }
            else if (ui.fxp == FXP_LESLIE) { if (pad < LES_NT) les_pick(pad); }
            else if (pad < ORG_NMAN) focus_set(pad);
            else if (pad == 3u) les_toggle();
        } else if (ui.mode == MODE_MIX) {                   /* MIXER: top pads mute, bottom pads solo */
            if (pad < 8u) mx_mute(mx.page * 8u + pad); else mx_solo(mx.page * 8u + pad - 8u);
        } else if (ui.shift) { recall_setup(pad); pnl_setup_recall(pad); mx.off = 0; }   /* SCALE + pad: setup 1..16 (mutes sent again) */
        else if (smk_midi(MS_CLONE)) smk_clone_pad(pad);   /* CLONE: the channels */ else if (smk_midi(MS_HARM)) {                     /* HARMONIC: the drawbars sent */
            if (pad < 9u && mls.focus != MAN_PEDAL) {
                ml_release_all();
                zn.z[mls.focus].harm_mask ^= (uint16_t)(1u << pad);
                ml_harm_resend();
                touched();
            }
        } else if (smk_midi(MS_ZONE)) smk_zone_pad(pad);   /* ZONE: its switches */
        else if (smk_midi(MS_CHORD)) {                      /* CHORD: pad 1 takes the chord held, pad 2 clears it */
            if (pad < 2u) midi_switch(pad + 1u);
        } else if (pnl_st.lay != LAY_FULL) {               /* B1, C: the top pads switch; B2: C's bottom row on top */
            if (!(pad & 8u)) { if (pnl_st.lay == LAY_PADS2) smk_c_pad(pad, pad); else smk_live_pad(pad); }
            else if (pnl_st.lay == LAY_KNOBS) smk_c_pad(pad & 7u, pad);
            else smk_db_flip(pad & 7u);
        }
        else smk_db_set(pad & 7u, pad & 8u ? 8u : 0u);
    }
    for (f = 0; f < 4u; f++) {                              /* the faders: only when moved (they start where they are) */
        uint32_t v = fm1_in.fader[f];
        if (fm1_ms < 1500u || fad_last[f] == 0xFFu) { fad_last[f] = (uint8_t)v; continue; }
        if (v + 2u > fad_last[f] && v < fad_last[f] + 2u && !(v != fad_last[f] && (v == 0u || v == 127u))) continue;
        prev = fad_last[f];
        fad_last[f] = (uint8_t)v;
        if (f == 3u) continue;                              /* fader 4: the volume (platform.c, MASTER) */
        if (smk_yst()) { smk_yst_fader(f, v); fad_job[f] = 0; continue; }   /* (the faders: picked up again after) */
        fad_move(f, prev, v);                               /* drawbar 9, the reverb send, the MIDI out's sum */
    }
    if (fm1_ms >= 1500u) {                                  /* the wheels, once the ADC has settled */
        int32_t b = smk_pw_bend(&pw, fm1_in.wheel[0], fm1_ms);   /* +-8192 = +-2 semitones' travel */
        uint32_t mv = smk_mod_val(fm1_in.wheel[1]);
        if (b > 8191) b = 8191;
        if (b != bend_last) { bend_last = b; whl_bend(b); }
        if (mod_last == 0xFFFFu) mod_last = mv;
        else if (mv + 4u <= mod_last || mv >= mod_last + 4u || (mv != mod_last && (mv == 0u || mv == 127u))) {
            mod_last = mv;
            whl_mod(mv);                                    /* (the WHEELS page: what it moves) */
        }
    }
    smk_sustain_tick();
    vb_tick();
    mv_tick();
    cpl_tick();
    smk_master_pickup();
    mx_apply();
    les_tick();
    if (!smk_yst() && ysb.on == 1u) ysb.on = 0;
    if (ysb.on == 2u && !smk_lr()) ysb_quick_end();
    ysb_tick();
    smk_pads_show();
    if (smk_lr() && ui.mode != MODE_MIX) {              /* < + > held: K4 the YST category, K5 the sound (the browser) */
        if ((st = fm1_enc_take(3)) != 0) ysb_quick(0, st);
        if ((st = fm1_enc_take(4)) != 0) ysb_quick(1, st);
    }
    if (smk_lr() && ui.mode != MODE_MIDI && ui.mode != MODE_MIX) {                 /* < + > held: K1 the drawbar list, K2 the model, K3 the layout */
        if ((st = fm1_enc_take(0)) != 0) { ctl_preset_step(st); lr_turned = 1; }
        if ((st = fm1_enc_take(1)) != 0) { ctl_model_step(st); lr_turned = 1; }
        if ((st = fm1_enc_take(2)) != 0) { pnl_layout_step(st, 0); lr_turned = 1; }   /* (A and D the ends; active at once) */
        for (f = 3; f < 8u; f++) (void)fm1_enc_take(f);
        return 1;
    }
    if (ui.mode == MODE_FX) {                               /* FX: K1..K4 the manual's, K5..K8 shared; ROTARY */
        for (f = 0; f < 8u; f++)
            if ((st = fm1_enc_take(f)) != 0) {
                if (smk_live_takes(f) && smk_d_here()) smk_live_knob(f, st);   /* D: pad 1, 3, 6, 7 or 8 held */
                else smk_knob_fx(f, st);
            }
        return 1;
    }
    if (ui.mode == MODE_MIX) {                              /* the mixer: K1..K8 the page's channels */
        for (f = 0; f < 8u; f++)
            if ((st = fm1_enc_take(f)) != 0) {
                if (smk_live_takes(f) && smk_d_here()) smk_live_knob(f, st);
                else mx_step(f, st);
            }
        return 1;
    }
    if (ui.mode == MODE_MIDI) {                             /* the MIDI screens: K1..K8 */
        for (f = 0; f < 8u; f++)
            if ((st = fm1_enc_take(f)) != 0) smk_knob_midi(f, st);
        return 1;
    }
    if (ui.mode == MODE_LIGHT) {                            /* the lights: K1..K3, K5 the tuning (knob_light) */
        for (f = 0; f < 8u; f++)
            if ((st = fm1_enc_take(f)) != 0 && (f < 3u || f == 4u)) knob_light(f, st);
        return 1;
    }
    if (!play) { (void)fm1_enc_take(ENC_MASTER); return 0; }   /* (K1 has no job off the drawbars) */
    for (f = 0; f < 8u; f++)
        if ((st = fm1_enc_take(f)) != 0) {
            uint32_t m = mls.focus;
            if (smk_live_takes(f) && pnl_st.lay != LAY_FULL) { smk_live_knob(f, st); continue; }   /* B, C: pad 1, 3, 6, 7 or 8 held */
            if (m == MAN_PEDAL && f >= 4u) continue;
            ctl_drawbar(f, st);
        }
    return 1;
}

/* ---- KNOB BANK and FADER BANK, on letting go (smk_controls); both together: HOME.
   KNOB BANK the next manual (the mixer: its other 8 channels; SCALE + KNOB BANK: the next layout). FADER BANK the
   next performance layout, A B1 B2 C D A (anywhere else: back to the drawbars first). HOME: the upper manual, model 1,
   its first drawbar preset, every coupler off, layout A (the cloner as it is) */
static void smk_home(void)
{
    char t[24];
    set_mode(MODE_PLAY);
    cpl_apply(0);                       /* (it lets go of the notes) */
    focus_set(MAN_UPPER);
    set_model(0);
    dbp_recall(MAN_UPPER, 0);
    pnl_st.lay = LAY_ORDER[0];
    pnl_touch();
    dbp_name(t, MAN_UPPER, 0);
    ctl_say("HOME", "UM, MODEL 1, P1, PERF A, NO CPL");
    board_splash("HOME", ORG_MODELS[0].name, t);
    touched();
}
static void smk_banks(void)
{
    static uint8_t prev, both;
    uint32_t now = (fm1_in.buttons >> BTN_KNOB_BANK & 1u) | (fm1_in.buttons >> BTN_FADER_BANK & 1u) << 1, up = prev & ~now;
    prev = (uint8_t)now;
    if (now == 3u && !both) { both = 1; smk_home(); }
    if (up && !both) {
        if (up & 1u) {
            if (ui.mode == MODE_MIX) { mx.page ^= 1u; ctl_say("MIXER", mx.page ? "CHANNELS 9-16" : "CHANNELS 1-8"); }
            else if (ui.shift && ui.mode == MODE_PLAY) pnl_layout_step(1, 1);   /* SCALE + KNOB BANK: the layout */
            else focus_set((mls.focus + 1u) % ORG_NMAN);
        }
        if (up & 2u) {
            if (ui.mode != MODE_PLAY) { set_mode(MODE_PLAY); ctl_say("PAGE", "DRAWBARS"); }
            else pnl_layout_step(1, 1);
        }
    }
    if (!now) both = 0;
}

static uint32_t smk_buttons(uint32_t b)  /* the SMK's own buttons; returns what the FM-1's panel still does */
{
    b &= ~(1u << BTN_KNOB_BANK | 1u << BTN_FADER_BANK | 1u << BTN_LEFT | 1u << BTN_RIGHT);
    if (ui.mode == MODE_FX) {                             /* ARP / PATCH: the pages FX, ROTARY */
        if (b & 1u << BTN_ARP) ui.fxp = (ui.fxp + FXP_N - 1u) % FXP_N;
        if (b & 1u << BTN_HOME && !ui.shift) ui.fxp = (ui.fxp + 1u) % FXP_N;
        return b & ~(1u << BTN_ARP | 1u << BTN_ENV | 1u << BTN_LFO | (ui.shift ? 0u : 1u << BTN_HOME));
    }
    if (ui.mode == MODE_MIDI) {                           /* ARP / PATCH the screens, PARA the mixer; ZONE: pads only */
        if (b & 1u << BTN_ARP) smk_midi_page(-1);
        if (b & 1u << BTN_HOME && !ui.shift) smk_midi_page(1);
        if (b & 1u << BTN_EDIT) set_mode(MODE_MIX);
        b &= ~(1u << BTN_ARP | 1u << BTN_EDIT | (ui.shift ? 0u : 1u << BTN_HOME));   /* (SCALE + PATCH: panic) */
        if (ui.ms == MS_ZONE) b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_SEQ);
        return b;
    }
    if (ui.mode == MODE_PLAY) {
        uint32_t out = b & ~(1u << BTN_ARP | 1u << BTN_HOME | 1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT);
        if (b & 1u << BTN_ARP) smk_db_set(8, 0);
        if (b & 1u << BTN_HOME) {                           /* SCALE + PATCH: panic, as everywhere */
            if (ui.shift) out |= 1u << BTN_HOME; else smk_db_set(8, 8);
        }
        if (b & 1u << BTN_LFO) out |= 1u << BTN_ARP;        /* NOTE RPT: the vibrato (CHORD: smk_chord_mem) */
        if (b & 1u << BTN_EDIT) out |= 1u << BTN_HOME;      /* PARA: the mixer */
        return out;
    }
    if (ui.mode == MODE_MIX && (b & 1u << BTN_EDIT)) b = (b & ~(1u << BTN_EDIT)) | 1u << BTN_HOME;   /* PARA: back */
    return b;
}

static void smk_leds(uint8_t *lv)
{
    uint32_t i;
    if (ui.mode == MODE_PLAY) {
        uint32_t d9 = mls.focus == MAN_PEDAL ? 0u : org_par.drawbar[mls.focus][8];
        lv[BTN_LFO] = lv[BTN_ARP];                          /* NOTE RPT: the vibrato */
        lv[BTN_ENV] = zn.z[mls.focus].flags & Z_CHORD || fm1_in.buttons >> BTN_ENV & 1u ? 2u : 0u;   /* CHORD memory */
        lv[BTN_ARP] = d9 < 4u ? 2u : 0u;                    /* as the pads: ARP (to 0) lit while drawbar 9 is in, */
        lv[BTN_HOME] = d9 >= 4u ? 2u : 0u;                  /* PATCH (to 8) while it is out */
        lv[BTN_EDIT] = 0;
    } else if (ui.mode == MODE_MIX) lv[BTN_EDIT] = 2u;
    else if (ui.mode == MODE_FX) {
        lv[BTN_ENV] = lv[BTN_LFO] = 0;
        lv[BTN_ARP] = lv[BTN_HOME] = 1u;                    /* (the pages, around) */
    }
    for (i = BTN_KNOB_BANK; i <= BTN_RIGHT; i++) fm1_led_key(i, 0);
}

/* ---- the lights screen: K1 the pads' colours, K3 their brightness (the keys have no LEDs; K2 the buttons) */
static uint32_t smk_knob_light(uint32_t k, int32_t st)
{
    uint32_t s = smk_pad_scheme(), br = smk_pad_bright();
    if (k == 0u) {
        s = (uint32_t)clampi((int32_t)s + st, 0, PS_N - 1);
        ctl_say("PAD COLOURS", PS_NAME[s]);
    } else if (k == 2u) {
        br = (uint32_t)clampi((int32_t)br + st, 1, 8);
        ctl_say_int("PAD BRIGHTNESS", (int32_t)br, 0);
    } else return 0;
    st0.pads = (uint8_t)(s | (8u - br) << 3);
    return 1;
}
static void smk_light_cells_(char lab[4][10], char val[4][10], uint16_t *col)
{
    cat(lab[0], "PADS");
    cat(val[0], PS_TAG[smk_pad_scheme()]);
    cat(lab[2], "PAD BRT");
    org_fmt(val[2], (int32_t)smk_pad_bright(), 0);
    (void)col;
}

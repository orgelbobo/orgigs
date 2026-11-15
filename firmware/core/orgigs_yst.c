/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs YST: a Yamaha SEQTRAK on the USB cable (main loop only). The SEQTRAK's 11 tracks sit on MIDI channels
 * 1..11: 1..7 drum tracks (OS 2.0: each can become a synth), 8 and 9 synth, 10 DX, 11 sampler.
 *
 * What it does:
 *   - is MIDI module 5, switched on per setup (zn.yst); off, it sends nothing and takes nothing
 *   - reads the SEQTRAK only when asked: switched on, the USB cable plugged in, or REFRESH (after a project change
 *     there). It asks the type and the sound of every track, one question at a time (the USB takes in one SysEx at a
 *     time), and never waits for room to send: a question that cannot go out now is counted and dropped
 *   - follows what the SEQTRAK sends: sound changes (bank select 63 / 62 + program change), its knobs (CC 74 71 73
 *     75 7 10 91 94 26 116..119), a track re-typed on the panel (SysEx 01 1p 3C). While the module is on,
 *     everything on channels 1..11 from the USB and the clock are the SEQTRAK's: they do not reach the zones
 *   - sends: the parameters as CC on the track's channel, a sound as bank select + program change, mute CC 23
 *   - the selected track: the focused zone's MIDI out channel follows it, so the keys play that track; it is sent
 *     to the SEQTRAK (CurrentPart 01 10 27), and a track chosen on the SEQTRAK (it sends 01 10 27) is followed
 * The protocol is from jaeggerr/seqtrak-docs (MIT): SysEx F0 43 1n/3n 7F 1C 0C ah am al data F7, the sound list
 * firmware/gen/yst_presets.h (tools/gen_yst_presets.py). The panel's parts are the boards' (the YST screen, the
 * SMK-37's bank YST). */

#include "yst_presets.h"

#define YST_NTRK 11u
enum { YT_NONE, YT_DRUM, YT_SYNTH, YT_DRUMKIT, YT_DX, YT_SAMPLER };      /* the SEQTRAK's PartAwmType */
enum { YP_CUTOFF, YP_RES, YP_ATTACK, YP_DECAY, YP_VOL, YP_PAN, YP_REV, YP_DLY, YP_ALG, YP_FMAMT, YP_FMFRQ, YP_FMFB,
       YP_MODE, YP_N };
static const uint8_t YP_CC[YP_N] = {74, 71, 73, 75, 7, 10, 91, 94, 116, 117, 118, 119, 26};
static const char *const YP_NAME[YP_N] = {"CUTOFF", "RESONANCE", "ATTACK", "DECAY", "VOLUME", "PAN", "REVERB",
                                          "DELAY", "FM ALGORITHM", "FM AMOUNT", "FM FREQUENCY", "FM FEEDBACK", "VOICE"};
static const uint8_t YP_MAX[YP_N] = {127, 127, 127, 127, 127, 127, 127, 127, 11, 127, 127, 127, 2};
#define YP_UNKNOWN 0xFFu
static const char *const YST_TRACK[YST_NTRK] = {"KICK", "SNARE", "CLAP", "HAT 1", "HAT 2", "PERC 1", "PERC 2",
                                                "SYNTH 1", "SYNTH 2", "DX", "SAMPLER"};
static const char *const YST_TAG[YST_NTRK] = {"KCK", "SNR", "CLP", "HH1", "HH2", "PC1", "PC2", "SY1", "SY2", "DX", "SMP"};
static const char *const YT_NAME[6] = {"?", "DRUM", "SYNTH", "KIT", "DX", "SAMPLER"};
static const char *const YMODE_NAME[3] = {"MONO", "POLY", "CHORD"};

typedef struct {
    uint16_t snd;                       /* the sound's number (1..; 0: not known) in its list (MSB 63 or 62) */
    uint8_t type, mute, msb, lsb;       /* YT_...; muted (by us); the bank select coming in */
    uint8_t v[YP_N];                    /* the parameters as last sent or heard, YP_UNKNOWN */
} yst_trk_t;

static struct {
    yst_trk_t t[YST_NTRK];
    uint8_t cur;                        /* the selected track */
    uint8_t on;                         /* the SEQTRAK answered (since the cable came) */
    uint8_t ask;                        /* the question on its way: 0..21 (type, sound of each track), 22 none */
    uint8_t wait, tries;                /* ask sent, no answer yet; times sent */
    uint8_t usb, en;                    /* the USB host was there, the module on (last look) */
    uint8_t run;                        /* the sequencer: 0 stopped, 1 playing, 2 paused (ours; FA FB FC heard too) */
    uint16_t lost, silent;              /* questions that could not go out; that got no answer */
    uint16_t midi_in, sx_in;            /* (diagnosis) messages from the USB while on; Yamaha SysEx frames */
    uint8_t sx_last[10], sx_last_n;     /* (diagnosis) the last Yamaha frame's first bytes */
    uint32_t ask_ms;
    uint32_t gen;                       /* counts the changes (the screens redraw) */
} yst;

static void yst_reset(void)
{
    uint32_t i;
    static const uint8_t TYPE0[YST_NTRK] = {YT_DRUM, YT_DRUM, YT_DRUM, YT_DRUM, YT_DRUM, YT_DRUM, YT_DRUM,
                                            YT_SYNTH, YT_SYNTH, YT_DX, YT_SAMPLER};
    for (i = 0; i < YST_NTRK; i++) {
        memset(&yst.t[i], 0, sizeof yst.t[i]);
        memset(yst.t[i].v, YP_UNKNOWN, YP_N);
        yst.t[i].type = TYPE0[i];
    }
    yst.gen++;
}

/* ---- the sound list */
static uint32_t yst_smp(uint32_t t) { return yst.t[t].type == YT_SAMPLER; }
static void yst_range(uint32_t t, uint32_t *lo, uint32_t *hi)   /* the sounds a track's type can load */
{
    switch (yst.t[t].type) {
    case YT_SYNTH: *lo = YST_SYNTH1; *hi = YST_DX1 - 1u; break;
    case YT_DX: *lo = YST_DX1; *hi = YST_N63; break;
    case YT_SAMPLER: *lo = 1; *hi = YST_NSMP; break;
    default: *lo = YST_DRUM1; *hi = YST_SYNTH1 - 1u; break;
    }
}
static const char *yst_sound(uint32_t t, const char **cat)    /* the selected sound's name and category, 0 */
{
    uint32_t n = yst.t[t].snd, smp = yst_smp(t);
    if (!n || n > (smp ? YST_NSMP : YST_N63)) { if (cat) *cat = ""; return 0; }
    if (cat) *cat = YST_CAT[smp ? YST_CATSMP[n - 1u] : YST_CAT63[n - 1u]];
    return YST_NAMES + (smp ? YST_OFFSMP[n - 1u] : YST_OFF63[n - 1u]);
}

/* the boards' channel labels: a SEQTRAK track's tag on channels 1..11 while it is there, else 0 */
static const char *yst_ch_tag(uint32_t ch) { return yst.on && ch < YST_NTRK ? YST_TAG[ch] : 0; }

/* ---- out */
static uint32_t yst_sx_req(uint32_t ah, uint32_t am, uint32_t al)   /* a parameter request; 0: no room now */
{
    uint8_t m[10] = {0xF0, 0x43, 0x30, 0x7F, 0x1C, 0x0C, (uint8_t)ah, (uint8_t)am, (uint8_t)al, 0xF7};
    if (!sysex_room(sizeof m) || sysex_send(m, sizeof m)) { yst.lost++; yst.gen++; return 0; }
    return 1;
}
static void yst_sx_set(uint32_t ah, uint32_t am, uint32_t al, uint32_t v)   /* a parameter change, if there is room */
{
    uint8_t m[11] = {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x0C, (uint8_t)ah, (uint8_t)am, (uint8_t)al, (uint8_t)v, 0xF7};
    if (!sysex_room(sizeof m) || sysex_send(m, sizeof m)) { yst.lost++; yst.gen++; }
}
static void yst_cc(uint32_t t, uint32_t cc, uint32_t v) { ml_q(0xB0u | t, cc, v); }

static void yst_set(uint32_t t, uint32_t p, uint32_t v)        /* parameter p of track t to v, sent */
{
    if (t >= YST_NTRK || p >= YP_N) return;
    if (v > YP_MAX[p]) v = YP_MAX[p];
    yst.t[t].v[p] = (uint8_t)v;
    yst_cc(t, YP_CC[p], p == YP_VOL ? ml_vol_out(v) : v);   /* (the volume: x the MIDI out's sum) */
    yst.gen++;
}
static void yst_step(uint32_t t, uint32_t p, int32_t st)       /* an encoder: from the value known (else the middle) */
{
    int32_t v = yst.t[t].v[p] == YP_UNKNOWN ? (YP_MAX[p] + 1) / 2 : yst.t[t].v[p];
    char s[24];
    if (p >= YP_ALG && p <= YP_FMFB && yst.t[t].type != YT_DX) { ctl_say(YP_NAME[p], "DX TRACK ONLY"); return; }
    v = clampi(v + (p == YP_ALG || p == YP_MODE ? st : st * 2), 0, YP_MAX[p]);
    yst_set(t, p, (uint32_t)v);
    cat(cat(cat(s, YST_TAG[t]), " "), YP_NAME[p]);
    if (p == YP_ALG) ctl_say_int(s, v + 1, 0);
    else if (p == YP_PAN) ctl_say_int(s, v - 64, 1);
    else ctl_say_int(s, v, 0);
}

static void yst_load(uint32_t t, uint32_t n)                   /* sound n of the track's list */
{
    const char *nm;
    yst.t[t].snd = (uint16_t)n;
    yst_cc(t, 0, yst_smp(t) ? 62u : 63u);
    yst_cc(t, 32, (n - 1u) >> 7);
    ml_q(0xC0u | t, (n - 1u) & 127u, 0);
    yst.gen++;
    nm = yst_sound(t, 0);
    ctl_say(YST_TAG[t], nm ? nm : "?");
}
static void yst_browse(int32_t st)                             /* the selected track's next / previous sound */
{
    uint32_t t = yst.cur, lo, hi, n = yst.t[t].snd;
    yst_range(t, &lo, &hi);
    n = n < lo || n > hi ? lo : (uint32_t)clampi((int32_t)n + st, (int32_t)lo, (int32_t)hi);
    yst_load(t, n);
}

static void yst_mute(uint32_t t)
{
    yst.t[t].mute ^= 1u;
    yst_cc(t, 23, yst.t[t].mute ? 127u : 0u);
    ctl_say(YST_TRACK[t], yst.t[t].mute ? "MUTED" : "ON");
    yst.gen++;
}
static void yst_voice(uint32_t t)                              /* mono, poly, chord (synth and DX tracks) */
{
    uint32_t m = yst.t[t].v[YP_MODE];
    if (yst.t[t].type != YT_SYNTH && yst.t[t].type != YT_DX) { ctl_say("VOICE", "SYNTH TRACKS ONLY"); return; }
    m = m == YP_UNKNOWN ? 1u : (m + 1u) % 3u;
    yst_set(t, YP_MODE, m);
    ctl_say(YST_TRACK[t], YMODE_NAME[m]);
}

static void yst_pick(uint32_t t, uint32_t send)   /* the selected track; the focused zone plays it; send: tell the SEQTRAK */
{
    zone_t *z = &zn.z[mls.focus];
    const char *nm;
    char s[24];
    if (t >= YST_NTRK) return;
    yst.cur = (uint8_t)t;
    if (send && yst.on) yst_sx_set(0x01, 0x10, 0x27, t);   /* CurrentPart: the SEQTRAK selects it too */
    if (z->out_ch != t) {
        ml_release_all();
        z->out_ch = (uint8_t)t;
        touched();
    }
    nm = yst_sound(t, 0);
    org_fmt(cat(cat(s, YST_TRACK[t]), " CH "), (int32_t)t + 1, 0);
    ctl_say(s, nm ? nm : YT_NAME[yst.t[t].type]);
    yst.gen++;
}
static void yst_select(uint32_t t) { yst_pick(t, 1); }
static void yst_follow_zone(void)       /* the YST screen opens on the track the focused zone plays */
{
    uint32_t ch = zn.z[mls.focus].out_ch;
    if (ch < YST_NTRK && ch != yst.cur) { yst.cur = (uint8_t)ch; yst.gen++; }
}

/* ---- in */
/* 1: the SEQTRAK's, taken. While the module is on, everything from the USB on channels 1..11 (the SEQTRAK's knobs,
   keys, patterns) and the clock stay here: the zones would send it straight back to it, doubled notes that hang */
static uint32_t yst_midi_in(uint8_t st, uint8_t d1, uint8_t d2, uint32_t usb)
{
    uint32_t t = st & 15u, type = st & 0xF0u, p;
    yst_trk_t *k;
    if (!zn.yst || !usb) return 0;
    if (!yst.on && st != 0xF8u) { yst.midi_in++; yst.gen++; }   /* (diagnosis, not the clock: the screen would redraw) */
    if (st == 0xFAu || st == 0xFBu) { if (yst.run != 1u) { yst.run = 1; yst.gen++; } }
    else if (st == 0xFCu && yst.run == 1u) { yst.run = 0; yst.gen++; }   /* (its own stop; ours to pause: stays 2) */
    if (st >= 0xF0u) return 1;
    if (t >= YST_NTRK) return 0;
    k = &yst.t[t];
    if (type == 0xB0u) {
        if (d1 == 0u) { k->msb = d2; return 1; }
        if (d1 == 32u) { k->lsb = d2; return 1; }
        for (p = 0; p < YP_N; p++)
            if (YP_CC[p] == d1) { k->v[p] = d2 > YP_MAX[p] ? YP_MAX[p] : d2; yst.gen++; return 1; }
        return 1;
    }
    if (type == 0xC0u) {
        if (k->msb == 63u || k->msb == 62u) k->snd = (uint16_t)(k->lsb * 128u + d1 + 1u);
        yst.gen++;
    }
    return 1;
}

static void yst_sysex(const uint8_t *p, uint32_t n)            /* F0 43 .. F7, p after F0 */
{
    uint32_t t;
    if (zn.yst) {                       /* (diagnosis) what came, answer or not */
        yst.sx_in++;
        yst.sx_last_n = (uint8_t)(n < sizeof yst.sx_last ? n : sizeof yst.sx_last);
        memcpy(yst.sx_last, p, yst.sx_last_n);
        yst.gen++;
    }
    if (n < 8u || p[0] != 0x43u || (p[1] & 0xF0u) != 0x10u || p[2] != 0x7Fu || p[3] != 0x1Cu || p[4] != 0x0Cu) return;
    if (!zn.yst) return;
    if (!yst.on) { yst.on = 1; ctl_say("SEQTRAK", "CONNECTED"); }
    yst.gen++;
    if (n >= 9u && p[5] == 0x01u && p[6] == 0x10u && p[7] == 0x27u) {   /* CurrentPart: a track chosen there */
        if (p[8] >= YST_NTRK || p[8] == yst.cur) return;
        if (zn.z[mls.focus].out_ch < YST_NTRK) yst_pick(p[8], 0);   /* the keys go along if they play the SEQTRAK */
        else { yst.cur = p[8]; yst.gen++; }
        return;
    }
    if (p[5] != 0x01u || (p[6] & 0xF0u) != 0x10u || (t = p[6] & 15u) >= YST_NTRK || n < 9u) return;
    if (yst.wait && t == yst.ask >> 1u && p[7] == (yst.ask & 1u ? 0x33u : 0x3Cu)) {   /* the answer: the next one */
        yst.wait = 0;
        yst.tries = 0;
        yst.ask++;
        if (yst.ask >= 2u * YST_NTRK - 1u) ctl_say("SEQTRAK", "TRACKS READ");
    }
    if (p[7] == 0x3Cu) yst.t[t].type = p[8] <= YT_SAMPLER ? p[8] : YT_NONE;        /* PartAwmType */
    else if (p[7] == 0x33u && n >= 10u) yst.t[t].snd = (uint16_t)((p[8] << 7 | p[9]) + 1u);   /* the sound */
}

static void yst_refresh(void)           /* read the SEQTRAK's tracks again (REFRESH) */
{
    yst.ask = 0;
    yst.wait = 0;
    yst.tries = 0;
    yst.gen++;
}
static void yst_enable(uint32_t on)     /* MIDI module 5 on / off (the setup keeps it) */
{
    ml_release_all();
    zn.yst = (uint8_t)(on & 1u);        /* (yst_tick sees it, as a setup recalled) */
    ctl_say("YST (SEQTRAK)", zn.yst ? "ON" : "OFF");
}

static void yst_tick(void)              /* main loop: the cable, and the questions asked */
{
    uint32_t up = usb_host_up();
    if (up != yst.usb || zn.yst != yst.en) {   /* plugged in or switched on: read; pulled or off: forget */
        yst.usb = (uint8_t)up;
        yst.en = zn.yst;
        yst.on = 0;
        yst_reset();
        yst.ask = 2u * YST_NTRK;
        if (up && zn.yst) yst_refresh();
    }
    if (!zn.yst || !up) return;
    if (yst.ask == 2u * YST_NTRK - 1u) yst.ask++;   /* the sampler's sound: it never answers that */
    if (yst.ask >= 2u * YST_NTRK) return;
    if (yst.wait && fm1_ms - yst.ask_ms < 1000u) return;   /* (it answers in about 250 ms as USB host) */
    if (yst.wait && yst.tries >= 3u) {  /* no answer, three times */
        yst.wait = 0;
        yst.tries = 0;
        yst.silent++;
        if (!yst.on) {                  /* not even the first: nobody there; REFRESH tries again */
            yst.ask = 2u * YST_NTRK;
            ctl_say("SEQTRAK", "NO ANSWER");
        } else yst.ask++;
        yst.gen++;
        return;
    }
    yst.ask_ms = fm1_ms;                /* (a question that could not go out counts as a try too) */
    yst.tries++;
    yst.wait = 1;
    yst_sx_req(0x01, 0x10u | (uint32_t)(yst.ask >> 1), yst.ask & 1u ? 0x33 : 0x3C);
}

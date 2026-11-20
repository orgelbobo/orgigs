/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: the panel and the 240 x 240 screen (main loop). The panel maps the FM-1's controls onto the
 * actions in core/orgigs_ctl.c, which every board shares; the screens, the lights and the modes are its own.
 *
 * The FM-1 as Dinghy's platform sees it: 27 keys (F3..G5), 14 buttons, KNOB1..4, the SELECT, ALGORITHM and
 * PRESETS encoders, the MASTER knob. KNOB1..4 have no push: BTN_SEL held is the "push" (SEL + turn = the
 * second function of a knob). The touch strips are not reachable from this platform: their jobs come in as
 * MIDI CC 1 (mod strip) and pitch bend (pitch strip), and the browser simulator draws them as two sliders.
 *
 * The buttons on the panel:   FX   SEL  ENV  LFO  EDIT GLO
 *                             HOME SAVE ARP  SEQ  PLAY REC
 *
 * Everywhere:
 *   SELECT enc    manual (zone)             ALGORITHM enc  model 1..8       PRESETS enc  the drawbar list
 *   SAVE          tap: store the drawbars of the manual into a user slot; held + one of the 16 white keys:
 *                 store the whole instrument into that performance setup
 *   SEL           held + one of the 16 white keys: recall that performance setup; SEL + HOME: panic;
 *                 SEL + GLO: the lights
 *   PLAY / REC    rotary brake / rotary slow-fast (SEL + REC: the focused manual through the rotary or not)
 *   OCT- OCT+     octave of the FM-1's keys
 *   FX            the effects screen (again: back); SEL + FX the insert effect, again the master
 *                                                        GLO   the MIDI zones (again: the next screen)
 *   HOME          on the drawbars: the MIDI mixer (again: back); elsewhere back to the drawbars
 * The drawbars (PLAY): ENV drawbars 1-4, LFO 5-8, EDIT 1 3 4 + 9 on KNOB1..4; ARP vibrato (tremulant) of the
 * manual on/off, SEQ coupler II/I.
 * The effects (FX): a row per manual (UPPER, LOWER, PEDAL = the focused manual: ENV LFO ARP or SELECT), KNOB1..4
 * drive, vibrato / tremulant, delay send, reverb send; SEQ the manual through the rotary. SEL + knobs: the
 * shared ones (percussion / chiff / detune, delay feedback and time, the reverb's room).
 * INSERT (SEL + FX; core/orgigs_params.c): one effect on one manual's bus, after its vibrato, before the rotary. EDIT the next
 * effect (SEL + EDIT: the one before), SEQ it on / off, ENV LFO ARP the manual it sits on; KNOB1..4 its values 1-4,
 * SEL + knobs 5-8. While WAH is in, CC 1 (the mod strip's) is its pedal.
 * MASTER (SEL + FX on INSERT): EQ and compressor on the sum. ENV the EQ on / off, LFO the compressor, EDIT what the knobs hold: the EQ
 * (KNOB1..3 low, mid, high; SEL + them their frequencies) or the compressor (threshold, ratio, attack, release;
 * SEL + KNOB1 the make-up gain).
 * The MIDI mixer: 16 channels in 4 rows; ENV LFO ARP SEQ choose the row, KNOB1..4 send CC 7.
 * The MIDI zones: ENV LFO EDIT ARP SEQ are the screen's switches (SEL + them: the second ones).
 * The lights: KNOB1 the keys, KNOB2 the buttons (OFF, DIM: all glow, FULL: all lit and what matters blinks),
 * KNOB3 how bright DIM glows; SEQ sends a backup of all settings over USB-MIDI (core/orgigs_backup.c).
 * A backup coming in asks first: SAVE loads it, any other button keeps what is there. */

enum { MODE_PLAY, MODE_FX, MODE_MIDI, MODE_MIX, MODE_LIGHT, MODE_INS, MODE_MST };
enum { GRP_DB14, GRP_DB58, GRP_DB9 };
enum { MS_ZONE, MS_CLONE, MS_HARM, MS_CHORD, MS_YST, MS_N };

static const char *const GRP_BADGE[3] = {"DB1-4", "DB5-8", "DB9"};
static const uint8_t GRP_DB[3][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 2, 3, 8}};
static const char *const MS_NAME[MS_N] = {"ZONE", "CLONE", "HARMONIC", "CHORD", "YST"};
static const char *const LT_NAME[LT_N] = {"OFF", "DIM", "FULL"};
static const uint8_t SETUP_KEY[NSETUP] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* the 16 white keys */

static struct {
    uint32_t mode, grp, ms;
    uint32_t mix_row;                   /* MIDI mixer: the row the knobs hold (channels 4r+1 .. 4r+4) */
    uint32_t shift, save_used;
    uint32_t clone_cur;                 /* CLONE: the channel KNOB2 sets the octave of */
    uint32_t mst_comp;                  /* MASTER: the knobs hold the compressor (else the EQ) */
    uint32_t fx_last;                   /* INSERT: the effect SEQ switches back on */
} ui = {.fx_last = FXI_CHORUS};

/* ---- knobs */
static void knob_fx(uint32_t k, int32_t st)
{
    if (ui.shift) {                     /* the shared ones */
        if (k == 0u) ctl_character(st);
        else if (k == 1u) ctl_delay_fb(st);
        else if (k == 2u) ctl_delay_time(st);
        else ctl_rev_mode(st);
    } else if (k == 0u) ctl_drive(st);
    else if (k == 1u) ctl_vib(st);
    else if (k == 2u) ctl_dsend(st);
    else ctl_rsend(st);
}

/* the MIDI mixer: KNOB1..4 = the four channels of the row; each step sends CC 7 */
static char *mix_label(char *p, uint32_t ch)
{
    uint32_t zi;
    const char *t = yst_ch_tag(ch);
    for (zi = 0; zi < ORG_NMAN; zi++) if (zn.z[zi].out_ch == ch) return cat(p, MAN_TAG[zi]);
    return t ? cat(p, t) : org_fmt(p, (int32_t)ch + 1, 0);   /* (a SEQTRAK track: its tag) */
}
static void knob_mix(uint32_t k, int32_t st) { ctl_mix(ui.mix_row * 4u + k, st); }
/* the lights: what the LEDs of the keys and the buttons do */
/* the dim level: 1..4 a short glow pulse every frame, 5..10 also fully lit in 1, 2, 3, 4, 6, 8 of every 8
   frames (10 = as bright as FULL). The pulse stays at most 3 us: a pulse longer than the matrix shift makes
   the key-scan interrupt wait out the rest (the frames cost nothing) */
static const uint16_t LT_NS[LT_LEVELS] = {500, 1000, 2000, 3000, 3000, 3000, 3000, 3000, 3000, 0};
static const uint8_t LT_DUTY[LT_LEVELS] = {0, 0, 0, 0, 1, 2, 3, 4, 6, 8};
static void knob_light(uint32_t k, int32_t st)
{
    if (k == 0u) { st0.lt_keys = (uint8_t)clampi((int32_t)st0.lt_keys + st, 0, LT_N - 1); ctl_say("KEYS LIGHT", LT_NAME[st0.lt_keys]); }
    else if (k == 1u) { st0.lt_btns = (uint8_t)clampi((int32_t)st0.lt_btns + st, 0, LT_N - 1); ctl_say("BUTTONS LIGHT", LT_NAME[st0.lt_btns]); }
    else if (k == 2u) { st0.lt_level = (uint8_t)clampi((int32_t)st0.lt_level + st, 0, LT_LEVELS - 1); ctl_say_int("DIM LEVEL", st0.lt_level + 1, 0); }
    else return;
    touched();
}

static void knob_midi(uint32_t k, int32_t st)
{
    zone_t *z = &zn.z[mls.focus];
    char s[24], *p = s;
    switch (ui.ms) {
    case MS_ZONE:
        if (k == 0u) {
            uint8_t *c = ui.shift ? &z->out_ch : &z->in_ch;
            ml_release_all();
            *c = (uint8_t)clampi((int32_t)*c + st, 0, CH_OFF);
            ch_name(s, *c);
            ctl_say(ui.shift ? "MIDI OUT CH" : "MIDI IN CH", s);
        } else if (k == 1u) {
            if (ui.shift) z->hi = (uint8_t)clampi((int32_t)z->hi + st, z->lo, 127);
            else z->lo = (uint8_t)clampi((int32_t)z->lo + st, 0, z->hi);
            note_name(s, ui.shift ? z->hi : z->lo);
            ctl_say(ui.shift ? "HIGHEST KEY" : "LOWEST KEY", s);
        } else if (k == 2u) {
            ml_release_all();
            if (ui.shift) { z->octave = (int8_t)clampi(z->octave + st, -3, 3); ctl_say_int("OUT OCTAVE", z->octave, 1); }
            else { z->transpose = (int8_t)clampi(z->transpose + st, -24, 24); ctl_say_int("OUT TRANSPOSE", z->transpose, 1); }
        } else if (!ui.shift) {
            z->vel = (uint8_t)clampi((int32_t)z->vel + st * 3, 1, 127);
            ctl_say_int("FIXED VELOCITY", z->vel, 0);
        } else ctl_bend_mode(st);      /* for every zone: what bend and CC 1 do on the organs */
        break;
    case MS_CLONE:
        if (k == 0u) {
            ui.clone_cur = (uint32_t)clampi((int32_t)ui.clone_cur + st, 0, 15);
            ctl_say_int("CHANNEL", (int32_t)ui.clone_cur + 1, 0);
        } else if (k == 1u) {
            ml_release_all();
            z->clone_oct[ui.clone_cur] = (int8_t)clampi(z->clone_oct[ui.clone_cur] + st, -3, 3);
            p = cat(p, "CH"); p = org_fmt(p, (int32_t)ui.clone_cur + 1, 0); cat(p, " OCTAVE");
            ctl_say_int(s, z->clone_oct[ui.clone_cur], 1);
        }
        break;
    case MS_YST:                        /* KNOB1..4 cutoff, resonance, attack, decay; SEL + them volume, pan, sends */
        yst_step(yst.cur, (ui.shift ? YP_VOL : YP_CUTOFF) + k, st);
        break;
    case MS_HARM:
        if (k == 0u) {
            ml_release_all();
            z->harm_base = (uint8_t)clampi((int32_t)z->harm_base + st, 0, 7);
            p = cat(p, "CH "); p = org_fmt(p, z->harm_base + 1, 0); p = cat(p, "-"); org_fmt(p, z->harm_base + 9, 0);
            ctl_say("HARMONIC ON", s);
        } else if (k == 1u) {
            z->harm_send = (uint8_t)clampi((int32_t)z->harm_send + st, 0, ML_NCSEND - 1);
            ml_harm_resend();
            ctl_say("DRAWBARS SEND", CSEND_NAME[z->harm_send]);
        }
        break;
    default:
        break;
    }
    touched();
}

/* INSERT: KNOB1..4 the values 1-4, SEL + them 5-8; MASTER: the EQ's gains (SEL: their frequencies) or the compressor */
static const uint8_t MST_KNOB[2][2][4] = {
    {{MSP_LO, MSP_MID, MSP_HI, MSP_N}, {MSP_LFREQ, MSP_MFREQ, MSP_HFREQ, MSP_N}},
    {{MSP_THR, MSP_RATIO, MSP_ATT, MSP_REL}, {MSP_MAKEUP, MSP_N, MSP_N, MSP_N}},
};
static void knob_mst(uint32_t k, int32_t st)
{
    uint32_t i = MST_KNOB[ui.mst_comp][ui.shift][k];
    if (i < MSP_N) mst_step(i, st);
}

static void knob(uint32_t k, int32_t st)        /* k = 0..3 */
{
    if (ui.mode == MODE_FX) knob_fx(k, st);
    else if (ui.mode == MODE_INS) fxi_step(k + (ui.shift ? 4u : 0u), st);
    else if (ui.mode == MODE_MST) knob_mst(k, st);
    else if (ui.mode == MODE_MIDI) knob_midi(k, st);
    else if (ui.mode == MODE_MIX) knob_mix(k, st);
    else if (ui.mode == MODE_LIGHT) knob_light(k, st);
    else ctl_drawbar(mls.focus == MAN_PEDAL ? k : GRP_DB[ui.grp][k], st);
}

/* ---- the MIDI screens' switches: ENV LFO EDIT ARP SEQ = 0..4 */
static uint32_t flag_toggle(zone_t *z, uint32_t f, const char *name)
{
    z->flags ^= (uint8_t)f;
    ctl_say(name, z->flags & f ? "ON" : "OFF");
    return z->flags & f;
}

static void midi_switch(uint32_t i)
{
    zone_t *z = &zn.z[mls.focus];
    if (ui.ms == MS_YST) {                      /* ENV on / off, LFO REFRESH, ARP the track's mute, SEQ mono / poly / chord */
        if (i == 0u) { yst_enable(!zn.yst); touched(); }
        else if (!zn.yst) ctl_say("YST IS OFF", BOARD_YST_ON ": ON");
        else if (i == 1u) { yst_refresh(); ctl_say("SEQTRAK", "READING"); }
        else if (i == 3u) yst_mute(yst.cur);
        else if (i == 4u) yst_voice(yst.cur);
        return;
    }
    if (ui.ms == MS_ZONE) {
        if (ui.shift) {
            if (i == 0u) flag_toggle(z, Z_VELEXPR, "VELOCITY x EXPRESSION");
            else if (i == 1u) { ml_release_all(); zn.thru ^= 1u; ctl_say("THRU OTHER CHANNELS", zn.thru ? "ON" : "OFF"); }
            else if (i == 2u) { ml_rate_safe ^= 1u; ctl_say("MIDI RATE", ml_rate_safe ? "TRS-SAFE" : "USB-FAST"); }
            else if (i == 3u) { ml_release_all(); zn.keys ^= 1u; ctl_say("FM-1 KEYS", zn.keys == KEYS_ZONES ? "BY ZONE RANGES" : "FOLLOW MANUAL"); }
            else { ml_release_all(); zn.auto_pedal ^= 1u; ctl_say("AUTO-PEDAL", zn.auto_pedal ? "ON" : "OFF"); }
        } else {
            if (i == 0u) flag_toggle(z, Z_VELTHRU, "VELOCITY THRU");
            else if (i == 1u) flag_toggle(z, Z_NOCC, "FILTER CC");
            else if (i == 2u) flag_toggle(z, Z_NOPC, "FILTER PROG CHANGE");
            else if (i == 3u) { ml_release_all(); z->flags ^= Z_NOINT; ctl_say("INTERNAL SOUND", z->flags & Z_NOINT ? "OFF" : "ON"); }
            else {
                mls.learn = mls.learn ? 0u : 1u;
                ctl_say(mls.learn ? "LEARN: PLAY LOWEST + HIGHEST" : "LEARN OFF", 0);
            }
        }
    } else if (i == 0u) {
        static const uint8_t F[MS_N] = {0, Z_CLONE, Z_HARM, Z_CHORD};
        ml_release_all();
        if (ui.ms == MS_HARM && mls.focus == MAN_PEDAL) ctl_say("HARMONIC: NOT FOR THE PEDAL", 0);
        else { flag_toggle(z, F[ui.ms], MS_NAME[ui.ms]); ml_harm_resend(); }
    } else if (ui.ms == MS_CHORD && i == 1u) {
        ctl_say_int("CHORD NOTES", (int32_t)ml_chord_capture(), 0);
    } else if (ui.ms == MS_CHORD && i == 2u) {
        ml_release_all();
        z->chord_n = 0;
        ctl_say("CHORD CLEARED", 0);
    }
    touched();
}

static void set_mode(uint32_t m)
{
    if (m == MODE_MIDI && (ui.ms == MS_CLONE || ui.ms == MS_HARM)) ml_release_keys();   /* the keys become switches */
    if (m != MODE_MIDI) mls.learn = 0;
    ui.mode = m;
}

/* =========================================================== the screen */
#define C_BG RGB(10, 10, 14)
#define C_ACC RGB(255, 176, 48)          /* amber: what the knobs hold */
#define C_TXT RGB(220, 220, 220)
#define C_MUT RGB(110, 110, 120)
#define C_GRID RGB(40, 40, 48)
#define C_CH_ON RGB(40, 200, 120)
#define C_BROWN RGB(150, 82, 40)
#define C_IVORY RGB(236, 230, 210)
#define C_DARKB RGB(70, 70, 78)
#define C_PRN RGB(200, 170, 80)
#define C_FLT RGB(110, 170, 230)
#define C_STR RGB(210, 90, 80)
#define C_REED RGB(170, 120, 220)
#define C_HGREEN RGB(52, 178, 84)
#define C_HRED RGB(214, 56, 42)
#define C_HYELLOW RGB(244, 196, 36)

static uint16_t bar_colour(uint32_t dept, uint32_t d)
{
    if (dept == DEPT_VINTAGE) {
        if (d < 2u) return C_BROWN;
        return (d == 4u || d == 6u || d == 7u) ? C_DARKB : C_IVORY;
    }
    if (dept == DEPT_PIPE) {
        static const uint8_t FAM[9] = {1, 0, 1, 2, 0, 1, 0, 0, 2};
        return FAM[d] == 0 ? C_PRN : FAM[d] == 1 ? C_FLT : C_STR;
    }
    return d == 8u ? C_MUT : (d == 3u || d == 4u || d == 6u) ? C_REED : C_IVORY;
}

static uint32_t hash_add(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
/* Kept out of line: inlined with a variable row, clang emits a load with register post-increment
   (b[rX++=rY]) that the FM-1 emulator rejects and Dinghy's firmware never uses. */
static __attribute__((noinline)) uint32_t hash_row(uint32_t h, const volatile uint8_t *p, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) h = hash_add(h, p[i]);
    return h;
}
static __attribute__((noinline)) uint32_t hash_mem(uint32_t h, const void *p, uint32_t n) {
    const uint8_t *b = p;
    uint32_t i;
    for (i = 0; i < n; i++) h = hash_add(h, b[i]);
    return h;
}
static int32_t text_w_s(const char *s) { return text_w(&FONT_S, s); }

/* the regions drawn last time (a hash of what they show; redrawn when it changes) */
enum { R_TOP, R_BARS, R_MINI, R_BOT, R_VIS, R_ROW1, R_ROW2, R_INFO, R_MSG, R_BTN, R_N };
static uint32_t drawn[R_N], drawn_view = 99;

/* the battery (core/orgigs_bat.c): the symbol at the top bar's right end, 18 x 10; charging: a bolt in it */
static void draw_bat(void)
{
    uint32_t pct = bat_pct(), w = (pct * 12u + 50u) / 100u;
    uint16_t c = pct < 20u ? C_HRED : C_TXT;
    cv_rect(221, 7, 15, 10, C_MUT);
    cv_rect(222, 8, 13, 8, C_BG);
    cv_rect(236, 10, 2, 4, C_MUT);
    if (w) cv_rect(223, 9, (int32_t)w - (w > 11u ? 1 : 0), 6, c);
    if (bat_charging()) draw_bolt(229, 5);
}

/* the top bar: a number + tag, then badges; the right-hand text */
static void draw_top_bar(const char *tag, const char *b1, uint16_t c1, const char *b2, const char *right, uint16_t cr)
{
    int32_t x;
    cv_begin(240, 24, C_BG);
    x = cv_text(4, 4, &FONT_S, tag, C_ACC) + 6;
    cv_rect(x - 2, 3, text_w_s(b1) + 4, 18, C_GRID);
    x = cv_text(x, 4, &FONT_S, b1, c1) + 7;
    cv_rect(x - 2, 3, text_w_s(b2) + 4, 18, C_ACC);
    cv_text(x, 4, &FONT_S, b2, C_BLACK);
    cv_text((bat_ok() ? 216 : 236) - text_w_s(right), 4, &FONT_S, right, cr);
    if (bat_ok()) draw_bat();
    cv_rect(0, 23, 240, 1, C_GRID);
    cv_blit(0, 0);
}

static char *model_tag(char *p)
{
    *p++ = (char)('1' + org_par.model); *p++ = ' ';
    return cat(p, ORG_MODELS[org_par.model].tag);
}

/* ---- PLAY: nine drawbars, pulled down like a tonewheel organ's, 128 px tall: two canvases of 64 rows */
static void draw_bars(void)
{
    uint32_t man = mls.focus, dept = dept_now(), n = man == MAN_PEDAL ? ORG_NDB_PEDAL : ORG_NDB, half, d;
    for (half = 0; half < 2u; half++) {
        int32_t oy = (int32_t)half * 64;
        cv_begin(240, 64, C_BG);
        for (d = 0; d < n; d++) {
            int32_t cw = man == MAN_PEDAL ? 56 : 26, x = (man == MAN_PEDAL ? 8 : 4) + (int32_t)d * cw, bw = cw - 8;
            int32_t lvl = org_par.drawbar[man][d], len = 8 + lvl * 11, y;
            uint16_t c = man == MAN_PEDAL ? (d == 3u ? C_STR : C_BROWN) : bar_colour(dept, d);
            uint32_t sel = man == MAN_PEDAL || GRP_DB[ui.grp][0] == d || GRP_DB[ui.grp][1] == d ||
                           GRP_DB[ui.grp][2] == d || GRP_DB[ui.grp][3] == d;
            cv_rect(x + bw / 2 - 1, 2 - oy, 2, 100, C_GRID);              /* the slot */
            cv_rect(x, 2 - oy, bw, len, c);                                 /* the bar */
            for (y = 1; y <= lvl; y++)                                      /* the steps */
                cv_rect(x + 2, 2 + 8 + y * 11 - 2 - oy, bw - 4, 1, C_BG);
            cv_rect(x - 1, 2 + len - oy, bw + 2, 6, sel ? C_ACC : C_TXT);  /* the handle */
            {
                char s[4];
                s[0] = (char)('0' + lvl); s[1] = 0;
                cv_text(x + bw / 2 - 4, 106 - oy, &FONT_S, s, sel ? C_ACC : C_MUT);
            }
            if (sel) cv_rect(x - 2, 124 - oy, bw + 4, 3, C_ACC);           /* the bracket: what the knobs hold */
        }
        cv_blit(0, 24 + (uint32_t)oy);
    }
}

/* the other two manuals, small, with the coupler and the setup; the last change shows over them */
static void draw_mini(void)
{
    uint32_t i, j, d, x = 4;
    char s[8];
    cv_begin(240, 28, C_BG);
    cv_rect(0, 0, 240, 1, C_GRID);
    for (i = 0, j = 0; i < 3u; i++) {
        uint32_t n = i == MAN_PEDAL ? ORG_NDB_PEDAL : ORG_NDB;
        static const char *const T[3] = {"UP", "LO", "PD"};
        if (i == mls.focus) continue;
        x = j ? 90 : 4;
        cv_text((int32_t)x, 6, &FONT_S, T[i], C_MUT);
        for (d = 0; d < n; d++) {
            int32_t h = org_par.drawbar[i][d] * 3;
            cv_rect((int32_t)(x + 22 + d * 7), 3, 5, 24, C_GRID);
            cv_rect((int32_t)(x + 22 + d * 7), 3, 5, h, i == MAN_PEDAL ? C_BROWN : C_IVORY);
        }
        j++;
    }
    cv_text(176, 6, &FONT_S, org_par.coupler ? "II/I" : "", C_CH_ON);
    s[0] = 'S'; org_fmt(s + 1, (int32_t)st0.setup + 1, 0);
    cv_text(220, 6, &FONT_S, s, C_MUT);
    if (ctl_msg_on()) {                                                       /* the last change, over the strip */
        cv_rect(0, 1, 240, 27, C_ACC);
        cv_text(4, 6, &FONT_S, ctl.msg, C_BLACK);
    }
    cv_blit(0, 152);
}

/* a rocker tab, 36 x 52: the name on the tab (lit in its colour when on), the setting underneath */
static void draw_tab(int32_t x, uint16_t col, uint32_t on, const char *lab, const char *val)
{
    cv_rect(x, 4, 36, 52, on ? col : C_GRID);
    cv_rect(x + 2, 28, 32, 26, C_BG);
    cv_text(x + 18 - text_w_s(lab) / 2, 7, &FONT_S, lab, on ? C_BLACK : C_MUT);
    cv_text(x + 18 - text_w_s(val) / 2, 33, &FONT_S, val, on ? col : C_MUT);
}

/* a knob arc of radius r around (cx, cy): the track from 7 to 5 o'clock, the value lit, a cap and a pointer */
static void draw_knob_at(int32_t cx, int32_t cy, int32_t r, uint16_t col, uint32_t v)
{
    const int32_t N = 72;
    int32_t i, rr, dy, cap = r - 7;
    for (i = 0; i <= N; i++) {
        uint32_t ph = 0xA0000000u + (uint32_t)i * (0xC0000000u / (uint32_t)N);
        int32_t sn = org_sin(ph), cs = org_sin(ph + 0x40000000u);
        uint16_t c = (uint32_t)i * 127u <= v * (uint32_t)N && v ? col : C_GRID;
        for (rr = r - 3; rr <= r; rr++) cv_pset(cx + ((rr * sn) >> 15), cy - ((rr * cs) >> 15), c);
    }
    for (dy = -cap; dy <= cap; dy++) {                  /* the cap */
        int32_t dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= cap * cap) dx++;
        cv_rect(cx - dx, cy + dy, 2 * dx + 1, 1, C_DARKB);
    }
    {
        uint32_t ph = 0xA0000000u + v * (0xC0000000u / 127u);
        int32_t sn = org_sin(ph), cs = org_sin(ph + 0x40000000u);
        cv_line(cx + ((3 * sn) >> 15), cy - ((3 * cs) >> 15), cx + ((cap * sn) >> 15), cy - ((cap * cs) >> 15), C_TXT);
        cv_line(cx + ((3 * sn) >> 15) + 1, cy - ((3 * cs) >> 15), cx + ((cap * sn) >> 15) + 1, cy - ((cap * cs) >> 15), C_TXT);
    }
}
static void draw_knob(int32_t cx, uint16_t col, uint32_t v, const char *lab)
{
    draw_knob_at(cx, 22, 17, col, v);
    cv_text(cx - text_w_s(lab) / 2, 42, &FONT_S, lab, C_TXT);
}

/* the panel strip: three tonewheel-organ tabs (switches) and three knobs, in the tonewheel-organ colours */
static void draw_bottom(void)
{
    static const char *const ROOM[ORG_NREV] = {"SPR", "STG", "HAL", "CAT"};
    static const char *const LES_TAG[3] = {"SLOW", "BRK", "FAST"};
    uint32_t dept = dept_now(), m = mls.focus;
    char n[8];
    cv_begin(240, 60, C_BG);
    cv_rect(0, 0, 240, 1, C_GRID);
    if (dept == DEPT_VINTAGE)
        draw_tab(2, C_HGREEN, org_par.vib[m] != 0u, org_par.vib[m] >= VIB_C1 ? "CHO" : "VIB", VIB_NAME[org_par.vib[m]]);
    else {
        org_fmt(n, org_par.trem[m], 0);
        draw_tab(2, C_HGREEN, org_par.trem[m] != 0u, "TRM", org_par.trem[m] ? n : "OFF");
    }
    if (org_par.model == MODEL_B3)
        draw_tab(41, C_HRED, org_par.perc != 0u, "PERC", PERC_TAG[org_par.perc]);
    else
        draw_tab(41, C_HRED, org_par.coupler, "II/I", org_par.coupler ? "ON" : "OFF");
    draw_tab(80, C_HYELLOW, org_par.les_on[m], "ROT", org_par.les_on[m] ? LES_TAG[org_par.leslie] : "OFF");
    cv_rect(120, 6, 1, 48, C_GRID);
    draw_knob(142, C_HRED, org_par.drive[m], "DRV");
    draw_knob(182, C_HYELLOW, org_par.dsend[m], "DLY");
    draw_knob(221, C_HGREEN, org_par.rsend[m], ROOM[org_par.rev_mode < ORG_NREV ? org_par.rev_mode : 0u]);
    cv_blit(0, 180);
}

/* ---- FX and MIDI: four columns under KNOB1..4, the SEL row under them, the buttons as on the panel */
typedef struct { char lab[4][10], val[4][10]; uint16_t col[4]; uint8_t lit; } cells_t;
typedef struct { char lab[12][5]; uint8_t on[12]; } btnmap_t;
static const uint8_t BTN_AT[12] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO,
                                   BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC};

static void cells_set(cells_t *c, uint32_t i, const char *lab, const char *val)
{
    char *p = c->lab[i], *q = c->val[i];
    uint32_t n = 0;
    while (*lab && n < 9u) { *p++ = *lab++; n++; }
    n = 0;
    while (*val && n < 9u) { *q++ = *val++; n++; }
}

/* a row of four cells, 36 px: the label small and grey, the value lit when this row is the knobs' */
static void draw_cells(uint32_t y, const cells_t *c)
{
    uint32_t i;
    cv_begin(240, 36, C_BG);
    for (i = 0; i < 4u; i++) {
        int32_t x = (int32_t)i * 60;
        uint16_t vc = !c->val[i][0] ? C_MUT : c->lit ? (c->col[i] ? c->col[i] : C_ACC) : C_TXT;
        if (i) cv_rect(x, 2, 1, 32, C_GRID);
        cv_text(x + 30 - text_w_s(c->lab[i]) / 2, 1, &FONT_S, c->lab[i], c->lit ? C_TXT : C_MUT);
        cv_text(x + 30 - text_w_s(c->val[i]) / 2, 18, &FONT_S, c->val[i], vc);
    }
    if (c->lit) cv_rect(0, 35, 240, 1, C_ACC);
    cv_blit(0, y);
}

/* the twelve buttons as they sit on the panel (2 rows of 6), 60 px */
static void draw_btnmap(const btnmap_t *b)
{
    uint32_t i;
    cv_begin(240, 60, C_BG);
    cv_rect(0, 0, 240, 1, C_GRID);
    for (i = 0; i < 12u; i++) {
        int32_t x = (int32_t)(i % 6u) * 40, y = 4 + (int32_t)(i / 6u) * 28;
        uint32_t any = b->lab[i][0] != 0;
        cv_rect(x + 1, y, 38, 26, b->on[i] ? C_ACC : any ? C_GRID : C_BG);
        if (!b->on[i] && any) cv_rect(x + 2, y + 1, 36, 24, C_BG);
        cv_text(x + 20 - text_w_s(b->lab[i]) / 2, y + 5, &FONT_S, b->lab[i], b->on[i] ? C_BLACK : C_TXT);
    }
    cv_blit(0, 180);
}

static void btn_set(btnmap_t *b, uint32_t id, const char *lab, uint32_t on)
{
    uint32_t i, n = 0;
    for (i = 0; i < 12u; i++)
        if (BTN_AT[i] == id) {
            char *p = b->lab[i];
            while (*lab && n < 4u) { *p++ = *lab++; n++; }
            b->on[i] = (uint8_t)(on != 0);
        }
}

/* what every FX / MIDI screen shows on the global buttons */
static void btn_common(btnmap_t *b)
{
    btn_set(b, BTN_FX, !ui.shift ? "FX" : ui.mode == MODE_INS ? "MST" : "INS",
            ui.mode == MODE_FX || ui.mode == MODE_INS || ui.mode == MODE_MST);
    btn_set(b, BTN_SEL, "2ND", ui.shift);
    btn_set(b, BTN_GLO, "MIDI", ui.mode == MODE_MIDI);
    btn_set(b, BTN_HOME, "HOME", 0);
    btn_set(b, BTN_SAVE, "SAVE", fm1_in.buttons >> BTN_SAVE & 1u);
    btn_set(b, BTN_PLAY, "BRK", org_par.leslie == LESLIE_BRAKE);
    btn_set(b, BTN_REC, ui.shift ? "ROT" : org_par.leslie == LESLIE_FAST ? "FAST" : "SLOW",
            ui.shift ? org_par.les_on[mls.focus] : org_par.leslie == LESLIE_FAST);
}

/* the line over the buttons: the last change, else a hint */
static void draw_msgline(const char *hint)
{
    cv_begin(240, 20, C_BG);
    if (ctl_msg_on()) {
        cv_rect(0, 0, 240, 20, C_ACC);
        cv_text(4, 2, &FONT_S, ctl.msg, C_BLACK);
    } else {
        cv_text(4, 2, &FONT_S, hint, C_MUT);
    }
    cv_blit(0, 160);
}
static uint32_t msg_hash(const char *hint)
{
    uint32_t h = hash_mem(9u, ctl.msg, sizeof ctl.msg);
    return hash_add(hash_mem(h, hint, 30), ctl_msg_on()) | 1u;
}

/* ---- FX: a row per manual (the focused one lit), four knobs; SEL: the shared ones as cells */
static void fx_cells(cells_t *sel)
{
    uint32_t dept = dept_now();
    char s[12];
    memset(sel, 0, sizeof *sel);
    sel->lit = 1;
    if (dept == DEPT_VINTAGE) cells_set(sel, 0, "PERC", org_par.model == MODEL_B3 ? PERC_TAG[org_par.perc] : "WHL ONLY");
    else { org_fmt(s, org_par.character, 0); cells_set(sel, 0, dept == DEPT_PIPE ? "CHIFF" : "DETUNE", s); }
    org_fmt(s, org_par.delay_fb, 0);
    cells_set(sel, 1, "FEEDBK", s);
    org_fmt(cat(s, ""), 20 + org_par.delay_time * 980 / 127, 0);
    cells_set(sel, 2, "D-TIME", s);
    cells_set(sel, 3, "SPACE", REV_TAG[org_par.rev_mode < ORG_NREV ? org_par.rev_mode : 0u]);
    sel->col[0] = C_HRED;
    sel->col[1] = sel->col[2] = C_HYELLOW;
    sel->col[3] = C_FLT;
}

/* the grid: column heads (16 px), then UM LM PM rows of 40 px: the manual and its rotary, four knobs with values */
static void draw_fx_grid(void)
{
    static const uint16_t COL[4] = {C_HRED, C_HGREEN, C_HYELLOW, C_FLT};
    uint32_t dept = dept_now(), m, k, half;
    const char *head[4] = {"DRIVE", dept == DEPT_VINTAGE ? "VIB" : "TREM", "DELAY", "REVERB"};
    for (half = 0; half < 4u; half++) {     /* 136 rows: four canvases of 34 (the canvas holds 64) */
        int32_t oy = (int32_t)half * 34;
        cv_begin(240, 34, C_BG);
        cv_oy = -oy;
        for (k = 0; k < 4u; k++) cv_text(28 + (int32_t)k * 53 + 26 - text_w_s(head[k]) / 2, 0, &FONT_S, head[k], C_MUT);
        for (m = 0; m < ORG_NMAN; m++) {
            int32_t y = 16 + (int32_t)m * 40;
            uint32_t f = m == mls.focus;
            if (y + 40 <= oy || y >= oy + 34) continue;
            if (f) cv_rect(0, y, 240, 40, RGB(30, 26, 20));
            cv_rect(0, y, 240, 1, C_GRID);
            cv_text(3, y + 3, &FONT_S, MAN_TAG[m], f ? C_ACC : C_TXT);
            cv_text(3, y + 21, &FONT_S, "L", org_par.les_on[m] ? C_HYELLOW : C_GRID);
            for (k = 0; k < 4u; k++) {
                int32_t x = 28 + (int32_t)k * 53;
                uint32_t v = k == 0u ? org_par.drive[m] : k == 2u ? org_par.dsend[m] : k == 3u ? org_par.rsend[m]
                           : dept == DEPT_VINTAGE ? org_par.vib[m] * 127u / (ORG_NVIB - 1u) : org_par.trem[m];
                char s[8];
                if (k == 1u && dept == DEPT_VINTAGE) cat(s, VIB_NAME[org_par.vib[m]]);
                else org_fmt(s, (int32_t)v, 0);
                draw_knob_at(x + 14, y + 20, 13, f ? COL[k] : C_MUT, v);
                cv_text(x + 30, y + 12, &FONT_S, s, f ? COL[k] : C_TXT);
            }
        }
        cv_oy = 0;
        cv_blit(0, 24u + (uint32_t)oy);
    }
}

static void fx_btnmap(btnmap_t *b)
{
    memset(b, 0, sizeof *b);
    btn_common(b);
    btn_set(b, BTN_ENV, "UM", mls.focus == MAN_UPPER);
    btn_set(b, BTN_LFO, "LM", mls.focus == MAN_LOWER);
    btn_set(b, BTN_ARP, "PM", mls.focus == MAN_PEDAL);
    btn_set(b, BTN_SEQ, "ROT", org_par.les_on[mls.focus]);
}

/* ---- INSERT: the effect big and the manual it sits on, the seven in a row (the one chosen lit); its values in two
   rows of cells, KNOB1..4 and SEL + them */
static uint32_t ins_shown(void) { return fxi_st.type ? fxi_st.type : ui.fx_last; }   /* (off: the one SEQ brings back) */
static void ins_cells(cells_t *top, cells_t *sel)
{
    uint32_t t = ins_shown(), k;
    char s[12];
    memset(top, 0, sizeof *top);
    memset(sel, 0, sizeof *sel);
    top->lit = fxi_st.type && !ui.shift;
    sel->lit = fxi_st.type && ui.shift;
    for (k = 0; k < FXI_NP; k++) {
        const par_t *q = &FXI_PAR[t][k];
        if (!q->tag) continue;
        par_fmt(s, q, fxi_st.p[t][k]);
        cells_set(k < 4u ? top : sel, k & 3u, q->tag, s);
    }
}
static void draw_vis_ins(void)
{
    uint32_t t = ins_shown(), on = fxi_st.type != FXI_OFF, i;
    char s[16];
    cv_begin(240, 64, C_BG);
    cv_text(4, 0, &FONT_L, FXI_NAME[t], on ? C_ACC : C_MUT);
    cat(cat(s, on ? "ON " : "OFF "), on ? MAN_BADGE[fxi_st.man] : "");
    cv_text(236 - text_w_s(s), 8, &FONT_S, s, on ? C_TXT : C_MUT);
    for (i = 1; i < FXI_N; i++) {
        int32_t x = 2 + (int32_t)(i - 1u) * 34;
        uint32_t cur = i == t;
        cv_rect(x, 40, 32, 22, cur ? (on ? C_ACC : C_DARKB) : C_GRID);
        if (!cur) cv_rect(x + 1, 41, 30, 20, C_BG);
        cv_text(x + 16 - text_w_s(FXI_TAG[i]) / 2, 43, &FONT_S, FXI_TAG[i], cur && on ? C_BLACK : C_TXT);
    }
    cv_blit(0, 24);
}
static void ins_btnmap(btnmap_t *b)
{
    memset(b, 0, sizeof *b);
    btn_common(b);
    btn_set(b, BTN_ENV, "UM", fxi_st.man == MAN_UPPER);
    btn_set(b, BTN_LFO, "LM", fxi_st.man == MAN_LOWER);
    btn_set(b, BTN_ARP, "PM", fxi_st.man == MAN_PEDAL);
    btn_set(b, BTN_EDIT, ui.shift ? "PREV" : "NEXT", 0);
    btn_set(b, BTN_SEQ, fxi_st.type ? "ON" : "OFF", fxi_st.type != FXI_OFF);
}
static void ins_next(int32_t d)         /* EDIT: the next effect (d -1: the one before); from off the last one again */
{
    uint32_t t = ins_shown();
    if (fxi_st.type) t = (t - 1u + (uint32_t)(d + (int32_t)FXI_N - 1)) % (FXI_N - 1u) + 1u;
    ui.fx_last = t;
    if (t != fxi_st.type) fxi_pick(t);
}
static void ins_toggle(void)            /* SEQ: the effect off, or the last one on again */
{
    if (fxi_st.type) { ui.fx_last = fxi_st.type; fxi_pick(fxi_st.type); }
    else fxi_pick(ui.fx_last);
}

/* ---- MASTER: the EQ and the compressor, on or off, and how much the compressor takes now; the knobs' values in two
   rows of cells */
static void mst_cells(cells_t *top, cells_t *sel)
{
    uint32_t r, k;
    char s[12];
    memset(top, 0, sizeof *top);
    memset(sel, 0, sizeof *sel);
    top->lit = !ui.shift;
    sel->lit = (uint8_t)ui.shift;
    for (r = 0; r < 2u; r++)
        for (k = 0; k < 4u; k++) {
            uint32_t i = MST_KNOB[ui.mst_comp][r][k];
            if (i >= MSP_N) continue;
            par_fmt(s, &MSP[i], mst_st.p[i]);
            cells_set(r ? sel : top, k, MSP[i].tag, s);
        }
}
static int32_t mst_gr10(void)           /* the compressor's gain now, 1/10 dB (0 or less) */
{
    int32_t g = mst_gain;
    return g >= 4096 || g < 1 ? 0 : (ilog2_q12((uint32_t)g) - 12 * 4096) * 602 / 40960;
}
static void draw_vis_mst(void)
{
    static const char *const NAME[2] = {"EQ", "COMPRESSOR"};
    uint32_t i;
    int32_t gr = mst_gr10(), w = -gr * 130 / 200;    /* the bar: 20 dB over 130 px */
    char s[16], *p;
    cv_begin(240, 64, C_BG);
    for (i = 0; i < 2u; i++) {
        uint32_t on = i ? mst_st.comp_on : mst_st.eq_on, held = i == ui.mst_comp;
        int32_t x = 2 + (int32_t)i * 119;
        cv_rect(x, 0, 117, 22, on ? C_ACC : held ? C_DARKB : C_GRID);
        if (!on) cv_rect(x + 1, 1, 115, 20, C_BG);
        cv_text(x + 58 - text_w_s(NAME[i]) / 2, 3, &FONT_S, NAME[i], on ? C_BLACK : held ? C_TXT : C_MUT);
    }
    cv_text(2, 30, &FONT_S, "GAIN", C_MUT);
    if (w > 130) w = 130;
    cv_rect(44, 34, 130, 10, C_GRID);
    if (mst_st.comp_on && w > 0) cv_rect(174 - w, 34, w, 10, C_HRED);
    p = s;
    if (gr < 0) *p++ = '-';
    p = org_fmt(p, -gr / 10, 0); *p++ = '.'; *p++ = (char)('0' + -gr % 10); cat(p, "dB");
    cv_text(236 - text_w_s(s), 30, &FONT_S, mst_st.comp_on ? s : "-", C_TXT);
    cv_text(2, 48, &FONT_S, ui.mst_comp ? "KNOBS: COMPRESSOR" : "KNOBS: EQ  SEL: ITS HZ", C_MUT);
    cv_blit(0, 24);
}
static void mst_btnmap(btnmap_t *b)
{
    memset(b, 0, sizeof *b);
    btn_common(b);
    btn_set(b, BTN_ENV, "EQ", mst_st.eq_on);
    btn_set(b, BTN_LFO, "COMP", mst_st.comp_on);
    btn_set(b, BTN_EDIT, ui.mst_comp ? "K:CP" : "K:EQ", 0);
}

/* ---- the MIDI mixer: 4 x 4 channels, a knob each (CC 7); the row the knobs hold lit */
static void draw_mix_grid(void)
{
    uint32_t half, r, c, used = ml_used_channels();
    for (half = 0; half < 4u; half++) {     /* four canvases of 34 rows */
        int32_t oy = (int32_t)half * 34;
        cv_begin(240, 34, C_BG);
        cv_oy = -oy;
        for (r = 0; r < 4u; r++) {
            int32_t y = (int32_t)r * 34;
            uint32_t f = r == ui.mix_row;
            if (y + 34 <= oy || y >= oy + 34) continue;
            if (f) cv_rect(0, y, 240, 34, RGB(30, 26, 20));
            for (c = 0; c < 4u; c++) {
                uint32_t ch = r * 4u + c;
                int32_t x = (int32_t)c * 60;
                char s[8];
                uint16_t lc = used >> ch & 1u ? C_CH_ON : C_MUT;
                if (c) cv_rect(x, y + 2, 1, 30, C_GRID);
                draw_knob_at(x + 15, y + 17, 12, f ? C_ACC : C_DARKB, ml_mix[ch]);
                mix_label(s, ch);
                cv_text(x + 31, y + 1, &FONT_S, s, lc);
                org_fmt(s, ml_mix[ch], 0);
                cv_text(x + 31, y + 17, &FONT_S, s, f ? C_ACC : C_TXT);
            }
        }
        cv_oy = 0;
        cv_blit(0, 24u + (uint32_t)oy);
    }
}

static void mix_btnmap(btnmap_t *b)
{
    memset(b, 0, sizeof *b);
    btn_common(b);
    btn_set(b, BTN_HOME, "BACK", 0);
    btn_set(b, BTN_ENV, "1-4", ui.mix_row == 0u);
    btn_set(b, BTN_LFO, "5-8", ui.mix_row == 1u);
    btn_set(b, BTN_ARP, "9-12", ui.mix_row == 2u);
    btn_set(b, BTN_SEQ, "13+", ui.mix_row == 3u);
}

/* ---- the lights */
static void light_cells(cells_t *top)
{
    char s[8];
    memset(top, 0, sizeof *top);
    top->lit = 1;
    cells_set(top, 0, "KEYS", LT_NAME[st0.lt_keys]);
    cells_set(top, 1, "BUTTONS", LT_NAME[st0.lt_btns]);
    org_fmt(s, st0.lt_level + 1, 0);
    cells_set(top, 2, "LEVEL", s);
    cells_set(top, 3, "BACKUP", "SEQ");
    top->col[0] = top->col[1] = top->col[2] = C_HYELLOW;
    top->col[3] = C_HGREEN;
}

static void light_btnmap(btnmap_t *b)
{
    memset(b, 0, sizeof *b);
    btn_common(b);
    btn_set(b, BTN_GLO, "LITE", 1);
    btn_set(b, BTN_SEQ, "SEND", 0);
}

/* ---- MIDI: the zone screens */
static void midi_cells(cells_t *top, cells_t *sel)
{
    const zone_t *z = &zn.z[mls.focus];
    char s[12];
    memset(top, 0, sizeof *top);
    memset(sel, 0, sizeof *sel);
    top->lit = !ui.shift;
    sel->lit = (uint8_t)ui.shift;
    switch (ui.ms) {
    case MS_ZONE:
        ch_name(s, z->in_ch); cells_set(top, 0, "IN CH", s);
        note_name(s, z->lo); cells_set(top, 1, "LOWEST", s);
        org_fmt(s, z->transpose, 1); cells_set(top, 2, "TRANSP", s);
        org_fmt(s, z->vel, 0); cells_set(top, 3, "VEL", s);
        ch_name(s, z->out_ch); cells_set(sel, 0, "OUT CH", s);
        note_name(s, z->hi); cells_set(sel, 1, "HIGHEST", s);
        org_fmt(s, z->octave, 1); cells_set(sel, 2, "OCTAVE", s);
        cells_set(sel, 3, "BEND", CTL_NAME[ml_ctl_mode]);
        break;
    case MS_CLONE:
        org_fmt(s, (int32_t)ui.clone_cur + 1, 0); cells_set(top, 0, "CHANNEL", s);
        org_fmt(s, z->clone_oct[ui.clone_cur], 1); cells_set(top, 1, "OCTAVE", s);
        break;
    case MS_YST: {
        static const char *const L[8] = {"CUTOFF", "RESO", "ATTACK", "DECAY", "VOLUME", "PAN", "REVERB", "DELAY"};
        uint32_t i;
        for (i = 0; i < 8u; i++) {
            uint32_t v = yst.t[yst.cur].v[YP_CUTOFF + i];
            if (v == YP_UNKNOWN) cat(s, "--");
            else org_fmt(s, YP_CUTOFF + i == YP_PAN ? (int32_t)v - 64 : (int32_t)v, YP_CUTOFF + i == YP_PAN);
            cells_set(i < 4u ? top : sel, i & 3u, L[i], s);
        }
        break;
    }
    case MS_HARM:
        org_fmt(cat(org_fmt(s, z->harm_base + 1, 0), "-"), z->harm_base + 9, 0);
        cells_set(top, 0, "CH FROM", s);
        cells_set(top, 1, "SENDS", CSEND_NAME[z->harm_send]);
        break;
    default:
        org_fmt(s, z->chord_n, 0); cells_set(top, 0, "NOTES", s);
        break;
    }
}

static void midi_btnmap(btnmap_t *b)
{
    const zone_t *z = &zn.z[mls.focus];
    memset(b, 0, sizeof *b);
    btn_common(b);
    if (ui.ms == MS_ZONE) {
        if (ui.shift) {
            btn_set(b, BTN_ENV, "EXPR", z->flags & Z_VELEXPR);
            btn_set(b, BTN_LFO, "THRU", zn.thru);
            btn_set(b, BTN_EDIT, ml_rate_safe ? "TRS" : "USB", ml_rate_safe);
            btn_set(b, BTN_ARP, zn.keys == KEYS_ZONES ? "K:ZN" : "K:FL", zn.keys == KEYS_ZONES);
            btn_set(b, BTN_SEQ, "AUTO", zn.auto_pedal);
        } else {
            btn_set(b, BTN_ENV, "VEL", z->flags & Z_VELTHRU);
            btn_set(b, BTN_LFO, "CC-F", z->flags & Z_NOCC);
            btn_set(b, BTN_EDIT, "PC-F", z->flags & Z_NOPC);
            btn_set(b, BTN_ARP, "INT", !(z->flags & Z_NOINT));
            btn_set(b, BTN_SEQ, "LRN", mls.learn);
        }
    } else if (ui.ms == MS_YST) {
        uint32_t m = yst.t[yst.cur].v[YP_MODE];
        btn_set(b, BTN_ENV, "ON", zn.yst);
        btn_set(b, BTN_LFO, "RFSH", zn.yst && yst.ask < 2u * YST_NTRK);
        btn_set(b, BTN_ARP, "MUTE", yst.t[yst.cur].mute);
        btn_set(b, BTN_SEQ, m == 0u ? "MONO" : m == 2u ? "CHRD" : "POLY", 0);
    } else if (ui.ms == MS_CLONE) {
        btn_set(b, BTN_ENV, "ON", z->flags & Z_CLONE);
    } else if (ui.ms == MS_HARM) {
        btn_set(b, BTN_ENV, "ON", z->flags & Z_HARM && mls.focus != MAN_PEDAL);
    } else {
        btn_set(b, BTN_ENV, "ON", z->flags & Z_CHORD);
        btn_set(b, BTN_LFO, "CAPT", 0);
        btn_set(b, BTN_EDIT, "CLR", 0);
    }
}

static const char *midi_hint(void)
{
    static const char *const H[MS_N] = {"SEL+BUTTONS: GLOBAL SWITCHES", "KEYS 1-16: CHANNELS", "KEYS 1-9: FOOTAGES",
                                        "HOLD A CHORD, PRESS CAPT", BOARD_YST_HINT};
    if (ui.ms == MS_YST && !zn.yst) return "YST OFF  " BOARD_YST_ON ": ON";
    if (ui.ms == MS_YST && !yst.on) return yst.ask < 2u * YST_NTRK ? "LOOKING FOR THE SEQTRAK" : "NO SEQTRAK " BOARD_YST_REF ": AGAIN";
    return H[ui.ms];
}

/* the picture over the knob rows, 64 px */
static void draw_vis_zone(void)
{
    uint32_t zi, o;
    static const char *const T[3] = {"UP", "LO", "PD"};
    cv_begin(240, 64, C_BG);
    for (o = 0; o <= 10u; o++) cv_rect(24 + (int32_t)(o * 12u * 212u / 128u), 0, 1, 50, C_GRID);   /* the Cs */
    for (zi = 0; zi < ORG_NMAN; zi++) {
        const zone_t *z = &zn.z[zi];
        int32_t y = (int32_t)zi * 16 + 1, x0 = 24 + (int32_t)(z->lo * 212u / 128u), x1 = 24 + (int32_t)((z->hi + 1u) * 212u / 128u);
        uint32_t live = z->in_ch < CH_OFF && !(zi == MAN_PEDAL && zn.auto_pedal);
        uint16_t c = zi == mls.focus ? C_ACC : live ? C_CH_ON : C_DARKB;
        char s[24], *p;
        cv_text(2, y - 1, &FONT_S, T[zi], zi == mls.focus ? C_ACC : C_MUT);
        cv_rect(x0, y + 1, x1 - x0, 13, c);
        p = s;
        if (zi == MAN_PEDAL && zn.auto_pedal) p = cat(p, "AUTO");
        else { p = ch_name(p, z->in_ch); }
        p = cat(p, ">"); ch_name(p, z->out_ch);
        cv_text(x0 + 2 + text_w_s(s) > 236 ? 236 - text_w_s(s) : x0 + 2, y - 1, &FONT_S, s, C_BLACK);
    }
    cv_text(20, 48, &FONT_S, "C-1", C_MUT);
    cv_text(24 + (int32_t)(48u * 212u / 128u) - 8, 48, &FONT_S, "C3", C_MUT);
    cv_text(24 + (int32_t)(96u * 212u / 128u) - 8, 48, &FONT_S, "C7", C_MUT);
    cv_text(206, 48, &FONT_S, zn.keys == KEYS_ZONES ? "K:Z" : "K:F", C_MUT);
    cv_blit(0, 24);
}

static void draw_vis_clone(void)
{
    const zone_t *z = &zn.z[mls.focus];
    uint32_t ch;
    cv_begin(240, 64, C_BG);
    for (ch = 0; ch < 16u; ch++) {
        int32_t x = (int32_t)(ch % 8u) * 30, y = (int32_t)(ch / 8u) * 32;
        uint32_t on = z->clone_mask >> ch & 1u, main = ch == z->out_ch;
        uint32_t act = mls.act_ms[ch] && fm1_ms - mls.act_ms[ch] < 150u;
        char s[4];
        cv_rect(x + 1, y + 1, 28, 30, ch == ui.clone_cur ? C_ACC : act ? C_WHITE : C_GRID);
        cv_rect(x + 3, y + 3, 24, 26, main ? C_DARKB : on ? C_CH_ON : C_BG);
        org_fmt(s, (int32_t)ch + 1, 0);
        cv_text(x + 15 - text_w_s(s) / 2, y + 2, &FONT_S, s, on ? C_BLACK : C_MUT);
        if (on && z->clone_oct[ch]) { org_fmt(s, z->clone_oct[ch], 1); cv_text(x + 15 - text_w_s(s) / 2, y + 15, &FONT_S, s, C_BLACK); }
        else if (main) cv_text(x + 15 - text_w_s(MAN_TAG[mls.focus]) / 2, y + 15, &FONT_S, MAN_TAG[mls.focus], C_TXT);
        else if (yst_ch_tag(ch)) cv_text(x + 15 - text_w_s(yst_ch_tag(ch)) / 2, y + 15, &FONT_S, yst_ch_tag(ch), on ? C_BLACK : C_MUT);
    }
    cv_blit(0, 24);
}

static void draw_vis_harm(void)
{
    static const char *const FT[9] = {"16", "5", "8", "4", "2", "2", "1", "1", "1"};
    const zone_t *z = &zn.z[mls.focus];
    uint32_t f;
    cv_begin(240, 64, C_BG);
    if (mls.focus == MAN_PEDAL) {
        cv_text(4, 24, &FONT_S, "NOT FOR THE PEDAL", C_MUT);
    } else {
        for (f = 0; f < 9u; f++) {
            int32_t x = 4 + (int32_t)f * 26, ch = z->harm_base + (int32_t)f;
            uint32_t on = z->harm_mask >> f & 1u, db = org_par.drawbar[mls.focus][f];
            char s[4];
            cv_rect(x, 0, 24, 20, on ? C_CH_ON : C_GRID);
            cv_text(x + 12 - text_w_s(FT[f]) / 2, 2, &FONT_S, FT[f], on ? C_BLACK : C_MUT);
            org_fmt(s, ch + 1, 0);
            cv_text(x + 12 - text_w_s(s) / 2, 22, &FONT_S, s, ch > 15 ? C_STR : C_TXT);
            cv_rect(x + 4, 40, 16, 24, C_GRID);
            cv_rect(x + 4, 40, 16, (int32_t)db * 3, bar_colour(dept_now(), f));
        }
    }
    cv_blit(0, 24);
}

static void draw_vis_yst(void)          /* the SEQTRAK's selected track: its name, type, voice, sound */
{
    const yst_trk_t *k = &yst.t[yst.cur];
    const char *cat_n, *nm = yst_sound(yst.cur, &cat_n);
    char s[40], *p;
    cv_begin(240, 64, C_BG);
    if (!yst.on) {                              /* off, looking, or nobody answered: what to do, and the count */
        static const char HX[] = "0123456789ABCDEF";
        uint32_t i;
        if (!zn.yst) {
            cv_text(2, 0, &FONT_S, "YST IS OFF", C_ACC);
            cv_text(2, 20, &FONT_S, BOARD_YST_ON ": SWITCH IT ON", C_TXT);
            cv_blit(0, 24);
            return;
        }
        if (yst.sx_last_n) {                    /* (diagnosis) the last Yamaha SysEx: was it an answer? */
            p = cat(s, "43:");
            for (i = 1; i < yst.sx_last_n; i++) { *p++ = HX[yst.sx_last[i] >> 4]; *p++ = HX[yst.sx_last[i] & 15u]; }
            *p = 0;
        } else cat(s, yst.ask < 2u * YST_NTRK ? "LOOKING FOR THE SEQTRAK" : "THE SEQTRAK DID NOT ANSWER");
        cv_text(2, 0, &FONT_S, s, C_ACC);
        p = cat(s, "TX "); org_fmt(p, (int32_t)(usb_tx_count() % 100000u), 0);   /* (the host took this many) */
        cv_text(238 - text_w_s(s), 20, &FONT_S, s, C_MUT);
        p = cat(s, "IN "); p = org_fmt(p, yst.midi_in, 0); p = cat(p, "  SX IN "); org_fmt(p, yst.sx_in, 0);
        cv_text(2, 20, &FONT_S, s, C_TXT);
        p = cat(s, "NOT SENT "); p = org_fmt(p, yst.lost, 0); p = cat(p, "  NO ANSWER "); org_fmt(p, yst.silent, 0);
        cv_text(2, 40, &FONT_S, s, C_MUT);
        cv_blit(0, 24);
        return;
    }
    p = org_fmt(s, yst.cur + 1, 0); *p++ = ' '; p = cat(p, YST_TRACK[yst.cur]);
    cv_text(2, 0, &FONT_S, s, C_ACC);
    p = cat(s, YT_NAME[k->type]);
    if (k->v[YP_MODE] != YP_UNKNOWN && (k->type == YT_SYNTH || k->type == YT_DX)) { *p++ = ' '; p = cat(p, YMODE_NAME[k->v[YP_MODE]]); }
    if (k->mute) p = cat(p, " MUTE");
    cv_text(238 - text_w_s(s), 0, &FONT_S, s, k->mute ? C_HRED : C_TXT);
    cv_text(2, 20, &FONT_S, nm ? nm : yst.on ? "(sound not known yet)" : "-", nm ? C_WHITE : C_MUT);
    if (nm) { p = cat(s, cat_n); p = cat(p, "  #"); org_fmt(p, k->snd, 0); }
    else cat(s, yst.on ? "PRESETS / < >: CHOOSE ONE" : "");
    cv_text(2, 40, &FONT_S, s, C_MUT);
    cv_blit(0, 24);
}

static void draw_vis_chord(void)
{
    const zone_t *z = &zn.z[mls.focus];
    uint32_t k, w = 0, lit = 0, i;
    for (i = 0; i < z->chord_n && i < ML_MAXCHORD; i++) if (z->chord[i] >= 0 && z->chord[i] < 25) lit |= 1u << z->chord[i];
    cv_begin(240, 64, C_BG);
    for (k = 0; k < 25u; k++) {                 /* two octaves from C: the chord over its lowest note */
        uint32_t black = (0x54Au >> (k % 12u)) & 1u;
        if (black) continue;
        cv_rect((int32_t)(8 + w * 15), 4, 14, 56, lit >> k & 1u ? C_ACC : C_IVORY);
        w++;
    }
    for (k = 0, w = 0; k < 25u; k++) {
        uint32_t black = (0x54Au >> (k % 12u)) & 1u;
        if (black) cv_rect((int32_t)(8 + w * 15 - 5), 4, 10, 34, lit >> k & 1u ? C_ACC : C_BLACK);
        else w++;
    }
    cv_blit(0, 24);
}

static void ui_draw(void)
{
    uint32_t h, d, m, view = ui.mode == MODE_PLAY ? 0u : ui.mode == MODE_FX ? 1u : ui.mode == MODE_MIX ? 2u + MS_N :
                             ui.mode == MODE_LIGHT ? 3u + MS_N : ui.mode == MODE_INS ? 4u + MS_N :
                             ui.mode == MODE_MST ? 5u + MS_N : 2u + ui.ms;
    char tag[16], b[16], r[16];
    memset(tag, 0, sizeof tag); memset(b, 0, sizeof b); memset(r, 0, sizeof r);
    if (view != drawn_view) {
        lcd_fill(0, 0, 240, 240, C_BG);
        drawn_view = view;
        for (d = 0; d < R_N; d++) drawn[d] = 0;
    }
    model_tag(tag);
    if (ui.mode == MODE_PLAY) {
        char *p = cat(r, "P");
        p = org_fmt(p, (int32_t)dbp_cur() + 1, 0);
        if (dbp_modified(mls.focus)) cat(p, "*");
        h = hash_mem(hash_mem(hash_add(hash_add(1u, mls.focus), ui.grp), tag, sizeof tag), r, 8) | 1u;
        if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, GRP_BADGE[ui.grp], r, C_MUT); }
        h = hash_add(hash_add(hash_add(2u, mls.focus), ui.grp), org_par.model);
        h = hash_row(h, org_par.drawbar[mls.focus < ORG_NMAN ? mls.focus : 0u], ORG_NDB) | 1u;
        if (h != drawn[R_BARS]) { drawn[R_BARS] = h; draw_bars(); }
        h = hash_add(hash_add(hash_add(3u, mls.focus), org_par.coupler), st0.setup);
        for (m = 0; m < ORG_NMAN; m++) h = hash_row(h, org_par.drawbar[m], ORG_NDB);
        h = hash_add(hash_mem(h, ctl.msg, sizeof ctl.msg), ctl_msg_on()) | 1u;
        if (h != drawn[R_MINI]) { drawn[R_MINI] = h; draw_mini(); }
        m = mls.focus;
        h = hash_add(hash_add(hash_add(hash_add(hash_add(4u, org_par.drive[m]), org_par.vib[m]), org_par.perc), org_par.trem[m]), m);
        h = hash_add(hash_add(hash_add(hash_add(h, org_par.rev_mode), org_par.rsend[m]), org_par.dsend[m]), org_par.leslie + 4u * org_par.les_on[m]);
        h = hash_add(hash_add(hash_add(h, org_par.model), org_par.coupler), (uint32_t)(ctl.oct + 8)) | 1u;
        if (h != drawn[R_BOT]) { drawn[R_BOT] = h; draw_bottom(); }
        return;
    }
    {
        static cells_t top, sel;
        static btnmap_t bm;
        const char *hint;
        if (ui.mode == MODE_FX) {
            fx_btnmap(&bm);
            cat(b, ui.shift ? "FX 2ND" : "FX");
            cat(r, LESLIE_NAME[org_par.leslie]);
            hint = ui.shift ? "SHARED BY ALL MANUALS" : "ENV LFO ARP: MANUAL SEQ: ROT";
            h = hash_mem(hash_add(hash_add(5u, ui.shift + 2u * org_par.leslie), mls.focus), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, b, r, C_MUT); }
            if (ui.shift) {
                fx_cells(&sel);
                h = hash_mem(7u, &sel, sizeof sel) | 1u;
                if (h != drawn[R_VIS]) {
                    drawn[R_VIS] = h;
                    lcd_fill(0, 24, 240, 136, C_BG);
                    draw_cells(60, &sel);
                }
            } else {
                h = hash_add(hash_add(6u, dept_now()), mls.focus);
                for (m = 0; m < ORG_NMAN; m++)
                    h = hash_add(h, org_par.drive[m] | org_par.vib[m] << 7 | org_par.trem[m] << 10 | org_par.les_on[m] << 17 |
                                    org_par.dsend[m] << 18 | (uint32_t)org_par.rsend[m] << 25);
                h |= 1u;
                if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_fx_grid(); }
            }
        } else if (ui.mode == MODE_INS || ui.mode == MODE_MST) {
            uint32_t ins = ui.mode == MODE_INS;
            if (ins) { ins_cells(&top, &sel); ins_btnmap(&bm); } else { mst_cells(&top, &sel); mst_btnmap(&bm); }
            cat(b, ins ? (ui.shift ? "INSERT 2ND" : "INSERT") : ui.shift ? "MASTER 2ND" : "MASTER");
            hint = ins ? (ui.shift ? "KNOBS: VALUES 5-8" : "EDIT: EFFECT  SEQ: ON/OFF") : "ENV:EQ  LFO:COMP  EDIT:KNOBS";
            h = hash_mem(hash_add(hash_add(19u + ins, ui.shift), fxi_st.man), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, ins ? MAN_BADGE[fxi_st.man] : "SUM", C_TXT, b, r, C_MUT); }
            h = ins ? hash_add(hash_add(hash_add(20u, fxi_st.type), fxi_st.man), ui.fx_last)
                    : hash_add(hash_add(hash_add(hash_add(21u, mst_st.eq_on), mst_st.comp_on), ui.mst_comp),
                               (uint32_t)(mst_st.comp_on ? mst_gr10() / 5 : 0) + 64u);
            h |= 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; if (ins) draw_vis_ins(); else draw_vis_mst(); }
            h = hash_mem(22u, &top, sizeof top) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(88, &top); }
            h = hash_mem(23u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(124, &sel); }
        } else if (ui.mode == MODE_MIX) {
            uint32_t ch;
            mix_btnmap(&bm);
            cat(tag, "MIDI");
            cat(b, "MIXER");
            cat(r, "CC7");
            hint = "ENV LFO ARP SEQ: THE ROW";
            h = hash_add(hash_add(15u, ui.mix_row), mls.focus) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_ACC, b, r, C_MUT); }
            h = hash_add(hash_add(16u, ui.mix_row), ml_used_channels());
            for (ch = 0; ch < 16u; ch++) h = hash_add(h, ml_mix[ch]);
            for (m = 0; m < ORG_NMAN; m++) h = hash_add(h, zn.z[m].out_ch);
            h |= 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_mix_grid(); }
        } else if (ui.mode == MODE_LIGHT) {
            light_cells(&top);
            light_btnmap(&bm);
            cat(b, "LIGHTS");
            hint = BOARD_LIGHT_HINT;
            h = 17u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, "PANEL", C_TXT, b, r, C_MUT); }
            h = hash_mem(18u, &top, sizeof top) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(60, &top); }
        } else {
            uint32_t act = 0, ch;
            const zone_t *z = &zn.z[mls.focus];
            midi_cells(&top, &sel);
            midi_btnmap(&bm);
            {
                char *p = cat(b, MS_NAME[ui.ms]);
                *p++ = ' '; *p++ = (char)('1' + ui.ms); *p++ = '/'; *p++ = (char)('0' + MS_N); *p = 0;
            }
            cat(tag, "MIDI");
            cat(r, "");
            hint = midi_hint();
            h = hash_mem(hash_add(10u, ui.ms), MAN_BADGE[mls.focus], 5) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_ACC, b, r, C_MUT); }
            for (ch = 0; ch < 16u; ch++) if (mls.act_ms[ch] && fm1_ms - mls.act_ms[ch] < 150u) act |= 1u << ch;
            h = hash_mem(hash_add(hash_add(hash_add(11u, ui.ms), mls.focus), ui.clone_cur), &zn, sizeof zn);
            h = hash_add(h, ml_ctl_mode);
            if (ui.ms == MS_CLONE) h = hash_add(h, act);
            if (ui.ms == MS_HARM) h = hash_row(h, org_par.drawbar[mls.focus], ORG_NDB);
            if (ui.ms == MS_YST) h = hash_add(hash_add(hash_add(h, yst.gen), yst.cur), yst.on ? 0u : usb_tx_count());
            h = hash_add(h, dept_now()) | 1u;
            (void)z;
            if (h != drawn[R_VIS]) {
                drawn[R_VIS] = h;
                if (ui.ms == MS_ZONE) draw_vis_zone();
                else if (ui.ms == MS_CLONE) draw_vis_clone();
                else if (ui.ms == MS_HARM) draw_vis_harm();
                else if (ui.ms == MS_YST) draw_vis_yst();
                else draw_vis_chord();
            }
            h = hash_mem(12u, &top, sizeof top) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(88, &top); }
            h = hash_mem(13u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(124, &sel); }
        }
        h = msg_hash(hint);
        if (h != drawn[R_MSG]) { drawn[R_MSG] = h; draw_msgline(hint); }
        h = hash_mem(14u, &bm, sizeof bm) | 1u;
        if (h != drawn[R_BTN]) { drawn[R_BTN] = h; draw_btnmap(&bm); }
    }
}

/* =========================================================== the loop */
static board_keys_t ui_prev_keys;
#include "../smk37/orgigs_intro.c"         /* the welcome screen (the Elite's: the logo; no pads here) */

static void ui_init(void)
{
    ctl_init();
    par_init();                         /* the insert effect, the master: as kept */
    yst_reset();
#if !defined(ORGIGS_NO_INTRO) && !defined(ORGIGS_SELFTEST) && !defined(ORGIGS_TONETEST)
    intro_start();
#endif
}

static int32_t setup_of_key(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < NSETUP; i++) if (SETUP_KEY[i] + BOARD_SETUP_K0 == k) return (int32_t)i;
    return -1;
}

/* one LED at level lv (0 off, 1 glow, 2 lit), as the lights of its area want it:
 *   OFF   as it is;  DIM   and every LED glows (the backlight layer);  FULL  every LED lit, what was lit blinks */
static void led_out(uint32_t id, uint32_t lv, uint32_t lt)
{
    if (lt == LT_FULL) {
        uint32_t on = (fm1_ms >> 8) & 1u;
        led_level(id, !lv ? 2u : on ? 2u : lv == 2u ? 0u : 1u);
        led_bg(id, 0);
    } else {
        led_level(id, lv);
        led_bg(id, lt == LT_DIM);
    }
}

static void ui_leds(void)
{
    static const uint8_t BTNS[12] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO,
                                     BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY, BTN_REC};
    uint32_t k, b, save = fm1_in.buttons >> BTN_SAVE & 1u, midi = ui.mode == MODE_MIDI;
    uint8_t lv[FM1_NBTN];
    k = st0.lt_level < LT_LEVELS ? st0.lt_level : 0u;
    led_bg_level(LT_NS[k], LT_DUTY[k]);
    const zone_t *z = &zn.z[mls.focus];
    for (k = 0; k < BOARD_NKEYS; k++) {
        uint32_t l = 0;
        if (ui.shift || save) { int32_t n = setup_of_key(k); l = n < 0 ? 0u : (uint32_t)n == st0.setup ? 2u : 1u; }
        else if (midi && ui.ms == MS_CLONE) l = k < 16u ? (z->clone_mask >> k & 1u ? 2u : k == z->out_ch ? 1u : 0u) : 0u;
        else if (midi && ui.ms == MS_HARM) l = k < 9u && mls.focus != MAN_PEDAL ? (z->harm_mask >> k & 1u ? 2u : 1u) : 0u;
        else l = fm1_in.notes >> k & 1u ? 2u : 0u;
        led_out(KEY_LED(k), l, st0.lt_keys);
    }
    memset(lv, 0, sizeof lv);
    if (midi) {
        static btnmap_t bm;
        midi_btnmap(&bm);
        for (b = 0; b < 12u; b++)
            if (BTN_AT[b] == BTN_ENV || BTN_AT[b] == BTN_LFO || BTN_AT[b] == BTN_EDIT || BTN_AT[b] == BTN_ARP || BTN_AT[b] == BTN_SEQ)
                lv[BTN_AT[b]] = bm.on[b] ? 2u : 0u;
    } else if (ui.mode == MODE_MIX) {           /* the four row buttons glow, the row the knobs hold is lit */
        lv[BTN_ENV] = ui.mix_row == 0u ? 2u : 1u;
        lv[BTN_LFO] = ui.mix_row == 1u ? 2u : 1u;
        lv[BTN_ARP] = ui.mix_row == 2u ? 2u : 1u;
        lv[BTN_SEQ] = ui.mix_row == 3u ? 2u : 1u;
    } else if (ui.mode == MODE_FX) {            /* the manuals' buttons glow, the focused one is lit */
        lv[BTN_ENV] = mls.focus == MAN_UPPER ? 2u : 1u;
        lv[BTN_LFO] = mls.focus == MAN_LOWER ? 2u : 1u;
        lv[BTN_ARP] = mls.focus == MAN_PEDAL ? 2u : 1u;
        lv[BTN_SEQ] = org_par.les_on[mls.focus] ? 2u : 0u;
    } else if (ui.mode == MODE_INS) {           /* the manuals' buttons glow, the effect's is lit; SEQ: the effect on */
        lv[BTN_ENV] = fxi_st.man == MAN_UPPER ? 2u : 1u;
        lv[BTN_LFO] = fxi_st.man == MAN_LOWER ? 2u : 1u;
        lv[BTN_ARP] = fxi_st.man == MAN_PEDAL ? 2u : 1u;
        lv[BTN_EDIT] = 1u;
        lv[BTN_SEQ] = fxi_st.type ? 2u : 1u;
    } else if (ui.mode == MODE_MST) {           /* EQ, compressor: lit when on; EDIT lit while the knobs hold the compressor */
        lv[BTN_ENV] = mst_st.eq_on ? 2u : 1u;
        lv[BTN_LFO] = mst_st.comp_on ? 2u : 1u;
        lv[BTN_EDIT] = ui.mst_comp ? 2u : 1u;
    } else if (ui.mode == MODE_PLAY) {
        uint32_t m = mls.focus;
        lv[BTN_ENV] = ui.grp == GRP_DB14 ? 2u : 0u;
        lv[BTN_LFO] = ui.grp == GRP_DB58 ? 2u : 0u;
        lv[BTN_EDIT] = ui.grp == GRP_DB9 ? 2u : 0u;
        lv[BTN_ARP] = (dept_now() == DEPT_VINTAGE ? org_par.vib[m] != 0u : org_par.trem[m] != 0u) ? 2u : 0u;
        lv[BTN_SEQ] = org_par.coupler ? 2u : 0u;
    }
    lv[BTN_FX] = ui.mode == MODE_FX || ui.mode == MODE_INS || ui.mode == MODE_MST ? 2u : 0u;
    lv[BTN_GLO] = midi || ui.mode == MODE_LIGHT ? 2u : 0u;
    lv[BTN_HOME] = ui.mode == MODE_MIX ? 2u : 0u;
    lv[BTN_PLAY] = org_par.leslie == LESLIE_BRAKE ? 2u : 0u;
    lv[BTN_REC] = org_par.leslie == LESLIE_FAST ? 2u : 0u;
    lv[BTN_SEL] = ui.shift ? 2u : 0u;
    lv[BTN_SAVE] = save ? 2u : 0u;
    for (b = 0; b < 12u; b++) led_out(BTNS[b], lv[BTNS[b]], st0.lt_btns);
    fm1_led_key(BTN_OCT_DOWN, ctl.oct < 0);     /* the octave buttons: as ever */
    fm1_led_key(BTN_OCT_UP, ctl.oct > 0);
}

static void ui_buttons(uint32_t b, uint32_t rel)
{
    uint32_t midi = ui.mode == MODE_MIDI, m = mls.focus;
    if (bk_asking()) {                          /* a backup came in: SAVE loads it, any other button keeps all */
        if (b & 1u << BTN_SAVE) bk_answer(1);
        else if (b) bk_answer(0);
        ui.save_used = 1;                       /* (SAVE's release stores no drawbars) */
        return;
    }
    if (b & (1u << BTN_OCT_DOWN | 1u << BTN_OCT_UP)) ctl_octave(b >> BTN_OCT_DOWN & 1u, b >> BTN_OCT_UP & 1u);
    if (b & 1u << BTN_SAVE && !ui.save_used) ctl_say("TAP: DRAWBARS  +KEY: SETUP", 0);
    if (rel & 1u << BTN_SAVE && !ui.save_used) {
        char s[24], *p = s;
        uint32_t u = dbp_store(m);
        if (!u) ctl_say("ALL USER SLOTS TAKEN:", "TURN TO ONE");
        else {
            p = cat(p, MAN_TAG[m]); p = cat(p, m == MAN_PEDAL ? " PEDAL USER " : " USER "); org_fmt(p, (int32_t)u, 0);
            ctl_say("DRAWBARS SAVED", s);
        }
        touched();
    }
    if (b & 1u << BTN_FX) {                     /* FX: the effects, again back; SEL + FX: INSERT, again MASTER */
        if (ui.shift) set_mode(ui.mode == MODE_INS ? MODE_MST : MODE_INS);
        else set_mode(ui.mode == MODE_FX || ui.mode == MODE_INS || ui.mode == MODE_MST ? MODE_PLAY : MODE_FX);
    }
    if (b & 1u << BTN_GLO) {
        if (ui.shift) set_mode(ui.mode == MODE_LIGHT ? MODE_PLAY : MODE_LIGHT);
        else if (!midi) set_mode(MODE_MIDI);
        else { ui.ms = (ui.ms + 1u) % MS_N; mls.learn = 0; set_mode(MODE_MIDI); }
        if (ui.mode == MODE_MIDI && ui.ms == MS_YST) yst_follow_zone();
    }
    if (b & 1u << BTN_HOME) {
        if (ui.shift) { ml_panic(); ctl_say("PANIC", "ALL NOTES OFF"); }
        else if (ui.mode == MODE_PLAY) set_mode(MODE_MIX);
        else set_mode(MODE_PLAY);
    }
    if (ui.mode == MODE_MIX) {                  /* ENV LFO / ARP SEQ: the rows, as the buttons sit (2 x 2) */
        if (b & 1u << BTN_ENV) ui.mix_row = 0;
        if (b & 1u << BTN_LFO) ui.mix_row = 1;
        if (b & 1u << BTN_ARP) ui.mix_row = 2;
        if (b & 1u << BTN_SEQ) ui.mix_row = 3;
        if (b & (1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_ARP | 1u << BTN_SEQ)) {
            char t[16], *p = cat(t, "CH ");
            p = org_fmt(p, (int32_t)ui.mix_row * 4 + 1, 0); p = cat(p, "-"); org_fmt(p, (int32_t)ui.mix_row * 4 + 4, 0);
            ctl_say("MIXER ROW", t);
        }
        b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT | 1u << BTN_ARP | 1u << BTN_SEQ);
    } else if (ui.mode == MODE_FX) {            /* ENV LFO ARP: the manual's row; SEQ: its rotary */
        if (b & 1u << BTN_ENV) focus_set(MAN_UPPER);
        if (b & 1u << BTN_LFO) focus_set(MAN_LOWER);
        if (b & 1u << BTN_ARP) focus_set(MAN_PEDAL);
        if (b & 1u << BTN_SEQ) les_toggle();
        b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT | 1u << BTN_ARP | 1u << BTN_SEQ);
    } else if (ui.mode == MODE_INS) {           /* ENV LFO ARP: the manual it sits on; EDIT: the next effect; SEQ: on / off */
        if (b & 1u << BTN_ENV) fxi_man(MAN_UPPER);
        if (b & 1u << BTN_LFO) fxi_man(MAN_LOWER);
        if (b & 1u << BTN_ARP) fxi_man(MAN_PEDAL);
        if (b & 1u << BTN_EDIT) ins_next(ui.shift ? -1 : 1);
        if (b & 1u << BTN_SEQ) ins_toggle();
        b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT | 1u << BTN_ARP | 1u << BTN_SEQ);
    } else if (ui.mode == MODE_MST) {           /* ENV: the EQ on / off; LFO: the compressor; EDIT: what the knobs hold */
        if (b & 1u << BTN_ENV) mst_toggle(0);
        if (b & 1u << BTN_LFO) mst_toggle(1);
        if (b & 1u << BTN_EDIT) { ui.mst_comp ^= 1u; ctl_say("KNOBS:", ui.mst_comp ? "COMPRESSOR" : "EQ"); }
        b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT | 1u << BTN_ARP | 1u << BTN_SEQ);
    } else if (ui.mode == MODE_LIGHT) {
        if (b & 1u << BTN_SEQ) bk_send_all();
        b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT | 1u << BTN_ARP | 1u << BTN_SEQ);
    }
    if (b & (1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT)) {
        uint32_t g = b & 1u << BTN_ENV ? GRP_DB14 : b & 1u << BTN_LFO ? GRP_DB58 : GRP_DB9;
        if (midi) midi_switch(g);
        else { ui.grp = g; set_mode(MODE_PLAY); }
    }
    if (b & 1u << BTN_ARP) {
        if (midi) midi_switch(3);
        else ctl_vib_toggle();
    }
    if (b & 1u << BTN_SEQ) {
        if (midi) midi_switch(4);
        else ctl_coupler_toggle();
    }
    if (b & 1u << BTN_PLAY) ctl_leslie_brake();
    if (b & 1u << BTN_REC) {
        if (ui.shift) les_toggle();
        else ctl_leslie_fast();
    }
}

static void ui_step(void)
{
    board_keys_t keys, down, up;
    uint32_t b, rel, k, save;
    int32_t st;
    uint8_t ms, m1, m2;
    int src;

    /* MIDI in, then the panel */
    while ((src = midi_get(&ms, &m1, &m2)) != 0) if (!yst_midi_in(ms, m1, m2, src == 1)) ml_midi_in(ms, m1, m2);   /* (the SEQTRAK: YST) */
    yst_tick();
    if (bat_tick()) drawn[R_TOP] = 0;
    if (intro_step()) { ml_flush(); return; }

    if (!ui.shift && (fm1_in.buttons >> BTN_SEL & 1u)) {     /* SEL pressed: the setups, the audio interrupt's load, */
        char s[40], *p = cat(s, "KEYS 1-16 DSP ");           /* the battery and (for now) its raw reading: the line */
        p = org_fmt(p, (int32_t)(org_stat.load_us * 100u / 5805u), 0);   /* holds about 30 characters */
        if (bat_ok()) {
            p = cat(org_fmt(cat(p, "% BAT "), (int32_t)bat_pct(), 0), "% ");
            org_fmt(p, (int32_t)batt_raw(), 0);
        } else cat(p, "%");
        ctl_say(s, 0);
    }
    ui.shift = fm1_in.buttons >> BTN_SEL & 1u;
    save = fm1_in.buttons >> BTN_SAVE & 1u;
    b = fm1_input_edges(&rel);
    if (b & 1u << BTN_SAVE) { ui.save_used = 0; save = 1; }
    keys = fm1_in.notes;
    down = (keys & ~ui_prev_keys) | fm1_input_note_edges();
    up = ui_prev_keys & ~keys;
    ui_prev_keys = keys;
    for (k = 0; k < BOARD_NKEYS; k++) {
        if (down >> k & 1u) {
            if (ui.shift || save) {             /* the performance setups */
                int32_t n = setup_of_key(k);
                if (n >= 0 && save) { ctl_setup_store((uint32_t)n); ui.save_used = 1; }
                else if (n >= 0) recall_setup((uint32_t)n);
            } else if (ui.mode == MODE_MIDI && ui.ms == MS_CLONE) {
                if (k < 16u) {
                    ml_release_all();
                    zn.z[mls.focus].clone_mask ^= (uint16_t)(1u << k);
                    ui.clone_cur = k;
                    ctl_say_int(zn.z[mls.focus].clone_mask >> k & 1u ? "CLONE ON CH" : "CLONE OFF CH", (int32_t)k + 1, 0);
                    touched();
                }
            } else if (ui.mode == MODE_MIDI && ui.ms == MS_HARM) {
                if (k < 9u && mls.focus != MAN_PEDAL) {
                    ml_release_all();
                    zn.z[mls.focus].harm_mask ^= (uint16_t)(1u << k);
                    ml_harm_resend();
                    touched();
                }
            } else {
                ml_local_key(k, BOARD_KEY0 + (int32_t)k + 12 * ctl.oct, 1);
            }
        }
        if (!(keys >> k & 1u) && ((up | down) >> k & 1u) && mls.src[k] != SRC_NONE) ml_local_key(k, 0, 0);
    }

    ui_buttons(b, rel);

    /* encoders (the SMK-37: its pads, faders and wheels first; on the drawbars its 8 knobs are drawbars 1..8) */
    {
        for (k = 0; k < 4u; k++)
            if ((st = fm1_enc_take(ENC_KNOB1 + k)) != 0) knob(k, st);
        uint32_t yst_on = ui.mode == MODE_MIDI && ui.ms == MS_YST;   /* YST: SELECT the track, PRESETS its sound */
        if ((st = fm1_enc_take(ENC_SELECT)) != 0) {
            if (yst_on) yst_select((uint32_t)clampi((int32_t)yst.cur + st, 0, YST_NTRK - 1));
            else focus_set((uint32_t)clampi((int32_t)mls.focus + st, 0, 2));
        }
        if ((st = fm1_enc_take(ENC_ALGORITHM)) != 0) set_model((int32_t)org_par.model + st);
        if ((st = fm1_enc_take(ENC_PRESETS)) != 0) { if (yst_on) yst_browse(st); else ctl_preset_step(st); }
    }

    ml_drawbars_tick();
    ml_flush();
    ui_leds();
    bk_tick();
    ui_draw();
    ctl_save_tick();
    if (!org_stat.held && org_stat.peak < 64u) par_save_tick();   /* (an erase stalls the audio: when nothing sounds) */
}

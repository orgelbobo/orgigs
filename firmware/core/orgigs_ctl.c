/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs: what a panel can do with the organ (main loop), the same on every board.
 *
 * A board's panel (board/<name>/orgigs_ui.c) reads its keys, buttons, knobs and faders and calls these; it
 * draws its own screens and lights. Here: the shared state (the octave of the panel's keys, the rotary speed
 * the player chose, the message line), the names the screens show, and the actions with their messages:
 * model, manual, drawbars, effects, rotary, the switches, presets and setups, the mod and pitch strips (or the
 * CC 1 and pitch bend that stand in for them), the MIDI mixer, and the saving when nothing sounds.
 * Knob actions take steps as the FM-1's knobs give them. */

static struct {
    int32_t oct;                        /* the panel's keys: octave -2 .. +3 */
    uint32_t leslie_user;               /* the speed the buttons chose; the strips kick it */
    uint32_t kick;
    uint8_t vib_last, trem_last;        /* what vibrato / tremulant comes back when switched on */
    uint32_t t_change;                  /* fm1_ms of the last change (saving waits 2 s) */
    char msg[40];                       /* the message line */
    uint32_t msg_until;
    uint32_t mod_strip;
    int32_t pitch_strip;
} ctl = {.vib_last = VIB_C3, .trem_last = 40};

static const char *const MAN_BADGE[3] = {"UPPER", "LOWER", "PEDAL"};
static const char *const MAN_TAG[3] = {"UM", "LM", "PM"};
static const char *const VIB_NAME[ORG_NVIB] = {"OFF", "V1", "V2", "V3", "C1", "C2", "C3"};
static const char *const PERC_NAME[ORG_NPERC_ALL] = {"OFF", "2ND FAST", "2ND SLOW", "3RD FAST", "3RD SLOW",
                                                     "4TH FAST", "4TH SLOW"};
static const char *const PERC_TAG[ORG_NPERC_ALL] = {"OFF", "2F", "2S", "3F", "3S", "4F", "4S"};
static uint32_t ctl_nperc = ORG_NPERC;  /* the percussions offered (the SMK-37: with 4TH) */
static const char *const REV_NAME[ORG_NREV] = {"SPRING", "STAGE", "HALL", "CATHEDRAL"};
static const char *const REV_TAG[ORG_NREV] = {"SPRING", "STAGE", "HALL", "CATHED"};
static const char *const LESLIE_NAME[3] = {"SLOW", "BRAKE", "FAST"};

static int32_t ui_oct_get(void) { return ctl.oct; }
static void ui_oct_set(int32_t o) { ctl.oct = o; }
static uint32_t ctl_msg_on(void) { return (int32_t)(ctl.msg_until - fm1_ms) > 0; }

/* ---- small text helpers */
static char *cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *org_fmt(char *p, int32_t v, int sign)
{
    char t[12];
    uint32_t n = 0, u = (uint32_t)(v < 0 ? -v : v);
    if (v < 0) *p++ = '-'; else if (sign) *p++ = '+';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static char *note_name(char *p, uint32_t n)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    p = cat(p, NN[n % 12u]);
    return org_fmt(p, (int32_t)(n / 12u) - 1, 0);
}
static const char *yst_ch_tag(uint32_t ch);   /* orgigs_yst.c: a SEQTRAK track on channels 1..11 */
static char *ch_name(char *p, uint32_t ch)
{
    const char *t;
    if (ch >= CH_OFF) return cat(p, "OFF");
    p = org_fmt(p, (int32_t)ch + 1, 0);
    if ((t = yst_ch_tag(ch)) != 0) { *p++ = ' '; p = cat(p, t); }
    return p;
}

static void ctl_say(const char *a, const char *b)
{
    uint32_t i = 0;
    while (*a && i < 38u) ctl.msg[i++] = *a++;
    if (b) { if (i < 38u) ctl.msg[i++] = ' '; while (*b && i < 38u) ctl.msg[i++] = *b++; }
    ctl.msg[i] = 0;
    ctl.msg_until = fm1_ms + 1400u;
}
static void ctl_say_int(const char *a, int32_t v, int sign)
{
    char b[12];
    org_fmt(b, v, sign);
    ctl_say(a, b);
}
static void touched(void) { st_dirty |= 1u; ctl.t_change = fm1_ms; }

static uint32_t dept_now(void) { return ORG_MODELS[org_par.model].dept; }

static void set_model(int32_t m)
{
    const org_model_t *md;
    if (m < 0) m = 0;
    if (m >= (int32_t)ORG_NMODEL) m = ORG_NMODEL - 1;
    if ((uint32_t)m == org_par.model) return;
    md = &ORG_MODELS[m];
    org_par.model = (uint8_t)m;                         /* the effects stay with the manuals */
    ctl_say(md->name, 0);
#ifdef BOARD_SPLASH
    {
        char t[12];
        org_fmt(cat(t, "MODEL "), m + 1, 0);
        board_splash(t, md->name, 0);
    }
#endif
    touched();
}

static void set_leslie(uint32_t s) { org_par.leslie = (uint8_t)s; }

/* ---- the strips (or what stands in for them): internal effect only; local moves also go out */
/* CC 1 and pitch bend (and the strips, where there are any): reeds always bellows and bend; the organs as
   ml_ctl_mode says: PITCH = bend and rotary slow/fast, ROTARY = kick/tutti and slow/brake/fast */
static void ml_strip_mod(uint32_t v)
{
    ctl.mod_strip = v;
    if (dept_now() == DEPT_REED) { org_par.expression = (uint8_t)v; return; }   /* the bellows */
    if (ml_ctl_mode == CTL_PITCH) ctl.leslie_user = v < 64u ? LESLIE_SLOW : LESLIE_FAST;
    else ctl.leslie_user = v < 43u ? LESLIE_SLOW : v < 85u ? LESLIE_BRAKE : LESLIE_FAST;
    if (!ctl.kick) set_leslie(ctl.leslie_user);
}
static void ml_strip_pitch(int32_t b)
{
    ctl.pitch_strip = b;
    if (dept_now() == DEPT_REED || ml_ctl_mode == CTL_PITCH) { org_par.bend = b; return; }
    org_par.bend = 0;                   /* ROTARY: up = rotary kick, down = tutti boost, while held */
    ctl.kick = b > 2000;
    set_leslie(ctl.kick ? LESLIE_FAST : ctl.leslie_user);
    org_par.boost = b < -2000;
}
static void ui_strip_mod_local(uint32_t v) { ml_strip_mod(v); ml_send_mod(v); }
static void ui_strip_pitch_local(int32_t b) { ml_strip_pitch(b); ml_send_bend(b); }

static void ml_learned(void)
{
    char s[24], *p = s;
    const zone_t *z = &zn.z[mls.focus];
    p = note_name(p, z->lo); p = cat(p, " - "); note_name(p, z->hi);
    ctl_say("RANGE", s);
    touched();
}

/* ---- the drawbar presets and the setups, as the panel calls them */
static void say_preset(void)
{
    char s[40], *p = s;
    p = cat(p, MAN_TAG[mls.focus]); p = cat(p, " "); p = org_fmt(p, (int32_t)dbp_cur() + 1, 0);
    p = cat(p, " "); dbp_name(p, mls.focus, dbp_cur());
    ctl_say(s, 0);
}

static void recall_setup(uint32_t n)
{
    setup_recall(n);
    ctl_say_int("PERFORMANCE SETUP", (int32_t)n + 1, 0);
    touched();
}

/* ---- the manual, the drawbars */
static void focus_set(uint32_t m)
{
    if (m == mls.focus) return;
    ml_release_keys();
    mls.learn = 0;
    mls.focus = (uint8_t)m;
    ctl_say("MANUAL", MAN_BADGE[m]);
    touched();
}

static void les_toggle(void)            /* the focused manual through the rotary, or past it */
{
    char t[12];
    org_par.les_on[mls.focus] ^= 1u;
    cat(cat(t, MAN_TAG[mls.focus]), " ROTARY");
    ctl_say(t, org_par.les_on[mls.focus] ? "ON" : "OFF");
    touched();
}

static void ctl_drawbar(uint32_t d, int32_t steps)
{
    uint32_t m = mls.focus;
    int32_t v;
    char s[24], *p;
    if (m == MAN_PEDAL) d &= 3u;
    v = clampi((int32_t)org_par.drawbar[m][d] + steps, 0, 8);
    org_par.drawbar[m][d] = (uint8_t)v;
    p = s;
    if (m == MAN_PEDAL) p = cat(p, PEDAL_LABEL[d]);
    else { const char *q = DB_FOOT[dept_now()][d]; while (*q && p < s + 17) *p++ = *q++; }
    *p++ = ' ';
    org_fmt(p, v, 0);
    ctl_say(s, 0);
    touched();
}

/* ---- the effects: KNOB steps on the focused manual, or the shared ones */
static void ctl_character(int32_t st)  /* tonewheel: percussion; pipes: chiff; reeds: the musette's detune */
{
    uint32_t dept = dept_now();
    if (dept == DEPT_VINTAGE) {
        org_par.perc = (uint8_t)clampi((int32_t)org_par.perc + st, 0, (int32_t)ctl_nperc - 1);
        ctl_say(org_par.model == MODEL_B3 ? "PERCUSSION" : "PERC (WHEEL ONLY)", PERC_NAME[org_par.perc]);
    } else {
        org_par.character = (uint8_t)clampi((int32_t)org_par.character + st * 2, 0, 127);
        ctl_say_int(dept == DEPT_PIPE ? "CHIFF" : "MUSETTE DETUNE C",
                   dept == DEPT_REED ? (int32_t)((ORG_MODELS[org_par.model].detune * (64u + org_par.character)) >> 7) : org_par.character, 0);
    }
    touched();
}
static void ctl_delay_fb(int32_t st) { org_par.delay_fb = (uint8_t)clampi((int32_t)org_par.delay_fb + st * 3, 0, 127); ctl_say_int("DELAY FEEDBACK", org_par.delay_fb, 0); touched(); }
static void ctl_delay_time(int32_t st) { org_par.delay_time = (uint8_t)clampi((int32_t)org_par.delay_time + st * 2, 0, 127); ctl_say_int("DELAY MS", 20 + org_par.delay_time * 980 / 127, 0); touched(); }
static void ctl_rev_mode(int32_t st) { org_par.rev_mode = (uint8_t)clampi((int32_t)org_par.rev_mode + st, 0, ORG_NREV - 1); ctl_say("REVERB", REV_NAME[org_par.rev_mode]); touched(); }
static char *man_label(char *t, const char *what) { char *p = cat(t, MAN_TAG[mls.focus]); *p++ = ' '; return cat(p, what); }
static void ctl_drive(int32_t st)
{
    char t[16];
    uint32_t m = mls.focus;
    org_par.drive[m] = (uint8_t)clampi((int32_t)org_par.drive[m] + st * 2, 0, 127);
    man_label(t, "DRIVE"); ctl_say_int(t, org_par.drive[m], 0);
    touched();
}
static void ctl_vib(int32_t st)         /* vibrato / chorus on the tonewheels, tremulant on the rest */
{
    char t[16];
    uint32_t m = mls.focus;
    if (dept_now() == DEPT_VINTAGE) {
        org_par.vib[m] = (uint8_t)clampi((int32_t)org_par.vib[m] + st, 0, ORG_NVIB - 1);
        if (org_par.vib[m]) ctl.vib_last = org_par.vib[m];
        man_label(t, "VIBRATO"); ctl_say(t, VIB_NAME[org_par.vib[m]]);
    } else {
        org_par.trem[m] = (uint8_t)clampi((int32_t)org_par.trem[m] + st * 4, 0, 127);
        if (org_par.trem[m]) ctl.trem_last = org_par.trem[m];
        man_label(t, "TREMULANT"); ctl_say_int(t, org_par.trem[m], 0);
    }
    touched();
}
static void ctl_dsend(int32_t st)
{
    char t[16];
    uint32_t m = mls.focus;
    org_par.dsend[m] = (uint8_t)clampi((int32_t)org_par.dsend[m] + st * 3, 0, 127);
    man_label(t, "DELAY SEND"); ctl_say_int(t, org_par.dsend[m], 0);
    touched();
}
static void ctl_rsend(int32_t st)
{
    char t[16];
    uint32_t m = mls.focus;
    org_par.rsend[m] = (uint8_t)clampi((int32_t)org_par.rsend[m] + st * 3, 0, 127);
    man_label(t, "REVERB SEND"); ctl_say_int(t, org_par.rsend[m], 0);
    touched();
}

/* ---- the MIDI mixer: a channel's CC 7, sent at once */
static void ctl_mix(uint32_t ch, int32_t st)
{
    char t[20], *p = t;
    ml_mix[ch] = (uint8_t)clampi((int32_t)ml_mix[ch] + st * 2, 0, 127);
    ml_mix_send(ch);
    p = cat(p, "CH "); p = org_fmt(p, (int32_t)ch + 1, 0);
    { uint32_t zi; for (zi = 0; zi < ORG_NMAN; zi++) if (zn.z[zi].out_ch == ch) { p = cat(p, " "); p = cat(p, MAN_TAG[zi]); break; } }
    cat(p, " CC7");
    ctl_say_int(t, ml_mix[ch], 0);
    touched();
}

static void ctl_bend_mode(int32_t st)  /* what bend and CC 1 do on the organs (every zone) */
{
    ml_ctl_mode = st > 0 ? CTL_LESLIE : CTL_PITCH;
    org_par.bend_all = ml_ctl_mode == CTL_PITCH;
    org_par.bend = 0; org_par.boost = 0; ctl.kick = 0;
    set_leslie(ctl.leslie_user);
    ctl_say("BEND + MOD", ml_ctl_mode == CTL_PITCH ? "PITCH + SLOW/FAST" : "ROTARY KICK/TUTTI");
}

/* ---- the switches */
static void ctl_vib_toggle(void)       /* the focused manual's vibrato (tremulant) off, or back to the last */
{
    char t[16];
    uint32_t m = mls.focus;
    cat(cat(t, MAN_TAG[m]), dept_now() == DEPT_VINTAGE ? " VIBRATO" : " TREMULANT");
    if (dept_now() == DEPT_VINTAGE) {
        if (org_par.vib[m]) { ctl.vib_last = org_par.vib[m]; org_par.vib[m] = VIB_OFF; } else org_par.vib[m] = ctl.vib_last ? ctl.vib_last : VIB_C3;
        ctl_say(t, VIB_NAME[org_par.vib[m]]);
        touched();
    } else {
        if (org_par.trem[m]) { ctl.trem_last = org_par.trem[m]; org_par.trem[m] = 0; } else org_par.trem[m] = ctl.trem_last ? ctl.trem_last : 40u;
        ctl_say_int(t, org_par.trem[m], 0);
        touched();
    }
}
static void ctl_coupler_toggle(void) { org_par.coupler = !org_par.coupler; ctl_say("COUPLER II/I", org_par.coupler ? "ON" : "OFF"); touched(); }
static void ctl_leslie_brake(void)     /* brake, or back to slow */
{
    ctl.leslie_user = ctl.leslie_user == LESLIE_BRAKE ? LESLIE_SLOW : LESLIE_BRAKE;
    set_leslie(ctl.leslie_user);
    ctl_say("ROTARY", LESLIE_NAME[ctl.leslie_user]);
}
static void ctl_leslie_fast(void)      /* slow <-> fast */
{
    ctl.leslie_user = ctl.leslie_user == LESLIE_FAST ? LESLIE_SLOW : LESLIE_FAST;
    set_leslie(ctl.leslie_user);
    ctl_say("ROTARY", LESLIE_NAME[ctl.leslie_user]);
}
static void ctl_octave(uint32_t down, uint32_t up)     /* the panel's keys an octave down and / or up */
{
    if (down && ctl.oct > -2) ctl.oct--;
    if (up && ctl.oct < 3) ctl.oct++;
    ctl_say_int("OCTAVE", ctl.oct, 1);
    touched();
}

/* ---- presets and setups */
static void ctl_preset_step(int32_t st)        /* the focused manual's drawbar list, st steps on */
{
    dbp_recall(mls.focus, (uint32_t)clampi((int32_t)dbp_cur() + st, 0, (int32_t)dbp_count(mls.focus) - 1));
    say_preset();
    touched();
}
static void ctl_setup_store(uint32_t n)
{
    setup_store(n);
    ctl_say_int("SETUP STORED", (int32_t)n + 1, 0);
    touched();
}

/* ---- power-on and the saving */
static void ctl_init(void)
{
    org_par.expression = 127;
    org_par.leslie = LESLIE_SLOW;
    ctl.leslie_user = LESLIE_SLOW;
    ml_init();
    store_load_all();
    org_par.mute = 0;
    org_init();
}
static void ctl_save_tick(void)        /* save 2 s after the last change, when nothing sounds (an erase stalls the audio) */
{
    if (st_dirty && fm1_ms - ctl.t_change > 2000u && !org_stat.held && org_stat.peak < 64u)
        store_save_due();
}

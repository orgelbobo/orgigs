/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs: the parameter table, the same on every board (main loop). Every value a panel sets is a par_t: its tag
 * (a screen's cell), its name (the message line), its range, the step one detent of a knob moves it, its value at
 * the start and how it is shown. The boards step them through here and show them with par_fmt; a host build prints
 * them all (par_dump: tools/params_doc.py writes docs/PARAMETERS.md from it).
 *   1  the organ's own (OPAR, PR_...): org_par, the MIDI zones and the mixer; shared, per manual or per channel,
 *      where each is kept and the MIDI CC that sets it from outside. The plain ones step through opar_step; those
 *      with their own logic (OP_OWN: side effects, their own message) are stepped by core/orgigs_ctl.c or the board
 *   2  the insert effect (FXI_PAR, flash blob 2): one effect on one manual's bus (core/orgigs_fx.c, fxi_rt)
 *   3  the master (MSP, flash blob 3): EQ and compressor on the sum (core/orgigs_fx.c, mst_rt)
 * The insert and the master stay as they are when a setup is recalled; they are saved 2 s after the last change and
 * the settings backup carries them as its records F and M (core/orgigs_backup.c). */

/* how a value is shown: as it is, 1/100 Hz, %, 1/10 ms, +-%, the amp's model, its box, +-, + dB, n:1, a name from
   the list (the organ's own), the delay's 0..127 as ms, a MIDI channel (0..15 as 1..16, CH_OFF), a note, 1/10 s */
enum { PU_NUM, PU_HZ, PU_PCT, PU_MS, PU_SPCT, PU_AMP, PU_CAB, PU_SNUM, PU_DB, PU_RATIO, PU_LIST, PU_DLYMS, PU_CH, PU_NOTE,
       PU_DSEC };
static const char *const AMP_NAME[4] = {"CLEAN", "CRUNCH", "LEAD", "FUZZ"};
static const char *const CAB_NAME[4] = {"OFF", "1X12", "2X12", "4X12"};
typedef struct { const char *tag, *name; int16_t min, max, step, def; uint8_t unit; } par_t;

static char *par_fmt(char *p, const par_t *q, int32_t v)
{
    if (q->unit == PU_HZ && v == 0) return cat(p, "OFF");
    if (q->unit == PU_DLYMS) return org_fmt(p, 20 + v * 980 / 127, 0);
    if (q->unit == PU_CH) return v >= CH_OFF ? cat(p, "OFF") : org_fmt(p, v + 1, 0);
    if (q->unit == PU_NOTE) return note_name(p, (uint32_t)v);
    if (q->unit == PU_AMP) return cat(p, AMP_NAME[v & 3]);
    if (q->unit == PU_CAB) return cat(p, CAB_NAME[v & 3]);
    if (q->unit == PU_SNUM) return org_fmt(p, v, 1);
    if (q->unit == PU_DB) return cat(org_fmt(p, v, 1), "dB");
    if (q->unit == PU_RATIO) return cat(org_fmt(p, v, 0), ":1");
    if (q->unit == PU_HZ) { p = org_fmt(p, v / 100, 0); *p++ = '.'; *p++ = (char)('0' + v / 10 % 10); *p++ = (char)('0' + v % 10); *p = 0; return p; }
    if (q->unit == PU_MS || q->unit == PU_DSEC) {
        p = org_fmt(p, v / 10, 0); *p++ = '.'; *p++ = (char)('0' + v % 10); *p = 0;
        return q->unit == PU_DSEC ? cat(p, "s") : p;
    }
    if (q->unit == PU_PCT || q->unit == PU_SPCT) return cat(org_fmt(p, v, q->unit == PU_SPCT), "%");
    return org_fmt(p, v, 0);
}
static int32_t par_next(const par_t *q, int32_t v, int32_t st)   /* v moved st detents (step 0: ~6 % of v a detent) */
{
    return clampi(v + st * (q->step ? q->step : v / 16 > 1 ? v / 16 : 1), q->min, q->max);
}

/* ---- 1 the organ's own. per: one value, one per manual (the focused one), one per MIDI channel; keep: in the
   setups (and the panel as left), the device's own settings, not kept; cc: the MIDI CC that sets it (the drawbars
   12..20, the pedal's 12..15) */
enum { PER_ONE, PER_MAN, PER_CH };
enum { PK_SETUP, PK_DEVICE, PK_FX, PK_MASTER, PK_NONE };
#define OP_OWN 1u
typedef struct { par_t p; const char *const *list; uint16_t off; uint8_t per, keep, cc, flags; } opar_t;   /* list: PU_LIST's names */
enum { PR_MODEL, PR_DRAWBAR, PR_PERC, PR_CHAR, PR_COUPLER, PR_OCTAVE,
       PR_DRIVE, PR_VIB, PR_TREM, PR_ROTARY, PR_DSEND, PR_RSEND,
       PR_SPEED, PR_DLY_TIME, PR_DLY_FB, PR_REV, PR_EXPR, PR_BEND_MODE, PR_TUNING,
       PR_IN_CH, PR_OUT_CH, PR_LO, PR_HI, PR_TRANSP, PR_ZOCT, PR_VEL, PR_MIX, PR_N };
static const char *const OFF_ON[2] = {"OFF", "ON"};
static const char *const TUNING_NAME[2] = {"HISTORIC", "EQUAL"};   /* the models' own temperaments, or equal for all */
#define OPA(f) (uint16_t)__builtin_offsetof(org_params_t, f)
static const opar_t OPAR[PR_N] = {
    [PR_MODEL] = {{"MODEL", "MODEL", 1, ORG_NMODEL, 1, 1, PU_NUM}, 0, 0, PER_ONE, PK_SETUP, 0, OP_OWN},
    [PR_DRAWBAR] = {{"DB", "DRAWBARS 1-9", 0, 8, 1, 0, PU_NUM}, 0, 0, PER_MAN, PK_SETUP, 12, OP_OWN},
    [PR_PERC] = {{"PERC", "PERCUSSION", 0, ORG_NPERC_ALL - 1, 1, 0, PU_LIST}, PERC_NAME, 0, PER_ONE, PK_SETUP, 0, OP_OWN},
    [PR_CHAR] = {{"CHIFF", "CHIFF / MUSETTE DETUNE", 0, 127, 2, 0, PU_NUM}, 0, 0, PER_ONE, PK_SETUP, 0, OP_OWN},
    [PR_COUPLER] = {{"II/I", "COUPLER II/I", 0, 1, 1, 0, PU_LIST}, OFF_ON, 0, PER_ONE, PK_SETUP, 0, OP_OWN},
    [PR_OCTAVE] = {{"OCT", "OCTAVE", -2, 3, 1, 0, PU_SNUM}, 0, 0, PER_ONE, PK_SETUP, 0, OP_OWN},
    [PR_DRIVE] = {{"DRIVE", "DRIVE", 0, 127, 2, 0, PU_NUM}, 0, OPA(drive), PER_MAN, PK_SETUP, 0, 0},
    [PR_VIB] = {{"VIB", "VIBRATO", 0, ORG_NVIB - 1, 1, 0, PU_LIST}, VIB_NAME, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_TREM] = {{"TREM", "TREMULANT", 0, 127, 4, 0, PU_NUM}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_ROTARY] = {{"ROT", "ROTARY", 0, 1, 1, 0, PU_LIST}, OFF_ON, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_DSEND] = {{"DELAY", "DELAY SEND", 0, 127, 3, 0, PU_NUM}, 0, OPA(dsend), PER_MAN, PK_SETUP, 0, 0},
    [PR_RSEND] = {{"REVERB", "REVERB SEND", 0, 127, 3, 0, PU_NUM}, 0, OPA(rsend), PER_MAN, PK_SETUP, 0, 0},
    [PR_SPEED] = {{"SPEED", "ROTARY", 0, 2, 1, 0, PU_LIST}, LESLIE_NAME, 0, PER_ONE, PK_NONE, 1, OP_OWN},
    [PR_DLY_TIME] = {{"D-TIME", "DELAY MS", 0, 127, 2, 40, PU_DLYMS}, 0, OPA(delay_time), PER_ONE, PK_SETUP, 0, 0},
    [PR_DLY_FB] = {{"FEEDBK", "DELAY FEEDBACK", 0, 127, 3, 50, PU_NUM}, 0, OPA(delay_fb), PER_ONE, PK_SETUP, 0, 0},
    [PR_REV] = {{"SPACE", "REVERB", 0, ORG_NREV - 1, 1, 0, PU_LIST}, REV_NAME, OPA(rev_mode), PER_ONE, PK_SETUP, 0, 0},
    [PR_EXPR] = {{"EXPR", "EXPRESSION", 0, 127, 1, 127, PU_NUM}, 0, 0, PER_ONE, PK_NONE, 11, OP_OWN},
    [PR_BEND_MODE] = {{"BEND", "BEND + MOD", 0, CTL_N - 1, 1, 0, PU_LIST}, CTL_NAME, 0, PER_ONE, PK_DEVICE, 0, OP_OWN},
    [PR_TUNING] = {{"TUNING", "TUNING", 0, 1, 1, 0, PU_LIST}, TUNING_NAME, 0, PER_ONE, PK_DEVICE, 0, OP_OWN},
    [PR_IN_CH] = {{"IN CH", "MIDI IN CH", 0, CH_OFF, 1, 0, PU_CH}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_OUT_CH] = {{"OUT CH", "MIDI OUT CH", 0, CH_OFF, 1, 0, PU_CH}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_LO] = {{"LOWEST", "LOWEST KEY", 0, 127, 1, 0, PU_NOTE}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_HI] = {{"HIGHEST", "HIGHEST KEY", 0, 127, 1, 127, PU_NOTE}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_TRANSP] = {{"TRANSP", "OUT TRANSPOSE", -24, 24, 1, 0, PU_SNUM}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_ZOCT] = {{"OCTAVE", "OUT OCTAVE", -3, 3, 1, 0, PU_SNUM}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_VEL] = {{"VEL", "FIXED VELOCITY", 1, 127, 3, 100, PU_NUM}, 0, 0, PER_MAN, PK_SETUP, 0, OP_OWN},
    [PR_MIX] = {{"CC7", "MIXER CC7", 0, 127, 2, 100, PU_NUM}, 0, 0, PER_CH, PK_SETUP, 0, OP_OWN},
};
/* a plain one's byte in org_par (the focused manual's, if it has one each); out of line: as fxi_val below */
static __attribute__((noinline)) volatile uint8_t *opar_ref(uint32_t id)
{
    const opar_t *o = &OPAR[id];
    return (volatile uint8_t *)&org_par + o->off + (o->per == PER_MAN ? mls.focus : 0u);
}
static char *opar_fmt(char *p, const opar_t *o, int32_t v)
{
    return o->p.unit == PU_LIST ? cat(p, o->list[v < o->p.min ? o->p.min : v > o->p.max ? o->p.max : v]) : par_fmt(p, &o->p, v);
}
static void opar_step(uint32_t id, int32_t st)     /* a plain one, st detents: set, said, kept */
{
    const opar_t *o = &OPAR[id];
    volatile uint8_t *r = opar_ref(id);
    char t[24], s[12];
    *r = (uint8_t)par_next(&o->p, *r, st);
    opar_fmt(s, o, *r);
    if (o->per == PER_MAN) man_label(t, o->p.name); else cat(t, o->p.name);
    ctl_say(t, s);
    touched();
}
static void ctl_drive(int32_t st) { opar_step(PR_DRIVE, st); }
static void ctl_dsend(int32_t st) { opar_step(PR_DSEND, st); }
static void ctl_rsend(int32_t st) { opar_step(PR_RSEND, st); }
static void ctl_delay_time(int32_t st) { opar_step(PR_DLY_TIME, st); }
static void ctl_delay_fb(int32_t st) { opar_step(PR_DLY_FB, st); }
static void ctl_rev_mode(int32_t st) { opar_step(PR_REV, st); }

/* ---- 2 the insert effect: one effect on one manual, after its vibrato, before its rotary (core/orgigs_fx.c,
   fxi_rt). Each effect keeps its own eight values; all kept in flash (blob 2). The Elite: its EFFECT page (pads 1-7
   the effect, again: off; pads 9-11 the manual); the FM-1: FX, FX again. CHORUS, WAH (while WAH is chosen the mod
   wheel, on the FM-1 CC 1, is its pedal, not the rotary's), AUTO-WAH, FLANGER, PHASER, RING MOD and AMP */
static const char *const FXI_NAME[FXI_N] = {"OFF", "CHORUS", "WAH-WAH", "AUTO-WAH", "FLANGER", "PHASER", "RING MOD", "AMP"};
static const char *const FXI_TAG[FXI_N] = {"OFF", "CHO", "WAH", "AWA", "FLG", "PHS", "RNG", "AMP"};
static const uint8_t FXI_READY[FXI_N] = {1, 1, 1, 1, 1, 1, 1, 1};
#define FXI_NP 8u
#ifndef BOARD_FXI_PICK
#define BOARD_FXI_PICK "PADS 1-7: PICK ONE"     /* (the message when a knob turns with no effect chosen) */
#endif
static const par_t FXI_PAR[FXI_N][FXI_NP] = {
    [FXI_CHORUS] = {{"RATE", "CHORUS RATE HZ", 10, 500, 5, 80, PU_HZ}, {"DEPTH", "CHORUS DEPTH", 0, 100, 5, 50, PU_PCT},
                    {"DELAY", "CHORUS DELAY MS", 30, 250, 5, 120, PU_MS}, {"MIX", "CHORUS MIX", 0, 100, 5, 50, PU_PCT},
                    {"WIDTH", "CHORUS WIDTH", 0, 100, 5, 70, PU_PCT}, {"FEEDBK", "CHORUS FEEDBACK", 0, 90, 5, 0, PU_PCT}},
    [FXI_WAH] = {{"PEDAL", "WAH PEDAL", 0, 100, 2, 50, PU_PCT}, {"LOW", "WAH LOW HZ", 200, 800, 10, 350, PU_NUM},
                 {"HIGH", "WAH HIGH HZ", 1000, 4000, 50, 2200, PU_NUM}, {"RESO", "WAH RESONANCE", 1, 10, 1, 5, PU_NUM},
                 {"MIX", "WAH MIX", 0, 100, 5, 100, PU_PCT}, {"LEVEL", "WAH LEVEL", 30, 300, 10, 120, PU_PCT}},
    [FXI_AUTOWAH] = {{"SENS", "AUTO-WAH SENSITIVITY", 0, 100, 5, 60, PU_PCT}, {"RATE", "AUTO-WAH LFO HZ", 0, 500, 5, 0, PU_HZ},
                     {"LOW", "AUTO-WAH LOW HZ", 200, 800, 10, 300, PU_NUM}, {"HIGH", "AUTO-WAH HIGH HZ", 1000, 4000, 50, 2500, PU_NUM},
                     {"RESO", "AUTO-WAH RESONANCE", 1, 10, 1, 6, PU_NUM}, {"DECAY", "AUTO-WAH DECAY MS", 20, 2000, 20, 260, PU_NUM},
                     {"MIX", "AUTO-WAH MIX", 0, 100, 5, 100, PU_PCT}, {"LEVEL", "AUTO-WAH LEVEL", 30, 300, 10, 120, PU_PCT}},
    [FXI_FLANGER] = {{"RATE", "FLANGER RATE HZ", 5, 500, 5, 25, PU_HZ}, {"DEPTH", "FLANGER DEPTH", 0, 100, 5, 70, PU_PCT},
                     {"DELAY", "FLANGER DELAY MS", 5, 50, 1, 20, PU_MS}, {"FEEDBK", "FLANGER FEEDBACK", -90, 90, 5, 50, PU_SPCT},
                     {"MIX", "FLANGER MIX", 0, 100, 5, 100, PU_PCT}, {"WIDTH", "FLANGER WIDTH", 0, 100, 5, 60, PU_PCT}},
    [FXI_PHASER] = {{"RATE", "PHASER RATE HZ", 5, 500, 5, 40, PU_HZ}, {"LOW", "PHASER LOW HZ", 100, 1000, 10, 300, PU_NUM},
                    {"HIGH", "PHASER HIGH HZ", 1000, 6000, 100, 2500, PU_NUM}, {"STAGES", "PHASER STAGES", 2, 8, 2, 4, PU_NUM},
                    {"FEEDBK", "PHASER FEEDBACK", -90, 90, 5, 30, PU_SPCT}, {"MIX", "PHASER MIX", 0, 100, 5, 100, PU_PCT},
                    {"WIDTH", "PHASER WIDTH", 0, 100, 5, 50, PU_PCT}},
    [FXI_RING] = {{"FREQ", "RING FREQ HZ", 20, 2000, 0, 440, PU_NUM}, {"MIX", "RING MIX", 0, 100, 5, 50, PU_PCT},
                  {"RATE", "RING LFO HZ", 0, 1000, 5, 0, PU_HZ}, {"DEPTH", "RING LFO DEPTH", 0, 100, 5, 30, PU_PCT},
                  {"WIDTH", "RING WIDTH", 0, 100, 5, 50, PU_PCT}},
    [FXI_AMP] = {{"MODEL", "AMP MODEL", 0, 3, 1, 1, PU_AMP}, {"GAIN", "AMP GAIN", 0, 100, 2, 40, PU_PCT},
                 {"TONE", "AMP TONE", -10, 10, 1, 0, PU_SNUM}, {"CAB", "AMP CABINET", 0, 3, 1, 2, PU_CAB},
                 {"LEVEL", "AMP LEVEL", 10, 200, 5, 50, PU_PCT}, {"MIX", "AMP MIX", 0, 100, 5, 100, PU_PCT}},
};
#define FXI_VER 1u
static struct { uint8_t ver, type, man, seen; int16_t p[FXI_N][FXI_NP]; } fxi_st;   /* seen: the effects given their start */
static uint32_t fxi_dirty, fxi_at;
static uint32_t fxi_pedal;              /* WAH: the pedal now, 0..1000 (the mod wheel or K1; not kept) */
static struct { uint8_t on, type[2], k[2]; int16_t v[2]; } fxi_ovr;   /* a wheel's hold on a value (bit w: wheel w) */

static void fxi_apply(void)
{
    fxi_rt_t r;
    int16_t p[FXI_NP];
    uint32_t w;
    memcpy(p, fxi_st.p[fxi_st.type < FXI_N ? fxi_st.type : 0u], sizeof p);
    for (w = 0; w < 2u; w++)                    /* the wheels' values over the knobs' */
        if (fxi_ovr.on >> w & 1u && fxi_ovr.type[w] == fxi_st.type) p[fxi_ovr.k[w] & 7u] = fxi_ovr.v[w];
    memset(&r, 0, sizeof r);
    r.type = fxi_st.type < FXI_N && FXI_READY[fxi_st.type] ? fxi_st.type : FXI_OFF;
    r.man = fxi_st.man < ORG_NMAN ? fxi_st.man : MAN_UPPER;
    if (r.type == FXI_CHORUS) {
        r.rate_inc = ((uint32_t)p[0] * 31165u) >> 5;            /* 2^32 / 44100 per 1/100 Hz */
        r.base_q8 = p[2] * 1129;                                /* 4.41 samples per 1/10 ms, 1/256 */
        r.swing_q8 = p[1] * 450;                                /* 100 % = 4 ms */
        if (r.swing_q8 > r.base_q8 - 512) r.swing_q8 = r.base_q8 - 512;
        r.mix = p[3] * 128 / 100;
        r.width = p[4] * 128 / 100;
        r.fb = p[5] * 128 / 100;
    } else if (r.type == FXI_FLANGER) {     /* the delay swings DEPTH % of its middle around it */
        r.rate_inc = ((uint32_t)p[0] * 31165u) >> 5;
        r.base_q8 = p[2] * 1129;
        r.swing_q8 = r.base_q8 * p[1] / 100;
        if (r.swing_q8 > r.base_q8 - 256) r.swing_q8 = r.base_q8 - 256;
        r.fb = p[3] * 128 / 100;
        r.mix = p[4] * 128 / 100;
        r.width = p[5] * 128 / 100;
    } else if (r.type == FXI_PHASER) {
        r.rate_inc = ((uint32_t)p[0] * 31165u) >> 5;
        r.aw_lo = ilog2_q12((uint32_t)p[1]);
        r.aw_hi = ilog2_q12((uint32_t)p[2]);
        r.ph_stages = (uint32_t)p[3];
        r.fb = p[4] * 128 / 100;
        r.mix = p[5] * 128 / 100;
        r.width = p[6] * 128 / 100;
    } else if (r.type == FXI_RING) {        /* the LFO swings the carrier up to +-50 % */
        r.rm_inc = (uint32_t)p[0] * 97391u;                     /* 2^32 / 44100 per Hz */
        r.mix = p[1] * 128 / 100;
        r.rate_inc = ((uint32_t)p[2] * 31165u) >> 5;
        r.rm_swing = r.rm_inc / 200u * (uint32_t)p[3];
        r.width = p[4] * 128 / 100;
    } else if (r.type == FXI_AMP) {         /* the gain 1 .. 64 times, even in octaves */
        static const int16_t CAB_A[4] = {0, 16696, 14235, 11397};   /* the box's corner: 5, 4, 3 kHz */
        r.amp_model = (uint32_t)p[0] & 3u;
        int32_t oct = p[1] * 6 * 4096 / 100;                    /* the gain in octaves, Q12 */
        r.amp_gain = (int32_t)iexp2_q12(8 * 4096 + oct);
        r.amp_tone = p[2];
        r.amp_cab = CAB_A[p[3] & 3];
        r.level = (p[4] * 128 / 100) * (int32_t)iexp2_q12(12 * 4096 - oct * 35 / 100) >> 12;   /* gain^-0.35: the
                                                                                level about the same at any gain */
        r.mix = p[5] * 128 / 100;
    } else if (r.type == FXI_WAH) {         /* the pedal: from LOW to HIGH, even in octaves */
        int32_t lo = ilog2_q12((uint32_t)p[1]), hi = ilog2_q12((uint32_t)p[2]);
        uint32_t fc = iexp2_q12(lo + (hi - lo) * (int32_t)fxi_pedal / 1000);
        r.wah_f = wah_f_of(fc);
        r.wah_q = 4096 / p[3];
        r.mix = p[4] * 128 / 100;
        r.level = p[5] * 128 / 100;
    } else if (r.type == FXI_AUTOWAH) {     /* the level (and an LFO) moves it from LOW to HIGH */
        r.aw_sens = p[0];
        r.rate_inc = ((uint32_t)p[1] * 31165u) >> 5;
        r.aw_lo = ilog2_q12((uint32_t)p[2]);
        r.aw_hi = ilog2_q12((uint32_t)p[3]);
        r.wah_q = 4096 / p[4];
        r.aw_rel = 47552 / p[5];                                /* a block (0.73 ms) of the decay's time constant */
        r.mix = p[6] * 128 / 100;
        r.level = p[7] * 128 / 100;
    }
    fxi_rt = r;
}
static void fxi_check(uint32_t ok)     /* fxi_st as read (ok: read at all): checked, new effects their start; applied */
{
    uint32_t t, k;
    ok = ok && fxi_st.ver == FXI_VER;
    if (!ok) { memset(&fxi_st, 0, sizeof fxi_st); fxi_st.ver = FXI_VER; }
    else if (!fxi_st.seen) fxi_st.seen = 1u << FXI_CHORUS;     /* (a store of the CHORUS-only firmware) */
    if (fxi_st.type >= FXI_N) fxi_st.type = FXI_OFF;
    if (fxi_st.man >= ORG_NMAN) fxi_st.man = MAN_UPPER;
    for (t = 1; t < FXI_N; t++) {               /* an effect new here: its start; a value out of range: its start */
        uint32_t fresh = !(fxi_st.seen >> t & 1u) && FXI_PAR[t][0].tag;
        for (k = 0; k < FXI_NP; k++) {
            const par_t *q = &FXI_PAR[t][k];
            if (fresh || fxi_st.p[t][k] < q->min || fxi_st.p[t][k] > q->max) fxi_st.p[t][k] = q->tag ? q->def : 0;
        }
        if (FXI_PAR[t][0].tag) fxi_st.seen |= (uint8_t)(1u << t);
    }
    fxi_pedal = (uint32_t)fxi_st.p[FXI_WAH][0] * 10u;
    fxi_apply();
}
static void fxi_init(void) { fxi_check(blob_load(2, &fxi_st, sizeof fxi_st) == 0); }
static void fxi_init_loaded(void) { fxi_check(1); }
static void fxi_touch(void) { fxi_dirty = 1; fxi_at = fm1_ms; }
static void fxi_wheel(uint32_t mv)      /* WAH: the mod wheel (the FM-1: CC 1) is the pedal (0..127) */
{
    fxi_pedal = mv * 1000u / 127u;
    fxi_apply();
    ctl_say_int("WAH PEDAL %", (int32_t)(fxi_pedal / 10u), 0);
}
static void fxi_pick(uint32_t t)        /* the effect; the one chosen again: off */
{
    if (t >= FXI_N) return;
    if (!FXI_READY[t]) { ctl_say(FXI_NAME[t], "NOT YET"); return; }
    fxi_st.type = (uint8_t)(t == fxi_st.type ? FXI_OFF : t);
    if (fxi_st.type == FXI_WAH) fxi_pedal = (uint32_t)fxi_st.p[FXI_WAH][0] * 10u;
    fxi_apply();
    fxi_touch();
    ctl_say("EFFECT", FXI_NAME[fxi_st.type]);
}
static void fxi_man(uint32_t m)
{
    char t[24];
    fxi_st.man = (uint8_t)m;
    fxi_apply();
    fxi_touch();
    cat(cat(t, FXI_NAME[fxi_st.type]), " ON");
    ctl_say(t, MAN_TAG[m]);
}
/* an effect's value, kept out of line: inlined, clang loads it with an immediate pre-increment (h[++rX=n] (s)) that
   the FM-1 emulator rejects */
static __attribute__((noinline)) int16_t *fxi_val(uint32_t t, uint32_t k) { return &fxi_st.p[t][k & 7u]; }
static void fxi_step(uint32_t k, int32_t st)
{
    const par_t *q;
    int16_t *v;
    char s[12];
    if (fxi_st.type == FXI_OFF) { ctl_say("EFFECT OFF", BOARD_FXI_PICK); return; }
    q = &FXI_PAR[fxi_st.type][k & 7u];
    if (!q->tag) return;
    v = fxi_val(fxi_st.type, k);
    *v = (int16_t)par_next(q, *v, st);
    if (fxi_st.type == FXI_WAH && k == 0u) fxi_pedal = (uint32_t)*v * 10u;   /* K1: the pedal set */
    fxi_apply();
    fxi_touch();
    par_fmt(s, q, *v);
    ctl_say(q->name, s);
}

/* ---- 3 the master: EQ and compressor on the sum (core/orgigs_fx.c, mst_rt), each on / off, eleven values; all kept
   in flash (blob 3). The Elite: its MASTER page (pad 1 the EQ, pad 2 the compressor); the FM-1: FX three times */
enum { MSP_LO, MSP_MID, MSP_MFREQ, MSP_HI, MSP_THR, MSP_RATIO, MSP_ATT, MSP_REL, MSP_LFREQ, MSP_HFREQ, MSP_MAKEUP, MSP_N };
static const par_t MSP[MSP_N] = {
    {"LOW", "EQ LOW", -12, 12, 1, 0, PU_DB}, {"MID", "EQ MID", -12, 12, 1, 0, PU_DB},
    {"M FREQ", "EQ MID HZ", 200, 5000, 0, 1000, PU_NUM}, {"HIGH", "EQ HIGH", -12, 12, 1, 0, PU_DB},
    {"THRESH", "COMP THRESHOLD", -40, 0, 1, -18, PU_DB}, {"RATIO", "COMP RATIO", 1, 20, 1, 4, PU_RATIO},
    {"ATTACK", "COMP ATTACK MS", 1, 100, 1, 10, PU_NUM}, {"RELEASE", "COMP RELEASE MS", 20, 1000, 0, 200, PU_NUM},
    {"L FREQ", "EQ LOW HZ", 50, 400, 0, 120, PU_NUM}, {"H FREQ", "EQ HIGH HZ", 2000, 12000, 0, 6000, PU_NUM},
    {"MAKEUP", "COMP MAKEUP", 0, 12, 1, 0, PU_DB},
};
#define MST_VER 1u
static struct { uint8_t ver, eq_on, comp_on, spare; int16_t p[MSP_N]; } mst_st;
static uint32_t mst_dirty, mst_at;

static int32_t onepole_q15(int32_t f)   /* 1 - e^(-2 pi f / 44100), Q15 (a series: good to ~5 kHz) */
{
    int32_t x = (f * 9562) >> 11, x2 = (x * x) >> 15;
    return x - x2 / 2 + ((x2 * x) >> 15) / 6;
}
static int32_t db_q12(int32_t db) { return (int32_t)iexp2_q12(12 * 4096 + db * 4096 * 100 / 602); }   /* 10^(dB/20) */
static void mst_apply(void)
{
    mst_rt_t r;
    const int16_t *p = mst_st.p;
    int32_t ms;
    memset(&r, 0, sizeof r);
    r.eq_on = mst_st.eq_on;
    r.comp_on = mst_st.comp_on;
    r.g_lo = db_q12(p[MSP_LO]); r.g_mid = db_q12(p[MSP_MID]); r.g_hi = db_q12(p[MSP_HI]);
    r.a_lo = onepole_q15(p[MSP_LFREQ]);
    r.a_hi = p[MSP_HFREQ] > 5000 ? 16696 + (p[MSP_HFREQ] - 5000) * 2 : onepole_q15(p[MSP_HFREQ]);   /* (above 5 kHz: near) */
    r.f_mid = wah_f_of((uint32_t)p[MSP_MFREQ]);
    r.thr = 15 * 4096 + p[MSP_THR] * 4096 * 100 / 602;              /* 0 dB = 32768 */
    r.slope = 4096 - 4096 / p[MSP_RATIO];
    ms = p[MSP_ATT];
    r.att = 47552 / ms > 65535 ? 65535 : 47552 / ms;                /* a block (0.73 ms) of the time */
    r.rel = 47552 / p[MSP_REL];
    r.makeup = db_q12(p[MSP_MAKEUP]);
    mst_rt = r;
}
static void mst_check(uint32_t ok)     /* mst_st as read (ok: read at all): checked, else the start; applied */
{
    uint32_t i;
    ok = ok && mst_st.ver == MST_VER;
    if (!ok) { memset(&mst_st, 0, sizeof mst_st); mst_st.ver = MST_VER; }
    for (i = 0; i < MSP_N; i++) if (!ok || mst_st.p[i] < MSP[i].min || mst_st.p[i] > MSP[i].max) mst_st.p[i] = MSP[i].def;
    mst_st.eq_on &= 1u; mst_st.comp_on &= 1u;
    mst_apply();
}
static void mst_init(void) { mst_check(blob_load(3, &mst_st, sizeof mst_st) == 0); }
static void mst_init_loaded(void) { mst_check(1); }
static void mst_touch(void) { mst_dirty = 1; mst_at = fm1_ms; }
static void mst_toggle(uint32_t comp)   /* 0: the EQ, 1: the compressor, on / off */
{
    uint8_t *o = comp ? &mst_st.comp_on : &mst_st.eq_on;
    *o ^= 1u;
    mst_apply();
    mst_touch();
    ctl_say(comp ? "COMPRESSOR" : "EQ", *o ? "ON" : "OFF");
}
static __attribute__((noinline)) int16_t *mst_val(uint32_t i) { return &mst_st.p[i]; }   /* (out of line: as fxi_val) */
static void mst_step(uint32_t i, int32_t st)   /* value i (MSP_...), st detents */
{
    int16_t *v;
    char s[12];
    if (i >= MSP_N) return;
    v = mst_val(i);
    *v = (int16_t)par_next(&MSP[i], *v, st);
    mst_apply();
    mst_touch();
    par_fmt(s, &MSP[i], *v);
    ctl_say(MSP[i].name, s);
}

/* ---- the insert and the master at power-on, their saving (2 s after the last change), the backup's records F and M */
static void par_init(void) { fxi_init(); mst_init(); }
static void par_save_tick(void)
{
    if (fxi_dirty && fm1_ms - fxi_at > 2000u) { fxi_dirty = 0; (void)blob_save(2, &fxi_st, sizeof fxi_st); }
    if (mst_dirty && fm1_ms - mst_at > 2000u) { mst_dirty = 0; (void)blob_save(3, &mst_st, sizeof mst_st); }
}
#define PAR_BK_MAX (3u + sizeof fxi_st + 3u + sizeof mst_st)
static uint32_t bk_rec(uint8_t *b, uint32_t room, uint8_t tag, const void *p, uint32_t n)   /* a tag, its length (LE 16), the bytes */
{
    if (room < n + 3u) return 0;
    b[0] = tag; b[1] = (uint8_t)n; b[2] = (uint8_t)(n >> 8);
    memcpy(b + 3, p, n);
    return n + 3u;
}
static uint32_t par_bk_put(uint8_t *b, uint32_t room)
{
    uint32_t o = bk_rec(b, room, 'F', &fxi_st, sizeof fxi_st);
    return o + bk_rec(b + o, room - o, 'M', &mst_st, sizeof mst_st);
}
static void par_bk_get(const uint8_t *b, uint32_t n)   /* the records of a backup loaded: F and M (the rest: the board's) */
{
    uint32_t o = 0;
    while (o + 3u <= n) {
        uint32_t len = b[o + 1] | (uint32_t)b[o + 2] << 8;
        const uint8_t *d = b + o + 3;
        if (o + 3u + len > n) break;
        if (b[o] == 'F' && len == sizeof fxi_st && d[0] == FXI_VER) {
            memcpy(&fxi_st, d, len); fxi_init_loaded(); fxi_dirty = 1; fxi_at = fm1_ms - 2000u;
        } else if (b[o] == 'M' && len == sizeof mst_st && d[0] == MST_VER) {
            memcpy(&mst_st, d, len); mst_init_loaded(); mst_dirty = 1; mst_at = fm1_ms - 2000u;
        }
        o += 3u + len;
    }
}

#ifdef DINGHY_HOST
/* ---- the table printed (tests/host.c --params; tools/params_doc.py): one line per value, fields split by '|':
   group, tag, name, min, max, step, start, unit, per, kept, cc. The units as the screen shows them */
static void par_dump_one(const char *grp, const par_t *q, const char *const *list, const char *per, const char *keep, uint32_t cc)
{
    opar_t o;
    static const char *const U[] = {"", "Hz", "%", "ms", "+-%", "model", "box", "+-", "dB", ":1", "list", "ms", "channel", "note", "s"};
    char lo[24], hi[24], df[24];
    if (!q->tag) return;
    o.p = *q; o.list = list;
    opar_fmt(lo, &o, q->min); opar_fmt(hi, &o, q->max); opar_fmt(df, &o, q->def);
    printf("%s|%s|%s|%s|%s|%d|%s|%s|%s|%s|%u\n", grp, q->tag, q->name, lo, hi, q->step, df, U[q->unit], per, keep, cc);
}
#ifdef BOARD_PAR_DUMP
static void BOARD_PAR_DUMP(void);
#endif
static void par_dump(void)
{
    static const char *const PER[] = {"one", "per manual", "per channel"};
    static const char *const KEEP[] = {"setup", "device", "blob 2", "blob 3", "-"};
    uint32_t i, t;
    for (i = 0; i < PR_N; i++)
        par_dump_one(i >= PR_IN_CH ? "MIDI" : "ORGAN", &OPAR[i].p, OPAR[i].list, PER[OPAR[i].per], KEEP[OPAR[i].keep], OPAR[i].cc);
    for (t = 1; t < FXI_N; t++) {
        char g[24];
        cat(cat(g, "INSERT "), FXI_NAME[t]);
        for (i = 0; i < FXI_NP; i++) par_dump_one(g, &FXI_PAR[t][i], 0, "one", KEEP[PK_FX], 0);
    }
    for (i = 0; i < MSP_N; i++) par_dump_one("MASTER", &MSP[i], 0, "one", KEEP[PK_MASTER], 0);
#ifdef BOARD_PAR_DUMP
    BOARD_PAR_DUMP();                   /* the board's own (the Elite: the rotary cabinets) */
#endif
}
#endif

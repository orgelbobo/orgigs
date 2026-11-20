/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs on the SMK-37 (Elite): the panel and the screen (main loop), the Elite's own copy since 2026-11-07.
 * It began as board/fm1/orgigs_ui.c with the SMK-37's parts kept (the FM-1's copy grows little; the Elite's grows
 * into its own control concept, see docs/WISHLIST.md round 5). board.c includes it and adds the SMK-37's own
 * controls on top. The notes below still describe the FM-1's panel under the SMK's button names (platform.c).
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
 *   FX            the effects screen (again: back)       GLO   the MIDI zones (again: the next screen)
 *   HOME          on the drawbars: the MIDI mixer (again: back); elsewhere back to the drawbars
 * The drawbars (PLAY): ENV drawbars 1-4, LFO 5-8, EDIT 1 3 4 + 9 on KNOB1..4; ARP vibrato (tremulant) of the
 * manual on/off, SEQ coupler II/I.
 * The effects (FX): a row per manual (UPPER, LOWER, PEDAL = the focused manual: ENV LFO ARP or SELECT), KNOB1..4
 * drive, vibrato / tremulant, delay send, reverb send; SEQ the manual through the rotary. SEL + knobs: the
 * shared ones (percussion / chiff / detune, delay feedback and time, the reverb's room).
 * The MIDI mixer: 16 channels in 4 rows; ENV LFO ARP SEQ choose the row, KNOB1..4 send CC 7.
 * The MIDI zones: ENV LFO EDIT ARP SEQ are the screen's switches (SEL + them: the second ones).
 * The lights: KNOB1 the keys, KNOB2 the buttons (OFF, DIM: all glow, FULL: all lit and what matters blinks),
 * KNOB3 how bright DIM glows; SEQ sends a backup of all settings over USB-MIDI (core/orgigs_backup.c).
 * A backup coming in asks first: SAVE loads it, any other button keeps what is there. */

enum { MODE_PLAY, MODE_FX, MODE_MIDI, MODE_MIX, MODE_LIGHT };
enum { GRP_DB14, GRP_DB58, GRP_DB9 };
enum { MS_ZONE, MS_CLONE, MS_YST, MS_HARM, MS_CHORD, MS_N };   /* (the Elite: YST right after the cloner) */

static const char *const GRP_BADGE[3] = {"DB1-4", "DB5-8", "DB9"};
static const uint8_t GRP_DB[3][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 2, 3, 8}};
static const char *const MS_NAME[MS_N] = {"ZONE", "CLONE", "YST", "HARMONIC", "CHORD"};
static const char *const LT_NAME[LT_N] = {"OFF", "DIM", "FULL"};

static struct {
    uint32_t mode, grp, ms;
    uint32_t shift, save_used;
    uint32_t clone_cur;                 /* CLONE: the channel KNOB2 sets the octave of */
    uint32_t fxp;                       /* FX: the page (FXP_FX, FXP_LESLIE) */
} ui;
enum { FXP_FX, FXP_LESLIE, FXP_EFFECT, FXP_MASTER, FXP_WHEELS, FXP_N };

/* ---- the rotary cabinets: CLASSIC and COMPACT fixed, USER 1 and USER 2 edited on the ROTARY page (FX, then PATCH) and
   kept in flash (blob 1: there is no way back to their first values); the cabinet chosen is kept too. A cabinet is
   twelve values; the audio reads them worked out (les_rt, core/orgigs_fx.c); the CLASSIC is the audio's own, exactly */
enum { LESP_HSLOW, LESP_HFAST, LESP_HRISE, LESP_DSLOW, LESP_DFAST, LESP_DRISE, LESP_XOVER, LESP_BAL,
       LESP_HDOP, LESP_DDOP, LESP_AM, LESP_WIDTH, LESP_N };
enum { LES_T122, LES_T145, LES_U1, LES_U2, LES_NT };
static const char *const LES_TNAME[LES_NT] = {"CLASSIC", "COMPACT", "USER 1", "USER 2"};
/* the cabinet's values (core/orgigs_params.c's par_t; the start: the CLASSIC's): speeds in 1/100 Hz, rise (to ~95 %)
   in 1/10 s, crossover in Hz, balance -10 (drum) .. 10 (horn), Doppler swing in samples, level swing and width in %
   of the CLASSIC */
static const par_t LESP[LESP_N] = {
    {"H SLOW", "HORN SLOW HZ", 30, 150, 2, 80, PU_HZ}, {"H FAST", "HORN FAST HZ", 300, 900, 10, 680, PU_HZ},
    {"H RISE", "HORN RISE S", 2, 40, 1, 8, PU_DSEC}, {"D SLOW", "DRUM SLOW HZ", 20, 150, 2, 66, PU_HZ},
    {"D FAST", "DRUM FAST HZ", 300, 800, 10, 590, PU_HZ}, {"D RISE", "DRUM RISE S", 5, 100, 2, 30, PU_DSEC},
    {"XOVER", "CROSSOVER HZ", 200, 2000, 20, 800, PU_NUM}, {"BALANCE", "BALANCE HORN", -10, 10, 1, 0, PU_SNUM},
    {"H DOPP", "HORN DOPPLER", 0, 18, 1, 14, PU_NUM}, {"D DOPP", "DRUM DOPPLER", 0, 7, 1, 6, PU_NUM},
    {"AM", "LEVEL SWING %", 0, 200, 5, 100, PU_PCT}, {"WIDTH", "STEREO WIDTH %", 0, 150, 5, 100, PU_PCT},
};
static const int16_t LES_DEF[LES_NT][LESP_N] = {
    {80, 680, 8, 66, 590, 30, 800, 0, 14, 6, 100, 100},       /* CLASSIC: the audio's (core/orgigs_fx.c) */
    {86, 700, 6, 76, 620, 22, 900, 0, 10, 4, 85, 80},         /* COMPACT: smaller, closer, quicker */
    {60, 720, 14, 50, 610, 50, 800, 1, 16, 7, 115, 130},      /* USER 1 to start: gospel, slow dramatic rise, wide */
    {90, 660, 5, 70, 600, 20, 300, 8, 16, 6, 130, 60},        /* USER 2 to start: vibratone, nearly all horn, narrow */
};
#define LES_VER 1u
static struct { uint8_t ver, type, spare[2]; int16_t user[2][LESP_N]; } les_st;
static les_rt_t les_122;                /* the audio's own at the start */
static uint32_t les_dirty, les_at;

static const int16_t *les_par(uint32_t t) { return t >= LES_U1 && t < LES_NT ? les_st.user[t - LES_U1] : LES_DEF[t < LES_NT ? t : 0u]; }
static void les_derive(const int16_t *p, les_rt_t *r)
{
    int32_t x = (p[LESP_XOVER] * 9562) >> 11, x2 = (x * x) >> 15;     /* 2 pi f / 44100 in Q15; 1 - e^-x */
    uint32_t i;
    static const uint8_t SP[4] = {LESP_HSLOW, LESP_HFAST, LESP_DSLOW, LESP_DFAST};
    for (i = 0; i < 4u; i++) r->inc[i] = ((uint32_t)p[SP[i]] * 31165u) >> 5;   /* 2^32 / 44100 per 1/100 Hz */
    r->h_glide = 1427 / p[LESP_HRISE];  /* blocks of 32: three time constants */
    r->d_glide = 1427 / p[LESP_DRISE];
    r->xo_a = x - x2 / 2 + ((x2 * x) >> 15) / 6;
    r->h_gain = 16384 + p[LESP_BAL] * 1638;
    r->d_gain = 16384 - p[LESP_BAL] * 1638;
    r->h_dop = p[LESP_HDOP];
    r->d_dop = p[LESP_DDOP];
    r->h_am = 8192 * p[LESP_AM] / 100;
    r->d_am = 11264 * p[LESP_AM] / 100;
    r->width = p[LESP_WIDTH] * 64 / 100;
}
static void les_apply(void)
{
    les_rt_t r;
    if (les_st.type == LES_T122) r = les_122; else les_derive(les_par(les_st.type), &r);
    les_rt = r;
}
static void les_check(uint32_t ok)     /* les_st as read (ok: read at all): checked, else the start; applied */
{
    uint32_t t, i;
    ok = ok && les_st.ver == LES_VER && les_st.type < LES_NT;
    for (t = 0; ok && t < 2u; t++)
        for (i = 0; i < LESP_N; i++) ok &= les_st.user[t][i] >= LESP[i].min && les_st.user[t][i] <= LESP[i].max;
    if (!ok) {
        memset(&les_st, 0, sizeof les_st);
        les_st.ver = LES_VER;
        memcpy(les_st.user, LES_DEF[LES_U1], sizeof les_st.user);
    }
    les_apply();
}
static void les_init(void) { les_122 = les_rt; les_check(blob_load(1, &les_st, sizeof les_st) == 0); }
static void les_init_loaded(void) { les_check(1); }
static void les_touch(void) { les_dirty = 1; les_at = fm1_ms; }
static char *les_fmt(char *p, uint32_t i, int32_t v) { return par_fmt(p, &LESP[i], v); }
#ifdef DINGHY_HOST
static void smk_par_dump(void)          /* (core/orgigs_params.c par_dump: the board's own) */
{
    uint32_t i;
    for (i = 0; i < LESP_N; i++) par_dump_one("ROTARY CABINET (USER 1, 2)", &LESP[i], 0, "one", "blob 1", 0);
}
#endif
static void les_pick(uint32_t t)
{
    les_st.type = (uint8_t)t;
    les_apply();
    les_touch();
    ctl_say("ROTARY", LES_TNAME[t]);
}
static void les_copy(uint32_t slot)     /* the cabinet shown into USER 1 / 2 */
{
    memcpy(les_st.user[slot], les_par(les_st.type), sizeof les_st.user[slot]);
    les_pick(LES_U1 + slot);
    ctl_say("ROTARY COPIED TO", LES_TNAME[LES_U1 + slot]);
}
static void les_step(uint32_t k, int32_t st)   /* K1..K8; SCALE + K1..K4 the four more */
{
    uint32_t i = ui.shift ? (k < 4u ? LESP_HDOP + k : LESP_N) : k;
    int16_t *v;
    char s[12];
    if (i >= LESP_N) return;
    if (les_st.type < LES_U1) { ctl_say(LES_TNAME[les_st.type], "FIXED: BT + PAD 3/4 COPIES"); return; }
    v = &les_st.user[les_st.type - LES_U1][i];
    *v = (int16_t)par_next(&LESP[i], *v, st);
    les_apply();
    les_touch();
    les_fmt(s, i, *v);
    ctl_say(LESP[i].name, s);
}
/* (the insert effect and the master: core/orgigs_params.c, its EFFECT and MASTER pages below) */

/* ---- the coupler (the SMK-37's, in place of the organ's II/I): which manuals the keys play together, in turn
   OFF, UM+LM, UM+PM, LM+PM, UM+LM+PM (the zones' ZX_CPL, kept with the setup). A coupled pedal takes the lowest note.
   The II/I of a setup (the factory ones, the FM-1's) becomes UM+LM */
#define CPL_N 5u
static const uint8_t CPL_SET[CPL_N] = {0, 3, 5, 6, 7};
static const char *const CPL_NAME[CPL_N] = {"OFF", "UM+LM", "UM+PM", "LM+PM", "UM+LM+PM"};
static const char *const CPL_TAG[CPL_N] = {"OFF", "U+L", "U+P", "L+P", "ULP"};
static uint32_t cpl_idx(void)
{
    uint32_t i, c = ml_cpl();
    for (i = 1; i < CPL_N; i++) if (CPL_SET[i] == c) return i;
    return 0;
}
static void cpl_set(uint32_t i)
{
    uint32_t zi;
    ml_release_all();
    for (zi = 0; zi < ORG_NMAN; zi++)
        if (CPL_SET[i] >> zi & 1u) zn.z[zi].xf |= ZX_CPL; else zn.z[zi].xf &= (uint8_t)~ZX_CPL;
}
static uint8_t cpl_mask = 3;           /* the manuals pad 12 (layout C) couples when switched on (not kept) */
static uint32_t cpl_raw(void)           /* the manuals marked for the coupler (one alone: none coupled) */
{
    uint32_t zi, c = 0;
    for (zi = 0; zi < ORG_NMAN; zi++) if (zn.z[zi].xf & ZX_CPL) c |= 1u << zi;
    return c;
}
static void cpl_apply(uint32_t c)
{
    uint32_t zi;
    ml_release_all();
    for (zi = 0; zi < ORG_NMAN; zi++)
        if (c >> zi & 1u) zn.z[zi].xf |= ZX_CPL; else zn.z[zi].xf &= (uint8_t)~ZX_CPL;
}
static void cpl_say(uint32_t c, uint32_t on)
{
    static const char *const MN[ORG_NMAN] = {"UM", "LM", "PM"};
    char t[16];
    uint32_t zi, n = 0, k = 0;
    for (zi = 0; zi < ORG_NMAN; zi++)
        if (c >> zi & 1u) {
            if (n++) t[k++] = '+';
            t[k++] = MN[zi][0]; t[k++] = MN[zi][1];
        }
    t[k] = 0;
    ctl_say("COUPLER", !on ? "OFF" : n == 0u ? "NONE" : n == 1u ? (c & 1u ? "UM ONLY" : c & 2u ? "LM ONLY" : "PM ONLY") : t);
}
static void cpl_step(void)
{
    uint32_t i = (cpl_idx() + 1u) % CPL_N;
    cpl_set(i);
    if (i) cpl_mask = CPL_SET[i];
    ctl_say("COUPLER", CPL_NAME[i]);
    touched();
}
static void cpl_toggle(void)            /* pad 12: off / on with the manuals marked */
{
    uint32_t c = cpl_raw();
    if (c) cpl_mask = (uint8_t)c;
    cpl_apply(c ? 0u : cpl_mask);
    cpl_say(cpl_mask, !c);
    touched();
}
static void cpl_man(uint32_t m)         /* pad 12 held + pad 9-11: that manual in / out of the coupler */
{
    uint32_t c = cpl_raw();
    if (c) cpl_mask = (uint8_t)c;
    cpl_mask ^= (uint8_t)(1u << m);
    if (c) cpl_apply(cpl_mask);
    cpl_say(cpl_mask, 1);
    touched();
}
static void cpl_tick(void)              /* the organ's II/I (a setup recalled): the coupler UM+LM */
{
    if (!org_par.coupler) return;
    org_par.coupler = 0;
    cpl_set(1);
}

/* ---- the panel's layouts (board.c): PERFORMANCE A FULL DRAWBARS (the pads: drawbars to 0 / to 8), B DRAWBARS + PADS
   (bottom pads: a drawbar in / out, top pads: live switches), C KNOBS + 16 PADS (top pads: B's switches, bottom pads:
   the manual, the coupler, the zone's MIDI switches), D SOUND DESIGN (top pads: B's switches, bottom pads: the
   pages FX ROTARY EFFECT MASTER MIXER for the knobs, and the manual). In blob 4: the layout in use and the percussion's SOFT,
   what each setup keeps of them (a setup stored before: A, normal), and the favourites C had once (per manual; no
   longer used, kept: the blob's layout stays) */
enum { LAY_FULL, LAY_PADS, LAY_KNOBS, LAY_SOUND, LAY_PADS2, LAY_N };   /* (as stored; B2 came last) */
static const char *const LAY_NAME[LAY_N] = {"A FULL DRAWBARS", "B1 DRAWBARS + LIVE", "C KNOBS + 16 PADS", "D SOUND DESIGN",
                                            "B2 DRAWBARS + MIDI"};
static const char *const LAY_TAG[LAY_N] = {"PERF A", "PERF B1", "PERF C", "PERF D", "PERF B2"};
static const uint8_t LAY_ORDER[LAY_N] = {LAY_FULL, LAY_PADS, LAY_PADS2, LAY_KNOBS, LAY_SOUND};   /* the round: A B1 B2 C D */
/* the pedal socket (board.c smk_pedal_tick): AUTO tells a sustain pedal (a switch: the ends) from an expression pedal
   (a pot: the socket feeds its ring, the travel in between) and makes the latter the swell */
enum { PJ_AUTO, PJ_SUS, PJ_SWELL, PJ_N };
static const char *const PJ_NAME[PJ_N] = {"AUTO", "SUSTAIN", "SWELL"};
static struct { uint8_t expr, swell; uint16_t lo, hi, acc; uint32_t mid_at; } ped = {0, 0xFFu, 0xFFFFu, 0, 0, 0};
static void smk_pedal_mode(void);       /* (board.c) K4 on the WHEELS page changed it */
#define PNL_VER 5u
#define PNL_NFAV 4u
typedef struct { uint8_t ver, lay, soft, spare, set_lay[NSETUP], set_soft[NSETUP]; } pnl_v1_t;
static struct {
    uint8_t ver, lay, soft, spare, set_lay[NSETUP], set_soft[NSETUP];   /* (as version 1) */
    uint8_t fav_ok[ORG_NMAN];                                          /* bit n: favourite n stored */
    uint8_t fav[ORG_NMAN][PNL_NFAV][ORG_NDB];
    uint8_t pdk[2];                     /* (version 3; no longer used: the decays are not kept) */
    uint8_t whl[2], set_whl[NSETUP][2]; /* (version 4) the wheels' targets (WT_...): now, each setup's */
    uint8_t ped;                        /* (version 5) the pedal socket: PJ_AUTO, PJ_SUS, PJ_SWELL */
} pnl_st;
static uint32_t ped_swell(void) { return pnl_st.ped == PJ_SWELL || (pnl_st.ped == PJ_AUTO && ped.expr); }
#define PNL_V4_SIZE (sizeof pnl_st - 1u)
#define PNL_V3_SIZE (PNL_V4_SIZE - 2u - 2u * NSETUP)
#define PNL_V2_SIZE (PNL_V3_SIZE - 2u)
/* the YST action pad's CC (pad 12; K3 here; in blob 4's spare byte): the master effects' 102..106 on channel 1, the
   single effect's 107..109 on the track's */
#define VB_N 8u
#define VB_TRK 5u
static const uint8_t VB_CC[VB_N] = {102, 103, 104, 105, 106, 107, 108, 109};
static const char *const VB_NAME[VB_N] = {"MST1 P1", "MST1 P2", "MST1 P3", "MST2 P1", "MST3 P1", "TRK P1", "TRK P2", "TRK P3"};
static uint32_t vb_idx(void) { return pnl_st.spare < VB_N ? pnl_st.spare : 0u; }
/* the percussion's decay (pad 3 held + K3), each of the six its own, until power-off (not kept): the time to -60 dB in
   ms, from 100 ms to 12 s, the last one holds (0); the first 40 steps fine (100 ms .. 1.5 s), then coarser. Q: the
   factor per control tick (32 / 44.1 kHz). On the classic step (200 ms fast, 989 ms slow) the DSP's own, exact */
#define PERC_DK_N 64u
#define PERC_DK_FAST 10u                /* 200 ms, the classic fast */
#define PERC_DK_SLOW 33u                /* 989 ms, the classic slow (~1 s) */
static const uint16_t PERC_DK_MS[PERC_DK_N] = {100, 107, 115, 123, 132, 142, 152, 163, 174, 187, 200, 215, 230, 247, 264, 283, 304, 326, 349, 374, 401, 430, 461, 494, 529, 567, 608, 652, 699, 749, 803, 861, 923, 989, 1060, 1136, 1218, 1306, 1399, 1500, 1642, 1797, 1967, 2154, 2357, 2580, 2825, 3092, 3384, 3705, 4055, 4439, 4859, 5319, 5822, 6373, 6976, 7636, 8358, 9149, 10015, 10963, 12000, 0};
static const uint16_t PERC_DK_Q[PERC_DK_N] = {31166, 31271, 31369, 31461, 31547, 31628, 31703, 31773, 31839, 31900, 31958, 32012, 32062, 32109, 32153, 32193, 32232, 32267, 32301, 32332, 32361, 32388, 32413, 32437, 32459, 32480, 32499, 32517, 32534, 32549, 32564, 32578, 32590, 32602, 32613, 32624, 32633, 32642, 32651, 32659, 32668, 32677, 32685, 32692, 32698, 32704, 32710, 32715, 32720, 32724, 32728, 32731, 32734, 32737, 32740, 32742, 32744, 32746, 32748, 32750, 32752, 32753, 32754, 32768};static uint8_t perc_dk[ORG_NPERC_ALL];  /* per PERC_...: the step + 1, 0 the classic */static uint8_t pnl_dirty;
static uint32_t pnl_at;
static void pnl_touch(void) { pnl_dirty = 1; pnl_at = fm1_ms; }
static void pnl_init_loaded(void)
{
    uint32_t i;
    pnl_st.ver = PNL_VER;
    if (pnl_st.lay >= LAY_N) pnl_st.lay = LAY_FULL;
    pnl_st.soft &= 1u;
    if (pnl_st.spare >= VB_N) pnl_st.spare = 0;
    if (pnl_st.ped >= PJ_N) pnl_st.ped = PJ_AUTO;
    for (i = 0; i < NSETUP; i++) { if (pnl_st.set_lay[i] >= LAY_N) pnl_st.set_lay[i] = LAY_FULL; pnl_st.set_soft[i] &= 1u; }
    for (i = 0; i <= NSETUP; i++) {     /* the wheels' targets (WT_N: 17, the mod wheel no pitch); NSETUP: now */
        uint8_t *w = i < NSETUP ? pnl_st.set_whl[i] : pnl_st.whl;
        if (w[0] >= 17u || (w[0] >= 2u && w[0] <= 5u)) w[0] = 0;
        if (w[1] >= 17u) w[1] = 0;
    }
    org_par.perc_soft = pnl_st.soft;
}
static void pnl_init(void)
{
    memset(&pnl_st, 0, sizeof pnl_st);
    if (blob_load(4, &pnl_st, sizeof pnl_st) != 0 || pnl_st.ver != PNL_VER) {
        memset(&pnl_st, 0, sizeof pnl_st);
        if (blob_load(4, &pnl_st, PNL_V4_SIZE) == 0 && pnl_st.ver == 4u) { pnl_init_loaded(); return; }
        memset(&pnl_st, 0, sizeof pnl_st);
        if (blob_load(4, &pnl_st, PNL_V3_SIZE) == 0 && pnl_st.ver == 3u) { pnl_init_loaded(); return; }
        memset(&pnl_st, 0, sizeof pnl_st);
        if (blob_load(4, &pnl_st, PNL_V2_SIZE) != 0 || pnl_st.ver != 2u) {
            memset(&pnl_st, 0, sizeof pnl_st);
            if (blob_load(4, &pnl_st, sizeof(pnl_v1_t)) != 0 || pnl_st.ver != 1u) memset(&pnl_st, 0, sizeof pnl_st);
        }
    }
    pnl_init_loaded();
}
static void pnl_layout_step(int32_t st, uint32_t round)  /* st layouts on (back: < 0) in A B1 B2 C D; round: D -> A */
{
    uint32_t i = 0;
    int32_t j;
    while (i < LAY_N - 1u && LAY_ORDER[i] != pnl_st.lay) i++;
    j = round ? ((int32_t)i + st % (int32_t)LAY_N + (int32_t)LAY_N) % (int32_t)LAY_N : (int32_t)i + st;
    pnl_st.lay = LAY_ORDER[j < 0 ? 0 : j >= (int32_t)LAY_N ? LAY_N - 1u : (uint32_t)j];
    ctl_say("LAYOUT", LAY_NAME[pnl_st.lay]);
    board_splash("PERFORMANCE LAYOUT", LAY_TAG[pnl_st.lay], LAY_NAME[pnl_st.lay]);
    pnl_touch();
}
static void pnl_soft_toggle(void)
{
    pnl_st.soft = org_par.perc_soft = (uint8_t)!org_par.perc_soft;
    ctl_say("PERC VOLUME", pnl_st.soft ? "SOFT" : "NORMAL");
    pnl_touch();
}
static void pnl_perc_decay(uint32_t p, int32_t st)   /* percussion p's decay, st steps */
{
    uint32_t cl = PERC_IS_SLOW(p) ? PERC_DK_SLOW : PERC_DK_FAST;
    int32_t i = perc_dk[p] ? (int32_t)perc_dk[p] - 1 : (int32_t)cl;
    char h[24], t[24], *e;
    i += st;
    if (i < 0) i = 0;
    if (i >= (int32_t)PERC_DK_N) i = PERC_DK_N - 1u;
    perc_dk[p] = (uint32_t)i == cl ? 0u : (uint8_t)(i + 1);
    org_par.perc_q15[p] = perc_dk[p] ? PERC_DK_Q[i] : 0u;
    e = PERC_DK_MS[i] ? cat(org_fmt(t, PERC_DK_MS[i], 0), " MS") : cat(t, "HOLD");
    if ((uint32_t)i == cl) (void)cat(e, " CLASSIC");
    (void)cat(cat(h, PERC_NAME[p]), " DECAY");
    ctl_say(h, t);
}
static void whl_select(uint32_t w, uint32_t t);
static void pnl_setup_store(uint32_t n)
{
    pnl_st.set_lay[n] = pnl_st.lay; pnl_st.set_soft[n] = pnl_st.soft;
    pnl_st.set_whl[n][0] = pnl_st.whl[0]; pnl_st.set_whl[n][1] = pnl_st.whl[1];
    pnl_touch();
}
static void pnl_setup_recall(uint32_t n)
{
    pnl_st.lay = pnl_st.set_lay[n]; pnl_st.soft = org_par.perc_soft = pnl_st.set_soft[n];
    whl_select(0, pnl_st.set_whl[n][0]); whl_select(1, pnl_st.set_whl[n][1]);
    pnl_touch();
}
static void les_tick(void)              /* the cabinets, the effect, the master, the panel: saved 2 s after the last change */
{
    if (pnl_dirty && fm1_ms - pnl_at > 2000u) { pnl_dirty = 0; (void)blob_save(4, &pnl_st, sizeof pnl_st); }
    if (les_dirty && fm1_ms - les_at > 2000u) { les_dirty = 0; (void)blob_save(1, &les_st, sizeof les_st); }
    par_save_tick();
}

/* ---- the wheels (FX page WHEELS, K1 the mod wheel's, K5 the pitch-bend wheel's): what they move, each setup its own
   (blob 4). AUTO as before: the mod wheel the WAH's pedal while WAH is in, else the rotary as BEND + MOD says; the bend
   wheel as BEND + MOD says. ROTARY: the mod wheel its speed, the bend wheel kick (up) and tutti (down). PITCH: +-2
   semitones, SHIFT 1-3: +-1..3 octaves, the tonewheels themselves (every manual; out the pitch bend, and its range as
   RPN 0). A value (the focused manual's drive or sends, or one of the insert effect's): the mod wheel from its knob's
   setting up to the top, the bend wheel up to the top and down to the bottom; at rest the knob's setting again. Only
   AUTO, ROTARY, PITCH and SHIFT send the wheel out */
enum { WT_AUTO, WT_LESLIE, WT_PITCH, WT_SHIFT1, WT_SHIFT2, WT_SHIFT3, WT_DRIVE, WT_DSEND, WT_RSEND, WT_FX,
       WT_N = WT_FX + FXI_NP };
typedef char wt_n_ok[WT_N == 17u ? 1 : -1];
static const char *const WT_NAME[WT_FX] = {"AUTO", "ROTARY", "PITCH", "SHIFT 1", "SHIFT 2", "SHIFT 3", "DRIVE", "DLY SEND",
                                           "REV SEND"};
static struct { int32_t pos[2]; int16_t base[2]; uint8_t held[2], man[2], range_out; } whl;
static uint32_t whl_bends(uint32_t t) { return t >= WT_PITCH && t <= WT_SHIFT3; }
static uint32_t whl_range(uint32_t t) { return t == WT_PITCH ? 2u : 12u * (t - WT_PITCH); }
static uint32_t whl_ok(uint32_t w, uint32_t t)   /* t a target wheel w can take now */
{
    if (t >= WT_N || (!w && whl_bends(t))) return 0;
    if (t >= WT_FX) return fxi_st.type != FXI_OFF && FXI_PAR[fxi_st.type][t - WT_FX].tag != 0;
    return 1;
}
static char *whl_name(char *p, uint32_t t)
{
    if (t < WT_FX) return cat(p, WT_NAME[t]);
    p = cat(p, "FX ");
    return fxi_st.type != FXI_OFF && FXI_PAR[fxi_st.type][t - WT_FX].tag ? cat(p, FXI_PAR[fxi_st.type][t - WT_FX].tag)
                                                                         : org_fmt(p, (int32_t)(t - WT_FX + 1u), 0);
}
static void whl_rpn(uint32_t r)         /* the pitch-bend range out (RPN 0), on the focused zone's channels */
{
    uint32_t ch, m = zone_ctl_mask(&zn.z[mls.focus], 1);
    if (r == whl.range_out) return;
    whl.range_out = (uint8_t)r;
    for (ch = 0; ch < 16u; ch++)
        if (m >> ch & 1u) {
            ml_q(0xB0u | ch, 101, 0); ml_q(0xB0u | ch, 100, 0); ml_q(0xB0u | ch, 6, r); ml_q(0xB0u | ch, 38, 0);
            ml_q(0xB0u | ch, 101, 127); ml_q(0xB0u | ch, 100, 127);
        }
}
static int32_t whl_val(uint32_t w, int32_t base, int32_t lo, int32_t hi, int32_t pos)   /* pos: 0..8191, +-8192 */
{
    if (pos >= 0) return base + (int32_t)(((int64_t)(hi - base) * pos) / 8191);
    return w ? base + (int32_t)(((int64_t)(base - lo) * pos) / 8192) : base;
}
static void whl_value(uint32_t w, uint32_t t, int32_t pos)   /* a value target: held from the wheel off rest */
{
    char s[12];
    int32_t v;
    if (t >= WT_FX) {                   /* one of the insert's */
        uint32_t k = t - WT_FX, ty = fxi_st.type;
        const par_t *q = &FXI_PAR[ty][k];
        if (!pos || ty == FXI_OFF || !q->tag) {
            if (fxi_ovr.on >> w & 1u) { fxi_ovr.on &= (uint8_t)~(1u << w); if (fxi_ovr.type[w] == FXI_WAH && !fxi_ovr.k[w]) fxi_pedal = (uint32_t)fxi_st.p[FXI_WAH][0] * 10u; fxi_apply(); }
            return;
        }
        v = whl_val(w, fxi_st.p[ty][k], q->min, q->max, pos);
        fxi_ovr.on |= (uint8_t)(1u << w); fxi_ovr.type[w] = (uint8_t)ty; fxi_ovr.k[w] = (uint8_t)k; fxi_ovr.v[w] = (int16_t)v;
        if (ty == FXI_WAH && !k) fxi_pedal = (uint32_t)v * 10u;   /* (the WAH's pedal: its own) */
        fxi_apply();
        par_fmt(s, q, v);
        ctl_say(q->name, s);
        return;
    }
    {                                   /* the focused manual's drive, delay or reverb send */
        volatile uint8_t *a;
        uint32_t m = whl.held[w] ? whl.man[w] : mls.focus;
        a = t == WT_DRIVE ? &org_par.drive[m] : t == WT_DSEND ? &org_par.dsend[m] : &org_par.rsend[m];
        if (!pos) { if (whl.held[w]) { *a = (uint8_t)whl.base[w]; whl.held[w] = 0; } return; }
        if (!whl.held[w]) { whl.held[w] = 1; whl.man[w] = (uint8_t)m; whl.base[w] = *a; }
        v = whl_val(w, whl.base[w], 0, 127, pos);
        *a = (uint8_t)v;
        ctl_say_int(WT_NAME[t], v, 0);
    }
}
static void whl_mod(uint32_t mv)        /* the mod wheel moved: 0..127 */
{
    uint32_t t = pnl_st.whl[0];
    whl.pos[0] = (int32_t)(mv * 8191u / 127u);
    if (t == WT_AUTO && fxi_rt.type == FXI_WAH) fxi_wheel(mv);   /* WAH: the wheel is its pedal */
    else if (t <= WT_LESLIE) ui_strip_mod_local(mv);
    else whl_value(0, t, whl.pos[0]);
}
static void whl_bend(int32_t b)         /* the bend wheel moved: -8192..8191 */
{
    uint32_t t = pnl_st.whl[1];
    whl.pos[1] = b;
    if (t == WT_AUTO) ui_strip_pitch_local(b);
    else if (t == WT_LESLIE) {          /* up: the rotary kicked fast, down: tutti, while held */
        org_par.bend = 0;
        ctl.kick = b > 2000;
        set_leslie(ctl.kick ? LESLIE_FAST : ctl.leslie_user);
        org_par.boost = b < -2000;
        ml_send_bend(b);
    } else if (whl_bends(t)) {
        org_par.bend_range = (uint8_t)whl_range(t);
        org_par.bend = b;
        ctl.pitch_strip = b;
        ml_send_bend(b);
    } else whl_value(1, t, b);
}
static void whl_select(uint32_t w, uint32_t t)   /* wheel w now moves t (the old one let go) */
{
    uint32_t old = pnl_st.whl[w];
    if (t >= WT_N || (!w && whl_bends(t))) t = WT_AUTO;
    if (old >= WT_DRIVE) whl_value(w, old, 0);
    if (w && (old == WT_LESLIE || old == WT_AUTO)) { ctl.kick = 0; org_par.boost = 0; set_leslie(ctl.leslie_user); }
    if (w && whl_bends(old) && !whl_bends(t)) { org_par.bend = 0; org_par.bend_range = 0; whl_rpn(2); }
    pnl_st.whl[w] = (uint8_t)t;
    if (w && whl_bends(t)) { org_par.bend_range = (uint8_t)whl_range(t); whl_rpn(whl_range(t)); }
    if (!w && t != old && whl.pos[0]) whl_mod((uint32_t)whl.pos[0] * 127u / 8191u);   /* (where the wheel stands) */
}
static void whl_knob(uint32_t k, int32_t st)   /* K1: the mod wheel's target, K3 pad 12's CC, K4 the pedal, K5 the bend wheel's */
{
    uint32_t w = k == 4u ? 1u : 0u, t = pnl_st.whl[w], n;
    char s[16];
    if (k == 2u) {                      /* K3: the YST action pad's CC */
        int32_t i = (int32_t)vb_idx() + (st > 0 ? 1 : -1);
        pnl_st.spare = (uint8_t)(i < 0 ? 0 : i >= (int32_t)VB_N ? VB_N - 1u : (uint32_t)i);
        pnl_touch();
        org_fmt(cat(cat(s, VB_NAME[pnl_st.spare]), " CC "), VB_CC[pnl_st.spare], 0);
        ctl_say("ACTION PAD", s);
        return;
    }
    if (k == 3u) {                      /* K4: what the pedal socket takes */
        pnl_st.ped = (uint8_t)((pnl_st.ped + (st > 0 ? 1u : PJ_N - 1u)) % PJ_N);
        pnl_touch();
        smk_pedal_mode();
        ctl_say("PEDAL", PJ_NAME[pnl_st.ped]);
        return;
    }
    if (k != 0u && k != 4u) return;
    for (n = 0; n < WT_N; n++) {        /* the next one it can take, round */
        t = (t + (st > 0 ? 1u : WT_N - 1u)) % WT_N;
        if (whl_ok(w, t)) break;
    }
    whl_select(w, t);
    whl_name(s, t);
    ctl_say(w ? "BEND WHEEL" : "MOD WHEEL", s);
    pnl_touch();
}
static void whl_init(void)              /* at power-on: the targets as kept */
{
    uint32_t t = pnl_st.whl[1];
    pnl_st.whl[1] = WT_AUTO;
    whl_select(1, t);
}

/* ---- the backup's extra part (core/orgigs_backup.c), after the core's F and M: records L (the rotary cabinets), P (the
   panel's layouts), each a tag, its length (LE 16) and the store as kept in flash (bk_rec); one of another length or
   version is skipped */
typedef char smk_bk_fits[3u + sizeof les_st + 3u + sizeof pnl_st <= BOARD_BK_EXTRA_MAX ? 1 : -1];
static uint32_t board_bk_put(uint8_t *b, uint32_t room)
{
    uint32_t o = bk_rec(b, room, 'L', &les_st, sizeof les_st);
    return o + bk_rec(b + o, room - o, 'P', &pnl_st, sizeof pnl_st);
}
static void board_bk_get(const uint8_t *b, uint32_t n)
{
    uint32_t o = 0;
    while (o + 3u <= n) {
        uint32_t len = b[o + 1] | (uint32_t)b[o + 2] << 8;
        const uint8_t *d = b + o + 3;
        if (o + 3u + len > n) break;
        if (b[o] == 'L' && len == sizeof les_st && d[0] == LES_VER) {
            memcpy(&les_st, d, len); les_init_loaded(); les_dirty = 1; les_at = fm1_ms - 2000u;
        } else if (b[o] == 'P' && ((len == sizeof pnl_st && d[0] == PNL_VER) || (len == PNL_V4_SIZE && d[0] == 4u)
                                    || (len == PNL_V3_SIZE && d[0] == 3u)
                                    || (len == PNL_V2_SIZE && d[0] == 2u)
                                    || (len == sizeof(pnl_v1_t) && d[0] == 1u))) {
            memset(&pnl_st, 0, sizeof pnl_st);
            memcpy(&pnl_st, d, len); pnl_init_loaded(); pnl_dirty = 1; pnl_at = fm1_ms - 2000u;
        }
        o += 3u + len;
    }
}

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

/* the MIDI mixer: the channel's name (its manual, its SEQTRAK track, else its number) */
static char *mix_label(char *p, uint32_t ch)
{
    uint32_t zi;
    const char *t = yst_ch_tag(ch);
    for (zi = 0; zi < ORG_NMAN; zi++) if (zn.z[zi].out_ch == ch) return cat(p, MAN_TAG[zi]);
    return t ? cat(p, t) : org_fmt(p, (int32_t)ch + 1, 0);   /* (a SEQTRAK track: its tag) */
}
/* the MIDI mixer (the Elite): 16 channels on two pages of 8 (KNOB BANK), K1..K8 one parameter (< >: volume, pan,
   reverb, delay); the top pads mute, the bottom pads solo. A SEQTRAK track mutes by its own CC 23 (as SCALE + pad in
   YST), the other channels by their volume (0, and back); a solo mutes all the channels not soloed */
enum { MXP_VOL, MXP_PAN, MXP_REV, MXP_DLY, MXP_N };
static const char *const MXP_NAME[MXP_N] = {"VOLUME", "PAN", "REVERB", "DELAY"};
static const char *const MXP_TAG[MXP_N] = {"VOL", "PAN", "REV", "DLY"};
static const uint8_t MXP_CC[MXP_N] = {7, 10, 91, 94};
static struct {
    uint8_t page, par;                  /* channels 1..8 / 9..16; the parameter on the knobs */
    uint16_t mute, solo, off;           /* muted (not the SEQTRAK's), soloed; silenced as last sent */
    uint8_t v[MXP_N - 1][16];           /* pan, reverb, delay as sent + 1 (0: not known) */
} mx;
static uint32_t mx_yst(uint32_t ch) { return yst_ch_tag(ch) != 0; }
static uint32_t mx_muted(uint32_t ch) { return mx_yst(ch) ? yst.t[ch].mute : mx.mute >> ch & 1u; }
static int32_t mx_get(uint32_t ch, uint32_t p)          /* -1: not known */
{
    if (mx_yst(ch)) { uint32_t v = yst.t[ch].v[YP_VOL + p]; if (v != YP_UNKNOWN) return (int32_t)v; }
    if (p == MXP_VOL) return ml_mix[ch];
    return mx_yst(ch) ? -1 : (int32_t)mx.v[p - 1u][ch] - 1;
}
static char *mx_name(char *p, uint32_t ch)    /* "CH 3", "CH 1 UM", "CH 8 SY1" */
{
    char n[8];
    p = org_fmt(cat(p, "CH "), (int32_t)ch + 1, 0);
    mix_label(n, ch);
    return n[0] >= '0' && n[0] <= '9' ? p : cat(cat(p, " "), n);
}
static void mx_step(uint32_t k, int32_t st)
{
    uint32_t ch = mx.page * 8u + k, p = mx.par;
    int32_t v = mx_get(ch, p);
    char t[28], *q;
    v = clampi((v < 0 ? 64 : v) + st * 2, 0, 127);
    if (p == MXP_VOL) { ml_mix[ch] = (uint8_t)v; touched(); }
    if (mx_yst(ch)) yst_set(ch, YP_VOL + p, (uint32_t)v);
    else {
        if (p != MXP_VOL) mx.v[p - 1u][ch] = (uint8_t)(v + 1);
        if (p != MXP_VOL || !(mx.off >> ch & 1u)) ml_q(0xB0u | ch, MXP_CC[p], p == MXP_VOL ? ml_mix_out(ch) : (uint32_t)v);   /* (muted: kept) */
    }
    q = mx_name(t, ch);
    cat(cat(q, " "), MXP_NAME[p]);
    if (p == MXP_PAN) ctl_say_int(t, v - 64, 1); else ctl_say_int(t, v, 0);
}
static void mx_apply(void)              /* what is silenced to the channels: on a change (a solo silences the */
{                                       /* channels the zones play and the SEQTRAK's tracks, not every channel) */
    uint32_t ch, heard = ml_used_channels() | (yst.on ? (1u << YST_NTRK) - 1u : 0u);
    for (ch = 0; ch < 16u; ch++) {
        uint32_t eff = mx_muted(ch) || (mx.solo && !(mx.solo >> ch & 1u) && heard >> ch & 1u);
        if (eff == (mx.off >> ch & 1u)) continue;
        mx.off ^= (uint16_t)(1u << ch);
        if (mx_yst(ch)) ml_q(0xB0u | ch, 23u, eff ? 127u : 0u);
        else ml_q(0xB0u | ch, 7u, eff ? 0u : ml_mix_out(ch));
    }
}
static void mx_mute(uint32_t ch)
{
    char t[20];
    if (mx_yst(ch)) { yst_mute(ch); return; }
    mx.mute ^= (uint16_t)(1u << ch);
    mx_name(t, ch);
    ctl_say(t, mx.mute >> ch & 1u ? "MUTED" : "ON");
}
static void mx_solo(uint32_t ch)
{
    char t[20];
    mx.solo ^= (uint16_t)(1u << ch);
    mx_name(t, ch);
    ctl_say(t, mx.solo >> ch & 1u ? "SOLO" : "SOLO OFF");
}
static void knob_mix(uint32_t k, int32_t st) { mx_step(k, st); }
/* the lights: what the LEDs of the keys and the buttons do */
/* the dim level: 1..4 a short glow pulse every frame, 5..10 also fully lit in 1, 2, 3, 4, 6, 8 of every 8
   frames (10 = as bright as FULL). The pulse stays at most 3 us: a pulse longer than the matrix shift makes
   the key-scan interrupt wait out the rest (the frames cost nothing) */
static const uint16_t LT_NS[LT_LEVELS] = {500, 1000, 2000, 3000, 3000, 3000, 3000, 3000, 3000, 0};
static const uint8_t LT_DUTY[LT_LEVELS] = {0, 0, 0, 0, 1, 2, 3, 4, 6, 8};
static void knob_light(uint32_t k, int32_t st)   /* the Elite: K1 the pads' colours, K2 the buttons, K3 the pads' */
{                                                   /* brightness (the keys have no LEDs, the buttons' can't dim) */
    if (smk_knob_light(k, st)) { touched(); return; }
    if (k != 1u) return;
    st0.lt_btns = st > 0 ? LT_FULL : LT_OFF;
    ctl_say("BUTTONS LIGHT", LT_NAME[st0.lt_btns]);
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

static void knob(uint32_t k, int32_t st)        /* k = 0..3 */
{
    if (ui.mode == MODE_FX) knob_fx(k, st);
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
            else if (i == 1u) flag_toggle(z, Z_NOCC, "FILTER CC OUT");
            else if (i == 2u) flag_toggle(z, Z_NOPC, "FILTER PROG CHANGE");
            else if (i == 3u) { ml_release_all(); z->flags ^= Z_NOINT; ctl_say("INTERNAL SOUND", z->flags & Z_NOINT ? "OFF" : "ON"); }
            else {
                mls.learn = mls.learn ? 0u : 1u;
                ctl_say(mls.learn ? "LEARN: PLAY LOWEST + HIGHEST" : "LEARN OFF", 0);
            }
        }
    } else if (i == 0u) {
        static const uint8_t F[MS_N] = {0, Z_CLONE, 0, Z_HARM, Z_CHORD};
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

/* the battery: core/orgigs_bat.c */
static void draw_bat(void)              /* the symbol at the top bar's right end, 18 x 10; charging: a bolt in it */
{
    uint32_t pct = bat_pct(), w = (pct * 12u + 50u) / 100u;
    uint16_t c = pct < 20u ? C_HRED : C_TXT;
    cv_rect(221, 7, 15, 10, C_MUT);
    cv_rect(222, 8, 13, 8, C_BG);
    cv_rect(236, 10, 2, 4, C_MUT);
    if (w) cv_rect(223, 9, (int32_t)w - (w > 11u ? 1 : 0), 6, c);
    if (bat_charging()) draw_bolt(229, 5);
}

/* the top bar: a number + tag, then badges; the right-hand text (left of the battery, when there is one) */
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
            uint32_t sel = 1;                   /* (the SMK-37: all of them on the knobs and fader 1) */
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
    cv_text(176, 6, &FONT_S, cpl_idx() ? CPL_TAG[cpl_idx()] : "", C_CH_ON);
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
        draw_tab(41, C_HRED, org_par.perc != 0u, org_par.perc_soft ? "SOFT" : "PERC", PERC_TAG[org_par.perc]);
    else
        draw_tab(41, C_HRED, cpl_idx() != 0u, "CPL", CPL_TAG[cpl_idx()]);
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
/* the SMK-37's right block: ARP NOTE-REPEAT SCALE CHORD GLOBE BT / PATCH PARA FX SEQ PLAY REC */
static const uint8_t BTN_AT[12] = {BTN_ARP, BTN_LFO, BTN_SEL, BTN_ENV, BTN_GLO, BTN_SAVE,
                                   BTN_HOME, BTN_EDIT, BTN_FX, BTN_SEQ, BTN_PLAY, BTN_REC};

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
            memset(b->lab[i], 0, sizeof b->lab[i]);     /* (a label set again replaces the first) */
            while (*lab && n < 4u) { *p++ = *lab++; n++; }
            b->on[i] = (uint8_t)(on != 0);
        }
}

/* what every FX / MIDI screen shows on the global buttons */
static void btn_common(btnmap_t *b)
{
    btn_set(b, BTN_FX, "FX", ui.mode == MODE_FX);
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

static void les_cells(cells_t *c, uint32_t from)   /* four values from the cabinet shown */
{
    const int16_t *p = les_par(les_st.type);
    uint32_t i;
    char s[12];
    memset(c, 0, sizeof *c);
    c->lit = les_st.type >= LES_U1;
    for (i = 0; i < 4u; i++) { les_fmt(s, from + i, p[from + i]); cells_set(c, i, LESP[from + i].tag, s); }
}
static void draw_les_types(void)        /* the four cabinets, the one chosen lit (pads 1..4) */
{
    uint32_t i;
    cv_begin(240, 28, C_BG);
    for (i = 0; i < LES_NT; i++) {
        int32_t x = (int32_t)i * 60;
        uint32_t on = i == les_st.type;
        cv_rect(x + 1, 2, 58, 24, on ? C_ACC : C_GRID);
        if (!on) cv_rect(x + 2, 3, 56, 22, C_BG);
        cv_text(x + 30 - text_w_s(LES_TNAME[i]) / 2, 7, &FONT_S, LES_TNAME[i], on ? C_BLACK : i >= LES_U1 ? C_TXT : C_MUT);
    }
    cv_blit(0, 24);
}

static void fxi_cells(cells_t *c, uint32_t from)   /* four values of the effect shown */
{
    uint32_t i, t = fxi_st.type;
    char s[12];
    memset(c, 0, sizeof *c);
    c->lit = t != FXI_OFF;
    for (i = 0; i < 4u; i++) {
        const par_t *q = &FXI_PAR[t][from + i];
        if (!q->tag) continue;
        par_fmt(s, q, fxi_st.p[t][from + i]);
        cells_set(c, i, q->tag, s);
    }
}
static void draw_fxi_types(void)        /* the effects on pads 1-7, the one chosen lit; dark: not yet */
{
    uint32_t i;
    cv_begin(240, 28, C_BG);
    for (i = 1; i < FXI_N; i++) {
        int32_t x = (int32_t)(i - 1u) * 34 + 1;
        uint32_t on = i == fxi_st.type;
        cv_rect(x + 1, 2, 32, 24, on ? C_ACC : C_GRID);
        if (!on) cv_rect(x + 2, 3, 30, 22, C_BG);
        cv_text(x + 17 - text_w_s(FXI_TAG[i]) / 2, 7, &FONT_S, FXI_TAG[i], on ? C_BLACK : FXI_READY[i] ? C_TXT : C_GRID);
    }
    cv_blit(0, 24);
}

static void draw_whl_head(int32_t m, int32_t b)   /* the wheels now: MOD 0..100 %, BEND -100..+100 % */
{
    cv_begin(240, 28, C_BG);
    cv_text(4, 7, &FONT_S, "MOD", C_MUT);
    cv_rect(34, 9, 80, 10, C_GRID);
    cv_rect(34, 9, (int32_t)(m * 80 / 100), 10, C_ACC);
    cv_text(124, 7, &FONT_S, "BEND", C_MUT);
    cv_rect(160, 9, 76, 10, C_GRID);
    cv_rect(197, 5, 2, 18, C_TXT);
    if (b > 0) cv_rect(198, 9, (int32_t)(b * 38 / 100), 10, C_ACC);
    else if (b < 0) cv_rect(198 + (int32_t)(b * 38 / 100), 9, (int32_t)(-b * 38 / 100), 10, C_ACC);
    cv_blit(0, 24);
}
static void mst_cells(cells_t *c, uint32_t from, uint32_t n, uint32_t lit)   /* n values of the master */
{
    uint32_t i;
    char s[12];
    memset(c, 0, sizeof *c);
    c->lit = lit;
    for (i = 0; i < n; i++) { par_fmt(s, &MSP[from + i], mst_st.p[from + i]); cells_set(c, i, MSP[from + i].tag, s); }
}
static int32_t mst_gr10(void)           /* the compressor's cut now, 1/10 dB */
{
    int32_t g = mst_gain;
    return g >= 4096 || g < 1 ? 0 : (12 * 4096 - ilog2_q12((uint32_t)g)) * 602 / 40960;
}
static void draw_mst_head(void)         /* EQ, COMP (pads 1, 2; lit: on), the compressor's cut as a bar */
{
    static const char *const NM[2] = {"EQ", "COMP"};
    uint32_t i;
    int32_t gr = mst_gr10(), w = gr > 120 ? 120 : gr;
    char s[16], *q;
    cv_begin(240, 28, C_BG);
    for (i = 0; i < 2u; i++) {
        int32_t x = (int32_t)i * 60;
        uint32_t on = i ? mst_st.comp_on : mst_st.eq_on;
        cv_rect(x + 2, 2, 56, 24, on ? C_ACC : C_GRID);
        if (!on) cv_rect(x + 3, 3, 54, 22, C_BG);
        cv_text(x + 30 - text_w_s(NM[i]) / 2, 7, &FONT_S, NM[i], on ? C_BLACK : C_TXT);
    }
    q = cat(s, "GR -"); q = org_fmt(q, gr / 10, 0); *q++ = '.'; *q++ = (char)('0' + gr % 10); *q = 0;
    cv_text(124, 2, &FONT_S, s, mst_st.comp_on ? C_TXT : C_MUT);
    cv_rect(124, 18, 112, 6, C_GRID);
    if (w) cv_rect(124, 18, w * 112 / 120, 6, C_HRED);
    cv_blit(0, 24);
}

/* the grid (the Elite: 100 rows, the shared ones below): column heads (16 px), then UM LM PM rows of 28 px: the
   manual and its rotary, four knobs with values */
static void draw_fx_grid(void)
{
    static const uint16_t COL[4] = {C_HRED, C_HGREEN, C_HYELLOW, C_FLT};
    uint32_t dept = dept_now(), m, k, half;
    const char *head[4] = {"DRIVE", dept == DEPT_VINTAGE ? "VIB" : "TREM", "DELAY", "REVERB"};
    for (half = 0; half < 3u; half++) {     /* 100 rows: three canvases of 34 (the canvas holds 64) */
        int32_t oy = (int32_t)half * 34;
        cv_begin(240, half == 2u ? 32 : 34, C_BG);
        cv_oy = -oy;
        for (k = 0; k < 4u; k++) cv_text(28 + (int32_t)k * 53 + 26 - text_w_s(head[k]) / 2, 0, &FONT_S, head[k], C_MUT);
        for (m = 0; m < ORG_NMAN; m++) {
            int32_t y = 16 + (int32_t)m * 28;
            uint32_t f = m == mls.focus;
            if (y + 28 <= oy || y >= oy + 34) continue;
            if (f) cv_rect(0, y, 240, 28, RGB(30, 26, 20));
            cv_rect(0, y, 240, 1, C_GRID);
            cv_text(3, y + 2, &FONT_S, MAN_TAG[m], f ? C_ACC : C_TXT);
            cv_text(3, y + 14, &FONT_S, "L", org_par.les_on[m] ? C_HYELLOW : C_GRID);
            for (k = 0; k < 4u; k++) {
                int32_t x = 28 + (int32_t)k * 53;
                uint32_t v = k == 0u ? org_par.drive[m] : k == 2u ? org_par.dsend[m] : k == 3u ? org_par.rsend[m]
                           : dept == DEPT_VINTAGE ? org_par.vib[m] * 127u / (ORG_NVIB - 1u) : org_par.trem[m];
                char s[8];
                if (k == 1u && dept == DEPT_VINTAGE) cat(s, VIB_NAME[org_par.vib[m]]);
                else org_fmt(s, (int32_t)v, 0);
                draw_knob_at(x + 14, y + 14, 11, f ? COL[k] : C_MUT, v);
                cv_text(x + 28, y + 7, &FONT_S, s, f ? COL[k] : C_TXT);
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
    btn_set(b, BTN_ARP, "<", 0);                          /* (the Elite: ARP / PATCH the pages FX ROTARY EFFECT) */
    btn_set(b, BTN_HOME, ">", 0);
    btn_set(b, BTN_SEQ, "ROT", org_par.les_on[mls.focus]);
}

/* ---- the MIDI mixer: 4 x 4 channels, a knob each (the parameter on the knobs); the page's two rows lit;
   muted: MUTE in red (as the pad), soloed: the name in blue, silenced by a solo: grey */
static void draw_mix_grid(void)
{
    uint32_t half, r, c, used = ml_used_channels();
    for (half = 0; half < 4u; half++) {     /* four canvases of 34 rows */
        int32_t oy = (int32_t)half * 34;
        cv_begin(240, 34, C_BG);
        cv_oy = -oy;
        for (r = 0; r < 4u; r++) {
            int32_t y = (int32_t)r * 34;
            uint32_t f = r >> 1 == mx.page;
            if (y + 34 <= oy || y >= oy + 34) continue;
            if (f) cv_rect(0, y, 240, 34, RGB(30, 26, 20));
            for (c = 0; c < 4u; c++) {
                uint32_t ch = r * 4u + c;
                int32_t x = (int32_t)c * 60;
                char s[8];
                int32_t v = mx_get(ch, mx.par);
                uint32_t off = mx.off >> ch & 1u;
                uint16_t lc = mx.solo >> ch & 1u ? RGB(70, 100, 255) : used >> ch & 1u ? C_CH_ON : C_MUT;
                if (c) cv_rect(x, y + 2, 1, 30, C_GRID);
                draw_knob_at(x + 15, y + 17, 12, f && !off ? C_ACC : C_DARKB, v < 0 ? 64u : (uint32_t)v);
                mix_label(s, ch);
                cv_text(x + 31, y + 1, &FONT_S, s, lc);
                if (mx_muted(ch)) cat(s, "MUTE");
                else if (v < 0) cat(s, "--");
                else org_fmt(s, mx.par == MXP_PAN ? v - 64 : v, mx.par == MXP_PAN);
                cv_text(x + 31, y + 17, &FONT_S, s, mx_muted(ch) ? RGB(255, 40, 40) : off ? C_MUT : f ? C_ACC : C_TXT);
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
    btn_set(b, BTN_EDIT, "BACK", 0);
}

/* ---- the lights */
static void light_cells(cells_t *top)
{
    memset(top, 0, sizeof *top);
    top->lit = 1;
    cells_set(top, 1, "BUTTONS", LT_NAME[st0.lt_btns == LT_FULL ? LT_FULL : LT_OFF]);   /* (a DIM kept: dark) */
    cells_set(top, 3, "BACKUP", "SEQ");
    smk_light_cells(top);               /* K1 the pads' colours, K3 their brightness */
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

/* ---- MIDI: the zone screens. The Elite: K1..K4 the top row, K5..K8 the second (both lit) */
static void midi_cells(cells_t *top, cells_t *sel)
{
    const zone_t *z = &zn.z[mls.focus];
    char s[12];
    memset(top, 0, sizeof *top);
    memset(sel, 0, sizeof *sel);
    top->lit = sel->lit = 1;
    switch (ui.ms) {
    case MS_ZONE:
        ch_name(s, z->in_ch); cells_set(top, 0, "IN CH", s);
        ch_name(s, z->out_ch); cells_set(top, 1, "OUT CH", s);
        note_name(s, z->lo); cells_set(top, 2, "LOWEST", s);
        note_name(s, z->hi); cells_set(top, 3, "HIGHEST", s);
        org_fmt(s, z->transpose, 1); cells_set(sel, 0, "TRANSP", s);
        org_fmt(s, z->octave, 1); cells_set(sel, 1, "OCTAVE", s);
        org_fmt(s, z->vel, 0); cells_set(sel, 2, "VEL", s);
        cells_set(sel, 3, "BEND", CTL_NAME[ml_ctl_mode]);
        break;
    case MS_CLONE: {
        char l[12];
        cat(org_fmt(cat(l, "CH"), (int32_t)ui.clone_cur + 1, 0), " OCT");
        org_fmt(s, z->clone_oct[ui.clone_cur], 1); cells_set(top, 0, l, s);
        org_fmt(s, (int32_t)ui.clone_cur + 1, 0); cells_set(top, 1, "CHANNEL", s);
        break;
    }
    case MS_YST: {
        static const char *const L[8] = {"CUTOFF", "RESO", "ATTACK", "DECAY", "VOLUME", "PAN", "REVERB", "DELAY"};
        static const char *const FM[4] = {"FM ALG", "FM AMT", "FM FREQ", "FM FB"};
        uint32_t i;
        for (i = 0; i < 8u; i++) {
            uint32_t p = ui.shift && i < 4u ? YP_ALG + i : YP_CUTOFF + i, v = yst.t[yst.cur].v[p];
            if (v == YP_UNKNOWN) cat(s, "--");
            else org_fmt(s, p == YP_PAN ? (int32_t)v - 64 : p == YP_ALG ? (int32_t)v + 1 : (int32_t)v, p == YP_PAN);
            cells_set(i < 4u ? top : sel, i & 3u, p >= YP_ALG ? FM[i] : L[i], s);
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

static void midi_btnmap(btnmap_t *b)        /* the Elite: ARP / PATCH the screens, PARA the mixer, the switches on pads */
{
    const zone_t *z = &zn.z[mls.focus];
    memset(b, 0, sizeof *b);
    btn_common(b);
    btn_set(b, BTN_ARP, "<", 0);
    btn_set(b, BTN_HOME, ">", 0);
    btn_set(b, BTN_EDIT, "MIX", 0);
    if (ui.ms == MS_YST) {
        uint32_t m = yst.t[yst.cur].v[YP_MODE];
        btn_set(b, BTN_ENV, "ON", zn.yst);
        btn_set(b, BTN_LFO, "RFSH", zn.yst && yst.ask < 2u * YST_NTRK);
        btn_set(b, BTN_SEQ, m == 0u ? "MONO" : m == 2u ? "CHRD" : "POLY", 0);
    } else if (ui.ms == MS_CLONE) {
        btn_set(b, BTN_ENV, "ON", z->flags & Z_CLONE);
    } else if (ui.ms == MS_HARM) {
        btn_set(b, BTN_ENV, "ON", z->flags & Z_HARM && mls.focus != MAN_PEDAL);
    } else if (ui.ms == MS_CHORD) {
        btn_set(b, BTN_ENV, "ON", z->flags & Z_CHORD);
    }
}

/* ZONE: its switches on the pads (top row 1..8, bottom 9..12); 5..9 are midi_switch's second ones, 10 the zone's
   FILTER CC IN, 11 its MIDI OUT (board.c) */
#define ZSW_N 12u
static const char *const ZSW_TAG[ZSW_N] = {"VEL", "FCCO", "PCF", "INT", "LRN", "EXP", "THR", "TRS", "KZN", "AUT", "FCCI",
                                           "MOUT"};
static uint32_t zone_sw_on(uint32_t i)
{
    const zone_t *z = &zn.z[mls.focus];
    switch (i) {
    case 0: return (z->flags & Z_VELTHRU) != 0;
    case 1: return (z->flags & Z_NOCC) != 0;
    case 2: return (z->flags & Z_NOPC) != 0;
    case 3: return !(z->flags & Z_NOINT);
    case 4: return mls.learn != 0;
    case 5: return (z->flags & Z_VELEXPR) != 0;
    case 6: return zn.thru != 0;
    case 7: return ml_rate_safe != 0;
    case 8: return zn.keys == KEYS_ZONES;
    case 9: return zn.auto_pedal != 0;
    case 10: return (z->xf & ZX_FCCI) != 0;
    default: return !(z->xf & ZX_MUTE);
    }
}
/* the pads as they sit (2 rows of 8, 30 px), 60 px */
static void draw_padmap(void)
{
    uint32_t i;
    cv_begin(240, 60, C_BG);
    cv_rect(0, 0, 240, 1, C_GRID);
    for (i = 0; i < 16u; i++) {
        int32_t x = (int32_t)(i % 8u) * 30, y = 4 + (int32_t)(i / 8u) * 28;
        uint32_t on = i < ZSW_N && zone_sw_on(i);
        const char *l = i < ZSW_N ? ZSW_TAG[i] : "";
        if (!*l) continue;
        cv_rect(x + 1, y, 28, 26, on ? (i >= 6u && i < 10u ? C_FLT : C_CH_ON) : C_GRID);
        if (!on) cv_rect(x + 2, y + 1, 26, 24, C_BG);
        cv_text(x + 15 - text_w_s(l) / 2, y + 5, &FONT_S, l, on ? C_BLACK : C_TXT);
    }
    cv_blit(0, 180);
}
static uint32_t padmap_hash(void)
{
    uint32_t i, h = 15u;
    for (i = 0; i < ZSW_N; i++) h = hash_add(h, zone_sw_on(i));
    return h | 1u;
}

static const char *midi_hint(void)
{
    static const char *const H[MS_N] = {"PADS: SWITCHES  K-BANK: ZONE", "PADS 1-16: CHANNELS", BOARD_YST_HINT,
                                        "PADS 1-9: FOOTAGES", "HOLD A CHORD, PRESS PAD 1"};
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

/* ---- YST: the sound browser, over the whole screen: the track and the category small on top, the sounds of the
   track's type below, the full width. Open it on the YST screen with pad 16 (again or GLOBE: back; K1 the category,
   K2 the sound), or anywhere with < + > held (K4 the category, K5 the sound; let go: back), for the focused manual's
   track. K2 / K5 run on over the categories; the list moves only when the cursor would leave it; K1 / K4 put the
   category's first sound on top. A sound loads once the knob rests (150 ms). The names big; the one under the cursor
   runs when it is too long, the others are cut */
#define YSB_ROWS 6u                     /* (the big font: 36 px a row) */
/* a DRUM KIT track: its kits are not in the list (nor how to pick one over MIDI): nothing loads, the SEQTRAK picks */
static uint32_t ysb_kit(uint32_t t) { return yst.t[t].type == YT_DRUMKIT; }
static void ysb_kit_say(void) { ctl_say("DRUM KIT TRACK", "PICK THE KIT ON THE SEQTRAK"); }
static void yst_browse_kit(int32_t st)  /* < > on the YST screen: the next sound, not on a kit track */
{
    if (ysb_kit(yst.cur)) ysb_kit_say();
    else yst_browse(st);
}
#define YSB_W 226                       /* a name's room */
static struct {
    uint32_t on, t, n;                  /* open (1: pad 16, 2: < + > held); the track; the sound under the cursor */
    uint32_t top, lo, hi;               /* the first sound shown; the type's range */
    uint32_t pend, at;                  /* to load, since */
    uint16_t run[17];                   /* the categories in the type's range: where each starts (run[nrun] = past) */
    uint32_t nrun;
} ysb;
static uint32_t ysb_catof(uint32_t t, uint32_t n) { return yst_smp(t) ? YST_CATSMP[n - 1u] : YST_CAT63[n - 1u]; }
static void ysb_follow(void)            /* the track (and its sound) the browser shows */
{
    uint32_t t = yst.cur, lo, hi, n;
    yst_range(t, &lo, &hi);
    ysb.t = t;
    ysb.n = yst.t[t].snd >= lo && yst.t[t].snd <= hi ? yst.t[t].snd : lo;
    ysb.nrun = 0;
    for (n = lo; n <= hi && ysb.nrun < 16u; n++)
        if (n == lo || ysb_catof(t, n) != ysb_catof(t, n - 1u)) ysb.run[ysb.nrun++] = (uint16_t)n;
    ysb.run[ysb.nrun] = (uint16_t)(hi + 1u);
    ysb.lo = lo; ysb.hi = hi;
    ysb.top = ysb.n > lo + 2u ? ysb.n - 2u : lo;
    if (hi + 1u >= lo + YSB_ROWS && ysb.top > hi + 1u - YSB_ROWS) ysb.top = hi + 1u - YSB_ROWS;
    ysb.pend = 0;
}
static void ysb_view(uint32_t cat_top)  /* the window after the cursor moved (cat_top: the category's start on top) */
{
    uint32_t last = ysb.hi + 1u >= ysb.lo + YSB_ROWS ? ysb.hi + 1u - YSB_ROWS : ysb.lo;
    if (cat_top) ysb.top = ysb.n;
    else if (ysb.n < ysb.top) ysb.top = ysb.n;
    else if (ysb.n >= ysb.top + YSB_ROWS) ysb.top = ysb.n + 1u - YSB_ROWS;
    if (ysb.top > last) ysb.top = last;
    if (ysb.top < ysb.lo) ysb.top = ysb.lo;
}
static uint32_t ysb_runof(uint32_t n)
{
    uint32_t r = 0;
    while (r + 1u < ysb.nrun && ysb.run[r + 1u] <= n) r++;
    return r;
}
static void ysb_open(uint32_t on)
{
    ysb.on = on;
    if (on) ysb_follow();
    ctl_say("SOUNDS", on ? "K1: CATEGORY  K2: SOUND" : "CLOSED");
}
static void ysb_say(void)
{
    const char *nm = YST_NAMES + (yst_smp(ysb.t) ? YST_OFFSMP[ysb.n - 1u] : YST_OFF63[ysb.n - 1u]);
    char s[8];
    org_fmt(s, (int32_t)ysb.n, 0);
    ctl_say(nm, s);
}
static void ysb_knob(uint32_t k, int32_t st)   /* K1 the category (its first sound), K2 the sound in it */
{
    uint32_t r;
    if (ysb.t != yst.cur) ysb_follow();
    if (ysb_kit(ysb.t)) { ysb_kit_say(); return; }
    r = ysb_runof(ysb.n);
    if (k == 0u) { ysb.n = ysb.run[clampi((int32_t)r + st, 0, (int32_t)ysb.nrun - 1)]; ysb_view(1); }
    else if (k == 1u) { ysb.n = (uint32_t)clampi((int32_t)ysb.n + st, (int32_t)ysb.lo, (int32_t)ysb.hi); ysb_view(0); }
    else return;
    ysb.pend = 1;
    ysb.at = fm1_ms;
    ysb_say();
}
static void ysb_tick(void)              /* the sound loads once the knobs rest; the browser follows the track */
{
    if (!ysb.on) return;
    if (ysb.t != yst.cur) ysb_follow();
    if (ysb_kit(ysb.t)) { ysb.pend = 0; return; }
    if (ysb.pend && fm1_ms - ysb.at >= 150u) { ysb.pend = 0; yst_load(ysb.t, ysb.n); }
    else if (!ysb.pend && yst.t[ysb.t].snd != ysb.n && yst.t[ysb.t].snd) {   /* (< >, or the SEQTRAK's own) */
        uint32_t lo, hi;
        yst_range(ysb.t, &lo, &hi);
        if (yst.t[ysb.t].snd >= lo && yst.t[ysb.t].snd <= hi) { ysb.n = yst.t[ysb.t].snd; ysb_view(0); }
    }
}
static void ysb_quick(uint32_t k, int32_t st)   /* < + > held, K4 (k 0) / K5 (k 1): the focused manual's track */
{
    uint32_t ch = zn.z[mls.focus].out_ch;
    if (!zn.yst) { ctl_say("YST", "OFF (MIDI: YST)"); return; }
    if (ch >= YST_NTRK) { ctl_say("YST", "NOT ON A SEQTRAK TRACK"); return; }
    if (!ysb.on) { yst_follow_zone(); ysb.on = 2u; ysb_follow(); }
    ysb_knob(k, st);
}
static void ysb_quick_end(void)         /* < + > let go: the quick browser closed, the sound loaded now */
{
    if (ysb.on != 2u) return;
    if (ysb.pend && !ysb_kit(ysb.t)) yst_load(ysb.t, ysb.n);
    ysb.pend = 0;
    ysb.on = 0;
}
static void ysb_short(char *t, const char *nm)   /* 17 characters; a name ending in a number keeps the number */
{
    uint32_t n = 0, d, k;
    while (nm[n]) n++;
    if (n <= 17u) { for (k = 0; k <= n; k++) t[k] = nm[k]; return; }
    for (d = n; d > 0u && nm[d - 1u] >= '0' && nm[d - 1u] <= '9'; d--) { }
    if (d == n || n - d > 4u) { for (k = 0; k < 17u; k++) t[k] = nm[k]; t[17] = 0; return; }
    for (k = 0; k < 16u - (n - d); k++) t[k] = nm[k];
    t[k++] = ' ';
    while (d < n) t[k++] = nm[d++];
    t[k] = 0;
}
static void ysb_fit(char *t, const char *nm, int32_t w)   /* nm cut to w px (the big font); a name ending in a number keeps it */
{
    uint32_t n = 0, d, k, cut;
    while (nm[n] && n < 39u) { t[n] = nm[n]; n++; }
    t[n] = 0;
    if (text_w(&FONT_L, t) <= w) return;
    for (d = n; d > 0u && nm[d - 1u] >= '0' && nm[d - 1u] <= '9'; d--) { }
    if (d == n || n - d > 4u || d == 0u) d = n;         /* (no number to keep) */
    for (cut = d - 1u; cut > 0u; cut--) {
        for (k = 0; k < cut; k++) t[k] = nm[k];
        if (d < n) { uint32_t j; t[k++] = ' '; for (j = d; j < n; j++) t[k++] = nm[j]; }
        t[k] = 0;
        if (text_w(&FONT_L, t) <= w) return;
    }
}
static int32_t ysb_run_off(uint32_t n, int32_t over)   /* the cursor's name running: 1 s still, on at 60 px/s, 1 s at the end */
{
    static uint32_t was, t0;
    uint32_t el, run;
    if (n != was) { was = n; t0 = fm1_ms; }
    if (over <= 0) return 0;
    run = (uint32_t)over * 1000u / 60u;
    el = (fm1_ms - t0) % (run + 2000u);
    if (el < 1000u) return 0;
    if (el >= 1000u + run) return over;
    return (int32_t)((el - 1000u) * 60u / 1000u) & ~3;  /* (4 px steps: fewer redraws) */
}
static uint32_t ysb_drawn[YSB_ROWS + 1u];
static void ysb_redraw(void) { memset(ysb_drawn, 0, sizeof ysb_drawn); }
static void draw_ysb(void)              /* the whole screen: the head (24 px), 6 rows of 36 px */
{
    uint32_t i, r = ysb_runof(ysb.n), h;
    const char *cn = YST_CAT[ysb_catof(ysb.t, ysb.n)];
    char t[48], *p;
    if (ysb_kit(ysb.t)) {                               /* a kit track: no list */
        h = hash_add(93u, ysb.t) | 1u;
        if (h == ysb_drawn[0]) return;
        memset(ysb_drawn, 0, sizeof ysb_drawn);
        ysb_drawn[0] = h;
        static const char *const L[YSB_ROWS] = {"DRUM KIT", "", "THE KITS ARE NOT", "IN THE LIST: PICK", "IT ON THE SEQTRAK.", "THE KEYS PLAY IT."};
        cv_begin(240, 24, C_BG);
        cv_text(2, 3, &FONT_S, YST_TAG[ysb.t], C_ACC);
        cv_text(46, 3, &FONT_S, "KIT TRACK", C_MUT);
        cv_rect(0, 23, 240, 1, C_GRID);
        cv_blit(0, 0);
        for (i = 0; i < YSB_ROWS; i++) {
            cv_begin(240, 36, C_BG);
            if (i == 0u) cv_text(12, 2, &FONT_L, L[i], C_ACC);
            else cv_text(12, 10, &FONT_S, L[i], C_TXT);
            cv_blit(0, 24u + i * 36u);
        }
        return;
    }
    h = hash_add(hash_add(hash_add(hash_add(hash_add(91u, ysb.t), r), ysb.nrun), ysb.n), ysb.on) | 1u;
    if (h != ysb_drawn[0]) {                            /* the head: track, category (n of m), the sound's number */
        ysb_drawn[0] = h;
        cv_begin(240, 24, C_BG);
        cv_text(2, 3, &FONT_S, YST_TAG[ysb.t], C_ACC);
        p = cat(cat(t, cn), "  ");
        p = org_fmt(p, (int32_t)r + 1, 0); *p++ = '/'; org_fmt(p, (int32_t)ysb.nrun, 0);
        cv_text(46, 3, &FONT_S, t, C_MUT);
        org_fmt(t, (int32_t)ysb.n, 0);
        cv_text(238 - text_w_s(t), 3, &FONT_S, t, C_TXT);
        cv_rect(0, 23, 240, 1, C_GRID);
        cv_blit(0, 0);
    }
    for (i = 0; i < YSB_ROWS; i++) {
        uint32_t n = ysb.top + i, cur = n == ysb.n, on = n <= ysb.hi && n == yst.t[ysb.t].snd;
        uint32_t edge = n > ysb.lo && n <= ysb.hi && ysb_catof(ysb.t, n) != ysb_catof(ysb.t, n - 1u);
        const char *nm = n <= ysb.hi ? YST_NAMES + (yst_smp(ysb.t) ? YST_OFFSMP[n - 1u] : YST_OFF63[n - 1u]) : "";
        int32_t off = 0;
        if (cur) {
            uint32_t k = 0;
            while (nm[k] && k < 39u) { t[k] = nm[k]; k++; }
            t[k] = 0;
            off = ysb_run_off(n, text_w(&FONT_L, t) - YSB_W);
        } else ysb_fit(t, nm, YSB_W);
        h = n > ysb.hi ? 7u : hash_add(hash_add(hash_add(hash_add(hash_add(92u, ysb.t), n), cur * 2u + on), edge), (uint32_t)off) | 1u;
        if (h == ysb_drawn[1u + i]) continue;
        ysb_drawn[1u + i] = h;
        cv_begin(240, 36, cur ? C_ACC : C_BG);
        if (n <= ysb.hi) {
            cv_text(12 - off, 2, &FONT_L, t, cur ? C_BLACK : on ? C_ACC : C_TXT);
            if (cur && off) cv_rect(0, 0, 10, 36, C_ACC);       /* (the running name: its own margin) */
            if (on) cv_rect(2, 6, 4, 24, cur ? C_BLACK : C_ACC);   /* the one loaded: a bar */
            if (edge && !cur) cv_rect(0, 0, 240, 1, C_MUT);    /* (a new category starts; after the text: it
                                                                   paints its whole cell) */
        }
        cv_blit(0, 24u + i * 36u);
    }
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

/* ---- the splash: a model or a layout just chosen (or HOME), big over the whole screen for a moment; the panel
   goes on as ever meanwhile (a new one starts it again) */
#define SPLASH_MS 1200u
static struct { char kind[24], name[24], more[32]; uint32_t at, on, drawn; } spl;
static void board_splash(const char *kind, const char *name, const char *more)
{
    memset(spl.kind, 0, sizeof spl.kind); memset(spl.name, 0, sizeof spl.name); memset(spl.more, 0, sizeof spl.more);
    if (kind) cat(spl.kind, kind);
    if (name) cat(spl.name, name);
    if (more) cat(spl.more, more);
    spl.at = fm1_ms;
    spl.on = 1;
}
static uint32_t splash_on(void)
{
    if (spl.on && fm1_ms - spl.at >= SPLASH_MS) spl.on = 0;
    return spl.on;
}
static void draw_splash(void)           /* the kind small, the name big (two lines if it is too wide), more small */
{
    char l1[24], l2[24];
    uint32_t i, half, h = hash_mem(hash_mem(hash_mem(97u, spl.kind, sizeof spl.kind), spl.name, sizeof spl.name), spl.more, sizeof spl.more) | 1u;
    int32_t y;
    if (h == spl.drawn) return;
    spl.drawn = h;
    memset(l1, 0, sizeof l1); memset(l2, 0, sizeof l2);
    cat(l1, spl.name);
    if (text_w(&FONT_L, l1) > 236) {    /* at the last space that leaves the first line short enough */
        uint32_t cut = 0;
        for (i = 1; l1[i]; i++) {
            char c = l1[i];
            l1[i] = 0;
            if (c == ' ' && text_w(&FONT_L, l1) <= 236) cut = i;
            l1[i] = c;
        }
        if (cut) { cat(l2, l1 + cut + 1u); l1[cut] = 0; }
    }
    y = l2[0] ? 70 : 88;
    for (half = 0; half < 4u; half++) {     /* four strips of 60 rows (the canvas holds 64) */
        cv_begin(240, 60, C_BG);
        cv_oy = -(int32_t)half * 60;
        cv_rect(0, 36, 240, 2, C_ACC);
        cv_text(120 - text_w(&FONT_S, spl.kind) / 2, 48, &FONT_S, spl.kind, C_MUT);
        cv_text(120 - text_w(&FONT_L, l1) / 2, y, &FONT_L, l1, C_ACC);
        if (l2[0]) cv_text(120 - text_w(&FONT_L, l2) / 2, y + 38, &FONT_L, l2, C_ACC);
        if (spl.more[0]) cv_text(120 - text_w(&FONT_S, spl.more) / 2, y + (l2[0] ? 84 : 46), &FONT_S, spl.more, C_TXT);
        cv_rect(0, 200, 240, 2, C_ACC);
        cv_oy = 0;
        cv_blit(0, half * 60u);
    }
}

static void ui_draw(void)
{
    uint32_t h, d, m, view = ui.mode == MODE_PLAY ? 0u : ui.mode == MODE_FX ? 1u + 39u * ui.fxp : ui.mode == MODE_MIX ? 2u + MS_N :
                             ui.mode == MODE_LIGHT ? 3u + MS_N : 2u + ui.ms;
    char tag[16], b[16], r[16];
    memset(tag, 0, sizeof tag); memset(b, 0, sizeof b); memset(r, 0, sizeof r);
    if (ysb.on) view = 250u;                           /* the sound browser: the whole screen */
    if (splash_on()) view = 251u;                      /* a model or layout chosen: its name, big */
    if (view != drawn_view) {
        lcd_fill(0, 0, 240, 240, C_BG);
        drawn_view = view;
        for (d = 0; d < R_N; d++) drawn[d] = 0;
        spl.drawn = 0;
        ysb_redraw();
    }
    if (view == 251u) { draw_splash(); return; }
    if (ysb.on) { draw_ysb(); return; }
    model_tag(tag);
    if (ui.mode == MODE_PLAY) {
        char *p = cat(r, "P");
        p = org_fmt(p, (int32_t)dbp_cur() + 1, 0);
        if (dbp_modified(mls.focus)) cat(p, "*");
        h = hash_mem(hash_mem(hash_add(hash_add(hash_add(1u, mls.focus), ui.grp), pnl_st.lay), tag, sizeof tag), r, 8) | 1u;
        if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, LAY_TAG[pnl_st.lay], r, C_MUT); }
        h = hash_add(hash_add(hash_add(2u, mls.focus), ui.grp), org_par.model);
        h = hash_row(h, org_par.drawbar[mls.focus < ORG_NMAN ? mls.focus : 0u], ORG_NDB) | 1u;
        if (h != drawn[R_BARS]) { drawn[R_BARS] = h; draw_bars(); }
        h = hash_add(hash_add(hash_add(3u, mls.focus), cpl_idx()), st0.setup);
        for (m = 0; m < ORG_NMAN; m++) h = hash_row(h, org_par.drawbar[m], ORG_NDB);
        h = hash_add(hash_mem(h, ctl.msg, sizeof ctl.msg), ctl_msg_on()) | 1u;
        if (h != drawn[R_MINI]) { drawn[R_MINI] = h; draw_mini(); }
        m = mls.focus;
        h = hash_add(hash_add(hash_add(hash_add(hash_add(4u, org_par.drive[m]), org_par.vib[m]), org_par.perc + 8u * org_par.perc_soft), org_par.trem[m]), m);
        h = hash_add(hash_add(hash_add(hash_add(h, org_par.rev_mode), org_par.rsend[m]), org_par.dsend[m]), org_par.leslie + 4u * org_par.les_on[m]);
        h = hash_add(hash_add(hash_add(h, org_par.model), cpl_idx()), (uint32_t)(ctl.oct + 8)) | 1u;
        if (h != drawn[R_BOT]) { drawn[R_BOT] = h; draw_bottom(); }
        return;
    }
    {
        static cells_t top, sel;
        static btnmap_t bm;
        const char *hint;
        if (ui.mode == MODE_FX && ui.fxp == FXP_WHEELS) {   /* WHEELS: what the mod and the bend wheel move */
            char v[12];
            int32_t pc0 = whl.pos[0] * 100 / 8191, pc1 = whl.pos[1] >= 0 ? whl.pos[1] * 100 / 8191 : whl.pos[1] * 100 / 8192;
            fx_btnmap(&bm);
            cat(b, "WHEELS");
            hint = "K1 MOD K3 PAD K4 PED K5 BEND";
            h = hash_mem(hash_add(81u, mls.focus), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, b, r, C_MUT); }
            h = hash_add(hash_add(82u, (uint32_t)(pc0 + 200)), (uint32_t)(pc1 + 200)) | 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_whl_head(pc0, pc1); }
            memset(&top, 0, sizeof top);
            memset(&sel, 0, sizeof sel);
            top.lit = pnl_st.whl[0] != WT_AUTO;
            sel.lit = pnl_st.whl[1] != WT_AUTO;
            whl_name(v, pnl_st.whl[0]); cells_set(&top, 0, "MOD", v);
            cat(org_fmt(v, pc0, 0), "%"); cells_set(&top, 1, "NOW", v);
            cells_set(&top, 2, "PAD 12", VB_NAME[vb_idx()]);
            cells_set(&top, 3, "PEDAL", PJ_NAME[pnl_st.ped]);
            whl_name(v, pnl_st.whl[1]); cells_set(&sel, 0, "BEND", v);
            cat(org_fmt(v, pc1, 1), "%"); cells_set(&sel, 1, "NOW", v);
            if (whl_bends(pnl_st.whl[1])) { uint32_t rg = whl_range(pnl_st.whl[1]); cat(org_fmt(cat(v, "+-"), (int32_t)(rg < 12u ? rg : rg / 12u), 0), rg < 12u ? " ST" : " OCT"); cells_set(&sel, 2, "RANGE", v); }
            if (!ped_swell()) cells_set(&sel, 3, "SUSTAIN", mls.key_sus ? "DOWN" : "UP");
            else if (ped.swell == 0xFFu) cells_set(&sel, 3, "SWELL", "MOVE IT");   /* (its travel not learned yet) */
            else { cat(org_fmt(v, (int32_t)(ped.swell * 100u / 127u), 0), "%"); cells_set(&sel, 3, "SWELL", v); }
            h = hash_add(hash_mem(83u, &top, sizeof top), fxi_st.type) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(52, &top); }
            h = hash_mem(84u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(88, &sel); }
        } else if (ui.mode == MODE_FX && ui.fxp == FXP_MASTER) {   /* MASTER: EQ, compressor */
            static cells_t more;
            fx_btnmap(&bm);
            cat(b, "MASTER");
            cat(r, mst_st.eq_on && mst_st.comp_on ? "EQ+COMP" : mst_st.eq_on ? "EQ" : mst_st.comp_on ? "COMP" : "OFF");
            hint = "PADS: EQ, COMP  SCALE: MORE";
            h = hash_mem(hash_add(hash_add(61u, mst_st.eq_on + 2u * mst_st.comp_on), mls.focus), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, b, r, C_MUT); }
            h = hash_add(hash_add(62u, mst_st.eq_on + 2u * mst_st.comp_on), (uint32_t)mst_gr10()) | 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_mst_head(); }
            mst_cells(&top, MSP_LO, 4u, mst_st.eq_on && !ui.shift);
            mst_cells(&sel, MSP_THR, 4u, mst_st.comp_on && !ui.shift);
            mst_cells(&more, MSP_LFREQ, 3u, ui.shift);
            h = hash_mem(63u, &top, sizeof top) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(52, &top); }
            h = hash_mem(64u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(88, &sel); }
            h = hash_mem(65u, &more, sizeof more) | 1u;
            if (h != drawn[R_INFO]) { drawn[R_INFO] = h; draw_cells(124, &more); }
        } else if (ui.mode == MODE_FX && ui.fxp == FXP_EFFECT) {   /* EFFECT: the insert, its eight values, its manual */
            static cells_t where;
            fx_btnmap(&bm);
            cat(b, "EFFECT");
            cat(r, FXI_NAME[fxi_st.type]);
            hint = "PADS 1-7: EFFECT 9-11: MANUAL";
            h = hash_mem(hash_add(hash_add(51u, fxi_st.type), mls.focus), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, b, r, C_MUT); }
            h = hash_add(52u, fxi_st.type) | 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_fxi_types(); }
            fxi_cells(&top, 0);
            fxi_cells(&sel, 4);
            memset(&where, 0, sizeof where);
            where.lit = fxi_st.type != FXI_OFF;
            cells_set(&where, 0, "MANUAL", MAN_TAG[fxi_st.man < ORG_NMAN ? fxi_st.man : 0u]);
            cells_set(&where, 1, "EFFECT", FXI_TAG[fxi_st.type]);
            h = hash_mem(53u, &top, sizeof top) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(52, &top); }
            h = hash_mem(54u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(88, &sel); }
            h = hash_mem(55u, &where, sizeof where) | 1u;
            if (h != drawn[R_INFO]) { drawn[R_INFO] = h; draw_cells(124, &where); }
        } else if (ui.mode == MODE_FX && ui.fxp == FXP_LESLIE) {   /* ROTARY: the cabinets, its twelve values */
            static cells_t more;
            fx_btnmap(&bm);
            cat(b, "ROTARY");
            cat(r, LES_TNAME[les_st.type]);
            hint = les_st.type >= LES_U1 ? "PADS 1-4: CABINET SCALE: MORE" : "BT + PAD 3/4: COPY TO USER";
            h = hash_mem(hash_add(hash_add(41u, les_st.type), mls.focus), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, b, r, C_MUT); }
            h = hash_add(42u, les_st.type) | 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_les_types(); }
            les_cells(&top, 0);
            les_cells(&sel, 4);
            les_cells(&more, LESP_HDOP);
            if (ui.shift) { top.lit = 0; sel.lit = 0; } else more.lit = 0;   /* (what the knobs hold, lit) */
            if (les_st.type < LES_U1) top.lit = sel.lit = more.lit = 0;
            h = hash_mem(43u, &top, sizeof top) | 1u;
            if (h != drawn[R_ROW1]) { drawn[R_ROW1] = h; draw_cells(52, &top); }
            h = hash_mem(44u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(88, &sel); }
            h = hash_mem(45u, &more, sizeof more) | 1u;
            if (h != drawn[R_INFO]) { drawn[R_INFO] = h; draw_cells(124, &more); }
        } else if (ui.mode == MODE_FX) {            /* FX: K1..K4 the manual's, K5..K8 the shared ones */
            fx_btnmap(&bm);
            cat(b, "FX");
            cat(r, LESLIE_NAME[org_par.leslie]);
            hint = "PADS 1-3: MANUAL  4: ROTARY";
            h = hash_mem(hash_add(hash_add(5u, 2u * org_par.leslie), mls.focus), tag, sizeof tag) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_TXT, b, r, C_MUT); }
            h = hash_add(hash_add(6u, dept_now()), mls.focus);
            for (m = 0; m < ORG_NMAN; m++)
                h = hash_add(h, org_par.drive[m] | org_par.vib[m] << 7 | org_par.trem[m] << 10 | org_par.les_on[m] << 17 |
                                org_par.dsend[m] << 18 | (uint32_t)org_par.rsend[m] << 25);
            h |= 1u;
            if (h != drawn[R_VIS]) { drawn[R_VIS] = h; draw_fx_grid(); }
            fx_cells(&sel);
            h = hash_mem(7u, &sel, sizeof sel) | 1u;
            if (h != drawn[R_ROW2]) { drawn[R_ROW2] = h; draw_cells(124, &sel); }
        } else if (ui.mode == MODE_MIX) {
            uint32_t ch;
            mix_btnmap(&bm);
            cat(tag, "MIDI");
            cat(b, "MIXER");
            cat(r, MXP_TAG[mx.par]);
            hint = "K-BANK: 1-8/9-16  < >: PARAM";
            h = hash_add(hash_add(15u, mx.par), mls.focus) | 1u;
            if (h != drawn[R_TOP]) { drawn[R_TOP] = h; draw_top_bar(tag, MAN_BADGE[mls.focus], C_ACC, b, r, C_MUT); }
            h = hash_add(hash_add(hash_add(16u, mx.page), mx.par), ml_used_channels());
            h = hash_add(hash_add(h, mx.off | (uint32_t)mx.solo << 16), yst.gen);
            for (ch = 0; ch < 16u; ch++) h = hash_add(h, (uint32_t)(mx_get(ch, mx.par) + 1) | mx_muted(ch) << 8);
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
        if (ui.mode == MODE_MIDI && ui.ms == MS_ZONE) {   /* ZONE: the pads' switches */
            h = padmap_hash();
            if (h != drawn[R_BTN]) { drawn[R_BTN] = h; draw_padmap(); }
        } else {
            h = hash_mem(14u, &bm, sizeof bm) | 1u;
            if (h != drawn[R_BTN]) { drawn[R_BTN] = h; draw_btnmap(&bm); }
        }
    }
}

/* =========================================================== the loop */
static board_keys_t ui_prev_keys;
#include "orgigs_intro.c"                   /* the welcome screen (the Elite's own: the logo, the pads' wave) */

static void ui_init(void)
{
    ctl_init();
    ctl_nperc = ORG_NPERC_ALL;          /* (the SMK-37: with 4TH) */
    yst_reset();
    les_init();
    par_init();
    pnl_init();
    whl_init();
    smk_pad_events = 1;                         /* the pads are the SMK panel's (board/smk37/board.c), no notes */
#if !defined(ORGIGS_NO_INTRO) && !defined(ORGIGS_SELFTEST) && !defined(ORGIGS_TONETEST)
    intro_start();
#endif
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
    memset(lv, 0, sizeof lv);
    if (midi) {
        static btnmap_t bm;
        midi_btnmap(&bm);
        for (b = 0; b < 12u; b++)
            if (BTN_AT[b] == BTN_ENV || BTN_AT[b] == BTN_LFO || BTN_AT[b] == BTN_EDIT || BTN_AT[b] == BTN_ARP || BTN_AT[b] == BTN_SEQ)
                lv[BTN_AT[b]] = bm.on[b] ? 2u : 0u;
    } else if (ui.mode == MODE_FX) {            /* the manuals' buttons glow, the focused one is lit */
        lv[BTN_ENV] = mls.focus == MAN_UPPER ? 2u : 1u;
        lv[BTN_LFO] = mls.focus == MAN_LOWER ? 2u : 1u;
        lv[BTN_ARP] = mls.focus == MAN_PEDAL ? 2u : 1u;
        lv[BTN_SEQ] = org_par.les_on[mls.focus] ? 2u : 0u;
    } else if (ui.mode == MODE_PLAY) {
        uint32_t m = mls.focus;
        lv[BTN_ENV] = ui.grp == GRP_DB14 ? 2u : 0u;
        lv[BTN_LFO] = ui.grp == GRP_DB58 ? 2u : 0u;
        lv[BTN_EDIT] = ui.grp == GRP_DB9 ? 2u : 0u;
        lv[BTN_ARP] = (dept_now() == DEPT_VINTAGE ? org_par.vib[m] != 0u : org_par.trem[m] != 0u) ? 2u : 0u;
        lv[BTN_SEQ] = cpl_idx() ? 2u : 0u;
    }
    lv[BTN_FX] = ui.mode == MODE_FX ? 2u : 0u;
    lv[BTN_GLO] = midi || ui.mode == MODE_LIGHT ? 2u : 0u;
    lv[BTN_HOME] = ui.mode == MODE_MIX ? 2u : 0u;
    lv[BTN_PLAY] = org_par.leslie == LESLIE_BRAKE ? 2u : 0u;
    lv[BTN_REC] = org_par.leslie == LESLIE_FAST ? 2u : 0u;
    lv[BTN_SEL] = ui.shift ? 2u : 0u;
    lv[BTN_SAVE] = save ? 2u : 0u;
    smk_leds(lv);
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
    b = smk_buttons(b);                         /* the SMK-37's own: drawbar 9, the manual, the list, the model */
    if (b & (1u << BTN_OCT_DOWN | 1u << BTN_OCT_UP)) ctl_octave(b >> BTN_OCT_DOWN & 1u, b >> BTN_OCT_UP & 1u);
    if (b & 1u << BTN_SAVE && !ui.save_used) ctl_say("TAP: DRAWBARS  +PAD: SETUP", 0);
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
    if (b & 1u << BTN_FX) set_mode(ui.mode == MODE_FX ? MODE_PLAY : MODE_FX);
    if (b & 1u << BTN_GLO) {
        if (ui.shift) set_mode(ui.mode == MODE_LIGHT ? MODE_PLAY : MODE_LIGHT);
        else if (midi && ui.ms == MS_YST && ysb.on) ysb_open(0);   /* (the sound browser: back to YST) */
        else set_mode(midi ? MODE_PLAY : MODE_MIDI);   /* (the Elite: again = back; ARP / PATCH the screens) */
        if (ui.mode == MODE_MIDI && ui.ms == MS_YST) yst_follow_zone();
    }
    if (b & 1u << BTN_HOME) {
        if (ui.shift) { ml_panic(); ctl_say("PANIC", "ALL NOTES OFF"); }
        else if (ui.mode == MODE_PLAY) set_mode(MODE_MIX);
        else set_mode(MODE_PLAY);
    }
    if (ui.mode == MODE_MIX) {                  /* (the Elite's mixer: KNOB BANK, < >, the pads: board.c) */
        b &= ~(1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_EDIT | 1u << BTN_ARP | 1u << BTN_SEQ);
    } else if (ui.mode == MODE_FX) {            /* ENV LFO ARP: the manual's row; SEQ: its rotary */
        if (b & 1u << BTN_ENV) focus_set(MAN_UPPER);
        if (b & 1u << BTN_LFO) focus_set(MAN_LOWER);
        if (b & 1u << BTN_ARP) focus_set(MAN_PEDAL);
        if (b & 1u << BTN_SEQ) les_toggle();
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
        else cpl_step();
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
    uint32_t b, rel, k;
    int32_t st;
    uint8_t ms, m1, m2;
    int src;

    /* MIDI in, then the panel */
    while ((src = midi_get(&ms, &m1, &m2)) != 0) if (!yst_midi_in(ms, m1, m2, src == 1)) ml_midi_in(ms, m1, m2);   /* (the SEQTRAK: YST) */
    yst_tick();
    if (bat_tick()) drawn[R_TOP] = 0;
    if (intro_step()) { ml_flush(); return; }

    if (!ui.shift && (fm1_in.buttons >> BTN_SEL & 1u)) {     /* SEL pressed: the setups, the audio interrupt's load */
        char s[48], *p = cat(s, "PED ");                     /* and the battery, the pedal's jack (raw) first: the */
        p = org_fmt(p, (int32_t)fm1_in.pedal, 0);            /* line holds about 30 characters */
        p = org_fmt(cat(p, " DSP "), (int32_t)(org_stat.load_us * 100u / 5805u), 0);
        bat_fmt(cat(p, "% BAT "));
        ctl_say(s, 0);
    }
    ui.shift = fm1_in.buttons >> BTN_SEL & 1u;
    b = fm1_input_edges(&rel);
    if (b & 1u << BTN_SAVE) ui.save_used = 0;   /* (BT + a pad stores a setup: board.c) */
    keys = fm1_in.keys;                         /* all 37 (bit 0 = C3); a tap shorter than a round: smk_key_taps */
    down = (keys & ~ui_prev_keys) | smk_key_taps();
    up = ui_prev_keys & ~keys;
    ui_prev_keys = keys;
    for (k = 0; k < BOARD_NKEYS; k++) {
        if (down >> k & 1u) {
            ml_local_key_vel(k, BOARD_KEY0 + (int32_t)k + 12 * ctl.oct, 1, fm1_in.key_vel[k]);   /* (they only play) */
        }
        if (!(keys >> k & 1u) && ((up | down) >> k & 1u) && mls.src[k] != SRC_NONE) ml_local_key(k, 0, 0);
    }

    ui_buttons(b, rel);

    /* encoders (the SMK-37: its pads, faders and wheels first; on the drawbars its 8 knobs are drawbars 1..8) */
    if (!smk_controls())
    {
        for (k = 0; k < 4u; k++)
            if ((st = fm1_enc_take(ENC_KNOB1 + k)) != 0) knob(k, st);
        uint32_t yst_on = ui.mode == MODE_MIDI && ui.ms == MS_YST;   /* YST: SELECT the track, PRESETS its sound */
        if ((st = fm1_enc_take(ENC_SELECT)) != 0) {
            if (yst_on) yst_select((uint32_t)clampi((int32_t)yst.cur + st, 0, YST_NTRK - 1));
            else focus_set((uint32_t)clampi((int32_t)mls.focus + st, 0, 2));
        }
        if ((st = fm1_enc_take(ENC_ALGORITHM)) != 0) set_model((int32_t)org_par.model + st);
        if ((st = fm1_enc_take(ENC_PRESETS)) != 0) { if (yst_on) yst_browse_kit(st); else ctl_preset_step(st); }
    }

    ml_drawbars_tick();
    ml_flush();
    ui_leds();
    bk_tick();
    ui_draw();
    ctl_save_tick();
}

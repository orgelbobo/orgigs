/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: the effects (run in the audio interrupt, after the generators). Included by orgigs_dsp.c.
 *
 * Per manual (each division's bus, as a mixer's channel strip):
 *   1  the model's body: 4.5 kHz cabinet (combo organs), a clean limiter (pipes), or the wooden cassotto
 *      + bellows brightness (free reeds); then the DRIVE: an asymmetric tube soft clip (combo organs: the
 *      preamp itself; pipes and reeds: blended in as the knob opens)
 *   2  scanner vibrato / chorus V1-V3 C1-C3 (combo organs), tremulant 4.8 Hz (pipes, reeds)
 *   then the bus goes to the rotary or straight out, and sends to the delay and the reverb.
 * Shared (as a mixer's send effects):
 *   3  rotary speaker (les_rt: the cabinet; the CLASSIC below): 800 Hz crossover, horn and drum turning against each other with their own inertia
 *      (~0.8 s and ~4.2 s), Doppler delay lines and amplitude modulation, two microphones for stereo
 *   4  stereo ping-pong delay up to 1 s, 3.2 kHz damping in the feedback (kept at half rate)
 *   5  reverb: spring, stage (0.9 s), hall (2.8 s), cathedral (7.5 s, 45 ms pre-delay); a 4-line FDN at half rate
 * A bus that has been silent for 24 blocks (17 ms, longer than its vibrato line) is skipped.
 *
 * The FM-1 has no SDRAM: all delay memory lives in the 336 KB pool (int16), about 160 KB of it here. */

#define VIB_LEN 512u
#define ROT_LEN 1024u
#define DLY_LEN 22050u                 /* 1 s at 22.05 kHz */
#define AP_N 4u
#define SP_N 6u
static int16_t vib_buf[ORG_NMAN][VIB_LEN] ORG_POOL;
static int16_t horn_buf[ROT_LEN] ORG_POOL, drum_buf[ROT_LEN] ORG_POOL;
static int16_t dly_l[DLY_LEN] ORG_POOL, dly_r[DLY_LEN] ORG_POOL;
/* the reverb's delays are power-of-two rings on one write counter (fx.rv_t): a read is (t - d) & mask */
#define RV_LINE 4096u                  /* > REV_LINE_MAX3 */
#define RV_PRE 1024u                   /* > REV_PRE_MAX */
typedef char rv_fits[REV_LINE_MAX0 < RV_LINE && REV_LINE_MAX1 < RV_LINE && REV_LINE_MAX2 < RV_LINE &&
                     REV_LINE_MAX3 < RV_LINE && REV_PRE_MAX <= RV_PRE ? 1 : -1];
static int16_t rv_line[4][RV_LINE] ORG_POOL;
static int16_t rv_pre[RV_PRE] ORG_POOL;
static int16_t rv_ap[AP_N][256] ORG_POOL;        /* diffusion: 71, 53, 191, 139 samples (half rate) */
static int16_t rv_sp[SP_N][32] ORG_POOL;         /* the spring's dispersion: 7 .. 29 */

/* the rotary speaker's cabinet: what the audio reads (the Elite chooses and edits it, board/smk37; the FM-1 keeps
   these, the CLASSIC): rotor speeds (slow, fast: horn, drum), their glide per block (Q16), the crossover (one pole, Q15),
   horn / drum levels (Q14), Doppler swing (samples), level swing (Q15 of the sine), stereo width (64 = as miked) */
typedef struct {
    uint32_t inc[4];
    int32_t h_glide, d_glide, xo_a, h_gain, d_gain, h_dop, d_dop, h_am, d_am, width;
} les_rt_t;
static les_rt_t les_rt = {{INC_HORN_SLOW, INC_HORN_FAST, INC_DRUM_SLOW, INC_DRUM_FAST},
                          176, 48, LP_XOVER_A, 16384, 16384, 14, 6, 8192, 11264, 64};

/* the insert slot (the Elite's EFFECT page; the FM-1 leaves it off): one effect on one manual's bus, after its
   vibrato, before the rotary and the sends; a chorus's stereo side goes straight to the output. What the audio reads:
   the effect, its manual, and its values worked out (board/smk37) */
enum { FXI_OFF, FXI_CHORUS, FXI_WAH, FXI_AUTOWAH, FXI_FLANGER, FXI_PHASER, FXI_RING, FXI_AMP, FXI_N };
typedef struct {
    uint32_t type, man;
    uint32_t rate_inc;                  /* the LFO, per sample */
    int32_t base_q8, swing_q8;          /* the modulated delay: its middle and swing, 1/256 samples */
    int32_t mix, width, fb;             /* wet added (chorus) or share (wah), stereo side, feedback: Q7 (128 = all) */
    int32_t wah_f, wah_q, level;        /* WAH: the filter's frequency, its damping 1 / resonance (Q12), the level (Q7) */
    int32_t aw_lo, aw_hi, aw_sens, aw_rel;   /* AUTO-WAH: the range (log2 Hz, Q12), sensitivity, release (Q16 a block) */
    uint32_t ph_stages;                 /* PHASER: its all-pass stages (the range in aw_lo, aw_hi) */
    uint32_t rm_inc, rm_swing;          /* RING: the carrier per sample; its LFO swing (rate_inc) */
    uint32_t amp_model;                 /* AMP: CLEAN CRUNCH LEAD FUZZ */
    int32_t amp_gain, amp_tone, amp_cab;   /* the gain (Q8), the tilt (-10 .. 10), the box's low-pass (Q15, 0: none) */
} fxi_rt_t;
static fxi_rt_t fxi_rt;                 /* (type FXI_OFF: off) */
#define FXI_LEN 2048u                  /* 46 ms */
static int16_t fxi_buf[FXI_LEN] ORG_POOL;
static struct {
    uint32_t w, ph, hold;               /* hold: blocks it still runs on a silent bus (its tail) */
    int32_t wf, lp, bp;                 /* WAH: the frequency gliding to wah_f; the filter */
    int32_t env;                        /* AUTO-WAH: the bus's level followed */
    int32_t ap_x[2][8], ap_y[2][8], ap_out[2];   /* PHASER: two chains (left, right of the side), their last outputs */
    uint32_t cph;                       /* RING: the carrier's phase */
    int32_t a_pre, a_tone, a_hp, a_lp1, a_lp2;   /* AMP: its filters */
    int32_t side[32];                   /* (SUB) */
} fxi;

/* the master (the Elite's MASTER page; the FM-1 leaves it off): a three-band EQ and a stereo compressor on the sum,
   after the delay and the reverb, before the expression. EQ: the band below a one-pole (low shelf), a band-pass
   (Chamberlin, Q 1) and the band above a one-pole (high shelf), each added at its gain. Compressor: the block's peak
   (both sides), in octaves over the threshold, cut by (1 - 1 / ratio); the gain glides (attack, release) and is
   ramped across the next block */
typedef struct {
    uint32_t eq_on, comp_on;
    int32_t g_lo, g_mid, g_hi;          /* the bands' gains, Q12 */
    int32_t a_lo, a_hi, f_mid;          /* the shelves' one-poles (Q15), the band-pass's frequency (Q12) */
    int32_t thr, slope;                 /* the threshold (log2 of the level, Q12), 1 - 1 / ratio (Q12) */
    int32_t att, rel, makeup;           /* the gain's glide down, up (Q16 a block), the make-up gain (Q12) */
} mst_rt_t;
static mst_rt_t mst_rt;
static struct {
    int32_t lo[2], hi[2], svl[2], svb[2];   /* the EQ's filters, left and right */
    int32_t g, g_prev, pk;              /* the compressor's gain (Q12, 4096 = none) now and a block ago; the peak */
} mst = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, 4096, 4096, 0};
static volatile int32_t mst_gain;       /* (the screen: the compressor's gain, Q12) */

static struct {                        /* a bus: stages 1 and 2 */
    int32_t cab_lp, dc_m, bright_lp, bq_x1, bq_x2, bq_y1, bq_y2;
    uint32_t vib_w;
    uint32_t idle;                      /* blocks without input */
} fxb[ORG_NMAN];
static struct {
    /* stage 2: the LFOs, shared (one scanner, one wind) */
    uint32_t vib_ph, trem_ph;
    /* stage 3 */
    int32_t xo_lp;
    uint32_t horn_ph, drum_ph, horn_inc, drum_inc, rot_w;
    /* stage 4 */
    uint32_t dly_w, dly_half;
    uint32_t dly_hold;                  /* blocks the delay still runs: its echoes on their way, or still heard */
    int32_t dly_in_prev, dly_lp_l, dly_lp_r, dly_ol, dly_or, dly_pl, dly_pr;
    /* stage 5 */
    uint32_t rv_t;
    int32_t rv_lp[4];
    uint32_t rv_half;
    int32_t rv_in_prev, rv_ol, rv_or, rv_pl, rv_pr;     /* half rate: the input before, the outputs */
    uint8_t rv_mode;
    /* output */
    int32_t expr_q12;
} fx;

#ifdef ORG_LEVEL_DEBUG
static int32_t lv_dbg[3];               /* host: peaks of dry, after the preamp, before the last clip */
#endif
/* gain staging, Q8 (the generators sum up to ~80000 for full chords on both manuals and the pedal) */
#ifndef G_IN_VINTAGE
#define G_IN_VINTAGE 102               /* 0.4 into the tube: the drive knob still decides how hard it bites */
#define G_POST_VINTAGE 135             /* 0.53: the tube's full swing leaves room for rotary and reverb */
#define G_IN_PIPE 80
#define G_POST_PIPE 256
#define G_IN_REED 102
#define G_POST_REED 192
#endif
static const uint16_t TRIM_VINTAGE_Q8[4] = {256, 448, 371, 282};   /* tonewheel, Brit Combo, Italo Combo, Tokyo Combo: their waves' crest */
static int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }

static inline __attribute__((always_inline)) int32_t softclip(int32_t x)              /* linear to 20000, then bends towards 32767 */
{
    int32_t e;
    if (x > 20000) { e = x - 20000; if (e > 150000) e = 150000; return 20000 + (e * 12767) / (e + 12767); }
    if (x < -20000) { e = -20000 - x; if (e > 150000) e = 150000; return -20000 - (e * 12767) / (e + 12767); }
    return x;
}

static int32_t tube(int32_t x, int32_t gain_q8)  /* asymmetric tanh, table over [-4, 4) */
{
    int32_t v = (x * gain_q8) >> 8, idx, fr;
    if (v > 131000) v = 131000;
    if (v < -131000) v = -131000;
    v += 131072;
    idx = v >> 8;
    fr = v & 255;
    return TANH_Q15[idx] + (((TANH_Q15[idx + 1] - TANH_Q15[idx]) * fr) >> 8);
}

static int32_t rd_frac(const int16_t *buf, uint32_t mask, uint32_t w, int32_t d_q8)   /* d in 1/256 samples */
{
    uint32_t di = (uint32_t)d_q8 >> 8, fr = (uint32_t)d_q8 & 255u;
    int32_t a = buf[(w - di) & mask], b = buf[(w - di - 1u) & mask];
    return a + (((b - a) * (int32_t)fr) >> 8);
}

/* the chorus: two taps on a modulated delay, their LFOs half a turn apart; their mean is the wet signal, added to the
   dry (mono, before the rotary; trimmed so the level stays), half their difference the side (stereo, after it) */
static void fxi_chorus(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    int32_t s0 = org_sin(fxi.ph), s1, i;
    fxi.ph += c->rate_inc * n;
    s1 = org_sin(fxi.ph);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t sn = s0 + (((s1 - s0) * i) >> 5), dry = x[i], v1, v2, wet;
        int32_t sw = (sn * (c->swing_q8 >> 4)) >> 11;           /* (swing up to ~2^16: no overflow) */
        v1 = rd_frac(fxi_buf, FXI_LEN - 1u, fxi.w - 1u, c->base_q8 + sw);
        v2 = rd_frac(fxi_buf, FXI_LEN - 1u, fxi.w - 1u, c->base_q8 - sw);
        fxi_buf[fxi.w & (FXI_LEN - 1u)] = sat16(dry + ((v1 * c->fb) >> 7));
        fxi.w++;
        wet = (v1 + v2) >> 1;
        x[i] = ((dry + ((wet * c->mix) >> 7)) * (128 - ((c->mix * 38) >> 7))) >> 7;   /* added, the level kept */
        fxi.side[i] = ((((v1 - v2) >> 1) * c->width) >> 7) * c->mix >> 7;
    }
}

/* the flanger: one tap on a short modulated delay, fed back (+ or -), added to the dry: the sweeping comb; a second
   tap a quarter turn on gives the side (stereo, after the rotary) */
static void fxi_flanger(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    int32_t s0 = org_sin(fxi.ph), c0 = org_sin(fxi.ph + 0x40000000u), s1, c1, i;
    fxi.ph += c->rate_inc * n;
    s1 = org_sin(fxi.ph); c1 = org_sin(fxi.ph + 0x40000000u);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t sn = s0 + (((s1 - s0) * i) >> 5), cn = c0 + (((c1 - c0) * i) >> 5), dry = x[i], v1, v2;
        v1 = rd_frac(fxi_buf, FXI_LEN - 1u, fxi.w - 1u, c->base_q8 + ((sn * (c->swing_q8 >> 4)) >> 11));
        v2 = rd_frac(fxi_buf, FXI_LEN - 1u, fxi.w - 1u, c->base_q8 + ((cn * (c->swing_q8 >> 4)) >> 11));
        fxi_buf[fxi.w & (FXI_LEN - 1u)] = sat16(dry + ((v1 * c->fb) >> 7));
        fxi.w++;
        x[i] = ((dry + ((v1 * c->mix) >> 7)) * (128 - ((c->mix * 38) >> 7))) >> 7;   /* added, the level kept */
        fxi.side[i] = ((((v1 - v2) >> 1) * c->width) >> 7) * c->mix >> 7;
    }
}

/* the phaser: two chains of first-order all-passes (2 .. 8), their corner swept by an LFO from LOW to HIGH (even in
   octaves), the second chain a quarter turn on; chain one fed back (+ or -) and added to the dry: the moving notches;
   half the chains' difference the side. Placed after the log2 helpers (it needs 2^x) */
static uint32_t iexp2_q12(int32_t y);
static int32_t ap_coef(const fxi_rt_t *c, int32_t sn)     /* the all-pass coefficient (Q13) at LFO value sn */
{
    int32_t pos = (sn + 32767) >> 4;                        /* 0 .. 4095 */
    int32_t w = (int32_t)((iexp2_q12(c->aw_lo + (((c->aw_hi - c->aw_lo) * pos) >> 12)) * 2390u) >> 10);   /* pi fc / fs, Q15 */
    if (w > 30000) w = 30000;
    return ((w - 32768) * 8192) / (32768 + w);              /* (tan - 1) / (tan + 1): the corner at fc */
}
static void fxi_phaser(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    int32_t a[2], i, k, ch;
    uint32_t ns = c->ph_stages > 8u ? 8u : c->ph_stages;
    a[0] = ap_coef(c, org_sin(fxi.ph));
    a[1] = ap_coef(c, org_sin(fxi.ph + 0x40000000u));
    fxi.ph += c->rate_inc * n;
    for (i = 0; i < (int32_t)n; i++) {
        int32_t dry = x[i], w[2];
        for (ch = 0; ch < 2; ch++) {
            int32_t v = dry + ((fxi.ap_out[ch] * c->fb) >> 7);
            if (v > 60000) v = 60000;
            if (v < -60000) v = -60000;
            for (k = 0; k < (int32_t)ns; k++) {                 /* y = a (x - y1) + x1 */
                int32_t y = ((a[ch] * (v - fxi.ap_y[ch][k])) >> 13) + fxi.ap_x[ch][k];
                if (y > 120000) y = 120000;                     /* (a stage's swing: the product stays < 2^31) */
                if (y < -120000) y = -120000;
                fxi.ap_x[ch][k] = v; fxi.ap_y[ch][k] = y; v = y;
            }
            fxi.ap_out[ch] = w[ch] = v;
        }
        x[i] = ((dry + ((w[0] * c->mix) >> 7)) * (128 - ((c->mix * 38) >> 7))) >> 7;   /* added, the level kept */
        fxi.side[i] = ((((w[0] - w[1]) >> 1) * c->width) >> 7) * c->mix >> 7;
    }
}

/* the ring modulator: the bus times a sine carrier (its frequency wobbled by an LFO), crossfaded with the dry; the
   same times the carrier a quarter turn on gives the side */
static void fxi_ring(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    uint32_t i, inc = c->rm_inc;
    if (c->rate_inc) {
        int32_t l = org_sin(fxi.ph);
        fxi.ph += c->rate_inc * n;
        inc = (uint32_t)((int32_t)inc + (int32_t)(c->rm_swing >> 8) * (l >> 7));
    }
    for (i = 0; i < n; i++) {
        int32_t dry = x[i], s1 = org_sin(fxi.cph), s2 = org_sin(fxi.cph + 0x40000000u), w1, w2;
        fxi.cph += inc;
        w1 = (int32_t)(((int64_t)dry * s1) >> 15);
        w2 = (int32_t)(((int64_t)dry * s2) >> 15);
        x[i] = dry + (((w1 - dry) * c->mix) >> 7);
        fxi.side[i] = ((((w1 - w2) >> 1) * c->width) >> 7) * c->mix >> 7;
    }
}

/* the amp: (CRUNCH, LEAD: the highs lifted before) the tube's curve (LEAD: twice; FUZZ: a hard clip), a tilt
   around 800 Hz, the box (90 Hz high-pass, a two-pole low-pass), the level, crossfaded with the dry */
static void fxi_amp(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t dry = x[i], y = dry, lo;
        if (c->amp_model == 1u || c->amp_model == 2u) {        /* the highs lifted into the tube: bite */
            fxi.a_pre += ((y - fxi.a_pre) * 3110) >> 15;        /* 700 Hz */
            y += (y - fxi.a_pre) >> 1;
        }
        if (y > 120000) y = 120000;                             /* (times the gain: < 2^31) */
        if (y < -120000) y = -120000;
        if (c->amp_model == 3u) {                               /* FUZZ: hard */
            y = (y * c->amp_gain) >> 8;
            if (y > 14000) y = 14000;
            if (y < -14000) y = -14000;
            y *= 2;
        } else {
            y = tube(y, c->amp_gain);
            if (c->amp_model == 2u) y = tube(y, 512);           /* LEAD: a second stage */
        }
        if (c->amp_tone) {                                      /* the tilt: dark .. bright */
            fxi.a_tone += ((y - fxi.a_tone) * 3530) >> 15;      /* 800 Hz */
            lo = fxi.a_tone;
            y = (lo * (10 - c->amp_tone) + (y - lo) * (10 + c->amp_tone)) / 10;
        }
        if (c->amp_cab) {                                       /* the box */
            fxi.a_hp += ((y - fxi.a_hp) * 417) >> 15;           /* 90 Hz */
            y -= fxi.a_hp;
            fxi.a_lp1 += ((y - fxi.a_lp1) * c->amp_cab) >> 15;
            fxi.a_lp2 += ((fxi.a_lp1 - fxi.a_lp2) * c->amp_cab) >> 15;
            y = fxi.a_lp2;
        }
        y = (y * c->level) >> 7;
        x[i] = dry + (((y - dry) * c->mix) >> 7);
    }
}

/* log2 and 2^x in Q12 (the wah's sweep is even in octaves) */
static const uint16_t LOG2T[17] = {0, 358, 696, 1016, 1319, 1607, 1882, 2145, 2396, 2637, 2869, 3092, 3307, 3514, 3715, 3908, 4096};
static const uint16_t EXP2T[17] = {16384, 17109, 17867, 18658, 19484, 20347, 21247, 22188, 23170, 24196, 25268, 26386,
                                   27554, 28774, 30048, 31379, 32768};
static int32_t ilog2_q12(uint32_t x)    /* x >= 1 */
{
    uint32_t n = 0, m, i, fr;
    while (x >> (n + 1u)) n++;
    m = n >= 16u ? x >> (n - 16u) : x << (16u - n);     /* 65536 .. 131071 */
    i = (m - 65536u) >> 12; fr = (m - 65536u) & 4095u;
    return (int32_t)(n * 4096u + LOG2T[i] + (((LOG2T[i + 1u] - LOG2T[i]) * fr) >> 12));
}
static uint32_t iexp2_q12(int32_t y)    /* y >= 0, the result < 2^17 */
{
    uint32_t n = (uint32_t)y >> 12, f = (uint32_t)y & 4095u, i = f >> 8, fr = f & 255u;
    uint32_t mant = EXP2T[i] + (((EXP2T[i + 1u] - EXP2T[i]) * fr) >> 8);
    return n >= 14u ? mant << (n - 14u) : mant >> (14u - n);
}
static int32_t wah_f_of(uint32_t fc) { return (int32_t)((fc * 598u) >> 10); }   /* 2 pi fc / 44100, Q12 */

/* the wah: a resonant band-pass (Chamberlin state variable) swept by the pedal (the mod wheel, or K1); its peak lifted
   by the resonance, as a pedal's, then the level; the frequency glides a quarter of the way each block (no zipper) */
static void fxi_svf(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t dry = x[i], hp, wet;
        fxi.lp += (fxi.wf * fxi.bp) >> 12;
        hp = dry - fxi.lp - ((c->wah_q * fxi.bp) >> 12);
        fxi.bp += (fxi.wf * hp) >> 12;
        if (fxi.bp > 400000) fxi.bp = 400000;
        if (fxi.bp < -400000) fxi.bp = -400000;
        if (fxi.lp > 400000) fxi.lp = 400000;
        if (fxi.lp < -400000) fxi.lp = -400000;
        wet = (fxi.bp * c->level) >> 7;                     /* (the peak: resonance x level) */
        x[i] = dry + (((wet - dry) * c->mix) >> 7);
    }
}
static void fxi_wah(int32_t *x, uint32_t n)
{
    fxi.wf += (fxi_rt.wah_f - fxi.wf) >> 2;
    fxi_svf(x, n);
}
/* the auto-wah: the same filter, opened by the bus's level (quick up, down in its decay) and, with a rate, an LFO */
static void fxi_autowah(int32_t *x, uint32_t n)
{
    const fxi_rt_t *c = &fxi_rt;
    uint32_t i, sum = 0;
    int32_t m, pos;
    for (i = 0; i < n; i++) sum += (uint32_t)(x[i] < 0 ? -x[i] : x[i]);
    m = (int32_t)(sum >> 5);                                    /* (n = SUB = 32) */
    if (m > 200000) m = 200000;
    if (m > fxi.env) fxi.env += (m - fxi.env) >> 1;
    else fxi.env -= ((fxi.env - m) * c->aw_rel) >> 16;     /* (the level < 2^18, aw_rel < 2^12) */
    pos = fxi.env * c->aw_sens >> 6;
    if (c->rate_inc) { fxi.ph += c->rate_inc * n; pos += (org_sin(fxi.ph) + 32767) >> 4; }
    if (pos > 4096) pos = 4096;
    fxi.wf += (wah_f_of(iexp2_q12(c->aw_lo + (((c->aw_hi - c->aw_lo) * pos) >> 12))) - fxi.wf) >> 1;
    fxi_svf(x, n);
}

static void fx_init(void)
{
    fx.rv_mode = 0xFFu;
    fx.expr_q12 = 4096;
}

static void rev_clear(uint32_t mode)
{
    uint32_t i, j;
    for (j = 0; j < 4u; j++) for (i = 0; i < RV_LINE; i++) rv_line[j][i] = 0;
    for (i = 0; i < RV_PRE; i++) rv_pre[i] = 0;
    for (j = 0; j < AP_N; j++) for (i = 0; i < 256u; i++) rv_ap[j][i] = 0;
    for (j = 0; j < SP_N; j++) for (i = 0; i < 32u; i++) rv_sp[j][i] = 0;
    fx.rv_ol = fx.rv_or = fx.rv_pl = fx.rv_pr = fx.rv_in_prev = 0;
    for (j = 0; j < 4u; j++) fx.rv_lp[j] = 0;
    fx.rv_t = 0;
    fx.rv_mode = (uint8_t)mode;
}

static void fx_panic(void)              /* interrupt: delay and reverb silent at once */
{
    uint32_t i, m;
    for (i = 0; i < DLY_LEN; i++) { dly_l[i] = 0; dly_r[i] = 0; }
    fx.dly_in_prev = fx.dly_lp_l = fx.dly_lp_r = fx.dly_ol = fx.dly_or = fx.dly_pl = fx.dly_pr = 0;
    fx.dly_hold = 0;
    for (m = 0; m < ORG_NMAN; m++) for (i = 0; i < VIB_LEN; i++) vib_buf[m][i] = 0;
    for (i = 0; i < ROT_LEN; i++) { horn_buf[i] = 0; drum_buf[i] = 0; }
    rev_clear(fx.rv_mode < ORG_NREV ? fx.rv_mode : 0u);
}

static inline __attribute__((always_inline)) int32_t allpass(int16_t *buf, uint32_t mask, uint32_t t, uint32_t len,
                                                             int32_t x, int32_t g_q15)
{
    int32_t d = buf[(t - len) & mask], y = d - ((g_q15 * x + 16384) >> 15);     /* rounded: no DC creeps in */
    buf[t & mask] = sat16(x + ((g_q15 * y + 16384) >> 15));
    return y;
}

/* stages 1 and 2 of bus m, in place; 0 = the bus is silent and was skipped */
static uint32_t fx_bus(uint32_t m, int32_t *x, uint32_t n, const int32_t *lfo_v, const int32_t *lfo_t, uint32_t bright_a)
{
    uint32_t dept = ORG_MODELS[model_applied < ORG_NMODEL ? model_applied : 0].dept, i, any = 0;
    uint32_t vib = org_par.vib[m] < ORG_NVIB ? org_par.vib[m] : 0u, drive = org_par.drive[m] & 127u;
    int32_t drive_q8 = 128 + (int32_t)drive * 12;
    int32_t trem_depth = 0, vib_depth = 0, vib_mix = 0;
    int32_t post_vintage = (G_POST_VINTAGE * TRIM_VINTAGE_Q8[model_applied & 3u]) >> 8;
    int16_t *vb = vib_buf[m];
    for (i = 0; i < n; i++) any |= (uint32_t)x[i];
    if (any) fxb[m].idle = 0;
    else if (fxb[m].idle >= 24u) return 0;
    else fxb[m].idle++;

    if (dept == DEPT_VINTAGE && vib) {
        static const uint8_t DEPTH[4] = {0, 10, 18, 30};                     /* peak delay swing, samples */
        vib_depth = DEPTH[vib > 3u ? vib - 3u : vib] << 8;
        vib_mix = vib > 3u ? 128 : 256;                                     /* chorus: half dry */
    } else if (dept != DEPT_VINTAGE)
        trem_depth = org_par.trem[m] & 127u;

    for (i = 0; i < n; i++) {
        int32_t v = x[i];
#ifdef ORG_LEVEL_DEBUG
        if (v > lv_dbg[0] || -v > lv_dbg[0]) lv_dbg[0] = v < 0 ? -v : v;
#endif
        /* ---- 1 body and drive. Gain staging (tests/levels.txt): three notes come out near -8 dB of the
           ceiling, both manuals and the pedal at full registration just touch the last soft clip */
        if (dept == DEPT_VINTAGE) {
            v = tube((v * G_IN_VINTAGE) >> 8, drive_q8);
            fxb[m].dc_m += ((v << 8) - fxb[m].dc_m) >> 9;      /* the asymmetric clip makes DC: take the mean out (Q8 state) */
            v -= fxb[m].dc_m >> 8;
            fxb[m].cab_lp += ((v - fxb[m].cab_lp) * LP_CAB_A) >> 15;
            v = (fxb[m].cab_lp * post_vintage) >> 8;
        } else {
            if (dept == DEPT_PIPE) {
                v = (softclip((v * G_IN_PIPE) >> 8) * G_POST_PIPE) >> 8;
            } else {
                /* the state keeps 8 bits below the sample (y in Q8): a fixed-point biquad otherwise holds a DC dead band */
                int64_t y = ((int64_t)CASSOTTO_BQ[0] * v + (int64_t)CASSOTTO_BQ[1] * fxb[m].bq_x1 + (int64_t)CASSOTTO_BQ[2] * fxb[m].bq_x2) * 256
                          - (int64_t)CASSOTTO_BQ[3] * fxb[m].bq_y1 - (int64_t)CASSOTTO_BQ[4] * fxb[m].bq_y2;
                fxb[m].bq_x2 = fxb[m].bq_x1; fxb[m].bq_x1 = v;
                fxb[m].bq_y2 = fxb[m].bq_y1; fxb[m].bq_y1 = (int32_t)(y >> 14);
                fxb[m].bright_lp += (int32_t)(((int64_t)(fxb[m].bq_y1 - fxb[m].bright_lp) * (int32_t)bright_a) >> 15);
                v = (softclip(((fxb[m].bright_lp >> 8) * G_IN_REED) >> 8) * G_POST_REED) >> 8;
            }
            if (drive) {                        /* the tube, blended in as the knob opens */
                int32_t d = tube(v, drive_q8);
                fxb[m].dc_m += ((d << 8) - fxb[m].dc_m) >> 9;
                d = ((d - (fxb[m].dc_m >> 8)) * G_POST_VINTAGE) >> 8;
                v += ((d - v) * (int32_t)drive) >> 7;
            }
        }
#ifdef ORG_LEVEL_DEBUG
        if (v > lv_dbg[1] || -v > lv_dbg[1]) lv_dbg[1] = v < 0 ? -v : v;
#endif
        /* ---- 2 scanner vibrato / chorus, tremulant */
        if (vib_depth || trem_depth) {
            int32_t d, w, lfo;
            vb[fxb[m].vib_w & (VIB_LEN - 1u)] = sat16(v);
            if (vib_depth) {                    /* the scanner: a triangle at 6.9 Hz */
                d = (2 << 8) + ((vib_depth * lfo_v[i]) >> 15);
                w = rd_frac(vb, VIB_LEN - 1u, fxb[m].vib_w, d);
                v = vib_mix == 256 ? w : (v + w) >> 1;
            } else {                            /* tremulant: the wind shakes, level and a little pitch */
                lfo = lfo_t[i];
                d = (8 << 8) + ((trem_depth * lfo) >> 13);                  /* +-2 samples: a few cents */
                w = rd_frac(vb, VIB_LEN - 1u, fxb[m].vib_w, d);
                v = w + (((w * lfo) >> 15) * trem_depth >> 8);
            }
            fxb[m].vib_w++;
        }
        x[i] = v;
    }
    return 1;
}

static void fx_block(int32_t (*bus)[SUB], int32_t *out, uint32_t n, int32_t *peak)
{
    const org_model_t *md = &ORG_MODELS[model_applied < ORG_NMODEL ? model_applied : 0];
    uint32_t dept = md->dept, i, m, on[ORG_NMAN], rot_on = 0, dly_on = 0, rev_on = 0;
    uint32_t les_shaped, fxi_side_on, mst_on = mst_rt.eq_on || mst_rt.comp_on;
    uint32_t rmode = org_par.rev_mode < ORG_NREV ? org_par.rev_mode : 0u;
    const rev_mode_t *rm = &REV_MODES[rmode];
    int32_t ds[ORG_NMAN], rs[ORG_NMAN], lfo_v[SUB], lfo_t[SUB];
    int32_t dly_fb = ((int32_t)org_par.delay_fb * 110) >> 7;
    uint32_t dly_d = 441u + (uint32_t)org_par.delay_time * 170u;    /* 20 .. 1000 ms at 22.05 kHz */
    int32_t expr_t;
    uint32_t bright_a = 0;
    /* rotor sine/cosine at the block's start and end, interpolated (the rotors move slowly next to 32 frames) */
    int32_t hs0, hc0, hs1, hc1, ds0, dc0, ds1, dc1;

    if (rmode != fx.rv_mode) rev_clear(rmode);
    if (dly_d >= DLY_LEN) dly_d = DLY_LEN - 1u;

    /* expression (CC 11 / bellows): volume, and for the bandoneon the brightness */
    expr_t = ((int32_t)org_par.expression * org_par.expression) >> 2;      /* 127 -> 4032 */
    fx.expr_q12 += (expr_t - fx.expr_q12) >> 2;
    if (dept == DEPT_REED)
        bright_a = model_applied == MODEL_BANDONEON ? BRIGHT_A[org_par.expression] : BRIGHT_A[96 + (org_par.expression >> 2)];

    /* stage 2: the LFOs, one for all buses */
    if (dept == DEPT_VINTAGE)
        for (i = 0; i < n; i++) {
            int32_t t;
            fx.vib_ph += INC_SCANNER;                                       /* the scanner: a triangle at 6.9 Hz */
            t = (int32_t)(fx.vib_ph >> 16);
            lfo_v[i] = t < 32768 ? t : 65535 - t;                           /* 0..32767 */
        }
    else
        for (i = 0; i < n; i++) {
            fx.trem_ph += model_applied >= MODEL_MUSETTE ? INC_TREM_REED : INC_TREMULANT;
            lfo_t[i] = org_sin(fx.trem_ph);
        }

    /* stages 1 and 2 per bus; what goes where */
    fxi_side_on = 0;
    for (m = 0; m < ORG_NMAN; m++) {
        on[m] = fx_bus(m, bus[m], n, lfo_v, lfo_t, bright_a);
        if (fxi_rt.type != FXI_OFF && fxi_rt.man == m) {   /* the insert slot (its tail runs on a silent bus) */
            if (on[m]) fxi.hold = FXI_LEN / SUB + 4u;
            else if (fxi.hold) { fxi.hold--; on[m] = 1; }   /* (a silent bus holds zeros) */
            if (on[m] && fxi_rt.type == FXI_CHORUS) { fxi_chorus(bus[m], n); fxi_side_on = 1; }
            else if (on[m] && fxi_rt.type == FXI_FLANGER) { fxi_flanger(bus[m], n); fxi_side_on = 1; }
            else if (on[m] && fxi_rt.type == FXI_PHASER) { fxi_phaser(bus[m], n); fxi_side_on = 1; }
            else if (on[m] && fxi_rt.type == FXI_RING) { fxi_ring(bus[m], n); fxi_side_on = 1; }
            else if (on[m] && fxi_rt.type == FXI_AMP) fxi_amp(bus[m], n);
            else if (on[m] && fxi_rt.type == FXI_WAH) fxi_wah(bus[m], n);
            else if (on[m] && fxi_rt.type == FXI_AUTOWAH) fxi_autowah(bus[m], n);
        }
        ds[m] = on[m] ? org_par.dsend[m] & 127u : 0;
        rs[m] = on[m] ? org_par.rsend[m] & 127u : 0;
        if (on[m] && org_par.les_on[m]) rot_on = 1;
        dly_on |= (uint32_t)ds[m];
        rev_on |= (uint32_t)(org_par.rsend[m] & 127u);
    }
    /* the delay runs on while echoes are on their way (twice its time after the last send) and while they are still
       heard: stopped early, it froze them and played the frozen bits at the next note (clicks) */
    if (dly_on || fx.dly_ol > 2 || fx.dly_ol < -2 || fx.dly_or > 2 || fx.dly_or < -2) fx.dly_hold = (dly_d * 4u) / SUB + 2u;   /* (dly_d at half rate) */
    if (fx.dly_hold) { fx.dly_hold--; dly_on = 1; }

    /* stage 3: rotor speeds glide to the target (the CLASSIC: horn ~0.8 s, drum ~3 s) */
    {
        uint32_t ht = org_par.leslie == LESLIE_FAST ? les_rt.inc[1] : org_par.leslie == LESLIE_SLOW ? les_rt.inc[0] : 0u;
        uint32_t dt = org_par.leslie == LESLIE_FAST ? les_rt.inc[3] : org_par.leslie == LESLIE_SLOW ? les_rt.inc[2] : 0u;
        fx.horn_inc += (int32_t)(((int32_t)(ht - fx.horn_inc)) * les_rt.h_glide) >> 16;
        fx.drum_inc += (int32_t)(((int32_t)(dt - fx.drum_inc) >> 4) * les_rt.d_glide) >> 12;
        if (org_par.leslie == LESLIE_BRAKE && fx.horn_inc < 200u) fx.horn_inc = 0;
        if (org_par.leslie == LESLIE_BRAKE && fx.drum_inc < 200u) fx.drum_inc = 0;
    }
    les_shaped = les_rt.h_gain != 16384 || les_rt.d_gain != 16384 || les_rt.width != 64;
    hs0 = org_sin(fx.horn_ph); hc0 = org_sin(fx.horn_ph + 0x40000000u);
    ds0 = org_sin(fx.drum_ph); dc0 = org_sin(fx.drum_ph + 0x40000000u);
    fx.horn_ph += fx.horn_inc * n;
    fx.drum_ph -= fx.drum_inc * n;                                          /* the drum turns the other way */
    hs1 = org_sin(fx.horn_ph); hc1 = org_sin(fx.horn_ph + 0x40000000u);
    ds1 = org_sin(fx.drum_ph); dc1 = org_sin(fx.drum_ph + 0x40000000u);
    org_stat.horn_ph = fx.horn_ph;
    org_stat.drum_ph = fx.drum_ph;
    org_stat.horn_inc = fx.horn_inc;

    for (i = 0; i < n; i++) {
        int32_t x = 0, dry = 0, l, r, j = (int32_t)i, d_in = 0, r_in = 0;
        for (m = 0; m < ORG_NMAN; m++) {
            int32_t v;
            if (!on[m]) continue;
            v = bus[m][i];
            if (org_par.les_on[m]) x += v; else dry += v;
            d_in += v * ds[m];
            r_in += v * rs[m];
        }

        /* ---- 3 rotary speaker */
        if (rot_on) {
            int32_t lo, hi, hs = hs0 + (((hs1 - hs0) * j) >> 5), hc = hc0 + (((hc1 - hc0) * j) >> 5);
            int32_t dsn = ds0 + (((ds1 - ds0) * j) >> 5), dcs = dc0 + (((dc1 - dc0) * j) >> 5);
            int32_t hl, hr, dl, dr;
            fx.xo_lp += ((x - fx.xo_lp) * les_rt.xo_a) >> 15;
            lo = fx.xo_lp;
            hi = x - lo;
            horn_buf[fx.rot_w & (ROT_LEN - 1u)] = sat16(hi);
            drum_buf[fx.rot_w & (ROT_LEN - 1u)] = sat16(lo);
            /* horn: +-14 samples of Doppler, strong level swing; mics 90 degrees apart */
            hl = rd_frac(horn_buf, ROT_LEN - 1u, fx.rot_w, (20 << 8) + ((hs * les_rt.h_dop) >> 7));
            hr = rd_frac(horn_buf, ROT_LEN - 1u, fx.rot_w, (20 << 8) + ((hc * les_rt.h_dop) >> 7));
            hl = (hl * (24576 + ((hc * les_rt.h_am) >> 15))) >> 15;
            hr = (hr * (24576 - ((hs * les_rt.h_am) >> 15))) >> 15;
            /* drum: +-6 samples; the baffle throws the bass past the mics, a deep level swing (0.47 .. 1.15) */
            dl = rd_frac(drum_buf, ROT_LEN - 1u, fx.rot_w, (8 << 8) + ((dsn * les_rt.d_dop) >> 7));
            dr = rd_frac(drum_buf, ROT_LEN - 1u, fx.rot_w, (8 << 8) + ((dcs * les_rt.d_dop) >> 7));
            dl = (dl * (26500 + ((dcs * les_rt.d_am) >> 15))) >> 15;
            dr = (dr * (26500 - ((dsn * les_rt.d_am) >> 15))) >> 15;
            fx.rot_w++;
            if (les_shaped) {                                               /* a cabinet other than the CLASSIC's mix */
                hl = (hl * les_rt.h_gain) >> 14; hr = (hr * les_rt.h_gain) >> 14;
                dl = (dl * les_rt.d_gain) >> 14; dr = (dr * les_rt.d_gain) >> 14;
                l = hl + dl; r = hr + dr;
                if (les_rt.width != 64) { int32_t mid = (l + r) >> 1, side = (((l - r) >> 1) * les_rt.width) >> 6; l = mid + side; r = mid - side; }
                l += dry; r += dry;
            } else {
                l = hl + dl + dry;
                r = hr + dr + dry;
            }
        } else {
            l = r = x + dry;
        }

        if (fxi_side_on) { l += fxi.side[i]; r -= fxi.side[i]; }   /* the insert's stereo side */

        /* ---- 4 ping-pong delay (half rate) */
        if (dly_on) {
            int32_t in = d_in >> 7;
            if ((fx.dly_half ^= 1u) == 0u) {
                uint32_t rp = fx.dly_w >= dly_d ? fx.dly_w - dly_d : fx.dly_w + DLY_LEN - dly_d;
                int32_t yl = dly_l[rp], yr = dly_r[rp];
                fx.dly_lp_l += ((yl - fx.dly_lp_l) * LP_DELAY_A) >> 15;
                fx.dly_lp_r += ((yr - fx.dly_lp_r) * LP_DELAY_A) >> 15;
                dly_l[fx.dly_w] = sat16(((in + fx.dly_in_prev) >> 1) + ((fx.dly_lp_r * dly_fb) >> 7));
                dly_r[fx.dly_w] = sat16((fx.dly_lp_l * dly_fb) >> 7);
                if (++fx.dly_w >= DLY_LEN) fx.dly_w = 0;
                fx.dly_pl = fx.dly_ol; fx.dly_pr = fx.dly_or;
                fx.dly_ol = yl; fx.dly_or = yr;
                l += yl; r += yr;
            } else {
                l += (fx.dly_ol + fx.dly_pl) >> 1;
                r += (fx.dly_or + fx.dly_pr) >> 1;
            }
            fx.dly_in_prev = in;
        }

        /* ---- 5 reverb, at half rate: every second sample one step from the mean of two inputs; between,
           the mean of its last two outputs */
        if (rev_on && (fx.rv_half ^= 1u) != 0u) {
            fx.rv_in_prev = r_in >> 8;
            l += (fx.rv_ol + fx.rv_pl) >> 1;
            r += (fx.rv_or + fx.rv_pr) >> 1;
        } else if (rev_on) {
            int32_t v = r_in >> 8, y0, y1, y2, y3, a, b, c, d, h0, h1, h2, h3, wl, wr;
            uint32_t t = fx.rv_t++;
            {
                int32_t now = v;
                v = (v + fx.rv_in_prev) >> 1;
                fx.rv_in_prev = now;
            }
            if (rm->pre) {
                rv_pre[t & (RV_PRE - 1u)] = sat16(v);
                v = rv_pre[(t - rm->pre) & (RV_PRE - 1u)];
            }
            v = allpass(rv_ap[0], 255u, t, 71u, v, 20000);
            v = allpass(rv_ap[1], 255u, t, 53u, v, 20000);
            v = allpass(rv_ap[2], 255u, t, 191u, v, 20000);
            v = allpass(rv_ap[3], 255u, t, 139u, v, 20000);
            if (rm->spring) {                   /* the spring's dispersion: chirping all-passes */
                v = allpass(rv_sp[0], 31u, t, 7u, v, 22000);
                v = allpass(rv_sp[1], 31u, t, 11u, v, 22000);
                v = allpass(rv_sp[2], 31u, t, 13u, v, 22000);
                v = allpass(rv_sp[3], 31u, t, 17u, v, 22000);
                v = allpass(rv_sp[4], 31u, t, 23u, v, 22000);
                v = allpass(rv_sp[5], 31u, t, 29u, v, 22000);
            }
            y0 = rv_line[0][(t - rm->len[0]) & (RV_LINE - 1u)]; y1 = rv_line[1][(t - rm->len[1]) & (RV_LINE - 1u)];
            y2 = rv_line[2][(t - rm->len[2]) & (RV_LINE - 1u)]; y3 = rv_line[3][(t - rm->len[3]) & (RV_LINE - 1u)];
            fx.rv_lp[0] += ((y0 - fx.rv_lp[0]) * rm->damp) >> 15;
            fx.rv_lp[1] += ((y1 - fx.rv_lp[1]) * rm->damp) >> 15;
            fx.rv_lp[2] += ((y2 - fx.rv_lp[2]) * rm->damp) >> 15;
            fx.rv_lp[3] += ((y3 - fx.rv_lp[3]) * rm->damp) >> 15;
            a = fx.rv_lp[0] + fx.rv_lp[1]; b = fx.rv_lp[0] - fx.rv_lp[1];
            c = fx.rv_lp[2] + fx.rv_lp[3]; d = fx.rv_lp[2] - fx.rv_lp[3];
            h0 = (a + c) >> 1; h1 = (b + d) >> 1; h2 = (a - c) >> 1; h3 = (b - d) >> 1;   /* Hadamard: lossless */
            rv_line[0][t & (RV_LINE - 1u)] = sat16(v + ((h0 * rm->g[0] + 16384) >> 15));   /* rounded: no limit cycle */
            rv_line[1][t & (RV_LINE - 1u)] = sat16(v + ((h1 * rm->g[1] + 16384) >> 15));
            rv_line[2][t & (RV_LINE - 1u)] = sat16(v + ((h2 * rm->g[2] + 16384) >> 15));
            rv_line[3][t & (RV_LINE - 1u)] = sat16(v + ((h3 * rm->g[3] + 16384) >> 15));
            wl = y0 + y2 - y3;
            wr = y1 + y3 - y0;
            fx.rv_pl = fx.rv_ol; fx.rv_pr = fx.rv_or;
            fx.rv_ol = wl; fx.rv_or = wr;
            l += wl;
            r += wr;
        }

        /* ---- master: EQ, compressor */
        if (mst_on) {
            int32_t ch, v[2] = {l, r};
            for (ch = 0; ch < 2; ch++) {
                int32_t y = v[ch];
                if (y > 100000) y = 100000;
                if (y < -100000) y = -100000;
                if (mst_rt.eq_on) {
                    int32_t hp;
                    mst.lo[ch] += ((y - mst.lo[ch]) * mst_rt.a_lo) >> 15;
                    mst.hi[ch] += ((y - mst.hi[ch]) * mst_rt.a_hi) >> 15;
                    mst.svl[ch] += (mst_rt.f_mid * mst.svb[ch]) >> 12;
                    hp = y - mst.svl[ch] - mst.svb[ch];
                    mst.svb[ch] += (mst_rt.f_mid * hp) >> 12;
                    if (mst.svb[ch] > 200000) mst.svb[ch] = 200000;
                    if (mst.svb[ch] < -200000) mst.svb[ch] = -200000;
                    y += (((mst_rt.g_lo - 4096) * mst.lo[ch]) >> 12) + (((mst_rt.g_mid - 4096) * mst.svb[ch]) >> 12)
                       + (((mst_rt.g_hi - 4096) * (y - mst.hi[ch])) >> 12);
                    if (y > 100000) y = 100000;
                    if (y < -100000) y = -100000;
                }
                if (mst_rt.comp_on) {
                    int32_t a = y < 0 ? -y : y, gi = mst.g_prev + (((mst.g - mst.g_prev) * j) >> 5);
                    if (a > mst.pk) mst.pk = a;
                    y = (y * ((gi * mst_rt.makeup) >> 12)) >> 12;
                }
                v[ch] = y;
            }
            l = v[0]; r = v[1];
        }

        /* ---- out: expression, a soft ceiling */
#ifdef ORG_LEVEL_DEBUG
        if (l > lv_dbg[2] || -l > lv_dbg[2]) lv_dbg[2] = l < 0 ? -l : l;
#endif
        l = softclip((l * fx.expr_q12) >> 12);
        r = softclip((r * fx.expr_q12) >> 12);
        out[2u * i] += l;
        out[2u * i + 1u] += r;
        if (l > *peak) *peak = l;
        if (-l > *peak) *peak = -l;
    }
    if (mst_rt.comp_on) {                                       /* the compressor's gain for the next block */
        int32_t over = ilog2_q12((uint32_t)mst.pk + 1u) - mst_rt.thr, t = 4096;
        if (over > 0) t = (int32_t)iexp2_q12(12 * 4096 - ((over * mst_rt.slope) >> 12));
        mst.g_prev = mst.g;
        mst.g += ((t - mst.g) * (t < mst.g ? mst_rt.att : mst_rt.rel)) >> 16;
        mst.pk = 0;
        mst_gain = mst.g;
    } else { mst.g = mst.g_prev = 4096; mst.pk = 0; mst_gain = 4096; }
}

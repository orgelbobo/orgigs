/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs FM-1: MIDI Zones (main loop only; the audio interrupt never sees it).
 *
 * Three zones, one per manual (upper, lower, pedal). Each zone takes the notes of its MIDI-in channel
 * that fall in its key range (lowest..highest; zones may overlap), plays them on its manual inside, and
 * sends them out on its own MIDI-out channel, transposed, with the incoming velocity or a fixed one.
 * Three modules can be switched on per zone, in this order:
 *
 *   CHORD     one note plays a memorised chord (inside and out)
 *   CLONE     the notes also go out on more channels, each with its own octave
 *   HARMONIC  the notes also go out on 9 channels, transposed to the drawbar footages 16' .. 1'; the
 *             drawbars of the zone's manual set each channel's velocity, or send CC 7 / CC 11 when moved
 *
 * The FM-1's own keys play the zone of the focused manual (FOLLOW) or every zone whose range holds the
 * note (ZONES). The auto-pedal makes the pedal zone play the lowest note held in the lower zone.
 * CC, aftertouch and program changes are passed to the zone's out channels unless filtered; pitch bend
 * always is. Sustain (CC 64) holds the notes that came in on its channel, inside and out, until it lifts.
 * Pitch bend and CC 1 play the organs as set in ml_ctl_mode: PITCH bends (+-2 semitones) and switches the
 * rotary slow/fast; ROTARY makes bend up a rotary kick, bend down a tutti, CC 1 slow/brake/fast. Channels no zone listens to are dropped, or passed through unchanged (THRU).
 *
 * Out goes through a queue: USB-FAST sends at once; TRS-SAFE paces to what a 31250 baud DIN cable
 * carries (with running status, note-offs as note-on velocity 0), for a USB-to-DIN host on the far end.
 * Notes are reference counted, per manual and per output channel, and every held note remembers what it
 * switched on: changing a zone while notes sound never leaves one hanging. */

#define CH_OFF 16u                      /* a channel setting: none */
#define ML_MAXCHORD 8u

/* zone flags */
#define Z_VELTHRU 0x01u                 /* the incoming velocity goes out (else the fixed one) */
#define Z_VELEXPR 0x02u                 /* the velocity is scaled by the expression (CC 11 / mod strip) */
#define Z_NOCC    0x04u                 /* CC and aftertouch are not passed on */
#define Z_NOPC    0x08u                 /* program changes are not passed on */
#define Z_NOINT   0x10u                 /* the zone does not sound inside */
#define Z_CLONE   0x20u
#define Z_HARM    0x40u
#define Z_CHORD   0x80u
/* more zone flags (zone_t.xf) */
#define ZX_FCCI   0x01u                 /* FILTER CC IN: CC and pitch bend coming in on in_ch do not act inside
                                           (drawbars, rotary, expression, sustain, bend); they still go out as Z_NOCC says */
#define ZX_MUTE   0x02u                 /* MIDI OUT off: the zone sends nothing (inside it still sounds) */
#define ZX_CPL    0x04u                 /* coupled (the SMK-37's coupler): the panel's keys on one coupled zone play
                                           them all; a coupled pedal takes only the lowest note (ml_cpl) */

enum { CSEND_VELOCITY, CSEND_CC7, CSEND_CC11, ML_NCSEND };
enum { KEYS_FOLLOW, KEYS_ZONES, ML_NKEYS };

static const char *const CSEND_NAME[ML_NCSEND] = {"VEL", "CC7", "CC11"};
static const int8_t HARM_OFF[9] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};   /* 16' .. 1' */

typedef struct {                        /* one zone, as it is saved */
    uint8_t in_ch, out_ch;              /* 0..15, CH_OFF */
    uint8_t lo, hi;                     /* the notes it takes, 0..127 */
    int8_t transpose, octave;           /* MIDI out only: -24..+24 semitones, -3..+3 octaves */
    uint8_t vel;                        /* 1..127: the fixed velocity (the FM-1's keys always use it) */
    uint8_t flags;                      /* Z_... */
    uint16_t clone_mask;                /* CLONE: bit c = channel c + 1 */
    uint16_t harm_mask;                 /* HARMONIC: bit f = footage f (16' .. 1') */
    int8_t clone_oct[16];               /* CLONE: octave per channel, -3..+3 */
    uint8_t harm_base;                  /* HARMONIC: channel of the 16' (0..7); footage f on harm_base + f */
    uint8_t harm_send;                  /* HARMONIC: CSEND_... */
    uint8_t chord_n;
    int8_t chord[ML_MAXCHORD];          /* CHORD: intervals over the lowest note */
    uint8_t xf;                         /* ZX_...: (where the struct had its padding byte: setups keep their size) */
} zone_t;
typedef char zone_size[sizeof(zone_t) == 40u ? 1 : -1];   /* (the store's layout: as before xf) */

typedef struct {                        /* the zones, as a performance setup keeps them */
    zone_t z[ORG_NMAN];
    uint8_t keys;                       /* KEYS_...: what the FM-1's keys play */
    uint8_t auto_pedal;                 /* the pedal zone plays the lowest note held in the lower zone */
    uint8_t thru;                       /* channels no zone listens to pass through */
    uint8_t yst;                        /* MIDI module 5, YST (the SEQTRAK): on (0 in a setup of before) */
} zones_t;

#define ZONE_INIT(ch, base) {(ch), (ch), 0, 127, 0, 0, 100, Z_VELTHRU, 0, 0x01FFu, {0}, (base), CSEND_VELOCITY, 0, {0}}
static const zones_t ZONES_DEFAULT = {{ZONE_INIT(0, 3), ZONE_INIT(1, 3), ZONE_INIT(2, 3)}, KEYS_FOLLOW, 0, 0, 0};

/* a signed byte of a zone, kept out of line: inlined in a loop, clang loads it with an immediate
   pre-increment (b[++rX=n] (s)) that the FM-1 emulator rejects */
static __attribute__((noinline)) int32_t s8(const int8_t *p) { return *p; }

static zones_t zn = {{ZONE_INIT(0, 3), ZONE_INIT(1, 3), ZONE_INIT(2, 3)}, KEYS_FOLLOW, 0, 0, 0};
static uint8_t ml_rate_safe;            /* 0 USB-FAST, 1 TRS-SAFE (the device's, not a setup's) */
enum { CTL_PITCH, CTL_LESLIE, CTL_N };
static uint8_t ml_ctl_mode;             /* what bend and CC 1 do on the organs (the device's, not a setup's) */
static const char *const CTL_NAME[CTL_N] = {"PITCH", "ROTARY"};

/* ---- held notes: a slot per FM-1 key, one for the auto-pedal, a pool for the notes coming in */
#define SL_PEDAL BOARD_NKEYS
#define SL_MIDI0 (BOARD_NKEYS + 1u)
#define SL_NMIDI 48u
#define ML_SLOTS (SL_MIDI0 + SL_NMIDI)
#define ML_MAXOUT 64u
#define ML_MAXIN 24u                    /* a chord of 8 on three manuals */
#define SRC_NONE 0xFFu
#define SRC_KEY 0x80u                   /* the FM-1's keys and the auto-pedal; else the MIDI-in channel */

static struct {
    uint16_t out[ML_SLOTS][ML_MAXOUT];  /* what each held note sent: ch << 8 | note */
    uint16_t in[ML_SLOTS][ML_MAXIN];    /* what it plays inside: manual << 7 | key */
    uint8_t nout[ML_SLOTS], nin[ML_SLOTS];
    uint8_t src[ML_SLOTS];              /* SRC_NONE: free */
    uint8_t note[ML_SLOTS];             /* the note played (FM-1 keys: the octave applied) */
    uint8_t vel[ML_SLOTS];              /* the velocity it came with (0: an FM-1 key) */
    uint8_t zones[ML_SLOTS];            /* the zones that took it, bit = zone */
    uint8_t sus[ML_SLOTS];              /* released, but held by the sustain pedal */
    uint16_t pedal;                     /* sustain down, bit = MIDI-in channel */
    uint8_t key_sus;                    /* the panel's own sustain pedal down (the SMK-37): it holds the keys let go */
    uint8_t int_ref[ORG_NMAN][ORG_NGEN];
    uint8_t out_ref[16][128];
    uint8_t pedal_note;                 /* the auto-pedal's note, 0xFF none */
    uint8_t focus;                      /* the manual (zone) the panel works on */
    uint8_t learn;                      /* range learning: 0 off, 1 waiting for the first note, 2 the second */
    uint8_t learn_a;
    uint32_t act_ms[16];                /* last time each channel sent, for the screen */
    uint8_t last_db[ORG_NMAN][9];       /* HARMONIC CC 7 / CC 11: what was sent */
} mls;

/* ---- the out queue */
#define MQ_LEN 512u
static uint8_t mq_buf[MQ_LEN][3];
static uint32_t mq_head, mq_tail, mq_drop, mq_last_ms;
static int32_t mq_budget;               /* TRS-SAFE: bytes we may send now (x 1000) */
static uint8_t mq_status;               /* running status as a DIN cable would see it */

static void ml_q(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (mq_head - mq_tail >= MQ_LEN) { mq_drop++; return; }
    mq_buf[mq_head % MQ_LEN][0] = (uint8_t)st;
    mq_buf[mq_head % MQ_LEN][1] = (uint8_t)(d1 & 0x7Fu);
    mq_buf[mq_head % MQ_LEN][2] = (uint8_t)(d2 & 0x7Fu);
    mq_head++;
    if (st < 0xF0u) mls.act_ms[st & 15u] = fm1_ms | 1u;
}

static void ml_flush(void)
{
    uint32_t now = fm1_ms, sent = 0, room = midi_out_room();
    if (ml_rate_safe) {                 /* 3125 bytes a second */
        mq_budget += (int32_t)(now - mq_last_ms) * 3125;
        if (mq_budget > 24000) mq_budget = 24000;
    }
    mq_last_ms = now;
    while (mq_tail != mq_head && sent < room) {
        uint8_t *m = mq_buf[mq_tail % MQ_LEN];
        uint32_t st = m[0], d2 = m[2];
        if (ml_rate_safe) {
            int32_t cost;
            if ((st & 0xF0u) == 0x80u) { st = 0x90u | (st & 15u); d2 = 0; }   /* keeps the running status */
            if (st >= 0xF8u) cost = 1000;                                     /* realtime: one byte, no status */
            else {
                cost = (st == mq_status ? 2 : 3) * 1000;
                if ((st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u) cost -= 1000;
            }
            if (mq_budget < cost) break;
            mq_budget -= cost;
            if (st < 0xF8u) mq_status = (uint8_t)st;
        }
        midi_send(st, m[1], d2);
        mq_tail++;
        sent++;
    }
}

/* ---- notes inside: reference counted per manual */
static void int_on(uint32_t man, int32_t note, uint32_t slot)
{
    int32_t k = note - (int32_t)ORG_GEN0;
    if (k < 0 || k >= (int32_t)ORG_NGEN || mls.nin[slot] >= ML_MAXIN) return;
    mls.in[slot][mls.nin[slot]++] = (uint16_t)(man << 7 | (uint32_t)k);
    if (mls.int_ref[man][k]++ == 0u) org_key_set(man, (uint32_t)k, 1);
}

/* ---- notes out: reference counted per channel */
static void out_on(uint32_t ch, int32_t note, uint32_t vel, uint32_t slot)
{
    if (note < 0 || note > 127 || ch > 15u || !vel || mls.nout[slot] >= ML_MAXOUT) return;
    mls.out[slot][mls.nout[slot]++] = (uint16_t)(ch << 8 | (uint32_t)note);
    if (mls.out_ref[ch][note]++ == 0u) ml_q(0x90u | ch, (uint32_t)note, vel > 127u ? 127u : vel);
}

static void slot_off(uint32_t slot)
{
    uint32_t i;
    for (i = 0; i < mls.nin[slot]; i++) {
        uint32_t man = mls.in[slot][i] >> 7, k = mls.in[slot][i] & 0x7Fu;
        if (mls.int_ref[man][k] && --mls.int_ref[man][k] == 0u) org_key_set(man, (uint32_t)k, 0);
    }
    for (i = 0; i < mls.nout[slot]; i++) {
        uint32_t ch = mls.out[slot][i] >> 8, note = mls.out[slot][i] & 0x7Fu;
        if (mls.out_ref[ch][note] && --mls.out_ref[ch][note] == 0u) ml_q(0x80u | ch, note, 0);
    }
    mls.nin[slot] = 0;
    mls.nout[slot] = 0;
    mls.src[slot] = SRC_NONE;
    mls.zones[slot] = 0;
    mls.sus[slot] = 0;
}

static uint32_t zone_vel(const zone_t *z, uint32_t in_vel)   /* in_vel 0: an FM-1 key */
{
    uint32_t v = (z->flags & Z_VELTHRU) && in_vel ? in_vel : z->vel ? z->vel : 1u;
    if (z->flags & Z_VELEXPR) v = (v * (org_par.expression + 1u)) >> 7;
    return v ? v : 1u;
}

/* the out channels of a zone that controllers go to: its own, and the clones (mask, bit = channel) */
static uint32_t zone_ctl_mask(const zone_t *z, uint32_t with_harm)
{
    uint32_t m;
    if (z->out_ch >= CH_OFF || (z->xf & ZX_MUTE)) return 0;
    m = 1u << z->out_ch;
    if (z->flags & Z_CLONE) m |= z->clone_mask;
    if (with_harm && (z->flags & Z_HARM)) {
        uint32_t f;
        for (f = 0; f < 9u; f++) if (z->harm_mask >> f & 1u && z->harm_base + f < 16u) m |= 1u << (z->harm_base + f);
    }
    return m & 0xFFFFu;
}

/* one zone plays one note: inside and out, through its modules */
static void zone_on(uint32_t zi, uint32_t slot, int32_t note, uint32_t in_vel)
{
    const zone_t *z = &zn.z[zi];
    int32_t notes[ML_MAXCHORD], shift = s8(&z->transpose) + 12 * s8(&z->octave);
    uint32_t n = 1, i, ch, f, vel;
    notes[0] = note;
    if ((z->flags & Z_CHORD) && z->chord_n) {
        n = z->chord_n > ML_MAXCHORD ? ML_MAXCHORD : z->chord_n;
        for (i = 0; i < n; i++) notes[i] = note + s8(&z->chord[i]);
    }
    if (!(z->flags & Z_NOINT))
        for (i = 0; i < n; i++) int_on(zi, notes[i], slot);
    if (z->out_ch >= CH_OFF || (z->xf & ZX_MUTE)) return;
    vel = zone_vel(z, in_vel);
    for (i = 0; i < n; i++) {
        out_on(z->out_ch, notes[i] + shift, vel, slot);
        if (z->flags & Z_CLONE)
            for (ch = 0; ch < 16u; ch++)
                if (z->clone_mask >> ch & 1u && ch != z->out_ch) out_on(ch, notes[i] + shift + 12 * s8(&z->clone_oct[ch]), vel, slot);
    }
    if ((z->flags & Z_HARM) && zi != MAN_PEDAL)
        for (f = 0; f < 9u; f++) {
            uint32_t db = org_par.drawbar[zi][f], v = vel;
            if (!(z->harm_mask >> f & 1u) || !db || z->harm_base + f > 15u) continue;
            if (z->harm_send == CSEND_VELOCITY) v = (vel * db + 4u) >> 3;
            for (i = 0; i < n; i++) out_on(z->harm_base + f, notes[i] + shift + HARM_OFF[f], v, slot);
        }
}

static void slot_on(uint32_t slot, uint32_t src, int32_t note, uint32_t vel, uint32_t zones)
{
    uint32_t zi;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    mls.src[slot] = (uint8_t)src;
    mls.note[slot] = (uint8_t)note;
    mls.vel[slot] = (uint8_t)vel;
    mls.zones[slot] = (uint8_t)zones;
    for (zi = 0; zi < ORG_NMAN; zi++)
        if (zones >> zi & 1u) zone_on(zi, slot, note, vel);
}

static uint32_t zone_takes(uint32_t zi, int32_t note)
{
    return note >= zn.z[zi].lo && note <= zn.z[zi].hi;
}

/* the coupled zones (bit = zone), when two or more are (else 0) */
static uint32_t ml_cpl(void)
{
    uint32_t zi, m = 0;
    for (zi = 0; zi < ORG_NMAN; zi++) if (zn.z[zi].xf & ZX_CPL) m |= 1u << zi;
    return m & (m - 1u) ? m : 0u;
}

/* auto-pedal: the lowest note held in the lower zone, if it lies in the pedal zone's range; a coupled pedal: the
   lowest of the panel's keys on the other coupled zones */
static void ml_pedal_update(void)
{
    uint32_t s, low = 0xFFu, vel = 0, c = ml_cpl(), pc = c >> MAN_PEDAL & 1u;
    if (zn.auto_pedal || pc)
        for (s = 0; s < ML_SLOTS; s++)
            if (s != SL_PEDAL && mls.src[s] != SRC_NONE && mls.note[s] < low
                && ((zn.auto_pedal && (mls.zones[s] >> MAN_LOWER & 1u))
                    || (pc && mls.src[s] == SRC_KEY && (mls.zones[s] & c) && !(mls.zones[s] >> MAN_PEDAL & 1u)))) {
                low = mls.note[s];
                vel = mls.vel[s];
            }
    if (low != 0xFFu && !zone_takes(MAN_PEDAL, (int32_t)low)) low = 0xFFu;
    if (low == mls.pedal_note) return;
    if (mls.pedal_note != 0xFFu) slot_off(SL_PEDAL);
    mls.pedal_note = (uint8_t)low;
    if (low != 0xFFu) slot_on(SL_PEDAL, SRC_KEY, (int32_t)low, vel, 1u << MAN_PEDAL);
}

/* range learning (the zone screen): the next two notes into the focused zone set its range */
static void ml_learn_note(uint32_t note)
{
    zone_t *z = &zn.z[mls.focus];
    if (mls.learn == 1u) { mls.learn_a = (uint8_t)note; mls.learn = 2; return; }
    z->lo = (uint8_t)(note < mls.learn_a ? note : mls.learn_a);
    z->hi = (uint8_t)(note < mls.learn_a ? mls.learn_a : note);
    mls.learn = 0;
}
static void ml_learned(void);           /* orgigs_ctl.c: the range was learnt */

/* the zones an FM-1 key's note goes to */
static uint32_t ml_key_zones(int32_t note)
{
    uint32_t zi, m = 0, c = ml_cpl();
    if (zn.keys == KEYS_FOLLOW) m = 1u << mls.focus;
    else
        for (zi = 0; zi < ORG_NMAN; zi++)
            if (zone_takes(zi, note) && !(zi == MAN_PEDAL && zn.auto_pedal)) m |= 1u << zi;
    if (m & c) m |= m >> MAN_PEDAL & 1u ? c : c & ~(1u << MAN_PEDAL);   /* coupled (the pedal: ml_pedal_update) */
    return m;
}

/* a key of the panel (0 .. BOARD_NKEYS-1) went down or up; note is what it plays now (octave applied);
   vel 1..127 the key's velocity (the SMK-37), 0: none (the FM-1's keys: the zones' fixed velocity) */
static void ml_local_key_vel(uint32_t key, int32_t note, uint32_t on, uint32_t vel)
{
    if (key >= BOARD_NKEYS) return;
    if (on) {
        if (mls.src[key] != SRC_NONE) slot_off(key);
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        if (mls.learn) { ml_learn_note((uint32_t)note); if (!mls.learn) ml_learned(); }
        slot_on(key, SRC_KEY, note, vel, ml_key_zones(note));
    } else if (mls.src[key] != SRC_NONE) {
        if (mls.key_sus) mls.sus[key] = 1; else slot_off(key);
    }
    ml_pedal_update();
}

static void ml_local_key(uint32_t key, int32_t note, uint32_t on) { ml_local_key_vel(key, note, on, 0); }

static void ml_release_keys(void)       /* the FM-1's keys and the auto-pedal (the notes coming in stay) */
{
    uint32_t s;
    for (s = 0; s <= SL_PEDAL; s++) if (mls.src[s] != SRC_NONE) slot_off(s);
    mls.pedal_note = 0xFFu;
}

static void ml_release_all(void)
{
    uint32_t s;
    for (s = 0; s < ML_SLOTS; s++) if (mls.src[s] != SRC_NONE) slot_off(s);
    mls.pedal_note = 0xFFu;
}

static void ml_init(void)
{
    uint32_t s, d;
    for (s = 0; s < ML_SLOTS; s++) mls.src[s] = SRC_NONE;
    for (s = 0; s < ORG_NMAN; s++) for (d = 0; d < 9u; d++) mls.last_db[s][d] = 0xFFu;
    mls.pedal_note = 0xFFu;
}

/* hold a chord, press capture: the notes held in the focused zone, as intervals over the lowest;
   nothing held clears */
static uint32_t ml_chord_capture(void)
{
    uint32_t s, n = 0, low = 0xFFu, zi = mls.focus;
    zone_t *z = &zn.z[zi];
    for (s = 0; s < ML_SLOTS; s++)
        if (s != SL_PEDAL && mls.src[s] != SRC_NONE && (mls.zones[s] >> zi & 1u) && mls.note[s] < low) low = mls.note[s];
    for (s = 0; s < ML_SLOTS && n < ML_MAXCHORD; s++)
        if (s != SL_PEDAL && mls.src[s] != SRC_NONE && (mls.zones[s] >> zi & 1u)) {
            uint32_t j, iv = mls.note[s] - low, dup = 0;
            for (j = 0; j < n; j++) if ((uint32_t)z->chord[j] == iv) dup = 1;
            if (!dup) z->chord[n++] = (int8_t)iv;
        }
    z->chord_n = (uint8_t)n;
    return n;
}

/* the strips of the panel (or what stands in for them): out on the focused zone's channels */
static void ml_send_mod(uint32_t v)
{
    uint32_t ch, m = zn.z[mls.focus].flags & Z_NOCC ? 0u : zone_ctl_mask(&zn.z[mls.focus], 0);
    for (ch = 0; ch < 16u; ch++) if (m >> ch & 1u) ml_q(0xB0u | ch, 1, v);
}
static void ml_send_bend(int32_t b)     /* -8192..8191 */
{
    uint32_t ch, m = zone_ctl_mask(&zn.z[mls.focus], 1), v = (uint32_t)(b + 8192);
    for (ch = 0; ch < 16u; ch++) if (m >> ch & 1u) ml_q(0xE0u | ch, v & 0x7Fu, v >> 7);
}

/* HARMONIC with CC 7 / CC 11: a moved drawbar sends its channel's volume */
static void ml_drawbars_tick(void)
{
    uint32_t zi, d;
    for (zi = 0; zi < MAN_PEDAL; zi++) {
        const zone_t *z = &zn.z[zi];
        if (!(z->flags & Z_HARM) || z->harm_send == CSEND_VELOCITY || z->out_ch >= CH_OFF || (z->xf & ZX_MUTE)) continue;
        for (d = 0; d < 9u; d++) {
            uint32_t v = org_par.drawbar[zi][d];
            if (v == mls.last_db[zi][d] || !(z->harm_mask >> d & 1u) || z->harm_base + d > 15u) continue;
            mls.last_db[zi][d] = (uint8_t)v;
            ml_q(0xB0u | (z->harm_base + d), z->harm_send == CSEND_CC7 ? 7u : 11u, (v * 127u + 4u) >> 3);
        }
    }
}
/* ---- the MIDI mixer: a volume (CC 7) per channel, kept with the performance setup; over them all the MIDI out's
   sum (ml_mvol, 127 = as the mixer has them; not kept: the Elite's fader 3). What goes out: the channel's x the sum */
static uint8_t ml_mix[16] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
static uint8_t ml_mvol = 127;
static uint32_t ml_vol_out(uint32_t v) { return ((v & 127u) * ml_mvol + 63u) / 127u; }
static uint32_t ml_mix_out(uint32_t ch) { return ml_vol_out(ml_mix[ch & 15u]); }
static void ml_mix_send(uint32_t ch) { ml_q(0xB0u | (ch & 15u), 7u, ml_mix_out(ch)); }
/* the channels the zones send on: out channels, clones, harmonic (bit = channel) */
static uint32_t ml_used_channels(void)
{
    uint32_t zi, f, m = 0;
    for (zi = 0; zi < ORG_NMAN; zi++) {
        const zone_t *z = &zn.z[zi];
        if (z->out_ch >= CH_OFF || (z->xf & ZX_MUTE)) continue;
        m |= 1u << z->out_ch;
        if (z->flags & Z_CLONE) m |= z->clone_mask;
        if (z->flags & Z_HARM && zi != MAN_PEDAL)
            for (f = 0; f < 9u; f++) if (z->harm_mask >> f & 1u && z->harm_base + f < 16u) m |= 1u << (z->harm_base + f);
    }
    return m & 0xFFFFu;
}
static void ml_mix_send_used(void) { uint32_t ch, m = ml_used_channels(); for (ch = 0; ch < 16u; ch++) if (m >> ch & 1u) ml_mix_send(ch); }

static void ml_harm_resend(void) { uint32_t zi, d; for (zi = 0; zi < ORG_NMAN; zi++) for (d = 0; d < 9u; d++) mls.last_db[zi][d] = 0xFFu; }

/* ---- MIDI in */
static void ml_strip_mod(uint32_t v);   /* orgigs_ctl.c */
static void ml_strip_pitch(int32_t b);

static uint32_t ml_listen_mask(uint32_t ch)     /* the zones whose MIDI-in channel is ch */
{
    uint32_t zi, m = 0;
    for (zi = 0; zi < ORG_NMAN; zi++)
        if (zn.z[zi].in_ch == ch && !(zi == MAN_PEDAL && zn.auto_pedal)) m |= 1u << zi;
    return m;
}

static uint32_t ml_find(uint32_t ch, uint32_t note)
{
    uint32_t s;
    for (s = SL_MIDI0; s < ML_SLOTS; s++) if (mls.src[s] == ch && mls.note[s] == note) return s;
    return 0xFFu;
}

static void ml_note_off_in(uint32_t ch, uint32_t note)
{
    uint32_t s = ml_find(ch, note);
    if (s == 0xFFu) return;
    if (mls.pedal >> ch & 1u) { mls.sus[s] = 1; return; }   /* the sustain pedal holds it */
    slot_off(s);
    ml_pedal_update();
}

static void ml_sustain(uint32_t ch, uint32_t down)    /* CC 64 on ch: lifting it lets the held notes go */
{
    uint32_t s;
    if (down) { mls.pedal |= (uint16_t)(1u << ch); return; }
    mls.pedal &= (uint16_t)~(1u << ch);
    for (s = SL_MIDI0; s < ML_SLOTS; s++)
        if (mls.src[s] == ch && mls.sus[s]) { mls.sus[s] = 0; slot_off(s); }
    ml_pedal_update();
}

static void ml_midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t ch = st & 15u, type = st & 0xF0u, lm, li = 0, zi, m = 0, c;
    if (st >= 0xF0u) {                  /* clock and transport */
        if (zn.thru) ml_q(st, 0, 0);
        return;
    }
    lm = ml_listen_mask(ch);
    if (!lm) {                          /* no zone listens: through, or nothing */
        if (zn.thru) ml_q(st, d1, d2);
        return;
    }
    if (type == 0x90u && d2) {
        uint32_t s, take = 0;
        if (mls.learn && (lm >> mls.focus & 1u)) { ml_learn_note(d1); if (!mls.learn) ml_learned(); }
        for (zi = 0; zi < ORG_NMAN; zi++) if (lm >> zi & 1u && zone_takes(zi, d1)) take |= 1u << zi;
        if ((s = ml_find(ch, d1)) != 0xFFu) slot_off(s);            /* the same note again */
        if (take) {
            for (s = SL_MIDI0; s < ML_SLOTS && mls.src[s] != SRC_NONE; s++) {}
            if (s == ML_SLOTS)                                      /* full: a note the pedal holds makes room */
                for (s = SL_MIDI0; s < ML_SLOTS && !(mls.src[s] != SRC_NONE && mls.sus[s]); s++) {}
            if (s < ML_SLOTS) {
                if (mls.src[s] != SRC_NONE) slot_off(s);
                slot_on(s, ch, d1, d2, take);
                mls.sus[s] = 0;
            }
        }
        ml_pedal_update();
        return;
    }
    if (type == 0x80u || type == 0x90u) { ml_note_off_in(ch, d1); return; }

    for (zi = 0; zi < ORG_NMAN; zi++) if (lm >> zi & 1u && !(zn.z[zi].xf & ZX_FCCI)) li |= 1u << zi;   /* CC acts inside */
    if (type == 0xB0u) {                /* inside: drawbars of the zone's manual, the strips' jobs, expression */
        for (zi = 0; zi < ORG_NMAN; zi++)
            if (li >> zi & 1u && d1 >= 12u && d1 <= 20u && (zi != MAN_PEDAL || d1 <= 15u))
                org_par.drawbar[zi][d1 - 12u] = (uint8_t)((d2 * 8u + 63u) / 127u);
        if (!li && d1 != 123u && d1 != 120u) {}                     /* (FILTER CC IN: all notes off still acts) */
        else if (d1 == 1u) ml_strip_mod(d2);
        else if (d1 == 11u) org_par.expression = d2;
        else if (d1 == 64u) ml_sustain(ch, d2 >= 64u);
        else if (d1 == 121u) ml_sustain(ch, 0);                     /* reset all controllers */
        else if (d1 == 123u || d1 == 120u) {                        /* all notes off on this channel */
            uint32_t s;
            mls.pedal &= (uint16_t)~(1u << ch);
            for (s = SL_MIDI0; s < ML_SLOTS; s++) if (mls.src[s] == ch) { mls.sus[s] = 0; slot_off(s); }
            ml_pedal_update();
        }
    } else if (type == 0xE0u && li) {
        ml_strip_pitch((int32_t)((uint32_t)d2 << 7 | d1) - 8192);
    }
    /* out: to every listening zone's channels, once per channel */
    for (zi = 0; zi < ORG_NMAN; zi++) {
        const zone_t *z = &zn.z[zi];
        if (!(lm >> zi & 1u)) continue;
        if (type == 0xE0u) m |= zone_ctl_mask(z, 1);
        else if (type == 0xC0u) { if (!(z->flags & Z_NOPC)) m |= zone_ctl_mask(z, 0); }
        else if (!(z->flags & Z_NOCC)) m |= zone_ctl_mask(z, 0);   /* CC, aftertouch */
    }
    for (c = 0; c < 16u; c++) if (m >> c & 1u) ml_q(type | c, d1, d2);
}

/* panic (SEL + HOME): every note off inside and out, whatever the bookkeeping thinks, and the FX tails gone */
static void ml_panic(void)
{
    uint32_t s, ch, n;
    ml_release_all();                   /* note-offs for what we know we sent */
    for (s = 0; s < ML_SLOTS; s++) { mls.nin[s] = 0; mls.nout[s] = 0; mls.src[s] = SRC_NONE; mls.zones[s] = 0; mls.sus[s] = 0; }
    mls.pedal = 0;
    for (s = 0; s < ORG_NMAN; s++)
        for (n = 0; n < ORG_NGEN; n++) mls.int_ref[s][n] = 0;
    for (ch = 0; ch < 16u; ch++)
        for (n = 0; n < 128u; n++) mls.out_ref[ch][n] = 0;
    mls.learn = 0;
    org_all_off();
    org_par.panic_seq++;                /* after the keys: the interrupt silences what is left */
    for (ch = 0; ch < 16u; ch++) {      /* sustain off, all sound off, all notes off */
        ml_q(0xB0u | ch, 64, 0);
        ml_q(0xB0u | ch, 120, 0);
        ml_q(0xB0u | ch, 123, 0);
    }
}

/* the zones were changed as a whole (a setup recalled): sane values, nothing left sounding */
static void zones_sanitise(zones_t *p)
{
    uint32_t zi, c;
    for (zi = 0; zi < ORG_NMAN; zi++) {
        zone_t *z = &p->z[zi];
        if (z->in_ch > CH_OFF) z->in_ch = (uint8_t)zi;
        if (z->out_ch > CH_OFF) z->out_ch = (uint8_t)zi;
        z->lo &= 127u;                  /* (masks, not "> 127": clang tests that as a signed byte load) */
        z->hi &= 127u;
        if (z->hi < z->lo) z->hi = 127;
        if (s8(&z->transpose) < -24 || s8(&z->transpose) > 24) z->transpose = 0;
        if (s8(&z->octave) < -3 || s8(&z->octave) > 3) z->octave = 0;
        z->vel &= 127u;
        if (!z->vel) z->vel = 100;
        for (c = 0; c < 16u; c++) if (s8(&z->clone_oct[c]) < -3 || s8(&z->clone_oct[c]) > 3) z->clone_oct[c] = 0;
        if (z->harm_base > 7u) z->harm_base = 3;
        if (z->harm_send >= ML_NCSEND) z->harm_send = CSEND_VELOCITY;
        if (z->chord_n > ML_MAXCHORD) z->chord_n = 0;
        if (z->xf > 7u) z->xf = 0;      /* (a padding byte before: anything else is off) */
    }
    if (p->keys >= ML_NKEYS) p->keys = KEYS_FOLLOW;
    p->auto_pedal &= 1u;
    p->thru &= 1u;
    p->yst &= 1u;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* OrGigs: the settings as a SysEx backup over USB-MIDI (main loop only). tools/orgigs_backup.py is the computer's
 * side; any SysEx program can keep and send the files too (with ~50 ms between the messages).
 *
 * The messages, F0 7D 4F 47 cmd .. F7 (7D: non-commercial, "OG"):
 *   01                     computer -> OrGigs: send everything
 *   02 n                   computer -> OrGigs: send setup n (0..15)
 *   10 sl sh tl th data..  a part of a backup, both ways: part s of t (7-bit halves), data = up to 256 bytes,
 *                          7-bit packed (8 bytes carry 7: first the top bits, bit i = byte i)
 *   20 sl sh code          OrGigs -> computer: part s received (code 0) or not (BK_...)
 *   21 code                OrGigs -> computer: what became of a backup sent to it (BK_...)
 * A backup, the parts joined: header 16 bytes, the data, CRC-32 (LE) of both.
 *   0..3 "OGBK"  4 format 1  5 STORE_VER  6 kind (0 everything, 1 one setup)  7 setup n (kind 1)
 *   8..9 data length  10..11 sector 0's length (kind 0)  12..13 where setup 1 starts in it  14..15 setup length
 *   data: kind 0 sector 0 (the panel as it is, the 16 setups, the lights) + sector 1 (the user drawbars), then on a
 *         board with more (the SMK-37: BOARD_BK_EXTRA_MAX) "OGEX" and records: a tag, its length (LE 16), its bytes;
 *         kind 1 one setup_t. A backup without the extra part loads as ever (what it lacks stays); records a board
 *         does not know are skipped
 * A backup received is checked, then the screen asks: SAVE loads it, any other button keeps what is there.
 * Before it loads, OrGigs sends everything as it was (the computer keeps that as the undo). */

#define BK_MAGIC "OGBK"
#define BK_FMT 1u
#define BK_HDR 16u
#define BK_PART 256u                    /* data bytes per message: 300 on the wire, the platform takes 640 */
#ifndef BOARD_BK_EXTRA_MAX
#define BOARD_BK_EXTRA_MAX 0u           /* (the FM-1: none) */
#else
static uint32_t smk_bk_put(uint8_t *b, uint32_t room);     /* board: its records after "OGEX"; the bytes */
static void smk_bk_get(const uint8_t *b, uint32_t n);      /* board: the records of a backup loaded */
#endif
#define BK_MAX (BK_HDR + sizeof(store0_t) + sizeof(store1_t) + BOARD_BK_EXTRA_MAX + 4u)
#define BK_ASK_MS 60000u
enum { BK_OK, BK_ASK, BK_LOADED, BK_CANCELLED, BK_BAD, BK_VERSION, BK_TIMEOUT, BK_ORDER };
enum { BK_ALL, BK_SETUP };

#define BK_RX_MAX (BK_HDR + sizeof(store0_t) + sizeof(store1_t) + 512u + 4u)   /* (any board takes another's extra) */
static uint8_t bk_rx[BK_RX_MAX] ORG_POOL, bk_tx[BK_MAX] ORG_POOL;
static uint8_t bk_wire[16u + (BK_PART + 6u) / 7u * 8u];
static struct {
    uint32_t rx_n, rx_next, rx_parts;   /* the backup coming in: bytes, the part expected, of how many */
    uint32_t tx_n, tx_part, tx_on;      /* the backup going out, one part a main-loop round */
    uint32_t tx_then;                   /* BK_LOADED: say so when the undo is out */
    uint32_t asking, ask_until;
    char ask[24];
} bk;

static uint32_t bk_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static void bk_w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint32_t bk_r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

static int bk_send(const uint8_t *body, uint32_t n)     /* F0 7D 4F 47 body F7 */
{
    uint32_t i;
    bk_wire[0] = 0xF0; bk_wire[1] = 0x7D; bk_wire[2] = 0x4F; bk_wire[3] = 0x47;
    for (i = 0; i < n; i++) bk_wire[4 + i] = body[i];
    bk_wire[4 + n] = 0xF7;
    return sysex_send(bk_wire, n + 5u);
}
static void bk_state(uint32_t code) { uint8_t m[2] = {0x21, (uint8_t)code}; bk_send(m, 2); }

/* ---- out: a backup into bk_tx, sent a part at a time by bk_tick */
static void store_capture_now(void);    /* orgigs_presets.c */
static uint32_t bk_build(uint8_t *b, uint32_t kind, uint32_t n)
{
    uint32_t len = kind == BK_ALL ? sizeof st0 + sizeof st1 : sizeof(setup_t);
    memcpy(b, BK_MAGIC, 4);
    b[4] = BK_FMT; b[5] = STORE_VER; b[6] = (uint8_t)kind; b[7] = (uint8_t)n;
    bk_w16(b + 8, len);
    bk_w16(b + 10, kind == BK_ALL ? sizeof st0 : 0u);
    bk_w16(b + 12, kind == BK_ALL ? (uint32_t)((const uint8_t *)st0.setups - (const uint8_t *)&st0) : 0u);
    bk_w16(b + 14, sizeof(setup_t));
    if (kind == BK_ALL) {
        store_capture_now();
        memcpy(b + BK_HDR, &st0, sizeof st0);
        memcpy(b + BK_HDR + sizeof st0, &st1, sizeof st1);
#if BOARD_BK_EXTRA_MAX
        {
            uint8_t *x = b + BK_HDR + len;
            memcpy(x, "OGEX", 4);
            len += 4u + smk_bk_put(x + 4, BOARD_BK_EXTRA_MAX - 4u);
            bk_w16(b + 8, len);
        }
#endif
    } else memcpy(b + BK_HDR, &st0.setups[n], sizeof(setup_t));
    len += BK_HDR;
    {
        uint32_t c = bk_crc32(b, len);
        b[len] = (uint8_t)c; b[len + 1] = (uint8_t)(c >> 8); b[len + 2] = (uint8_t)(c >> 16); b[len + 3] = (uint8_t)(c >> 24);
    }
    return len + 4u;
}
static void bk_send_start(uint32_t kind, uint32_t n)
{
    bk.tx_n = bk_build(bk_tx, kind, n);
    bk.tx_part = 0;
    bk.tx_on = 1;
}
static void bk_send_all(void) { bk.tx_then = 0; bk_send_start(BK_ALL, 0); }   /* the panel's "send backup" */

static void bk_tx_part(void)
{
    static uint8_t m[5u + (BK_PART + 6u) / 7u * 8u];
    uint32_t parts = (bk.tx_n + BK_PART - 1u) / BK_PART, at = bk.tx_part * BK_PART, n = bk.tx_n - at, i, j, o = 5;
    if (n > BK_PART) n = BK_PART;
    m[0] = 0x10;
    m[1] = (uint8_t)(bk.tx_part & 127u); m[2] = (uint8_t)(bk.tx_part >> 7);
    m[3] = (uint8_t)(parts & 127u); m[4] = (uint8_t)(parts >> 7);
    for (i = 0; i < n; i += 7u) {                       /* 7-bit packing */
        uint32_t top = o++;
        m[top] = 0;
        for (j = 0; j < 7u && i + j < n; j++) {
            uint8_t v = bk_tx[at + i + j];
            m[top] |= (uint8_t)((v >> 7) << j);
            m[o++] = v & 127u;
        }
    }
    if (bk_send(m, o) != 0) {
        bk.tx_on = 0;
        ctl_say("BACKUP:", "NO USB");
        return;
    }
    if (++bk.tx_part >= parts) {
        bk.tx_on = 0;
        if (bk.tx_then == BK_LOADED) bk_state(BK_LOADED);
        else ctl_say("BACKUP SENT", 0);
        bk.tx_then = 0;
    }
}

/* ---- in */
static uint32_t bk_check(void)          /* the received backup: BK_OK, BK_BAD or BK_VERSION */
{
    const uint8_t *b = bk_rx;
    uint32_t len, kind;
    if (bk.rx_n < BK_HDR + 4u || memcmp(b, BK_MAGIC, 4) != 0) return BK_BAD;
    len = bk_r16(b + 8);
    if (BK_HDR + len + 4u != bk.rx_n) return BK_BAD;
    if (bk_crc32(b, BK_HDR + len) != ((uint32_t)b[BK_HDR + len] | (uint32_t)b[BK_HDR + len + 1] << 8 |
                                      (uint32_t)b[BK_HDR + len + 2] << 16 | (uint32_t)b[BK_HDR + len + 3] << 24))
        return BK_BAD;
    kind = b[6];
    if (b[4] != BK_FMT || b[5] != STORE_VER || bk_r16(b + 14) != sizeof(setup_t)) return BK_VERSION;
    if (kind == BK_ALL)
        return (len == sizeof st0 + sizeof st1 || (len >= sizeof st0 + sizeof st1 + 4u &&
                memcmp(b + BK_HDR + sizeof st0 + sizeof st1, "OGEX", 4) == 0)) && bk_r16(b + 10) == sizeof st0 ? BK_OK : BK_VERSION;
    if (kind == BK_SETUP) return len == sizeof(setup_t) && b[7] < NSETUP ? BK_OK : BK_BAD;
    return BK_VERSION;
}

static void bk_ack(uint32_t s, uint32_t code)
{
    uint8_t m[4] = {0x20, (uint8_t)(s & 127u), (uint8_t)(s >> 7 & 127u), (uint8_t)code};
    bk_send(m, 4);
}

static void bk_part_in(const uint8_t *p, uint32_t n)   /* p: after the 10 */
{
    uint32_t s, t, i, o, code;
    if (n < 4u) return;
    s = p[0] | (uint32_t)p[1] << 7;
    t = p[2] | (uint32_t)p[3] << 7;
    if (s == 0) { bk.rx_n = 0; bk.rx_next = 0; bk.rx_parts = t; bk.asking = 0; }
    if (s != bk.rx_next || t != bk.rx_parts || t == 0) {
        bk.rx_next = 0; bk.rx_parts = 0;
        bk_ack(s, BK_ORDER);
        ctl_say("BACKUP:", "PARTS MISSING");
        return;
    }
    for (i = 4; i < n; ) {                              /* unpack */
        uint32_t top = p[i++];
        for (o = 0; o < 7u && i < n; o++, i++) {
            if (bk.rx_n >= sizeof bk_rx) { bk.rx_next = 0; bk.rx_parts = 0; bk_ack(s, BK_BAD); return; }
            bk_rx[bk.rx_n++] = (uint8_t)(p[i] | (top >> o & 1u) << 7);
        }
    }
    bk_ack(s, BK_OK);
    if (++bk.rx_next < t) return;
    bk.rx_next = 0; bk.rx_parts = 0;
    code = bk_check();
    if (code != BK_OK) {
        bk_state(code);
        ctl_say("BACKUP:", code == BK_VERSION ? "OTHER VERSION" : "DAMAGED");
        return;
    }
    if (bk_rx[6] == BK_ALL) memcpy(bk.ask, "LOAD BACKUP?", 13);
    else {
        char *q = cat(bk.ask, "LOAD INTO SETUP ");
        q = org_fmt(q, bk_rx[7] + 1, 0);
        cat(q, "?");
    }
    bk.asking = 1;
    bk.ask_until = fm1_ms + BK_ASK_MS;
    bk_state(BK_ASK);
}

static void store_apply_loaded(void);   /* orgigs_presets.c */
static void bk_answer(uint32_t yes)     /* the panel's answer to "LOAD ...?" */
{
    if (!bk.asking) return;
    bk.asking = 0;
    if (!yes) { bk_state(BK_CANCELLED); ctl_say("BACKUP NOT LOADED", 0); return; }
    bk_send_start(BK_ALL, 0);           /* the undo: everything as it was */
    bk.tx_then = BK_LOADED;
    if (bk_rx[6] == BK_ALL) {
        memcpy(&st0, bk_rx + BK_HDR, sizeof st0);
        memcpy(&st1, bk_rx + BK_HDR + sizeof st0, sizeof st1);
#if BOARD_BK_EXTRA_MAX
        {
            uint32_t base = sizeof st0 + sizeof st1, len = bk_r16(bk_rx + 8);
            if (len > base + 4u) smk_bk_get(bk_rx + BK_HDR + base + 4u, len - base - 4u);
        }
#endif
        store_apply_loaded();
        st_dirty |= 3u;
        ctl_say("BACKUP LOADED", 0);
    } else {
        memcpy(&st0.setups[bk_rx[7] & (NSETUP - 1u)], bk_rx + BK_HDR, sizeof(setup_t));
        st_dirty |= 1u;
        ctl_say_int("SETUP LOADED:", bk_rx[7] + 1, 0);
    }
    ctl.t_change = fm1_ms;
}
static uint32_t bk_asking(void) { return bk.asking; }

/* platform.c: one message F0 7D 4F 47 .. F7, p = the bytes between F0 and F7 */
static void yst_sysex(const uint8_t *p, uint32_t n);      /* orgigs_yst.c */
static void app_sysex(const uint8_t *p, uint32_t n)
{
    if (n && p[0] == 0x43u) { yst_sysex(p, n); return; }   /* Yamaha: the SEQTRAK (F0 43 ..) */
    if (n < 4u) return;
    if (p[3] == 0x01) { bk.tx_then = 0; bk_send_start(BK_ALL, 0); }
    else if (p[3] == 0x02 && n >= 5u && p[4] < NSETUP) { bk.tx_then = 0; bk_send_start(BK_SETUP, p[4]); }
    else if (p[3] == 0x10) bk_part_in(p + 4, n - 4u);
}

static void bk_tick(void)               /* main loop: the next part out; the question on the screen */
{
    if (bk.tx_on) bk_tx_part();
    if (bk.asking) {
        if ((int32_t)(fm1_ms - bk.ask_until) > 0) { bk.asking = 0; bk_state(BK_TIMEOUT); ctl_say("BACKUP NOT LOADED", 0); }
        else ctl_say(bk.ask, BOARD_SAVE_NAME ": YES");
    }
}

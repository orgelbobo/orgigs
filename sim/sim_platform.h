/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 for a computer: what platform.c gives the app, without the hardware. Shared by the native
 * host test (tests/host.c: a script in, a WAV and screens out) and the browser simulator (sim/wasm_host.c).
 * The app code (orgigs*.c) runs on it unchanged. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "ORGIGS FM-1"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

#if BOARD_SMK37
#define FM1_NBTN 18u                    /* the SMK-37's buttons (platform.c BTN_*), its 8 encoders (ENC_*) */
#define FM1_NENC 8u
#else
#define FM1_NBTN 14u
#define FM1_NENC 7u
#endif
static volatile uint32_t fm1_ms;
#if BOARD_SMK37
/* the SMK-37's extras (hal/smk_input.h): all 37 keys with velocity, pads as events with colours, faders, wheels */
static struct { uint32_t notes, buttons; uint64_t keys; uint8_t key_vel[37], fader[4]; uint16_t wheel[2], pedal, batt; uint32_t frames; } fm1_in;
static volatile uint8_t smk_pad_events, smk_master_hold;
static uint16_t host_padq[64];
static uint32_t host_padq_w, host_padq_r;
static uint8_t host_padrgb[16][3];
static uint32_t host_vel = 100;
static int32_t smk_pad_take(void) { return host_padq_r == host_padq_w ? -1 : host_padq[host_padq_r++ % 64u]; }
static int32_t smk_extkey_take(void) { return -1; }
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad < 16u) { host_padrgb[pad][0] = r; host_padrgb[pad][1] = g; host_padrgb[pad][2] = b; }
}
static void padled_flush(void) {}
static void host_pad(uint32_t pad, uint32_t vel) { if (pad < 16u && host_padq_w - host_padq_r < 64u) host_padq[host_padq_w++ % 64u] = (uint16_t)(pad << 8 | vel); }
#else
static struct { uint32_t notes, buttons; } fm1_in;
#endif
static uint32_t host_edges, host_released, host_note_edges;
static int32_t host_enc[FM1_NENC];
static uint8_t host_led[64];

static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_released;
    host_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t e = host_note_edges; host_note_edges = 0; return e; }
static int32_t fm1_enc_take(uint32_t e) { int32_t v = e < FM1_NENC ? host_enc[e] : 0; if (e < FM1_NENC) host_enc[e] = 0; return v; }
static void fm1_led_key(uint32_t id, int on) { if (id < 64u) host_led[id] = (uint8_t)(on != 0 ? 2 : 0); }
static void led_level(uint32_t id, uint32_t lv) { if (id < 64u) host_led[id] = (uint8_t)(lv > 2u ? 2u : lv); }
static uint8_t host_led_bg[64];         /* the backlight layer: a dark LED shows as glowing */
static void led_bg(uint32_t id, uint32_t on) { if (id < 64u) host_led_bg[id] = (uint8_t)(on != 0); }
static uint8_t host_bg_full;             /* the backlight at its top level: shown lit */
static void led_bg_level(uint32_t ns, uint32_t duty) { (void)ns; host_bg_full = duty >= 8u; }

static void host_button(uint32_t id, int down)
{
    if (id >= FM1_NBTN) return;
    if (down) { if (!(fm1_in.buttons >> id & 1u)) host_edges |= 1u << id; fm1_in.buttons |= 1u << id; }
    else { if (fm1_in.buttons >> id & 1u) host_released |= 1u << id; fm1_in.buttons &= ~(1u << id); }
}
#if BOARD_SMK37
static void host_keys(uint64_t mask)    /* all 37 keys, bit 0 = C3; a key going down gets host_vel */
{
    uint32_t k;
    for (k = 0; k < 37u; k++) if ((mask & ~fm1_in.keys) >> k & 1u) fm1_in.key_vel[k] = (uint8_t)host_vel;
    fm1_in.keys = mask & 0x1FFFFFFFFFull;
    fm1_in.notes = (uint32_t)(fm1_in.keys >> 5) & 0x7FFFFFFu;
}
#else
static void host_keys(uint32_t mask)
{
    host_note_edges |= mask & ~fm1_in.notes;
    fm1_in.notes = mask & 0x7FFFFFFu;
}
#endif

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static uint32_t fb_gen;                 /* counts the draws: the browser repaints when it changes */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    fb_gen++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    fb_gen++;
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

/* settings: two 4 KiB sectors, as the flash holds them: [valid][length 2 bytes][pad] then the bytes */
#define STORE_NSECT 2u
#define STORE_SECT 4096u
static uint8_t host_store[STORE_NSECT * STORE_SECT];
static void host_settings_saved(void);
static int store_load(uint32_t sect, void *p, uint32_t n)
{
    const uint8_t *h;
    if (sect >= STORE_NSECT || n > STORE_SECT - 12u) return -1;
    h = host_store + sect * STORE_SECT;
    if (h[0] != 1 || (uint32_t)(h[1] | h[2] << 8) != n) return -1;
    memcpy(p, h + 12, n);
    return 0;
}
static int store_save(uint32_t sect, const void *p, uint32_t n)
{
    uint8_t *h;
    if (sect >= STORE_NSECT || n > STORE_SECT - 12u) return -1;
    h = host_store + sect * STORE_SECT;
    h[0] = 1; h[1] = (uint8_t)n; h[2] = (uint8_t)(n >> 8);
    memcpy(h + 12, p, n);
    host_settings_saved();
    return 0;
}
static int settings_load(void *p, uint32_t n) { return store_load(0, p, n); }
static int settings_save(const void *p, uint32_t n) { return store_save(0, p, n); }
/* (OrGigs) blob sectors 1..4 (the Elite's rotary cabinets, the insert effect, the master, the Elite's panel); not kept
   by the browser simulator. host_blob_log: each save printed (tests/host.c: ORGIGS_BLOB_LOG=1) */
static uint8_t host_blobs[8][STORE_SECT];
static int host_blob_log;
static __attribute__((unused)) int blob_load(uint32_t i, void *p, uint32_t n)
{
    const uint8_t *host_blob = host_blobs[i < 8u ? i : 0u];
    if (i < 1u || i > 7u || n > STORE_SECT - 12u || host_blob[0] != 1 || (uint32_t)(host_blob[1] | host_blob[2] << 8) != n) return -1;
    memcpy(p, host_blob + 12, n);
    return 0;
}
static __attribute__((unused)) int blob_save(uint32_t i, const void *p, uint32_t n)
{
    uint8_t *host_blob = host_blobs[i < 8u ? i : 0u];
    if (i < 1u || i > 7u || n > STORE_SECT - 12u) return -1;
    host_blob[0] = 1; host_blob[1] = (uint8_t)n; host_blob[2] = (uint8_t)(n >> 8);
    memcpy(host_blob + 12, p, n);
    if (host_blob_log) printf("%6u ms  blob %u saved\n", fm1_ms, i);
    return 0;
}

/* USB-MIDI in: a small queue the host fills */
static uint8_t mq[256][3];
static uint32_t mq_w, mq_r;
static void host_midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (mq_w - mq_r >= 256u) return;
    mq[mq_w % 256u][0] = st; mq[mq_w % 256u][1] = d1; mq[mq_w % 256u][2] = d2;
    mq_w++;
}
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 256u][0]; *d1 = mq[mq_r % 256u][1]; *d2 = mq[mq_r % 256u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2);   /* the host's */
static uint32_t midi_out_room(void) { return 64u; }

/* SysEx (core/orgigs_backup.c): the host hands app_sysex the bytes between F0 and F7; sysex_send is the host's */
static int sysex_send(const uint8_t *p, uint32_t n);
static uint32_t sysex_room(uint32_t n) { (void)n; return 1; }
static uint32_t sim_usb_up = 1;          /* T usb 0: off a USB host (the battery shows no charging) */
static uint32_t usb_host_up(void) { return sim_usb_up; }
static uint32_t sim_batt;                 /* T batt RAW: the battery divider's raw ADC (0: none, nothing shown) */
static uint32_t batt_raw(void) { return sim_batt; }
static uint32_t usb_tx_count(void) { return 0; }
static void app_sysex(const uint8_t *p, uint32_t n);

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void app_init(void);
static void ui_step(void);
static void ui_strip_mod_local(uint32_t v);
static void ui_strip_pitch_local(int32_t b);

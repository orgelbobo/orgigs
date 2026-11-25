/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's entry points for JavaScript (sim/web/sim.js). Included at the end of orgigs.c. */

#define SIM_MAX_FRAMES 8192u
static int16_t sim_audio_buf[2u * SIM_MAX_FRAMES];

EMSCRIPTEN_KEEPALIVE void sim_init(void)
{
#if BOARD_SMK37
    fm1_in.wheel[0] = 512;              /* the pitch wheel rests in the middle */
    fm1_in.wheel[1] = 1023;             /* the mod wheel down (the Elite reads it backwards) */
#endif
    app_init();
}

/* one pass of the main loop, at the browser's clock */
EMSCRIPTEN_KEEPALIVE void sim_step(uint32_t now_ms) { fm1_ms = now_ms; ui_step(); }

/* the audio interrupt, as often as the browser asks: frames in blocks of 256, 16-bit stereo out */
EMSCRIPTEN_KEEPALIVE int16_t *sim_render(uint32_t frames)
{
    static int32_t blk[2u * HALF_FRAMES];
    uint32_t done, i;
    if (frames > SIM_MAX_FRAMES) frames = SIM_MAX_FRAMES;
    for (done = 0; done + HALF_FRAMES <= frames; done += HALF_FRAMES) {
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];
            sim_audio_buf[2u * done + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
    }
    return sim_audio_buf;
}

EMSCRIPTEN_KEEPALIVE uint16_t *sim_fb(void) { return fb; }
EMSCRIPTEN_KEEPALIVE uint32_t sim_fb_gen(void) { return fb_gen; }
EMSCRIPTEN_KEEPALIVE uint8_t *sim_leds(void)    /* the LEDs, the backlight as glow */
{
    static uint8_t v[41];
    uint32_t i;
    for (i = 0; i < 41u; i++) v[i] = host_led[i] ? host_led[i] : host_led_bg[i] ? (host_bg_full ? 2u : 1u) : 0u;
    return v;
}
EMSCRIPTEN_KEEPALIVE uint8_t *sim_store(void) { return host_store; }
EMSCRIPTEN_KEEPALIVE uint32_t sim_store_len(void) { return sizeof host_store; }

#if BOARD_SMK37
/* the Elite: 37 keys (lo = C3..C4+ bits 0..31, hi = bits 32..36), the velocity of the next keys, pads, faders, wheels,
   the pedal socket; the pads' colours (16 x r,g,b) */
EMSCRIPTEN_KEEPALIVE void sim_keys37(uint32_t lo, uint32_t hi) { host_keys((uint64_t)hi << 32 | lo); }
EMSCRIPTEN_KEEPALIVE void sim_vel(uint32_t v) { host_vel = v < 1u ? 1u : v > 127u ? 127u : v; }
EMSCRIPTEN_KEEPALIVE void sim_pad(uint32_t pad, uint32_t vel) { host_pad(pad, vel > 127u ? 127u : vel); }
EMSCRIPTEN_KEEPALIVE void sim_fader(uint32_t f, uint32_t v) { if (f < 4u) fm1_in.fader[f] = (uint8_t)(v > 127u ? 127u : v); }
EMSCRIPTEN_KEEPALIVE void sim_wheel(uint32_t w, uint32_t raw) { if (w < 2u) fm1_in.wheel[w] = (uint16_t)(raw > 1023u ? 1023u : raw); }
EMSCRIPTEN_KEEPALIVE void sim_pedal(uint32_t raw) { fm1_in.pedal = (uint16_t)(raw > 1023u ? 1023u : raw); }
EMSCRIPTEN_KEEPALIVE uint8_t *sim_padrgb(void) { return &host_padrgb[0][0]; }
#else
EMSCRIPTEN_KEEPALIVE void sim_keys(uint32_t mask) { host_keys(mask); }
#endif
EMSCRIPTEN_KEEPALIVE void sim_button(uint32_t id, int down) { host_button(id, down); }
EMSCRIPTEN_KEEPALIVE void sim_enc(uint32_t id, int32_t steps) { if (id < FM1_NENC) host_enc[id] += steps; }
EMSCRIPTEN_KEEPALIVE void sim_midi_in(int st, int d1, int d2) { host_midi_in((uint8_t)st, (uint8_t)d1, (uint8_t)d2); }
EMSCRIPTEN_KEEPALIVE void sim_strip_mod(int v) { ui_strip_mod_local((uint32_t)(v < 0 ? 0 : v > 127 ? 127 : v)); }
EMSCRIPTEN_KEEPALIVE void sim_strip_pitch(int b) { ui_strip_pitch_local(b < -8192 ? -8192 : b > 8191 ? 8191 : b); }

/* what the page shows next to the device */
EMSCRIPTEN_KEEPALIVE int32_t sim_stat(int i)
{
    switch (i) {
    case 0: return (int32_t)org_stat.peak;
    case 1: return (int32_t)org_stat.held;
    case 2: return (int32_t)org_stat.active_gens;
    case 3: return (int32_t)ui.mode;
    case 4: return (int32_t)org_par.model;
    case 5: return (int32_t)org_par.leslie;
    case 6: return (int32_t)org_par.expression;
    case 7: {                           /* the zones' MIDI-out channels */
        uint32_t z, m = 0;
        for (z = 0; z < ORG_NMAN; z++) if (zn.z[z].out_ch < CH_OFF) m |= 1u << zn.z[z].out_ch;
        return (int32_t)m;
    }
    case 8: return (int32_t)ui.ms;
    case 9: return (int32_t)mls.focus;
    case 10: return (int32_t)ORG_MODELS[org_par.model].dept;
    case 11: return (int32_t)mq_drop;
    case 12: return (int32_t)ui.grp;
    default: return 0;
    }
}

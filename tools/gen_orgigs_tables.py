#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate firmware/gen/orgigs_tables.h: every table OrGigs needs, as integers.

The FM-1 has no FPU, so wavetables, pitch increments, filter and reverb coefficients are computed
here once and written into the source as numbers.

  python3 tools/gen_orgigs_tables.py            (-> firmware/gen/orgigs_tables.h)
"""
import math
import random
from pathlib import Path

FS = 44100
NGEN = 84                 # generators C1 (MIDI 24) .. B7 (MIDI 107)
GEN0 = 24
WT_BITS = 10              # 1024 samples a cycle
WT_LEN = 1 << WT_BITS
NBANDS = 4                # band-limited copies of each table, by pitch range
BAND_TOP = [53, 71, 89, 107]          # highest MIDI note in each band
WT_PEAK = 16383           # Q14 peak: leaves headroom in the summing
OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "orgigs_tables.h"


def mtof(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def hmax_for_band(b):
    """highest harmonic that stays under ~18 kHz at the top note of band b"""
    return max(1, min(96, int(18000.0 / mtof(BAND_TOP[b]))))


# ---- the eight waveforms, as harmonic spectra (amplitude, phase) up to a limit
def dft_of(shape, nh, n=4096):
    """harmonics 1..nh of a periodic function shape(x), x in [0, 1)"""
    xs = [shape(i / n) for i in range(n)]
    mean = sum(xs) / n
    xs = [v - mean for v in xs]
    out = []
    for h in range(1, nh + 1):
        re = im = 0.0
        w = 2 * math.pi * h / n
        for i, v in enumerate(xs):
            re += v * math.cos(w * i)
            im += v * math.sin(w * i)
        out.append((2 * math.hypot(re, im) / n, math.atan2(re, im)))
    return out


def pulse(x, duty, edge=0.0):
    if edge <= 0:
        return 1.0 if x < duty else -duty / (1 - duty)
    # trapezoid: linear edges of width `edge`
    if x < edge:
        return -1 + 2 * x / edge
    if x < duty:
        return 1.0
    if x < duty + edge:
        return 1 - 2 * (x - duty) / edge
    return -1.0


def tri(x):
    return 4 * x - 1 if x < 0.5 else 3 - 4 * x


def saw(x):
    return 1 - 2 * x


NH = 96
SPECTRA = {}
# 1 tonewheel: a sine; band 0 and 1 get the faint odd harmonics of the low wheels
SPECTRA["TONEWHEEL"] = None
# 2 Brit Combo: triangle + narrow pulse from the dividers
SPECTRA["VOX"] = dft_of(lambda x: 0.75 * tri(x) + 0.45 * pulse(x, 0.12), NH)
# 3 Italo Combo: bright saw / pulse hybrid
SPECTRA["FARFISA"] = dft_of(lambda x: 0.6 * saw(x) + 0.5 * pulse(x, 0.25), NH)
# 4 Tokyo Combo: transistor complex wave, a little FM and a soft clip
SPECTRA["YAMAHA_YC"] = dft_of(lambda x: math.tanh(1.6 * math.sin(2 * math.pi * x + 1.1 * math.sin(4 * math.pi * x))), NH)
# 5 principal / open diapason: balanced even and odd
_pr = [1, .55, .42, .30, .22, .18, .13, .10, .08, .06, .05, .04]
SPECTRA["PIPE_PRINCIPAL"] = [((_pr[h - 1] if h <= len(_pr) else 0.04 / (h - len(_pr) + 1)) if h <= 24 else 0.0, 0.0)
                             for h in range(1, NH + 1)]
# 6 stopped flute / bourdon / gedackt: fundamental, soft 3rd and 5th
_fl = {1: 1.0, 2: 0.03, 3: 0.16, 4: 0.02, 5: 0.06, 7: 0.025}
SPECTRA["PIPE_FLUTE"] = [(_fl.get(h, 0.0), 0.0) for h in range(1, NH + 1)]
# 7 gamba / trumpet / reed: rich, a formant around the 7th
SPECTRA["PIPE_STRING_REED"] = [(0.8 / h ** 0.6 * (1 + 1.5 * math.exp(-((h - 7) / 3.0) ** 2)) if h <= 48 else 0.0, 0.0)
                               for h in range(1, NH + 1)]
# 8 free reed: trapezoid pulse, metallic presence around harmonics 4..10
_acc = dft_of(lambda x: pulse(x, 0.33, 0.035), NH)
SPECTRA["ACCORDION_REED"] = [(a * (1 + 0.8 * math.exp(-((h - 7) / 3.5) ** 2)), p) for h, (a, p) in enumerate(_acc, 1)]

# ---- OrGigs 2
# 9 Osaka King (a Japanese transistor combo): a hollow square with soft edges, a little triangle body
SPECTRA["OSAKA"] = dft_of(lambda x: 0.7 * pulse(x, 0.5, 0.06) + 0.3 * tri(x), NH)
# 10 Il Castellano (an Italian combo): buzzier than the Italo, a narrow pulse on a saw (its TONE knob is the filter)
SPECTRA["CASTELLANO"] = dft_of(lambda x: 0.55 * saw(x) + 0.6 * pulse(x, 0.16, 0.01), NH)
# 11 Dutch Rock (a Dutch valve organ): an uneven square from the valve dividers, a formant around the 3rd and 4th
_dr = dft_of(lambda x: pulse(x, 0.42, 0.08), NH)
SPECTRA["DUTCH"] = [(a * (1 + 0.6 * math.exp(-((h - 3.5) / 1.5) ** 2)) / (1 + h / 24), p) for h, (a, p) in enumerate(_dr, 1)]
# 12 dry free reed (the German accordions): a narrower pulse, its presence higher than the musette reed's
_dry = dft_of(lambda x: pulse(x, 0.27, 0.025), NH)
SPECTRA["REED_DRY"] = [(a * (1 + 0.9 * math.exp(-((h - 10) / 4.0) ** 2)), p) for h, (a, p) in enumerate(_dry, 1)]
# 13 helicon bass (the Steirische's bass reeds): wide, brassy, a strong formant around the 5th to 8th
_hel = dft_of(lambda x: pulse(x, 0.2, 0.02), NH)
SPECTRA["HELIKON"] = [(a * (1 + 1.6 * math.exp(-((h - 6) / 2.5) ** 2)), p) for h, (a, p) in enumerate(_hel, 1)]

# 14 Italian renaissance principal (wide, lead-rich pipes on low wind): a strong fundamental, the partials falling as
# 1/n^1.6 and damped above the 6th: soft, silvery, no edge
SPECTRA["PIPE_RENAIS"] = [(h ** -1.6 * (1.0 if h <= 6 else math.exp(-(h - 6) / 1.8)) if h <= 24 else 0.0, 0.0)
                          for h in range(1, NH + 1)]

# 15..17 the German romantic organ: everything dark and round, the spectrum falling off early (its warmth sits low)
# its principal: a strong fundamental, 1/n^1.5, damped above the 4th
SPECTRA["PIPE_ROMANTIC"] = [(h ** -1.5 * (1.0 if h <= 4 else math.exp(-(h - 4) / 2.5)) if h <= 20 else 0.0, 0.0)
                            for h in range(1, NH + 1)]
# its soft strings (salicional, aeoline): the fundamental held back, a gentle body around the 5th, gone above the 12th
SPECTRA["PIPE_STRING_SOFT"] = [((0.6 if h == 1 else 0.5 / h ** 0.7 * (1 + 0.9 * math.exp(-((h - 5) / 2.5) ** 2)))
                               * (1.0 if h <= 10 else math.exp(-(h - 10) / 2.0)) if h <= 24 else 0.0, 0.0)
                              for h in range(1, NH + 1)]
# its soft reed (oboe, the orchestral clarinet): the odd partials ahead, a low formant, filtered dark
SPECTRA["PIPE_REED_SOFT"] = [((1.0 if h % 2 else 0.45) / h ** 0.9 * (1 + 0.8 * math.exp(-((h - 4) / 2.0) ** 2))
                             * math.exp(-max(0, h - 8) / 3.0) if h <= 24 else 0.0, 0.0) for h in range(1, NH + 1)]

# 18 the baroque regal (short-resonator reed): a weak fundamental, a narrow nasal formant high up (around the 11th)
_rg = dft_of(lambda x: pulse(x, 0.18, 0.012), NH)
SPECTRA["PIPE_REGAL"] = [(a * (0.55 if h == 1 else 1.0) * (1 + 2.4 * math.exp(-((h - 11) / 2.2) ** 2))
                          * (1.0 if h <= 18 else math.exp(-(h - 18) / 3.0)), p) for h, (a, p) in enumerate(_rg, 1)]
# 19 the French chorus reed (trompette, clairon, bombarde): a rich saw driven into a soft clip, a broad brassy
# presence over the 4th to 12th
_an = dft_of(lambda x: math.tanh(2.2 * saw(x)), NH)
SPECTRA["PIPE_ANCHE"] = [(a * (1 + 1.1 * math.exp(-((h - 7) / 4.0) ** 2)) if h <= 48 else 0.0, p)
                         for h, (a, p) in enumerate(_an, 1)]

# 20 the Playroom (a toy chord organ: plastic free reeds blown by a small electric fan): a thin, quacking reed,
# the fundamental weak, a nasal hump around the 4th to 6th, the top rough
_toy = dft_of(lambda x: pulse(x, 0.1, 0.008), NH)
SPECTRA["TOY_REED"] = [(a * (0.5 if h == 1 else 1.0) * (1 + 1.8 * math.exp(-((h - 5) / 1.8) ** 2)) * (1.0 if h <= 16 else 0.6), p)
                       for h, (a, p) in enumerate(_toy, 1)]

WAVES = ["TONEWHEEL", "VOX", "FARFISA", "YAMAHA_YC", "PIPE_PRINCIPAL", "PIPE_FLUTE", "PIPE_STRING_REED", "ACCORDION_REED",
         "OSAKA", "CASTELLANO", "DUTCH", "REED_DRY", "HELIKON", "PIPE_RENAIS", "PIPE_ROMANTIC", "PIPE_STRING_SOFT",
         "PIPE_REED_SOFT", "PIPE_REGAL", "PIPE_ANCHE", "TOY_REED"]


def synth(spec, nh):
    out = []
    for i in range(WT_LEN):
        x = 2 * math.pi * i / WT_LEN
        v = 0.0
        for h in range(1, nh + 1):
            a, p = spec[h - 1]
            if a:
                v += a * math.sin(h * x + p)
        out.append(v)
    return out


def tonewheel(band):
    odd = {0: {3: 0.03, 5: 0.012}, 1: {3: 0.012}}.get(band, {})
    spec = [(1.0 if h == 1 else odd.get(h, 0.0), 0.0) for h in range(1, NH + 1)]
    return synth(spec, 5)


def wave_tables():
    tables = []
    for w in WAVES:
        bands = []
        for b in range(NBANDS):
            bands.append(tonewheel(b) if w == "TONEWHEEL" else synth(SPECTRA[w], hmax_for_band(b)))
        peak = max(max(abs(v) for v in t) for t in bands)          # one factor: no level step between bands
        tables.append([[int(round(v / peak * WT_PEAK)) for v in t] for t in bands])
    return tables


# ---- pitch
def inc_of(f):
    return int(round(f * 2 ** 32 / FS)) & 0xFFFFFFFF


def cents_k(c):           # (2^(c/1200) - 1) * 65536
    return int(round((2 ** (c / 1200.0) - 1) * 65536))


def main():
    rnd = random.Random(1792)
    lines = ["/* generated by tools/gen_orgigs_tables.py: do not edit */", "#pragma once", "#include <stdint.h>", ""]

    def arr(ctype, name, vals, per=12, dims=""):
        lines.append(f"static const {ctype} {name}{dims} = {{")
        for i in range(0, len(vals), per):
            lines.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
        lines.append("};")

    lines += [f"#define ORG_FS {FS}u", f"#define ORG_NGEN {NGEN}u", f"#define ORG_GEN0 {GEN0}u",
              f"#define ORG_WT_BITS {WT_BITS}u", f"#define ORG_WT_LEN {WT_LEN}u", f"#define ORG_NWAVE {len(WAVES)}u",
              f"#define ORG_NBANDS {NBANDS}u", ""]
    for i, w in enumerate(WAVES):
        lines.append(f"#define WT_{w} {i}u")
    lines.append("")

    # wavetables [wave][band][1024]
    tabs = wave_tables()
    lines.append(f"static const int16_t WT_ROM[{len(WAVES)}][{NBANDS}][{WT_LEN}] = {{")
    for w, t in zip(WAVES, tabs):
        lines.append(f"  {{ /* {w} */")
        for b in range(NBANDS):
            lines.append("    {")
            for i in range(0, WT_LEN, 16):
                lines.append("      " + ",".join(str(v) for v in t[b][i:i + 16]) + ",")
            lines.append("    },")
        lines.append("  },")
    lines.append("};")

    # band of each generator
    band = [next(b for b in range(NBANDS) if GEN0 + g <= BAND_TOP[b]) for g in range(NGEN)]
    arr("uint8_t", "GEN_BAND", band, 21, f"[{NGEN}]")
    # equal-tempered increments and the static micro-detune (+-1.2 cents) of pipes and reeds
    arr("uint32_t", "INC_ET", [inc_of(mtof(GEN0 + g)) for g in range(NGEN)], 6, f"[{NGEN}]")
    micro = [rnd.uniform(-1.2, 1.2) for _ in range(NGEN)]
    arr("uint32_t", "INC_MICRO", [inc_of(mtof(GEN0 + g) * 2 ** (micro[g] / 1200)) for g in range(NGEN)], 6, f"[{NGEN}]")
    # quarter-comma meantone (pure major thirds, the wolf between G# and Eb), the same micro-detune on top
    mt = [0.0, -23.95, -6.84, 10.27, -13.69, 3.42, -20.53, -3.42, -27.37, -10.26, 6.84, -17.11]   # C .. B, from C
    mt = [c - sum(mt) / 12 for c in mt]     # centred on equal temperament (A 1.7 cents flat): it stays with the others
    arr("uint32_t", "INC_MEANTONE", [inc_of(mtof(GEN0 + g) * 2 ** ((micro[g] + mt[(GEN0 + g) % 12]) / 1200))
                                     for g in range(NGEN)], 6, f"[{NGEN}]")
    wm = [0.0, -9.77, -7.82, -5.86, -9.77, -1.95, -11.73, -3.91, -7.82, -11.73, -3.91, -7.82]   # Werckmeister III
    wm = [c - sum(wm) / 12 for c in wm]     # centred on equal temperament, as the meantone
    arr("uint32_t", "INC_WERCKMEISTER", [inc_of(mtof(GEN0 + g) * 2 ** ((micro[g] + wm[(GEN0 + g) % 12]) / 1200))
                                         for g in range(NGEN)], 6, f"[{NGEN}]")
    trnd = random.Random(1972)              # the Playroom: each plastic reed a few cents off, never retuned
    toy = [trnd.uniform(-9.0, 9.0) for _ in range(NGEN)]
    arr("uint32_t", "INC_TOY", [inc_of(mtof(GEN0 + g) * 2 ** (toy[g] / 1200)) for g in range(NGEN)], 6, f"[{NGEN}]")
    lines.append(f"#define INC_VOCE_UMANA {inc_of(2.0)}u   /* +2 Hz: the voce umana beats the same at every pitch */")
    # per-generator spread of the detuned reed ranks (+-2 cents around the model's setting), as cents
    arr("int8_t", "DET_VAR", [rnd.randint(-2, 2) for _ in range(NGEN)], 21, f"[{NGEN}]")
    arr("uint16_t", "CENT_UP_K", [cents_k(c) for c in range(0, 25)], 13, "[25]")
    arr("uint16_t", "CENT_DN_K", [-cents_k(-c) for c in range(0, 25)], 13, "[25]")
    # pitch bend +-2 semitones in 1/32 steps (129 entries), Q16
    arr("uint32_t", "BEND_Q16", [int(round(2 ** ((i - 64) / 32 / 12) * 65536)) for i in range(129)], 8, "[129]")

    # drawbar levels 0..8, -3 dB a step, Q12
    arr("uint16_t", "DRAWBAR_Q12", [0] + [int(round(4096 * 10 ** (-3 * (8 - l) / 20))) for l in range(1, 9)], 9, "[9]")

    # asymmetric soft clip: y = tanh(x + b) - tanh(b) over x in [-4, 4), 1025 points, Q15
    b = 0.22
    ys = [math.tanh(-4 + 8 * i / 1024 + b) - math.tanh(b) for i in range(1025)]
    m = max(abs(v) for v in ys)
    arr("int16_t", "TANH_Q15", [int(round(v / m * 32767)) for v in ys], 16, "[1025]")

    def onepole(fc, fs=FS):           # y += a (x - y), Q15
        return int(round((1 - math.exp(-2 * math.pi * fc / fs)) * 32768))

    def svf_f(fc, fs=FS):             # Chamberlin SVF frequency coefficient, Q15
        return int(round(2 * math.sin(math.pi * min(fc, fs / 8) / fs) * 32768))

    lines += ["", f"#define LP_CAB_A {onepole(4500)}       /* 4.5 kHz cabinet */",
              f"#define LP_XOVER_A {onepole(800)}       /* 800 Hz rotary crossover */",
              f"#define LP_CAB_DUTCH_A {onepole(3000)}   /* 3 kHz: the Dutch Rock's small speaker */",
              f"#define LP_CAB_TOY_A {onepole(3800)}     /* the Playroom's plastic speaker: 3.8 kHz ... */",
              f"#define HP_TOY_A {onepole(320)}          /* ... and nothing below 320 Hz */",
              f"#define INC_FAN_HUM {inc_of(1.0)}u        /* the fan motor's hum: this per Hz */",
              f"#define LP_DELAY_A {onepole(3200, FS // 2)}   /* 3.2 kHz feedback damping, half rate */",
              f"#define CLICK_SVF_F {svf_f(2600)}     /* key click band */",
              f"#define AIR_SVF_F {svf_f(1400)}       /* bellows air */"]
    # chiff: band of noise near the 4th harmonic of the pipe
    arr("uint16_t", "CHIFF_F", [svf_f(4 * mtof(GEN0 + g)) for g in range(NGEN)], 12, f"[{NGEN}]")
    # bellows brightness: a one-pole low pass from 700 Hz (soft) to 9 kHz (full) over expression 0..127
    arr("uint16_t", "BRIGHT_A", [onepole(700 * (9000 / 700) ** (i / 127)) for i in range(128)], 16, "[128]")
    # the TONE knob of the Il Castellano: the cabinet's low pass from 1.5 kHz (dark) to 10 kHz (open) over 0..127
    arr("uint16_t", "CAB_TONE_A", [onepole(1500 * (10000 / 1500) ** (i / 127)) for i in range(128)], 16, "[128]")

    # cassotto body: a peak at 520 Hz (+5 dB, Q 1.1) as an RBJ biquad, Q14 coefficients
    def peak_biquad(fc, gdb, q):
        A = 10 ** (gdb / 40)
        w = 2 * math.pi * fc / FS
        al = math.sin(w) / (2 * q)
        b0, b1, b2 = 1 + al * A, -2 * math.cos(w), 1 - al * A
        a0, a1, a2 = 1 + al / A, -2 * math.cos(w), 1 - al / A
        return [int(round(v / a0 * 16384)) for v in (b0, b1, b2, a1, a2)]
    arr("int32_t", "CASSOTTO_BQ", peak_biquad(520, 5.0, 1.1), 5, "[5]")

    # rotary and LFO rates as phase increments a frame
    lfo = {"HORN_SLOW": 0.80, "HORN_FAST": 6.80, "DRUM_SLOW": 0.66, "DRUM_FAST": 5.90,
           "SCANNER": 6.90, "TREMULANT": 4.80, "TREM_REED": 5.60}
    lines.append("")
    for k, f in lfo.items():
        lines.append(f"#define INC_{k} {inc_of(f)}u   /* {f} Hz */")

    # reverb: a 4-line FDN per mode, run at half rate (22.05 kHz: its tail has nothing above 6.5 kHz).
    # lengths (samples at the half rate, primes), feedback gains for the RT60 (Q15), damping (Q15)
    RFS = FS // 2

    def prime_from(n):
        while any(n % q == 0 for q in range(2, int(n ** 0.5) + 1)):
            n += 1
        return n
    modes = [  # name, RT60 s, lines, damping fc, predelay ms, spring dispersion on
        ("SPRING", 2.4, [1031, 1201, 1361, 1489], 4200, 0, 1),
        ("STAGE", 0.9, [1559, 1693, 1867, 2053], 6500, 8, 0),
        ("HALL", 2.8, [2687, 3203, 3631, 4073], 5200, 22, 0),
        ("CATHEDRAL", 7.5, [4799, 5683, 6577, 7477], 3600, 45, 0),
    ]
    modes = [(n, rt, [prime_from(L // 2) for L in ls], fc, pre, sp) for n, rt, ls, fc, pre, sp in modes]
    lines.append("")
    for i in range(4):
        lines.append(f"#define REV_LINE_MAX{i} {max(m[2][i] for m in modes)}u")
    lines.append(f"#define REV_PRE_MAX {int(45 * RFS / 1000) + 8}u")
    lines.append("typedef struct { uint16_t len[4]; uint16_t g[4]; uint16_t damp; uint16_t pre; uint8_t spring; } rev_mode_t;")
    lines.append("static const rev_mode_t REV_MODES[4] = {")
    for name, rt, ls, fc, pre, sp in modes:
        gs = [int(round(10 ** (-3 * L / (rt * RFS)) * 32768)) for L in ls]
        lines.append(f"    {{{{{', '.join(map(str, ls))}}}, {{{', '.join(map(str, gs))}}}, {onepole(fc, RFS)}, "
                     f"{int(pre * RFS / 1000)}, {sp}}},   /* {name} {rt} s */")
    lines.append("};")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n")
    print(f"tables -> {OUT} ({OUT.stat().st_size // 1024} KB source)")


if __name__ == "__main__":
    main()

// SPDX-License-Identifier: GPL-3.0-only
// OrGigs FM-1 simulator: drives the firmware (compiled to WebAssembly, orgigs_sim.js) like the FM-1 would:
// the main loop every 4 ms, the audio "interrupt" whenever the browser wants sound, the panel from the page.
'use strict';

const BTN = [
  ['OCT-', 'octave down'], ['OCT+', 'octave up'], ['FX', 'effects screen'], ['SEL', 'shift · +key: setup · +GLO: lights'],
  ['ENV', 'drawbars 1–4'], ['LFO', 'drawbars 5–8'], ['EDIT', 'drawbars 1 3 4 + 9'], ['GLO', 'MIDI zones · next screen'],
  ['HOME', 'MIDI mixer / drawbars · SEL: panic'], ['SAVE', 'tap: drawbars to USER · +key: setup'], ['ARP', 'vibrato / tremulant'], ['SEQ', 'coupler II/I'],
  ['PLAY', 'Rotary brake'], ['REC', 'Rotary slow/fast · SEL: manual via rotary'],
];
// as on the FM-1: two rows of six, the octave buttons in front
const BTN_ORDER = [0, 2, 3, 4, 5, 6, 7, 1, 8, 9, 10, 11, 12, 13];
const ENC_NAMES = ['SELECT', 'ALGORITHM', 'KNOB 1', 'KNOB 2', 'KNOB 3', 'KNOB 4', 'PRESETS'];
const KEYMAP = {
  z: 0, s: 1, x: 2, d: 3, c: 4, f: 5, v: 6, b: 7, h: 8, n: 9, j: 10, m: 11, ',': 12, l: 13, '.': 14, ';': 15, '/': 16,
  q: 16, '2': 17, w: 18, e: 19, '4': 20, r: 21, '5': 22, t: 23, y: 24, '7': 25, u: 26,
};
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const KEY0 = 53;
// what KNOB1..4 do: per mode (play: per drawbar group; MIDI: per screen)
const DB_FN = [
  [['DB1 16\'', ''], ['DB2 5⅓\'', ''], ['DB3 8\'', ''], ['DB4 4\'', '']],
  [['DB5 2⅔\'', ''], ['DB6 2\'', ''], ['DB7 1⅗\'', ''], ['DB8 1⅓\'', '']],
  [['DB1 16\'', ''], ['DB3 8\'', ''], ['DB4 4\'', ''], ['DB9 1\'', '']],
];
const FX_FN = [['Drive / Chiff / Detune', 'SEL: percussion'], ['Vibrato / Tremulant', 'SEL: delay feedback'],
  ['Delay mix', 'SEL: delay time'], ['Reverb mix', 'SEL: reverb space']];
const MIDI_FN = [
  [['MIDI in channel', 'SEL: out channel'], ['Lowest key', 'SEL: highest key'], ['Transpose', 'SEL: octave'], ['Fixed velocity', 'SEL: bend PITCH / ROTARY']],
  [['Channel', ''], ['Its octave', ''], ['', ''], ['', '']],
  [['Channels from', ''], ['Sends VEL/CC7/CC11', ''], ['', ''], ['', '']],
  [['', ''], ['', ''], ['', ''], ['', '']],
];
const ENC_FN = [['Manual / zone', ''], ['Model 1–8', ''], ['Drawbar preset 1–16', '']];

let M = null, ctx = null, proc = null, masterGain = 1, keysMask = 0, midiOut = null, midiAccess = null;
let loadAvg = 0, lastGen = -1, shiftKey = false, outPeak = 0, outCalls = 0;
const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------- panel
function buildPanel() {
  const knobs = $('knobs'), encs = $('encs');
  ENC_NAMES.forEach((name, id) => {
    const el = document.createElement('div');
    el.className = 'enc';
    el.innerHTML = `<div class="name">${name}</div><div class="knob" tabindex="0" aria-label="${name}"></div>` +
      `<div class="steps"><button aria-label="${name} left">◀</button><button aria-label="${name} right">▶</button></div>` +
      `<div class="fn" id="fn${id}"></div>`;
    (id >= 2 && id <= 5 ? knobs : encs).appendChild(el);
    const knob = el.querySelector('.knob');
    let rot = 0, dragY = null, acc = 0;
    const turn = (n) => { if (!M || !n) return; M._sim_enc(id, n); rot += n * 18; knob.style.setProperty('--rot', rot + 'deg'); };
    knob.addEventListener('wheel', (e) => { e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1); }, { passive: false });
    knob.addEventListener('pointerdown', (e) => { dragY = e.clientY; acc = 0; knob.setPointerCapture(e.pointerId); });
    knob.addEventListener('pointermove', (e) => {
      if (dragY === null) return;
      acc += dragY - e.clientY; dragY = e.clientY;
      const n = Math.trunc(acc / 8); if (n) { acc -= n * 8; turn(n); }
    });
    knob.addEventListener('pointerup', () => { dragY = null; });
    knob.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { turn(1); e.preventDefault(); }
      if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { turn(-1); e.preventDefault(); }
    });
    const [l, r] = el.querySelectorAll('.steps button');
    l.addEventListener('click', () => turn(-1));
    r.addEventListener('click', () => turn(1));
  });

  const btns = $('btns');
  BTN_ORDER.forEach((id) => {
    const el = document.createElement('div');
    el.className = 'btn';
    el.id = 'btn' + id;
    el.innerHTML = `<span class="led"></span><div class="lbl">${BTN[id][0]}</div><div class="fn">${BTN[id][1]}</div>`;
    el.addEventListener('pointerdown', (e) => { el.setPointerCapture(e.pointerId); el.classList.add('down'); M && M._sim_button(id, 1); });
    const up = () => { if (!el.classList.contains('down')) return; el.classList.remove('down'); M && M._sim_button(id, 0); };
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', up);
    btns.appendChild(el);
  });

  const kb = $('kb');
  let w = 0;
  const WW = 46;
  for (let k = 0; k < 27; k++) {
    const note = KEY0 + k, black = (0x54A >> (note % 12)) & 1;
    const el = document.createElement('div');
    el.className = black ? 'bk' : 'wk';
    el.dataset.k = k;
    const SK = [0, 2, 4, 6, 7, 9, 11, 12], si = SK.indexOf(k);
    const label = si >= 0 ? `S${si + 1}` : '';
    el.innerHTML = `<span class="kl">${black ? '' : NOTE_NAMES[note % 12] + (Math.floor(note / 12) - 1)}<br>${label}</span><span class="kled"></span>`;
    if (black) { el.style.left = (w * WW - 14) + 'px'; el.style.width = '28px'; }
    else { el.style.left = (w * WW) + 'px'; el.style.width = (WW - 2) + 'px'; w++; }
    kb.appendChild(el);
  }
  kb.style.width = (w * WW) + 'px';
  const ptrKey = new Map();
  const press = (k, on) => { keysMask = on ? keysMask | (1 << k) : keysMask & ~(1 << k); M && M._sim_keys(keysMask >>> 0); };
  kb.addEventListener('pointerdown', (e) => {
    const t = e.target.closest('[data-k]'); if (!t) return;
    kb.setPointerCapture(e.pointerId);
    const k = +t.dataset.k; ptrKey.set(e.pointerId, k); press(k, true);
  });
  kb.addEventListener('pointermove', (e) => {
    if (!ptrKey.has(e.pointerId)) return;
    const t = document.elementFromPoint(e.clientX, e.clientY)?.closest('[data-k]'); if (!t) return;
    const k = +t.dataset.k, old = ptrKey.get(e.pointerId);
    if (k !== old) { press(old, false); ptrKey.set(e.pointerId, k); press(k, true); }
  });
  const kup = (e) => { if (!ptrKey.has(e.pointerId)) return; press(ptrKey.get(e.pointerId), false); ptrKey.delete(e.pointerId); };
  kb.addEventListener('pointerup', kup);
  kb.addEventListener('pointercancel', kup);

  window.addEventListener('keydown', (e) => {
    if (e.target.tagName === 'SELECT' || e.target.tagName === 'INPUT') return;
    if (e.key === 'Shift' && !shiftKey) { shiftKey = true; M && M._sim_button(3, 1); return; }
    const k = KEYMAP[e.key.toLowerCase()];
    if (k === undefined || e.repeat || e.ctrlKey || e.metaKey) return;
    press(k, true); e.preventDefault();
  });
  window.addEventListener('keyup', (e) => {
    if (e.key === 'Shift') { shiftKey = false; M && M._sim_button(3, 0); return; }
    const k = KEYMAP[e.key.toLowerCase()];
    if (k !== undefined) press(k, false);
  });
  window.addEventListener('blur', () => { keysMask = 0; M && M._sim_keys(0); });

  $('mod').addEventListener('input', (e) => { $('modv').textContent = e.target.value; M && M._sim_strip_mod(+e.target.value); });
  const pitch = $('pitch');
  pitch.addEventListener('input', () => { $('pitchv').textContent = pitch.value; M && M._sim_strip_pitch(+pitch.value); });
  const springBack = () => { pitch.value = 0; $('pitchv').textContent = '0'; M && M._sim_strip_pitch(0); };
  pitch.addEventListener('pointerup', springBack);
  pitch.addEventListener('change', springBack);
  // MASTER: 70 % = 2.4 (a full chord on both manuals and the pedal then peaks near -2 dB; one note ~ -15 dB)
  const setMaster = (v) => { masterGain = (v / 70) ** 2 * 2.4; };
  $('master').addEventListener('input', (e) => setMaster(+e.target.value));
  setMaster(+$('master').value);
}

// ---------------------------------------------------------------- screen and LEDs
const lcd = $('lcd').getContext('2d');
const img = lcd.createImageData(240, 240);
function drawScreen() {
  if (!M) return;
  const gen = M._sim_fb_gen();
  if (gen !== lastGen) {
    lastGen = gen;
    const fb = M.HEAPU16.subarray(M._sim_fb() >> 1, (M._sim_fb() >> 1) + 240 * 240), d = img.data;
    for (let i = 0, j = 0; i < 57600; i++, j += 4) {
      const c = fb[i];
      d[j] = ((c >> 11) * 527 + 23) >> 6; d[j + 1] = (((c >> 5) & 63) * 259 + 33) >> 6; d[j + 2] = ((c & 31) * 527 + 23) >> 6; d[j + 3] = 255;
    }
    lcd.putImageData(img, 0, 0);
  }
  const leds = M.HEAPU8.subarray(M._sim_leds(), M._sim_leds() + 41);
  for (let b = 0; b < 14; b++) $('btn' + b).querySelector('.led').classList.toggle('on', !!leds[b]);
  document.querySelectorAll('#kb [data-k]').forEach((el) => {
    const k = +el.dataset.k;
    el.querySelector('.kled').classList.toggle('on', leds[14 + k] === 2);
    el.querySelector('.kled').classList.toggle('dim', leds[14 + k] === 1);
    el.classList.toggle('down', !!(keysMask >> k & 1));
  });
  // knob functions for the bank we are in
  const mode = M._sim_stat(3), fnTab = mode === 1 ? FX_FN : mode === 2 ? MIDI_FN[M._sim_stat(8)] : M._sim_stat(9) === 2 ? DB_FN[0] : DB_FN[M._sim_stat(12)];
  for (let i = 0; i < 4; i++) { const f = fnTab[i]; $('fn' + (i + 2)).innerHTML = `${f[0]}${f[1] ? '<br><span style="color:#8c8d99">' + f[1] + '</span>' : ''}`; }
  [0, 1, 6].forEach((id, i) => { const f = ENC_FN[i]; $('fn' + id).innerHTML = `${f[0]}${f[1] ? '<br><span style="color:#8c8d99">' + f[1] + '</span>' : ''}`; });
  // meters
  const peak = M._sim_stat(0);
  $('m-peak').textContent = peak ? (20 * Math.log10(peak / 32767)).toFixed(1) + ' dBFS' : '-∞';
  $('m-peakbar').style.width = Math.min(100, peak / 327.67) + '%';
  $('m-peakbar').style.background = peak > 30000 ? '#ff4a3a' : '';
  $('m-gen').textContent = `${M._sim_stat(1)} / ${M._sim_stat(2)}`;
  const mask = M._sim_stat(7);
  const chs = []; for (let c = 0; c < 16; c++) if (mask >> c & 1) chs.push(c + 1);
  $('m-ch').textContent = chs.length ? chs.join(' ') : '1';
  $('m-load').textContent = ctx ? (loadAvg * 100).toFixed(1) + ' %' : '–';
  if (ctx) {   // the output: is anything coming out, how loud (0 dB = the browser's ceiling)
    const db = outPeak > 1e-5 ? 20 * Math.log10(outPeak) : -99;
    $('m-out').textContent = ctx.state !== 'running' ? ctx.state : !outCalls ? 'no audio callback!' : db <= -98 ? 'silence' : db.toFixed(1) + ' dB' + (db > -0.1 ? ' CLIP' : '');
    $('m-outbar').style.width = Math.max(0, Math.min(100, (db + 60) / 60 * 100)) + '%';
  }
  $('m-loadbar').style.width = Math.min(100, loadAvg * 100) + '%';
  requestAnimationFrame(drawScreen);
}

// ---------------------------------------------------------------- audio
function startAudio() {
  if (ctx) { ctx.state === 'running' ? ctx.suspend() : ctx.resume(); updateStart(); return; }
  try { ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' }); }
  catch (e) { ctx = new AudioContext(); }
  const N = 1024;
  proc = ctx.createScriptProcessor(N, 0, 2);
  proc.onaudioprocess = (e) => {
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    if (!M) { L.fill(0); R.fill(0); return; }
    const t0 = performance.now();
    const p = M._sim_render(N) >> 1, h = M.HEAP16, g = masterGain / 32768;
    let pk = 0;
    for (let i = 0; i < N; i++) {
      const l = h[p + 2 * i] * g, r = h[p + 2 * i + 1] * g;
      L[i] = l > 1 ? 1 : l < -1 ? -1 : l; R[i] = r > 1 ? 1 : r < -1 ? -1 : r;
      if (l > pk) pk = l; else if (-l > pk) pk = -l;
    }
    outPeak = Math.max(pk, outPeak * 0.8);
    outCalls++;
    const used = (performance.now() - t0) / (N / 44.1);
    loadAvg += (used - loadAvg) * 0.1;
  };
  proc.connect(ctx.destination);
  updateStart();
}
function updateStart() {
  const run = ctx && ctx.state === 'running';
  $('start').textContent = run ? 'Audio on (click to pause)' : 'Start audio';
  $('start').classList.toggle('running', !!run);
  if (ctx && ctx.sampleRate !== 44100) $('start').title = `browser runs at ${ctx.sampleRate} Hz: pitch is off by ${(ctx.sampleRate / 44100).toFixed(3)}x`;
}
$('start').addEventListener('click', startAudio);

// ---------------------------------------------------------------- MIDI
const CH_COL = ['#ffb030', '#28c878', '#46a0ff', '#ff6a5a', '#c27cff', '#e8e050', '#40d8d0', '#ff8ac8',
  '#9ad04a', '#ff9a40', '#7a8cff', '#d0a070', '#60e0a0', '#f070f0', '#a0a0a0', '#ffffff'];
const logLines = [];
function logMidi(st, d1, d2) {
  const ch = (st & 15) + 1, t = st & 0xF0;
  const what = t === 0x90 ? (d2 ? `note on  ${noteName(d1)} vel ${d2}` : `note off ${noteName(d1)}`) : t === 0x80 ? `note off ${noteName(d1)}`
    : t === 0xB0 ? `CC ${d1} = ${d2}` : t === 0xE0 ? `bend ${((d2 << 7) | d1) - 8192}` : `${st.toString(16)} ${d1} ${d2}`;
  logLines.push(`<span class="ch" style="background:${CH_COL[ch - 1]}">ch ${ch}</span>${what}`);
  if (logLines.length > 200) logLines.shift();
  scheduleLog();
}
let logPending = false;
function scheduleLog() {
  if (logPending) return;
  logPending = true;
  requestAnimationFrame(() => { logPending = false; const el = $('log'); el.innerHTML = logLines.join('\n'); el.scrollTop = el.scrollHeight; });
}
const noteName = (n) => NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
$('clearLog').addEventListener('click', () => { logLines.length = 0; scheduleLog(); });

async function enableMidi() {
  if (!navigator.requestMIDIAccess) { $('midiOn').textContent = 'No Web MIDI in this browser'; return; }
  try { midiAccess = await navigator.requestMIDIAccess(); }
  catch (e) { $('midiOn').textContent = 'MIDI refused (serve over http://localhost)'; return; }
  $('midiOn').textContent = 'Web MIDI on';
  const fill = () => {
    const sin = $('midiIn'), sout = $('midiOut'), vi = sin.value, vo = sout.value;
    sin.innerHTML = '<option value="">—</option>'; sout.innerHTML = '<option value="">—</option>';
    midiAccess.inputs.forEach((p) => sin.add(new Option(p.name, p.id)));
    midiAccess.outputs.forEach((p) => sout.add(new Option(p.name, p.id)));
    sin.value = vi; sout.value = vo;
  };
  fill();
  midiAccess.onstatechange = fill;
}
$('midiOn').addEventListener('click', enableMidi);
$('midiIn').addEventListener('change', (e) => {
  midiAccess.inputs.forEach((p) => { p.onmidimessage = null; });
  const p = midiAccess.inputs.get(e.target.value);
  if (p) p.onmidimessage = (m) => { const d = m.data; if (d.length >= 2 && d[0] >= 0x80 && d[0] < 0xF0 && M) M._sim_midi_in(d[0], d[1], d[2] || 0); };
});
$('midiOut').addEventListener('change', (e) => { midiOut = midiAccess.outputs.get(e.target.value) || null; });

// ---------------------------------------------------------------- boot
buildPanel();
OrGigsModule({
  onMidiOut: (st, d1, d2) => { logMidi(st, d1, d2); if (midiOut) midiOut.send([st, d1, d2]); },
  onSettingsSaved: () => {
    try { localStorage.setItem('orgigs.settings', JSON.stringify(Array.from(M.HEAPU8.subarray(M._sim_store(), M._sim_store() + M._sim_store_len())))); } catch (e) { /* private window */ }
  },
}).then((m) => {
  M = m;
  try {
    const saved = JSON.parse(localStorage.getItem('orgigs.settings') || 'null');
    if (Array.isArray(saved) && saved.length === M._sim_store_len()) M.HEAPU8.set(saved, M._sim_store());
  } catch (e) { /* nothing saved */ }
  M._sim_init();
  const t0 = performance.now();
  setInterval(() => M._sim_step(Math.floor(performance.now() - t0) + 1), 4);   // the main loop
  requestAnimationFrame(drawScreen);
});

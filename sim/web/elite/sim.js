// SPDX-License-Identifier: GPL-3.0-only
// OrGigs Elite simulator: drives the Elite firmware (BOARD_SMK37, compiled to WebAssembly, orgigs_sim.js) like the
// keyboard would: the main loop every 4 ms, the audio "interrupt" whenever the browser wants sound, the panel from the
// page (sim/wasm_exports.c: sim_keys37, sim_pad, sim_fader, sim_wheel, sim_pedal, sim_padrgb).
'use strict';

// the buttons by id (firmware/src/platform.c BTN_*, hal/smk_input.h: the printed names)
const BTN = ['KNOB BANK', 'FADER BANK', '<', '>', 'OCT−', 'OCT+', 'ARP', 'N.RPT', 'SCALE', 'CHORD', 'GLOBE', 'BT',
  'PATCH', 'PARA', 'FX', 'SEQ', 'PLAY', 'REC'];
const BTN_SCALE = 8;
const LEFT_ORDER = [0, 1, 2, 3, 4, 5];
const RIGHT_ORDER = [6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
const KEYMAP = {   // computer keys -> key 0..36 (0 = C3)
  z: 0, s: 1, x: 2, d: 3, c: 4, v: 5, g: 6, b: 7, h: 8, n: 9, j: 10, m: 11,
  q: 12, '2': 13, w: 14, '3': 15, e: 16, r: 17, '5': 18, t: 19, '6': 20, y: 21, '7': 22, u: 23,
  i: 24, '9': 25, o: 26, '0': 27, p: 28,
};
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const KEY0 = 48;   // C3

let M = null, ctx = null, masterGain = 2.4, keys = 0n, midiOut = null, midiAccess = null;
let loadAvg = 0, lastGen = -1, shiftKey = false, outPeak = 0, outCalls = 0, susDown = false;
const $ = (id) => document.getElementById(id);
const call = (f, ...a) => { if (M) M['_' + f](...a); };

// ---------------------------------------------------------------- panel
function makeBtn(id) {
  const el = document.createElement('div');
  el.className = 'btn';
  el.id = 'btn' + id;
  el.innerHTML = `<span class="led${id === 17 ? ' red' : ''}"></span>${BTN[id].replace('<', '&lt;').replace('>', '&gt;')}`;
  el.addEventListener('pointerdown', (e) => { el.setPointerCapture(e.pointerId); el.classList.add('down'); call('sim_button', id, 1); });
  const up = () => { if (!el.classList.contains('down')) return; el.classList.remove('down'); call('sim_button', id, 0); };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
  return el;
}

function makeKnob(id) {
  const el = document.createElement('div');
  el.className = 'enc';
  el.innerHTML = `<div class="knob" tabindex="0" aria-label="K${id + 1}"></div><div class="lbl">K${id + 1}</div>`;
  const knob = el.firstChild;
  let rot = 0, dragY = null, acc = 0;
  const turn = (n) => { if (!n) return; call('sim_enc', id, n); rot += n * 18; knob.style.setProperty('--rot', rot + 'deg'); };
  knob.addEventListener('wheel', (e) => { e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1); }, { passive: false });
  knob.addEventListener('pointerdown', (e) => { dragY = e.clientY; acc = 0; knob.setPointerCapture(e.pointerId); knob.focus(); });
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
  return el;
}

function makePad(p) {
  const el = document.createElement('div');
  el.className = 'pad dark';
  el.id = 'pad' + p;
  el.innerHTML = `<span>${p + 1}</span>`;
  el.addEventListener('pointerdown', (e) => {
    el.setPointerCapture(e.pointerId); el.classList.add('down');
    call('sim_pad', p, e.shiftKey ? 127 : +$('vel').value);
  });
  const up = () => { if (!el.classList.contains('down')) return; el.classList.remove('down'); call('sim_pad', p, 0); };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
  return el;
}

// a wheel: drag the thumb; raw 0..1023. Pitch springs back to 512; the mod wheel stays (raw 1023 = down)
function makeWheel(el, w, rest, spring) {
  const thumb = el.firstChild;
  let raw = rest;
  // display: up = top. For the mod wheel raw runs the other way, so the thumb follows f, not raw
  const showF = (f) => { const h = el.clientHeight - 28; thumb.style.top = (1 + (1 - f) * h) + 'px'; };
  let drag = false;
  el.addEventListener('pointerdown', (e) => { drag = true; el.setPointerCapture(e.pointerId); move(e); });
  const move = (e) => {
    if (!drag) return;
    const r = el.getBoundingClientRect(), f = 1 - Math.min(1, Math.max(0, (e.clientY - r.top - 14) / (r.height - 28)));
    raw = Math.round(w === 1 ? (1 - f) * 1023 : f * 1023);
    call('sim_wheel', w, raw); showF(f);
  };
  el.addEventListener('pointermove', move);
  const up = () => { drag = false; if (spring) { raw = rest; call('sim_wheel', w, raw); showF(0.5); } };
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
  el.addEventListener('wheel', (e) => {
    e.preventDefault();
    if (spring) return;
    const f = Math.min(1, Math.max(0, (w === 1 ? 1 - raw / 1023 : raw / 1023) + (e.deltaY < 0 ? 0.05 : -0.05)));
    raw = Math.round(w === 1 ? (1 - f) * 1023 : f * 1023); call('sim_wheel', w, raw); showF(f);
  }, { passive: false });
  requestAnimationFrame(() => showF(w === 1 ? 0 : 0.5));
}

function buildPanel() {
  const left = $('left'), wheels = left.firstElementChild;
  LEFT_ORDER.forEach((id) => left.insertBefore(makeBtn(id), wheels));
  makeWheel($('pw'), 0, 512, true);
  makeWheel($('mw'), 1, 1023, false);

  for (let k = 0; k < 8; k++) $('knobs').appendChild(makeKnob(k));
  for (let p = 0; p < 16; p++) $('pads').appendChild(makePad(p));

  const right = $('right'), vel = right.firstElementChild;
  const FADER_START = [127, 40, 100, 100];   // F1 up (drawbar 9 at 0), F4 the volume
  for (let f = 0; f < 4; f++) {
    const el = document.createElement('div');
    el.className = 'fader';
    el.style.gridColumn = String(f + 1); el.style.gridRow = '1';
    el.innerHTML = `<input type="range" min="0" max="127" value="${FADER_START[f]}" aria-label="F${f + 1}"><div class="lbl">F${f + 1}</div>`;
    const inp = el.firstChild;
    inp.addEventListener('input', () => { call('sim_fader', f, +inp.value); if (f === 3) setMaster(+inp.value); });
    inp.id = 'f' + f;
    right.insertBefore(el, vel);
  }
  vel.style.gridRow = '1';
  RIGHT_ORDER.forEach((id) => right.appendChild(makeBtn(id)));

  $('vel').addEventListener('input', (e) => { $('velv').textContent = e.target.value; call('sim_vel', +e.target.value); });

  // the pedal socket: nothing plugged in reads 0; a sustain pedal its ends; an expression pedal the travel
  const sus = $('sus');
  const pedal = () => call('sim_pedal', $('expOn').checked ? +$('exp').value : susDown ? 1023 : 0);
  sus.addEventListener('pointerdown', (e) => { sus.setPointerCapture(e.pointerId); susDown = true; sus.classList.add('down'); pedal(); });
  const susUp = () => { susDown = false; sus.classList.remove('down'); pedal(); };
  sus.addEventListener('pointerup', susUp);
  sus.addEventListener('pointercancel', susUp);
  $('expOn').addEventListener('change', (e) => { $('exp').disabled = !e.target.checked; $('expv').textContent = e.target.checked ? $('exp').value : '–'; pedal(); });
  $('exp').addEventListener('input', (e) => { $('expv').textContent = e.target.value; pedal(); });

  // the 37 keys
  const kb = $('kb');
  let w = 0;
  const WW = 30;
  for (let k = 0; k < 37; k++) {
    const note = KEY0 + k, black = (0x54A >> (note % 12)) & 1;
    const el = document.createElement('div');
    el.className = black ? 'bk' : 'wk';
    el.dataset.k = k;
    el.innerHTML = black ? '' : `<span class="kl">${note % 12 === 0 ? 'C' + (Math.floor(note / 12) - 1) : ''}</span>`;
    if (black) { el.style.left = (w * WW - 9) + 'px'; el.style.width = '18px'; }
    else { el.style.left = (w * WW) + 'px'; el.style.width = (WW - 2) + 'px'; w++; }
    kb.appendChild(el);
  }
  kb.style.width = (w * WW) + 'px';
  const ptrKey = new Map();
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
    if (e.key === 'Shift' && !shiftKey) { shiftKey = true; call('sim_button', BTN_SCALE, 1); return; }
    const k = KEYMAP[e.key.toLowerCase()];
    if (k === undefined || e.repeat || e.ctrlKey || e.metaKey) return;
    press(k, true); e.preventDefault();
  });
  window.addEventListener('keyup', (e) => {
    if (e.key === 'Shift') { shiftKey = false; call('sim_button', BTN_SCALE, 0); return; }
    const k = KEYMAP[e.key.toLowerCase()];
    if (k !== undefined) press(k, false);
  });
  window.addEventListener('blur', () => { keys = 0n; sendKeys(); });
}
function sendKeys() { call('sim_keys37', Number(keys & 0xFFFFFFFFn) >>> 0, Number(keys >> 32n)); }
function press(k, on) { const b = 1n << BigInt(k); keys = on ? keys | b : keys & ~b; sendKeys(); }
// F4: the volume. At 100 the gain is 2.4 (a full chord on both manuals and the pedal then peaks near -2 dB)
function setMaster(v) { masterGain = (v / 100) ** 2 * 2.4; }

// ---------------------------------------------------------------- screen, LEDs, pads
const lcd = $('lcd').getContext('2d');
const img = lcd.createImageData(240, 240);
const padEls = [], btnLeds = [];
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
  for (let b = 0; b < 18; b++) btnLeds[b].classList.toggle('on', leds[b] === 2 || (b === 16 && leds[18] === 2));
  // the pads: the LED values are what the WS2812s get (scaled by the brightness); shown brighter, as the eye sees them
  const rgb = M.HEAPU8.subarray(M._sim_padrgb(), M._sim_padrgb() + 48);
  for (let p = 0; p < 16; p++) {
    const r = rgb[3 * p], g = rgb[3 * p + 1], b = rgb[3 * p + 2], mx = Math.max(r, g, b);
    const el = padEls[p];
    if (!mx) { el.style.background = ''; el.classList.add('dark'); continue; }
    const s = Math.sqrt(mx / 255) * 255 / mx;
    el.style.background = `rgb(${Math.min(255, r * s) | 0},${Math.min(255, g * s) | 0},${Math.min(255, b * s) | 0})`;
    el.classList.toggle('dark', mx * s < 90);
  }
  document.querySelectorAll('#kb [data-k]').forEach((el) => el.classList.toggle('down', !!(keys >> BigInt(+el.dataset.k) & 1n)));
  $('m-gen').textContent = `${M._sim_stat(1)} / ${M._sim_stat(2)}`;
  const mask = M._sim_stat(7);
  const chs = []; for (let c = 0; c < 16; c++) if (mask >> c & 1) chs.push(c + 1);
  $('m-ch').textContent = chs.length ? chs.join(' ') : '–';
  $('m-load').textContent = ctx ? (loadAvg * 100).toFixed(1) + ' %' : '–';
  $('m-loadbar').style.width = Math.min(100, loadAvg * 100) + '%';
  if (ctx) {
    const db = outPeak > 1e-5 ? 20 * Math.log10(outPeak) : -99;
    $('m-out').textContent = ctx.state !== 'running' ? ctx.state : !outCalls ? 'no audio callback!' : db <= -98 ? 'silence' : db.toFixed(1) + ' dB' + (db > -0.1 ? ' CLIP' : '');
    $('m-outbar').style.width = Math.max(0, Math.min(100, (db + 60) / 60 * 100)) + '%';
  }
  requestAnimationFrame(drawScreen);
}

// ---------------------------------------------------------------- audio
function startAudio() {
  if (ctx) { ctx.state === 'running' ? ctx.suspend() : ctx.resume(); updateStart(); return; }
  try { ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' }); }
  catch (e) { ctx = new AudioContext(); }
  const N = 1024;
  const proc = ctx.createScriptProcessor(N, 0, 2);
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
    loadAvg += ((performance.now() - t0) / (N / 44.1) - loadAvg) * 0.1;
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
const noteName = (n) => NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
function logMidi(st, d1, d2) {
  const ch = (st & 15) + 1, t = st & 0xF0;
  const what = t === 0x90 ? (d2 ? `note on  ${noteName(d1)} vel ${d2}` : `note off ${noteName(d1)}`) : t === 0x80 ? `note off ${noteName(d1)}`
    : t === 0xB0 ? `CC ${d1} = ${d2}` : t === 0xE0 ? `bend ${((d2 << 7) | d1) - 8192}` : t === 0xC0 ? `program ${d1}` : `${st.toString(16)} ${d1} ${d2}`;
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

// the panel fits the window: scaled down when it is narrower than the panel (no scrolling)
function fitPanel() {
  const dev = document.querySelector('.device'), deck = document.querySelector('.deck'), kbw = document.querySelector('.kbwrap');
  deck.style.transform = ''; kbw.style.transform = ''; dev.style.height = '';
  const room = dev.clientWidth - 36, need = deck.scrollWidth, s = Math.min(1, room / need);
  deck.style.transform = s < 1 ? `scale(${s})` : '';
  // the keyboard sits under the deck's unscaled height: pull it up by what the deck lost
  kbw.style.transform = s < 1 ? `translateY(${-deck.offsetHeight * (1 - s)}px) scale(${s})` : '';
  kbw.style.marginLeft = s < 1 ? '0' : '';
  if (s < 1) dev.style.height = (deck.offsetHeight + kbw.offsetHeight + 18) * s + 32 + 'px';
}

// ---------------------------------------------------------------- boot
buildPanel();
window.addEventListener('resize', fitPanel);
requestAnimationFrame(fitPanel);
for (let p = 0; p < 16; p++) padEls.push($('pad' + p));
for (let b = 0; b < 18; b++) btnLeds.push($('btn' + b).querySelector('.led'));
const STORE_KEY = 'orgigs.elite.settings';
OrGigsModule({
  onMidiOut: (st, d1, d2) => { logMidi(st, d1, d2); if (midiOut) midiOut.send([st, d1, d2]); },
  onSysexOut: (bytes) => { if (midiOut) try { midiOut.send([0xF0, ...bytes, 0xF7]); } catch (e) { /* no SysEx permission */ } },
  onSettingsSaved: () => {
    try { localStorage.setItem(STORE_KEY, JSON.stringify(Array.from(M.HEAPU8.subarray(M._sim_store(), M._sim_store() + M._sim_store_len())))); } catch (e) { /* private window */ }
  },
}).then((m) => {
  M = m;
  try {
    const saved = JSON.parse(localStorage.getItem(STORE_KEY) || 'null');
    if (Array.isArray(saved) && saved.length === M._sim_store_len()) M.HEAPU8.set(saved, M._sim_store());
  } catch (e) { /* nothing saved */ }
  for (let f = 0; f < 4; f++) M._sim_fader(f, +$('f' + f).value);
  M._sim_vel(+$('vel').value);
  M._sim_init();
  setMaster(+$('f3').value);
  const t0 = performance.now();
  setInterval(() => M._sim_step(Math.floor(performance.now() - t0) + 1), 4);   // the main loop
  requestAnimationFrame(drawScreen);
});

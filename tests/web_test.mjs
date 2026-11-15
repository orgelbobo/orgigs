// SPDX-License-Identifier: GPL-3.0-only
// The site's installer page (build/site/install/index.html, tools/make_site.py) against a simulated device on Web
// MIDI, no hardware: it finds the keyboard, picks its package, writes it through the update loader and ends "done".
// The fake device after Skiff's web/test_web.mjs (FakeFM1). Run from the repo root after tools/make_site.py:
//   node tests/web_test.mjs
import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";

const SITE = "build/site";
let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(78)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };
const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

const html = readFileSync(join(SITE, "install/index.html"), "utf8");
const script = html.slice(html.lastIndexOf("<script>") + 8, html.lastIndexOf("</script>"));

// a keyboard on Web MIDI: its identity on the handshake, then "device asks, host answers" reads of the image
class FakeKeyboard {
  constructor(identity, port, after) {
    this.after = after; this.bad = 0; this.served = 0; this.image = null;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, port);
  }
  boot(identity, port) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name: port, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name: port, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(b) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(b) }); }, 1); }
  rx(d) {
    const { pack7, unpack7 } = this.lib;
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 8 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null; this.served++;
      const model = this.identity.replace(/^ota-/, "").split("_")[0];
      if (addr === 0xE0000000) setTimeout(() => this.boot(`ota-${model}_970`, "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.after, this.input.name), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...this.lib.pack7(u), 0xF7]);
  }
}

// the page in a sandbox: its elements, Web MIDI (the fake keyboard), fetch from build/site, confirm: yes
async function run(identity, port, after, { midi = true } = {}) {
  const els = {};
  const el = (id) => els[id] || (els[id] = { id, textContent: "", value: 0, disabled: true, listeners: {},
    addEventListener(t, f) { this.listeners[t] = f; } });
  const dev = new FakeKeyboard(identity, port, after);
  const asked = [];
  const ctx = {
    document: { getElementById: el }, window: { addEventListener() {} }, console, setTimeout, clearTimeout, TextEncoder,
    Uint8Array, Promise, Error, Object, JSON, Math, Date, Map, Set, Array, String, parseInt,
    navigator: midi ? { requestMIDIAccess: async () => dev.access } : {},
    confirm: (q) => { asked.push(q); return true; },
    crypto: globalThis.crypto,
    fetch: async (url) => {
      const data = readFileSync(join(SITE, "install", url));
      return { ok: true, status: 200, arrayBuffer: async () => data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) };
    },
  };
  vm.createContext(ctx);
  vm.runInContext(script + "\n;globalThis.__lib = { pack7, unpack7, logicalImage, productOf };", ctx);
  dev.lib = ctx.__lib;
  if (!midi) return { els };
  const pkgName = identity.startsWith("FM-1") ? "orgigs-dev.fwsc" : "orgigs-dev-smk37-elite.fwsc";
  dev.image = ctx.__lib.logicalImage(new Uint8Array(readFileSync(join(SITE, "firmware", pkgName))));
  await els.go.listeners.click();
  return { els, dev, asked };
}

const elite = await run("SMK-37 Elite_016", "SMK-37 Elite", "SMK-37 Elite_970");
ok(/^Done/.test(elite.els.status.textContent) && elite.dev.bad === 0 && elite.dev.served > 10 && elite.els.bar.value === 100,
  `a stock Elite (V016): installed, every read answered right (${elite.dev.served} reads)`);
ok(elite.asked.length === 1 && /SMK-37 Elite_016/.test(elite.asked[0]) && /SMK-37 Elite_970/.test(elite.asked[0]),
  "it asks before installing over another firmware");
const upd = await run("SMK-37 Elite_970", "SMK-37 Elite", "SMK-37 Elite_970");
ok(/^Done/.test(upd.els.status.textContent) && upd.asked.length === 0, "an Elite running OrGigs: updated without asking");
const fm1 = await run("FM-1_015", "FM-1", "FM-1_970");
ok(/^Done/.test(fm1.els.status.textContent) && fm1.dev.bad === 0, "an FM-1: gets the FM-1 package");
const pro = await run("SMK-37 Pro_016", "SMK-37 Pro", "SMK-37 Pro_970");
ok(/no OrGigs package/.test(pro.els.status.textContent) && pro.dev.served === 0, "an SMK-37 Pro: no package, nothing written");
const old = await run("SMK-37 Elite_015", "SMK-37 Elite", "SMK-37 Elite_970");
ok(/older than V016/.test(old.els.status.textContent) && old.dev.served === 0, "an Elite on stock firmware before V016: refused, nothing written");
const wrong = await run("SMK-37 Elite_016", "SMK-37 Elite", "SMK-37 Elite_016");
ok(/reports another firmware/.test(wrong.els.status.textContent), "the keyboard reports another identity afterwards: said so");
const none = await run("FM-1_015", "FM-1", "FM-1_970", { midi: false });
ok(/no Web MIDI/.test(none.els.status.textContent) && none.els.go.disabled, "a browser without Web MIDI: said so, Install stays off");

console.log(failed ? "WEB TESTS FAILED" : "web tests passed");
process.exit(failed ? 1 : 0);

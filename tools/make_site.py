#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make OrGigs's site (for GitHub Pages) in one folder, after Skiff's web/make_site.py:

  index.html                  the start page: both editions, links
  install/index.html          web/installer.html with fm1pkg.js, fm1ota.js and the packages' list inlined
  firmware/orgigs-VER.fwsc    the FM-1 package, orgigs-VER-smk37-elite.fwsc the Elite's (+ LICENSE, LICENSING.md:
                              the packages hold JieLi SDK files under Apache-2.0, see LICENSING.md)
  try/                        the FM-1 browser simulator (sim/web, built by sim/build_wasm.sh)
  docs/                       the manuals and the at-a-glance sheets (PDF), the card images

  tools/make_site.py [--version X.Y] [OUT_DIR]      (default: dev, build/site)

Build the packages first (./build.sh and ./build.sh --board smk37 --model elite, with --release X.Y for --version X.Y). Nothing is uploaded."""
import argparse
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

# the editions on the site: (build file, published suffix, model the package must carry); a release's files carry
# its version (./build.sh --release X.Y: build/orgigs_fm1-X.Y.fwsc)
PACKAGES = [("build/orgigs_fm1{v}-smk37-elite.fwsc", "-smk37-elite", "SMK-37 Elite"), ("build/orgigs_fm1{v}.fwsc", "", "FM-1")]
DOCS = ["OrGigs_Elite_Manual.pdf", "OrGigs_Elite_At_a_glance.pdf", "OrGigs_FM-1_Manual.pdf", "OrGigs_FM-1_At_a_glance.pdf"]


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def package(path, suffix, model, version):
    raw = (ROOT / path).read_bytes()
    product = I.product_of(raw)
    if product.rpartition("_")[0] != model or not re.fullmatch(r"674\d{3}", product.rpartition("_")[2]):
        raise SystemExit(f"{path}: identity {product!r} is not OrGigs's {model}_674XYZ")
    if I.LOADER_MARK not in raw:                    # never publish a package with vendor files
        raise SystemExit(f"{path}: no Felucca loader in it")
    return product, raw, f"orgigs-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}{suffix}.fwsc"


ORGAN = ("A three-manual organ with 84 free-running tone generators and no voice stealing: eight models from "
         "tonewheel and combo organs to pipe organs and free reeds, drawbars, percussion, rotary, drive, delay and reverb, "
         "16 performance setups, three MIDI zones and Yamaha SEQTRAK control.")

START = """<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OrGigs</title>
<style>
  :root { --bg: #f6f5f2; --panel: #fff; --fg: #1d1b18; --dim: #6b655c; --line: #dcd7ce; --acc: #b86a00; }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) { --bg: #12110f; --panel: #1b1a17; --fg: #ece8e1; --dim: #a39b8f; --line: #34302a; --acc: #e68700; }
  }
  :root[data-theme="dark"] { --bg: #12110f; --panel: #1b1a17; --fg: #ece8e1; --dim: #a39b8f; --line: #34302a; --acc: #e68700; }
  body { margin: 0; background: var(--bg); color: var(--fg); font: 16px/1.6 system-ui, -apple-system, "Segoe UI", sans-serif; }
  main { max-width: 860px; margin: 0 auto; padding: 40px 16px 64px; }
  h1 { font-size: 34px; margin: 0; } h1 span { color: var(--acc); }
  .lead { color: var(--dim); max-width: 64ch; }
  .eds { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 16px; margin: 24px 0; }
  .ed { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 16px; display: grid; gap: 10px; align-content: start; }
  .ed img { width: 100%; max-width: 240px; image-rendering: pixelated; border-radius: 4px; justify-self: center; }
  .ed h2 { margin: 0; font-size: 20px; } .ed h2 small { color: var(--dim); font-weight: 400; font-size: 14px; }
  .ed p { margin: 0; }
  .links { display: flex; flex-wrap: wrap; gap: 8px; }
  .links a { border: 1px solid var(--acc); color: var(--acc); border-radius: 4px; padding: 4px 10px; text-decoration: none; font-size: 15px; }
  .links a.main { background: var(--acc); color: #fff; }
  .small { color: var(--dim); font-size: 14px; } a { color: var(--acc); }
</style>
</head>
<body>
<main>
  <h1>Or<span>Gigs</span></h1>
  <p class="lead">It's an organ. Free and open source (GPL-3.0), for the M-VAVE SMK-37 Elite and FM-1. Version VERSION.</p>
  <div class="eds">
    <div class="ed">
      <img src="docs/card-elite.png" alt="The OrGigs welcome screen on the SMK-37 Elite">
      <h2>SMK-37 Elite</h2>
      <p>ORGAN</p>
      <div class="links"><a class="main" href="install/">Install</a><a href="docs/OrGigs_Elite_Manual.pdf">Manual</a>
        <a href="docs/OrGigs_Elite_At_a_glance.pdf">At a glance</a></div>
    </div>
    <div class="ed">
      <img src="docs/card-fm1.png" alt="The OrGigs welcome screen on the FM-1">
      <h2>FM-1</h2>
      <p>ORGAN</p>
      <div class="links"><a class="main" href="install/">Install</a><a href="try/">Try it</a>
        <a href="docs/OrGigs_FM-1_Manual.pdf">Manual</a><a href="docs/OrGigs_FM-1_At_a_glance.pdf">At a glance</a></div>
    </div>
  </div>
  <p class="small">Packages: FILES · <a href="firmware/LICENSING.md">licences</a>. Installing custom firmware is at
    your own risk. All product names are trademarks of their owners and are used only to identify compatible devices;
    OrGigs is not affiliated with them.</p>
</main>
</body>
</html>
"""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", default="dev")
    ap.add_argument("out", nargs="?", default=str(ROOT / "build/site"))
    a = ap.parse_args(argv)
    out = Path(a.out)
    v = "" if a.version == "dev" else "-" + a.version
    pkgs = [package(p.format(v=v), s, m, a.version) for p, s, m in PACKAGES]
    sim = ROOT / "sim/web"
    for f in ("index.html", "sim.js", "orgigs_sim.js"):
        if not (sim / f).exists():
            raise SystemExit(f"{sim / f}: missing (sim/build_wasm.sh)")
    for d in DOCS:
        if not (ROOT / "docs/manual" / d).exists():
            raise SystemExit(f"docs/manual/{d}: missing (docs/manual/build_pdf.sh)")
    lib = strip_module((ROOT / "web/fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((ROOT / "web/fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": a.version, "packages": [{"product": p, "pkg": "../firmware/" + n} for p, _, n in pkgs]})
    html = (ROOT / "web/installer.html").read_text(encoding="utf-8")
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"web/installer.html must contain {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)

    shutil.rmtree(out, ignore_errors=True)
    for d in ("install", "firmware", "try", "docs"):
        (out / d).mkdir(parents=True)
    (out / "install/index.html").write_text(html, encoding="utf-8")
    for _, raw, n in pkgs:
        (out / "firmware" / n).write_bytes(raw)
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(ROOT / doc, out / "firmware" / doc)
    for f in ("index.html", "sim.js", "orgigs_sim.js"):
        shutil.copy(sim / f, out / "try" / f)
    for d in DOCS:
        shutil.copy(ROOT / "docs/manual" / d, out / "docs" / d)
    for c in ("card-elite.png", "card-fm1.png"):
        shutil.copy(ROOT / "docs/hub" / c, out / "docs" / c)
    files = ", ".join(f'<a href="firmware/{n}">{n}</a> ({p})' for p, _, n in pkgs)
    page = START.replace("VERSION", a.version).replace("FILES", files).replace("ORGAN", ORGAN)
    (out / "index.html").write_text(page, encoding="utf-8")
    print(f"site: {out}: index.html, install/ ({len(html)} B), try/, docs/ ({len(DOCS)} PDFs), "
          + ", ".join(f"firmware/{n} ({p})" for p, _, n in pkgs))


if __name__ == "__main__":
    main()

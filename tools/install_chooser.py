#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 RichAldana
"""The install page asks first which firmware: LOOM-8 PANEL MIDI (this site's installer, right below) or Skiff PANEL
MIDI (Skiff-Remote's installer, on its own site). The link stays the same (…/LOOM8-Remote/install/); ?fw=loom8 (or
#loom8) goes straight to LOOM-8's. Without JavaScript the page is as before.

  tools/install_chooser.py build/pages/install/index.html [SKIFF_INSTALLER_URL]

Run by pages.yml after tools/make_pages.py."""
import sys
from pathlib import Path

SKIFF = "https://richaldana.github.io/Skiff-Remote/install/"
MARK = "<!--fw-chooser-->"

CSS = """<style>
  .fwpick main > :not(#fwpick) { display: none; }
  #fwpick { display: none; }
  .fwpick #fwpick { display: block; }
  #fwpick .fw { display: block; width: 100%; box-sizing: border-box; text-align: left; margin: 0 0 12px;
                padding: 14px 18px; border-radius: 12px; border: 2px solid var(--rule, var(--dim));
                background: var(--card, transparent); color: var(--fg); text-decoration: none; cursor: pointer; font: inherit; }
  #fwpick .fw:hover { border-color: var(--accent); }
  #fwpick .fw strong { display: block; font-size: 18px; }
  #fwpick .fw span { color: var(--dim); font-size: 14px; }
</style>"""

BODY = """<section id="fwpick" aria-labelledby="fwpick-h">
  <h1 id="fwpick-h" data-fr="Quel firmware installer ?">Which firmware?</h1>
  <p data-fr="Les deux prêtent leur panneau à FM1 Mix (PANEL MIDI). Branchez le FM-1 en USB, puis choisissez :">Both lend their panel to FM1 Mix (PANEL MIDI). Plug the FM-1 in over USB, then choose:</p>
  <button type="button" class="fw" id="fw-loom8"><strong>LOOM-8 PANEL MIDI</strong>
    <span data-fr="Le groovebox 8 pistes. Installé depuis cette page.">The 8-track groovebox. Installed from this page.</span></button>
  <a class="fw" id="fw-skiff" href="%SKIFF%"><strong>Skiff PANEL MIDI</strong>
    <span data-fr="Skiff (illuminus5) avec PANEL MIDI et USB PLAY. Son installeur s'ouvre.">Skiff (illuminus5) with PANEL MIDI and USB PLAY. Opens its installer.</span></a>
  <p class="small" data-fr="Un seul firmware à la fois sur le FM-1 ; l'autre se réinstalle de la même façon.">One firmware at a time on the FM-1; the other installs again the same way.</p>
</section>"""

JS = """<script>
(function () {
  var q = location.search + location.hash, root = document.documentElement;
  if (/(^|[?&#])(fw=)?loom8\\b/.test(q)) return;
  root.classList.add('fwpick');
  document.addEventListener('DOMContentLoaded', function () {
    if (/^fr\\b/i.test(navigator.language || '')) document.querySelectorAll('#fwpick [data-fr]').forEach(function (e) {
      if (e.children.length === 0) e.textContent = e.getAttribute('data-fr'); });
    var b = document.getElementById('fw-loom8');
    if (!b) return;
    b.addEventListener('click', function () {
      root.classList.remove('fwpick');
      try { history.replaceState(null, '', '?fw=loom8'); } catch (e) {}
      var h = document.querySelector('main h1:not(#fwpick-h)');
      if (h) { h.setAttribute('tabindex', '-1'); h.focus(); }
      scrollTo(0, 0);
    });
  });
})();
</script>"""


def main(path, skiff=SKIFF):
    p = Path(path)
    html = p.read_text(encoding="utf-8")
    if MARK in html:
        print(f"{p}: the chooser is there already")
        return
    for need in ("</head>", "<main>"):
        if html.count(need) != 1:
            raise SystemExit(f"{p}: {need} must appear once; update install_chooser.py")
    html = html.replace("</head>", CSS + "\n" + JS + "\n</head>")
    html = html.replace("<main>", "<main>\n" + MARK + "\n" + BODY.replace("%SKIFF%", skiff))
    p.write_text(html, encoding="utf-8")
    print(f"{p}: the chooser added (Skiff: {skiff})")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])

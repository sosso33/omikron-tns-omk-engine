// SPDX-License-Identifier: GPL-3.0-or-later
// THE PROFILER PAGE, RUN - `tools/omkprof.html`'s own script under node with
// a DOM stub, against a live `omkprof.py --serve` (`verify.py: profiler
// page`). A syntax check passes a page that throws at run time (CLAUDE.md 5);
// this runs it: the first poll, a frame selected, and what each panel drew.
//
//     node tools/profcheck.js <page.html> <port> <frame index>
//
// Prints one JSON line: {frames, sel, title, tree, sections, zones, stats}.
"use strict";
const fs = require("fs");
const [page, port, pick] = process.argv.slice(2);
const els = {};
function el(id) {
  return {
    id, children: [], style: {}, className: "", title: "", checked: true, _text: "",
    // as a real DOM does: setting the text replaces the children
    get textContent() { return this._text; },
    set textContent(v) { this._text = v; this.children = []; },
    clientWidth: 900, clientHeight: 150, width: 0, height: 0,
    appendChild(c) { this.children.push(c); return c; },
    addEventListener() {}, set onclick(f) { this._click = f; },
    getContext() {
      return new Proxy({}, { get: (t, k) => (k in t ? t[k] : () => {}),
                             set: (t, k, v) => { t[k] = v; return true; } });
    },
  };
}
global.document = {
  getElementById: id => (els[id] ||= el(id)),
  createElement: t => el(t), createTextNode: t => ({ textContent: t }),
  documentElement: {}, addEventListener() {},
};
global.window = { devicePixelRatio: 2, addEventListener() {} };
global.getComputedStyle = () => ({ getPropertyValue: () => "#000" });
global.setInterval = () => 0;
const realFetch = fetch;
global.fetch = (u, o) => realFetch("http://127.0.0.1:" + port + u, o);
const html = fs.readFileSync(page, "utf8");
const src = html.split("<script>")[1].split("</script>")[0];
eval(src + "\n;global.__t = { poll, select, get frames() { return frames; }, get sel() { return sel; } };");
(async () => {
  await __t.poll();                       // the page's own first poll is the same promise
  await new Promise(r => setTimeout(r, 200));
  await __t.select(Number(pick));
  const n = id => els[id].children.length;
  console.log(JSON.stringify({ frames: __t.frames.length, sel: __t.sel, title: els.ftitle.textContent,
                               tree: n("tree"), sections: n("sections"), zones: n("zones"),
                               stats: els.stats.textContent }));
})().catch(e => { console.log(JSON.stringify({ error: String(e.stack || e) })); process.exit(1); });

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the book's figures as SVG. Every figure illustrates something the
text states; none asserts a structure the text does not."""
import os

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "Helvetica, Arial, sans-serif"
INK = "#1d1d1f"
C = {"machine": "#e8eefc", "data": "#fdf1e3", "port": "#e9f6ec", "warn": "#fdecec",
     "grey": "#f2f2f4", "blue": "#3b73b9", "green": "#2e8b57", "red": "#c0392b",
     "amber": "#b7791f"}


class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.parts = w, h, []
        self.parts.append(
            '<defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
            'markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" '
            'fill="%s"/></marker></defs>' % INK)

    def box(self, x, y, w, h, lines, fill="#fff", bold_first=True, size=13, stroke=INK, rx=6, align="middle"):
        self.parts.append('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" stroke="%s" stroke-width="1.3"/>'
                          % (x, y, w, h, rx, fill, stroke))
        n = len(lines)
        lh = size * 1.35
        y0 = y + h / 2 - (n - 1) * lh / 2 + size * 0.35
        tx = x + w / 2 if align == "middle" else x + 10
        for k, t in enumerate(lines):
            wgt = "bold" if (k == 0 and bold_first) else "normal"
            sz = size if k == 0 else size - 1.5
            self.text(tx, y0 + k * lh, t, size=sz, weight=wgt, anchor=align if align == "middle" else "start")

    def text(self, x, y, t, size=12, weight="normal", anchor="middle", fill=INK, italic=False):
        t = t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        st = ' font-style="italic"' if italic else ""
        self.parts.append('<text x="%g" y="%g" font-family="%s" font-size="%g" font-weight="%s" '
                          'text-anchor="%s" fill="%s"%s>%s</text>' % (x, y, FONT, size, weight, anchor, fill, st, t))

    def arrow(self, x1, y1, x2, y2, label=None, dy=-6, color=INK, dash=False, size=11):
        d = ' stroke-dasharray="5,4"' if dash else ""
        self.parts.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="1.5" '
                          'marker-end="url(#ar)"%s/>' % (x1, y1, x2, y2, color, d))
        if label:
            self.text((x1 + x2) / 2, (y1 + y2) / 2 + dy, label, size=size, fill=color)

    def path(self, d, color=INK, dash=False, marker=True):
        dd = ' stroke-dasharray="5,4"' if dash else ""
        m = ' marker-end="url(#ar)"' if marker else ""
        self.parts.append('<path d="%s" fill="none" stroke="%s" stroke-width="1.5"%s%s/>' % (d, color, dd, m))

    def rect(self, x, y, w, h, fill, stroke="none"):
        self.parts.append('<rect x="%g" y="%g" width="%g" height="%g" fill="%s" stroke="%s"/>' % (x, y, w, h, fill, stroke))

    def save(self, name):
        s = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
             '<rect width="100%%" height="100%%" fill="white"/>%s</svg>'
             % (self.w, self.h, self.w, self.h, "".join(self.parts)))
        open(os.path.join(OUT, name), "w").write(s)


def fig_split():
    s = Svg(760, 300)
    s.box(20, 30, 330, 240, ["Runtime.exe  (~985 KB)", "THE MACHINE", "", "a bytecode interpreter (153 opcodes)",
                             "a state-graph runner for bodies", "a renderer, a sound bank, a UI layer",
                             "a reader for each file format"], fill=C["machine"])
    s.box(410, 30, 330, 240, ["gamedata/  (~1.7 GB, 2 812 files)", "THE CONTENT", "",
                              "5 785 world scripts", "the state graphs (.CTL), the clips",
                              "sets, characters, textures", "conversations, voices, music"], fill=C["data"])
    s.arrow(350, 150, 408, 150, "reads", dy=-8)
    s.save("fig01-split.svg")


def fig_frame():
    s = Svg(780, 330)
    steps = [("Win32 idle", ["the message pump", "has nothing to do"]),
             ("the clock", ["dt = 30 / fps", "capped at 3.0"]),
             ("input", ["14 bits:", "the keys, bound"]),
             ("scripts", ["every live context", "runs until it parks"]),
             ("bodies", ["state graphs,", "the walker, the crowd"]),
             ("scene", ["objects' programs,", "paths, the camera"]),
             ("draw", ["visible set ->", "bucket keys -> D3D"])]
    x, y, w, h, gap = 12, 60, 98, 96, 12
    for k, (t, sub) in enumerate(steps):
        s.box(x + k * (w + gap), y, w, h, [t] + sub, fill=C["machine"] if k else C["grey"], size=12)
        if k:
            s.arrow(x + k * (w + gap) - gap, y + h / 2, x + k * (w + gap) - 1, y + h / 2)
    s.path("M %g %g C %g %g, %g %g, %g %g" % (x + 6 * (w + gap) + w / 2, y + h, x + 6 * (w + gap) + w / 2, 260,
                                               x + w / 2, 260, x + w / 2, y + h + 2), dash=True)
    s.text(390, 285, "one frame - then the pump idles again; at 30 fps each tick is exactly 1.0 of the game's clock", size=12, italic=True)
    s.text(390, 30, "One frame of the original engine (Game_RunLoop -> Game_Frame)", size=14, weight="bold")
    s.save("fig02-frame.svg")


def fig_data():
    s = Svg(780, 380)
    s.text(390, 26, "Where each part of the game lives", size=14, weight="bold")
    rows = [("IAM/", "archives: areas, scenes, world scripts, conversations, interface text, saves", "the VM, the world, dialogue, UI"),
            ("MESHES/", ".3DO models and sets, .3DT textures", "the renderer, collision"),
            ("SCPTDATA/", ".SCX scene scripts, .3DA clips, .3DP paths, .SFX sound tables", "scene objects, cutscenes"),
            ("MORPH/", ".3DM: a line's facial animation WITH its voice", "conversations, audio"),
            (".CTL / .ani", "the bodies' state graphs and their clips", "the actor runtime"),
            ("FONTS/ I2D/ IMAGES/ MAP2D/", "fonts, sprite sheets, bitmaps, maps (and the gunmen's nav grid)", "the interface, shoot mode"),
            ("TRACKS/ SOUNDS/ VOICEOFF/", "music, effects, narration", "the sound bank"),
            ("FLIS/", "three MPEG-1 films", "boot")]
    y = 48
    for k, (d, what, who) in enumerate(rows):
        s.box(12, y + k * 40, 190, 32, [d], fill=C["data"], size=12)
        s.text(214, y + k * 40 + 20, what, size=11.5, anchor="start")
        s.box(600, y + k * 40, 168, 32, [who], fill=C["machine"], size=11, bold_first=False)
    s.save("fig03-data.svg")


def fig_park():
    s = Svg(760, 300)
    s.text(380, 26, "How a script waits: it parks, and something else wakes it", size=14, weight="bold")
    s.box(30, 110, 150, 70, ["status 1", "running"], fill=C["machine"])
    s.box(300, 50, 190, 190, ["parked", "status 3  a fight", "status 4  a scene object", "status 6  a screen",
                              "status 7  a camera move", "status 8-11  a transition"], fill=C["warn"], size=12)
    s.box(590, 110, 150, 70, ["the event", "that answers"], fill=C["grey"])
    s.arrow(180, 130, 298, 110, "a handler writes", dy=-8)
    s.arrow(490, 145, 588, 145)
    s.path("M 665 180 C 665 280, 105 280, 105 182")
    s.text(380, 272, "writes 1 back: the script carries on at the next instruction, stack intact", size=12, italic=True)
    s.save("fig04-park.svg")


def fig_slots():
    s = Svg(760, 280)
    s.text(380, 26, "Two places resident at once", size=14, weight="bold")
    s.box(30, 50, 320, 180, ["the ACTIVE slot", "drawn", "its scripts and zones run", "solid", "e.g. the building you entered"], fill=C["machine"])
    s.box(410, 50, 320, 180, ["the HIDDEN slot", "not drawn", "still animating", "STILL SOLID", "e.g. the street you left"], fill=C["grey"])
    s.path("M 350 120 L 408 120")
    s.path("M 408 160 L 352 160")
    s.text(380, 108, "walk out", size=11)
    s.text(380, 180, "walk back: no reload", size=11)
    s.text(380, 258, "hidden is not unloaded - and it shares the 58-slot texture cache with the active one", size=12, italic=True)
    s.save("fig05-slots.svg")


def fig_ctl():
    s = Svg(760, 320)
    s.text(380, 26, "A body is a state graph that shipped on the disc (.CTL)", size=14, weight="bold")
    s.box(40, 130, 130, 56, ["H_STAND", "standing"], fill=C["machine"])
    s.box(300, 60, 130, 56, ["walk"], fill=C["machine"])
    s.box(560, 60, 130, 56, ["run"], fill=C["machine"])
    s.arrow(170, 145, 298, 95, "forward", dy=-4)
    s.arrow(430, 88, 558, 88, "forward + run", dy=-8)
    s.path("M 300 105 C 250 150, 220 160, 172 162", dash=True)
    s.text(238, 180, "released", size=11)
    chain = [("H_ADJSTP", "step in"), ("H_TAK*12", "reach"), ("H_TAK*22", "stand up"),
             ("H_WAITOB", "wait"), ("your press", "bag or cancel")]
    for k, (t, sub) in enumerate(chain):
        s.box(200 + k * 112, 214, 96, 50, [t, sub], fill=C["port"], size=11)
        if k:
            s.arrow(200 + k * 112 - 16, 239, 200 + k * 112 - 1, 239)
    s.arrow(105, 186, 198, 232)
    s.text(60, 250, "action, beside", size=11, anchor="start")
    s.text(60, 265, "an object", size=11, anchor="start")
    s.text(380, 300, "every edge names the input bits that take it - the keyboard, or an AI pressing the same bits", size=12, italic=True)
    s.save("fig06-ctl.svg")


def fig_keys():
    s = Svg(780, 340)
    s.text(390, 26, "The draw order is a number: the 14-bit bucket key", size=14, weight="bold")
    bits = 14
    bw = 44
    x0 = 390 - bits * bw / 2
    for b in range(bits):
        hi = bits - 1 - b
        fill = C["port"] if hi < 6 else C["machine"]
        s.box(x0 + b * bw, 50, bw, 40, [str(hi)], fill=fill, size=11, bold_first=False, rx=0)
    s.text(x0 + 4 * bw, 112, "state bits, from the mesh's flags", size=12)
    s.text(x0 + 11 * bw, 112, "texture slot (low 6 bits)", size=12)
    s.text(390, 150, "Render_FlushBuckets walks the keys 0 .. 0x3FFF in ASCENDING order:", size=12)
    s.box(40, 170, 440, 40, ["opaque geometry, bucket by bucket, texture by texture"], fill=C["machine"], size=12, bold_first=False)
    s.box(500, 170, 240, 40, ["blended: key bit 13 set"], fill="#fff", stroke=C["amber"], size=12, bold_first=False)
    s.arrow(40, 230, 740, 230, "draw order", dy=16)
    s.text(390, 290, "blended geometry lands at the top of the range, so it is drawn last - which is what transparency needs;", size=11.5, italic=True)
    s.text(390, 308, "two coincident faces in one mesh differ only in texture slot, so the lower slot is drawn first - and wins", size=11.5, italic=True)
    s.save("fig07-keys.svg")


def fig_tie():
    s = Svg(780, 360)
    s.text(390, 26, "Two faces in the same place: the original's answer, a GPU's, and the port's", size=14, weight="bold")
    cols = [("1999: quantised depth, strict test", "the FIRST drawn face wins", "a later face must beat it by a step"),
            ("a GPU float compare", "last-bit noise picks per pixel", "dots of the other face"),
            ("the port: the depth tie", "losers decided once per set", "and drawn two 16-bit steps back")]
    for k, (a, b, c) in enumerate(cols):
        x = 30 + k * 250
        s.box(x, 50, 220, 60, [a, b], fill=[C["machine"], C["warn"], C["port"]][k], size=12)
        s.text(x + 110, 128, c, size=11.5, italic=True)
        # the panel
        px, py = x + 30, 145
        s.rect(px, py, 160, 110, "#fff", INK)
        if k == 0 or k == 2:
            s.rect(px + 1, py + 1, 158, 108, "#9fc3e6")
            s.text(px + 80, py + 60, "face A", size=13, weight="bold")
        else:
            s.rect(px + 1, py + 1, 158, 108, "#9fc3e6")
            import random
            random.seed(7)
            for _ in range(160):
                s.rect(px + 2 + random.random() * 154, py + 2 + random.random() * 104, 3, 3, "#e8a36b")
            s.text(px + 80, py + 60, "A + dots of B", size=13, weight="bold")
    s.text(390, 290, "Pushed back by the engine's own band rather than removed, a loser that SEPARATES - a door's leaf", size=12)
    s.text(390, 308, "swinging open - simply draws again: no per-frame watch, and no hybrid (bodies keep the per-frame tie).", size=12)
    s.text(390, 326, "A shop sign's two sides are NOT such a pair: they wind opposite ways, and the back-face cull keeps one.", size=12)
    s.save("fig08-tie.svg")


def fig_port():
    s = Svg(780, 420)
    s.text(390, 26, "The port: one engine, reference and live backends behind one boundary", size=14, weight="bold")
    s.box(20, 50, 740, 50, ["frontends:  omk-play (SDL: macOS/Linux, Tiger on PowerPC)   -   the PS Vita build   -   200 probes in engine/tools"], fill=C["grey"], size=12)
    s.box(20, 120, 740, 90, ["the engine  (engine/src, C++20, no required dependency)",
                             "formats/  script/ (Session, VM, zones, dialogue, scenes)  actor/ (.CTL, walker, crowd, fight, shoot)",
                             "o3de/ (buckets, texture cache, depth tie, particles)  ui/  audio/  input/  platform/ (DataFs)"], fill=C["port"], size=12)
    s.box(20, 230, 740, 40, ["the renderer boundary - DECISIONS, not API calls:  begin(view)  submit(draw)  end()"], fill="#fff", size=12)
    s.box(20, 290, 175, 70, ["software rasterizer", "the REFERENCE", "what the checks measure"], fill=C["machine"], size=12)
    s.box(208, 290, 175, 70, ["Vulkan (MoltenVK)", "live, desktop"], fill=C["machine"], size=12)
    s.box(396, 290, 175, 70, ["GLES2", "live, the Vita", "poses bodies in its shader"], fill=C["machine"], size=12)
    s.box(585, 290, 175, 70, ["OpenGL 1.x", "fixed function", "a 1999 Mac, on PowerPC"], fill=C["machine"], size=12)
    for x in (107, 295, 483, 672):
        s.arrow(x, 270, x, 288)
    s.arrow(390, 100, 390, 118)
    s.arrow(390, 210, 390, 228)
    s.text(390, 395, "the same shape for sound (PCM buffers vs a device) and input (a replayable stream vs a pad)", size=12, italic=True)
    s.save("fig09-port.svg")


def fig_ladder():
    s = Svg(760, 380)
    s.text(380, 26, "How much a green check means: the six tiers (docs/PORTING.md B1)", size=14, weight="bold")
    tiers = [("1  exact", "byte-identical to the shipped data", C["green"]),
             ("2  corpus-constrained", "an invariant the shipped data could fail", C["green"]),
             ("3  differential", "agrees with a second implementation", C["amber"]),
             ("4  behavioural", "reproduces the original engine's own output", C["green"]),
             ("5  data-constrained", "only the data it reads is checkable", C["amber"]),
             ("6  read and explained", "a transcription, internally consistent", C["red"])]
    for k, (a, b, col) in enumerate(tiers):
        y = 50 + k * 52
        s.rect(40, y, 8, 42, col)
        s.box(56, y, 250, 42, [a], fill="#fff", size=12.5, align="start")
        s.text(320, y + 26, b, size=12, anchor="start")
    s.text(380, 368, "only tiers 1, 2 and 4 can catch a wrong reading applied consistently", size=12, italic=True)
    s.save("fig10-ladder.svg")


def fig_optim():
    s = Svg(760, 300)
    s.text(380, 26, "Two of the 2026-09-29 measurements (M3; counts, not milliseconds)", size=14, weight="bold")
    data = [("GL state calls a frame", 1809, 92), ("heap allocations a frame*", 724, 160)]
    for k, (lab, a, b) in enumerate(data):
        y = 70 + k * 110
        s.text(30, y - 10, lab, size=12.5, weight="bold", anchor="start")
        m = max(a, b)
        s.rect(30, y, 640 * a / m, 30, "#c9d6ee")
        s.text(40 + 640 * a / m, y + 20, "before %d" % a, size=12, anchor="start")
        s.rect(30, y + 36, 640 * b / m, 30, C["green"])
        s.text(40 + 640 * b / m, y + 56, "after %d" % b, size=12, anchor="start")
    s.text(380, 290, "* outside the software reference rasterizer, which the Vita never runs (1214 -> 651 in all)", size=11.5, italic=True)
    s.save("fig11-optim.svg")


if __name__ == "__main__":
    for f in (fig_split, fig_frame, fig_data, fig_park, fig_slots, fig_ctl, fig_keys, fig_tie, fig_port, fig_ladder, fig_optim):
        f()
    print("figures written")

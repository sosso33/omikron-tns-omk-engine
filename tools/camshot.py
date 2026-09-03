#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Render what a dialogue camera sees, offline, as a PNG.

The web viewer draws the same thing, but it draws it through a stack of its own
conventions - and a rendering convention that is wrong everywhere at once looks
right from inside. This is the independent check: it takes a DialogCamera out
of IAM\DIALOG, a set out of MESHES/DECORS, and projects one against the other
with nothing else in the loop, so the result can be laid over a screenshot of
the real game and compared feature by feature.

That comparison is what found two errors the viewer could not show:

  * the viewer's world map W(v) = [x, -y, z] negates ONE axis, which is a
    reflection - every frame was mirrored left to right;
  * angle[1] is the HORIZONTAL field of view and the 3D view is letterboxed to
    800x440, so reading it as vertical on whatever shape the window happened to
    be zoomed a long way out.

With both fixed, the wireframe of Aapkayl from camera 4555 lands on the pillars,
plant, sofa, rug and ceiling of a real screenshot of dialog 402.

    python3 tools/camshot.py 402 0                     # flat render -> PNG
    python3 tools/camshot.py 402 0 --over shot.png     # wireframe over a frame
    python3 tools/camshot.py 402 0 --mirror            # the reflected reading
    python3 tools/camshot.py 402 0 --over shot.png --speakers

Axes are the game's own: Y points down, so "up" is (0,-1,0) and the basis is
right-handed. That is the reading the screenshot confirms.
"""
import sys, os, math, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import omkdata as O, tex3dt

try:
    from PIL import Image, ImageDraw, ImageChops
except ImportError:
    sys.exit("camshot needs Pillow: python3 -m pip install pillow")

# The game letterboxes the 3D view during a conversation. Measured off a real
# 800x600 frame: the first non-black row is 80 and the bars are symmetric, so
# the picture is 800x440 and angle[1] is horizontal at that aspect.
W, H = 800, 440


def _avg_colors(decor):
    """One flat colour per material - enough to read the room's shape."""
    try: txs = tex3dt.textures(O.decor_path(decor))
    except Exception: txs = []
    out = {}
    for i, t in enumerate(txs):
        rgb = t["rgb"]; n = len(rgb) // 3
        if not n: out[i] = (128, 128, 128); continue
        step = max(1, n // 2000)
        r = g = b = c = 0
        for k in range(0, n, step):
            r += rgb[3*k]; g += rgb[3*k+1]; b += rgb[3*k+2]; c += 1
        out[i] = (r // c, g // c, b // c)
    return out


def projector(eye, at, hfov_deg, mirror=False, roll_deg=0.0):
    """-> f(worldPoint) = (px, py, depth), or None behind the camera.

    `roll_deg` turns the camera about its own view axis. This works in the
    GAME's space, where Y points down - so the sign here is the record's own,
    with no reflection in between."""
    f = [at[i] - eye[i] for i in range(3)]
    l = math.hypot(*f) or 1.0
    f = [v / l for v in f]
    up = [0.0, -1.0, 0.0]                      # the game's Y points down
    cross = lambda a, b: [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2],
                          a[0]*b[1]-a[1]*b[0]]
    def unit(a):
        m = math.hypot(*a) or 1.0
        return [v / m for v in a]
    if roll_deg:                               # Rodrigues about the view axis
        r = math.radians(roll_deg)
        c, sn = math.cos(r), math.sin(r)
        d = sum(f[i] * up[i] for i in range(3))
        cr = cross(f, up)
        up = [up[i]*c + cr[i]*sn + f[i]*d*(1-c) for i in range(3)]
    s = unit(cross(f, up))
    if mirror: s = [-v for v in s]             # the reflected reading
    u = cross(s, f)
    tanh = math.tan(math.radians(hfov_deg) / 2)
    tanv = tanh / (W / H)
    def project(p):
        d = [p[i] - eye[i] for i in range(3)]
        z = sum(d[i] * f[i] for i in range(3))
        if z <= 1: return None
        x = sum(d[i] * s[i] for i in range(3))
        y = sum(d[i] * u[i] for i in range(3))
        return (W * 0.5 * (1 + (x / z) / tanh),
                H * 0.5 * (1 - (y / z) / tanv), z)
    return project


def line_camera(dialog, node):
    """The eye, look-at and fov of the camera a node's own line uses."""
    conv = O.conversation(dialog)
    if not conv: sys.exit("no conversation %d" % dialog)
    n = conv["nodes"][node]
    ids = [n["lineCamera2"], n["lineCamera"], n["replyCamera"]]
    for cid in ids:
        c = next((c for c in conv["cameras"] if c["id"] == cid), None)
        if c: return conv, c
    sys.exit("node %d names no camera" % node)


GREY = False    # --grey: the old reading, the green byte as a brightness


def render(decor, eye, at, fov, out, mirror=False, over=None, marks=()):
    geo = O.decor_geometry_cached(decor)
    if not geo: sys.exit("no set %r" % decor)
    pr = projector(eye, at, fov, mirror)
    V = geo["verts"]

    if over:
        img = Image.open(over).convert("RGB")
        if img.size == (800, 600): img = img.crop((0, 80, 800, 520))
        img = img.resize((W, H))
        dr = ImageDraw.Draw(img)
        for t in range(0, len(V), 3):
            P = [pr(V[t+k]) for k in range(3)]
            if any(p is None for p in P): continue
            if max(p[2] for p in P) > 2000: continue     # keep the far wall out
            for k in range(3):
                a, b = P[k], P[(k+1) % 3]
                dr.line([a[0], a[1], b[0], b[1]], fill=(0, 255, 0), width=1)
    else:
        cols = _avg_colors(decor)
        img = Image.new("RGB", (W, H), (0, 0, 0))
        dr = ImageDraw.Draw(img)
        tris = []
        for b in geo["batches"]:
            c = cols.get(b["material"], (128, 128, 128))
            for t in range(b["start"], b["start"] + b["count"], 3):
                P = [pr(V[t+k]) for k in range(3)]
                if any(p is None for p in P): continue
                # The baked vertex light is a COLOUR, not a brightness - the
                # engine copies the whole dword at vertex +28 into the
                # D3DTLVERTEX (ASSETS 4c) - so it modulates the three channels
                # separately. `--grey` reproduces the old single-channel
                # reading for comparison.
                lit = [0.25 + 0.75 * sum(V[t+k][5+j] for k in range(3)) / 3.0
                       for j in range(3)]
                if GREY: lit = [sum(lit) / 3.0] * 3
                tris.append((max(p[2] for p in P), [(p[0], p[1]) for p in P],
                             tuple(min(255, int(c[j] * lit[j])) for j in range(3)),
                             b.get("blend") or ""))
        tris.sort(key=lambda t: -t[0])          # painter's algorithm
        for _, poly, c, mode in tris:
            if not mode:
                dr.polygon(poly, fill=c)
                continue
            # additive / multiply, over what is already there (ASSETS 4c)
            layer = Image.new("RGB", img.size, (0, 0, 0))
            mask = Image.new("L", img.size, 0)
            ImageDraw.Draw(layer).polygon(poly, fill=c)
            ImageDraw.Draw(mask).polygon(poly, fill=255)
            if mode == "add":
                blended = ImageChops.add(img, layer)
            else:                       # dest * (1 - src)
                blended = ImageChops.multiply(img, ImageChops.invert(layer))
            img.paste(blended, (0, 0), mask)

    for p, col, label in marks:
        q = pr(p)
        if not q: continue
        dr.ellipse([q[0]-7, q[1]-7, q[0]+7, q[1]+7], outline=col, width=3)
        # a bar the height of a character, standing on that spot
        head = pr((p[0], p[1] - 70.9, p[2]))
        if head:
            dr.line([q[0], q[1], head[0], head[1]], fill=col, width=2)
            dr.text((head[0] + 9, head[1]), label, fill=col)
    img.save(out)
    return img


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("dialog", type=int)
    ap.add_argument("node", type=int, nargs="?", default=0)
    ap.add_argument("--set", default=None, help="decor name (default: guessed)")
    ap.add_argument("--over", default=None, help="draw a wireframe over this frame")
    ap.add_argument("--speakers", action="store_true")
    ap.add_argument("--mirror", action="store_true",
                    help="the reflected reading, for comparison")
    ap.add_argument("-o", "--out", default="camshot.png")
    a = ap.parse_args()

    conv, cam = line_camera(a.dialog, a.node)
    eye, at = cam["pos"][:3], cam["pos"][3:6]
    name = a.set
    if not name:
        # nearest set by its own cameras, the same guess the viewer makes
        best = None
        for d in O.list_decors():
            g = O.decor_geometry_cached(d["name"])
            if not g or not g["cameras"]: continue
            m = min(math.dist(eye, c["pos"]) for c in g["cameras"])
            if best is None or m < best[0]: best = (m, d["name"])
        if not best: sys.exit("no set found")
        name = best[1]
        print("set: %s (%.1f units from its own cameras)" % (name, best[0]))

    marks = []
    if a.speakers:
        ws = O.speaker_positions(conv, name)
        if ws:
            marks = [(ws["npc"], (255, 64, 64), "npc"),
                     (ws["player"], (64, 160, 255), "player")]
            print("npc    %s  +-%s over %d %s" %
                  ([round(v) for v in ws["npc"]], ws["npcScatter"],
                   ws["npcRays"], ws["npcSource"]))
            print("player %s  %s" % ([round(v) for v in ws["player"]],
                                     ws["playerSource"]))
    print("camera %d  eye %s  at %s  fov %d horizontal" %
          (cam["id"], [round(v) for v in eye], [round(v) for v in at],
           cam["angle"][1]))
    render(name, eye, at, cam["angle"][1] or 74, a.out,
           mirror=a.mirror, over=a.over, marks=marks)
    print("wrote", a.out)

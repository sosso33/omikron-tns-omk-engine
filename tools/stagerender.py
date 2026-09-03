# SPDX-License-Identifier: GPL-3.0-or-later
r"""Draw a scene dialog's staging with the VIEWER's own matrices.

`tools/stagecheck.js --dump=<file>` runs `tools/omkweb.html`'s `stageMatrices`
under node and writes out the two model matrices it produced.  This reads that
file and rasterises the set plus both posed bodies through one of the
conversation's real dialogue cameras, so the picture is drawn with what the
page computes rather than with a second copy of the recipe - which is the whole
point, since both times the player's staging was wrong the fault was in the
client and every server-side number was already right.

    node tools/stagecheck.js 8752 387 AResto14 --dump=/tmp/s.json
    python3 tools/stagerender.py /tmp/s.json out.png [--cam N]

Add `--floor` to the stagecheck run for the same frame under the rule the
pelvis anchor replaced; the two PNGs side by side are what "the player sits in
the wrong spot" looks like.
"""
import sys, os, json, math, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import omkdata as O, camshot, tex3dt
from PIL import Image, ImageDraw

W, H = camshot.W, camshot.H


def qrot(q, v):
    w, x, y, z = q
    tx = 2 * (y * v[2] - z * v[1])
    ty = 2 * (z * v[0] - x * v[2])
    tz = 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))


def apply4(M, p):
    """The page's matrices are column-major, the way it hands them to WebGL."""
    return (M[0]*p[0] + M[4]*p[1] + M[8] *p[2] + M[12],
            M[1]*p[0] + M[5]*p[1] + M[9] *p[2] + M[13],
            M[2]*p[0] + M[6]*p[1] + M[10]*p[2] + M[14])


def avg_colors(path):
    """One flat colour per material - enough to read a body against a room."""
    try: txs = tex3dt.textures(path)
    except Exception: txs = []
    out = {}
    for i, t in enumerate(txs):
        rgb = t["rgb"]; n = len(rgb) // 3
        if not n: out[i] = (150, 150, 150); continue
        step = max(1, n // 2000); r = g = b = c = 0
        for k in range(0, n, step):
            r += rgb[3*k]; g += rgb[3*k+1]; b += rgb[3*k+2]; c += 1
        out[i] = (r // c, g // c, b // c)
    return out


def body(model, clip, frame, M, centred, centre):
    """The posed body, transformed by the page's matrix -> [(tri, colour)].

    `centred` mirrors the client's own asymmetry: the npc's vertex buffers are
    uploaded minus `centre` and the player's (bakeStatic) are raw, and the two
    matrices are built to match.  Getting this backwards is exactly the bug
    being tested for, so it is passed in rather than guessed.
    """
    g = O.model_geometry(model)
    meta, buf = O.ani_pose_stream(model, clip["scx"], clip["anim"])
    n = meta["meshes"]
    f = min(frame, meta["frames"] - 1)
    P = struct.unpack_from("<%df" % (n * 7), buf, f * n * 7 * 4)
    pts = []
    for i, L in enumerate(g["staticLocal"]):
        mi = g["staticMesh"][i]
        if L is None or mi is None or mi < 0:
            v = g["static"][i]; p = (v[0], v[1], v[2])
        else:
            b = mi * 7
            r = qrot(P[b:b+4], L)
            p = (r[0] + P[b+4], r[1] + P[b+5], r[2] + P[b+6])
        if centred: p = (p[0]-centre[0], p[1]-centre[1], p[2]-centre[2])
        pts.append(apply4(M, p))
    cols = avg_colors(os.path.join(O.PERSOS, model + ".3DO"))
    out = []
    for b in g["batches"]:
        col = cols.get(b["material"], (170, 150, 140))
        for t in range(b["start"], b["start"] + b["count"], 3):
            cs = [g["corners"][t+k] for k in range(3)]
            # the face mesh indexes faceBind, not staticLocal; it is one small
            # mesh and the staging question is about the body, so it is dropped
            if any(c[0] == 1 or c[1] >= len(pts) for c in cs): continue
            # back to the game's axes: the matrix delivered the viewer's Y-up
            out.append(([(pts[c[1]][0], -pts[c[1]][1], pts[c[1]][2])
                         for c in cs], col))
    return out


def main():
    src = sys.argv[1]
    out = sys.argv[2]
    cam_i = 0
    if "--cam" in sys.argv: cam_i = int(sys.argv[sys.argv.index("--cam") + 1])
    d = json.load(open(src))

    # both bodies, already in the viewer's space (Y up)
    tris = []
    tris += body(d["npcModel"], d["npcClip"], 0, d["M"], True, d["centre"])
    tris += body("HO1_FNM", d["plyClip"], d["plyFrame"], d["Mplayer"],
                 False, d["centre"])

    # the set, which the page draws through MFLIP alone
    geo = O.decor_geometry_cached(d["set"])
    V = geo["verts"]
    dcols = camshot._avg_colors(d["set"])

    # camshot.projector works in the GAME's space (its up is [0,-1,0]), so
    # everything stays in the game's axes and only the bodies are converted
    conv, cam = camshot.line_camera(d["dialog"], cam_i)
    eye = list(cam["pos"][0:3])
    at  = list(cam["pos"][3:6])
    fov = float(cam["angle"][1])
    label = "camera %d" % cam["id"]
    if "--camid" in sys.argv:                 # a camera by its own id
        cid = int(sys.argv[sys.argv.index("--camid") + 1])
        c2 = next(c for c in conv["cameras"] if c["id"] == cid)
        eye, at = list(c2["pos"][0:3]), list(c2["pos"][3:6])
        fov, label = float(c2["angle"][1]), "camera %d" % cid
    if "--wide" in sys.argv:
        # a wide eye-level shot from beside the pair, for looking at the whole
        # staging rather than at whatever a line camera frames
        n, p2 = d["ws"]["npc"], d["ws"]["player"]
        mid = [(n[k] + p2[k]) / 2 for k in range(3)]
        dx, dz = p2[0] - n[0], p2[2] - n[2]
        l = math.hypot(dx, dz) or 1.0
        # step off the line between them and back a little
        # `--wide N` sets how far to stand off (default 130); the room is
        # small, so a long lens puts the eye through a wall and draws black
        i = sys.argv.index("--wide")
        r = float(sys.argv[i+1]) if len(sys.argv) > i+1 and \
            not sys.argv[i+1].startswith("-") else 130.0
        eye = [mid[0] - dz / l * r, mid[1] - r * 0.55, mid[2] + dx / l * r]
        at, fov, label = [mid[0], mid[1] - 15, mid[2]], 60.0, "wide r=%g" % r
    pr = camshot.projector(eye, at, fov)

    img = Image.new("RGB", (W, H), (0, 0, 0))
    dr = ImageDraw.Draw(img)
    faces = []
    for b in geo["batches"]:
        col = dcols.get(b["material"], (120, 120, 120))
        for t in range(b["start"], b["start"] + b["count"], 3):
            P = [pr((V[t+k][0], V[t+k][1], V[t+k][2])) for k in range(3)]
            if any(p is None for p in P): continue
            lit = 0.25 + 0.75 * sum(V[t+k][5] for k in range(3)) / 3.0
            faces.append((max(p[2] for p in P), [(p[0], p[1]) for p in P],
                          tuple(int(v * lit) for v in col)))
    for pts3, col in tris:
        P = [pr(p) for p in pts3]
        if any(p is None for p in P): continue
        faces.append((max(p[2] for p in P), [(p[0], p[1]) for p in P], col))
    faces.sort(key=lambda t: -t[0])
    for _, poly, col in faces: dr.polygon(poly, fill=col)
    img.save(out)
    print("wrote %s  (dialog %d, %s, %s, %s anchor; eye %s at %s)" %
          (out, d["dialog"], d["set"], label,
           "floor" if d["floorMode"] else "pelvis",
           [round(v) for v in eye], [round(v) for v in at]))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render one frame of a 3DM onto a model's face mesh, to a PNG.

A software check on the same data the browser front end receives: the face
geometry from the .3DO, the per-frame vertices from the .3DM and the texture
from the .3dt. If this produces a face, the pipeline is right.

    python3 tools/facepreview.py 071348 BOZ_FNM 40 out.png
"""
import os, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import omkdata, tex3dt

W = H = 420

SKIP_PARENT = False

def render(asset, model, frame, out, shade=True):
    g = omkdata.face_geometry(model)
    meta, blob = omkdata.morph_frames(asset)
    if not g or not meta: raise SystemExit("missing data")
    nv = meta["vertices"]
    fl = struct.unpack("<%df" % (len(blob)//4), blob)
    frame = max(0, min(meta["frames"] - 1, frame))
    base = frame * nv * 6

    tex = None
    if g["material"] is not None:
        p = os.path.join(omkdata.PERSOS, g["model"] + ".3DO")
        if not os.path.exists(p):
            for fn in os.listdir(omkdata.PERSOS):
                if fn.lower() == (g["model"] + ".3do").lower():
                    p = os.path.join(omkdata.PERSOS, fn); break
        txs = tex3dt.textures(p)
        if 0 <= g["material"] < len(txs): tex = txs[g["material"]]

    def vert(src):
        if src < nv:
            o = base + src * 6
            return fl[o:o+3], fl[o+3:o+6]
        v = g["static"][src - nv]
        return v[0:3], v[3:6]

    pts = [vert(c[0])[0] for c in g["corners"]]
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]; zs = [p[2] for p in pts]
    cx, cy = (min(xs)+max(xs))/2, (min(ys)+max(ys))/2
    span = max(max(xs)-min(xs), max(ys)-min(ys)) or 1
    s = W * 0.82 / span

    img = bytearray(b"\x14\x16\x1c" * (W*H))
    zb = [1e9] * (W*H)

    def project(p):
        return (W/2 + (p[0]-cx)*s, H/2 + (p[1]-cy)*s, p[2])  # model Y points down

    skipped = 0
    for t in range(len(g["corners"]) // 3):
        c = g["corners"][t*3:t*3+3]
        if SKIP_PARENT and any(x[0] >= nv for x in c):
            skipped += 1; continue
        P, N = zip(*[vert(x[0]) for x in c])
        sc = [project(p) for p in P]
        # flat shade from the face normal
        nx, ny, nz = N[0]
        lit = 0.45 + 0.55 * max(0.0, nx*0.35 + ny*0.55 + nz*0.9) if shade else 1.0
        x0 = max(0, int(min(v[0] for v in sc))); x1 = min(W-1, int(max(v[0] for v in sc))+1)
        y0 = max(0, int(min(v[1] for v in sc))); y1 = min(H-1, int(max(v[1] for v in sc))+1)
        ax, ay = sc[0][0], sc[0][1]
        d = ((sc[1][1]-sc[2][1])*(sc[0][0]-sc[2][0]) + (sc[2][0]-sc[1][0])*(sc[0][1]-sc[2][1]))
        if abs(d) < 1e-9: continue
        for py in range(y0, y1+1):
            for px in range(x0, x1+1):
                l0 = ((sc[1][1]-sc[2][1])*(px+0.5-sc[2][0]) + (sc[2][0]-sc[1][0])*(py+0.5-sc[2][1]))/d
                l1 = ((sc[2][1]-sc[0][1])*(px+0.5-sc[2][0]) + (sc[0][0]-sc[2][0])*(py+0.5-sc[2][1]))/d
                l2 = 1-l0-l1
                if l0 < 0 or l1 < 0 or l2 < 0: continue
                z = l0*sc[0][2] + l1*sc[1][2] + l2*sc[2][2]
                o = py*W+px
                if z >= zb[o]: continue
                zb[o] = z
                if tex:
                    u = (l0*c[0][1] + l1*c[1][1] + l2*c[2][1]) / 256.0
                    v = (l0*c[0][2] + l1*c[1][2] + l2*c[2][2]) / 256.0
                    tx = min(tex["w"]-1, max(0, int(u*tex["w"])))
                    ty = min(tex["h"]-1, max(0, int(v*tex["h"])))
                    k = (ty*tex["w"]+tx)*3
                    r, gg, b = tex["rgb"][k], tex["rgb"][k+1], tex["rgb"][k+2]
                else:
                    r = gg = b = 190
                img[o*3]   = min(255, int(r*lit))
                img[o*3+1] = min(255, int(gg*lit))
                img[o*3+2] = min(255, int(b*lit))
    open(out, "wb").write(tex3dt.png(W, H, bytes(img)))
    print(f"{out}: {g['model']}/{g['faceMesh']} frame {frame+1}/{meta['frames']}, "
          f"{g['faceTris']} tris ({skipped} skinned to parent, skipped), "
          f"texture={'yes' if tex else 'no'}")

if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) < 3: sys.exit(__doc__)
    render(a[0], a[1], int(a[2]), a[3] if len(a) > 3 else "/tmp/face.png")

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render a whole .3DO character to a PNG, with an optional .3DM frame driving
the face. Uses omkdata.model_geometry - the same data path the browser app
takes - so this doubles as a check on what the app will show.

    python3 tools/modelpreview.py BOZ_FNM out.png [asset] [frame]
"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import omkdata, tex3dt

W = H = 460

def render(model, out, asset=None, frame=0, animate=True):
    g = omkdata.model_geometry(model)
    p = os.path.join(omkdata.PERSOS, g["model"] + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(omkdata.PERSOS):
            if fn.lower() == (g["model"] + ".3do").lower():
                p = os.path.join(omkdata.PERSOS, fn); break
    txs = tex3dt.textures(p)

    fl = None
    if asset:
        meta, blob = omkdata.morph_frames(asset)
        if meta and meta["vertices"] == g["faceVerts"]:
            fl = struct.unpack("<%df" % (len(blob)//4), blob)
            frame = max(0, min(meta["frames"] - 1, frame))
            nv = meta["vertices"]

    # per-frame skeleton pose, if the asset supplies one
    P = None
    if asset and animate:
        nq = omkdata.node_tracks(asset, omkdata.root_track_of(g["model"], asset))
        if nq and nq["count"]:
            P = omkdata.pose(g["model"], nq, frame)

    fo = g["faceOffset"]
    fmi = g["faceMeshIndex"]
    def place(loc, nrm, mesh_i, fallback_off):
        """rest position, or the animated one when a pose is available"""
        if P is not None and mesh_i in P:
            q, pos = P[mesh_i]
            r = omkdata._qrot(q, loc)
            return [r[0]+pos[0], r[1]+pos[1], r[2]+pos[2]]
        off = fallback_off if fallback_off is not None else [0, 0, 0]
        return [loc[0]+off[0], loc[1]+off[1], loc[2]+off[2]]

    def corner_pos(c):
        if c[0] == 1:
            bind = g["faceBind"][c[1]] if c[1] < len(g["faceBind"]) else [0,0,0,0,1,0,1.0]
            lit = bind[6] if len(bind) > 6 else 1.0
            if fl is not None:
                o = frame*nv*6 + c[1]*6
                loc = [fl[o], fl[o+1], fl[o+2]]; nrm = [fl[o+3], fl[o+4], fl[o+5]]
            else:
                loc = bind[0:3]; nrm = bind[3:6]
            return (place(loc, nrm, fmi, fo), nrm, lit)
        v = g["static"][c[1]]
        if P is None: return (v[0:3], v[3:6], v[6] if len(v) > 6 else 1.0)
        loc = g["staticLocal"][c[1]]
        return (place(loc, v[3:6], g["staticMesh"][c[1]], None),
                v[3:6], v[6] if len(v) > 6 else 1.0)

    pos = [corner_pos(c) for c in g["corners"]]
    xs=[q[0][0] for q in pos]; ys=[q[0][1] for q in pos]
    cx,cy=(min(xs)+max(xs))/2,(min(ys)+max(ys))/2
    span=max(max(xs)-min(xs), max(ys)-min(ys)) or 1
    s=W*0.86/span
    img=bytearray(b"\x14\x16\x1c"*(W*H)); zb=[1e9]*(W*H)
    proj=lambda q:(W/2+(q[0]-cx)*s, H/2+(q[1]-cy)*s, q[2])

    matof=[0]*len(g["corners"]); cutof=[False]*len(g["corners"])
    for b in g["batches"]:
        for i in range(b["start"], b["start"]+b["count"]):
            matof[i]=b["material"]; cutof[i]=b.get("cutout", False)

    for t in range(len(g["corners"])//3):
        c=g["corners"][t*3:t*3+3]; P=[pos[t*3+k] for k in range(3)]
        sc=[proj(q[0]) for q in P]
        # no fake light: the game bakes brightness into the vertex, and for
        # characters that is white, so the texture shows unshaded
        lit=P[0][2]
        mid=matof[t*3]; cut=cutof[t*3]; tex=txs[mid] if 0<=mid<len(txs) else None
        x0=max(0,int(min(v[0] for v in sc))); x1=min(W-1,int(max(v[0] for v in sc))+1)
        y0=max(0,int(min(v[1] for v in sc))); y1=min(H-1,int(max(v[1] for v in sc))+1)
        den=((sc[1][1]-sc[2][1])*(sc[0][0]-sc[2][0])+(sc[2][0]-sc[1][0])*(sc[0][1]-sc[2][1]))
        if abs(den)<1e-9: continue
        for py in range(y0,y1+1):
            for px in range(x0,x1+1):
                l0=((sc[1][1]-sc[2][1])*(px+.5-sc[2][0])+(sc[2][0]-sc[1][0])*(py+.5-sc[2][1]))/den
                l1=((sc[2][1]-sc[0][1])*(px+.5-sc[2][0])+(sc[0][0]-sc[2][0])*(py+.5-sc[2][1]))/den
                l2=1-l0-l1
                if l0<0 or l1<0 or l2<0: continue
                z=l0*sc[0][2]+l1*sc[1][2]+l2*sc[2][2]; o=py*W+px
                if z>=zb[o]: continue
                zb[o]=z
                if tex:
                    u=(l0*c[0][2]+l1*c[1][2]+l2*c[2][2])/tex["w"]
                    v=(l0*c[0][3]+l1*c[1][3]+l2*c[2][3])/tex["w"]  # width on both axes
                    tx=min(tex["w"]-1,max(0,int(u*tex["w"]))); ty=min(tex["h"]-1,max(0,int(v*tex["h"])))
                    k=(ty*tex["w"]+tx)*3
                    r,gg,b=tex["rgb"][k],tex["rgb"][k+1],tex["rgb"][k+2]
                    if cut and r==0 and gg==0 and b==0: continue   # cutout
                else: r=gg=b=180
                img[o*3]=min(255,int(r*lit));img[o*3+1]=min(255,int(gg*lit));img[o*3+2]=min(255,int(b*lit))
    open(out,"wb").write(tex3dt.png(W,H,bytes(img)))
    print(f"{out}: {g['model']} {g['triangles']} tris, {len(g['batches'])} materials, "
          f"face {'animated frame %d'%(frame+1) if fl else 'bind pose (no matching stream)'}")

if __name__=="__main__":
    a=sys.argv[1:]
    if not a: sys.exit(__doc__)
    render(a[0], a[1] if len(a)>1 else "/tmp/body.png",
           a[2] if len(a)>2 else None, int(a[3]) if len(a)>3 else 0,
           "--rest" not in sys.argv)

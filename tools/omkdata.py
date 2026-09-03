#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Data access layer shared by the web app: conversations, models, morphs."""
import os, re, struct, sys, collections, math

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import statistics
import omkpaths
import adp, morph3dm, mesh3do, tex3dt

DIALOG  = omkpaths.data("IAM/DIALOG")
TAGDIR  = omkpaths.data("IAM")
MORPH   = omkpaths.data("MORPH")
PERSOS  = omkpaths.data("MESHES/PERSOS")
DECORS  = omkpaths.data("MESHES/DECORS")

# ------------------------------------------------------------------ tags
def _tags():
    out = {}
    for fn in os.listdir(TAGDIR):
        if not fn.upper().endswith(".TAG"): continue
        cur = {}
        raw = open(os.path.join(TAGDIR, fn), "rb").read().decode("cp1252", "replace")
        for line in raw.splitlines():
            m = re.match(r"^(\d+)=(.*)$", line)
            if m: cur[int(m.group(1))] = m.group(2).strip()
        out[fn[:-4].upper()] = cur
    return out
TAGS = _tags()
def tag(sec, i): return TAGS.get(sec, {}).get(i)

# ------------------------------------------------------------------ dialog
def _chunks():
    d = open(DIALOG, "rb").read(); n = len(d); first = None
    for i in range(n // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n:
            if first is None or off < first: first = off
        if first is not None and 8 * (i + 1) > first: break
    out = {}
    for i in range(first // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n and size >= 8:
            out[i] = d[off:off + size]
    return out
CHUNKS = _chunks()

def _strings(b, at, count=6):
    out, p = [], at
    for _ in range(count):
        if p >= len(b): break
        e = b.find(b"\0", p)
        if e < 0: break
        out.append(b[p:e].decode("cp1252", "replace")); p = e + 1
    return out

def conversation(idx):
    b = CHUNKS.get(idx)
    if not b: return None
    speaker, nn, nc, _ = struct.unpack_from("<4h", b, 0)
    if nn <= 0 or nc <= 0 or 8 + 64 * nn + 44 * nc > len(b): return None
    nodes = []
    for j in range(nn):
        o = 8 + 64 * j
        ptr = struct.unpack_from("<9I", b, o)
        pool = _strings(b, ptr[8]) if ptr[8] else []
        nodes.append({
            "index": j,
            "id": struct.unpack_from("<h", b, o + 44)[0],
            "asset": b[o+46:o+56].split(b"\0")[0].decode("ascii", "replace"),
            "param": list(struct.unpack_from("<4h", b, o + 36)),
            "line": (pool[0].strip() if len(pool) > 0 else ""),
            "selfLine": (pool[5].strip() if len(pool) > 5 else ""),
            "replies": [(pool[1+k].strip() if len(pool) > 1+k else "") for k in range(4)],
            "lineCamera":   struct.unpack_from("<h", b, o + 60)[0],
            "lineCamera2":  struct.unpack_from("<h", b, o + 62)[0],
            "replyCamera":  struct.unpack_from("<h", b, o + 56)[0],
            "replyCamera2": struct.unpack_from("<h", b, o + 58)[0],
            "cond": [ptr[k] for k in range(4)],
            "act":  [ptr[4+k] for k in range(4)],
        })
    cams = []
    for j in range(nc):
        o = 8 + 64 * nn + 44 * j
        # the file stores these in authoring units; Dialog_Load converts them
        # once at load time, so do the same here
        raw = struct.unpack_from("<6i", b, o)
        ang = struct.unpack_from("<2h", b, o + 28)
        cams.append({"id": struct.unpack_from("<h", b, o + 24)[0],
                     "pos": [int(v * (100.0 / 256.0 / 2.54) - 1.0) for v in raw],
                     "angle": [int(a * (360.0 / 4096.0)) for a in ang],
                     "subject": list(struct.unpack_from("<2H", b, o + 32))})
    return {"id": idx, "name": tag("DIALOGS", idx) or "", "speaker": speaker,
            "nodes": nodes, "cameras": cams}

def conversations():
    out = []
    for i in sorted(CHUNKS):
        c = conversation(i)
        if c:
            first = c["nodes"][0]["asset"] if c["nodes"] else ""
            out.append({"id": i, "name": c["name"], "nodes": len(c["nodes"]),
                        "asset": first, "hasMorph": os.path.exists(
                            os.path.join(MORPH, first + ".3DM")) if first else False})
    return out

def chunk_bytes(idx): return CHUNKS.get(idx)

# ------------------------------------------------------------------ models
_MODELS = None
def models():
    global _MODELS
    if _MODELS is not None: return _MODELS
    out = []
    for fn in sorted(os.listdir(PERSOS)):
        if not fn.lower().endswith(".3do"): continue
        p = os.path.join(PERSOS, fn)
        try: h, ms = mesh3do.meshes(p)
        except Exception: continue
        # A model without a `visage` mesh used to be dropped here, which left
        # 64 of the 193 characters selectable. That was fine while the only way
        # to choose one was matching a .3DM's face-vertex count - but the actor
        # record names the speaker outright, and 23 conversations name a
        # face-less model (AN1_FN, YES_FN, ...). They have a body and animate;
        # they just cannot lip-sync, which `faceVerts: 0` says.
        vis = [m for m in ms if "visage" in m["name"].lower()]
        out.append({"file": fn, "name": fn[:-4],
                    "faceMesh": vis[0]["name"] if vis else "",
                    "faceVerts": vis[0]["vertices"] if vis else 0,
                    "faceTris": vis[0]["triangles"] if vis else 0,
                    "meshes": h["meshes"], "nodes": h["meshes"] - 1,
                    "materials": h["materials"]})
    _MODELS = out
    return out

def face_geometry(model):
    """Flat (non-indexed) face triangles: one entry per corner, giving the
    source vertex index and its UV. Corners whose index has the top bit set
    come from the parent mesh and are static, so they are appended after the
    animated face vertices."""
    p = os.path.join(PERSOS, model if model.lower().endswith(".3do") else model + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(PERSOS):
            if fn.lower() == (model + ".3do").lower(): p = os.path.join(PERSOS, fn); break
    h, ms = mesh3do.meshes(p)
    d = open(p, "rb").read()
    base_v = base_t = 0
    face = None
    for m in ms:
        if "visage" in m["name"].lower(): face = m; break
        base_v += m["vertices"]; base_t += m["triangles"]
    if face is None: return None

    # Vertices borrowed from the parent mesh are stored in the parent's local
    # space; the .3DM stream is in the face mesh's. Shift them by the offset
    # between the two mesh origins so both end up in the same space.
    # A face index with the top bit set refers to the PARENT mesh's own vertex
    # array, not the model-wide one, so it needs the parent's base added.
    # The parent stores its vertices in its own local space, so they are also
    # shifted by the offset between the two mesh origins.
    parent = next((m for m in ms if m["id"] == face["parent"]), None)
    pbase, acc = 0, 0
    for m in ms:
        if parent and m is parent: pbase = acc
        acc += m["vertices"]
    off = [face["pos"][k] - parent["pos"][k] for k in range(3)] if parent else [0, 0, 0]

    static, static_map = [], {}
    corners, mats = [], []
    for t in range(base_t, base_t + face["triangles"]):
        o = h["triOff"] + 28 * t
        idx = struct.unpack_from("<3h", d, o)
        uv  = struct.unpack_from("<6B", d, o + 6)
        mid = struct.unpack_from("<i", d, o + 12)[0]
        mats.append(mid)
        for k in range(3):
            i = idx[k]
            if i < 0:                       # parent mesh vertex: static
                gi = (i & 0x7FFF)
                if gi not in static_map:
                    static_map[gi] = len(static)
                    vo = h["vtxOff"] + 32 * (pbase + gi)
                    v = list(struct.unpack_from("<6f", d, vo))
                    v[0] -= off[0]; v[1] -= off[1]; v[2] -= off[2]
                    static.append(v)
                src = face["vertices"] + static_map[gi]
            else:
                src = i if i < face["vertices"] else face["vertices"] - 1
            corners.append([src, uv[k*2] + 1, uv[k*2+1] + 1])
    tex = None
    try:
        txs = tex3dt.textures(p)
        mid = mats[0] if mats else 0
        if 0 <= mid < len(txs): tex = mid
    except Exception:
        pass
    return {"model": os.path.basename(p)[:-4], "faceMesh": face["name"],
            "faceVerts": face["vertices"], "faceTris": face["triangles"],
            "facePos": list(face["pos"]),
            "parentOffset": off,
            "corners": corners, "static": static, "material": tex,
            "materials": [t["name"] for t in (txs if tex is not None else [])]}

# ------------------------------------------------------------------ morph
def morph_path(asset):
    for cand in (asset + ".3DM", asset.upper() + ".3DM", asset.lower() + ".3dm"):
        p = os.path.join(MORPH, cand)
        if os.path.exists(p): return p
    return None

def morph_frames(asset):
    """-> (meta, float32 blob of frames * verts * 6)"""
    p = morph_path(asset)
    if not p: return None, None
    L = morph3dm.layout(p)
    d = open(p, "rb").read()
    n = L["frames"]; nv = L["vertices"]
    buf = bytearray()
    vb0 = 12 + 16 * L["nodes"]
    # The leading float[3] of each record, served RAW. Reading it as deltas
    # (which the engine's parser does integrate) is refuted by the corpus -
    # nearly every file carries a near-constant unit-ish vector, so the
    # integral walks every character. Its playable meaning is not
    # established, the viewer does not apply it, and it is exposed here only
    # so future work can look at it without re-parsing files.
    root = []
    for i in range(n):
        o = L["preamble"] + i * L["record"]
        root.append([round(x, 4) for x in struct.unpack_from("<3f", d, o)])
        buf += d[o + vb0:o + vb0 + 24 * nv]
    return {"frames": n, "vertices": nv, "nodes": L["nodes"],
            "fps": 30, "record": L["record"], "root": root}, bytes(buf)

def morph_audio(asset):
    p = morph_path(asset)
    if not p: return None
    pcm, ch, L = morph3dm.read(p)
    return adp.wav(pcm, ch)


def model_geometry(model):
    """Whole character, ready to draw: triangle corners grouped by material.

    Mesh positions are absolute in model space - accumulating them up the
    parent chain pulls the model apart, rendering them as-is gives a coherent
    figure. Vertices are stored per-mesh in that mesh's local space, so each
    one is shifted by its mesh position.

    Corners are [isFace, index, u, v]; when isFace the index is into the face
    mesh's own vertex array, which is exactly what a .3DM frame supplies, and
    the caller adds `faceOffset`. Everything else is baked to model space in
    `static`.
    """
    p = os.path.join(PERSOS, model + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(PERSOS):
            if fn.lower() == (model + ".3do").lower():
                p = os.path.join(PERSOS, fn); break
    h, ms = mesh3do.meshes(p)
    d = open(p, "rb").read()
    byid = {m["id"]: m for m in ms}
    # the head family, for the look-at turn: the neck pivots them
    headMeshes = [m["i"] for m in ms
                  if any(k in m["name"].lower()
                         for k in ("tete", "visage", "cou", "cheveu"))]
    headPivot = next((m["i"] for m in ms if "cou" in m["name"].lower()),
                     headMeshes[0] if headMeshes else -1)
    basev, baset, av, at = {}, {}, 0, 0
    for m in ms:
        basev[m["i"]] = av; baset[m["i"]] = at
        av += m["vertices"]; at += m["triangles"]
    face = next((m for m in ms if "visage" in m["name"].lower()), None)
    fbase = basev[face["i"]] if face else -1
    fn = face["vertices"] if face else 0
    # the model's own face vertices, in the same local space a .3DM frame uses.
    # Used as the bind pose whenever the chosen model's face vertex count does
    # not match the stream's, so the character still draws correctly - just
    # without lip-sync - instead of collapsing.
    def vlight(gi):
        # The importer renders `Color.white * lighting.g`; the lighting field is
        # 4 bytes at +28 and its green byte is the baked brightness. Every
        # character vertex has it at 255 (unlit, full-bright texture) - it is
        # scenery that actually varies.
        return d[h["vtxOff"] + 32 * gi + 29] / 255.0

    facebind = [list(struct.unpack_from("<6f", d, h["vtxOff"] + 32 * (fbase + i)))
                + [vlight(fbase + i)] for i in range(fn)] if face else []

    # `static` holds the rest pose in model space; `staticLocal` keeps the same
    # vertex in its own mesh's local space along with that mesh's index, which
    # is what an animated pose needs.
    static, staticLocal, staticMesh, smap, groups = [], [], [], {}, {}
    def push_static(gi, off, owner):
        key = (gi, round(off[0], 4), round(off[1], 4), round(off[2], 4))
        if key not in smap:
            smap[key] = len(static)
            v = list(struct.unpack_from("<6f", d, h["vtxOff"] + 32 * gi))
            static.append([v[0]+off[0], v[1]+off[1], v[2]+off[2],
                           v[3], v[4], v[5], vlight(gi)])
            staticLocal.append([v[0], v[1], v[2]])
            staticMesh.append(owner)
        return smap[key]

    def ancestor_for(m, k):
        """A face index with the top bit set is skinned to a mesh higher in the
        hierarchy. Two things have to be skipped on the way up.

        First the attachment markers: an arm's parent is the 3-vertex shoulder
        marker, whose coordinates sit far outside the model, so an index of 0-2
        "fits" it and drags a stray triangle off the shoulder. The reference
        importer skips any mesh with `vertexCount <= 3` for exactly this reason
        (its own comment: "issues on character's shoulders!!!").

        Then the range: 1529 of the 9405 skinned corners in the shipped models
        index past their immediate parent's vertex array, and every one of them
        fits some further ancestor.
        """
        cur, guard = byid.get(m["parent"]), 0
        while cur is not None and guard < 16:
            if cur["vertices"] > 3 and k < cur["vertices"]: return cur
            cur = byid.get(cur["parent"]); guard += 1
        return None

    def drawable(m):
        """A mesh with no render flags at all is not geometry - across PERSOS
        that is 123 meshes out of 3730, all of them `Vise`/`Tire` aim and shoot
        markers or `M*` proxy skeletons, and they show up as stray black
        rectangles if drawn. CollisionOnly and the bullet mesh are skipped for
        the same reason the reference importer skips them."""
        f = m["flags"] & 0xFFFFFFFF
        if f == 0: return False
        if f & 0x800000: return False          # CollisionOnly
        if m["name"].lower() == "tir": return False
        # Attachment markers: a single triangle of 3 vertices with flag bit 0.
        # 532 of the 547 bit-0 meshes in PERSOS are exactly this shape and all
        # are named *Epauled / *Epauleg / *Ventre (shoulders and belly); they
        # sit far outside the model and draw as stray dark rectangles. Only 4
        # meshes of that shape lack the flag, so the pair of tests is precise.
        if (f & 1) and m["vertices"] <= 3 and m["triangles"] <= 1: return False
        return True

    for m in ms:
        if not drawable(m): continue
        off = list(m["pos"])
        for t in range(baset[m["i"]], baset[m["i"]] + m["triangles"]):
            o = h["triOff"] + 28 * t
            idx = struct.unpack_from("<3h", d, o)
            uv  = struct.unpack_from("<6B", d, o + 6)
            mid = struct.unpack_from("<i", d, o + 12)[0]
            if mid < 0: continue                    # not drawn
            tri, ok = [], True
            for k in range(3):
                i = idx[k]
                if i < 0:
                    mi = i & 0x7FFF
                    anc = ancestor_for(m, mi)
                    if anc is None: ok = False; break
                    gi = basev[anc["i"]] + mi
                    src, poff, owner = gi, list(anc["pos"]), anc["i"]
                else:
                    gi = basev[m["i"]] + i
                    src, poff, owner = gi, off, m["i"]
                if gi >= h["vertices"]: ok = False; break
                if fbase >= 0 and fbase <= gi < fbase + fn and poff == list(face["pos"]):
                    tri.append([1, gi - fbase, uv[k*2], uv[k*2+1]])
                else:
                    tri.append([0, push_static(gi, poff, owner), uv[k*2], uv[k*2+1]])
            # flag 0x800 (MaterialCutout) means solid black in the texture is
            # the transparent colour - Boz is entirely cutout meshes, which is
            # why he reads as a fragmented hologram rather than a gold statue.
            if ok: groups.setdefault((mid, bool(m["flags"] & 0x800)), []).extend(tri)

    try: txs = tex3dt.textures(p)
    except Exception: txs = []
    corners, batches = [], []
    for key in sorted(groups):
        mid, cut = key
        batches.append({"material": mid, "cutout": cut, "start": len(corners),
                        "count": len(groups[key]),
                        "name": txs[mid]["name"] if 0 <= mid < len(txs) else "",
                        "w": txs[mid]["w"] if 0 <= mid < len(txs) else 256,
                        "h": txs[mid]["w"] if 0 <= mid < len(txs) else 256,
                        "exact": txs[mid]["exact"] if 0 <= mid < len(txs) else True})
        corners.extend(groups[key])
    # The body's vertical axis, for standing a model in a set and turning it to
    # face someone.  A model's *origin* will not do: AMH_FN's body sits 570
    # units away from it, and even AKG_FN's is 14 units off, so rotating about
    # the origin swings the body sideways.  The pelvis mesh is the axis - it
    # matches the bounding-box centre to within a unit and 180 of the 181
    # character models have one.
    pelvis = next((m for m in ms if "bassin" in m["name"].lower()), None)
    if pelvis:
        axis = [pelvis["pos"][0], pelvis["pos"][2]]
    elif static:
        axis = [(min(v[0] for v in static) + max(v[0] for v in static)) / 2,
                (min(v[2] for v in static) + max(v[2] for v in static)) / 2]
    else:
        axis = [0.0, 0.0]

    # The whole pelvis rest position, not just its x/z, because it is also the
    # ANCHOR a scene clip's root places: `scene_idle` only accepts a root track
    # whose first track is the `*Bassin` one, so key 0 is this mesh's world
    # position.  It is usable as an anchor in every pose because the pelvis is
    # the hierarchy root in all 181 character models - `_compose` gives a
    # parentless mesh `tuple(m["pos"])` unchanged, so no rotation moves it.
    pelvisPos = [float(v) for v in pelvis["pos"]] if pelvis else None
    # the model's own feet height (max Y - the game's Y points down), for the
    # floor-standing anchor when a pose does not declare its own
    feetY = max((v[1] for v in static), default=0.0) if static else 0.0

    return {"model": os.path.basename(p)[:-4], "bodyAxis": axis,
            "pelvis": pelvisPos, "feetY": round(feetY, 1),
            "faceMesh": face["name"] if face else "",
            "faceVerts": fn, "faceOffset": list(face["pos"]) if face else [0,0,0],
            "faceBind": facebind,
            "faceMeshIndex": face["i"] if face else -1,
            "headMeshes": headMeshes, "headPivot": headPivot,
            "staticLocal": staticLocal, "staticMesh": staticMesh,
            "meshes": h["meshes"], "triangles": len(corners) // 3,
            "corners": corners, "static": static, "batches": batches}


# ------------------------------------------------------------------ sets
def list_decors():
    """Every set in MESHES/DECORS, with how much geometry it carries."""
    out = []
    for fn in sorted(os.listdir(DECORS)):
        if not fn.lower().endswith(".3do"): continue
        try: h = mesh3do.header(os.path.join(DECORS, fn))
        except Exception: continue
        out.append({"name": fn[:-4], "triangles": h["triangles"] + 2 * h["quads"],
                    "meshes": h["meshes"], "cameras": h["cameras"]})
    return out


def decor_path(name):
    p = os.path.join(DECORS, name + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(DECORS):
            if fn.lower() == (name + ".3do").lower():
                return os.path.join(DECORS, fn)
    return p


_DECOR_CACHE = {}

def decor_geometry_cached(name):
    if name not in _DECOR_CACHE:
        _DECOR_CACHE[name] = decor_geometry(name)
    return _DECOR_CACHE[name]


_COLL_CACHE = {}

def decor_collision(name, walkable=True):
    r"""A set's WALKABLE surfaces - the probe set the engine's walker implies.

    Two differences from the render soup `decor_geometry` builds:

      * **every** mesh is included, the CollisionOnly (0x800000) volumes too -
        the engine's ground probe runs on collision geometry, and the render
        pass is what drops those meshes, not the walker;
      * only triangles the walker could stand on are kept: `Actor_Move`
        (0x00469580) rejects slopes past the tunable at 0x91033C - shipped
        value **30 degrees** - so faces steeper than that (walls, table sides)
        never answer a ground probe. Game Y grows downward, so a floor's
        normal has a *negative* y.

    -> flat [x0,y0,z0, x1,y1,z1, ...] triangle list, world space.
    """
    import mesh3do
    p = decor_path(name)
    if not os.path.exists(p): return None
    h, ms = mesh3do.meshes(p)
    d = open(p, "rb").read()
    byid = {m["id"]: m for m in ms}
    basev, baset, baseq = {}, {}, {}
    av = at = aq = 0
    for m in ms:
        basev[m["i"]] = av; baset[m["i"]] = at; baseq[m["i"]] = aq
        av += m["vertices"]; at += m["triangles"]; aq += m["quads"]

    def vertex(gi, off):
        v = struct.unpack_from("<3f", d, h["vtxOff"] + 32 * gi)
        return (v[0] + off[0], v[1] + off[1], v[2] + off[2])

    def ancestor_for(m, k):
        cur, guard = byid.get(m["parent"]), 0
        while cur is not None and guard < 16:
            if cur["vertices"] > 3 and k < cur["vertices"]: return cur
            cur = byid.get(cur["parent"]); guard += 1
        return None

    def resolve(m, i, off):
        if i < 0:
            anc = ancestor_for(m, i & 0x7FFF)
            if anc is None: return None
            return vertex(basev[anc["i"]] + (i & 0x7FFF), anc["pos"])
        gi = basev[m["i"]] + i
        return None if gi >= h["vertices"] else vertex(gi, off)

    COS30 = 0.86602540378
    out = []
    def emit(a, b, c):
        ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]
        vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
        nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
        n2 = nx*nx + ny*ny + nz*nz
        if n2 <= 0: return
        # walkable=True keeps only what the ground probe can answer with;
        # walkable=False keeps EVERY face, which is what the narrow phase
        # sweeps against - `Sweep_MeshTest` filters by mesh flag, not by slope
        if walkable and abs(ny) / (n2 ** 0.5) < COS30: return
        out.extend(a); out.extend(b); out.extend(c)

    for m in ms:
        off = m["pos"]
        for t in range(baset[m["i"]], baset[m["i"]] + m["triangles"]):
            o = h["triOff"] + 28 * t
            idx = struct.unpack_from("<3h", d, o)
            cs = [resolve(m, idx[k], off) for k in range(3)]
            if None not in cs: emit(*cs)
        for q in range(baseq[m["i"]], baseq[m["i"]] + m["quads"]):
            o = h["quadOff"] + 32 * q
            idx = struct.unpack_from("<4h", d, o)
            cs = [resolve(m, idx[k], off) for k in range(4)]
            if None not in cs:
                emit(cs[0], cs[1], cs[2]); emit(cs[0], cs[2], cs[3])
    return out


def decor_collision_cached(name):
    if name not in _COLL_CACHE:
        _COLL_CACHE[name] = decor_collision(name)
    return _COLL_CACHE[name]


_BLOCK_CACHE = {}

def decor_blockers_cached(name):
    """Every collision face of a set, walls included."""
    if name not in _BLOCK_CACHE:
        _BLOCK_CACHE[name] = decor_collision(name, walkable=False)
    return _BLOCK_CACHE[name]


_WALL_CACHE = {}

def decor_walls_cached(name):
    r"""The faces a walker canNOT stand on - what the horizontal sweep hits.

    The complement of `decor_collision`'s walkable set, and the split matters.
    The engine keeps the vertical and the horizontal apart: the ground probe
    answers with a floor, the sweep answers with a wall. Sweeping a horizontal
    move against FLOORS makes the actor block on his own footing - the sphere
    sits one radius above the floor, so a floor with any slope at all gives a
    tiny closing rate and therefore a hit at t about 0, and the actor cannot
    take a step. Steeper than the 30 degrees the walker climbs, and it is a
    wall; shallower, and it is the probe's business.
    """
    if name not in _WALL_CACHE:
        tris = decor_collision(name, walkable=False) or []
        COS30 = 0.86602540378
        out = []
        for i in range(0, len(tris), 9):
            a = tris[i:i+3]; b = tris[i+3:i+6]; c = tris[i+6:i+9]
            ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]
            vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
            nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
            n2 = nx*nx + ny*ny + nz*nz
            if n2 <= 0: continue
            if abs(ny) / (n2 ** 0.5) >= COS30: continue     # a floor, not a wall
            out.extend(tris[i:i+9])
        _WALL_CACHE[name] = out
    return _WALL_CACHE[name]


def floor_under_walkable(tris, p):
    """Like floor_under, over the walkable collision soup."""
    if not tris: return None
    best = None
    for t in range(0, len(tris), 9):
        ax, ay, az = tris[t], tris[t+1], tris[t+2]
        bx, by, bz = tris[t+3], tris[t+4], tris[t+5]
        cx, cy, cz = tris[t+6], tris[t+7], tris[t+8]
        d = (bz-cz)*(ax-cx) + (cx-bx)*(az-cz)
        if abs(d) < 1e-9: continue
        w0 = ((bz-cz)*(p[0]-cx) + (cx-bx)*(p[2]-cz)) / d
        w1 = ((cz-az)*(p[0]-cx) + (ax-cx)*(p[2]-cz)) / d
        w2 = 1 - w0 - w1
        if w0 < 0 or w1 < 0 or w2 < 0: continue
        y = w0*ay + w1*by + w2*cy
        if y > p[1] + 1 and (best is None or y < best): best = y
    return best


def ground_under(tris, p):
    r"""The LOWEST walkable surface over this x/z - the room's own ground.

    Not `floor_under_walkable`, which answers with the NEAREST surface below a
    point.  For a body that is standing those agree; for one **sitting** they
    do not, because the nearest surface below its pelvis is the seat.  Probing
    a seated speaker in AResto14 returns 15.9, the stool, where the room's
    ground is 31.7 - and a check that calls the first of those "the floor"
    then reads a correctly seated character as having sunk 16 units through
    it.  The game's Y grows downward, so the ground is the largest y.
    """
    best = None
    for t in range(0, len(tris), 9):
        ax, ay, az = tris[t], tris[t+1], tris[t+2]
        bx, by, bz = tris[t+3], tris[t+4], tris[t+5]
        cx, cy, cz = tris[t+6], tris[t+7], tris[t+8]
        d = (bz-cz)*(ax-cx) + (cx-bx)*(az-cz)
        if abs(d) < 1e-9: continue
        w0 = ((bz-cz)*(p[0]-cx) + (cx-bx)*(p[2]-cz)) / d
        w1 = ((cz-az)*(p[0]-cx) + (ax-cx)*(p[2]-cz)) / d
        if w0 < 0 or w1 < 0 or 1 - w0 - w1 < 0: continue
        y = w0*ay + w1*by + (1-w0-w1)*cy
        if best is None or y > best: best = y
    return best


def floor_under(geo, p):
    """Y of the nearest surface below p, or None.

    Game Y grows downward, so "below" means a larger Y.  Straight ray-cast down
    through the set's triangles."""
    V, best = geo["verts"], None
    for t in range(0, len(V), 3):
        a, b, c = V[t], V[t+1], V[t+2]
        d = (b[2]-c[2])*(a[0]-c[0]) + (c[0]-b[0])*(a[2]-c[2])
        if abs(d) < 1e-9: continue
        w0 = ((b[2]-c[2])*(p[0]-c[0]) + (c[0]-b[0])*(p[2]-c[2])) / d
        w1 = ((c[2]-a[2])*(p[0]-c[0]) + (a[0]-c[0])*(p[2]-c[2])) / d
        w2 = 1 - w0 - w1
        if w0 < 0 or w1 < 0 or w2 < 0: continue
        y = w0*a[1] + w1*b[1] + w2*c[1]
        if y > p[1] + 1 and (best is None or y < best): best = y
    return best


_ADDR = None
_DLG_ADDR = None

def _address_table():
    r"""ADDRESSES id -> world position, from AREA +60 (16 bytes, id at +14)."""
    global _ADDR
    if _ADDR is None:
        import dialog_triggers as T
        conv = lambda v: int(v * 100 * 0.00390625 * 0.3937007874015748 - 1.0)
        _ADDR = {}
        for k, b in sorted(T.archive(os.path.join(TAGDIR, "AREA")).items()):
            if len(b) < 84: continue
            lo = struct.unpack_from("<i", b, 60)[0]
            n = struct.unpack_from("<h", b, 82)[0]
            if n <= 0 or lo <= 0 or lo + 16 * n > len(b): continue
            for i in range(n):
                o = lo + 16 * i
                x, y, z = struct.unpack_from("<3i", b, o)
                _ADDR[struct.unpack_from("<h", b, o + 14)[0]] = [conv(x), conv(y), conv(z)]
    return _ADDR


def dialog_player_address(dialog_id):
    r"""Where the script puts the player before starting this conversation.

    The cutscene idiom in the world scripts is

        actor.goto_address N   ->   fade.to_black   ->   dialog.start   ->   fade.from_black

    so when a `dialog.start` is preceded by an `actor.goto_address`, that
    address is the authored player position - exact, rather than inferred from
    where the reply cameras happen to look. 95 conversations have one, and the
    names say what they are: 'Teleport Dialogue', 'Dialogue Vendeur'.

    It agrees with the camera-derived guess on 60 of the 65 conversations where
    both exist, median gap 43 units, so the two corroborate each other; the
    handful that disagree are ones the inference gets wrong.
    """
    global _DLG_ADDR
    if _DLG_ADDR is None:
        import dialog_disasm as D, dialog_triggers as T
        _DLG_ADDR = {}
        for name, fn in (("AREA", T.area_records), ("SCENE", T.scene_records)):
            for k, b in sorted(T.archive(os.path.join(TAGDIR, name)).items()):
                r = fn(b)
                if not r: continue
                for rec, f, p in (list(T._scripts_from_records(b, r[0], r[1]))
                                  + T._second_table(name, b)):
                    ops, st = D.disasm(b, p, len(b))
                    if st != "ok": continue
                    for i, (pc, op, raw) in enumerate(ops):
                        if op != 61 or len(raw) != 2: continue
                        did = struct.unpack("<h", raw)[0]
                        for j in range(max(0, i - 6), i):
                            if ops[j][1] == 73 and len(ops[j][2]) == 2:
                                _DLG_ADDR.setdefault(
                                    did, struct.unpack("<h", ops[j][2])[0])
                                break
    aid = _DLG_ADDR.get(dialog_id)
    if aid is None: return None
    pos = _address_table().get(aid)
    return None if pos is None else {"address": aid, "pos": pos,
                                     "name": tag("ADDRESSES", aid)}


def _converge(rays, drop=3.0):
    """Least-squares intersection of a bundle of aim rays.

    Each ray is (eye, unit direction).  Minimising the summed squared
    perpendicular distance gives the linear system

        sum (I - u u^T) P = sum (I - u u^T) e

    One pass of outlier rejection follows: a conversation's cameras are not
    all reverse shots on the same subject, and a single wide establishing shot
    aimed elsewhere drags the point off.  Rays more than `drop` times the
    median perpendicular distance away are dropped and the fit repeated.

    -> (point, median perpendicular residual, rays kept) or None.
    """
    def solve(rs):
        A = [[0.0] * 3 for _ in range(3)]
        b = [0.0] * 3
        for e, u in rs:
            for i in range(3):
                for j in range(3):
                    m = (1.0 if i == j else 0.0) - u[i] * u[j]
                    A[i][j] += m
                    b[i]    += m * e[j]
        M = [A[i][:] + [b[i]] for i in range(3)]
        for i in range(3):
            pv = max(range(i, 3), key=lambda r: abs(M[r][i]))
            if abs(M[pv][i]) < 1e-9: return None
            M[i], M[pv] = M[pv], M[i]
            for r in range(3):
                if r == i: continue
                f = M[r][i] / M[i][i]
                for c in range(i, 4): M[r][c] -= f * M[i][c]
        return [M[i][3] / M[i][i] for i in range(3)]

    def perp(P, e, u):
        t = sum((P[i] - e[i]) * u[i] for i in range(3))
        return sum((e[i] + t * u[i] - P[i]) ** 2 for i in range(3)) ** 0.5

    rs = list(rays)
    if len(rs) < 2: return None
    P = solve(rs)
    if P is None: return None
    d = [perp(P, e, u) for e, u in rs]
    lim = max(5.0, drop * statistics.median(d))
    keep = [r for r, dd in zip(rs, d) if dd <= lim]
    if 2 <= len(keep) < len(rs):
        Q = solve(keep)
        if Q is not None:
            P, rs = Q, keep
            d = [perp(P, e, u) for e, u in rs]
    return P, statistics.median(d), len(rs)


def _camera_rays(conv, which):
    """The aim rays of one half of a conversation's cameras.

    `which` is "line" (the camera used while a node's own line is spoken, so
    it frames whoever speaks it - the other character in all but a handful of
    nodes) or "reply" (the camera for choosing a reply).

    Only cameras with subject -1 are used: those hold absolute scene
    coordinates.  The 253 that name a subject are offsets from that actor and
    would need the actor's position, which is what this is trying to find.
    """
    fields = (("lineCamera", "lineCamera2") if which == "line"
              else ("replyCamera", "replyCamera2"))
    by = {c["id"]: c for c in conv["cameras"]
          if tuple(c["subject"]) == (65535, 65535)}
    ids, out = set(), []
    for n in conv["nodes"]:
        ids.update(n[f] for f in fields)
    for i in sorted(ids):
        c = by.get(i)
        if not c: continue
        e, a = c["pos"][:3], c["pos"][3:6]
        d = [a[k] - e[k] for k in range(3)]
        L = (d[0]*d[0] + d[1]*d[1] + d[2]*d[2]) ** 0.5
        if L < 1: continue
        out.append((e, [v / L for v in d]))
    return out


def speaker_positions(conv, decor_name):
    r"""Where the two speakers stand in a set.

    **The look-at point is not the subject.**  `pos[3..5]` sits at a fixed
    768 raw units from `pos[0..2]` - 1615 of the 1670 absolute cameras in the
    shipped file are within 1 unit of exactly that - so it is an aim handle,
    a direction stored as a point, and nothing about it says how far away the
    person being framed is.  Taking the median of those points, as this used
    to, put every speaker 118 units from the camera whatever the shot.

    What does locate them is where the rays MEET.  A conversation's line
    cameras all aim at the character speaking, from slightly different places,
    so intersecting them in the least-squares sense gives that character's
    head.  The bundle really does converge: across the 89 conversations with
    three or more usable line cameras the median residual is **1.9 units**.

    Checked against the game itself.  For dialog 402 the ray bundle meets at
    (3502.8, 1020.0, -901.2); solving a real screenshot of that line for the
    depth at which TEL_FNM's head is the size it appears puts her at
    (3502.3, 1018.6, -899.3) - **2.4 units apart**, from two methods sharing
    no assumptions.  The screenshot solution is itself self-checking: it was
    fitted on head size alone, and the feet then land at y=1081.5 against a
    floor at 1078.

    The player's position is authored where the launching script teleports
    him (`actor.goto_address`, 95 conversations); the reply cameras are the
    fallback, and they are a poor one - their bundle converges to a median
    residual of 9.1 rather than 1.9, because a reply shot is framed much more
    freely than a line shot.

    Y comes from the set's floor rather than from the fit: the convergence
    point is a head, and standing the model on the floor under it is both
    more accurate and self-correcting on sets with steps.

    **Except where a scene clip's root places the speaker.**  That key is the
    `*Bassin` bone's world position and its height is authored, so it is kept
    and `<who>Anchor` says "pelvis" instead of "floor".  Floor-probing it is
    wrong twice: it throws a measured height away, and beside furniture the
    ray hits the table rather than the ground - in AResto14 the surface under
    Telis reads 15.9 where the player, 74 units away, stands on 31.7.  Kept
    honest by `verify.py: dialog staging`, which checks the two anchors put
    both speakers of a conversation on the same floor.
    """
    L = _camera_rays(conv, "line")
    R = _camera_rays(conv, "reply")
    if not L and not R: return None
    fitL, fitR = _converge(L), _converge(R)
    if not fitL and not fitR: return None

    npc    = (fitL or fitR)[0]
    player = (fitR or fitL)[0]
    geo = decor_geometry_cached(decor_name)
    if not geo: return None
    out = {"samples": [len(L), len(R)], "playerSource": "reply cameras",
           "npcSource": "line cameras" if fitL else "reply cameras"}

    # The script that launches the conversation usually teleports the player
    # first - actor.goto_address, immediately before dialog.start. That address
    # is authored rather than inferred, so it wins where it exists.
    authored = dialog_player_address(conv.get("id"))
    if authored:
        player = list(authored["pos"])
        out["playerSource"] = "ADDRESSES[%d] %r" % (authored["address"],
                                                    authored["name"] or "")
    # The strongest source of all: the scene clip's own root key 0 - the
    # engine (Anim_SnapRootToStart) copies it onto the node, so it IS the
    # staged position, exactly. It beats both the camera solve and the
    # address teleport (the walk simulation would land on it anyway).
    did = conv.get("id")
    anchored = {"npc": False, "player": False}
    coll_for_pick = decor_collision_cached(decor_name)
    _a = dialog_actor(did) if did is not None else None
    model_of_npc = (_a or {}).get("model")
    si = scene_idle(did) if did is not None else None
    if si and si.get("rootPos"):
        npc, w = _stage_root(si, model_of_npc, coll_for_pick)
        out["npcSource"] = "scene clip root (%s, %s)" % (si["clip"], w)
        out["npcRootKey0"] = si["rootPos"]
        out["npcRootPick"] = w
        anchored["npc"] = True
    pi = scene_idle(did, player=True) if did is not None else None
    if pi and pi.get("rootPos"):
        player, w = _stage_root(pi, "HO1_FNM", coll_for_pick)
        out["playerSource"] = "scene clip root (%s, %s)" % (pi["clip"], w)
        out["playerRootKey0"] = pi["rootPos"]
        out["playerRootPick"] = w
        anchored["player"] = True
    coll = coll_for_pick
    for key, p, fit in (("npc", npc, fitL or fitR),
                        ("player", player, fitR or fitL)):
        # the engine's walker probes collision geometry and only stands on
        # slopes under 30 degrees - the walkable soup mirrors that; the render
        # soup stays as the fallback for sets whose collision layer is thin
        fy = floor_under_walkable(coll, p)
        src = "walkable"
        if fy is None:
            fy = floor_under(geo, p); src = "render"
        # WHICH POINT OF THE BODY this is, carried on the placement so nothing
        # downstream has to guess - and so it cannot change with the pose.
        #
        #   "pelvis": a scene clip's root key 0.  It is the `*Bassin` bone's
        #     own world position, height included, so the height is kept and
        #     the body hangs from it.  Probing a floor here is wrong twice
        #     over: it discards a measured height, and beside furniture the
        #     ray hits the TABLE.  Under Telis in AResto14 the floor reads
        #     15.9 where the player, two units away, stands on 31.7.
        #   "floor": everything else - a camera-ray solve (which converges on
        #     a HEAD) or an `actor.goto_address` teleport (ADDRESSES 678 sits
        #     2 units above Aapkayl's floor).  Both name a spot on the ground,
        #     so the floor under it is better than the point itself.
        if anchored[key]:
            out[key] = [p[0], p[1], p[2]]
            out[key + "Anchor"] = "pelvis"
        else:
            out[key] = [p[0], fy if fy is not None else p[1], p[2]]
            out[key + "Anchor"] = "floor"
        out[key + "Floor"] = fy is not None
        out[key + "FloorSource"] = src if fy is not None else None
        out[key + "FloorY"] = round(fy, 1) if fy is not None else None
        # the room's own ground, which is a different question once a speaker
        # is SEATED: the nearest surface below a seated pelvis is the stool
        gy = ground_under(coll, p)
        out[key + "GroundY"] = round(gy, 1) if gy is not None else None
        out[key + "Target"] = p
        # how tightly that speaker's rays actually met - the viewer shows it so
        # a loose fit is not mistaken for a measurement
        out[key + "Scatter"] = round(fit[1], 1) if fit else None
        out[key + "Rays"]    = fit[2] if fit else 0
    if authored:
        out["playerScatter"] = None
        out["playerRays"] = 0
    return out


def _stage_root(idle, model, coll):
    r"""Which root position stages a scene-clip speaker -> (pos, "key 0"|"settled").

    **This is a viewer HEURISTIC, not the engine's rule, and it is labelled as
    one because the rule is not known.**  See CLAUDE.md 6.

    What is established: key 0 is the authored placement
    (`Anim_SnapRootToStart` copies it onto the node) and keys 1.. are per-frame
    deltas `Anim_RootDelta` sums.  What is NOT established is which of those a
    dialogue's staging should show, and the corpus says plainly that neither
    answers everywhere - across the 50 staged bodies, summing the deltas puts
    the feet closer to the ground for **11** of them and further for **7**,
    with the medians almost equal (2.5 against 3.0).

    Both extremes were play-tested and both were wrong:

      * key 0 alone leaves dialog **387**'s pair standing-height over a seated
        pose, floating 16-18 units with the tabletop at their thighs;
      * the summed deltas sink dialog **401**'s Telis 26 units through the
        floor of the same apartment, and `M_DEAD` 37.

    And the two clips are structurally identical - each puts its entire root
    motion in key 1 and zero in every key after - so nothing in the track's
    shape tells them apart.

    So this picks whichever lands the posed body's feet nearer the ground
    beneath it, which gets both confirmed cases right and is honest about
    being a fit rather than a reading.  It is exactly the kind of rule CLAUDE
    .md 1 warns about - a hypothesis that matches the shipped bytes looks the
    same as a right one - so it stays flagged in `<who>RootPick`, the viewer
    prints which was used, and `verify.py: dialog staging` asserts the split
    rather than the outcome.  Do not promote it to a finding without a loader
    that says so.
    """
    k0 = idle.get("rootPos")
    st = idle.get("rootSettled")
    if not st or st == k0: return list(k0), "key 0"
    if not model or not coll: return list(st), "settled"
    try:
        best, pick = None, "key 0"
        for cand, name in ((k0, "key 0"), (st, "settled")):
            feet = _posed_feet(model, idle["scx"], idle["anim"], cand[1])
            g = ground_under(coll, cand)
            if g is None or feet is None: continue
            d = abs(g - feet)
            if best is None or d < best: best, pick = d, name
        return list(k0 if pick == "key 0" else st), pick
    except Exception:
        return list(st), "settled"


def _posed_feet(model, scx, anim, worldY, frame=1):
    """The lowest posed vertex, in game coordinates, with the pelvis at worldY."""
    import struct as _s
    g = model_geometry(model)
    if not g or not g.get("pelvis"): return None
    meta, buf = ani_pose_stream(model, scx, anim)
    n = meta["meshes"]; f = min(frame, meta["frames"] - 1)
    P = _s.unpack_from("<%df" % (n * 7), buf, f * n * 7 * 4)
    lo = -1e9
    for i, L in enumerate(g["staticLocal"]):
        mi = g["staticMesh"][i]
        if L is None or mi is None or mi < 0: continue
        b = mi * 7
        w, x, y, z = P[b:b + 4]
        ty = 2 * (z * L[0] - x * L[2])
        py = L[1] + w * ty + (z * 2 * (y * L[2] - z * L[1])
                              - x * 2 * (x * L[1] - y * L[0])) + P[b + 5]
        lo = max(lo, py)
    return lo + worldY - g["pelvis"][1]


def decor_geometry(name):
    """A whole set, baked to world space and grouped by material.

    Sets are static, so unlike a character there is nothing to skin or animate
    and the vertices can be baked once.  Two differences from `model_geometry`:

      * Sets are quad-heavy - Aapkayl has 1236 quads against 1023 triangles -
        so the quad block has to be read too.  A quad record is 32 bytes:
        4 int16 indices, 8 UV bytes, then the material int, mirroring the
        28-byte triangle record.  The four indices are in perimeter order, so
        they split into (0,1,2) and (0,2,3).
      * The per-vertex light actually varies here (it is a flat white on
        characters), so it is kept and used to shade the set - and it is a
        COLOUR, not a brightness; see `shade` below.

    Mesh positions are absolute, exactly as in a character model.
    """
    p = decor_path(name)
    if not os.path.exists(p): return None
    h, ms = mesh3do.meshes(p)
    d = open(p, "rb").read()
    byid = {m["id"]: m for m in ms}
    # the head family, for the look-at turn: the neck pivots them
    headMeshes = [m["i"] for m in ms
                  if any(k in m["name"].lower()
                         for k in ("tete", "visage", "cou", "cheveu"))]
    headPivot = next((m["i"] for m in ms if "cou" in m["name"].lower()),
                     headMeshes[0] if headMeshes else -1)
    basev, av = {}, 0
    baset, at = {}, 0
    baseq, aq = {}, 0
    for m in ms:
        basev[m["i"]] = av; baset[m["i"]] = at; baseq[m["i"]] = aq
        av += m["vertices"]; at += m["triangles"]; aq += m["quads"]

    # Mesh flag 0x8000000: the vertex colour SHIMMERS. sub_4947F0 adds a signed
    # value from the engine's own 32-entry table to all three channels and
    # clamps, with the phase taken from `(clock >> 2) + (vertexAddress >> 4)`.
    # The vertices are a contiguous 32-byte stride, so `addr >> 4` steps by 2
    # per vertex - which is reproducible as `2 * vertexIndex` even though the
    # address is not. 233 set meshes carry it; see docs/ASSETS.md 4.
    FLICKER = 0x8000000

    def shade(gi):
        """The baked per-vertex light, as (r, g, b) in 0..1.

        The reference importer renders `Color.white * lighting.g` and this
        followed it, taking byte +29 as a brightness. The engine says it is a
        whole colour: the transform pass (`sub_4947F0`) copies the DWORD at
        vertex +28 into the D3DTLVERTEX colour, clamped between scene+416 and
        scene+420, and the software rasterizer's grey fallback then weights the
        three bytes

            (587 * v[69] + 299 * v[70] + 114 * v[68]) / 1000

        which are the standard luma coefficients - 0.299 R, 0.587 G, 0.114 B.
        That names the channels without any judgement call: +28 is BLUE, +29
        GREEN, +30 RED, i.e. a D3D 0xAARRGGBB dword, and byte +31 the alpha
        (0 in every ANEKBAH vertex).

        It matters: 157562 of the 405537 vertices in the shipped sets - 38.9% -
        have r, g and b that are not all equal, so reading the green byte as a
        grey level renders every set in monochrome. Anekbah's street lighting
        is the obvious loser. See docs/ASSETS.md 4c."""
        o = h["vtxOff"] + 32 * gi
        return (d[o + 30] / 255.0, d[o + 29] / 255.0, d[o + 28] / 255.0)

    def vertex(gi, off):
        v = struct.unpack_from("<3f", d, h["vtxOff"] + 32 * gi)
        return (v[0] + off[0], v[1] + off[1], v[2] + off[2])

    def ancestor_for(m, k):
        cur, guard = byid.get(m["parent"]), 0
        while cur is not None and guard < 16:
            if cur["vertices"] > 3 and k < cur["vertices"]: return cur
            cur = byid.get(cur["parent"]); guard += 1
        return None

    def resolve(m, i, off):
        """-> (globalVertex, meshOffset) or None."""
        if i < 0:
            anc = ancestor_for(m, i & 0x7FFF)
            if anc is None: return None
            gi = basev[anc["i"]] + (i & 0x7FFF)
            return (gi, anc["pos"])
        gi = basev[m["i"]] + i
        return None if gi >= h["vertices"] else (gi, off)

    def drawable(m):
        """Only CollisionOnly is dropped from a set.

        Note this deliberately differs from `model_geometry`, which also drops
        every mesh with no flags at all.  That rule was derived from PERSOS,
        where such meshes are aim/shoot markers - on a set they are ordinary
        scenery.  In Aapkayl the 21 flagless meshes are APlit (the bed), APmur
        (a wall), the doors, the chests and the books: 830 of the set's 3495
        faces.  The CollisionOnly meshes really are volumes - introgrid and the
        APface* series, 76 faces.
        """
        return not (m["flags"] & 0x800000)

    groups, order, seq = {}, {}, 0
    for m in ms:
        if not drawable(m): continue
        off = m["pos"]
        cut = bool(m["flags"] & 0x800)
        # The blend MODE, not a flag. Reading the render bucket key settles it
        # (ASSETS 4b/4c): mesh 0x1000 -> key 0x2000 turns ZWRITEENABLE off and
        # ALPHABLENDENABLE on, and the sub-mode then comes from mesh 0x2000 ->
        # key 0x100 = SRCBLEND/DESTBLEND ONE/ONE (additive) or mesh 0x4000 ->
        # key 0x200 = ZERO/INVSRCCOLOR (a darkening multiply).
        #
        # The shipped sets use exactly these two and nothing else: 211 meshes
        # additive, 6 multiply (`AB_mirror`, `mirroir`, `flam01/02`,
        # `scre01/02`), and no mesh carries 0x1000 without a sub-mode or a
        # sub-mode without 0x1000. The older reading here - "0x2000 means alpha
        # blend, draw it at 50%" - had the flag one place out; the
        # SetRenderState(27, 1) it pointed at is the CUTOUT path, from 0x800.
        f = m["flags"] & 0xFFFFFFFF
        blend = ("add" if (f & 0x1000 and f & 0x2000) else
                 "mul" if (f & 0x1000 and f & 0x4000) else "")
        # -1 marks a vertex that does not shimmer, which is nearly all of them
        phase = (lambda gi: float((2 * gi) % 32)) if (f & FLICKER) else \
                (lambda gi: -1.0)
        for t in range(baset[m["i"]], baset[m["i"]] + m["triangles"]):
            o = h["triOff"] + 28 * t
            idx = struct.unpack_from("<3h", d, o)
            uv  = struct.unpack_from("<6B", d, o + 6)
            mid = struct.unpack_from("<i", d, o + 12)[0]
            if mid < 0: continue
            corners = []
            for k in range(3):
                r = resolve(m, idx[k], off)
                if r is None: corners = None; break
                corners.append(vertex(*r) + (uv[k*2], uv[k*2+1]) + shade(r[0])
                               + (phase(r[0]),))
            if corners:
                k = (mid, cut, blend)
                if k not in order: order[k] = seq; seq += 1
                groups.setdefault(k, []).extend(corners)
        for q in range(baseq[m["i"]], baseq[m["i"]] + m["quads"]):
            o = h["quadOff"] + 32 * q
            idx = struct.unpack_from("<4h", d, o)
            uv  = struct.unpack_from("<8B", d, o + 8)
            mid = struct.unpack_from("<i", d, o + 16)[0]
            if mid < 0: continue
            corners = []
            for k in range(4):
                r = resolve(m, idx[k], off)
                if r is None: corners = None; break
                corners.append(vertex(*r) + (uv[k*2], uv[k*2+1]) + shade(r[0])
                               + (phase(r[0]),))
            if corners:
                k = (mid, cut, blend)
                if k not in order: order[k] = seq; seq += 1
                groups.setdefault(k, []).extend(
                    [corners[0], corners[1], corners[2],
                     corners[0], corners[2], corners[3]])

    try: txs = tex3dt.textures(p)
    except Exception: txs = []
    verts, batches = [], []
    # Batches by material id, transparent ones last so they composite over the
    # opaque geometry - and within those, additive before multiply, which is
    # the engine's own order: it walks 0x4000 buckets ASCENDING and the two
    # keys are 0x2100 and 0x2200 (ASSETS 4b).
    #
    # The material-id order is the engine's too, for a reason it took a while
    # to see. The bucket key's low six bits are the material's runtime TEXTURE
    # SLOT, handed out in material-record order within one load, so of two
    # exactly-coincident faces the lower material index is drawn first and wins
    # under a strict depth test. Ordering by first appearance in the .3DO was
    # tried instead and is REFUTED by what it draws - ANEKBAH's shop signs then
    # show `BATITR12` where the game shows `BATITR15`. "Material order happens
    # to be right" was never an accident of numbering. See ASSETS 4b.
    for key in sorted(groups, key=lambda k: (k[2], k[0], k[1])):
        mid, cut, blend = key
        batches.append({"material": mid, "cutout": cut, "blend": blend,
                        "start": len(verts),
                        "count": len(groups[key]),
                        "name": txs[mid]["name"] if 0 <= mid < len(txs) else "",
                        "w": txs[mid]["w"] if 0 <= mid < len(txs) else 256,
                        "h": txs[mid]["h"] if 0 <= mid < len(txs) else 256})
        verts.extend(groups[key])

    lo = [min(v[i] for v in verts) for i in range(3)] if verts else [0, 0, 0]
    hi = [max(v[i] for v in verts) for i in range(3)] if verts else [0, 0, 0]
    cams = mesh3do.cameras(p)
    return {"name": os.path.basename(p)[:-4], "triangles": len(verts) // 3,
            "batches": batches, "verts": verts, "min": lo, "max": hi,
            "cameras": cams}


# --------------------------------------------------------------- animation
def _qmul(a, b):
    aw, ax, ay, az = a; bw, bx, by, bz = b
    return (aw*bw - ax*bx - ay*by - az*bz,
            aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw)

def _qrot(q, v):
    w, x, y, z = q
    tx, ty, tz = 2*(y*v[2]-z*v[1]), 2*(z*v[0]-x*v[2]), 2*(x*v[1]-y*v[0])
    return (v[0] + w*tx + (y*tz - z*ty),
            v[1] + w*ty + (z*tx - x*tz),
            v[2] + w*tz + (x*ty - y*tx))

def node_tracks(asset, root_track):
    """Per-frame bone rotations from a .3DM, plus the root's translation.

    Read straight off the demuxer, sub_42D960 (0x0042D960), which per frame does

        for i in 0 .. trackCount-1:
            if i == rootTrack:  read 12 bytes -> root translation
            read 16 bytes -> quaternion[i]
        read 24 * vertexCount bytes -> vertices

    so there are `nodeCount` quaternions, not `nodeCount - 2`. The 12-byte
    block is not a header at the start of the record: it belongs to the root
    track and sits wherever that track falls, which is why scanning for a
    single aligned quaternion array missed the two tracks before it.

    The root translation is a per-frame DELTA - the player accumulates it
    (`f32(v4, 212) += ...`).

    The file's preamble is the track table: `nodeCount` uint32 ids, which are
    mesh indices in .3DO file order.
    """
    p = morph_path(asset)
    if not p: return None
    L = morph3dm.layout(p)
    d = open(p, "rb").read()
    n = L["nodes"]
    ids = list(struct.unpack_from("<%dI" % n, d, 16))
    offs, o = [], 0
    for i in range(n):
        if i == root_track: o += 12
        offs.append(o); o += 16
    quats, trans = [], []
    acc = [0.0, 0.0, 0.0]
    for f in range(L["frames"]):
        base = L["preamble"] + f * L["record"]
        quats.append([struct.unpack_from("<4f", d, base + x) for x in offs])
        if 0 <= root_track < n:
            dt = struct.unpack_from("<3f", d, base + offs[root_track] - 12)
            acc = [acc[k] + dt[k] for k in range(3)]
        trans.append(tuple(acc))
    return {"frames": L["frames"], "count": n, "ids": ids,
            "quats": quats, "trans": trans, "rootTrack": root_track}

def node_quaternions(asset, root_track=0):
    """Back-compatible wrapper."""
    return node_tracks(asset, root_track)

def hierarchy_order(ms):
    """Meshes in depth-first skeleton order, following child then next.

    This is the order the .3DM rotations come in - NOT the order they are
    stored in the file. Taking file order puts rotations on the wrong bones:
    mean drift from the rest pose is 29-33 units, against 3-8 for hierarchy
    order. For TEL_FNM the walk gives pelvis, right leg, left leg, belly,
    torso, neck, head, face, then each shoulder and arm chain.
    """
    byid = {m["id"]: m for m in ms}
    order, seen = [], set()
    def walk(m):
        while m is not None and m["i"] not in seen:
            seen.add(m["i"]); order.append(m)
            walk(byid.get(m["child"]))
            m = byid.get(m["next"])
    for r in [m for m in ms if byid.get(m["parent"]) is None]:
        walk(r)
    for m in ms:                       # anything the links miss
        if m["i"] not in seen: order.append(m); seen.add(m["i"])
    return order

# The bone order the engine itself uses. Morph_Play's setup (0x0041A9xx)
# resolves each mesh BY NAME into fixed slots 3..19 of the actor record, and
# those 17 slots, in slot order, are exactly this list. Note what is absent:
# Bassin (the root), Cou, and Epauled - and note that Epauleg and Ventre ARE
# present even though they are attachment markers. 519 of the 708 shipped
# morphs carry exactly 17 quaternions, which is this list's length.
BONE_ORDER = ["Visage", "Tete", "Buste", "Epauleg", "Brasd", "Brasg",
              "Avantd", "Avantg", "Maing", "Maind", "Cuisseg", "Cuissed",
              "Jambeg", "Jambed", "Piedg", "Piedd", "Ventre"]

def named_bones(ms):
    """-> the meshes for BONE_ORDER, by name suffix; None where absent.
    Mesh names carry a per-character prefix (TeVisage, BoVisage, ...)."""
    found = {}
    for m in ms:
        low = m["name"].lower()
        for b in BONE_ORDER:
            if low.endswith(b.lower()): found.setdefault(b, m)
    return [found.get(b) for b in BONE_ORDER]

def animated_meshes(ms):
    """The meshes a .3DM's rotations apply to, in the order they arrive:
    hierarchy order, minus the three attachment markers. That count is exactly
    `meshCount - 3`, which is how many quaternions a frame carries."""
    return [m for m in hierarchy_order(ms)
            if not ((m["flags"] & 1) and m["vertices"] <= 3 and m["triangles"] <= 1)]

def _compose(ms, qof, upright=False):
    """rot[m] = rot[parent] * q[m]; pos[m] = pos[parent] + rot[parent]*(rest offset)"""
    byid = {m["id"]: m for m in ms}
    out = {}
    def solve(m, guard=0):
        if m["i"] in out: return out[m["i"]]
        q = qof.get(m["i"], (1.0, 0.0, 0.0, 0.0))
        par = byid.get(m["parent"])
        if par is None or guard > 24:
            res = (q, tuple(m["pos"]))
        else:
            pq, pp = solve(par, guard + 1)
            off = tuple(m["pos"][k] - par["pos"][k] for k in range(3))
            res = (_qmul(pq, q), tuple(pp[k] + _qrot(pq, off)[k] for k in range(3)))
        out[m["i"]] = res
        return res
    for m in ms: solve(m)
    if upright:
        root = next((m for m in ms if byid.get(m["parent"]) is None), None)
        if root is not None and root["i"] in out:
            rq = out[root["i"]][0]
            inv = (rq[0], -rq[1], -rq[2], -rq[3])
            origin = out[root["i"]][1]
            for i, (q, pos) in list(out.items()):
                rel = tuple(pos[k] - origin[k] for k in range(3))
                out[i] = (_qmul(inv, q),
                          tuple(_qrot(inv, rel)[k] + origin[k] for k in range(3)))
    return out

def pose(model, tracks, frame, upright=True):
    """Compose one frame's rotations down the mesh hierarchy.

    Track i drives the mesh whose index is `tracks["ids"][i]` - the preamble is
    a plain 0,1,2,... in every shipped file, so that is .3DO file order. The
    last mesh (the face) has no track; its vertices are animated instead.

    Rest positions are absolute, so only the position accumulates:

        rot[m] = rot[parent] * q[m]
        pos[m] = pos[parent] + rot[parent] * (rest[m] - rest[parent])

    -> {mesh index: (quaternion, position)}
    """
    p = os.path.join(PERSOS, model + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(PERSOS):
            if fn.lower() == (model + ".3do").lower():
                p = os.path.join(PERSOS, fn); break
    h, ms = mesh3do.meshes(p)
    byid = {m["id"]: m for m in ms}
    quats = tracks["quats"][max(0, min(tracks["frames"] - 1, frame))]
    qof = {}
    for i, mi in enumerate(tracks["ids"]):
        if i < len(quats) and 0 <= mi < len(ms):
            # The stored quaternion is the CONJUGATE of the rotation to
            # apply - negate the vector part. Without this the torso and head
            # still look right (the root's rotation is cancelled anyway) but
            # every limb bends the wrong way: arms swing backwards and knees
            # invert. With it, characters sit, stand and gesture naturally.
            q = quats[i]
            qof[mi] = (q[0], -q[1], -q[2], -q[3])
    out = _compose(ms, qof, upright=False)

    if upright:
        root = next((m for m in ms if byid.get(m["parent"]) is None), None)
        if root is not None and root["i"] in out:
            rq = out[root["i"]][0]
            inv = (rq[0], -rq[1], -rq[2], -rq[3])
            origin = out[root["i"]][1]
            for i, (q, pos) in list(out.items()):
                rel = tuple(pos[k] - origin[k] for k in range(3))
                out[i] = (_qmul(inv, q),
                          tuple(_qrot(inv, rel)[k] + origin[k] for k in range(3)))
    return out

def root_track_of(model, asset):
    """Which track carries the root - the one whose mesh has no parent."""
    p = os.path.join(PERSOS, model + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(PERSOS):
            if fn.lower() == (model + ".3do").lower():
                p = os.path.join(PERSOS, fn); break
    h, ms = mesh3do.meshes(p)
    byid = {m["id"]: m for m in ms}
    root = next((m for m in ms if byid.get(m["parent"]) is None), None)
    return root["i"] if root else 0

def pose_stream(model, asset):
    """Every frame's skeleton pose for a model/animation pair, flattened for
    the browser: meshCount entries of (quat w,x,y,z + position x,y,z) per frame,
    indexed by the mesh index the geometry's corners carry."""
    nq = node_tracks(asset, root_track_of(model, asset))
    if not nq or not nq["count"]: return None, None
    p = os.path.join(PERSOS, model + ".3DO")
    if not os.path.exists(p):
        for fn in os.listdir(PERSOS):
            if fn.lower() == (model + ".3do").lower():
                p = os.path.join(PERSOS, fn); break
    h, ms = mesh3do.meshes(p)
    n = len(ms)
    buf = bytearray()
    for f in range(nq["frames"]):
        P = pose(model, nq, f)
        for m in ms:
            q, pos = P.get(m["i"], ((1.0, 0.0, 0.0, 0.0), tuple(m["pos"])))
            buf += struct.pack("<7f", q[0], q[1], q[2], q[3], pos[0], pos[1], pos[2])
    return {"frames": nq["frames"], "meshes": n, "stride": 7}, bytes(buf)


# ------------------------------------------------------- body animation (.ani)
ANIMS = omkpaths.data("ANIMS")

def _bone_suffix(name):
    """`ChBassin` -> `bassin`; the prefix is per character set."""
    for k in (2, 3, 1, 4):
        if len(name) > k and name[k:k+1].isupper():
            return name[k:].lower()
    return name.lower()

_ANI_CACHE = {}
def ani_file(fn):
    if fn not in _ANI_CACHE:
        import anim_ani
        d, clips = anim_ani.load(os.path.join(ANIMS, fn))
        out = []
        for c in clips:
            desc = anim_ani.descriptor(d, c["desc"])
            if desc: out.append({**c, "desc_parsed": desc})
        _ANI_CACHE[fn] = (d, out)
    return _ANI_CACHE[fn]

def list_anims():
    """Every clip in gamedata/ANIMS - both the .ani libraries and the .CTL state
    tables, which carry 398 more of them in the same descriptor format."""
    out = []
    import anim_ctl, anim_ani
    for fn in sorted(os.listdir(ANIMS)):
        if not fn.lower().endswith(".ctl"): continue
        try: w = anim_ctl.walk(os.path.join(ANIMS, fn))
        except Exception: continue
        for i, c in enumerate(w["clips"]):
            dd = anim_ani.descriptor(w["data"], c["offset"])
            if not dd: continue
            out.append({"file": fn, "clip": i, "name": c["name"],
                        "frames": dd["frames"], "bones": dd["bones"],
                        "suffixes": [_bone_suffix(t["name"]) for t in dd["tracks"]]})
    for fn in sorted(os.listdir(ANIMS)):
        if not fn.lower().endswith(".ani"): continue
        try: d, clips = ani_file(fn)
        except Exception: continue
        for i, c in enumerate(clips):
            dd = c["desc_parsed"]
            out.append({"file": fn, "clip": i,
                        "name": c["name"] or f"clip{i}",
                        "type": c.get("type"),
                        "frames": dd["frames"], "bones": dd["bones"],
                        "suffixes": [_bone_suffix(t["name"]) for t in dd["tracks"]]})
    return out

_ACTORS = None

def _actor_tables():
    r"""Every actor record in IAM\AREA and IAM\SCENE, by chunk and actor id.

    sub_40B190 resolves an actor id by scanning two 276-byte arrays - AREA's at
    +56 (count int16 at +80) and SCENE's at +24 (count int16 at +48) - matching
    the id at +272. Each record names the character's model at +144 and its
    .CTL animation state machine at +72.
    """
    global _ACTORS
    if _ACTORS is None:
        import dialog_triggers as T
        _ACTORS = {}
        for name, p_at, c_at in (("AREA", 56, 80), ("SCENE", 24, 48)):
            per = {}
            for k, b in sorted(T.archive(os.path.join(TAGDIR, name)).items()):
                if len(b) < c_at + 2: continue
                lo = struct.unpack_from("<i", b, p_at)[0]
                n  = struct.unpack_from("<h", b, c_at)[0]
                if n <= 0 or lo <= 0 or lo + 276 * n > len(b): continue
                d = {}
                for i in range(n):
                    o = lo + 276 * i
                    # +108 is the Sexe field Actor_GetProperty exposes as
                    # property 0: a single ASCII byte, 77 'M' or 70 'F'
                    # (Telis/Jenna/the nurse are F, Den/Matanboukous M).
                    sx = b[o + 108]
                    d[struct.unpack_from("<h", b, o + 272)[0]] = {
                        "model": b[o+144:o+164].split(b"\0")[0].decode("cp1252", "replace"),
                        "ctl":   b[o+72:o+92].split(b"\0")[0].decode("cp1252", "replace"),
                        "sexe":  chr(sx) if sx in (70, 77) else ""}
                if d: per[k] = d
            _ACTORS[name] = per
    return _ACTORS


_SCENE_AREA = None
def _scene_area():
    """{scene id: area id} from the scene.load (op 71) sites."""
    global _SCENE_AREA
    if _SCENE_AREA is None:
        import dialog_triggers as T, dialog_disasm as D
        out = {}
        for name in ("AREA", "SCENE"):
            for k, b in sorted(T.archive(os.path.join(TAGDIR, name)).items()):
                r = T.LAYOUT[name](b)
                if not r: continue
                for rec, f, pp in (list(T._scripts_from_records(b, r[0], r[1]))
                                   + T._second_table(name, b)):
                    ops, st = D.disasm(b, pp, len(b))
                    if st != "ok": continue
                    for pc, op, raw in ops:
                        if op == 71:
                            a, sc = struct.unpack("<2h", raw)
                            out.setdefault(sc, a)
        _SCENE_AREA = out
    return _SCENE_AREA


def _area_scx(area):
    """AREA id -> its SCPTDATA file, via the base name at +97."""
    import dialog_triggers as T, glob
    ab = T.archive(os.path.join(TAGDIR, "AREA")).get(area)
    if not ab or len(ab) < 118 or not ab[97]: return None
    base = ab[97:118].split(b"\0")[0].decode("cp1252", "replace")
    return next((f for f in glob.glob(omkpaths.data("SCPTDATA/*"))
                 if os.path.basename(f).upper() == (base + ".SCX").upper()), None)


def scene_idle(dialog_id, player=False):
    r"""The contextual idle the scene gives the speaker - resolved to a clip.

    Two mechanisms, both established on real dialogs:

    * 402: the launch script's last `scx.play.actor` before `dialog.start` is
      a loop object (306, `A_3_TelisStand`) whose `Script_SelectBodyAnimation`
      names the target node in the object's table (`TeBassin` - the pelvis;
      the names are BONES) and picks the clip by **position** in the scene's
      animation array: index 36 = `TE_STD.3DA`, on Telis's own `Te*` rig.
      (By *id* it would be `VIRTUAL.3DA`, on a `Vi*` rig - wrong skeleton,
      which is what settles position over id.)
    * 387: the launch script never touches the actor; the pose belongs to a
      scene object (`Telis_eat` -> `TELRES02/05.3DA`) started elsewhere. The
      fallback finds any object whose SelectBodyAnimation targets the
      speaker's bone prefix.

    -> {clip, object, scx, anim} where anim indexes the SCX's animation array
    (playable through ani_pose_stream(model, "<scx>", anim)).
    """
    import dialog_triggers as T, dialog_disasm as D, scene_scx as S
    import anim_3da, re
    sites = _dialog_triggers().get(dialog_id)
    if not sites: return None
    arch, chunk, rec, field = sites[0]
    try:
        b = T.archive(os.path.join(TAGDIR, arch))[chunk]
        r = T.LAYOUT[arch](b)
        slots = list(T._scripts_from_records(b, r[0], r[1])) + T._second_table(arch, b)
    except Exception:
        return None

    area = chunk if arch == "AREA" else _scene_area().get(chunk)
    if area is None:
        a = dialog_actor(dialog_id)
        m = a and re.match(r"AREA (\d+)", a.get("from", ""))
        if m: area = int(m.group(1))
    path = _area_scx(area) if area is not None else None
    if not path: return None
    try:
        objs = S.scene(path)["objects"]
        st = anim_3da.scx_stream(path)
    except Exception:
        return None

    def resolve(obj):
        idxs = []
        for f in obj["functions"]:
            if f["id"] != 0x02000004 or len(f["params"]) < 2: continue
            i = f["params"][1]
            if 0 <= i < len(st["anims"]) and i not in idxs: idxs.append(i)
        if not idxs: return None
        # the clip's root yaw is the character's facing for the WHOLE
        # conversation - the game keeps the scene orientation during spoken
        # lines too (compared frame-by-frame against a longplay of 387)
        yaw, rootPos, rootSettled = None, None, None
        a0 = st["anims"][idxs[0]]
        r0 = anim_3da.descriptor(st["data"], a0["offset"], a0["declared"])
        if r0 and r0["tracks"] and "bassin" in r0["tracks"][0]["name"].lower():
            t0 = r0["tracks"][0]
            if t0["rotOffset"] and t0["rotKeys"] > 1:
                w, x, y, z = anim_3da.rotations(st["data"], a0["offset"], t0)[1]
                yaw = round(math.degrees(2 * math.atan2(y, w)), 1)
            # key 0 of the root position track is the AUTHORED PLACEMENT in
            # scene coordinates - the engine's Anim_SnapRootToStart copies
            # exactly that key onto the node (740 of 874 scene clips carry a
            # world-scale one; TELRES05 lands 2 units from the camera solve,
            # HO14_01R exposes the solve as 41 units off)
            if t0["posOffset"] and t0["posKeys"]:
                P = anim_3da.positions(st["data"], a0["offset"], t0)
                p0 = P[0]
                if sum(v * v for v in p0) > 500 * 500:
                    rootPos = [round(v, 1) for v in p0]
                    # Key 0 is where the character is placed; keys 1.. are the
                    # animation, as per-frame DELTAS that Anim_RootDelta sums
                    # onto the node.  For staging, key 0 alone is only right
                    # when the clip does not move - and a **sit-down** clip
                    # moves a long way.  HO14_01R drops its root 17.3 units in
                    # one key and then holds: key 0 puts Kay'l's pelvis 41.7
                    # above the restaurant floor, which is exactly HO1_FNM's
                    # standing pelvis height (41.8), and the integral puts it
                    # 24.4 above - 8.6 above the seat he sits on.  A real
                    # screenshot of the lunch shows him ON the stool, so the
                    # settled value is the staged one.
                    #
                    # 402's two clips move 1.0 and 2.0 units, which is why key
                    # 0 looked right there and why that conversation could
                    # never have caught this.
                    cur = list(p0)
                    for d in P[1:]:
                        cur = [cur[i] + d[i] for i in range(3)]
                    rootSettled = [round(v, 1) for v in cur]
        # the game alternates the object's clips (Telis_eat: sitting and
        # eating), so the whole list rides along for the viewer to cycle
        return {"clip": a0["name"], "object": obj["name"],
                "scx": os.path.basename(path), "anim": idxs[0],
                "rootYaw": yaw, "rootPos": rootPos,
                "rootSettled": rootSettled,
                "anims": [{"anim": i, "clip": st["anims"][i]["name"]}
                          for i in idxs]}

    # 1. the launching script's own loop object
    # the player's own loop object comes from ops 46/90 (scx.play.player),
    # the speaker's from 59/60 - dialog 387: `Uzal_Stand` -> HO14_01R.3DA,
    # Kay'l's seated loop, exactly parallel to Telis's
    off = next((pp for rc, f, pp in slots if rc == rec and f == field), None)
    if off is not None:
        ops, stx = D.disasm(b, off, len(b))
        last, held = None, False
        if stx == "ok":
            for pc, op, raw in ops:
                if op == 104:                       # player.anim.hold
                    held = True
                if not player and op in (59, 60) and len(raw) >= 4:
                    last = struct.unpack_from("<2h", raw)
                elif player and op in (46, 90) and len(raw) >= 2:
                    last = (0, struct.unpack_from("<h", raw)[0])
                elif op == 61 and struct.unpack_from("<h", raw)[0] == dialog_id:
                    break
        # the engine's rule (FILE_FORMATS 5b2): a scene clip owns the
        # player's body only under an authored player.anim.hold - without
        # it the dialogue stance would override the clip
        if player and last and not held:
            last = None
        if last:
            o = next((o for o in objs if (o["handle"] >> 16) == last[1]), None)
            got = o and resolve(o)
            if got: return got

    if player: return None
    # 2. any scene object animating the speaker's own rig
    act = dialog_actor(dialog_id)
    prefix = None
    if act and act.get("model"):
        try:
            import mesh3do
            pmod = os.path.join(PERSOS, act["model"] + ".3DO")
            if not os.path.exists(pmod):
                for f2 in os.listdir(PERSOS):
                    if f2.lower() == (act["model"] + ".3do").lower():
                        pmod = os.path.join(PERSOS, f2); break
            _, ms = mesh3do.meshes(pmod)
            root = next((m["name"] for m in ms if "bassin" in m["name"].lower()), None)
            if root: prefix = root[:root.lower().index("bassin")]
        except Exception:
            pass
    if prefix:
        for o in objs:
            for f in o["functions"]:
                if f["id"] != 0x02000004 or not f["params"]: continue
                t = o["tables"][0] if o["tables"] else []
                k = f["params"][0]
                if 0 <= k < len(t) and t[k].startswith(prefix):
                    got = resolve(o)
                    if got: return got
    return None


def dialog_lookat(dialog_id):
    """Does the launch script leave the speaker looking at the player?

    op 138 `character.look_at_player` sets the actor's +400 look-at field and
    the renderer turns the head toward the other speaker; 139 clears it. The
    last of the two on the speaker's id before `dialog.start` decides.
    """
    import dialog_triggers as T, dialog_disasm as D
    sites = _dialog_triggers().get(dialog_id)
    if not sites: return False
    arch, chunk, rec, field = sites[0]
    try:
        b = T.archive(os.path.join(TAGDIR, arch))[chunk]
        r = T.LAYOUT[arch](b)
        slots = list(T._scripts_from_records(b, r[0], r[1])) + T._second_table(arch, b)
    except Exception:
        return False
    off = next((pp for rc, f, pp in slots if rc == rec and f == field), None)
    if off is None: return False
    ops, st = D.disasm(b, off, len(b))
    if st != "ok": return False
    state = False
    for pc, op, raw in ops:
        if op in (138, 139) and raw:
            state = (op == 138)
        elif op == 61 and struct.unpack_from("<h", raw)[0] == dialog_id:
            return state
    return False


def dialog_actor(dialog_id):
    """Which character speaks a conversation: {model, ctl, actor, from}.

    The chain, all of it from the loaders:

        the DIALOG chunk's header word 0 is the speaker's actor id
          -> the actor record with that id at +272, in the AREA or SCENE
             chunk whose script launches the conversation
          -> +144 is the model in MESHES/PERSOS, +72 the .CTL

    Checked against the shipped data: 1032/1032 actor records name a real
    model, and for 150 of the 153 conversations where both are known the
    model's face-vertex count is exactly what the line's .3DM supplies - two
    independent chains agreeing.
    """
    b = CHUNKS.get(dialog_id)
    if not b or len(b) < 2: return None
    speaker = struct.unpack_from("<h", b, 0)[0]
    tables = _actor_tables()

    # Prefer the chunk whose script actually launches this conversation - an
    # actor id can appear in several areas (the shop keepers are reused), and
    # the launching one is the one that means something here.
    for arch, chunk, _rec, _field in _dialog_triggers().get(dialog_id, []):
        d = tables.get(arch, {}).get(chunk)
        if d and speaker in d:
            r = dict(d[speaker]); r["actor"] = speaker
            r["from"] = "%s %d (launches it)" % (arch, chunk)
            return r
    for arch in ("AREA", "SCENE"):
        for chunk, d in tables[arch].items():
            if speaker in d:
                r = dict(d[speaker]); r["actor"] = speaker
                r["from"] = "%s %d" % (arch, chunk)
                return r
    return None


_TRIG = None
def _dialog_triggers():
    global _TRIG
    if _TRIG is None:
        import dialog_triggers as T
        _TRIG = T.triggers()[0]
    return _TRIG


def list_ctl():
    """The .CTL state machines available."""
    import anim_ctl
    out = []
    for fn in sorted(os.listdir(ANIMS)):
        if not fn.lower().endswith(".ctl"): continue
        try: w = anim_ctl.walk(os.path.join(ANIMS, fn))
        except Exception: continue
        out.append({"file": fn, "states": len(w["states"]),
                    "clips": len(w["clips"]), "groups": w["groups"]})
    return out


def ctl_graph(fn):
    """One .CTL as a graph: states, their clip, and the edges between them.

    Children and parents are the same edges stored both ways - all 931 child
    edges in the shipped files are listed back as a parent by their target - so
    the viewer only needs to follow one direction. A `goto` is the transition
    the state runs into when it finishes, and it is resolved across the whole
    file rather than within the group.
    """
    import anim_ctl, anim_ani
    w = anim_ctl.walk(os.path.join(ANIMS, fn))
    frames = {}
    for i, c in enumerate(w["clips"]):
        dd = anim_ani.descriptor(w["data"], c["offset"])
        frames[i] = {"frames": dd["frames"], "bones": dd["bones"]} if dd else None
    states = []
    for st in w["states"]:
        info = frames.get(st["clip"]) if st["clip"] is not None else None
        states.append({k: st[k] for k in
                       ("index", "group", "id", "name", "flags",
                        "parents", "children", "goto", "clip",
                        "turn", "shift", "mdname", "combat")}
                      | {"frames": info["frames"] if info else 0,
                         "bones":  info["bones"]  if info else 0})
    roots = [s["index"] for s in states if not s["parents"]]
    return {"file": w["file"], "groups": w["groups"], "states": states,
            "roots": roots}


def ani_pose_stream(model, fn, clip):
    """Every frame of a .ani clip posed onto a model, in the same flat layout
    as pose_stream: meshCount * (quat w,x,y,z + position x,y,z) per frame.

    Bones are matched by name suffix - `ChBassin` in the animation drives
    `TeBassin` in the model - which resolves 19 of a character's 20 meshes; the
    face is the one left out, and body animations do not drive it.
    """
    import anim_ani
    if fn.lower().endswith(".scx"):
        # An SCX-embedded .3DA clip - same 40-byte bone tracks and key
        # shapes, offsets payload-relative (tools/anim_3da.py). This is what
        # plays the contextual idles: TE_STD in the flat, TELRES* at the
        # restaurant table.
        import anim_3da
        st = anim_3da.scx_stream(omkpaths.data("SCPTDATA", fn))
        if not (0 <= clip < len(st["anims"])): return None, None
        a = st["anims"][clip]
        d = st["data"]
        r = anim_3da.descriptor(d, a["offset"], a["declared"])
        if not r: return None, None
        # Scene clips are self-orienting: the ROOT track carries the
        # character's authored facing as a constant yaw (TELRES05: 118.2°).
        # The rotation is stripped here and the yaw reported in the meta, so
        # the client can apply it through the game's own facing convention
        # (forward = (sin θ, −cos θ), the ADDRESSES rule) instead of pushing
        # a scene-space rotation through the model-space mirror.
        rootYaw = None
        tracks = []
        for ti, t in enumerate(r["tracks"]):
            rot = (a["offset"] + t["rotOffset"]) if t["rotOffset"] else 0
            if ti == 0 and "bassin" in t["name"].lower() and rot and t["rotKeys"] > 1:
                import anim_3da as _a3
                w, x, y, z = _a3.rotations(d, a["offset"], t)[1]
                rootYaw = round(math.degrees(2 * math.atan2(y, w)), 1)
                rot = 0                       # facing applied by the client
            tracks.append({"name": t["name"], "rotOff": rot,
                           "rotKeys": t["rotKeys"],
                           "posOff": (a["offset"] + t["posOffset"]) if t["posOffset"] else 0,
                           "posKeys": t["posKeys"]})
        desc = {"frames": r["frames"], "tracks": tracks}
        clipName = a["name"]
    elif fn.lower().endswith(".ctl"):
        # A .CTL clip is the same descriptor, just reached a different way.
        import anim_ctl
        w = anim_ctl.walk(os.path.join(ANIMS, fn))
        if not (0 <= clip < len(w["clips"])): return None, None
        d = w["data"]
        desc = anim_ani.descriptor(d, w["clips"][clip]["offset"])
        clipName = w["clips"][clip]["name"]
    else:
        d, clips = ani_file(fn)
        if not (0 <= clip < len(clips)): return None, None
        desc = clips[clip]["desc_parsed"]
        clipName = clips[clip]["name"] or f"clip{clip}"
    if not desc: return None, None
    p = os.path.join(PERSOS, model + ".3DO")
    if not os.path.exists(p):
        for f2 in os.listdir(PERSOS):
            if f2.lower() == (model + ".3do").lower():
                p = os.path.join(PERSOS, f2); break
    h, ms = mesh3do.meshes(p)
    bysuffix = {_bone_suffix(m["name"]): m for m in ms}
    tracks = []
    for t in desc["tracks"]:
        m = bysuffix.get(_bone_suffix(t["name"]))
        tracks.append((m, anim_ani.rotations(d, t), anim_ani.positions(d, t)))
    frames = max(1, desc["frames"])
    buf = bytearray()
    for f in range(frames):
        qof = {}
        for m, rot, _pos in tracks:
            if m is None or not rot: continue
            # Key 0 is a rest sentinel, not the first frame: a track carries
            # frames+1 rotation keys (all 5193 .ani tracks and 7795 of the
            # 8433 .CTL ones), and that first key is the identity quaternion in
            # 8416 of them. Reading it as frame 0 put a T-pose at the start of
            # every clip, which showed up as a one-frame flash on each loop.
            # Tracks with fewer keys than frames are sparse and still clamp.
            q = rot[min(f + 1, len(rot) - 1)]
            qof[m["i"]] = (q[0], -q[1], -q[2], -q[3])   # same conjugate as .3DM
        P = _compose(ms, qof)
        for m in ms:
            q, pos = P.get(m["i"], ((1.0, 0.0, 0.0, 0.0), tuple(m["pos"])))
            buf += struct.pack("<7f", q[0], q[1], q[2], q[3], pos[0], pos[1], pos[2])
    meta = {"frames": frames, "meshes": len(ms), "stride": 7, "name": clipName}
    try:
        if rootYaw is not None: meta["rootYaw"] = rootYaw
    except NameError:
        pass
    # The pose's own feet height (game axes, y down): a seated clip's feet sit
    # ~40 where a standing body's sit ~58, and lifting a seated pose by the
    # standing modelFeet floats it above its stool. +6 is the sole offset
    # below the foot mesh origin, measured on the standing pose (58 + 6 = 64,
    # the long-standing modelFeet constant).
    if frames > 1:
        base = 1 * len(ms) * 7
        vals = struct.unpack_from("<%df" % (len(ms) * 7), buf, base * 4)
        meta["feet"] = round(max(vals[i * 7 + 5] for i in range(len(ms))) + 6, 1)
    return meta, bytes(buf)

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The ambient effects of a set, resolved end to end, for the viewers.

A set's neon, smoke, fire and steam are not in the `.3DO`. They are emitters,
and finding one means crossing three files that were authored separately:

    the .3DO     a mesh flagged 0x40000000, and its position
       |         the first FOUR BYTES of its name, compared as a dword
    the .SFX     section D: [i32 effect id][char[4] tag][f32][f32]
       |         that row's id
    section C    the effect: sprite id, velocity, lifetime, cone, scale,
       |         rotation, blend mode, name
    the .SCX     chunk 4: the sprite, whose QUADS are its animation frames

Every link is read from the engine - `Sfx_BindAmbientEffects` does the name
compare, `Sfx_RegisterEmitter` fills a 100-slot table, `Sfx_TickAmbient`
emits, and `Render_SubmitSprites` draws. See docs/FILE_FORMATS.md 5b6 and
docs/ASSETS.md 3b.

    python3 tools/ambientfx.py                 # every set, one line each
    python3 tools/ambientfx.py ANEKBAH         # its emitters in detail

The emitter's **cadence is section D `+12`, a period in frames**:
`Sfx_BindAmbientEffects` compares it against `flt_4BC520` (`= 0.0`) and, when
it is greater, seeds the starting phase with `rand() % (int)period`. **120 of
the 151 shipped rows are 0** - a continuous emitter - and the rest run 1..55
frames. The clock counts FRAMES, not seconds: the engine's default frame delta
is `flt_4C30D8 dd 1.0`, so a section C lifetime of 15 is half a second.
"""
import omkpaths
import sys, os, struct, glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mesh3do, scene_scx, anim_3da, tex3dt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# section C, 80 bytes - docs/FILE_FORMATS.md 5b6. `drift` (+28) is an
# ACCELERATION on world Y: the integrator does `vel.y += it` once a frame.
# `c48`/`c52` are the particle's COLOUR at birth and at death, packed
# 0x00RRGGBB and taken high byte to low. Sfx_TickAmbient compares the two
# dwords and builds a `(c1-c0)/life` ramp only when they differ - which is what
# makes the intro's portal blue out of an ORANGE impact sprite, so a viewer
# that ignores them draws the wrong colour for every emitter in the game.
C_FIELDS = """id:0:i  sound:4:i  sprite:8:H  flags:12:I
              vx:16:f vy:20:f vz:24:f  drift:28:f  life:32:f
              c48:48:I c52:52:I
              scale:56:f cone:60:f spin:64:f  count:68:h""".split()

# The emitter flags at section C +12, read from Sfx_TickAmbient. Every one of
# these is a randomisation or a ramp; the engine has no other per-particle
# behaviour. 0x2 is used by NO shipped effect, so the alpha ramp never runs and
# every particle keeps the default 0.5.
FLAG_FADE   = 0x0002   # alpha ramps 1/life per frame  (unused: 0 rows)
FLAG_GROW   = 0x0004   # scale += scale/life per frame  (120 rows)
FLAG_ANGLE  = 0x0010   # random start angle, 0..360     (301 rows)
FLAG_JITDIR = 0x0040   # jitter the emission AXIS once per emitter, +[0,2)
                       # on each component - always positive, so the lean is
                       # consistent in world space          (73 rows)
FLAG_NCOUNT = 0x0080   # count += count * rand * 0.1    (26 rows)
FLAG_NLIFE  = 0x0100   # life  += life  * rand * 0.1    (47 rows)
FLAG_DRIFT  = 0x0200   # sign of the +28 ACCELERATION    (79 rows)
FLAG_NCONE  = 0x1000   # cone  += cone  * rand          (294 rows)
FLAG_SHRINK = 0x2000   # scale -= scale/life per frame  (121 rows)


def _sfx_path(stem):
    """The set's .SFX, whatever case the extension is written in.

    Eight of the 67 shipped files spell it `.Sfx` or `.SfX` - PAstarot,
    QTemple, SAppt, SArmu02, SBozOrdi, Ssuperm, hamesta, hospital - so trying
    only `.SFX` and `.sfx` silently finds nothing for those sets. Same trap as
    the `.3DM` sweep that reported 708 files where there are 777 (CLAUDE.md 4).
    """
    d = omkpaths.data("SCPTDATA")
    want = (stem + ".SFX").upper()
    try:
        for f in os.listdir(d):
            if f.upper() == want: return os.path.join(d, f)
    except OSError:
        pass
    return None


def sfx_effects(stem):
    """-> ({tag: effect}, [effect]) for one set's .SFX, or ({}, [])."""
    p = _sfx_path(stem)
    if not p: return {}, []
    d = open(p, "rb").read()
    if d[:4] != b"5.0V": return {}, []
    A = struct.unpack_from("<I", d, 4)[0]; o = 8 + 40 * A
    B = struct.unpack_from("<I", d, o)[0]; o += 4 + 44 * B
    C = struct.unpack_from("<I", d, o)[0]; cb = o + 4; o = cb + 80 * C
    D = struct.unpack_from("<I", d, o)[0]; db = o + 4

    rows = {}
    for i in range(C):
        r = d[cb + 80 * i:cb + 80 * i + 80]
        e = {}
        for f in C_FIELDS:
            nm, off, ty = f.split(":")
            e[nm] = struct.unpack_from("<" + ty, r, int(off))[0]
        e["speed"] = (e["vx"] ** 2 + e["vy"] ** 2 + e["vz"] ** 2) ** 0.5
        e["mode"] = r[78]
        e["name"] = r[70:78].split(b"\0")[0].decode("cp1252", "replace")
        rows[e["id"]] = e
    tags = {}
    for i in range(D):
        eid = struct.unpack_from("<i", d, db + 16 * i)[0]
        tag = d[db + 16 * i + 4:db + 16 * i + 8]
        if eid not in rows: continue
        e = dict(rows[eid])
        # +12 is the emitter's PERIOD in frames. Sfx_BindAmbientEffects tests it
        # against flt_4BC520 (= 0.0) and, when it is greater, seeds the phase
        # with `rand() % (int)period`; 0 means no phase and a continuous
        # emitter, which is 120 of the 151 shipped rows.
        e["period"] = struct.unpack_from("<f", d, db + 16 * i + 12)[0]
        e["range"] = struct.unpack_from("<f", d, db + 16 * i + 8)[0]
        tags[tag] = e
    return tags, list(rows.values())


def scx_sprites(stem):
    """-> {sprite id: {name, model, texture}} from the .SCX chunk 4 + stream."""
    for e in (".SCX", ".scx"):
        p = omkpaths.data("SCPTDATA", stem + e)
        if os.path.exists(p): break
    else:
        return {}
    try:
        sc = scene_scx.scene(p)
        st = anim_3da.scx_stream(p)
    except Exception:
        return {}
    recs = sc["chunks"].get(4, [])
    d = st["data"]
    out = {}
    for i, r in enumerate(recs):
        if i >= len(st["sprites"]): break
        sp = st["sprites"][i]
        o, m, t = sp["offset"], sp["model"], sp["texture"]
        out[struct.unpack_from("<H", r, 32)[0]] = {
            "name": r[:24].split(b"\0")[0].decode("cp1252", "replace"),
            "model": d[o:o + m], "texture": d[o + m:o + m + t]}
    return out


def sprite_frames(model):
    """-> (w, h, [[u0, v0, u1, v1]]) - one UV cell per quad, in 0..1."""
    h = mesh3do.header_bytes(model, len(model))
    out = []
    for q in range(h["quads"]):
        uv = struct.unpack_from("<8B", model, h["quadOff"] + 32 * q + 8)
        us, vs = uv[0::2], uv[1::2]
        out.append([min(us) / 255.0, min(vs) / 255.0,
                    max(us) / 255.0, max(vs) / 255.0])
    # the quad's own size in world units, from its diagonal corners
    o = h["quadOff"] + 8 if h["quads"] else 0
    size = [1.0, 1.0]
    if h["quads"]:
        idx = struct.unpack_from("<4h", model, h["quadOff"])
        a = struct.unpack_from("<3f", model, h["vtxOff"] + 32 * idx[0])
        c = struct.unpack_from("<3f", model, h["vtxOff"] + 32 * idx[2])
        size = [abs(c[0] - a[0]), abs(c[1] - a[1])]
    return size, out


def emitters(setname):
    """-> {"emitters": [...], "sprites": {...}} for one decor set."""
    stem = setname.upper()
    path = None
    for f in os.listdir(omkpaths.data("MESHES/DECORS")):
        if f.upper().rsplit(".", 1)[0] == stem and f.upper().endswith(".3DO"):
            path = omkpaths.data("MESHES/DECORS", f); break
    if not path: return None
    h, ms = mesh3do.meshes(path)
    raw = open(path, "rb").read()
    tags, _all = sfx_effects(stem)
    sprites = scx_sprites(stem)

    ems, used = [], {}
    for m in ms:
        if not ((m["flags"] & 0xFFFFFFFF) & 0x40000000): continue
        o = h["meshOff"] + 140 * m["i"] + 16
        e = tags.get(raw[o:o + 4])
        if not e: continue
        sp = sprites.get(e["sprite"])
        if not sp: continue
        key = sp["name"]
        if key not in used:
            size, frames = sprite_frames(sp["model"])
            used[key] = {"size": size, "frames": frames,
                         "id": e["sprite"], "index": len(used)}
        ems.append({
            "mesh": m["name"], "pos": list(m["pos"]),
            "effect": e["name"], "sprite": key,
            "life": e["life"], "speed": e["speed"], "cone": e["cone"],
            "scale": e["scale"], "spin": e["spin"], "mode": e["mode"],
            "flags": e["flags"], "period": e.get("period", 0.0),
            "col0": [(e["c48"] >> 16) & 255, (e["c48"] >> 8) & 255, e["c48"] & 255],
            "col1": [(e["c52"] >> 16) & 255, (e["c52"] >> 8) & 255, e["c52"] & 255],
            "vel": [e["vx"], e["vy"], e["vz"]],
            # +68 is how many particles ONE emission makes - the emit loop in
            # Sfx_TickAmbient runs exactly this many times
            "count": max(1, e["count"]),
            "grow": 1 if e["flags"] & FLAG_GROW else
                   -1 if e["flags"] & FLAG_SHRINK else 0,
            # the integrator does `vel.y += accel` every frame, so a particle
            # ACCELERATES along world Y - which is what makes a flame a tall
            # column rather than a short puff. 159 of the 366 effects use it.
            "accel": (e["drift"] if (e["flags"] & 0x600) == FLAG_DRIFT
                      else -e["drift"]),
            "jitterDir": bool(e["flags"] & FLAG_JITDIR),
            "randAngle": bool(e["flags"] & FLAG_ANGLE),
            "randCone": bool(e["flags"] & FLAG_NCONE),
            "randLife": bool(e["flags"] & FLAG_NLIFE),
        })
    return {"set": stem, "emitters": ems, "sprites": used}


def sprite_png(setname, name):
    """The atlas of one sprite, as PNG bytes."""
    for sid, sp in scx_sprites(setname.upper()).items():
        if sp["name"] == name:
            tx = tex3dt.textures_bytes(sp["model"], sp["texture"])
            if tx: return tex3dt.png(tx[0]["w"], tx[0]["h"], tx[0]["rgb"])
    return None


if __name__ == "__main__":
    args = sys.argv[1:]
    if args:
        g = emitters(args[0])
        if not g: sys.exit("no such set")
        print("%s: %d emitters, %d sprites" %
              (g["set"], len(g["emitters"]), len(g["sprites"])))
        for k, v in g["sprites"].items():
            print("   sprite %-24s %2d frames  %.0fx%.0f units"
                  % (k, len(v["frames"]), v["size"][0], v["size"][1]))
        for e in g["emitters"][:40]:
            print("   %-14s -> %-10s life %5.1f speed %5.1f cone %5.1f "
                  "scale %4.1f mode %d" % (e["mesh"], e["effect"], e["life"],
                                           e["speed"], e["cone"], e["scale"],
                                           e["mode"]))
        sys.exit(0)
    tot = 0
    for f in sorted(os.listdir(omkpaths.data("MESHES/DECORS"))):
        if not f.upper().endswith(".3DO"): continue
        g = emitters(f.rsplit(".", 1)[0])
        if g and g["emitters"]:
            tot += len(g["emitters"])
            print("%-14s %3d emitters  %s" % (g["set"], len(g["emitters"]),
                  ", ".join(sorted({e["effect"] for e in g["emitters"]}))[:60]))
    print("total emitters: %d" % tot)

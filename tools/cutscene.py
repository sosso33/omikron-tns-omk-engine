#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The in-engine cutscenes: a shot resolved end to end, ready to play.

A cutscene in Omikron is not a format. It is an `.SCX` scene object whose
program animates the characters, plus a chunk-10 *camera editing* linked to
that object, plus the set the area names. This module joins the three and
hands back everything a viewer needs to replay one - see docs/CUTSCENES.md.

    python3 tools/cutscene.py                     # every shot in the game
    python3 tools/cutscene.py Impasse.SCX intro   # one shot, resolved
    python3 tools/cutscene.py --selftest

The camera sampler is a transcription of `Cam_PlayEditing` (0x0049ECE0) and
its slope helper (0x0049EC10), so the path a viewer draws is the path the
engine drives:

    tracks are laid END TO END in time - each track owns [base, base+last),
    where `last` is its own final key frame, and `base` accumulates;
    inside a track, the pair of keys bracketing t gives two whole CAMERAS,
    and every field (eye xyz, target xyz, roll, fov) is interpolated
    linearly between them by (c1 - c0) / (frame1 - frame0).

The engine gives up outside the editing's duration - `Cam_PlayEditing` returns
0 without touching the camera when `t >= duration`, and logs "key not found for
frame %2.2f" if no track brackets t. Both cases return None here.
"""
import omkpaths
import glob, math, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(HERE)

import cam_editing, scene_scx, anim_3da, mesh3do

SCPTDATA = omkpaths.data("SCPTDATA")
PERSOS   = omkpaths.data("MESHES/PERSOS")
FPS      = 30.0                      # Game_Frame's fixed step

# Script_SelectBodyAnimation and its relative twin: param 0 indexes the
# object's own name table (a scene NODE - `UBassin` is a pelvis, not a clip),
# param 1 the scene's animation array BY POSITION. FILE_FORMATS 5c.
ANIM_FNS  = {0x02000004, 0x0200002A}
PLAY_SOUND      = 0x05000014
PLAY_SYNC_SOUND = 0x05000015
STOP_SOUND      = 0x05000016
SOUND_FNS = {PLAY_SOUND, PLAY_SYNC_SOUND, STOP_SOUND}


# --------------------------------------------------------------- the camera
def sample(shot, t):
    """Cam_PlayEditing at frame `t`. -> {eye, at, roll, fov} or None.

    `shot` is one entry of `index()`'s editings, carrying the resolved tracks.
    """
    if t < 0 or t >= shot["duration"]: return None
    base = 0.0
    for tr in shot["tracks"]:
        ks = tr["keys"]
        if not ks: continue
        last = ks[-1]["frame"]
        for i in range(len(ks) - 1):
            a, b = ks[i], ks[i + 1]
            if a["frame"] + base <= t < b["frame"] + base:
                span = b["frame"] - a["frame"]
                if span <= 0: return None
                u = t - base - a["frame"]
                c0, c1 = a["camera"], b["camera"]
                def lerp(f):
                    return (c1[f] - c0[f]) / span * u + c0[f]
                return {"eye":  [lerp("px"), lerp("py"), lerp("pz")],
                        "at":   [lerp("tx"), lerp("ty"), lerp("tz")],
                        "roll": lerp("roll"), "fov": lerp("fov"),
                        "track": tr["name"], "camera": a["name"]}
        base += last
    return None                       # the engine's "key not found" branch


def path(shot, step=1.0):
    """The whole move, one sample per `step` frames. -> [ {..} or None ]."""
    n = int(math.ceil(shot["duration"] / step))
    return [sample(shot, i * step) for i in range(n)]


# ------------------------------------------------------------ where it plays
_AREA_ASSETS = None

def area_assets():
    r"""{SCX stem: {"set": stem, "areas": [(id, name)]}} from the AREA header.

    `Area_TickLoad` appends `.3DO` to the 9-byte name at +88 and `.SCX` to the
    one at +97 (docs/CUTSCENES.md 1), so this is the map from a scene file back
    to the room it is played in.
    """
    global _AREA_ASSETS
    if _AREA_ASSETS is None:
        import dialog_triggers as T, omkdata
        out = {}
        A = T.archive(omkpaths.data("IAM/AREA"))
        names = omkdata.TAGS.get("AREAS", {})
        for k, b in sorted(A.items()):
            if len(b) < 130: continue
            dec = b[88:97].split(b"\0")[0].decode("cp1252", "replace").upper()
            scx = b[97:106].split(b"\0")[0].decode("cp1252", "replace").upper()
            if not scx: continue
            e = out.setdefault(scx, {"set": "", "areas": []})
            e["areas"].append([k, names.get(k, "")])
            if dec and not e["set"]: e["set"] = dec
        _AREA_ASSETS = out
    return _AREA_ASSETS


# -------------------------------------------------------------- the music
_TRACKS = None

def tracks():
    global _TRACKS
    if _TRACKS is None:
        _TRACKS = {int(f.split(".")[0]) for f in os.listdir(omkpaths.data("TRACKS"))
                   if f.upper().endswith(".ADP") and f.split(".")[0].isdigit()}
    return _TRACKS


def music_for(areas):
    r"""The music track playing under a scene. -> {"track", "source"} or None.

    Not part of the cutscene data: a scene's own object programs never touch
    music. The bed comes from the area, two ways, in the order the engine
    would:

      * `AREA +142` - the area's own track, which `Area_Load`'s case 9 hands
        to `Music_PlayTrack` on arrival (verified 189/189 elsewhere). 26 of
        the 29 cutscene scenes have one;
      * failing that, whatever the area's own scripts play with `music.play`,
        which is how Impasse gets its two.

    The voices do NOT resolve the same way: `media.play` names a `ZVO ...`
    OBJECTS entry whose stem is a `VOICEOFF\*.ADP`, and only **10 of the 561**
    ship in this tree. See docs/CUTSCENES.md.
    """
    import dialog_triggers as T, dialog_disasm as D
    A = T.archive(omkpaths.data("IAM/AREA"))
    for aid, _name in areas:
        b = A.get(aid)
        if b is None or len(b) < 144: continue
        t = struct.unpack_from("<h", b, 142)[0]
        if t and t in tracks():
            return {"track": t, "source": "AREA %d +142" % aid}
    for aid, _name in areas:
        b = A.get(aid)
        if b is None: continue
        r = T.LAYOUT["AREA"](b)
        if not r: continue
        for rec, f, pp in (list(T._scripts_from_records(b, r[0], r[1]))
                           + T._second_table("AREA", b)):
            ops, st = D.disasm(b, pp, len(b))
            if st != "ok": continue
            for pc, op, raw in ops:
                if op == 103:
                    n = struct.unpack_from("<h", raw, 0)[0]
                    if n in tracks():
                        return {"track": n, "source": "music.play in AREA %d" % aid}
    return None


# --------------------------------------------------------------- the beats
# The shots of one scene are one cutscene, and the authors numbered them in
# the object names: `A_1_KaylArrives`, `A_2_DemonLook`, `C_1_BoxMoves`,
# `C_2_MecaSpeaks`, `C_3_KaylsUp`. A leading letter is the beat and the number
# the step inside it; some scenes use a bare number (`2_Combat_Kayl`).
#
# This is an AUTHORING CONVENTION read off the names, not a mechanism traced in
# the engine - see docs/CUTSCENES.md, "what orders the beats". The editing ids
# are no help (Impasse's `intro` is id 15) and no shipped world script fires
# these objects at all.
_BEAT = re.compile(r"^(?:([A-Za-z])_)?(\d+)[_ ]")

def beat_key(name):
    """-> (letter, step) for sorting, or None when the name says nothing.

    An unlettered name (`2_Combat_Kayl`) sorts after the lettered ones - in
    Impasse those are the fight, which comes last. That tie-break is a guess
    about one scene and nothing rests on it: the beat is a label in the UI,
    and the viewer lets the order be changed.
    """
    m = _BEAT.match(name or "")
    if not m: return None
    return ((m.group(1) or "Z").upper(), int(m.group(2)))


# ------------------------------------------------------------- the actors
_RIGS = None

def rigs():
    """{bone prefix: [model, ...]} over MESHES/PERSOS.

    Every character mesh is named `<prefix><bone>` - `UBassin`, `TeCuissed` -
    so the prefix of a clip's root track identifies the rig it animates. Most
    prefixes name exactly one model; where several share one, `model_for`
    scores them against the clip's whole track list.
    """
    global _RIGS
    if _RIGS is None:
        out = {}
        for p in sorted(glob.glob(os.path.join(PERSOS, "*.3DO"))):
            try: _h, ms = mesh3do.meshes(p)
            except Exception: continue
            name = os.path.basename(p)[:-4]
            for m in ms:
                if m["name"].lower().endswith("bassin"):
                    out.setdefault(m["name"][:-6], []).append(name)
                    break
        _RIGS = out
    return _RIGS


def model_for(track_names):
    """The character model a clip's tracks belong to, or None.

    Scored, not guessed: the winner is the model that owns the most of the
    clip's track names outright. A tie goes to the `*_FNM` variant, which is
    the one carrying a face mesh.
    """
    if not track_names: return None
    root = track_names[0]
    if not root.lower().endswith("bassin"): return None
    cands = rigs().get(root[:-6]) or []
    if len(cands) == 1: return cands[0]
    best = None
    for c in cands:
        try: _h, ms = mesh3do.meshes(os.path.join(PERSOS, c + ".3DO"))
        except Exception: continue
        have = {m["name"] for m in ms}
        score = sum(n in have for n in track_names)
        key = (score, c.upper().endswith("FNM"))
        if best is None or key > best[0]: best = (key, c)
    return best[1] if best else None


def _clip(stream, idx):
    """One SCX-embedded .3DA, with its root track turned into a world path.

    Two rules, both from the engine:

    * **key 0 is the authored placement**, not frame 0 - the same rest-sentinel
      slot the whole animation runtime skips (ASSETS; `verify.py: scene clip
      roots` finds a world-scale value there in 740 of the 874 scene clips),
      and `Anim_SnapRootToStart` is what consumes it;
    * **keys 1.. are per-frame deltas**, which is what `Anim_RootDelta`
      (0x004711D0) says outright: given a frame interval it *sums* the float[3]
      at `clip+28` over it, scaling the fractional ends, and hands the total to
      `Actor_MoveBy`. So the position at frame f is key 0 plus the running sum.

    The deltas are left unrotated here. `Anim_RootDelta` will rotate them by a
    3x3 if its caller passes one, and for `1-01KAY.3DA` - Kay'l walking into
    the alley, the longest travel in the game's cutscenes - rotating them by
    the root's own quaternion sinks the pelvis 70 units over the walk while
    leaving them alone holds it level. Unrotated also gives the only sensible
    travel (119 units across the alley against 69 rotated).

    The quaternion is returned **conjugated**, because that is the convention
    every other bone reaches the viewer in: `ani_pose_stream` conjugates each
    track's rotation before composing, and strips the root's so a caller can
    apply it as a world orientation. See `docs/CUTSCENES.md` on what this is
    and is not checked against.
    """
    if not (0 <= idx < len(stream["anims"])): return None
    a = stream["anims"][idx]
    r = anim_3da.descriptor(stream["data"], a["offset"], a["declared"])
    if not r or not r["tracks"]: return None
    t0 = r["tracks"][0]
    pos = anim_3da.positions(stream["data"], a["offset"], t0) if t0["posKeys"] else []
    rot = anim_3da.rotations(stream["data"], a["offset"], t0) if t0["rotKeys"] else []
    frames = max(1, r["frames"])
    place = list(pos[0]) if pos else [0.0, 0.0, 0.0]
    root, at = [], list(place)
    for f in range(frames):
        if len(pos) > 1:
            d = pos[min(f + 1, len(pos) - 1)]
            at = [at[0] + d[0], at[1] + d[1], at[2] + d[2]]
        q = rot[min(f + 1, len(rot) - 1)] if len(rot) > 1 else (1.0, 0.0, 0.0, 0.0)
        root.append([round(at[0], 3), round(at[1], 3), round(at[2], 3),
                     round(q[0], 5), round(-q[1], 5), round(-q[2], 5),
                     round(-q[3], 5)])
    names = [t["name"] for t in r["tracks"]]
    travel = math.dist((root[0][0], root[0][2]), (root[-1][0], root[-1][2]))
    return {"name": a["name"], "index": idx, "frames": frames,
            "tracks": len(names), "trackNames": names,
            "place": [round(v, 2) for v in place], "root": root,
            "travel": round(travel, 1), "model": model_for(names)}


# ---------------------------------------------------------------- the index
_SCENES = {}

def scene(fn):
    """Everything one .SCX contributes, cached. -> dict or None (no chunk 10)."""
    if fn in _SCENES: return _SCENES[fn]
    p = os.path.join(SCPTDATA, fn)
    body = cam_editing.payload(p)
    if body is None:
        _SCENES[fn] = None
        return None
    cams = cam_editing.parse(body)
    sc = scene_scx.scene(p)
    stream = anim_3da.scx_stream(p)
    byhandle = {o["handle"] >> 16: o for o in sc["objects"]}
    sounds = scene_scx.resource_names(sc["chunks"].get(3, []))

    edts = []
    for e in cams["editings"]:
        tracks = []
        for tid in e["tracks"]:
            tr = cams["tracks"].get(tid)
            if not tr: continue
            ks = []
            for kid in tr["keys"]:
                k = cams["keys"].get(kid)
                if not k: continue
                c = cams["cameras"].get(k["camera"])
                if not c: continue
                ks.append({"frame": k["frame"], "name": c["name"],
                           "camera": {"px": c["pos"][0], "py": c["pos"][1],
                                      "pz": c["pos"][2], "tx": c["target"][0],
                                      "ty": c["target"][1], "tz": c["target"][2],
                                      "roll": c["roll"], "fov": c["fov"]}})
            tracks.append({"name": tr["name"], "keys": ks})
        obj = byhandle.get(e["target"]) if e["target"] else None
        edts.append({"id": e["id"], "name": e["name"], "duration": e["duration"],
                     "tracks": tracks, "object": obj, "objectName":
                     obj["name"] if obj else None})

    a = area_assets().get(fn.upper().split(".")[0], {})
    out = {"file": fn, "set": a.get("set", ""), "areas": a.get("areas", []),
           "music": music_for(a.get("areas", [])),
           "editings": edts, "stream": stream, "sounds": sounds,
           "objects": sc["objects"]}
    _SCENES[fn] = out
    return out


def files():
    """Every .SCX carrying a camera editing, in name order."""
    seen = []
    for p in (sorted(glob.glob(os.path.join(SCPTDATA, "*.SCX"))) +
              sorted(glob.glob(os.path.join(SCPTDATA, "*.scx")))):
        fn = os.path.basename(p)
        if scene(fn): seen.append(fn)
    return seen


def index():
    """The whole catalogue, JSON-ready and light: no keys, no clips."""
    out = []
    for fn in files():
        s = scene(fn)
        shots = [{"id": e["id"], "name": e["name"], "duration": e["duration"],
                  "seconds": round(e["duration"] / FPS, 1),
                  "tracks": len(e["tracks"]),
                  "keys": sum(len(t["keys"]) for t in e["tracks"]),
                  "object": e["objectName"]}
                 for e in s["editings"]]
        for e in shots: e["beat"] = beat_key(e["object"] or "")
        named = sum(e["beat"] is not None for e in shots)
        # beat order when the names carry one, longest first otherwise
        if named >= 2:
            shots.sort(key=lambda e: (e["beat"] is None, e["beat"] or ("", 0),
                                      -e["duration"]))
        else:
            shots.sort(key=lambda e: -e["duration"])
        out.append({"file": fn, "set": s["set"], "areas": s["areas"],
                    "shots": shots, "ordered": named >= 2, "beats": named,
                    "music": s["music"],
                    "frames": sum(e["duration"] for e in s["editings"])})
    return out


# ----------------------------------------------------------------- one shot
def _f32(bits):
    """A script parameter read as the float it was authored as."""
    return struct.unpack("<f", struct.pack("<i", bits))[0]


def program(s, obj):
    """One scene object's program, with every parameter resolved."""
    if not obj: return None
    out = []
    for f in obj["functions"]:
        name = scene_scx.function_name(f["id"])
        row = {"kind": f["kind"], "name": name, "params": f["params"]}
        p = f["params"]
        if f["id"] in ANIM_FNS and len(p) >= 2:
            tab = obj["tables"][0] if obj["tables"] else []
            row["node"] = tab[p[0]] if 0 <= p[0] < len(tab) else None
            row["anim"] = p[1]
            a = s["stream"]["anims"]
            row["clip"] = a[p[1]]["name"] if 0 <= p[1] < len(a) else None
        elif f["id"] in SOUND_FNS and p:
            # The two play functions do NOT share a parameter layout, which is
            # worth stating because reading one as the other silently invents a
            # cue time. From their handlers:
            #
            #   Script_PlaySyncSound (0x004A14D0)  0 sound  1 the FRAME on the
            #     object's program clock to fire at (`if param1 > obj+88
            #     return busy` - so a pending cue also holds the chain)
            #     2 &1 loop  3 a runtime latch, 0 on disk  4 the node to
            #     position it at, -1 = non-positional
            #   Script_PlaySound (0x004A12D0)      0 sound  1 &1 loop
            #     2 the latch  3 the node
            row["sound"] = (s["sounds"][p[0]] if 0 <= p[0] < len(s["sounds"])
                            else None)
            row["wav"] = p[0]
            if f["id"] == PLAY_SYNC_SOUND:
                row["sync"] = True
                row["at"] = round(_f32(p[1]), 2) if len(p) > 1 else 0
                row["loop"] = bool(len(p) > 2 and p[2] & 1)
                row["node"] = (p[4] if len(p) > 4 else -1)
            elif f["id"] == PLAY_SOUND:
                # fires when the program counter reaches it, which for a
                # cutscene object is the start of its one main chain
                row["sync"] = False
                row["at"] = 0
                row["loop"] = bool(len(p) > 1 and p[1] & 1)
        out.append(row)
    return {"name": obj["name"], "index": obj["index"], "link": obj["link"],
            "functions": out}


def shot(fn, editing):
    """One cutscene, resolved. `editing` is an id or a name.

    -> {file,set,areas,name,duration,camera[],shots[],program,actors[]}
       camera[i] is the engine's camera at frame i (or None where it gives up).
    """
    s = scene(fn)
    if not s: return None
    e = next((x for x in s["editings"]
              if str(x["id"]) == str(editing) or x["name"] == editing), None)
    if not e: return None

    segs, base = [], 0.0
    for tr in e["tracks"]:
        last = tr["keys"][-1]["frame"] if tr["keys"] else 0
        segs.append({"name": tr["name"], "from": round(base, 2),
                     "to": round(base + last, 2),
                     "keys": [{"frame": k["frame"], "camera": k["name"],
                               "fov": round(k["camera"]["fov"], 1)}
                              for k in tr["keys"]]})
        base += last

    cam = []
    for i in range(int(e["duration"])):
        c = sample(e, i)
        cam.append(None if c is None else
                   [round(v, 2) for v in c["eye"]] +
                   [round(v, 2) for v in c["at"]] +
                   [round(c["roll"], 3), round(c["fov"], 3)])

    prog = program(s, e["object"])
    actors = []
    if prog:
        for row in prog["functions"]:
            if "anim" not in row: continue
            c = _clip(s["stream"], row["anim"])
            if not c: continue
            bind = None
            if c["model"]:
                try:
                    _h, ms = mesh3do.meshes(os.path.join(PERSOS,
                                                         c["model"] + ".3DO"))
                    m = next((m for m in ms if m["name"] == c["trackNames"][0]),
                             None)
                    if m: bind = [round(v, 3) for v in m["pos"]]
                except Exception:
                    pass
            actors.append({"node": row.get("node"), "clip": c["name"],
                           "anim": c["index"], "frames": c["frames"],
                           "model": c["model"], "place": c["place"],
                           "rootBind": bind, "root": c["root"],
                           "travel": c["travel"], "tracks": c["tracks"]})

    cues = []
    if prog:
        for row in prog["functions"]:
            if "wav" not in row or row.get("sound") is None: continue
            cues.append({"wav": row["wav"], "name": row["sound"],
                         "at": row.get("at", 0), "loop": row.get("loop", False),
                         "sync": row.get("sync", False)})
    cues.sort(key=lambda c: c["at"])

    return {"file": fn, "set": s["set"], "areas": s["areas"], "id": e["id"],
            "name": e["name"], "duration": e["duration"], "fps": FPS,
            "music": s["music"], "beat": beat_key(e["objectName"] or ""),
            "camera": cam, "shots": segs, "program": prog, "actors": actors,
            "cues": cues, "coverage": sum(c is not None for c in cam)}


def wav(fn, index):
    r"""One chunk-3 sound out of the .SCX stream, as a whole RIFF file.

    The scene's sounds are not in `gamedata/SOUNDS` - that ships two files - they
    are embedded in the .SCX streamed section, plain 16-bit PCM mono RIFF at
    22050 Hz, exactly as `Sound_Play3D` hands them to DirectSound. 563 of them
    across the 29 cutscene scenes.
    """
    s = scene(fn)
    if not s: return None
    ws = s["stream"]["wavs"]
    if not (0 <= index < len(ws)): return None
    w = ws[index]
    return s["stream"]["data"][w["offset"]:w["offset"] + w["size"]]


# ===========================================================================
# The OTHER kind of cutscene: world cameras, driven by a world script
# ===========================================================================
# Not every cutscene is an .SCX camera editing. A world script can direct one
# itself, cutting and travelling between entries of the WORLD camera table with
# `camera.set` / `camera.set.wait`, with `media.play` for the voice-over and
# `music.play` underneath. The game's title sequence is one of these - AREA 0
# record 78, 46 moves over 145.7 s while the credits play - and none of them
# has an editing, which is why a chunk-10 index cannot see them.
#
# The table is the one Camera_FindWorld scans: 44-byte records at AREA +64
# (count +84) / SCENE +32 (+52) / GLOBAL +20 (+30), and its coordinates are
# raw, in the same units `Global_Load` converts - the scaled eyes of AREA 0
# span x -276..16272 z -12139..2732 inside an ANEKBAH set of x -3399..16397
# z -12909..5147, where the raw ones are 6.5x too big for the room.

CAM_SET, CAM_WAIT, CAM_AT_ADDRESS = 95, 96, 126
MEDIA_PLAY, MUSIC_PLAY = 92, 103


def _raw_to_world(v):
    """The engine's raw -> world conversion, as Global_Load applies it."""
    return v * 100 * 0.00390625 * 0.3937007874015748 - 1.0


def _angle(raw):
    r"""A 4096-per-turn angle field, in degrees on (-180, 180].

    `Global_Load` reads these signed and multiplies by 360/4096 without
    wrapping, so a stored 4086 comes out as 358.6 - which is the same rotation
    as -1.4 and renders identically **standing still**. It stops being the same
    the moment two of them are interpolated: 28 of AREA 0's 151 cameras carry a
    small negative roll stored near 4096, and lerping one of those to 0 sweeps
    the long way round. That is the "camera makes several complete loops where
    the game turns a few degrees" the title sequence showed at frames 3580 and
    3840. Wrapping first makes the short way the numerically short one too.
    """
    return ((raw + 2048) % 4096 - 2048) * 0.087890625


def _lerp_angle(a, b, u):
    """Interpolate two degree angles along the SHORTEST arc."""
    d = (b - a + 180.0) % 360.0 - 180.0
    return a + d * u


def world_cameras(arch, chunk):
    """{id: {eye, at, roll, fov}} for one chunk, plus GLOBAL's shared table."""
    import dialog_triggers as T
    out = {}
    def take(b, po, co):
        if len(b) < co + 2: return
        p, n = struct.unpack_from("<I", b, po)[0], struct.unpack_from("<h", b, co)[0]
        if n <= 0 or p + 44 * n > len(b): return
        for i in range(n):
            o = p + 44 * i
            v = struct.unpack_from("<6i", b, o)
            cid, _mode, roll, fov = struct.unpack_from("<4h", b, o + 24)
            out.setdefault(cid, {
                "eye": [round(_raw_to_world(x), 2) for x in v[:3]],
                "at":  [round(_raw_to_world(x), 2) for x in v[3:]],
                "roll": round(_angle(roll), 3),
                "fov": round(fov * 0.087890625, 3)})
    g = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
    take(g, 20, 30)
    if arch in ("AREA", "SCENE"):
        po, co = (64, 84) if arch == "AREA" else (32, 52)
        b = T.archive(omkpaths.data("IAM", arch)).get(chunk)
        if b is not None: take(b, po, co)
    return out


def _script_ops(arch, chunk, rec):
    import dialog_triggers as T, dialog_disasm as D
    b = T.archive(omkpaths.data("IAM", arch))[chunk]
    r = T.LAYOUT[arch](b)
    if not r: return None, None
    for k, f, p in (list(T._scripts_from_records(b, r[0], r[1]))
                    + T._second_table(arch, b)):
        if k == rec:
            ops, st = D.disasm(b, p, len(b))
            return (ops if st == "ok" else None), b
    return None, b


def camera_shot(arch, chunk, rec):
    r"""One world-camera cutscene as a timeline the viewer can play.

    The move model is the dialogue one: `camera.set X, 0` CUTS to X, and a
    following `camera.set X, t` or `camera.set.wait X, t` TRAVELS to it over
    `t` frames (the handler's second field). So the script reads as a shot
    list, and the frames it names are its own clock.
    """
    ops, _b = _script_ops(arch, chunk, rec)
    if not ops: return None
    cams = world_cameras(arch, chunk)
    steps, cues, t, cur = [], [], 0.0, None
    def fields(raw):
        """The operand as its int16 fields - lengths differ per opcode, and
        assuming three of them made `media.play` read (0,0,0)."""
        n = len(raw) // 2
        return struct.unpack_from("<%dh" % n, raw, 0) + (0,) * (3 - n) if n else (0, 0, 0)
    for pc, op, raw in ops:
        f = fields(raw)
        if op in (CAM_SET, CAM_WAIT):
            cid, travel = f[0], max(0, f[1])
            if travel == 0 or cur is None:
                cur = cid                                   # a cut
                steps.append({"at": round(t, 1), "camera": cid, "travel": 0,
                              "from": cur, "wait": op == CAM_WAIT})
            else:
                steps.append({"at": round(t, 1), "camera": cid,
                              "travel": travel, "from": cur,
                              "wait": op == CAM_WAIT})
                if op == CAM_WAIT: t += travel               # only .wait holds
                cur = cid
        elif op == MEDIA_PLAY:
            import omkdata
            cues.append({"at": round(t, 1), "kind": "voice", "id": f[0],
                         "name": omkdata.TAGS.get("OBJECTS", {}).get(f[0], "")})
        elif op == MUSIC_PLAY:
            cues.append({"at": round(t, 1), "kind": "music", "id": f[0],
                         "name": "track %d" % f[0]})
    duration = max(1, int(round(t)))

    # one camera per frame, so the client plays these exactly like an editing
    cam = []
    for i in range(duration):
        act = None
        for stp in steps:
            if stp["travel"] and stp["wait"] and stp["at"] <= i < stp["at"] + stp["travel"]:
                act = stp; break
            if stp["at"] <= i: act = act or None
        # the step in force at frame i
        cur_step = None
        for stp in steps:
            if stp["at"] <= i: cur_step = stp
        if cur_step is None: cam.append(None); continue
        a = cams.get(cur_step["from"]); b2 = cams.get(cur_step["camera"])
        if b2 is None: cam.append(None); continue
        if not cur_step["travel"] or a is None:
            c = b2; u = 1.0
        else:
            u = min(1.0, (i - cur_step["at"]) / cur_step["travel"])
            c = None
        if c is None:
            L = lambda k, j: a[k][j] + (b2[k][j] - a[k][j]) * u
            cam.append([round(L("eye", 0), 2), round(L("eye", 1), 2),
                        round(L("eye", 2), 2), round(L("at", 0), 2),
                        round(L("at", 1), 2), round(L("at", 2), 2),
                        # roll is an ANGLE: take the short way round, or a move
                        # between +179 and -179 spins the camera 358 degrees
                        round(_lerp_angle(a["roll"], b2["roll"], u), 3),
                        round(a["fov"] + (b2["fov"] - a["fov"]) * u, 3)])
        else:
            cam.append([c["eye"][0], c["eye"][1], c["eye"][2],
                        c["at"][0], c["at"][1], c["at"][2], c["roll"], c["fov"]])

    import dialog_triggers as T, omkdata
    b = T.archive(omkpaths.data("IAM", arch))[chunk]
    # a SCENE is played over an area, so its set is that area's
    host = chunk if arch == "AREA" else omkdata._scene_area().get(chunk)
    hb = T.archive(omkpaths.data("IAM/AREA")).get(host) if host is not None else None
    setname = (hb[88:97].split(b"\0")[0].decode("cp1252", "replace").upper()
               if hb is not None and len(hb) > 106 else "")
    tag = "AREAS" if arch == "AREA" else "SCENES"
    music = next((c["id"] for c in cues if c["kind"] == "music"), None)
    return {"kind": "camera", "arch": arch, "chunk": chunk, "rec": rec,
            "name": "%s %d rec %d" % (arch, chunk, rec),
            "where": omkdata.TAGS.get(tag, {}).get(chunk, ""),
            "set": setname, "areas": [[chunk, omkdata.TAGS.get(tag, {}).get(chunk, "")]]
                             if arch == "AREA" else [],
            "duration": duration, "fps": FPS,
            "music": {"track": music, "source": "music.play in the script"}
                     if music in tracks() else None,
            "camera": cam, "steps": steps, "cues": cues,
            "shots": [{"name": "cam %d" % s["camera"], "from": s["at"],
                       "to": s["at"] + s["travel"], "keys": []}
                      for s in steps if s["travel"]],
            "actors": [], "program": None,
            "coverage": sum(c is not None for c in cam)}


def camera_scripts(min_moves=6, min_frames=300):
    """Every world script that directs a camera sequence worth watching."""
    import dialog_triggers as T, dialog_disasm as D, omkdata
    out = []
    for arch in ("AREA", "SCENE"):
        for k, b in sorted(T.archive(omkpaths.data("IAM", arch)).items()):
            r = T.LAYOUT[arch](b)
            if not r: continue
            for rec, f, p in (list(T._scripts_from_records(b, r[0], r[1]))
                              + T._second_table(arch, b)):
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": continue
                moves = frames = voice = 0
                for pc, op, raw in ops:
                    n = len(raw) // 2
                    fl = (struct.unpack_from("<%dh" % n, raw, 0) + (0,) * (3 - n)
                          if n else (0, 0, 0))
                    if op in (CAM_SET, CAM_WAIT):
                        moves += 1
                        if op == CAM_WAIT: frames += max(0, fl[1])
                    elif op == MEDIA_PLAY: voice += 1
                if moves >= min_moves and frames >= min_frames:
                    tag = "AREAS" if arch == "AREA" else "SCENES"
                    out.append({"arch": arch, "chunk": k, "rec": rec,
                                "name": "%s %d rec %d" % (arch, k, rec),
                                "where": omkdata.TAGS.get(tag, {}).get(k, ""),
                                "moves": moves, "duration": frames,
                                "seconds": round(frames / FPS, 1), "voice": voice})
    out.sort(key=lambda e: -e["duration"])
    return out


# ------------------------------------------------------------------ selftest
def selftest():
    """Every shot in the game, sampled at every frame.

    The invariant is the engine's own: outside a gap between tracks, every
    frame of every editing must resolve to a camera. Anything else means the
    sampler and Cam_PlayEditing disagree about how tracks are laid out.
    """
    nsh = full = 0
    frames = covered = 0
    noobj = 0
    for fn in files():
        for e in scene(fn)["editings"]:
            nsh += 1
            if not e["objectName"]: noobj += 1
            d = int(e["duration"])
            got = sum(sample(e, i) is not None for i in range(d))
            frames += d; covered += got
            full += (got == d)
    print("%d shots in %d scenes; %d of %d frames resolve to a camera; "
          "%d shots resolve at every frame; %d ship unlinked"
          % (nsh, len(files()), covered, frames, full, noobj))
    return nsh, full, frames, covered, noobj


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--selftest" in sys.argv:
        selftest(); return 0
    if len(args) >= 2:
        s = shot(args[0], args[1])
        if not s: print("no such shot"); return 1
        print("%s  %s  set %s" % (s["file"], s["name"], s["set"] or "?"))
        print("  %d frames (%.1f s), camera resolves at %d of them"
              % (s["duration"], s["duration"] / FPS, s["coverage"]))
        for t in s["shots"]:
            print("  track %-12s %6.0f..%-6.0f %d keys"
                  % (t["name"], t["from"], t["to"], len(t["keys"])))
        if s["program"]:
            print("  object %s" % s["program"]["name"])
            for f in s["program"]["functions"]:
                extra = ""
                if "clip" in f: extra = "  %s -> %s" % (f.get("node"), f["clip"])
                if "sound" in f: extra = "  %s @%s" % (f.get("sound"), f.get("at"))
                print("    %-5s %-36s%s" % (f["kind"], f["name"], extra))
        for a in s["actors"]:
            print("  actor %-12s %-16s %-8s %d frames at %s, travels %s"
                  % (a["node"], a["clip"], a["model"], a["frames"], a["place"],
                     a["travel"]))
        return 0
    for sc in index():
        print("%-16s set %-10s %2d shots  %5d frames"
              % (sc["file"], sc["set"] or "?", len(sc["shots"]), sc["frames"]))
        for e in sc["shots"][:4]:
            print("     %-12s %5d fr (%4.1f s)  %s"
                  % (e["name"], e["duration"], e["seconds"], e["object"] or "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

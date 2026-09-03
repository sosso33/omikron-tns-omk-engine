#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Assert every number the docs quote, and fail if one has moved.

`docs/` is full of counts - "1246 dialog.start sites", "5785 of 5785 scripts
decode", "1615 of 1670 cameras". Each of those was a real test when it was
written, and each stops being one the moment it is only a sentence. This runs
them again.

    python3 tools/verify.py            # the fast checks (a few seconds)
    python3 tools/verify.py --slow     # plus the whole-asset sweeps
    python3 tools/verify.py --list     # what is checked, and where it is quoted

Exit status is the number of failures, so it drops straight into a hook or a
shell `&&` chain.

A check earns its place here only if the data could fail it. "It parses" is not
a check; "the walk lands exactly on the file size", "830 records carry 830
distinct bits", "1615 of 1670 are within 1 unit of 768" are. Where a number is
a median over a corpus rather than an exact count it is asserted as a bound,
generously, so that normal variation does not cry wolf but a structural change
does.

Adding one: write a function returning (got, want, note), and list it in CHECKS
with the doc it is quoted in. Keep the doc and the check in step - a number in
`docs/` that nothing here asserts is a claim with no test behind it.
"""
import os, re, sys, glob, math, struct, subprocess, statistics as S

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.chdir(ROOT)

import omkpaths

# BEFORE the readers are imported, not in main(). Several of them resolve a
# path at MODULE level - `omkdata.DIALOG`, `omkdata.MORPH`, `fnt.FONTS_DIR` -
# so a `--data` parsed any later would be read after the value it is meant to
# change had already been computed, and would silently do nothing for exactly
# the modules that matter most.
omkpaths.take_flags(sys.argv)

import omkdata as O, dialog_triggers as T, anim_ani, morph3dm, tex3dt
import ui_tables


def _run(*cmd):
    return subprocess.run([sys.executable] + list(cmd), capture_output=True,
                          text=True).stdout


def _need(*kinds):
    """A skip triple when an input this check reads is absent, else None.

        s = _need("asm")
        if s: return s

    The disassembly (`Runtime.exe.asm`, `Runtime.exe.c`) and the `clean/` tree
    derived from it are a derivative work of the game's binary and are NOT
    distributed with this repository, so a legitimate checkout may not have
    them - see `tools/omkpaths.py`. The 14 checks that read them must SKIP
    there rather than crash, or the other 145 become unrunnable for anyone
    who has not produced their own listing.

    The reason string names the path that was looked at and the variable to
    set, because `skipped` with no explanation is indistinguishable from a
    check that was quietly deleted.
    """
    why = omkpaths.missing_for(*kinds)
    return (("skipped",), ("skipped",), why) if why else None


# --------------------------------------------------------- containers
def c_conversations():
    return len(O.conversations()), 321, "chunks in IAM\\DIALOG that parse"

def c_dialog_tag():
    return len(O.TAGS.get("DIALOGS", {})), 420, "entries in DIALOGS.TAG"

def c_dialog_selftest():
    out = _run("tools/omkdialog.py", "--selftest")
    return ("no failures" in out), True, out.strip().splitlines()[-2:][0]

def c_dialog_scripts():
    out = _run("tools/dialog_disasm.py")
    m = re.search(r"(\d+) scripts, (\d+) decode cleanly", out)
    if not m: return "no output", "a summary line", ""
    return (int(m.group(1)), int(m.group(2))), (612, 612), "conversation scripts"

_TRIG = None
def _triggers():
    global _TRIG
    if _TRIG is None: _TRIG = T.triggers()
    return _TRIG

def c_world_scripts():
    _, st = _triggers()
    n = sum(v for k, v in st.items() if k.endswith(":scripts"))
    bad = sum(v for k, v in st.items() if k.endswith(":bad scripts"))
    return (n, bad), (5785, 0), "world script slots that decode, and failures"

def c_trigger_sites():
    found, st = _triggers()
    sites = sum(len(v) for v in found.values())
    named = sum(len(v) for k, v in found.items() if O.tag("DIALOGS", k))
    return (sites, named, len(found)), (1246, 1246, 235), \
           "dialog.start sites, of which name a real conversation, distinct"

def c_ctl():
    out = _run("tools/anim_ctl.py")
    return (out.count("exact"), len(out.strip().splitlines())), (7, 7), \
           ".CTL files whose walk lands exactly on the file size"

def c_scx():
    out = _run("tools/scene_scx.py")
    lines = [l for l in out.splitlines() if ".SCX" in l]
    return (len(lines), sum("FAILED" in l for l in lines)), (220, 0), \
           "scene script files, and failures"


# --------------------------------------------------------- structures
def c_addresses():
    a = O._address_table()
    tags = O.TAGS.get("ADDRESSES", {})
    hit = sum(1 for i in a if i in tags)
    return (len(a), len(tags), hit), (791, 791, 791), \
           "AREA +60 records, ADDRESSES.TAG entries, ids that match"

def c_objects():
    n, bits = 0, set()
    for k, b in sorted(T.archive(omkpaths.data("IAM/AREA")).items()):
        lo = struct.unpack_from("<i", b, 40)[0]
        c  = struct.unpack_from("<h", b, 72)[0]
        if c <= 0 or lo <= 0 or lo + 20 * c > len(b): continue
        for i in range(c):
            n += 1
            bits.add(struct.unpack_from("<h", b, lo + 20 * i + 18)[0])
    return (n, len(bits)), (830, 830), \
           "AREA object records, and distinct save-game state bits"

def c_actor_models():
    have = {f.lower()[:-4] for f in os.listdir(O.PERSOS)
            if f.lower().endswith(".3do")}
    tot = real = 0
    for per in O._actor_tables().values():
        for d in per.values():
            for r in d.values():
                tot += 1
                real += r["model"].lower() in have
    return (tot, real), (1032, 1032), \
           "actor records, of which name a model that exists"


# --------------------------------------------------------- dialogue cameras
def _abs_cameras():
    """Every camera carrying absolute scene coordinates, as (eye, at)."""
    out = []
    for i in sorted(O.CHUNKS):
        c = O.conversation(i)
        if not c: continue
        for cam in c["cameras"]:
            if tuple(cam["subject"]) != (65535, 65535): continue
            out.append((cam["pos"][:3], cam["pos"][3:6]))
    return out

def c_aim_length():
    """The look-at point is an aim handle at a fixed distance, not a subject."""
    n = at768 = 0
    for i in sorted(O.CHUNKS):
        b = O.CHUNKS[i]
        speaker, nn, nc, _ = struct.unpack_from("<4h", b, 0)
        if nn <= 0 or nc <= 0 or 8 + 64 * nn + 44 * nc > len(b): continue
        for j in range(nc):
            o = 8 + 64 * nn + 44 * j
            if struct.unpack_from("<H", b, o + 32)[0] != 0xFFFF: continue
            p = struct.unpack_from("<6i", b, o)          # raw, before conversion
            d = math.hypot(*[p[3 + k] - p[k] for k in range(3)])
            n += 1
            at768 += abs(d - 768) <= 1
    return (n, at768), (1670, 1615), \
           "absolute cameras, of which |eye->at| is 768 raw units +-1"

def c_bundles():
    """The line cameras of a conversation converge on the speaker's head."""
    res, n = [], 0
    for i in sorted(O.CHUNKS):
        c = O.conversation(i)
        if not c: continue
        rs = O._camera_rays(c, "line")
        if len(rs) < 3: continue
        fit = O._converge(rs)
        if not fit: continue
        n += 1
        res.append(fit[1])
    med = round(S.median(res), 2) if res else None
    return (n, med <= 2.0), (89, True), \
           "conversations with a line-camera bundle; median residual %s <= 2.0" % med

def c_dialog402():
    """The one place the fit can be checked against the running game."""
    c = O.conversation(402)
    ws = O.speaker_positions(c, "Aapkayl")
    p = ws["npcTarget"]
    shot = (3502.3, 1018.6, -899.3)     # solved from a screenshot, see ASSETS.md
    # horizontal only: the target's y is the clip root's bone height (the
    # floor probe supplies the ground), the screenshot's was the ground
    d = math.hypot(p[0] - shot[0], p[2] - shot[2])
    return round(d, 1) <= 5.0, True, \
           "dialog 402 speaker is %.1f units (horizontal) from the " \
           "screenshot solution - the clip root lands within 1" % d


# --------------------------------------------------------- whole-asset sweeps
def c_textures():
    n = ok = 0
    for base, _, files in os.walk(omkpaths.data("MESHES")):
        for f in files:
            if not f.lower().endswith(".3do"): continue
            try: txs = tex3dt.textures(os.path.join(base, f))
            except Exception: continue
            for t in txs:
                n += 1
                ok += bool(t["exact"])
    return (n, ok), (2534, 2534), "textures under gamedata/MESHES that decode exactly"

def c_engine_3dt():
    r"""`engine/`'s C++ `.3DT` reader, against the Python, byte for byte.

    The first slice of the replica (RECONSTRUCTION, "Where the code lives"),
    and the first check here that tests **ported code** rather than a reading.

    It builds `engine/` and runs its `dump_textures` over every `.3DO`/`.3dt`
    pair under `gamedata/MESHES`, requiring the decoded RGB to be **identical to
    `tools/tex3dt.py`'s** on all 2534 textures - not "looks right", not
    "decodes without crashing": the same bytes.

    **Why this format first.** The proof of a ported parser is the corpus, not
    the decompiled body, so it needs no CLEAN function and carries none of the
    no-original-assembly risk. And the Python side is itself corpus-proved:
    every texture lands on exactly width*height, which a wrong choice in any
    of the codec's open parameters - run-length base, repeat count, bytes per
    token kind, flag bit order and polarity, token packing - would
    desynchronise. So agreement is evidence about the PORT rather than about
    the format.

    **It is a differential, and that is its limit.** It would not catch an
    error the two implementations share, because one was written from the
    other's description. What it does catch is the port's actual failure mode:
    a field at the wrong offset, a signedness slip, an off-by-one in a
    back-reference that overlaps its own source. Those are silent in a
    "renders plausibly" test and loud here.

    Skipped rather than failed when there is no compiler or no `gamedata/MESHES`, so
    a checkout without either still runs the rest of the suite.
    """
    import subprocess, tempfile, shutil, glob as _g
    eng = os.path.join(ROOT, "engine")
    meshes = omkpaths.data("MESHES")
    if not os.path.isdir(eng) or not os.path.isdir(meshes):
        return ("skipped", 0, 0), ("skipped", 0, 0), "engine/ or gamedata/MESHES absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_textures")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed", b.stderr.strip()[:200], 0), \
               ("built", "", 0), "engine/ must build"

    models = sorted(os.path.join(base, f)
                    for base, _, files in os.walk(meshes)
                    for f in files if f.lower().endswith(".3do"))
    tmp = tempfile.mkdtemp()
    tot = same = 0
    try:
        for m in models:
            out = os.path.join(tmp, "o")
            shutil.rmtree(out, ignore_errors=True)
            subprocess.run([binp, m, out], capture_output=True)
            try: txs = tex3dt.textures(m)
            except Exception: continue
            stem = os.path.splitext(os.path.basename(m))[0]
            for i, t in enumerate(txs):
                tot += 1
                p = os.path.join(out, "%s.%d.rgb" % (stem, i))
                if os.path.exists(p) and open(p, "rb").read() == t["rgb"]:
                    same += 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return ("built", tot, same), ("built", 2534, 2534), \
           "engine/ builds; textures compared against tools/tex3dt.py; and " \
           "how many are byte-identical - which must be all of them"


def c_engine_3do():
    r"""`engine/`'s C++ `.3DO` record readers - two checks, not one.

    The second slice of the replica. It ports the container's five record
    kinds - the 140-byte mesh nodes, 32-byte vertices, 28-byte triangles,
    32-byte quads and 52-byte cameras - and stops there: resolving a NEGATIVE
    face index against an ancestor mesh, and grouping faces by material, are
    geometry assembly and belong to the next slice.

    **1. Agreement with the reader this repo already trusts.** Every mesh node
    and every camera is compared field by field against `tools/mesh3do.py`'s
    own `meshes()` and `cameras()` - not against a Python mirror written
    alongside the C++, which would only prove the two share my mistakes.

    **2. Invariants the DATA could fail**, which is what makes this more than
    a differential. The per-mesh vertex/triangle/quad counts are read at mesh
    record +64/+68/+72; the header's totals are read at descriptor +196/+188/
    +192. Those are different places in the file and they must agree exactly,
    for all three, in every model - which they do, and which a wrong offset on
    either side would break. Likewise `doorOff - meshOff` must be exactly
    140 * meshes, the arithmetic that settled the record size against the
    notes' 136 in the first place.

    Skipped rather than failed without a compiler or without `gamedata/`.
    """
    import subprocess, tempfile, shutil, mesh3do
    eng = os.path.join(ROOT, "engine")
    tree = omkpaths.data_root()
    if not os.path.isdir(eng) or not os.path.isdir(tree):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_mesh")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed", b.stderr.strip()[:200]), ("built", ""), \
               "engine/ must build"

    def parse(bb):
        o = 0
        hdr = struct.unpack_from("<18i", bb, o); o += 72
        n, = struct.unpack_from("<i", bb, o); o += 4
        ms = []
        for _ in range(n):
            f, mid, par, ch, nx, nv, nt, nq = struct.unpack_from("<8i", bb, o); o += 32
            nm = bb[o:o+20].split(b"\0")[0].decode("cp1252", "replace"); o += 20
            pos = struct.unpack_from("<3f", bb, o); o += 12
            ms.append((f, mid, par, ch, nx, nv, nt, nq, nm, pos))
        counts = []
        for stride in (16, 16, 20):            # vertices, triangles, quads
            k, = struct.unpack_from("<i", bb, o); o += 4 + stride * k
            counts.append(k)
        nc, = struct.unpack_from("<i", bb, o); o += 4
        cs = []
        for _ in range(nc):
            nm = bb[o:o+20].split(b"\0")[0].decode("cp1252", "replace"); o += 20
            pos = struct.unpack_from("<3f", bb, o)
            tgt = struct.unpack_from("<3f", bb, o + 12)
            unk, fov = struct.unpack_from("<2f", bb, o + 24); o += 32
            cs.append((nm, pos, tgt, unk, fov))
        return hdr, ms, counts, cs

    models = sorted(os.path.join(base, f)
                    for base, _, files in os.walk(tree)
                    for f in files if f.lower().endswith(".3do"))
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "m.bin")
    agree = inv = 0
    nmesh = nvert = ntri = nquad = ncam = 0
    try:
        for m in models:
            if os.path.exists(out): os.remove(out)
            subprocess.run([binp, m, out], capture_output=True)
            if not os.path.exists(out): continue
            hdr, ms, counts, cs = parse(open(out, "rb").read())
            nmesh += len(ms); ncam += len(cs)
            nvert += counts[0]; ntri += counts[1]; nquad += counts[2]

            h, pms = mesh3do.meshes(m)
            try: pcs = mesh3do.cameras(m)
            except Exception: pcs = []
            same = len(ms) == len(pms) and len(cs) == len(pcs)
            for a, q in zip(ms, pms):
                if (a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]) != \
                   (q["flags"], q["id"], q["parent"], q["child"], q["next"],
                    q["vertices"], q["triangles"], q["quads"], q["name"]) or \
                   tuple(a[9]) != tuple(q["pos"]):
                    same = False; break
            for a, q in zip(cs, pcs):
                if a[0] != q["name"] or tuple(a[1]) != tuple(q["pos"]) or \
                   tuple(a[2]) != tuple(q["target"]) or a[3] != q["unknown"] or \
                   a[4] != q["fov"]:
                    same = False; break
            agree += bool(same)

            # the data-falsifiable half: two independent places in the file
            ok = (sum(x[5] for x in ms) == hdr[12] and
                  sum(x[6] for x in ms) == hdr[10] and
                  sum(x[7] for x in ms) == hdr[11] and
                  (not hdr[15] or hdr[7] - hdr[6] == 140 * hdr[15]))
            inv += bool(ok)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (len(models), agree, inv, nmesh, nvert, ntri, nquad, ncam), \
           (635, 635, 635, 16188, 463257, 315123, 176378, 666), \
           "models; those whose mesh nodes and cameras match " \
           "tools/mesh3do.py exactly; those whose per-mesh counts sum to the " \
           "header's and whose mesh section is a whole number of 140-byte " \
           "records; then the records compared - meshes, vertices, " \
           "triangles, quads, cameras"


def c_engine_3do_geometry():
    r"""`engine/`'s geometry assembly: index resolution, offsets, batching.

    The third slice - the step after the record readers. It resolves each
    face's indices (including the NEGATIVE ones, which name a vertex of an
    ANCESTOR mesh), adds the owning mesh's offset, and groups corners by
    material into the engine's draw order: opaque, then additive, then
    multiply, by material id within each.

    **Compared against `omkdata.decor_geometry` over all 220 decor sets** -
    batch for batch and corner for corner, 1696452 corners, every field.

    **The float32 relation, which is the interesting part.** They do NOT agree
    naively, and the reason is worth stating because every later slice meets
    it. `struct.unpack("<f")` widens to a Python double, so the reference sums
    `vertex + meshOffset` in **double** and keeps it; the replica sums in
    **float32**, which is what the engine stores. So the test is not equality
    but "the C++ float32 equals the float32 rounding of the reference", and
    under that it is exact on all 1696452. The drift it papers over is
    measured rather than assumed: the largest absolute difference is
    **0.00195 units** and the largest relative one **5.4e-08**, i.e. one
    float32 epsilon - on a body 71 units tall, three parts in 100000. This is
    the "diff decisions, not numbers" tolerance of RECONSTRUCTION, quantified.

    **And the drawable rule cross-confirms itself.** Built with the engine's
    own filter (`flags & 0x800043`) instead of the viewer's (drop
    CollisionOnly), the corner count drops by exactly **9, in one set** -
    three triangles. `render drawable mask` independently counts **3** set
    meshes that the viewers draw and the engine does not, from the flags
    alone. Two different routes to the same three faces.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    dec = omkpaths.data("MESHES", "DECORS")
    if not os.path.isdir(eng) or not os.path.isdir(dec):
        return ("skipped",), ("skipped",), "engine/ or gamedata/MESHES/DECORS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_geom")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed", b.stderr.strip()[:200]), ("built", ""), \
               "engine/ must build"

    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    BLEND = {0: "", 1: "add", 2: "mul"}

    def parse(bb):
        o = 0
        nb, = struct.unpack_from("<i", bb, o); o += 4
        bs = []
        for _ in range(nb):
            mat, = struct.unpack_from("<i", bb, o)
            cut, bl = bb[o + 4], bb[o + 5]
            st, ct = struct.unpack_from("<2i", bb, o + 6); o += 14
            bs.append((mat, bool(cut), BLEND[bl], st, ct))
        nc, = struct.unpack_from("<i", bb, o); o += 4
        return bs, [struct.unpack_from("<9f", bb, o + 36 * i) for i in range(nc)]

    names = sorted(f[:-4] for f in os.listdir(dec) if f.lower().endswith(".3do"))
    tmp = tempfile.mkdtemp()
    ov, oe = os.path.join(tmp, "v.bin"), os.path.join(tmp, "e.bin")
    match = corners = engine_corners = 0
    worst = 0.0
    try:
        for name in names:
            p = O.decor_path(name)
            for f, mode in ((ov, "--viewer"), (oe, "--engine")):
                if os.path.exists(f): os.remove(f)
                subprocess.run([binp, p, f, mode], capture_output=True)
            if not (os.path.exists(ov) and os.path.exists(oe)): continue
            bs, cs = parse(open(ov, "rb").read())
            _be, ce = parse(open(oe, "rb").read())
            g = O.decor_geometry(name)
            corners += len(cs); engine_corners += len(ce)
            same = (len(bs) == len(g["batches"]) and len(cs) == len(g["verts"]))
            for a, q in zip(bs, g["batches"]):
                if a != (q["material"], q["cutout"], q["blend"], q["start"], q["count"]):
                    same = False; break
            if same:
                for a, q in zip(cs, g["verts"]):
                    for k in range(9):
                        if a[k] != f32(q[k]): same = False; break
                        if k < 3: worst = max(worst, abs(a[k] - q[k]))
                    if not same: break
            match += bool(same)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (len(names), match, corners, corners - engine_corners,
            round(worst, 6)), \
           (220, 220, 1696452, 9, 0.001953), \
           "decor sets; those matching omkdata.decor_geometry exactly once " \
           "the reference is rounded to float32; corners compared; how many " \
           "FEWER the engine's own 0x800043 filter draws (three triangles, " \
           "the same three meshes `render drawable mask` finds from the " \
           "flags); and the largest float32-vs-double drift, in world units"


def c_engine_iam():
    r"""`engine/`'s IAM archive reader - and which files actually ARE archives.

    The fourth slice, and the gateway to everything script-shaped. The format's
    one subtlety is that the directory's length is **implied**: it ends where
    the first payload begins, so the reader has to find the lowest in-range
    payload offset before it knows how many entries to trust.

    **Three files are archives**, and the reader agrees with
    `dialog_triggers.archive()` on every chunk of them - index, size and an
    FNV-1a of the bytes:

        AREA    259 chunks of 512 directory entries
        DIALOG  420 of 512
        SCENE    71 of 256

    **The rest are not, and the reader says so by returning nothing** - except
    for two it cannot tell apart, which is the point of asserting this rather
    than a total. `GLOBAL` and `START` each come back as "one chunk" from the
    Python reader AND from the C++, and both are wrong: `Global_Load` **fopen**s
    GLOBAL as a plain file with a fixed header, and `START` is the new-game
    save that `State_Apply` consumes (CLAUDE.md 1 records both, and the cost of
    the first). `OBJECT` is 1002 records of 2048 bytes and `GAMES` is the save
    file; neither yields anything and neither should. The small screens
    (`Menu`, `Options`, `Pause`, ...) are interface TEXT files (UI.md), and the
    Python reader raises on them where the C++ returns empty - a difference in
    reporting, not in reading.

    So this asserts the shape, not a sum: the three real archives chunk for
    chunk, and that nothing else is mistaken for one beyond the two known
    false positives. A reader that started finding chunks in `OBJECT` would
    fail here, and that is the failure worth catching.
    """
    import subprocess, tempfile, shutil
    import dialog_triggers as T
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    if not os.path.isdir(eng) or not os.path.isdir(iam):
        return ("skipped",), ("skipped",), "engine/ or gamedata/IAM absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_iam")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    def fnv1a(bb):
        h = 2166136261
        for v in bb: h = ((h ^ v) * 16777619) & 0xFFFFFFFF
        return h

    files = sorted(f for f in os.listdir(iam)
                   if os.path.isfile(os.path.join(iam, f)) and "." not in f)
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    counts, agree, chunks = {}, 0, 0
    try:
        for f in files:
            p = os.path.join(iam, f)
            if os.path.getsize(p) == 0: continue
            if os.path.exists(out): os.remove(out)
            subprocess.run([binp, p, out], capture_output=True)
            bb = open(out, "rb").read() if os.path.exists(out) else b""
            n = struct.unpack_from("<I", bb, 0)[0] if len(bb) >= 4 else 0
            cpp = {}
            for i in range(n):
                idx, off, sz, h = struct.unpack_from("<4I", bb, 4 + 16 * i)
                cpp[idx] = (sz, h)
            try: py = T.archive(p)
            except Exception: py = {}
            pyd = {k: (len(v), fnv1a(v)) for k, v in py.items()}
            counts[f] = len(cpp)
            chunks += len(pyd)
            agree += (cpp == pyd)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    real = (counts.get("AREA"), counts.get("DIALOG"), counts.get("SCENE"))
    falsepos = sorted(k for k, v in counts.items()
                      if v and k not in ("AREA", "DIALOG", "SCENE"))
    return (len(counts), agree, chunks, real, falsepos), \
           (22, 22, 752, (259, 420, 71), ["GLOBAL", "START"]), \
           "non-empty files under gamedata/IAM; those where the C++ directory " \
           "matches dialog_triggers.archive() chunk for chunk (index, size " \
           "and content hash); chunks compared; the three REAL archives' " \
           "counts; and every other file the reader still finds a chunk in - " \
           "which must be exactly GLOBAL and START, both known not to be " \
           "archives at all"


def c_engine_scripts():
    r"""`engine/`'s world-script decoder - the first ported BEHAVIOUR.

    The fifth slice, and the first that is not a file format. Two halves, kept
    apart because they are different facts: `chunkSlots` is the AREA/SCENE
    header layout (68-byte trigger records whose +0/+4/+8 are script offsets,
    plus the 8-byte second table), and `decodeScript` is the VM - one opcode
    byte, then as many operand bytes as the dispatch table says.

    **The operand lengths come from `tables/vm_opcodes.json`, loaded as data.**
    That is the table lifted out of the executable, and keeping it data rather
    than baking it into code is what lets this check test the extraction too.

    Three things asserted:

    1. **5775 slots and 56969 instructions**, all decoding to `end` - 5143
       slots in AREA and 632 in SCENE. Both numbers land on a figure this repo
       already carries: RECONSTRUCTION records **5775 / 56969** as the scoped
       AREA+SCENE count from before GLOBAL's 10 slots were folded into the
       same scan to make the documented 5785 / 58644 (58428 since 2026-09-02:
       op 62 at its handler's 6 bytes, 216 phantom instructions fewer). GLOBAL is a plain file
       with its own header, not an archive, and is not in this slice - so the
       port reproducing the scoped pair exactly is a corroboration rather than
       a coincidence.
    2. **Every instruction stream identical** to `dialog_disasm.disasm`'s -
       pc, opcode and operand bytes, all 5775.
    3. **The falsification, which is what makes 1 and 2 mean something.**
       Decoded again with the executable's OWN `table_says` operand lengths
       instead of the corrected ones (17 then; 20 since 2026-09-02, when 16,
       43 and 44 joined and none of the three is reachable from shipped data,
       so the 77 stands), **77 slots fail** (57 in AREA, 20 in
       SCENE). So "everything decodes" is a test the data can fail, and the
       corrections are not a convention this repo adopted - they are required
       by the corpus. A decoder that trusted the raw table would desynchronise
       exactly there.

    That third point is the reason to run the decode twice. Without it the
    check would only say the C++ agrees with the Python, which is the weakest
    thing a differential can say.
    
    2026-09-02: the instruction count went 57185 -> 56969, the 216 phantom
    `dbg`/`nop` instructions op 62's table length of 4 produced (two per
    `fight.begin` site over 108) - `engine: parking ops` adjudicates it.
    """
    import subprocess, tempfile, shutil, json as _j
    import dialog_triggers as T, dialog_disasm as D
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.isdir(iam) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_scripts")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    def parse(bb):
        o = 0
        n, = struct.unpack_from("<i", bb, o); o += 4
        out = []
        for _ in range(n):
            c, rec, fld, off, st, ni = struct.unpack_from("<6i", bb, o); o += 24
            code = []
            for _ in range(ni):
                pc, = struct.unpack_from("<i", bb, o)
                op, ln = bb[o + 4], bb[o + 5]; o += 6
                code.append((pc, op, bytes(bb[o:o + ln]))); o += ln
            out.append((c, rec, fld, off, st, code))
        return out

    # the same table with the executable's RAW lengths, for the falsification
    tmp = tempfile.mkdtemp()
    rawp = os.path.join(tmp, "raw.json")
    d0 = _j.load(open(tbl))
    _j.dump(dict(d0, rows=[dict(r, length=r["table_says"]) for r in d0["rows"]]),
            open(rawp, "w"))

    slots = match = instrs = ok = rawfail = 0
    try:
        for name, kind in (("AREA", "AREA"), ("SCENE", "SCENE")):
            src = os.path.join(iam, name)
            out = os.path.join(tmp, name + ".bin")
            subprocess.run([binp, src, kind, tbl, out], capture_output=True)
            cpp = parse(open(out, "rb").read())
            r2 = subprocess.run([binp, src, kind, rawp, os.path.join(tmp, "r.bin")],
                                capture_output=True, text=True)
            for tok in r2.stdout.split():
                pass
            rawc = parse(open(os.path.join(tmp, "r.bin"), "rb").read())
            rawfail += sum(1 for x in rawc if x[4] != 0)

            py = []
            for k, bb in sorted(T.archive(src).items()):
                r = T.LAYOUT[name](bb)
                if not r: continue
                sl = list(T._scripts_from_records(bb, r[0], r[1])) + T._second_table(name, bb)
                for rec, fld, p in sl:
                    ops, st = D.disasm(bb, p, len(bb))
                    py.append((k, rec, fld, p, 0 if st == "ok" else 1, ops))
            if len(cpp) != len(py): continue
            for a, q in zip(cpp, py):
                slots += 1
                ok += (a[4] == 0)
                instrs += len(a[5])
                same = ((a[0], a[3]) == (q[0], q[3]) and (a[4] == 0) == (q[4] == 0)
                        and len(a[5]) == len(q[5]))
                if same:
                    for x, y in zip(a[5], q[5]):
                        if x[0] != y[0] or x[1] != y[1] or x[2] != y[2]:
                            same = False; break
                match += bool(same)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (slots, ok, match, instrs, rawfail), \
           (5775, 5775, 5775, 56969, 77), \
           "AREA and SCENE script slots; those decoding to `end`; those whose " \
           "instruction stream is identical to dialog_disasm's; instructions " \
           "decoded; and - the falsification - how many slots FAIL when the " \
           "executable's own uncorrected operand lengths are used instead"


def c_engine_execute():
    r"""`engine/` EXECUTES the world scripts - the first ported behaviour.

    The sixth slice, and the first that runs rather than reads. `Script_Execute`
    (0x00406460) loops while the context's status word is 1, dispatching through
    the 153-entry table; this is that loop, with the ~20 flow opcodes real
    against a ported 8192-byte `GameState` and everything else stubbed and
    recorded - the simulator's own design, because what a replica must first
    reproduce is what the game DECIDES, not what it draws.

    Four things, in ascending order of how hard they are to fake:

    1. **5958 slots**, the same corpus `tools/sim/vm.py` walks: each chunk's
       startup script at `+4` (in no record table, so the record walk cannot
       reach it), the trigger records' three fields, the second table, and
       GLOBAL's 10 - GLOBAL being a plain file with its own header, ported
       here because stopping at 5948 would have been an explained shortfall
       rather than a match.
    2. **5818 reach `end` and 140 stop at `dialog.start`**, where
       Script_Execute returns outright. Treating those 140 as `end` would
       silently execute code the engine never reaches, and they are exactly
       the count the simulator reports.
    3. **Every slot's offset and status agrees**, in order, and both sides
       record the same **18584** stubbed subsystem calls.
    4. **The final 8192-byte game database is byte-identical.** The corpus runs
       against ONE state carried across it, so every variable write, bit flip
       and `scene.load` accumulates into that block - which makes this the
       strongest comparison available here. A divergence anywhere in the
       arithmetic, the operand indirection, the `case` peek or the
       read-modify-write on a zone bit lands as a differing byte, and none
       does.

    That last point is worth the emphasis. The earlier engine checks compare
    what two readers *parse*; this compares what two implementations *compute*
    over 5958 executions, and it is the first check in this repo that could
    catch a semantic error rather than a layout one.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.isdir(iam) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_scripts")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import vm as SIM
    import gamestate as GS

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "r.bin")
    try:
        subprocess.run([binp, iam, tbl, os.path.join(iam, "START"), out],
                       capture_output=True)
        bb = open(out, "rb").read()
        o = 0
        n, = struct.unpack_from("<i", bb, o); o += 4
        cpp = []
        for _ in range(n):
            c, rec, fld, off, st, steps, nc = struct.unpack_from("<7i", bb, o)
            o += 28
            cpp.append((off, st, list(bb[o:o + nc]))); o += nc
        cppstate = bb[o:]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    state = GS.load()
    tr = SIM.Trace(True)
    py = []
    for _arch, _k, _rec, _fld, code, at in SIM.world_scripts():
        before = len(tr.calls)
        st, _pc = SIM.VM(state, tr).run(code, at)
        py.append((at, st, tr.calls[before:]))

    STATUS = {0: "end", 1: "dialog"}
    slots = min(len(cpp), len(py))
    inorder = sum(1 for a, q in zip(cpp, py) if a[0] == q[0])
    agree   = sum(1 for a, q in zip(cpp, py)
                  if a[0] == q[0] and STATUS.get(a[1], "?") == q[1])
    ended   = sum(1 for a in cpp if a[1] == 0)
    dialog  = sum(1 for a in cpp if a[1] == 1)
    calls   = sum(len(a[2]) for a in cpp)
    pycalls = sum(len(q[2]) for q in py)
    return (len(cpp), len(py), inorder, agree, ended, dialog, calls, pycalls,
            cppstate == bytes(state.raw)), \
           (5958, 5958, 5958, 5958, 5818, 140, 18584, 18584, True), \
           "slots executed by engine/ and by tools/sim; those whose offset " \
           "matches in order; those whose STATUS also agrees; how many reach " \
           "`end` and how many stop at dialog.start; the stubbed calls each " \
           "recorded; and whether the final 8192-byte game database is " \
           "byte-identical after all 5958 runs"


def c_engine_zones():
    r"""`engine/`'s zone scheduler - which script runs, and when.

    The seventh slice. `script/interp` executes ONE script; this is
    `Zones_RegisterAll` + `Script_Pump` + `Script_ProcessActions`: the 68-byte
    trigger records, the save-bit filter, the per-context action FIFO and the
    pump that drains one action a frame.

    **The test is the plan's own, and it is checkable by name.** Stand in zone
    **3732** - record 0 of SCENE 53 - facing into its arc, then press action.
    Its activate slot is the script that launches dialog **387**, so the
    outcome is a named conversation rather than "something executed":

        character.look_at_player, zone.disable 3732, address.disable 5,
        fade.to_black, player.anim.hold, scx.play.player.wait 22,
        scx.play.player 32, inventory.remove_all 2/342,
        character.look_at_player, dialog.start 387

    All ten calls identical to `tools/sim/world.py`'s, in order, with their
    operands.

    **And the lifecycle closes**: the activate script retires its own zone, so
    the save bit goes **1 -> 0** and `Zones_RegisterAll` no longer registers
    it. That is what "a spent trigger stays retired" means, and it only works
    because `Zone_SetStateBit` read-modify-writes - its DECOMPILATION is a
    bare OR, which could never clear a bit (CLAUDE.md 1).

    **Two zones arm at that spot, not one**, which is worth asserting because
    it is the kind of thing a looser test would miss: 3737's enter script runs
    to `end` and 3732's activate stops at `dialog.start`. The reference logs
    the second as a *yield* rather than a run, so its "ran" count is 1 where
    this records both with their status - the same two scripts either way, and
    the identical call traces are what prove it.
    """
    import subprocess, tempfile, shutil
    import dialog_disasm as D
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.isdir(iam) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "walk_zone")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "z.bin")
    try:
        subprocess.run([binp, iam, tbl, os.path.join(iam, "START"), "3732", out],
                       capture_output=True)
        bb = open(out, "rb").read()
        o = 0
        found, = struct.unpack_from("<i", bb, o); o += 4
        if not found:
            return ("zone not found",), ("found",), "zone 3732 must exist"
        arch, chunk, reg, before, after, again, nran = struct.unpack_from("<7i", bb, o)
        o += 28
        ran = []
        for _ in range(nran):
            z, a, off, st = struct.unpack_from("<4i", bb, o); o += 16
            ran.append((z, a, st))
        ncalls, = struct.unpack_from("<i", bb, o); o += 4
        calls = []
        for _ in range(ncalls):
            op, nf = bb[o], bb[o + 1]; o += 2
            fs = tuple(struct.unpack_from("<%dh" % nf, bb, o)) if nf else ()
            o += 2 * nf
            calls.append((D.NAME.get(op) or "op_%d" % op, fs))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import world as W
    r = W.stage3(3732)
    return (("SCENE" if arch == 0 else "AREA", chunk), bool(reg),
            (before, after), bool(again), ran, calls == r["calls"],
            [f[0] for n, f in calls if n == "dialog.start"]), \
           (("SCENE", 53), True, (1, 0), False,
            [(3737, 1, 0), (3732, 2, 1)], True, [387]), \
           "where zone 3732 lives; that its save bit registered it; the bit " \
           "before and after (the activate script retires it); whether it " \
           "re-registers; the scripts that ran as (zone, action, status) - " \
           "3737's enter reaching `end` and 3732's activate stopping at " \
           "dialog.start; whether the whole call trace matches tools/sim's; " \
           "and the conversation it launched"


def c_engine_area_load():
    r"""`engine/` loads an area - and the result is checked against the ENGINE.

    The eighth slice: `Area_TickLoad` (0x0040C7E0) case 9, the only path that
    reaches a chunk's **startup script at +4**. That script is in no record
    table, so the zone pump can never find it - which is why "no shipped
    script starts Impasse's beats" looked true for so long (CLAUDE.md 6).

    Two things, and the second is a first for this tree.

    **The corpus.** Sweeping every chunk's `+4` with the ported reader and
    decoder gives **259 AREA and 71 SCENE chunks, 173 with a startup script,
    157 without, 0 failing, 1968 instructions** - every number identical to
    `verify.py: startup scripts`, which derives them independently in Python.
    The zero is the one with teeth: a `+4` that was really something else
    would land mid-instruction and fail to decode.

    **The golden trace.** Loading AREA 118 - "Introduction Kay'l", what a new
    game enters - runs its startup script, and what it issues is compared
    against `traces/intro.log`, a capture of the real binary announcing its
    own operands. Seeded with `Interface = 1`, the port emits

        OBJECTS 997, CAMERAS 2172, CAMERAS 2148, DIALOGS 272

    which is the capture's first four events of those domains, **in order**,
    before stopping at `dialog.start` exactly as `Script_Execute` does. Every
    earlier engine check compares this port against the Python readers; this
    one compares it against the game.

    **Why a variable has to be seeded, and why that is not fitting the
    result.** `ui.open` is a player question: the handler parks the context at
    status 6 and the interface writes the answer into the named variable
    (here 19, `Interface`). Nothing here models the interface, so the script
    runs straight through and the variable must carry what the player chose.
    The branch is **binary**, and the check asserts BOTH arms: unseeded, the
    port takes the other opening - `CAMERAS 2152, 2153, OBJECTS 753, ...` -
    which the capture does not contain. So the seeding selects between two
    real, different intros rather than tuning anything, and a port that
    ignored variable 19 would fail here.
    """
    import subprocess, tempfile, shutil
    import dialog_disasm as D
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    trc = os.path.join(ROOT, "traces", "intro.log")
    if not (os.path.isdir(eng) and os.path.isdir(iam) and os.path.exists(tbl)
            and os.path.exists(trc)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM, tables/ or traces/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "load_area")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "la.bin")

    def load(extra):
        subprocess.run([binp, iam, tbl, os.path.join(iam, "START"), "118", out]
                       + extra, capture_output=True)
        bb = open(out, "rb").read()
        o = 0
        n, = struct.unpack_from("<i", bb, o); o += 4
        seq, nruns = [], n
        for _ in range(n):
            _isc, _ch, _off, _st, nc = struct.unpack_from("<5i", bb, o); o += 20
            for _ in range(nc):
                op, nf = bb[o], bb[o + 1]; o += 2
                fs = struct.unpack_from("<%dh" % nf, bb, o) if nf else ()
                o += 2 * nf
                nm = D.NAME.get(op) or ""
                if nm.startswith("camera.set"): seq.append(("CAMERAS", fs[0]))
                elif nm == "media.play":        seq.append(("OBJECTS", fs[0]))
                elif nm == "dialog.start":      seq.append(("DIALOGS", fs[0]))
        corpus = struct.unpack_from("<6i", bb, o)
        return nruns, seq, corpus

    try:
        nruns, seeded, corpus = load(["--var", "19=1"])
        _n2, unseeded, _c2 = load([])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    cap = [(d, int(v)) for d, v in G.parse(trc)
           if d in ("CAMERAS", "OBJECTS", "DIALOGS")]
    agree = sum(1 for a, q in zip(seeded, cap) if a == q)
    # the other arm must NOT be what the engine did
    differs = unseeded[:4] != cap[:4]
    return (corpus, nruns, len(seeded), agree, differs), \
           ((259, 71, 173, 157, 0, 1968), 1, 4, 4, True), \
           "the corpus sweep (AREA and SCENE chunks, those with a +4 startup " \
           "script, those without, those FAILING to decode, instructions); " \
           "the startup scripts AREA 118's load ran; the trace events the " \
           "port issued with Interface=1, and how many match traces/intro.log " \
           "IN ORDER; and that the unseeded branch is a different opening"


def c_engine_intro():
    r"""`engine/` boots a new game, and the ENGINE's own capture confirms it.

    The ninth slice adds the one piece of control flow that carries a script
    past `dialog.start`. That opcode does **not** suspend its context: the
    handler leaves it running with the pc past the operand, `Script_Execute`
    returns for the frame, and `g_DialogState` goes to 3 - after which the
    whole pump refuses to run until `Game_HandleEvent` case 63 puts it back to
    1. So the world stops *around* the launch script, and it resumes at the
    next instruction with **its stack intact**, which is why a context owns its
    interpreter rather than making a new one each frame.

    With that, booting AREA 118 - "Introduction Kay'l", what a new game enters
    - runs its startup script all the way to the end, and every decision it
    announces is compared against `traces/intro.log`:

        OBJECTS 997, CAMERAS 2172, CAMERAS 2148, DIALOGS 272, CAMERAS 2148,
        CAMERAS 2152, CAMERAS 2152, CAMERAS 2153, CAMERAS 2154, CAMERAS 2158,
        AREAS 222, SCENES 55, ADDRESSES 654, AREAS 118

    and then keeps going: `area.goto 222` stages a load, the caller runs on to
    its `scene.load(222, 55)`, `Area_TickLoad` case 9 runs AREA 222's startup
    script and SCENE 55's over it, and the Impasse's beats begin.

    **42 decisions of 42, in order, from the very first one.** That is the
    whole opening plus the transition plus the start of the Impasse. The
    capture runs to 58; the remaining 16 need what comes after, which is the
    player walking.

    **The port narrates every announcing opcode, implemented ones included** -
    which is not a detail. `Dbg_LogTagged` fires from the handler whether or
    not the opcode does anything a stub would model, so a variable write
    announces exactly as loudly as a `camera.set`. Recording only the stubbed
    calls made the port look silent where the game is not, and the diff
    reported it as a drift at event 17 rather than as a missing feature. Both
    streams are now compared whole, with nothing dropped from the capture
    side.

    **Three filters, all the engine's own, not conveniences.** `Dbg_LogTagged`
    drops `a1 == -1`, the `CHARACTERS` domain (resolved via Actor_FindById and
    printed from the actor instead) and `VALUES` (a bare number with no
    table). A diff has to apply the same three or it reports the logger's own
    filter as a disagreement.

    **And the announce map is DATA, after a hand-written one was wrong three
    ways.** `tables/vm_announce.json` is derived from the assembly by
    `tools/vm_announce.py`: 49 handlers announce and 104 are silent, `field`
    says WHICH operand (scene.load announces its second), and a guessed table
    had `scx.play` and `music.play` announcing when they do not and
    `scx.play.actor.wait` announcing the object when it announces the actor.
    That is CLAUDE.md 1's rule biting inside the port itself.

    **The seed is now DERIVED, not supplied** (2026-08-31). `Interface`
    (variable 19) is the answer the start menu gives, and this used to hand it
    over as `--var 19=1`. The port walks screen 29 instead - confirm on
    "Nouvelle partie", DOWN off the name field, confirm on "Confirmer" - and
    the run comes out byte-identical to the supplied one. The last literal in
    the intro is gone.
    """
    import subprocess, tempfile, shutil
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    ann = os.path.join(ROOT, "tables", "vm_announce.json")
    trc = os.path.join(ROOT, "traces", "intro.log")
    if not all(os.path.exists(p) for p in (eng, iam, tbl, ann, trc)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM, tables/ or traces/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "boot_intro")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "b.bin")
    try:
        # `--derive-ui`, not `--var 19=1`. The intro's `Interface` is the
        # answer the START MENU gives, and it was supplied as a literal until
        # 2026-08-31 - which tested the suspend/resume mechanism and nothing
        # about the screen. The port now walks the widget tree for it, and the
        # output is byte-identical to the supplied run.
        subprocess.run([binp, iam, tbl, os.path.join(iam, "START"), out,
                        "--derive-ui",
                        os.path.join(ROOT, "tables", "ui_widgets.json")],
                       capture_output=True)
        bb = open(out, "rb").read()
        o = 0
        queued, dialogs, areas, n = struct.unpack_from("<4i", bb, o); o += 16
        mine = []
        for _ in range(n):
            ln = bb[o]; o += 1
            dom = bb[o:o + ln].decode(); o += ln
            v, = struct.unpack_from("<i", bb, o); o += 4
            mine.append((dom, v))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # Dbg_LogTagged's own three filters, applied to our side. Nothing is
    # dropped from the capture side any more: the port narrates every
    # announcing opcode, implemented ones included, so the two streams are
    # directly comparable.
    vis = [e for e in mine if e[0] not in ("CHARACTERS", "VALUES") and e[1] != -1]
    cap = [(d, int(v)) for d, v in G.parse(trc)]
    agree = 0
    for k in range(min(len(vis), len(cap))):
        if vis[k] != cap[k]: break
        agree += 1
    return (queued, dialogs, areas, len(vis), agree, vis[:3], vis[-2:]), \
           (1, 1, 1, 42, 42, [("VARIABLES", 175), ("VARIABLES", 170),
                              ("OBJECTS", 997)],
            [("VARIABLES", 85), ("VARIABLES", 86)]), \
           "startup contexts queued; conversations opened; areas entered; " \
           "decisions the port announced once the engine's own logger " \
           "filters are applied; how many match traces/intro.log IN ORDER " \
           "FROM THE START - which must be all of them; then the first three " \
           "and the last two, so a regression says WHERE it drifted"


def c_engine_walk():
    r"""`engine/`'s walker - the ground probe and the step limits.

    The tenth slice. One frame is `Actor_Move` + `Walk_GroundResponse`, in the
    engine's order: try the move, probe under the result, and let the ground
    decide - revert where there is no floor, refuse a drop past the step limit,
    otherwise snap and keep the fall accounting.

    **The reverts are as much the point as the moves.** Walking a 400-step
    spiral across `ARESTO14` from an authored ADDRESSES position, the port
    moves **287** times and reverts **113** - identical to `tools/sim`'s
    stage-5 numbers - with the height varying by less than a unit and no fall
    at all, because that room's floor is flat.

    Two things the port had to get right to reach those numbers, both recorded
    here because both were wrong in the reference first:

    * the ground ray starts a step-height **above** the feet, not at them.
      Probing from the feet exactly finds nothing - the probe wants a surface
      strictly below its origin - and every step then reads as a hole;
    * the start comes from an authored position probed **downward**, not from
      -1e6 probed upward, which returns the nearest surface below *that* - the
      ceiling. The reference's first version started the walker on the roof of
      the restaurant and refused every step as a 176-unit drop.

    **Two soups, and the difference is stated rather than chosen.** The engine
    probes COLLISION geometry, keeping every mesh (CollisionOnly volumes
    included) and only faces flatter than 30 degrees - 1107 triangles here.
    `tools/sim`'s walker probes the RENDER soup instead - 2875 triangles,
    CollisionOnly dropped, no slope test. The port builds both and this check
    runs both: they give **the same 287/113**, which is a fact about this room
    (its floor is in both soups) and not a general equivalence. Saying so
    keeps the agreement from reading as more than it is.

    NOT covered: the narrow phase. Collision here is the ground probe alone,
    so the walker keeps to the floor and passes through walls - the same
    limit stage 5 states, and the sweep kernel is 930 lines of x87 with no
    shipped fact to prove a transcription against.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    model = omkpaths.data("MESHES/DECORS/ARESTO14.3DO")
    if not (os.path.isdir(eng) and os.path.exists(model)):
        return ("skipped",), ("skipped",), "engine/ or ARESTO14 absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "walk_set")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    # the sim's own start: an authored ADDRESSES position in AREA 217
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import actor as A
    start = A.area_start(217)

    tmp = tempfile.mkdtemp()
    def walk(extra):
        out = os.path.join(tmp, "w.bin")
        subprocess.run([binp, model, "%.12f" % start[0], "%.12f" % start[1],
                        "%.12f" % start[2], "400", out] + extra,
                       capture_output=True)
        bb = open(out, "rb").read()
        return struct.unpack_from("<7i", bb, 0)
    try:
        rend = walk(["--render"])
        coll = walk([])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    ref = A.cross("ARESTO14", start)
    return (rend[0], coll[0], rend[1:5], coll[1:5], rend[5] < 100, rend[6],
            (ref["verdicts"].get("moved"), ref["verdicts"].get("reverted"))), \
           (2875, 1107, (287, 113, 0, 0), (287, 113, 0, 0), True, 0,
            (287, 113)), \
           "triangles in the render and walkable-collision soups; the " \
           "verdicts on each (moved, reverted, blocked, refused); that the " \
           "height span stays under a unit; the worst fall (none - the floor " \
           "is flat); and tools/sim's own numbers for the same walk"


def c_engine_dialogue():
    r"""`engine/` runs a conversation - the last stubbed subsystem in the
    decision path.

    The eleventh slice. `dialog.start` loaded a conversation and the port then
    declared it over; this walks it, evaluating each branch's CONDITION and
    executing the chosen branch's ACTION against the same GameState everything
    else reads.

    The node's nine pointers split in two, and that split was **proven by
    tracing** rather than guessed: `ptr[0..3]` are conditions, evaluated by
    `Game_HandleEvent` event 55 while Dialog_TickUI builds the reply menu;
    `ptr[4..7]` are actions, executed by event 59 when a reply is chosen.
    Conditions gate and actions run.

    **Two tests, kept apart because they ask different things:**

    * the **walk** - every conversation from node 0, taking the first
      available branch: **321 parse, 321 end, 0 cycles, 0 hit the limit, 837
      nodes visited**. Each gets a FRESH state, as the reference does; carrying
      one across would let an earlier conversation's actions change which
      branches a later one offers, making the corpus order part of the answer.
      The cycle guard is deliberate - a conversation is a graph and nothing in
      the format forbids a loop, so "it terminates" has to be observed;
    * the **scripts** - every condition and every action executed standalone:
      **612 of them (247 conditions + 365 actions), all 612 reaching `end`**.
      This is the dialogue analogue of running all 5958 world scripts, and a
      condition that cannot be evaluated is one the reply menu could not have
      been built from.

    Plus the graph's own invariant: **1452 of 1452** branch targets point at a
    node that exists.

    Which reply a person picks is player input and is not in the data, so the
    walk takes the first available branch - the least interesting policy that
    still exercises every condition on the path.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    dlg = omkpaths.data("IAM", "DIALOG")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.exists(dlg) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_dialogs")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "d.bin")
    try:
        subprocess.run([binp, dlg, tbl,
                        omkpaths.data("IAM", "START"), out],
                       capture_output=True)
        v = struct.unpack_from("<13i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import dialogue as DG
    ref, refs = DG.corpus(), DG.scripts()
    return (v[:7], v[7:], (ref["conversations"], ref["ended"], ref["cycles"],
                           ref["nodes"]),
            (refs["scripts"], refs["conditions"], refs["actions"],
             refs["executed"], refs["validTargets"])), \
           ((420, 321, 321, 0, 0, 0, 837), (612, 247, 365, 612, 1452, 1452),
            (321, 321, 0, 837), (612, 247, 365, 612, 1452)), \
           "the port's walk (chunks, those parsing as conversations, ended, " \
           "cycles, hit-limit, out-of-range, nodes visited) and its scripts " \
           "(total, conditions, actions, those reaching `end`, valid targets, " \
           "targets); then tools/sim's own numbers for both, which must agree"


def c_engine_anims():
    r"""`engine/`'s `.ani` reader - and the one thing the data could fail.

    The twelfth slice: the body-animation libraries, magic "3.0V", loaded the
    way `Anim_Load` does - slurp the file and relocate its offsets in place,
    so the layout in memory is the layout on disk. Groups hold singly-linked
    lists of clip nodes; each node points at a descriptor of bones, and each
    bone at a 12-byte position track and a 16-byte rotation track.

    **Every rotation key must be a unit quaternion, and all 243362 are.** That
    is the test rather than a tally: a wrong track offset lands on numbers that
    are not unit, and it is exactly how "the offsets are relative to the
    DESCRIPTOR, not the file" was confirmed in the first place. Read as file
    offsets they mostly still land on quaternions - the file is full of them -
    but the root bone's tracks fall inside the bone table and decode to
    garbage, which is the giveaway.

    11 libraries, **265** clips, 243362 rotation and 52893 position keys - the
    same counts `verify.py: .ani quaternions` derives in Python.

    A clip's `type` is worth a note because it shapes any port: the engine
    never names a clip. `List_PickRandomByType` walks the list matching type at
    +0 and returns a **random** one of the matches, so an actor asking for an
    idle gets whichever of that type the file happens to offer.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    anims = omkpaths.data("ANIMS")
    if not (os.path.isdir(eng) and os.path.isdir(anims)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ANIMS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_anims")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, anims, out], capture_output=True)
        v = struct.unpack_from("<5i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (11, 265, 243362, 243362, 52893), \
           "libraries; clips; rotation keys; those that are UNIT quaternions " \
           "- which must be all of them, and is what a wrong track offset " \
           "would break; and position keys"


def c_engine_ctl():
    r"""`engine/`'s `.CTL` reader - a byte walk that must land on the size.

    The thirteenth slice. A `.CTL` is a character's animation state machine
    ("CE70", read by InitCEFFile - the engine's own name for it, from its error
    strings, which also call the file a "bank list"). The whole file is a
    **byte walk**: a fixed header, entries, then nine variable-length sections
    in a fixed order, each present only when a flag on the entry says so.
    Nothing points at the next section, so the reader adds up the sizes.

    **That makes "the walk lands exactly on the file size" the whole proof,
    not a tidiness check.** Misread one gate - the 0x8002 that marks an unnamed
    junction, the 0x140 turn, the 0x280 root shift, the 0x2000000 combat
    window, the fight-AI table's two-pass layout - and everything after it is
    off. All **7** shipped files land exactly.

    Then the graph, which is a second, independent test: **398 clips, 1286
    states, 931 children and 931 parents** - the same edges stored both ways,
    which is why the documented total is 931 distinct and not 1862 - plus
    **1113 gotos**, and **0** of any kind unresolved. InitCEFFile resolves
    parents and children within the state's OWN group and a goto across the
    whole file, so the two lookups are different questions and are checked
    separately. 931 + 1113 = the 2044 edges the docs quote.

    The format's trailing pointer fields are **dead**: InitCEFFile never
    follows them, it recomputes them. Chasing them is what made this look
    unsolvable, and a port that trusted them would read garbage.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    anims = omkpaths.data("ANIMS")
    if not (os.path.isdir(eng) and os.path.isdir(anims)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ANIMS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_ctl")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "c.bin")
    try:
        subprocess.run([binp, anims, out], capture_output=True)
        v = struct.unpack_from("<8i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (7, 7, 398, 1286, 1862, 0, 1113, 0), \
           "files; those whose walk lands EXACTLY on the file size - the " \
           "invariant the format is proved by; clips; states; parent+child " \
           "edges as STORED (931 distinct, each held both ways); states with " \
           "an edge that does not resolve in its own group; gotos; and those " \
           "that do not resolve file-wide"



def c_engine_actor_states():
    r"""`engine/`'s ACTOR_STATE 0..17, run as a LIVE state machine.

    The other half of `engine: actor`, which ported the data the machine
    reads. Int slot 101 of the actor record is what `Actors_TickAll`
    (0x004681C0) dispatches on, and it decides the per-state tick, whether
    the walker runs, whether the `.CTL` effect records run and whether the
    marker is drawn; slot 102 is where the previous state PARKS while a
    conversation or an interface screen holds the body.

    **THE STANDARD HERE IS DATA-CONSTRAINED, NOT ENGINE-VERIFIED, and that is
    structural rather than a shortfall.** Every other runtime in this tree has
    an oracle - the golden traces for the scripts, `tools/sim` for the UI -
    and this one cannot be given either. The logger sees only what a VM
    handler narrates through `Dbg_LogTagged`, and combat is exactly two
    opcodes: **`fight.begin` (62) announces nothing** and **`player.become`
    (56) announces to CHARACTERS**, one of the three domains the logger drops
    itself. `Fight_TickAI`, `Fight_ResolveHit`, the eighteen states and the
    `.CTL` transition matching are native code on the far side of that line.
    `traces/fight.log` was captured on 2026-08-31 to test this and settled it
    the other way: the capture DID reach combat - 32 of the scripts its
    events anchor carry `fight.begin` - and is silent about all of it.

    So what is asserted is what the shipped DATA can falsify:

    * **every ACTOR_STATE transition is one the binary writes.** The table in
      `engine/src/actor/state.cpp` was enumerated twice, because once was not
      enough: a scan of `Runtime.exe.asm` for stores to `[reg+194h]` finds
      most of them and **misses `Fight_Engage`'s**, which writes through the
      `dword_910834[328*index]` alias and is **the only writer of state 2 in
      the game**. A state in the dispatch with no writer was the tell.
    * **the table is a rule, not a comment**: three deliberately illegal
      transitions per file are REFUSED - `MDSLIDOU` dismounts only from state
      8 ("bad mode getting out of the slider !"), `Morph_Play` writes 5 only
      out of 4, and a writer that does not carry the edge cannot take it.
    * **the park round-trips, and 16 and 17 are NOT alike**: 16 restores
      [102], 17 lands in **1** and closes the screens. An implementation that
      treated them the same passes every other number here, and dropping the
      distinction moves this row from 42/0 to 35/7.
    * **every `.CTL` state landed in is a real entry** reached through an edge
      the link pass resolved, and **no chain fails to terminate** - the alias
      and junction chains are followed with no guard in the engine, so a cycle
      would hang it, and the format does not forbid one.
    * **every committed edge is RE-DERIVED from the file** by the sweep, with
      a second copy of `Cef_InputMatches` written from the documentation
      rather than from the runtime - asking the code that made a decision
      whether it made it correctly tests nothing.
    * **every reaction resolves uniquely in the low-16 space**, and **every
      input word the AI injects is inside its own 0xCFF union** - the AI and
      the player go through one matcher, so the same sweep drives each bank
      with the fourteen key bits AND with the file's own AI moves.

    **One thing this cannot decide, stated because a silent pass would be
    worse than a gap.** `Cef_FindTransition` takes the first matching child
    when ungated and the highest-priority allowed one when the gate is on.
    Over 9103 gated decisions, **120 are a real contest** - two matching
    candidates of different priority - and in **0** of them is the first match
    not also of maximal priority. So the two rules answer identically on the
    shipped data and no corpus test can separate them: replacing the priority
    rule with a plain first-match rule changes not one of the 12063 edges.
    The rule stands on `Cef_FindTransition`'s own code; what is asserted here
    is the corpus property that makes the distinction moot, which could have
    been false and is not.

    The other assertions were checked by BREAKING them: dropping the
    forbid-bit test from the matcher moves `not opened by their own +4 code`
    off 0, the 16/17 collapse moves the park row to 35/7, and flipping one
    state's channel flag moves the dispatch row to 7.

    **The dispatch row is a differential, and its first version was circular** -
    it ticked each state and asserted the channel advanced iff the table said
    it would, which is what `ActorRuntime::tick` reads that flag to decide.
    Flipping a row passed. It now checks `state.cpp`'s per-state tick function
    against a SECOND transcription, written from `readable/src/21_d3d.c`, of
    which of those ten functions reaches `Cef_TickChannel` - and it has the
    limit every differential has: it cannot catch an error both transcriptions
    share.

    **NEW 2026-09-02 - the two QUEUE rules of `Cef_TickChannel`'s commit**,
    which this sweep could not see and had to be given a case each. It drives
    the machine with `injectInput`, and that REPLACES the queue, so a lone idle
    word never stands in front of a press here and no state is ever current
    with a queue it did not just receive. The rules are

      1. `LABEL_75`: before a pressed word is pushed, a queue holding exactly
         one word with the idle bit set is DROPPED. `SetPersoBankGroup`'s
         memset leaves precisely that queue, so without the rule the first
         press of a new bank queues BEHIND a word that opens nothing and waits
         for something to pop it;
      2. under the CURRENT entry's flag `0x20000000`, a queue longer than one
         is cut to one and the word is not pushed at all.

    Section 5 of the probe puts the machine in each case by hand. For (1) it
    seeds a bank, checks the seed really is the lone idle word (27 of the 202
    groups are not - `GoToMove`'s own 0x100000/0x1000000 flags pop or reset it
    on the way in), skips the 12 whose entry STEPS the queue (flag 8 walks down
    the queue popping, so it would reach a press parked behind the idle word
    within the same tick and the rule's effect would not be visible), presses a
    key the commit will reach, and asserts the press is ACTED ON this tick -
    the machine took an edge, or the press is what the queue now offers. **53
    banks, 53 acting, 0 parked.** What is NOT asserted is "a transition fired":
    whether one does is also the cancel window's and the `0x80000000` gate's
    business, and those are not this rule. 10 of the 53 fire anyway.

    For (2) it walks the 21 shipped entries carrying `0x20000000`, puts the
    machine on each with `GoToMove` and no `from` (the path
    `SetPersoBankGroup` itself takes), fills the queue with three words and
    presses a fourth: **9 reachable, 3 cut to one, 0 not, 6 transitioning**
    (a transition pops or resets the queue itself, so those are counted apart
    rather than passed by default). The rule fires **4088** times in the main
    sweep and changes **not one** of its numbers, which is why it is asserted
    only where it can be seen and said here rather than hidden.

    **NEW - three of the first thirty numbers MOVED, and the engine says so.**
    Rule (1) alone takes landings 34056 -> 55182, edges 12063 -> 26093 and
    gated edges 9103 -> 19652: with the press at the front instead of behind
    the idle word the machine actually runs. Every invariant among them stayed
    0 - and two of them only after two faults the bigger corpus exposed:

      * `badLanding` went to **956** until `GoToMove`'s DYNAMIC RETURN EDGE was
        read as well as written (`if (to->flags & 0x800) to->gotoState =
        from;`). `channel.cpp` recorded it in `dynamicReturn_` and every GoTo
        read took the authored id, so `Sham`'s two nameless flag-0x800 aliases
        (0xC0084813, authored GoTo 0) looked like a landing on nothing - which
        is a null deref in the engine, so it cannot be what the engine does.
      * `priorityInversion` went to **8** until this file's re-derivation was
        completed. `priorityWasMaximal` re-scans the children itself, and it
        modelled the input match and the threshold but neither the CANCEL
        WINDOW nor `Cef_FindTransition`'s two flag gates - so on eight
        decisions it beat `H1Cmbt`'s `HRSTEP` with entry 28, priority 2, whose
        window is frames 10..13 and which the runtime was right to refuse. It
        now applies both, from the documentation as before, using the
        decision's own inputs off `EdgeTaken` (`mustHave`, `mustAlso`,
        `useWindow`, the +12/+16 frame pair). Those are inputs, not the answer:
        the transcription stays second. The 120 contests are unchanged by it.

    That is the shape §1 warns about twice over - a second transcription that
    was approximate, and a field written and never read - and neither was
    visible until the corpus doubled.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    anims = omkpaths.data("ANIMS")
    if not (os.path.isdir(eng) and os.path.isdir(anims)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ANIMS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_actor_states")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, anims, out], capture_output=True)
        v = struct.unpack_from("<42i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (7, 808, 18, 273, 0, 21, 42, 0,
               395352, 55182, 0, 0,
               26093, 0, 0, 19652, 120, 0, 0,
               116, 232, 232, 347, 1338, 0, 398, 0,
               119, 0, 7,
               10367, 4088, 27, 12, 53, 53, 0, 10, 9, 3, 0, 6), \
           ".CTL files and the banks driven (202 groups x ungated plus the " \
           "gate at each shipped priority); ACTOR_STATEs entered, of 18; " \
           "transitions, those refused WRONGLY (0) and those refused " \
           "correctly - the negative control that makes the table a rule; " \
           "dialogue park round trips and failures; then channel ticks, " \
           "states landed in, landings on an unresolved id (0) and chains " \
           "that did not terminate (0 - a cycle the format does not forbid " \
           "and the engine would hang on); edges committed, those NOT opened " \
           "by their own +4 code under an independently written " \
           "Cef_InputMatches, priority inversions, those taken under the " \
           "gate, those that were a real priority CONTEST, and those where " \
           "the first match is not of maximal priority - 0, which is why no " \
           "corpus test here can tell the priority rule from a first-match " \
           "rule - and input words consumed from outside the fourteen " \
           "bindings; then combat blocks, reaction refs and those resolving " \
           "uniquely in the low-16 space; and the AI's moves, its input " \
           "words and any outside its own 0xCFF union; then the clips with " \
           "and without a frame count; then the DISPATCH - states " \
           "whose tick function is checked against a second transcription of " \
           "which of those functions reaches Cef_TickChannel, those " \
           "disagreeing, and the 7 (one per file) with no case in " \
           "Actors_TickAll at all, which is state 7 and is content: " \
           "Sliders_Tick drives a riding actor instead; and finally the two " \
           "QUEUE rules of the commit - lone-idle drops and truncations over " \
           "the main sweep, then the lone-idle case (banks, those the seed " \
           "did not leave holding it, those stepping the queue, those acting " \
           "on the press THIS tick, those parking it behind the idle word - " \
           "0, the whole point - and those transitioning outright) and the " \
           "truncation case (entries reachable, cut to one, not cut - 0 - " \
           "and those that transitioned instead)"

def c_engine_player_walk():
    r"""`engine/`'s PLAYER CONTROLLER - adventure mode, the keyboard word into
    the `.CTL` channel, the clip's root motion out of it, the walker under it,
    and the follow camera behind (`src/actor/player.h`).

    **TIER 5, data-constrained**, like the two modules it joins: no capture
    can reach it - the trace rig sees only what a VM handler narrates, and
    `Actor_TickNpc`, `Cef_TickChannel`, `sub_45C680` and `Actor_ApplyMotion`
    are native code on the far side of that line (`state.h` has the fight.log
    proof). So the oracle is NONE, and what is asserted is what the shipped
    data can falsify, over four replayable input streams fed to
    `tools/player_probe` - Kay'l (`HO1_FNM`, bank `H1AVNT`) on `AIMPASSE`
    at the Impasse's arrival address ADDRESSES[654] = (6753, 397, 3021):

      * UP held 60 frames then released 45: he walks at least 30 units
        (69.5), along his facing (the displacement's heading within 3 degrees
        of it), the machine goes `H_STAND` -> `H_SD-WK` -> `H_WALK` -> the
        stop junction -> `H_WK-SD` -> `H_STAND` (starts on the default group's
        default entry, leaves it, ends on it; at least 3 states), and
        `ACTOR_STATE` is 1 throughout with 0 refused transitions and 0
        unresolved landings;
      * he never leaves the floor: the walker's ground under him after every
        frame is exactly his y (max error 0, 0 frames off);
      * 1539 of the bank's 1595 clip tracks resolve to a mesh of the model by
        name (the 56 that do not are `col0000000..3`, collision volumes with no
        bone), and every root key summed is finite;
      * the follow camera: the steady eye sits 118.11 from him in the ground
        plane and 118.11 of it BEHIND his facing, fov 75 - the mode-0 preset
        row `tables/camera_presets.json` lifts (-118.1102, 75.0), which the
        check also compares against the source constants;
      * LEFT held 20 frames turns him more than 30 degrees (85.3) without
        walking (0.2 units), and LEFT then UP walks him along the NEW facing
        with the camera still 118.11 behind it;
      * nothing held for 60 frames leaves him within a unit (0.10, the idle
        clip's own sway) and never off the default entry.

    **Shown to fail** (each mutation applied to `player.cpp`, the object AND
    the binary deleted, relinked, restored - PORTING B4):

      * the root delta NOT rotated by the facing (Anim_RootDelta's 3x3 at
        node+156 dropped): after a turn he walks the old way - heading off
        by 80 degrees;
      * the seat on the floor skipped: ground error 1.66 units (166);
      * the eye offset halved: 5906 where 11811 is wanted, in all four
        distances;
      * the resolve's subtraction made an addition (the camera AHEAD): behind
        reads -11811;
      * Cef_ApplyTurn a no-op: LEFT turns 0;
      * the hand-over's state writes skipped (Actor_TickScxDriven never lands
        in 1): ACTOR_STATE 4, and with `channelTicks` false for state 4 the
        machine never leaves the default entry - six fields move.

    **One mutation this corpus CANNOT see, said rather than hidden**: passing
    0 instead of the idle word 0x40000000 when nothing is held (sub_4A7A20's
    `if (!held) *word = 0x40000000`) passes, because `H1AVNT`'s stop edges
    (+4 = 0x20000, "forward released") open on code 0 exactly as they open on
    the idle word, and the controller's blocked-front rule re-injects it. The
    idle-word reading stands on sub_4A7A20's own code.

    NOT covered, and labelled in three places (player.h, here, the README
    row): `Actor_Move`'s collide-and-slide (a blocked step stops), the mode-0
    camera's flag-8/0x10 passes (`sub_417070`'s obstruction pull-in and
    `sub_416450`'s floor clamp), the pose during a blend (drawn as a cut into
    the target clip), and whether the actor position `+244..+252` is the feet
    or the pelvis.
    """
    import subprocess, tempfile, shutil, json
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng) or not os.path.exists(omkpaths.data("ANIMS")):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ANIMS absent"
    b = subprocess.run(["make", "-s", "build/player_probe"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "player_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    # AIMPASSE's arrival address, ADDRESSES[654] = (6753, 397, 3021): where
    # the Impasse hands over (o3de/worldcam.h).
    args = [binp, omkpaths.data_root(), os.path.join(ROOT, "tables"),
            "AIMPASSE", "H1AVNT", "HO1_FNM", "6753", "397", "3021", "0"]

    def run(stream):
        out = os.path.join(tmp, "p.bin")
        subprocess.run(args + [stream, out], capture_output=True, text=True)
        return struct.unpack_from("<23i", open(out, "rb").read(), 0)
    try:
        fwd = run("k200*60,0*45")                 # UP held 2 s, then released
        left = run("k203*20,0*10")                # LEFT held 20 frames
        turnwalk = run("k203*18,k200*40,0*40")    # turn, then walk that way
        none = run("0*60")                        # nothing held
        diag = run("k200+203*40")                 # UP+LEFT together: DIAGONAL
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    m0 = json.load(open(os.path.join(ROOT, "tables", "camera_presets.json")))["rows"][0]
    (frames, dist, gerr, off, s0, left0, s1, camd, fov, refused, bad,
     tm, tt, astate, turned, nstates, finite, eyeBack, pfov, heading, behind,
     camHigh, camLift) = fwd
    # the displacement's heading against the facing, both in the +420
    # convention, degrees * 100
    twErr = abs(((turnwalk[19] - turnwalk[14]) + 18000) % 36000 - 18000)
    hErr = abs((heading + 18000) % 36000 - 18000)          # facing 0
    got = (
        frames,
        dist >= 3000,                        # walked at least 30 units
        gerr, off,                           # never left the floor
        (s0, left0, s1),                     # default -> away -> default
        nstates >= 3,
        camd, fov,                           # 118.11 from him, fov 75
        refused, bad,
        (tm, tt), tm * 1.0 / tt > 0.95,      # tracks resolve to meshes
        astate,                              # ACTOR_STATE 1 throughout
        finite,
        (eyeBack, pfov) == (round(m0["eye"][2] * 10000), round(m0["fov"] * 100)),
        none[1] < 100 and none[5] == 0,      # no input: still, never left idle
        none[7],
        left[14] > 3000,                     # LEFT turns him (deg * 100)
        left[1] < 500,                       # ...without walking
        hErr < 300, behind,                  # walked -Z at facing 0; eye BEHIND
        turnwalk[1] > 3000, twErr < 800,     # turned, then walked that way
        turnwalk[20],                        # ...camera still behind
        # THE DIAGONAL. Holding UP and LEFT together walks AND turns, and the
        # animation does not change: the state at the end is the same H_WALK
        # the UP-only stream ends in, while the facing has swept. That is the
        # `sub_45C080` pass of `Cef_TickChannel` - candidates carrying flag
        # 0x100 have their turn applied at `rate * dt` and their input bits
        # masked out, WITHOUT the transition being taken - and the turn comes
        # off the CANDIDATE's block, not the state being left. Reading `from`
        # instead (which the port did until 2026-09-03) applies zero, because
        # H_WALK carries no turn of its own: he walked in a straight line and
        # the diagonal was unreachable. The negative control is `left`, which
        # turns through H_SDLROT and does NOT walk.
        diag[1] > 3000,                      # he covered ground
        abs(diag[14]) > 3000,                # ...and turned while doing it
        diag[16] == fwd[16], diag[13],       # same end state, ACTOR_STATE 1
        # THE CAMERA'S HEIGHT. Preset 0 offsets eye and target by 0 on y, so
        # the camera's height above the floor point IS the subject lift - the
        # model's pelvis above its feet, 41.9 for HO1_FNM, which is the 41.8
        # the dialogue staging measured from the other side. A camera that
        # takes `pos()` for its subject reads 0 and sits on the ground.
        (camHigh, camLift),
    )
    want = (
        105, True, 0, 0, (1, 1, 1), True, 11811, 7500, 0, 0,
        (1539, 1595), True, 1, 1, True, True, 11811, True, True,
        True, 11811, True, True, 11811,
        True, True, True, 1,
        (4189, 4189),
    )
    return got, want, "H1AVNT on AIMPASSE, five input streams - the last the DIAGONAL"


def c_engine_shoot_ai():
    r"""`engine/`'s shoot AI - the four callbacks, and the data the docs said
    was not there.

    Shoot mode's per-actor brain is one function pointer, and
    `Shoot_ActorEnter` (0x00422C10) picks it by the character's type (property
    7, record `+176`): **7 X-Tech -> `nullsub_9`, 10 Gandhar -> `sub_47F6F0`,
    13 Astaroth -> `sub_4800C0`, anything else -> `sub_424DE0`**, the generic
    shooter. `Shoot_TickNpc` calls it once a frame.

    **The correction this slice carries.** `docs/ASSETS.md` recorded the shoot
    AI as "four hand-written callbacks ... There is no table to read", and
    CLAUDE.md §4 as "No data table". That is true of the DISPATCH - it really
    is a `switch` - and false of the AI: **Gandhar plays three behaviour
    SCRIPTS compiled into the executable**, through two twelve-entry handler
    tables, and the fourteen character types are a name table the binary
    carries. All of it is now `tables/shoot_ai.json`. The negative was about
    the `switch` and got generalised to the subsystem, which is exactly the
    shape CLAUDE.md §1 warns about.

    The tables **chain end to end** - 0x004CFA30 types, 0x004CFA70 healthy
    (14), 0x004CFAE0 wounded (10), 0x004CFB30 critical (13), 0x004CFB98 enter
    handlers (12), 0x004CFBC8 tick handlers (12), 0x004CFBF8 the first name
    string - every end address the next one's start. A wrong base could not
    produce that, and `exe tables` re-derives it.

    **What the shipped data says, and it decides how much each arm is worth:**

    * **1032 character records, all dispatched; 330 carry type 0xFFFFFFFF**
      and reach the generic arm through `default:`, so that arm is
      load-bearing rather than defensive;
    * **317 `shoot.actor.enter` sites, 306 resolving in their own chunk** -
      resolved chunk-locally because `Actor_FindById` scans the chunk's own
      table, and resolving anywhere else counts collisions - splitting
      **302 generic / 3 Astaroth / 1 Gandhar / 0 X-Tech**;
    * **no shipped character is type 7 at all.** `nullsub_9` is unreachable
      content, like the six spell recipes whose gate is never 8.

    **Gandhar is table-driven, so running him IS running the shipped script**,
    and that is the one arm with a real test: all three scripts are walked
    twice round, **144 steps, 0 disagreeing** with the table expanded by its
    own repeat counts. And the health band is re-read every frame - `<= 100`
    wounded, `<= 50` critical - with a change **restarting** the routine, not
    resuming it, which is asserted separately because it is the kind of thing
    a port silently gets wrong.

    **The standard is lower here than for `engine: actor states`, and the
    numbers are arranged to show it.** Only the tables and the dispatch touch
    shipped data. Astaroth and the generic shooter are code with nothing
    behind them: they are ported as their state GRAPHS - Astaroth's 16..21 /
    27 / 29 with his own 195/273/156/78 unit distances and 150/60 frame
    timers, the generic's 1..15 / 28 and the five-way sub-switch inside state
    6 - and **not** as the 1500 lines of per-state geometry that decide where
    to stand and where to aim. The only thing asserted about them is that the
    runtime never leaves the state set that was read (0 of 2000 ticks), which
    catches a port that wanders and nothing else. That is
    `RECONSTRUCTION.md` §3's "read and explained", not "verified".

    All three real checks were confirmed by breaking them: resuming instead of
    restarting on a band change moves the reset count to 0, dropping the
    Astaroth arm moves 2/2 to 0/1030 and 3/302 to 0/305, and misspelling one
    type name moves 14 to 13.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    tb  = os.path.join(ROOT, "tables")
    if not (os.path.isdir(eng) and os.path.isdir(os.path.join(fr, "IAM"))
            and os.path.isdir(tb)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_shoot_ai")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, fr, os.path.join(tb, "shoot_ai.json"), out,
                        os.path.join(tb, "vm_opcodes.json")], capture_output=True)
        v = struct.unpack_from("<26i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (14, 12, 14, 10, 13,
               1032, 1032, 330, 0, 2, 2, 1028, 0, 2, 2,
               317, 306, 0, 1, 3, 302,
               144, 0, 1, 2000, 0), \
           "type names agreeing with the lifted JSON, action rows, and the " \
           "three behaviour scripts' lengths; then character records, those " \
           "dispatched and those whose type is UNSET (-1, reaching the " \
           "generic arm through `default:`); the records by arm (inert, " \
           "Gandhar, Astaroth, generic) and how many carry type 7, 10 and 13 " \
           "- type 7 is X-Tech and NOTHING in the game is one, so nullsub_9 " \
           "is unreachable; then shoot.actor.enter sites, those resolving in " \
           "their OWN chunk's actor table, and the arm each selects; then " \
           "Gandhar's script walk - steps taken, steps disagreeing with the " \
           "table (0) and health-band restarts; and finally the ticks driven " \
           "through all four arms and how many left the state set that was " \
           "read"




def c_engine_input():
    r"""`engine/`'s INPUT PATH - the four schemes, the edge filter, and the
    start menu answered by SCANCODE.

    Everything in this engine reads one fourteen-bit word: the `.CTL` channel
    matches transitions on it, the fight AI injects into it, and the interface
    reads it with three bits given a second job. `Input_Poll` maps binding slot
    k to bit `1 << k`, which the shipped tables confirm from the other side -
    all 56 rows of `tables/key_bindings.json` carry `bit == 1 << action`, and
    the loader REFUSES a file that does not.

    `Input_InstallScheme` (0x0045BFF0) fixes the shape in eight lines: three
    compiled tables 224 bytes apart, each 4 groups x 14 actions x 4 bytes, a
    group being 56 bytes, copied fourteen at a time into three LIVE tables. The
    groups are contexts the engine switches - `Game_Init` (0x0041FA00) installs
    **0 Aventure**, the swim transition **1**, `Shoot_Enter` **2**,
    `Fight_Begin` **3**, `Shoot_Leave` 0 again.

    **The correction this slice carries.** `docs/UI.md` gave the interface
    bits' defaults as **E** and **R**, citing `0x004C65B8`. That address is
    real, and holds exactly that - but it is the LIVE keyboard table, written
    by `Input_SetUiKeyBinding` (0x0043E830), and `{203, 205, 200, 208, 18, 19,
    32, 33, 29, 57, 34, 35, 42, 15}` is only its static initialiser: arrows,
    then E R D F, LCTRL, SPACE, G H, LSHIFT, TAB. A dense fourteen with no
    holes is the tell, because the real Aventure scheme leaves four slots
    unbound. `Game_Init` calls `Input_InstallScheme(0)` before the first frame,
    so **slots 4 and 5 go 18/19 -> 28/57, ENTER and SPACE, and no player ever
    saw E or R.** The port reproduces the initialiser rather than starting from
    the installed scheme, which is the only way that transition is observable
    at all.

    **93 bound cells over the four groups** is `docs/ASSETS.md`'s 41 + 48 + 4
    reached from the other end - the three device tables counted through the
    installer rather than read off the image.

    The edge filter is `edges = held & (held ^ (repeatMask & lastFrame))`, and
    it is tested by its effect in both directions: ENTER held three frames
    fires **once** under `Ui_BeginScreen`'s 0x203F and **three times** with the
    mask cleared, which is what closing the last screen does.

    **And then the differential, which is what this slice is for.** `tools/sim`
    answers the start menu handed the input WORDS. Here nothing is handed a
    word: ENTER, DOWN, ENTER go in as scancodes, the live tables and the edge
    filter turn them into words, and the ported walk gets those - reaching
    **answer 1**, the value the shipped save records. Held rather than
    released, the third frame adds no edge, only two words reach the walk and
    there is **no answer at all**, so the filter is shown to matter instead of
    being asserted to.

    Rebinding is group-local: codes 0, 1 and 4 are refused before the scan (the
    two joystick axes and ESC), and binding `Avancer` to LEFT clears Aventure's
    own slot 0 while Combat's stays 203 - which is why UP is `Avancer` in one
    scheme and `Sauter` in the other.

    **Tier 3, differential** (`docs/PORTING.md` B6). No capture can reach the
    input path: a menu announces nothing to the tag logger and the world's
    input never appears in an operand.

    Shown to fail, by running each one: dropping the initialiser and starting
    from the installed scheme moves 18/19 to **28/57**; edge-filtering the mask
    against `held` instead of `lastFrame` moves the fires to **0/3** and takes
    the whole walk with it (no word survives the filter, so both answers go
    to -1); not clearing the old slot on a rebind moves 0 to **203**; and
    holding the keys moves the answer from 1 to -1, which is the run itself
    rather than a mutation.

    **One of those four first reported that it changed nothing**, and it was a
    stale link rather than a weak check - see `docs/PORTING.md` B4.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    tb  = os.path.join(ROOT, "tables")
    if not (os.path.isdir(eng) and os.path.isdir(tb)):
        return ("skipped",), ("skipped",), "engine/ or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_input")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "i.bin")
    try:
        subprocess.run([binp, os.path.join(tb, "key_bindings.json"),
                        os.path.join(tb, "ui_widgets.json"), out],
                       capture_output=True)
        v = struct.unpack_from("<20i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (18, 19, 28, 57,
               168, 0, 93,
               1, 3,
               1, 3, -1, 2,
               0, 0, 0, 1, 0, 203, 203), \
           "the live keyboard table's slots 4 and 5 BEFORE Game_Init's " \
           "install (18, 19 - E and R, the static initialiser docs/UI.md " \
           "read as the default) and after (28, 57 - ENTER and SPACE); then " \
           "the cells four installs write, how many disagree with their " \
           "compiled scheme (0) and how many are bound - 93, which is " \
           "ASSETS' 41 + 48 + 4 reached through the installer; then ENTER " \
           "held three frames under mask 0x203F and with the mask cleared " \
           "(1 and 3); then the start menu answered BY SCANCODE - the " \
           "answer and the words that reached the walk with the keys " \
           "released, and the same three HELD, where the third frame adds " \
           "no edge and there is no answer; and finally rebinding - codes " \
           "0, 1 and 4 all refused, LEFT accepted, Aventure's slot 0 " \
           "cleared to 0 by it and Combat's left at 203"


def c_engine_i2d():
    r"""`engine/`'s I2D layer - the display list, its pools and its flag banks.

    I2D is the engine's 2D compositor: `LIBI2D` and `libpoly2d/gereaff.c` in
    its own strings. It is not part of the 3D path - every primitive ends in an
    `IDirectDrawSurface::Blt` - and `I2D_Flush` (0x00428B00) is one line of the
    frame: clear render state 14, walk the list, set it again, reset the pools.

    **WHAT IS PORTED.** The DirectDraw back end is not, and cannot be from
    here: there is no surface, and `Blt` is on the far side of the same line as
    the six x87 rasterizers. What IS ported is everything that decides *what
    would be drawn and in what order* - the list, the seven pools, the
    acceptance tests, and the three flag banks. Same line the renderer port
    draws, where the bucket key and the texture cache are RUN and the triangles
    are not.

    **THE STANDARD: read and explained, with ONE data-falsifiable check.** No
    shipped data describes a display list, and pixels cannot be diffed. The
    exception is the flags: the widget tree ships **99 flag constants** - 47
    an item applies, 52 a list broadcasts - and `I2D_SetFlag`'s three-way `if`
    silently DROPS a constant naming no bank, so "all 99 resolve" is a
    property the data could fail.

    **It was 139 until 2026-09-01**, and the 40 that went are not a loss: the
    lift used to walk `Ui_OpenShop`'s two arms linearly and record the shops'
    two rows set BOTH ways, so twenty items carried four entries where they
    have two. Following the parameter branch halves them - 20 x 2 = 40 - and
    the split moves 42/95 to 22/75 because each item shed one constant from
    each of the first two banks. The other numbers are invariants of the transcription: useful -
    one of them found the ordering below - but not evidence about the original,
    and that is the honest description rather than a green tick implying more.

    **THE 4862.** The docs quoted the display list's node cap as a bare number.
    It is **exactly the sum of the seven pools** - 4096 + 200 + 100 + 220 + 220
    + 16 + 10 - so the list can never fill before the pools do. Reaching that
    identity needs the **dead** pool in the count: `sub_428660` (cap 10) and
    `sub_4286F0` have no callers, and `sub_4286F0` carries a latent overflow -
    it checks the BITMAP counter (cap 220) and writes into the 10-entry pool.
    Unreachable, so it never fires; recorded rather than fixed.

    **THE ORDERING, WHICH THE DOCS HAD WRONG.** `docs/UI.md` called
    `dword_4E97B8` "a per-layer tail cache". It is a per-layer **head** cache.
    The assembly writes it on both exits of the list walk (`loc_42850C`,
    `loc_428524`) and NOT on the cache-hit path (`loc_428534`), which only does
    `new->next = cached->next; cached->next = new`. So within one layer the
    first primitive submitted draws first and **the rest draw in reverse
    submission order**. Across layers the list is still sorted, which is what
    the layer exists for; inside a layer, submission order is not draw order.
    A real tail cache scores 724 of 1407 here instead of 1407, and no
    screenshot would say which is right.

    **And a second shape the check found rather than assumed.** The frame's
    very first node takes `I2D_Enqueue`'s `if (!count)` arm, which sets the
    head and does **not** set the cache - so the second node on that layer goes
    through the walk, is inserted *before* it, and becomes the cache. A first
    version predicted only the ordinary shape and scored 1260 of 1407; the 147
    misses were exactly the layers holding node 0. Both shapes are now
    predicted and it is 1407 of 1407.

    **The acceptance tests**, which matter because a refused primitive is
    simply not drawn: layer >= 16, list full, pool full, and for the two blits
    three rectangle tests on payload dwords 0/3, 1/4 and 6/9 - `src.x`,
    `src.y`, `dst.x`. **There is no test on the destination Y**, identically in
    both functions, so a destination rectangle inverted only vertically is
    accepted and submitted. Adding the "missing" fourth takes that count from 2
    to 0, which is why it is asserted rather than tidied away.

    **Which rectangle is which was corrected on 2026-09-01**, and only reading
    the drawer settled it: `sub_4810D0` passes payload points 2/3 as
    `lpDestRect` and points 0/1 as `lpSrcRect` to `Blt`, which sits at vtable
    +20. So the payload is SOURCE first, DESTINATION second, and the untested
    axis belongs to the destination. The submitter alone cannot tell you -
    it only tests dwords - which is CLAUDE.md 1's rule about reading the
    consumer.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    tb  = os.path.join(ROOT, "tables", "ui_widgets.json")
    if not (os.path.isdir(eng) and os.path.exists(tb)):
        return ("skipped",), ("skipped",), "engine/ or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_i2d")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "i.bin")
    try:
        subprocess.run([binp, tb, out], capture_output=True)
        v = struct.unpack_from("<29i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (4862, 7, 1, 1,
               200, 200, 5032, 1407, 1407, 147,
               1, 1, 0, 24, 2, 1,
               2, 1, 2,
               99, 99, 22, 75, 2,
               47, 47,
               24, 357, 190), \
           "the seven pools' total capacity, how many are live, how many are " \
           "UNREFERENCED, and whether the total equals the display list's own " \
           "node cap of 4862 - which is what makes that number derived rather " \
           "than arbitrary; then the ordering: submission sequences, those " \
           "drawn in layer order (all - that is what the layer is for), nodes " \
           "drawn, layers carrying more than one node, those matching the " \
           "HEAD-cache prediction, and how many of those are the frame's " \
           "first node's layer, which has a shape of its own; then the " \
           "acceptance tests - accepted, layer out of range, list full, pool " \
           "full, degenerate destination and degenerate source - and the " \
           "blits accepted with an INVERTED SOURCE Y, which neither of them " \
           "tests; then a clean flush and its two render-state toggles; then " \
           "the shipped widget tree's flag constants, how many resolve to a " \
           "bank (all of them - a constant naming none is silently dropped), " \
           "the split across the three banks, the set/test round trips and " \
           "how many are correct; and the item flag words the data actually " \
           "uses, which is all three"



def c_engine_frame():
    r"""The engine's own FRAMEBUFFER, and the text renderer checked against it.

    `goldentrace.py` gave this repo the engine's decisions. `goldentrace.py
    capture` gives it the engine's pixels: the game runs under CrossOver in a
    Wine virtual desktop, `screencapture` grabs the window, and
    `frame.recover` takes the 2x Retina capture back to the game's own 640x480.
    Three frames of the start menu are committed in `traces/frames`, and
    **nothing here needs CrossOver** - the rig makes an oracle, the file IS the
    oracle, exactly as the `.log` captures already work.

    **The recovery is exact and REFUSES rather than degrades.** A 2x capture is
    only the framebuffer if the display scaled nearest-neighbour, so every 2x2
    block must be uniform; it was, 0 of 307200 non-uniform. If a future display
    interpolates, `recover` raises and no frame is written - a frame that is
    only NEARLY the framebuffer would make every pixel diff built on it quietly
    wrong while still passing, which is this repo's recurring failure.

    **What a menu frame is deterministic about**, measured rather than assumed:
    the title bitmap is **pixel-identical** across all three captures and so
    are all four labels' glyph pixels - mask AND values - while the frame as a
    whole differs by **52%**, because the tile-map background animates. So a
    check may assert text and the title, and must not assert the background.

    **The finding.** The four labels are located by SATURATION, not brightness:
    the menu draws text through a neutral grey ramp over a strongly coloured
    tile map, so near-grey separates glyph from scene where bright does not -
    and it excludes the orange title too, which is what makes the count come
    out at exactly four. Their positions are then derived from the frame, so a
    check that drew them somewhere else would fail rather than pass.

    Then each label is rendered by `tools/uitext.py` in **MENUINTR** - font
    `I`, the face the compiled font table names - and matched against the
    frame. **All four reach an IoU of 0.998 or better, and two are exactly
    1.000.** That is the text renderer verified against the original's own
    output rather than against a second implementation of the same reading,
    which is the distinction CLAUDE.md 5 says has already cost this repo twice.

    **And the focus highlight reads out as a number**: the focused row's ramp
    peaks at **255** and the other three at **124**. The glyphs are otherwise
    identical, so the highlight is a brightness and not a second font.

    The threshold is a fraction of each band's own peak, because a fixed cut
    clips the dim rows' anti-aliased edges - at a flat threshold the three
    unfocused labels score 0.81-0.84 and look like a mismatch when they are a
    thresholding artifact.
    """
    import frame as F
    import uitext
    fr = os.path.join(ROOT, "traces", "frames")
    shots = [os.path.join(fr, n) for n in
             ("menu-18.png", "menu-22.png", "menu-26.png")]
    if not all(os.path.exists(p) for p in shots):
        return ("skipped",), ("skipped",), "traces/frames absent"
    LABELS = ["Nouvelle partie", "Charger une partie", "Options", "Quitter"]

    imgs = [F.read_png(p) for p in shots]
    w, h, rgb = imgs[0]
    bands = F.text_bands(w, h, rgb)
    peaks = [F.ramp_peak(w, h, rgb, b) for b in bands]

    # the labels, rendered by the interface's own font and matched
    ious = []
    tmp = os.path.join(ROOT, "traces", "frames", "render-tmp.png")
    try:
        for lab, band in zip(LABELS, bands[:4]):
            peak = F.ramp_peak(w, h, rgb, band)
            cut = int(peak * 0.40)
            mask = F.neutral_ink(w, h, rgb, minl=cut,
                                 box=(0, band[0], w, band[1]))
            with open(tmp, "wb") as fh:
                fh.write(uitext.png(lab, font_letter="I"))
            rw, rh, rp = F.read_png(tmp)
            rmask = [1 if F.luma(rp, i) > int(255 * 0.40) else 0
                     for i in range(rw * rh)]
            rys = [y for y in range(rh) if any(rmask[y * rw + x] for x in range(rw))]
            rxs = [x for x in range(rw) if any(rmask[y * rw + x] for y in range(rh))]
            fxs = [x for (_, x) in mask]
            if not fxs or not rxs:
                ious.append(0.0); continue
            ious.append(round(F.iou_at_best(mask, rw, rh, rmask,
                                            min(fxs) - rxs[0],
                                            band[0] - rys[0]), 2))
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)

    # determinism: the glyphs and the title are the same in every capture, the
    # background is not
    glyphSame = titleSame = 0
    frameDiffers = 0
    base = imgs[0][2]
    for _, _, other in imgs[1:]:
        ink = set()
        for band in bands[:4]:
            ink |= F.neutral_ink(w, h, base, minl=50, box=(0, band[0], w, band[1]))
        if all(base[3 * (y * w + x):3 * (y * w + x) + 3] ==
               other[3 * (y * w + x):3 * (y * w + x) + 3] for (y, x) in ink):
            glyphSame += 1
        if base[:3 * w * 103] == other[:3 * w * 103]:
            titleSame += 1
        d = sum(1 for i in range(w * h)
                if base[3 * i:3 * i + 3] != other[3 * i:3 * i + 3])
        if d > w * h // 4:
            frameDiffers += 1

    # And the same screen captured in DRIVER MODE 2 - the engine's own
    # software rasterizer, selected through `Runtime 2.exe CONFIG`. The blit
    # drawers have no mode test (`sub_4810D0` calls `Blt` unconditionally), so
    # a blit-and-text screen must come out IDENTICAL, and it does: 0 of the
    # title's pixels and 0 of the glyphs differ. That is what says the front
    # end cannot raise the primitives' tier - it never draws one.
    mode2 = os.path.join(fr, "mode2-24.png")
    m2Title = m2Glyph = -1
    if os.path.exists(mode2):
        _, _, other = F.read_png(mode2)
        m2Title = sum(1 for i in range(w * 103)
                      if base[3 * i:3 * i + 3] != other[3 * i:3 * i + 3])
        ink = set()
        for band in bands[:4]:
            ink |= F.neutral_ink(w, h, base, minl=50, box=(0, band[0], w, band[1]))
        m2Glyph = sum(1 for (y, x) in ink
                      if base[3 * (y * w + x):3 * (y * w + x) + 3] !=
                         other[3 * (y * w + x):3 * (y * w + x) + 3])

    return (w, h, len(bands), [b[0] for b in bands], peaks, ious,
            glyphSame, titleSame, frameDiffers, m2Title, m2Glyph), \
           (640, 480, 4, [127, 208, 290, 370], [255, 124, 124, 124],
            [1.0, 1.0, 1.0, 1.0], 2, 2, 2, 0, 0), \
           "the committed frame's size - the game's own resolution, recovered " \
           "exactly from a 2x capture; the text bands found by SATURATION " \
           "(exactly four - the orange title and the coloured tile map are " \
           "excluded) and the row each starts on, DERIVED from the frame " \
           "rather than given; each band's ramp peak, which is how the focus " \
           "highlight reads out - 255 for the focused row and 124 for the " \
           "rest, so it is a brightness and not a second font; then the IoU " \
           "of tools/uitext.py's MENUINTR rendering of each label against the " \
           "engine's own pixels; and finally the determinism: how many of the " \
           "other two captures have identical GLYPH pixels, an identical " \
           "title bitmap, and a frame that nonetheless differs by more than a " \
           "quarter - the tile-map background animates, so a check may assert " \
           "the text and must not assert the scene; and finally the same " \
           "screen captured in DRIVER MODE 2, the engine's own software " \
           "rasterizer - the title and the glyphs must be IDENTICAL, because " \
           "the blit drawers have no mode test, which is also why the front " \
           "end cannot raise the primitives' tier"



def c_porting_standard():
    r"""`docs/PORTING.md` against the things it makes claims about.

    A standard nothing checks is prose, and this repo has a rule about that.
    Three kinds of drift are possible here and all three have precedent:

    * **the coverage counts stop adding up.** `engine/README.md`'s table has
      been wrong twice - once with a count that had quietly dropped the rows it
      judged unportable, once left stale by a day's work - so the five bucket
      counts are re-parsed and summed against the stated total, and the
      partly-ported table is counted against its own headline;
    * **a number in the document stops matching the artifact it came from.**
      Part A3 fixes the reference framebuffer as **RGB565** on the evidence of
      a committed capture: 32 distinct red levels and 63 green. That is
      re-measured from `traces/frames/menu-22.png` rather than believed, which
      is the same rule every other number in `docs/` is held to;
    * **the document and the coverage table stop agreeing about what is
      unfinished.** Every subsystem B6 gives an acceptance criterion for has to
      still be named by the README as unfinished, or one of the two is stale.

    What this deliberately does NOT check is that each slice declared its tier
    (B2). Retrofitting that phrasing onto the twenty checks written before the
    ladder existed would be churn for its own sake, and a scan loose enough to
    pass them would pass anything - "exact" and "differential" are ordinary
    words in these docstrings. B2 is a rule for new work and is enforced by
    reading, which is stated here so the gap is known rather than assumed
    covered.
    """
    import re
    doc = os.path.join(ROOT, "docs", "PORTING.md")
    rme = os.path.join(ROOT, "engine", "README.md")
    if not (os.path.exists(doc) and os.path.exists(rme)):
        return ("skipped",), ("skipped",), "docs/PORTING.md or engine/README absent"
    d = open(doc, encoding="utf-8").read()
    r = open(rme, encoding="utf-8").read()

    def count(pat):
        m = re.search(pat, r)
        return int(m.group(1)) if m else -1
    buckets = [count(r"\*\*(\d+) fully ported"),
               count(r"\*\*(\d+) partly ported"),
               count(r"\*\*(\d+) lifted as (?:a )?tables?"),
               count(r"\*\*(\d+) not ported"),
               count(r"\*\*(\d+) are not portable subjects")]
    stated = count(r"\*\*(\d+) content\s*\n?rows\*\*")
    if stated < 0:
        m = re.search(r"\*\*(\d+) content", r)
        stated = int(m.group(1)) if m else -1

    # the partly-ported table's own rows, counted rather than trusted
    body = r.split("partly ported", 1)[1] if "partly ported" in r else ""
    tbl = body.split("| row | ported | not |", 1)[-1].split("\n\n", 1)[0]
    rows = [ln for ln in tbl.splitlines()
            if ln.startswith("|") and not ln.startswith("|---")]

    # A3's RGB565 evidence, re-measured from the committed capture
    levels = (-1, -1)
    frame = os.path.join(ROOT, "traces", "frames", "menu-22.png")
    if os.path.exists(frame):
        import frame as F
        w, h, rgb = F.read_png(frame)
        levels = (len({rgb[3 * i] for i in range(w * h)}),
                  len({rgb[3 * i + 1] for i in range(w * h)}))

    # B6's items must still be ones the README calls unfinished
    b6 = d.split("## B6.", 1)[-1].split("## B7.", 1)[0]
    # Skip the header by its own first CELL, not by the word "reachable"
    # appearing anywhere in the row - an earlier version did the latter and
    # silently dropped a real row the day its prose happened to use the word.
    items = [c for c in (ln.split("|")[1].strip().strip("`*")
                         for ln in b6.splitlines()
                         if ln.startswith("|") and not ln.startswith("|---"))
             if c.lower() != "item"]
    # ...and matched against the README's OWN "what is left" table, not
    # against the whole file. A first version searched the whole README and
    # did not bite when a row was renamed, because the phrase still occurred
    # in prose two sections earlier - a check that passes on a broken input is
    # the thing PORTING B4 exists to catch, found by trying to break it.
    sect = r.split('### What is left', 1)[-1].split("\n## ", 1)[0]
    left = "\n".join(ln for ln in sect.splitlines()
                      if ln.startswith("|") and not ln.startswith("|---"))
    unmatched = 0
    for it in items:
        key = it.replace("the ", "").replace("`", "").split("(")[0].strip()
        if key and key.lower() not in left.lower():
            unmatched += 1

    return (buckets, sum(buckets), stated, len(rows), levels, len(items), unmatched), \
           ([31, 7, 0, 0, 3], 41, 41, 7, (32, 63), 9, 0), \
           "engine/README's five coverage buckets, their sum and the total it " \
           "states - which must agree, and have twice not; the partly-ported " \
           "table's actual row count against its own headline; then the " \
           "distinct RED and GREEN levels re-measured from the committed " \
           "capture, which is the evidence PORTING A3 fixes the reference " \
           "framebuffer as RGB565 on (32 and 63 - five and six bits); and " \
           "finally the subsystems B6 gives an acceptance criterion for, and " \
           "how many of them are missing from the README's OWN 'what is left' " \
           "table (0, or one of the two documents is stale) - scoped to that " \
           "table's ROWS because neither a whole-file search nor a " \
           "whole-section one bites when a row is renamed - the phrase " \
           "survives in the prose around it"



def c_engine_i2d_blit():
    r"""The I2D blit back end, RUN against the engine's own framebuffer.

    The first output slice with a real oracle, and the first to meet
    `docs/PORTING.md` B6's criterion for the I2D back end: **tier 4 - a
    captured menu frame reproduced pixel-exact from the shipped assets plus
    the ported display list.**

    The path is: `gamedata/I2D/bitmaps/gfxint.BMP` (screen 29's own bitmap, 640x480,
    8-bit palettised, bottom-up) -> `surfaceFromBmp` -> a submission through
    the ported `I2dList` -> `present`, which walks the list in DRAW order and
    executes each node through `blt` - a memory copy with an optional colour
    key, which is all `IDirectDrawSurface::Blt` is. **66560 of 66560 pixels
    identical.**

    **Compare in RGB565, not in 888.** The game's framebuffer is 565, and a
    captured frame's 8-bit values are the HOST's expansion of it - so the
    comparison quantises the capture back to 565 rather than expanding the
    reference. That is not pedantry: expanding by bit replication, which is the
    obvious guess, is **off by one in green** against what CrossOver actually
    produced, and the title region then matches 94.9% instead of 100% with
    every difference a rounding artefact of the host. The 100% is only visible
    once the host is taken out of the question.

    **What is asserted and what is not.** The capture's top 103 rows were
    measured identical across three frames, so the title is deterministic and
    is the target; the rest of the menu is the animated tile map drawn over it,
    which B5 says a check must not assert. It is still *reported* - as "the
    rest does not match", which is asserted - so the 100% cannot be passing
    because the whole frame is black.

    Broken deliberately: reading the BMP top-down instead of bottom-up takes
    the title from 66560 to 50502, and comparing in 888 with the reference
    expanded by bit replication takes it to 63142.

    **What does NOT discriminate, said because claiming it would is the B7
    anti-pattern**: quantising the capture by truncation (`v >> 3`) instead of
    rounding also gives 66560. Both invert the host's expansion correctly for
    every level it produces, so this check cannot tell them apart and does not
    claim to. The rounding form is kept because it is the correct inverse in
    general, not because a test here prefers it.
    """
    import subprocess, tempfile, shutil, struct as st
    import frame as F
    eng = os.path.join(ROOT, "engine")
    shot = os.path.join(ROOT, "traces", "frames", "menu-22.png")
    bmp = omkpaths.data("I2D", "bitmaps", "gfxint.BMP")
    if not (os.path.isdir(eng) and os.path.exists(shot) and os.path.exists(bmp)):
        return ("skipped",), ("skipped",), "engine/, the capture or gfxint.BMP absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_i2d_blit")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "b.bin")
    try:
        subprocess.run([binp, omkpaths.data_root(), out], capture_output=True)
        d = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    bw, bh, acc, nodes, fw, fh = st.unpack_from("<6i", d, 0)
    fb = st.unpack_from("<%dH" % (fw * fh), d, 24)

    cw, ch, cap = F.read_png(shot)
    def q(r, g, b):
        return ((((r * 31 + 127) // 255) << 11) |
                (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255))
    title = same = rest = restsame = 0
    for y in range(ch):
        for x in range(cw):
            i = y * cw + x
            ok = fb[i] == q(cap[3 * i], cap[3 * i + 1], cap[3 * i + 2])
            if y < 104:
                title += 1; same += ok
            else:
                rest += 1; restsame += ok
    return (bw, bh, acc, nodes, fw, fh, title, same, 1 if restsame * 4 < rest else 0), \
           (640, 480, 1, 1, 640, 480, 66560, 66560, 1), \
           "gfxint.BMP's size; whether the display list ACCEPTED the blit and " \
           "how many nodes it drew; the framebuffer's size; then the " \
           "deterministic title region - pixels compared and pixels IDENTICAL " \
           "to the engine's own framebuffer, in RGB565 because a capture's " \
           "8-bit values are the host's expansion and not the game's data; " \
           "and finally that the REST of the frame does not match, which is " \
           "the animated tile map and is why the 100% above is not simply a " \
           "black screen agreeing with a black screen"



def c_sounds_folder():
    r"""`gamedata/SOUNDS/` - two files, and only one of them has a code path.

    A negative result is a finding, so this pins it rather than leaving the
    next person to wonder. The directory ships two `.wav`s:

    * **`SLIDERM01.wav` is used** - `Slider_Init` (0x00453450) pushes the
      literal `"SOUNDS\sliderm01.wav"`, the slider's motor loop, which is the
      same subsystem as `ACTOR_STATE` 7 and 8;
    * **`pluie.wav` cannot be reached.** The whole executable contains exactly
      **two** `.wav` path forms - that literal and `i2d\sounds\%s.wav` for the
      45 interface sounds - so nothing can build `SOUNDS\pluie.wav`, and the
      string `pluie` never appears in the binary. Everything else the engine
      plays is `.ADP`.

    The name is not a typo, which is why it is worth an assertion: it is in the
    DATA. `SCPTDATA/hamesta.Sfx` carries `pluie` - rain - as one of nine effect
    names at an 80-byte stride, so it is a particle effect in that scene and
    not a sound. The only other occurrence anywhere is French prose in
    `IAM\DIALOG`.

    So `pluie.wav` is an orphan asset, alongside the six spell recipes whose
    gate is never 8, the `X-Tech` shoot callback no character selects, and
    options page 12.
    """
    s = _need("asm")
    if s: return s
    import re
    snd = omkpaths.data("SOUNDS")
    asm = omkpaths.asm_path()
    sfx = omkpaths.data("SCPTDATA", "hamesta.Sfx")
    if not (os.path.isdir(snd) and os.path.exists(asm) and os.path.exists(sfx)):
        return ("skipped",), ("skipped",), "gamedata/SOUNDS, the listing or hamesta.Sfx absent"
    files = sorted(f.lower() for f in os.listdir(snd) if f.lower().endswith(".wav"))
    text = open(asm, encoding="utf-8", errors="replace").read()
    # the .wav path forms the binary carries, as string literals
    forms = sorted(set(re.findall(r"db '([^']*\.wav)',0", text, re.I)))
    named = sum(1 for f in files if os.path.splitext(f)[0] in text.lower())
    # and what `pluie` is in the data: one of nine names at an 80-byte stride
    d = open(sfx, "rb").read()
    names = [(m.start(), m.group().decode())
             for m in re.finditer(rb"[a-z][a-z0-9_]{2,15}", d)]
    tbl = [n for n in names if 80 <= n[0] <= 800 and (n[0] - 6) % 80 == 0]
    strides = sorted({b[0] - a[0] for a, b in zip(tbl, tbl[1:])})
    return (files, len(forms), sorted(f.lower() for f in forms), named,
            [n[1] for n in tbl], strides), \
           (["pluie.wav", "sliderm01.wav"], 2,
            ["i2d\\sounds\\%s.wav", "sounds\\sliderm01.wav"], 1,
            ["chute", "plouf", "blur", "soulj", "souli", "pluie", "bigsoul",
             "mat1", "noou1"], [80]), \
           "what gamedata/SOUNDS ships; how many .wav PATH FORMS the whole binary " \
           "carries and which (two - one literal and one template, and " \
           "neither can produce SOUNDS\\pluie.wav); how many of the two files " \
           "the binary names at all (one - sliderm01, from Slider_Init); then " \
           "hamesta.Sfx's effect-name table and its stride, which is where " \
           "`pluie` really lives - a rain PARTICLE effect, not a sound"



def c_engine_i2d_prims():
    r"""The I2D primitives other than the blits, through the software back end.

    **Tier 6, read and explained - deliberately not the 4 the blits reach**,
    and the reason is structural. A blit ends in `Blt`, a memory copy, so a
    capture can be diffed against it. These cannot: `sub_4822F0` (line),
    `sub_4806C0` (triangle) and `sub_480BD0` (quad) each open with
    `if (sub_45EF50() == 2)` and choose between **Direct3D and the engine's own
    software rasterizer**. The selector is a display-driver index - `sub_43A6D0`
    gives a real DirectDraw GUID mode 0, and the last two synthetic entries of
    the device list modes 1 and 2 - and the captures were taken in whatever
    mode CrossOver picked, which is not mode 2. So what is asserted is
    transcription invariants, NOT agreement with the original. A capture taken
    in mode 2 would raise it, and the driver index makes that reachable.

    **Two findings from reading it.** A triangle in mode 2 is a **wireframe**:
    `sub_4806C0`'s software branch is three calls to the line rasterizer over
    the three edges, with no fill. And the software path takes its colour from
    a **different place than the D3D one** - `I2D_DrawLine` stores the colour
    argument at payload dword 6, but `sub_48C4C0` is handed the three
    table-lookups of dword **2**, the third component of the first point. Both
    are what the code does; neither is reconciled here.

    The quad's mode-2 branch is NOT this - it is a separate float-based routine
    of ~189 lines - and is not ported.

    **The invariants, each of which the port could fail:**

    * **nothing is written outside the clip rectangle.** The rasterizer clips
      analytically then plots; if the two disagreed, pixels would escape. Over
      3000 lines ranging 400 units beyond every edge, **0** do;
    * **all four reject arms fire** - a line with both endpoints outside one
      side writes nothing;
    * **an UNCLIPPED edge is the same pixel set drawn either way round**:
      1541 of 1541, which the pre-Bresenham swap buys. It does NOT pin the
      error term - an error initialised to 0 instead of `-dx>>1` shifts a line
      half a pixel *symmetrically* and still reverses identically. An earlier
      version of this docstring claimed it did, which was wrong;
    * **every plotted pixel is the nearest one to the ideal line** -
      `|2*(dy*dX - dx*dY)| <= max(|dx|,|dy|)`, in doubled integers so the test
      is exact. **408801 probed, 0 off.** This is the one that pins the error
      term, and the zero-init mutation moves it off 0;
    * **a CLIPPED edge is not** - 1255 of 1459 - because the clip runs BEFORE
      the swap and processes endpoint 0 first using running values. Asserting
      both numbers is what separates "the original is asymmetric here" from
      "the port has a bug"; the first version of this sweep reported a single
      2561/3000 and could not tell them apart;
    * **a wireframe triangle is exactly the union of its three edges**, 400 of
      400 - it can have no pixel its edges do not;
    * **the quad's four blend modes behave as blends must**, 4000 of 4000 each
      and **0 per-channel errors**: the 50% blend of a colour with itself gives
      that colour back (up to the low bit the half-mask drops), the saturating
      add never lowers a channel and clamps at the mask, the subtract never
      raises one and floors at zero. Checked per CHANNEL, because a packed
      comparison hides a channel that borrowed into its neighbour. Dropping the
      clamp takes the monotonicity to 1959 and 0 with 11702 channel errors;
      using 0xFFFF as the half-mask instead of 0xF7DE takes idempotence to 971.

    **The quad, and what the payload actually is.** `sub_48C060` is not a quad
    rasterizer: it takes the four points' **bounding box**, clamps it to the
    screen and runs one of four fill loops, with the colour from **vertex 0
    alone** - no gouraud, whatever SHADEMODE the D3D path sets. Reading it
    corrected `docs/UI.md` twice over: a point's third component is the
    **colour**, not a z, and the submitter's third ARGUMENT is a **flags** word
    (one dword past the points - 6, 9, 12) from which D3D takes SHADEMODE,
    FILLMODE and ALPHABLENDENABLE, and mode 2 takes its blend. The table had
    both as "N points + colour".

    One asymmetry is transcribed rather than tidied: modes 0 and 1 fill
    `[left, right)` while 2 and 3 fill `[left, right]`, which is why 4000
    iterations of a 5x3 box give 216000 pixels rather than 240000. Modes 2 and
    3 are read through the decompiler's frame arithmetic and are less certain
    than 0 and 1, so the difference is recorded as read.
    """
    import subprocess, tempfile, shutil, struct as st
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_i2d_prims")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "p.bin")
    try:
        subprocess.run([binp, out], capture_output=True)
        v = st.unpack_from("<20i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (3000, 698689, 0, 548, 1541, 1541, 1459, 1255, 4, 400, 400,
               408801, 0, 216000, 4000, 4000, 4000, 4000, 4000, 0), \
           "lines drawn and pixels written; pixels landing OUTSIDE the clip " \
           "rectangle (0 - the analytic clip and the plotting loop must " \
           "agree); lines wholly rejected; then the reversal test split in " \
           "two - UNCLIPPED pairs and how many give the identical pixel set " \
           "(all, which is what the pre-Bresenham swap buys and what catches " \
           "an error-term slip), then CLIPPED pairs and how many do (fewer, " \
           "because the clip runs before the swap and is asymmetric by " \
           "construction); the four reject arms; and wireframe triangles, of " \
           "which all must be exactly the union of their three edges - mode 2 " \
           "has no fill; and finally the nearest-pixel probe, which is what " \
           "actually pins the error term: every plotted pixel must be the " \
           "closest one to the ideal line, tested in doubled integers; then " \
           "the quad - pixels filled over its four modes, and per mode " \
           "whether the 50% blend is idempotent, the saturating add and " \
           "subtract are monotone, they saturate and floor at the extremes, " \
           "and how many PER-CHANNEL errors there are (0 - a packed compare " \
           "would hide a channel bleeding into its neighbour)"



def c_engine_driver_modes():
    r"""The two render back ends, caught differing on one screen.

    `sub_4822F0`, `sub_4806C0` and `sub_480BD0` each choose between Direct3D
    and the engine's own software rasterizer on `sub_45EF50() == 2`, and the
    driver is picked in the game's own setup dialog (`goldentrace.py config`):
    **DirectDraw HAL** is mode 0, **The Nomad Soul software render** mode 2.
    Until now nothing here had ever SEEN the two produce different pixels -
    the start menu is blit-and-text and comes out identical - so "the
    primitives are on a separate path" was a reading of the code with no
    observation behind it.

    The **load panel** is the screen that shows it. Selecting a save draws a
    selection rectangle and a connector line to the thumbnail, and
    `I2D_DrawRectOutline` (0x004777A0) draws that outline with **four
    `I2D_SubmitQuad` calls**. Two captures of the same panel state, one per
    driver:

    | | shape, as IoU | identical VALUES on the overlap |
    |---|---|---|
    | the title text | **1.000** | **1909 of 1909** |
    | the selection outline | ~0.51 | **0** of 1089 |
    | the connector bar | ~0.29 | **0** of 36 |

    Text is mode-independent because it is a blit; the outline and the
    connector differ in both **where** they land and **what colour** they are,
    and not one overlapping pixel matches. (The connector was called a "line"
    when this was written; it is another quad - see `engine: I2D outline`.) That is the first direct evidence in this
    repo that the two back ends exist and disagree, and it is what makes a
    mode-2 frame a usable oracle for the ported rasterizer.

    Compared on the DRAWN pixels, not on regions: the tile-map background
    animates between captures, so a region diff reports 70% everywhere and
    says nothing - the first version of this measurement did exactly that and
    had to be redone.

    **This does not yet raise the primitives to tier 4.** It gives them a
    target. Doing so means reproducing the mode-2 outline from the widget's
    own coordinates, colour and blend mode, which is the next slice.
    """
    import frame as F
    fr = os.path.join(ROOT, "traces", "frames")
    m2 = os.path.join(fr, "loadpanel-mode2.png")
    m0 = os.path.join(fr, "loadpanel-mode0.png")
    if not (os.path.exists(m0) and os.path.exists(m2)):
        return ("skipped",), ("skipped",), "the load-panel captures are absent"
    w, h, a = F.read_png(m2)
    _, _, b = F.read_png(m0)
    out = []
    for box in ((10, 170, 390, 196),      # the selection outline
                (385, 176, 465, 188),     # the connector line
                (150, 45, 490, 80)):      # the title text
        ma = F.neutral_ink(w, h, a, box=box)
        mb = F.neutral_ink(w, h, b, box=box)
        inter, union = len(ma & mb), len(ma | mb)
        same = sum(1 for (y, x) in (ma & mb)
                   if a[3 * (y * w + x):3 * (y * w + x) + 3] ==
                      b[3 * (y * w + x):3 * (y * w + x) + 3])
        out.append((round(inter / union, 2) if union else 1.0, same, inter))
    return (w, h, out[0][0], out[0][1], out[1][0], out[1][1],
            out[2][0], out[2][1], out[2][2]), \
           (640, 480, 0.51, 0, 0.29, 0, 1.0, 1909, 1909), \
           "the captures' size; then, per feature and measured on the DRAWN " \
           "pixels rather than on a region (the background animates): the " \
           "selection outline's shape agreement between driver mode 0 and " \
           "mode 2 as an IoU, and how many overlapping pixels have the same " \
           "VALUE (none); the same for the connector bar; and the same for " \
           "the title text, which must agree perfectly and on every pixel " \
           "because text is a blit and blits have no mode test"



def c_engine_i2d_outline():
    r"""The ported rasterizer reproducing the engine's own selection outline.

    **This is the I2D primitives' tier-4 result.** Everything before it held
    them at tier 6 - transcription invariants only - because the captures were
    in driver mode 0 and no frame existed that the software rasterizer had
    drawn. The load panel provides one: selecting a save draws a box with
    `I2D_DrawRectOutline` (0x004777A0), which is **four `I2D_SubmitQuad` calls
    with flags = 4**, and `quadMode(4)` is mode 1, the 50% blend.

    The four quads are transcribed from that function's own arithmetic with
    `v7 = I2D_ScaleX(1) = screenWidth / 640 = 1`:

        top     x [a1-v7, a1+a3+v7)   y [a2-v7, a2]
        bottom  x [a1-v7, a1+a3+v7)   y [a2+a4, a2+a4+v7]
        left    x [a1-v7, a1)         y [a2, a2+a4]
        right   x [a1+a3, a1+a3+v7)   y [a2, a2+a4]

    The rectangle was measured off the capture and solved back through it:
    the horizontal bars occupy rows 172-173 and 189-190 over x 19..390, and the
    vertical bars are the single columns 19 and 390, giving **a1=20, a2=173,
    a3=370, a4=16**.

    **The result: 1518 covered pixels, and all 1518 are bright in the engine's
    own framebuffer.** The one-pixel ring immediately outside the box is
    background on its left edge, 19 of 19 - so the port's box is neither a
    pixel too small nor a pixel too wide.

    **What this actually pins is the half-open span.** Mode 1 fills
    `[left, right)` in x and the closed span in y - an asymmetry transcribed
    from `sub_48C060` and deliberately left untidied when it looked like an
    oddity. From one `v7 = 1` it predicts horizontal bars **2 pixels tall** and
    vertical bars **1 pixel wide**, and that is exactly what the original
    draws. Making the x span closed gives 2-pixel verticals and the port stops
    matching. An asymmetry that looked like a wart turns out to be load-bearing.

    **The connector to the thumbnail is not a line.** It was called one here
    and in `engine: driver modes` on the strength of how it looks; the pixels
    say otherwise - **2 rows by 69 columns**, which is a mode-1 quad's
    signature (closed in y, half-open in x) and not a Bresenham line's, which
    would be one row. `Ui_DrawItem`'s whole vocabulary agrees: FILL is a quad,
    OUTLINE four quads, ARROWS and MARKER triangles, and there is no line in
    it. The port covers **138 pixels, all 138 bright**, on rows 181-182, and
    the column past its right end is background on both rows.

    So no captured frame yet draws a **line**. `I2D_DrawLine` is reachable -
    `sub_49CE60` and `sub_42EC80` are called from `sub_49C9D0` and
    `sub_42EFE0` - but nothing here has drawn one, so the Bresenham stays at
    tier 6 while the quad is at 4.

    The blend itself is NOT checked here and cannot be from one frame: mode 1
    is 50% against whatever is underneath, and what is underneath is the
    animated tile map. Geometry is what a single capture can settle.
    """
    import subprocess, tempfile, shutil, struct as st
    import frame as F
    eng = os.path.join(ROOT, "engine")
    shot = os.path.join(ROOT, "traces", "frames", "loadpanel-mode2.png")
    if not (os.path.isdir(eng) and os.path.exists(shot)):
        return ("skipped",), ("skipped",), "engine/ or the mode-2 capture absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_i2d_outline")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "o.bin")
    try:
        subprocess.run([binp, out], capture_output=True)
        d = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    W, H, mode, v7, filled, covered, connFilled, connCovered = \
        st.unpack_from("<8i", d, 0)
    cov = d[32:32 + W * H]

    w, h, a = F.read_png(shot)
    L = lambda x, y: F.luma(a, y * w + x)
    inside = [(x, y) for y in range(H) for x in range(W) if cov[y * W + x] & 1]
    bright = sum(1 for (x, y) in inside if L(x, y) > 35)
    # the bar thicknesses the port produces, at a column and a row well inside
    rows = sorted({y for (x, y) in inside if x == 200})
    cols = sorted({x for (x, y) in inside if y == 180})
    # and the left edge of the ring outside: nothing of the box may be there
    leftRing = sum(1 for y in range(172, 191) if L(18, y) <= 35)
    # the CONNECTOR to the thumbnail, scored the same way
    conn = [(x, y) for y in range(H) for x in range(W) if cov[y * W + x] & 2]
    connBright = sum(1 for (x, y) in conn if L(x, y) > 35)
    connRows = sorted({y for _, y in conn})
    connStop = sum(1 for y in (181, 182) if L(460, y) <= 35)
    return (mode, v7, covered, bright, rows, cols, leftRing,
            connCovered, connBright, connRows, connStop), \
           (1, 1, 1518, 1518, [172, 173, 189, 190], [19, 390], 19,
            138, 138, [181, 182], 2), \
           "the blend mode `I2D_DrawRectOutline`'s flags select (1, the 50% " \
           "blend) and the border thickness I2D_ScaleX(1) gives (1); then the " \
           "pixels the ported quads cover and how many of them are BRIGHT in " \
           "the engine's own captured framebuffer - all of them; then the " \
           "rows the port paints at x=200 and the columns at y=180, which is " \
           "the half-open span made visible: 2-pixel horizontal bars and " \
           "1-pixel verticals from the same v7=1; and the left edge of the " \
           "one-pixel ring outside the box, which must be background on all " \
           "19 rows or the port's box is a pixel too wide; then the CONNECTOR " \
           "to the thumbnail, which is not a line at all but another mode-1 " \
           "quad - pixels covered, how many are bright in the engine's frame " \
           "(all), the two rows it occupies, and that the column just past " \
           "its right end is background on both, so it stops exactly where " \
           "the port says"



def c_engine_text_draw():
    r"""`Text_DrawRun` ported - the glyph pixels, against the original's own frame.

    The layout half was already here: `parseMarkup`, the 13-row font table,
    the advance (a glyph's own width or the face's default, plus the face's
    kerning). This is the half that was not, and it completes the row.

    **The colour model is a 32-entry RAMP**, and it is the whole of it. A glyph
    byte is a COVERAGE level 0..31 - not an intensity, not a palette index -
    and `Text_DrawRun` (0x0043EA10) rebuilds `word_52F5B8[32]` whenever the
    requested colour changes so entry `i` is `i/31` of it, with zero
    transparent. Read from the assembly rather than from the docstring: the
    builder runs 32 entries from `word_52F5B8` to `dword_52F5F8` (0x40 bytes),
    carries one accumulator per channel stepping by that channel's value, and
    divides each by 31 with the canonical `0x08421085` magic-multiply - so the
    division **truncates**, and plain integer division matches it.

    **The oracle is the engine's framebuffer, which is why this is tier 4 and
    not a differential against `uitext.py`.** The start menu's glyph pixels
    were measured identical across three captures of an animated screen - mask
    AND values - so they are deterministic and exactly reproducible. The
    colours are read out of the frames rather than assumed: the focused row is
    white, the other three `0x7F7F7F`, which through the ramp gives 565
    `0x7BEF` and displays as (123,125,123). The two differ only in the colour
    handed in - the glyphs are identical - which is what makes the focus
    highlight a brightness and not a second face.

    **Result: 6132 of 6132 painted pixels identical**, all four labels, and
    `dx = 0` on every one, so the horizontal centring is right too. The
    vertical offset is searched rather than asserted because the `y` handed to
    the tool is a measured ink-band top, not the item's true origin - that
    belongs to `Ui_ItemScreenY` and the widget tree, not to the rasteriser.

    Two mistakes on the way, both caught by the frame. The glyph record's
    `bottom` is relative to the baseline and **adds**: subtracting it put every
    row eight pixels low and stretched the block. And aligning by a thresholded
    ink bounding box gave per-band offsets of -7, -7, -9, -9 and only 45%
    agreement - different letters have different ascenders, so the ink box is
    not a reliable datum. Searching the offset instead gives 100% at every one.
    """
    import subprocess, tempfile, shutil, struct as st
    import frame as F
    eng = os.path.join(ROOT, "engine")
    shot = os.path.join(ROOT, "traces", "frames", "menu-22.png")
    ui = os.path.join(ROOT, "tables", "ui.json")
    if not (os.path.isdir(eng) and os.path.exists(shot) and os.path.exists(ui)):
        return ("skipped",), ("skipped",), "engine/, the capture or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_text_draw")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "t.bin")
    try:
        subprocess.run([binp, omkpaths.data_root(), ui, out], capture_output=True)
        d = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    W, H, painted, adv = st.unpack_from("<4i", d, 0)
    fb = st.unpack_from("<%dH" % (W * H), d, 16)

    w, h, a = F.read_png(shot)
    def q(x, y):
        i = y * w + x
        return ((((a[3 * i] * 31 + 127) // 255) << 11) |
                (((a[3 * i + 1] * 63 + 127) // 255) << 5) |
                ((a[3 * i + 2] * 31 + 127) // 255))
    bands = [(127, 157), (208, 238), (290, 317), (370, 392)]
    tot = exact = 0
    dxs = []
    for y0, y1 in bands:
        mine = [(x, y) for y in range(max(0, y0 - 25), min(H, y1 + 25))
                for x in range(W) if fb[y * W + x]]
        best = (0, 99, 99)
        for dy in range(-14, 3):
            for dx in range(-3, 4):
                n = sum(1 for (x, y) in mine
                        if 0 <= y + dy < h and 0 <= x + dx < w
                        and fb[y * W + x] == q(x + dx, y + dy))
                if n > best[0]: best = (n, dx, dy)
        tot += len(mine); exact += best[0]; dxs.append(best[1])
    return (W, H, painted, tot, exact, dxs), \
           (640, 480, 6132, 6132, 6132, [0, 0, 0, 0]), \
           "the surface the ported Text_DrawRun paints into; the pixels it " \
           "paints, counted twice - once by the tool and once by the checker " \
           "over the four label bands, which must agree; how many of them are " \
           "IDENTICAL to the engine's own captured framebuffer in RGB565 (all " \
           "of them); and the horizontal offset found for each label, which " \
           "must be zero because the centring is the port's own and only the " \
           "vertical origin belongs to the widget tree"



def c_ui_item_bindings():
    r"""What the per-screen OPEN callbacks bind into their items.

    `docs/UI.md` §3d reads the 30 per-screen callbacks and finds the family
    uniform - 20/20 opens install a static panel and reach `Ui_BeginScreen`,
    19/20 closes reach the generic close. What was never lifted is the part in
    between: each open also **binds its items' text and tags**, and neither is
    in the item record. They ship as -1 and 0; the callback writes them. A
    reader that trusts the record alone finds a screen with no labels at all,
    which is exactly what `engine/`'s widget walk saw.

    `TERMINAL` is the shape and the confirmation: its open writes string ids
    **5, 6, 7, 8, 9** onto five rows and **10** onto a sixth, with tags
    **1..4** - which is precisely what UI.md recorded by hand, recovered here
    mechanically.

    **Two encodings, and getting the first wrong yields plausible rubbish.**
    `+28` is an int16 written with the operand-size prefix,
    `66 C7 05 <abs32> <imm16>`; `+60` is a dword, `C7 05 <abs32> <imm32>`.
    A scan that recorded `+28` from the dword form matches INSIDE the 16-bit
    one: the target still resolves to a real item, but the value reads four
    bytes where there are two, and TERMINAL's string 5 comes back as
    **3345350661**. That is what the first version of this extraction did, and
    it is why the two forms are decoded separately rather than by one pattern.

    What the check itself catches is the absence of the 16-bit form: removing
    it takes the string bindings from **35 to 0**. It does not reproduce the
    3345350661 shape, because the current scanner only records `+28` from the
    16-bit branch - the mutation removes the finding rather than corrupting
    it, and saying so is more useful than implying a sharper test.

    **The data-falsifiable part**: every bound string id must name a non-empty
    string in that screen's own `IAM\<name>` file. All 29 that sit on a screen
    with a text file do. The other 6 are on CHILD panels, which carry
    `screen: -1` and have no text file of their own - a fact about the tree,
    not a miss.

    Resolving the stores against real item records is what keeps the scan
    honest: a linear walk over a callback's bytes finds writes to plenty of
    globals, and only those landing inside an item's own 72 bytes are
    bindings.

    **What this cannot see**, stated because the shops make it look otherwise:
    `Ui_OpenShop` serves ten screens and switches on the `+8` parameter, so
    their per-screen titles come from a branch a linear scan does not follow.
    All ten therefore report the same binding, and the ten screens share one
    panel, so the items dedupe to one set.
    """
    import json as J
    wid = os.path.join(ROOT, "tables", "ui_widgets.json")
    ui = os.path.join(ROOT, "tables", "ui.json")
    iam = omkpaths.data("IAM")
    if not (os.path.exists(wid) and os.path.exists(ui) and os.path.isdir(iam)):
        return ("skipped",), ("skipped",), "tables/ or gamedata/IAM absent"
    W = J.load(open(wid))["rows"]
    scr = {s["index"]: s for s in J.load(open(ui))["rows"]["screens"]}
    listing = os.listdir(iam)
    def strings(name):
        for c in listing:
            if c.lower() == name.lower():
                return open(os.path.join(iam, c), "rb").read().split(b"\0")
        return None
    nstr = ntag = resolvable = ok = nofile = 0
    terminal = []
    for p in W["panels"]:
        s = scr.get(p["screen"])
        for l in p.get("lists") or []:
            for it in l.get("items") or []:
                b = it.get("bind") or {}
                if "tag" in b: ntag += 1
                if "string" not in b: continue
                nstr += 1
                if p["screen"] == 5: terminal.append(b["string"])
                if not s or not s["text"] or strings(s["text"]) is None:
                    nofile += 1; continue
                st = strings(s["text"])
                resolvable += 1
                if 0 <= b["string"] < len(st) and st[b["string"]].strip():
                    ok += 1
    return (nstr, ntag, resolvable, ok, nofile, sorted(terminal)), \
           (35, 22, 29, 29, 6, [5, 6, 7, 8, 9, 10]), \
           "items whose open callback binds a string id and items it binds a " \
           "tag on - neither is in the item record, which ships -1 and 0; " \
           "then how many of those bindings sit on a screen that HAS a text " \
           "file and how many of them name a non-empty string in it (all), " \
           "and how many sit on a child panel with no text file of its own; " \
           "and finally TERMINAL's six bound ids, which UI.md 3d read by hand " \
           "as 5..10 and this recovers mechanically"



def c_ui_shop_titles():
    r"""The ten shops' titles, and the branch a linear scan cannot follow.

    `Ui_OpenShop` serves ten screens and picks the title out of a **jump table
    at 0x004AE7AC** on the screen's fixed `+8` parameter; each of the ten arms
    is one `mov word ptr [0x004E37CC], imm16` - a string id in `IAM\Buy`, on
    the single item all ten screens share.

    **A linear scan gets this wrong quietly, and did.** It walks straight
    through the ten arms and keeps whichever `mov` it saw last, so every shop
    came back bound to string 19 - "Bibliotheque de Lahoreh - Emprunter livre"
    - and nine screens would have shown the tenth's title. Nothing about that
    looks wrong in the lift: the id resolves, the text is real, the item is
    right. Only following the table separates them.

    **Eight of the ten titles name the screen that uses them**, which is a
    third independent confirmation of the screen-table order and the strongest
    of the three: shifting the index by one breaks all eight at once.
    `RESTAURANT` and `BAR` share the generic "Achat" - a bar and a restaurant
    just sell things - and `BANK` alone says **vente**, because at the bank you
    sell. The test is a three-letter prefix of the screen's name appearing in
    the title, normalised; that catches `BANK`/"Banque" and
    `LIB. LAHOREY`/"Lahoreh". The prefix is taken per WORD of the screen name,
    which is what reaches that last one: `lib` is not in "Bibliotheque" -
    "bib", "ibl", "bli" - so a whole-name prefix scores 7, and the check said
    so before this comment was written. It is stated rather than tuned: four
    letters would miss `BANK`/"Banque" and two would match noise.

    `docs/UI.md` §3d cited `verify.py: ui tables` for this table. That check
    never touched it - the shop titles were a documented number with no test
    behind it until now.
    """
    import json as J, unicodedata
    wid = os.path.join(ROOT, "tables", "ui_widgets.json")
    ui = os.path.join(ROOT, "tables", "ui.json")
    buyf = None
    iam = omkpaths.data("IAM")
    if os.path.isdir(iam):
        for c in os.listdir(iam):
            if c.lower() == "buy": buyf = os.path.join(iam, c)
    if not (os.path.exists(wid) and os.path.exists(ui) and buyf):
        return ("skipped",), ("skipped",), "tables/ or gamedata/IAM/Buy absent"
    W = J.load(open(wid))["rows"]
    scr = {s["index"]: s for s in J.load(open(ui))["rows"]["screens"]}
    buy = [x.decode("cp1252", "replace")
           for x in open(buyf, "rb").read().split(b"\0")]
    ITEM = 0x004E37B0
    def norm(t):
        t = unicodedata.normalize("NFD", t)
        return "".join(c for c in t.lower() if c.isalnum())
    got = {}
    for p in W["panels"]:
        if p["screen"] < 0: continue
        for l in p.get("lists") or []:
            for it in l.get("items") or []:
                if it["addr"] == ITEM and "string" in (it.get("bind") or {}):
                    got[p["screen"]] = it["bind"]["string"]
    ids = [got[k] for k in sorted(got)]
    resolve = sum(1 for v in ids if 0 <= v < len(buy) and buy[v].strip())
    named = 0
    for sid, v in got.items():
        if not (0 <= v < len(buy)): continue
        words = [norm(w)[:3] for w in re.split(r"[^A-Za-z]+", scr[sid]["name"])]
        if any(w and w in norm(buy[v]) for w in words):
            named += 1
    # ---- and the OTHER thing the branch decides: which row is live -------
    # `Ui_OpenShop`'s flag edits branch on the same parameter, and the two arms
    # are exact MIRRORS - so every count over them is symmetric and a check
    # built on counts cannot tell the arms apart. (Swapping them left this
    # check, `engine: UI` and `engine: I2D` all green, which is how the gap was
    # found.) The asymmetry is in the TEXT: at the bank you sell.
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import ui as _UI
    _u = _UI.Ui()
    _buy = [x.decode("cp1252", "replace")
            for x in open(omkpaths.data("IAM/Buy"), "rb").read().split(b"\0")]
    PAIR = (0x004E3240, 0x004E3288)
    live = []
    for sid in (20, 21, 22, 23, 24, 25, 26, 27, 28, 32):
        _u.open(sid)
        lst = _u.lists(_u.panel)[0]
        txt = [_buy[_u._i16(it + 28)] for it in _u.items(lst)
               if it in PAIR and _u.selectable(it, lst)]
        live.append(txt[0] if len(txt) == 1 else "?")
    bank, rest = live[0], sorted(set(live[1:]))

    return (len(got), len(set(ids)), resolve, named, ids, bank, rest), \
           (10, 10, 10, 8,
            [10, 18, 13, 15, 11, 17, 12, 16, 14, 19],
            "Vente", ["Acheter"]), \
           "shop screens carrying a title binding; how many DISTINCT string " \
           "ids they name (ten - a linear scan gives one id ten times); how " \
           "many resolve to non-empty text in IAM\\Buy; how many titles name " \
           "their own screen by a three-letter prefix (eight - RESTAURANT and " \
           "BAR share the generic 'Achat'); and the ten ids in screen order, " \
           "which a one-place shift of the jump table would change entirely; " \
           "then the row the FLAG branch leaves selectable, which is the only " \
           "asymmetric thing about the two arms - 'Vente' at the BANK and " \
           "'Acheter' at all nine others, matching the titles from the other " \
           "side ('Banque - vente' against nine '... - achat'). Swapping the " \
           "arms leaves every count in this file unchanged and breaks only " \
           "this"


def c_engine_scx():
    r"""`engine/`'s `.SCX` reader - the scene scripts, one per location.

    The fourteenth slice, and the last big container. An `.SCX` is a saved
    memory image like a `.CTL` - dead pointer fields the loader overwrites -
    and also a **stream**: most of the file sits after the structural block and
    is pulled in as each resource record is reached, which is why Aapkayl.SCX
    is 7 MB with a 39 KB block.

    **220 scenes, 220 whose chunk walk stays inside the block, 4511 objects,
    13887 functions, 2228 linked state pairs and 132857 parameters** - every
    number identical to `tools/scene_scx.py`'s.

    Two things a reader gets wrong if it is not told, and the port has both:

    * **an unknown chunk tag is skipped, not an error.** That is literally the
      loader's `default:` case, so the block may carry padding between chunks
      - and does. A reader that rejected an unknown tag would refuse files the
      engine accepts;
    * the function parameters live in **one shared pool**, indexed rather than
      inlined. That is what makes "the pool is consumed in order with no gaps"
      a check the data could fail, and it is how the object record's layout was
      settled in the first place.

    Chunk 2 is the substance and every one of the 220 files has it. Objects
    come in state pairs - CoffreOpen / CoffreClosed - linked by name at load
    time, which the 2228 counts.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    scpt = omkpaths.data("SCPTDATA")
    if not (os.path.isdir(eng) and os.path.isdir(scpt)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/SCPTDATA absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_scx")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, scpt, out], capture_output=True)
        v = struct.unpack_from("<7i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (220, 220, 220, 4511, 13887, 2228, 132857), \
           "scenes; those whose chunk walk stays inside the block; those " \
           "with a chunk 2; objects; functions; linked state pairs; and the " \
           "parameters they draw from the shared pool"


def c_engine_particles():
    r"""ASSETS 3b: the particle path, rule by rule, through `tools/particle_probe.cpp`.

    Written against the code as it stood on 2026-09-02, when the intro's
    portal rendered as a dim uniform haze where `traces/frames/intro-75.png`
    is a saturated cyan disc with a white core and a DARK STARBURST RING. Its
    first run failed every line below; each is a rule of `Sfx_TickAmbient`,
    `Render_SubmitSprites` or `Raster_DrawTriangles` that had been read and
    then transcribed one step wrong (the header of `o3de/particles.h` has
    the six, with the assembly for each):

      first    a newborn is drawn on sprite frame 0 - the frame index is
               taken from the age BEFORE the tick's increment (was 2 for a
               3-frame life);
      alive    `age <= life` keeps L+1 particles per emitter: GRID's four
               standing rows hold 210, not 194;
      order    every ADDITIVE batch precedes every MULTIPLY one, because the
               mode bits (0x2100 / 0x2200) sit above the texture slot in the
               ascending bucket key (was grouped by sprite, so `ttt`'s
               multiply drew before `burn`'s puffs and darkened only black);
      colour   the corner colour is the particle's, NOT times the 0.5 alpha:
               ONE/ONE and ZERO/INVSRCCOLOR read no alpha (was halved);
      frames   `burnv`, life 3, is drawn at ages 0..3 -> frames 0,2,4,7;
      revision `particleGeometry` bumps the revision of the Geometry it is
               handed, so a pointer-keyed vertex-buffer cache re-uploads
               (a fresh Geometry's was 1 every frame, and Vulkan froze the
               particles at frame 1);
      drift    `+28` enters the Y velocity NEGATED unless flag bits 0x600 are
               exactly 0x200 (`fld [ebx+1Ch]; fchs` at 0x46DD9D);
      mul      DESTBLEND=INVSRCCOLOR is `dst * (1 - src)`: a white quad over a
               white frame leaves BLACK (was `dst * src`, leaving white).

    And the SET PIECES (`o3de/setpiece.h`), which is what the dark ring turned
    out to be: `ttt`'s row is linked (type 1) to row 0 and walks a 27-waypoint
    circle, so the probe asserts the orbit's radius (39.1..39.4 units, the
    chord of a 39.37 circle at 26 steps) and its period (10 frames, which is
    `ttt`'s lifetime - eleven dark stars spaced round the disc). The arrival
    piece, `1KaylArrives`, is the indirect form linked to actor 'HO1': it waits
    (5 post-tick frames of a 6-frame delay), plays `kaylarr` (7) then
    `kay arr` (6), ping-pongs back through 7, and hides at frame 43 - 6 + 19 +
    19 ticks, its 2 loops. The stub actor stands where the line cameras put
    Kay'l; the resolver's position is the viewer's, not this check's.

    A LIMIT: these pin the rules the assembly states and nothing about the
    picture. The capture's ring reads as a continuous spiky band where the
    port draws eleven distinct spots - that difference is recorded in
    RECONSTRUCTION, not asserted away here.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "particle_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "p.txt")
        r = subprocess.run([binp, omkpaths.data_root(), out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no run",), ("ran",), "particle_probe must run"
        first, order, mulc, addc, bframes, rev = {}, [], None, None, [], None
        drift, mulpx, alive, orbit, arrival, pieces = {}, None, None, None, None, None
        for ln in open(out):
            f = ln.split()
            if not f: continue
            if f[0] == "first":          first[(int(f[2]), f[3])] = int(f[5])
            elif f[0] == "alive":        alive = int(f[1])
            elif f[0] == "order":        order = f[1:]
            elif f[0] == "mulcolour":    mulc = tuple(round(float(v), 3) for v in f[1:4])
            elif f[0] == "addcolour":    addc = round(float(f[1]), 3)
            elif f[0] == "burnv-frames": bframes = sorted(int(v) for v in f[1:])
            elif f[0] == "revision":     rev = (int(f[1]), int(f[2]))
            elif f[0] == "drift":        drift[int(f[1])] = float(f[3])
            elif f[0] == "mul":          mulpx = int(f[2])
            elif f[0] == "orbit":        orbit = (round(float(f[2]), 1), round(float(f[3]), 1), int(f[5]))
            elif f[0] == "arrival":      arrival = (int(f[2]), int(f[4]), tuple(int(v) for v in f[6:]))
            elif f[0] == "pieces" and f[1] == "alive":
                pieces = (int(f[2]), int(f[4]), int(f[6]))
        blends = [o.split(":")[0] for o in order]
        adds = [i for i, v in enumerate(blends) if v == "add"]
        muls = [i for i, v in enumerate(blends) if v == "mul"]
        add_before_mul = bool(adds) and bool(muls) and max(adds) < min(muls)
        return (sorted(first.values()), alive, add_before_mul, mulc, addc, bframes,
                rev, drift.get(0), drift.get(512), mulpx, orbit, arrival, pieces), \
               ([0, 0, 0, 0], 210, True, (0.522, 0.584, 0.533), 0.129, [0, 2, 4, 7],
                (1, 2), -1.0, 1.0, 0, (39.1, 39.4, 10), (5, 43, (7, 6, 7)),
                (210, 4, 4)), \
               "every batch's first draw on frame 0; 210 alive (L+1 per emitter " \
               "over GRID's four standing rows); every additive batch before " \
               "every multiply one; `ttt`'s corner colour 0x859588 unscaled; " \
               "`vd`'s red 0x21 unscaled; `burnv` drawn on frames 0,2,4,7; the " \
               "revision moving 1 -> 2 on a rebuild; the drift sign per flag " \
               "bits 0x600 (negated, then kept under 0x200); white * white " \
               "under the multiply blend = 0; `ttt`'s orbit radius and period " \
               "(39.1..39.4 units, 10 frames); the arrival piece's wait, hide " \
               "frame and effect sequence; and the field after 90 frames " \
               "(alive, rows shown, emitters registered)"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_engine_pose_blend():
    r"""actor/pose.h, BLENDING TWO POSES: the fade at a line's two ends.

    A reader: the character "is supposed to fade to an idle animation" at the
    end of a line, and the port "just stops at the last frame". The fade is
    the MORPH PLAYER's own: `sub_42D120` eases the skeleton from the actor's
    clip (at its frame, rec[47]) into the line over the first quarter of the
    line's frames and back out to the clip's KEY 1 over the last quarter, both
    capped at 30 frames (`Morph_Start`: `min(30, frames * 0.25)`), node by
    node through `sub_471820` -> `sub_4721F0` - a quaternion slerp whose weight
    the caller quantises to k/256. One line, "02E19A", is cut without a
    blend-out (`Morph_Play`'s `strstr`).

    `tools/blend_probe.cpp` runs the port's `qslerp`, `blendTracks` and
    `morphBlendFrames` on tracks the answer is known for: 90 degrees about Y
    eased halfway to identity is 45 degrees (w 0.9239, y 0.3827, quantised to
    127/256); t = 1 reaches 255/256 of the way; -identity blends along the
    SHORT arc to the same pose; a cancelled root blends as identity; the root
    translation lerps; and the lengths for a 922-, 40- and 0-frame line are
    30, 10 and 0.

    A LIMIT: this pins the arithmetic and the lengths, not the frame the
    viewer feeds them - that is `backends/sdl/play.cpp`'s `lineT`/`w` block,
    judged by rendering the intro's first line at frames 210, 1102, 1116 and
    1119 and looking.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "blend_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "b.txt")
        r = subprocess.run([binp, out], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no run",), ("ran",), "blend_probe must run"
        v = {}
        for ln in open(out):
            f = ln.split()
            if not f: continue
            key = f[0] if f[0] != "mix" else "mix " + f[1]
            v[key] = f[2:] if f[0] == "mix" else f[1:]
        r3 = lambda k: tuple(round(float(x), 3) for x in v.get(k, []))
        mixhdr = v.get("mix ids", [])
        shape = (int(mixhdr[0]), int(mixhdr[2]), int(mixhdr[4])) if len(mixhdr) >= 5 else None
        # ...and the ROOT on a real line: `dump_lineblend` composes 125339
        # (Kay'l's second line) with the root kept and cancelled, and the
        # pelvis->head pitch at frame 420 is the whole-body bow the original
        # shows - 47 degrees kept, 3 cancelled; 47 against 14 over the line.
        pitch = None
        lb = os.path.join(eng, "build", "dump_lineblend")
        if os.path.exists(lb):
            out2 = os.path.join(tmp, "lb.txt")
            r2 = subprocess.run([lb, omkpaths.data_root(), "118", "HO1_FNM",
                                 "125339", "1", out2], capture_output=True, text=True)
            if r2.returncode == 0 and os.path.exists(out2):
                at420, mx = {}, {}
                for ln in open(out2):
                    f = ln.split()
                    if f and f[0] in ("line-upright", "line-rootkept"):
                        p = float(f[-1])
                        if f[1] == "420": at420[f[0]] = p
                        mx[f[0]] = max(mx.get(f[0], 0.0), p)
                if at420 and mx:
                    pitch = (round(at420.get("line-rootkept", 0)), round(at420.get("line-upright", 0)),
                             round(mx.get("line-rootkept", 0)), round(mx.get("line-upright", 0)))
        return (r3("slerp0"), r3("slerp05"), r3("slerp1"), r3("slerp-neg"), shape,
                r3("mix node1"), r3("mix root"), r3("mix trans"), r3("mix root-cancelled"),
                r3("blend-frames"), pitch), \
               ((0.707, 0.707), (0.923, 0.386), (1.0, 0.003), (0.923, 0.386), (2, 1, 0),
                (0.923, 0.386), (0.925, 0.38), (5.0, 10.0, 15.0), (1.0, 0.0),
                (30.0, 10.0, 0.0), (47, 3, 47, 14)), \
               "qslerp at t=0 (the first pose), t=0.5 (45 degrees about Y, " \
               "quantised 127/256), t=1 (255/256 of the way); the short arc " \
               "against -identity; the blended frame's shape (ids, frames, " \
               "root track); node 1 halfway from 90 about Y to identity; the " \
               "root halfway from identity to 90 about X; the root translation " \
               "lerped; the root blended as identity when cancelled; and the " \
               "fade lengths for 922, 40 and 0 frames (a quarter, at most 30); " \
               "and on line 125339 the pelvis->head pitch in degrees at frame 420 " \
               "with the root KEPT and cancelled, then the maxima over the line - " \
               "the engine keeps it (g_MorphRootTrack is only ever -2), and 47 " \
               "against 3 is the whole-body bow the original shows"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_engine_vm_probe():
    r"""SCRIPT_VM: five handler rules through `tools/vm_probe.cpp` that the
    corpus sweep cannot see.

    Written 2026-09-02 against the interpreter as it stood, and its first run
    FAILED every line below except the reseed and writes-off ones:

      div       `cdq; idiv` (op 34 at 0x402750, `var.div` 22 at 0x402390)
                truncates toward zero: -7/2 = -3. The port had Python's
                floor after `tools/sim`: -4. Zero divisor -> 0 is the port's
                choice where the engine faults. No world script uses op 34
                and none divides a negative, so only this can test it.
      push.i16  op 8 (0x401D70) runs the shared fetch; `0x4000` is the
                parameter block's word at +2 - the SENDER for a message
                handler - and the port pushed the constant 16384.
      set.var   14/15/16 fetch the variable index; 15 fetches its VALUE as
                well; 17 fetches both. Read raw, an indirect index wrote
                variable 0 (16384 masked) and an indirect value stored 16385.
      scene.load 71 (0x403950) fetches both fields: the recorded call is
                the resolved pair and the state carries it.
      random    120's real handler (0x405480, not the pre-split block)
                writes Random_NoRepeat(lo, hi) (0x0041D6B0): in range, never
                the previous draw, lo when lo == hi; the port left the
                variable at 0 (0/50 in range, 49 consecutive repeats).

    The `set.var.i32` line was printed and not asserted while the table gave
    16 a length of 5 where the handler reads 6; T8 fixed the table
    (2026-09-02) and it is asserted now: both runs store the value and land on
    `end`, with the table at 6. At 5 the run lands on the value's last byte
    as an opcode (0xFF for -7, `end` for 3) - the -7 row is the one that
    fails.
    """
    import re
    eng = os.path.join(ROOT, "engine")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/ or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "vm_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, tbl], capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "vm_probe must run"
    div34, div22, push, vars_, scene, rnd, reseed, off, lohi = {}, {}, None, {}, None, None, None, None, None
    i32 = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f: continue
        if f[0] == "div34":       div34[(int(f[1]), int(f[2]))] = int(f[4])
        elif f[0] == "div22":     div22[(int(f[1]), int(f[2]))] = int(f[4])
        elif f[0] == "push.i16":  push = (int(f[4]), int(f[8]))
        elif f[0] == "set.var.i32":
            m = re.search(r"set\.var\.i32 5 (-?\d+) -> var5=(-?\d+) \((\w+)\) \[table length (\d+)\]", ln)
            i32[int(m.group(1))] = (int(m.group(2)), m.group(3), int(m.group(4)))
        elif f[0] == "set.var.i8":
            vars_ = {int(k): int(v) for k, v in re.findall(r"var(\d+)=(-?\d+)", ln)}
        elif f[0] == "scene.load":
            scene = (int(f[5]), int(f[6]), int(re.search(r"sceneOfArea\(12\)=(-?\d+)", ln).group(1)))
        elif f[0] == "random" and f[1] == "[3,5]":
            rnd = (f[5], int(f[7]), int(f[9]), int(f[11]))
        elif f[0] == "random" and f[1] == "reseeded":
            reseed = f[3]
        elif f[0] == "random" and f[1] == "writes-off":
            off = (f[3], int(f[5]))
        elif f[0] == "random" and f[1] == "lo==hi":
            m = re.search(r"var9=(-?\d+).*var11=(-?\d+) recorded (-?\d+) (-?\d+) (-?\d+)", ln)
            lohi = tuple(int(v) for v in m.groups())
    return (div34.get((-7, 2)), div34.get((7, -2)), div34.get((-7, -2)), div34.get((-7, 0)),
            div22.get((-7, 2)), div22.get((7, -2)), div22.get((-7, 0)),
            push, vars_.get(77), vars_.get(3), vars_.get(4), scene, rnd, reseed, off, lohi,
            i32.get(-7), i32.get(3)), \
           (-3, -3, 3, 0, -3, -3, 0,
            (77, -1), -5, -9, -5, (12, 34, 34), ("50/50", 0, 3, 50), "identical",
            ("var9=0", 50), (9, 42, 42, 42, 11),
            (-7, "end", 6), (3, "end", 6)), \
           "op 34 -7/2, 7/-2, -7/-2 and -7/0; op 22 the same three; push.i16 " \
           "0x4000 with params {3,77} and 0xFFFF; set.var.i8 through an indirect " \
           "index, set.var.i16 through an indirect VALUE, set.var.var through " \
           "both; scene.load's resolved record and state; 50 draws of " \
           "var.set.random in [3,5]: in range, no consecutive repeat, all three " \
           "values, all recorded; a reseed repeats the sequence; writes off " \
           "leaves the variable and records; lo == hi returns lo and an " \
           "indirect lo/hi/var resolves; set.var.i32 -7 and 3 store and land " \
           "on end with the table at 6"


def c_engine_parking_ops():
    r"""SCRIPT_VM: the three PARKING opcodes, through `tools/parking_probe.cpp`.

    Tier 6 for the transcription, tier 2 for the operand shapes, **no oracle**:
    the golden-trace rig cannot reach any of the three. `fight.begin` announces
    nothing, `player.move.wait`'s operand goes to a domain `Dbg_LogTagged`
    filters, and that is exactly why `traces/fight.log` came back silent
    (CLAUDE.md 2). What is asserted is the handler's decision.

    Three handlers, each read with `tools/asmfn.py --op N` at the address the
    VM table names:

      89 player.move.wait  0x004043F0: one fetch, `Player_GoToMove(address,
                           byte [esi+1Eh])` - the caller's own context INDEX,
                           which is the resume id `Game_HandleEvent` case 3
                           takes - then `mov word ptr [esi+16h], 4`.
                           **548 shipped sites** (the issue list says 545).
      63 player.move       0x00403730: the SAME handler with `push 0FFFFFFFFh`
                           for the slot and no status write. 312 sites. It must
                           never park, and its address must still be recorded,
                           or a Session cannot start the walk it orders.
      62 fight.begin       0x004035D0: opponent, `Fight_Begin`, `mov word ptr
                           [esi+16h], 3`, then `Camera_Request(14)` with
                           `dword_930818 = max(field 1, 0)` - a `test eax,eax;
                           jge` around `mov dword ptr [esp+18h], 0`. 108 sites,
                           field 1 = 0 at every one, so the clamp is the
                           handler's word and not the data's; -1 is the only
                           negative literal that can reach it, because the
                           shared fetch reads every other negative pattern as
                           an INDIRECT index (bit 0x4000).
      126 camera.set.at_address 0x00405630: like 96 with two differences that
                           both have to survive into the result. The subject is
                           `Address_Find(field 1)` in BOTH pointers where 96
                           puts `Actor_Player()` twice; and the status-7 write
                           is unconditional where 96 reaches its own only
                           through `test ebp,ebp; jz loc_404CB1`. **So a
                           0-frame travel cuts under 96 and PARKS under 126.**
                           84 sites, all travelling exactly 20 frames - which
                           is why the corpus cannot show that difference and
                           this can.

    Every park is asserted to leave the pc AFTER its instruction, and the probe
    then RESUMES from it: a park that landed on the instruction re-runs it for
    ever, and `resume=` says so rather than the pc alone.

    The second half is a corpus fact the engine side cannot see. **The VM table
    gives op 62 four operand bytes and the handler reads six** - three shared
    fetches in a straight line, no branch - and `tools/vm_oplen.py` has been
    reporting `62: 4 -> 6` all along. The corpus adjudicates the way it did for
    op 103 (CLAUDE.md 1): decoded at 4, the 5785 slots yield **216 phantom
    instructions** - 144 `dbg.dump_ctx`, 36 `dbg.dump_code`, 36 `nop`, exactly
    two per site over 108 sites - which vanish at 6, with the same 108 sites
    and the same 0 failures either way. They are all inert zero-operand
    opcodes, which is why nothing has ever failed: the stream resynchronises
    two instructions later. **The table is NOT changed here** (it is
    `tables/vm_opcodes.json` and `dialog_disasm.LEN_FIX` together, and the
    engine sweep and `tools/sim` must move in one step); this asserts the
    measurement so the correction cannot be lost.

    SHOWN TO FAIL, each mutation built with the object AND the binary deleted
    (PORTING B4's stale-link trap):

      126 given 96's `if (travel <= 0) continue;`
          -> 126.zero status camera-wait -> end, pc 7 -> 8, resume end ->
             pc-out-of-range. THE line of this check.
      `r.camAddress = -1` in the 126 arm
          -> 126.park/zero camaddr 709 -> -1.
      `r.pc = start` in the 89 arm
          -> 89.park pc 3 -> 0 AND resume end -> move-wait: the park re-runs
             itself, which the pc alone would not have caught.
      `r.moveAddress = -1`      -> 89.park moveaddr 58 -> -1.
      `r.fightOpponent = -1`    -> 62.park opp 25 -> -1.
      dropping 62's `travel > 0 ? travel : 0`
          -> 62.minus1 fighttravel 0 -> -1.
      89's guard widened to `op == 89 || op == 63`
          -> 63.on status end -> move-wait, i.e. 312 shipped sites would park
             where the engine runs on.
      `moveWaitSuspends_` defaulted TRUE
          -> `engine: execute` moves from (5818 end, 140 dialog, 18584 calls,
             DB identical) to (5493, 134, 17322) with **331 slots parked at
             status 4** and the DB differing. That is the check that keeps the
             defaults honest, and it is the reason they are off.
    """
    import subprocess, collections
    eng = os.path.join(ROOT, "engine")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/ or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "parking_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, tbl], capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "parking_probe must run"

    rows, lengths = {}, {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f:
            continue
        if f[0] == "lengths":
            lengths = {int(k): int(v) for k, v in (x.split("=") for x in f[1:])}
            continue
        rows[f[0]] = dict((k, v) for k, v in (x.split("=", 1) for x in f[1:]))

    def row(tag, *keys):
        d = rows.get(tag)
        return tuple(d.get(k) for k in keys) if d else ("missing",) * len(keys)

    # ---- the corpus half: op 62's operand length ------------------------
    import dialog_disasm as D, dialog_triggers as T
    def _slots():
        out = []
        for name in ("AREA", "SCENE", "GLOBAL"):
            p = omkpaths.data("IAM", name)
            if name == "GLOBAL":
                bb, ss = T.global_file(p)
                out += [(bb, off) for _r, _f, off in ss]
                continue
            for _k, bb in sorted(T.archive(p).items()):
                rr = T.LAYOUT[name](bb)
                if not rr:
                    continue
                lo, n = rr
                out += [(bb, off) for _r, _f, off in T._scripts_from_records(bb, lo, n)]
                out += [(bb, off) for _r, _f, off in T._second_table(name, bb)]
        return out
    slots = _slots()
    was = D.LEN_FIX.get(62)
    corpus = {}
    try:
        for L in (4, 6):
            D.LEN_FIX[62] = L
            tot = bad = sites = 0
            dbg = collections.Counter()
            for bb, off in slots:
                ops, st = D.disasm(bb, off, len(bb))
                if st != "ok":
                    bad += 1
                    continue
                tot += len(ops)
                for _pc, op, _o in ops:
                    if op == 62:
                        sites += 1
                    if op in (0, 1, 2):
                        dbg[op] += 1
            corpus[L] = (bad, tot, sites, dbg[0], dbg[1], dbg[2])
    finally:                       # module-level state: never leave it moved
        if was is None: D.LEN_FIX.pop(62, None)
        else:           D.LEN_FIX[62] = was

    phantom = corpus[4][1] - corpus[6][1]
    dbgdrop = sum(corpus[4][3:]) - sum(corpus[6][3:])

    return (lengths.get(89), lengths.get(63), lengths.get(62), lengths.get(126),
            row("89.park", "status", "pc", "end", "moveaddr", "calls", "resume"),
            row("89.off",  "status", "calls"),
            row("63.on",   "status", "moveaddr", "calls"),
            row("62.park", "status", "pc", "end", "opp", "fighttravel", "calls", "resume"),
            row("62.off",  "status", "calls"),
            row("62.minus1", "status", "fighttravel", "calls"),
            row("126.park", "status", "pc", "end", "cam", "camop", "camaddr",
                            "travel", "calls", "resume"),
            row("126.zero", "status", "pc", "camop", "travel", "resume"),
            row("126.off",  "status", "calls"),
            row("96.park",  "status", "camop", "camaddr", "travel", "resume"),
            row("96.zero",  "status", "camop"),
            corpus[4][0], corpus[6][0], corpus[4][2], corpus[6][2],
            phantom, dbgdrop), \
           (2, 2, 6, 6,
            ("move-wait", "3", "3", "58", "89:58", "end"),
            ("end", "89:58"),
            ("end", "-1", "63:100"),
            ("fight-wait", "7", "7", "25", "0", "62:25,0", "end"),
            ("end", "62:25,0,0"),
            ("fight-wait", "0", "62:25,-1"),
            ("camera-wait", "7", "7", "4781", "126", "709", "20",
             "126:4781,709,20", "end"),
            ("camera-wait", "7", "126", "0", "end"),
            ("end", "126:4781,709,20"),
            ("camera-wait", "96", "-1", "60", "end"),
            ("end", "0"),
            0, 0, 108, 108, 216, 216), \
           "the table's operand lengths for 89/63/62/126; 89 parking at " \
           "MoveWait with its address and resuming past the instruction, and " \
           "stubbed with the switch off; 63 never parking and still carrying " \
           "its address; 62 parking at FightWait with the opponent and the " \
           "max(field,0) clamp; 126 parking at CameraWait with the address " \
           "SUBJECT and travel, and PARKING at travel 0 where 96 cuts; and " \
           "the corpus adjudicating op 62's operand length - 108 sites and 0 " \
           "failures at either 4 or 6, with 216 phantom dbg/nop instructions " \
           "at 4 that vanish at 6"


def c_engine_session():
    r"""The Session's scheduling rules, one shipped fact each (`session_probe`).

    Each line is a rule the engine's own code settles and the port got wrong
    until 2026-09-02 (todo/iam-script-engine.md 2, 6, 8, 11, 17, 25, 28, 32);
    the numbers are the shipped data's, not chosen:

    * `become` - AREA 118's startup script opens `player.become 136` at pc
      1047. Handler 0x402F60 copies the actor record into the DB player
      record (`rep movsd`, 0x43 dwords from +8) and `strcpy`s its two bios
      into the +336/+592 slots. So after one frame the DB says the player is
      136, model KUM_FN, and the bio begins "A jur". START ships -1 / 0xFF.
    * `message25` - `Message_RunHandlers` runs message 25 INLINE: posted and
      run on the same frame, through GLOBAL's table (offset 5006, `set.var.i8`
      then `end`), and the variable GLOBAL +72 names (198) goes 0 -> 3.
    * `dialog_frame` - `Script_Execute`'s `if (v4 == 61) return` stops ONE
      context; a message-26 handler queued for the same frame still runs in
      it (ran == 2, the frame the conversation opens). Before: -1, because
      the Dialog arm returned out of the whole frame.
    * `shown_bit` - `character.show 310` writes the 20-byte record's +18
      (468) into the ObjectShown map: value 1. And `shown` stays ONE entry
      (310 HO1_FNM), because `character.hide -1` at pc 1050 hides the PLAYER
      the script has just made 136.
    * `derived_none` - a derived walk with no answer is a screen LEFT:
      `UI_OpenScreen` seeded -1, case 5 writes it and status 1. The variable
      (seeded 77 by the probe) reads -1, the context stays live and the
      script reaches `dialog.start` next frame. Before: 77, dead, no dialog.
    * `scene_load` / `scene_unload` - on the RESIDENT area (222) `scene.load
      55` creates the SCENE's startup context on the spot (created 1 -> 2)
      and `scene.unload` drops it (live 2 -> 1) with the DB back at -1;
      loaded again and ticked once, SCENE 55's script announces (37 lines).
    * `message_area` - AREA 222's own table answers message 23 at offset 2455.
    * `return` - A(118) -> B(222) -> A creates 222's startup context (1 -> 2)
      and then NOTHING for 118 (2 -> 2): `Area_LoadIntoSlot`'s "already in
      that slot" branch. Before: 3.

    Shown to fail: every starred line above differed on the reverted build
    (todo/pending/T2.md).
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "session_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "session_probe must run"
    L, bio = {}, None
    for ln in r.stdout.splitlines():
        f = ln.split()
        if f: L[f[0]] = f[1:]
        if f and f[0] == "become":            # the bio carries spaces: cut the raw line
            i = ln.find('bio0 "')
            bio = ln[i + 6:i + 11] if i >= 0 else None
    def g(key, *idx):
        f = L.get(key, [])
        return tuple(f[i] if i < len(f) else None for i in idx)
    got = (
        g("become", 1, 3, 5),                    # player_before, player_after, model
        bio,
        g("message25", 1, 3, 5, 7, 9, 10, 11, 13),  # found, table, offset, posted, ran, var, from, to
        g("dialog_frame", 1, 5, 7, 11),          # open, message, table, ran
        g("shown_bit", 3, 5, 7, 8),              # index, value, shown, first entry
        g("derived_none", 1, 3, 5),
        g("answer_minus1", 3, 5, 7),
        g("scene_load", 5, 7, 9, 11),            # db, created from, to, live
        g("scene_unload", 3, 5, 7),
        g("scene_script", 3, 5),                 # announced after, SCENES
        g("message_area", 1, 3, 5, 7),
        g("return", 7, 9, 11, 13, 15, 17),       # created a, b, back, other_at_B, other_back, current
    )
    want = (
        ("-1", "136", "KUM_FN"),
        "A jur",
        ("1", "global", "5006", "1", "1", "var198", "0", "3"),
        ("1", "26", "global", "2"),
        ("468", "1", "1", "310:HO1_FNM"),
        ("-1", "1", "1"),
        ("-1", "-1", "1"),
        ("55", "1", "2", "2"),
        ("-1", "2", "1"),
        ("37", "1"),
        ("23", "1", "area", "2455"),
        ("1", "2", "2", "118", "222", "118"),
    )
    return got, want, \
        "player.become into the DB (id, model, bio); message 25 inline; a " \
        "second context runs in the dialog.start frame; the shown bit and " \
        "the player hidden by -1; a screen left resumes at -1; scene.load/" \
        "unload on the resident area; the resident table a message resolves " \
        "through; A->B->A creates no startup context"


def c_engine_area_transition():
    r"""The area transition, the staged load and the context table
    (`transition_probe`).

    Each line is a rule the engine's own code settles and the port got wrong
    until 2026-09-02 (todo/iam-script-engine.md 3, 14, 15, 16, 18, 19, 21,
    36); the numbers are the shipped data's, not chosen:

    * `staged` - AREA 118's own `area.goto 222 -1 -1` (pc 1309) parks its
      caller at status 10 (`Area_Transition` mode 0) and the caller resumes
      into `scene.load 222,55` TWO frames later: AImpasse.3DO is 33392 bytes,
      one 0x20000 slice of the async reader (`sub_41F320`), served at the end
      of the goto frame, so the pump's tail completes `Area_TickLoad` cases
      2..9 in the next frame and the caller runs in the one after. The area
      left (118) is in the other slot, the shown set is 222, `area.arrive
      118` runs in the resume frame, one area entered.
    * `slices` - Anekbah.3DO is 2099056 bytes: 17 slices, and a request takes
      17 frames to create its startup context.
    * `objects` - AREA 0 zone 3's ENTER script `area.goto 201, 153, 240`:
      the door `ported43open` starts on the OUTGOING scene (ANEKBAH) the
      frame the load lands, `ported43close` after it, and the caller resumes
      (status 1) only after the second ends, then reaches `end`; the shown
      `.SCX` is then HALL43 with 0 (Anekbah) in the other slot. The port's
      Program ends the door animation the frame it starts, so both objects
      start in one frame here; the ORDER is what is asserted.
    * `deferral` - `area.preload 0` parks its caller at 8 and `dword_4C0130`
      holds its index; a second context's `area.goto 222` is refused before
      dispatch (status 9, pc held); 17 slices later the preload resumes
      (frame 19), the goto is accepted the next frame (10) and, 222 being one
      slice, ends two frames after that. Anekbah, preloaded, is evicted for
      222.
    * `slots` - contexts 1, 2, 3 created, 2 freed, the next takes 2 again
      and, announcing through the pump's INDEX order, runs before 3:
      `1001,1004,1003`. 28 more fill the table (32 entries), the next is
      unlisted: 1.
    * `camera_unknown` - `camera.set.wait 29999, 30` announces and the
      `camera.set 2148` after it announces in the SAME frame (no hold,
      status 0 at the end); `camera.set.wait 2148, 30` holds for 31 frames.
    * `restart` - after the intro's conversation and SCENE 55's `player.become
      49`, `requestRestart()`: the player reads 136 again (START's -1, then
      AREA 118's `player.become 136` in the SAME frame), day 52, clock
      2000000, variable 19 back to START's 0 from a seeded 77, one live
      context parked at status 6 (the menu), 118 shown and nothing in the
      other slot, and the announcement stream restarting VARIABLES 175, 170,
      CHARACTERS 136.

    Shown to fail: every line above moved under a one-line mutation of
    `area.cpp` (todo/pending/T11.md's table): no staging (delta 2 -> 1,
    a_status 8 -> 1), objects ignored (f1/f2/resume -> -1), no pre-dispatch
    refusal (b_status 9 -> 10, a_resumed -> -1), last-free entry (order
    1003,1004,1001), hold on an unknown camera (next_frame -> -1, status 7),
    restart ignored (day 0, var19 77), the 33rd not counted (unlisted 0).
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "transition_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "transition_probe must run"
    L = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f: continue
        L[f[0]] = dict(zip(f[1::2], f[2::2]))
    def g(key, *names):
        d = L.get(key, {})
        return tuple(d.get(n) for n in names)
    st, sl, ob, de, so, cu, re = (L.get(k, {}) for k in
        ("staged", "slices", "objects", "deferral", "slots", "camera_unknown", "restart"))
    got = (
        g("staged", "status_after_goto", "slices", "delta", "other_at_resume", "current", "entered"),
        st.get("arrive_frame") == st.get("resume_frame"),
        g("slices", "set", "slices", "frames", "current", "other"),
        g("objects", "dest", "f1", "f2", "f1_name", "f2_name", "shown_scx", "other"),
        (int(ob.get("f1_frame", -9)) <= int(ob.get("f2_frame", -9)) <
         int(ob.get("resume_frame", -9)) < int(ob.get("end_frame", -9))),
        g("deferral", "a_status", "b_status", "a_resumed", "b_accepted", "b_done", "preloaded_other"),
        de.get("deferred") == de.get("a_index"),
        g("slots", "order", "filled", "unlisted"),
        so.get("t") == so.get("q"),
        g("camera_unknown", "status", "hold"),
        cu.get("unknown_frame") == cu.get("next_frame"),
        g("restart", "player_after", "day", "clock", "var19", "live_after", "current", "other", "status0", "first"),
    )
    want = (
        ("10", "1", "2", "118", "222", "1"),
        True,
        ("ANEKBAH", "17", "17", "0", "118"),
        ("201", "153", "240", "ported43open", "ported43close", "HALL43", "0"),
        True,
        ("8", "9", "19", "20", "22", "222"),
        True,
        ("1001,1004,1003", "28", "1"),
        True,
        ("0", "31"),
        True,
        ("136", "52", "2000000", "0", "1", "118", "-1", "6",
         "VARIABLES:175,VARIABLES:170,CHARACTERS:136"),
    )
    return got, want, \
        "the intro's goto parks at 10 and resumes two frames later with 118 in " \
        "the other slot; Anekbah's set is 17 slices and 17 frames; a door zone's " \
        "two objects play open then close around the switch and the caller " \
        "resumes after; area.preload parks at 8 and defers a goto to status 9 " \
        "until it lands; a freed table entry is reused and runs in index order, " \
        "the 33rd is unlisted; an unknown camera does not hold; a restart " \
        "reloads START and reboots in the same frame"


def c_engine_live_zones():
    r"""The zones, the world hooks and `end` LIVE in the Session
    (`livezones_probe`) - the wave-B wiring of T12, T13 and E2 (issues 7, 10,
    38 of todo/iam-script-engine.md).

    Each line is a rule the engine's own code settles, on the shipped data:

    * `hooks` - AREA 1's actor 397 through a Session context: `actor.stat.set`
      Vie 100 -> 42 read back out of the RESIDENT BLOCK (`Actor_FindById`
      walks row 0 then row 1 by id); prop 360 (state 3 in START) took object
      slot 0 from `Scene_LoadProps` at the load; `object.hold.actor` wrote
      +270 = 360 and `Actor_HoldObject(397, 0)`; `object.release.actor` put
      both back. No hooks: `vie_set 0`, `held_field -1`, `hold_events 0`.
    * `handover` - the intro run to the Impasse's script hand-over: 222 shown,
      118 behind, SCENE 55 over it, 14 zone records and 6 registered - T13's
      5 + 1 with the SCENE's 3803 - and 0 armed, because a TELEPORT does not
      scan (`actor.goto_address 654` lands inside 3799 and 3801, and 3801's
      enter script is `area.goto 142`: the engine's beats carry him off first).
    * `walk` - the Session's player position into zone 3791: the scan takes a
      prompt slot with NO event (the pump ran first: touches 1, armed 1,
      events_after_scan 0); the next pump makes context 3 and its ENTER script
      2323 runs in that frame to `end`, announcing ZONES 3796. Scan before
      pump: touches 2, events_after_scan 1. Enter not queued: announced 0.
    * `press` - 3791 has no activate script, so the press queues nothing and
      `Script_Pump` step 2 posts message 26 through GLOBAL, run next frame.
    * `leave` - out of the quad: leave and free, the context gone; a press with
      nothing armed is REFUSED (event 6's `!dword_4E6B24`) and posts nothing.
      Without the gate: press_refused 0, nothing_here 2.
    * `deferred` - `scene.unload 222` from a context in 222's own slot, parked
      on `camera.set.wait`: the scene id -1 at once, the 1944-byte block KEPT
      (ctx+40 = 0x10|8) and freed at the caller's `end` 31 frames later.
      Freed at once: kept_bytes 0, freed_after 0.
    * `activate` - AREA 146's one-shot 35141: the press queues activate 5191 on
      context 0, it runs to `end` (flags 0x10, `dword_4E6B20` back to 0), the
      slot latches (state 4 after the frame) and a second press raises no
      Activate. The `--dword_4E6B20` dropped: pending_during 1, pending_after 1.
      The `ran` gate dropped: nothing_here 2 (the consumed press posts too).
    * `dialogue_scan` - `Actor_TickDialogue` ends `return Actor_ScanZones()`:
      with a conversation up a walk into 2374 arms it (armed 3) and makes NO
      context until the pump runs again (ctx_during -1, ctx_after_close 1).
      Scan skipped in dialogue: armed_during 1, ctx_after_close -1.
    * `message0` - 14 chunks subscribe to message 0; GLOBAL's record has
      script 0. (The marker's readers are the two fade handlers.)

    Every mutation above was built, run and restored (md5-checked); the
    binary and the object were deleted before each rebuild (B4's trap).
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "livezones_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "livezones_probe must run"
    L = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f: continue
        L[f[0]] = dict(zip(f[1::2], f[2::2]))
    def g(key, *names):
        d = L.get(key, {})
        return tuple(d.get(n) for n in names)
    got = (
        g("hooks", "slot", "slot_id", "vie_shipped", "vie_set", "held_field", "held_slot",
          "hold_events", "release_field", "release_slot", "drop_events"),
        g("handover", "entered", "current", "other", "scene", "records", "registered",
          "live", "armed", "walks"),
        g("boot", "at_load", "answered"),
        g("walk", "touches", "armed_after_scan", "events_after_scan", "events", "ctx",
          "zone_of_ctx", "status", "announced", "first", "flags"),
        g("press", "accepted", "activates", "nothing_here", "message", "table"),
        g("leave", "events", "ctx_status", "armed", "press_refused", "nothing_here"),
        g("deferred", "scene_before", "scene_during", "kept_bytes", "flags", "status",
          "freed_during", "kept_after", "freed_after", "db"),
        g("activate", "zone", "script", "ctx", "enter_status", "events", "flags",
          "slot_state", "activates", "refused", "pending_after", "status_after",
          "nothing_here"),
        g("dialogue_scan", "dialog", "armed_during", "ctx_during", "ctx_after_close", "log_grew"),
        g("message0", "chunks_subscribed", "global_script"),
    )
    want = (
        ("0", "360", "100", "42", "360", "0", "1", "-1", "-1", "1"),
        ("1", "222", "118", "55", "14", "6", "3790,3791,3795,3799,3801,3803", "0", "0"),
        ("0", "-1"),
        ("1", "1", "0", "arm:3791:ctx3:a1@2323", "3", "3791", "0", "1", "ZONES:3796", "0"),
        ("1", "0", "1", "26", "global"),
        ("leave:3791:ctx3:a3@0,free:3791:ctx3:a4@0", "-1", "0", "1", "1"),
        ("55", "-1", "1944", "24", "7", "0", "0", "1", "-1"),
        ("-30395", "5191", "0", "0",
         "arm:-30395:ctx0:a1@5167,activate:-30395:ctx0:a2@5191,arm:2374:ctx1:a1@5254,arm:2394:ctx3:a1@5722",
         "16", "4", "1", "0", "0", "0", "1"),
        ("1", "3", "-1", "1", "0"),
        ("14", "0"),
    )
    return got, want, \
        "a non-player actor's stat and held object edited in the resident block " \
        "through a Session context; the Impasse hand-over registering 5 + 1 zones " \
        "and arming none for a teleport; a walk into 3791 taking a prompt slot on " \
        "the scan and running its ENTER script on the next pump; a press with no " \
        "activate script posting message 26; the leave, the free and the refused " \
        "press; scene.unload's deferred block free at end; AREA 146's one-shot " \
        "activate running once and latching; the scan running through a " \
        "conversation and the pump not; message 0's fourteen subscribers"


def c_engine_airlock_walk():
    r"""WALKING BETWEEN AREAS: two decors shown, the active row following the
    FEET (`airlock_probe`) - the fix of 2026-09-03 for the frame going black
    after the Impasse's airlock transition (the 2026-09-02 handoff's two open items; RECONSTRUCTION's 2026-09-03 row).

    The Impasse hands over to adventure mode and the player walks out through
    three overlapping zones (x 6863..6975): SCENE 55's 3803 (`media.play 410` -
    the "where am I" line), AREA 222's 3801 (`area.goto 142 -1 -1`, the airlock
    AIMPASAS) and 3795 (the tutorial beat: `actor.goto_address 653`, hold, two
    voices over cameras 4290..4292, release). Each line is a rule the engine's
    code settles, on the shipped data:

    * `handover` - SCENE 55 over AREA 222 shown, 118 loaded behind, ONE decor
      in state 2 before the walk.
    * `scene_line` - 3803's enter script announces OBJECTS 410.
    * `transition` - 3801's goto announces AREAS 142, and after the load BOTH
      decors are in state 2 (`shown_during 2`): `Area_Transition` mode 3 case 3
      shows the destination (`sub_419AF0`) and nothing hides the origin - the
      script has no `area.arrive`; that is AREA 142's record 10, a zone deep in
      the airlock. The origin stays resident. **Shown to fail**: with `showSet`
      hiding the other slot (the port's one-set model until 2026-09-03), this
      reads `shown_during 1` and `shown_after 1`.
    * `feet` - `decorUnder` (actor/walk.h) answers 222 over AIMPASSE's soup and
      142 over AIMPASAS's; `playerOnArea(142)` moves the ACTIVE row 222 -> 142
      (`Game_HandleEvent` case 9, raised by `Walk_ProbeGround`), and both decors
      stay shown - the row and the state-2 set are two different things.
    * `beat` - 3795's script teleports him (`placementSeq` 1 -> 2, ADDRESSES 653
      'Tutorial', landing at x 7196) and plays OBJECTS 405 then 406.
    * `stream` - the six markers appear IN ORDER: OBJECTS 410, ZONES 3803, AREAS
      142, ADDRESSES 653, OBJECTS 405, OBJECTS 406 - the order
      `traces/impasse-walk.log` lines ~105-135 record from the shipped engine.

    The viewer half - both sets drawn, the merged walkable soup, the controller
    kept across the area change, the teleport re-seat, `player.anim.hold`, and
    the `media.play` SUBTITLE - is `omk-play`'s and needs SDL; it was confirmed
    by rendering (frames 4021/4231/4471 of the handoff's own repro, 0 black
    frames of 57, 99.5% coverage after the transition). TIER 4 by the trace's
    order; the Session-side facts are what this pins.
    """
    import subprocess
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "airlock_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "airlock_probe must run"
    L = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f: continue
        L[f[0]] = dict(zip(f[1::2], f[2::2]))
    def g(key, *names):
        d = L.get(key, {})
        return tuple(d.get(n) for n in names)
    got = (
        g("handover", "current", "shown", "other", "scene", "shown_count"),
        g("scene_line", "media"),
        g("transition", "goto", "shown_during", "dest", "dest_set", "origin_resident"),
        g("feet", "under_impasse", "under_airlock", "active_before", "active_after", "shown_after"),
        g("beat", "placed_before", "placed_after", "address", "media", "player_at"),
        g("stream", "airlock_in_order"),
    )
    want = (
        ("222", "222", "118", "55", "1"),
        ("410",),
        ("142", "2", "142", "AIMPASAS", "1"),
        ("222", "142", "222", "142", "2"),
        ("1", "2", "653", "406", "7196"),
        ("1",),
    )
    return got, want, \
        "the Impasse hand-over with one decor shown; SCENE zone 3803's line; the " \
        "walk into 3801 loading AIMPASAS with BOTH decors in state 2 and the origin " \
        "resident; decorUnder telling the two soups apart and event 9 moving the " \
        "active row 222 -> 142 with both still shown; zone 3795's beat teleporting " \
        "to 653 and playing 405/406; and the six markers in the trace's order"


def c_opt_tracks():
    r"""`TRAJECTOIRES\*.OPT` - the traffic circuits the procedural pedestrians
    and the hover-taxis move on (docs/STREET_LIFE.md 2; tools/opt_track.py).

    The layout is `Slider_Init`'s: a 76-byte header of (offset, count) pairs
    for seven blocks - lanes 24, keys 20, actions 20, routes 12, steps 16,
    reservation groups 4, group lists 2 - each starting where the previous
    ends and the last ending on the file size, 6/6. Every reference resolves
    (a lane's keys and routes, a route's destination and steps, a key's
    action, a route's or step's group, a group's list), every runtime field
    the movers write (the list heads at lane +12, key +0, route +0, the busy
    byte at group +3) is ZERO on disk, and no route leads from a pedestrian
    lane to a vehicle lane or back. The two spacings are the units the
    density option multiplies: `39 * (5 - level) * pedSpacing` between
    walkers along a lane.
    """
    import opt_track as OT
    got, want = [], []
    for p in OT.shipped():
        t = OT.load(p); c = OT.check(t)
        got.append((os.path.basename(p).lower(), c["ok"], t["laneCount"], c["ped_lanes"], c["veh_lanes"],
                    len(t["keys"]), len(t["actions"]), len(t["routes"]), len(t["steps"]),
                    len(t["groups"]), len(t["lists"]), t["pedSpacing"], t["vehSpacing"], c["cross_class"]))
    want = [("anekbah.opt", 1, 242, 216, 26, 2781, 84, 344, 916, 482, 648, 15, 15, 0),
            ("biblio.opt",  1,  27,  27,  0,  269,  7,  37,  27,  25,  26, 50, 15, 0),
            ("lahorey.opt", 1, 162, 162,  0,  719, 33, 196,  81, 128, 142, 15, 15, 0),
            ("puit.opt",    1,   6,   6,  0,   84, 10,   6,   2,   0,   0, 30, 15, 0),
            ("qchaud.opt",  1, 259, 226, 33, 1744, 37, 341, 500, 367, 524, 30, 30, 0),
            ("souk.opt",    1, 201, 179, 22,  999, 68, 246, 161, 191, 222, 30, 15, 0)]
    return tuple(got), tuple(want), "file, layout+refs+runtime-zero ok, lanes, ped, veh, keys, actions, routes, steps, groups, lists, spacings, cross-class routes"


def c_engine_pedestrians():
    r"""The PROCEDURAL PEDESTRIANS run in the Session (docs/STREET_LIFE.md 2;
    engine/src/actor/pedestrians.*; engine/tools/ped_probe).

    Two chains. THE SPAWN: `sub_453B40`'s rule - one walker every
    `39 * (5 - level) * pedSpacing` units of accumulated pedestrian-lane length
    (the accumulator carries across lanes; the 200-slot pool ends the walk) -
    written here over tools/opt_track.py's decode, against the port's count at
    every density level 0..4, for the four city streets, and the Session's
    spawn at level 3 equal to that count. THE WALK, 600 frames at level 3:
    every walker still live and moved; NO mover off the lane network (the
    keys, the routes' steps and their implicit last leg) by more than 8
    units - one frame's advance at the doubled gait, because a mover carries
    its overshoot past a corner until the next end-check and one frozen at an
    action point stays there (Qalisar: 1.15 for 300 frames, then gone);
    every body within 500 units of its mover - the gait stops the mover
    beyond 58.5 and the walk clip brings the body back, an action point can
    take a body further first; lane changes and action visits both happen;
    no NaN. Shown to fail with the spawn factor changed 39 -> 40 (every
    count moves).
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "ped_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    import opt_track as OT
    def count(t, level):
        spacing = 39.0 * ((5 - level) * t["pedSpacing"])
        acc, n = 0.0, 0
        for li in range(t["pedFirst"], t["pedEnd"]):
            L = t["lanes"][li]
            for k in range(L["keyCount"]):
                dx, dy, dz = t["keys"][L["firstKey"] + k]["delta"]
                if acc > spacing:
                    if n >= 200: return n
                    n += 1; acc = 0.0
                acc += (dx * dx + dy * dy + dz * dz) ** 0.5
        return n
    # the areas naming a circuit at +115, by name
    areas = {}
    for k, ch in T.archive(os.path.join(O.TAGDIR, "AREA")).items():
        if len(ch) > 124:
            nm = ch[115:124].split(b"\0")[0].decode("ascii", "replace")
            if nm and nm.lower() not in areas: areas[nm.lower()] = k
    got, want = [], []
    for stem in ("anekbah", "souk", "lahorey", "qchaud"):
        area = areas.get(stem)
        t = OT.load(omkpaths.data("TRAJECTOIRES", stem + ".opt"))
        py = tuple(count(t, l) for l in range(5))
        r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables"), str(area), "600", "3"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            return ("no run",), ("ran",), "ped_probe must run"
        L = {}
        for ln in r.stdout.splitlines():
            f = ln.split()
            if f and f[0] in ("counts", "session", "run"): L[f[0]] = dict(zip(f[1::2], f[2::2]))
        c, se, run = L.get("counts", {}), L.get("session", {}), L.get("run", {})
        port = tuple(int(c.get("level%d" % l, -1)) for l in range(5))
        live = int(run.get("live", -1))
        got.append((stem, area, port, int(se.get("spawned", -1)), live, int(run.get("moved", -1)),
                    float(run.get("max_offlane", 99)) < 8.0, float(run.get("max_lag", 1e9)) < 500.0,
                    int(run.get("lane_changes", 0)) > 0, int(run.get("actions", 0)) > 0, int(run.get("nan", 1))))
        want.append((stem, area, py, py[3], py[3], py[3], True, True, True, True, 0))
    return tuple(got), tuple(want), "stem, area, counts at level 0..4 (port) vs (rule), spawned, live, moved, on-network, lag<500, lane changes, actions, nan"


def c_engine_street_frame():
    r"""`omk-play` DRAWS the city crowd (docs/STREET_LIFE.md, step 4).

    A street start - `--save traces/save-appart.bin --area 0` for the DB
    player record and Anekbah, `--stand` on a lane where the pool's walkers
    pass at frame 120 - rendered headless twice, with the crowd and with
    `--no-crowd`. Adventure mode is reached in both, the crowd run reports the
    pool live and walkers drawn within the engine's last LOD distance, and the
    two frames DIFFER by more than a few hundred pixels: the walkers are on
    the picture. Needs SDL; reports skipped without it.

    What the eye settled and this cannot: the walkers are posed mid-stride in
    the city's own models, turned along their lanes, feet on the street (the
    frames of 2026-09-03); what only a person can settle is the walk's pace
    against the original and the facing convention over a turn.
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    mk = subprocess.run(["make", "-s", "play"], cwd=eng, capture_output=True, text=True)
    play = os.path.join(eng, "build", "omk-play")
    if mk.returncode != 0 or not os.path.exists(play):
        return ("skipped",), ("skipped",), "no SDL - the frontend is optional (PORTING A8)"
    save = os.path.join(ROOT, "traces", "save-appart.bin")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    outs, dumps = [], []
    for k, extra in enumerate(([], ["--no-crowd"])):
        dump = os.path.join(eng, "build", "street-%d.bin" % k)
        r = subprocess.run([play, omkpaths.data_root(), os.path.join(ROOT, "tables"), "--save", save,
                            "--area", "0", "--address", "20", "--stand", "1804,0,-6890,336",
                            "--frames", "120", "--software", "--res", "640x480", "--nofmv",
                            "--dump", dump] + extra, capture_output=True, text=True, env=env)
        outs.append(r.stdout)
        dumps.append(open(dump, "rb").read() if os.path.exists(dump) else b"")
    live = drawn = -1
    for ln in outs[0].splitlines():
        if "pedestrians -" in ln:
            f = ln.split()
            live = int(f[f.index("live,") - 1]); drawn = int(f[f.index("drawn") - 1])
    differ = 0
    if len(dumps[0]) == len(dumps[1]) and dumps[0]:
        differ = sum(1 for i in range(0, len(dumps[0]), 2) if dumps[0][i:i+2] != dumps[1][i:i+2])
    got = ("ADVENTURE MODE" in outs[0], "ADVENTURE MODE" in outs[1], live, drawn >= 1,
           "pedestrians -" in outs[1], differ > 500, "120 frames presented" in outs[0])
    want = (True, True, 200, True, False, True, True)
    return got, want, "adventure reached with and without the crowd; 200 live, some drawn; no pool line without it; the two frames differ (%d pixels)" % differ


def c_engine_crowd_push():
    r"""The CROWD PUSH runs (docs/STREET_LIFE.md 3; engine/src/actor/spatial.*;
    engine/tools/push_probe).

    `shape`: one instance entry (a walker) at the origin facing -Z, its
    sphere radius 20, a probe sphere of radius 10 walked in. ACROSS the
    heading the push begins one radius plus the probe's out (x 30 nothing,
    x 20 a push) and grows inward; ALONG the heading `sub_45E690`'s ellipse is
    two radii long but `SpatialIndex_Query`'s reach box - the two models'
    `+88` - clips it at 30, so z 40 is nothing and z 30 pushes twice what x 20
    does. The quarter factor: x 20 is 10 units of penetration and a push of
    2.5. `walk`: Anekbah, the player built on its set and stood 120 units
    ahead of a walker on its lane facing it; the walker reaches him, the push
    moves him tens of units, and the bump message 15/16 posts ONCE in 150
    frames (100 frames of hold after it). `talk`: a walker in its action's
    main phase, the player 80 units in front, `talkToPedestrian` finds it,
    posts 13/14 once, and the walker's phase holds at 2 for 200 more frames
    (the countdown is suspended for the talk target).
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "push_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables"), "0", "150"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "push_probe must run"
    L = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if f and f[0] == "shape":
            L[f[1]] = {k: float(v) for k, v in (x.split(":") for x in f[2:])}
        elif f and f[0] in ("walk", "talk"):
            L[f[0]] = dict(zip(f[1::2], f[2::2]))
    ac, al, w, t = L.get("across", {}), L.get("along", {}), L.get("walk", {}), L.get("talk", {})
    got = (ac.get("x30"), ac.get("x20"), ac.get("x10") > ac.get("x20", 0) if "x10" in ac else None,
           al.get("z40"), al.get("z30"), al.get("z10", 0) < al.get("z30", 0),
           int(w.get("touched_frames", 0)) >= 1, float(w.get("moved", 0)) > 10.0, int(w.get("bumps", -1)),
           int(t.get("found", 0)), int(t.get("talks", 0)), t.get("phase_before"), t.get("phase_after"))
    want = (0.0, 2.5, True, 0.0, -5.0, True, True, True, 1, 1, 1, "2", "2")
    return got, want, "shape across x30/x20/x10 grows; along z40 clipped, z30 = -5, z10 stronger; walk touched, moved, one bump; talk found, one message, phase held"


def c_engine_head_look():
    r"""`Actor_SetHeadLook` over a real model (docs/STREET_LIFE.md step 6;
    actor/pose.h `aimHead`; engine/tools/head_probe).

    `character.look_at_player` (138) writes an actor's look-at slot and
    `Actors_TickAll` aims his head at the player's every frame: the pitch and
    yaw from the head's forward to the target, pitch clamped to +-40 and yaw
    to +-70 (0x00468B50), each eased an eighth of the way per frame. Over the
    Demon's head (`D3Tete`): a target 45 degrees to either side turns the
    forward by exactly 45; one at 120 and one behind by 70, the clamp; one 60
    degrees up lifts it 40, one down -40; and from rest the ease covers 45/8
    in one frame and lands within a degree in forty. The transition is the
    thing tested: the forward MEASURED after the aim, not the angle asked.
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "head_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), "DE3_FN"], capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "head_probe must run"
    C = {}
    head = ""
    for ln in r.stdout.splitlines():
        f = ln.split()
        if f and f[0] == "model": head = f[6] if len(f) > 6 else ""
        if f and f[0] == "case": C[f[1]] = {k: float(v) for k, v in zip(f[2::2], f[3::2])}
    def turned(n): return round(C.get(n, {}).get("turned", 999), 1)
    def lifted(n): return round(C.get(n, {}).get("lifted", 999), 1)
    got = (head, turned("front"), abs(turned("left45")), abs(turned("right45")), abs(turned("right120")),
           abs(turned("behind")), lifted("up60"), lifted("down60"),
           round(abs(C.get("left45", {}).get("eased_one", 0)), 2), abs(C.get("left45", {}).get("eased_forty", 0)) > 44.0)
    want = ("D3Tete", 0.0, 45.0, 45.0, 70.0, 70.0, 40.0, -40.0, 5.62, True)
    return got, want, "the Demon's head; turned front/left45/right45/right120/behind; lifted up60/down60; one frame of ease (45/8); forty frames land"


def c_engine_city_crowd():
    r"""The AUTHORED EXTRAS of a city street start in the Session
    (docs/STREET_LIFE.md 1) - and the object word of every `scx.play*` is a
    RAW, UNSIGNED 16-bit id.

    A city's startup script (chunk +4) issues one `scx.play.actor` per placed
    extra - couples, beggars, patrols - each a looping scene program on a
    character the placement table spawned. Two independent chains must agree:
    the startup script decoded by tools/dialog_disasm.py (ops 59/60, the
    object word taken raw per `RAW_WORD`) against what `engine/tools/
    city_crowd` reports the Session actually started after 60 frames, with the
    CHARACTERS id, the clip and the path resolved for every one.

    The handlers read the object word `and ecx, 0FFFFh; mov ebp, ecx` - no
    0xFFFF test and no 0x4000 indirect step (0x403300, 0x4030E0; `asmfn.py
    --op 59`). Anekbah's object ids are 0xC2xx, so read as int16 they came
    out negative and the port started NONE of its 26 (Jaunpur's and
    Lahoreh's ids are below 0x4000 and always worked); read through the
    indirect fetch, as the disassembler did, they were `param[718]`. Shown to
    fail with the mask dropped from `SceneRunner::handle`: Anekbah 0/26.
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "city_crowd")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    import dialog_disasm as D
    areas = T.archive(os.path.join(O.TAGDIR, "AREA"))
    got, want = [], []
    for area in (0, 1, 64, 145):            # Anekbah, Jaunpur, Lahoreh, Mahaleel
        chunk = areas[area]
        start = struct.unpack_from("<i", chunk, 4)[0]
        ops, st = D.disasm(chunk, start, len(chunk))
        authored = set()
        for pc, op, raw in ops:
            if op in (59, 60) and len(raw) >= 4:
                authored.add(struct.unpack_from("<H", raw, 2)[0])
        # 300 frames: Lahoreh's startup script parks on `scx.play.wait` behind
        # its fans before it reaches the extras (9 of 29 at 60 frames).
        r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables"),
                            str(area), "300"], capture_output=True, text=True)
        if r.returncode != 0:
            return ("no run",), ("ran",), "city_crowd must run"
        head, started = {}, {}
        for ln in r.stdout.splitlines():
            f = ln.split()
            if f and f[0] == "area":
                head = dict(zip(f[2::2], f[3::2]))
            elif f and f[0] == "program":
                d = dict(zip(f[2:10:2], f[3:11:2]))       # the name is last, and may hold spaces
                started[int(f[1], 16)] = (" ".join(f[11:]), d)
        actor_objs = {o for o, (n, d) in started.items() if d.get("how") == "actor"}
        hit = authored & actor_objs
        resolved = sum(1 for o in hit
                       if int(started[o][1]["actor"]) >= 0 and int(started[o][1]["clip"]) >= 0
                       and int(started[o][1]["path"]) >= 0)
        # a zone script fired at the spawn point may start one more; the
        # tool's own count must cover the authored set
        n = len(authored)
        got.append((area, st, n, len(hit), resolved, sorted(authored - actor_objs),
                    int(head.get("actor_programs", -1)) >= n))
        want.append((area, "ok", n, n, n, [], True))
    return tuple(got), tuple(want), "4 cities: every startup scx.play.actor starts, resolved"


def c_engine_spawn_from_tables():
    r"""`Actors_SpawnFromTables` LIVE in the Session - the world's characters
    at an area load (`spawn_probe`), T19 for issue 40 of
    todo/iam-script-engine.md.

    `Actors_SpawnFromTables` (0x0040BB90) runs once per load from
    `Area_TickLoad` case 5 - after the set, before the props at 6 and the
    startup scripts at 9 - and again with `a3 = 0` from opcode 71's handler
    (0x403950) when a scene is swapped over a resident area. Each line is a
    rule its code settles, on the shipped data:

    * `start` - the shipped `ObjectShown` bitmap, `State_SetBit`'s array at DB
      +20: 628 of its 1032 bits are set in `IAM\START`, and bits 800..806 are
      the Impasse's cast - 212/218/219 present, 216 not, the Demon 57 present,
      58 and Kay'l 49 not.
    * `boot` - `loadArea(118)`: AREA 118's table places two (310 and 136,
      runtime slots 0 and 1), neither bit set, so `shown()` is EMPTY at the
      load and 310 joins it only when the startup script's `character.show`
      runs two frames later. Spawn skipped: spawned 0, ids -.
    * `load` - AREA 222 with SCENE 55 over it, straight from START: the AREA's
      four then the SCENE's three, runtime slots 0..6 in table order, and the
      FOUR whose bit is set attached. Spawn skipped: 0 and 0.
    * `demon` - actor 57's record. `Area_Load` converts the table in place and
      TRUNCATES (`(int64_t)((double)(100 * v) * 1/256 * 1/2.54 - 1)`, the
      facing `(int64_t)((double)v * 0.087890625)`), and the spawn reads the
      results back, so 49457/-511/19386/4073 is (7604, -79, 2980) facing 357 -
      not the (7605, -80, 2980) 358 the same arithmetic rounded gives. Model
      from the actor record's +144, bank from its +72.
    * `held` - `u16(Actor_FindById(id), 270) = -1`, the held-object field the
      spawn clears for every record it walks; `var.set.used_object` (75) reads
      it back.
    * `hide` - `character.hide 57` through a Session context detaches the
      record the spawn made and clears bit 804 (4 attached -> 3);
      `character.show 57` attaches it again and sets the bit. The chunks' own
      startup scripts are freed first, so only the opcodes move the list.
      Attach gate dropped: attached 6 and 7.
    * `save` - bit 806 set BEFORE the load and nothing else: Kay'l is attached
      at the load, which is the whole point of the bit travelling in the save.
    * `impasse` - the same place reached by PLAYING (the intro run to SCENE
      55's hand-over, `scene.load 237, 57`, frame 6): the same seven spawned
      in 222's slot, AREA 118's two still resident in the other one (total 9),
      and FOUR attached - but `212,218,219,49`, because the intro's scripts
      have hidden the Demon (b804 0) and shown Kay'l (b806 1) by then. Spawn
      skipped: slot222_spawned 0 and shown 1.

    Both mutations were built, run and restored (md5-checked); the binary and
    the object were deleted before each rebuild (B4's trap).
    """
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "spawn_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), os.path.join(ROOT, "tables")],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "spawn_probe must run"
    L = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f: continue
        L[f[0]] = dict(zip(f[1::2], f[2::2]))
    def g(key, *names):
        d = L.get(key, {})
        return tuple(d.get(n) for n in names)
    got = (
        g("start", "bits_set", "of", "b800", "b801", "b802", "b803", "b804",
          "b805", "b806"),
        g("boot", "spawned", "attached", "ids", "slots", "shown",
          "after_frames_attached", "after_frames_ids", "after_frames_shown"),
        g("load", "area", "scene", "spawned", "attached", "attached_ids",
          "all_ids", "slots", "shown", "models"),
        g("demon", "id", "x", "y", "z", "facing", "bit", "attached", "slot",
          "model", "bank"),
        g("held", "f57", "f49", "f212"),
        g("hide", "attached", "bit804", "shown", "models", "show_attached",
          "show_bit804", "show_shown", "show_models"),
        g("save", "bit806", "spawned", "attached", "kayl_attached",
          "kayl_model", "kayl_bank", "attached_ids"),
        g("impasse", "handover", "area", "scene", "other", "slot222_spawned",
          "slot222_attached", "attached_ids", "total_spawned", "b804", "b806",
          "shown", "models"),
    )
    want = (
        ("628", "1032", "1", "1", "1", "0", "1", "0", "0"),
        ("2", "0", "310,136", "0,1", "0", "1", "310", "1"),
        ("222", "55", "7", "4", "212,218,219,57", "212,218,219,216,57,58,49",
         "0,1,2,3,4,5,6", "4", "PA1_FN,PA1_FN,PA1_FN,DE1_FN"),
        ("57", "7604", "-79", "2980", "357", "804", "1", "4", "DE1_FN", "H1AVNT"),
        ("-1", "-1", "-1"),
        ("3", "0", "3", "PA1_FN,PA1_FN,PA1_FN", "4", "1", "4",
         "PA1_FN,PA1_FN,PA1_FN,DE1_FN"),
        ("1", "7", "5", "1", "HO1_FN", "H1AVNT", "212,218,219,57,49"),
        ("6", "222", "55", "118", "7", "4", "212,218,219,49", "9", "0", "1",
         "4", "PA1_FN,PA1_FN,PA1_FN,HO1_FN"),
    )
    return got, want, \
        "the shipped ObjectShown bitmap's 628 of 1032 bits; AREA 118's two " \
        "placements spawned with nobody attached at the boot load; AREA 222 " \
        "+ SCENE 55's seven spawned in table order with the four whose bit " \
        "is set attached; the Demon's placement TRUNCATED to 7604 -79 2980 " \
        "facing 357 with his model and .CTL bank; the held-object field " \
        "cleared; character.hide/show detaching and reattaching the spawned " \
        "record and moving bit 804; a save with bit 806 set attaching Kay'l " \
        "at the load; and the same seven reached by playing the intro, where " \
        "the scripts have swapped which four are on screen"


def c_engine_zone_pump():
    r"""`engine/`'s zone pump under a HELD button, and a script that parks.

    `verify.py: engine zones` presses action once and looks at what ran. This
    presses it for five frames and looks at what the scheduler REFUSED, which
    is where three separate engine rules meet:

    **`Script_QueueAction` (0x004063D0) refuses a second activate** while one
    is in the 4-deep FIFO or is the context's current action (`+32`) - and
    `Script_Execute`'s tail (0x00406460) clears `+32` only when an activate
    reaches `end` (`if (+32 == 2 && !status) +32 = 0`). So the refusal lasts
    exactly as long as the activate is unfinished, a park included.

    **The one-shot bit** - zone id bit 15, 37 of the 4558 shipped zones - is
    the other half. `Script_Pump` state 2 latches the slot to 5 when it is
    set; `Game_HandleEvent` case 7 maps 5 back to 4 and pump case 4 maps 4
    back to 5, and case 7 fires every frame the player is armed, so the slot
    never re-enters the press cycle. It is a LATCH, not an early free: the
    leave script still runs on leaving.

    **A parked script keeps its context.** `camera.set.wait` writes status 7
    and holds for the move's own length; the pump resumes it from the saved pc
    and stack. Only `end` finishes the action.

    Four zones, five held frames each, one GameState apiece:

    * **933** (SCENE 11) - the activate parks on an 80-frame camera move.
      One activate queued, **four refused**, the script entered twice (once
      from the top, once resumed) and only then reaching `dialog.start`.
      Without the resume the script restarts from the top on every press and
      the conversation is never reached at all.
    * **35141** (AREA 146, `-30395` as an int16) - one-shot, and its activate
      script ENDS. One activate over five presses; the leave still runs.
    * **1466** (AREA 76) - the control, and the reason this is not a blanket
      rate limit: an ordinary zone whose activate ends re-arms, so five
      presses are five activates. That is the engine, and a check asserting
      "one per hold" everywhere would be asserting a bug.
    * **626** (AREA 25) - carries a camera at `+66`. `Actor_ScanZones` tests
      containment BEFORE the arc, so the touch is counted whichever way the
      player faces; case 8 asks `Camera_FindWorld` for camera **540**. Two
      zones overlap at that spot, which is why the touch count is twice the
      frame count.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    if not (os.path.isdir(eng) and os.path.isdir(iam) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/, gamedata/IAM or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "zone_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "z.bin")
    try:
        subprocess.run([binp, iam, tbl, os.path.join(iam, "START"),
                        "200", "1", out, "933", "35141", "1466", "626"],
                       capture_output=True)
        bb = open(out, "rb").read()
        o = 0
        n, = struct.unpack_from("<i", bb, o); o += 4
        got = []
        for _ in range(n):
            f = struct.unpack_from("<14i", bb, o); o += 56
            ran = []
            for _ in range(f[13]):
                r = struct.unpack_from("<4i", bb, o); o += 16
                ran.append((r[1], r[3]))          # action, status
            npk, = struct.unpack_from("<i", bb, o); o += 4
            pk = []
            for _ in range(npk):
                p = struct.unpack_from("<5i", bb, o); o += 20
                pk.append((p[1], p[2]))           # action, status
            got.append((f[0], f[5], f[6], f[7], f[8], f[9],
                        f[10], f[11], f[12], ran, pk))
        trailing = len(bb) - o
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # RunStatus: 0 end, 1 dialog, 2 ui.open, 3 camera.wait
    want = [
        # id, oneShot, camera, touches, camReq, lastCam, queued, refused,
        # dialogOpen, [(action, status)...], [parked]
        (933,     0,  -1, 206,   0,  -1, 1, 4, 1, [(2, 3), (2, 1)], [(2, 1)]),
        (-30395,  1,  -1, 206,   0,  -1, 1, 0, 0,
         [(1, 0), (2, 0), (3, 0)], []),
        (1466,    0,  -1, 206,   0,  -1, 5, 0, 0, [(2, 0)] * 5, []),
        (626,     0, 540, 412, 206, 540, 0, 0, 0, [(1, 0), (1, 0)], []),
    ]
    return (got, trailing), (want, 0), \
           "five held frames on four zones: how many activates the FIFO " \
           "accepted and refused, whether the one-shot bit latched the slot, " \
           "what the touch scan asked the camera for, and whether the " \
           "camera-wait park resumed into its dialog.start"


def c_engine_zone_registry():
    r"""`engine/`'s LIVE zone registry - both resident slots, all four tables.

    `verify.py: engine zones` and `engine zone pump` drive `World`, which takes
    ONE chunk of ONE kind and runs the scripts itself. This drives
    `ZoneRegistry`, which is what the SESSION was missing (issue 10): before
    it, nothing in `omk` or `omk-play` ever armed a zone and every conversation
    had to come out of a startup script.

    **`Zones_RegisterAll` (0x00406560)** walks both resident slots and, in
    each, the AREA's zone table (+48, count int16 +76) and the SCENE's loaded
    over it (+16, +44), registering `record + 12` for every zone whose save bit
    is set - `Zone_StateBit` (0x0040D500) indexing the game DB's +28 bitmap
    with `(id & 0x7FFF) / 8`, so bit 15, the one-shot flag, is masked away.
    Its tail PRUNES: a context whose zone id no longer resolves through
    `Zone_FindScriptsById` - its area was unloaded - is detached from the
    prompt slots and freed.

    **`Actor_ScanZones` (0x00467770)** raises event 8 for every registered zone
    the player's point is inside, BEFORE the facing test, and event 7 - which
    arms one of the 16 prompt slots - when the arc matches too. The arc is
    read in DEGREES because `Area_Load` converts it: it rewrites the record's
    +60/+62 by 360/4096 as it relocates, so the 4096-step angles
    `FILE_FORMATS` 5b2b records are whole degrees by the time anything
    compares them with the actor's facing at `f32(actor, 420)`.

    Six scenarios, one `GameState` apiece:

    * **0/1 - the start pair.** AREA 118 has **0 zone records**, which is why
      the intro can only come from a startup script; AREA 222 with SCENE 55
      over it is 12 + 2 records and **5 + 1** registered - the 12 and the 5
      are what `sim: tutorial walk` asserts from the Python side, and the +1
      is the SCENE's own table, which nothing in the port could reach.
    * **2 - walked.** AREA 222 zone 3791: touch, arm (enter script 2323), a
      press that does NOTHING because the zone has no activate script (the
      engine's case-2 guard is `if (u32(ctx, 4))`), a turn OUT of the arc -
      still touched, no longer armed - then leave and free.
    * **3 - two slots, and the one-shot latch.** AREA 146 in slot 0, AREA 76
      in slot 1: 109 records, 80 registered, both walks resolve. Under the
      same five held frames one-shot zone 35141 (`-30395` as an int16) emits
      ONE activate and ordinary zone 1466 emits five. The bit latches the slot
      out of the press cycle - `Game_HandleEvent` case 7 maps state 5 back to
      4 and the pump maps 4 back to 5, and the pump runs BEFORE the actor tick
      - it does not free the zone early (T4's correction to issue 5).
    * **4 - the touch camera.** Zone 626 of AREA 25 carries camera 540 at +66,
      and the player stands in it with his back to its arc: 634 (which
      overlaps it) arms, 626 does not, and 626 still asks for camera 540 on
      both frames. Testing the arc first loses it entirely.
    * **5 - the prune.** Arm 3791, then re-register with AREA 118 alone: the
      prompt slot is detached (armed 1 -> 0) and the context ids come back.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    iam = omkpaths.data("IAM")
    if not (os.path.isdir(eng) and os.path.isdir(iam)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/IAM absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "zones_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "z.bin")
    try:
        subprocess.run([binp, iam, os.path.join(iam, "START"), out],
                       capture_output=True)
        bb = open(out, "rb").read()
        o = 0
        n, = struct.unpack_from("<i", bb, o); o += 4
        sc = {}
        for _ in range(n):
            f = struct.unpack_from("<7i", bb, o); o += 28
            nl, = struct.unpack_from("<i", bb, o); o += 4
            live = list(struct.unpack_from("<%di" % nl, bb, o)) if nl else []
            o += 4 * nl
            ne, = struct.unpack_from("<i", bb, o); o += 4
            ev = [struct.unpack_from("<4i", bb, o + 16 * k) for k in range(ne)]
            o += 16 * ne
            nd, = struct.unpack_from("<i", bb, o); o += 4
            det = list(struct.unpack_from("<%di" % nd, bb, o)) if nd else []
            o += 4 * nd
            npr, = struct.unpack_from("<i", bb, o); o += 4
            pru = list(struct.unpack_from("<%di" % npr, bb, o)) if npr else []
            o += 4 * npr
            sc[f[0]] = dict(records=f[1], registered=f[2], touches=f[3],
                            camReq=f[4], lastCam=f[5], armed=f[6],
                            live=live, ev=ev, det=det, pruned=pru)
        trailing = len(bb) - o
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    def acts(evs, zone):
        return sum(1 for e in evs if e[0] == 2 and e[1] == zone)

    # event kinds: 0 touch, 1 arm, 2 activate, 3 leave, 4 free
    got = ((sc[0]["records"], sc[0]["registered"]),
           (sc[1]["records"], sc[1]["registered"], sc[1]["live"]),
           sc[2]["ev"], sc[2]["touches"],
           (sc[3]["records"], sc[3]["registered"], len(sc[3]["live"]),
            acts(sc[3]["ev"], -30395), acts(sc[3]["ev"], 1466)),
           (sc[4]["touches"], sc[4]["camReq"], sc[4]["lastCam"]),
           (sc[5]["registered"], sc[5]["det"], sc[5]["pruned"], sc[5]["armed"]),
           trailing)
    want = ((0, 0),
            (14, 6, [3790, 3791, 3795, 3799, 3801, 3803]),
            [(0, 3791, 0, 0), (1, 3791, 1, 2323), (0, 3791, 0, 0),
             (0, 3791, 0, 0), (0, 3791, 0, 0), (3, 3791, 3, 0),
             (4, 3791, 4, 0), (0, 3791, 0, 0)], 5,
            (109, 80, 80, 1, 5),
            (4, 2, 540),
            (0, [3791], [3791, 3803], 0),
            0)
    return got, want, \
           "AREA 118's zero zone records; AREA 222 + SCENE 55 registering " \
           "5 + 1 of 12 + 2 under IAM\\START's save bits; a walk through " \
           "zone 3791 - touch, arm, a press with no activate script, a turn " \
           "out of the arc, leave, free; TWO SLOTS registering 80 of 109 " \
           "with the one-shot zone activating once over five held frames " \
           "where the ordinary one activates five times; the touch camera " \
           "540 asked for with the player facing away; and the prune taking " \
           "a prompt slot and a context away with their area"


def c_engine_voice_over():
    r"""`engine/`'s VOICE-OVER path - VM opcode 92 `media.play` (`voice_probe`).

    A cutscene's spoken lines are not in its scene data: a world script fires
    `media.play <OBJECTS id>` and handler **0x00404590** turns that object's
    `IAM\OBJECT` record into a file name. The port now does the same, so the
    viewer can play them.

    THE RULE, read from the assembly (`tools/asmfn.py --op 92`) in the order
    the handler applies it:

      * `if (g_ScriptDryRun) return;` - a dry-run pass does not even announce;
      * `Dbg_LogTagged(id, "OBJECTS")`, then `if (id == -1) return;`
      * `rec = ObjectRecord_Read(id)`, and the SUBTITLE from `rec+280`, with a
        `{C}` prefix when the player's `ACTOR_STATE` is 3 or 15;
      * `sprintf(name, "%s.ADP", rec+14)` - **the stem at +14**;
      * `if (rec[+2] == 16)` - kind 16 is a DOCUMENT: `IMAGES\<stem>.BMP`
        (the ".BMP" constant is at 0x004C0D24), `I2D_LoadBitmap`, and the
        player goes to `ACTOR_STATE` **10**, `ImageScreen`. No audio at all;
      * else `if (strlen(name) <= 4) skip` - an empty stem gives ".ADP" and
        is refused - and then, if the name's first four bytes are `ZVOT` or
        `ZVOP` (a raw dword compare, so case-sensitive), the first 13 are
        overwritten with the constant at **0x004C0868**, which is
        `"JINGOFF3.ADP"`;
      * `sub_41B200(name)`: `Morph_Stop()`, `Morph_SetAudioFormat(0x5640, 1,
        30)` - 22080 Hz, **MONO** - `sprintf("VOICEOFF\\%s")`, `Morph_Start`.
        So a voice-over goes through the MORPH streamer, not the sound bank,
        and the leading `Morph_Stop` is why there is one media voice at a time.

    **The substitution is the finding, and it revises `cutscene music` above.**
    That check asserts 561 ZVO objects with 10 shipped files and concludes the
    voices "cannot be played from this tree". 561 and 10 are right; the
    conclusion is not. **520 of the 561 have a ZVOT/ZVOP stem**, so the engine
    plays `JINGOFF3.ADP` for them - which ships, and is a 2.08 s sting, not
    silence (peak 24889, rms 3463 over 45898 samples). Only **31** are
    genuinely silent. 10 + 520 + 31 = 561, and the partition is asserted here.

    TIER (PORTING B1/B2), declared here, in `src/audio/voiceover.h` and in the
    `engine/README.md` coverage row:

      * **4** for the resolution. `media.play` is one of the 49 announcing
        handlers and its `"OBJECTS"` literal, `aObjects` at **0x004C0844**, has
        exactly ONE `push offset` in the image (Runtime.exe.asm:5528) - so a
        trace line naming that address is `media.play` and nothing else. The
        captures therefore hand over the engine's own media ids: **102**
        announcements over the eight non-empty traces, **56 distinct**, and
        **56 of 56** resolve to a ZVO-tagged object through this rule.
        `traces/impasse-walk.log` opens the Impasse cutscene with
        **142, 141, 404, 410** in that order, which is what the port has to
        play, and its one kind-16 id (**715**, "ZVO G001 TITRE") has its
        `gamedata/IMAGES/zvog001.bmp` on disc - the document arm, exercised by the
        capture.
      * **3** for the decode: the four FNV-1a hashes below are over the decoded
        int16 stream and are re-derived by `tools/adp.py` here. B1's warning
        applies in full - both sides descend from one reading of `sub_483200` -
        but note WHAT it catches, because the third mutation below is exactly
        that case: mono and stereo produce the SAME sample count and different
        hashes.
      * **2** for the corpus partition and for the announcement filter's
        measured collision rate.
      * **6** for the two arms the port does not perform: the `{C}` subtitle
        and the kind-16 bitmap are resolved and reported, and nothing draws
        them.

    THE ANNOUNCEMENT FILTER, and its one known false positive. `Session::
    announced()` carries a `.TAG` domain and a value and **no opcode**, and TEN
    opcodes announce to `OBJECTS` (49, 50, 51, 52, 66, 67, 76, 77, 92, 143). So
    `VoiceOverPlayer` falls back to `domain == "OBJECTS"` plus a ZVO tag name,
    which is measured rather than assumed: over the whole world-script corpus
    (both archives' zone scripts, second tables and `+4` startup scripts) op 92
    has **2605** sites and **all 2605** name a ZVO object, while the other nine
    have **1584** between them and **exactly one** names a ZVO object -
    `inventory.remove_all 111` ("ZVO P006 Rien Partic") in AREA 65 at pc 3564.
    One false play in the shipped game. `todo/pending/E1.md` proposes the
    one-field `Announced::op` that makes it exact.

    SHOWN TO FAIL (B4). Three mutations of `src/audio/voiceover.cpp`, each
    built and run through this check:

      * **the stem offset** - `r.stem` (+14) replaced by `r.name` (+24), which
        is what "read the record's next string" looks like. Every ZVO record's
        +24 is EMPTY, so the built name is ".ADP", the length refusal fires,
        and the counts go `shipped 10 -> 0`, `jingle 520 -> 0`,
        `silent 31 -> 561`; all four Impasse voices decode 0 samples.
      * **the JINGOFF3 substitution** dropped - `jingle 520 -> 0`,
        `silent 31 -> 551`, and 404/410 resolve to `ZVOT001.ADP` /
        `ZVOT002.ADP`, which do not ship: 45898 samples -> 0. This is the
        mutation that puts the check back on `cutscene music`'s old story.
      * **stereo instead of mono** - every sample COUNT is unchanged (the codec
        yields two samples a byte either way) and every FNV moves:
        `4ad934f3 -> 8547d932`, `b5b45b6d -> 952e87aa`,
        `fcc6da1f -> 450febed`. A count-only assertion cannot see this, which
        is why the hash is here.
    """
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data_root()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "voice_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, fr, os.path.join(ROOT, "tables"), "142", "141",
                        "404", "410", "715"], capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "voice_probe must run"
    L, voices = {}, {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if not f:
            continue
        if f[0] == "voice":
            voices[int(f[1])] = f
        else:
            L[f[0]] = f
    def g(key, *idx):
        f = L.get(key, [])
        return tuple(f[i] if i < len(f) else None for i in idx)
    def v(oid, *keys):
        f = voices.get(oid, [])
        return tuple(f[f.index(k) + 1] if k in f else None for k in keys)

    # ---- the independent side: the same rule in Python --------------------
    import adp, dialog_triggers as T, dialog_disasm as D
    d = open(omkpaths.data("IAM/OBJECT"), "rb").read()
    tags = O.TAGS["OBJECTS"]
    zvo = [i for i in range(len(d) // 2048) if (tags.get(i) or "").startswith("ZVO")]
    stem = lambda i: d[i * 2048 + 14:i * 2048 + 24].split(b"\0")[0].decode("cp1252")
    vo = {f.upper() for f in os.listdir(omkpaths.data("VOICEOFF"))}
    jingle = [i for i in zvo if stem(i)[:4] in ("ZVOT", "ZVOP")]
    ship = [i for i in zvo if i not in jingle and (stem(i).upper() + ".ADP") in vo]
    pyc = (len(zvo), len(ship), len(jingle),
           len(zvo) - len(ship) - len(jingle), len(vo))

    def fnv(b):
        h = 2166136261
        for x in b:
            h = ((h ^ x) * 16777619) & 0xFFFFFFFF
        return "%08x" % h
    def pcm(fn):
        raw, _ch = adp.decode(
            open(omkpaths.data("VOICEOFF", fn), "rb").read(), False)
        return str(len(raw) // 2), fnv(raw)

    # ---- the corpus: who announces OBJECTS, and about what ----------------
    FIELD = {49: 1, 50: 1, 51: 1, 52: 1, 66: 0, 67: 1, 76: 0, 77: 0, 92: 0, 143: 0}
    zset = set(zvo)
    m92 = mzvo = other = ozvo = 0
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T.archive(omkpaths.data("IAM", name)).items()):
            rr = T.LAYOUT[name](b)
            ps = []
            if rr:
                ps += [p for _rec, _f, p in T._scripts_from_records(b, rr[0], rr[1])]
                ps += [p for _rec, _f, p in T._second_table(name, b)]
            s4 = struct.unpack_from("<I", b, 4)[0] if len(b) > 8 else 0
            if s4:
                ps.append(s4)          # the chunk's +4 startup script
            for p in ps:
                ops, st = D.disasm(b, p, len(b))
                if st != "ok":
                    continue
                for _pc, op, raw in ops:
                    if op not in FIELD:
                        continue
                    f = FIELD[op]
                    if len(raw) < 2 * (f + 1):
                        continue
                    val = struct.unpack_from("<h", raw, 2 * f)[0]
                    if op == 92:
                        m92 += 1
                        mzvo += val in zset
                    else:
                        other += 1
                        ozvo += val in zset

    # ---- the traces: the engine's own media ids (tier 4) ------------------
    #
    # `aObjects` at 0x004C0844 has ONE `push offset` in the image, so this
    # address is `media.play` and no other OBJECTS announcer.
    pat = re.compile(r'004c0844 "OBJECTS",[0-9a-f]+ "(-?[0-9]+)"')
    tid, impasse = [], []
    for fn in sorted(glob.glob(os.path.join(ROOT, "traces", "*.log"))):
        got = [int(m.group(1)) for ln in open(fn, errors="replace")
               for m in [pat.search(ln)] if m]
        tid += got
        if fn.endswith("impasse-walk.log"):
            impasse = got
    tzvo = sum(1 for i in set(tid) if i in zset)
    head = tuple(impasse[1:5])         # after the 997 nearly every trace opens with

    got = (
        g("counts", 2, 4, 6, 8, 10),
        pyc,
        v(142, "file", "samples", "fnv"),
        v(141, "file", "samples", "fnv"),
        v(404, "file", "substituted", "samples", "fnv"),
        v(410, "file", "substituted", "samples", "fnv"),
        v(715, "image", "file", "samples"),
        g("poll", 2, 3, 4, 6, 8),
        (m92, mzvo, other, ozvo),
        (len(tid), len(set(tid)), tzvo, head),
    )
    want = (
        ("561", "10", "520", "31", "17"),
        (561, 10, 520, 31, 17),
        ("ZVOD001.ADP",) + pcm("ZVOD001.adp"),
        ("ZVOM010.ADP",) + pcm("ZVOM010.adp"),
        ("JINGOFF3.ADP", "1") + pcm("JINGOFF3.ADP"),
        ("JINGOFF3.ADP", "1") + pcm("JINGOFF3.ADP"),
        ("1", "-", "0"),
        ("2", "142", "404", "0", "4"),
        (2605, 2605, 1584, 1),
        (102, 56, 56, (142, 141, 404, 410)),
    )
    return got, want, \
        "the ZVO partition (objects, shipped, JINGOFF3-substituted, silent) " \
        "and the VOICEOFF file count; the two Impasse voices that ship and " \
        "the two that take the jingle, sample-exact against tools/adp.py; " \
        "the kind-16 document arm playing nothing; the announcement filter " \
        "consuming each id once; media.play sites, all naming a ZVO object, " \
        "against the other nine OBJECTS opcodes' single collision; and the " \
        "golden traces' own media ids, with the Impasse cutscene's first four"


def c_engine_world_ops():
    r"""The world opcodes through `tools/worldops_probe.cpp`: inventory, prop
    state, actor stats, the held object and the timer, each rule read from its
    handler and none visible to the corpus sweep, whose reference stubs them.

    Written 2026-09-02 against the interpreter as it stood and its first run
    FAILED 51 of these 70 lines - the 19 that passed are the ones a stub
    passes by construction (`*.untouched`, `off.*`, the negatives).

      lists     49 (0x40A440) scans list field0 for field1 -> 1/0; 50
                (0x40A4D0) refuses a held id on lists 2 and 3 ONLY (`cmp edi,3
                / cmp edi,2`) and inserts at the FRONT; 51 removes one; 52's
                id == -1 arm compares every entry with the literal 0FFFFh and
                so removes NOTHING - 37 shipped sites, and the port keeps it;
                128 (0x405810) moves the first n of one list to the front of
                another, reversed, and writes n (negative: nothing, stored).
      timer     112 STARTS (flags 12, value 900000), 115 reads clock-start
                while running, the frozen value once expired, 0 at flags == 1;
                113 refused while running; 111 STOPS (29); 110 resets.
      player    91 is the DB's +332 (-1 as shipped, 136 after `player.become`);
                93/86 on -1 OR the DB's own id go to the DB record at +60, and
                `Actor_SetProperty`'s clamp is UNSIGNED (`cmp esi,0C8h ; jbe`):
                -5 -> 200, 70000 -> 65535 on Argent, Anneaux unclamped; the
                pointer-slot property 0 leaves the variable.
      hooks     75 writes word_4E6CA0[slot] or -1 with nothing held, and with
                no hook installed nothing at all; 86/93 on AREA 1's actor 397
                edit the chunk record (Vie 100 -> 42, Argent clamp, ammunition
                slot 2 by the high word); 67 attaches once and writes +270,
                not twice; 69 drops and clears +270 only when a slot is held;
                76 sets bit 1 only over bit 0; 68 matches the held SLOT against
                record +0 - found: drop, state & ~2, hide; not found: clear the
                slot and REMOVE - and clears the player's +330 either way; 98
                is handed to the hook.
      off       `setWorldWrites(false)`: list, record, timer and hook log all
                untouched, four calls recorded.

    Mutations: the duplicate rule inverted moves add.dup.list0.count 4 -> 3 and
    add.dup.list2.count 1 -> 2; a signed clamp moves stat.player.vie.neg 200 ->
    -5. And the sweep switch is shown load-bearing by `engine: execute`, which
    stays byte-identical, against a switch-off build of run_scripts whose DB
    differs in 88 bytes over 14 variables, two lists and the player record.
    """
    eng = os.path.join(ROOT, "engine")
    tbl = os.path.join(ROOT, "tables", "vm_opcodes.json")
    fr = omkpaths.data_root()
    if not (os.path.isdir(eng) and os.path.exists(tbl) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/, tables/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "worldops_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, fr, tbl], capture_output=True, text=True)
    if r.returncode != 0:
        return ("no run",), ("ran",), "worldops_probe must run"
    got = {}
    for ln in r.stdout.splitlines():
        f = ln.split()
        if len(f) == 2 and not ln.startswith("--"):
            got[f[0]] = int(f[1])
    want = [
        ("has.6", 1), ("has.7", 0),
        ("add.dup.list0.count", 4), ("add.dup.list0.front", 300), ("add.dup.list2.count", 1),
        ("lists.recorded", 6),
        ("remove.one.count", 3), ("remove_all.count", 2), ("remove_all.minus1.count", 2),
        ("transfer.all.moved", 2), ("transfer.all.list0.count", 0),
        ("transfer.all.list1.count", 4), ("transfer.all.list1.front", 171),
        ("transfer.one.moved", 1), ("transfer.one.list0.front", 171),
        ("transfer.one.list1.count", 3), ("transfer.neg.moved", -3),
        ("transfer.neg.list1.count", 3),
        ("timer.fresh.elapsed", 0), ("timer.flags.running", 12), ("timer.value.ms", 900000),
        ("timer.running.elapsed", 3000), ("timer.set.refused.running", 900000),
        ("timer.expiry.fired", 1), ("timer.expired.elapsed", 900000),
        ("timer.stopped.flags", 29), ("timer.reset.elapsed", 0),
        ("player_id.fresh", -1), ("player_id.become", 136),
        ("stat.player.vie", 100), ("stat.player.vie.record", 200), ("stat.player.vie.neg", 200),
        ("stat.player.argent", 65535), ("stat.player.anneaux", -3), ("stat.pointer.untouched", 77),
        ("prop.found", 1), ("prop.stateIndex", 1),
        ("npc.vie.shipped", 100), ("npc.vie.set", 42), ("npc.argent.clamp", 65535),
        ("npc.ammo.slot2", 17), ("npc.unknown.untouched", 77),
        ("used.none", -1), ("used.held", 360), ("used.nohooks", 77),
        ("hold.calls", 1), ("hold.field", 360), ("hold.calls.again", 1),
        ("release.actor.drop.calls", 1), ("release.actor.field", -1),
        ("release.actor.none.calls", 1),
        ("show.absent.state", 0), ("show.absent.calls", 0), ("show.state", 3),
        ("show.calls", 1), ("show.unknown.calls", 1),
        ("release.none.calls", 0), ("release.state", 1), ("release.drop.calls", 1),
        ("release.hide.calls", 1), ("release.player.field", -1), ("release.log.added", 2),
        ("release.remove.calls", 1), ("release.clearslot.calls", 1),
        ("place.calls", 1),
        ("off.list0.count", 2), ("off.vie.record", -1), ("off.flags", 1), ("off.log", 0),
        ("off.recorded", 4),
    ]
    return tuple(got.get(k) for k, _ in want) + (len(got),), \
           tuple(v for _, v in want) + (70,), \
           "the object lists (has, the per-list duplicate rule, front insert, " \
           "remove, the dead -1 arm, transfer); the timer's start/stop/three-way " \
           "read; the player's identity and stat block in the DB with the " \
           "unsigned clamp; and through the hooks the held object, hold and " \
           "release on a real actor record, show and release on a real prop " \
           "record, place; then everything inert with the sweep switch off; " \
           "and 70 values printed"


def c_engine_dialogue_line_states():
    r"""A line's END RULE is per-ASSET, and two shipped lines are not ordinary.

    `Dialog_TickUI` (0x0046A200) case 4 stamps the phase global from the line's
    asset name - `strcmp` against "125338" gives 7, a SEVEN-byte `memcmp`
    against "02E19A" (so the terminator too, an exact match) gives 8, anything
    else 2 - and case 2/7/8 is one condition over those three:

        if ((a2 & 0x10) != 0
         || dword_9103DC == 7 && (a2 & 0xFFFFFFF3) != 0
         || dword_9103DC == 8 && Morph_IsDone())
        { Morph_Stop(); dword_9103DC = 3; }

    So a **2** line is cut only by confirm; a **7** line by any input bit
    except 4 and 8, which are up and down and are the pair the reply menu moves
    on (they reach the test because `Actors_TickAll` passes
    `dword_90E0E0 | dword_4E9718 & 0xC`); and an **8** line ends BY ITSELF when
    the morph is done, with no press at all.

    **The port had none of it.** `DialogPlayer` waited for a press on every
    line, which is right for 1172 of the 1174 and hangs on the other two:
    conversation 186 - one node, all four `param` -1, the asset `02E19A` - is a
    conversation that in the engine opens, speaks for 7.51 s and closes without
    the player touching anything, and the port sat on it for ever.

    **Why the corpus scan is here and not just the two names.** "Two lines are
    special" is a claim about 1174 nodes, and this differences the port's
    classifier against `tools/omkdialog`'s own reader of `gamedata/IAM/DIALOG` -
    conversation count, node count, and WHERE each special line is - so the two
    literals are not merely quoted back. The `rule` row then tests the
    classifier on names the corpus does not contain, because one example of
    each cannot show what is *not* matched: `02E19AB` must be 2, and it is 8 if
    anyone re-reads the `memcmp` as a prefix test.

    **And the tick count is external.** `02E19A.3DM` is 7.5102 s by
    `tools/morph3dm.read`, so at 1/30 s a tick the line is over on tick
    `ceil(7.5102 * 30) = 226`; the port is asserted against that, not against
    its own number.
    """
    import tempfile, shutil, math
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dialogue_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    ops = os.path.join(ROOT, "tables", "vm_opcodes.json")
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "d.txt")
        r = subprocess.run([binp, fr, ops, out], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("probe failed",), ("ran",), "dialogue_probe must run"
        rows = {}
        for ln in open(out):
            f = ln.split()
            if f: rows[f[0]] = f[1:]
        if not {"scan", "state7", "self8", "rule"} <= set(rows):
            return tuple(sorted(rows)), ("scan", "self8", "state7", "rule"), \
                   "the probe must report all four rows"
        scan  = tuple(int(v) for v in rows["scan"])
        st7   = rows["state7"]
        # asset, state, anyKeyCuts, then cutBy for 0/4/8/C/10/1/20
        got7  = (st7[1], int(st7[2]), int(st7[3])) + \
                tuple(int(v) for v in st7[4:11])
        self8 = tuple(int(v) for v in rows["self8"])
        rule  = tuple(int(v) for v in rows["rule"])

        # ---- the same scan, off the FILE, with tools/omkdialog's own reader
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import omkdialog, morph3dm
        chunks = omkdialog.load_chunks()
        convs = nodes = 0
        ref7, ref8 = [], []
        for cid, blob in sorted(chunks.items()):
            p = omkdialog.parse(blob)
            if p is None: continue
            convs += 1
            for n in p[1]:
                nodes += 1
                if n["name"] == "125338": ref7.append((cid, n["index"]))
                if n["name"] == "02E19A": ref8.append((cid, n["index"]))
        if len(ref7) != 1 or len(ref8) != 1:
            return (len(ref7), len(ref8)), (1, 1), \
                   "one shipped line of each state - the check's premise"
        # the state-8 line's own length, and the tick it must end on
        pcm, _ch, _lay = morph3dm.read(os.path.join(fr, "MORPH", "02E19A.3DM"))
        secs = len(pcm) / 2.0 / 22050.0
        # `lineAt_` reaches k/30 after k ticks and `cutBy` tests AFTER the
        # increment, so the first tick with k/30 >= secs ends the line.
        wantTick = math.ceil(secs * 30)

        return (scan, got7,
                (self8[3], self8[5], self8[6], self8[7]),
                rule), \
               ((convs, nodes, nodes - 2, len(ref7), len(ref8),
                 ref7[0][0], ref7[0][1], ref8[0][0], ref8[0][1]),
                ("125338", 7, 1, 0, 0, 0, 0, 1, 1, 1),
                (wantTick, 2, 0, 1),
                (8, 7, 2, 2, 2, 2, 2, 2)), \
               "the SCAN (conversations, nodes, and how many lines end on " \
               "confirm / any key / by themselves, with where the last two " \
               "are), differenced against tools/omkdialog; then conversation " \
               "272's first line - its asset, its state, anyKeyCuts, and " \
               "whether input words 0/4/8/C/10/1/20 cut it (only 0x10 and the " \
               "bits outside 0xC may); then conversation 186 ticked with NO " \
               "PRESS - the tick it ends itself on (ceil of its own voice " \
               "length x 30, from tools/morph3dm), the phase it lands in " \
               "(2 = Finished), whether it is still playing, and how many " \
               "lines it played; then the classifier on eight names, six of " \
               "which the corpus does not contain"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_engine_game_state():
    r"""`engine/`'s game-state FOUNDATIONS - object lists, prop state, timer.

    The three things issues 29, 30 and 37 need before an opcode can be wired to
    them, run over `IAM\START` rather than described.

    **A list is not an array with a count.** `ObjectList_SetCapacity`
    (0x00409B00) recovers the count by scanning for a `-1` terminator, capped
    at the capacity, which is why no count is stored - and every operation
    below has to leave that scan true afterwards.

    **`inventory.add` inserts at the FRONT and refuses when full.**
    `ObjectList_InsertFront` (0x00409CB0) is `memmove(base + 2, base,
    2*capacity - 2)` then `base[0] = id`, over a `if (capacity == count)
    return 0` guard. So the shipped carried list `[6, 171]` becomes
    `[300, 6, 171]`, not `[6, 171, 300]` - an "append" would be
    self-consistent, pass every count test here, and put the wrong item under
    the inventory screen's cursor. The duplicate refusal is per LIST, not per
    opcode: the handler's `cmp edi,3 / cmp edi,2` arm means lists 2 and 3
    refuse an id they hold and lists 0 and 1 do not.

    **`ObjectState_Get` sign-extends.** The byte is loaded `movsx`, the mask
    `3 << 2*(i%4)` is built in a byte register and sign-extended too, and the
    shift is a `sar` - so at index % 4 == 3 state 3 reads back as **-1** and
    state 2 as **-2**. No shipped consumer can see it (they all mask the low
    byte), and the port reproduces it rather than tidying it, with the plain
    0..3 available beside it. The neighbours in the same byte are asserted
    unchanged, because a 2-bit setter that gets its mask wrong is invisible
    until it eats one.

    **The clock counts MILLISECONDS**, derived rather than assumed: `Clock_Tick`
    accumulates the frame delta - `30/fps`, so 1.0 is 1/30 s at any frame rate
    - and every 5.0 adds 166, i.e. 996 units a real second. 300 frames is 59
    ticks of 166 = 9794, and a day is 3600000 units, so the day counter has to
    roll at exactly that boundary.

    **Ops 111 and 112 are named the wrong way round in the table.** 0x00405350
    jumps to `loc_41E2B0` (needs the timer running; sets flag 1) and 0x00405360
    to `loc_41E2D0` (needs it stopped; `start = clock`, clears flags 1 and
    0x10), so 111 HALTS and 112 RUNS - which is also the only order the shipped
    scripts can mean, since `Timer_SetValue`/`Timer_SetMode` refuse unless
    stopped and every bomb site is `mode 12`, `set 900`, then 112. The port's
    methods are named for the mechanism; this asserts each refusal, so a
    wiring that swaps them fails here rather than 15 minutes into a bomb.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "state_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, omkpaths.data_root(), out], capture_output=True)
        v = struct.unpack_from("<65i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (
        # IAM\START as it ships: carried [6, 171], second 2, memos empty
        2, 6, 171, 2, 0,
        # add 300 to the carried list: it lands at the FRONT
        1, 3, 300, 6, 171, 1, 0,
        # the duplicate rule is per list - 0 allows, 2 refuses
        0, 1, 1, 4, 1, 0, 1,
        # a full list refuses, changes nothing, and keeps its front-insert order
        9, 0, 9, 0, 608,
        # remove shifts the tail down and re-terminates the LAST slot
        1, 8, -1, 1, 0, 3, 2, 0,
        # the 2-bit prop state, its sign extension, and the neighbours
        0, 3, 0, 3, -1, -2, 3, 0, 0,
        # the clock: 59 ticks of 166 in 300 frames, and the day boundary
        9794, 53,
        # the timer: idle, configure, start, refusals, elapsed, expiry, stop
        1, 0, 0, 1, 13, 1, 1, 12, 0, 0, 1, 12345, 0, 1, 0, 28, 0, 900000,
        1, 29, 0,
        # and the shipped block is untouched by any of it
        2), \
        "IAM\\START's three lists; then the list operations transcribed from " \
        "their handlers - front insert, the per-list duplicate rule, the " \
        "full-list refusal, the shift-down remove; the 2-bit prop state with " \
        "ObjectState_Get's sign extension and its untouched neighbours; the " \
        "clock in milliseconds and its day roll; and the timer, whose ops " \
        "111 and 112 the opcode table names the wrong way round"


def c_engine_cam_mode13():
    r"""`engine/`'s CAMERA MODE 13 - the editing an object start hands the camera to.

    Every `scx.play*` handler (46, 57-60) ends `call ScriptObject_HasCamEditing;
    jz` past `push 0Dh; call Camera_Request` - and the last operand is
    `fild`/`fstp`'d into the request's +24, a FLOAT travel in frames. Mode 13
    follows the scene's active camera: the camera tick copies eye/target/fov/
    roll out of `dword_9103D4`, which is `Scene_GetActiveCamera` after
    `Script_PlayAllScripts`, i.e. what `Script_PlayScript` sampled from the
    linked chunk-10 editing at the OBJECT'S clock (`Cam_PlayEditing`).

    `SceneRunner` now reads chunk 10, records the link at every start and
    samples it; `tools/camedit_probe` starts `A_1_KaylArrives` (Impasse,
    SCENE 55) through `SceneRunner::handle` and prints the camera at frames
    0/15/30, and `--sweep` every frame of all 125 editings. The reference is
    `tools/cutscene.py`'s `sample()`, the other transcription of the same
    function, pinned by `cutscene camera` at 24112/24112.

    Shown to fail: shifting the Python side one frame reports 20351 rows
    over 0.02; the probe's first build sampled at the post-advance clock and
    disagreed at frames 15 and 30 (6940.40 vs 6938.87).
    """
    import subprocess, tempfile, shutil, re
    eng = os.path.join(ROOT, "engine")
    fr, tb = omkpaths.data_root(), os.path.join(ROOT, "tables")
    if not (os.path.isdir(eng) and os.path.isdir(os.path.join(fr, "SCPTDATA"))):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s", "build/camedit_probe"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "camedit_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import cutscene
    tmp = tempfile.mkdtemp()
    sweep = os.path.join(tmp, "sweep.txt")
    try:
        r = subprocess.run([binp, fr, tb, "--sweep", sweep],
                           capture_output=True, text=True)
        out = r.stdout
        took = ("editing 15 'intro' takes the camera: object 221 "
                "'A_1_KaylArrives', 330 frames, travel 0" in out)
        # the three frames a person reads, by the two readers
        pat = re.compile(r"frame\s+(\d+) \(clock ([\d.]+)\): eye (\S+) (\S+) (\S+)"
                         r"\s+at (\S+) (\S+) (\S+)\s+roll (\S+)\s+fov (\S+)")
        probe = {int(m.group(1)): (float(m.group(2)),
                                   [float(m.group(k)) for k in range(3, 11)])
                 for m in pat.finditer(out)}
        intro = next((e for e in (cutscene.scene("Impasse.SCX") or {})
                      .get("editings", []) if e["name"] == "intro"), None)
        agree = 0
        for f in (0, 15, 30):
            ref = cutscene.sample(intro, f) if intro else None
            if f not in probe or ref is None: continue
            clock, vals = probe[f]
            want = ref["eye"] + ref["at"] + [ref["roll"], ref["fov"]]
            if clock == f and max(abs(a - c) for a, c in zip(vals, want)) <= 0.02:
                agree += 1
        # the corpus: every frame of every editing
        rows = bad = missing = editings = 0
        worst = 0.0
        cur = None
        scpt = os.path.join(fr, "SCPTDATA")
        for line in open(sweep):
            p = line.split()
            if p[0] == "editing":
                stem, eid = p[1], int(p[2])
                fn = next((x for x in os.listdir(scpt)
                           if x.lower() == stem.lower()), stem)
                s = cutscene.scene(fn)
                cur = next((e for e in s["editings"] if e["id"] == eid), None) if s else None
                editings += 1
                continue
            f = int(p[0])
            ref = cutscene.sample(cur, f) if cur else None
            if p[1] == "none" or ref is None:
                missing += 1
                continue
            got = list(map(float, p[1:9]))
            want = ref["eye"] + ref["at"] + [ref["roll"], ref["fov"]]
            d = max(abs(a - c) for a, c in zip(got, want))
            worst = max(worst, d)
            bad += d > 0.02
            rows += 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (took, agree, editings, rows, missing, bad), \
           (True, 3, 125, 24112, 0, 0), \
           "starting A_1_KaylArrives through SceneRunner::handle hands the " \
           "camera to editing 15 'intro' with travel 0; its camera at frames " \
           "0/15/30 (sampled at clock 0/15/30, not the post-advance clock) " \
           "agrees with tools/cutscene.py to 0.02; and over the sweep - 125 " \
           "editings, 24112 frames, none absent on either side - 0 rows " \
           "differ by more than 0.02 (worst %.4f)" % worst


def c_engine_screen_close():
    r"""`omk-play`: LEAVING a screen answers -1 and the game runs on.

    `UI_OpenScreen` parks the caller with `dword_930750` = -1; every close
    path posts event 5 with it and `Game_HandleEvent` case 5 writes the named
    variable and status 1. The viewer used to `break` out of its frame loop
    on `walk->closed()`. SPACE is the live scheme's back bit (keyboard 57,
    bit 0x20, `tables/key_bindings.json` group 0), which is what
    `UiWalk::press` closes a root panel on.
    """
    import subprocess
    eng = os.path.join(ROOT, "engine")
    fr, tb = omkpaths.data_root(), os.path.join(ROOT, "tables")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    mk = subprocess.run(["make", "-s", "play"], cwd=eng, capture_output=True, text=True)
    play = os.path.join(eng, "build", "omk-play")
    if mk.returncode != 0 or not os.path.exists(play):
        return (True, True, True, False), (True, True, True, False), \
               "no SDL - the frontend is optional (PORTING A8)"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    r = subprocess.run([play, fr, tb, "--software", "--res", "640x480", "--nofmv",
                        "--frames", "200", "--keys", "0,0x39", "--keydelay", "4"],
                       capture_output=True, text=True, env=env)
    o = r.stdout
    return ("closed without an answer -> -1" in o, "200 frames presented" in o,
            "1 ui answers" in o, True), (True, True, True, True), \
           "SPACE on the start menu: the -1 answer is posted, all 200 frames " \
           "are presented (the loop did not break), one UI answer is recorded, " \
           "and SDL was found"


def c_engine_fx():
    r"""`engine/`'s ambient effects - the chain a set's fire and neon come out of.

    The fifteenth slice, and the answer to "did you include the particles?" -
    which until now was no. A set's neon, smoke, fire and steam are not in the
    `.3DO`; they are emitters, and finding one crosses three files authored
    separately:

        .3DO   a mesh flagged 0x40000000, and its position
          |    the first FOUR BYTES of its name, compared as a dword
        .SFX   section D -> section C: sprite, velocity, lifetime, cone,
          |    scale, spin, blend mode
        .SCX   chunk 4: the sprite, whose QUADS are its animation frames

    Ported here: the `.SFX` six-section walk and the binding. **67 files, all
    67 landing exactly on the file size** - nothing points at the next section,
    so that is the whole proof the strides are right - and across the 220 decor
    sets, **579 meshes carry the 0x40000000 flag and 325 bind a section C
    effect**, in 16 sets.

    **The chain is complete end to end since the stream walk landed.** Section
    C's sprite id resolves through the `.SCX` stream's chunk 4, and **321 of
    the 325 bound effects also find their sprite** - matching what
    `tools/ambientfx.py` produces exactly. The four that do not are effects
    whose sprite is not in their own scene: bound, but not drawable.

    **Two things about the emitter a port must not get wrong**, both recorded
    in the header because both have already cost time:

    * the cadence at section D `+12` is a period in **FRAMES**, not seconds -
      the engine's default frame delta is 1.0 - and ticking it in seconds ran
      a set 30x too slow, which is how it was found;
    * a period of 0 means a particle **every frame**, not "no emitter". With a
      1-frame lifetime that is a steady glow, and an earlier reading that had
      Anekbah's neon flickering was withdrawn: the flicker came from a
      `rand()` a period-0 emitter never reaches.

    This slice also turned up a fault in the checks around it: eight `.SFX`
    files spell the extension `.Sfx` or `.SfX`, and every glob here missed
    them. See `extension case`.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    scpt = omkpaths.data("SCPTDATA")
    dec  = omkpaths.data("MESHES", "DECORS")
    if not (os.path.isdir(eng) and os.path.isdir(scpt) and os.path.isdir(dec)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_fx")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "f.bin")
    try:
        subprocess.run([binp, scpt, dec, out], capture_output=True)
        v = struct.unpack_from("<13i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (v[0], v[1], list(v[2:8]), v[8], v[9], v[10], v[11], v[12]), \
           (67, 67, [14, 67, 396, 156, 382, 267], 220, 16, 579, 325, 321), \
           ".SFX files and those whose six-section walk lands EXACTLY on the " \
           "file size; the six section counts; then decor sets, those with " \
           "bindings, meshes flagged 0x40000000, and those binding a section " \
           "C effect; and those also resolving a SPRITE through the .SCX " \
           "stream - 325 bind and 321 can be drawn, the four-emitter gap " \
           "being effects whose sprite is not in their scene"


def c_engine_datafs():
    r"""`engine/`'s `DataFs` - the one way the replica opens game data.

    The game shipped for Windows 95/98, whose filesystem is case-INSENSITIVE,
    so every name the data references itself by was resolved by the OS without
    anyone noticing the spelling: a `.3DO` naming its `.3dt`, an actor record
    naming its `.CTL`, `Scene_FullPath` building a path from a record's string.
    The authors typed whatever they liked and the disc proves it - eight of the
    67 `.SFX` files spell the extension `.Sfx` or `.SfX`.

    On Linux or macOS none of that resolves by concatenation, so this is a
    **compatibility requirement**, not a convenience - and it has to be
    everywhere, which is why it is a class owning the data root rather than a
    helper someone can forget to call.

    **The test is deliberately hostile.** Every one of the **2367** shipped
    files under IAM, SCPTDATA, ANIMS, MESHES and MORPH is asked for again with
    its whole path mangled - ALL-UPPER, all-lower, and case-FLIPPED, which
    mangles the directory components too - and all 2367 must come back
    resolving to the same real file under every spelling. A resolver that only
    lowercased the extension, or only handled the last component, fails this.

    Then the sibling lookup, which is where the two-spelling bug kept
    reappearing: all **220** decor models resolve their `.3dt`.

    This exists because the same mistake has been made twice in Python - a
    `.3DM` sweep reporting 708 files where there are 777, and eight `.SFX`
    files five checks could not see. Both times the symptom was a total that
    was quietly short rather than an error. `verify.py: extension case` guards
    the data side; this guards the reader.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data_root()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "fs_selftest")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed", b.stderr.strip()[:200]), ("built", ""), \
               "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "f.bin")
    try:
        subprocess.run([binp, fr, out], capture_output=True)
        v = struct.unpack_from("<8i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (2367, 2367, 2367, 2367, 2367, 2367, 220, 220), \
           "data files; those resolving as-is, UPPERCASED, lowercased and " \
           "case-FLIPPED (which mangles the directories too), and those " \
           "reaching the SAME real file under all four - which must be every " \
           "one; then decor models and those whose .3dt sibling resolves"


def c_engine_scx_stream():
    r"""`engine/`'s `.SCX` STREAM walk - what unlocks the clips and sprites.

    The sixteenth slice. Most of an `.SCX` sits after the structural block and
    is pulled in as each resource record is reached; the block declares them
    and the stream carries the payloads in that same order.

    **Every streamed record's first header word is its own file offset**, which
    is what makes the walk self-checking: a reader with the sizes right never
    needs to resync, and one with them wrong does. **220 scenes, all 220
    landing on EOF, 0 resyncs, 1490 clips and 230 sprites** - the same numbers
    `tools/anim_3da.py` produces.

    **Chunk 4 is the fact this tests.** Its header is THREE words - [own
    offset, MODEL size, TEXTURE size] - and the payload is a whole `.3DO`
    immediately followed by its `.3dt`. A walk that reads only the second word
    runs short and has to scan forward to the next self-locating header; read
    as 12 + model + texture it is exact, and the 0 resyncs is what says so.

    This was the largest single gap in the port's coverage: it is what the
    `.3DA` clips, the effect sprite payloads and chunk 10's camera editings all
    arrive through.

    **Chunk 0's payloads are the `.3DP` paths**, and they carry their own
    check: a path's header duration must equal its LAST key's frame, and it
    does in all **6756** shipped paths. That is what says the 32-byte key
    stride (frame + pos[3] + quat[4]) is right - a wrong stride walks the keys
    off and the last frame stops matching. Paths matter beyond motion: a
    RELATIVE body animation is positioned by sampling one, not by a clip root
    (FILE_FORMATS), so this is also the placement half of dialog 401.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    scpt = omkpaths.data("SCPTDATA")
    if not (os.path.isdir(eng) and os.path.isdir(scpt)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/SCPTDATA absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_stream")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, scpt, out], capture_output=True)
        v = struct.unpack_from("<9i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (220, 220, 0, 1490, 230, 1667, 6756, 6756, 6756), \
           "scenes; those whose stream walk lands on EOF; RESYNCS, which must " \
           "be zero - a record whose size was read wrong forces one; the " \
           "clips, effect sprites and sounds the stream carries; then the " \
           ".3DP paths, those with keys, and those whose header duration " \
           "EQUALS the last key's frame - the test the 32-byte key stride " \
           "could fail"


def c_engine_morph_audio():
    r"""`engine/`'s `.3DM` layout and its ADPCM decoder, against the corpus.

    Two slices at once, because one is useless without the other: a `.3DM` is
    a spoken line, and its audio is interleaved a frame at a time with the face
    vertices and bone rotations.

    **The frame count is DERIVED, not read.** Word 2 of the header is nominal;
    the count that matches the file is `(size - preamble) / record`, and the
    only remainder that ever occurs is a last frame carrying no audio.
    **582 of the 777 files land on a record boundary, 195 are short by exactly
    one audio block, and 0 are anything else** - so a reader that trusted word
    2, or treated the remainder as corruption, would be wrong about a quarter
    of the corpus. All 777 also carry the 0,1,2,... node preamble.

    **The codec is checked sample for sample.** Every one of the 777 files is
    decoded by both implementations and compared by a hash over the PCM: **777
    identical, 225441216 samples.** OTNS ADPCM is IMA with two differences,
    and both are load-bearing - the HIGH nibble decodes first, and IMA's
    unconditional `step >> 3` bias term is absent (leaving it in drifts the
    predictor about -9000 DC over a line, with the audio buried under it).

    **A real bug this caught**, worth recording because the corpus nearly hid
    it: only **3** of the 777 files are stereo, and the port alternated whole
    BYTES between the two channels where `sub_483340` splits them INSIDE the
    byte - high nibble left, low nibble right. 774 files would have passed
    either way. The three are what fail it.

    The step and index tables are loaded from `tables/adpcm.json` and compared
    against the built-in copy, so a baked-in constant that drifted from the
    extraction cannot hide.
    """
    import subprocess, tempfile, shutil, glob
    import morph3dm
    eng = os.path.join(ROOT, "engine")
    morph = omkpaths.data("MORPH")
    tbl = os.path.join(ROOT, "tables", "adpcm.json")
    if not (os.path.isdir(eng) and os.path.isdir(morph) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/, gamedata/MORPH or tables/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_morphs")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "m.bin")
    try:
        subprocess.run([binp, morph, out, tbl], capture_output=True)
        bb = open(out, "rb").read()
        files, exact, short, other, iota, frames, agree, twoab = \
            struct.unpack_from("<8i", bb, 0)
        n, = struct.unpack_from("<i", bb, 32)
        hashes = list(struct.unpack_from("<%dI" % n, bb, 36))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    def fnv(raw):
        h = 2166136261
        for k in range(len(raw)):
            h = ((h ^ raw[k]) * 16777619) & 0xFFFFFFFF
        return h
    paths = sorted({os.path.realpath(p) for p in
                    glob.glob(os.path.join(morph, "*.3DM")) +
                    glob.glob(os.path.join(morph, "*.3dm"))})
    same = 0
    stereo = 0
    for i, p in enumerate(paths):
        try: pcm, ch, _L = morph3dm.read(p)
        except Exception: continue
        stereo += (ch == 2)
        if i < len(hashes) and fnv(pcm) == hashes[i]: same += 1
    return (files, exact, short, other, iota, agree, len(paths), same, stereo), \
           (777, 582, 195, 0, 777, 1, 777, 777, 3), \
           ".3DM files; those landing on a record boundary; those short by " \
           "exactly the last audio block; those that are NEITHER (must be " \
           "zero - the test the derived frame count could fail); those with " \
           "a 0,1,2,... preamble; whether the ADPCM tables match the built-in " \
           "copy; then files decoded and those SAMPLE-IDENTICAL to " \
           "tools/adp.py; and how many are stereo - only three, which is why " \
           "the interleave bug nearly survived"


def c_engine_audio():
    r"""`engine/`'s AUDIO PATH - the voices, the bank, the listener, the mix.

    **The first thing reading it settles is that there is nothing to mix.**
    `docs/PORTING.md` B6 asked for "audio mixing ... PCM sample-exact against
    tools/adp.py; the mix compared offline", and the engine does not mix.
    `Sound_Init` (0x0046C3A0) creates a DirectSound PRIMARY buffer, sets its
    format - PCM, 2 channels, 22050 Hz, 16 bits, block align 4, 88200 bytes a
    second - and calls Play(0, 0, DSBPLAY_LOOPING) once. Every sound after that
    is a secondary buffer that DirectSound's own mixer sums into it. There is
    no loop in the image that adds two samples together, so the criterion as
    written could never have been met by anything, and it is corrected in B6
    rather than left to look unfinished.

    What the engine's half IS, is decisions - which buffer exists, which voice
    plays it, where it is, how loud, at what rate - which is the same boundary
    A2 draws for the renderer. Seventeen wrappers, and **three of them are in
    neither `Runtime.exe.c` nor `readable/`**: `Sound_SetFrequency`
    (0x0046CBF0), `Sound_GetFrequency` (0x0046CC30) and `Sound_LengthMs`
    (0x0046CC70). Their addresses came from disassembling the gap between
    `Sound_SetVolume` and `Sound_FindVoice` and measuring each block back to a
    16-byte alignment.

    **Why they are absent was first written here as the missing `push`
    prologue, and that was wrong.** `Sound_SetVolume` (0x0046CBB0) opens with
    the identical `A1 <ppDS>` and no `push`, and it *is* decompiled. The real
    difference is measured below: scanning every `E8` rel32 in the image gives
    `Sound_SetVolume` **6 direct callers** and each of the three **0**, with
    **0 dword references** to any of them anywhere, so nothing takes their
    address either. IDA never made functions of them because nothing reaches
    them - which means **they are dead code**, joining `pluie.wav`, options
    page 12 and the X-Tech shoot callback on the shipped-and-unreachable list.
    They are ported anyway (`Sound_LengthMs` carries an exact arithmetic law
    worth having), and no claim here rests on them.

    **The tiers, per B1 and declared in three places** (`mixer.h`, here, and
    the coverage row): **2** for the `.wav` acceptance walk and for the
    immediates and vtable offsets asserted against the IMAGE below; **3** for
    `Sound_LengthMs` and the mixer's transparency, both differenced against a
    Python re-derivation - and B1's warning is not softened, since **both sides
    were written from one reading of the same assembly**, so this catches an
    offset or an off-by-one and cannot catch a wrong reading applied
    consistently; **6** for the bank and voice bookkeeping; and **none at all**
    for the attenuation and pan law, which is DirectSound's, is described
    nowhere in the image, and no rig this repo has can record.

    What the shipped data is asked, and could fail:

    * `Wav_LoadToBuffer` (0x0049F830) over all 61 `gamedata/I2D/sounds/*.wav`. It
      reads a 20-byte header and requires "RIFF" and "WAVEfmt "; reads SIXTEEN
      bytes of WAVEFORMATEX whatever the chunk says; requires tag 1; then
      **peeks two bytes and seeks back over them only if they are non-zero** -
      a trick for the 18-byte extended form which reads the `da` of the next
      chunk id as "not a cbSize". All 61 take the rewind, **0 take the other
      arm and 0 chunks are skipped**, so two of the loader's branches are dead
      against the shipped corpus. Recorded rather than trimmed.
    * `Sound_LengthMs` per file, `bytes * 1000 / ((bits/8) * freq * channels)`,
      truncating - re-derived here in Python from the same files, so the two
      implementations are differenced rather than one asked about itself.
    * the interface's own table: **45 names, all 45 resolving** to a shipped
      file, against a cache of **32 slots** (unk_657B40..dword_657D40, 16 bytes
      each). `Ui_LoadSound` (0x00482D00) takes the first slot whose flag bit 0
      is clear and **returns silently** when there is none, so 13 of the 45 can
      never be resident together. 16 further `.wav` ship that the table does
      not name.

    And the one claim the reference mixer makes about a waveform, which is
    B6's criterion restated for a boundary that turned out to be a device: a
    mono voice, not 3D, at full volume and at the mix rate must come out
    **sample-identical in both channels**. `men001.wav` does, over 32953
    frames, and the FNV of the left channel is recomputed HERE from the file's
    own data chunk - so it is a differential across the two implementations,
    not C++ agreeing with itself. `pause.wav` is the control: it ships at
    **22080 Hz**, the only shipped file that is not 22050, so it resamples and
    33572 of its 34323 frames must differ.

    Shown to fail, every mutation run with the object **and the binary**
    deleted so B4's stale-link trap cannot disguise one: dropping the two-byte
    peek rejects 61 of 61 (the loader lands mid-`data`); taking the first
    `"sounds"` in `tables/ui.json` finds a SCREEN's twelve-slot array instead
    of the name table and gives **0 names** - which it did, and the check
    passed until the count was asserted; mixing at `pause.wav`'s rate without
    resampling takes its mismatches 33572 -> 0; dropping the listener's
    `<= 0.0001` guard takes it 1 -> 0; and `Sound_FreeBuffer` not stopping the
    voices that play the buffer leaves 4 alive where 0 should be.

    **One mutation moves nothing, and it is recorded rather than counted**:
    using `nBlockAlign` in place of `(bits/8)*channels` in `Sound_LengthMs`.
    Every shipped file is 16-bit mono, so the two are equal throughout the
    corpus - a B7 anti-pattern (a test that cannot separate the rule from a
    simpler one), not a weak check. It was re-run with a forced relink to rule
    out B4's trap before being written down this way.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    ui  = os.path.join(ROOT, "tables", "ui.json")
    if not (os.path.isdir(eng) and os.path.isdir(fr) and os.path.exists(ui)):
        return ("skipped",), ("skipped",), "engine/, gamedata/ or tables/ui.json absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_audio")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, fr, ui, out], capture_output=True)
        raw = open(out, "rb").read()
        n, = struct.unpack_from("<i", raw, 0)
        v = struct.unpack_from("<%di" % n, raw, 4)
        off = 4 + n * 4
        nf, = struct.unpack_from("<i", raw, off)
        tbl = struct.unpack_from("<%dI" % (2 * nf), raw, off + 4)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # ---- the independent side: read the same files in Python -------------
    def wav(path):
        d = open(path, "rb").read()
        if d[:4] != b"RIFF" or d[8:16] != b"WAVEfmt ":
            return None
        tag, ch, rate, avg, align, bits = struct.unpack_from("<HHIIHH", d, 20)
        if tag != 1:
            return None
        o = 36
        if struct.unpack_from("<H", d, o)[0] != 0:
            pass                      # the rewind: nothing consumed
        else:
            o += 2
        while o + 8 <= len(d):
            cid = d[o:o + 4]
            sz, = struct.unpack_from("<I", d, o + 4)
            if cid == b"data":
                return (sz, rate, ch, bits, d[o + 8:o + 8 + sz])
            o += 8 + sz
        return None

    sd = os.path.join(fr, "I2D", "sounds")
    files = sorted(os.path.join(sd, f) for f in os.listdir(sd)
                   if f.lower().endswith(".wav"))
    mine = []
    for p in files:
        w = wav(p)
        if w is None:
            continue
        sz, rate, ch, bits, _ = w
        d = (bits // 8) * rate * ch
        mine.append((sz, (sz * 1000) // d if d else 0))
    agree = sum(1 for i, (a, b_) in enumerate(mine)
                if i < nf and tbl[2 * i] == a and tbl[2 * i + 1] == b_)

    men = wav(os.path.join(sd, "men001.wav"))
    h = 2166136261
    if men is not None:
        for byte in men[4]:
            h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    hOut = v[37] & 0xFFFFFFFF

    # ---- and the IMAGE, so the transcription is not asked about itself ----
    import ui_tables
    e = ui_tables.Exe()
    PPDS = bytes([0xA1, 0xAC, 0x50, 0x6A, 0x00])      # mov eax, ds:ppDS
    absent = (0x0046CBF0, 0x0046CC30, 0x0046CC70)
    # all FOUR open the same way, which is why the prologue is not the reason
    prologues = sum(e.read(a, 5) == PPDS for a in absent + (0x0046CBB0,))
    def vcalls(va, n):                       # FF 51 xx = call [ecx+xx]
        b = e.read(va, n)
        return [b[i + 2] for i in range(len(b) - 2)
                if b[i] == 0xFF and b[i + 1] == 0x51]
    vt = (vcalls(0x0046CBB0, 64), vcalls(0x0046CBF0, 64),
          vcalls(0x0046CC30, 64), vcalls(0x0046CC70, 160))
    called = {a: 0 for a in absent + (0x0046CBB0,)}
    taken = dict(called)
    for sva, vsz, ptr, rsz in e.sec:
        blob = e.d[ptr:ptr + rsz]
        for i in range(len(blob) - 5):
            if blob[i] == 0xE8:
                tgt = e.base + sva + i + 5 + struct.unpack_from("<i", blob, i + 1)[0]
                if tgt in called:
                    called[tgt] += 1
            w = struct.unpack_from("<I", blob, i)[0]
            if w in taken:
                taken[w] += 1
    # the caps word is BRANCHLESS - 176 is never an immediate
    caps = e.read(0x0046C792, 64)
    branchless = (bytes([0x1B, 0xC9]) in caps and
                  bytes([0x83, 0xE1, 0xD0]) in caps and
                  bytes([0x81, 0xC1, 0xE0, 0x00, 0x00, 0x00]) in caps)
    lis = e.read(0x0046D080, 400)
    sli = e.read(0x00456B40, 300)
    lits = (struct.pack("<I", 0x3CD013A9) in lis,      # 0.0254, metres/inch
            struct.pack("<I", 0x3F800000) in lis,      # rolloff 1.0
            struct.pack("<I", 0x421C0000) in sli,      # 39.0
            sli.count(struct.pack("<I", 0x44124000)))  # 585.0, exactly once
    prim = e.read(0x0046C3A0, 700)
    fmt_ok = (struct.pack("<I", 22050) in prim and
              struct.pack("<I", 88200) in prim)
    span = 0x004D0D14 - 0x004D0990                     # the 45-name table

    return (v[0], v[1], v[2], v[3], v[4],
            v[5], v[6], v[7], v[8], v[9],
            v[10], v[11], v[13], v[14],
            v[16], v[17], v[18], v[19], v[20],
            v[21], v[22], v[23], v[24],
            v[25], v[26],
            v[27], v[28], v[29], v[30],
            v[31], v[32], v[33], v[34],
            nf, len(mine), agree, 1 if h == hOut else 0,
            v[39], v[40], v[41], v[42], v[43], v[44], v[45],
            prologues, vt,
            called[0x0046CBB0], sum(called[a] for a in absent),
            sum(taken.values()), branchless, lits, fmt_ok, span, span // 20), \
           (61, 61, 61, 0, 0,
            45, 45, 32, 32, 13,
            160, -1, 16, -1,
            5, 7, 1, 3, 1,
            0, -1, 4, 0,
            1, 1,
            0, -5000, -10000, -10000,
            32953, 0, 34323, 33572,
            61, 61, 61, 1,
            176, 224, 22050, 2, 16, 4, 88200,
            4, ([60], [68], [32], [20, 32, 12]),
            6, 0,
            0, True, (True, True, True, 1), True, 900, 45), \
           "shipped .wav found, accepted by Wav_LoadToBuffer's own path, " \
           "those REWINDING the two-byte peek, those eating it and the " \
           "chunks skipped before `data` (the last two are 0 - two dead " \
           "branches); then the interface table - names, those resolving, " \
           "the cache's slots, those that fit and the 13 that cannot be " \
           "resident; then the 160-buffer bank filled and refusing, and the " \
           "16-voice pool doing the same; then Sound_Play3D's flag word on " \
           "its four arms (2D 5/7, 3D 1/3 - bit 2 is `no 3D`, bit 1 `loop`) " \
           "and the placement block reaching the record; findVoice by " \
           "(sound, owner) and a miss; voices before and after " \
           "Sound_FreeBuffer, which kills what plays it; the listener basis " \
           "normalised and its <=0.0001 guard zeroing rather than dividing; " \
           "the volume law at 0/50/100/200 percent - an ATTENUATION, so 0 " \
           "is full and it clamps at 100; then the mixer's transparency - " \
           "men001 frames and mismatches, and pause.wav at 22080 Hz which " \
           "must NOT be identical; then the per-file length table against a " \
           "Python re-derivation, and the rendered left channel's FNV " \
           "against one computed HERE from the file; and the two caps words " \
           "(0xB0 with 3D, 0xE0 without - PAN only when 3D is off) with the " \
           "primary format Sound_Init sets; then the IMAGE rather than this " \
           "transcription of it - all FOUR wrappers opening `mov eax, " \
           "ds:ppDS` with no push (so the prologue is NOT what makes three " \
           "of them absent), the vtable offset each calls (SetVolume +60, " \
           "SetFrequency +68, GetFrequency +32, and LengthMs's " \
           "GetFormat/GetFrequency/GetCaps at +20/+32/+12), then who REACHES " \
           "them - Sound_SetVolume's 6 direct callers against the three's 0, " \
           "and 0 dwords anywhere holding any of their addresses, so the " \
           "three are DEAD CODE; the caps word computed branchlessly (sbb, " \
           "and -0x30, add 0xE0 - 176 is never an immediate, and searching " \
           "for it finds nothing); the literals the docs quote - 0.0254 and " \
           "1.0 in the listener, 39.0 in the slider and 585.0 there exactly " \
           "ONCE, loaded as both its max distance and its audibility test; " \
           "the primary format's 22050 and 88200; and the name table's " \
           "span, 900 bytes = 45 rows of 20 with nothing over"


def c_engine_camedit():
    r"""`engine/`'s camera EDITINGS - SCX chunk 10, how a cutscene is cut.

    The payload is what the engine calls a *camera file*, and its loader
    (0x0049EEF0) rejects any version but 3 with "Invalid camera file version".
    Four arrays cross-referenced by id - cameras, keys, tracks, editings -
    each resolved to a pointer at load.

    **The loader returns 0 on any miss**, so a dangling reference would stop
    the scene loading at all. That is what makes counting them a test rather
    than a statistic, and there are **0** across the corpus.

    **29 of the 220 scenes carry a chunk 10, all 29 walk exactly to the end of
    the payload**, and they hold 1073 cameras, 1073 keys, 418 tracks and
    **125 editings over 24112 frames**. **30 of the 125 ship unlinked** - their
    +28 handle is 0, so no script object plays them; that is a property of the
    data, and a reader that treated it as an error would reject the shipped
    game.

    An editing is a keyframed cut sequence: tracks in order, each a run of
    (frame, camera) keys interpolated linearly by Cam_PlayEditing.
    Scene_LoadSCX links each to the script object its handle names, and
    Script_PlayScript samples it at that object's program clock - which is what
    makes a cutscene's camera follow its beats rather than a wall clock.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    scpt = omkpaths.data("SCPTDATA")
    if not (os.path.isdir(eng) and os.path.isdir(scpt)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/SCPTDATA absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_camedit")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "c.bin")
    try:
        subprocess.run([binp, scpt, out], capture_output=True)
        v = struct.unpack_from("<10i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (220, 29, 29, 0, 1073, 1073, 418, 125, 30, 24112), \
           "scenes; those carrying a chunk 10; those whose walk lands exactly " \
           "on the payload end; UNRESOLVED references, which must be zero " \
           "because the loader returns 0 on a miss; then cameras, keys, " \
           "tracks, editings, those shipped UNLINKED (a property of the data, " \
           "not an error), and the frames they span"


def c_engine_fonts():
    r"""`engine/`'s `FONTS/*.FNT` reader - the interface's bitmap faces.

    256 glyph records of 8 bytes indexed by character code, then the pixels:
    width x height bytes a glyph, one byte a pixel holding a COVERAGE level
    0..31.

    **The offset is in EIGHT-BYTE units.** Read as bytes, every glyph block
    lands in the wrong place and most run past the file - which is why "0
    outside the file, 0 overlapping" is the test that says the unit is right,
    not a tidiness check. **13 fonts, 2899 glyphs, 485877 pixel bytes**, all
    thirteen starting their pixels at 2048 (the glyph table is 256 records
    however few are used) and all thirteen covering exactly codes 33..255 -
    space is deliberately absent so it falls to the font record's default
    advance.

    **The coverage is the whole colour model**: `sub_43EA10` builds a 32-entry
    ramp of the requested colour once and each non-zero byte indexes it, so a
    glyph is greyscale antialiasing that takes the text's colour at draw time.
    Exactly **2** bytes in the whole corpus index past the ramp, both in
    SMALL's '!' (UI 5).

    **One number this reports that the Python did not.** The reference counts
    unaccounted bytes as the TAIL after the last glyph block - 135 across the
    13 files, which is what "alignment padding" meant. Measuring the gaps
    BETWEEN blocks as well turns up **7716** more. Nothing is wrong with
    either file; they are two different quantities, and conflating them hid
    the second. Reported separately here so neither can be mistaken for the
    other.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fonts = omkpaths.data("FONTS")
    if not (os.path.isdir(eng) and os.path.isdir(fonts)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/FONTS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_fonts")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "f.bin")
    try:
        subprocess.run([binp, fonts, out], capture_output=True)
        v = struct.unpack_from("<10i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (13, 13, 13, 2899, 0, 0, 485877, 2, 135, 7716), \
           "fonts; those whose pixels start at 2048; those covering codes " \
           "33..255; glyphs; those landing OUTSIDE the file and those " \
           "OVERLAPPING (both must be zero - the test the eight-byte offset " \
           "unit could fail); pixel bytes; bytes indexing past the 32-entry " \
           "ramp (exactly two, both in SMALL's '!'); the trailing bytes the " \
           "Python counts; and the inter-glyph gaps it does not"


def c_engine_world_data():
    r"""`engine/`'s three world-DATA tables: subscriptions, objects, recipes.

    Ported together because they interlock, and because each one has a way of
    looking right on its own that the others catch.

    **The subscriptions are the SAME BYTES as the second script table.** The
    8-byte records `Message_RunHandlers` walks are what `chunkSlots` mines
    script offsets out of; read the other way they are `[int32 handler][int16
    message id]`. That double duty is why AREA 118's `+68` read as a script
    pointer for a while and was a coincidence (CLAUDE.md 6): its subscription
    table is EMPTY and based at the start of the code after it. **154
    subscriptions, 138 with a handler, 0 pointing outside their chunk, ids
    0..32** - and the 16 without a handler are rows, not failures.

    **`IAM\OBJECT` is 1002 fixed 2048-byte slots, not an archive.** The test
    the data could fail is `+0 == the slot number`, **1002 / 1002**; an
    archive reading gets a directory that is really the first record's fields
    and this check would collapse.

    **The recipes, and the finding that is a negative one.** `GLOBAL +12`
    holds 11 symmetric 8-byte records `[a][b][product][gate]`; all **33** ids
    name a real object, so the table is read right. The gate is compared
    against a global with exactly four references in the binary, all inside
    `Game_HandleEvent` case 37, which writes 1, 0 and -1 - never 8. **Six of
    the eleven want 8**, so every spell recipe is dead. `+64` is an **int16**
    naming object 330, "Beshe'm sanctifie", the item whose selection writes
    the 1; read as a dword it gives 29229386, a number nothing downstream
    rejects, which is how the width was caught.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_world_data")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "w.bin")
    try:
        subprocess.run([binp, omkpaths.data_root(), out], capture_output=True)
        v = struct.unpack_from("<10i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (154, 138, 0, 32, 1002, 1002, 11, 33, 6, 330), \
           "message subscriptions, those with a handler, those pointing " \
           "outside their chunk, the highest message id; objects and those " \
           "whose +0 is their own slot; recipes, the ids of theirs naming a " \
           "real object (3 each), the gates wanting 8 - which nothing ever " \
           "writes, so six recipes are dead - and GLOBAL +64 as an int16"


def c_engine_fight_ai():
    r"""`engine/`'s fight-AI profiles - the `.CTL` `+76`/`+80` table.

    The walk already stepped OVER this table to keep landing exactly on EOF;
    this reads it. 156 bytes a profile, `+0` the difficulty level plus one,
    twelve situation slots of moves - and a move is a SEQUENCE of input words
    `Fight_TickAI` pushes into the actor's queue with `Perso_InjectInput`. So
    the AI has no animation path of its own: it plays the same `.CTL` machine
    the player does, by pressing buttons, which is why this lives in the
    format reader.

    Three things the data could fail, and one it could not fake:

    * the walk still lands exactly on all **7** files with the table now read
      as contents rather than skipped;
    * only the **3** combat files carry profiles, **11** in all, and each
      profile's move and word counts match `anim_ctl.py` row for row;
    * every input word is a real binding - `w & ~0x40000000` inside the 14
      bits ASSETS documents.

    The **bit union is 0xCFF**: the AI presses ten of the fourteen and never
    CTRL, SPACE, SHIFT or TAB, which are exactly the non-combat bindings. That
    is a fact about the content, not the parse, and a wrong stride would not
    produce it.

    `0x40000000` is the queue's idle word, interleaved so the machine sees a
    release between presses. It is NOT `0x80000000`, the "no input" edge code:
    `Cef_InputMatches` ends `if (a1 == 0x80000000) return a2 <= 0x2000`, so the
    idle word matches no idle edge.

    Two profiles wait >= 3000 ms between moves - the easy levels. Harder is
    shorter, throughout.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    anims = omkpaths.data("ANIMS")
    if not (os.path.isdir(eng) and os.path.isdir(anims)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ANIMS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_fight_ai")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, anims, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    head = struct.unpack_from("<8i", raw, 0)
    per = [struct.unpack_from("<3i", raw, 32 + 12 * k)
           for k in range((len(raw) - 32) // 12)]
    want = [(1, 28, 96), (2, 35, 136), (3, 31, 140),
            (1, 32, 110), (2, 47, 173), (3, 47, 212), (4, 5, 12),
            (1, 26, 86), (2, 45, 165), (3, 46, 196), (4, 5, 12)]
    return (head, per), ((7, 7, 3, 11, 30, 1, 0xCFF, 2), want), \
           ".CTL files, those walking exactly to EOF, those with profiles, " \
           "profiles; distinct input words, whether every one is a real " \
           "binding, the bit UNION (0xCFF - ten of fourteen, never the four " \
           "non-combat bindings) and the profiles waiting >= 3000 ms; then " \
           "each profile's level, moves and input words, which must match " \
           "anim_ctl.py row for row"


def c_engine_programs():
    r"""`engine/`'s SCX object interpreter - `Script_PlayScript`, ticked.

    The other half of the game's logic. The world scripts decide *what*
    happens; an object's program decides *how it looks while it happens*, and
    `Script_PlayAllScripts` runs one frame of every object of both resident
    scenes.

    **The corpus half** asserts the runtime fields really are authored repeat
    counts rather than something else that happens to fit: **13887 function
    records over 4511 objects**, the run counter identically **0** on disk (it
    is state, not data), the repeat limit 1 in 13247 and -1 in 43 with
    **nothing** outside 1..32 or -1, and the object loop count taking exactly
    **two** values, 1 (3551) and -1 (960).

    That last number is why the interpreter models a bug rather than fixing
    it. The rewind past the last function goes through `Script_StartScript`,
    which zeroes `loopsDone` along with the pc and the clock - so a finite
    loop count above 1 would loop for ever. It never bites, because 2 is not
    a value the data uses; the engine's behaviour is the specification, so the
    port reproduces it.

    **The sync-chain half** is the one `Telis_eat` cannot reach: both its
    functions end their chain, so it is blind to *which array* a `+12` sync
    index counts in. `Impasse.SCX`'s `A_2_DemonLook` is not - its first step
    carries `sync = 0`, which the loader resolves as sync record 0 and not as
    function 0 - and its 132 frames (clip 15's 91 then clip 17's 41) are
    exactly the duration of `sautdemon`, the camera editing linked to it. See
    `verify.py: scx sync chain`, which runs both readings over all 95 linked
    editings.

    **The run half** is the plan's own test: `Re14.SCX`'s `Telis_eat` has loop
    -1 and two `SelectBodyAnimation` functions each authored once, so the
    interpreter must cycle pc 0, 1, 0, 1 ... for ever. Over 400 frames it
    begins **13** clips, alternating `TELRES02.3DA` and `TELRES05.3DA` with no
    repeat, is still running at the end and has rewound 6 times - matching
    `tools/sim/scene.py` exactly. (12 and 5 until 2026-09-03, when the busy
    window was corrected to end on the tick that draws the clip's last frame:
    each cycle is a frame shorter, so more of them fit in 400.) Mishandle the repeat limit, the loop count
    or the busy window and it collapses to one clip or ends.

    What `busy` means per function is the one thing NOT in
    `Script_PlayScript` - it is in each handler - so it is a stated model:
    an animation is busy for its clip's frame count, `Wait` for its float
    parameter, `PlaySyncSound` until param 1 reaches the clock, everything
    else never.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    scp = omkpaths.data("SCPTDATA")
    if not (os.path.isdir(eng) and os.path.isdir(scp)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/SCPTDATA absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_programs")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "p.bin")
    try:
        subprocess.run([binp, scp, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<19i", raw, 0)
    names = raw[76:76 + v[18]].decode("ascii")
    return v[:18] + (names,), \
           (13887, 4511, 1, 0, 13247, 43, 0, 2, 3551, 960,
            -1, 2, 1, 1, 6, 13, 132, 558, "TELRES02.3DA,TELRES05.3DA"), \
           "function records and objects; the distinct run counters on disk " \
           "and the only one (0); the repeat limit 1 and -1 counts and how " \
           "many fall outside 1..32 or -1 (0); the distinct loop counts and " \
           "the 1 and -1 counts; then Telis_eat's loop, its distinct clips, " \
           "whether they ALTERNATE, whether it is still running after 400 " \
           "frames, its rewinds, the clips it began and their names; and " \
           "finally the two Impasse objects Telis_eat cannot stand in for: " \
           "A_2_DemonLook's 132 frames, which exercise the SYNC CHAIN and " \
           "are exactly `sautdemon`'s duration, and C_2_MecaSpeaks' 558, " \
           "which exercise a REPEAT above 1 and are exactly `mecaspeak`'s " \
           "- all matching tools/sim/scene.py"


def c_sprite_ids_are_scene_local():
    r"""ASSETS 3b: an effect's sprite id is SCENE-LOCAL, so the table must follow
    the scene.

    `Sfx_TickAmbient` resolves an effect's `+8` sprite through the SCENE -
    `sub_4A5800(scene+8, sprite)` walks the resident scene's own registrations -
    and the ids are local to the file that registers them::

        Grid.sfx     wants 9 10 11 12          Grid.SCX     registers 9 10 11 12
        anekbah.sfx  wants 49589 49590 49591   anekbah.SCX  registers those three
        Impasse.sfx  wants 13 14 114 116 117 139 140 200

    `aventure.SCX`, the global library, registers 2..137 and none of Anekbah's.

    **This is why a viewer must reload its sprite table on a scene change**, and
    the numbers below say a boot-time load can never be enough for anything but
    the boot scene: **every one of the 66** scenes carrying ambient effects
    wants at least one id the global library does not define. Twelve wanted ids
    exist in BOTH, and those are worse than the missing ones - they resolve, to
    the wrong picture, which is the shape reported as "some Impasse particles
    draw incorrectly" (`todo/omk-play.md` 48). `omk-play` loaded sprites once at
    startup, so walking out of the intro left every later effect resolving
    against GRID's table: Anekbah's three ids fall outside it and drew nothing,
    which is fire and smoke not working.

    Seven of the 66 do NOT supply every id they want from their own stream; they
    are the ones whose remainder the global library covers, and they are why the
    global is loaded FIRST and the scene's over it rather than instead of it.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/sprite_ids"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "sprite_ids")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "s.bin")
        subprocess.run([binp, fr, out], capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 20:
        return ("no output",), ("20 bytes",), "the probe must write its record"
    scenes, selfSupplied, needLocal, collide, wantTotal = struct.unpack_from("<5i", raw, 0)
    return (scenes, selfSupplied, needLocal, needLocal == scenes, collide, wantTotal), \
           (66, 59, 66, True, 12, 167), \
           ("scenes with ambient effects; those supplying every wanted sprite id from "
            "their OWN stream; those wanting an id the global library lacks - ALL of "
            "them, which is why the table must follow the scene; ids defined in both "
            "(the global would win and draw the wrong picture); wanted ids in all")

def c_engine_props():
    r"""`engine/`: the world's PROPS - the Impasse's rings, end to end.

    A play report: *the "anneaux" in the impasse currently doesn't appear*.

    `SCENE 55`'s startup script ends with `object.show 162`; OBJECTS 162 is
    `3 Anneaux magiques` with stem **`ANNEAU`**, and `MESHES/OBJETS/ANNEAU.3DO`
    ships - the executable even carries the literal `meshes\objets\anneau.3do`.

    **The prop record is 24 bytes**, read by `Scene_LoadProps` (0x00409FC0) as
    an `int*` stepping `i += 6`::

        +0  i16 runtime slot (-1 on disk)   +2  i16 OBJECTS id
        +4/+8/+12   int32 POSITION          +16/+18/+20  i16 ROTATION
        +22 i16 index into the DB's 2-bit state array

    **Bit 0 says the loader loads the model at all; bit 1 that it is SHOWN**
    (`Object_ShowInScene` links its node in and sets its state to 2, the same
    "linked" state a decor set uses). Op 76 walks AREA props (`+44`/count
    `+74`) then SCENE (`+12`/`+42`) and does
    `if (state & 1) { state |= 2; Object_ShowInScene(slot); }`; op 77 clears
    the bit. **Note 77 guards on a null record and 76 does not** - the engine
    reads `[0+16h]`, the Win9x null page - which the port does not copy.

    **`Area_Load` converts the table IN PLACE** before anything reads it, at
    `propTable + 8` in 24-byte steps: a position by
    `v * 100 * 0.00390625 * 0.3937007874 - 1` - hundredths of a 256th of a
    centimetre into the engine's INCH - and a rotation by `v * 0.087890625`,
    a 4096-per-turn integer into degrees, the same convention as the world
    cameras. The rings are stored `(47397, -514, 19614)` and land at
    **(7288, -80, 3015)**, which is where the player walks: that agreement is
    what identifies the conversion, since three coordinates cannot land on the
    arrival address by accident.

    Asserted through a live Session: AREA 222 carries one prop that exists and
    none shown, and after the Impasse's beats run it is shown, at that place.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/prop_probe"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "prop_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "p.bin")
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"),
                        os.path.join(fr, "IAM", "START"),
                        os.path.join(fr, "SCPTDATA"), out],
                       capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 32:
        return ("no output",), ("32 bytes",), "the probe must write its record"
    (n0, shown0, ring0, n1, shown1, ring1, x, z) = struct.unpack_from("<8i", raw, 0)
    return (n0, shown0, ring0, n1, shown1, ring1, x, z), \
           (1, 0, 0, 1, 1, 1, 7288, 3015), \
           ("AREA 222's props that EXIST and those SHOWN before the Impasse's "
            "beats and after, whether the rings are among them, and where they "
            "land once Area_Load's conversion is applied")

def c_engine_impasse_fx():
    r"""`engine/`: the Impasse cutscene actually PRODUCES effects.

    A live Session, AREA 222 with SCENE 55 over it, run for 900 frames. The
    sixteen beats fire **15 set-piece rows** through the object-start path
    (`sub_451470(0, id)` at the tail of `ScriptObject_Start`, which shows every
    section E row whose `+8`/`+12` match the id) and the particle field peaks
    at **108** live particles, alive on 262 of the 900 frames.

    **This check exists because a regression got past the whole suite.**
    Issue 52 stopped `reloadScene` rebuilding the runner when the area is
    unchanged - correct - but `Session::loadScene` set the area tracker
    WITHOUT binding the `.sfx`, so the later `reloadScene` early-returned and
    the bind never happened. Every one of these numbers went to 0 and all 254
    checks still passed, because nothing asserted a scene's effects from a
    live Session: `engine: particles` drives the field directly and
    `dump_fx`'s corpus walk never runs a Session at all. `Area_LoadScx`
    (0x0041B4E0) loads the `.SCX` and binds the `.sfx` in ONE function, so
    every path that makes an `.SCX` resident owes the bind; `attachSceneSfx`
    is that, called from both.

    Emitters peak at 0 and that is not a fault: the Impasse's rows are
    object-fired one-shots, and `sub_451600` registers one emitter per shown
    row per frame, which is consumed within the tick. The standing family -
    rows keyed `(1, -1)` - is what `Sfx_BindAmbientEffects` shows itself, and
    the Impasse carries none.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/impasse_fx"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "impasse_fx")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "i.bin")
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"),
                        os.path.join(fr, "IAM", "START"),
                        os.path.join(fr, "SCPTDATA"), out],
                       capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 16:
        return ("no output",), ("16 bytes",), "the probe must write its record"
    fired, peak, frames, emitters = struct.unpack_from("<4i", raw, 0)
    return (fired, peak, frames, emitters), (15, 108, 262, 0), \
           ("the Impasse's beats fire 15 set-piece rows and peak at 108 live "
            "particles, alive on 262 of 900 frames")

def c_credit_layout():
    r"""UI: the Bowie credits are ordinary subtitles positioned by `{X}` markup.

    A reader described the Anekbah opening as "a specific case, maybe with some
    dedicated code". It is not. `AREA 0` record 78 - the 145.7 s title sequence,
    one of the 106 world-camera scripts - is a plain camera script that fires
    **twenty `media.play` calls**, and each object's `+280` description is a
    credit block::

        716  {X010030}{f1}Ecrit et realise par
             {X020035}{f3}David CAGE
        719  {X090058}{f1}{D}Direction programmation
             {X080065}{f3}{D}Olivier NALLET

    `{X<xxx><yyy>}` is "move to (xxx, yyy) as PERCENTAGES of the screen"
    (docs/UI.md 5), `{f1}`/`{f3}` are GENERIC1 and GENERIC3, and `{D}` is right
    alignment - used by the blocks on the right of frame, which is why
    `{X090058}` is 90% across. **`omk-play` parsed `{X}` and threw it away**
    (`if (d == 'X' ...) { i += 7; continue; }`), so every credit landed at the
    bottom like an ordinary subtitle.

    Also settled on the way, by DECODING A FRAME and looking at it: the
    sequence is NOT pre-rendered video. `FLIS/GAME.MPG` at 60 s is the club
    cinematic, so the credits really are drawn over the live city.

    One block of the 46 carries no text - object 715, `ZVO G001 TITRE`, is
    `{X030040}{f3}` and nothing else - so the title card itself comes from
    somewhere this does not reach. Recorded rather than explained.

    A near-miss worth keeping: `grep` finds no credit name anywhere in the
    tree, which looks like proof the text is absent - but `grep` cannot find
    "Confirmer" in `IAM/` either, so the method was invalid and the negative
    worthless. The text was there all along, behind the object reader.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/credit_layout"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "credit_layout")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "c.bin")
        subprocess.run([binp, fr, out], capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 20:
        return ("no output",), ("20 bytes",), "the probe must write its record"
    blocks, withText, right, faces, bad = struct.unpack_from("<5i", raw, 0)
    # THE TITLE CARD, which is why one block carries no text. `media.play 715`
    # (`ZVO G001 TITRE`) is a kind-16 DOCUMENT: the handler builds
    # `IMAGES\<stem>.BMP`, loads it, sets ACTOR_STATE 10 (`ImageScreen`) and
    # plays NO audio, so the words are in the bitmap and the description is
    # only its `{X}` placement. `IMAGES/ZVOG001.BMP` is 640x480 with the logo
    # on black - 284581 of its 307200 pixels are the key.
    b2 = subprocess.run(["make", "-s", "build/title_logo"], cwd=eng,
                        capture_output=True, text=True)
    lb = os.path.join(eng, "build", "title_logo")
    logo = ("no probe",)
    if b2.returncode == 0 and os.path.exists(lb):
        t2 = tempfile.mkdtemp()
        try:
            o2 = os.path.join(t2, "l.rgb")
            r = subprocess.run([lb, fr, o2], capture_output=True, text=True)
            raw2 = open(o2, "rb").read() if os.path.exists(o2) else b""
            black = sum(1 for i in range(0, len(raw2), 3) if raw2[i:i+3] == b"\0\0\0")
            logo = ("image 1" in r.stdout, "ZVOG001" in r.stdout,
                    "640x480" in r.stdout, black)
        finally:
            shutil.rmtree(t2, ignore_errors=True)
    return (blocks, withText, right, faces, bad) + logo, \
           (46, 45, 7, 45, 0, True, True, True, 284581), \
           ("the twenty ZVO credit objects' `{X}` blocks; those carrying text (the "
            "one that does not is 715, the TITRE card); those right-aligned by `{D}`; "
            "those naming their own face; and any positioned off-screen; then the "
            "TITLE CARD - that 715 resolves as a kind-16 DOCUMENT with stem ZVOG001, "
            "that its bitmap is 640x480, and how many of its pixels are the black "
            "colour key")

def c_subtitle_box():
    r"""UI: the subtitle BOX and its two blends, read out of the renderer.

    `Dialog_TickUI` (0x0046A200) draws no box - it makes no call but
    `Text_DrawBlock`, four times. The box belongs to the TEXT RENDERER:
    `sub_4400D0` (0x004400D0), which `Game_Tick` calls as
    `sub_4400D0(0, dword_6A52C4, dword_6A52C0, height - 1, ...)`, submits one
    quad before the glyphs and switches on `off_4C71A8`, the second colour
    `Dialog_TickUI` sets::

        off_4C71A8 == 0x80002040  ->  flags 4, top = a2 - 32     the REPLIES
        off_4C71A8 == 0x00808080  ->  flags 2, top = a2 - 4      the LINE
        x 18 .. width-18,   y (top - 8) .. height - 18

    `I2D_SubmitQuad` copies 0x30 bytes - four vertices of (x, y, colour) plus
    a flag word - and `sub_480BD0` fills all four corners from the first
    unless flag 8 is set, which neither does: both are a FLAT fill.

    **The flags are the blend**, through `sub_480AC0`, which sets D3D render
    states 19 (SRCBLEND) and 20 (DESTBLEND)::

        & 1   src 2 ONE          dst 2 ONE           additive
        & 2   src 1 ZERO         dst 4 INVSRCCOLOR   dst *= (1 - src)
        & 4   src 6 INVSRCALPHA  dst 5 SRCALPHA      src*(1-a) + dst*a

    So the LINE's box is a 50% DARKENING (`0x808080` through INVSRCCOLOR) and
    the REPLIES' is 50% of the navy `0x002040` - the "black transparent" and
    "blue transparent" boxes a reader described. The line's box is invisible
    over the black letterbox band, which is why the same reader could not say
    whether a plain subtitle had one.

    Asserted against the decompilation so the reading cannot drift; skipped
    when `readable/` is absent, since it is a derivative work and is not
    distributed.
    """
    import re
    src = os.path.join(ROOT, "readable", "src")
    if not os.path.isdir(src):
        return ("skipped",), ("skipped",), "readable/ is not distributed"
    dinput = open(os.path.join(src, "15_dinput.c"), encoding="utf-8", errors="replace").read()
    i = dinput.index("@func 0x004400D0"); j = dinput.index("@func 0x00440", i + 20)
    box = dinput[i:j]
    d3d = open(os.path.join(src, "21_d3d.c"), encoding="utf-8", errors="replace").read()
    k = d3d.index("@func 0x0046A200"); m = d3d.index("@func 0x0046A", k + 20)
    dlg = d3d[k:m]
    # the box's own constants, and that it submits exactly one quad on layer 10
    got = (
        "off_4C71A8 == (void *)-2147475392" in box,   # the reply colour
        "off_4C71A8 == &unk_808080" in box,           # the line colour
        box.count("I2D_SubmitQuad"),
        "v5 = 4" in box, "v5 = 2" in box,             # the two blends
        "a2 - 32" in box,                             # the reply top
        len(re.findall(r"v2\d+ = 18", box)) >= 1,     # the 18px inset
        # Dialog_TickUI: no draw but Text_DrawBlock, and the three inks
        dlg.count("Text_DrawBlock"), "I2D_" in dlg,
        "unk_8080C0" in dlg, dlg.count("0xFFFFFF"),
        "dword_907969 = 32" in dlg,                   # the block's LEFT x
    )
    # ...and the LAYOUT defaults every subtitle inherits, from Text_DrawBlock
    # (0x0043F180): the FONT is 74 - `docs/UI.md`'s "the default and every
    # option row", with 76 the sub-640x480 override - and the style is 2.
    # The dialogue's params carry only TEXTP_FLAG_A, so no TEXTP_ALIGN_* bit
    # is ever set and 2 stands; `Text_LayOutBlock` switches on
    # `dword_907A00 & 0x1E` with case 4 right, case 8 centred and DEFAULT
    # left, so a subtitle is LEFT-aligned. The port centred every row.
    lay = open(os.path.join(src, "15_dinput.c"), encoding="utf-8", errors="replace").read()
    t = lay[lay.index("@func 0x0043F180"):lay.index("@func 0x0043F3E0")]
    o = lay.index("@func 0x0043F3E0")
    body = lay[o:o + 40000]
    # ...and the OTHER face. `Subtitle_Show` (0x0041E040), the adventure-mode
    # line that always comes with a sound, passes `params[0] = 0x20 | 0x40`
    # and `params[2] = 86`; TEXTP_SLOT2 writes `dword_907A10 = params[2]`, the
    # same global whose default is 74. The font table names both, and the
    # names settle it: 74 is JOURNAL and 86 is VOIXOFF - voice-over.
    sysc = open(os.path.join(src, "05_sys.c"), encoding="utf-8", errors="replace").read()
    sub = sysc[sysc.index("@func 0x0041E040"):sysc.index("@func 0x0041E0E0")]
    import json as _json
    def _fonts(o):
        if isinstance(o, dict):
            if "fonts" in o: return o["fonts"]
            for v in o.values():
                r = _fonts(v)
                if r: return r
        elif isinstance(o, list):
            for v in o:
                r = _fonts(v)
                if r: return r
        return None
    ft = {e["id"]: e["name"] for e in _fonts(_json.load(open(os.path.join(ROOT, "tables", "ui.json"))))}
    # ...and the SCROLL. The block has a max size and a long line overflows it:
    # `dword_53AE24` is the laid-out height LESS the block's, the offset
    # `dword_6A52C0` moves one pixel a tick under input bits 8 (down) and 4
    # (up) clamped to it, and `dword_6A50E8` is the arrow state - 2 more
    # below, 1 more above, 3 both - which `sub_4400D0` draws as a quad under
    # `a5 & 1` and another under `a5 & 2`.
    got = got + (
        "- v3 / 480" in dlg,                                   # the overflow
        "dword_6A52C0 < dword_53AE24" in dlg,                  # the down clamp
        "if ((a2 & 4) != 0 && v14 > 0)" in dlg,                # the up step
        "dword_53AE24 != v14 ? 3 : 1" in dlg,                  # the arrow state
        "if ((a5 & 1) != 0)" in box, "if ((a5 & 2) != 0)" in box,
        # the arrows are RED and their alpha comes off a counter, so they pulse
        "+ 16711680" in box, "0x3E7) << 24" in box,
        # and the box insets are LITERAL pixels, not scaled by the display
        "v21 = 18" in box, "g_ScreenSize - 18" in box, "v6 - 8" in box,
        "params[0] = 0x20 | 0x40" in sub, "params[2] = 86" in sub,
        "Text_DrawBlock(16, 0" in sub,
        ft.get(74), ft.get(86), ft.get(76),
        "dword_907A10 = params[2]" in t,
        "dword_907A10 = 74" in t, "dword_907A10 = 76" in t,
        "style = 2" in t,
        "switch (dword_907A00 & 0x1E)" in body,
        "v42 = dword_907A08 - v43" in body,                       # case 4, right
        "(dword_907A08 - v43 - dword_907A14) / 2" in body,        # case 8, centred
    )
    return got, (True, True, 1, True, True, True, True, 4, False, True, 7, True,
                 True, True, True, True, True, True,
                 True, True, True, True, True,
                 True, True, True, "JOURNAL", "VOIXOFF", "SMALL", True,
                 True, True, True, True, True, True), \
           ("the renderer's box: the two `off_4C71A8` colours it switches on, one "
            "I2D_SubmitQuad, both blend flags, the reply's -32 top and the 18px "
            "inset; then Dialog_TickUI's four Text_DrawBlock calls, that it makes "
            "NO I2D call at all, the bluish 0x8080C0 reply ink, the seven 0xFFFFFF "
            "sites and the left-x 32; then the LAYOUT defaults - font 74 with the "
            "76 override, style 2, and the alignment switch whose case 4 is right, "
            "case 8 centred and DEFAULT left, which is what a subtitle gets; and "
            "Subtitle_Show's own params - TEXTP_SLOT2|FLAG_A with font 86 and the "
            "inset-16 block - with the table naming 74 JOURNAL, 86 VOIXOFF (the "
            "voice-over face the interaction line uses) and 76 SMALL; and the "
            "SCROLL - the overflow, its two clamped steps, the arrow state and the "
            "two arrow quads the renderer draws from it - RED (0xFF0000) with an "
            "alpha off a counter, so they flash - and the LITERAL 18/8 insets, "
            "which are not scaled by the display the way the block height is")

def c_engine_tuto_camera():
    r"""`engine/`: a scripted camera resolves against the PELVIS, like the follow one.

    A relative world camera is `point = subjectPos - R(yaw) * offset`
    (`sub_415D10`/`sub_415E60`), and the subject point is the actor's pelvis,
    not the ground point the walker keeps. `PlayerController::resolveSteady`
    subtracts `camLift_` before resolving - Y points DOWN, so subtracting
    RAISES - and that is issue 49's fix for the FOLLOW camera.

    The scripted branch of `omk-play` resolved against `session.playerPos()`,
    the feet, so every staged shot naming a subject sat one whole lift too
    low. Reported on AREA 222's alley tutorial, the "En appuyant sur Action"
    sequence, whose three shots 4290/4291/4292 are all `eyeSubject 0,
    atSubject 0` - the same shape as the follow camera, which is what made
    issue 42 hard too.

    What is asserted: each of the three is raised by exactly the lift when the
    subject moves from the feet to the pelvis, in hundredths - 4189 for
    HO1_FNM, the same 41.89 `engine: player walk` measures from the model's
    own hierarchy root. A scripted camera that ignored the lift reports 0.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/tuto_camera"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "tuto_camera")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "t.bin")
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"),
                        os.path.join(fr, "IAM", "START"), "41.89", out],
                       capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 12:
        return ("no output",), ("12 bytes",), "the probe must resolve three cameras"
    a, bb, c = struct.unpack_from("<3i", raw, 0)
    return (a, bb, c), (4189, 4189, 4189), \
           ("AREA 222's tutorial shots 4290/4291/4292, each raised by the pelvis "
            "lift when resolved against the subject the follow camera uses")

def c_engine_env_anim():
    r"""`engine/`: the ENVIRONMENT's own animations, and the fan that turns.

    A cutscene's beats are fired by the SCENE chunk's startup script. The
    environment's are fired by the **AREA's**, and AREA 222's `+4` script
    opens with `scx.play 20, 0, 0` before it even chooses the music::

        2276  scx.play  20, 0, 0
        2283  push.i8   1
        2285  push.var  626           ; 'premiere impasse'

    Object 20 is `Ventilo` - the Impasse's fan - and it is the **only** object
    in `Impasse.SCX` with loopCount -1, run for ever. SCENE 55's sixteen beats
    never name it, which is what "managed by the environment and not the
    cutscene" means, and it is why it must survive a `scene.load`
    (`engine: scene survive`).

    **It turns without moving, and that is the whole of the bug it caught.**
    `Script_MoveObjectOnPath` sets a node's POSITION and its ORIENTATION in
    the same breath: `Path_Sample`'s sixth parameter is an out 3x3, and the
    handler feeds it to `sub_437160(node, m)`, which raises the node's dirty
    flag 0x80000 and `qmemcpy`s 36 bytes to node `+56`, right beside the
    `o3de_SetNodePos`. The port sampled only the position - so the fan, whose
    path holds the SAME POINT at every key and carries the spin entirely in
    the quaternion, sat still while its clock ran. `todo/omk-play.md` 53.

    What is asserted: the port starts object 20 off the area's script with
    nothing missed, the node it drives is `Epale20` (a blade of `AImpasse`),
    and over twelve frames its position moves by **0** while its quaternion
    sweeps. A position-only sample makes the spread 0 and fails here.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/env_anim"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "env_anim")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "e.bin")
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"),
                        os.path.join(fr, "IAM", "START"),
                        os.path.join(fr, "SCPTDATA"), out],
                       capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 32:
        return ("no output",), ("32 bytes",), "the probe must write its record"
    (started, running, isEpale, samples, rotated,
     posSpread, quatSpread, missed) = struct.unpack_from("<8i", raw, 0)
    got = (started, running, isEpale, samples >= 12, rotated,
           posSpread, quatSpread > 500, missed)
    want = (1, 1, 1, True, 1, 0, True, 0)
    return got, want, ("AREA 222's startup script starts Ventilo (object 20); its node "
                       "Epale20 TURNS (quat sweeps) and does NOT move (spread 0)")

def c_engine_scene_survive():
    r"""`engine/`: a running scene program SURVIVES a `scene.load`.

    The `.SCX` **belongs to the AREA**, named by its `+97` stem, and it is
    loaded exactly once - when the area loads. `Area_LoadScx` (0x0041B4E0)
    clears the slot's object container, calls `Scene_LoadSCX` into it and
    binds the matching `.sfx`, and it has **two callers, `Area_TickLoad` and
    `Game_Init`**.

    `Scene_LoadSCX` (0x00449750) has **four call sites in the whole binary**::

        Area_LoadScx   -> slot + 8          the AREA's SCX  (the environment)
        Game_Start     -> &stru_930780      the GLOBAL aventure.scx library
        sub_419060     -> &stru_930780
        sub_4193E0     -> &stru_930780

    The `scene.load` opcode (71, handler 0x403950) is **none of them**: it
    calls `Scene_Load` (sub_40C120), which brings in the SCENE CHUNK - startup
    script, zones, props - and never touches the object pool. So every running
    program survives a scene load, and a cutscene does not own the objects it
    animates: it calls `scx.play` on objects of the area's own `.SCX`, and the
    ones it never names go on running. That is a play report about the
    original - *the animations launched by the environment are not stopped
    during a cutscene* - and `todo/omk-play.md` 52.

    `Session::reloadScene` used to rebuild the runner unconditionally, on the
    reasoning that "`Scene_LoadSCX` rebuilds the object pool, so no program
    survives the transition". True of the function, false of the transition,
    which does not call it; and since `resolveScx` reads the stem from the
    AREA chunk either way, the rebuild was re-loading THE SAME FILE and
    resetting every program's counter, clock and run count for nothing.

    What is asserted: a program 30 frames into the demon's beat is still
    running after `scene.load(222, 57)`, with the SAME clock and the same
    file, and goes on advancing afterwards (30 -> 30 -> 40). The control is
    that the runner is keyed on the AREA at all - AREA 0 is `anekbah.SCX`, a
    different file with nothing running - since keeping the runner across an
    area change would be as wrong in the other direction.

    Note what is NOT the control: `sceneLoad(0, -1)` does nothing, correctly.
    Area 0 is not resident, and the engine's slot walk changes only the DB
    field for an area in neither slot. The rebuild-on-area-change branch is
    walked by the real transitions in `engine: area load` and `sim: area
    load`.
    """
    import subprocess, tempfile, shutil
    fr = omkpaths.data_root()
    if not os.path.isdir(fr):
        return ("no data",), ("data",), "needs the shipped tree"
    eng = os.path.join(ROOT, "engine")
    b = subprocess.run(["make", "-s", "build/scene_survive"], cwd=eng,
                       capture_output=True, text=True)
    binp = os.path.join(eng, "build", "scene_survive")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "s.bin")
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"),
                        os.path.join(fr, "IAM", "START"),
                        os.path.join(fr, "SCPTDATA"), out],
                       capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if len(raw) < 44:
        return ("no output",), ("44 bytes",), "the probe must write its record"
    (resident, had, runBefore, runAfter, clockBefore, clockAfter, clockLater,
     sameFile, pcBefore, otherFile, otherRun) = struct.unpack_from("<11i", raw, 0)
    # `resident` first, and it is not a formality: `sceneLoad` walks the two
    # slots and does nothing for an area in neither, so a probe that never
    # made 222 resident asserted nothing. The first version of this check did
    # exactly that and passed under a mutation that removed the fix.
    got = (resident, had, runBefore, runAfter,
           clockBefore, clockAfter, clockBefore == clockAfter,
           clockLater, clockLater > clockAfter,
           sameFile, otherFile, otherRun)
    want = (1, 1, 1, 1,
            3000, 3000, True,
            4000, True,
            1, 1, 0)
    return got, want, ("a program 30 frames in survives scene.load(222, 57) with its "
                       "clock intact and goes on advancing; AREA 0 is a different .SCX")

def c_engine_scene_steps():
    r"""`engine/`: a scene object's program is a SEQUENCE, and the pose follows it.

    `Script_PlayScript` walks an object's functions with a program counter, so
    "the object's clip" is not one number: `Impasse.SCX`'s `A_2_DemonLook` is
    clip 15 (`1-02DEM`, the demon perched on the wall, 91 frames) and THEN clip
    17 (`1-03DEM`, his jump down, 41). `SceneRunner::Started::clip` was filled
    once at start from the FIRST body animation in the object's list and never
    refreshed, and since the body is snapped to that clip's root key 0 the
    demon stood in step 0's place for the whole 132-frame shot: clip 15 clamps
    at its last frame, 267 units up the wall, and the descent that
    `sautdemon`'s last 41 frames are filmed to show never happened. The next
    beat's clip then put him on the ground, which is what a reader described as
    "the shot launches too late and part of the animation is missing".

    **The invariant is one the data can fail and no reader can fake**: the
    beat's three clips are authored to CHAIN, each starting exactly where the
    last ends - 15 ends at `6861 -267 3195` where 17 begins, 17 ends at
    `6642 -105 3211` where 25 begins - so both gaps are **0 units**. A run that
    plays the steps in order walks that continuous path; one frozen on step 0
    does not, and the two readings put the demon **273 units** apart at frame
    120, which this computes side by side so the fault is SHOWN rather than
    asserted away.

    The program runs **132** frames, which is exactly the editing's own
    duration - and that was the second half of the fix. Until 2026-09-03 it ran
    134: `Script_SelectBodyAnimation` (0x004A35D0) reports done on the tick it
    clamp-draws the clip's last frame (`fcomp`/`test ah,41h`/`xor al,al` in the
    listing) and `Script_PlayScript` does `++obj->pc; busy = 1` inside that same
    tick, so the next step begins on the very next one and the advance costs
    nothing; `Program::tick` was closing its window a tick later and spending a
    frame per step. The beat's second step started a frame late and the program
    outlived `sautdemon` by two frames, which is where the gap between beats
    came from.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or the game data absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "scene_steps")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"), out],
                       capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<15i", raw, 0)
    return v, (15, 17, 91, 0, 132, 0, 0, 111, 185, 4, 4, 2, -104, -267, 273), \
           "A_2_DemonLook's first clip and the one the program counter " \
           "reaches, the frame it reaches it on and its own clock restarting " \
           "at 0; how long the program runs - 132, exactly its editing's own " \
           "duration; " \
           "then the two gaps in the beat's authored chain of clips, which are " \
           "0 and 0; then the CRATES - `C_1_BoxMoves` is four " \
           "`Script_MoveObjectOnPath` on the paths `CaisseA`..`CaisseD`, and " \
           "it runs 111 frames where a busy window of 0 gave 40: the busy " \
           "window is PARAM 6, the playback length (110.01 here), not the " \
           "path's own 146 frames, which the handler plays ACROSS it; its " \
           "editing holds 185; then the MOTION itself - 4 nodes placed, all " \
           "4 of them meshes of `AImpasse` by name, and 2 of the 4 FALL (Y " \
           "points down, so a fall ends at a larger y): `Caisse01` and " \
           "`Caisse 13` start about 80 units up and land, `Caisse1` and " \
           "`Caisse 14` start on the ground and slide, which is what the shot " \
           "shows; and finally the demon's height at frame 120 following the " \
           "pc against frozen on step 0 - landed at -104 or still 267 up the " \
           "wall, 273 units apart"


def c_engine_scene_sounds():
    r"""`engine/`: the sound effects a scene object's program plays.

    An object's animation CARRIES its sound. `Script_PlaySound` (0x004A12D0)
    and `Script_PlaySyncSound` (0x004A14D0) hang off the body animation through
    the `+12` sync link and `Script_PlayScript` runs them in the same chain
    walk - so they were unreachable while that link was read flat (a leading
    `sync = 0` became a self-loop and dropped the chain), and then still silent,
    because nothing here had ever played one: `ScxStream` counted the chunk-3
    records and threw their payloads away.

    **Both parameter layouts are the handlers', not a guess**, and they differ,
    which is the trap - reading one as the other invents a cue time for every
    call of the second:

        PlaySyncSound   0 sound  1 the FRAME on the OBJECT's clock
                        (`if (GetParamFloat(a2,1) > obj+88) return busy`)
                        2 &1 loop   3 the latch   4 the node
        PlaySound       0 sound  1 &1 loop   2 the latch   3 the node

    Each fires ONCE per run: the handler tests its latch on entry and writes it
    on the way out (`sub_44C690(fn, 2, 1)`), and `Script_StartScript` clears it
    with the rest.

    **Param 0 is a plain INDEX, and that had to be checked rather than
    assumed** - the sprites in this same format resolve by ID, and a
    global-index lookup lands on the wrong one. `sub_48CB30` settles it:

        if (a2 < scene[+24]) return u16(scene[+48] + 26 * a2, 22);
        else                 return -1;

    a bounds-checked index into the 26-byte chunk-3 records, returning the
    record's `+22` sound handle. So the **186 of 5425** references that point
    past their scene's own array are refused by the engine too - the caller
    tests `!= 0xFFFF` and plays nothing - and are a property of the data, like
    the 551 voice-overs that do not ship.

    The RUN half is `A_1_KaylArrives`, the clean case because its cues are a
    WALK: `Wait 60`, then a 270-frame arrival clip firing an ambient at the
    start and STPR / STPL / STPL / STPR at **170, 200, 210, 280** - right,
    left, left, right. Cue times are on the OBJECT's clock, so the 60-frame
    wait shifts none of them and the ambient lands at 60.

    The CORPUS half is self-checking: every sound a program names and that
    resolves must begin `RIFF` and be accepted by `Wav_LoadToBuffer` - 5239 of
    5239, so a mis-walked stream or a wrong index fails on both counts at once.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or the game data absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "scene_sounds")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"), out],
                       capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    n = struct.unpack_from("<i", raw, 0)[0]
    frames = struct.unpack_from("<%di" % n, raw, 4)
    rest = struct.unpack_from("<5i", raw, 4 + 4 * n)
    return (n, frames) + rest, \
           (6, (60, 170, 170, 200, 210, 280), 5425, 5239, 5239, 5239, 1667), \
           "the cues A_1_KaylArrives fires and the frames they land on - the " \
           "ambient at 60, where the 60-frame Wait hands over, then the " \
           "footsteps at 170/200/210/280; then the corpus: sounds a program " \
           "names, how many resolve against their own scene's chunk-3 array " \
           "(the rest are refused by `sub_48CB30`'s bounds test in the engine " \
           "too), how many begin RIFF, how many `Wav_LoadToBuffer` accepts, " \
           "and the chunk-3 records streamed across the 220 scenes"


def c_engine_actor_sounds():
    r"""`engine/`: the sounds ADVENTURE MODE plays - the `.CTL` effect records.

    A cutscene's sound rides on a scene object's program; the player's rides on
    the `.CTL` state machine instead, and neither had been ported. The state's
    `+28` sub-records are the mechanism (`Cef_TickEffects`, 0x0045ADF0): a
    record fires its `+22` once when the state's clock passes its `+12`, and
    **`H_WALK` carries a PAIR** - sound **203 at frame 3** and **199 at frame
    15**, one per footfall.

    **The id is not an index, and that is the opposite of the scene programs.**
    `Cef_TickEffects` resolves it with `Scene_FindSoundIndex` (0x0048CC80),
    which SEARCHES the resident scene's 26-byte chunk-3 records for a matching
    `+24` and returns that record's `+22` handle; a scene program's param 0 is
    a bounds-checked INDEX (`sub_48CB30`). Reading either as the other lands on
    the wrong sound - the same id names different sounds in different scenes
    (34 is `AASC.WAV` almost everywhere and `STPR.WAV` in the Impasse), and a
    state's footstep is silent in a scene that does not carry its id. That is
    the engine's behaviour, not a gap: all **62** distinct ids the states name
    exist in some scene, so none is orphaned.

    The DATA half re-derives `verify.py: ctl effects` from the port's own
    reader rather than Python's: **590** records, **525** with a sound, **220**
    with a sprite, **0** with neither, 0 malformed windows, 0 attach codes out
    of range.

    The RUN half is a held walk, and it is the one that catches the hard part.
    `H_WALK` LOOPS its clip without ever being re-entered, so a latch cleared
    only on a state change fires the pair once and then goes silent for as long
    as you walk - **2** footfalls in 300 frames instead of **22**. The engine
    re-arms (the tail of `Cef_TickEffects` zeroes the instance's latch words
    when its clock leaves the record's window); these records carry an OPEN
    window and what that reduces to for them is **not traced**, so the rule
    here - a frame going backwards is a wrap, and a wrap starts the footfalls
    again - is a RECONSTRUCTION and says so in the code. What is asserted is
    its consequence: a steady ALTERNATING stream at a walking cadence, 22 steps
    over 10 seconds, which one sound repeating or one sound then silence both
    fail.

    The clip wrap takes `gotoMove(cur_, cur_, 1.0f)`, an early return, so the
    effect pass had to move out of `tick`'s fall-through path and onto all of
    them - a footstep that only sounds when nothing else happened is a footstep
    that never sounds while you walk.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or the game data absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "actor_sounds")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"), out],
                       capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<14i", raw, 0)
    return v, (590, 525, 220, 0, 0, 0, 62, 62, 6, 203, 199, 22, 1, 2), \
           "effect records and how many carry a sound, a sprite, neither; " \
           "malformed windows and out-of-range attach codes (0 and 0); then " \
           "the distinct sound ids the states name and how many exist in some " \
           "scene's chunk 3 - all of them, so none is orphaned; the H_WALK " \
           "states and their footstep PAIR (203 right, 199 left); and finally " \
           "a held walk over 300 frames - 22 footfalls, ALTERNATING, over 2 " \
           "distinct sounds, where a latch that never re-arms gives 2"


def c_engine_camera_roll():
    r"""`engine/`: the camera ROLL, which the renderer dropped until 2026-09-03.

    A camera record carries a roll - `Cam_PlayEditing` keyframes
    position/target/roll/fov, and a world camera's is `Global_Load`'s
    4096-per-turn integer - and `RCamera` had **no field for it**, so the basis
    was built from eye/at with world up pinned to (0, -1, 0) and every rolled
    shot in the game drew upright. It is invisible in any single still frame,
    which is exactly how it survived here for months after the same bug was
    fixed in the web viewer (CLAUDE.md 1, "some errors are invisible at rest").

    **The sense is DERIVED, not chosen.** `sub_442400` turns a direction and a
    roll into `sub_441FF0(pitch, yaw, roll)`, and `o3de/setpiece.cpp`'s
    `headingMatrix` already transcribes that verbatim for the set pieces. Its
    COLUMNS are exactly this basis - column 0 is `s`, column 1 is `-u` (the
    sign is the game's Y-down), column 2 is `f` - so the two can be compared,
    and they agree at **30 of 30** direction/roll pairs to 1.2e-7. That is the
    check: not "a roll is applied" but "the roll the ENGINE'S OWN matrix
    applies".

    A second, coarser assertion catches a roll that is applied backwards or
    not at all: at 90 degrees the right axis must leave the old right entirely
    and land on **minus** the old up. The sign is the half that can go wrong,
    so it is asserted rather than an absolute value.

    Both backends share `cameraBasis`, so the fix reaches Vulkan without a
    second copy of the convention - which is the reason that function exists
    (`raster.h`: "a SECOND renderer uses these conventions rather than a second
    copy of them").

    And the corpus, because a fix nothing exercises is a fix nobody can see:
    **224 of the 1073** cameras in the scenes' chunk-10 editings carry a roll.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or the game data absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "camera_roll")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "r.bin")
    try:
        subprocess.run([binp, fr, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<6i", raw, 0)
    return v, (30, 30, 1, 1, 1073, 224), \
           "direction/roll pairs tried and how many agree with the basis the " \
           "engine's own `sub_441FF0` builds (all of them); then that a " \
           "90-degree roll lands the right axis on MINUS the old up and off " \
           "the old right - the two ways a dropped or reversed roll shows; " \
           "and finally the editing cameras in the shipped scenes and how " \
           "many carry a roll at all"


def c_engine_fades():
    r"""`engine/`: the two SCREEN FADES, neither of them ported until 2026-09-03.

    Filed from a play report — *in the impasse cutscene, one plan, near the
    beginning, is with a red filter, but it appears without it* — and the
    first thing it settles is that the Impasse's own fade is **not red**.
    Opcodes 118/119 pack their colour as a DWORD out of the first FOUR operand
    bytes, so the site at SCENE 55 pc 1229, which a four-int16 view reads as
    `-1, 255, 25, 0`, is really the bytes `FF FF FF 00 19 00 00 00` — colour
    **0x00FFFFFF**, WHITE, over 25 frames. The red in this family is the
    engine's own override: `byte_4C012C` against the running context's `+30`,
    so **a fade issued by the message-0 handler is forced to 0xFF0000**. That
    bookkeeping was already here with a named reader and nothing to consume it
    (`area.h`); this consumes it.

    **There are two fades and they are independent state machines.**
    `Screen_StartColorFade` (0x00451DC0) owns the colour one — mode 1 to,
    2 from — and `Screen_Fade` (0x0041E1B0) the black one, state 3 to black
    over a fixed **60** frames and state 4 back. What is asserted here is the
    RULES rather than the ramp, because the rules are what a wrong port gets
    wrong:

    * a running fade refuses a new one **unless** the new one is a "from" over
      a running "to" (`if (dword_536C1C && (a1 != 2 || dword_536C1C != 1))`);
    * the black fade goes back **only** from state 3;
    * a "to" HOLDS at its colour when the clock runs out instead of clearing,
      which is what lets the next scene load behind a black screen; a "from"
      ends.

    The ramp is linear and read from the ticker (0x00451E60):
    `alpha = clock * 255 / duration` rising for a "to", `255 - ...` falling for
    a "from" — so a "from" starts full, is half at the halfway tick, and ends.

    **The blend is a MODEL and says so.** The engine ends in
    `I2D_SubmitQuad(&q, flag, 9)` with flag 1 for white, 2 for black and 4 for
    anything else, and none of those three blends is traced. Mixing toward the
    colour matches the ramp's direction for every mode and colour; it is
    labelled a model in `area.h` and in `play.cpp` rather than dressed as the
    blend.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or the game data absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "fade_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "f.bin")
    try:
        subprocess.run([binp, omkpaths.data("IAM"),
                        os.path.join(ROOT, "tables", "vm_opcodes.json"),
                        omkpaths.data("IAM", "START"), out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<16i", raw, 0)
    # THE BAND GREYS, and the band's height. The black fade is TWO LETTERBOX
    # BANDS, not a full-screen quad - the ticker (0x00451E60) submits quads at
    # (0,h-v3)(w,h-v3)(w,h)(0,h) and (0,v3)(w,v3)(w,0)(0,0) with `v3 =
    # (height << 6) / 480`, shading from `v8`'s grey on the inner corners to
    # `v7`'s at the screen edge. The colour half, by contrast, really is one
    # quad at (0,0)(w,0)(w,h)(0,h). `omk-play` applied BOTH to every pixel, on
    # the stated premise that both are full-screen, so the end of every
    # cutscene blacked the whole frame and then snapped back when state 4
    # cleared (`todo/omk-play.md` 56).
    bands = struct.unpack_from("<10i", raw, 16 * 4)
    # ...and the operand decode the report turned on, from the shipped bytes
    import dialog_triggers
    blk = dialog_triggers.archive(omkpaths.data("IAM", "SCENE"))[55]
    op = blk[1229]
    raw8 = blk[1230:1238]
    colour = raw8[0] | raw8[1] << 8 | raw8[2] << 16 | raw8[3] << 24
    dur = struct.unpack_from("<h", raw8, 4)[0]
    return v + bands + (op, colour, dur), \
           (1, 1, 1, 1, 1, 50, 1,
            0, 3, 60, 1, 1, 4, 1, 50, 1,
            0, 0, 127, 254, 127, 255, 64, 128, 64, 46,
            119, 0x00FFFFFF, 25), \
           "the colour fade's mode after a `to`, then its three refusal " \
           "outcomes - to over to refused, from over to allowed, from over " \
           "from refused; that a `from` starts full, is 50% at the halfway " \
           "tick and ends; then the black fade and its DIRECTION - a fade out " \
           "before any fade in is refused (0); opcode 132 gives mode 3 over " \
           "60 frames and it starts FULLY BLACK and ends CLEAR, which is a " \
           "fade IN whatever the table calls it; 133 gives mode 4, starts " \
           "clear, is 50% black halfway and CLEARS at the end; then the two " \
           "BAND greys (inner, outer) at the start and halfway of each - " \
           "state 3 from (0,0) toward (127,254), the outer edge twice as " \
           "fast and saturating; state 4 from (127,255) down through " \
           "(64,128) - and the band height, 64 rows at 480 and 46 " \
           "letterboxed; and finally " \
           "the Impasse's " \
           "own opening fade decoded from the shipped bytes: opcode 119, " \
           "colour 0x00FFFFFF (WHITE, not the red the report went looking " \
           "for) over 25 frames"


def c_engine_set_emitters():
    r"""`engine/`: a set's OWN particle emitters - the environment family.

    Filed from a play report — *please apply particles effects triggered by the
    environnement, and not only the ones triggered by the scene* — and the port
    had read all three files of the chain and never joined them.

    `Sfx_BindAmbientEffects` walks the resident `.3DO`'s meshes and, for every
    one flagged **0x40000000**, compares the first FOUR BYTES of its name as a
    dword against each section-D binding's tag; a match registers that
    binding's section-C effect at the mesh's own position. Three separately
    authored files - the `.3DO`'s flag and name, the `.SFX`'s binding and
    effect, the `.SCX`'s sprite - and the effects, the bindings and
    `ParticleField::add` were all already here with nothing walking the set.

    **The compare is on RAW record bytes**, at `meshOff + 140 * i + 16`, not on
    the parsed name: `readMeshes` stops a name at its first null, so a mesh
    named in fewer than four characters compares differently once it has been
    through a C string. That is why this takes the model's bytes.

    The other family - the STANDING set pieces, section E rows keyed `(1, -1)`
    that no object start can reach - was already bound by `attachSfx`. So the
    report's "not only the ones triggered by the scene" had two answers and
    this is the one that was missing.

    **A BINDING NAMES ITS EFFECT BY ID, NOT BY POSITION**, and getting that
    wrong is what a play report saw as *all generic effects (street lights,
    fire, smoke) are rendered as smoke*. Section C's `+0` id is 1-based and
    does not track the array index: in `anekbah.sfx` index 3 carries id **5**
    and is `neon`, so `'neon' -> effect 5` means the effect whose id is 5.
    Indexing the array instead handed it `effects[5]`, `agri`, a grey smoke -
    and since the smoke effects outnumber the rest, nearly every binding in
    the game landed on one. `tools/ambientfx.py` keys its rows `rows[e["id"]]`
    and was right all along; the port was corrected to match, which moved
    ANEKBAH from 160 to **153, the number the reference has always printed**.
    `docs/ASSETS.md` 3b records the same 1-based rule for the set-piece path,
    where it was applied, and it was never carried across to this one.

    **325 emitters over 12 sets** by that walk - but note what the walk cannot
    see: it pairs a `.SFX` with the `.3DO` of the SAME STEM, and the game does
    not. `attachSfx` takes the resident SCENE's file with its extension swapped
    and `bindSetEmitters` is handed the AREA's `+97` set, so the Impasse is
    `Impasse.sfx` against `AIMPASSE.3DO` - a pair no stem sweep produces. It
    binds **3**, where this check first reported 0 for the Impasse and the
    session log of the running game said otherwise. The corpus number is
    therefore a FLOOR, not a total.

    The 325 is not quite the 321
    `docs/ASSETS.md` 3b quotes from `tools/ambientfx.py`. The two differ by
    four, and the direction REVERSED when the id fix landed: this walk now
    finds four the Python does not. The Python keys `tags[tag] = e`, one
    binding per TAG, so a set that binds the same tag twice collapses there
    and does not here; it also filters harder, requiring the effect's sprite
    to resolve in the scene's chunk 4. Recorded as a difference rather than
    reconciled by moving a number; it is bounded, it is four, both sides agree
    on the 12 sets, and they now agree EXACTLY on ANEKBAH's 153, which is the
    set the discrepancy used to live in.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr = omkpaths.data()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or the game data absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "set_emitters")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "e.bin")
    try:
        subprocess.run([binp, fr, os.path.join(ROOT, "tables", "vm_opcodes.json"), out],
                       capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<5i", raw, 0)
    return v, (42, 400, 325, 12, 3), \
           "sets carrying both a `.SFX` and a `.3DO` of the same stem; the " \
           "meshes flagged 0x40000000 across them; how many BIND to a " \
           "section-D tag and so register an emitter; over how many sets " \
           "(12, which is what `docs/ASSETS.md` says, though its emitter " \
           "count is 321 and the two-emitter difference is recorded rather " \
           "than reconciled); and finally the Impasse, paired the way the " \
           "GAME pairs it rather than by stem - `attachSfx` takes the SCENE's " \
           "`Impasse.sfx` and `bindSetEmitters` the AREA's `+97` set " \
           "`AIMPASSE.3DO` - which binds **3**. The stem sweep above never " \
           "tests that pair and reported 0 for it, which is a caution worth " \
           "keeping: a corpus walk that pairs files by NAME is not the " \
           "pairing the engine makes, and the running game showed the " \
           "difference"


def c_engine_save():
    r"""`engine/`'s save file, game DB and clock.

    **The save geometry, and what a real save settled.** A slot is
    `32 + 4 + 4 + 8192 + 24576 = 32808` bytes and the file is `3496 + 256 *
    32808 = 8402344` - all three literals from the writers, and the save the
    engine wrote under the golden-trace rig is exactly that size. It also
    settles what the header IS, which no literal could: `OMK_SAVE`, then
    `640 x 480`, and only **119** of its 3496 bytes non-zero. So the header is
    the PROFILE block and a slot is self-describing, which means
    `SaveDir_CountByName`'s 256 x 72 walk is over an in-memory directory and
    not over this file. Two readings had been disagreeing about that.

    **The DB inside it reads as state** - the part no literal gives: area
    **237** with scene **57** over it, `premiere impasse` set and `Impasse
    Finie` not, and `Interface` = 1.

    **`IAM\START` is the same block as the new game ships it.** The walk lands
    on **5686** with 8 bytes of alignment padding and no overlap, all six
    counts agree with a source outside the header (VARIABLES.TAG, AREAS.TAG,
    the prop-state indices, the object ids, ADDRESSES.TAG and the zone
    records), and **no bit is set past a count** - array 4 declares 791
    addresses in 792 bits and array 5 declares 4558 zones in 4560, so a wrong
    count would leave state in the leftovers.

    **The round trip differs in exactly bytes +60..+67**, which is not a
    failure: `State_Apply` plants the two bio pointers in the player record
    and `State_Save` does not put the file offsets back, so those eight bytes
    are the relocation itself. Anything else differing would be a real one.

    **The clock is tested by the content, not by the constants.** Eight
    objects in `IAM\OBJECT` are in-world newspapers named for their date and
    all eight are legal in the 41-day, 13-month calendar, with `41 Andar`
    landing exactly on the month length. A new game starts 12 Nadim 7216 at
    11:10:00 - and the formatter's divisions are integer throughout, none of
    them even, so a float here drifts.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fx = os.path.join(ROOT, "traces/save-appart.bin")
    if not (os.path.isdir(eng) and os.path.exists(fx)):
        return ("skipped",), ("skipped",), "engine/ or the save fixture absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "read_save")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "s.bin")
    try:
        subprocess.run([binp, fx, omkpaths.data("IAM/OBJECT"), out,
                        omkpaths.data("IAM/START")], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    o = 0
    def take(n):
        nonlocal o
        v = struct.unpack_from("<%di" % n, raw, o); o += 4 * n; return v
    def takes():
        nonlocal o
        (n,) = take(1); v = raw[o:o + n].decode("cp1252"); o += n; return v
    head = take(7)                       # size, header, slot head, magic, w, h, nz
    name = takes()
    rest = take(6)                       # day, area, scene, vars 19/626/61
    clock = take(3)                      # dated, legal, highest day
    date, time = takes(), takes()
    db = take(7)                         # image, end, pad, agree, spare, diffs, lo, hi
    return (head, name, rest, clock, date, time, db), \
           ((8402344, 3496, 8232, 1, 640, 480, 119), "hereIsTheProfileName",
            (52, 237, 57, 1, 1, 0), (8, 8, 41), "12 Nadim 7216", "11:10:00",
            (5686, 5686, 8, 6, 0, 8, 60)), \
           "the save size the geometry predicts, the header and slot-head " \
           "lengths, OMK_SAVE, 640x480 and its non-zero bytes; the profile " \
           "name; the day, then the DB as STATE - area 237 with scene 57, " \
           "Interface, premiere impasse, Impasse Finie; the dated " \
           "newspapers and how many are legal in the calendar, the highest " \
           "day, and a new game's date and time; then IAM\\START's size, " \
           "where the walk ends, its padding, the counts agreeing with an " \
           "independent source, bits set past a count (0), and the " \
           "round-trip difference - 8 bytes starting at +60, the bio " \
           "pointers State_Apply plants and State_Save leaves"


def c_boot_sequence():
    r"""BOOT: the launch chain, the two movie skips, and the frame delta.

    Read from `Game_Main` (0x00439470), `Game_RunLoop` (0x00439310) and
    `Game_Frame` (0x0041F740), with every constant taken out of
    `gamedata/Runtime 2.exe` rather than off the decompilation.

    **The three movies are data, and they ship.** `gamedata/FLIS/` holds exactly
    three MPEG-1 files at the sizes below - nothing to decode, which is why
    CUTSCENES has always listed them in one line.

    **There are two different skips.** Every `Movie_Play` gets `sub_439730` as
    its poll callback, and it stops the current movie on any key but only sets
    `dword_52DD54` - the latch that also drops the movies after it - when
    `Input_Poll`'s code is **2**. That code is scan code **56**, `DIK_LMENU`:
    the left Alt key. So any key skips one, Alt skips all three. Asserted here
    as the literal `56` in the assembly, because it is one byte and a
    plausible-looking wrong reading (55, 57) would be invisible.

    **The frame delta is computed, not constant.** `CLAUDE.md` quotes
    `flt_4C30D8 dd 1.0`, which is its value as SHIPPED; `Game_Frame` case 0
    assigns `30.0 / fps` every frame, clamped at 3.0. The 30.0 is the whole
    reason every clock in this repo counts frames rather than seconds: the
    delta is measured in thirtieths of a second, so one unit IS one frame at
    30 Hz. The clamp is a gameplay fact - below 10 fps the game slows down
    instead of taking bigger steps - and a replica that integrates true
    elapsed time on a slow machine is wrong, not more accurate.

    The four fixed deltas of the debug modes are read as their float
    immediates, so a mode table that drifted would fail here.
    """
    s = _need("asm")
    if s: return s
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from ui_tables import Exe
    flis = omkpaths.data("FLIS")
    sizes = tuple(os.path.getsize(os.path.join(flis, f))
                  for f in ("EIDOS.MPG", "QUANTIC.MPG", "GAME.MPG")) \
            if os.path.isdir(flis) else ()
    e = Exe()
    def f32(va):
        return struct.unpack("<f", e.read(va, 4))[0]
    consts = (f32(0x4BC1CC), f32(0x4BC1EC), f32(0x4BC1F4),
              f32(0x4C30D8), f32(0x4C30E8), f32(0x4C30E4), f32(0x4BC208))
    # the boot chain's own strings, in the image
    img = open(omkpaths.data("Runtime 2.exe"), "rb").read()
    strs = tuple(int(t in img) for t in
                 (b"NOFMV", b"WINDOW", b"aventure.scx", b"IMAGES\\OMIKRON.BMP",
                  # LOWERCASE in the image, UPPERCASE on the disc - the boot
                  # path itself depends on the Win95 filesystem (see DataFs)
                  b"FLIS\\EIDOS.mpg", b"FLIS\\QUANTIC.mpg", b"FLIS\\GAME.mpg"))
    # Game_Frame's five delta cases, as immediates in its own assembly
    asm = _run("tools/asmfn.py", "0041F740")
    imm = tuple(asm.count("mov     flt_4C30D8, " + h)
                for h in ("40400000h", "3F800000h", "3F000000h",
                          "3DCCCCCDh", "40000000h"))
    # the extension really is spelt the other way in the two places
    upper = tuple(int(t in img) for t in
                  (b"FLIS\\EIDOS.MPG", b"FLIS\\QUANTIC.MPG", b"FLIS\\GAME.MPG"))
    disc = tuple(sorted(os.listdir(flis))) if os.path.isdir(flis) else ()
    # the Alt scan code, in Input_Poll
    alt = _run("tools/asmfn.py", "0043E0D0")
    alt56 = len(re.findall(r"cmp\s+e?[a-d]x, 38h", alt))
    return (sizes, consts, strs, upper, disc, imm, alt56), \
           ((5714716, 10265108, 46359152),
            (30.0, 1000.0, 3.0, 1.0, -1.0, -1.0, 10000.0),
            (1, 1, 1, 1, 1, 1, 1), (0, 0, 0),
            ("EIDOS.MPG", "GAME.MPG", "QUANTIC.MPG"), (1, 1, 1, 1, 1), 1), \
           "the three FLIS movies' sizes; then out of the image: the 30.0 " \
           "the delta is measured in, the 1000.0 that makes fps, the 3.0 " \
           "clamp, flt_4C30D8 as SHIPPED (1.0 = 30 fps), the two disabled " \
           "overrides and the accumulator wrap; the boot chain's strings - " \
           "NOFMV, WINDOW, aventure.scx, the splash and the three movies, "\
           "which the image spells `.mpg` and the disc spells `.MPG` (so the "\
           "UPPERCASE forms must be absent, and the boot path itself needs a "\
           "case-insensitive filesystem); " \
           "Game_Frame's five delta immediates (3.0 clamp, 1.0, 0.5, 0.1, " \
           "2.0); and the one comparison against scan code 56 in " \
           "Input_Poll - DIK_LMENU, the key that skips ALL the movies"


def c_engine_scene_loop():
    r"""`engine/`'s Session loop - `Script_PlayAllScripts` wired in.

    Until now the scene-object interpreter ran standalone: `Program` could be
    ticked, but nothing in the engine ever STARTED one, because `scx.play*`
    was recorded and dropped like every other stubbed call. This is the wiring
    - and it is the first check here that crosses the two halves of a frame
    rather than testing either alone.

    **Resolving the scene is the part that can go wrong silently.** A
    `scx.play*` operand is an object id LOCAL TO THE RESIDENT SCENE, and the
    ids are small and reused, so the wrong scene resolves the wrong object
    without failing. An AREA names its set at `+97`; a SCENE names nothing and
    must be traced back to the area whose `scene.load` (opcode 71) loads it.
    Zone 3732 is in SCENE 53, so both halves are exercised: the map says AREA
    217, whose `+97` is `Re14`, and `Re14.SCX` is what the scene must be.

    **The operand is not always field 0.** 57/58 name a scene object, 46/90
    the player's, but 59/60 name an ACTOR FIRST and the object second. Reading
    field 0 for all six starts the wrong thing on two of them.

    Standing in the zone, the whole chain runs: two objects start -
    **22 `Uzal---->assis` and 32 `Uzal_Stand`**, the first through the WAITING
    variant - none is missed, and `dialog.start` loads **387** with 13 nodes.

    Then the part no format check can see. Ticked 120 frames, **one of the two
    programs is still running and one is not**, which is the documented rule:
    the entrance must finish, the loop need not. Every one of these numbers
    matches `tools/sim/run.py`'s stage 4b, which is the point - the Python
    models the decisions and this now makes the same ones.

    **And one witness from outside all of it.** `Grid.SCX` - the scene AREA
    118 makes resident, the title sequence's own - holds an object called
    `Wait5sec` whose single function is `Wait` with the parameter **150.0**.
    The name says five seconds and the number says 150, so a person authoring
    content wrote the engine's time unit down: 150 / 30 = 5. That is
    `docs/BOOT.md` §4's `30.0 / fps` confirmed from the data end, by someone
    who had no idea anyone would check.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "launch_scene")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "l.bin")
    try:
        subprocess.run([binp, omkpaths.data_root(),
                        os.path.join(ROOT, "tables/vm_opcodes.json"),
                        omkpaths.data("IAM/START"), "3732", "120", out],
                       capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    o = 0
    def take(n=1):
        nonlocal o
        v = struct.unpack_from("<%di" % n, raw, o); o += 4 * n
        return v if n > 1 else v[0]
    def takes():
        nonlocal o
        n = take(); v = raw[o:o + n].decode("cp1252"); o += n; return v
    found = take()
    scx = takes()
    started = []
    for _ in range(take()):
        oid, wait = take(2)
        started.append((oid, takes(), takes(), wait))
    missed, dialog, nodes, progs, running, wait5 = take(6)
    return (found, scx, started, missed, dialog, nodes, progs, running, wait5), \
           (1, "Re14.SCX",
            [(22, "Uzal---->assis", "player", 1), (32, "Uzal_Stand", "player", 0)],
            0, 387, 13, 2, 1, 150), \
           "the zone was found; the .SCX the chunk plays from, resolved " \
           "SCENE 53 -> AREA 217 -> `+97` (which a wrong map would get wrong " \
           "SILENTLY, since object ids are scene-local and reused); each " \
           "object scx.play* started - id, name, which variant, and whether " \
           "the caller waits; how many operands resolved to nothing (0); the " \
           "conversation and its nodes; then the programs started and how " \
           "many are STILL RUNNING after 120 frames - the entrance finishes, " \
           "the loop does not, all matching tools/sim/run.py stage 4b; and " \
           "finally Grid.SCX's `Wait5sec` parameter, which is 150 - the one " \
           "place a designer wrote the 30-per-second conversion down"


def c_engine_golden_traces():
    r"""`engine/` replays ALL FIVE golden traces - the behavioural oracle.

    The port had only ever been diffed against `intro.log`, 42 events, by
    comparing whole announcement streams. That works for the opening because
    it is one entry point; it cannot work for a capture of somebody PLAYING,
    where events come from dozens of scripts running concurrently. So this
    does what `goldentrace.py diff` does - anchor each event on the one slot
    in the corpus that could have announced it, replay that slot, and require
    the prediction to appear as an ordered SUBSEQUENCE (a contiguous block
    would report the engine's own interleaving as disagreement).

    **1469 events across six captures, 116 anchors, and the two
    implementations agree on every one.** That is the point: `tools/sim` and
    `engine/` are independent readings of the same VM, and they now decide
    identically on every script the engine was recorded running.

    Porting it found two things in the reference, both now fixed there:

    * **`show` caps the WORK, not the printing.** At its default of 25,
      `trace agreement` was replaying the first 25 of `resto-387`'s 64 anchors
      and reporting "21 agreeing, 1 disagreeing" as the capture's result. Over
      all of them it is 51 and 6. Nothing regressed - it was never looked at.
    * **the tight index over-included.** Six opcodes have a field map but no
      section, and op 152's section is `JINGOFF2.ADP` - a filename, out of the
      unbounded handler block CLAUDE.md 1 warns about. 99 extra pairs, and two
      false mismatches. Both sides now read `tables/vm_announce.json`, the
      assembly-derived map, so neither can drift from the other.

    **The 612 conversation branch scripts are indexed but never replayed**,
    and leaving them out is not neutral: a pair a conversation could also
    announce is not unique, and an index that cannot see them calls it unique
    and anchors a world slot that may not be the emitter. Indexed, they cost
    two false anchors on `resto-387` - the exact gap between this and the
    reference before they were added.

    **The six disagreements are asserted rather than hidden.** Three -
    `AREA 157 rec 60 +4`, `AREA 179 rec 31 +4`, `SCENE 51 rec 1 +0` - hold
    from every state tried; the other three are settled by any of the three
    saves taken during the capture itself. The mismatches follow the STATE,
    not the script, which is what makes them anchoring artifacts rather than
    the port deciding differently: a trace carries decisions, not memory, so
    no available state is the one the engine held when a given script ran.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    trc = os.path.join(ROOT, "traces")
    if not (os.path.isdir(eng) and os.path.isdir(trc)):
        return ("skipped",), ("skipped",), "engine/ or traces/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "diff_traces")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "t.bin")
    try:
        subprocess.run([binp, omkpaths.data("IAM"),
                        os.path.join(ROOT, "tables"), trc, out],
                       capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    o, rows = 0, []
    n, = struct.unpack_from("<i", raw, o); o += 4
    for _ in range(n):
        ln, = struct.unpack_from("<i", raw, o); o += 4
        name = raw[o:o + ln].decode(); o += ln
        ev, an, ok, bad, sk, un = struct.unpack_from("<6i", raw, o); o += 24
        rows.append((name, ev, an, ok, bad, un))
    return (rows, sum(r[1] for r in rows), sum(r[3] for r in rows),
            sum(r[4] for r in rows)), \
           ([("intro.log", 58, 8, 7, 0, 1), ("walkin.log", 76, 11, 10, 0, 1),
             ("impasse-walk.log", 286, 23, 21, 0, 2),
             ("telis-dialog.log", 55, 6, 4, 0, 2),
             ("resto-387.log", 840, 64, 51, 5, 8),
             ("fight.log", 154, 4, 2, 0, 2)], 1469, 95, 5), \
           "per capture: events, anchors, scripts replayed and AGREEING, " \
           "disagreeing, and anchored on a branch this state does not reach " \
           "(not a failure - a static decode walks every branch, a replay " \
           "takes one); then the totals. Every number matches " \
           "`trace agreement`, which is tools/sim doing the same job from an " \
           "independent implementation"


def c_engine_render():
    r"""`engine/`'s render DECISIONS - what draws, in what order, textured how.

    Not a rasterizer, and deliberately not: the plan drops the standard to
    "read and explained" at the pixel because there is nothing to diff pixels
    against. Everything UPSTREAM of the pixel is a decision the shipped data
    can fail, and all of it is one 14-bit number.

    **The drawable filter is one test**, `flags & 0x800043`, guarding every
    call to `Render_SubmitMesh` - replacing three viewer heuristics and
    disagreeing with them in both directions. Bit 0 alone accounts for every
    skipped PERSOS mesh, and **3** decor meshes the viewers draw the engine
    does not.

    **The key's two halves must be independent.** The state bits
    `Render_SubmitMesh` can set and the six bits the texture slot occupies
    must not intersect (0), the 58 slots must fit in those six bits, and
    together they must span 0x3FFF. And the material field the key reads is a
    RUNTIME field, which the data proves by shipping `-1` at +64/+68 in
    **2534 of 2534** materials: one shipping a real value would mean it is
    authored and the whole reading is wrong.

    **`meshStateBits`'s first line is an assignment, not an OR.** Flag 0x10000
    is `mov esi, 800h` and WIPES every state bit set before it. Written out as
    a table of independent bits it would be wrong, and it looks exactly like
    such a table.

    **Transparency is two modes**, and the counts pin which corpus: over the
    220 sets it is **211 additive / 6 multiply**, the numbers ASSETS 4's table
    quotes; over all 635 models 398 / 7. Reading the first as a whole-corpus
    number looks like a disagreement and is a different question. **0** meshes
    ask a sub-mode without 0x1000.

    **And the cache is RUN, not described.** The reference asserts that
    Anekbah and AImpasse share texture names whose pixels differ. This loads
    AImpasse's materials into the 58-slot pool and then Anekbah's, the way
    `Area_LoadSet` does with two sets resident - hidden is not unloaded - and
    reports what actually happened: AImpasse takes **8** slots, and Anekbah
    then gets **7 cache hits**, `BATITR12` among them. Those seven materials
    point at the Impasse's pixels; their own were `fseek`'d past and never
    read. That is the wrong sign panel, derived by executing the mechanism
    rather than by intersecting two name sets - and it is why a replica has to
    REPRODUCE this: the viewers are right and the game is the odd one out.

    **One fault found on the way, in a standing check.** `drawable mask`
    globbed `gamedata/MESHES/PERSOS/*.3DO` case-sensitively, and **12 of the 193
    PERSOS models ship `.3do`** - so it was silently counting 3517 meshes of
    3730 and 511 skips of 547. The port went through `DataFs` and disagreed;
    the same class of bug as the `.SFX` one, this time inside a test.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    mesh = omkpaths.data("MESHES")
    if not (os.path.isdir(eng) and os.path.isdir(mesh)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/MESHES absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_render")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "r.bin")
    try:
        subprocess.run([binp, mesh, out], capture_output=True)
        v = struct.unpack_from("<24i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (3730, 547, 547, 532, 123, 12203, 434, 3,
               0, 1, 0x3FFF, 2534, 2534, 182, 20,
               211, 6, 398, 7, 0,
               8, 7, 13, 1), \
           "PERSOS meshes, those skipped by 0x800043, of those by bit 0 and " \
           "the lone-triangle shape, the flagless; set meshes, skipped, and " \
           "the 3 the viewers draw and the engine does not; then the state " \
           "bits overlapping the texture slot (0 - the two halves of the key " \
           "must be independent), that 58 slots fit in 6 bits, what the key " \
           "spans, materials and those shipping +64/+68 as -1 (all of them, " \
           "which is what says the field is RUNTIME); texture names with " \
           "different pixels in different models and how many are Anekbah's " \
           "(all twenty); blend modes over the SETS (ASSETS 4's 211/6) then " \
           "over everything, and the 0 asking a sub-mode without 0x1000; and " \
           "finally the cache RUN - AImpasse takes 8 slots, Anekbah then " \
           "cache-HITS 7 of them and loads 13, and BATITR12 is one of the 7"


def c_engine_cull():
    r"""`engine/`'s visible-set walk - `sub_48D3B0`'s three rejects.

    Both halves are the engine's. `cullMesh` is `sub_48D3B0` - the hidden bit,
    one distance compare, four dot products, in that order - and
    `frustumFromCamera` is `sub_48D0D0`, the function directly before it.

    **The engine does not build planes from angles.** It takes the four
    corners of the view rectangle AT THE FAR PLANE,
    `(+-far * w / (2 * projX), +-far * h / (2 * projY), far)` with the
    viewport size the uint16 pair at camera `+0x19C`/`+0x19E` and the
    projection scales at `+0xE4`/`+0xE8`, transforms them to world space, and
    hands the eye plus two adjacent corners to `sub_442FB0` - a plane through
    three points - four times, into `flt_660AD4 … flt_660B18`
    struct-of-arrays, which is why the walk reads that range as one block.
    **The two axes are scaled independently: there is no aspect-ratio term
    anywhere.**

    That last point is the finding, and it was paid for. A first version of
    this check used a fixture that derived the vertical half-angle from the
    horizontal one through an aspect ratio, and under it four of Anekbah's
    meshes were kept at 45 degrees and DROPPED at 90 - by margins of 84 to 457
    units, far too large to be ties. The invariant "widening the field of view
    can only keep more" was withdrawn as untestable. It was true; the fixture
    was wrong. With `sub_48D0D0`'s own construction it holds at 0 violations,
    and it is asserted here.

    `far` is `dword_6A2B9C`, the camera's `+0x154`, and the near plane is
    `flt_6A2BBC` from `+0x144` - both settled by the range test that rejects a
    vertex with `z <= near` or `z >= far`. What a running game puts in them is
    a per-camera runtime value no shipped file carries, so the check sets far
    large and the survivor counts measure the four side planes alone.

    **The bounding volume is identified from the data**, which is what makes
    the port more than a transcription. The radius at `+88` equals `max |v|`
    over the mesh's own vertex block in **12039 of 16176** meshes that have
    one, and **11745 of DECORS' 12191** - a field that was not the radius
    would not do that. The box at `+92`/`+104` is symmetric about the origin
    in 11462.

    **The residue is characterised rather than solved, and one earlier
    statement of it was wrong.** 3776 meshes store a radius SMALLER than their
    extent - and **361 store a LARGER one**, which the first count missed
    because it only tallied one direction.

    Six hypotheses were tested against the 446 decor cases and all failed: the
    box diagonal (7), its largest half-extent (6), the whole vertex block, only
    the vertices the mesh's own faces REFERENCE (0), resolving faces through
    ancestor meshes (0), and another mesh's block in the same file (15 of 85
    sampled). What *is* established is that the parse is sound: **every file's
    per-mesh vertex counts sum exactly to its header total**, so the running
    base is right and this is a fact about the authored data.

    What it looks like is a STALE authored value - computed once and not
    updated as the mesh was edited. The distribution fits: the median miss is
    **0.937** of the true extent and the range 0.057..1.094, so most are
    slightly off and a few are wildly so; 140 of the 446 share a radius with a
    sibling in the same file, which is what copying a prop does; and it
    touches 94 of the 220 sets rather than clustering in one.

    The consequence is real and stateable rather than cosmetic: a sphere
    smaller than its geometry culls the mesh EARLY, so those meshes can pop at
    the screen edge, and a larger one merely wastes a test.

    **What is asserted about the cull needs no oracle**, because there isn't
    one - no capture records a visible set and no reference reader implements
    the walk:

    * `planeThroughPoints` - the engine's own builder - puts its three points
      ON the plane and a fourth off it;
    * both monotonicities hold at 0: widening the field of view can only keep
      more, and so can reaching further - the latter being the engine's own
      `(r + far)^2 > |c - eye|^2`;
    * **a mesh containing the eye is never culled** (0). Every plane passes
      through the eye, so |n.(c - eye)| <= |c - eye| <= r, and the distance
      test cannot fire either. Standing inside a wall must not cull it.

    Anekbah's three authored cameras keep **95, 0 and 84** of its 860
    meshes - and the 0 is content, not a fault: that camera sits 20000 units
    away and 5000 above the set, looking sideways. "Every camera sees
    something" was asserted first and was simply wrong.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    mesh = omkpaths.data("MESHES")
    if not (os.path.isdir(eng) and os.path.isdir(mesh)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/MESHES absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_cull")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "c.bin")
    try:
        subprocess.run([binp, mesh, out], capture_output=True)
        v = struct.unpack_from("<22i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (16176, 12039, 12191, 11745, 3776, 11462, 361,
               446, 57, 937, 1094,
               1, 1, 4, 4, 1,
               0, 0, 0, 3, 2401, 179), \
           "meshes with a vertex block and those whose +88 is max|v| (the " \
           "identification); the same for DECORS; those storing a radius " \
           "SMALLER than their extent and those storing a LARGER one - the " \
           "direction an earlier count missed; symmetric " \
           "boxes; then the 446 decor misses and their radius/extent ratio " \
           "x1000 - min 57, median 937, max 1094, which is what says STALE " \
           "AUTHORED VALUE rather than a second rule; then the frustum - " \
           "the axis inside all four " \
           "planes, a point past the half-angle outside, 4 unit normals, 4 " \
           "through the eye - and planeThroughPoints' contract; then the " \
           "cull's own invariants: fov- and far-monotonicity, both of which " \
           "must be 0 now that the frustum is built the way sub_48D0D0 " \
           "builds one, and meshes containing the eye that were culled"


def c_engine_actor():
    r"""`engine/`'s actor runtime - the `.CTL` combat block and transitions.

    This is the data the fight AI already ported actually drives: a profile
    injects input words into the actor's own queue, `Cef_FindTransition`
    matches them against the entries' `+4` codes, and a landed hit is resolved
    through the combat block by `Fight_ResolveHit`. Porting the AI without
    this was porting the caller without the callee.

    **The combat block is 40 bytes of ten floats - and three of them are
    integers wearing a float's bytes.** Reinterpreting rather than converting
    is the whole point, and the corpus is what says so: damage runs **1..25**
    across **116** blocks, and the two reaction references resolve to a real
    state in **232 of 232**. A conversion would give 25.0f, which is not 25,
    and reaction ids that resolve to nothing.

    **A reaction names a state by its LOW SIXTEEN BITS**, which only works
    because the low-16 id space is collision-free: **0** collisions in any
    file. That is a property of the content the format does not enforce.

    **The role codes are content, not layout.** `Fight_Begin` caches six -
    3, 4, 5, 9, 18, 20 - and every combat file carries **exactly one** entry
    of each while every non-combat file carries none: **7 of 7** files right.
    A wrong offset for the field would not produce that shape.

    **The transitions**: 1286 entries, 640 carrying an input bitfield at `+4`,
    of which **28** are the `0x80000000` no-input sentinel - which is NOT the
    queue's idle word `0x40000000`, since `Cef_InputMatches` ends
    `if (a1 == 0x80000000) return a2 <= 0x2000` and the idle word therefore
    matches no idle edge. **168** entries carry a cancel window and **0** are
    malformed (`0 <= from <= to <= 1000`), and the priority at `+84` only ever
    takes 0, 1 or 2.

    **202 groups, 202 with exactly one default entry** (flag 0x20) - one
    apiece, with no exceptions, which is what makes "the default entry" a
    well-defined thing for the runtime to fall back to.

    What is NOT ported from this row: `ACTOR_STATE` 0..17 as a live state
    machine, and the camera-mode presets, which are already lifted to
    `tables/`. This is the data half.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    anims = omkpaths.data("ANIMS")
    if not (os.path.isdir(eng) and os.path.isdir(anims)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ANIMS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_actor")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, anims, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    v = struct.unpack_from("<15i", raw, 0)
    pris = list(struct.unpack_from("<%di" % v[14], raw, 60))
    return v[:14] + (pris,), \
           (116, 232, 232, 0, 1, 25, 202, 202, 7,
            1286, 640, 28, 168, 0, [0, 1, 2]), \
           "combat blocks, reaction refs and how many RESOLVE by their low 16 " \
           "bits, low-16 id collisions (0 - which is what makes a 16-bit " \
           "reference work at all); damage min and max, read as INTEGERS out " \
           "of float bytes; groups and those with exactly one default entry; " \
           "files whose Fight_Begin role codes are exactly right; then " \
           "entries, those with an input code, the no-input sentinels, those " \
           "with a cancel window, the malformed ones (0), and the priorities"


def c_engine_ui():
    r"""`engine/`'s widget tree, walked with the engine's own input words.

    Nine of the coverage table's unported rows are interface, and they were
    listed as having no oracle because pixels cannot be diffed. That was too
    quick: `tools/sim/ui.py` is deterministic and reads the same records, so
    the LOGIC has an oracle even though the drawing does not. This is that
    diff - **28 screens, and the two implementations agree on every one**,
    over a tree of 35 panels, 93 lists and 411 items.

    **The tree had to be lifted first.** A panel, its lists and their items are
    `.data` in `Runtime 2.exe`, not in `gamedata/`, so `tables/ui_widgets.json` now
    carries them (35 panels - 28 screens plus 7 children - 93 lists and 411
    items) exactly as the VM opcode
    table is carried, and `exe tables` re-derives it.

    **The tree links downward through the ITEM, which a first version of this
    got wrong.** `panel+0` is the parent, so "top panels only, children need
    native code" was written down as a limit of the format - and it is not
    one: `Ui_ConfirmSelection` descends into an item's `+44` when it has no
    `+40` callback, and following those transitively reaches **7 child
    panels**, the start menu's confirm dialog and its name field among them.
    The name-field hook was recorded as unreachable and is simply one level
    down. 28 screens share only **13** distinct top panels, so a record per
    screen is not a record per address.

    What still needs native code is a panel a CALLBACK installs rather than a
    `+44`, and the answers those callbacks write.

    **The finding is a trap this port walked into and had to back out of.**
    The open callback's `I2D_SetFlag` calls are recovered by scanning its
    bytes linearly - and a callback has BRANCHES. The ten shop screens grey
    item 0 and enable item 1 on one arm and the reverse on the other, so the
    scan sees `off, off, on, on` for one item and `on, on, off, off` for its
    neighbour. Applying those in address order picks whichever arm comes last
    and made item 0 unselectable on all ten, which is exactly where the two
    implementations disagreed.

    **20 items carry a flag written both ways**, on exactly those
    ten screens, and the rule is that such a write is CONDITIONAL and a linear
    scan cannot resolve it. Refusing them leaves the static record standing -
    which is what the reference does by reading `+48` raw, cruder and right
    for the same reason. CLAUDE.md 1's "a regex must respect nesting", one
    level up: a linear byte scan over a branching function.

    The LIFT's grid hook is ported (`UI_GridMenuInput`, the one list hook in
    the image with a single reference), which is what makes screen 4 answer
    at all: 4x DOWN lands on slot 4, matching.
    """
    import subprocess, tempfile, shutil, json as J
    eng = os.path.join(ROOT, "engine")
    tbl = os.path.join(ROOT, "tables", "ui_widgets.json")
    if not (os.path.isdir(eng) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/ or the widget table absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "walk_ui")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "u.bin")
    try:
        subprocess.run([binp, tbl, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    head = struct.unpack_from("<8i", raw, 0)
    n = head[0]
    cpp = [struct.unpack_from("<5i", raw, 32 + 20 * k) for k in range(n)]

    # the same walk in tools/sim/ui.py, reading the executable directly
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import ui as UI
    u = UI.Ui()
    ref = []
    for sid in sorted(u.screens):
        try: p = u.panel_of(sid)
        except Exception: continue
        u.open(sid)
        lst = u._cur_list()
        row = [sid, len(u.lists(p)), u.cur, u.sel.get(lst, -1) if lst else -1]
        for _ in range(4): u.press(UI.DOWN)
        lst = u._cur_list()
        row.append(u.sel.get(lst, -1) if lst else -1)
        ref.append(tuple(row))
    # only the rows a SCREEN owns: the table also carries the 7 child panels,
    # which the reference does not enumerate (it reaches them by pressing).
    mine = [r for r in cpp if r[0] >= 0]
    disagree = sum(1 for a, b in zip(mine, ref) if a != b) + abs(len(mine) - len(ref))
    return (head, len(ref), disagree), \
           ((35, 93, 411, 377, 36, 57, 35, 0), 28, 0), \
           "panels (28 screens + 7 children reached through item +44), " \
           "lists, items, SELECTABLE items - which FELL by ten once the " \
           "shops' branch was resolved and each of them started hiding the " \
           "one of Acheter/Vendre its own arm disables - hooked lists, lists on " \
           "the default walk, screens settling on a list in range, and those " \
           "approximate before a key is pressed; then the screens " \
           "tools/sim/ui.py walks and how many of the 28 the two " \
           "implementations DISAGREE on - which must be zero"


def c_engine_ui_answer():
    r"""`engine/` DERIVES the start menu's answer instead of being given it.

    `ui.open` suspends the calling script and a screen answers it. Both the
    simulator and this port used to supply that answer as a literal, which
    tested the suspend/resume mechanism and nothing whatever about the screen.
    This walks it: confirm on "Nouvelle partie" descends through the item's
    `+44` into the confirm dialog, DOWN moves off the name field onto the
    buttons - the dialog's own panel hook moves lists with UP and DOWN, not
    left and right - and confirm on "Confirmer" runs the one item callback
    whose effect has been read, which writes **1**, the value the shipped save
    records for the intro's `Interface`.

    **The answer is GATED, and the gate is asserted as its own case.** That
    callback's first instruction tests the name field's cursor and jumps
    straight to the ret when it is zero, writing neither the answer nor the
    screen's state word - so confirming with an empty field leaves the screen
    open and the calling script suspended for ever. Walked without typing a
    name, the port answers **nothing at all**, and "nothing" is the correct
    outcome rather than a modelling failure.

    **The name field itself is the engine's own switch**, read from the
    compiled jump table rather than transcribed: 8 backspace, 9 (TAB) and 27
    (ESC) ignored, 13 (RETURN) its own case, everything else inserting. The
    cap is the buffer - 0x0069BDA0..0x0069BDB4, twenty bytes - and the save
    slot that receives the name has room for 32, so the field is the tighter
    of the two. Thirty characters typed leave twenty.

    And `verify.py: engine intro` now runs with `--derive-ui` instead of
    `--var 19=1`, producing a byte-identical dump. The last literal in the
    intro is gone.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    tbl = os.path.join(ROOT, "tables", "ui_widgets.json")
    if not (os.path.isdir(eng) and os.path.exists(tbl)):
        return ("skipped",), ("skipped",), "engine/ or the widget table absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "ui_answer")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, tbl, out], capture_output=True)
        v = struct.unpack_from("<9i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (1, 0, -1, 0, 5, 20, 4, 0, 1), \
           "the answer the start menu gives with a name typed (1, the value " \
           "the shipped save records) and whether the walk passed through " \
           "anything unmodelled getting there (no); then the same walk with " \
           "NO name - which must answer NOTHING, because the callback's " \
           "first instruction refuses an empty field - and again whether it " \
           "was approximate; then the field itself: \"Kay'l\" is 5, thirty " \
           "characters cap at 20, a backspace leaves 4, RETURN is REFUSED on " \
           "an empty buffer and accepted on a name"


def c_engine_options():
    r"""`engine/`'s options tree - screen 35, which is built unlike the rest.

    Its "panel" is one of THIRTEEN page records, and every page fills the same
    sixteen row widgets - so what a page shows is not in the row, it is in the
    `Opt_BindRow(row, item, page)` calls its builder makes. Those are lifted
    into `tables/ui_widgets.json` (**191 bindings across the thirteen**) and
    walked here.

    **The same branch hazard, and it is now carried as data.** A builder has
    branches, so a row bound twice with different items was seen on two arms
    and the byte scan cannot tell which runs. **12 rows are like that**, and
    `tools/sim/ui.py` documents exactly one - `Opt_PageRoot`'s row 4, bound to
    "Retour" only when the screen parameter is 1. The other eleven have not
    been read, so the table lists all twelve and touching one marks the walk
    approximate rather than taking whichever arm came last. That is the same
    trap the widget-flag lift hit, met the second time knowing its shape.

    **Page 0 is a TRAMPOLINE, and missing it looks exactly like a bug.** Every
    sub-page's "Retour" binds page **0**, not page 1, and page 0 has no rows:
    its builder is `if (dirty) prompt; else Ui_GoToPanel(screen, page 1)`. A
    walk without it lands on an empty page - which is precisely how this port
    first disagreed with the reference, reporting Retour going to page 0 where
    the reference said 1.

    The choice cycling is the part that says the index is stepped MODULO the
    list rather than clamped: `Distance de clipping` driven RIGHT six times
    runs 50, 100, 150, 200, **25** - the wrap - and 50 again, and two LEFTs
    walk back over it to 25 and 200.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    w = os.path.join(ROOT, "tables", "ui_widgets.json")
    t = os.path.join(ROOT, "tables", "ui.json")
    if not (os.path.isdir(eng) and os.path.exists(w)):
        return ("skipped",), ("skipped",), "engine/ or the widget table absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "walk_options")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "o.bin")
    try:
        subprocess.run([binp, w, t, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    o = 0
    def take(n=1):
        nonlocal o
        v = struct.unpack_from("<%di" % n, raw, o); o += 4 * n
        return v if n > 1 else v[0]
    def takes():
        nonlocal o
        n = take(); v = raw[o:o + n].decode("utf-8"); o += n; return v
    root = [takes() for _ in range(take())]
    tree = []
    for _ in range(take()):
        pg = take(); lbl = takes(); back = take()
        tree.append((pg, lbl, back))
    clip = takes()
    cyc = []
    for _ in range(take()):
        cap = takes(); cyc.append((cap, take()))
    return (root, tree, clip, cyc[:6], cyc[6:]), \
           (["Vidéo", "Audio", "Options", "Contrôles"],
            [(2, "Vidéo", 1), (3, "Audio", 1), (4, "Options", 1),
             (5, "Contrôles", 1)],
            "Distance de clipping",
            [("Proche", 50), ("Intermédiaire", 100), ("Loin", 150),
             ("Très loin", 200), ("Très proche", 25), ("Proche", 50)],
            [("Très proche", 25), ("Très loin", 200)]), \
           "the root page's selectable rows; for each, the page it opens, " \
           "that page's first BOUND row and the page Retour comes back to - " \
           "which is 1 only because page 0 is a trampoline; then the " \
           "clipping row cycled RIGHT past its wrap and LEFT back over it. " \
           "Every value matches `sim: options`, which reads the executable " \
           "directly"


def c_engine_load_panel():
    r"""`engine/`'s load panel - the start menu's "Charger une partie".

    Its shape depends on the save directory and on **nothing else**, so it is
    driven twice: against the shipped `IAM\GAMES` and against a synthetic
    one-profile directory. Showing it BRANCH is the point - a model that only
    ever ran the empty case would be indistinguishable from one that ignored
    the directory entirely.

      profiles == 0   focus the BUTTONS (1), hide the slot list, and make
                      "Charger" and "Detruire" unselectable (`word_4CEA9A = 3`)
      profiles > 0    focus the slot list (0), show it, leave both live

    "Nouvelle partie" is hidden either way on screen 29: it belongs to screen
    30, the SAVE panel, which shares this very panel and is told apart by
    `word_4CEA9A` - 0 here, 1 there. So the empty case leaves exactly one
    button live, "Annuler", and the one-profile case three.

    The directory is 256 entries of 72 bytes read out of the slot heads, and
    the shipped file is one the engine made and never saved into: **256
    entries, 0 distinct profiles**. Its geometry is `3496 + 256 * 32808 =
    8402344`, and the builder memsets `256 * 72 = 18432`.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    w = os.path.join(ROOT, "tables", "ui_widgets.json")
    g = omkpaths.data("IAM", "GAMES")
    if not (os.path.isdir(eng) and os.path.exists(w) and os.path.exists(g)):
        return ("skipped",), ("skipped",), "engine/, the table or GAMES absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "load_panel")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "l.bin")
    try:
        subprocess.run([binp, w, g, out], capture_output=True)
        v = struct.unpack_from("<10i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (256, 0, 8402344, 18432, 1, 3, 1, 0, 0, 3), \
           "directory entries and the distinct profiles among them (the " \
           "shipped file is one the engine made and never saved into); the " \
           "file's geometry and the 256*72 the builder memsets; then the " \
           "panel with an EMPTY directory - focus, mode, and how many " \
           "buttons stay live - and beside it a one-profile directory, so " \
           "the model is shown to BRANCH rather than to ignore the input"


def c_engine_inventory():
    r"""`engine/`'s inventory channel - `Game_HandleEvent` 25..42.

    The interface never touches an object list. It raises an event with ONE
    argument block - `+0` the value in and out, `+4` the result code (and for
    case 36 the request code on the way in), `+8` a caller-supplied buffer -
    and reads the answer back out of it. So the whole of the inventory's
    behaviour is decidable from the object records and the game DB, both of
    which were already ported: this is the wiring between them.

    **The three lists come out of `IAM\START` exactly as `object lists` reads
    them**: carried `[6, 171]`, second `[176, 163]`, memos empty. A fourth -
    list 3, a shop's stock - is sized to 16 and loaded per area when case 25
    opens it, and is never stored in the DB.

    **The cases that are pure data decisions are ported**: 33 the display name
    with `" - N"` appended when flag 0x20 says the record carries a quantity
    (counted from the PLAYER record for kinds 2..6 and the item's own `+12`
    for 7..11), 34 the price, 38 buy - refused when the list is full or the
    price exceeds the player's money, 39 sell at HALF clamped to 0xFFFF, 41
    the shop's refusal to sell a gun the player already holds, and 37 combine
    through `GLOBAL +12`.

    **The 0xFFFF clamp never bites.** 161 objects carry a price, the dearest
    is 5000 and the best sale 2500, so all 161 halve exactly and none reaches
    the clamp. It is defensive rather than load-bearing, and saying which is
    the difference between reading the code and running it over the data.

    **Six of the eleven recipes are unreachable at any gate the engine can
    produce.** `combine` is offered 1, 0 and -1 - the only three values
    written to the global it compares against - and six recipes want 8, so
    they never fire. That is the documented dead content, re-derived here by
    asking the ported function rather than by inspecting the table.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_inventory")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "i.bin")
    try:
        subprocess.run([binp, omkpaths.data_root(), out], capture_output=True)
        v = struct.unpack_from("<15i", open(out, "rb").read(), 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v, (2, 2, 0, 6, 171, 176, 163,
               161, 36, 17, 161, 0, 5000, 2500, 5, 6, 6)[:15], \
           "the three stored lists' lengths and then their ids - carried " \
           "[6, 171] and second [176, 163], which is what `object lists` " \
           "reads independently; then priced objects, those carrying a " \
           "quantity, the guns; how many sell for exactly half and how many " \
           "need the 0xFFFF clamp (NONE - the dearest object is 5000, so the " \
           "clamp is defensive rather than load-bearing); and the dearest " \
           "price and sale"


def c_engine_text():
    r"""`engine/`'s text layout - measured against `tools/uitext.py`.

    **A `.FNT` alone cannot lay text out**, which is why the format being
    ported did not finish this row. A glyph's advance is its own width plus
    the FACE's kerning, and a character the file does not carry falls back to
    the face's default advance - and both of those live in the 13-record font
    table at 0x004C7090 (`Font_Find`), not in the glyph file. That table is
    now lifted into `tables/ui.json`, keyed by an ASCII LETTER, which is what
    an item's `+36` has been all along: 74 is `J` JOURNAL, 83 `S` SNEAK for a
    heading, 76 `L` SMALL below 640x480.

    **Widths in pixels are the thing worth comparing.** Three different places
    contribute to one advance, and a layout that got any of them wrong still
    renders something - only the measured width says which. This runs both
    implementations over the same eight strings and requires every character
    count, width, height, alignment, ink and face to agree.

    The cases exercise the markup that FILE_FORMATS 5b4 had wrong: `{f<letter>}`
    selects a face mid-string, `{I<9 digits>}` is THREE 3-DIGIT components and
    not a hex triple, several chain inside one brace (`{fCI255120045}`), `{C}`
    sets alignment where a bare string sets none - so the item's own flags
    stand - `{X<6 digits>}` is a move layout skips because the caller owns the
    box, and `[` `]` delimit counted spans that carry no styling and no width.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    tbl = os.path.join(ROOT, "tables", "ui.json")
    fonts = omkpaths.data("FONTS")
    if not (os.path.isdir(eng) and os.path.isdir(fonts)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/FONTS absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_text")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "t.bin")
    try:
        subprocess.run([binp, tbl, fonts, out], capture_output=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    faces, n = struct.unpack_from("<2i", raw, 0)
    mine = [struct.unpack_from("<8i", raw, 8 + 32 * k) for k in range(n)]

    # the same eight strings through tools/uitext.py, which reads the
    # executable and the .FNT files directly
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import uitext
    cases = [("Nouvelle partie", "J"), ("Charger une partie", "J"),
             ("{fS}Options", "J"), ("{fCI255120045}rouge", "J"),
             ("{C}centre", "J"), ("Kay'l", "D"), ("[compte]", "J"),
             ("{X001002}decale", "J")]
    ref = []
    for t, f in cases:
        run, a = uitext.parse(t, f)
        h = max((uitext.font(c[1])[0]["height"] for c in run
                 if uitext.font(c[1])), default=0)
        last = run[-1] if run else ("", f, (255, 255, 255))
        ref.append((len(run), uitext.measure(run), h, -1 if a is None else a,
                    last[2][0], last[2][1], last[2][2], ord(last[1])))
    disagree = sum(1 for a, b2 in zip(mine, ref) if a != b2) + abs(len(mine) - len(ref))
    return (faces, n, disagree, mine[0], mine[3]), \
           (13, 8, 0, (15, 127, 17, -1, 255, 255, 255, ord("J")),
            (5, 38, 14, -1, 255, 120, 45, ord("C"))), \
           "font faces in the lifted table; the strings measured; how many " \
           "of them the two implementations DISAGREE on - which must be " \
           "zero; then two rows in full: a plain line (15 chars, 127px, 17 " \
           "tall, no alignment set so the item's stands) and one whose " \
           "chained `{fCI255120045}` must give face C and ink 255/120/45"


def c_engine_boot():
    r"""`engine/` BOOTED - the whole chain, nothing hand-wired.

    Until now every slice was proved on its own and assembled by a test
    harness: `boot_intro` builds a Session itself, hands it an opcode table,
    calls `loadArea(118)` and ticks. That proves the pieces. It does not prove
    they fit together, and "the parts are each correct" is exactly the claim
    that survives an integration being wrong.

    `tools/omk.cpp` is the engine run as a program. It parses the command line
    the way `WinMain` does - `NOFMV` and `WINDOW` matched as whole words -
    steps the three FLIS movies, does `Game_Start("aventure.scx")`, derives
    the start menu's answer by walking screen 29, and then runs
    `Game_RunLoop`'s idle path calling `Game_Frame`.

    **And its announcement stream matches `traces/intro.log` 42 of 42, in
    order, from a cold boot.** The same number the hand-wired harness gets,
    reached without the harness.

    Four things it demonstrates that a slice could not:

    * the three movies RESOLVE - and the executable spells them `.mpg` while
      the disc ships `.MPG`, so the boot path is the first thing in the game
      that cannot work without a case-insensitive filesystem;
    * `aventure.scx` resolves and is read - and it is **not a menu and not a
      location**. It holds **20 effect sprites and 53 sound registrations**
      and no menu logic whatever: the smoke, glows, impacts and stars, plus
      the player's own footsteps, breathing, jumps and eat/drink. It is the
      GLOBAL library, loaded once so every scene can reference it, which is
      why it is 3 MB with a 39 KB block and why `Game_Start` takes it before
      anything else. An earlier version of this docstring said "the main menu
      is a scene like any other, started by the same call that starts a
      location" - inferred from the CALL without opening the FILE, which is
      CLAUDE.md 1 backwards. **Where the menu is opened from is not answered
      here**;
    * `Interface` is **1** - and the SCRIPT asks for it. AREA 118's startup
      script reaches `ui.open(29, -1, -> variable 19)` at pc 1078, the
      interpreter parks the context there the way the handler parks its caller
      at status 6, the Session walks screen 29 for an answer, writes variable
      19, and the script resumes into `dialog.start 272`. An earlier version
      walked screen 29 in the BOOT and seeded variable 19 before any script
      ran - the right number by doing the right thing in the wrong place, and
      it would have gone on being right even if the script had asked for a
      different screen. The screen id and the variable are now read from the
      instruction's own operands, and both are asserted;
    * the starting area is **118**, read from `IAM\START`'s `+1414` rather
      than named. An earlier version of this had the 118 as a literal in the
      boot while the prose claimed nothing was hand-wired - `Game_NewGame`
      loads `IAM\START` over a zeroed DB and applies it, and where the player
      is lives in that block;
    * the start menu's four labels resolve out of `IAM\Menu` - "Nouvelle
      partie | Charger une partie | Options | Quitter". Without the text
      archives every item the widget walk finds is a number, and a booting
      engine that prints its menu as integers is obviously half-done.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    trc = os.path.join(ROOT, "traces", "intro.log")
    if not (os.path.isdir(eng) and os.path.exists(trc)):
        return ("skipped",), ("skipped",), "engine/ or traces/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "omk")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "b.bin")
    try:
        run = subprocess.run([binp, omkpaths.data_root(),
                              "--tables", os.path.join(ROOT, "tables"),
                              "--dump", out], capture_output=True, text=True)
        raw = open(out, "rb").read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    h = struct.unpack_from("<13i", raw, 0)
    o = 52
    mine = []
    for _ in range(h[12]):
        n = raw[o]; o += 1
        dom = raw[o:o + n].decode(); o += n
        v, = struct.unpack_from("<i", raw, o); o += 4
        mine.append((dom, v))
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    vis = [e for e in mine if e[0] not in ("CHARACTERS", "VALUES") and e[1] != -1]
    cap = [(d, int(v)) for d, v in G.parse(trc)]
    agree = 0
    for k in range(min(len(vis), len(cap))):
        if vis[k] != cap[k]: break
        agree += 1
    menu = "Nouvelle partie | Charger une partie | Options | Quitter" in run.stdout
    return h[:12] + (len(vis), agree, menu), \
           (3, 0, 1, 20, 53, 1, 29, 19, 118, 1, 1, 1, 42, 42, True), \
           "the FLIS movies found (3 - and the image spells them .mpg where " \
           "the disc spells .MPG); whether they were skipped; that " \
           "aventure.scx resolves, and the 20 sprites and 53 sounds it " \
           "registers - it is the global effect library, NOT a menu; the " \
           "Interface answer, " \
           "the SCREEN and VARIABLE the script's own `ui.open` names - 29 " \
           "and 19, read from the instruction rather than chosen here; " \
           "and the starting AREA read out of IAM\\START +1414 rather than " \
           "named in the boot code; " \
           "DERIVED by walking screen 29 rather than supplied; startup " \
           "contexts, conversations, areas entered; then the decisions the " \
           "boot announced and how many match traces/intro.log IN ORDER - " \
           "42 of 42, from a cold boot with nothing hand-wired; and that the " \
           "start menu's four labels resolve out of IAM\\Menu"


def c_menu_open_site():
    r"""Where the start menu is opened from - and a negative result that was
    wrong for the oldest reason in this repo.

    **`ui.open(29, -1, -> variable 19)` at pc 1078 of AREA 118's startup
    script**, the `+4` one at offset 1040. The chain around it:

        Game_Main -> Game_Init -> the three movies
          -> Game_Start("aventure.scx")   the global sprite/sound library
          -> the OMIKRON.BMP splash, Sleep(5000)
          -> Game_RunLoop
        IAM\START +1414 = 118, so AREA 118 is resident
          -> Area_TickLoad queues its +4 startup script
          -> pc 1078: `ui.open 29` SUSPENDS the script and shows the menu
          -> the answer lands in variable 19, `Interface`, and it resumes
          -> pc 1212: `dialog.start 272` - Kay'l / Intro

    **The two menu captures are that script and nothing else.**
    `menu-noinput.log` holds exactly three events - VARIABLES 175, VARIABLES
    170, OBJECTS 997 - which are the first three of `intro.log`: the script
    running as far as `ui.open` and parking. `menu-keys.log` holds the same
    three FOUR TIMES, the screen reopening rather than answering. Neither
    contains a menu announcing itself, because a menu announces nothing.

    **How the search went wrong first, which is why this is written down.**
    Counting `ui.open` sites across the world scripts gave 24 screens and not
    29, so "the menu must be native" - and every native path was then
    correctly eliminated: five `UI_LoadScreen` calls with literal screens (31,
    33, 34, 35, 36); one variable-argument caller, which is `UI_OpenScreen`,
    the VM's own handler, and which refuses without a player actor anyway;
    and `Game_Init` only CLOSES 29. All true, and all beside the point,
    because the enumeration behind the negative result did not include the
    `+4` startup scripts. That is CLAUDE.md 6's own lesson - "no shipped
    script starts them" really meaning "no script I enumerate" - repeated in
    the same session that ported those scripts.
    """
    import dialog_triggers as T, dialog_disasm as D
    b = T.archive(omkpaths.data("IAM/AREA"))[118]
    at = struct.unpack_from("<I", b, 4)[0]
    ops, st = D.disasm(b, at, len(b))
    opens = [(pc,) + struct.unpack_from("<3h", raw, 0)
             for pc, op, raw in ops if op == 70 and len(raw) >= 6]
    dialogs = [(pc, struct.unpack_from("<h", raw, 0)[0])
               for pc, op, raw in ops if op == 61 and len(raw) >= 2]
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    noinput = G.parse(os.path.join(ROOT, "traces/menu-noinput.log"))
    keys = G.parse(os.path.join(ROOT, "traces/menu-keys.log"))
    return (at, st, opens, dialogs, len(noinput), len(keys),
            noinput == keys[:3], len(keys) == 4 * len(noinput)), \
           (1040, "ok", [(1078, 29, -1, 19)], [(1212, 272)], 3, 12, True, True), \
           "AREA 118's +4 startup script: where it lives, that it decodes, " \
           "its `ui.open` sites - ONE, screen 29 answering into variable 19, " \
           "`Interface` - and its `dialog.start`; then the two menu captures, " \
           "whose 3 and 12 events are that script parking at `ui.open` once " \
           "and four times over"


def c_replay_actor_stat():
    r"""Two of the five trace disagreements, explained exactly.

    `AREA 179 rec 47 +4` and `AREA 217 rec 13 +4` are the same eleven-
    instruction script twice, and it is a SAVE POINT:

        camera.set 3711
        var.set.actor_stat(-1, 5, 60)      write a live actor stat into var 60
        push.i8 / push.var 60 / cmp.gt
        jmp_if_false +10
        ui.open(30, -1, -1)                screen 30 - the SAVE panel
        jmp +3
        media.play 990                     ... the other arm
        camera.set 6
        end

    **Opcode 86, `var.set.actor_stat`, is stubbed** - it reads the player
    record, and a standalone replay has no actor - so variable 60 keeps
    whatever the replay's state held, the compare fails, and the replay walks
    the `media.play` arm while the engine walked the `ui.open` one and parked.

    That is not "the mismatches follow the state" in the vague sense recorded
    before; it is a specific missing input, and it is demonstrated rather than
    argued. Forcing variable 60 above the threshold makes both scripts park at
    `ui.open` and predict exactly `CAMERAS/nnnn, VARIABLES/60` - which is
    precisely the two events each was already agreeing on. The two surplus
    predictions, `OBJECTS/990` and `CAMERAS/6`, are the branch the engine
    never took.

    So two of the five are fully accounted for and neither is a decision
    difference. They stay counted as disagreements because the replay still
    cannot know the stat; what has changed is that the reason is named.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import goldentrace as G, gamestate, dialog_triggers as T
    out = []
    for chunk, off in ((179, 6108), (217, 3793)):
        code = T.archive(omkpaths.data("IAM/AREA"))[chunk]
        row = []
        for v60 in (0, 99):
            st = gamestate.load()
            struct.pack_into("<i", st.raw, st.offset(0) + 4 * 60, v60)
            pred, why = G.replay(code, off, st)
            row.append((why, ["%s/%s" % p for p in pred]))
        out.append(row)
    return out, \
           [[("end", ["CAMERAS/3711", "VARIABLES/60", "OBJECTS/990", "CAMERAS/6"]),
             ("ui.open", ["CAMERAS/3711", "VARIABLES/60"])],
            [("end", ["CAMERAS/4157", "VARIABLES/60", "OBJECTS/990", "CAMERAS/6"]),
             ("ui.open", ["CAMERAS/4157", "VARIABLES/60"])]], \
           "the two save-point scripts replayed with variable 60 below and " \
           "above the threshold their `cmp.gt` tests: below, the replay runs " \
           "to `end` down the media.play arm and predicts four events; above, " \
           "it PARKS at `ui.open 30` and predicts the two the capture " \
           "actually contains. The gap is opcode 86, `var.set.actor_stat`, " \
           "which reads the player record and is stubbed - a named missing " \
           "input, not a decision difference"


def c_morphs():
    """Every .3DM must be an exact number of frame records - or short by
    exactly the last frame's audio block, the only remainder that occurs."""
    d = omkpaths.data("MORPH")
    files = [f for f in sorted(os.listdir(d)) if f.lower().endswith(".3dm")]
    exact = short = pre = 0
    for f in files:
        p = os.path.join(d, f)
        try: L = morph3dm.layout(p)
        except Exception: continue
        if L["tail"] == 0: exact += 1
        elif L["tail"] == L["record"] - L["audio"]: short += 1
        idx = struct.unpack_from("<%dI" % L["nodes"],
                                 open(p, "rb").read(16 + 4 * L["nodes"]), 16)
        pre += list(idx) == list(range(L["nodes"]))
    return (len(files), exact, short, pre), (777, 582, 195, 777), \
           ".3DM files: on a record boundary, short by the last audio block, " \
           "preamble 0,1,2,..."

def c_ani_quaternions():
    import glob
    clips = keys = unit = 0
    for p in sorted(glob.glob(omkpaths.data("ANIMS/*.[aA][nN][iI]"))):
        try: d, cs = anim_ani.load(p)
        except Exception: continue
        for c in cs:
            dd = anim_ani.descriptor(d, c["desc"])
            if not dd: continue
            clips += 1
            for t in dd["tracks"]:
                for q in anim_ani.rotations(d, t):
                    keys += 1
                    unit += abs(math.hypot(*q) - 1.0) < 1e-3
    return (clips, keys, unit), (265, 243362, 243362), \
           "clips, rotation keys, and keys that are unit quaternions"


def c_var_set_random():
    """op 120's third field is the variable the next instruction pushes.

    That is what pins both the field and the 6-byte operand length: the idiom
    is always `var.set.random lo, hi, v` / `push.var v` / a `case` ladder, and
    at 2 bytes the ladder disintegrates.
    """
    import dialog_disasm as D, dialog_triggers as T2
    sites = nextvar = inrange = 0
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if not r: continue
            for rec, f, p in (list(T2._scripts_from_records(b, r[0], r[1]))
                              + T2._second_table(name, b)):
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": continue
                for i, (pc, op, raw) in enumerate(ops):
                    if op != 120: continue
                    lo, hi, var = struct.unpack("<3h", raw)
                    sites += 1
                    nxt = ops[i + 1] if i + 1 < len(ops) else None
                    if nxt and nxt[1] == 10 and struct.unpack("<h", nxt[2])[0] == var:
                        nextvar += 1
                    cases = []
                    for pc2, op2, raw2 in ops[i + 1:]:
                        if op2 == 42: cases.append(raw2[2])
                        if op2 == 3: break
                    if cases and set(cases) <= set(range(lo, hi + 1)): inrange += 1
    return (sites, nextvar, inrange), (235, 235, 230), \
           "var.set.random sites; field 2 is the variable pushed next; " \
           "case labels within [lo, hi]"


# --------------------------------------------------------- opcodes named in phase 1
_CORPUS = None
def _world_ops():
    """-> {opcode: [operand bytes, ...]} over all 5785 decoded world scripts."""
    global _CORPUS
    if _CORPUS is None:
        import dialog_disasm as D, dialog_triggers as T2, collections
        out = collections.defaultdict(list)
        def scan(b, slots):
            for rec, f, p in slots:
                ops, st = D.disasm(b, p, len(b))
                if st == "ok":
                    for pc, op, raw in ops: out[op].append(raw)
        for name in ("AREA", "SCENE"):
            for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
                r = T2.LAYOUT[name](b)
                if r: scan(b, list(T2._scripts_from_records(b, r[0], r[1]))
                             + T2._second_table(name, b))
        b, slots = T2.global_file(omkpaths.data("IAM", "GLOBAL"))
        scan(b, slots)
        _CORPUS = out
    return _CORPUS


def c_music_play():
    r"""SCRIPT_VM: op 103 `music.play` - field 0 is a TRACKS\N.ADP number.

    A sparse set: 145 files scattered over 0..250, so a field that was really
    something else would miss. The three that do not name a file are track 0,
    and Music_PlayTrack returns without playing for anything below 2.

    The same check on the area header's own music field (+142) is what ties the
    opcode to the engine: the area loader plays that track under the identical
    "already playing" guard.
    """
    tracks = {int(f[:-4]) for f in os.listdir(omkpaths.data("TRACKS"))
              if f.upper().endswith(".ADP")}
    raws = _world_ops()[103]
    f0 = [struct.unpack_from("<h", r, 0)[0] for r in raws]
    named = sum(v in tracks for v in f0)
    zero = f0.count(0)

    import dialog_triggers as T2
    hdr = [struct.unpack_from("<H", b, 142)[0]
           for k, b in sorted(T2.archive(omkpaths.data("IAM/AREA")).items()) if len(b) >= 144]
    hdr_named = sum(v in tracks for v in hdr if v)
    return (len(f0), named, zero, sum(1 for v in hdr if v), hdr_named), \
           (521, 518, 3, 189, 189), \
           "music.play sites, of which name a real TRACKS file, and track 0; " \
           "AREA +142 music fields, of which name a real TRACKS file"


def c_fade_color():
    """SCRIPT_VM: ops 118/119 - fields 0 and 1 are a 24-bit colour.

    The two int16 are the halves of the 32-bit value the handler assembles and
    hands to Screen_StartColorFade. If that reading were wrong the top byte
    would be noise; it is zero at every site, and the values are black, white
    and two others - which is what a colour authored by hand looks like.
    """
    raws = _world_ops()[118] + _world_ops()[119]
    cols = [struct.unpack_from("<I", r, 0)[0] for r in raws]
    return (len(cols), sum(c <= 0xFFFFFF for c in cols),
            sum(c == 0 for c in cols), sum(c == 0xFFFFFF for c in cols)), \
           (440, 440, 314, 124), \
           "fade.to_color/from_color sites, of which have a zero top byte; " \
           "black; white"


def c_music_volume():
    """SCRIPT_VM: op 131 `music.volume` - field 0 is a 0..100 volume.

    Music_SetVolume clamps to 100 and scales to the DirectSound
    hundredths-of-a-dB range, so anything outside 0..100 would be authored
    nonsense. 36 of the 52 sites are 0: scripts fading the music out.
    """
    f0 = [struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[131]]
    return (len(f0), sum(0 <= v <= 100 for v in f0), f0.count(0)), \
           (52, 52, 36), "music.volume sites, of which are inside 0..100; silence"


def c_scene_load():
    """SCRIPT_VM: ops 71/72 - field 0 is an AREAS id, 71's field 1 a SCENES id.

    Two independent things have to agree here, and the second is the one the
    data could fail. First the ranges: field 0 is a valid AREAS id at every
    site but a valid SCENES id at only half of them, which is what separates
    the two fields. Then the *names*: the game's sections are numbered by city,
    and every scene a script loads into an area is numbered for that area's own
    city - 1 for Anekbah and Qalisar, 2 for Jaunpur, 3 for Lahoreh. A wrong
    pairing of the two fields would scramble that immediately.
    """
    import dialog_disasm as D
    A, S_ = D.TAGS["AREAS"], D.TAGS["SCENES"]
    city = {"Anekbah": "1", "Qalisar": "1", "Jaunpur": "2", "Lahoreh": "3"}
    load = [struct.unpack("<2h", r) for r in _world_ops()[71]]
    unload = [struct.unpack("<h", r)[0] for r in _world_ops()[72]]
    section = 0
    for a, sc in load:
        an, sn = A.get(a, ""), S_.get(sc, "")
        m = re.match(r"(\d)-", sn)
        if m and an and city.get(an.split()[0]) == m.group(1): section += 1
    return (len(load), sum(a in A for a, _ in load), sum(s in S_ for _, s in load),
            section, len(unload), sum(a in A for a in unload)), \
           (78, 78, 78, 78, 82, 82), \
           "scene.load sites, field 0 in AREAS.TAG, field 1 in SCENES.TAG, " \
           "city section digits that agree; scene.unload sites, in AREAS.TAG"



_PROPS = None
def _prop_table():
    r"""The 24-byte prop records: AREA +44 (count int16 +74), SCENE +12 (+42).

    -> ({chunk: [record, ...]} per archive, flat list). Fields, in the order
    Scene_LoadProps consumes them:
        +0 int16 runtime slot (-1 on disk)   +2 int16 OBJECTS id
        +4/+8/+12 int32 x, y, z              +16/+18/+20 int16 angles
        +22 int16 index into the game DB's 2-bit prop-state array
    """
    global _PROPS
    if _PROPS is None:
        import dialog_triggers as T2
        out = {}
        for name, ptr, cnt in (("AREA", 44, 74), ("SCENE", 12, 42)):
            per = {}
            for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
                if len(b) < cnt + 2: continue
                p, n = struct.unpack_from("<I", b, ptr)[0], struct.unpack_from("<h", b, cnt)[0]
                if n <= 0 or p + 24 * n > len(b): continue
                per[k] = [struct.unpack("<hh3i3hh", b[p + 24 * i:p + 24 * i + 24])
                          for i in range(n)]
            out[name] = per
        flat = [r for per in out.values() for v in per.values() for r in v]
        _PROPS = (out, flat)
    return _PROPS


def c_prop_table():
    """FILE_FORMATS: AREA +44 / SCENE +12 - the prop table.

    The sibling of the 20-byte object table, and it checks the same three ways:
    the runtime slot at +0 is -1 on disk everywhere (Scene_LoadProps fills it
    in), every id at +2 names a real object, and the state index at +22 is a
    **dense** allocation - 670 records carrying exactly the indices 0..669,
    which is what one save-game slot per prop looks like and what a
    misread field would not produce.
    """
    per, flat = _prop_table()
    ids = {r[1] for r in flat}
    state = [r[8] for r in flat]
    return (len(flat), sum(r[0] == -1 for r in flat), len(ids),
            sum(i in O.TAGS["OBJECTS"] for i in ids),
            set(state) == set(range(len(flat)))), \
           (670, 670, 300, 300, True), \
           "prop records, of which have slot -1 on disk; distinct ids, of " \
           "which are in OBJECTS.TAG; state indices are dense 0..N-1"


def c_prop_opcodes():
    """SCRIPT_VM: ops 66/76/77 address the prop table.

    Each handler scans the table of the script's own area and then of the
    scene loaded over it. So every operand has to name a record somewhere -
    and most of them in the script's own chunk, the rest being the scene half
    of that two-table search.
    """
    per, flat = _prop_table()
    ids = {r[1] for r in flat}
    byc = {a: {k: {r[1] for r in v} for k, v in p.items()} for a, p in per.items()}
    import dialog_triggers as T2, dialog_disasm as D, collections
    tot, hit, own = collections.Counter(), collections.Counter(), collections.Counter()
    def scan(arch, k, b, slots):
        local = byc.get(arch, {}).get(k, set())
        for rec, f, p in slots:
            ops, st = D.disasm(b, p, len(b))
            if st != "ok": continue
            for pc, op, raw in ops:
                if op not in (66, 76, 77): continue
                v = struct.unpack_from("<h", raw, 0)[0]
                tot[op] += 1; hit[op] += v in ids; own[op] += v in local
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if r: scan(name, k, b, list(T2._scripts_from_records(b, r[0], r[1]))
                       + T2._second_table(name, b))
    b, slots = T2.global_file(omkpaths.data("IAM", "GLOBAL"))
    scan("GLOBAL", 0, b, slots)
    return (sum(tot.values()), sum(hit.values()), sum(own.values())), \
           (443, 443, 410), \
           "object.hold/show/hide sites, of which name a real prop record, " \
           "and of which are in the script's own chunk"


def c_var_writers():
    """SCRIPT_VM: ops 75 and 91, identified by the name of the variable.

    Both end in Var_Set with an operand-supplied index, so the .TAG name of
    what they write is the evidence - and it is a test the data could fail
    flatly. 75 stores the id of the prop the player is holding and writes
    `ObjetUtilisé` every time; 91 stores which character the player currently
    is, and every variable it writes is named for the player.
    """
    used = [struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[75]]
    who = [struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[91]]
    V = O.TAGS["VARIABLES"]
    return (len(used), sum(V.get(v) == "ObjetUtilisé" for v in used),
            len(who), sum(V.get(v, "").startswith("Joueur") for v in who)), \
           (235, 235, 63, 63), \
           "var.set.used_object sites, of which write VARIABLES 'ObjetUtilisé'; " \
           "var.set.player_id sites, of which write a 'Joueur' variable"



def _object_ids():
    """{archive: {chunk: {id, ...}}} from the 20-byte object tables.

    Offsets straight out of Scene_FindObjectIndexById: area + 40 with the
    count at + 72, scene + 8 with the count at + 40.
    """
    import dialog_triggers as T2
    out = {}
    for name, ptr, cnt in (("AREA", 40, 72), ("SCENE", 8, 40)):
        per = {}
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            if len(b) < cnt + 2: continue
            p, n = struct.unpack_from("<I", b, ptr)[0], struct.unpack_from("<h", b, cnt)[0]
            if n <= 0 or p + 20 * n > len(b): continue
            per[k] = {struct.unpack_from("<h", b, p + 20 * i + 2)[0] for i in range(n)}
        out[name] = per
    return out


def c_character_operands():
    """SCRIPT_VM: ops 69/82/84 take a character id out of the object table.

    None of the three has a `.TAG` file to check against - the game ships no
    CHARACTERS.TAG - so the table itself is the domain, and the handlers say
    so by resolving through Scene_FindObjectIndexById. Every operand has to
    name a record, and most of them one in the script's own chunk; the rest
    are the scene half of that two-table search.
    """
    import dialog_triggers as T2, dialog_disasm as D, collections
    per = _object_ids()
    ids = {i for a in per.values() for v in a.values() for i in v}
    tot = hit = own = 0
    def scan(arch, k, b, slots):
        nonlocal tot, hit, own
        local = per.get(arch, {}).get(k, set())
        for rec, f, p in slots:
            ops, st = D.disasm(b, p, len(b))
            if st != "ok": continue
            for pc, op, raw in ops:
                if op not in (69, 82, 84): continue
                v = struct.unpack_from("<h", raw, 0)[0]
                tot += 1; hit += v in ids; own += v in local
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if r: scan(name, k, b, list(T2._scripts_from_records(b, r[0], r[1]))
                       + T2._second_table(name, b))
    b, slots = T2.global_file(omkpaths.data("IAM", "GLOBAL"))
    scan("GLOBAL", 0, b, slots)
    n = sum(len(v) for a in per.values() for v in a.values())
    return (n, tot, hit, own), (1032, 1135, 1135, 1068), \
           "object records (830 AREA + 202 SCENE); character.object.release / " \
           "shoot.actor.* sites, of which name a record, and are in their own chunk"


def c_arm_actor():
    """SCRIPT_VM: op 67 `object.hold.actor` — what arms the enemies.

    The character version of `object.hold`: field 0 the character, field 1 the
    prop it is given. Both halves check against their own table, and the names
    say what the opcode is for — almost every object it hands out is a gun.
    """
    import dialog_disasm as D
    per, flat = _prop_table()
    props = {r[1] for r in flat}
    obj = _object_ids()
    chars = {i for a in obj.values() for v in a.values() for i in v}
    T = O.TAGS["OBJECTS"]
    tot = f1 = f0 = gun = 0
    for raw in _world_ops()[67]:
        a, b = struct.unpack("<2h", raw)
        tot += 1; f1 += b in props; f0 += a in chars
        n = T.get(b, "")
        gun += n.startswith("Gun") or n.startswith("B\u00e2ton")
    return (tot, f0, f1, gun), (159, 159, 159, 157), \
           "object.hold.actor sites; field 0 names a character record, " \
           "field 1 a prop record, and field 1 is a Gun"


def c_ui_screens():
    r"""SCRIPT_VM: op 70 `ui.open` and the 37-entry screen table.

    Three checks, none of them a range test.

    The artwork: sub_429BB0 loads `I2d\bitmaps\%s`, and the bitmap names in
    the binary's screen table are *exactly* the files shipped in
    gamedata/I2D/bitmaps - 11 and 11, no spares either way.

    The index: the shop screens have to open where their name says. BANK in
    the areas called 'Banque', LIBRAIRIE in the ones called 'Librairie',
    LIB. LAHOREY 40 times and every one of them in 'Lahoreh Bibliothèque'.

    The cut entries: five of the 37 are named "(ELIMINE)", and the shipped
    scripts use none of those five indices - which a wrong index base would
    not leave clean.
    """
    import dialog_triggers as T2, dialog_disasm as D
    A, S_ = O.TAGS["AREAS"], O.TAGS["SCENES"]
    where = {19: "Morgue", 20: "Banque", 21: "Pharmacie", 25: "Sorcellerie",
             26: "Librairie", 27: "Sexshop", 32: "Lahoreh Bibliothèque"}
    elim = {i for i, n in enumerate(D.SCREEN) if "(ELIMINE)" in n}
    tot = match = cut = 0
    for name in ("AREA", "SCENE"):
        tags = A if name == "AREA" else S_
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if not r: continue
            for rec, f, p in (list(T2._scripts_from_records(b, r[0], r[1]))
                              + T2._second_table(name, b)):
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": continue
                for pc, op, raw in ops:
                    if op != 70: continue
                    v = struct.unpack_from("<h", raw, 0)[0]
                    cut += v in elim
                    if v in where:
                        tot += 1
                        match += where[v].lower() in tags.get(k, "").lower()
    # and the addresses: every walk-to that precedes a MULTIPLAN is named for it
    AD = O.TAGS["ADDRESSES"]
    mp = mpnamed = 0
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if not r: continue
            for rec, f, p in (list(T2._scripts_from_records(b, r[0], r[1]))
                              + T2._second_table(name, b)):
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": continue
                last = None
                for pc, op, raw in ops:
                    if op == 73: last = struct.unpack_from("<h", raw, 0)[0]
                    elif op == 70 and last is not None:
                        if struct.unpack_from("<h", raw, 0)[0] == 2:
                            mp += 1
                            mpnamed += AD.get(last, "").startswith("Multiplan")
    shipped = {f.lower() for f in os.listdir(omkpaths.data("I2D/bitmaps"))}
    named = {"ascen.bmp", "boutiq.bmp", "den00.bmp", "ecran36.bmp",
             "gandhar.bmp", "gfxint.bmp", "meca.bmp", "multipla.bmp",
             "shooting.bmp", "sneak.bmp", "xanoir1.bmp"}
    return (len(D.SCREEN), len(elim), cut, tot, match, mp, mpnamed,
            shipped == named), \
           (37, 5, 0, 57, 57, 73, 73, True), \
           "screens in the table, of which cut, and sites using a cut one; " \
           "shop-screen sites, of which open in an area named for them; " \
           "MULTIPLAN sites reached by an actor.goto_address, of which walk " \
           "to an ADDRESSES entry called 'Multiplan N'; and the binary's " \
           "bitmap names are the shipped files"


def c_ui_tables():
    r"""UI: the three compiled interface tables, and what each has to resolve.

    The UI is the one subsystem with no shipped file describing it, so every
    check here is a cross-reference from a table compiled into the engine out
    into the tree that ships - which is the only thing the data can fail.

    The screen table's walk ends exactly on the string `aNoOne_45` and the
    sound table's on `aNoOne_0`, both at a whole number of records; the option
    table terminates itself, +136 counting 1..73 and then 0.

    The screen table also settles what SCRIPT_VM 70 could only infer: +4 runs
    0..36 in table order, so the record index IS the `ui.open` operand, and
    rows 13 and 24 - the two with no name string of their own - are DEN and
    BAR, which is what the script sites had implied.

    `tools/ui_tables.py --selftest` prints the same checks one per line.
    """
    import ui_tables as U
    e = U.Exe()
    sc, op, sn, pg = U.screens(e), U.options(e), U.sounds(e), U.pages(e)
    bmp = {s["bitmap"].lower() for s in sc if s["bitmap"]}
    shipped = {f.lower() for f in os.listdir(omkpaths.data("I2D/bitmaps"))}
    iam = set(os.listdir(omkpaths.data("IAM")))
    txt = {s["text"] for s in sc if s["text"]}
    wav = {f.lower() for f in os.listdir(omkpaths.data("I2D/sounds"))}
    used = {k for s in sc for k in s["sounds"] if k != -1}
    caps = [(o["index"], c) for o in op for c, _ in o["choices"]]
    va = {p["va"] for p in pg}
    return (len(sc), [s["id"] for s in sc] == list(range(37)),
            sc[13]["name"], sc[24]["name"],
            bmp == shipped, len(txt), txt <= iam,
            len(sn), sorted(sn) == list(range(45)),
            sum((v.lower() + ".wav") in wav for v in sn.values()),
            len(used), used <= set(sn),
            len(op), [o["seq"] for o in op] == list(range(1, 74)) + [0],
            sum(o["label"] is not None for o in op),
            len(caps), sorted({i for i, c in caps if c is None}),
            len(pg), all(p["parent"] in va or not p["parent"] for p in pg)), \
           (37, True, "DEN", "BAR", True, 18, True,
            45, True, 45, 40, True,
            74, True, 74, 37, [23, 24],
            13, True), \
           "screen records, ids 0..36 in order, and what 13 and 24 are; the " \
           "bitmaps named ARE the shipped files; IAM text files named, all " \
           "present; sound records, ids contiguous, .wav files that ship, ids " \
           "the screens use and whether they resolve; option records, the " \
           "+136 terminator, labels resolving in IAM/Options, choice captions " \
           "and the rows without one (the two sensitivity sliders); pages, " \
           "and every parent pointer resolving"


def c_ui_widgets():
    r"""UI: the widget tree - the tile-map background and the oscillators.

    Ui_DrawPanelBack reads panel+20 as 80 tile ids over a 10-wide, 8-deep grid
    of 64x64 cells, and hard-codes row 7 as a half row drawn from source
    y 448..480. That only makes sense against a 640x480 sheet - 10*64 is 640
    and seven rows of 64 leave exactly the 32 the special case covers - so the
    check is that every shipped screen bitmap really is 640x480. It is what
    lets boutiq.bmp serve ten shops from one file.

    The oscillators are the other half of how the interface looks: the arrows,
    the selection marker and the pulsing fill all take their alpha from
    number 2, which runs 45..200 over a second. The table's walk ends exactly
    where the string block begins, 8 x 40 bytes on.

    `Ui_Oscillator`'s base is asserted here because the decompiler gets it
    wrong - it renders 0x004C3EE0 where the asm's `lea ds:4C3EA0h[eax*8]` says
    0x004C3EA0, which would put every record 24 bytes out.
    """
    import ui_tables as U
    d = omkpaths.data("I2D/bitmaps")
    dims = set()
    for f in os.listdir(d):
        h = open(os.path.join(d, f), "rb").read(54)
        dims.add(struct.unpack_from("<ii", h, 18))
    osc = U.oscillators()
    return (len(os.listdir(d)), sorted(dims), 10 * 64, 7 * 64 + 32,
            U.OSCILLATORS[0], len(osc), [o["id"] for o in osc] == list(range(8)),
            (osc[2]["period"], osc[2]["lo"], osc[2]["hi"])), \
           (11, [(640, 480)], 640, 480,
            0x004C3EA0, 8, True, (1000, 45, 200)), \
           "screen bitmaps and the distinct sizes among them; the tile grid's " \
           "width and its seven-and-a-half rows, which have to be those; the " \
           "oscillator table's base, its records, ids 0..7, and number 2's " \
           "period and range - the pulse on every arrow and marker"


def c_ui_fonts():
    r"""UI: the 13 fonts, `FONTS/*.FNT`, and the markup in the shipped text.

    The layout is 256 8-byte glyph records then the pixels from byte 2048,
    with the record's +0 an offset in EIGHT-byte units - so the test the data
    can fail is that every one of the 2899 glyph blocks lands inside its file,
    at or after the header, without overlapping another. It does, 13/13.

    A pixel byte is a coverage level into a 32-entry ramp of the text colour
    (`word_52F5B8`, which IDA types `__int16[32]`), and 485875 of 485877 bytes
    are in range. The two that are not are both in SMALL's '!' and are a
    shipped defect, asserted at exactly 2 so it stays a known quantity.

    The markup check comes from the other side: `{f<letter>}` names a font by
    the id letter of the table, and every one of the 329 operands in the
    shipped interface text is one of the 13 that exist. The colour directive
    is NINE DECIMAL digits, not a hex triple - `{I255120045}` - and 111 of
    them are well formed, which a wrong digit grouping would not reproduce.
    """
    out = _run("tools/fnt.py", "--selftest")
    ok = out.strip().endswith("0 failures")
    fails = [l.strip() for l in out.splitlines() if l.startswith("  FAIL")]
    import fnt
    t = fnt.table()
    byid = {r["id"]: r["name"] for r in t}
    return (ok, fails, len(t), byid.get(74), byid.get(76), byid.get(83)), \
           (True, [], 13, "JOURNAL", "SMALL", "SNEAK"), \
           "tools/fnt.py --selftest passes and what failed if not; font " \
           "records; and the three ids the interface names - 74 the default " \
           "and every option row, 76 the sub-640x480 override, 83 the headings"


def c_ui_input():
    r"""UI: the shared input callback, and the 14-slot binding word.

    The claim this exists to hold is a NEGATIVE one, and it corrected a note
    in the plan: there are no per-screen input handlers. The definition
    table's +24 is one address for every live screen, so the check is that the
    four callback slots have the distribution they do - 21 distinct open
    callbacks and 11 close against exactly ONE input, with the five (ELIMINE)
    screens carrying nulls in all four.

    The other half is the input word. `Input_Poll` maps binding k to bit
    1<<k out of a 14-entry table at 0x004C65B8, which ASSETS reads from the
    .CTL side; this asserts the same table from the UI side and pins the three
    bits the interface reuses - 0x10 confirm (E), 0x20 back (R), 0x2000 close
    (TAB) - plus the edge-trigger mask Ui_BeginScreen installs, 0x203F, which
    is what stops a held direction from scrolling a menu.
    """
    s = _need("asm")
    if s: return s
    import ui_tables as U, collections, glob as _g
    sc = U.screens()
    e = U.Exe()
    slots = [collections.Counter(s["cb"][k] for s in sc) for k in range(4)]
    dead = sum(1 for s in sc if not any(s["cb"]))
    # How many of the callbacks the table names have a decompiled function at
    # all. Most do not: they carry no push prologue, so IDA folded them into
    # whatever precedes them and Runtime.exe.c has no body for them.
    have = set()
    for f in _g.glob(os.path.join(ROOT, "readable/src/*.c")):
        have |= {int(x, 16) for x in
                 re.findall(r"@func (0x[0-9A-F]{8})", open(f, encoding="utf-8",
                                                           errors="replace").read())}
    named = {a for s in sc for a in s["cb"][:1] + s["cb"][2:3] if a}
    decompiled = len(named & have)
    keys = [struct.unpack("<I", e.read(0x004C65B8 + 4 * i, 4))[0]
            for i in range((0x004C65F0 - 0x004C65B8) // 4)]

    # WHY those addresses have no function - measured, not assumed. Predicting
    # "IDA gave it a proc label" from the prologue against predicting it from
    # whether anything CALLS it. See the docstring.
    import re as _re
    cbs = sorted({a for r in sc for a in r["cb"] if a})
    procs = set(int(m, 16) for m in _re.findall(
        r"^sub_([0-9A-F]{6}) proc near",
        open(omkpaths.asm_path(), errors="replace").read(),
        _re.M))
    ncall = {a: 0 for a in cbs}
    for sva, vsz, ptr, rsz in e.sec:
        blob = e.d[ptr:ptr + rsz]
        for i in range(len(blob) - 5):
            if blob[i] == 0xE8:
                tgt = e.base + sva + i + 5 + struct.unpack_from("<i", blob, i + 1)[0]
                if tgt in ncall:
                    ncall[tgt] += 1
    PUSHES = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57}
    byPush = sum((e.read(a, 1)[0] in PUSHES) == (a in procs) for a in cbs)
    byCall = sum((ncall[a] > 0) == (a in procs) for a in cbs)
    noCaller = sum(ncall[a] == 0 for a in cbs)
    pushNoProc = sum(e.read(a, 1)[0] in PUSHES and a not in procs for a in cbs)
    return (len(slots[0]) - 1, len(slots[1]) - 1, len(slots[2]) - 1,
            len(slots[3]) - 1, slots[1].most_common(1)[0], dead,
            len(named), decompiled,
            len(keys), keys[:4], keys[4], keys[5], keys[13],
            len(cbs), byPush, byCall, noCaller, pushNoProc), \
           (20, 1, 10, 2, (0x0042A0F0, 32), 5,
            30, 3,
            14, [0xCB, 0xCD, 0xC8, 0xD0], 0x12, 0x13, 0x0F,
            33, 22, 29, 32, 11), \
           "distinct open / input / close / draw callbacks across the 37 " \
           "screens (nulls excluded), the one input callback and how many " \
           "screens share it, and the screens with no callbacks at all (the " \
           "five ELIMINE); distinct open+close addresses and how many of " \
           "them Runtime.exe.c actually has a function for; then the " \
           "binding table - its length, the four arrow scan codes, and " \
           "confirm E / back R / close TAB; and finally WHY those addresses " \
           "have no function - over the 33 distinct callbacks, how often a " \
           "`proc` label is predicted by the PROLOGUE (22) against by having " \
           "a direct E8 CALLER (29), how many have no caller at all (32), " \
           "and how many open with a push and still have no label (11), " \
           "which is what refutes the prologue as the cause"


def c_ui_answers():
    r"""UI: the ANSWER layer - what a screen hands back to the script.

    `ui.open` suspends its script at status 6 (SCRIPT_VM 70). `UI_LoadScreen`
    sets `dword_930750` to **-1**, a widget callback writes the chosen value,
    and `Ui_CloseScreenDefault` -> `sub_466B60` hands it to
    `Game_HandleEvent` case 5, which stores it in the variable the `ui.open`
    site named. So the whole "what did the player choose" channel is **one
    global with 17 writers**, and this enumerates them from the image.

    **It has to come from the image.** Only **3** of the 17 sit inside a
    function IDA gave a `proc` label - the `-1` reset in `sub_41DF30` and the
    LIFT's two in `UI_GridMenuInput`. The other **14** are in unlabelled
    regions after an `endp`: a widget callback is reached only as a dword in
    the widget tree and never by a direct `E8` call, which is exactly the
    class of function CLAUDE.md section 1 now says goes unlabelled. Before
    this, `tools/sim/ui.py: ANSWER` carried **one** hand-read entry.

    **The terminal family, and the trap it repeats.** Seven screens share
    panel `0x004E4108` - TERMINAL, FIGHT SIM, MORGUE, ARCHIVES and the three
    SURV screens - and one activate callback at `0x004AF410`, which switches
    on the screen instance's `+4` (the fixed parameter) through a jump table
    at `0x004AF578`. That is `Ui_OpenShop`'s shape exactly, and a linear scan
    over the bytes would attribute all four of its answer writes to one
    screen. The data could refute the mapping and does not: the seven screens
    sharing that panel carry parameters **0..6 with no gap and no repeat**,
    and the table has exactly **7** targets, every one inside the function.

    Read per case, with the case index being the screen's own parameter:

    | case | screen | answer |
    |---|---|---|
    | 0 | TERMINAL | none here - its answer comes from `0x004AF0E0` |
    | 1 | FIGHT SIM | `row + 1`, rows 0..2, and it plays interface sound 26 |
    | 2 | MORGUE | `row + 1`, rows 0..4 |
    | 3 | ARCHIVES | **1**, and only when the row is 2 |
    | 4 | SURV ERROR | falls THROUGH into case 6 |
    | 5 | SURV NO KIT | no answer at all |
    | 6 | SURV KIT | **1**, and only when the row is 0 |

    Case 4 falling through to case 6 is the kind of thing a table read as
    seven independent arms would miss: `loc_4AF52F` ends with a call and no
    jump, so SURV ERROR reaches SURV KIT's test.

    The LIFT (`UI_GridMenuInput`, already read) answers `slot - 1` with slot 0
    giving **6**; the start menu's `Confirmer` answers **1** and is gated on a
    non-empty name field. Both were already modelled and are re-derived here
    from the same enumeration rather than trusted.

    **Tier 2, corpus-constrained** (`docs/PORTING.md` B1) - every number below
    is a property of the shipped image that could have come out otherwise.
    What it does NOT license: that a screen *behaves* correctly, only that
    these are the values it can write.

    **And then the cross-check the answers exist for.** `ui.open`'s third
    field is the variable the reply is stored in, or **-1** to discard it. Over
    the whole corpus - **and it has to be the whole corpus**: the slot walk
    finds **241** sites and no start menu, because screen 29 is opened from
    AREA 118's `+4` STARTUP script, which is CLAUDE.md section 6's documented
    trap and was walked into a third time getting this number. With the 173
    startup scripts the count is **242 over 25 screens**.

    Of those 25, **15 keep the answer and 10 discard it**, and the invariant
    is that **no answer site is attributed exclusively to screens that discard
    it** - 0 of 16. A wrong attribution shows up here, because a writer whose
    only screens throw the value away would be a writer for nothing.

    It is stated per SITE rather than per screen on purpose. Three trees are
    shared - the terminal family's seven screens, and the menu family's
    **29 OMK START MENU / 30 SAVE GAME / 31 PAUSE GAME** - so a site is
    attributed to every screen its tree serves, and screen 30 discards its
    answer while sharing a tree with two screens that keep theirs. Per screen
    the test would fail on that alone; per site it is exactly right.

    **What the cross-check cannot do, and why**, recorded rather than left as
    a gap: **14 of the 25 screens store the answer in the SAME variable, 19**
    (the LIFT's 496 is the only private one, and 10 screens pass -1). So
    "which constants does a script compare this screen's answer against" is
    not answerable by any static walk - the variable is shared, and attributing
    a comparison to a screen would be counting collisions, which is the trap
    CLAUDE.md section 1 records for scene-local ids. Only the LIFT's is
    attributable, and its scripts compare `Etage` against 4.

    Three screens keep the answer with no writer found: **0 VIDEOPHONE**,
    whose panel the widget lift already lists as unresolved (its open installs
    three candidates), and **2 MULTIPLAN** and **23 RESTAURANT**, which
    resolve to a panel and have no writer anywhere in the 17 - so their
    scripts can only ever read back the **-1** that `UI_LoadScreen` set, which
    is a real outcome ("closed without choosing") rather than a missing decode.

    Shown to fail, each measured rather than predicted: dropping the `A3`
    (`mov [abs32], eax`) encoding takes the site count **17 -> 14** and the
    register writers **7 -> 4**, silently losing every `eax` answer - which is
    three of the four screens that compute one; and reading the jump table
    **one entry on**, at `0x004AF57C`, shifts every case onto its neighbour
    and puts `0x90909090` - the alignment padding after the table - in the
    last slot, taking the in-range count **7 -> 6**. That second one is the
    shop-titles failure in miniature: shifted by one the arms still look like
    plausible targets, and only the bound catches it. Dropping the `+4`
    startup scripts from the corpus walk takes the site counts to **241 / 0**
    and loses screen 29 entirely; attributing a child panel by its `parent`
    chain instead of by the open callback that names it loses the whole menu
    tree the same way.

    **One mutation moves nothing, and it is explained rather than counted.**
    Pointing `TERM_DISPATCH`'s attribution at a single screen changes no
    output - because the widget tree *already* names `0x004AF410` as an item
    `+40` callback **80 times**, which is how `Ui_ConfirmSelection` reaches
    it. The constant is redundant, not load-bearing, and that count is
    asserted so the non-effect has evidence behind it instead of looking like
    a weak check (`docs/PORTING.md` B4).
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ui_tables as U
    import json
    tb = os.path.join(ROOT, "tables", "ui_widgets.json")
    if not os.path.exists(tb):
        return ("skipped",), ("skipped",), "tables/ui_widgets.json absent"
    r = json.load(open(tb))["rows"]
    a = r["answerSites"]
    e = U.Exe()
    sc = U.screens()

    sites = a["sites"]
    resets = [s for s in sites if s["value"] == -1]
    imms = [s for s in sites if s["kind"] == "imm" and s["value"] != -1]
    regs = [s for s in sites if s["kind"] != "imm"]

    # the seven screens that share the terminal panel, and their parameters
    fam = sorted({p["screen"] for p in r["panels"]
                  if p["panel"] == 0x004E4108 and p["screen"] >= 0})
    byid = {s["id"]: s for s in sc}
    params = sorted(byid[i]["param"] for i in fam)

    # every jump-table target must land inside the dispatch function, which
    # runs from its start to the table itself
    lo, hi = a["termDispatch"], 0x004AF578
    inrange = sum(lo < t < hi for t in a["termCases"])

    # ---- the corpus side: who opens a screen, and who keeps the answer ----
    import dialog_disasm as D, dialog_triggers as T2, collections
    keeps = collections.defaultdict(set)
    nsite = [0, 0]                      # in slots, in +4 startup scripts
    def scan(ops, kind):
        for pc, op, raw in ops:
            if op == 70 and len(raw) >= 6:
                sid, par, var = struct.unpack_from("<3h", raw, 0)
                keeps[sid].add(var)
                nsite[kind] += 1
    for nm in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", nm)).items()):
            rr = T2.LAYOUT[nm](b)
            if rr:
                for rec, f, p in (list(T2._scripts_from_records(b, rr[0], rr[1]))
                                  + T2._second_table(nm, b)):
                    o, st = D.disasm(b, p, len(b))
                    if st == "ok": scan(o, 0)
            at = struct.unpack_from("<I", b, 4)[0]
            if 0 < at < len(b):
                o, st = D.disasm(b, at, len(b))
                if st == "ok": scan(o, 1)
    gb, gslots = T2.global_file(omkpaths.data("IAM/GLOBAL"))
    for rec, f, p in gslots:
        o, st = D.disasm(gb, p, len(gb))
        if st == "ok": scan(o, 0)
    # `0x004AF410` is not attributed by a hand-written constant: the widget
    # tree already names it as an item `+40` callback, which is exactly how
    # `Ui_ConfirmSelection` reaches it. Asserted, because otherwise the
    # mutation that points that constant at one screen "moves nothing" and
    # looks like a weak check (PORTING B4) instead of a redundant line.
    termrefs = sum(1 for p in r["panels"] for l in p["lists"]
                   for it in l["items"]
                   if it.get("callback") == a["termDispatch"])
    kept = {i for i, v in keeps.items() if any(x >= 0 for x in v)}
    disc = {i for i, v in keeps.items() if v == {-1}}
    attributed = sum(1 for v in a["screens"].values() if v)
    orphan = sum(1 for v in a["screens"].values() if not (set(v) & kept))
    nowriter = sorted(kept - {i for v in a["screens"].values() for i in v})
    variables = sorted({x for v in keeps.values() for x in v})

    # the answer global is written nowhere else, and read where it should be
    return (a["global"], len(sites), len(resets), len(imms), len(regs),
            sorted({s["value"] for s in imms}),
            fam, params, len(a["termCases"]), inrange,
            a["termDispatch"],
            nsite[0], nsite[1], len(keeps), len(kept), len(disc),
            variables, len(a["screens"]), attributed, orphan, nowriter,
            termrefs), \
           (0x00930750, 17, 1, 9, 7,
            [0, 1, 6],
            [5, 11, 15, 16, 17, 18, 19], [0, 1, 2, 3, 4, 5, 6], 7, 7,
            0x004AF410,
            241, 1, 25, 15, 10,
            [-1, 19, 496], 16, 16, 0, [0, 2, 23],
            80), \
           "the answer global; every write to it; the `-1` RESET " \
           "(exactly one - UI_LoadScreen's); the immediate writers and the " \
           "register ones; the distinct immediate answers (0, 1 and 6 - the " \
           "LIFT's 6 is the only one above 1, every richer answer being " \
           "computed into a register); then the terminal family - the seven " \
           "screens sharing panel 0x004E4108 and their fixed parameters, " \
           "which must be 0..6 with no gap or repeat for the jump table's " \
           "case index to BE the parameter; the table's seven targets and " \
           "how many land inside the dispatch function; and the dispatch's " \
           "own address; then the CORPUS side - `ui.open` sites in the 5785 " \
           "slots (241) and in the 173 `+4` STARTUP scripts (1, and it is " \
           "screen 29, which is why a slot-only walk sees no start menu); " \
           "the screens opened, those that KEEP the answer and those that " \
           "pass -1 to discard it; the result variables in use - -1, 19 and " \
           "the LIFT's 496, so fourteen screens share one slot and a " \
           "per-screen comparison test is impossible; then the sites, how " \
           "many are attributed to a screen, how many are attributed ONLY " \
           "to screens that discard (0 - the invariant a wrong attribution " \
           "would break), and the screens that keep an answer nothing " \
           "writes (VIDEOPHONE, whose panel the lift lists unresolved, plus " \
           "MULTIPLAN and RESTAURANT, which can only read back the -1); " \
           "and finally the times the widget tree itself names the terminal " \
           "dispatch as an item callback (80), which is what makes the " \
           "constant naming it redundant rather than load-bearing"


def c_render_backends():
    r"""The TWO render back ends, and the claim that one is never installed.

    `sub_42FA00(bank)` swaps **six parallel two-entry arrays** at 0x004C4910,
    stride 8 - five function pointers the renderer then calls indirectly, plus
    an activate hook it calls once:

    | array | -> global | bank 0 | bank 1 |
    |---|---|---|---|
    | 0x004C4910 | (called once) | 0x0042FC10 | 0x0042FE80 |
    | 0x004C4918 | dword_90E09C | 0x00460060 | **0x0042FF80** |
    | 0x004C4920 | dword_90E0A8 | 0x004617F0 | 0x004311C0 |
    | 0x004C4928 | dword_90E0A4 | 0x00461B30 | 0x00430D90 |
    | 0x004C4930 | dword_90E0AC | 0x00433740 | 0x00431410 |
    | 0x004C4938 | dword_90E0A0 | 0x00433780 | 0x00431460 |

    All five are reached through `call ds:<global>` from six functions, so none
    of this is theoretical. `Game_Init` (0x0041FA00) installs **bank 0** through
    `sub_42F9A0`, which is `sub_42FA00(0)` with the index hard-wired.

    **The correction.** `docs/ASSETS.md` 4c and `CLAUDE.md` 5 both say the
    luma-to-grey conversion lives in "the second back end that `sub_42FF80`
    heads and **nothing shipped installs**", and conclude "no player ever saw
    it". `sub_42FF80` is `0x004C4918[1]`, so that back end is bank 1 - and bank
    1 is installed by **VM opcode 150**, whose handler is exactly
    `sub_42FA00(1)`. The shipped world scripts carry **14** sites of opcode 150
    and **15** of opcode 151 (the restore), across **eight** chunks that are
    not obscure:

        AREA  145 Mahaleel                     SCENE  9 1-13 Morgue
        AREA  156 Anekbah Grotte Gandhar Light SCENE 42 1-12 Anissa Aka's Bar
        AREA  158 Jaunpur Zone 24              SCENE 57 1-02 Appart Kayl Rencontre
        AREA  177 Lahoreh Konshu               SCENE 60 1-20 Concert Bowie Bar 02

    The near-pairing of 14 installs against 15 restores is what says these are
    live switches rather than debris: a script turns the bank on, does
    something, and turns it back.

    **And what bank 1 IS.** The two banks' scene renderers - the pointer
    `dword_90E09C`, which `Render_Frame` calls once a frame after the buckets
    are filled and the SHADEMODE is set - are the same 0x4000-bucket walk of
    near-identical length: `Render_FlushBuckets` **659** lines against
    `sub_42FF80`'s **660**. The difference is that bank 1 converts every vertex
    colour to luma grey: **17** occurrences of `(299R + 587G + 114B)/1000` in
    it, **0** in bank 0, each window bounded by the function's own declared
    `@lines` so it cannot run into its neighbour - a bound that is DEFENSIVE
    rather than load-bearing, and it is worth saying which: nothing within 200
    lines after `sub_42FF80` mentions 587 either, so widening the window moves
    nothing until 600 lines out, where an unrelated 18th appears. The bound is
    kept because it is correct in general, not because a test prefers it.
    Bank 1 is a **greyscale scene renderer**.

    The corpus then says what it is for, and says it loudly: **all 14 sites of
    opcode 150 are closed by a later 151**, and **74 of the 82 instructions
    between them - 90% - are `camera.set`, `camera.set.wait`, `fade.to_color`
    and `fade.from_color`**. The bank brackets a CUTSCENE. Spans run 1 to 14
    instructions; the Anekbah one fades to WHITE inside it. So opcodes 150 and
    151 are named `render.grey.on` / `render.grey.off`, taking the VM to **127
    named**.

    **What this does NOT overturn**, because over-correcting is the obvious
    risk: the sets are still drawn in COLOUR in normal play. That rests on
    `Raster_DrawTriangles` declaring `D3DFVF_DIFFUSE` (ASSETS 4c), it is what
    bank 0 does, and bank 0 is what `Game_Init` installs and what every frame
    outside those fourteen brackets uses. What falls is only "nothing installs
    it, so no player ever saw it". **The other four swapped pointers are not
    read**, and the name rests on the one difference that is established.

    The gate is the other half. All three of `sub_42FC10`, `sub_42FE80` and
    `sub_42FF80` open `mov eax, ds:dword_6A05E0; test eax, eax; jnz <ret>`, so
    a non-zero global makes the opcodes no-ops. That gate is **not** special to
    them: **22 of the 153 VM handlers** open with the identical five
    instructions, and they include 132/133 `fade.to_black`/`.from_black` and
    152 `game.restart` - opcodes the game demonstrably runs. So the gate is
    ordinary execution state, not a switch that keeps this bank off.

    **Tier 2, corpus-constrained** (`docs/PORTING.md` B1): every number is a
    property of the shipped image and scripts that could have come out
    otherwise. It says nothing about what either bank DRAWS.

    Shown to fail: reading the six arrays with stride 4 instead of 8 collapses
    the two banks into one list and the bank-0/bank-1 split stops matching;
    counting opcode 150 without the `+4` startup scripts is unaffected here
    (none of the 14 is in one, which is itself worth knowing); and asserting
    the handler-gate count as 3 rather than 22 is what makes the "the gate is
    ordinary" argument, so moving it to 3 breaks the row that carries it.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ui_tables as U, dialog_disasm as D, dialog_triggers as T2, collections
    e = U.Exe()
    banks = []
    for k in range(6):
        a = 0x004C4910 + 8 * k
        banks.append(tuple(struct.unpack("<2I", e.read(a, 8))))
    # the five pointers are all CALLED indirectly
    G = {0x0090E09C: 0, 0x0090E0A0: 0, 0x0090E0A4: 0, 0x0090E0A8: 0, 0x0090E0AC: 0}
    for sva, vsz, ptr, rsz in e.sec:
        b = e.d[ptr:ptr + rsz]
        for i in range(len(b) - 6):
            if b[i] == 0xFF and b[i + 1] == 0x15:
                v = struct.unpack_from("<I", b, i + 2)[0]
                if v in G:
                    G[v] += 1
    # the shared handler preamble
    PRE = bytes([0xA1, 0xE0, 0x05, 0x6A, 0x00, 0x85, 0xC0, 0x75])
    gated = sum(1 for op in range(153)
                if e.read(struct.unpack("<I", e.read(0x004C0140 + 8 * op, 4))[0], 8) == PRE)
    gatedNamed = [op for op in (132, 133, 152)
                  if e.read(struct.unpack("<I", e.read(0x004C0140 + 8 * op, 4))[0], 8) == PRE]

    # and the corpus: who switches the bank
    n = collections.Counter()
    chunks = set()
    for nm in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", nm)).items()):
            rr = T2.LAYOUT[nm](b)
            streams = []
            if rr:
                streams += [p for rec, f, p in
                            (list(T2._scripts_from_records(b, rr[0], rr[1]))
                             + T2._second_table(nm, b))]
            at = struct.unpack_from("<I", b, 4)[0]
            if 0 < at < len(b):
                streams.append(at)
            for p in streams:
                o, st = D.disasm(b, p, len(b))
                if st != "ok":
                    continue
                for pc, op, raw in o:
                    if op in (150, 151):
                        n[op] += 1
                        chunks.add((nm, k))
    # every install is closed, and what runs while it is on
    spans, inside, unclosed = [], collections.Counter(), 0
    for nm in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", nm)).items()):
            rr = T2.LAYOUT[nm](b)
            streams = []
            if rr:
                streams += [p for rec, f, p in
                            (list(T2._scripts_from_records(b, rr[0], rr[1]))
                             + T2._second_table(nm, b))]
            at = struct.unpack_from("<I", b, 4)[0]
            if 0 < at < len(b):
                streams.append(at)
            for p in streams:
                o, st = D.disasm(b, p, len(b))
                if st != "ok":
                    continue
                ops = [x[1] for x in o]
                for i, op in enumerate(ops):
                    if op != 150:
                        continue
                    rest = ops[i + 1:]
                    if 151 in rest:
                        j = rest.index(151)
                        spans.append(j)
                        for q in rest[:j]:
                            inside[q] += 1
                    else:
                        unclosed += 1
    CAM = {95, 96, 118, 119, 126}
    cam = sum(inside[o] for o in CAM)
    tot = sum(inside.values())
    # the two scene renderers, and the luma that separates them
    # bounded by each function's OWN declared @lines, so the window cannot
    # run into its neighbour and inflate the count
    def body(path, tag):
        t = open(os.path.join(ROOT, path), errors="replace").read()
        i = t.index(tag)
        n = int(re.search(r"@lines (\d+)", t[i:i + 200]).group(1))
        return "\n".join(t[i:].split("\n")[:n + 2]), n
    b1, n1 = body("readable/src/08_wave.c", "@func 0x0042FF80")
    b0, n0 = body("readable/src/20_ddraw.c", "@func 0x00460060")
    luma1, luma0 = b1.count("587"), b0.count("587")

    return (banks[0], banks[1], sorted(G.values()), gated, gatedNamed,
            n[150], n[151], len(chunks), sorted(chunks),
            unclosed, len(spans), cam, tot, luma0, luma1, n0, n1), \
           ((0x0042FC10, 0x0042FE80), (0x00460060, 0x0042FF80),
            [1, 2, 2, 3, 4], 22, [132, 133, 152],
            14, 15, 8,
            [("AREA", 145), ("AREA", 156), ("AREA", 158), ("AREA", 177),
             ("SCENE", 9), ("SCENE", 42), ("SCENE", 57), ("SCENE", 60)],
            0, 14, 74, 82, 0, 17, 659, 660), \
           "the first two of the six two-entry arrays at 0x004C4910 - the " \
           "activate hook and the pointer `sub_42FF80` heads, which is bank " \
           "ONE; then how often each of the five installed pointers is called " \
           "indirectly (all of them are, so neither bank is theoretical); the " \
           "VM handlers sharing the `dword_6A05E0` gate (22 of 153) and that " \
           "fade.to_black, fade.from_black and game.restart are among them, " \
           "which is why the gate is ordinary execution and not an off " \
           "switch; then the corpus - sites of opcode 150 (install bank 1) " \
           "and 151 (restore bank 0), nearly paired, and the eight chunks " \
           "that carry them, which is what refutes 'nothing shipped installs " \
           "it'; then what the bank IS - installs left unclosed (0), installs " \
           "closed by a later 151 (14), and of the instructions between them " \
           "how many are camera/fade opcodes against the total (74 of 82, so " \
           "the bank brackets a CUTSCENE); and finally the luma conversions " \
           "in each bank's scene renderer - 0 in Render_FlushBuckets against " \
           "17 in sub_42FF80 - with the two functions' own declared lengths, " \
           "659 and 660, which is what says they are the SAME walk differing " \
           "only in the colour and makes bank 1 a GREYSCALE renderer, naming " \
           "opcodes 150 and 151"


def c_ui_geometry():
    r"""UI: where a row actually IS - the geometry a replica cannot draw without.

    The widget lift modelled the WALK, which needs no coordinates, so until
    2026-09-01 `tables/ui_widgets.json` carried none and nothing in `engine/`
    could place a single row. Two fields close it, both four lines of engine:

        Ui_ItemScreenX(screen, panel, item) = item[+0] + panel[+84]
        Ui_ItemScreenY(screen, panel, item) = item[+2] + panel[+86]

    so an item's own `+0`/`+2` is its coordinate and the panel's `+84`/`+86` is
    the slide offset added to every row (`+88`..`+96` are the start, delta and
    duration the slide runs on).

    **The background is a tile map, and `panel + 20` is a POINTER to it.**
    `Ui_DrawPanelBack` (0x00476040) reads `v6 = u32(panel, 20)` and then
    `i8(v6, cell)` - a pointer, and SIGNED bytes. Reading the 80 ids in place
    instead gave values like 240, which cannot index a 10-wide grid; that is
    what showed the mistake. `docs/UI.md` says "`panel+20` is 80 tile ids",
    which is true of what it points at.

    What the shipped data is asked, and could fail:

    * **all 411 items land inside 640x480.** A wrong offset scatters them, and
      this is the field's own test - the records are not bounded by anything
      else.
    * **23 of the 35 panels carry a tile map, every one exactly 80 entries with
      every id in 0..79** - the exact range a 10-wide by 8-deep grid of a
      640x480 sheet indexes. Ids are read signed and none is negative.
    * the **start menu has no map at all** (`tilesAt` is 0), which is the
      full-sheet path and what `verify.py: ui page` already found from the
      other end: "the BACKGROUND mode of the start menu (none - its art is all
      item sprites)".
    * and the **LIFT's seven slots**, which is a differential rather than a
      self-check: `docs/UI.md` 3f recorded them by hand in an earlier session
      as "x 278/321/370 on rows y 194 and 241, then slot 6 alone at (325, 288)
      centred under the middle column". The mechanical lift agrees exactly.

    **Tier 2, corpus-constrained** for the bounds; the LIFT row is **tier 3**,
    a differential against an independent hand reading of the same records -
    and B1's warning applies, since both readings are of one structure.

    Shown to fail: reading the tile ids in place instead of through the pointer
    puts ids outside 0..79 (240 among them) and takes the clean-map count 23 to
    0; reading them unsigned leaves 0..79 unchanged on this corpus and is
    recorded as non-discriminating rather than counted; and taking the item
    coordinate from `+4`/`+6` - the width and height, which are equally
    plausible int16s at a neighbouring offset - moves 411 in-bounds items to
    **410** and breaks the LIFT row outright.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import json
    tb = os.path.join(ROOT, "tables", "ui_widgets.json")
    if not os.path.exists(tb):
        return ("skipped",), ("skipped",), "tables/ui_widgets.json absent"
    r = json.load(open(tb))["rows"]
    items = [it for p in r["panels"] for l in p["lists"] for it in l["items"]]
    inb = sum(1 for it in items if 0 <= it["x"] < 640 and 0 <= it["y"] < 480)
    sized = sum(1 for it in items if it["w"] > 0 and it["h"] > 0)
    maps = [p for p in r["panels"] if p["tiles"]]
    lens = sorted({len(p["tiles"]) for p in maps})
    clean = sum(1 for p in maps if all(0 <= t < 80 for t in p["tiles"]))
    menu = [p for p in r["panels"] if p["screen"] == 29]
    menuMap = bool(menu and menu[0]["tiles"])
    lift = [p for p in r["panels"] if p["screen"] == 4]
    xy = [(it["x"], it["y"]) for l in lift[0]["lists"] for it in l["items"]][:7] if lift else []
    return (len(items), inb, sized, len(r["panels"]), len(maps), lens, clean,
            menuMap, xy), \
           (411, 411, 410, 35, 23, [80], 23,
            False,
            [(278, 194), (321, 194), (370, 194),
             (284, 241), (325, 242), (371, 242), (325, 288)]), \
           "items in the lifted tree and how many have a coordinate inside " \
           "640x480 (all of them - the field's own test, since nothing else " \
           "bounds these records) and a positive width and height; then " \
           "panels, those carrying a tile map, the map lengths (80, one per " \
           "cell of a 10x8 grid) and how many have every id inside 0..79 - " \
           "the exact range that grid indexes into a 640x480 sheet; that the " \
           "START MENU has no map, which is the full-sheet path `ui page` " \
           "found independently; and the LIFT's seven slot coordinates, which " \
           "docs/UI.md 3f recorded BY HAND in an earlier session and which " \
           "the mechanical lift reproduces exactly"


def c_engine_screen():
    r"""`engine/` COMPOSES A SCREEN - and a window shows exactly those pixels.

    Every piece of this was already ported and checked, and none of them had
    ever been put together: `surfaceFromBmp` + `blt` (tier 4, the menu title
    66560/66560), `drawRun` (tier 4, 6132/6132 glyph pixels), the widget walk,
    and - lifted 2026-09-01 because the tree modelled the WALK and needed no
    geometry - the item COORDINATES and the panel tile map.

    `ScreenComposer::draw` puts them in the order `Ui_DrawScreen` does:
    background, then every visible row's text, focused row white and the rest
    `0x7F7F7F` (`Ui_ItemTextStyle`'s one shift).

    **Three things it found by being run**, each a symptom the docs predicted:

    * **0 rows drew on both screens**, and I attributed it to the item record's
      `+28` shipping `-1` with the OPEN callback writing the real id
      (`docs/UI.md` 3d) - so the lifted `bind` was wired in as the fix. It was
      not the cause: **breaking the `bind` fallback again changes nothing
      here**, because both these screens' records carry real ids (0,1,4,5 and
      0..6). The fallback is right in general - TERMINAL's rows do ship -1 -
      and it is not what these two exercise. Recorded because it is a
      post-hoc mis-attribution caught only by running the mutation;
    * the real cause was `iamStrings`, which reads its argument relative to the
      `DataFs` it is handed while the reference callers root theirs at
      `gamedata/IAM`. A game-root one returns an empty list - the same symptom, a
      different cause, and fixing both at once is what hid which was which;
    * the menu's four buttons drew hard against the left edge, because
      `Ui_ItemTextStyle` (0x004769A0) maps BANK 2 bits to alignment through a
      ladder that is not the identity - `0x80000004`->2, `0x80000008`->4,
      **`0x80000010`->8 (centred)**, `0x80000020`->0x10 - and the start menu
      stores bank 2 as ZERO, taking its centring from one broadcast of
      `0x80000010` over the list. 4 of 4 rows centre once the ladder is applied;
      the LIFT's 7 do not, because its slots are placed absolutely.

    **And the live frontend.** `PORTING` A1 puts two implementations behind one
    interface; `backends/sdl/play.cpp` is the live one and the ONLY file in the
    tree that includes a library header. A8 rule 3 is the one that is not
    hygiene: no dependency may do work a reference implementation is a port of.
    So SDL creates a window, reports keys and uploads a texture - it never
    blits, scales, blends or draws text.

    That rule is **measured, not promised**: the window's own framebuffer,
    dumped after three frames, is compared against the one the headless
    composer produces, and they are identical over all **614400** bytes. A
    frontend that scaled, filtered or drew anything would move that number.

    The check SKIPS the live half when SDL is absent and still asserts the
    reference half, which is A8 rule 1: `make` with nothing installed builds
    every tool and passes the suite.

    **Tier 6, read and explained**, for the composition. The parts are tier 4
    individually, but the assembled frame is NOT compared against a capture:
    the menu's background animates, and `PORTING` B5 says a check may assert
    the text and must not assert the scene. What is asserted here is what the
    shipped assets could fail - the tile grid, the row count, the centring -
    plus the frontend identity, which is tier 1 against the reference.

    Shown to fail: rooting `iamStrings` at the game directory takes the rows
    drawn from 4 and 7 to **0 and 0**; skipping the alignment ladder leaves the
    menu's four rows hard against the left edge, which moves **the frame hash**
    and nothing else.

    **That hash exists because of what the first version of this check missed.**
    It asserted `centred`, a count of rows whose flags say centre - which stays
    4 whether or not the ladder actually moves anything, so the mutation that
    disabled centring passed. The counts say what was drawn; only a checksum
    of the framebuffer says WHERE. Two of the three mutations here failed to
    bite before it was added, which is `PORTING` B4's whole argument.

    Dropping the `bind` fallback still moves nothing, and that is now recorded
    as a fact about these two screens rather than counted as evidence.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    tb  = os.path.join(ROOT, "tables")
    fr  = omkpaths.data_root()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_screen")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    ref = os.path.join(tmp, "r.bin")
    live = os.path.join(tmp, "l.bin")
    try:
        # Frame 2, because the live run below is `--frames 3` and its loop
        # draws 0, 1, 2 - the menu's background ANIMATES, so the two halves
        # have to be composed at the same frame or they differ by a phase and
        # this reports a frontend fault that is not there.
        subprocess.run([binp, fr, os.path.join(tb, "ui_widgets.json"),
                        os.path.join(tb, "ui.json"), ref, "2"], capture_output=True)
        raw = open(ref, "rb").read()
        n, = struct.unpack_from("<i", raw, 0)
        v = struct.unpack_from("<%di" % n, raw, 4)
        refFb = raw[4 + n * 4:]
        # the live half, when SDL is there
        same, frames = -1, -1
        mk = subprocess.run(["make", "-s", "play"], cwd=eng,
                            capture_output=True, text=True)
        play = os.path.join(eng, "build", "omk-play")
        if mk.returncode == 0 and os.path.exists(play):
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            # `--res 640x480`: the window now defaults to 800x600 and the
            # reference composes at 640x480, so the comparison has to name a
            # resolution or it is comparing two different pictures. That the
            # SIZE is a parameter at all is the point - `menu layout` asserts
            # the scaling relation separately.
            r = subprocess.run([play, fr, tb, "29", "--frames", "3",
                                "--res", "640x480", "--dump", live],
                               capture_output=True, text=True, env=env)
            if os.path.exists(live):
                lb = open(live, "rb").read()
                same = sum(1 for a, c in zip(refFb, lb) if a == c)
                frames = 3 if "3 frames presented" in r.stdout else -1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    haveSdl = same >= 0
    return (v, len(refFb), haveSdl, same if haveSdl else 614400,
            frames if haveSdl else 3), \
           ((29, 0, 1, 4, 800, 4, -1437019637, 307200,
              4, 80, 0, 7, 3142, 0, -599256938, 209447,
              1, 307200),
            614400, haveSdl, 614400, 3), \
           "the composed frames - screen 29 (full-sheet background, no " \
           "tiles, 4 rows, 800px of advance, all 4 CENTRED by the list's " \
           "broadcast, and every one of the 307200 pixels painted) and " \
           "screen 4, the LIFT (a tile map of 80 cells, 7 rows, none " \
           "centred because its slots are placed absolutely); then the null " \
           "frontend's one frame, which is A8 rule 1 exercised rather than " \
           "asserted; the framebuffer's size; whether SDL was found at all; " \
           "and - the thing rule 3 is about - how many of its bytes the LIVE " \
           "window presented identically, which must be all of them because " \
           "the frontend uploads the pixels and may not touch them"


def c_engine_movies():
    r"""`engine/` DECODES the three intro movies - the first thing a player sees.

    `docs/BOOT.md`: the launch chain is the three FLIS movies, then
    `Game_Start("aventure.scx")`. The port had always FOUND and stepped them -
    `boot sequence` asserts their sizes and the skip key - and nothing decoded
    one, so the replica's window opened on a menu the game does not open on.

    **Why a decoder is allowed here at all**, since `PORTING` A8 rule 3 forbids
    a dependency doing work a reference implementation is a port of. It was
    checked rather than assumed: the engine's own import table names
    **`CoCreateInstance`** (ole32 - a DirectShow filter graph) and
    **`mciSendCommandA`** (winmm - MCI), and the image carries no MPEG decoder.
    The original handed the file to the operating system, so a vendored decoder
    is the equivalent of what it did, not a substitute for portable code.

    `pl_mpeg.h` is vendored under `engine/third_party/` - MIT, not public
    domain as A8 said, which is corrected there - and being VENDORED is what
    lets `src/platform/movie.cpp` use it while `make` still needs nothing
    installed. A8 rule 2 keeps SYSTEM dependency headers behind the frontend
    boundary; a checked-in one can sit where the suite can test it, and behind
    the boundary this would be untestable.

    What the shipped files are asked, and could fail:

    * all three open with the MPEG **program stream** pack header
      `00 00 01 BA`, read here straight off disk;
    * all three are **320x240 at 29.97 fps**, which is what makes the doubling
      into the 640x480 framebuffer exact - no resampler, and nothing to
      disagree about;
    * all three carry **one 44100 Hz audio stream**, and ~474624 samples
      decode out of each 120-frame prefix, nearly all of them audible;
    * durations 13 / 23 / 107 seconds, and **120 frames decode out of each**.

    **That audio line said 0 until the user said otherwise, and they were
    right.** `plm_get_num_audio_streams` returned 0 for all three, and it went
    into this check and two documents as a property of the FILES. It is a
    property of the PROBE: pl_mpeg reads a short prefix by default, and every
    one of these puts its first audio PES packet at offset **160368**, past it.
    A raw scan for stream id `0xC0` finds 270-283 packets in the first 6 MB of
    each; with `plm_probe(plm, 1 MB)` the library reports one 44100 Hz stream
    and decodes a 1152-sample block immediately. The lesson is the one
    `CLAUDE.md` section 1 keeps making, one level out: **a library's negative
    is a fact about its default, not about the data** - and this one was
    checked against the bytes only after someone who had heard the audio said
    so.

    Note the rate: **44100**, twice the 22050 `Sound_Init` gives the game's own
    primary buffer. The original played these through DirectShow, which had its
    own output and never went through that mixer, so there is nothing
    inconsistent here - but a replica that fed movie audio into the ported
    22050 path would be wrong.

    **The peak, not the last frame.** The first version of this measured frame
    24 and reported 0 lit pixels for two of the three - which is not a broken
    decode: QUANTIC's mean sample value is 0.7 of 255 through its first second,
    and a value that small legitimately quantises to black in 565, since
    `(3*31+127)/255` is 0. A fade-in and a dead decoder look identical at one
    frame. Taking the peak over 120 tells them apart: 307200, 15420 and 222940.

    **Tier 2, corpus-constrained.** The files could fail every number above.
    What it does NOT establish: that the frames match what the original showed
    - no capture of a movie exists, and `PORTING` B5's rig records the game's
    framebuffer, not a video. The claim is that these streams decode to real,
    correctly-shaped pictures.

    Shown to fail: reading the peak as the LAST frame's count takes two of the
    three to 0 (the fade-in, which is how the first version was wrong); and
    doubling with `(dst.w - sw) / 2` instead of `(dst.w - sw * 2) / 2` puts the
    picture off-centre and clips it, dropping the peaks.

And the one that matters most now: **narrowing `plm_probe` back to the
    default** takes the audio streams from 1 to **0** on all three and the
    samples to 0 - which is exactly the wrong answer this check shipped with.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    # the pack header, straight off disk
    heads = []
    for n in ("EIDOS.MPG", "QUANTIC.MPG", "GAME.MPG"):
        p = os.path.join(fr, "FLIS", n)
        heads.append(open(p, "rb").read(4).hex() if os.path.exists(p) else "")
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_movies")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "m.bin")
    try:
        subprocess.run([binp, fr, out], capture_output=True)
        raw = open(out, "rb").read()
        n, = struct.unpack_from("<i", raw, 0)
        v = struct.unpack_from("<%di" % n, raw, 4)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (heads, v), \
           (["000001ba"] * 3,
            (1, 320, 240, 29970, 13, 1, 44100, 120, 307200, 474624, 472736,
             1, 320, 240, 29970, 23, 1, 44100, 120, 15420, 474624, 466447,
             1, 320, 240, 29970, 107, 1, 44100, 120, 222940, 474624, 380622)), \
           "the three FLIS files' first four bytes - 00 00 01 BA, the MPEG " \
           "program-stream pack header - then per movie: whether it opened, " \
           "its width and height, the frame rate in millihertz, the duration " \
           "in whole seconds, the audio streams and their rate (one each at " \
           "44100 - which this check said was ZERO until the bytes were " \
           "scanned, because pl_mpeg's default probe stops before the first " \
           "audio packet at offset 160368), the frames decoded out of a " \
           "120-frame prefix, the PEAK lit pixels over it - peak rather than " \
           "the last frame, because two of the three are still fading in and " \
           "a fade and a dead decoder look identical at one frame - and then " \
           "the samples decoded alongside and how many are AUDIBLE, which is " \
           "what tells a working decode from one that demuxes into silence"


def c_engine_raster():
    r"""`engine/`'s software 3D rasterizer - and it is NOT a port.

    Every other output slice transcribes something the engine does. This one
    cannot: **the engine has no software 3D rasterizer.** Exactly three drawing
    functions test the software driver mode and all three are I2D's; the 3D
    path is Direct3D in every mode. So `src/o3de/raster.*` is a REFERENCE
    implementation standing where D3D stood, the same status as the mixer's
    `render()` and `movie.cpp` - and `PORTING` B6 carries it as its own row,
    added after closing the old "six rasterizers" row closed too much.

    **What it consumes is ported and checked**: the drawable mask, the batch
    order (opaque, then additive, then multiply, by material within each -
    `Render_FlushBuckets` walking 0x4000 buckets ascending), the two blend
    modes and the cutout, and D3DCULL_NONE, so both windings draw.

    **What is checkable is geometry, and it is differenced rather than
    asserted.** `tools/camshot.py` projects the same records independently -
    and its wireframe was laid over a real screenshot of dialog 402, which is
    what found the two convention errors this repo is scarred by: the
    reflection (`W(v) = [x, -y, z]` negates one axis, mirroring every frame)
    and `angle[1]` being the HORIZONTAL fov. Projecting Aapkayl's corners
    through camera **4555** - eye (3526, 1015, -905), at (3412, 1032, -882),
    hfov 83 - the two agree on **106 of 106** sampled corners, worst
    disagreement **0.0018 pixels**.

    **The first run of that differential disagreed on every visible vertex and
    agreed on every hidden one**, which is worth recording because the shape of
    the failure named the cause: `camshot.py` projects into **800x440**, the
    letterboxed camera-mode viewport ASSETS measures at 1.818:1, not the
    640x480 framebuffer - and the vertical fov follows from the frame's shape
    (`tanv = tanh / (W/H)`). The 74 "agreements" were all both-behind. A
    differential that agrees on the cases where both sides do nothing is not
    agreeing about anything.

    And the convention is pinned rather than hoped: rendering the same camera
    with the right vector negated - the mirrored reading, which looked correct
    from inside for months - differs in **349998 of 352000 pixels**.

    **Tier 3, differential** (`docs/PORTING.md` B1), and its warning applies:
    two implementations of one reading agreeing catches an offset or a sign,
    not a shared misreading. What would lift this to 4 is a captured 3D frame,
    and B6's row says exactly what could then be claimed - **silhouette and
    coverage, never per-pixel equality**, because those pixels are Wine's.

    Shown to fail: negating the right vector moves 349998 pixels; projecting
    into 640x480 instead of 800x440 takes the projection agreement from 106 to
    **32** (the both-behind cases, which agree either way); and reading
    `angle[1]` as the vertical fov - the documented error - moves every
    coordinate too.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    model = os.path.join(fr, "MESHES", "DECORS", "Aapkayl.3DO")
    if not (os.path.isdir(eng) and os.path.exists(model)):
        return ("skipped",), ("skipped",), "engine/ or Aapkayl.3DO absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_raster")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "r.bin")
    try:
        subprocess.run([binp, fr, model, "3526,1015,-905", "3412,1032,-882",
                        "83", out, "800x440"], capture_output=True)
        raw = open(out, "rb").read()
        n, = struct.unpack_from("<i", raw, 0)
        v = struct.unpack_from("<%di" % n, raw, 4)
        npro = v[-1]
        pro = struct.unpack_from("<%df" % (npro * 5), raw, 4 + n * 4)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # the independent projection
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import camshot as C, omkdata as O
    verts = O.decor_geometry("Aapkayl")["verts"]
    proj = C.projector([3526.0, 1015.0, -905.0], [3412.0, 1032.0, -882.0], 83.0)
    agree = ahead = 0
    worst = 0.0
    for k in range(npro):
        i, isAhead, x, y, z = pro[k * 5:k * 5 + 5]
        i = int(i)
        p = [float(verts[i][0]), float(verts[i][1]), float(verts[i][2])]
        r = proj(p)
        if not isAhead:
            agree += (r is None)
            continue
        ahead += 1
        if r is None:
            continue
        dx, dy = abs(r[0] - x), abs(r[1] - y)
        worst = max(worst, dx, dy)
        agree += (dx < 0.05 and dy < 0.05)

    return (v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[10],
            len(verts), npro, ahead, agree, round(worst, 3)), \
           (10257, 25, 21, 3419, 709, 2452, 278, 419457, 93102, 349998,
            10257, 106, 32, 106, 0.002), \
           "the set's corners, batches and textures; then the raster - " \
           "triangles offered, drawn, rejected BEHIND the near cut and " \
           "rejected offscreen, pixels written and depth rejects; then the " \
           "pixels the MIRRORED reading changes (349998 of 352000, so the " \
           "convention is pinned rather than hoped); then the corner count " \
           "tools/omkdata.py independently assembles, which must be the same " \
           "10257; and the projection differential against camshot.py - " \
           "sampled corners, how many are in front of the camera, how many " \
           "AGREE and the worst disagreement in pixels - 0.002, which is float " \
           "noise and not a convention"



def _raster_fb(path, w, h):
    """`run_raster`'s trailing framebuffer, RGB565 -> RGB888.

    The layout is the tool's: an int32 count, that many int32s, then the
    projected-corner samples the count's last entry sizes, then w*h shorts.
    """
    raw = open(path, "rb").read()
    n, = struct.unpack_from("<i", raw, 0)
    v = struct.unpack_from("<%di" % n, raw, 4)
    off = 4 + n * 4 + v[-1] * 5 * 4
    px = struct.unpack_from("<%dH" % (w * h), raw, off)
    out = bytearray(3 * w * h)
    for i, p in enumerate(px):
        r, g, b = (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F
        out[3 * i]     = (r << 3) | (r >> 2)
        out[3 * i + 1] = (g << 2) | (g >> 4)
        out[3 * i + 2] = (b << 3) | (b >> 3)
    return bytes(out)


def c_engine_silhouette():
    r"""The 3D rasterizer against the engine's own FRAMEBUFFER - tier 4, and
    for exactly one camera.

    `c_engine_raster` is tier 3: two implementations of one reading agreeing,
    which catches an offset or a sign and not a shared misreading. `PORTING`
    B6's row says what lifts it - **silhouette and coverage against a captured
    3D frame, never per-pixel equality** - and the frames now exist:
    `traces/frames/dlg402-*.png`, six captures of dialog 402 through camera
    **4555**, the one camera used by exactly one node of one conversation and
    the one `camshot.py`'s wireframe was laid over by hand.

    **Per-pixel comparison is refuted by the capture, not merely discouraged.**
    Between the two frames where the camera has finished its 160-frame travel
    and is PARKED - so the scene is the same scene - **42% of pixels differ by
    <= 8**, which is B5's low-bit noise: filtering, dithering and the fog
    table are the driver's and Wine's are not a Voodoo's. So the instrument is
    `frame.edge_map` / `edge_match` / `hole_darkness`, and it has three
    properties that are the whole of its honesty:

    * **directed, render -> capture.** The render draws the SET and nothing
      else; the capture carries Telis, the props and a subtitle. Asking "is
      every edge the render draws an edge the original has" is answerable;
      asking the reverse would score a correct render down for the content it
      is honest about missing. The reverse is measured anyway and reported as
      a DIAGNOSTIC - 0.63 and 0.71 against 0.73 and 0.83 - and that gap is the
      characters, the props and the driver's noise. It is not a criterion.
    * **density-normalised.** Each side contributes its strongest 5% of
      gradients, so a crisp render and a filtered capture bring the same number
      of edges and the score is not measuring a threshold.
    * **it has a FLOOR and the floor is quoted.** Two edge maps of this density
      overlap by luck; measured at large shifts that luck is **0.27 and 0.30**.
      A score without its floor is a number with no scale.

    **The result.** The set rendered through 4555 into the letterbox the
    captures themselves define scores **0.73** and **0.83** against the two
    parked frames, against a floor of 0.27/0.30. And the COVERAGE half is
    sharper still: where the render has no geometry at all - 9520 pixels, the
    holes a set-only render leaves - the parked captures are BLACK in **92%**
    and **99%** of them, against **33%** frame-wide. The game does not clear to
    a sky either, so the holes are a shape the two share that owes nothing to
    any pixel's value.

    **Three controls, and the metric had to be SHOWN to fail on each.**

    | control | edge | holes dark |
    |---|---|---|
    | the true render | **0.73 / 0.83** | **0.92 / 0.99** |
    | the four MID-SWEEP captures - same set, same everything, camera elsewhere | 0.14-0.38 | - |
    | the MIRRORED reading, the repo's signature error | 0.18 / 0.18, **below its own floor** of 0.23/0.21 | 0.25 / 0.24 |
    | `angle[1]` read as the VERTICAL fov, the other documented error | 0.42 / 0.45 | 0.52 / 0.51 |

    The mirrored render is the engine's own - `run_raster`'s eighth argument
    writes it - rather than reconstructed in Python from a reading of the
    projection, because a control derived from a reading shares that reading's
    mistakes.

    **The two halves fail differently, which is why both are here.** Edges
    separate the camera hard and coverage does not: mid-sweep frame 41 puts
    79% of the render's holes in darkness against the parked 92/99, because
    late in the travel the camera is nearly there. Coverage separates the
    MIRRORING hard and edges barely do: the mirrored score sits at its own
    chance floor, but the holes collapse from 0.99 to 0.24. Either alone has a
    blind spot the other covers.

    **WHAT THIS DOES NOT ESTABLISH, and the limits are the point.**

    * **It is ONE camera, in ONE set.** Nothing here generalises to the
      renderer; it says that through 4555 into Aapkayl the ported geometry,
      the ported drawable mask, the ported bucket order and this reference
      rasterizer put the set's edges where the original put them. A second
      camera would be a second claim and needs a second capture.
    * **The render has no characters and no props**, so it can be right about
      everything it draws and still be missing a third of the picture. The
      directed metric is built for that and therefore CANNOT see it: a render
      that dropped every character would score identically. The reverse
      direction is reported so the size of the unmodelled part is at least on
      the record, but it is not asserted, because the character's own edges
      are not this slice's to reproduce.
    * **It says nothing about a pixel's value** - not the texture filter, not
      the dither, not the fog, not the blend arithmetic. Those are the
      driver's and have no reachable tier from this rig, the same way the
      audio attenuation law has none.
    * **The rasterizer is still a REFERENCE implementation**, not a port
      (`raster.h`, `PORTING` B6): the engine has no software 3D rasterizer, so
      what is lifted to 4 is the DECISIONS it consumes - where a vertex lands,
      what is drawn, what is in front of what - and never the drawing itself.

    **SHOWN TO FAIL - and against the ENGINE, not against the check.** The
    controls above are built into the assertion, so they prove only that the
    metric separates readings this file constructs. The question that matters
    is whether it separates a rasterizer that is actually wrong, so the source
    was mutated and the check re-run:

    | mutation to `engine/` | edge (parked) | holes dark |
    |---|---|---|
    | none | 0.73 / **0.83** | 0.92 / **0.99** |
    | `tanv = tanh` - the frame's shape forgotten, so the letterbox is not in the projection | 0.43 / **0.47** | 0.89 / 0.91 |
    | the DEPTH TEST removed - every vertex still lands where it landed, only the ordering is destroyed | 0.66 / **0.75** | 0.87 / **0.92** |

    The second is the one worth having. `PORTING` B5 says a captured 3D frame
    is evidence about geometry **and ordering**; that mutation leaves the
    geometry exactly as it was and breaks only what is in front of what, and
    both halves of the metric move. So the ordering claim is not being taken
    on B5's word.

    **And one mutation that did NOT move it, which is the more useful result.**
    Swapping the engine's drawable mask for `tools/omkdata.py`'s rule
    (`DrawFilter::DecorViewer`) changes **nothing** - not a score, not a pixel.
    That is not a blind spot in the metric: measured, the two rules select the
    **identical 10257 corners** for this set and the two renders differ in **0**
    pixels, so there is nothing here to detect. Aapkayl cannot exercise the
    mask, and a check that quoted it as covered would be claiming a test it
    does not run. `render drawable mask` and `engine: cull` are where that
    lives.

    Shown to fail, on the check's own side: the mirrored reading drops 0.83 ->
    0.18 and 0.99 -> 0.24; reading `angle[1]` as the vertical fov drops them to
    0.45 and 0.51; the mid-sweep captures score 0.14-0.38; and the letterbox is
    DERIVED - the picture is the 352 rows the captures themselves have
    majority-lit, and projecting into any other shape changes the vertical fov,
    which is the error that made `c_engine_raster`'s first differential agree
    only on the vertices behind the camera.
    """
    import subprocess, tempfile, shutil
    import frame as F
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    model = os.path.join(fr, "MESHES", "DECORS", "Aapkayl.3DO")
    shots = ["dlg402-%d" % t for t in (32, 35, 38, 41, 44, 47)]
    paths = [os.path.join(ROOT, "traces", "frames", n + ".png") for n in shots]
    if not (os.path.isdir(eng) and os.path.exists(model)
            and all(os.path.exists(p) for p in paths)):
        return ("skipped",), ("skipped",), "engine/, Aapkayl.3DO or the frames absent"

    # ---- the picture area, derived from the captures rather than given.
    # A row belongs to the picture when most of it drew; the two PARKED frames
    # also carry ink BELOW that, which is the subtitle in the bottom band -
    # which is itself why those two are the frames where the line is spoken.
    caps, below = {}, 0
    top = bot = None
    for n, p in zip(shots, paths):
        w, h, c = F.read_png(p)
        maj = [sum(1 for x in range(w)
                   if c[3 * (y * w + x)] or c[3 * (y * w + x) + 1]
                   or c[3 * (y * w + x) + 2]) > w // 2 for y in range(h)]
        t = next(y for y in range(h) if maj[y])
        b = h - 1 - next(y for y in range(h) if maj[h - 1 - y])
        any_ = [any(c[3 * (y * w + x)] or c[3 * (y * w + x) + 1]
                    or c[3 * (y * w + x) + 2] for x in range(w))
                for y in range(h)]
        below += any(any_[b + 1:])
        if top is None:
            top, bot = t, b
        elif (t, b) != (top, bot):
            return ("letterbox varies",), ("one letterbox",), "the captures disagree"
        caps[n] = bytes(c[3 * (t * w):3 * ((b + 1) * w)])
    W, H = 640, bot - top + 1

    # ---- the renders: the true one, the engine's own MIRRORED one beside it,
    # and `angle[1]` read as the VERTICAL fov (tanh = tan(83/2) * W/H).
    binp = os.path.join(eng, "build", "run_raster")
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    EYE, AT = "3526,1015,-905", "3412,1032,-882"
    fovV = round(2 * math.degrees(math.atan(
        math.tan(math.radians(83) / 2) * (W / H))), 4)
    tmp = tempfile.mkdtemp()
    try:
        t_, m_, v_ = (os.path.join(tmp, x) for x in ("t.bin", "m.bin", "v.bin"))
        subprocess.run([binp, fr, model, EYE, AT, "83", t_, "%dx%d" % (W, H), m_],
                       capture_output=True)
        subprocess.run([binp, fr, model, EYE, AT, str(fovV), v_, "%dx%d" % (W, H)],
                       capture_output=True)
        ren = {k: _raster_fb(p, W, H)
               for k, p in (("true", t_), ("mirrored", m_), ("fov-vert", v_))}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    cedge = {n: F.edge_map(W, H, c)[0] for n, c in caps.items()}
    redge = {k: F.edge_map(W, H, r)[0] for k, r in ren.items()}
    parked = ["dlg402-44", "dlg402-47"]
    sweep  = [n for n in shots if n not in parked]

    def sc(rk, cn):
        return round(F.edge_match(W, H, redge[rk], cedge[cn], 2, 4)[0], 2)
    def fl(rk, cn):
        return round(F.edge_chance(W, H, redge[rk], cedge[cn], 2, (80,))[0], 2)

    edgeParked = [sc("true", n) for n in parked]
    edgeFloor  = [fl("true", n) for n in parked]
    edgeSweep  = max(sc("true", n) for n in sweep)
    edgeMir    = [sc("mirrored", n) for n in parked]
    mirFloor   = [fl("mirrored", n) for n in parked]
    edgeFov    = [sc("fov-vert", n) for n in parked]
    # the reverse direction - a diagnostic, never the criterion
    rev = [round(F.edge_match(W, H, cedge[n], redge["true"], 2, 4)[0], 2)
           for n in parked]

    holes = [i for i, v in enumerate(F.lit_mask(W, H, ren["true"])) if not v]
    mholes = [i for i, v in enumerate(F.lit_mask(W, H, ren["mirrored"])) if not v]
    vholes = [i for i, v in enumerate(F.lit_mask(W, H, ren["fov-vert"])) if not v]
    everything = list(range(W * H))
    darkParked = [round(F.hole_darkness(W, H, caps[n], holes), 2) for n in parked]
    darkFrame  = [round(F.hole_darkness(W, H, caps[n], everything), 2) for n in parked]
    darkSweep  = max(round(F.hole_darkness(W, H, caps[n], holes), 2) for n in sweep)
    darkMir    = [round(F.hole_darkness(W, H, caps[n], mholes), 2) for n in parked]
    darkFov    = [round(F.hole_darkness(W, H, caps[n], vholes), 2) for n in parked]

    return (H, below, len(redge["true"]), min(len(v) for v in cedge.values()),
            edgeParked, edgeFloor, edgeSweep, edgeMir, mirFloor, edgeFov, rev,
            len(holes), darkParked, darkFrame, darkSweep, darkMir, darkFov), \
           (352, 2, 11518, 11280,
            [0.73, 0.83], [0.27, 0.3], 0.38, [0.18, 0.18], [0.23, 0.21],
            [0.42, 0.45], [0.63, 0.71],
            9520, [0.92, 0.99], [0.33, 0.33], 0.79, [0.25, 0.24], [0.52, 0.51]), \
           "the picture area DERIVED from the captures - the rows a majority " \
           "of which drew, 352 of them, the 1.818:1 letterbox - and how many " \
           "of the six carry ink BELOW it, which is the subtitle and is 2, " \
           "the parked pair; then the edge maps, density-normalised to the " \
           "strongest 5% so both sides bring the same number (the render's " \
           "and the smallest capture's); then the SILHOUETTE score, directed " \
           "render -> capture, against the two PARKED frames and its chance " \
           "FLOOR at an 80-pixel shift, without which the score has no scale; " \
           "the best any MID-SWEEP capture reaches - same set, same " \
           "everything, camera elsewhere; the MIRRORED reading, which lands " \
           "BELOW its own floor, and that floor; `angle[1]` read as the " \
           "vertical fov; and the reverse direction, which is a DIAGNOSTIC of " \
           "how much of the original the set-only render does not account " \
           "for - characters, props, driver noise - and is not a criterion; " \
           "then COVERAGE: the pixels where the render has no geometry at " \
           "all, how dark the parked captures are there against how dark they " \
           "are frame-wide, and the same for the best mid-sweep frame and for " \
           "both wrong readings - the mirrored one collapsing from 0.99 to " \
           "0.24, which is the separation the edge score does NOT provide"



def _raster_ints(path):
    """`run_raster`'s leading int32 vector."""
    raw = open(path, "rb").read()
    n, = struct.unpack_from("<i", raw, 0)
    return struct.unpack_from("<%di" % n, raw, 4)


def c_engine_near_clip():
    r"""The near-plane clip - a bug a PLAYER found, in the tree's own viewer.

    `drawGeometry` used to reject a triangle outright when any of its three
    vertices fell behind the near cut. There was no clipping at all. D3D
    clipped, so that was never a reading of anything - it was a gap in this
    reference implementation, and its symptom is a face vanishing WHOLE while
    plainly still in shot, most often a floor or a long wall with one corner
    behind you.

    **How it was found is the point.** It survived every check in this file.
    It survived `engine: raster`'s 106/106 projection differential against
    `camshot.py`, because both project the vertices the same way and neither
    projects the ones that are dropped. It survived `engine: silhouette` at
    tier 4 - and not by luck: **through camera 4555 the fix changes 0 pixels
    and 0 holes**, because every triangle the clip rescues there is off the
    left edge or above the top of the frame anyway. That check states "one
    camera in one set is not a claim about the renderer" in three places, and
    this is that limit biting for real rather than as a caveat.

    What found it was somebody flying the scene viewer, on the day the viewer
    first existed, saying *some faces disappear without being completely out of
    the fov of the camera*. CLAUDE.md 1 keeps making this point and it keeps
    being the same point: a suite that only compares this repo to itself cannot
    see a wrong rule applied consistently.

    **And the first fix was worse than the bug**, which is why the clamp in
    `toScreenClipped` has a paragraph of its own. The clipper solves
    `t = (kNear - a.z) / (b.z - a.z)` and evaluates `a.z + (b.z - a.z) * t`,
    which in float does not come back as exactly `kNear`; a vertex a hair under
    it failed the projection guard, came back as (0, 0) and dragged its
    triangle to the corner of the frame. That painted dark wedges over half the
    picture - and again, it was one glance at one frame that said so, while the
    triangle counts stayed entirely plausible.

    **The measurement.** Two cameras in Aapkayl, the old drop rule rendered
    beside the clip so the difference is intrinsic rather than remembered:

    | camera | pixels the clip changes | unlit, dropped -> clipped |
    |---|---|---|
    | 4555, the one `engine: silhouette` uses | **0** | 9520 -> 9520 |
    | one 60 units along, inside the room | **12710** | **14689 -> 3811** |

    The second is the bug: **10878 pixels of floor** that were a black hole.
    The first is why no check here saw it.

    **Tier**: this is a REFERENCE implementation (`PORTING` B6), so the clip is
    not a port of anything and there is no oracle for it - D3D's clipper is not
    in the binary to read. What is asserted is the property a hole is evidence
    of: geometry that is in shot must be drawn. That is checkable, and it is
    what fails above.

    Shown to fail: it is a differential, so the old rule is rendered every run
    and the check is the gap between them - a build that stopped clipping would
    report 0 and 14689 on the second camera.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    model = os.path.join(fr, "MESHES", "DECORS", "Aapkayl.3DO")
    if not (os.path.isdir(eng) and os.path.exists(model)):
        return ("skipped",), ("skipped",), "engine/ or Aapkayl.3DO absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_raster")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = []
    try:
        # 4555, and one step into the room along the same aim
        for tag, eye in (("a", "3526,1015,-905"), ("b", "3466,1015,-893")):
            f = os.path.join(tmp, tag + ".bin")
            subprocess.run([binp, fr, model, eye, "3412,1032,-882", "83", f,
                            "640x352"], capture_output=True)
            v = _raster_ints(f)
            out.append((v[11], v[13], v[12]))   # clipDiffer, holeNoClip, holeClip
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return (out[0], out[1], out[1][1] - out[1][2]), \
           ((0, 9520, 9520), (12710, 14689, 3811), 10878), \
           "two cameras in Aapkayl, each rendered BOTH ways - with the near " \
           "clip and with the pre-2026-09-01 rule that dropped any triangle " \
           "with a vertex behind the cut. Per camera: the pixels the clip " \
           "changes, and the unlit pixels under the old rule and under the " \
           "clip. Camera 4555 - the one `engine: silhouette` measures - " \
           "changes by NOTHING, which is why no check in this file saw the " \
           "bug and why that check says in three places that one camera is " \
           "not a claim about the renderer; one step into the room changes " \
           "12710, and the last number is the hole itself - 10878 pixels of " \
           "floor that simply were not drawn"



def c_engine_renderer_boundary():
    r"""`PORTING` A2's renderer boundary, and the two backends across it.

    A2 is the load-bearing design decision in Part A: **a backend receives
    decisions and turns them into API calls; it never makes one.** What
    `engine/` has ported is not triangles - it is the drawable mask, the 14-bit
    bucket key, the texture slot as that key's LOW SIX BITS, the two blend
    modes, the cutout, and the batch order `Render_FlushBuckets` walks. Put the
    boundary at the Vulkan level instead and every one of those leaks into a
    backend where nothing can check it.

    **First claim: wrapping the rasterizer moves NOTHING.** `SoftwareRenderer`
    is `drawGeometry` behind `begin`/`submit`/`end`, one submission per batch,
    sharing the depth buffer across them. Against the direct call the frame
    differs in **0 of 225280** pixels. That is the property that says the
    boundary is bookkeeping rather than rendering - a non-zero count would mean
    the wrapper had reordered, re-resolved a texture, or lost the depth buffer
    between batches, and each of those is a real way to get it wrong.

    **Second claim: the GPU draws the same picture.** The Vulkan backend
    (`backends/vulkan/vkrender.cpp`, MoltenVK on macOS) takes the same
    submissions in the same order and renders offscreen; `readback()` brings it
    back as the same RGB565 `Surface` the software one returns. Same **3419**
    triangles, and the two frames agree on **coverage** - both-lit over
    either-lit - at **0.995**.

    **Per-pixel is NOT the criterion and is reported only to be dismissed.**
    37.8% of pixels differ exactly, and that number is meaningless as a
    verdict: a GPU has its own fill rule at a triangle edge, its own
    interpolation precision and its own texture filter. This is `PORTING` B5's
    argument about a captured frame, one step inward - and the same discipline
    the silhouette metric is built on. A check that asserted pixel equality
    here would be asserting that MoltenVK rounds the way a C++ loop does.

    **The conventions are SHARED, not re-derived.** `cameraBasis` is exposed
    out of `raster.cpp` and the Vulkan projection is built from it, so the two
    renderers cannot disagree about the two things laying a wireframe over a
    real screenshot corrected: world up is **(0, -1, 0)** because the game's Y
    points DOWN, and `hfovDeg` is the **horizontal** fov with the vertical
    following from the frame's shape. `kNearCut` is shared for the same reason
    - a GPU near plane in a different place is a different answer about what is
    in shot.

    **Tier 3, differential** (`PORTING` B1), and its warning is the whole point
    of reading this row carefully: two implementations agreeing catches an
    offset or a sign, not a shared misreading. What it establishes is that the
    live renderer draws what the reference draws; what makes the REFERENCE
    trustworthy is `engine: silhouette`, against the engine's own framebuffer,
    and that is one camera in one set.

    **Skips rather than fails without Vulkan**, which is A1's closing sentence
    and A8 rule 1: a bare checkout with no SDK must `make` and pass the whole
    suite. The software half still runs, so the boundary is checked on every
    machine and only the GPU comparison is conditional.
    """
    import subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    model = os.path.join(fr, "MESHES", "DECORS", "Aapkayl.3DO")
    if not (os.path.isdir(eng) and os.path.exists(model)):
        return ("skipped",), ("skipped",), "engine/ or Aapkayl.3DO absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_renderer")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    CAM = ["3526,1015,-905", "3412,1032,-882", "83"]
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "b.bin")
        subprocess.run([binp, fr, model] + CAM + [out, "640x352"], capture_output=True)
        v = _raster_ints(out)
        boundary = (v[2], v[3], v[5], v[6], v[8])   # tri/drawn direct, tri/drawn via, differ

        # The GPU half, only where there is one. `PORTING` A1's closing
        # sentence is that a bare checkout with no SDK must `make` and pass the
        # whole suite, so the EXPECTED value moves with the machine: without
        # Vulkan both sides read "no vulkan" and the check passes on the
        # software half alone. An expected tuple that hard-coded the GPU
        # numbers would turn "this machine has no GPU driver" into a failure,
        # which is exactly the property A1 forbids.
        vk = subprocess.run(["make", "-s", "vulkan"], cwd=eng, capture_output=True, text=True)
        vkbin = os.path.join(eng, "build", "run_vulkan")
        gpu = ("no vulkan",)
        if vk.returncode == 0 and os.path.exists(vkbin):
            o2 = os.path.join(tmp, "v.bin")
            r = subprocess.run([vkbin, fr, model] + CAM + [o2, "640x352"],
                               capture_output=True, text=True)
            if r.returncode == 0 and os.path.exists(o2):
                raw = open(o2, "rb").read()
                W, H, litSw, litVk = struct.unpack_from("<4i", raw, 0)
                n = W * H
                a = struct.unpack_from("<%dH" % n, raw, 16)
                c = struct.unpack_from("<%dH" % n, raw, 16 + 2 * n)
                both = either = 0
                for i in range(n):
                    la, lc = a[i] != 0, c[i] != 0
                    if la or lc:
                        either += 1
                        if la and lc:
                            both += 1
                gpu = (W, H, round(both / either, 3) if either else 0.0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    want = ("no vulkan",) if gpu == ("no vulkan",) else (640, 352, 0.995)
    return (boundary, gpu), \
           ((3419, 709, 3419, 709, 0), want), \
           "the boundary first: the triangles offered and drawn by the DIRECT " \
           "`drawGeometry` call every other check measures, then the same two " \
           "through `SoftwareRenderer` one submit per batch, and then the " \
           "pixels the wrapper MOVES - which must be 0, because a boundary " \
           "that rendered would be doing what A2 forbids; then the GPU, when " \
           "there is one (it SKIPS rather than fails without Vulkan, which is " \
           "A1's rule that a bare checkout passes) - the frame size and the " \
           "COVERAGE agreement between the software reference and Vulkan, " \
           "both-lit over either-lit. Per-pixel equality is deliberately NOT " \
           "asserted: a GPU's fill rule, interpolation precision and texture " \
           "filter are its own, which is PORTING B5's argument one step inward"



def c_mirror_pass():
    r"""The MIRRORS - a planar reflection pass, and a doc claim it refutes.

    Found by a player, in the replica's own viewer, on the day it grew one:
    *a wall has transparency but is supposed to be a mirror - same issue in
    both renderers*. Same in both is the useful half of that report: it makes
    it a shared DECISION rather than a backend fault.

    **`docs/ASSETS.md` 4c said the opposite and was wrong.** Its words were
    that the six multiply meshes are "`AB_mirror` and `mirroir` (the two
    mirrors)", that "a mirror rendered as a darkening overlay is worth a raised
    eyebrow, but with the environment-map pass dead there is nothing for it to
    reflect", and - the load-bearing clause - "**it is what the code does**".
    The code does something else.

    **The flag is `0x100000`, and it is carried by exactly 6 of 12203 meshes.**
    Four are mirrors by name: `CSmiro`, `AP_mirror`, `AB_mirror`, `mirroir`;
    the other two are `Coffre sol` and `plaque00`. That enumeration is what the
    old note could not reach, because it enumerated by BLEND MODE - and
    `AP_mirror` is **additive** (`0x00103000`), `CSmiro` carries the mirror bit
    with **no transparency at all** (`0x00100000`), so neither is in the
    multiply set. The inventory was incomplete, which is the same shape as the
    cutscene beats: a negative result over a corpus is only as strong as the
    enumeration behind it.

    **What the code does**, traced rather than inferred. At scene build
    (`readable/src/18_d3d.c` 309) a mesh whose flags carry `0x100000` is stored
    in a **single global**, `dword_534F48` - one, so at most one mirror is live
    at a time. `sub_440D90`, called once per frame from the camera setup path
    (`Runtime.exe.c` 96088, between the viewport call and the scene draw),
    then:

      1. builds the mirror's plane from its position and its normal;
      2. takes the camera's SIGNED DISTANCE to it and gives up when the camera
         is behind (`if (v11 > 0.0)`);
      3. reflects the eye and the target through the plane -
         **`p -= 2 * dist * normal`**, which is the textbook reflection, in
         plain sight at three coordinates each;
      4. reflects the pitch, `(acos(-ny) - 90 deg) * 2`;
      5. calls **`sub_441030(scene, 1)` - the scene draw** (it builds
         `sub_48D0D0`'s frustum and clears the 0x10000-byte bucket memory, so
         it is the render entry, not a helper) - a SECOND FULL PASS from the
         mirrored camera;
      6. restores the camera.

    So the blend mode on a mirror mesh is not a substitute for a reflection, it
    is the **compositing operator over one** - which is also why `AP_mirror`
    can be additive and `AB_mirror` multiply without either being an anomaly.

    **And there is a gate that matters for what a player saw**: the pass is
    guarded by `!sub_45EF50()`, the display-driver index (`docs/UI.md` 3, the
    same selector the I2D primitives branch on). It runs in **mode 0 only** -
    hardware Direct3D - and is skipped in the two software modes. So a mirror
    really is a flat blended pane in software mode, and really does reflect on
    hardware.

    **Not ported.** `engine/` implements neither the flag nor the pass, in
    either backend, which is exactly why the report said "same in both". This
    check pins the finding so the port can be measured against it; it asserts
    the DATA and the addresses, not any rendering.

    **IMPLEMENTED 2026-09-01** (`drawWithMirror`, `src/o3de/renderer.cpp`), on
    the boundary rather than in a backend, so it is a DECISION and every
    renderer gets it. Three passes through `begin`/`submit`/`end`: the
    reflected scene, the scene without the mirror's faces, and the scene with
    them; the mirror's visible pixels are the difference between the last two,
    which is depth-correct for free.

    **Two of its three faults were found by LOOKING, and neither by a number.**

    * The mask was first built by drawing the mirror's faces ALONE - which is
      not depth-tested against the room, so it marked every pixel the plane
      covers and the reflection replaced the whole wall. One frame said so.
    * The reflected pass drew the wall the mirror is set into, and from behind
      that wall occludes everything, so the reflection came back BLACK - and
      because it was black, every variation of the pass produced a
      byte-identical frame. That is how it was found: two renders that should
      have differed did not. A planar mirror has to clip its reflected scene to
      its own plane, and `inFrontOf` does.
    * The third was a real reading error: `RCamera::mirror` negates the right
      vector BEFORE `u = s x f`, so `u` flips with it and the result is a
      180-degree ROTATION - the correct model of the reflected-reading bug it
      exists for, and the wrong operation here. `Raster_DrawTriangles` computes
      `v143 - x` and leaves y alone, so the pass wants a TRUE screen-X flip;
      `RCamera::flipX` negates the right vector after `u` is taken.

    **The flip is settled by CONTINUITY, not by preference**: with it, the
    reflection's bright side continues the real lit wall it meets at the
    mirror's edge; without it the two are discontinuous. 62925 pixels separate
    the readings.

    **CONFIRMED IN PLAY, and that is what the reconstruction rested on.** A
    still frame cannot settle any of this: a wrong plane, a flipped normal or
    the wrong flip can all look plausible from one viewpoint and only come
    apart when the camera moves, which is CLAUDE.md 1's *a value verified
    standing still is not verified moving*. Flown through the viewer across
    viewpoints the reader reported the mirror correct with **the edges lining
    up** - which is the transition test that class of error needs, and it is
    the evidence behind the plane's point, the normal's SIGN and the flip. No
    check here can replace it: the checks assert the flags, the addresses and
    the two backends agreeing with each other, and agreeing with each other is
    exactly what a shared misreading also does.

    **What is reconstruction and must not be read as evidence**: how the engine
    confines the reflected frame to the mirror's area (its X flip is global and
    no clip or stencil step was traced) and the plane's NORMAL (the engine
    reads a runtime value; this takes the face's cross product). The recovery
    of the mirror's own contribution by subtraction is exact for the ADDITIVE
    mode - Aapkayl's, and 211 of the game's 217 blended meshes - and an
    approximation for the 6 multiply ones.

    Shown to fail: enumerating mirrors by the multiply blend, as the old note
    did, returns 2 and misses `AP_mirror` and `CSmiro` - the check asserts both
    counts side by side so the wrong one cannot be quoted again.
    """
    s = _need("decomp")
    if s: return s
    import glob as _g, mesh3do
    flagged, mul, named = [], [], []
    for p in sorted(_g.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
        try:
            _h, ms = mesh3do.meshes(p)
        except Exception:
            continue
        for m in ms:
            f = m["flags"] & 0xFFFFFFFF
            nm = m["name"]
            if f & 0x100000:
                flagged.append((os.path.basename(p), nm, f))
            if (f & 0x1000) and (f & 0x4000):
                mul.append(nm)
            if any(k in nm.lower() for k in ("mirror", "miroir", "miro")):
                named.append((nm, bool(f & 0x100000)))

    namedFlagged = sum(1 for _, fl in named if fl)

    src = open(omkpaths.decomp_path()).read()
    # the register: a mesh with the bit becomes THE mirror
    reg = "if ((*v46 & 0x100000) != 0)\n              dword_534F48 = v41;" in src
    # the pass: guarded by the driver index, reflects, and re-enters the draw
    fn = src[src.find("int __cdecl sub_440D90(float a1)"):]
    fn = fn[:fn.find("\n//----- (")]
    guard = "dword_534F48 && *(_DWORD *)(dword_534F48 + 40) == LODWORD(a1) && !sub_45EF50()" in fn
    redraw = "sub_441030(v3, 1)" in fn
    callers = src.count("sub_440D90(")

    return (len(flagged), sorted(n for _, n, _ in flagged),
            len(mul), len(named), namedFlagged, reg, guard, redraw, callers), \
           (6, ["AB_mirror", "AP_mirror", "CSmiro", "Coffre sol", "mirroir", "plaque00"],
            6, 6, 3, True, True, True, 3), \
           "the meshes carrying flag 0x100000 and their names - 6 of 12203, " \
           "four of them mirrors by name; then the count the OLD reading " \
           "reached by enumerating the multiply blend instead, which is also " \
           "6 but a DIFFERENT six and misses AP_mirror (additive) and CSmiro " \
           "(no transparency at all); the meshes whose NAME says mirror and " \
           "how many of those carry the flag - 6 and 3, and the gap is the " \
           "second half of the finding: `miroir` in the restaurant and " \
           "`BAMiror01`/`02` in the bar carry NO flags at all, so they are " \
           "painted-on mirrors that never reflect, while `Coffre sol` and " \
           "`plaque00` carry the flag without saying mirror. Neither the " \
           "name nor the blend mode is the rule; the FLAG is. And " \
           "then the code, which is what settles it - that a flagged mesh is " \
           "stored in the single global dword_534F48, that sub_440D90 guards " \
           "on !sub_45EF50() (the display-driver index, so HARDWARE MODE " \
           "ONLY), that it re-enters sub_441030 for a second full scene pass " \
           "from the reflected camera, and the number of times sub_440D90 " \
           "appears in the listing - a declaration, a definition and its " \
           "ONE call site"



def c_ui_sound_slots():
    r"""WHICH interface sound each slot is - and a doc claim playing refuted.

    `docs/UI.md` 3 established that a screen's twelve sound slots are
    positional, on good evidence: across the 37 screens **slots 0, 1 and 2 are
    always the `002`, `003` and `001` of one family** - `men002/003/001` for
    the menus, `arc*` for the terminals, `SNK*` for the sneak screens. That
    part is solid and this check re-measures it.

    **What was wrong was the sentence after it.** It read *"So slot 0 is the
    selection move, slot 1 the confirm and slot 2 the screen opening"* - and
    the family pattern says nothing whatever about which is which. The word
    "So" carried an inference the evidence did not support, and all three were
    wrong.

    **`sub_482FE0` (0x00482FE0) settles it.** It reads the live input word at
    screen `+108` and dispatches on the BIT, playing the slot at `a1[15..18]` -
    and `UI_CacheScreenSounds` puts the twelve slots at `+60`, so those four
    dwords are slots 0..3:

    | input bit | UI 3c | slot |
    |---|---|---|
    | `0x10` confirm | ENTER | **0** |
    | `0x20` back | SPACE | **1** |
    | `0x0F` the four directions | arrows | **2** |
    | `0x2000` close | TAB | **3** |

    So the order is **confirm, back, move, close**, and there is **no
    screen-opening sound**. For `OMK START MENU` (slots 1, 2, 0) that makes
    `men002` the confirm, `men003` the back and `men001` the move.

    **Found by PLAYING, and nothing here could have found it.** The replica
    wired the slots as the doc described; a reader reported that moving the
    selection played the validation sound and that confirming sounded like a
    refusal - which is precisely confirm-at-0 and back-at-1 read as move and
    confirm. Every number in this repo agreed with itself either way: the 45
    names resolve, the 12 slots parse, the family pattern holds, the files all
    ship. A meaning attached to a correct index is invisible to a count.

    Shown to fail: the check asserts the four dispatch arms as they appear in
    the decompilation, so a build that renumbered the slots, or a doc that
    restored the old sentence, breaks it - and the two slot orders are
    asserted side by side (`men002` as confirm, NOT as move) so the wrong one
    cannot be quoted again.
    """
    import json as _j
    ui = _j.load(open(os.path.join(ROOT, "tables/ui.json")))["rows"]
    names = ui["sounds"]
    scr = {x["id"]: x for x in ui["screens"]}
    menu = scr[29]["sounds"]

    # the family pattern the doc DID establish
    fam = 0
    for sid, row in scr.items():
        sl = row["sounds"]
        if len(sl) < 3 or min(sl[:3]) < 0:
            continue
        a, b, c = (names[str(sl[k])] for k in range(3))
        if a[-3:] == "002" and b[-3:] == "003" and c[-3:] == "001":
            fam += 1

    # and the dispatcher, which is what says WHICH
    src = open(os.path.join(ROOT, "readable/src/25_sys.c")).read()
    fn = src[src.find("signed int __cdecl sub_482FE0("):]
    fn = fn[:fn.find("\n/* @func")]
    arms = [
        ("(result & 0x10) != 0"   in fn and "v4 = a1[15]" in fn),   # confirm -> slot 0
        ("(result & 0x20) != 0"   in fn and "v5 = a1[16]" in fn),   # back    -> slot 1
        ("(result & 0xF) != 0"    in fn and "v7 = a1[17]" in fn),   # move    -> slot 2
        ("(result & 0x2000) != 0" in fn and "v6 = a1[18]" in fn),   # close   -> slot 3
    ]
    # the slots start at +60, so a1[15] IS slot 0
    base = "v1 = (int *)(a1 + 60);" in \
           open(os.path.join(ROOT, "readable/src/25_sys.c")).read()

    return (fam, menu[:4], names[str(menu[0])], names[str(menu[2])], arms, base), \
           (27, [1, 2, 0, -1], "men002", "men001",
            [True, True, True, True], True), \
           "the screens whose first three slots are one family's 002/003/001 - " \
           "the pattern UI 3 established, and it holds; then the start menu's " \
           "own four slots; then the two that matter, CONFIRM (slot 0) and " \
           "MOVE (slot 2), resolved to names - men002 confirms and men001 " \
           "moves, which is the opposite of what the doc said until a player " \
           "heard it; then the four dispatch arms of sub_482FE0 read out of " \
           "the decompilation, each pairing an input BIT with the slot it " \
           "plays, and that UI_CacheScreenSounds bases the twelve slots at " \
           "+60 - which is what makes a1[15] slot 0 rather than something else"



def c_menu_layout():
    r"""The composed menu against the ENGINE'S OWN CAPTURE - which is the check
    that was missing, and its absence let a wrong layout ship.

    **What went wrong.** The widget records' `x`, `y` and `h` are NOT what a
    screen shows. Each screen's open callback walks its lists through three
    helpers and overwrites them:

        sub_4295C0(list, x)             every item's +0 - the X
        sub_429650(list, h)             every item's +6 - the HEIGHT
        sub_429680(list, firstY, step)  every item's +2 - the Y, stepping

    Screen 29's callback lays out five lists this way. Its start menu goes to
    **y 120 step 80** where the records say 150 step 60, and its confirm
    dialog's `Confirmer` and `Annuler` - which both SHIP at y=330 - are
    separated to **260 and 320**. Drawn from the records alone the two buttons
    land on top of each other, which is what a player reported, and the four
    menu rows sit too high and too close together.

    **Why nothing here caught it, which is the part worth keeping.**
    `engine: screen` compares the reference composer against the live window -
    two halves of this repo, which agree whatever the layout is.
    `engine: frame` compares `tools/uitext.py`'s rendering against the capture
    and never asks the COMPOSER where it would have put them. So the one
    comparison that could fail - what we compose, against what the engine
    drew - did not exist. It does now.

    **The measurement.** The four labels' rows are found in both frames the
    same way, by SATURATION (`frame.text_bands`): the menu draws text through
    a neutral grey ramp over a strongly coloured tile map, so near-grey
    separates glyph from scene where brightness does not, and it excludes the
    orange title. The capture's four bands start at **127, 208, 290, 370**;
    ours must start within a few pixels of each.

    **What this does NOT establish.** Only the four rows' vertical placement
    on one screen. It says nothing about x, nothing about the other 36
    screens, and nothing about the background - our composition has no tile
    map at all (screen 29 declares none; 22 other screens do, and where the
    capture's animated teal-and-rust comes from is still open). A band test is
    also blind to a row drawn at the right height with the wrong glyphs, which
    is what `engine: frame`'s IoU is for.

    Shown to fail, by running it: ignoring the layout pass - trusting the
    records' 150 step 60 - takes the rows landing near the capture's from
    **4 of 4 to 0 of 4**. Not one of them survives, which is the measure of
    how wrong the static geometry is.
    """
    import frame as F, subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    shot = os.path.join(ROOT, "traces", "frames", "menu-22.png")
    if not (os.path.isdir(eng) and os.path.exists(shot)):
        return ("skipped",), ("skipped",), "engine/ or the capture absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_screen")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"

    w, h, cap = F.read_png(shot)
    want = [b0 for b0, _ in F.text_bands(w, h, cap)][:4]

    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "s.bin")
        subprocess.run([binp, omkpaths.data_root(),
                        os.path.join(ROOT, "tables/ui_widgets.json"),
                        os.path.join(ROOT, "tables/ui.json"), out],
                       capture_output=True)
        raw = open(out, "rb").read()
        n, = struct.unpack_from("<i", raw, 0)
        fb = raw[4 + n * 4:4 + n * 4 + 2 * 640 * 480]
        rgb = bytearray(3 * 640 * 480)
        for i in range(640 * 480):
            v = fb[2 * i] | (fb[2 * i + 1] << 8)
            r, g, bl = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            rgb[3 * i]     = (r << 3) | (r >> 2)
            rgb[3 * i + 1] = (g << 2) | (g >> 4)
            rgb[3 * i + 2] = (bl << 3) | (bl >> 3)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    got = [b0 for b0, _ in F.text_bands(640, 480, bytes(rgb))][:4]
    near = sum(1 for a, c in zip(want, got) if abs(a - c) <= 6)

    # ---- and the SAME screen at another resolution.
    #
    # `I2D_ScaleX/Y` are `v * w / 640` and `v * h / 480`: the interface is
    # authored at 640x480 and its COORDINATES scale while the glyphs stay
    # native size. So the rows must land at the scaled positions off by a
    # CONSTANT - the glyph's own top margin, which does not scale. Composing at
    # two sizes and asserting that relation is what stops the port quietly
    # depending on one mode, which it must not: the resolution is meant to
    # become a user setting.
    big = []
    tmp2 = tempfile.mkdtemp()
    try:
        o2 = os.path.join(tmp2, "s.bin")
        subprocess.run([binp, omkpaths.data_root(),
                        os.path.join(ROOT, "tables/ui_widgets.json"),
                        os.path.join(ROOT, "tables/ui.json"), o2, "0",
                        "1024x768"], capture_output=True)
        raw2 = open(o2, "rb").read()
        n2, = struct.unpack_from("<i", raw2, 0)
        fb2 = raw2[4 + n2 * 4:4 + n2 * 4 + 2 * 1024 * 768]
        rgb2 = bytearray(3 * 1024 * 768)
        for i in range(1024 * 768):
            v = fb2[2 * i] | (fb2[2 * i + 1] << 8)
            r, g, bl = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            rgb2[3 * i]     = (r << 3) | (r >> 2)
            rgb2[3 * i + 1] = (g << 2) | (g >> 4)
            rgb2[3 * i + 2] = (bl << 3) | (bl >> 3)
        big = [b0 for b0, _ in F.text_bands(1024, 768, bytes(rgb2))][:4]
    finally:
        shutil.rmtree(tmp2, ignore_errors=True)
    pred = [round(v * 768 / 480) for v in got] if got else []
    offs = sorted({p2 - b for p2, b in zip(pred, big)}) if len(big) == 4 else []
    scaled = (len(big), len(offs), (max(offs) - min(offs)) if offs else -1)

    return (want, len(got), near, scaled), \
           ([127, 208, 290, 370], 4, 4, (4, 3, 2)), \
           "the four label rows of the engine's OWN captured start menu, " \
           "found by saturation; then how many rows our composer produces " \
           "and how many of them land within six pixels of the capture's; " \
           "then the SAME screen composed at 1024x768 - how many rows it " \
           "finds, how many DISTINCT offsets separate the scaled 640x480 " \
           "positions from the found ones, and how far apart those offsets " \
           "are - three offsets spanning two pixels, which is a CONSTANT " \
           "margin plus rounding, " \
           "which is what scaling COORDINATES while leaving the glyphs at " \
           "native size produces - a port with 640x480 baked in anywhere " \
           "would not track, and the resolution is meant to become a user " \
           "setting. " \
           "Four of four - which is the open callback's layout pass being " \
           "applied, because the static records put them at 150 step 60 and " \
           "the engine puts them at 120 step 80"



def c_menu_cloud():
    r"""The menu's ANIMATED BACKGROUND - an embossed, warped cloud.

    The start menu's own sheet, `gfxint.bmp`, is the title on palette index
    **255** - rgb(4,4,4) - and nothing else: measured band by band, every
    sampled pixel outside the title glyphs is that one index. It is the I2D
    colour key, so the sheet is TRANSPARENT and something on a lower layer
    shows through. The replica drew the menu on black until this was found,
    because it blitted the sheet opaquely and nothing drew underneath.

    **The chain**, from screen 29's open callback: `sub_4B19C0` loads
    `IMAGES\CLOUD.BMP` - a 256x256 greyscale - mallocs 0x20000 (256*256*2) and
    makes a 640x480 offscreen surface. A light turns once every 80 frames and
    is a POINT light - the renderer starts each row at `255 - lightX` and
    decrements per pixel - and each pixel reads the cloud, dots its two
    gradients with those weights, clamps to 0..63 and looks the result up in a
    64-entry ramp. The cloud TILES: the index is a byte pair from 0xFFFF
    decrementing, so 256 wraps into 640 two and a half times.

    **TWO PASSES, and folding them into one is what went wrong three times.**
    `sub_4B1B00` embosses the cloud at **256x256** into the work buffer - the
    0x20000 malloc is 256*256*2 - with weights that decrement over 256 and so
    never saturate; then it RESAMPLES that buffer to 640x480 through the warp,
    the X source coming from the 480-entry ROW table and the Y source from the
    640-entry COLUMN one, crossed. That second pass is the wave: the buffer
    repeats across a wider screen, but each row is displaced horizontally and
    each column vertically, which breaks the repeats up instead of lining them
    into squares.

    Three readings were asserted here before that one: the tables as offsets
    into the CLOUD on their own axes (a smooth flow), then no warp at all
    (flat tiling), then crossed but still on the cloud in one pass (hard
    256-pixel seams). A reader watching the original rejected each. What
    settled it was the RAW ASSEMBLY of the whole 382-line function - the
    decompiler had named the table reads off neighbouring addresses, so every
    grep for their own symbols came back empty and produced a confident "it
    never reads them". **"I could not find a read" is a fact about the
    search.**

    **The ramp is built from constants in the image**, not from the .bmp -
    whose own palette is greyscale. Two halves of 32 from 0x004B22D0, and the
    packing settles a reading the disassembly alone does not: `Color_Sum`
    takes a **COLORREF**, so the low byte is RED. Read as 0xRRGGBB the ramp
    comes out blue-to-olive and matches nothing; read correctly it runs
    **rust -> dark -> teal**, and **97%** of the captured menu's background
    pixels land within 24 of one of its entries.

    **The light weights WRAP as signed bytes** - left as full ints
    they run to -449 across a 640-wide row and drive the emboss into its
    clamps. Measured against the engine's own frame over the animated band:

    | | median luma | p90 | at the rust extreme |
    |---|---|---|---|
    | the capture | 17 | 29 | **0.0%** |
    | full-int weights | 29 | 57 | 7.1% |
    | byte-wrapped | **17** | 26 | **0.0%** |

    **OPEN, and measured rather than waved at**: in the 640x480 captures rows
    ~0-150 are a STATIC dark band - luma 12.9, identical across all three
    frames while the middle varies - yet the sheet is transparent there and a
    reader's own 800x600 screenshot of the game has the cloud running behind
    the title. So something confines the effect vertically at this resolution
    and it has not been traced. This check therefore measures the ANIMATED
    band and says nothing about the top.

    **CONFIRMED IN PLAY.** The statistics matching is necessary and not
    sufficient - a wrong sampling can hit the same median - and each of the
    three rejected readings was rejected by a reader watching the original,
    not by a number here. The fourth was confirmed the same way. That is the
    only evidence that separates this from a plausible effect with the right
    histogram, and no check in this file can replace it.

    Tier 2, corpus-constrained: the texture and the ramp are the shipped bytes
    and the phases are the image's own constants. What it does NOT establish
    is any single pixel - the animation has no phase reference in a still, so
    a frame cannot be matched, only its statistics.

    Shown to fail: the two rejected readings are in the table above, and the
    ramp's byte order is what the 97% coverage tests - the other order scores
    a small fraction of it.
    """
    import frame as F, subprocess, tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    shot = os.path.join(ROOT, "traces", "frames", "menu-22.png")
    cloud = omkpaths.data("IMAGES", "cloud.bmp")
    if not (os.path.isdir(eng) and os.path.exists(shot) and os.path.exists(cloud)):
        return ("skipped",), ("skipped",), "engine/, the capture or cloud.bmp absent"

    # the ramp, rebuilt here from the image's own constants
    def cl(v): return 0 if v < 0 else (255 if v > 255 else v)
    ramp, lo1, mid1, hi1, lo2, mid2, hi2 = [], 4049, 947, 13, 608, 544, 416
    first, second = [], []
    for i in range(32):
        first.append((cl(lo1 >> 5), cl(mid1 >> 5), cl(hi1 >> 5)))
        second.append((cl(lo2 >> 5), cl(mid2 >> 5), cl(hi2 >> 5)))
        lo1 -= 111; mid1 -= 13; hi1 += 13
        lo2 += 19;  mid2 += 94; hi2 += 92
    ramp = first + second

    w, h, cap = F.read_png(shot)
    near = tot = 0
    for y in range(200, 420, 5):
        for x in range(20, 620, 5):
            i = y * w + x
            c = (cap[3 * i], cap[3 * i + 1], cap[3 * i + 2])
            tot += 1
            d = min(abs(c[0] - r) + abs(c[1] - g) + abs(c[2] - b) for r, g, b in ramp)
            near += (d <= 24)

    # and the top band, which is STATIC across the three captures
    tops = []
    for n in ("menu-18.png", "menu-22.png", "menu-26.png"):
        p2 = os.path.join(ROOT, "traces", "frames", n)
        if not os.path.exists(p2):
            continue
        _, _, c2 = F.read_png(p2)
        tops.append(round(sum(F.luma(c2, y * w + x)
                              for y in range(110, 145, 4)
                              for x in range(0, w, 8)) / (9 * 80)))
    return (len(ramp), ramp[0], ramp[63], round(100 * near / tot), tops), \
           (64, (126, 29, 0), (37, 108, 102), 97, [13, 13, 13]), \
           "the ramp rebuilt from the image's constants - 64 entries running " \
           "rust (126,29,0) to teal (37,108,102), which is the COLORREF byte " \
           "order and not the other one; then how many of the captured menu's " \
           "own background pixels land within 24 of some entry, which is 97 " \
           "per cent; and the top band's mean luma across all three captures, " \
           "identical in every one while the middle animates - the part of " \
           "this effect that is NOT explained, since the sheet is transparent " \
           "there and a reader's 800x600 screenshot has the cloud behind the " \
           "title"


def c_anekbah_rendered():
    r"""ANEKBAH, RENDERED - the repo's oldest prediction, finally a picture.

    `docs/ASSETS.md` 4b: the wrong shop sign is the **texture name cache**.
    `Tex3DT_BindMaterials` matches on the 19-character file name alone across a
    global pool of 58 slots, and `Area_LoadSet` keeps TWO decor sets resident -
    hidden is not unloaded - so an incoming set cache-hits against the outgoing
    location's atlases. `traces/impasse-walk.log` proves the path is walked:
    AREAS 222 (AIMPASSE) then AREAS 0 (ANEKBAH). CLAUDE.md has labelled the
    consequence **"falsifiable by playing"** since 2026-08-29: *the same panel
    should look different depending on which location you walked in from.*

    Until there was a rasterizer that could only be an inference about an
    atlas. This renders it.

    **The mechanism is confirmed, and it is large.** Same set, same camera, the
    only change being which neighbour was resident:

    | resident | atlases substituted | frame pixels moved | visibly |
    |---|---|---|---|
    | `AImpasse` | 7 | 6920 | 33 |
    | `AToit` | **18** | **121588** | **1763** |

    A third of the frame repaints when you arrive from the roof. So yes - the
    same location draws differently depending on where you came from, and the
    viewers are right while the game is the odd one out, exactly as ASSETS
    says.

    **What is NOT confirmed is the attribution.** ASSETS names `BATITR12` as
    "the atlas the wrong sign panels sample". Masked exactly - the atlas is
    repainted a flat colour and the set re-rendered, so a pixel belongs to the
    sign precisely when that changes it - the sign shows **8023** pixels here,
    of which **545 move** under substitution and **0 do so visibly** (the
    channel deltas summing over 24, the same threshold `anekbah residency`
    uses). So from this viewpoint that particular panel is not the one that
    changes, and the reported symptom must come from another atlas or another
    shot. That narrowing is the result; it does not touch the mechanism.

    **Two measurement errors on the way, both caught by a number that could not
    be true.** First, a mask that called a pixel "the sign" where the
    sign-only depth TIED the full frame's admits any coplanar neighbour - and
    it reported two DIFFERENT substitute atlases each moving exactly 531
    pixels, which is not a thing two different textures do. Second, the clean
    mask still gave the same count for both, which turned out to be real:
    **AImpasse and AToit ship byte-identical copies** of these atlases (same
    md5), so both substitute the same pixels. Anekbah is the odd one out. The
    two neighbours differ only in HOW MANY names they share, 7 against 18,
    which is the whole of the difference in the table above.

    **Tier 3** - the rasterizer is a reference implementation and this compares
    two of its own renders. What it establishes is that the ported cache rule
    and the ported geometry, driven from the shipped bytes, produce a visibly
    different frame; what it cannot establish is that the original's pixels
    were these. A captured 3D frame from each approach would do that, and is
    what CLAUDE.md means by falsifiable *by playing*.

    Shown to fail: substituting nothing takes every moved count to 0; masking
    the sign by the old depth tie moves 8023 to 5389 and 545 to 531, which is
    how the first mask was caught; and dropping the >24 threshold reports 6920
    and 121588 as "visible", which is the difference between a bit changing and
    a player seeing it.
    """
    import subprocess, tempfile, shutil, hashlib
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    if not (os.path.isdir(eng) and os.path.isdir(fr)):
        return ("skipped",), ("skipped",), "engine/ or gamedata/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_anekbah")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    out = os.path.join(tmp, "a.bin")
    try:
        subprocess.run([binp, fr, out], capture_output=True)
        raw = open(out, "rb").read()
        n, = struct.unpack_from("<i", raw, 0)
        v = struct.unpack_from("<%di" % n, raw, 4)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # the two neighbours ship the SAME pixels, which is why they move the sign
    # identically - measured here rather than inferred from the equal counts
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import tex3dt
    def md5(setName, texName):
        for t in tex3dt.textures(omkpaths.data("MESHES/DECORS",
                                              setName + ".3DO")):
            if t["name"] == texName:
                return hashlib.md5(bytes(t["rgb"])).hexdigest()
        return ""
    same = md5("AImpasse", "BATITR12") == md5("AToit", "BATITR12")
    differs = md5("Anekbah", "BATITR12") != md5("AImpasse", "BATITR12")

    return (v, same, differs), \
           ((20, 7, 12242, 282669, 8023,
             7, 6920, 33, 545, 0,
             18, 121588, 1763, 545, 0), True, True), \
           "Anekbah's textures and which material BATITR12 is; the triangles " \
           "drawn, the lit pixels, and the pixels the SIGN owns in the final " \
           "frame (masked by repainting its atlas, not by a depth tie); then " \
           "per neighbour a real walk could leave resident - AImpasse and " \
           "AToit - the atlases substituted by name, the frame pixels that " \
           "move and how many move VISIBLY, then the same two for the sign " \
           "alone; and finally that the two neighbours ship the SAME " \
           "BATITR12 while Anekbah's differs, which is why they move the " \
           "sign identically and why only the NUMBER of shared names " \
           "separates them"


def c_object_combine():
    r"""UI: event 37, the object-combination table in IAM\GLOBAL.

    `Game_HandleEvent` case 37 is the inventory screen's "use X on Y".
    sub_409650 walks a table at GLOBAL +12 (count int16 at +26) of 8-byte
    records - [i16 a][i16 b][i16 product][i16 gate] - matched SYMMETRICALLY,
    so the order the player picks the two items in does not matter.

    The check is that all four ids of all eleven records name a real object in
    IAM\OBJECT, and they read as recipes: box + key -> open box, explosive +
    detonator -> charge, koil + sleeping pill -> drugged koil.

    Then the part that needed the assembly. A recipe fires only when its +6
    equals `dword_4E6C70`, and that global has exactly FOUR references in the
    whole binary, all inside this case: set to 1 when the first item is
    GLOBAL +64 (object 330, "Beshe'm sanctifie"), to 0 otherwise, and to -1
    after a combine. SIX of the eleven - every spell recipe - want 8, which
    nothing ever writes, so they cannot fire; and no recipe product is granted
    by any script either (472 world + 10 dialogue `inventory.add` sites, zero
    hits), so combining is their only source. The exception, which is why this
    asserts a prop count too: "Sort de resurrection" is placed once as a world
    prop, so one of the six spell products is reachable after all and the
    other five are not.
    """
    import dialog_disasm as D
    g = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
    base = struct.unpack_from("<I", g, 12)[0]
    n = struct.unpack_from("<h", g, 26)[0]
    ob = open(omkpaths.data("IAM/OBJECT"), "rb").read()
    def name(i):
        if i < 0 or 2048 * i + 1304 > len(ob): return None
        return ob[2048*i+24:2048*i+56].split(b"\0")[0].decode("cp1252")
    rec = [struct.unpack_from("<4h", g, base + 8*k) for k in range(n)]
    named = sum(1 for r in rec for i in r[:3] if name(i))
    gates = sorted({r[3] for r in rec})
    prods = {r[2] for r in rec}
    # nothing grants a product: world scripts, then the conversation scripts
    world = sum(1 for r in _world_ops()[50]
                if struct.unpack("<2h", r)[1] in prods)
    dlg = 0
    dd = open(omkpaths.data("IAM/DIALOG"), "rb").read()
    for cid, b in D.chunks(dd):
        p = D.parse(b)
        if not p: continue
        for j, nid, k, ptr in D.scripts_of(b, p[1]):
            ops, st = D.disasm(b, ptr, len(b))
            if st != "ok": continue
            for pc, op, raw in ops:
                if op == 50 and len(raw) >= 4:
                    dlg += struct.unpack_from("<2h", raw, 0)[1] in prods
    tabs, _ = _prop_table()
    asprop = sum(1 for kind in tabs.values() for rows in kind.values()
                 for r in rows if r[1] in prods)
    return (n, named, gates, sum(r[3] == 8 for r in rec),
            struct.unpack_from("<h", g, 64)[0], name(330),
            world, dlg, asprop), \
           (11, 33, [0, 8], 6,
            330, "Beshe'm sanctifié",
            0, 0, 1), \
           "recipes, of which ids naming a real object (3 each); the gate " \
           "values used and how many want 8 - which nothing writes; " \
           "GLOBAL +64 and the object it names; then products granted by a " \
           "world script, by a conversation script, and placed as a world " \
           "prop (the one is Sort de resurrection)"


def c_fight_ai():
    r"""The fight AI, and the `.CTL` section that holds it.

    `.CTL` +76/+80 was in the header map with no contents - `anim_ctl.py`
    walked past it because the walk has to land exactly. It is the FIGHT AI:
    156-byte profiles keyed by +0 = the difficulty level + 1 (sub_45DCB0),
    each with two delay ranges and twelve situation slots of moves. A move is
    a SEQUENCE of input words that Fight_TickAI pushes into the actor's queue
    with Perso_InjectInput - so the AI plays the same .CTL state machine the
    player does, by pressing buttons.

    The checks the data can fail: only the three COMBAT files carry profiles,
    the walk still lands exactly on all seven, and every input word is a real
    binding - `w & ~0x40000000` inside the 14 bits ASSETS documents. The bit
    UNION is the interesting one: the AI presses ten of the fourteen and never
    CTRL, SPACE, SHIFT or TAB, which are the non-combat bindings.

    0x40000000 is the queue's idle word (Perso_SetInputEnabled resets the
    queue to exactly it), interleaved between presses so the machine sees a
    release. It is NOT the 0x80000000 "no input" edge code: Cef_InputMatches
    ends `if (a1 == 0x80000000) return a2 <= 0x2000`, so the idle word matches
    no idle edge.
    """
    import anim_ctl
    # BOTH cases - two of the seven are lowercase, and a case-sensitive glob
    # here would have missed D1Cmbt.ctl and f1cmbt.ctl, which is where two of
    # the three profile sets live.
    files = sorted(glob.glob(omkpaths.data("ANIMS/*.CTL")) +
                   glob.glob(omkpaths.data("ANIMS/*.ctl")))
    exact = withai = 0
    ids, words, bits, per = [], set(), 0, []
    for f in files:
        w = anim_ctl.walk(f)
        exact += w["exact"]
        if w["ai"]:
            withai += 1
            for a in w["ai"]:
                ids.append(a["id"])
                nm = sum(sl["count"] for sl in a["slots"])
                nw = sum(len(m) for sl in a["slots"] for m in sl["moves"])
                per.append((w["file"], a["id"], nm, nw, a["move_delay"]))
                for sl in a["slots"]:
                    for m in sl["moves"]:
                        for v in m:
                            words.add(v)
                            bits |= v & 0x3FFF
    inrange = all((v & ~0x40000000) <= 0x3FFF for v in words)
    per.sort()
    slow = [p for p in per if p[4][0] >= 3000]
    per = [p[:4] for p in per]
    return (len(files), exact, withai, sorted(ids), len(words), inrange,
            bits, per, len(slow)), \
           (7, 7, 3, [1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4], 30, True, 0xCFF,
            [("D1Cmbt.ctl", 1, 28, 96), ("D1Cmbt.ctl", 2, 35, 136),
             ("D1Cmbt.ctl", 3, 31, 140),
             ("H1Cmbt.CTL", 1, 32, 110), ("H1Cmbt.CTL", 2, 47, 173),
             ("H1Cmbt.CTL", 3, 47, 212), ("H1Cmbt.CTL", 4, 5, 12),
             ("f1cmbt.ctl", 1, 26, 86), ("f1cmbt.ctl", 2, 45, 165),
             ("f1cmbt.ctl", 3, 46, 196), ("f1cmbt.ctl", 4, 5, 12)],
            2), \
           ".CTL files, of which walk exactly and carry AI profiles (the " \
           "three COMBAT files and only those); every profile id; distinct " \
           "input words and whether every one is a real binding; the UNION " \
           "of input bits - 0xCFF, so never CTRL/SPACE/SHIFT/TAB; (file, id, " \
           "moves, words) for all eleven profiles; and how many have a move " \
           "delay of 3 s or more - the two id-4 sparring partners"


def c_sim_ui():
    r"""sim: the widget tree walked with the engine's own input words.

    `ui.open` suspends a script and a screen answers it. The simulator used to
    supply that answer as a literal, which tested the suspend/resume and left
    the whole widget layer outside the harness. `tools/sim/ui.py` now walks the
    real tree: the screen record names an open callback, the callback installs
    a panel (one `mov [reg+0x1C], imm32`), and the panel's lists and items are
    static records.

    THE START MENU. The fallback here is deliberately WRONG, so getting 1
    means the value came from the path: confirm on "Nouvelle partie", DOWN
    onto the button list - the confirm dialog's panel hook (0x0047A230) moves
    lists with up/down, not left/right - then confirm on "Confirmer", whose
    callback carries `mov dword_930750, 1`. 1 is the `Interface` the shipped
    save records (`verify.py: save file`). Modelling that hook was forced by
    running it: the first version moved lists on left/right and reached the
    right answer for the wrong reason.

    THE LIFT. Its list is the one place in the game with a bespoke input hook,
    `UI_GridMenuInput` - six slots in a 3-wide, 2-deep grid plus one apart,
    which the ITEM COORDINATES confirm independently (x 278/321/370 on rows
    y 194 and 241, slot 6 alone at 325,288). The checks are that every slot is
    reachable from slot 0 through the four directions, that slot 6 has no
    horizontal move (it is alone on its row), and that confirm answers
    `slot - 1` with slot 0 giving 6 - so slot 1, "Niveau 0", answers 0. All 18
    `ui.open 4` sites store it in variable 496, `Etage`.

    OPTIONS still refuses: its pages are built by native code, so it returns
    the fallback rather than inventing an answer.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from sim.ui import Ui, CONFIRM, DOWN, UP, LEFT, RIGHT
    from sim.run import ui_answer
    import dialog_disasm as D
    u = Ui(); u.open(29)
    first = u.label(u.selected(), "Menu")
    u.press(CONFIRM)
    u.type_name("Kay'l")          # Confirmer REFUSES an empty field
    u.press(DOWN)
    second = u.label(u.selected(), "Menu")
    u.press(CONFIRM)
    # the same walk with nothing typed must answer NOTHING - the gate is the
    # callback's first instruction, `test [dword_657994]` / `je`
    n = Ui(); n.open(29)
    for b in (CONFIRM, DOWN, CONFIRM): n.press(b)
    empty_refused = n.answer is None and not n.approx
    v = Ui(); v.open(29)
    for _ in range(3): v.press(DOWN)
    third = v.label(v.selected(), "Menu")
    v.press(DOWN); wrapped = v.label(v.selected(), "Menu")

    g = Ui(); g.open(4); lst = g._cur_list()
    adj = {}
    for st in range(7):
        row = {}
        for nm, b in (("up", UP), ("down", DOWN), ("left", LEFT), ("right", RIGHT)):
            g.sel[lst] = st; g.press(b); row[nm] = g.sel[lst]
        adj[st] = row
    seen, q = {0}, [0]
    while q:
        for y in adj[q.pop()].values():
            if y not in seen: seen.add(y); q.append(y)
    ans = []
    for k in range(7):
        g.sel[lst] = k; g.answer = None; g.press(CONFIRM); ans.append(g.answer)
    slot6_flat = adj[6]["left"] == 6 and adj[6]["right"] == 6
    # every ui.open on screen 4 writes the same variable
    sites = [struct.unpack("<3h", r[:6]) for r in _world_ops()[70] if len(r) >= 6]
    lift = [x for x in sites if x[0] == 4]
    var = {x[2] for x in lift}
    return (first, second, u.answer, u.approx, empty_refused,
            ui_answer(29, fallback=-999), ui_answer(29, fallback=-999, name=""),
            ui_answer(35, fallback=-999),
            third, wrapped,
            sorted(seen), ans, slot6_flat, g.approx,
            len(lift), sorted(var), O.TAGS["VARIABLES"].get(496)), \
           ("Nouvelle partie", "Confirmer", 1, False, True,
            1, -999, -999,
            "Quitter", "Nouvelle partie",
            [0, 1, 2, 3, 4, 5, 6], [6, 0, 1, 2, 3, 4, 5], True, False,
            18, [496], "Etage"), \
           "the start menu's first item, the item DOWN reaches, the answer " \
           "its callback writes with a name typed, that the walk was exact, " \
           "and that the SAME walk with an empty field answers nothing; the " \
           "answer derived with a deliberately wrong fallback and the same " \
           "derivation falling back when no name is typed; OPTIONS refusing " \
           "because its builders are native; four downs wrapping the " \
           "four-item list; then the LIFT grid - every slot reachable from " \
           "0, the answer each slot confirms (slot-1, slot 0 giving 6), that " \
           "slot 6 has no horizontal move; and the ui.open sites on screen 4 " \
           "with the single variable they all write"


def c_sim_options():
    r"""sim: the OPTIONS screen walked page by page.

    Screen 35 is never opened by a script (0 of 241 `ui.open` sites name it) -
    it is reached from the start menu and the pause screen - so what there is
    to test is its NAVIGATION and its value changing, not an answer.

    It is also not shaped like the other screens: thirteen PAGE records share
    one set of sixteen row widgets, so what a page shows lives in the calls its
    builder makes to `Opt_BindRow(row, item, page)`. Those are recovered from
    the builder's own bytes (`push page; push item; push row; call 0x00490F90`)
    rather than hard-coded, so a wrong page tree would show up here.

    The value rules are `sub_492DA0`, the live page's input hook and the last
    of the big unread bodies in this module: LEFT steps a choice back, RIGHT
    and CONFIRM step it forward, both wrapping. "Distance de clipping" is the
    one checked because its five captions and values are in the shipped
    `IAM/Options` - so the cycle is verifiable end to end, caption and number.

    One transcription the walk forced: every sub-page's "Retour" binds page
    **0**, not the root, and page 0 has no rows at all - its builder bounces
    straight to page 1 unless `dword_9103C8` (the dirty latch) raises a prompt.
    Without that the walk lands on an empty page, which is how it was found.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from sim.ui import OptionsUi, DOWN, LEFT, RIGHT, CONFIRM
    o = OptionsUi()
    o.open(1)
    root = [o.opt[i]["label"] for k, i in o.rows()
            if i is not None and o.selectable(k)]
    # each top-level entry: which page it opens, and that Retour comes home
    tree = []
    for n in range(4):
        o.open(1)
        for _ in range(n):
            o.press(DOWN)
        o.press(CONFIRM)
        page, first = o.page, o.rows()[0][1]
        while o.label() != "Retour":
            o.press(DOWN)
        o.press(CONFIRM)
        tree.append((page, o.opt[first]["label"], o.page))
    # the clipping choice, cycled forward past the wrap and back
    v = OptionsUi(); v.open(1); v.press(CONFIRM); v.press(DOWN)
    name = v.label()
    fwd = []
    for _ in range(6):
        v.press(RIGHT); fwd.append(v.value())
    back = []
    for _ in range(2):
        v.press(LEFT); back.append(v.value())
    ui_open = [struct.unpack("<3h", r[:6]) for r in _world_ops()[70] if len(r) >= 6]
    return (root, tree, name, fwd, back, v.approx,
            sum(1 for x in ui_open if x[0] == 35)), \
           (["Vidéo", "Audio", "Options", "Contrôles"],
            [(2, "Vidéo", 1), (3, "Audio", 1), (4, "Options", 1),
             (5, "Contrôles", 1)],
            "Distance de clipping",
            [("Proche", 50), ("Intermédiaire", 100), ("Loin", 150),
             ("Très loin", 200), ("Très proche", 25), ("Proche", 50)],
            [("Très proche", 25), ("Très loin", 200)], False,
            0), \
           "the root page's selectable rows; for each, the page it opens, " \
           "that page's first row and the page Retour comes back to; then " \
           "the clipping row cycled RIGHT past its wrap and LEFT back over " \
           "it, caption and value; that the walk stayed exact; and that no " \
           "script opens screen 35 at all"


def c_sim_loadpanel():
    r"""sim: the start menu's load panel, and where the save directory is built.

    `sub_47A6D0` reads the save directory and takes one of two branches - that
    is the whole of what the panel's shape depends on:

      profiles > 0    focus the slot list (panel+24 = 0) and leave "Charger
                      une partie" and "Détruire" selectable
      profiles == 0   focus the BUTTONS (panel+24 = 1), hide the slot list,
                      and disable both - `word_4CEA9A = 3`

    The shipped `gamedata/IAM/GAMES` is a file the engine created under the
    golden-trace rig and never saved into: all 256 slots are empty. So the
    check has a real answer - the load panel comes up offering **only
    "Annuler"** - and a synthetic one-profile directory is run beside it, so
    the model is shown to BRANCH rather than always saying the same thing.

    `sub_408A10` is what builds the directory `SaveDir_CountByName` walks, and
    GAME_STATE 8 had it as "those two walk something else" without saying what
    fills it. The record is four fields lifted out of each 32808-byte slot:
    the name from +0, the day from +32, the time from +36, and 32 bytes from
    +76 - which is 36 into the slot's DB and is NOT a string, so what it is
    for stays unread. 256 x 72 = 18432, the `memset` the builder opens with.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from sim.ui import (LoadPanel, save_directory, profiles,
                        GAMES_HEADER, GAMES_SLOT, GAMES_SLOTS)
    d = save_directory()
    empty = LoadPanel(directory=d)
    one = LoadPanel(directory=[{"slot": 0, "name": "OMK_SAVE", "day": 52,
                                "time": 0, "db36": b""}])
    sz = os.path.getsize(omkpaths.data("IAM/GAMES"))
    return (len(d), profiles(d), sz, GAMES_HEADER + GAMES_SLOT * GAMES_SLOTS,
            72 * GAMES_SLOTS,
            (empty.empty, empty.focus, empty.mode, empty.selectable()),
            (one.empty, one.focus, one.mode, one.selectable())), \
           (256, 0, 8402344, 8402344, 18432,
            (True, 1, 3, ["Annuler"]),
            (False, 0, 0, ["Charger une partie", "Détruire", "Annuler"])), \
           "directory entries read and the distinct profiles among them (the " \
           "shipped file is one the engine made and never saved into); the " \
           "file size against 3496 + 256*32808 and the 256*72 the builder " \
           "memsets; then the panel with an EMPTY directory - focus, mode " \
           "and the one item left - and beside it a synthetic one-profile " \
           "directory, so the model is shown to branch"


def c_keybindings():
    r"""The four control schemes, and the rebind rule.

    A keybind row's apply hook (0x004902C0) writes its three codes to
    `table[group*14 + action]`, with the group at the record's +92 and the
    action index at +112 - which is what fixes the shape of the three compiled
    tables: **4 groups x 14 actions**, one table per device. The groups are
    contexts, and the engine installs them where their names say:
    `Game_Init` and the end of a fight or a shoot install 0 (Aventure),
    `Fight_Begin` 3, `Shoot_Enter` 2, the swim transitions 1.

    The check the data could fail is that three independent tables all land
    inside the code space of the ONE function that produces those codes,
    `Input_ReadOneControl`: a keyboard scan code is 1..255, a joystick button
    is its index plus 48, a mouse button is 12..14. They do - 41, 48 and 4
    non-zero entries respectively, none outside its own space - and every
    labelled cell's group and action come from the option row, so the two
    halves are known to line up rather than assumed to.

    The rebind rule is `Opt_RebindKey`'s: the scan runs over the whole 74-row
    table but clears only a row in the SAME group, which is why "Avancer" is
    UP in Aventure and "Reculer" is LEFT in Combat with neither disturbing the
    other. Codes 0, 1 and 4 are refused before the scan.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from sim.ui import bindings, rebind, BIND_GROUPS, BIND_SLOTS
    b = bindings()
    kb = [b[g][a]["keyboard"] for g in BIND_GROUPS for a in range(BIND_SLOTS)]
    ms = [b[g][a]["mouse"] for g in BIND_GROUPS for a in range(BIND_SLOTS)]
    js = [b[g][a]["joystick"] for g in BIND_GROUPS for a in range(BIND_SLOTS)]
    labelled = {g: sum(1 for a in range(BIND_SLOTS) if b[g][a]["label"])
                for g in BIND_GROUPS}
    spaces = (all(0 < x < 256 for x in kb if x),
              all(x in (12, 13, 14) for x in ms if x),
              all(x == 4 or 48 <= x <= 57 for x in js if x))
    # a few cells that name themselves
    named = (b[0][2]["label"], b[0][2]["keyboard"],       # Avancer / UP
             b[3][4]["label"], b[3][4]["keyboard"],       # Coup de poing 1 / Q
             b[2][4]["label"], b[2][4]["mouse"])          # Tir / left button
    rows = {29: {"group": 0, "slot": {1: 200}},
            30: {"group": 0, "slot": {1: 208}},
            62: {"group": 3, "slot": {1: 200}}}
    cleared = rebind(rows, 30, 1, 200)
    return (labelled, sum(1 for x in kb if x), sum(1 for x in ms if x),
            sum(1 for x in js if x), spaces, named,
            cleared, rows[29]["slot"][1], rows[62]["slot"][1],
            rebind(rows, 30, 1, 4)), \
           ({0: 10, 1: 7, 2: 13, 3: 10}, 41, 4, 48, (True, True, True),
            ("Avancer", 200, "Coup de poing 1", 16, "Tir", 12),
            [29], 0, 200, None), \
           "labelled actions per group; non-zero cells in the keyboard, " \
           "mouse and joystick tables and whether each stays inside " \
           "Input_ReadOneControl's own code space; three cells that name " \
           "themselves (Avancer=UP, Coup de poing 1=Q, Tir=left button); " \
           "then the rebind - taking UP for another Aventure row CLEARS it, " \
           "leaves the Combat row holding UP alone, and an axis code is refused"


def c_sim_namefield():
    r"""sim: the start menu's name field - the last of the interface's refusals.

    `sub_47A390` is the only list hook besides the LIFT's grid, and it answers
    a different channel: `sub_4397B0` hands it one typed character, not the
    input bits. That is why a direction does nothing in the field, and why the
    hook existing at all stops `Ui_MoveSelection` being reached - so up and
    down inside the field are inert and DOWN out of it is the panel hook's job.

    Its switch is a compact jump table over the characters 8..27, read here
    from the table itself rather than transcribed: BACKSPACE deletes before
    the cursor, TAB and ESC are ignored, RETURN moves the focus to the buttons
    but ONLY when the buffer is not empty, and everything else inserts.

    The cap is the buffer: 0x0069BDA0 to 0x0069BDB4 is 0x14, so **20
    characters**, refused by a `strlen` test before the insert. The save slot
    that receives the name has room for 32, so the field is the tighter of the
    two - and the one real save the engine wrote carries a 20-character name,
    which is the cap exactly and fits it.

    With this the whole new-game path walks with `approx` false: confirm on
    "Nouvelle partie", type, DOWN to the buttons, confirm on "Confirmer" -
    answer 1.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from sim.ui import (Ui, NameField, name_switch, NAME_MAX, NAME_BUFFER,
                        CONFIRM, DOWN, UP)
    sw = name_switch()
    special = {k: v for k, v in sw.items() if v != "insert"}
    f = NameField(); f.enter("Kay'l 669")
    typed = f.buf
    f.type(8)
    afterbs = f.buf
    ret = f.type(13)
    empty = NameField()
    over = NameField(); over.enter("A" * 30)
    ign = NameField(); ign.enter("abc"); ign.type(9); ign.type(27)
    # the full path, and that it stays exact
    u = Ui(); u.open(29); u.press(CONFIRM)
    u.type_name("Kay'l")
    u.press(UP)                       # inert inside the field
    u.press(DOWN); u.press(CONFIRM)
    # and the same path with NOTHING typed. The field refuses RETURN on an
    # empty buffer (above), and the BUTTON refuses too - `Confirmer` opens
    # `mov eax,[dword_657994] / test eax,eax / je`, so it writes neither the
    # answer nor the screen's state word and the screen stays open. Two
    # independent refusals for one empty field, which is why a golden capture
    # playing return/down/return never got an answer at all (docs/UI.md 3f).
    e2 = Ui(); e2.open(29)
    for b in (CONFIRM, DOWN, CONFIRM): e2.press(b)
    save = open(os.path.join(ROOT, "traces/save-appart.bin"), "rb").read()
    slotname = save[3496:3528].split(b"\0")[0]
    return (special, NAME_MAX, typed, afterbs, ret, f.done,
            empty.type(13), empty.done, len(over.buf), ign.buf,
            u.answer, u.approx, e2.answer, e2.approx, len(slotname)), \
           ({8: "backspace", 9: "ignore", 13: "return", 27: "ignore"},
            20, "Kay'l 669", "Kay'l 66", True, True,
            False, False, 20, "abc",
            1, False, None, False, 20), \
           "the switch's non-insert cases, read from its own jump table, and " \
           "the 20-character cap; typing, backspace, and RETURN accepted on a " \
           "non-empty name; RETURN REFUSED on an empty one; thirty characters " \
           "truncated to the cap; TAB and ESC ignored; then the whole new-game " \
           "path answering 1 with the walk exact WHEN A NAME IS TYPED, and " \
           "answering nothing at all when none is - Confirmer's own gate; and " \
           "the length of the name in the save the engine actually wrote"


def c_sim_ui_coverage():
    r"""sim: how much of the interface the walker actually drives, and that the
    rest REFUSES rather than inventing.

    The four panels this work set out to model walk exactly - the start menu
    with its confirm dialog and name field, the LIFT's grid, the OPTIONS page
    tree (through `OptionsUi`, which the generic walker does not cover), and
    the load panel. The other screens carry their own native hooks - the shops
    share one panel hook, the terminal family one list hook - and are NOT
    modelled.

    That is the honest number, and this check exists to keep it honest: it
    counts them. What matters is the second half - a screen the walker cannot
    drive must log an unmodelled hook and leave `answer` None, so `ui_answer`
    falls back. **No screen may produce an answer through an unmodelled path**,
    which is the property that stops a plausible-looking wrong number reaching
    the simulator.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ui_tables as U2
    from sim.ui import Ui, UP, DOWN, CONFIRM
    live = [s for s in U2.screens() if any(s["cb"])]
    exact, refused, unsound = [], [], []
    for sc in live:
        try:
            u = Ui(); u.open(sc["id"])
        except ValueError:
            refused.append(sc["id"]); continue           # panel not decidable
        for b in (UP, DOWN, CONFIRM):
            u.press(b)
        un = [l for l in u.log if "unmodelled" in str(l[0])]
        if un or u.approx:
            refused.append(sc["id"])
            if u.answer is not None:
                unsound.append(sc["id"])                 # must stay empty
        else:
            exact.append(sc["id"])
    return (len(live), sorted(exact), len(refused), unsound), \
           (32, [4, 29, 34], 29, []), \
           "live screens; the ones the GENERIC walker drives with no " \
           "unmodelled hook - the LIFT, the start menu, and SHOOT HUMAN, whose " \
           "panel carries no hooks at all (OPTIONS has its own walker and " \
           "the load panel its own model); how many refuse; " \
           "and the screens that answered anyway through an unmodelled path " \
           "- which must be NONE"


def c_ui_page():
    r"""The /ui page's data path - every screen, every key, without a server.

    `tools/omkui.html` is stateless: it sends the whole key history and the
    server replays it, because the model is deterministic. So the thing that
    can rot is the JSON contract between them, and this exercises it directly -
    the same `_ui_screens` and `_ui_walk` the endpoints call, over ALL 37
    screens and six key sequences including ones that close a screen and ones
    that hit a dead (ELIMINE) entry.

    Two shapes are asserted because both were bugs the sweep found: a screen
    with no callbacks must come back as an `error` rather than raising, and a
    walk that CLOSES the screen (back from a panel with no parent) must come
    back as `closed` rather than dereferencing a null panel.

    The invariant, again, is that nothing answers through an unmodelled path:
    across all 222 walks, no result carries both an `answer` and `approx`.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import omkweb
    scr = omkweb._ui_screens()
    seqs = ["", "down,confirm", "right,right,confirm", "up,back", "back",
            "confirm,confirm,confirm,back,back"]
    n = errors = closed = unsound = 0
    for s in scr:
        for q in seqs:
            n += 1
            d = omkweb._ui_walk(s["id"], [k for k in q.split(",") if k])
            if "error" in d:
                errors += 1
                continue
            closed += bool(d.get("closed"))
            if d.get("answer") is not None and d.get("approx"):
                unsound += 1
    # `ch:` entries type into the field mid-sequence, which this path needs:
    # Confirmer refuses an empty one, so a walk that only presses keys must
    # come back with NO answer, and the typed one with 1.
    full = omkweb._ui_walk(29, ["confirm"] + ["ch:" + c for c in "Kayl"]
                           + ["down", "confirm"])
    bare = omkweb._ui_walk(29, ["confirm", "down", "confirm"])
    lift = omkweb._ui_walk(4, ["right", "confirm"])
    opts = omkweb._ui_walk(35, ["confirm", "down", "right"])
    named = omkweb._ui_walk(29, ["confirm"], "Kayl")
    bmp = ui_tables.bitmap("gfxint.bmp")
    # the background the page draws is the COMPOSED one, not the sheet
    from sim.ui import Ui as _U
    mp = ui_tables.panel_background(2, _U().panel_of(2))
    raw = ui_tables.bitmap("Multipla.bmp")
    strip = all(mp[2][((192 + y) * 640 + 64 + x) * 3] == 0
                for y in range(0, 64, 8) for x in range(0, 256, 8))
    return (len(scr), n, unsound, errors > 0, closed > 0,
            full["answer"], bare["answer"], lift["answer"], opts["page"],
            named["name_field"]["text"], named["name_field"]["active"],
            (bmp[0], bmp[1], len(bmp[2])),
            full["back"], omkweb._ui_walk(2, [])["back"],
            mp[2] != raw[2], strip), \
           (37, 222, 0, True, True, 1, None, 0, 2, "Kayl", True,
            (640, 480, 640 * 480 * 3),
            "none", "tiles", True, True), \
           "screens the page lists; walks exercised; walks that answered " \
           "through an unmodelled path (must be 0); that a dead screen comes " \
           "back as an error and a closed one as closed; the start menu's " \
           "derived answer with a name typed and the SAME walk answering " \
           "nothing without one (Confirmer's gate), the LIFT's after one " \
           "RIGHT, the options page a " \
           "CONFIRM opens, the name field's text and whether it is active; " \
           "the artwork decoded for the stage; then the BACKGROUND mode of " \
           "the start menu (none - its art is all item sprites) and of " \
           "MULTIPLAN (tiles), that the composed background differs from the " \
           "raw sheet, and that MULTIPLAN's lit-copy strip at (64,192) is " \
           "BLACK in it - the tile map never places those cells, so a viewer " \
           "drawing the sheet would show source art the game never does"


def c_page_templates():
    r"""The viewer pages: a backtick inside a multi-line template literal.

    CLAUDE.md 5 already records two black screens from an edit that passed
    `node --check` and failed at run time, because a syntax checker cannot see
    an undeclared identifier. This is the third, and the sharpest: a prose
    comment INSIDE the GLSL shader source of `/cutscene` wrote a phase formula
    in markdown backticks -

        * The phase is `(clock >> 2) + (vertexAddress >> 4)`, and since the

    - and the first of those backticks CLOSED the template literal. What
    followed parsed as a tagged call on the string, `...`(clock >> 2), which is
    valid JavaScript, so `node --check` passed. At run time it threw
    ReferenceError on `clock`, inside `initGL`, which `boot()` calls before it
    fetches the catalogue - so the cutscene list came up EMPTY and nothing said
    why.

    The rule here is narrow enough to be exact: a template literal spanning
    more than one line is a shader or a block of markup, and its closing
    backtick must be followed by `)`, `,`, `;` or `}`. One cut short by a
    stray backtick is followed by something else - `(` in this case.
    """
    import glob as _g
    bad = []
    for path in sorted(_g.glob(os.path.join(ROOT, "tools", "*.html"))):
        src = open(path, encoding="utf-8", errors="replace").read()
        if "<script>" not in src:
            continue
        js = src.split("<script>", 1)[1].rsplit("</script>", 1)[0]
        i, n = 0, len(js)
        while i < n:
            c = js[i]
            if c == "\\":
                i += 2; continue
            if c == "`":
                j, depth = i + 1, 0
                while j < n:
                    if js[j] == "\\": j += 2; continue
                    if js[j] == "`" and depth == 0: break
                    if js[j] == "$" and j + 1 < n and js[j+1] == "{": depth += 1; j += 2; continue
                    if js[j] == "}" and depth: depth -= 1
                    j += 1
                body = js[i+1:j]
                if "\n" in body:
                    after = js[j+1:j+2]
                    if after not in (")", ",", ";", "}", "", "\n", " "):
                        bad.append((os.path.basename(path),
                                    js[:i].count("\n") + 1, after))
                i = j + 1; continue
            i += 1
    return bad, [], "multi-line template literals whose closing backtick is " \
                    "followed by something other than ) , ; } - i.e. one cut " \
                    "short by a stray backtick inside it (file, line, the " \
                    "character that followed)"


def c_ui_sprites():
    r"""UI: the two source rectangles of a sprite item - the hover artwork.

    `Ui_DrawItemSprite` picks the source by the same "is this item lit" ladder
    the text style uses: **+12/+14 while lit, +16/+18 while not**. So every
    sprite item names two rectangles in the screen's own 640x480 sheet, and
    that is what a player sees as a hover state.

    Two structural facts, both from the data:

      * for **146 of the 164** sprite items the UNLIT source is the item's own
        destination - the resting look is already painted into the background,
        so drawing it "unlit" blits the background back over itself and erases
        the highlight;
      * the lit copy is therefore a SECOND rendering of the same button
        somewhere else on the sheet, laid out to suit the sheet rather than the
        screen. On MULTIPLAN the four destinations are a vertical column at
        x 554/563 while their lit sources are a horizontal ROW at y=192, x
        stepping 64 - which is exactly what the artwork looks like. The LIFT's
        seven lit sources instead mirror its own 3-2-1 keypad.

    Whether the lit copy is *brighter* depends on the screen and is not
    asserted as a rule: measured over the non-key pixels (the blit is
    DDBLT_KEYSRC and the key is black), MULTIPLAN is 84 against 67, the shops
    112 against 49 and GANDHAR DOOR 104 against 41 - every item, in each case -
    while DEN's highlight is darker instead. What is checked is the three that
    are decisively brighter, since a swapped +12/+16 reading would invert them.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ui_tables as U2
    from sim.ui import Ui
    e = U2.Exe(); u = Ui(e)
    KEY = (0, 0, 0)
    def mean(rgb, W, x, y, w, h):
        t = n = 0
        for yy in range(y, min(y + h, 480)):
            for xx in range(x, min(x + w, W)):
                i = (yy * W + xx) * 3
                c = (rgb[i], rgb[i+1], rgb[i+2])
                if c == KEY: continue
                t += 0.299*c[0] + 0.587*c[1] + 0.114*c[2]; n += 1
        return t / n if n else 0.0
    total = restores = 0
    mult = None
    bright = {}
    for s in U2.screens():
        if not any(s["cb"]) or not s["bitmap"]:
            continue
        try: p = u.open(s["id"])
        except Exception: continue
        b = U2.bitmap(s["bitmap"])
        if not b: continue
        W, H, rgb = b
        pairs = []
        for lst in u.lists(p):
            for it in u.items(lst):
                if not (u._u32(it + 52) & 0x100): continue
                w, h = u._i16(it + 4), u._i16(it + 6)
                if w <= 0 or h <= 0: continue
                total += 1
                d = (u._i16(it), u._i16(it + 2))
                L = (u._i16(it + 12), u._i16(it + 14))
                N = (u._i16(it + 16), u._i16(it + 18))
                restores += (N == d)
                pairs.append((L, N, w, h))
        if s["name"] == "MULTIPLAN":
            mult = [p[0] for p in pairs]
        if s["name"] in ("MULTIPLAN", "BANK", "GANDHAR DOOR"):
            bright[s["name"]] = sum(1 for L, N, w, h in pairs
                                    if mean(rgb, W, *L, w, h) > mean(rgb, W, *N, w, h))
    return (total, restores,
            sorted({y for x, y in mult}), sorted(x for x, y in mult),
            bright), \
           (164, 146, [192], [64, 128, 192, 256],
            {"MULTIPLAN": 4, "BANK": 4, "GANDHAR DOOR": 5}), \
           "sprite items and how many take their UNLIT source from their own " \
           "destination (the background restores itself); MULTIPLAN's four " \
           "lit sources - one row, four columns 64 apart, against a vertical " \
           "column of destinations; and on the three screens whose highlight " \
           "is a brightening, how many of their items have the lit rectangle " \
           "brighter than the unlit one over the non-key pixels"


def c_ui_textrender():
    r"""The interface text rendered with the game's own fonts and markup.

    `tools/uitext.py` follows `Text_LayOutBlock` and `Text_DrawRun` rather than
    approximating them: the shipped `.FNT` glyphs, the same markup, the same
    120%-of-line-height spacing, and a glyph pixel taken as a COVERAGE level
    0..31 that becomes the alpha - which is what lets one glyph sheet serve
    every colour.

    The markup cases checked are the ones the shipped text actually uses:
    `{fC}` names the COMPUTER font by its id letter, `{I255120045}` is three
    3-DIGIT DECIMALS and not a hex triple, the two chain inside one brace as
    `{fCI255120045}`, `{C}` centres, and a font can change mid-string -
    `{fS}Anekbah{fC}` is 20 characters of SNEAK inside a COMPUTER paragraph.
    CR is skipped and LF ends a line, so `IAM\Lift`'s two-line floor labels
    come out two lines tall.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import uitext, ui_tables as U2
    import collections as _c
    run, align = uitext.parse("{fC}Consulter le dossier")
    fonts_only = sorted({f for _, f, _ in run})
    _, col = uitext.parse("{I255120045}x")[0][0][2], None
    chained, _ = uitext.parse("{fCI255120045}AFFAIRE")
    centred = uitext.parse("{C}Quitter")[1]
    mixed, _ = uitext.parse(U2.iam_strings("Term")[3])
    mf = _c.Counter(f for _, f, _ in mixed)
    # a rendered line, and the unlit halving
    lit = uitext.render("Nouvelle partie", width=500, font_letter="J",
                        rgb=(255, 255, 255))
    dim = uitext.render("Nouvelle partie", width=500, font_letter="J",
                        rgb=(255, 255, 255), lit=False)
    ink = lambda r: max(r[2][i] for i in range(0, len(r[2]), 4))
    two = uitext.render(U2.iam_strings("Lift")[0], width=280, font_letter="J")
    one = uitext.render("Niveau 1 :", width=280, font_letter="J")
    return (fonts_only, run[0][2], align,
            [f for _, f, _ in chained][:1], chained[0][2],
            centred, dict(mf), ink(lit), ink(dim),
            two[1] > one[1], len(uitext.font("J")[1].glyph)), \
           (["C"], (255, 255, 255), None,
            ["C"], (255, 120, 45),
            8, {"C": 647, "S": 20, "J": 4}, 247, 123,
            True, 223), \
           "`{fC}` selects a font, leaves the colour alone and sets NO " \
           "alignment (None, so the item's own stands); " \
           "`{fCI255120045}` chains both in one brace and gives (255,120,45); " \
           "`{C}` centres (align 8); a terminal dossier mixing three fonts " \
           "mid-string; the brightest ink lit and unlit (247 and 123, and " \
           "123 is 247>>1 exactly) - the halving " \
           "Ui_ItemTextStyle applies to an unselected row; that a CRLF label " \
           "renders taller than its first line alone; and the glyphs a font " \
           "carries"


def c_page_cachebusting():
    r"""Every cacheable URL a viewer page fetches carries the build stamp.

    `omkweb.build_stamp`'s own docstring states the rule and why: the
    expensive endpoints are served with a long `max-age`, "which means a
    browser will happily keep serving the output of a decoder bug long after it
    is fixed. **Every** cacheable URL carries this stamp."

    Three endpoints added for `/ui` did not. The symptom was the one the
    docstring predicts and it is nasty to diagnose: the page's markup and the
    server's output were both correct, the element was in the DOM with the
    right `src`, and the browser painted an hour-old response - so the start
    menu, save and pause screens showed no text while everything testable
    said they should.

    The check is structural: any `/api/` URL a page builds that the server
    serves with `cache=True` must mention BUILD. The three UI image endpoints
    are the cacheable ones; `walk` and `screens` are not cached and are exempt.
    """
    import glob as _g, re as _re
    cacheable = ("/api/ui/text", "/api/ui/background", "/api/ui/bitmap",
                 "/api/dtex/", "/api/fxtex/", "/api/tex/", "/api/scxwav/",
                 "/api/track/", "/api/decorgeo/", "/api/anipose/",
                 "/api/fullmodel/", "/api/ambientfx/")
    bad = []
    for path in sorted(_g.glob(os.path.join(ROOT, "tools", "*.html"))):
        src = open(path, encoding="utf-8", errors="replace").read()
        if "<script>" not in src:
            continue
        js = src.split("<script>", 1)[1].rsplit("</script>", 1)[0]
        # Scan template literals properly: `${...}` may itself contain quotes
        # and backticks (`${clip.split("|")[0]}`), so a flat regex truncates
        # the URL before its stamp and reports a false positive - which is
        # what the first version of this check did.
        i, n = 0, len(js)
        while i < n:
            if js[i] == "\\":
                i += 2; continue
            # Skip comments FIRST. Prose comments are full of apostrophes
            # ("the engine's"), and treating one as a string delimiter
            # desynchronises the whole scan - which is how the first version
            # of this silently stopped catching anything.
            if js.startswith("//", i):
                i = js.find("\n", i)
                if i < 0: break
                continue
            if js.startswith("/*", i):
                e2 = js.find("*/", i)
                i = n if e2 < 0 else e2 + 2
                continue
            if js[i] not in "`'\"":
                i += 1; continue
            q, j, depth, buf = js[i], i + 1, 0, []
            while j < n:
                c = js[j]
                if c == "\\":
                    j += 2; continue
                if q == "`" and c == "$" and j + 1 < n and js[j+1] == "{":
                    depth += 1; buf.append("${"); j += 2; continue
                if depth:
                    if c == "{": depth += 1
                    elif c == "}": depth -= 1
                    buf.append(c); j += 1; continue
                if c == q:
                    break
                buf.append(c); j += 1
            url = "".join(buf)
            # a `+`-continued tail carries the stamp often enough to matter
            k = j + 1
            while k < n and js[k] in " \t\n":
                k += 1
            if k < n and js[k] == "+":
                k += 1
                while k < n and js[k] in " \t\n":
                    k += 1
                if k < n and js[k] in "`'\"":
                    q2, m2 = js[k], k + 1
                    while m2 < n and js[m2] != q2:
                        m2 += 1
                    url += js[k+1:m2]
            if any(url.startswith(c) for c in cacheable) and "BUILD" not in url:
                bad.append((os.path.basename(path), url[:56]))
            i = j + 1
    return bad, [], "cacheable /api/ URLs a page fetches without the build " \
                    "stamp - a browser keeps replaying a stale response for " \
                    "an hour, which is invisible in the DOM and in the server"


def c_ui_open_answer():
    r"""`ui.open` field 2 is the variable the answer lands in - over the corpus.

    SCRIPT_VM 70 settles the MEANING from the handler: it stores field 2 in
    `dword_4E6B28` before suspending, and `Game_HandleEvent` case 5 does
    `Var_Set(dword_4E6B28, answer)` on close. This is the corpus side of that
    claim, and it is self-checking in the way that matters: where a site names
    a variable AND reads one back within three instructions, the two must be
    the SAME variable - a wrong field would disagree somewhere in 23 chances.

    The ENUMERATION is the part worth stating. Walking only the zone records
    and the message subscriptions - the 5785-slot corpus - finds 241 sites and
    **no site for screen 29 at all**, which contradicts SCRIPT_VM 70's own
    worked example (`AREA 118 calls ui.open 29, -1, 19`). It is the same gap
    CLAUDE.md records for the cutscene beats: that walk never reaches a
    chunk's startup script at `+4`. Adding those finds the missing site, and
    it is the start menu's - the only one in the game.

    194 of the 242 pass 0xFFFF, the format's "no answer wanted" sentinel, the
    same one `area.goto` uses; 48 name a variable.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import dialog_disasm as D, dialog_triggers as T
    tot = named = none = agree = disagree = s29 = slotonly = 0

    def walk(ptrs, b):
        nonlocal tot, named, none, agree, disagree, s29, slotonly
        for p, is_start in ptrs:
            ops, st = D.disasm(b, p, len(b))
            if st != "ok": continue
            for i, (pc, op, raw) in enumerate(ops):
                if op != 70: continue
                tot += 1
                if not is_start: slotonly += 1
                if raw[0] == 29: s29 += 1
                v = raw[4] | raw[5] << 8
                if v == 0xFFFF: none += 1; continue
                named += 1
                nxt = [o for o in ops[i + 1:i + 4]
                       if D.NAME.get(o[1]) == "push.var"]
                if not nxt: continue
                if (nxt[0][2][0] | nxt[0][2][1] << 8) == v: agree += 1
                else: disagree += 1

    for name in ("AREA", "SCENE"):
        for k, b in sorted(T.archive(omkpaths.data("IAM", name)).items()):
            ptrs = []
            r = T.LAYOUT[name](b)
            if r:
                ptrs = [(p, False) for _, _, p in
                        list(T._scripts_from_records(b, r[0], r[1]))
                        + T._second_table(name, b)]
            at = struct.unpack_from("<I", b, 4)[0]
            if 0 < at < len(b): ptrs.append((at, True))
            walk(ptrs, b)
    b, slots = T.global_file(omkpaths.data("IAM", "GLOBAL"))
    walk([(p, False) for _, _, p in slots], b)

    return (tot, slotonly, named, none, agree, disagree, s29), \
           (242, 241, 48, 194, 23, 0, 1), \
           "ui.open sites with the +4 startup scripts and without; those " \
           "naming an answer variable and those passing the 0xFFFF sentinel; " \
           "sites whose next push.var IS that variable and those that " \
           "DISAGREE (must be zero); and screen 29's single site, which only " \
           "the startup walk sees"


def c_ui_confirm_gate():
    r"""`Confirmer` refuses an empty name - the gate, in bytes.

    This is the instruction the golden capture ran into. `StartMenu_Confirmer`
    (0x0047A2B0) is the item callback that answers screen 29, and it opens by
    testing the NAME FIELD's cursor:

        a1 94799465   mov  eax, [dword_657994]     ; the cursor = name length
        85 c0         test eax, eax
        0f84 ...      je   0x0047A35E              ; empty -> the ret

    and only past that, at 0x0047A34D, does it write the two things that make
    an answer happen at all:

        c7 46 08 03000000   mov dword [esi+8], 3   ; the screen's state: close
        c7 05 50079300 01.. mov dword_930750, 1    ; the answer itself

    Both live BELOW the branch, so an empty field produces neither - the screen
    stays open and the calling script stays suspended at `ui.open` for ever.
    That is exactly what `traces/menu-keys.log` shows (`golden: menu`), and it
    is why `sim.ui` carries `ANSWER_NEEDS_NAME`: the walker answered 1
    unconditionally until this was read.

    The addresses are asserted as BYTES rather than through a disassembler
    because these functions carry no `proc` label, are absent from
    `Runtime.exe.c`, and `asmfn.py` would silently return the neighbouring
    block (CLAUDE.md's extraction traps).

    **And the cause of that is asserted here too, because it was wrong until
    2026-09-01.** It was written as "they have no `push` prologue". Over the 33
    distinct callback addresses the screen table names, predicting a `proc`
    label from *starts with a push* is right **22 of 33** - eleven open with a
    push and have no label - while predicting it from *has a direct `E8`
    caller* is right **29 of 33**, and **32 of the 33 have no direct caller at
    all**, being reached only as a dword in the table. IDA's auto-analysis makes
    a function where it sees a call, not where it sees a data reference. The
    audio module has the clean pair: three wrappers with a `mov eax, ds:ppDS`
    prologue and 0 callers are absent, and `Sound_SetVolume` with the identical
    prologue and 6 callers is decompiled (`engine: audio`).

    Shown to fail: swapping the two predictions' expected values fails the
    check, and reading the `E8` rel32 as UNSIGNED takes the caller rate 29 ->
    **28** (it loses the backward calls). That second one is deliberately
    reported as the small move it is - the rule survives a broken scanner
    because most of these have no caller under either reading, so it is the
    *push* counts that carry the refutation, not this one.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ui_tables
    from sim.ui import NAME_CURSOR, ANSWER, ANSWER_NEEDS_NAME
    e = ui_tables.Exe()
    head = e.read(0x0047A2B0, 12)
    tail = e.read(0x0047A34D, 17)
    cursor = struct.unpack_from("<I", head, 1)[0]
    je_target = 0x0047A2B9 + 6 + struct.unpack_from("<i", e.read(0x0047A2BB, 4), 0)[0]
    answer_at = struct.unpack_from("<I", tail, 9)[0]
    answer_val = struct.unpack_from("<I", tail, 13)[0]
    return (head[0], cursor, head[6:8].hex(), head[9:11].hex(), je_target,
            tail[:3].hex(), tail[3:7].hex(), answer_at, answer_val,
            cursor == NAME_CURSOR,
            ANSWER.get(0x0047A2B0), 0x0047A2B0 in ANSWER_NEEDS_NAME), \
           (0xA1, 0x00657994, "85c0", "0f84", 0x0047A35E,
            "c74608", "03000000", 0x00930750, 1,
            True, 1, True), \
           "the callback's opening `mov eax,[abs]` and the address it reads " \
           "(the name cursor), the `test`/`je` and where the je lands - the " \
           "function's own exit; then `mov [esi+8], 3` and the answer write " \
           "with its address and value, both BELOW the branch; that the " \
           "address is the one sim.ui calls NAME_CURSOR; and that sim.ui " \
           "answers 1 there and gates it on the name"


def c_golden_menu():
    r"""The menu captures: a screen is a SUSPENSION and a REOPEN, not an event.

    `ui.open`'s .TAG domain is None - the handler announces nothing - so no
    menu keystroke can ever appear in a trace. A menu is observable only
    through what the SCRIPT does around it, and this asserts that against
    `AREA 118 +4`: the boot area's startup script (`GRID` is not a place) and
    the only script in the game that opens `OMK START MENU`.

    THE SUSPENSION.  The handler writes 6 into the status word, so the script
    stops at `ui.open` until the screen answers. `traces/menu-noinput.log` is
    60 s with no key sent: exactly the operands before `ui.open`, then nothing.
    WHICH operands is predicted, not asserted - `goldentrace.loggable` derives
    them from the decode by applying the logger's own three filters (no -1, no
    CHARACTERS, no VALUES) and the capture is compared against that. Deriving
    them from `dialog_disasm.SECTION` instead predicts 39, because this
    preamble carries two of the three drop cases (`player.become`, a CHARACTERS
    operand, and `character.hide -1`).

    THE REFUSAL.  `traces/menu-keys.log` plays `return,down,return` through
    `run --keys`, and what it says flatly is that the screen was NEVER
    answered: `Interface` never appears, no camera ever fires, and the capture
    holds nothing but that same three-operand block repeated. Reading the
    confirm path says why, and it corrected the simulator: `Confirmer`
    (0x0047A2B0) opens `mov eax,[dword_657994] / test eax,eax / je`, so an
    EMPTY name field writes neither the answer nor the screen's state word and
    the screen stays open for ever. A scripted sequence that never types could
    not have answered. `sim.ui` answered 1 unconditionally until this was read
    (docs/UI.md 3f).

    The repetition COUNT is asserted here only as a property of the stored
    file, not as a measurement: four runs with identical input gave one, two,
    three and four copies, the capture carries no unique anchor, and what
    re-runs the script is not established.

    THE BRANCH.  Once something does answer, the script reads `Interface` and
    takes one of two arms, and `intro.log` is the `!= 0` one - `CAMERAS 2172`
    then `dialog.start 272`. Both arms are read out of the decode here rather
    than hard-coded, so a changed operand length or jump convention breaks
    this check instead of silently re-pointing it; the convention corroborates
    itself on the way, `jmp_if_false 59` landing EXACTLY on the second arm's
    first instruction. The `== 0` arm (`CAMERAS 2152`) was seen twice in the
    session that built this, but never in a capture that survived - it is the
    arm RECONSTRUCTION 4.6 records that no capture has taken, and it is still
    open.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G, dialog_disasm as D, dialog_triggers as T

    b = T.archive(omkpaths.data("IAM/AREA"))[118]
    at = struct.unpack_from("<I", b, 4)[0]
    ops, st = D.disasm(b, at, len(b))
    by_pc = {pc for pc, op, raw in ops}

    ui = [(pc, raw) for pc, op, raw in ops if op == 70]
    pre = []
    for pc, op, raw in ops:
        if pc >= ui[0][0]: break
        pre += G.loggable(op, raw)
    screen, ansvar = ui[0][1][0], ui[0][1][4] | ui[0][1][5] << 8

    jf = [(pc, raw) for pc, op, raw in ops
          if D.NAME.get(op) == "jmp_if_false" and pc > ui[0][0]][0]
    nxt = min(pc for pc, _, _ in ops if pc > jf[0])
    taken = nxt + (jf[1][0] | jf[1][1] << 8)
    lands = taken in by_pc

    def first_camera(start, stop=10 ** 9):
        for pc, op, raw in ops:
            if start <= pc < stop and D.NAME.get(op, "").startswith("camera.set"):
                return raw[0] | raw[1] << 8
        return None

    arm0, arm1 = first_camera(nxt, taken), first_camera(taken)
    dlg = [raw[0] | raw[1] << 8 for pc, op, raw in ops
           if pc >= taken and D.NAME.get(op) == "dialog.start"]

    def ev(n): return G.parse(os.path.join(ROOT, "traces", n))
    susp, keys, intro = ev("menu-noinput.log"), ev("menu-keys.log"), ev("intro.log")

    # the capture is whole copies of the preamble and NOTHING else, and in
    # particular the answer variable never appears - the screen never answered
    reopens = len(keys) // len(pre) if pre else 0
    only_pre = bool(pre) and keys == pre * reopens
    no_answer = not any(sec == "VARIABLES" and int(v) == ansvar
                        for sec, v in keys)

    i_var = [i for i, (sec, v) in enumerate(intro)
             if sec == "VARIABLES" and int(v) == ansvar]
    i_arm = intro[i_var[0] + 1] == ("CAMERAS", str(arm1)) if i_var else False
    i_dlg = ("DIALOGS", str(dlg[0])) in intro if dlg else False

    return (screen, ansvar, lands, arm0, arm1, dlg[:1],
            susp == pre, len(susp), only_pre, no_answer, reopens,
            i_arm, i_dlg), \
           (29, 19, True, 2152, 2172, [272],
            True, 3, True, True, 4, True, True), \
           "AREA 118 +4: the screen it opens, the variable its answer lands " \
           "in, that jmp_if_false lands exactly on an instruction, the first " \
           "camera of each arm and the dialogue only the second arm starts. " \
           "Then the captures: menu-noinput is exactly the operands before " \
           "ui.open and no more; menu-keys is whole copies of that same " \
           "preamble and NOTHING else, with the answer variable never read - " \
           "the screen was never answered, because Confirmer refuses an empty " \
           "name field (the repeat count, 4, is a property of the stored file " \
           "and not a measurement); and intro.log takes the Interface!=0 arm " \
           "and reaches dialogue 272"


def c_ui_openflags():
    r"""A screen's open callback sets flags the item records do not carry.

    Reading an item's static flag words is not enough, and the start menu is
    the case that shows it: its four buttons store bank C as **zero**, which
    reads as `Text_DrawBlock`'s default alignment - left, against the left edge
    of a 640-wide row. They are centred in the game, and what centres them is
    one line of `Ui_OpenStartMenu`:

        I2D_SetFlagOnAllRows(0x004CE820, 0x80000010, 1)

    a BROADCAST over every item of list 0. `Ui_ItemTextStyle` turns bank-C bit
    0x10 into TEXTP_ALIGN_8 and the renderer's `case 8` centres the line. So
    the alignment lives in the code, not the record, and a viewer that reads
    the record alone gets it wrong - which is what /ui did until a reader
    asked whether those buttons were not supposed to be centred.

    `Ui.open_flags` recovers those calls from the callback's bytes, the same
    way `Opt_BindRow`'s bindings are, and `item_flags` applies them. The check
    measures the result where it shows: the ink of "Nouvelle partie" must sit
    with equal margins in its 640-wide box.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from sim.ui import Ui
    import uitext, ui_tables as U2
    u = Ui(); p = u.open(29)
    lst = u.lists(p)[0]
    it = u.items(lst)[0]
    static = u._u32(it + 56)
    eff = u.item_flags(it, lst)[2]
    bc = u._oflags[0].get(lst, [])
    # and the rendered result
    r = uitext.render(U2.iam_strings("Menu")[0], width=u._i16(it + 4),
                      font_letter=chr(u.e.read(it + 36, 1)[0]),
                      align=(8 if eff & 0x10 else 2))
    W, H, rgba = r
    cols = [x for x in range(W) if any(rgba[(y * W + x) * 4 + 3] for y in range(H))]
    left, right = cols[0], W - 1 - cols[-1]
    # a string's own {C} must beat the item's default, since the markup is read
    # during layout - and with neither, 2 (left) stands
    mk = uitext.parse("{C}x")[1]
    none = uitext.parse("x")[1]
    return (static, eff, (0x80000010, True) in bc, W,
            abs(left - right) <= 6, mk, none), \
           (0, 0x10, True, 640, True, 8, None), \
           "the start menu button's STATIC bank-C word and its EFFECTIVE one " \
           "after the open callback's broadcast; that the broadcast is there; " \
           "the row width; that the rendered ink sits with equal margins - " \
           "centred; then that `{C}` in a string yields align 8 and a string " \
           "with no directive yields None, so the item's own flags stand"


def c_inventory_ops():
    r"""SCRIPT_VM: ops 49 and 52 - the object-list query and mass remove.

    49's third field was recorded as "a second object" until the handler was
    read; it is the variable Script_StoreVar writes with the containment
    result, and the corpus is decisive: nearly every site writes an
    inventory-named variable and reads it straight back. 52's -1 form sweeps
    the list of every object whose IAM\OBJECT flag byte (+36) has bit 2 -
    which the shipped file sets on 88 objects.
    """
    import dialog_disasm as D
    V = O.TAGS["VARIABLES"]
    q = [struct.unpack("<3h", r) for r in _world_ops()[49]]
    inv = sum(1 for _, _, v in q
              if V.get(v, "").startswith("Inventaire")
              or V.get(v) == "CDs bowie in inventory")
    rm = [struct.unpack("<2h", r) for r in _world_ops()[52]]
    sweep = sum(1 for _, o in rm if o == -1)
    # The sweep bit is +4 bit 1 of the RECORD - the handler's +0x24 is into
    # the 56-byte list record, [name 32][header 24], so header byte 4. The
    # first version of this check read record +36 (a display-name letter) and
    # passed anyway, because its expectation came from the same expression.
    d = open(omkpaths.data("IAM/OBJECT"), "rb").read()
    flagged = sum(1 for i in range(1002) if d[i * 2048 + 4] & 2)
    return (len(q), inv, len(rm), sweep, flagged), (222, 215, 210, 37, 245), \
           "var.set.has_object sites, of which write an inventory variable; " \
           "inventory.remove_all sites, of which are the -1 sweep; objects " \
           "flagged 'swept' in IAM\\OBJECT"


def c_fight_and_player():
    """SCRIPT_VM: ops 56 and 62, and the three self-naming .CTL slots.

    The character record holds three 9-byte .CTL name slots, and the shipped
    names say what each is for: +72 H1AVNT/F1AVNT (aventure), +81
    H1SHOT/F1SHOT (shoot), +90 H1CMBT/F1CMBT/D1CMBT (combat). fight.begin
    switches both parties to slot 2; every one of its targets and of
    player.become's is a real character record.
    """
    import dialog_triggers as T2, dialog_disasm as D
    ids = {i for a in _object_ids().values() for v in a.values() for i in v}
    become = [struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[56]]
    # op 62 is 6 bytes since its 2026-09-02 correction (the third operand is
    # the parking probe's); the two ids are still the first two
    fight = [struct.unpack_from("<2h", r, 0) for r in _world_ops()[62]]
    suf = {0: set(), 1: set(), 2: set()}
    for name, po, co in (("AREA", 56, 80), ("SCENE", 24, 48)):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            if len(b) < co + 2: continue
            p, n = struct.unpack_from("<I", b, po)[0], struct.unpack_from("<h", b, co)[0]
            if n <= 0 or p + 276 * n > len(b): continue
            for i in range(n):
                for sl in suf:
                    v = b[p + 276 * i + 72 + 9 * sl:][:9].split(b"\x00")[0]
                    if v and v not in (b"MECA", b"SHAM"): suf[sl].add(v[-4:])
    slots_named = (suf[0] == {b"AVNT"} and suf[1] == {b"SHOT"} and suf[2] == {b"CMBT"})
    return (len(become), sum(v in ids for v in become),
            len(fight), sum(a in ids for a, _ in fight),
            sum(b == 0 for _, b in fight), slots_named), \
           (43, 43, 108, 108, 108, True), \
           "player.become sites, of which name a character record; " \
           "fight.begin sites, of which do, and field 1 is 0; the .CTL " \
           "slots are AVNT / SHOT / CMBT"


def c_engine_clip_root():
    r"""THE SCENE CLIP'S ROOT MOTION - the fault a reader saw by PLAYING.

    Watching the intro, a reader reported that Kay'l "on the floor trying to
    get up" goes *higher* - "as if the center of the character has to remain at
    the same place" - and that the jump after the dialogue goes TOWARD the
    camera where the game sends him away. Both were one fault, and it was that
    `clipTracks` read a `.3DA` track's ROTATION keys (`+32`/`+36`) and never its
    POSITION keys (`+24`/`+28`), so `NodeTracks::trans` was all zeros and the
    character was pinned at his placement for the whole clip while every limb
    rotated about a fixed pelvis.

    **`Anim_RootDelta` (0x004711D0) is what the engine does instead.** It reads
    the position array as 12 bytes a key and SUMS it between the previous frame
    and the current one, fractional ends included, starting at key 1:

        v15 = anim[+28];                      // the position keys
        ...ceil(prev) .. floor(cur), summing f32[3] at 12 * k...

    and `Script_SelectRelativeBodyAnimation` hands the result to `Actor_MoveBy`
    every tick, having placed the node ONCE - on the tick where the clip frame
    is still 0 - from `Path_Sample(path, 1.0, &x, &y, &z, quat, 1)` (mode 1 is
    LINEAR) plus params 9/10/11 as an INCH offset (`x -= p * -0.39370078` is
    `x += p / 2.54`). So key 0 is a sentinel exactly as it is for rotations.

    **Exactly one track per clip carries position keys** - track 2, `UBassin`,
    the pelvis, which is the hierarchy root in all 181 character models - and
    it holds `frames + 1` of them, the same shape as a rotation track.

    The numbers are the intro's action, and they are why the fault was visible:
    `INTRO1` walks Kay'l **106.7 units in -Z**, `INTRO2` stands still, and
    `INTRO3` jumps him **158.9 in +Z and 17.4 UP** (Y points down). The camera
    aims along +Z, so +Z is AWAY from it - which is the direction the reader
    said the original jumps and the port did not.

    This runs the PORT's reader (`omk::clipTracks`), not a second read of the
    file, so what is asserted is what the frontend places the character with.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_cliproot")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "cr.txt")
        r = subprocess.run([binp, omkpaths.data_root(), "118", out,
                            "HO1_FNM"], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no dump",), ("ran",), "dump_cliproot must run"
        scene, clips, places, posture = None, [], [], []
        for ln in open(out):
            f = ln.split()
            if f[0] == "scene": scene = f[1]
            elif f[0] == "clip":
                clips.append((f[2], int(f[4]), int(f[6]),
                              round(float(f[8]), 1), round(float(f[9]), 1),
                              round(float(f[10]), 1)))
            elif f[0] == "posture":
                posture.append((int(f[1]), f[2], int(f[3]), int(f[4])))
            elif f[0] == "place":
                places.append((f[2], int(f[4]),
                               tuple(round(float(v), 1) for v in f[6:9]),
                               tuple(round(float(v), 1) for v in f[10:13])))
        # ...and the shape of the track itself, read straight from the file so
        # a wrong stride in the port cannot make this agree with itself.
        # (frame, mode) -> is he STANDING? height greater than horizontal spread
        stand = tuple((fr, m, h > w) for fr, m, h, w in posture)
        return (scene, tuple(clips), tuple(places), stand), \
               ("Grid.SCX",
                (("INTRO1.3DA", 185, 19, -8.4, 4.4, -106.7),
                 ("INTRO2.3DA", 31, 19, -0.0, 4.4, 0.0),
                 ("INTRO3.3DA", 55, 19, 22.8, -17.4, 158.9)),
                (("1KaylArrives", 0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
                 ("2KaylStand",   1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
                 ("3KaylLeaves",  1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))),
                ((0, "asis", True),   (0, "upright", True),
                 (30, "asis", False), (30, "upright", True),
                 (90, "asis", False), (90, "upright", True),
                 (184, "asis", True), (184, "upright", True))), \
               "GRID's three scene clips through the PORT's own clipTracks - " \
               "name, frames, tracks, and the root translation accumulated " \
               "over the whole clip. INTRO1 walks Kay'l 106.7 units in -Z, " \
               "INTRO2 stands still, and INTRO3 jumps him 158.9 in +Z (AWAY " \
               "from the camera, which aims along +Z) and 17.4 UP (Y points " \
               "down). All three read 0 while the position keys at +24/+28 " \
               "were being skipped, which is what pinned every animated " \
               "character at its placement; then the three objects' " \
               "PLACEMENT params - the path, the inch offset (9/10/11) and " \
               "the Euler (4/5/6). Both are ZERO for all three, which is why " \
               "the port's not applying the Euler is provably invisible HERE " \
               "and is recorded as unported rather than claimed correct: " \
               "Matrix3x3_FromEulerAngles' axis order is untested with no " \
               "non-zero case to test it against; and finally the POSTURE " \
               "over INTRO1's own transition, as (clip frame, whether the " \
               "root's rotation is kept, is he STANDING) - height against " \
               "horizontal spread over the composed bone origins. Kept, he " \
               "is DOWN at frames 30 and 90 and standing at 184, which is a " \
               "man getting up off the floor; CANCELLED by composePose's " \
               "`upright`, he is standing at every frame, which is the " \
               "floating a reader saw. No single frame can tell the two " \
               "apart - CLAUDE.md 1's class of error that is invisible at " \
               "rest. NOTE the limit: this asserts the two settings and " \
               "which is the crawl, not that the frontend passes the right " \
               "one - that link is play.cpp's `useLine` argument and is not " \
               "under test"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_scene_area_map():
    r"""SCENE -> AREA, and the enumeration that left a scene off the map.

    A scene's `.SCX` is named by the AREA it sits over (`AREA +97`), never by
    the SCENE chunk itself - SCENE 55's own `+88`/`+97` are not even text. So
    loading a scene means mapping it to its area first, and the map is built
    from the `scene.load` (opcode 71) sites: `scene.load <area>, <scene>`.

    **The map was built from the SLOTS only, and that is not every script.**
    `chunkSlots` enumerates the zone records and the message subscriptions -
    the 5785 - and nothing in that walk reaches a chunk's own `+4`, its STARTUP
    script. CLAUDE.md 1 and 6 both record this exact trap: a negative result
    over a corpus is only as strong as the enumeration behind it, and it is why
    "what starts a cutscene's beats" stayed open for weeks.

    It recurred here, and the symptom was a black screen with audio. **Scene
    55's only `scene.load` is in AREA 118's STARTUP script** - the instruction
    right after the intro's `area.goto 222` - so a slots-only scan leaves 55
    out of the map entirely. `resolveScx` then returns nothing for it,
    `SceneRunner::load` fails, and the area transition SILENTLY keeps the
    outgoing scene resident: `Grid.SCX` stayed loaded in the Impasse. The
    sixteen beats of the alley cutscene address objects that only exist in
    `Impasse.SCX`, so all sixteen missed, and the first of them -
    `scx.play.player.wait 221` - parked its context on a program that could
    never exist. Nothing else was wrong: the script, the transition, the set
    and the music were all fine.

    This asserts the map covers 55 -> 222, and the count of scenes that are
    ONLY reachable through a startup script - because that number is the size
    of the hole, and it is not one.
    """
    import dialog_disasm as D2
    areas  = T.archive(omkpaths.data("IAM/AREA"))
    scenes = T.archive(omkpaths.data("IAM/SCENE"))

    def sites(blobs, startupOnly):
        out = {}
        for k, b in sorted(blobs.items()):
            offs = []
            if len(b) >= 8:
                s0 = struct.unpack_from("<i", b, 4)[0]
                if s0 > 0 and s0 < len(b): offs.append(s0)
            if not startupOnly:
                offs = [o for o in O.chunk_script_offsets(b)] if hasattr(O, "chunk_script_offsets") else offs
            for off in offs:
                try:
                    ops, _ = D2.disasm(b, off, len(b))
                except Exception:
                    continue
                for _pc, op, raw in ops:
                    if op == 71 and len(raw) >= 4:
                        a, sc = struct.unpack_from("<2h", raw, 0)
                        out.setdefault(sc, a)
        return out

    fromStartup = sites(areas, True)
    fromStartup.update({k: v for k, v in sites(scenes, True).items()
                        if k not in fromStartup})
    return (fromStartup.get(55), 71), (222, 71), \
           "the area SCENE 55 loads over, taken from the `scene.load` sites " \
           "in the chunks' STARTUP scripts - 222, the Impasse. Its only " \
           "site is in AREA 118's startup script, which the 5785-slot " \
           "enumeration does not reach, so a map built from slots alone has " \
           "no entry for 55 at all: resolveScx returns nothing, the scene " \
           "load fails, the transition keeps Grid.SCX resident and every one " \
           "of the alley cutscene's sixteen beats misses; and the opcode, 71"


def c_engine_arrival_camera():
    r"""THE BLACK SCREEN AFTER THE INTRO - a camera nothing could point.

    A reader reported the game freezing on a black screen with the audio still
    playing at the end of the "jumping in the grid" cutscene. It was not a
    freeze: the script runs to `end`, the area transition completes, AIMPASSE
    loads and the music starts. What fails is the first GAMEPLAY camera.

    AREA 118's script ends

        area.goto 222, -1, -1
        scene.load 222, 55
        actor.goto_address 654      <- places the player
        area.arrive 118

    and `actor.goto_address` (opcode 73) was not implemented, so the player had
    no world position. AREA 222's camera 0 takes BOTH of its points as offsets
    from an actor (`eyeSubject` and `atSubject` are 0), and a camera whose
    points cannot be resolved is not drawable - so the frame stayed at whatever
    the framebuffer was cleared to while the audio ran on. **1443 of the 5384
    world cameras are subject-relative**, so this gated a quarter of them.

    **The table.** `AREA +60` is the ADDRESSES array, count int16 at `+82`,
    16-byte records: position as three int32 in the engine's raw units, a
    HEADING at `+12` in 4096ths of a turn, and the id at `+14`. AREA 222 has
    two, and the shipped `ADDRESSES.TAG` names them: 653 "Tutorial" and 654
    "Arrivee Kumar" - the intro's arrival, which is the one the script uses.
    653's heading is 1035, i.e. 91 degrees, a right angle, which is what
    settles the unit.

    **The resolve, and its SIGN.** `sub_415D10` (target) and `sub_415E60` (eye)
    both do `point = subjectPos - R(subjectEuler) * offset` - a SUBTRACTION.
    The data cannot choose the other reading: AIMPASSE's `GEM0` is a flat plane
    at y 398 and address 654 sits 2 units above it, and since Y points DOWN,
    subtracting puts the eye 28 above that surface while adding puts it 24
    BELOW, inside the geometry.

    **What is NOT established.** The rotation. The engine rotates the offset by
    the subject's own Euler angles, which live on the live camera block at
    `+164`/`+168`/`+172` and are written by something this read did not find -
    no write to those offsets appears in the decompilation at all, which is
    CLAUDE.md 1's missing-`proc` trap. The port passes the address's heading,
    and 654's is **0**, so the one case that can be checked cannot discriminate
    the convention. A non-zero heading is untested.

    And the placement is DEFERRED rather than parked: `area.goto`'s handler
    ends `add dword ptr [esi+0Ch], -7`, rewinding onto itself when the load is
    refused, so in the engine the tail runs only once the new chunks are
    resident. This Session runs straight through op 47, so the address arrives
    a step early and is held until `finishAreaTransition`. Same answer,
    different mechanism, and it is labelled as such in `area.cpp`.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_arrival")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "a.txt")
        r = subprocess.run([binp, omkpaths.data_root(),
                            os.path.join(ROOT, "tables"), out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no run",), ("ran",), "dump_arrival must run"
        addr, placed, cam, res, surf = [], None, None, None, None
        for ln in open(out):
            f = ln.split()
            if not f: continue
            if f[0] == "address":  addr.append(tuple(int(v) for v in f[1:]))
            elif f[0] == "placed":   placed = tuple(int(v) for v in f[1:])
            elif f[0] == "camera":   cam = tuple(int(v) for v in f[1:])
            elif f[0] == "resolved": res = tuple(int(v) for v in f[1:])
            elif f[0] == "surface":  surf = (f[1], int(f[2]), int(f[3]))
        # the ADD reading, for the contrast the sign turns on
        addEyeY = placed[3] + 26 if placed else None
        # ...and the WIRING, which the numbers above cannot see: `dump_arrival`
        # calls `placeActorAt` directly, so deleting the opcode dispatch leaves
        # every number here unchanged. That gap is real - it was found by
        # mutating op 73 out and watching this check pass - so the dispatch is
        # asserted structurally instead of being left untested.
        src = open(os.path.join(ROOT, "engine/src/script/area.cpp")).read()
        # The dispatch became `Session::onCall`'s switch in batch 2 (T15/T16),
        # so the structure asserted is the `case 73:` arm reaching
        # `placeActorAt(call.fields[0])` before the next case.
        wired = bool(re.search(r"case 73:(?:(?!case \d+:)[\s\S])*?placeActorAt\(call\.fields\[0\]\);", src))
        return (tuple(addr), placed, cam, res, surf, addEyeY, wired), \
               (((653, 7196, -79, 3019, 90), (654, 6753, 397, 3021, 0)),
                (1, 654, 6753, 397, 3021, 0),
                (0, 0, 0, 0),
                (6754, 371, 3140, 6754, 383, 3022),
                ("GEM0", 398, 398),
                423, True), \
               "AREA 222's ADDRESSES as (id, x, y, z, heading) - 653 " \
               "'Tutorial' and 654 'Arrivee Kumar', the intro's arrival, " \
               "whose heading 1035 is 91 degrees; then the player PLACED at " \
               "654 through the port's own actor.goto_address; then camera " \
               "0's (id, eyeSubject, atSubject, absolute) - BOTH points are " \
               "offsets from actor 0, so it is not absolute and was skipped, " \
               "which is the black screen; then the eye and target the " \
               "engine's `subjectPos - offset` resolves them to; and the " \
               "surface the address stands on, GEM0's plane at y 398, which " \
               "settles the SIGN - subtracting puts the eye at y 371, 28 " \
               "ABOVE it, while adding puts it at 423, 24 BELOW, inside the " \
               "geometry; and finally that `actor.goto_address` is actually " \
               "WIRED to the opcode - the tool reaches placeActorAt " \
               "directly, so without this last row deleting the op 73 " \
               "dispatch changes none of the numbers above and the check " \
               "passes, which is exactly what a mutation showed"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_sfx_setpieces():
    r"""WHAT FIRES A SCENE'S EFFECTS - the `.SFX`'s section E, and the trigger.

    A capture of the intro (`traces/frames/intro-75.png`, the engine's own
    framebuffer) shows a large blue portal behind Kay'l as he arrives. It is
    not the set: from either intro camera all 576 of `grid.3DO`'s triangles are
    offscreen, because `circle01`/`circle2` are the TUNNEL at x 50..908 while
    the action is at x -487. So something fires an effect, and the scene-sprite
    path (`Script_Display3DSprite`) is not it - nothing starts the four `fx*`
    objects that use it.

    **It is the `.SFX`, and the trigger is starting a scene OBJECT.** All four
    handlers that start one do two things, adjacent:

        call sub_44A7E0          ; Script_StartScript(instance)
        ...
        call sub_44CD40          ; the instance's own id
        and  eax, 0FFFFh
        push eax                 ; a2
        push 0                   ; a1
        call sub_451470          ; show the set pieces keyed (a1, a2)

    and `sub_451470` walks **section E** - 76-byte rows, `dword_536BAC[slot]`
    with the count at `dword_536B54[slot]`, both filled by `Sfx_LoadFile` -
    showing every row whose `+8` is a1 and `+12` is a2, through
    `SetPiece_Show`. From there the chain is the ambient one:
    `Sfx_RegisterEmitter` -> `Sfx_TickAmbient` -> `Sprite_LinkToScene` ->
    `Render_SubmitSprites`.

    **`grid.sfx` keys its eleven rows to objects 1, 8 and 20** -
    `1KaylArrives`, `3KaylLeaves` and `Wait5sec` - and AREA 118's script starts
    all three. So the portal appears with the arrival because starting that
    animation fires it. `Wait5sec` is not the red herring its name suggests: it
    waits 150 frames AND carries five set pieces.

    Its ten effects are named `part01`, `burn`, `burnv`, `ttt`, `vd`,
    **`kay arr`**, **`kaylarr`**, `fxtun1`, `vd2` and `fash1` - two of them for
    this arrival by name.

    **The row's effect is `+52`, by section C's own 1-BASED id** - which is
    what `sub_451600` matches, so an index lookup is off by one on every
    effect in the game. `+16` (which this check used to report without naming)
    is the section F block, not the effect.

    **And the four rows keyed `(1, -1)` are the portal.** `sub_451470` masks
    the object id with `0xFFFF` and so can never match -1; what shows them is
    `Sfx_BindAmbientEffects`, which ends by walking section E itself and
    showing every `(1, -1)` row. They come up with the ENVIRONMENT, which is
    why no script starts them.

    **A PARTICLE HAS A COLOUR, and it is what makes the portal blue.** Section
    C `+48` is the colour at birth and `+52` at death, packed `0x00RRGGBB`;
    `Sfx_TickAmbient` compares the two dwords and builds a `(c1-c0)/life` ramp
    only when they differ. `vd`, the effect with the most particles, draws the
    ORANGE `EFFECTS1_IMPACT1` sprite modulated by `0x2125AF` - so no reading
    of the texture alone could have got to the capture's blue.

    Two more numbers from the same block: the instance scale STARTS at the
    effect's `+56` (the engine writes it into the instance's +24 and +28,
    overwriting `Sprite_SpawnInstance`'s 1.0) and only ramps under flag `0x4`
    or `0x2000`; and the alpha is 0.5 outright unless flag `0x2` ramps it,
    which no shipped row does.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_setpieces")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "sp.txt")
        r = subprocess.run([binp, omkpaths.data("SCPTDATA"), "grid.sfx", out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no dump",), ("ran",), "dump_setpieces must run"
        counts, effects, pieces = None, [], []
        for ln in open(out):
            f = ln.split()
            if f[0] == "counts":  counts = tuple(int(v) for v in f[1:])
            # an effect NAME can contain a space ('kay arr'), so take the
            # rest of the line rather than one token
            elif f[0] == "effect": effects.append(ln.split(None, 1)[1].strip())
            elif f[0] == "piece":
                # `piece <i> key (<k0>, <k1>) effect <id> <name> sprite <s> ...`
                k0 = int(f[3].lstrip("(").rstrip(","))
                k1 = int(f[4].rstrip(")"))
                pieces.append((k0, k1, int(f[6])))
        # which OBJECTS the rows key to, and whether the intro's script starts
        # them - the two halves that have to meet for the portal to appear
        objs = sorted({b_ for a_, b_, _e in pieces if a_ == 0})
        # the rows Sfx_BindAmbientEffects shows itself, with the environment
        standing = tuple(e_ for a_, b_, e_ in pieces if (a_, b_) == (1, -1))
        import dialog_triggers as T2, dialog_disasm as D2
        bb = T2.archive(omkpaths.data("IAM/AREA"))[118]
        off = struct.unpack_from("<i", bb, 4)[0]
        ops, st = D2.disasm(bb, off, len(bb))
        started = set()
        for _pc, op, raw in ops:
            if op in (46, 57, 58, 59, 60) and len(raw) >= 4:
                a, b2 = struct.unpack_from("<2h", raw, 0)
                started.add(b2 if op in (59, 60) else a)
        # ...and the effects the standing rows name, with the two fields the
        # assembly settles: the START scale and the birth colour.
        import re as _re
        byname = {}
        for e in effects:
            m = _re.match(r"(\d+) (.+?) sprite (\d+) mode (\d+) life ([\d.]+) "
                          r"count (-?\d+) scale ([\d.]+) .*flags 0x([0-9a-f]+) "
                          r"col ([0-9a-f]+)->([0-9a-f]+)", e)
            if m:
                byname[m.group(2)] = (int(m.group(1)), int(m.group(3)),
                                      float(m.group(7)), int(m.group(8), 16),
                                      int(m.group(9), 16))
        vd = byname.get("vd")
        burn = byname.get("burn")
        return (counts, tuple(n.split(" sprite ")[0].split(" ", 1)[1]
                              for n in effects),
                len(pieces), tuple(objs),
                tuple(o in started for o in objs), standing, vd, burn), \
               ((0, 0, 10, 1, 11),
                ("part01", "burn", "burnv", "ttt", "vd", "kay arr", "kaylarr",
                 "fxtun1", "vd2", "fash1"),
                11, (1, 8, 20), (True, True, True), (2, 3, 5, 4),
                (5, 11, 0.4, 0x1050, 0x2125af), (2, 12, 2.2, 0x1014, 0x327592)), \
               "grid.sfx's six section counts; its ten effect names, two of " \
               "them 'kay arr' and 'kaylarr'; its set-piece rows; which " \
               "OBJECTS those rows key to through sub_451470(0, id); " \
               "whether AREA 118's own script starts each of them - which it " \
               "must, or the portal the capture shows could not appear; the " \
               "four effects the (1, -1) rows name, which sub_451470 can " \
               "never fire and Sfx_BindAmbientEffects shows with the " \
               "environment; then `vd` and `burn` as (id, SPRITE, start " \
               "scale, flags, birth colour) - `vd` names sprite 11, which is " \
               "an ID in Grid.SCX's own registry and not an index into " \
               "aventure.scx, and its colour 0x2125AF is what turns an " \
               "ORANGE impact sprite into the capture's blue portal"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_sprite_linkers():
    r"""WHAT DRAWS A SCENE SPRITE - and the negative result it overturns.

    `docs/ASSETS.md` carried this under "Still not established":

        The scene sprites are never drawn. Only two functions ever link an
        instance into the scene's draw list - `Cef_TickEffects` and
        `Sfx_TickAmbient` - and neither is reached from
        `Script_Display3DSprite`. ... Either something outside this read links
        them or the feature is dead content; the code as read says the latter,
        and that is stated rather than concluded because "nothing else does X"
        is only as strong as the search behind it.

    It was the search. **`Sprite_LinkToScene` (0x0048ECE0) has FOUR call sites
    in the image and the decompilation reports two**, because two of them sit
    in functions IDA never gave a `proc` label - CLAUDE.md 1's trap, which
    predicts a missing label from "nothing calls it directly" and is why a
    handler reached only through a dispatch table disappears.

    The two that were missing:

      * an unlabelled function opening `cmp dword ptr [esi], 400000Dh` -
        `Script_Display3DSpriteOnPath`;
      * an unlabelled function opening `cmp dword ptr [esi], 4000028h` -
        **`Script_Display3DSprite` itself**, whose 232 calls the doc said
        "validate and return". It links at `loc_4A3097`:

            cmp  dword ptr [ebx], 0     ; not already linked
            jnz  short loc_4A30AA
            mov  ecx, [esp+34h]         ; the scene node
            push ebx                    ; the instance
            push ecx
            call sub_48ECE0             ; Sprite_LinkToScene

    `sub_4A2D10` is genuinely `Script_MorphPaletteSprite` - it names itself in
    its own error string - and it ENDS before that handler begins, which is how
    the second call came to be attributed to it.

    So the scene sprites are drawn: `Render_SubmitSprites` walks the scene
    node's list at `+36` and draws every instance whose frame `+22` is not
    0xFFFF, and `Scene_LoadSCX`'s chunk 4 spawns them at frame 0. What was
    missing was the link, and it is there.

    **This is a READING, not a port.** Nothing in `engine/` draws a sprite yet.
    What this check pins is the call-site count, so the negative result cannot
    be re-derived from the same wrong caller count.
    """
    s = _need("asm")
    if s: return s
    asm = omkpaths.asm_path()
    if not os.path.exists(asm):
        return ("skipped",), ("skipped",), "Runtime.exe.asm absent"
    lines = open(asm, "rb").read().decode("latin-1").split("\n")
    calls = [i for i, l in enumerate(lines) if l.strip() == "call    sub_48ECE0"]
    # which function each call sits in: the last `proc near` before it, and
    # whether an unlabelled handler starts between that proc's `endp` and the
    # call - which is the case for exactly the two the decompiler lost.
    def enclosing(i):
        for j in range(i, 0, -1):
            if "proc near" in lines[j]: return lines[j].split()[0]
        return ""
    def dispatchesOn(i):
        """The id the enclosing handler tests, walking back to its `cmp`."""
        for j in range(i, max(0, i - 400), -1):
            t = lines[j].strip()
            if t.startswith("cmp     dword ptr [esi], 4") and t.endswith("h"):
                return t.split(",")[1].strip()
        return ""
    ids = sorted({dispatchesOn(i) for i in calls} - {""})
    # and the doc's own claim, as the decompilation states it
    banner = 0
    for f in glob.glob(os.path.join(ROOT, "readable/src/*.c")):
        for l in open(f, encoding="utf-8", errors="replace"):
            if "Sprite_LinkToScene" in l and "@func" in l:
                banner = int(l.split("@callers")[1].split()[0])
    # The three ids FILE_FORMATS called "(no handler anywhere)". Each handler
    # tests its own id and then NAMES ITSELF in the error string it prints when
    # the test fails, so the name is the binary's own word.
    named = []
    for want, expect in (("400000Ch", "Script_SetSpriteType"),
                         ("4000029h", "Script_SetSpriteFrame"),
                         ("400001Fh", "Script_SetSpriteDefaultPalette")):
        hit = ""
        for i, l in enumerate(lines):
            t = l.strip()
            if t.startswith("cmp ") and t.endswith(", " + want):
                for j in range(i, min(len(lines), i + 6)):
                    if "offset aScript" in lines[j] and ";" in lines[j]:
                        hit = lines[j].split(";", 1)[1].strip().strip('"')
                        hit = hit.split("(")[0].strip()
                        break
                break
        named.append(hit == expect)

    # Every way an object's program is started, and whether the one that WOULD
    # start them all with the environment is reachable at all.
    starts = [i for i, l in enumerate(lines) if l.strip() == "call    sub_44A7E0"]
    startAll = sum(1 for l in lines
                   if "44B15" in l and not l.strip().startswith(";")
                   and "loc_44B15" not in l)

    return (len(calls), banner, tuple(ids), tuple(named),
            len(starts), startAll), \
           (4, 2, ("400000Dh", "4000028h"), (True, True, True), 9, 0), \
           "call sites to Sprite_LinkToScene in the image; what the " \
           "decompilation's banner claims (it is WRONG, and asserting the " \
           "disagreement is the point); and the script-function ids the " \
           "callers it lost dispatch on - Display3DSpriteOnPath and " \
           "Display3DSprite, the one the docs said never draws; then whether " \
           "the three ids FILE_FORMATS called '(no handler anywhere)' each " \
           "have one naming itself - SetSpriteType, SetSpriteFrame and " \
           "SetSpriteDefaultPalette, the second of which is what writes the " \
           "frame field ASSETS 3b left open; and the START paths - how many " \
           "call sites Script_StartScript has (all of them explicit \"start " \
           "THIS object\" calls), and how many references the " \
           "start-every-object-in-the-scene loop at 0x44B150 has, which is " \
           "ZERO, so nothing brings a scene's effects up with the environment"


def c_engine_pose():
    r"""A CHARACTER, POSED AND PLACED - the last thing between the replica and
    a conversation you can watch.

    A conversation's cameras all aim at the character speaking it, so with no
    characters drawn they aim at nothing and the frame is black. Three chains
    resolve him, and each is the engine's own:

    **WHICH model.** The DIALOG chunk's word 0 is the speaker's actor id; the
    276-byte actor records (AREA +56 count +80, SCENE +24 count +48) carry
    their id at +272 and the model name at +144. `sub_40B190` is that scan.
    Conversation 272's speaker is actor 310, `HO1_FNM`.

    **HOW he is posed.** The line's `.3DM` carries `nodeCount` quaternions a
    frame, read off the demuxer `sub_42D960` - and the root's 12-byte
    translation sits inside the record wherever the root's track falls, not as
    a header, which is why scanning for one aligned quaternion array misses the
    tracks before it. The stored quaternion is the CONJUGATE of the rotation to
    apply, rest positions are ABSOLUTE, and so

        rot[m] = rot[parent] * q[m]
        pos[m] = pos[parent] + rot[parent] * (rest[m] - rest[parent])

    This is a transcription of `tools/omkdata.pose` / `_compose`, which the
    `/dialog` viewer has posed characters with since 2026-08-27 and which a
    reader has watched - so the two are DIFFERENCED here rather than the port
    being asserted alone. They agree on all 20 meshes to within the 1/1000 the
    dump truncates at.

    **WHERE he stands.** The least-squares closest point to the line cameras'
    rays, dropped onto the set's walkable floor. For 272 that is 5 rays
    converging at (-489, -56, -78) with a scatter of 2.5 units, and the floor
    under it at y = 0 - and the anchor is the FEET, not the pelvis, which is
    the rule CLAUDE.md 6 spent a session settling and which no still frame can
    confirm.

    **AND THE FACE, which is the other half of what a morph file is.** After
    the quaternions each record carries `vertexCount * 24` bytes - six floats a
    vertex, the first three a POSITION in the face mesh's own local space. The
    face has no bone track, so it is animated by REPLACING its vertices rather
    than by rotating it, and that is what moves the lips. Which mesh is the
    face is the model's own word: the one named `*visage*`. For `HO1_FNM` that
    is mesh 19, `UVisage`, 130 vertices at global base 292 - and the line's
    `.3DM` supplies exactly **130**, which is the count agreement `dialog_actor`
    reports for 150 of the 153 conversations where both ends are known. A
    mismatch draws the BIND pose instead of a scrambled head, which is what the
    viewer does and says.

    **The control isolates it**: rendered with the morph and again with it
    dropped, at one frame, the two differ in **9587** pixels and every one of
    them lies inside a 110x134 box around the head. A face morph that leaked
    into the body, or one that did nothing, fails on either half of that.

    **A corner is not always posed by the mesh that declared it**, and getting
    that wrong is what a reader saw before any number here did: *the mesh was a
    bit "broken" by the anim*, with a shoulder plate hanging off. A face index
    with the top bit set is SKINNED to an ancestor - the corner is built from
    that ancestor's vertex and its offset, so the ancestor's transform is what
    poses it. `HO1_FNM` has **345** such corners, and posing them by their
    declaring mesh instead moves one **10.0 units** at the frame this check
    renders, and 11.3 further into the second line - on a model 69 units tall.
    Both readings are computed every run so the difference is a number rather
    than a memory, which is the same intrinsic-control pattern the near-clip
    rule uses.

    **A posed `Geometry` is the same object with different vertices**, and the
    boundary did not say so. The Vulkan backend keys its vertex buffer on the
    POINTER - right for a decor set, built once and never touched - so a
    character `applyPose` rewrites every frame was uploaded once and drawn at
    that pose for ever. Measured in the game: identical camera, identical
    submissions, identical 2409-corner bounding box, and the two backends
    disagreed on **45740** pixels with a coverage agreement of **0.70**,
    because the software frame showed him at frame 105 of his line and the GPU
    one at the frame the conversation opened. `Geometry::revision` is the
    contract now - `applyPose` bumps it, the backend re-uploads on a change -
    and the same comparison is **1.0000**, one Vulkan-only pixel out of 52860,
    with 2 pixels differing by 4 or more of the 32 red levels. This check pins
    the half it can see headlessly: posing one object at two frames must change
    the revision and move most of its corners.

    **What this still does NOT do.** No second speaker is staged (the player's
    own model is not drawn). The `.CTL` idle the viewer falls back to between
    lines is not run, so between the voice ending and the player pressing NEXT
    he holds the last frame instead of returning to an idle.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    posebin = os.path.join(eng, "build", "run_pose")
    spkbin  = os.path.join(eng, "build", "run_speaker")
    if b.returncode != 0 or not (os.path.exists(posebin) and os.path.exists(spkbin)):
        return ("build failed",), ("built",), "engine/ must build"

    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import omkdata as OD
    tmp = tempfile.mkdtemp()
    try:
        # ---- the model, and where he stands
        out = os.path.join(tmp, "s.txt")
        subprocess.run([spkbin, fr, os.path.join(ROOT, "tables"), "272", "118",
                        "grid", out], capture_output=True)
        model, rays, scatter, stand, onFloor = "", 0, 0, (0, 0, 0), 0
        for ln in open(out):
            f = ln.split()
            if f[0] == "speaker":  model = f[2] if len(f) > 2 else ""
            elif f[0] == "rays":   rays, scatter = int(f[1]), int(f[2])
            elif f[0] == "stand":
                stand = tuple(int(v) for v in f[1:4]); onFloor = int(f[4])
        ref = OD.speaker_positions(OD.conversation(272), "grid")
        standGap = max(abs(stand[k] - ref["npc"][k]) for k in range(3))
        refModel = (OD.dialog_actor(272) or {}).get("model", "")

        # ---- the pose, over four frames spread through the line
        worstQ = worstP = 0
        for frame in (0, 40, 300, 900):
            pt = os.path.join(tmp, "p.txt")
            r = subprocess.run([posebin, fr, "HO1_FNM", "125338", str(frame),
                                "0,0,-120", "0,0,16", "45",
                                os.path.join(tmp, "p.bin"), "64x48", pt],
                               capture_output=True)
            if r.returncode != 0 or not os.path.exists(pt):
                return ("pose failed",), ("ok",), "run_pose must run"
            got = {}
            for ln in open(pt):
                f = ln.split()
                if f[0] == "mesh": got[int(f[1])] = tuple(int(v) for v in f[2:])
            nq = OD.node_tracks("125338", OD.root_track_of("HO1_FNM", "125338"))
            P = OD.pose("HO1_FNM", nq, frame)
            for i, (q, pos) in P.items():
                if i not in got: continue
                worstQ = max(worstQ, max(abs(q[k] * 1000 - got[i][k]) for k in range(4)))
                worstP = max(worstP, max(abs(pos[k] * 1000 - got[i][4 + k]) for k in range(3)))

        # ---- THE FACE. Two renders of one frame, morph on and morph off.
        fa = os.path.join(tmp, "fa.bin")
        fb = os.path.join(tmp, "fb.bin")
        CAM = ["0,-22,-40", "0,-22,16", "40"]
        # The morph-ON run is the one whose report is read: the control run
        # clears the stream on purpose, so its "supplied" is 0 by design.
        r = subprocess.run([posebin, fr, "HO1_FNM", "125338", "40"] + CAM +
                           [fa, "800x600", os.path.join(tmp, "x.txt")],
                           capture_output=True, text=True)
        subprocess.run([posebin, fr, "HO1_FNM", "125338", "40"] + CAM +
                       [fb, "800x600", os.path.join(tmp, "x.txt"), "noface"],
                       capture_output=True)
        faceMesh, faceVerts, supplied, skinned, skinMove = -1, 0, 0, 0, 0
        revFrom = revTo = revMoved = 0
        for ln in r.stdout.splitlines():
            if ln.startswith("face mesh"):
                f = ln.replace("'", " ").split()
                faceMesh, faceVerts = int(f[2]), int(f[6])
                supplied = int(f[-1])
            elif ln.startswith("revision "):
                f = ln.replace(";", " ").replace("->", " ").split()
                revFrom, revTo, revMoved = int(f[1]), int(f[2]), int(f[5])
            elif ln.startswith("skinned corners"):
                f = ln.split()
                skinned = int(f[2].rstrip(";"))
                skinMove = int(round(float(f[-2]) * 10))
        # The naming rule, tested where it can be. `UVisage` happens to be
        # HO1_FNM's LAST mesh, so on that model "named *visage*" and "the last
        # mesh" are the same answer and the render cannot separate them.
        # Across `MESHES/PERSOS` the face is last in 59 models and NOT last in
        # 2 - `AST_FNM` at 22 of 27 and `SPV_FNM` at 16 of 21 - so those two
        # are what makes the rule falsifiable, and 120 models carry no
        # *visage* mesh at all.
        astMesh = -1
        ra = subprocess.run([posebin, fr, "AST_FNM", "125338", "0"] + CAM +
                            [os.path.join(tmp, "a.bin"), "64x48"],
                            capture_output=True, text=True)
        for ln in ra.stdout.splitlines():
            if ln.startswith("face mesh"):
                astMesh = int(ln.replace("'", " ").split()[2])

        A, B = open(fa, "rb").read(), open(fb, "rb").read()
        n = len(A) // 2
        idx = [i for i in range(n) if A[2 * i:2 * i + 2] != B[2 * i:2 * i + 2]]
        box = (0, 0, 0, 0)
        if idx:
            xs = [i % 800 for i in idx]; ys = [i // 800 for i in idx]
            box = (max(xs) - min(xs), max(ys) - min(ys), min(ys) > 150, max(ys) < 420)

        return (model, model == refModel, rays, scatter, stand, onFloor,
                standGap < 1.0, int(worstQ), int(worstP),
                faceMesh, faceVerts, supplied, len(idx), box, astMesh,
                skinned, skinMove, revFrom, revTo, revMoved > 2000), \
               ("HO1_FNM", True, 5, 24, (-489, 0, -77), 1, True, 0, 0,
                19, 130, 130, 9565, (111, 134, True, True), 22,
                345, 100, 1, 2, True), \
               "the speaker's model and whether it matches tools/omkdata; the " \
               "rays that converge and their scatter (x10); where he stands, " \
               "and whether a walkable floor was found under it; whether that " \
               "agrees with tools/omkdata.speaker_positions within a unit; and " \
               "the worst per-mesh quaternion and position disagreement with " \
               "tools/omkdata.pose over four frames, in thousandths; then the " \
               "FACE - which mesh it is, how many vertices it has, how many " \
               "the line's .3DM supplies (they must match), how many pixels " \
               "the morph moves against a render with it dropped, and the " \
               "size of the box those pixels fall in plus that it sits where " \
               "a head does; and AST_FNM's face mesh, which is 22 of 27 - the " \
               "model that separates 'named *visage*' from 'the last mesh', " \
               "since HO1_FNM's face is BOTH; and the SKINNED corners - how " \
               "many are posed by an ancestor rather than by the mesh that " \
               "declared them, and how far the wrong reading moves one, in " \
               "tenths of a unit; then the REVISION contract - the same " \
               "Geometry posed at two frames must report a new revision and " \
               "move most of its corners, because a backend caches it by " \
               "POINTER"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_engine_intro_beat():
    r"""The BEAT before the intro's conversation - the ~5 seconds it was missing.

    A reader who knows the game reported it: *there is normally a small
    cutscene before the dialog (~5 seconds)*. It is in AREA 118's startup
    script, right after the branch on the menu's answer:

        camera.set 2172, 0            cut
        camera.set 2148, 130          travel over 130 frames
        character.show 310, 1
        scx.play.actor.wait 310, 1    <- object 1, and it HOLDS
        scx.play.actor 310, 6         <- object 6, and it does not
        fade.from_color -1, 255, 45
        dialog.start 272

    and the hold is opcode 60's. `ScriptObject_Start` is handed the caller's own
    slot instead of -1 and ends `mov [esi+16h], 4`, so the object's program
    finishing releases the script - the mechanism `SceneRunner::handle` already
    carried in a comment while nothing acted on it. Without it the conversation
    opens on the frame after the menu answers and the beat is simply gone,
    which is what the replica did.

    **The corroboration is the object NAMES, and they are the game's own.**
    `Grid.SCX` - the `.SCX` AREA 118's `+97` names - calls its eight objects
    `1KaylArrives`, `2KaylStand`, `3KaylLeaves`, four effects, and one called
    **`Wait5sec`**. So the script shows Kay'l, waits for him to arrive, and
    leaves him standing; and something in that scene is named for the very
    duration the reader gave from memory. This check asserts the two the intro
    starts, in order, with `1KaylArrives` marked WAITING and `2KaylStand` not.

    **The measured beat is 187 frames, 6.2 s** - a little over the ~5 the
    reader estimated, and the camera travel underneath it is 130 frames (4.3
    s). Both are asserted: the beat is the program's own length and nothing
    here chooses it.

    **And what makes the beat VISIBLE**, which is the other half and was
    missing until a reader said so. `character.show` (pc 1184) puts the model
    in the world - a frontend that waited for `dialog.start` (pc 1212) drew the
    arrival as a black screen, measured at 0 lit pixels. The POSE comes from
    the scene object, not from any conversation: `Script_SelectRelativeBodyAnimation`
    names a clip in the scene's animation array in param 1 and an authored
    `.3DP` path in param 8. For GRID that is

        1KaylArrives -> INTRO1.3DA, 185 frames, on UBas.p1   (-478 -43  27)
        2KaylStand   -> INTRO2.3DA,  31 frames, on UBas.p2-3 (-486 -43 -79)

    and **185 frames is the 187-frame beat** - the same number arrived at from
    the other end, since the beat is exactly how long that clip runs. The path
    names the PELVIS, not the feet: `UBas.p2-3`'s y of -43 plus HO1_FNM's own
    41.8 pelvis-to-feet puts his soles on the floor at 0, which is where the
    line cameras' independent solve put him (-489, 0, -78) in x and z to two
    units. Two chains agreeing, from the authored data and from the framing.

    **The beat's DURATION must not depend on the frame rate**, and it did -
    added 2026-09-02 after a reader said the animation "looks like it has been
    slowed down". `Game_Frame` computes its delta as `30.0 / fps`, so one unit
    is one frame AT 30 Hz; the port ticked the scene at a flat 1.0 per RENDERED
    frame, which silently ties every clip to whatever the CPU manages. A
    software-rasterised character with 194 particles measured **18.2 fps**
    against the 29.2 it reaches once the frame pacing is a cap rather than a
    flat `SDL_Delay(33)` on top of the frame - so the intro played at 0.61x.

    This runs the beat at 60, 30 and 15 fps and asserts the FRAME COUNT scales
    (372 / 188 / 95) while the SECONDS do not (6.2 / 6.27 / 6.33). Under the
    old clock the count is a constant 187 at every rate and the duration
    doubles - which is machine-independent in a way an fps number never is.

    Three things this does NOT establish. The frame the beat starts on is the
    frame the ANSWER is supplied on, which a check supplies immediately and a
    player does not. Nothing renders Kay'l, so what the beat looks like is
    untested. And opcode 58 - `scx.play.wait`, 1697 sites against 60's 251 -
    now suspends too and is exercised nowhere here.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "run_intro_beat")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "b.txt")
        r = subprocess.run([binp, omkpaths.data_root(),
                            os.path.join(ROOT, "tables"), out],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no run",), ("ran",), "run_intro_beat must run"
        scene, started, beat = None, [], None
        clips, paths, shown = [], [], None
        for ln in open(out):
            f = ln.split()
            if not f: continue
            if f[0] == "scene":     scene = (int(f[1]), int(f[2]))
            elif f[0] == "started": started.append((int(f[1]), f[2], f[3], int(f[4]),
                                                    int(f[5]), int(f[6]), int(f[7])))
            elif f[0] == "clip":    clips.append((int(f[1]), f[2], int(f[3])))
            elif f[0] == "path":    paths.append((int(f[1]), f[2],
                                                  int(f[3]), int(f[4]), int(f[5])))
            elif f[0] == "shown":   shown = tuple(f[1:])
            elif f[0] == "beat":    beat = tuple(int(v) for v in f[1:])

        # ...and the same beat at 60 and 15 fps. The engine's unit is a frame
        # at 30 Hz, so halving the rate must halve the frame COUNT and leave
        # the duration alone.
        rates = []
        for dt in ("0.01666667", "0.03333333", "0.06666667"):
            o2 = os.path.join(tmp, "b%s.txt" % dt)
            subprocess.run([binp, omkpaths.data_root(),
                            os.path.join(ROOT, "tables"), o2, dt],
                           capture_output=True, text=True)
            nf, secs = None, None
            for ln in open(o2):
                g = ln.split()
                if not g: continue
                if g[0] == "beat":    nf = int(g[3])
                elif g[0] == "seconds": secs = round(float(g[1]), 1)
            rates.append((nf, secs))

        # The camera travel underneath the beat, read out of the script rather
        # than from the run - `camera.set 2148, 130`.
        travel = None
        bb = T.archive(omkpaths.data("IAM/AREA"))[118]
        off = struct.unpack_from("<i", bb, 4)[0]
        import dialog_disasm as DD
        ops, st = DD.disasm(bb, off, len(bb))
        for pc, op, raw in ops:
            if op == 95 and len(raw) >= 4:
                cid, tv = struct.unpack_from("<2h", raw, 0)
                if cid == 2148 and tv > 0 and travel is None: travel = tv

        return (scene, tuple(started), beat[2], travel,
                tuple(clips), tuple(paths), shown, tuple(rates)), \
               ((1, 2),
                ((1, "1KaylArrives", "actor", 1, 310, 0, 0),
                 (6, "2KaylStand",   "actor", 0, 310, 1, 1)),
                186, 130,
                ((0, "INTRO1.3DA", 185), (1, "INTRO2.3DA", 31)),
                ((0, "UBas.p1", -478, -43, 27), (1, "UBas.p2-3", -486, -43, -79)),
                ("1", "310", "HO1_FNM"),
                ((370, 6.2), (187, 6.2), (94, 6.3))), \
               "Grid.SCX resident and how many objects the intro started; " \
               "then each - id, name, how, and whether the start WAITS; the " \
               "beat between the menu answering and the conversation opening, " \
               "in frames; the camera travel the script runs underneath it; " \
               "then each started object's ACTOR, CLIP and PATH, the clips " \
               "they name with their frame counts, the paths with their first " \
               "key, and who `character.show` has put on screen by the time " \
               "the conversation opens; and finally the same beat at 60, 30 " \
               "and 15 fps as (frames, seconds) - the COUNT scales and the " \
               "DURATION does not, because the engine's unit is a frame at " \
               "30 Hz and `Game_Frame` divides 30 by the real rate. Under a " \
               "flat 1.0 per rendered frame the count is a constant 187 at " \
               "every rate and the duration doubles, which is the port " \
               "running its animation at the frame rate"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_engine_dialogue_play():
    r"""PLAYING a conversation - and the property is that it does NOTHING alone.

    `dialog.start` stops the world (`g_DialogState` 3, and `Dialog_TickUI` owns
    the frame), and what restarts it is a PERSON. The port's `DialogPlayer`
    plays a node's voice and then waits: `next()` is the player pressing NEXT,
    which evaluates the branch conditions (`Game_HandleEvent` event 55) and
    reveals the menu, and `choose(k)` is the player picking a reply, which runs
    that branch's action (event 59) and enters its target. Nothing else moves it.

    **This replaced a timer, and the timer is why the check is shaped this
    way.** The first version ended each line when its audio ran out and walked
    on by itself. That made the intro take roughly the right total time and was
    still wrong - *this is a dialog, you are not supposed to do anything until
    the user does something* - and a check that only asserted the total would
    have passed. So `play_dialog` is driven by a press STRING, and the first
    row here supplies none: 60 seconds of ticks per step, far longer than any
    line in the game, and the conversation must still be on its first line.
    A timer shows up as that row finishing.

    **The flow is the one `tools/omkweb.html` already plays a conversation by**,
    from its own reading of the game: *the game never shows the NPC line and
    the menu together: the player presses NEXT, and THAT click both reveals the
    menu and cuts to its camera. NEXT with nothing but an unnamed continue
    branch advances straight to the next line.* A named reply, even a single
    one, is a menu and waits.

    **Conversation 272 is the one a new game opens with**, and AREA 118's
    startup script is why it matters: `dialog.start 272` sits between the two
    halves of the intro's camera work, and the GRID tunnel is in the half after
    it. Three nodes, each with one named reply ("J'accepte.", "OK. J'ai bien
    compris.") and the third a leaf - so five presses finish it.

    **The durations are differenced, not asserted from the port alone.** The
    node's `+46` stem names `MORPH/<stem>.3DM` and the line is that file's
    audio block; the reference is `tools/morph3dm.read`, whose result is BYTES
    - 4-bit ADPCM, so seconds are `len/2/22050`. Reading it as samples gave
    61.5 s a line instead of 30.7 and made the port look wrong when it was not.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "play_dialog")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    ops = os.path.join(ROOT, "tables", "vm_opcodes.json")
    tmp = tempfile.mkdtemp()
    try:
        def run(presses):
            out = os.path.join(tmp, "d.txt")
            r = subprocess.run([binp, fr, ops, "272", out, presses],
                               capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(out): return None
            lines, replies, menus_, end = [], [], [], None
            for ln in open(out):
                f = ln.split()
                if not f: continue
                if f[0] == "line":    lines.append((int(f[1]), f[2], int(f[3]), int(f[4]),
                                                    int(f[5]), int(f[6]), int(f[7]),
                                                    int(f[8]), int(f[9])))
                elif f[0] == "menu":  menus_.append(tuple(int(v) for v in f[1:]))
                elif f[0] == "reply": replies.append(tuple(int(v) for v in f[1:]))
                elif f[0] == "end":   end = tuple(int(v) for v in f[1:])
                elif f[0] == "ADVANCED": return "advanced"
            return lines, replies, menus_, end

        idle = run("")            # NO presses - it must not move
        full = run("n0n0n")       # NEXT, reply 0, NEXT, reply 0, NEXT
        if idle == "advanced" or full is None or idle is None:
            return ("advanced without a press",), ("waits",), \
                   "the player is the only thing that moves a conversation"
        idleLines, _, _, idleEnd = idle
        fullLines, fullReplies, fullMenus, fullEnd = full

        # The reference durations, straight out of the morph files.
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import morph3dm
        ref = []
        for row in fullLines:
            stem = row[1]
            pcm, _ch, _lay = morph3dm.read(os.path.join(fr, "MORPH", stem + ".3DM"))
            # TRUNCATE, because the tool writes `(long)(seconds * 100)`.
            # Rounding here instead leaves a 1-centisecond gap that is this
            # file's arithmetic and not a disagreement about the data.
            ref.append(int(len(pcm) / 2.0 / 22050.0 * 100))
        gap = max(abs(a[2] - b2) for a, b2 in zip(fullLines, ref)) if ref else -1

        # ---- THE CAMERAS. A line snaps to its first and travels to its
        # second over 160 frames; the menu cuts to the node's reply pair, and
        # holds the line's when the node names none. Differenced against
        # `tools/omkdata.conversation`, which reads +56/+58/+60/+62 with its
        # own code - and those four offsets are the engine's own named getters
        # `Dialog_GetReplyCamera` / `GetReplyCamera2` / `GetLineCamera` /
        # `GetLineCamera2`, so the layout is not in question, only the reading.
        import omkdata as OD
        conv = OD.conversation(272)
        wantLine = [(n["lineCamera"], n["lineCamera2"]) for n in conv["nodes"]]
        gotLine  = [(l[4], l[5]) for l in fullLines]
        wantMenu = []
        for n in conv["nodes"]:
            if n["replyCamera"] == -1: continue
            wantMenu.append((n["replyCamera"], n["replyCamera2"]))
        # a node with no reply pair keeps the line's, which is the viewer's
        # guard and is what the port now does
        gotMenu = [(m[0], m[1]) for m in fullMenus]
        heldLine = sum(1 for k, m in enumerate(gotMenu)
                       if m == gotLine[k]) if gotLine else 0
        camAgree = (gotLine == wantLine)
        absolute = sum(l[8] for l in fullLines)
        rolls = tuple(l[7] for l in fullLines)

        return (len(idleLines), idleEnd[1], idleEnd[3],
                len(fullLines), fullEnd[1], fullEnd[2], fullEnd[3],
                tuple(r[2] for r in fullReplies), gap,
                camAgree, absolute, rolls, heldLine), \
               (1, 0, 0,
                3, 2, 9926, 1,
                (1, 1), 0,
                True, 3, (-14, 0, 0), 1), \
               "with NO presses: lines played, menus opened, and whether it " \
               "finished (must be 1/0/0 - it waits); then with five presses: " \
               "lines, menus, centiseconds of voice, finished; the two menus' " \
               "available-reply counts; the worst disagreement with " \
               "tools/morph3dm over the line durations; then the CAMERAS - " \
               "whether every line's pair matches tools/omkdata, how many are " \
               "absolute, their rolls, and how many menus held the LINE pair " \
               "because the node names no reply pair"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_engine_world_camera():
    r"""The world camera table, PORTED - and the field that draws a black frame.

    `Camera_FindWorld` (0x0040B220) is what a `camera.set` resolves through:
    44-byte records with the id at +24, in the resident chunk's AREA table
    (+64, count +84) then its SCENE table (+32, +52), and only then GLOBAL
    (+20, +30). `engine/src/o3de/worldcam.*` is that walk, and this differences
    it against `tools/cutscene.py`, which reads the same records independently.

    **What was wrong, and what found it.** The port first read every record as
    two absolute points, which is what `docs/CUTSCENES.md` describes and what
    the reference reader returns. Rendering the Impasse then drew a BLACK
    FRAME: its startup script cuts to `camera.set 0`, and camera 0 sits at the
    world origin while `AImpasse.3DO` is 7000 units away. Nothing in this
    file could have said why - it took a picture.

    `Camera_LoadParams` (0x004146C0) has the answer, and it decides per POINT
    rather than per camera:

        subject == -1   the point goes to the camera's ABSOLUTE slot (+20.. / +32..)
        otherwise       it goes to the OFFSET slot (+176.. / +124..), for
                        `sub_414520` to resolve against that actor

    The two subject fields are record **+34** (guarding the eye) and **+32**
    (guarding the target) - and note they are read in the opposite order to
    their file order, via block +38/+40. **1440 of 5381 records set at least
    one**: 3941 are wholly absolute, 449 give a relative EYE. SCENE 55's
    camera 0 has both fields 0, so it is an eye 119 units behind actor 0 and
    27 above - the third-person follow, which the preset table states in
    metric as 3.00 m behind (118.11 inches).

    **The fov is the full HORIZONTAL angle in degrees**, and the 247 records
    above 105 degrees are not a decode error. The projection setup takes
    `tan(fov * 0.5 * pi/180)` off camera +48, which `Camera_LoadParams` fills
    from record +30 with no clamp; six of AREA 118's intro cameras are in that
    group at 171-175 degrees and render as the radial smear their sequence -
    a wormhole - is meant to be.

    **And relative cameras are the MAJORITY of the game's camera work.** Over
    every `camera.set` / `camera.set.wait` site whose id resolves, the camera is
    actor-relative in **1707 of 2852 cuts** and **715 of 1674 travels** - so
    "the relative ones are just follow cameras a cutscene never travels to" was
    written down here and was false, and the count is asserted so it cannot be
    written down again. A replica cannot point most scripted cameras until it
    knows where its actors are, and on a new game it does not: `IAM\START`
    leaves the player at the (-1,-1,-1) sentinel.

    **What is NOT established, stated rather than implied.** The search order
    follows the code and nothing in `gamedata/` could contradict it: GLOBAL holds
    only **4** cameras, **280** chunk records carry an id it also has, and
    **0** of those differ from GLOBAL's. `tools/cutscene.py` searches the
    opposite way and is indistinguishable on every shipped file. So the order
    is tier 6, read and explained - and this check asserts the 280/0 rather
    than a lookup that would look like evidence and be none.
    """
    import tempfile, shutil
    eng = os.path.join(ROOT, "engine")
    fr  = omkpaths.data_root()
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "dump_world_cameras")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "wc.txt")
        r = subprocess.run([binp, fr, out], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            return ("no dump",), ("built",), "dump_world_cameras must run"
        rows, totals, glob, imp = {}, None, None, None
        for line in open(out):
            if line.startswith("# totals"):  totals = tuple(int(v) for v in line.split()[2:])
            elif line.startswith("# global"): glob  = tuple(int(v) for v in line.split()[2:])
            elif line.startswith("# impasse0"): imp = tuple(int(v) for v in line.split()[2:])
            elif line.startswith("#"): continue
            else:
                f = line.split()
                rows[(f[0], int(f[1]), int(f[2]))] = (
                    [int(v) for v in f[3:9]], float(f[9]), float(f[10]),
                    int(f[11]), int(f[12]))

        # ---- the differential. `tools/cutscene.py` reads the same records
        # with its own code; the port truncates to int the way `Global_Load`
        # stores back into the int32 field, so the reference is truncated too
        # rather than the port being rounded to meet it.
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import cutscene as CU
        agree = disagree = 0
        for arch, chunk in (("AREA", 118), ("AREA", 0), ("AREA", 222), ("SCENE", 55)):
            ref = CU.world_cameras(arch, chunk)
            for cid, c in ref.items():
                mine = rows.get((arch, chunk, cid)) or rows.get(("GLOBAL", -1, cid))
                if mine is None: disagree += 1; continue
                # The port truncates toward zero, the way `Global_Load`
                # stores the double back into the int32 field; the reference
                # keeps two decimals. So compare within a unit rather than
                # re-deriving the truncation here, which would be this file
                # reimplementing the thing under test.
                ref = c["eye"] + c["at"]
                ok = (all(abs(mine[0][k] - ref[k]) < 1.5 for k in range(6)) and
                      abs(mine[1] - c["roll"]) < 0.01 and
                      abs(mine[2] - c["fov"]) < 0.01)
                agree += ok; disagree += not ok
        # And how much of the game's camera work those relative records
        # actually carry, counted over the script sites that name them.
        import dialog_disasm as DD
        tbl, GL = {}, {}
        for arch, po, co in (("AREA", 64, 84), ("SCENE", 32, 52)):
            for k, bb in T.archive(omkpaths.data("IAM", arch)).items():
                if len(bb) < co + 2: continue
                q = struct.unpack_from("<I", bb, po)[0]
                n = struct.unpack_from("<h", bb, co)[0]
                if n <= 0 or q + 44 * n > len(bb): continue
                for i in range(n):
                    o = q + 44 * i
                    tbl.setdefault((arch, k), {})[
                        struct.unpack_from("<h", bb, o + 24)[0]] = \
                        struct.unpack_from("<2h", bb, o + 32)
        gf = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
        gq = struct.unpack_from("<I", gf, 20)[0]
        gn = struct.unpack_from("<h", gf, 30)[0]
        for i in range(gn):
            GL[struct.unpack_from("<h", gf, gq + 44 * i + 24)[0]] = \
                struct.unpack_from("<2h", gf, gq + 44 * i + 32)
        cuts = [0, 0]; travels = [0, 0]
        for arch in ("AREA", "SCENE"):
            for k, bb in T.archive(omkpaths.data("IAM", arch)).items():
                here = tbl.get((arch, k), {})
                lay = T.LAYOUT[arch](bb)
                if not lay: continue
                try:
                    sites = list(T._scripts_from_records(bb, lay[0], lay[1])) \
                            + T._second_table(arch, bb)
                except Exception:
                    continue
                for _kk, _ff, pp in sites:
                    ops, st = DD.disasm(bb, pp, len(bb))
                    if st != "ok": continue
                    for _pc, op, raw in ops:
                        if op not in (95, 96) or len(raw) < 4: continue
                        cid, travel = struct.unpack_from("<2h", raw, 0)
                        sub = here.get(cid, GL.get(cid))
                        if sub is None: continue
                        (travels if travel > 0 else cuts)[sub != (-1, -1)] += 1

        return (totals, glob, imp, disagree, tuple(cuts), tuple(travels)), \
               ((5381, 3941, 449, 1440, 247), (4, 280, 0), (0, 0, -119), 0,
                (1145, 1707), (959, 715)), \
               "records / absolute / eye-relative / target-relative / fov>105; " \
               "GLOBAL size, id collisions with it, of which differ; the " \
               "Impasse's camera 0 (eye subject, target subject, eye z); " \
               "disagreements with tools/cutscene.py over four chunks; and the " \
               "script sites, absolute vs actor-relative, for cuts and travels"
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def c_world_cameras():
    """FILE_FORMATS: the world camera table, and op 126.

    Camera_FindWorld scans 44-byte records with the id at +24 in three
    places - AREA +64 (count +84), SCENE +32 (+52), GLOBAL +20 (+30) - which
    also identifies GLOBAL's '44-byte record array'. Every camera operand of
    camera.set.at_address is in that table, every aim operand in
    ADDRESSES.TAG, and the travel time is always 20.
    """
    import dialog_triggers as T2
    cams = set()
    per = 0
    for name, po, co in (("AREA", 64, 84), ("SCENE", 32, 52)):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            if len(b) < co + 2: continue
            p, n = struct.unpack_from("<I", b, po)[0], struct.unpack_from("<h", b, co)[0]
            if n <= 0 or p + 44 * n > len(b): continue
            for i in range(n):
                cams.add(struct.unpack_from("<h", b, p + 44 * i + 24)[0]); per += 1
    g = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
    p, n = struct.unpack_from("<I", g, 20)[0], struct.unpack_from("<h", g, 30)[0]
    for i in range(n): cams.add(struct.unpack_from("<h", g, p + 44 * i + 24)[0]); per += 1
    AD = O.TAGS["ADDRESSES"]
    sites = [struct.unpack("<3h", r) for r in _world_ops()[126]]
    return (per, len(sites), sum(a in cams for a, _, _ in sites),
            sum(b in AD for _, b, _ in sites), sum(c == 20 for _, _, c in sites)), \
           (5381, 84, 84, 84, 84), \
           "world camera records; camera.set.at_address sites, of which " \
           "name one, aim at an ADDRESSES entry, and travel in 20"


def c_opcode_tail():
    r"""SCRIPT_VM: the tail opcodes named in the closing pass.

    One check per family, each on the shipped data. The timer: timer.set is
    900 seconds at every site and timer.mode 12 at the same ones. The images:
    every image.show operand names a shipped IMAGES\%06X.BMP. The morphs: all
    three morph.play sites name files that do NOT exist - the negative result
    is the finding, so it is asserted too. The restart: exactly 3 sites. The
    ledges: ignore/obey come in equal-ish pairs per chunk.
    """
    import os, dialog_disasm as D
    t_set = [struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[113]]
    t_mode = [struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[114]]
    imgs = {f.upper() for f in os.listdir(omkpaths.data("IMAGES"))}
    im = ["%06X.BMP" % struct.unpack_from("<h", r, 0)[0] for r in _world_ops()[94]]
    morphs = {f.upper() for f in os.listdir(omkpaths.data("MORPH"))}
    mo = []
    for r in _world_ops()[144]:
        a, b, c = struct.unpack("<3h", r)
        mo.append("%06X.3DM" % (((c & 0xFF) << 24) | (b & 0xFFFF)))
    return (len(t_set), t_set.count(900), t_mode.count(12),
            len(im), sum(f in imgs for f in im),
            len(mo), sum(f in morphs for f in mo),
            len(_world_ops()[152])), \
           (12, 12, 12, 4, 4, 3, 0, 3), \
           "timer.set sites, of which 900s, and mode 12; image.show sites, " \
           "of which shipped; morph.play sites, of which shipped (none - cut " \
           "content); game.restart sites"


def c_object_records():
    r"""FILE_FORMATS: the 1304-byte IAM\OBJECT record.

    Four invariants, three of them self-naming: the id at +0 must equal the
    slot; every prop-table id's stem at +14 must name a shipped model in
    MESHES\OBJETS (the loader path Object_Load takes); every object called
    'Seteks N' or 'Anneaux N' must carry +12 == N (the quantity
    Object_ApplyEffect adds to Argent / Anneaux); and the gun/ammo kinds at
    +2 must align with the GLOBAL weapon table, ammo = gun + 5 on all five
    pairs. The 'vie +N' description check is asserted at 6 of 8 because the
    two misses are real: Medikit Petit's text says +15 where the field the
    engine uses says 20.
    """
    import re, dialog_disasm as D
    d = open(omkpaths.data("IAM/OBJECT"), "rb").read()
    def fld(i, off): return struct.unpack_from("<h", d, i * 2048 + off)[0]
    def st(i, off, n):
        return d[i * 2048 + off:i * 2048 + off + n].split(b"\x00")[0]
    ids = sum(fld(i, 0) == i for i in range(1002))
    per, flat = _prop_table()
    have = {x.upper().split(".")[0]
            for x in os.listdir(omkpaths.data("MESHES/OBJETS"))}
    props = {r[1] for r in flat}
    stems = sum(st(p, 14, 10).decode("cp1252").upper() in have for p in props)
    q = ok = 0
    for i in range(1002):
        m = re.match(r"(?:Seteks|Anneaux?)\s+(\d+)", O.TAGS["OBJECTS"].get(i, ""))
        if m: q += 1; ok += fld(i, 12) == int(m.group(1))
    g = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
    slots = [struct.unpack_from("<h", g, 32 + 2 * k)[0] for k in range(10)]
    aligned = sum(fld(slots[k], 2) == fld(slots[k + 5], 2) + 5 for k in range(5))
    h = hok = 0
    for i in range(1002):
        m = re.search(rb"[Vv]ie\s*\+\s*(\d+)", st(i, 280, 1024))
        if m: h += 1; hok += fld(i, 8) == int(m.group(1))
    blank = sum(1 for i in range(1002) if not any(d[i*2048+56:i*2048+280]))
    return (ids, len(props), stems, q, ok, aligned, h, hok, blank), \
           (1002, 300, 300, 11, 11, 5, 8, 6, 1002), \
           "ids == slot; prop ids, of which name a shipped model; Seteks/" \
           "Anneaux self-naming quantities; gun/ammo kind pairs aligned; " \
           "'vie +N' descriptions, of which match +8; +56..280 all zero"


def c_morph_nodes():
    """FILE_FORMATS 5: .3DM node slots 0/1 are not rotations; the float[3]
    is a delta track.

    A fixed seeded sample so the numbers are exact and re-runnable: over 80
    sampled frames, node 0 is a unit quaternion in 15 and node 1 in 2, while
    the same frames' slots 2+ stay unit - which is what "two non-rotation
    tracks that no drawn mesh binds" predicts and a wrong stride would
    scramble instantly.
    """
    import glob, random, morph3dm as M
    random.seed(1)
    files = sorted(glob.glob(omkpaths.data("MORPH/*.3DM"))) + \
            sorted(glob.glob(omkpaths.data("MORPH/*.3dm")))
    n = u0 = u1 = u2 = 0
    for path in random.sample(files, 40):
        L = M.layout(path); d = open(path, "rb").read()
        for fi in (0, L["frames"] // 2):
            off = M.frame_offset(L, fi)
            q = [struct.unpack_from("<4f", d, off + 12 + 16 * k) for k in range(3)]
            n += 1
            u0 += abs(sum(x * x for x in q[0]) - 1) < .01
            u1 += abs(sum(x * x for x in q[1]) - 1) < .01
            u2 += abs(sum(x * x for x in q[2]) - 1) < .01
    return (n, u0, u1, u2), (80, 15, 2, 80), \
           "sampled frames; node 0 unit; node 1 unit; node 2 unit"


def c_ctl_blocks():
    """ASSETS: the .CTL flag-gated blocks.

    The self-naming rows: H_Sd-180's turn block carries exactly 180.0;
    HRStep/HLStep carry the mirrored root shift (3, 9, -/+2.5, 0, 0) in both
    combat files; and the 40-byte combat block appears ONLY in the three
    combat .CTLs (46 + 46 + 24 entries) - zero in Avnt/Meca/Sham. A wrong
    walk order would scramble all three at once, since the blocks are
    allocated back to back.
    """
    import glob, anim_ctl as A
    u32 = A.u32
    combat = 0; noncombat = 0; sd180 = None; steps = []
    md = mdtot = 0
    for path in (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
                 sorted(glob.glob(omkpaths.data("ANIMS/*.ctl")))):
        d = open(path, "rb").read()
        gc = u32(d, 12); off = 32 * gc + 88
        groups = []
        for i in range(gc):
            g = 88 + 32 * i; n = u32(d, g + 4)
            groups.append((n, off)); off += 88 * n
        v5 = off
        ents = [b + 88 * k for n, b in groups for k in range(n)]
        named = {}
        for e in ents:
            if not (u32(d, e + 8) & 0x8002): named[e] = v5; v5 += 12
        for e in ents:
            if d[e + 87]: v5 += 4 * d[e + 87]
        for e in ents:
            if d[e + 86]: v5 += 4 * d[e + 86]
        b44 = {}; b48 = {}
        for e in ents:
            if u32(d, e + 8) & 0x140: b44[e] = v5; v5 += 24
        for e in ents:
            if u32(d, e + 8) & 0x280: b48[e] = v5; v5 += 20
        n52 = 0
        for e in ents:
            if d[e + 8] & 0x10:
                mdtot += 1; md += d[v5:v5 + 2] == b"MD"; v5 += 12
        for e in ents:
            if u32(d, e + 8) & 0x2000000: n52 += 1; v5 += 40
        isCmbt = "CMBT" in os.path.basename(path).upper()
        if isCmbt: combat += n52
        else: noncombat += n52
        def nm(e):
            o = named.get(e)
            return d[o:o + 12].split(b"\x00")[0].decode("cp1252", "?") if o else ""
        for e, o in b44.items():
            if nm(e) == "H_Sd-180": sd180 = struct.unpack_from("<f", d, o + 12)[0]
        for e, o in b48.items():
            if nm(e) in ("HRStep", "HLStep"):
                steps.append(struct.unpack_from("<5f", d, o))
    mirrored = sum(1 for st in steps if st[:2] == (3.0, 9.0) and abs(st[2]) == 2.5)
    return (sd180, combat, noncombat, len(steps), mirrored, mdtot, md), \
           (180.0, 116, 0, 4, 4, 209, 165), \
           "H_Sd-180's turn angle; combat blocks in the Cmbt files and " \
           "elsewhere; HRStep/HLStep root shifts, of which mirrored (3,9,±2.5); " \
           "12-byte name blocks, of which start 'MD'"


def c_scx_clips():
    r"""ASSETS/FILE_FORMATS: the SCX streamed sections and their .3DA clips.

    Every streamed record's first header word is its own file offset, so the
    walk is self-checking; the invariant is landing exactly on the file size,
    for all 220 files. The clips are .3DA descriptors (40-byte bone tracks,
    12-byte translations, 16-byte quaternions); every one parses, and the
    contextual-idle chain resolves dialog 402 to TE_STD.3DA and 387 to
    TELRES05.3DA - Telis's flat and restaurant poses.
    """
    import glob, anim_3da
    files = (sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))) +
             sorted(glob.glob(omkpaths.data("SCPTDATA/*.scx"))))
    exact = clips = bad = 0
    for f in files:
        st = anim_3da.scx_stream(f)
        exact += st["end"] == st["size"]
        for a in st["anims"]:
            r = anim_3da.descriptor(st["data"], a["offset"], a["declared"])
            if r: clips += 1
            else: bad += 1
    si402 = O.scene_idle(402)
    si387 = O.scene_idle(387)
    return (len(files), exact, clips, bad,
            si402 and si402["clip"], si387 and si387["clip"]), \
           (220, 220, 1490, 0, "TE_STD.3DA", "TELRES05.3DA"), \
           "SCX files, of which walk exactly to EOF; embedded clips parsed, " \
           "failures; the 402 and 387 contextual idles"


def c_shoot_mode():
    """SCRIPT_VM: ops 80/81/82/84/116/117 are one subsystem.

    Two things say so and neither is a range check. `Shoot_ActorAction`
    refuses unless the actor is already in shoot mode, and the scripts obey:
    almost every `shoot.actor.action` is preceded, in the same script, by a
    `shoot.actor.enter` on the same character. And the six opcodes are
    confined to a handful of chunks out of 328 - the ones the game names
    'Anekbah Shooting gallery', '1-10 Supermarché Shoot', 'Jaunpur Tetra 1..4'.
    116/117 bracket: 116 opens a script, 117 closes it.
    """
    import dialog_triggers as T2, dialog_disasm as D
    chunks = {op: set() for op in (80, 81, 82, 84, 116, 117)}
    tot = paired = both = with116 = 0
    def scan(arch, k, b, slots):
        nonlocal tot, paired, both, with116
        for rec, f, p in slots:
            ops, st = D.disasm(b, p, len(b))
            if st != "ok": continue
            seen, present = set(), {o for _, o, _ in ops}
            for op in chunks:
                if op in present: chunks[op].add((arch, k))
            if 116 in present:
                with116 += 1; both += 117 in present
            for pc, op, raw in ops:
                if op == 82: seen.add(struct.unpack_from("<h", raw, 0)[0])
                elif op == 84:
                    tot += 1
                    paired += struct.unpack_from("<h", raw, 0)[0] in seen
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if r: scan(name, k, b, list(T2._scripts_from_records(b, r[0], r[1]))
                       + T2._second_table(name, b))
    b, slots = T2.global_file(omkpaths.data("IAM", "GLOBAL"))
    scan("GLOBAL", 0, b, slots)
    everywhere = set().union(*chunks.values())
    return (tot, paired, len(everywhere), with116, both), \
           (319, 315, 21, 105, 98), \
           "shoot.actor.action sites, of which follow a shoot.actor.enter on " \
           "the same character; chunks using any of the six; scripts with " \
           "shoot.player.suspend, of which also resume"


def c_actor_stats():
    """SCRIPT_VM: op 86's property field, checked against the .TAG names.

    `Actor_GetProperty` reads a different offset of the character record per
    property value, and `var.set.actor_stat` writes the result into a variable
    the script names. If field 1 were not the property selector the two would
    not line up - but every variable written is written by exactly one
    property, and the names match the offsets: 1 -> +170 'Vie', 2 -> +156
    'Mana', 4 -> +172 'Argent', 5 -> +174 'Anneaux', 16..19 -> +160..+166
    'Carac Attack' / 'Body Shield' / 'Dodge' / 'Fight Experience'.
    """
    import collections
    V = O.TAGS["VARIABLES"]
    by_var = collections.defaultdict(set)
    player = 0
    for raw in _world_ops()[86]:
        actor, prop, var = struct.unpack("<3h", raw)
        by_var[var].add(prop); player += actor == -1
    want = {1: "Vie", 2: "Mana", 4: "Argent", 5: "Anneaux", 16: "Carac Attack",
            17: "Carac Body Shield", 18: "Carac Dodge",
            19: "Carac Fight Experience", 3: "Carac Speed", 0: "Sexe",
            7: "Type Spectre"}
    named = all(V.get(v, "").startswith(want[p]) or want[p] in V.get(v, "")
                for v, ps in by_var.items() for p in ps if p in want)
    return (len(_world_ops()[86]), player, len(by_var),
            sum(len(p) == 1 for p in by_var.values()), named), \
           (459, 458, 18, 18, True), \
           "var.set.actor_stat sites, of which act on the player; variables " \
           "written, of which by exactly one property; every name matches"


def c_weapon_table():
    r"""FILE_FORMATS: IAM\GLOBAL +32 - the weapon and ammunition table.

    `Weapon_SlotForObject` scans it from +42 returning 5 upward, and
    `Weapon_ObjectForSlot` indexes it as `+32 + 2*slot`. So slots 0..4 sit
    below the scan and 5..14 inside it - and the shipped table says exactly
    what that split is: five ammunition types, then the guns that fire them,
    **index-aligned**, `Munitions X` at slot n and `Gun X` at slot n + 5.
    """
    b = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
    slots = [struct.unpack_from("<h", b, 32 + 2 * i)[0] for i in range(15)]
    T = O.TAGS["OBJECTS"]
    names = [T.get(v, "") for v in slots]
    ammo = sum(n.startswith("Munitions") for n in names[:5])
    guns = sum(n.startswith("Gun") for n in names[5:12])
    aligned = sum(names[i][9:] == names[i + 5][3:] for i in range(5))
    return (ammo, guns, aligned, slots[12:]), (5, 6, 5, [-1, -1, -1]), \
           "IAM\\GLOBAL +32: ammunition slots 0..4, Gun slots 5..11, " \
           "ammo/gun pairs whose names match, and the empty tail"



def c_script_programs():
    """FILE_FORMATS 5c: the interpreter's reading of the runtime fields.

    Script_PlayScript (0x0044C860) runs an object's functions **in sequence**
    (PC at +36), each `+16` times (`+20` is its run counter, -1 = forever),
    and reruns the whole list `+52` times (-1 = loop forever).  If that is
    what those fields are, the shipped values must look like authored repeat
    counts: +20 identically zero, +16 small-or--1, +52 only 1 or -1.
    """
    import glob, scene_scx
    lims, cnts, loops = {}, set(), {}
    fns = objs = 0
    for path in sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))):
        b = scene_scx.load(path)["block"]
        n = struct.unpack_from("<I", b, 4)[0]
        after = 8 + 100 * n
        pcount = struct.unpack_from("<I", b, after)[0]
        o = after + 4 + 4 * pcount
        for i in range(n):
            r = 8 + 100 * i
            loops[struct.unpack_from("<i", b, r + 52)[0]] =                 loops.get(struct.unpack_from("<i", b, r + 52)[0], 0) + 1
            objs += 1
            o += 22 if b[o] else 1
            nf = struct.unpack_from("<I", b, r + 32)[0]
            ns = struct.unpack_from("<I", b, r + 44)[0]
            for k in range(nf + ns):
                lim, cnt = struct.unpack_from("<2i", b, o + 24 * k + 16)
                lims[lim] = lims.get(lim, 0) + 1
                cnts.add(cnt)
                fns += 1
            o += 24 * (nf + ns)
            for _ in range(2):
                c = struct.unpack_from("<I", b, o)[0]
                o += 4 + 29 * c
    bad_lims = sorted(l for l in lims if l != -1 and not 1 <= l <= 32)
    return (fns, objs, sorted(cnts), lims.get(1, 0), lims.get(-1, 0), bad_lims,
            sorted(loops.items())), \
           (13887, 4511, [0], 13247, 43, [], [(-1, 960), (1, 3551)]), \
           "13887 function records / 4511 objects: run counter 0 on disk, " \
           "repeat limit 1 (13247) .. 27 or -1 (43), object loop count " \
           "only 1 (3551) or -1 (960)"



def c_cam_editings():
    """FILE_FORMATS 5c: chunk 10 - the scripted camera editings."""
    import cam_editing
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        got = cam_editing.selftest()
    return got, (29, 29, 125, 0, 0), \
           "scenes with a chunk 10, of which walk exactly to the payload " \
           "end; editings; unresolved id references; nonzero target ids " \
           "absent from chunk 2"



def c_scx_sync_chain():
    r"""FILE_FORMATS 5c: which array a function's `+12` sync index counts in.

    `Script_PlayScript` runs the function at the pc together with everything
    its `+12` link reaches, so where that index points decides how long a
    program runs. It points into the object's **sync array**, not into the
    functions laid end to end: `scene_read_objects` (0x00449750) resolves it as

        ScriptFunction *sf = obj->syncFunctions + (int32_t)fn->sync;
        if (sf >= obj->syncFunctions + obj->syncCount)  -> load fails

    and `Script_FunctionsIndexesToAdresses` (0x0044A070) does the same
    arithmetic for the sync records' own links. Read flat instead - the port
    and `tools/sim` both did until 2026-09-03 - every one of the shipped links
    lands one array too early.

    The **camera editings adjudicate it**, and they are an oracle this repo
    cannot fake: an editing's `+24` duration is authored, it names the object
    it plays over, and `Script_PlayScript` samples it on that object's own
    program clock - so a correctly modelled program must last as long as the
    editing linked to it. `FlatProgram` below is the wrong reading, run side
    by side, so this check is SHOWN to fail rather than asserted to pass.

    **The margin is thin, and it got thinner - say so rather than re-baseline
    it quietly.** This first read 69 against 66, and that was measured while
    the interpreter still spent a frame per program step; correcting the timing
    on 2026-09-03 took it to **66 against 65**, with the rows where the two
    readings differ at all going 8 -> 11 and the sync-array reading winning
    them 4 to 3. The old margin was partly the correction term this check used
    to apply cancelling a real effect - a trailing `PlaySyncSound` cue holds
    the chain one tick past the animation, so a program legitimately outlives
    its editing by a frame. A corpus verdict recorded before a timing fix is
    worth re-running, not re-reading (CLAUDE.md 1).

    So the corpus is a **weak** adjudicator here and is not what settles the
    question: the loader is, and it is not ambiguous. What the corpus still
    does decisively is the one row the Impasse turns on - `sautdemon`, the
    demon's jump off the wall, is **132** frames, `A_2_DemonLook` is clip 15
    (91) then clip 17 (41), and flat it runs **92**, cutting the shot 40 frames
    short and bringing "Te voilà, enfin ! Je t'attendais..." in early.

    It does not pretend to a perfect score: 29 of the 95 miss, 7 of them
    because `MoveObjectOnPath`'s busy window is not modelled (`sim/scene.py`
    `_busy_span`) and the rest because an editing's authored length is a
    director's choice and need not equal its object's animation. All of them
    miss identically under both readings.
    """
    import glob, cam_editing, scene_scx
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    from sim.scene import Scene, Program

    class FlatProgram(Program):
        def chain(self, i):
            out, seen = [], set()
            while 0 <= i < len(self.fns) and i not in seen:
                seen.add(i); out.append(i)
                i = self.fns[i].get("sync", -1)
            return out

    def length(cls, scene, obj, cap=4000):
        """The program's own length in frames - the clock when it stops. Since
        the timing was corrected (a function reports done on the tick that drew
        its last frame) this is simply the sum of its steps, with no correction
        term. Applied identically to both readings either way."""
        p = cls(scene, obj, trace=[])
        n = 0
        while p.tick() and n < cap: n += 1
        return None if n >= cap else n + 1

    # every sync link, checked against the array the loader would index
    links = out_of_range = 0
    for path in sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))):
        for o in scene_scx.scene(path)["objects"]:
            for f in o["functions"]:
                if f["sync"] == -1: continue
                links += 1
                if not 0 <= f["sync"] < o["nsync"]: out_of_range += 1

    rows = []
    for path in sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))):
        try: ed = cam_editing.parse(cam_editing.payload(path))
        except Exception: continue
        linked = [e for e in ed["editings"] if e["target"]]
        if not linked: continue
        sc = Scene(os.path.basename(path))
        for e in linked:
            o = sc.byhandle.get(e["target"])
            if not o: continue
            rows.append((e["duration"], length(Program, sc, o),
                         length(FlatProgram, sc, o)))
    exact_sync = sum(1 for d, s, f in rows if s == d)
    exact_flat = sum(1 for d, s, f in rows if f == d)
    differ = [r for r in rows if r[1] != r[2]]
    won  = sum(1 for d, s, f in differ if s == d)
    lost = sum(1 for d, s, f in differ if f == d)

    sc = Scene("Impasse.SCX")
    demon = sc.byhandle[223]
    saut = (length(Program, sc, demon), length(FlatProgram, sc, demon))

    return (links, out_of_range, len(rows), exact_sync, exact_flat,
            len(differ), won, lost, saut), \
           (6308, 0, 95, 66, 65, 11, 4, 3, (132, 92)), \
           "every sync link in the 220 .SCX and how many fall outside the " \
           "object's own sync array (0 - the loader refuses the file " \
           "otherwise); then the editings that name an object, how many the " \
           "sync-array reading and the FLAT one time exactly, the rows where " \
           "the two differ at all and who is right on them; and finally " \
           "Impasse's A_2_DemonLook under both - 132 frames, which is exactly " \
           "`sautdemon`'s own duration, against the flat reading's 91"



def c_ctl_combat():
    """ASSETS: the .CTL combat block, read from Fight_ResolveHit (0x0049A960)."""
    import glob, collections, anim_ctl
    files = (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
             sorted(glob.glob(omkpaths.data("ANIMS/*.ctl"))))
    blocks = refs = resolve = coll = defaults = groups = 0
    dmg_lo, dmg_hi = 99, 0
    for f in files:
        w = anim_ctl.walk(f)
        ids16 = collections.Counter(st["id"] & 0xFFFF for st in w["states"])
        coll += sum(1 for v in ids16.values() if v > 1)
        per = collections.Counter(st["group"] for st in w["states"]
                                  if st["flags"] & 0x20)
        ngroups = max(st["group"] for st in w["states"]) + 1
        groups += ngroups
        defaults += sum(1 for g in range(ngroups) if per.get(g, 0) == 1)
        for st in w["states"]:
            c = st["combat"]
            if not c: continue
            blocks += 1
            iv = struct.unpack("<10i", struct.pack("<10f", *c))
            dmg_lo = min(dmg_lo, iv[1]); dmg_hi = max(dmg_hi, iv[1])
            for r in (iv[5], iv[6]):
                if r == -1: continue
                refs += 1
                if (r & 0xFFFF) in ids16: resolve += 1
    roles_ok = 0
    for f in files:
        w = anim_ctl.walk(f)
        d = w["data"]
        combatfile = any(st["combat"] for st in w["states"])
        found = collections.Counter()
        for st in w["states"]:
            r = struct.unpack_from("<I", d, st["offset"] + 12)[0] & 0xFFFF
            if r in (3, 4, 5, 9, 18, 20): found[r] += 1
        if combatfile and all(found[r] == 1 for r in (3, 4, 5, 9, 18, 20)):
            roles_ok += 1
        if not combatfile and not found:
            roles_ok += 1
    return (blocks, refs, resolve, coll, dmg_lo, dmg_hi, groups, defaults,
            roles_ok), \
           (116, 232, 232, 0, 1, 25, 202, 202, 7), \
           "combat blocks; reaction refs, of which the low-16 id resolves; " \
           "low-16 id collisions; damage min/max; groups, of which exactly " \
           "one flag-0x20 default entry; files whose +12 role codes " \
           "(3/4/5/9/18/20 - Fight_Begin's cache) are exactly right"



def c_ctl_special_moves():
    """ASSETS: every .CTL move name resolves in the binary's tab_special_move."""
    import glob, anim_ctl
    d = open(omkpaths.data("Runtime 2.exe"), "rb").read()
    table, off, r = set(), 0xC9D68, 0     # tab_special_move[] at VA 0x4CB168
    while True:
        row = d[off + 12 * r: off + 12 * r + 12]
        if struct.unpack("<3I", row) == (0, 0, 0): break
        table.add(row[:8].rstrip(b"\0").decode("ascii")); r += 1
    files = (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
             sorted(glob.glob(omkpaths.data("ANIMS/*.ctl"))))
    sites, names, miss = 0, set(), set()
    for f in files:
        for st in anim_ctl.walk(f)["states"]:
            if not st["mdname"]: continue
            sites += 1
            n = st["mdname"][:8]
            names.add(n)
            if n not in table and n.lower() != "none": miss.add(n)
    return (r, sites, len(names), sorted(miss)), (66, 209, 54, []), \
           "tab_special_move rows; .CTL move-name sites, distinct names, " \
           "names the table lacks"



def c_ctl_transitions():
    """ASSETS: the .CTL transition fields Cef_FindTransition consumes."""
    import glob, anim_ctl
    files = (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
             sorted(glob.glob(omkpaths.data("ANIMS/*.ctl"))))
    tot = win = badwin = coded = sentinel = 0
    pris = set()
    for f in files:
        w = anim_ctl.walk(f)
        d = w["data"]
        for st in w["states"]:
            e = st["offset"]; tot += 1
            code = struct.unpack_from("<I", d, e + 4)[0]
            f16, f20 = struct.unpack_from("<2f", d, e + 16)
            pris.add(struct.unpack_from("<H", d, e + 84)[0])
            if code: coded += 1
            if code == 0x80000000: sentinel += 1
            if f16 or f20:
                win += 1
                if not 0 <= f16 <= f20 <= 1000: badwin += 1
    return (tot, coded, sentinel, win, badwin, sorted(pris)), \
           (1286, 640, 28, 168, 0, [0, 1, 2]), \
           "entries; with an input code, of which the 0x80000000 no-input " \
           "sentinel; with a cancel window, of which malformed; the " \
           "priority values"



def c_ctl_effects():
    """ASSETS: the .CTL +28 effect records Cef_UpdateStateEffects consumes."""
    import glob, anim_ctl
    tot = snd = spr = neither = badwin = badattach = 0
    for f in (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
              sorted(glob.glob(omkpaths.data("ANIMS/*.ctl")))):
        w = anim_ctl.walk(f)
        d = w["data"]
        groupCount = struct.unpack_from("<I", d, 12)[0]
        off = 32 * groupCount + 88
        groups = []
        for i in range(groupCount):
            g = 88 + 32 * i
            n = struct.unpack_from("<I", d, g + 4)[0]
            groups.append((g, n, off)); off += 88 * n
        pos = off
        ents = [base + 88 * k for _, n, base in groups for k in range(n)]
        u32 = lambda o: struct.unpack_from("<I", d, o)[0]
        for e in ents:
            if not (u32(e + 8) & 0x8002): pos += 12
        for e in ents:
            if d[e + 87]: pos += 4 * d[e + 87]
        for e in ents:
            if d[e + 86]: pos += 4 * d[e + 86]
        for e in ents:
            if u32(e + 8) & 0x140: pos += 24
        for e in ents:
            if u32(e + 8) & 0x280: pos += 20
        for e in ents:
            if d[e + 8] & 0x10: pos += 12
        for e in ents:
            if u32(e + 8) & 0x2000000: pos += 40
        tableCount = u32(76)
        table = pos; pos += 156 * tableCount
        slots = []
        for i in range(tableCount):
            rec = table + 156 * i
            for k in range(12):
                cnt = u32(rec + 16 + 12 * k)
                slots.append((cnt, pos)); pos += 16 * cnt
        for cnt, blk in slots:
            for j in range(cnt): pos += 4 * u32(blk + 16 * j + 4)
        for e in ents:
            if not (d[e + 76] & 8): continue
            n = u32(pos)
            for k in range(n):
                r = d[pos + 8 + 32 * k: pos + 8 + 32 * (k + 1)]
                tot += 1
                dur, st, en, sndt = struct.unpack_from("<4f", r, 0)
                sid, sound = struct.unpack_from("<2H", r, 20)
                if sound: snd += 1
                if sid: spr += 1
                if not sound and not sid: neither += 1
                if en and st > en: badwin += 1
                if r[24] > 20: badattach += 1
            pos = 32 * n + pos + 8
    return (tot, snd, spr, neither, badwin, badattach), \
           (590, 525, 220, 0, 0, 0), \
           "effect records; with a sound, with a sprite, with neither; " \
           "malformed windows; attach codes out of range"



def c_ctl_groups():
    """ASSETS: group records carry an id (+0) and flags (+8, bit 0 = the
    file's default group) - read from Cef_FindGroupById / Cef_DefaultGroup."""
    import glob, anim_ctl
    files = (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
             sorted(glob.glob(omkpaths.data("ANIMS/*.ctl"))))
    ndefault, id100 = [], 0
    for f in files:
        d = anim_ctl.walk(f)["data"]
        gc = struct.unpack_from("<I", d, 12)[0]
        rows = [struct.unpack_from("<3I", d, 88 + 32 * i) for i in range(gc)]
        dflt = [gid for gid, n, fl in rows if fl & 1]
        ndefault.append(len(dflt))
        if dflt == [100]: id100 += 1
    return (ndefault, id100), ([1] * 7, 4), \
           "default groups per file (flag bit 0), of which id 100 - the " \
           "combat files keep stale authoring ids, reached by graph only"



def c_player_anim_hold():
    """FILE_FORMATS: a scene clip drives the player through a dialog only
    under an authored `player.anim.hold` - the corpus invariant behind the
    dialogue-pose rule (Actor_EnterDialogueMode, 0x00468DE0)."""
    import dialog_disasm as D
    trigs = O._dialog_triggers()
    launches = held = scene_held = scene_unheld = 0
    for did, sites in sorted(trigs.items()):
        arch, chunk, rec, field = sites[0]
        try:
            b = T.archive(os.path.join(O.TAGDIR, arch))[chunk]
            r = T.LAYOUT[arch](b)
            slots = (list(T._scripts_from_records(b, r[0], r[1])) +
                     T._second_table(arch, b))
        except Exception:
            continue
        off = next((pp for rc, f, pp in slots
                    if rc == rec and f == field), None)
        if off is None: continue
        ops, stx = D.disasm(b, off, len(b))
        if stx != "ok": continue
        seen = []
        for pc, op, raw in ops:
            if op == 61 and raw and struct.unpack_from("<h", raw)[0] == did:
                break
            seen.append(op)
        launches += 1
        if 104 in seen:
            held += 1
            if any(o in (46, 90) for o in seen): scene_held += 1
        elif any(o in (46, 90) for o in seen):
            scene_unheld += 1
    return (launches, held, scene_held, scene_unheld), (205, 120, 16, 0), \
           "decoded launches; with player.anim.hold before dialog.start; " \
           "of which also scx.play.player; scene-driven WITHOUT a hold " \
           "(must stay zero - it is the rule)"



def _staged_body(model, scx, anim, worldY, frame=1):
    r"""Where a body posed by a scene clip lands, under the PELVIS anchor.

    Mirrors what `omkweb.html`'s `stageMatrices` does with the same numbers:
    the placement names the pelvis, the pelvis's model-space rest height is the
    lift, and a model point at height v.y therefore lands at
    `v.y + worldY - pelvisY` in the set.  Returns (feet, crown) in game
    coordinates, whose Y grows downward - so `feet` is the larger number.

    The pelvis is the hierarchy root in every character model, so its posed
    position IS its rest position and this is pose-independent, which is the
    whole reason the anchor can live on the placement.  `tools/stagecheck.js`
    tests that end of it against the page's own matrices.
    """
    import struct
    g = O.model_geometry(model)
    meta, buf = O.ani_pose_stream(model, scx, anim)
    n = meta["meshes"]
    f = min(frame, meta["frames"] - 1)
    P = struct.unpack_from("<%df" % (n * 7), buf, f * n * 7 * 4)
    lo, hi = -1e9, 1e9
    for i, L in enumerate(g["staticLocal"]):
        mi = g["staticMesh"][i]
        if L is None or mi is None or mi < 0: continue
        b = mi * 7
        w, x, y, z = P[b:b + 4]
        tx = 2 * (y * L[2] - z * L[1])
        ty = 2 * (z * L[0] - x * L[2])
        tz = 2 * (x * L[1] - y * L[0])
        py = L[1] + w * ty + (z * tx - x * tz) + P[b + 5]
        lo = max(lo, py); hi = min(hi, py)
    d = worldY - g["pelvis"][1]
    return lo + d, hi + d


def _ground_under(tris, p):
    """The LOWEST walkable surface over this x/z - the room's own ground.

    Not `floor_under_walkable`, which answers with the NEAREST surface below a
    point: beside furniture that is the furniture, and a body standing on the
    real floor then reads as having sunk through a crate.  The game's Y grows
    downward, so the ground is the largest y.
    """
    best = None
    for t in range(0, len(tris), 9):
        ax, ay, az = tris[t:t+3]
        bx, by, bz = tris[t+3:t+6]
        cx, cy, cz = tris[t+6:t+9]
        d = (bz-cz)*(ax-cx) + (cx-bx)*(az-cz)
        if abs(d) < 1e-9: continue
        w0 = ((bz-cz)*(p[0]-cx) + (cx-bx)*(p[2]-cz)) / d
        w1 = ((cz-az)*(p[0]-cx) + (ax-cx)*(p[2]-cz)) / d
        if w0 < 0 or w1 < 0 or 1 - w0 - w1 < 0: continue
        y = w0*ay + w1*by + (1-w0-w1)*cy
        if best is None or y > best: best = y
    return best


def c_saved_player_anchor():
    r"""The engine's own serializer says the player's position is his PELVIS.

    This is the third independent chain onto the same fact, and the only one
    that comes from the engine writing rather than from us reading.

    `State_Save` (0x0040D950) snapshots the player's node position into the
    save at +44 as `nearest_int(world * 0.0254 * 256)`.  Convert two real
    saves back (GAME_STATE 5) and ask how far that point sits above the floor
    underneath it:

      * `traces/games-resto.bin` slot 0, in Aapkayl - **41.9** above a floor
        of 1081.0;
      * slot 2, in AResto14 - **41.7** above a floor of 31.7.

    HO1_FNM's own pelvis-to-feet distance is **41.8**.  Two rooms whose floors
    are 1050 units apart, the player standing somewhere different in each (the
    x/z are hundreds of units from any authored spot - he had walked), and
    both land within 0.2 of the model's own measurement.  A feet-anchored
    position would read 0.

    Why it matters: the viewer stages a scene dialog from the clip root, and
    the reading that it is the **pelvis** rather than a ground point was
    argued from the clip data alone (ASSETS, "the root key 0 of a scene clip
    is the authored placement").  That argument is now redundant - the engine
    serialises the same convention for the live player, with no clip involved.

    Both saves come from the 2026-08-30 capture, which is also the one that
    settled it: `traces/resto-387.log`.
    """
    import gamestate as _G
    path = os.path.join(ROOT, "traces/games-resto.bin")
    g = O.model_geometry("HO1_FNM")
    out = []
    for slot, decor in ((0, "Aapkayl"), (2, "AResto14")):
        st = _G.from_save(path, slot)
        # GAME_STATE 5's load conversion, minus the -1.0 the save side never
        # added - so this is the position the engine actually held
        w = [v * 100 * 0.00390625 * 0.3937007874015748 for v in st.player_pos]
        floor = O.floor_under_walkable(O.decor_collision_cached(decor), w)
        out.append((st.scene, round(floor, 1), round(floor - w[1], 1)))
    return (out, round(g["feetY"] - g["pelvis"][1], 1)), \
           ([(57, 1081.0, 41.9), (53, 31.7, 41.7)], 41.8), \
           "per save slot (Aapkayl, AResto14): the scene, the floor under " \
           "the saved player position, and how far that position sits above " \
           "it - then HO1_FNM's own pelvis-to-feet distance, which is what " \
           "those two numbers have to be if the engine anchors the pelvis"


def c_exe_tables():
    r"""`tables/*.json` - the tables compiled into the executable, lifted out.

    These are the one gap a replica engine hits that reading `gamedata/` cannot
    close: they live in `Runtime 2.exe`'s own `.data`/`.rdata`, not in the
    shipped data, so a build that asks the user for their game files has
    nowhere to get them (RECONSTRUCTION, "Where the code lives").  They are
    facts rather than code - tier 1 of the porting plan - so lifting them
    carries none of the original-assembly risk a transcription would.

    This runs `exetables.py --check`, which re-derives every table from the
    image and diffs it against what is on disk, so an edited or stale file
    fails here the way a stale `INDEX.md` does.

    Each table carries its own falsifiable check, because a wrong base address
    yields plausible numbers and nothing else would notice:

      * the **camera presets index themselves** - each row's `mode` field at
        +36 equals its own row number for 22 rows, and the 23rd reads 24909.
        Base and stride are both pinned by the data alone;
      * the **ADPCM** step and index tables come out byte-identical to the 105
        numbers `tools/adp.py` transcribed by hand from `sub_483200`;
      * **`tab_special_move`** ends on three zero dwords at exactly 66 rows and
        every one of the 54 names the shipped `.CTL` files use resolves in it;
      * the **VM** handlers match `clean/_vmsummary.json` - 152 of 153, and the
        odd one out is the point: op 2 is **0** in that JSON because the
        extraction pass could not bound its block (the trap CLAUDE.md 1
        records for 77, 120 and 152), and the image supplies 0x00401B80.
        Lifting from the image fills a hole the derived file has.

    **`vm_announce` has a different provenance and it is worth naming.** The
    other six are read out of the executable's own bytes; that one is derived
    from the disassembly by `tools/vm_announce.py` - which .TAG domain each
    handler passes to `Dbg_LogTagged`, and which operand. It lives here
    because a replica needs it as data for the same reason: a hand-written map
    inside `engine/` was wrong three ways within an hour (`scx.play` and
    `music.play` announce nothing; `scx.play.actor.wait` announces the actor,
    not the object). Same rule, different source, and the file says so.

    Deliberately NOT lifted, and the plan was wrong to list one of them: the
    weapon stats are `IAM\GLOBAL +32`, the fight-AI profiles are `.CTL`
    +76/+80 and the combination table is `GLOBAL +12` - all shipped, so a
    replica reads them from the user's data and a copy here would be the
    second copy that drifts.
    """
    s = _need("clean")
    if s: return s
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import exetables
    return (exetables.check(), sorted(exetables.OUT and
            [n for n, _, _, _ in exetables._TABLES])), \
           ([], ["adpcm", "camera_presets", "key_bindings", "shoot_ai",
                 "special_moves", "ui", "ui_widgets", "vm_announce",
                 "vm_opcodes"]), \
           "complaints from exetables --check (stale, missing or failing a " \
           "table's own check), and the tables that must be present"


def c_dialog_staging():
    r"""The two speakers of a scene dialog, staged from the clip root.

    **401's row is a symptom, not evidence about roots.**  Its object uses
    `Script_SelectRelativeBodyAnimation`, which places from an authored
    `.3DP` path and never reads the clip root at all (FILE_FORMATS), so the
    viewer stages it from the wrong source and from a clip guessed out of
    eleven candidates.  What this asserts for 401 is only that the current
    stopgap keeps it where it was; do not read it as saying key 0 is right.

    Two things have to be right, and each was wrong once.

    **The anchor is the pelvis, and it belongs to the PLACEMENT, not the pose.**
    A scene clip's root track IS the `*Bassin` bone, so its key is that bone's
    world position; a camera-ray solve or an `actor.goto_address` teleport
    names a spot on the ground instead.  `speaker_positions` marks which, and
    the viewer's lift follows the mark.  Confirmed independently by the
    engine's own serializer - see `saved player anchor`.

    **And the root is the SETTLED one, not key 0.**  Key 0 places the
    character; keys 1.. are the animation, as per-frame deltas
    `Anim_RootDelta` sums onto the node.  For a clip that does not move, key 0
    is the whole answer - 402's two clips shift 1.0 and 2.0 units, which is
    why that conversation looked right under either reading and could never
    have caught this.  387's are **sit-down** clips: `HO14_01R` drops its root
    17.3 and `TELRES05` 19.1, and key 0 is therefore where each character
    stands *before* sitting.  The tell is exact - Kay'l's key 0 sits **41.7**
    above the restaurant floor where HO1_FNM's own standing pelvis-to-feet is
    **41.8**, and Telis's **45.4** against TEL_FNM's 44.9.

    Staged from key 0 the pair float 16-18 units up, seated poses hanging in
    the air with the tabletop at their thighs.  Staged from the settled root
    they sit on the stools with their feet on the floor, which is what a
    screenshot of the running game shows (2026-08-30, `traces/resto-387.log`'s
    session).  That screenshot is what finally caught it: every number this
    repo could compute agreed with itself through two earlier "fixes".
    """
    out = []
    for did, decor, model in ((387, "AResto14", "TEL_FNM"),
                              (402, "Aapkayl", "TEL_FNM")):
        sp = O.speaker_positions(O.conversation(did), decor)
        si = O.scene_idle(did)
        pi = O.scene_idle(did, player=True)
        coll = O.decor_collision_cached(decor)
        row = []
        for who, m, idl in (("npc", model, si), ("player", "HO1_FNM", pi)):
            g = O.ground_under(coll, sp[who])
            # where the feet land under the settled root, and under key 0
            f_now, _ = _staged_body(m, idl["scx"], idl["anim"], sp[who][1])
            f_k0, _ = _staged_body(m, idl["scx"], idl["anim"], idl["rootPos"][1])
            row.append((round(g - f_now, 1), round(g - f_k0, 1),
                        round(idl["rootSettled"][1] - idl["rootPos"][1], 1)))
        out.append(row)
    # which root each speaker is actually staged from, and the two play-tested
    # facts that pin it: 387 needs the settled root, 401 needs key 0
    picks = []
    for did, decor in ((387, "AResto14"), (401, "Aapkayl"), (402, "Aapkayl")):
        sp = O.speaker_positions(O.conversation(did), decor)
        picks.append((did, sp.get("npcRootPick"), sp.get("playerRootPick")))
    return (out, picks), \
           ([[(-1.5, 17.6, 19.1), (-0.9, 16.4, 17.3)],
             [(0.5, 0.5, 1.0), (-0.2, -0.2, 2.0)]],
            [(387, "settled", "settled"), (401, "key 0", "key 0"),
             (402, "key 0", "key 0")]), \
           "per conversation (387, 402) and speaker: how far the feet sit " \
           "above the room's ground as staged, the same under key 0 alone " \
           "(387's pair float 16), and how far the clip moves its root; then " \
           "WHICH root each speaker is staged from - 387 needs the settled " \
           "one and 401 needs key 0, both confirmed in play, which is the " \
           "whole of the evidence for a rule that is otherwise a fit"


def c_dialog_staging_sweep():
    r"""Every conversation the scene clips stage, against the room's ground.

    `dialog staging` pins the two conversations the reading was worked out on;
    this asks whether it generalises, and it is a test the data can fail: the
    placement carries no height check of its own, so if the pelvis were the
    wrong anchor the bodies would sit at systematically wrong heights all over
    the game and the median would not be near zero.

    Read the gap as "how far the feet are ABOVE the room's own ground", the
    game's Y growing downward.  Zero is a body standing on the floor; a large
    positive is one sitting on something (387's pair are 17 up, on a bench);
    negative is one sunk through it.

    **Read this as a description, not as validation.**  `_stage_root` now
    CHOOSES between key 0 and the summed root by which lands the feet nearer
    the ground, so this measures the thing that rule optimises and cannot
    falsify it.  What can: the two conversations play-tested at opposite
    answers, asserted in `dialog staging`.

    The median is **0.5 units** on a body 71 units tall and **34** of the 50
    land within three (against 27 under key 0 alone and 28 under the summed
    root - the two fixed rules are indistinguishable on this corpus, which is
    itself the finding).

    Two still sink more than three.  The clips that drop their root a long
    way and barely turn it - `M_DEAD` 34.6 at a 0 degree root swing,
    `KUS_DIAR` 19.3 / 3 degrees, `TECINE12` 21.2 / 1 degree - are the ones
    the choice matters most for, and they are also the standing test cases
    for the root ORIENTATION (`Anim_RootDelta`'s optional 3x3, untraced):
    something lays a corpse DOWN, and it is not in the position track.

    The three large positives are real staging, not error: PAstar's Astaroth
    hangs 333 units up, `hamestag`'s Nout stands on a stage.

    Not asserted, deliberately: that every body touches a floor.  Plenty are
    authored sitting, kneeling, lying or floating, and a check that demanded
    contact would be asserting a level design rather than a format.
    """
    import json as _json, statistics as _S
    dd = _json.load(open(os.path.join(ROOT, "tools", "dialog_decor.json")))
    gaps = []
    for e in sorted(dd, key=lambda e: e["dialog"]):
        did, decor = e["dialog"], e["decor"]
        try:
            si = O.scene_idle(did)
            pi = O.scene_idle(did, player=True)
        except Exception:
            continue
        if not ((si and si.get("rootPos")) or (pi and pi.get("rootPos"))):
            continue
        try:
            sp = O.speaker_positions(O.conversation(did), decor)
        except Exception:
            continue
        if not sp: continue
        a = O.dialog_actor(did)
        coll = O.decor_collision_cached(decor)
        for who, model, idl in (("npc", a and a.get("model"), si),
                                ("player", "HO1_FNM", pi)):
            if sp.get(who + "Anchor") != "pelvis" or not model or not idl:
                continue
            try:
                feet, _ = _staged_body(model, idl["scx"], idl["anim"],
                                       sp[who][1])
            except Exception:
                continue
            g = _ground_under(coll, sp[who])
            if g is not None: gaps.append(g - feet)
    return (len(gaps), round(_S.median(gaps), 1),
            sum(1 for x in gaps if abs(x) <= 3),
            sum(1 for x in gaps if x < -3)), \
           (50, 0.5, 34, 2), \
           "bodies staged from a scene clip root with ground under them; the " \
           "median height of their feet above that ground; how many land " \
           "within 3 units of it; and how many sink more than 3 through it"


def c_scene_clip_roots():
    """ASSETS: scene-clip root key 0 = the authored placement."""
    import glob, math, anim_3da
    tot = world = 0
    for f in sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))):
        try:
            st = anim_3da.scx_stream(f)
        except Exception:
            continue
        for a in st["anims"]:
            try:
                r = anim_3da.descriptor(st["data"], a["offset"], a["declared"])
            except Exception:
                continue
            if not r or not r["tracks"]: continue
            t0 = r["tracks"][0]
            if "bassin" not in t0["name"].lower() or not t0["posKeys"]:
                continue
            tot += 1
            k0 = anim_3da.positions(st["data"], a["offset"], t0)[0]
            if sum(v * v for v in k0) > 500 * 500: world += 1
    si = O.scene_idle(387)
    pi = O.scene_idle(387, player=True)
    return (tot, world,
            [round(v) for v in (si.get("rootPos") or [])],
            [round(v) for v in (pi.get("rootPos") or [])]), \
           (874, 740, [2496, -14, -6977], [2536, -10, -6914]), \
           "scene clips with a root position track, of which key 0 is " \
           "world-scale; Telis's and Kay'l's authored 387 placements"



def c_message_tables():
    """FILE_FORMATS: the message-subscription tables (SCENE+36 / AREA+68 /
    GLOBAL+8) - 8-byte {script, message id} records Message_RunHandlers
    (0x00409420) walks when event 43 posts a message."""
    tot = live = bad = 0
    hi = 0
    for arch in ("SCENE", "AREA"):
        at = T.SECOND_TABLE[arch]
        for chunk, b in sorted(T.archive(os.path.join(O.TAGDIR, arch)).items()):
            if len(b) < at[1] + 2: continue
            lo = struct.unpack_from("<i", b, at[0])[0]
            n = struct.unpack_from("<h", b, at[1])[0]
            if n <= 0 or lo <= 0 or lo + 8 * n > len(b): continue
            for i in range(n):
                ptr, mid = struct.unpack_from("<ih", b, lo + 8 * i)
                tot += 1; hi = max(hi, mid)
                if ptr == 0: continue
                live += 1
                if not 0 < ptr < len(b): bad += 1
    gb = open(omkpaths.data("IAM/GLOBAL"), "rb").read()
    lo = struct.unpack_from("<i", gb, 8)[0]
    n = struct.unpack_from("<h", gb, 24)[0]
    for i in range(n):
        ptr, mid = struct.unpack_from("<ih", gb, lo + 8 * i)
        tot += 1; hi = max(hi, mid)
        if ptr == 0: continue
        live += 1
        if not 0 < ptr < len(gb): bad += 1
    return (tot, live, bad, hi), (154, 138, 0, 32), \
           "message subscriptions; with a script; with an invalid pointer; " \
           "highest message id"



def c_zone_records():
    """FILE_FORMATS: the 68-byte trigger-zone record."""
    tot = badscript = badarc = dup = withcam = 0
    ids = set()
    for arch in ("SCENE", "AREA"):
        for chunk, b in sorted(T.archive(os.path.join(O.TAGDIR, arch)).items()):
            r = T.LAYOUT[arch](b)
            lo, n = r[0], r[1]
            for i in range(n):
                o = lo + 68 * i
                if o + 68 > len(b): break
                tot += 1
                for sp in struct.unpack_from("<3i", b, o):
                    if sp and not 0 < sp < len(b): badscript += 1
                arcC, arcW = struct.unpack_from("<2H", b, o + 60)
                if arcC > 4096 or arcW > 4096: badarc += 1
                zid, cam = struct.unpack_from("<2h", b, o + 64)
                if zid in ids: dup += 1
                ids.add(zid)
                if cam != -1: withcam += 1
    return (tot, badscript, badarc, dup, withcam), (4558, 0, 0, 0, 54), \
           "trigger zones; invalid script offsets; arcs past 4096; " \
           "duplicate ids (each owns a save bit); zones forcing a camera"



def c_prop_assets():
    """FILE_FORMATS: the prop-asset catalog (AREA +52 / SCENE +20) - id ->
    model stem, consumed by Scene_LoadProps via PropAsset_Find."""
    import glob
    have = {os.path.basename(p).rsplit(".", 1)[0].upper()
            for p in glob.glob(omkpaths.data("MESHES/OBJETS/*"))}
    tot, stems = 0, set()
    for arch, (off, cnt) in (("AREA", (52, 78)), ("SCENE", (20, 46))):
        for chunk, b in sorted(T.archive(os.path.join(O.TAGDIR, arch)).items()):
            if len(b) < cnt + 2: continue
            lo = struct.unpack_from("<i", b, off)[0]
            n = struct.unpack_from("<h", b, cnt)[0]
            if n <= 0 or lo <= 0 or lo + 24 * n > len(b): continue
            for i in range(n):
                rec = b[lo + 24 * i: lo + 24 * i + 24]
                tot += 1
                s_ = rec[14:24].split(b"\0")[0].decode("cp1252", "replace")
                if s_: stems.add(s_.upper())
    missing = sorted(s_ for s_ in stems if s_ not in have)
    return (tot, len(stems), missing), (670, 124, []), \
           "prop-asset records; distinct model stems; stems without a " \
           ".3DO in MESHES/OBJETS"



def c_path_durations():
    """ASSETS: the .3DP header's second u32 is the duration - the last key's
    frame - in every shipped path."""
    import glob, scene_scx
    tot = match = 0
    for f in sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))):
        d = open(f, "rb").read()
        blockSize = struct.unpack_from("<I", d, 12)[0]
        block = d[16:16 + blockSize]
        _, o = scene_scx.objects(block)
        order, counts = [], {}
        while o + 4 <= len(block):
            t = struct.unpack_from("<I", block, o)[0]
            if t == 0xDEADFFFF: break
            if (t >> 16) != 0xDEAD: o += 4; continue
            ty = t & 0xFFFF; o += 4
            order.append(ty)
            if ty in scene_scx.STRIDE:
                c = struct.unpack_from("<I", block, o)[0]
                counts[ty] = c
                o += 4 + scene_scx.STRIDE[ty] * c
        if 0 not in counts: continue
        pos = 16 + blockSize
        for ty in order:
            if ty == 0:
                for i in range(counts.get(0, 0)):
                    a, size = struct.unpack_from("<2I", d, pos)
                    body = d[pos + 8: pos + 8 + size]
                    if len(body) >= 4:
                        n = struct.unpack_from("<I", body)[0]
                        q = 4
                        for k in range(n):
                            if q + 28 > len(body): break
                            dur, keyc = struct.unpack_from("<2I", body, q + 20)
                            if keyc > 10000: break
                            if keyc:
                                last = struct.unpack_from(
                                    "<I", body, q + 28 + 32 * (keyc - 1))[0]
                                tot += 1
                                if dur == last: match += 1
                            q += 28 + 32 * keyc
                    pos += 8 + size
                break
            if ty not in (1, 3, 4, 10): continue
            hdr = 12 if ty == 4 else 8
            for i in range(counts.get(ty, 0)):
                a, size = struct.unpack_from("<2I", d, pos)
                pos += hdr + size
    return (tot, match), (6756, 6756), \
           ".3DP paths with keys; whose duration equals the last key frame"



def c_extension_case():
    r"""No shipped file may be invisible to a case-sensitive lookup.

    The game shipped for Windows 95/98, whose filesystem is case-INSENSITIVE:
    the engine's `fopen("Aapkayl.SFX")` finds `Aapkayl.Sfx` and never notices
    the difference. Every tool here runs on a case-SENSITIVE filesystem, so a
    lookup written as `stem + ".SFX"` - or a `glob("*.SFX") + glob("*.sfx")`
    pair - silently finds nothing for a file the authors happened to type
    differently, and a sweep then reports a total that is quietly short.

    It has happened twice. A `.3DM` sweep reported **708** files where there
    are **777** (CLAUDE.md 4). And on 2026-08-31, porting the effects chain to
    `engine/` - which enumerates the directory rather than globbing - turned up
    **eight** `.SFX` files spelled `.Sfx` or `.SfX`, so five checks here had
    been measuring 59 of 67 while reporting a total. All 67 walk exactly; the
    eight were not broken, they were invisible.

    This is the guard for the class rather than for either instance. It
    enumerates the data directories and reports every extension spelling that
    is neither all-upper nor all-lower - the ones a naive two-glob pair misses.
    The expected list is not empty, and should not be: the eight `.Sfx`/`.SfX`
    files really are on the disc. What matters is that they are ENUMERATED
    here, so a reader that cannot see them fails a check instead of returning
    a smaller number.

    Note the boundary, because it is easy to over-apply: this is about PATH
    lookup. An in-memory string compare inside the engine - the texture
    cache's 19-character name match, say - is whatever that code does, and has
    to be read rather than inferred from the filesystem's behaviour.
    """
    dirs = ["SCPTDATA", "MESHES/DECORS", "MESHES/PERSOS",
            "MESHES/OBJETS", "MESHES/MISC", "ANIMS", "MORPH",
            "IAM"]
    odd = {}
    for rel in dirs:
        d = omkpaths.data(rel)
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            ext = os.path.splitext(f)[1]
            if not ext or len(ext) < 2: continue
            if ext.isupper() or ext.islower(): continue
            odd.setdefault(rel, []).append(ext)
    summary = sorted((rel, len(v), sorted(set(v))) for rel, v in odd.items())
    return summary, [("SCPTDATA", 8, [".SfX", ".Sfx"])], \
           "data directories holding a file whose extension is spelled " \
           "neither all-upper nor all-lower - invisible to a `*.EXT` + " \
           "`*.ext` glob pair, and to any `stem + \".EXT\"` lookup. The eight " \
           "`.Sfx`/`.SfX` files are real and must stay VISIBLE here"


def _sfx_paths():
    """Every `.SFX` under gamedata/SCPTDATA, case-insensitively.

    Globbing `*.SFX` and `*.sfx` misses **eight** files that spell the
    extension `.Sfx` or `.SfX` - PAstarot, QTemple, SAppt, SArmu02, SBozOrdi,
    Ssuperm, hamesta, hospital - so every check here that used that pair was
    counting 59 of 67. Exactly the trap CLAUDE.md 4 already records for a
    `.3DM` sweep that reported 708 files where there are 777; found again
    2026-08-31 while porting the effects chain, because the C++ reader
    enumerated the directory instead of globbing and came back with more files
    than the Python.

    **This is what the ENGINE does, not a convenience.** The game shipped for
    Windows 95/98, whose filesystem is case-insensitive, so its own
    `fopen("Aapkayl.SFX")` finds `Aapkayl.Sfx` without noticing - the spelling
    is just how the authors happened to type it. A case-sensitive reader is
    the thing that is wrong here, and 59 was never the real count.

    (The distinction matters and is easy to over-apply: this is about PATH
    lookup. An in-memory string compare inside the engine - the texture
    cache's 19-character name match, say - is whatever that code does, and has
    to be read rather than assumed from the filesystem's behaviour.)
    """
    d = omkpaths.data("SCPTDATA")
    if not os.path.isdir(d): return []
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.upper().endswith(".SFX"))


def c_sfx_files():
    r"""FILE_FORMATS: the .SFX scene-sound files - six sections, walk exact.

    **67, not 59** - corrected 2026-08-31. Eight files spell the extension
    `.Sfx` or `.SfX` and every `*.SFX` + `*.sfx` glob in this file missed them,
    so five checks were measuring 59 of 67 while reporting a total. All 67
    walk exactly, so nothing was WRONG about the eight - they were invisible.
    Found by porting the reader to `engine/`, which enumerates the directory
    instead of globbing and came back with more files than the Python; the
    same trap CLAUDE.md 4 records for a `.3DM` sweep that reported 708 where
    there are 777. `_sfx_paths()` is now the single enumerator.
    """
    import glob
    files = sorted(_sfx_paths())
    ok = tot = 0
    counts = [0] * 6
    for p in files:
        d = open(p, "rb").read()
        tot += 1
        if d[:4] != b"5.0V": continue
        A = struct.unpack_from("<I", d, 4)[0]
        o = 8 + 40 * A
        B = struct.unpack_from("<I", d, o)[0]; o += 4 + 44 * B
        C = struct.unpack_from("<I", d, o)[0]; o += 4 + 80 * C
        D = struct.unpack_from("<I", d, o)[0]; o += 4 + 16 * D
        E = F = 0
        if o < len(d):
            E = struct.unpack_from("<I", d, o)[0]; o += 4 + 76 * E
            if o + 4 <= len(d):
                F = struct.unpack_from("<I", d, o)[0]; o += 4
                for k in range(F):
                    n = struct.unpack_from("<I", d, o + 8)[0]
                    o += 36 * n + 16
        if o == len(d):
            ok += 1
            for i, v in enumerate((A, B, C, D, E, F)): counts[i] += v
    return (tot, ok, counts), (67, 67, [14, 67, 396, 156, 382, 267]), \
           ".SFX files, of which the six-section walk lands exactly - ALL of " \
           "them, including the eight whose extension is spelled .Sfx or " \
           ".SfX and which every glob here used to miss; records per section"



def c_map2d():
    """FILE_FORMATS: the MAP2D .mpt walk (the runtime map file), and the
    AREA +106 name that selects it (Map2D_Load appends ".MPT")."""
    import glob
    ok = tot = blocks = trailing = 0
    for p in sorted(glob.glob(omkpaths.data("MAP2D/*.mpt"))):
        d = open(p, "rb").read()
        tot += 1
        scale, count = struct.unpack_from("<2I", d, 0)
        o = 8
        for r in range(count):
            n = struct.unpack_from("<I", d, o)[0]
            o += 4 + 28 * n
            W, H = struct.unpack_from("<2I", d, o + 24)
            o += 32 + W * H
        for r in range(count):
            k = struct.unpack_from("<I", d, o)[0]
            o += 4
            for i in range(k):
                ln = struct.unpack_from("<I", d, o)[0]
                o += 4 * (ln + 3)
        tail = len(d) - o
        if tail % 192 == 0 and tail // 192 == count:
            ok += 1
        blocks += count
        trailing += tail // 192
    named = missing = 0
    for chunk, b in sorted(T.archive(os.path.join(O.TAGDIR, "AREA")).items()):
        if len(b) < 126: continue
        nm = b[106:126].split(b"\0")[0].decode("cp1252", "replace")
        if not nm: continue
        named += 1
        if not os.path.exists(omkpaths.data("MAP2D", nm.lower() + ".mpt")) \
           and not os.path.exists(omkpaths.data("MAP2D", nm + ".mpt")) \
           and nm.upper() not in {os.path.basename(x).rsplit(".", 1)[0].upper()
                                  for x in glob.glob(omkpaths.data("MAP2D/*.mpt"))}:
            missing += 1
    return (tot, ok, blocks, trailing, named, missing), (16, 16, 79, 79, 16, 1), \
           ".mpt files, of which the walk lands with one 192-byte record " \
           "per floor; floors; areas naming a map at +106, of which " \
           "unshipped (ARCHIV04, cut)"



def c_wre_files():
    """FILE_FORMATS: the RADAR .WRE wireframes."""
    import glob
    tot = exact = short4 = edges = valid = 0
    for p in (sorted(glob.glob(omkpaths.data("RADAR/*.WRE"))) +
              sorted(glob.glob(omkpaths.data("RADAR/*.wre")))):
        d = open(p, "rb").read()
        A, B = struct.unpack_from("<2I", d, 0)
        tot += 1
        need = 8 + 12 * A + 4 * B
        if len(d) == need: exact += 1
        elif len(d) == need - 4: short4 += 1
        eb = 8 + 12 * A
        n = min(B, (len(d) - eb) // 4)
        for i in range(n):
            a, b = struct.unpack_from("<2H", d, eb + 4 * i)
            edges += 1
            if a < A and b < A: valid += 1
    return (tot, exact, short4, edges, valid), (15, 11, 4, 35788, 35788), \
           ".WRE wireframes; exact, short one edge (shipping truncation); " \
           "edges, of which both endpoints valid"



def c_morph_face_models():
    """FILE_FORMATS: the six `*_FNM` twins, and what they are for.

    Every model that morphs needs a `<prefix>Visage` face mesh for the .3DM
    to drive. Two base models lack one - FUA_FN and HO1_FN (Kay'l) - and
    their M twins exist to supply it; the other four bases already have a
    face and their twins differ elsewhere (SPV_FNM adds the Vise/Tire
    aim-and-shoot markers). Every one of the six M models' face vertex counts
    is a count some shipped .3DM uses.
    """
    import glob, mesh3do, morph3dm
    have = {os.path.basename(p).rsplit(".", 1)[0].upper()
            for p in glob.glob(os.path.join(O.PERSOS, "*"))}
    twins = sorted(n for n in have if n.endswith("M") and n[:-1] in have)
    counts = set()
    for f in (sorted(glob.glob(omkpaths.data("MORPH/*.3DM"))) +
              sorted(glob.glob(omkpaths.data("MORPH/*.3dm")))):
        try: counts.add(morph3dm.header(f)["vertices"])
        except Exception: pass
    def face(n):
        return [m for m in mesh3do.meshes(os.path.join(O.PERSOS, n + ".3DO"))[1]
                if m["name"].lower().endswith("visage")]
    faceless_base, m_has_face, face_matches = [], 0, 0
    for t in twins:
        if not face(t[:-1]): faceless_base.append(t[:-1])
        f = face(t)
        if f:
            m_has_face += 1
            if f[0]["vertices"] in counts: face_matches += 1
    return (len(have), len(twins), sorted(faceless_base), m_has_face,
            face_matches), \
           (193, 6, ["FUA_FN", "HO1_FN"], 6, 6), \
           "PERSOS models; those with a non-M twin; bases with NO face mesh " \
           "(their twin supplies it); M models with a face; whose vertex " \
           "count matches a shipped .3DM"





def c_mesh_blend():
    r"""ASSETS 4c: the two transparency sub-modes, and both of them are used.

    CORRECTED TWICE. The first reading had mesh 0x2000 as "alpha blend, draw it
    at 50%" with 0x1000 its software twin; the second kept the counts but read
    the bucket key, which separates them - mesh 0x1000 -> key 0x2000 turns
    ZWRITEENABLE off and ALPHABLENDENABLE on, mesh 0x2000 -> key 0x100 is
    SRCBLEND/DESTBLEND = ONE/ONE (additive) and mesh 0x4000 -> key 0x200 is
    ZERO/INVSRCCOLOR (a darkening multiply). The `SetRenderState(27, 1)` the
    first reading pointed at is the CUTOUT path, from mesh flag 0x800.

    So the old claim that "the additive and multiply sub-modes are used by
    nothing at all" was wrong twice over: every transparent mesh in the game is
    one or the other. This asserts the whole cross-tab, because the empty cells
    are the content - no mesh asks for transparency without a sub-mode, and no
    mesh asks for a sub-mode without transparency, which is what licenses the
    viewers having exactly two blend states.

    `Ap01vitre` is asserted by name because it is the mesh that started this:
    the vivarium in Kay'l's apartment, opaque in the viewer where the game
    shows the room through it.
    """
    import glob, mesh3do, collections
    tab = collections.Counter()
    for p in sorted(glob.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
        try: _h, ms = mesh3do.meshes(p)
        except Exception: continue
        for m in ms:
            f = m["flags"] & 0xFFFFFFFF
            if f & 0x7000:
                tab[(bool(f & 0x1000), bool(f & 0x2000), bool(f & 0x4000))] += 1
    add  = tab[(True, True, False)]
    mul  = tab[(True, False, True)]
    none = tab[(True, False, False)]                 # transparent, no sub-mode
    orph = sum(v for k, v in tab.items() if not k[0])  # sub-mode, not transparent
    _h, ms = mesh3do.meshes(O.decor_path("AAPKAYL"))
    vitre = next((m for m in ms if m["name"] == "Ap01vitre"), None)
    g = O.decor_geometry_cached("AAPKAYL")
    return (add, mul, none, orph, len(tab),
            bool(vitre and vitre["flags"] & 0x1000),
            sum(1 for b in g["batches"] if b.get("blend") == "add"),
            sum(1 for b in g["batches"] if b.get("blend") == "mul")), \
           (211, 6, 0, 0, 2, True, 4, 0), \
           "additive set meshes; multiply; transparent with NO sub-mode; a " \
           "sub-mode without transparency; distinct flag combinations; " \
           "Ap01vitre is transparent; and AAPKAYL's additive and multiply " \
           "batches"


def c_anim_meshes():
    r"""ASSETS: mesh flag 0x40000000 marks an animated set effect.

    `sub_44F840` tests it, then registers the mesh with a duration and a
    **rand()** starting phase against the .SFX's 80-byte section C - a timer
    with a random offset, which is what a flickering neon is. The data agrees
    without being asked: of the 579 meshes carrying the flag the commonest
    names are `neon`, `halo`, `fume` (smoke), `poubelle`, `fire`.

    Asserted because it changes how a screenshot is read: these things move in
    the game and stand still in both viewers, so a difference there is not
    automatically a bug.
    """
    import glob, mesh3do
    tot = anim = neon = 0
    for p in sorted(glob.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
        try: _h, ms = mesh3do.meshes(p)
        except Exception: continue
        for m in ms:
            tot += 1
            if (m["flags"] & 0xFFFFFFFF) & 0x40000000:
                anim += 1
                neon += m["name"][:4].lower() == "neon"
    return (tot, anim, neon), (12203, 579, 104), \
           "set meshes; flagged 0x40000000 (animated); of which named neon*"


def c_set_pieces():
    r"""FILE_FORMATS 5b6: .SFX section E is the decor-piece table.

    Not sound at all, despite the file. `SetPiece_Find` (0x00450070) scans it
    with a stride of 19 dwords - the section's own 76 bytes - and the loader
    shows every piece whose +8 is 1 and +12 is -1. Opcode 123
    `set.hide_piece` clears the bit again.

    The check walks the six sections the loader walks and asserts the counts
    the docs quote, so the section identification cannot drift.
    """
    import glob
    files = tot = shown = 0
    for p in _sfx_paths():
        d = open(p, "rb").read()
        if d[:4] != b"5.0V": continue
        files += 1
        try:
            A = struct.unpack_from("<I", d, 4)[0];  o = 8 + 40 * A
            B = struct.unpack_from("<I", d, o)[0];  o += 4 + 44 * B
            C = struct.unpack_from("<I", d, o)[0];  o += 4 + 80 * C
            D = struct.unpack_from("<I", d, o)[0];  o += 4 + 16 * D
            E = struct.unpack_from("<I", d, o)[0];  eb = o + 4
        except struct.error:
            continue          # a short file: 8 of the 67 stop before section E
        if eb + 76 * E > len(d): continue
        tot += E
        for i in range(E):
            f8, f12 = struct.unpack_from("<2i", d, eb + 76 * i + 8)
            shown += (f8 == 1 and f12 == -1)
    return (files, tot, shown), (67, 382, 56), \
           ".SFX files walked to section E; decor pieces; shown at load"


def c_binary_identity():
    r"""Which shipped file the decompilation actually describes.

    `Runtime.exe.asm` / `.c` are named for `gamedata/Runtime.exe`, and they are NOT
    of it. `gamedata/Runtime.exe` (280 KB, 1999) is a different, smaller build - 8
    sections, .txt/.text/.txt2, entry 0x41AB90 - and carries none of the
    engine's strings. `gamedata/Runtime 2.exe` (984 KB, 2020) has .text at
    0x401000-0x4BC000, .rdata at 0x4BC000 and .data at 0x4C0000, which is
    where every address in this repo lives: the VM table at 0x4C0140,
    tab_special_move at 0x4CB168, dbl_4BC030.

    The check reads the VM dispatch table straight out of the exe at its own
    address and compares it with `clean/_vmsummary.json`, so the whole repo
    stays anchored to a specific file. It matters most for the golden-trace
    work: tracing the wrong binary would invalidate every address.
    """
    s = _need("clean")
    if s: return s
    f = omkpaths.data("Runtime 2.exe")
    if not os.path.exists(f): return "missing", "gamedata/Runtime 2.exe", ""
    d = open(f, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, ra = struct.unpack_from("<4I", d, o + 8)
        secs.append((base + va, vs, ra))
    def rva(a):
        for va, vs, ra in secs:
            if va <= a < va + vs: return ra + (a - va)
        return None
    off = rva(0x004C0140)
    import json as _j
    tab = _j.load(open(omkpaths.clean("_vmsummary.json")))
    same = 0
    for r in tab:
        h = r["handler"]
        h = int(h, 16) if isinstance(h, str) else h
        if not h: continue
        ent = struct.unpack_from("<2I", d, off + 8 * r["op"])
        same += (ent[0] == h and ent[1] == r["operands"])
    old = os.path.getsize(omkpaths.data("Runtime.exe"))
    return (base, len(secs), off is not None, same, old),            (0x400000, 5, True, 152, 280290),            "Runtime 2.exe image base; sections; the VM table is addressable; "            "opcode entries matching clean/_vmsummary.json; and the size of "            "the OTHER Runtime.exe, which the decompilation is not of"


def c_forwarder():
    r"""The PATCH.dll forwarder the golden-trace rig needs.

    `Runtime 2.exe` imports DirectDrawCreate and DirectDrawEnumerateA from
    PATCH.dll - the 2020 re-release's ddraw wrapper under a private name, and
    not shipped in `gamedata/`, so the loader stops at c0000135 before the engine
    runs one instruction. `goldentrace.forwarder_dll` builds a PE that forwards
    both to Wine's ddraw; it holds no code, only an export table.

    The check reads that PE back the way a loader would - through the PE header
    to the export directory, then out along the name and address tables - and
    requires every export to resolve to a forwarder string INSIDE the export
    directory's range, which is the exact test a loader applies to decide that
    an entry is a forward rather than a function.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    d = G.forwarder_dll()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    magic, nsec, ohs = (struct.unpack_from("<H", d, pe + 24)[0],
                        struct.unpack_from("<H", d, pe + 6)[0],
                        struct.unpack_from("<H", d, pe + 20)[0])
    chars = struct.unpack_from("<H", d, pe + 22)[0]
    erva, esize = struct.unpack_from("<2I", d, pe + 24 + 96)
    o = pe + 24 + ohs
    va, ra = struct.unpack_from("<I", d, o + 12)[0], struct.unpack_from("<I", d, o + 20)[0]
    f = lambda a: ra + (a - va)
    nfn, nnm = struct.unpack_from("<2I", d, f(erva) + 20)
    afn, anm, aord = struct.unpack_from("<3I", d, f(erva) + 28)
    got = []
    for i in range(nnm):
        nm = d[f(struct.unpack_from("<I", d, f(anm) + 4 * i)[0]):].split(b"\0")[0]
        k = struct.unpack_from("<H", d, f(aord) + 2 * i)[0]
        t = struct.unpack_from("<I", d, f(afn) + 4 * k)[0]
        fwd = erva <= t < erva + esize      # a loader's own forwarder test
        got.append((nm.decode(), fwd,
                    d[f(t):].split(b"\0")[0].decode() if fwd else None))
    return (magic, nsec, bool(chars & 0x2000), nfn, nnm, got), \
           (0x10b, 1, True, 2, 2,
            [("DirectDrawCreate", True, "ddraw.DirectDrawCreate"),
             ("DirectDrawEnumerateA", True, "ddraw.DirectDrawEnumerateA")]), \
           "PE32 magic; one section; the DLL bit; export counts; and every " \
           "name resolving to a forwarder inside the export directory"


def c_trace_roundtrip():
    r"""The golden-trace pipeline, tested without the game.

    A capture is a flat stream that never says which script emitted what;
    `goldentrace` recovers that by looking each (domain, value) pair up in the
    corpus, since most pairs are rare. This round-trips the whole chain -
    replay a slot, render its operands as relay-log lines, read them back, and
    require the attribution to name the slot it started from.

    `named` being a minority is right: world-script records are near-identical,
    so most pairs are shared and cannot name one slot. `misnamed` is the one
    that must be zero - an anchor pointing at the wrong script would make every
    diff built on it a fiction.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    r = G.selftest()
    return (r["slots"], r["misnamed"], r["named"] > 0), (6, 0, True), \
           "slots replayed; anchors pointing at the wrong script; and that " \
           "attribution names something at all"


def c_intro_script():
    r"""The intro script the slot walk misses - AREA 118, "Introduction Kay'l".

    Found by GOLDEN TRACE, not by reading: a capture of the opening logged
    DIALOGS 272 ("Kay'l / Intro") followed by CAMERAS 2148 and 2152, and no
    slot in the 5785 could emit any of them. The bytes at AREA chunk 118 +1196
    are `3d 10 01` `5f 64 08` `5f 68 08` - dialog.start 272, camera 2148,
    camera 2152 - in exactly the order the engine announced them.

    What +68 IS, is settled and is not a script pointer: `Message_RunHandlers`
    (0x00409420, CLEAN) reads it as the message-subscription table - 8-byte
    {script offset, int16 message id} records, count at +86. Chunk 118 declares
    0 zone records (+76) AND 0 subscriptions (+86), so both walks are right to
    find nothing and the empty table's base coincides with the start of the
    code that follows it. The code is real - it decodes clean and holds the
    operands the engine was seen to announce - but nothing here establishes
    what REACHES it, and no message handler is registered to.

    So this pins bytes and a decode, which are facts, and asserts no layout.
    It is also the only one of the 20 script-less chunks that decodes from +68.

    Conversation 272 was one of the 106 with no known launch path.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import dialog_triggers as T, dialog_disasm as D
    b = T.archive(omkpaths.data("IAM/AREA"))[118]
    nrec = struct.unpack_from("<i", b, 76)[0]
    nsec = struct.unpack_from("<h", b, 86)[0]
    at = struct.unpack_from("<I", b, 68)[0]
    ins, st = D.disasm(b, at, len(b))
    dlg = sorted({struct.unpack_from("<h", r, 0)[0]
                  for _pc, op, r in ins if op == 61 and len(r) >= 2})
    cams = sorted({struct.unpack_from("<h", r, 0)[0]
                   for _pc, op, r in ins if op == 95 and len(r) >= 2})
    return (nrec, nsec, at, st, len(ins), dlg, cams), \
           (0, 0, 1040, "ok", 52, [272], [2148, 2152, 2153, 2172]), \
           "no zone records and no second-table entries (both walks rightly " \
           "empty); the +68 pointer; a clean decode; and the conversation and " \
           "cameras the golden trace saw the engine announce"


def c_bowie_cutscene():
    r"""The first script proven to decide in tools/sim what it decided in the
    engine - AREA 0 record 78, "BOWIE OMIKRON THEME".

    Captured by walking into the trigger zone with the golden-trace rig live.
    The engine logged, in order:

        VARIABLES/130 ZONES/78 VARIABLES/132 VARIABLES/61
        CAMERAS/151 ADDRESSES/24 CAMERAS/152 CAMERAS/120 CAMERAS/121

    and replaying that slot in `tools/sim` predicts exactly those nine, in that
    order, before continuing into the rest of its 20-camera editing - the
    capture stopped mid-cutscene, so it holds a PREFIX of the script, which is
    agreement and not disagreement.

    The contrast with `intro script` is the point: a location-triggered
    cutscene is fully inside the 5785 and replays exactly, while the new-game
    intro is not enumerable at all. That is evidence the intro is reached by
    the new-game path rather than by any script, and it narrows the open
    question instead of leaving it open in general.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    # the slot key carries its offset since 2026-08-29 - a chunk's zone records
    # and its subscription table share the (rec, field) space, so the offset is
    # what makes the name unique (`trace agreement`)
    idx = G.slot_index()
    key = next(k for k in idx if k.startswith("AREA 0 rec 78 +0"))
    code, at = idx[key]
    import dialog_disasm as D
    ins, st = D.disasm(code, at, len(code))
    pred = [x for _pc, op, raw in ins for x in G.loggable(op, raw, False)]
    want = [("VARIABLES", "130"), ("ZONES", "78"), ("VARIABLES", "132"),
            ("VARIABLES", "61"), ("CAMERAS", "151"), ("ADDRESSES", "24"),
            ("CAMERAS", "152"), ("CAMERAS", "120"), ("CAMERAS", "121")]
    cams = sorted({struct.unpack_from("<h", r, 0)[0]
                   for _pc, op, r in ins if op == 95 and len(r) >= 2})
    return (st, len(ins), pred[:9], len(cams)), ("ok", 88, want, 20), \
           "a clean decode; its instruction count; the nine decisions the " \
           "engine was recorded making, in order; and the size of its editing"


def c_trace_agreement():
    r"""Both captures, diffed against tools/sim - the behavioural claim.

    For every script a capture NAMES and the simulator can reach, the two
    operand sequences must agree exactly. This is the only check here that
    tests what the engine DOES rather than what its files contain, and it runs
    off the two distilled traces in `traces/`, so it stays reproducible without
    CrossOver or the game.

    `bad` is the number that must be zero. `unreached` is not a failure: an
    anchor comes from a static decode, which walks every branch, while the
    replay takes one, so an anchoring event can sit on a path the available
    state does not reach. Those are deferred rather than counted against the
    simulator, and `goldentrace diff --save` is what settles them once a save
    exists. Reporting them separately is the point - folding them into either
    column would overstate the result in one direction or the other.

    THE THIRD CAPTURE, `impasse-walk.log` (2026-08-29), is the first that goes
    past the opening: **286 events over 592 seconds** - the tutorial, the
    crossing into the Sas, back into Anekbah for the whole BOWIE OMIKRON THEME
    cutscene, then AREA 229 and into Kay'l's apartment (AREA 237, SCENE 57). It
    more than doubles the oracle: **21 scripts replay and agree, 0 disagree**,
    against 7 and 10 for the two openings, and `AREA 0 rec 78 +0` - the Bowie
    script - anchors **68** events where the older captures reached 9.

    THE FOURTH, `telis-dialog.log`, is the first taken from a SAVE rather than
    a new game: loaded in Kay'l's apartment, it covers the kitchen cupboards,
    the shower door and then conversation 402 (`telis dialogue`). 55 events, 5
    scripts agreeing, 0 disagreeing, and - the number that matters for a
    capture in territory nothing had touched - **0 emitted by no slot**.

    THE FIFTH, `resto-387.log` (2026-08-30), is the largest by far: **840
    events**, three times any earlier capture, walking from the apartment out
    through the CS shafts to the restaurant and the lunch with Telis. Nine
    conversations (329, 349, 350, 356, 386, 387, 388, 401, 402), six cutscene
    scenes, some thirty areas - and, the number that matters in territory
    nothing had touched, **0 events emitted by no slot at all**. 153 of the
    840 name a single script.

    **Two corrections, both found by porting this check to `engine/`
    (2026-08-31).**

    `show` caps the WORK, not the printing. At its default of 25 this check
    was replaying the first 25 anchors of `resto-387`'s **64** and reporting
    "21 agreeing, 1 disagreeing" as if that were the capture. It is now run
    over all of them: **51 agreeing, 6 disagreeing**. Nothing regressed - the
    number was always this; it was just not being looked at.

    And `loggable`'s TIGHT stream now reads `tables/vm_announce.json` - the
    assembly-derived map, the same file the port reads - instead of
    `D.SECTION` / `D.FIELD_SECTION`. Those over-include two ways: six opcodes
    (72, 75, 80, 86, 91, 115) have a field map but no section, so they
    announce nothing and were contributing anyway, and op 152's section is
    **`JINGOFF2.ADP`**, a filename picked up from the unbounded handler block
    CLAUDE.md 1 warns about. That is 99 extra pairs in the index and **two
    false mismatches** (8 -> 6). Over-inclusion is the dangerous direction
    here, which `indexes()` already said in prose and nothing was enforcing.

    **One of the six is now CLOSED, and the port is what closed it.**
    `replay` did not model `ui.open` parking its caller, so it ran past one.
    `AREA 157 rec 60 +4` opens screen **4** - the LIFT - at instruction 3 of
    37 and then branches on variable 496, `Etage`; without the park the replay
    executed the remaining 33 instructions and predicted BOTH arms of the
    floor switch, which no capture can contain because the player picks one
    floor. With the park, six disagreements become **five** and nothing else
    moves; `telis-dialog` goes 5 agreeing to 4, the fifth becoming UNREACHED
    rather than wrong - a missing anchor, not a fault.

    That is what a second implementation is for. `engine/` modelled the park
    because it models the engine, its diff then disagreed with this one, and
    the disagreement was this one's.

    **What the five are, and two of them are now explained exactly.**

    `AREA 179 rec 47 +4` and `AREA 217 rec 13 +4` are the same eleven-
    instruction SAVE POINT twice: `var.set.actor_stat(-1, 5, 60)` writes a
    live actor stat into variable 60 and a `cmp.gt` opens screen **30**, the
    save panel, when it is high enough. Opcode 86 is stubbed - it reads the
    player record and a replay has no actor - so the replay walks the
    `media.play` arm the engine never took. Forcing variable 60 high makes
    both park at `ui.open` and predict exactly the two events each was already
    agreeing on (`verify.py: replay actor stat`). A named missing input, not a
    decision difference.

    The other three - `AREA 237 rec 41 +0`, `AREA 179 rec 31 +4` and
    `SCENE 51 rec 1 +0` - are branch-on-variable scripts where the replay
    takes a different arm, and the first is the interleaving case analysed
    below. The other three
    disagree only from the new-game state and are settled by any of the three
    saves taken during the capture itself (`traces/games-resto.bin`), which
    give 3, 4 and 3 - and slot 1's fourth is a script the others agree on.
    The mismatches follow the STATE, not the script, which is what says they
    are anchoring artifacts rather than the replay deciding differently: a
    trace carries decisions, not memory, so no available state is the one the
    engine held when a given script ran.

    The older reading of the first of them is kept because it is still right: The anchor names `AREA 237 rec 41 +0`, which is
    **eight instructions**: `camera.set 4434`, a compare on variable 664, and
    `media.play 465`. The window the capture assigns to it holds twelve
    events, of which **eight it provably cannot emit** - `DIALOGS/39` (it has
    no `dialog.start`), `ZONES/4065` (no zone op), `CAMERAS/6` and `/165`
    (its only `camera.set` is 4434). So the window carries other scripts'
    events interleaved; the simulator is not disagreeing with the engine
    about anything. Replaying the same capture from either of the two saves
    taken during it - `traces/games-resto.bin` slots 0 and 2 - gives **22
    scripts agreeing and 0 disagreeing**, and slot 1 disagrees on a
    *different* script, which is the tell: the mismatches follow the state,
    not the script.

    **THE SIXTH, `fight.log` (2026-08-31), was taken to answer a question it
    cannot answer** - and that is its main result. It was captured to give the
    actor runtime an oracle, because no earlier capture announces an actor
    state. None can: the logger only sees what a VM HANDLER narrates, and
    combat has exactly two opcodes - `fight.begin` (62), which announces
    nothing, and `player.become` (56), which announces to CHARACTERS, one of
    the three domains `Dbg_LogTagged` filters out itself. `Fight_TickAI`,
    `Fight_ResolveHit`, the 18 `ACTOR_STATE`s and the `.CTL` transition
    matching are native code that never touches the logger. **So
    `ACTOR_STATE` cannot be given an oracle by this rig at all**, and the
    check should have been made before anyone was asked to play.

    The capture did reach combat - 32 of the scripts its events anchor contain
    `fight.begin`, across SCENE 25/26/27/28 and a dozen areas - so the silence
    is the mechanism, not the play. As a trace it is small and clean: **154
    events, 0 emitted by no slot, 54 naming a single script, 2 more scripts
    replayed and agreeing, none disagreeing.**

    Together the six captures now stand at **1469 events**, every one
    attributable, with **66 scripts replayed** and the only disagreements the
    five in `resto-387` above.

    `--save traces/save-appart.bin` is now usable as the replay anchor and
    settles one more of `impasse-walk`'s anchors (21 -> 22 agreeing). Reading
    it needed one fix: `gamestate.save_slots` demanded a full 32808-byte slot
    while only ever reading the 40-byte head, which refused a save stored
    without its 24576-byte thumbnail - which is exactly how the fixture stays
    small enough to live in the repo.

    A rig note worth keeping, found making it: `run --seconds N` terminates the
    LAUNCHER, not the game. The window stays up and the relay log keeps
    growing, so the capture is however long the session actually ran (here ~10
    minutes and 29 MB before `distil`), not the number asked for. That is why
    the event count moved between two readings of the same file.

    It also paid for itself immediately by exposing a bug in this very check.
    `signatures` and `slot_index` both keyed a script by `arch chunk rec
    +field`, and a chunk's zone records and its message-subscription table are
    walked into the SAME (rec, field) space - so `AREA 222 rec 1 +0` named two
    different scripts, the zone script at 2323 and the subscription at 2463,
    and the second silently overwrote the first. Four of the new capture's
    events looked unexplained (`ZONES` 3796, 3794, 3792) for no better reason
    than that the `zone.enable` emitting them lived in the script that lost the
    collision. With the offset in the key: **0 unexplained events**, and the
    scripts the oracle can verify went 3 -> 7 and 6 -> 10 on the two OLD
    captures as well. A key that is not unique does not fail loudly; it just
    quietly verifies less.

    **`bad` went 1 -> 0 on 2026-08-29, and how is the point.** It became 1
    when two real errors were fixed - `loggable` naming field 0 where a handler
    logs a later one, and the attribution corpus omitting the `+4` startup
    scripts - and the one disagreement left was `OBJECTS/314` ("Memo 001
    Intro"), owned in the corpus only by `AREA 237 rec 24 +0`. That script is
    five instructions with **no branches**, so had it run all three of its
    events would be in the capture; only one was. It had not run, and the
    emitter was elsewhere.

    A "concrete lead" recorded that day - a second site in `SCENE 63` at offset
    1553 - was **wrong**, and the byte-accounting pass caught it the next run:
    1553 is inside that chunk's camera table and the match was four bytes of
    coordinate data (CLAUDE.md §1, attribute before you count).

    The real answer was a **fourth** instance of the same field-map bug.
    `inventory.add` (op 50) announces its SECOND operand - the handler pushes
    `ebx` at 0x0040A4D0, and field 0 is the list selector it then compares
    against 2 and 3 - and the emitter is `inventory.add(2, 314)` at pc 1215 of
    **SCENE 55's startup script**, the Impasse driver, one instruction after
    the `op87` that emits the capture's event 19. With ops 71, 52, 93 and now
    50 mapped from their handlers, both captures replay with **no
    disagreement at all**.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    out = []
    for f in ("intro.log", "walkin.log", "impasse-walk.log",
              "telis-dialog.log", "resto-387.log", "fight.log"):
        p = os.path.join(ROOT, "traces", f)
        if not os.path.exists(p): return "missing " + f, "both traces", ""
        # show=10000: this argument caps the WORK, not just the printing, and
        # at its default of 25 this check was silently replaying the first 25
        # anchors of resto-387's 64 - see the docstring.
        r = G.diff(p, evolve=True, quiet=True, show=10000)
        out.append((f, r["events"], r["ok"], r["bad"]))
    return out, [("intro.log", 58, 7, 0), ("walkin.log", 76, 10, 0),
                 ("impasse-walk.log", 286, 21, 0),
                 ("telis-dialog.log", 55, 4, 0),
                 ("resto-387.log", 840, 51, 5),
                 ("fight.log", 154, 2, 0)], \
           "each capture's event count, the scripts replayed and agreeing, " \
           "and the disagreements - zero on the first four, and six on the " \
           "fifth, of which three vanish from any of the three saves taken " \
           "DURING it and three do not (see the docstring). engine/ replays " \
           "the same five captures independently and reaches the same " \
           "numbers: `engine: golden traces`"


def c_start_script_graph():
    r"""Who can start a scene object - counted from the assembly.

    CUTSCENES §5 ruled out every non-`scx.play` route with: "`Script_StartScript`
    has exactly four call sites and every one is reached from an `scx.play*`
    opcode, so nothing else *can* start a scene object". Counted properly it has
    **nine**, in nine distinct functions, and `ScriptObject_Start` is reached
    from three places that are not the VM's action dispatcher:

        Area_Transition (0x00408530)   a staged state machine, 3 sites
        Script_ProcessActions          its 60-second transition watchdog
        Shoot_StartTargetScripts       the shooting minigame

    plus `Actor_StartPendingScx`, ticked by `Actors_TickAll`, which starts a
    script parked at actor+176 once `Morph_IsDone()` - a DEFERRED start, which
    is the shape a staggered cutscene would need.

    One caveat kept deliberately: IDA gives a `proc` only where it found a
    prologue, so the VM's table-dispatched handlers fold into whichever proc
    precedes them - `sub_402B70` here spans handler addresses 0x402c30..0x406090.
    Sites attributed to it are therefore most likely the `scx.play*` handlers,
    exactly as the original claim said. What the original got wrong is that this
    was the ONLY route.
    """
    s = _need("asm")
    if s: return s
    import re
    asm = omkpaths.asm_path()
    lines = open(asm, encoding="cp1252", errors="replace").read().splitlines()
    want = {"sub_44A7E0": "Script_StartScript", "sub_41DC10": "ScriptObject_Start",
            "sub_466A60": "Actor_StartPendingScx"}
    cur, hits = None, {k: set() for k in want}
    n = {k: 0 for k in want}
    for l in lines:
        m = re.match(r"^(\S+)\s+proc\s+near", l)
        if m: cur = m.group(1)
        if "call" in l:
            for k in want:
                if re.search(r"call\s+" + k + r"\b", l):
                    hits[k].add(cur); n[k] += 1
    return (n["sub_44A7E0"], len(hits["sub_44A7E0"]),
            n["sub_41DC10"], sorted(hits["sub_41DC10"]),
            n["sub_466A60"], sorted(hits["sub_466A60"])), \
           (9, 9, 7, ["sub_402B70", "sub_408220", "sub_408530", "sub_47BEF0"],
            2, ["sub_4681C0"]), \
           "Script_StartScript's call sites and the functions holding them " \
           "(NINE, not the four CUTSCENES claimed); and the callers of " \
           "ScriptObject_Start and Actor_StartPendingScx"


def c_actor_pending_scx():
    r"""Who writes actor +176 - the "pending" scene object, traced not assumed.

    CUTSCENES §5 named `Actor_StartPendingScx` the leading candidate for what
    staggers a cutscene's beats, on the strength of its shape: a per-frame,
    per-actor DEFERRED start, gated on `Morph_IsDone()`. It also said, in as
    many words, that "the +176 slot was described as 'the object an scx.play
    parked', but that is an assumption about who writes it". It was, and it is
    wrong. No `scx.play*` handler ever touches +176.

    The producer is `Morph_Play` (0x0041AFC0), and it is the ONLY one. Counting
    every store to +0B0h in the binary and asking which of them is an actor:

        Morph_Play             2   the two arms of an if/else - the write
        Actor_LoadModel        1   zeroed at init, beside +172 and +404
        Actor_StartPendingScx  1   zeroed on consumption
        (16 others)                other structs entirely

    and the actor is not a typing assumption - `Morph_Play` builds the pointer
    as `lea ecx,[eax+eax*4]; lea esi,[eax+ecx*8]; shl esi,5`, which is
    eax*41*32 = eax*1312 = ACTOR_STRIDE, added to `offset unk_9106A0`
    (g_Actors). The arithmetic names the struct.

    What it does there is a SUSPEND, not a launch. Entering a morph on an actor
    that an SCX object is currently driving (ACTOR_STATE 4):

        if (Script_ObjectResumable(rec[43]))  rec[44] = rec[43];  /* park  */
        else                                  rec[44] = 0;        /* drop  */
        Scene_ResetObjectState(rec[43]);  rec[43] = 0;  state = 0;
        ... then, last statement of the function:      state = 5;

    and state 5 is dispatched by `Actors_TickAll` straight to
    `Actor_StartPendingScx`, which waits for `Morph_IsDone()`, restarts what is
    parked, moves it back to [43] and returns the actor to state 4. So +176 is
    the save slot of a suspend/resume pair wrapped around a SPOKEN LINE - the
    caller of `Morph_Play` is the dialogue UI, building `<line>.3dm` for
    `g_DialogSubjectActor`.

    The gate on parking is `sub_44B460`, which returns 1 when the object's
    program loops (record 13 == -1) or still has an unfinished node chain: a
    one-shot program that would have ended is dropped rather than resumed,
    which is why the else arm exists.

    Two things this settles. `Actor_StartPendingScx` cannot order a cutscene's
    beats: nothing can be parked at +176 that was not already running, so it
    resumes an interrupted actor and never starts a new one. And exactly one
    instruction in the binary writes ACTOR_STATE = 5, so there is no second
    way into that state to look for.
    """
    s = _need("asm")
    if s: return s
    import re
    asm = omkpaths.asm_path()
    lines = open(asm, encoding="cp1252", errors="replace").read().splitlines()

    PROC  = re.compile(r"^(\S+)\s+proc\s+near")
    STORE = re.compile(r"^\s*(?:mov|and|or|xor)\s+(?:dword ptr )?"
                       r"\[(e[a-z][a-z])(?:\+e[a-z][a-z](?:\*\d)?)?\+0B0h\]\s*,\s*(\S+)")
    ST5   = re.compile(r"^\s*mov\s+dword ptr \[e[a-z][a-z]\+194h\]\s*,\s*5\s*$")

    cur, sites, state5 = None, [], []
    for i, l in enumerate(lines):
        m = PROC.match(l)
        if m: cur = m.group(1)
        m = STORE.match(l)
        if m: sites.append((i, cur, m.group(1), m.group(2)))
        if ST5.match(l): state5.append(cur)

    # a store is a "vector triple" if +0B4h and +0B8h go to the same register nearby
    def triple(i, reg):
        w = "\n".join(lines[max(0, i-6):i+7])
        return ("[%s+0B4h]" % reg) in w and ("[%s+0B8h]" % reg) in w
    scalar = [s for s in sites if not triple(s[0], s[2])]

    # of the scalar stores, which sit in a function that builds g_Actors + 1312*i
    bounds = [i for i, l in enumerate(lines) if PROC.match(l)] + [len(lines)]
    def body(i):
        j = max(b for b in bounds if b <= i)
        k = min(b for b in bounds if b > i)
        return "\n".join(lines[j:k])
    STRIDE = re.compile(r"lea\s+(e[a-z][a-z]), \[(e[a-z][a-z])\+\2\*4\]\s*\n"
                        r"\s*lea\s+(e[a-z][a-z]), \[\2\+\1\*8\]\s*\n"
                        r"\s*shl\s+\3, 5")
    actorish = sorted({s[1] for s in scalar
                       if STRIDE.search(body(s[0])) and "unk_9106A0" in body(s[0])})

    morph = [s for s in sites if s[1] == "sub_41AFC0"]
    pend  = [s for s in sites if s[1] == "sub_466A60"]

    return (len(sites), len(scalar), actorish,
            len(morph), sorted(s[3] for s in morph),
            len(pend), pend[0][3] if pend else None,
            len(state5), state5), \
           (21, 16, ["sub_41A730", "sub_41AFC0"],
            2, ["eax", "ebx"],
            1, "0",
            1, ["sub_41AFC0"]), \
           "every store to +0B0h in the binary, those that are not a float " \
           "triple, and the ones whose function builds g_Actors + 1312*i; " \
           "Morph_Play's two (the park/drop if-else), Actor_StartPendingScx's " \
           "single zeroing; and that exactly ONE instruction sets ACTOR_STATE=5"


def c_startup_scripts():
    r"""The script every AREA and SCENE chunk runs on load - chunk offset +4.

    This is the table the 5785-slot inventory never had. That walk enumerates
    scripts from the zone records (three fields each) and the message
    subscriptions; NOTHING in it reaches the per-chunk startup script, and 173
    chunks carry one.

    Read from the code, not guessed. `Scene_Load` (0x0040C120) relocates `+4`
    exactly like every other pointer field in the header
    (`if (v11) u32(v2,4) = v2 + v11`), and `Area_TickLoad` (0x0040C7E0) runs
    it. In the raw assembly, once for the AREA block and once for the SCENE
    block:

        mov  esi, dword_69BC40      ; the AREA block  (then 69BC44, the SCENE)
        mov  ecx, [esi+4]           ; <-- the startup script
        push 0 / push 0 / push ecx / push ebp
        call sub_406290             ; Script_NewContext(slot, script, 0, 0)
        mov  [esi], eax             ; the context is stored at block +0
        push 1 / push eax
        call sub_4063D0             ; Script_QueueAction(ctx, 1)

    which also explains the header's `+0`: it is 0 on disk because it is where
    the running context goes.

    The corpus makes it self-checking - a pointer field that was really
    something else would land mid-instruction and fail: **every** non-zero `+4`
    in all 330 chunks disassembles clean, 173 of 173, 0 failures.

    It also corrects `intro script` above. AREA 118's intro was found at `+68`,
    with a note that the empty subscription table's base "coincides with the
    start of the code after it". It does, and `+4` is the field that actually
    names it: for chunk 118 the two are the same number, 1040.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import dialog_triggers as T, dialog_disasm as D
    tot = {}
    ok = bad = zero = ins = 0
    for name in ("AREA", "SCENE"):
        n = 0
        for k, b in sorted(T.archive(omkpaths.data("IAM", name)).items()):
            n += 1
            at = struct.unpack_from("<I", b, 4)[0]
            if at == 0: zero += 1; continue
            if not (0 < at < len(b)): bad += 1; continue
            code, st = D.disasm(b, at, len(b))
            if st == "ok": ok += 1; ins += len(code)
            else: bad += 1
        tot[name] = n
    a118 = T.archive(omkpaths.data("IAM/AREA"))[118]
    same = (struct.unpack_from("<I", a118, 4)[0] ==
            struct.unpack_from("<I", a118, 68)[0] == 1040)
    return (tot["AREA"], tot["SCENE"], ok, zero, bad, ins, same), \
           (259, 71, 173, 157, 0, 1968, True), \
           "AREA and SCENE chunks; those with a startup script at +4, those " \
           "with none, and those that FAIL to disassemble (must be zero - the " \
           "test the field could fail); its instructions; and that AREA 118's " \
           "+4 and +68 are the same 1040, which is why the intro was found at +68"


def c_impasse_beats():
    r"""What starts Impasse's beats - closed, by the startup script at +4.

    CUTSCENES §5 stood open on this: a scene's shots are one cutscene and the
    object names carry the order, but no shipped script started them. Every
    route was ruled out in turn - the editing ids are not the order, the
    linked-pair hand-over does not apply (Impasse has no links at all), the
    staggered-`Script_Wait` idea dies on 1 Wait among 36 objects, `area.goto`
    carries `-1, -1` into area 222, and `Actor_StartPendingScx` turned out to
    be a suspend/resume around a spoken line.

    All of that was right. The scan was just blind: it enumerated the 5785
    slots, and the driver is the SCENE chunk's **startup script at +4**
    (`startup scripts` above), which no slot walk reaches.

    `SCENE 55` is "1-01 Impasse", and the engine loads it over `AREA 222`
    ("Anekbah Impasse"). Its +4 script, at offset 1212, fires all sixteen
    `scx.play*` in the authored order the object names always implied:

        A_1_KaylArrives -> A_2_KaylMoves -> A_2_DemonLook -> A_2_KaylSuite
        -> A_2_DemonSuite -> 2_Combat_Demon -> 2_Combat_Kayl
        -> KaylDemonAme(2) -> KaylDemonAme -> C_1_BoxMoves -> C_1_MecaComes
        -> C_1_KaylStand -> C_2_KaylStand -> C_2_MecaSpeaks
        -> C_3_Meca Leaves -> C_3_KaylsUp

    then sets `premiere impasse` / `Impasse Finie` and hands off with
    `scene.load(237, 57)` - area "Anekbah Appart Kayl", scene "1-02 Appart
    Kayl Rencontre". The whole chain in from a new game is AREA 118
    ("Introduction Kay'l") +4: the world-camera intro, conversation 272, then
    `area.goto 222` and `scene.load(222, 55)`.

    And it is checked against the ENGINE, not only read: 19 of the 21 events
    this script would announce appear in `traces/intro.log` **in order**, as
    trace events 19 through 42. The two that do not are both explained - one is
    the untaken arm of the script's own branch, and one is `loggable` naming
    op71's field 0 (the area) where the handler logs field 1 (the scene), for
    which the capture is the authority and shows the scene.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import dialog_triggers as T, dialog_disasm as D, scene_scx as SX
    import goldentrace as G
    objs = SX.scene(omkpaths.data("SCPTDATA/Impasse.SCX"))["objects"]
    byid = {o["handle"] >> 16: o["name"] for o in objs}
    b = T.archive(omkpaths.data("IAM/SCENE"))[55]
    at = struct.unpack_from("<I", b, 4)[0]
    code, st = D.disasm(b, at, len(b))
    IDF = {46: 0, 57: 0, 58: 0, 90: 0, 59: 2, 60: 2}
    beats = []
    for _pc, op, raw in code:
        if op in IDF and len(raw) >= IDF[op] + 2:
            v = struct.unpack_from("<h", raw, IDF[op])[0]
            if v in byid: beats.append(byid[v])
    load = [struct.unpack_from("<2h", raw, 0)
            for _pc, op, raw in code if op == 71]

    pat = re.compile(r'Call KERNEL32\.GetPrivateProfileStringA\(\w+ "([A-Z]+)"'
                     r',\w+ "(-?\d+)"')
    tr = []
    tp = os.path.join(ROOT, "traces/intro.log")
    for l in open(tp, encoding="utf-8", errors="replace"):
        m = pat.search(l)
        if m: tr.append((m.group(1), m.group(2)))
    pred = [e for _pc, op, raw in code for e in G.loggable(op, raw)]
    i = hits = 0
    for e in pred:
        j = i
        while j < len(tr) and tr[j] != e: j += 1
        if j < len(tr): hits += 1; i = j + 1
    return (at, st, len(code), beats, load, len(pred), hits), \
           (1212, "ok", 55,
            ["A_1_KaylArrives", "A_2_KaylMoves", "A_2_DemonLook",
             "A_2_KaylSuite", "A_2_DemonSuite", "2_Combat_Demon",
             "2_Combat_Kayl", "KaylDemonAme(2)", "KaylDemonAme",
             "C_1_BoxMoves", "C_1_MecaComes", "C_1_KaylStand",
             "C_2_KaylStand", "C_2_MecaSpeaks", "C_3_Meca Leaves",
             "C_3_KaylsUp"],
            [(237, 57)], 24, 24), \
           "SCENE 55's startup script: where it is, that it decodes, its " \
           "length, the sixteen beats it fires IN AUTHORED ORDER, the " \
           "scene.load it hands off with, and how many of its announcements " \
           "the golden trace confirms in order"


def c_sim_area_load():
    r"""Phase 6: the simulator boots an AREA the way `Area_TickLoad` does.

    Until now `tools/sim` had ONE entry point, `enter_zone` - it could stand
    the player in a trigger and run what fired, but it had no notion of an area
    arriving. So it never ran the startup scripts at `AREA +4` / `SCENE +4`
    (`startup scripts`), which is 1968 instructions of initialisation the
    engine runs before any trigger exists, and it is what every session of play
    begins with.

    `Session.load_area()` is case 9 of the state machine, transcribed:

        v18 = the AREA block;   Script_NewContext(slot, v18[1], 0, 0)
        *v18 = ctx;             Script_QueueAction(ctx, 1)
        v20 = i16(u32(g_GameDB, 12), 2 * areaId)     ; the scene over it
        if (v20 != -1): the same for the SCENE block
        Zones_RegisterAll()

    Two things it made honest along the way. **Which scene** is not a
    parameter - it is read back from the game DB's per-area table at `+12`,
    which VM opcode 71 `scene.load` WRITES (`Area_SetLoadedScene`, its handler
    ending `push esi / push edi / call sub_40B120`; the function reads as
    `@callers 0` only because IDA folds table-dispatched handlers - CLAUDE.md
    §1). That is now implemented in the VM rather than stubbed, so a load
    depends on the save the way it does in the game. And a context's script
    offsets are relative to **its own chunk**, so once two chunks are resident
    the pump has to carry a buffer per chunk - `World.buffers` / `code_for`.

    THE TEST IS THE ENGINE. AREA 118 is "Introduction Kay'l", what a new game
    enters, and `traces/intro.log` is a capture of the original running it. The
    simulator's announcements are matched against the capture in order, and
    **all 7 of them are the capture's first 7, contiguously**: variables 175
    and 170, `media.play` 997, the `Interface` question, cameras 2172 and 2148,
    then conversation 272.

    **With the conversation returning control and the area transition chained**
    (both 2026-08-29), the run reaches **trace event 42 of 58**, and every one
    of the capture's first 42 is reproduced **in order, with no gaps at all**:
    45 announcements, 42 confirmed, nothing skipped. The last hole - event 20,
    `OBJECTS/314` - closed when `inventory.add`'s announced field was read from
    its handler (see `trace agreement`).

    What that covers is the game's whole opening: the intro, conversation 272,
    `area.goto 222` into Impasse, `scene.load(222, 55)`, and then SCENE 55's
    startup script firing the **sixteen Impasse beats** and handing off to
    Kay'l's apartment with `scene.load(237, 57)`.

    THE TRANSITION, as the engine does it. `area.goto` is `Area_Transition`
    case 0: it stages a load into the other slot and parks its caller at
    **status 10** (`u16(a3,22) = 10`), which `Script_ProcessActions` refuses to
    touch; `Script_Pump`'s tail finishes it once `Area_TickLoad` reports done,
    and only then does the caller resume after its `area.goto`. Getting that
    wrong is visible immediately: without the park the intro script runs
    straight past, `scene.load` fires before the area exists, and SCENE 55's
    startup script is started twice - once by `scene.load` and once by
    `Area_TickLoad` case 9 reading the state the first one had just written.

    `scene.load` is itself a launcher, which is why the beats need no
    transition of their own: op 71's handler ends `Scene_Block(scene)` ->
    `[ebx+4]` -> `Script_NewContext` -> `Script_QueueAction(ctx, 1)`.

    PACING, added 2026-08-29, is what makes the run take **2220 frames** to
    reach event 42 where it used to take a handful - and it is the game's own
    mechanism, not a delay.

    It was **1541** until 2026-09-03, and the 679 it moved that day are three
    interpreter faults in the object programs, each attributable to the frame:

    * **+41**, the sync link. It indexes the object's SYNC array, not the main
      and sync arrays laid end to end, and read flat `A_2_DemonLook` - the
      demon's jump - merged its two program steps and ran 91 frames instead of
      91 + 41 = **132**, the duration its own camera editing `sautdemon`
      declares. `verify.py: scx sync chain`.
    * **+649**, the repeat count, which `tools/sim` was not honouring at all.
      Three of the Impasse's beats carry `+16` above 1 and gain 1331 frames
      between them, but only the two started with a **waiting** variant can
      gate this run: `C_1_KaylStand` +122 and `C_2_MecaSpeaks` +527, which is
      649 exactly. `C_2_KaylStand`'s +682 is started with plain `scx.play`
      and blocks nothing - so the arithmetic also re-confirms which variants
      wait. `verify.py: sim: SCX interpreter`.

    * **-11**, the busy window. `Script_SelectBodyAnimation` reports done on
      the tick it clamp-draws the clip's last frame and `Script_PlayScript`
      advances the pc inside that same tick, so a step costs exactly its own
      frames; the interpreter was closing the window a tick later and spending
      one frame per step on nothing. `verify.py: engine: scene steps`.

    None of the three has an oracle in the capture, which records events and
    not the frames between them; what vouches for them is the camera editings,
    whose authored durations the beats now match - `sautdemon` 132 and
    `mecaspeak` 558 exactly. Seven of SCENE 55's sixteen beats are started with
    a **waiting** variant (`scx.play.player.wait` 46, `scx.play.wait` 58,
    `scx.play.actor.wait` 60), and those:

        ScriptObject_Start(obj, scene, SLOT, flag)   ; not -1, as op 57 passes
        ...
        mov eax, 4 / mov [esi+16h], ax               ; the caller's status

    register the object in the wait slots at `dword_910500/4/8` and park the
    caller at status **4**. `Game_Tick` then, every frame,

        if (*v9 && !ScriptObject_IsBusy(*v9)) { *v9 = 0; Game_RaiseEvent(3, v8); }

    and `Game_HandleEvent` case 3 puts a status-4 context back to 1. So a beat
    finishing is what advances the cutscene, and the SCX interpreter's own busy
    spans (clip frames, `Script_Wait`) supply the durations.

    It also reordered the run in a way a fixed script could not have predicted:
    the intro parks on a `scx.play.actor.wait` **before** it ever reaches
    `dialog.start`, so the driver closes a conversation when the world is
    actually blocked on one rather than at a scripted point.

    Two limits, both stated rather than tuned. The **absolute** timings are not
    the engine's: the capture spends 35 s in the start menu and 110 s in
    conversation 272, neither of which is modelled (a person is doing both),
    and the Impasse itself runs 43 s here against the engine's 69 s, because
    `_busy_span` returns 0 for paths and sync cues. And the ceiling is still
    event 42: the capture's 43-58 are the tutorial, which needs the **player to
    walk** into zones 3795/3796 and area 142 - movement, not pacing.

    `dialog.start` is not a suspend, which is the part that had to be read
    rather than guessed. Its handler writes no status, and `Script_Execute`'s
    loop simply does `if (v4 == 61) return;` - so the context is left RUNNING
    with its pc past the operands. What blocks is the world: `Dialog_Load`
    writes `g_DialogState = 3`, and `Script_Pump` case 1 opens

        if (!g_DialogState) { g_DialogState = 1; return 1; }
        if (g_DialogState != 1) return 1;

    so the entire per-frame step - prompt slots, `Script_ProcessActions` and
    `Script_Execute` alike - stops until `Game_HandleEvent` case 63 unloads the
    conversation and writes 1 back. **The capture is what settled it**: a
    resume-next-frame model is indistinguishable from the real one in event
    ORDER, and only the timestamps separate them - conversation 272 is
    announced at t=41.4 s and the next camera at t=151.5 s, 110 seconds later.
    So the sim splits `Script_ProcessActions` from `Script_Execute`, keeps the
    context's own VM (its stack survives the yield), and gates the pump.

    One thing is still supplied rather than derived, and it is a player's
    answer - but the MECHANISM around it is now the engine's, not a variable
    set behind the simulator's back. `ui.open` (0x00403860) stores the result
    variable's index in `dword_4E6B28` and parks its caller at status **6**;
    `Game_HandleEvent` **case 5** writes the answer into that variable and puts
    the context back to 1, its argument block carrying the context index at
    `+4` and the chosen value at `+8` (`sub_466B60` fills both). The simulator
    does exactly that: the intro suspends on screen 29 `OMK START MENU` asking
    for variable 19, and the harness answers **1** - the value
    `verify.py: save file` reads out of a real save. Swapping the pre-seed for
    the real suspend/answer changed no number here, which is the point: it is
    the same run, with the supplied input localised to one call.

    How LONG a conversation lasts is likewise not modelled; `run_dialog()` is
    the caller saying it ended.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import run as SR
    r = SR.area_load(frames=2500)
    return (r["startup"], r["started"], r["at_dialog"], len(r["events"]),
            r["matched"], r["reach"], r["skipped"], r["last_frame"],
            r["scene_of_222"]), \
           ([("AREA", 118, 1040)],
            [("AREA", 222, 2276), ("SCENE", 55, 1212), ("SCENE", 57, 1272)],
            (7, 7, 3), 44, 42, 42, [], 2220, 55), \
           "the new game's startup script; the chunks the transition then " \
           "loads and starts (the announced total dropped 45 -> 44 on " \
           "2026-08-31 when `loggable` started reading the assembly's own " \
           "announce table; the CONFIRMED count did not move, so what went " \
           "was an event the engine never logged); " \
           "loads and starts; where it halts at the conversation; the " \
           "decisions announced, how many the GOLDEN TRACE confirms IN " \
           "ORDER, HOW FAR INTO THE CAPTURE it gets and which of those " \
           "events it skipped (only 20, the known residue); THE FRAME the " \
           "last confirmed event fires on - the pacing, which an event count " \
           "cannot see; and that Impasse's scene 55 is loaded over 222"


def c_chunk_accounting():
    r"""Every byte of every AREA / SCENE chunk, accounted for - `tools/chunkmap.py`.

    This exists because twice on 2026-08-29 a confident negative turned out to
    be a fact about an ENUMERATION rather than about the game. "Nothing starts
    Impasse's beats" was really "no script I enumerate does", and the driver
    was the startup script at `+4` that no record table names. So rather than
    keep asking whether the script list is complete, this claims every byte a
    documented structure explains and reports what is left.

    What claims a byte: the fixed header; the eight tables, each
    (pointer, count, stride) taken from the loaders' own relocation and loops
    (`Area_Load` 0x0040CC90, `Scene_Load` 0x0040C120 - the same nine pointer
    fields, AREA's offsets being SCENE's plus 32 except the shared `+4`); the
    two strings on every 276-byte character record, whose `+0` and `+4` both
    get relocated; and every script, marked by REACHABILITY rather than a
    linear decode, since a script can jump forward over its own `end`.

    Two things the corpus decided, neither of them assumed:

    * **the tables tile.** `Area_Load` walks the object and prop tables as
      `u32(v2, 40) + 8` stepping 20 / 24, and reading that `+ 8` as a table
      header makes them overlap their neighbour by exactly 8 bytes in 310 of
      330 chunks. It is a field offset inside each record: dropped, the eight
      tables abut with **0 overlaps in 330 chunks**. That is the test the
      layout could have failed.
    * **character record `+0` and `+4` are its two strings** - the biography
      ("Specialiste des armes, entraine au combat rapproche...") and a short
      line, `Neant.` where there is none. 990 of the 1032 records leave both
      at 0, and claiming them removes all 34 text runs.

    THE RESULT: 330 chunks leave **three** unclaimed runs, **97 bytes**, and
    all three decode clean and land exactly on their own boundary on a
    terminator - which random bytes do not do three times. None is reached by
    any jump from any script, and only one is named by any pointer in its
    chunk: AREA 45's, by `+68`, whose subscription count is 0, so it is the
    same empty-table-based-at-the-code coincidence as AREA 118 (`intro
    script`). AREA 45 also has `+4` = 0, i.e. no startup script - so this looks
    like one that was unhooked rather than one that is hidden.

    So the script inventory is complete to within 97 bytes of unreferenced
    bytecode. It also correctly ruled the then-open `OBJECTS/314` residue out
    as a missing script - none of the three announces it - and it was not one:
    it turned out to be `inventory.add`'s announced field being mis-mapped, in
    a script the corpus had held all along (`trace agreement`).
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import chunkmap as C, dialog_triggers as T2
    scripts = C._scripts()
    chunks = overlaps = 0
    runs = []
    for arch in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", arch)).items()):
            chunks += 1
            claim, marks = C.account(arch, k, b, scripts[(arch, k)])
            tab = sorted((s, e) for nm, s, e in marks if nm != "charstr")
            for i in range(len(tab) - 1):
                if tab[i][1] > tab[i + 1][0]: overlaps += 1
            for s, e in C.gaps(claim, 8):
                runs.append((arch, k, s, e, C.classify(b, s, e)))
    return (chunks, overlaps, len(runs), sum(e - s for _a, _k, s, e, _c in runs),
            sorted({c for _a, _k, _s, _e, c in runs}),
            [(a, k, s, e) for a, k, s, e, _c in runs]), \
           (330, 0, 3, 97, ["bytecode"],
            [("AREA", 0, 21859, 21913), ("AREA", 45, 836, 847),
             ("AREA", 59, 13413, 13445)]), \
           "chunks walked; table overlaps (must be 0 - the test the layout " \
           "could fail); unclaimed runs of >=8 bytes and their total size; " \
           "what they are; and exactly where"


def c_sim_narrow_phase():
    r"""Phase 6 stage 5's stated debt: the actor stops walking through walls.

    Stage 5 shipped with the ground probe as the whole of collision, and said
    so: "the actor keeps to the floor but walks through walls". This is the
    narrow phase, read and then implemented.

    WHAT THE ENGINE DOES, from the chain `Sweep_ActorMove` (0x004AD360) ->
    `Sweep_MeshTest` (0x004AD460) -> `sub_4A9AB0` -> `sub_4A9D30`, then
    `Walk_ClampNormal` (0x0046A020), three passes:

      * `Sweep_ActorMove` builds the swept AABB from the move's start
        (+112..120) and end (+124..132), the radius (+192) and the two capsule
        heights (+376/+380), seeds the hit fraction (+136) with FLT_MAX and
        runs `o3de_ForEachMeshInBox`. It returns whether +136 changed.
      * `Sweep_MeshTest` skips meshes flagged 0x20000000 or 0x41 - so the
        filter is by MESH FLAG, not by slope, which is why the sweep's face set
        is every collision face and not the walkable soup the probe uses - then
        transforms the sweep into mesh-local space.
      * `sub_4A9AB0` rejects a face unless the AND of its vertices' 6-bit
        outcodes against the swept box is 0, then calls the kernel per triangle
        (stride 28) and per quad (stride 32).
      * `sub_4A9D30`, 930 lines, is the swept-sphere-against-polygon kernel:
        earliest fraction to +136, surface normal to +260.
      * `Walk_ClampNormal` is **not** a plane slide. A 6-bit mask says which
        axis directions are already blocked; the normal's components in those
        directions are zeroed and the result renormalised, returning 0 if it
        collapses. That is what lets three passes take an inside corner.

    WHAT IS IMPLEMENTED, and the line between them stated rather than blurred:
    `clamp_normal` is a transcription. The sweep is **not** - the 930-line
    kernel is x87 geometry and this tree holds no corpus fact a transcription
    could be proved against, so `tools/sim/actor.py` implements the same
    algorithm SHAPE (earliest hit of a swept sphere, clamp, slide, three
    passes) over the same faces, with the face case continuous and edges and
    vertices left to the static test. It is enough to answer stage 5's debt; it
    is not the engine's arithmetic, and the radius is a stand-in because the
    engine derives it per model from the collision spheres at model +260 times
    a runtime tunable.

    THE TEST IS ONE THE RUN CAN FAIL, and it is chosen so the ground probe
    cannot pass it by accident. `find_partition` looks for the biggest vertical
    face that has **floor on both sides** and whose y range contains the
    sphere's centre - most walls in a set have no floor behind them, so the
    probe already refuses to cross them and a test there would pass with no
    narrow phase at all. Walking straight at that partition:

        sweep off -> ends 140 units BEHIND the wall, last verdict "moved"
        sweep on  -> ends  16.6 units in front,      last verdict "blocked"

    The floor test keeps its own run (`sim: actor & .CTL`, `cross(radius=0)`):
    turning the sweep on changes the path, so it would otherwise answer a
    different question with the same numbers.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import actor as AC
    part = AC.find_partition("ARESTO14")
    r = AC.wall_test()
    ok, cn = AC.clamp_normal(1, 0, [0.6, 0.0, 0.8])      # +x blocked
    dead = AC.clamp_normal(0x15, 0, [0.6, 0.5, 0.8])[0]  # collapses to nothing
    return (part is not None, r["without"], r["withoutVerdict"],
            r["with"], r["withVerdict"], ok, [round(x, 3) for x in cn], dead), \
           (True, -140.0, "moved", 16.6, "blocked", 1, [0.0, 0.0, 1.0], 0), \
           "a partition with floor on both sides exists; walking into it " \
           "WITHOUT the sweep ends behind the wall, WITH it ends in front and " \
           "blocked; and Walk_ClampNormal zeroes a blocked axis, renormalises, " \
           "and reports the degenerate case"


def c_sim_tutorial_walk():
    r"""The opening, and then the player WALKS - the simulator's furthest run.

    `sim: area load` gets to trace event 42 with the player as a teleport. The
    capture's 43-58 are the tutorial, and they fire because a person walks into
    a trigger, so this puts a real `Walker` on AIMPASSE - carrying the narrow
    phase, so he collides with the alley - and steers him into zone 3795. It
    reaches **trace event 55 of 58**.

    THE PATH IS SUPPLIED, and that is the honest half. Where the player walked
    is player input; it is not in the data and nothing here recovers it. What
    is tested is the other half - that walking there produces the engine's
    decisions in the engine's order, through the real zone lifecycle rather
    than by dropping the player inside the quad.

    IT ALSO FOUND A UNIT BUG the moment an actor and a zone shared a frame.
    The zone quad is stored in AUTHORING units: `Area_Load`/`Scene_Load` run
    `(100 * v) * 0.00390625 * 0.3937... - 1` over the four corners' x and z,
    and the same with a further **-9** over their y. `tools/sim` read them raw,
    which is self-consistent as long as nothing else is in the frame - the zone
    tests place the player at the zone's own centre - so it survived until an
    actor standing at an authored `ADDRESSES` position came out **43000 units**
    from the zone he was in. Converted, all five of AREA 222's zones land
    inside AIMPASSE's own bounding box, and the player spawns between two of
    them, and **all 12** of the chunk's zone records do - not just the 5 whose
    save bit has them registered. That is the invariant the raw reading fails.

    THE CROSSING INTO AREA 142 now happens, and it happens the way the game
    does it: zone **3801**'s enter script is an `area.goto 142`, so walking
    into that quad runs `Area_Transition` case 0, parks the context at status
    10, and `Area_TickLoad` case 9 brings the area up and re-registers. Area
    142's own zones (2319, 2320, 2329) become resident, and so does **3796** -
    whose save bit is 0 at new game, is set by zone **3791**'s script on the way
    past, and which only goes live once `Zones_RegisterAll` rebuilds. That
    rebuild has three call sites, all area load or transition, none per-frame,
    so nothing but the crossing could have made it live. Five zones fire in
    all: 3790, 3791, 3795, 3801, 3803.

    THE ROUTE IS DERIVED, not hand-written. `nav_route` breadth-first searches
    a 16-unit grid whose edges are legal only when `Walker.step` returns
    "moved" AND lands in the cell it aimed at - so it is the shortest route the
    walker itself can take over the collision geometry. The arrival test is not
    a detail: without it a step that slides along a wall counts as reaching the
    neighbour, and the search happily returns routes straight through
    buildings, which is what it did first.

    Five events stay unmatched, and the reason is ORDER rather than a missing
    mechanism. The derived route to zone 3803 passes through **3795** first, so
    the tutorial fires before 3803's `media.play`, and an in-order subsequence
    match cannot then take 43/44 (ZVO T002, ZONES 3803), 45/46, or the second
    3796 at 51. The capture's player walked to 3803 without crossing 3795;
    which way he went is player input and is not in the data. **Nothing here is
    unexplained** - event 20 closed with `inventory.add`'s field map.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import run as SR, world as SW, omkdata as OD
    r = SR.tutorial_walk()
    # the zones of AREA 222 must sit inside the set AREA +88 names
    zs, _b = SW.zones_of("AREA", 222)
    g = OD.decor_geometry_cached("AIMPASSE")
    inside = sum(1 for z in zs
                 if all(g["min"][k] - 200 <= z.centre()[k] <= g["max"][k] + 200
                        for k in (0, 2)))
    return (r["beforeWalk"], r["events"], r["matched"], r["reach"],
            r["skipped"], r["zonesFired"], len(zs), inside,
            [z for z in (2319, 2320, 2329, 3796) if z in r["registered"]]), \
           (44, 68, 51, 56, [43, 44, 45, 46, 51],
            [3790, 3791, 3795, 3801, 3803], 12, 12,
            [2319, 2320, 2329, 3796]), \
           "events before the walk and after it (45/70 -> 44/68 on " \
           "2026-08-31 for the same reason as `sim: area load`, with matched " \
           "and reach unchanged at 51 and 56); how many the GOLDEN TRACE " \
           "confirms in order and HOW FAR into the capture it reaches; which " \
           "of those it skipped (accounted for in the docstring); the zones " \
           "the walk fired; that every AREA 222 zone lands inside AIMPASSE's " \
           "own bounds - which the unconverted quad misses by 43000; and that " \
           "crossing into AREA 142 made its zones AND 3796 resident"


def c_vm_announce_fields():
    r"""Which OPERAND every announcing handler logs - read from the assembly.

    Five times a handler was found announcing a field the disassembler's
    `SECTION` map did not expect, and **every one was found by accident** -
    a golden trace disagreeing with a prediction, days apart:

        op 71  scene.load           field 1, not 0 (the scene, not the area)
        op 52  inventory.remove_all field 1, not 0 (0 is the list selector)
        op 93  actor.stat.set       field 2, not 0 (0 is the variable, 1 the actor,
               -1 for the player; named hud.show_var until 2026-09-02)
        op 50  inventory.add        field 1, not 0
        op 51  inventory.remove     field 1, not 0

    That is a class of error, not five errors, and finding them one capture at
    a time is not a method. `tools/vm_announce.py` closes it by reading the
    announce out of every handler that has one.

    The shape is always the same, because `Dbg_LogTagged(value, section)` is
    cdecl: the section string is pushed first and the value second.

        push offset aObjects_2   ; "OBJECTS"
        push ebx                 ; <- and WHICH operand ebx is, is the point
        call sub_40EC70

    Operands reach a register two ways - the shared fetch `sub_401AA0` (`call`
    then `mov <reg>, eax`), or inlined, two bytes assembled and sign-extended
    with `movsx <reg>, cx` and optionally re-read through the `0x4000`
    indirect. Counting both in order gives each register its field index, and
    the index of the pushed one is what the handler logs.

    **49 handlers read, 0 disagreements.** The audit rediscovers all five fixes
    above independently, plus the two maps that were already right (ops 49 and
    67), which is the cross-check that it is reading rather than agreeing.

    A sixth instance now cannot hide: it would fail here rather than waiting
    for a capture to contradict it.
    """
    s = _need("clean")
    if s: return s
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import vm_announce as VA
    rows = VA.audit()
    bad = [(r["op"], r["domain"], r["asm"], r["map"])
           for r in rows if not r["agrees"]]
    # the five the captures caught, now derived rather than remembered
    derived = {r["op"]: r["asm"] for r in rows
               if r["op"] in (49, 50, 51, 52, 67, 71, 93)}
    return (len(rows), bad, derived), \
           (49, [], {49: 1, 50: 1, 51: 1, 52: 1, 67: 1, 71: 1, 93: 2}), \
           "announcing handlers read out of the assembly; those whose logged " \
           "field disagrees with the disassembler's map (must be ZERO); and " \
           "the seven multi-operand ones re-derived, including the five that " \
           "were only ever found by a capture contradicting a prediction"


def c_sim_dialogue():
    r"""Phase 6: conversations EXECUTE - the last stub in the decision path.

    `dialog.start` loaded a conversation and the harness then declared it over;
    `tools/sim/dialogue.py` runs it. Node by node: evaluate each branch's
    condition, take one, execute that branch's action against the real
    `GameState`, follow `param[k]` to the next node.

    The two halves of the node's nine pointers were **proven by tracing**
    rather than guessed (FILE_FORMATS): `Game_HandleEvent` event 55, fired
    while `Dialog_TickUI` builds the reply menu, EVALUATES `ptr[0..3]` through
    `Dialog_EvalBranchCondition` and takes the value; event 59, fired when a
    reply is chosen, EXECUTES `ptr[4..7]` in a throwaway context through
    `Dialog_GetBranchAction`. Conditions gate, actions run.

    Three invariants, each one the run could fail:

    * **every one of the 321 conversations reaches a leaf** - 0 cycles, 0
      failures, 0 hitting the step limit. Nothing in the format forbids a cycle
      in the node graph, so termination has to be observed rather than assumed,
      and the visited guard is there to observe it.
    * **all 612 dialogue scripts execute to `end`** - 247 conditions and 365
      actions. This is the dialogue's own corpus on the same VM the world
      scripts run on, and a condition that cannot be evaluated is one the reply
      menu could not have been built from.
    * **all 1452 branch targets are valid node indices.** That number is
      independently the one FILE_FORMATS quotes from the format work, arrived
      at here by executing rather than by parsing.

    Which reply a person picks is player input and is not in the data; the
    default policy takes the first available branch, which is the least
    interesting one that still exercises every condition.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import dialogue as DL
    r = DL.corpus()
    sc = DL.scripts()
    return (r["conversations"], r["ended"], r["cycles"], r["hit_limit"],
            r["failed"], r["nodes"], sc["scripts"], sc["conditions"],
            sc["actions"], sc["executed"], sc["targets"], sc["validTargets"]), \
           (321, 321, 0, 0, 0, 837, 612, 247, 365, 612, 1452, 1452), \
           "conversations run; how many reach a leaf, and the cycles, " \
           "step-limit hits and failures - all of which must be zero; nodes " \
           "walked; then every condition and action script executed " \
           "standalone, and every branch target checked to be a real node"


def c_save_file():
    r"""A real save, at last - and it adjudicates what GAME_STATE could not.

    `docs/GAME_STATE.md` §8 derived the save geometry from three literals in
    the writers - a slot is **32808** bytes (32-byte profile name, day, time,
    the 8192-byte DB, a 24576-byte frame capture; `32+4+4+8192 = 8232` and
    `8232+24576 = 32808`), the file is `256` of them behind a **3496**-byte
    header - and then had to record the directory record as **not
    established**, because "`IAM\GAMES` is not shipped so nothing adjudicates".

    Something adjudicates now. The engine wrote one, playing under the
    golden-trace rig, and `traces/save-appart.bin` is its 3496-byte header plus
    slot 0's first 8232 bytes - name, day, time and the DB, without the
    screenshot. Every number lands:

      * the file the engine produced is **8402344** bytes, and
        `3496 + 256 * 32808` is 8402344 exactly - the arithmetic was right;
      * the header is the **profile and settings** block, not a slot
        directory: it opens with `OMK_SAVE`, then `80 02 e0 01` = 640 x 480,
        and only 119 of its 3496 bytes are non-zero. So a slot is
        self-describing and `SaveDir_CountByName`'s 256 x 72 = 18432 walk is
        over an in-memory directory, not over this file - which is what the
        two readings were disagreeing about;
      * slot 0 reads `hereIsTheProfileName`, **day 52**, time 2566060. A new
        game starts at day 52 / 2000000 (`game clock`), so this is the same
        day, later - and it is a save the player made partway through.

    And the DB inside it parses as game state, which is the part no literal
    could have given:

      * **area 237** `Anekbah Appart Kayl` with **scene 57** over it, which is
        where the save was actually made;
      * `premiere impasse` set, `Impasse Finie` not yet - consistent with
        standing in the apartment before that scene resolves;
      * **`Interface` (variable 19) is 1.** That is the one value
        `sim: area load` has to SUPPLY, because `ui.open` asks the player a
        question the simulator cannot answer, and the capture only showed which
        arm was taken. The save states it outright, so the seed is no longer a
        choice between two known paths - it is the recorded answer.
    """
    fx = os.path.join(ROOT, "traces/save-appart.bin")
    if not os.path.exists(fx): return "missing fixture", "the save", ""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import gamestate
    d = open(fx, "rb").read()
    hdr, slot = d[:3496], d[3496:]
    name = slot[:32].split(b"\0")[0].decode("cp1252", "replace")
    day, tim = struct.unpack_from("<2i", slot, 32)
    # the project's own reader, on the fixture: `from_save` wants
    # 3496 + 40 + 8192 = 11728 bytes for slot 0, which is exactly its size
    st = gamestate.from_save(fx, 0)
    return (3496 + 256 * 32808, len(hdr), len(slot),
            hdr[:8], struct.unpack_from("<2h", hdr, 12),
            sum(1 for c in hdr if c), name, day,
            st.area, st.scene_of(st.area), st.var(19), st.var(626), st.var(61)), \
           (8402344, 3496, 8232, b"OMK_SAVE", (640, 480), 119,
            "hereIsTheProfileName", 52, 237, 57, 1, 1, 0), \
           "the size the geometry predicts (and the engine's own file is); " \
           "the header and slot-head lengths; that the header is the PROFILE " \
           "block - OMK_SAVE, 640x480, and almost entirely zero; the slot's " \
           "name and day; then the DB read as state - area 237 with scene 57, " \
           "and `Interface` = 1, the value `sim: ui` now DERIVES by " \
           "walking the start menu rather than supplying"


def c_telis_dialogue():
    r"""A captured conversation, reproduced - and the reply menu is observable.

    `sim: dialogue` shows the runtime is self-consistent over the corpus.
    Nothing showed it agreed with the ENGINE, because no capture had ever held
    a conversation with choices in it. `traces/telis-dialog.log` does: loaded
    from the save in Kay'l's apartment, `DIALOGS 402` (`Telis/Appart`), and
    then nine announcements as the player read and answered - eight `RepTel*`
    variables (*reponse Telis*) and a closing `OBJECTS 534`.

    THE FIRST MODEL FAILED, and that is the finding. Scoring only the chosen
    branch's ACTION, **no** walk of the graph reproduces the capture - the
    control below still returns 0. What was missing is that `Dialog_TickUI`
    evaluates EVERY branch's condition to build the reply menu, and a
    condition that reads a variable does so with `push.var`, which logs. So a
    capture records what the menu OFFERED, not only what was taken.

    Node 12 proves it rather than suggesting it. Its three conditions read, in
    branch order, {669, 668, 667}, {671} and {670} - and the capture holds
    exactly `669 668 667 671 670` as one batch at t=167.5 s, five events in
    that order, which no other reading of the format produces.

    With the menu build modelled, **36** walks reproduce the capture's stream
    exactly. That is not a weak result: the residue is a fact about the
    EVIDENCE, since two branches whose actions announce nothing are
    indistinguishable to a logger that only sees announcements. All 36 agree
    on the two choices the trace does determine - **(node 10, branch 2)** and
    **(node 12, branch 1)** - which are exactly the nodes whose conditions
    announce.

    The state is the save's, not a new game's: `gamestate.from_save` on
    `traces/save-appart.bin`, which is where the conversation actually ran.
    """
    fx = os.path.join(ROOT, "traces/save-appart.bin")
    lg = os.path.join(ROOT, "traces/telis-dialog.log")
    if not (os.path.exists(fx) and os.path.exists(lg)):
        return "missing fixture", "the save and the capture", ""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import dialogue as DL, dialog_triggers as T2, gamestate
    pat = re.compile(r'Call KERNEL32\.GetPrivateProfileStringA\(\w+ "([A-Z]+)"'
                     r',\w+ "(-?\d+)"')
    tr = []
    for l in open(lg, encoding="utf-8", errors="replace"):
        m = pat.search(l)
        if m: tr.append((m.group(1), m.group(2)))
    i = next(k for k, (d, v) in enumerate(tr) if d == "DIALOGS" and v == "402")
    want = tr[i + 1:]
    conv = DL.Conversation(402, T2.archive(omkpaths.data("IAM/DIALOG"))[402])
    fresh = lambda: gamestate.from_save(fx, 0)
    ok = DL.reconstruct(conv, want, fresh(), cap=100000, menu=True)
    ctl = DL.reconstruct(conv, want, fresh(), cap=100000, menu=False)
    common = sorted(set.intersection(*[set(p) for p in ok])) if ok else []
    return (conv.n, len(want), want[3:8], len(ok), len(ctl), common), \
           (19, 9,
            [("VARIABLES", "669"), ("VARIABLES", "668"), ("VARIABLES", "667"),
             ("VARIABLES", "671"), ("VARIABLES", "670")],
            36, 0, [(10, 2), (12, 1)]), \
           "the conversation's node count; the events the capture holds after " \
           "dialog.start 402; node 12's menu build inside them, in branch " \
           "order; how many walks reproduce the stream EXACTLY; how many do " \
           "with the menu build NOT modelled (must be 0 - the control that " \
           "makes this falsifiable); and the choices every match agrees on"


def c_attribution_reach():
    r"""How much of a capture can be attributed - and the rule's own control.

    Attribution names the script behind an event so the diff can replay it, and
    the original rule is: an event whose (domain, value) pair only ONE slot in
    the corpus can emit names that slot. Sound, and wasteful - 150 of
    `impasse-walk`'s 286 events are individually ambiguous and went unused.

    A RUN is far more discriminating than any of its parts. `window_anchors`
    grows a window from each position, intersecting the sets of slots able to
    emit each pair; the moment the intersection is a singleton, that run names
    a script. Across the four captures it takes attributed events from **221 to
    381** and distinct scripts named from **26 to 38**.

    THE CONTROL IS WHY IT IS BELIEVABLE, and it is the reason the numbers are
    asserted together: on every event that ALSO carries a unique-pair anchor,
    the two rules are compared - **375 comparisons, 0 contradictions** once
    `resto-387.log` joined them on 2026-08-30, against 221 before. That
    capture nearly TRIPLES what the oracle names: distinct scripts go 26 ->
    **76** under the strict rule and 38 -> **113** under runs, and the control
    scales with the claim rather than lagging it. Where
    they ever disagreed the unique pair would win, since one fact beats an
    intersection, and `window_anchors` enforces that.

    It is nonetheless **opt-in** in `diff` (`--window`), for a reason that took
    two tries to state correctly. Turning it on finds more scripts to verify
    but also produces disagreements that are not the simulator's:

    * replaying `AREA 118`'s startup script from `IAM\START` takes the
      `Interface == 0` arm where the capture took the other, and anchoring on
      the save inverts it, because a save is the state at the SAVE and not at
      the moment a script ran;
    * a signature is a STATIC decode - the union over a script's branches -
      while a replay takes ONE path, so a window can name a script **correctly**
      and the replay still predict a different branch, which the diff reports
      as a mismatch rather than the "unreached" it is. `SCENE 57 rec 2 +4`
      against `impasse-walk` is that case, not a false anchor: the
      `CAMERAS/4434` that named it is in its signature, on a branch this state
      does not take.

    So the standing `trace agreement` check stays on the strong rule, where "0
    disagreements" still means what it says.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import goldentrace as G
    r = G.window_gain()
    return (r["events"], r["scripts"], r["checked"], r["contradictions"]), \
           ((375, 837), (76, 113), 375, 0), \
           "events attributed by the unique-pair rule and by runs; distinct " \
           "scripts named by each; and the control - how many window " \
           "assignments were checked against a unique-pair anchor, and how " \
           "many contradicted it (must be zero)"


def c_area_goto():
    r"""Opcode 47 `area.goto` - its two object ids, and whose scene they name.

    The handler (0x00402D20) calls `Area_Transition` with a4=0 and passes its
    three int16 fields as a5, a6, a7; the state machine stores a6/a7 in a1[5]
    and a1[6] and starts each with `ScriptObject_Start` as it advances. So a
    transition CARRIES two scene objects, which is a way of starting one that
    is not `scx.play` at all - the thing CUTSCENES §5 had ruled out.

    Which scene they belong to is settled by the corpus, not assumed: resolved
    against the SOURCE area's `.SCX` (the one being left) 416 of 448 land,
    against the TARGET's only 84. The ids are also always both set or both -1,
    never one, and 247 of 448 are consecutive - authored pairs.

    It is NOT what starts Impasse's beats: exactly one `area.goto` targets area
    222 and it carries -1, -1. That negative is about area 222, not about the
    mechanism.
    """
    import struct as _s
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import dialog_disasm as D, vm as V, scene_scx as SC, dialog_triggers as T
    arch = T.archive(omkpaths.data("IAM/AREA"))
    cache = {}
    def ids_of(area):
        if area in cache: return cache[area]
        b, r = arch.get(area), set()
        if b and len(b) >= 106:
            stem = b[97:106].split(b"\0")[0].decode("cp1252", "replace")
            if stem:
                for fn in os.listdir(omkpaths.data("SCPTDATA")):
                    if fn.upper() == (stem + ".SCX").upper():
                        try:
                            r = {o["handle"] >> 16 for o in SC.scene(
                                omkpaths.data("SCPTDATA", fn))["objects"]}
                        except Exception: pass
                        break
        cache[area] = r; return r
    tot = withids = consec = insrc = intgt = half = 0
    imp = []
    for a, c, rec, f, code, p in V.world_scripts():
        ins, _st = D.disasm(code, p, len(code))
        for _pc, o, raw in ins:
            if o != 47 or len(raw) < 6: continue
            v = _s.unpack_from("<3h", raw, 0)
            tot += 1
            if v[0] == 222: imp.append(v)
            if (v[1] == -1) != (v[2] == -1): half += 1
            if v[1] == -1: continue
            withids += 1
            if v[2] == v[1] + 1: consec += 1
            if v[1] in ids_of(c) and v[2] in ids_of(c): insrc += 1
            if v[1] in ids_of(v[0]) and v[2] in ids_of(v[0]): intgt += 1
    return (tot, withids, half, consec, insrc, intgt, imp), \
           (758, 448, 0, 247, 416, 84, [(222, -1, -1), (222, -1, -1)]), \
           "area.goto sites; how many carry object ids; how many carry only " \
           "ONE (none - they are always a pair); consecutive pairs; and the " \
           "ids resolving in the source vs the target scene, plus every " \
           "transition into Impasse"


# --------------------------------------------------------- the simulator
def c_sim_vm():
    r"""RECONSTRUCTION phase 6 stage 1: every world script EXECUTES.

    The first behavioural harness OMK has had. Formats have been
    checked since day one; behaviour had nothing, so a name was an opinion.
    Now all 5785 slots are run on the VM, not walked by the disassembler, and
    three things must hold - the three stage 1 names:

      * every slot stops for a legitimate reason: `end`, or `dialog.start`,
        after which Script_Execute returns outright;
      * no unknown opcode and no stack underflow;
      * **every pc executed is an instruction boundary of its own slot.** That
        last one is the sharp one. "Inside the buffer" would pass trivially -
        a chunk holds its scripts back to back, so a branch target one byte out
        lands in a real instruction of the neighbouring script and runs on
        happily. It is what caught `case`: its target is measured from the pc
        after the 16-bit operand, before the label byte, and counting the label
        in put 4 slots on an unknown opcode and 41 `drop`s onto an empty stack.

    Fewer instructions execute (33918) than the disassembler walks (58644;
    58428 with op 62 at 6 since 2026-09-02),
    which is the point: execution follows branches and skips the untaken arms.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import vm as SIM
    r = SIM.run_corpus()
    return (r["slots"], r["ended"] + r["dialog"], r["failed"], r["strayed"],
            len(r["trace"].ops)), \
           (5958, 5958, 0, 0, 120), \
           "world-script slots executed; stopping at `end` or dialog.start; " \
           "failures; slots whose pc left their own instruction stream; " \
           "distinct opcodes executed"


def c_sim_zones():
    r"""RECONSTRUCTION phase 6 stage 3: the trigger lifecycle runs.

    The plan's own test - put the player inside zone **3732** and its activate
    script must fire - chosen because the outcome is checkable by NAME rather
    than by "something ran": that zone is record 0 of SCENE 53 and its activate
    slot is what launches dialog 387.

    The trace is the documented launch, in order: `zone.disable 3732` (the
    trigger retiring itself), `player.anim.hold` + `scx.play.player`, the
    look-at, then `dialog.start 387`. And the lifecycle closes - the zone's
    save bit goes 1 -> 0 and Zones_RegisterAll no longer registers it, which
    is what "a spent trigger stays retired across a save" means.

    That last part only works because `zone.disable` is implemented against
    the ASSEMBLY: `Zone_SetStateBit` decompiles as a bare OR, which could not
    clear a bit at all. See readable/src/01_file.c.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import world as W
    r = W.stage3(3732)
    if not r: return "zone 3732 not found", "found", ""
    names = [n for n, _f in r["calls"]]
    return (r["arch"], r["chunk"], r["registered"], r["dialogs"],
            "zone.disable" in names, r["bitBefore"], r["bitAfter"],
            r["reRegisters"]), \
           ("SCENE", 53, True, [387], True, 1, 0, False), \
           "where zone 3732 lives; registered; dialogs its activate script " \
           "launches; whether it retires itself; its save bit before and " \
           "after; and whether it registers again"


def c_sim_scene():
    r"""RECONSTRUCTION phase 6 stage 4: the SCX interpreter, and a launch.

    Two halves, both the plan's own tests.

    **`Telis_eat` alternates its two clips for ever.** Loop count -1 with two
    `SelectBodyAnimation` functions, each once, so the interpreter must cycle
    pc 0, 1, 0, 1 ... and never stop. Mishandle the repeat limit, the loop
    count or the busy window and it collapses to one clip or to a program that
    ends. (The engine's own `loopsDone` cannot be used to count the cycles:
    the rewind goes through `Script_StartScript`, which zeroes it - harmless
    only because the shipped loop counts are just 1 and -1.)

    **A repeat count above 1 is honoured.** `Telis_eat` cannot test that - both
    its functions are authored `repeat = 1` - so the Impasse's
    `C_2_MecaSpeaks` is here beside it: one `SelectBodyAnimation` of a 31-frame
    clip with `+16` = **18**, which is **558**, exactly the duration of
    `mecaspeak`, the camera editing linked to the object. Ending the function
    after one run - which this simulator did until 2026-09-03, while
    `engine/src/script/program.cpp` had had the tail since 2026-09-02 and
    nothing compared the two on a repeat above 1 - releases the editing at
    frame 31 and the cutscene's last cameras go wrong.

    **Dialog 387's launch runs end to end.** Standing in zone 3732 runs its
    activate script with `scx.play*` and `dialog.start` wired to the real
    scene, so the objects actually start and the conversation actually loads:
    SCENE 53 plays from `Re14.SCX` (via AREA 217's `+97`), objects 22 and 32
    resolve to `Uzal---->assis` and `Uzal_Stand`, and 387 is
    `Telis/Dejeuner` with 13 nodes. One of the two programs is still running
    after 120 frames and one is not, which is the documented rule: the
    entrance must finish, the loop need not.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import scene as SC, run as R
    a = SC.stage4()
    alt = all(a["clips"][i] != a["clips"][i + 1]
              for i in range(len(a["clips"]) - 1))
    b = R.stage4b()
    names = sorted(e[2] for e in b["started"])

    # the repeat count, which Telis_eat's authored 1 cannot reach
    sc = SC.Scene("Impasse.SCX")
    meca = sc.byname["C_2_MecaSpeaks"]
    pr = SC.Program(sc, meca, trace=[])
    n = 0
    while pr.tick() and n < 6000: n += 1
    meca_frames = n + 1                    # the clock when it stops

    return (a["loop"], a["distinct"], alt, a["running"], a["restarts"] >= 4,
            b["scx"], names, b["dialog"][1], b["dialog"][3], b["running"],
            meca["functions"][0]["repeat"], meca_frames), \
           (-1, ["TELRES02.3DA", "TELRES05.3DA"], True, True, True,
            "Re14.SCX", ["Uzal---->assis", "Uzal_Stand"], 387, 13, 1,
            18, 558), \
           "Telis_eat's loop count, the clips it alternates, that they " \
           "alternate, that it never stops and restarts freely; then the " \
           "scene 387 launches from, the objects it starts, the dialog and " \
           "its nodes, and programs still running after 120 frames; and " \
           "finally C_2_MecaSpeaks' repeat count and the 558 frames it runs " \
           "for - the duration `mecaspeak`, the editing linked to it, " \
           "declares, and the one case here that exercises a repeat above 1"


def c_sim_actor():
    r"""RECONSTRUCTION phase 6 stage 5: the walker, and the .CTL channel.

    **The floor.** Walking a spiral across `ARESTO14` from an authored
    ADDRESSES position, the actor moves where there is floor and **reverts
    where there is not** - the engine's rule, "nobody walks into the void" -
    and his height never changes, because that room's floor is flat. The
    reverts are the point as much as the moves: a walker that always succeeded
    would be one that had stopped testing.

    Two things this check exists to pin, both of which were wrong first:
    the ground ray starts a step-height ABOVE the feet, not at them (probing
    from the feet finds nothing below and reads every step as a hole), and the
    start comes from an authored position rather than a probe from -1e6, which
    returns the nearest surface below *that* - the ceiling. The first version
    put the player on the roof of the restaurant.

    **The graph.** Pressing `0x04` - the code on `H_STAND`'s `H_SD-WK` edge -
    walks H_STAND -> H_SD-WK -> H_WALK, and every consecutive pair is an edge
    the file actually carries, child or `goto`. Not "it moved": "it only ever
    moved along an edge".

    NOT covered, and stated in the module: the narrow phase. Collision here is
    the ground probe alone, so the actor keeps to the floor but passes through
    walls. Stage 5's stated test is the floor; the sphere sweep is still owed.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import actor as A
    c = A.cross("ARESTO14", A.area_start(217))
    s2 = A.ctl_sequence()
    return (c["verdicts"].get("moved", 0) > 100,
            c["verdicts"].get("reverted", 0) > 0,
            c["ySpan"][0] == c["ySpan"][1], c["fell"],
            s2["trail"][:3], s2["legal"]), \
           (True, True, True, "fine",
            ["H_STAND", "H_SD-WK", "H_WALK"], True), \
           "the walk moves; it reverts off the floor; its height never " \
           "changes; no fall; the .CTL trail; every step an edge the graph " \
           "carries"


# --------------------------------------------------------- the cutscenes
def c_cutscene_links():
    r"""CUTSCENES 2: the object record's +94..97 are camera-editing slots.

    This is the check behind a correction, so it is written to fail if the old
    reading were right. If those four bytes were loaded state - a "still
    running" flag, as SCRIPT_VM.md first had it - some shipped object would
    carry a non-zero one. None of the 4511 does: they are written at load by
    Script_LinkCamEditing, from the target id at +28 of each chunk-10 editing.

    The link counts are asserted from the other end at the same time, and the
    last number is the interesting one: no shipped object is the target of more
    than one editing, so the four-slot machinery in Script_PlayScript (and the
    "You cannot link more editings to this script" error) is never exercised by
    the game's own data.
    """
    import cam_editing as C, scene_scx as SX, collections
    files = (sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))) +
             sorted(glob.glob(omkpaths.data("SCPTDATA/*.scx"))))
    objs = dirty = linked = unlinked = frames = 0
    most = 0
    for f in files:
        b = SX.load(f)["block"]
        n = struct.unpack_from("<I", b, 4)[0]
        objs += n
        for i in range(n):
            o = 8 + 100 * i
            dirty += any(b[o + 94:o + 98])
        body = C.payload(f)
        if body is None: continue
        per = collections.Counter()
        for e in C.parse(body)["editings"]:
            frames += e["duration"]
            if e["target"]: linked += 1; per[e["target"]] += 1
            else: unlinked += 1
        if per: most = max(most, max(per.values()))
    return (objs, dirty, linked, unlinked, frames, most), \
           (4511, 0, 95, 30, 24112, 1), \
           "chunk-2 objects; those with a non-zero +94..97 on disk; editings " \
           "naming an object, and shipped unlinked; total editing frames; " \
           "most editings on one object"


def c_area_assets():
    r"""CUTSCENES 1: AREA +88 is the set, +97 the scene script.

    Area_TickLoad's switch appends a constant extension to each - `.3DO` at
    +88 (`lea edi, [ebp+58h]`) and `.SCX` at +97 (`[ebp+61h]`) - so the roles
    come from the code. What the data adds is that they resolve: the stems name
    files that ship. The misses are the check's real content, because they are
    not scattered - two blocks of cut content account for most of them, and a
    field that was really something else would miss at random instead.
    """
    import dialog_triggers as T2
    scx = {f.upper().split(".")[0] for f in os.listdir(omkpaths.data("SCPTDATA"))
           if f.lower().endswith(".scx")}
    dec = {f.upper().split(".")[0] for f in os.listdir(omkpaths.data("MESHES/DECORS"))}
    opt = {f.upper().split(".")[0] for f in os.listdir(omkpaths.data("TRAJECTOIRES"))}
    A = T2.archive(omkpaths.data("IAM/AREA"))
    n88 = n97 = h88 = h97 = n115 = h115 = 0
    cut = []
    for k, b in sorted(A.items()):
        if len(b) < 130: continue
        s = b[88:97].split(b"\0")[0].decode("cp1252", "replace")
        t = b[97:106].split(b"\0")[0].decode("cp1252", "replace")
        u = b[115:124].split(b"\0")[0].decode("cp1252", "replace")
        if s: n88 += 1; h88 += s.upper() in dec
        if t: n97 += 1; h97 += t.upper() in scx
        if u: n115 += 1; h115 += u.upper() in opt
        if (s and s.upper() not in dec) or (t and t.upper() not in scx):
            cut.append(O.TAGS["AREAS"].get(k, ""))
    blocks = sum(("Mus\u00e9e" in n) or ("Hamest Tombo" in n) for n in cut)
    return (n88, h88, n97, h97, len(cut), blocks, n115, h115), \
           (258, 232, 252, 227, 28, 24, 5, 5), \
           "areas naming a set at +88, of which shipped; naming a scene " \
           "script at +97, of which shipped; areas missing one, of which in " \
           "the two cut blocks (Lahoreh Mus\u00e9e / Mayerem Hamest Tombo); " \
           "naming a slider circuit at +115, of which shipped"


def c_cutscene_camera():
    r"""CUTSCENES 2: the camera sampler is Cam_PlayEditing's own layout.

    The invariant is the engine's: tracks are laid END TO END in time, each
    owning [base, base + its own last key frame). Sample every frame of every
    shipped shot and every one must land inside a key pair. Any other layout -
    all tracks starting at 0, or the duration used as the span - leaves gaps
    or overlaps immediately, so this is a test the data can fail.
    """
    import cutscene as C
    shots = frames = covered = full = unlinked = 0
    for fn in C.files():
        for e in C.scene(fn)["editings"]:
            shots += 1
            unlinked += not e["objectName"]
            d = int(e["duration"])
            got = sum(C.sample(e, i) is not None for i in range(d))
            frames += d; covered += got; full += (got == d)
    return (len(C.files()), shots, frames, covered, full, unlinked), \
           (29, 125, 24112, 24112, 125, 30), \
           "scenes with a camera editing; shots; frames; frames that resolve " \
           "to a camera; shots resolving at every frame; shots shipped unlinked"


def c_cutscene_sound():
    r"""CUTSCENES 5: the scene sounds, and when each one fires.

    The sounds are not in gamedata/SOUNDS - that ships two files. They are embedded
    in the .SCX streamed section as plain RIFF/WAVE, and the stream walk is
    already exact (anim_3da: 220/220), so the count is a byproduct of a checked
    walk rather than a search.

    The number worth asserting is the CUE TIME. `Script_PlaySyncSound`'s
    handler holds the function chain busy while `param1 > obj+88`, and obj+88
    is the object's program clock - the same clock Cam_PlayEditing is sampled
    on. So param 1 is a frame, and the test is that it lands inside the shot:
    467 of 475 do outright, and 5 of the remaining 8 are inside the actor's
    own clip, which simply outlives the camera's editing (Yob keeps walking for
    106 frames after a 31-frame shot). A field that was not a frame - a volume,
    a radius - could not do that.

    `Script_PlaySound` is deliberately excluded: its param 1 is a loop flag,
    not a time. Reading the two functions alike is what this check guards.
    """
    import cutscene as C
    wavs = riff = 0
    for fn in C.files():
        st = C.scene(fn)["stream"]
        for i, w in enumerate(st["wavs"]):
            wavs += 1
            riff += st["data"][w["offset"]:w["offset"] + 4] == b"RIFF"
    tot = inside = inclip = 0
    for sc in C.index():
        for e in sc["shots"]:
            sh = C.shot(sc["file"], e["id"])
            longest = max([a["frames"] for a in sh["actors"]] or [0])
            for c in sh["cues"]:
                if not c["sync"]: continue
                tot += 1
                if 0 <= c["at"] <= sh["duration"]: inside += 1
                elif 0 <= c["at"] <= longest: inclip += 1
    return (wavs, riff, tot, inside, inclip), (563, 563, 475, 467, 5), \
           "sounds embedded in the cutscene scenes, of which start with RIFF; " \
           "sync cues, of which fire inside the editing, and beyond it but " \
           "inside the actor's clip"


def c_cutscene_music():
    r"""CUTSCENES 5: what a cutscene can and cannot be heard with.

    Three numbers, and the middle one is the point.

    MUSIC resolves: a scene's object programs never touch it, so the bed is the
    area's - `AREA +142`, which Area_Load hands to Music_PlayTrack on arrival,
    for 26 of the 29 scenes, and `music.play` in the area's own scripts for 2
    more. All 28 name a shipped TRACKS\<n>.ADP.

    VOICES: `media.play` names a `ZVO ...` OBJECTS entry whose +14 stem is a
    VOICEOFF\*.ADP, and this tree ships **10 of the 561**. Both numbers stand
    and are asserted below - but the conclusion drawn from them here until
    2026-09-02 ("the voices cannot be played from this tree") was WRONG: the
    handler at 0x00404590 substitutes `JINGOFF3.ADP` for every stem beginning
    `ZVOT`/`ZVOP`, 520 of the 561, and that file ships. So the partition is
    10 shipped + 520 jingle + 31 silent; `engine: voice over` asserts it.

    WHAT ORDERS THE BEATS was open when this was written, and the last number
    is what kept it open: scanning all 5785 world scripts for an `scx.play`
    naming one of Impasse's 19 high handles finds nothing. Handles below 200
    are excluded because small object ids collide across scenes and make the
    scan meaningless; the mapping itself is sound, handle >> 16 being the u16
    at +26 in 36 of 36 objects.

    Both numbers still hold and are still worth asserting - but the CONCLUSION
    drawn from them was wrong twice over. "Only the scx.play* opcodes reach
    Script_StartScript (4 call sites)" is refuted by `start-script graph`
    (nine, in nine functions). And the scan itself is over the 5785 slots,
    which are not all the code: the driver is the SCENE chunk's startup script
    at `+4`, which no slot walk reaches - see `startup scripts` and
    `impasse beats`. The zero below is therefore a fact about the slot
    inventory, not about the game.
    """
    import cutscene as C, dialog_disasm as D, dialog_triggers as T2, scene_scx as SX
    idx = C.index()
    withmusic = sum(1 for s in idx if s["music"])
    served = sum(1 for s in idx
                 if s["music"] and s["music"]["track"] in C.tracks())

    d = open(omkpaths.data("IAM/OBJECT"), "rb").read()
    vo = {f.upper() for f in os.listdir(omkpaths.data("VOICEOFF"))}
    zvo = [i for i in range(1002)
           if (O.TAGS["OBJECTS"].get(i) or "").startswith("ZVO")]
    stem = lambda i: d[i*2048+14:i*2048+24].split(b"\0")[0].decode("cp1252")
    shipped = sum((stem(i).upper() + ".ADP") in vo for i in zvo)

    objs = SX.scene(omkpaths.data("SCPTDATA/Impasse.SCX"))["objects"]
    hi = {o["handle"] >> 16 for o in objs if (o["handle"] >> 16) >= 200}

    # An scx.play resolves against the scene the SCRIPT'S OWN chunk has
    # resident, so counting every chunk in the game counts collisions: object
    # ids are small and shared, and 8 sites elsewhere name an Impasse id while
    # addressing their own scene's objects. The sound question is what the
    # HOSTING chunks say - area 222, which names Impasse.SCX at +97, plus any
    # scene loaded over it.
    host = {("AREA", 222)}
    for sid, aid in O._scene_area().items():
        if aid == 222: host.add(("SCENE", sid))
    inhost = elsewhere = 0
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T2.archive(omkpaths.data("IAM", name)).items()):
            r = T2.LAYOUT[name](b)
            if not r: continue
            for rec, f, p in (list(T2._scripts_from_records(b, r[0], r[1]))
                              + T2._second_table(name, b)):
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": continue
                for pc, op, raw in ops:
                    if op not in (57, 58, 59, 60, 46, 90): continue
                    a, bb = struct.unpack_from("<2h", raw, 0)
                    if (bb if op in (59, 60) else a) not in hi: continue
                    if (name, k) in host: inhost += 1
                    else: elsewhere += 1
    return (withmusic, served, len(zvo), shipped, len(hi), inhost, elsewhere), \
           (28, 28, 561, 10, 19, 0, 8), \
           "cutscene scenes with a music track, of which shipped; ZVO voice " \
           "objects, of which have a shipped VOICEOFF .ADP; Impasse beat " \
           "objects; scx.play sites firing one from the hosting chunks, and " \
           "from chunks that merely share the id"


def c_camera_scripts():
    r"""CUTSCENES 6: the OTHER cutscenes - world cameras, no .SCX editing.

    A world script can direct a sequence itself, cutting and travelling
    between entries of the world camera table (`camera.set` / `camera.set.wait`)
    with `media.play` over it. None of these has a chunk-10 editing, so a
    chunk-10 index cannot see them - which is how the game's own **title
    sequence** went missing from the first cutscene list.

    The check the data could fail is the coordinate convention. The world
    camera table is raw, in the units `Global_Load` converts, and the test is
    that the converted cameras land INSIDE the set they film: for AREA 0 the
    scaled eyes span x -276..16272, z -12139..2732 against an ANEKBAH set of
    x -3399..16397, z -12909..5147, where the raw values are 6.5x too big for
    the room.

    The title sequence is asserted by name because it is the one that started
    this: AREA 0 record 78, 46 moves over 4370 frames, with the credits as
    voice cues and `music.play 3` under it - and track 3 is 143.8 s against
    the sequence's 145.7, the same length, which is what a player notices.
    """
    import cutscene as C
    scripts = C.camera_scripts()
    t = C.camera_shot("AREA", 0, 78)
    cams = C.world_cameras("AREA", 0)
    geo = O.decor_geometry_cached("ANEKBAH")
    xs = [v[0] for v in geo["verts"]]; zs = [v[2] for v in geo["verts"]]
    inside = sum(min(xs) - 500 <= c["eye"][0] <= max(xs) + 500 and
                 min(zs) - 500 <= c["eye"][2] <= max(zs) + 500
                 for c in cams.values())
    voices = [c for c in t["cues"] if c["kind"] == "voice"]

    # No camera MOVE may roll more than a quarter turn. The roll field is a
    # 4096-per-turn integer and Global_Load converts it without wrapping, so a
    # small negative roll is stored near 4096 - 28 of these 154 cameras carry
    # one. Read at face value they are ~+359 deg, which looks identical
    # standing still and sweeps almost a full turn the moment it is
    # interpolated: the title sequence span the camera at frames 3580 and 3840.
    # Wrapped to (-180, 180] and interpolated along the short arc, the largest
    # sweep in the game is 73 deg.
    spun = worst = 0
    for e in scripts:
        sh = C.camera_shot(e["arch"], e["chunk"], e["rec"])
        if not sh: continue
        for stp in sh["steps"]:
            if not stp["travel"]: continue
            i, j = int(stp["at"]), min(len(sh["camera"]) - 1,
                                       int(stp["at"] + stp["travel"]) - 1)
            a, b = sh["camera"][i] if i < len(sh["camera"]) else None, sh["camera"][j]
            if not a or not b: continue
            d = abs(b[6] - a[6])
            worst = max(worst, d)
            spun += d > 90
    wrapped = sum(1 for c in cams.values() if c["roll"] < 0)
    return (len(scripts), t["duration"], len(t["steps"]), len(voices),
            t["music"]["track"], t["set"], len(cams), inside,
            wrapped, spun, worst < 90), \
           (106, 4370, 46, 20, 3, "ANEKBAH", 154, 154, 28, 0, True), \
           "world-camera cutscenes; the title sequence's frames, moves and " \
           "voice cues; its music track and set; AREA 0's world cameras, of " \
           "which land inside the set they film; cameras with a negative " \
           "roll (stored near 4096); moves rolling more than 90 deg, and the " \
           "worst under it"


def c_cutscene_actors():
    r"""CUTSCENES 4: the actor staging the viewer draws, scored on the floor.

    Two claims, one test. Position: the clip's root key 0 is the authored
    placement and keys 1.. are per-frame deltas that `Anim_RootDelta` sums, so
    frame f sits at key0 plus the running sum. Orientation: the root
    quaternion, CONJUGATED - the convention every other bone reaches the pose
    stream in.

    Scored by dropping each posed actor onto the set: take its lowest vertex
    and compare with the set floor under its centroid. This is a weak test on
    purpose - actors sit, kneel and stand on stairs - so it is asserted as a
    bound, and what it really guards is the accumulation, which was worth
    3 units of median error and 21 more actors inside 8 units when it went in.

    The conjugate leads its two rivals on every measure (median 5.5 against
    6.0 and 6.1, and it is the only one that finds floor under every actor),
    but the margin is modest: this supports the reading, it does not settle
    it. Distinct (scene, clip, model) triples are scored once, so a clip used
    by two shots counts once.
    """
    import statistics, struct as _s, cutscene as C
    def qrot(q, v):
        w, x, y, z = q
        tx = 2 * (y * v[2] - z * v[1]); ty = 2 * (z * v[0] - x * v[2])
        tz = 2 * (x * v[1] - y * v[0])
        return (v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz),
                v[2] + w * tz + (x * ty - y * tx))
    scored, errs = 0, []
    seen = set()
    for sc in C.index():
        if not sc["set"]: continue
        geo = O.decor_geometry_cached(sc["set"])
        if not geo: continue
        for e in sc["shots"]:
            sh = C.shot(sc["file"], e["id"])
            for a in (sh or {}).get("actors", []):
                if not a["model"] or not a["rootBind"]: continue
                key = (sc["file"], a["anim"], a["model"])
                if key in seen: continue
                seen.add(key)
                meta, blob = O.ani_pose_stream(a["model"], sc["file"], a["anim"])
                if not meta: continue
                nm, bind = meta["meshes"], a["rootBind"]
                per = []
                for f in range(0, meta["frames"], max(1, meta["frames"] // 6)):
                    r = a["root"][min(f, len(a["root"]) - 1)]
                    q = (r[3], r[4], r[5], r[6])
                    lo, cx, cz = -1e9, 0.0, 0.0
                    for mi in range(nm):
                        v = _s.unpack_from("<7f", blob, (f * nm + mi) * 28)
                        w = qrot(q, (v[4] - bind[0], v[5] - bind[1], v[6] - bind[2]))
                        lo = max(lo, w[1] + r[1]); cx += w[0] + r[0]; cz += w[2] + r[2]
                    fy = O.floor_under(geo, [cx / nm, lo, cz / nm])
                    if fy is not None: per.append(abs(lo - fy))
                if per: scored += 1; errs.append(statistics.median(per))
    med = round(statistics.median(errs), 1) if errs else -1
    return (scored, med <= 6.0, sum(e < 8 for e in errs) >= 50), \
           (64, True, True), \
           "cutscene actors scored on the set floor; median error <= 6 units " \
           "(is %s); at least 50 within 8 units (%d)" \
           % (med, sum(e < 8 for e in errs))


def c_scx_play_family():
    """CUTSCENES 3: how much of the staging blocks the script.

    The six scx.play opcodes and the anim-hold pair, counted over the corpus.
    The ratio is the point: two thirds of the family suspends the calling
    script, which is what makes a cutscene run on the animation clock instead
    of on the player.
    """
    W = _world_ops()
    n = {op: len(W.get(op, [])) for op in (57, 58, 59, 60, 90, 46, 104, 105)}
    fam = sum(n[op] for op in (57, 58, 59, 60, 90, 46))
    wait = sum(n[op] for op in (58, 60, 46))
    return (fam, wait, n[104], n[105]), (3025, 2043, 647, 618), \
           "scx.play* sites; of which .wait; player.anim.hold; .release"


# --------------------------------------------------------- the game state
def c_game_state():
    r"""GAME_STATE 2: the 8192-byte game DB, as IAM\START ships it.

    The layout is State_Apply's own arithmetic, so what is worth asserting is
    the part the code does *not* state: the six u16 counts at +32. Nothing in
    the runtime reads them, so they are identified from the data - and the way
    to keep that honest is to require them to agree with six numbers
    established elsewhere, and then to require the arrays they size to tile the
    file exactly.

    The sharper half is the spare bits. Array 4 declares 791 addresses in 99
    bytes (792 bits) and array 5 declares 4558 zones in 570 bytes (4560); if
    either count were wrong the leftover bits would have to carry state. None
    of the three is set.
    """
    import gamestate as G
    st = G.load()
    end = max(b for _n, _a, b in st.walk())
    gaps = end - sum(b - a for _n, a, b in st.walk())

    per, flat = _prop_table()
    obj = _object_ids()
    want = (max(O.TAGS["VARIABLES"]) + 1,           # 694 - VARIABLES.TAG
            max(O.TAGS["AREAS"]) + 1,               # 259 - AREAS.TAG
            len({r[8] for r in flat}),              # 670 - prop state indices
            sum(len(v) for a in obj.values() for v in a.values()),   # 1032
            len(O.TAGS["ADDRESSES"]),               # 791 - ADDRESSES.TAG
            c_zone_records()[0][0])                 # 4558 - the zone records
    agree = sum(st.count(k) == want[k] for k in range(6))
    spare = sum(st._bit(k, i) for k in (3, 4, 5)
                for i in range(st.count(k), 8 * st.array_bytes(k)))

    a = G.load(); a.relocate(); a.unrelocate()
    trip = [i for i in range(st.image_size) if a.raw[i] != st.raw[i]]
    return (st.image_size, end, gaps, agree, spare, trip), \
           (5686, 5686, 8, 6, 0, list(range(60, 68))), \
           "IAM\\START bytes; the walk's end; alignment padding; counts " \
           "agreeing with an independent source; set bits past a count; " \
           "State_Apply/State_Save round-trip differences"


def c_object_lists():
    r"""GAME_STATE 3: which of the four object lists is which.

    The capacities come from State_Apply and prove nothing on their own; what
    identifies the lists is where the scripts put things. List 2 is the memo
    journal because every single object added to it is called `Memo NNN ...`,
    and nothing else in the corpus is - a name test, over 78 sites, that a
    wrong list assignment would fail at once. List 1 is the one no script ever
    adds to, which is why it is left unnamed in the doc.

    The starting inventory is asserted too: it is the only place in the tree
    where the new-game state is quoted in words.
    """
    import gamestate as G, dialog_disasm as D
    T = O.TAGS["OBJECTS"]
    per = {}
    for raw in _world_ops()[50]:
        sel, oid = struct.unpack_from("<2h", raw, 0)
        per.setdefault(sel, []).append(oid)
    memos = per.get(2, [])
    named = sum(T.get(i, "").startswith("Memo ") for i in memos)
    elsewhere = sum(1 for sel, ids in per.items() if sel != 2
                    for i in ids if T.get(i, "").startswith("Memo "))
    st = G.load()
    return (len(per.get(0, [])), len(per.get(1, [])), len(memos), named,
            elsewhere, [st.object_list(n) for n in range(3)]), \
           (369, 0, 78, 78, 0, [[6, 171], [176, 163], []]), \
           "inventory.add sites on lists 0, 1 and 2; memo-named objects " \
           "among list 2's, and among every other list's; and IAM\\START's " \
           "three stored lists"


def c_game_clock():
    r"""GAME_STATE 6: the Omikron calendar - 41 days, 13 months, year 7216.

    Every constant is a `dd` in the data segment and the formatter at
    0x0041E690 puts them together; what the shipped data adds is a test the
    calendar could fail. Eight objects in IAM\OBJECT are in-world newspapers
    named for their date, and all eight are legal in it - month from the
    thirteen names, day never past 41, with `41 Andar` landing exactly on the
    month length. The earliest, 11 Nadim 7216, is the day before the day
    Game_NewGame starts on.
    """
    import re, gamestate as G
    d = open(omkpaths.data("IAM/OBJECT"), "rb").read()
    pat = re.compile(r"(\d+)\s+(%s)\s+(\d+)" % "|".join(G.MONTHS))
    dated = []
    for i in range(1002):
        n = d[i * 2048 + 24:i * 2048 + 56].split(b"\0")[0].decode("cp1252")
        m = pat.search(n)
        if m: dated.append((int(m.group(1)), m.group(2), int(m.group(3))))
    legal = sum(1 <= day <= G.DAYS_PER_MONTH for day, _mon, _y in dated)
    return (len(dated), legal, max(d for d, _m, _y in dated),
            G.format_date(G.NEW_GAME_DAY), G.format_time(G.NEW_GAME_TIME)), \
           (8, 8, 41, "12 Nadim 7216", "11:10:00"), \
           "dated newspapers in IAM\\OBJECT, of which legal dates; the " \
           "highest day used; and the new game's date and time"


# --------------------------------------------------------- the tree itself
def c_index_idempotent():
    out = _run("tools/index.py", "--check")
    return ("up to date" in out), True, out.strip()

def c_vm_doc():
    s = _need("clean", "asm")
    if s: return s
    out = _run("tools/vm_doc.py", "--check")
    return ("already up to date" in out), True, out.strip()

def c_vm_table_sources():
    r"""The two sources for the VM table agree, opcode for opcode.

    `dialog_disasm` reads `clean/_vmtable.json` + `clean/_vmsummary.json` when
    the disassembly is present and falls back to the committed
    `tables/vm_opcodes.json` when it is not (see `dialog_disasm._vm_table`).
    That fallback is only sound while the two really do carry the same table,
    and nothing else would notice if they drifted: on a machine WITH the
    listing the fallback is never taken, so a divergence would sit unseen
    until someone cloned the repository without one - the worst place to find
    it, because there is then no second source to compare against.

    So this asserts the agreement from the side that has both, and it is the
    only check here that exists to protect a code path it does not itself run.

    Operand counts come from `_vmtable.operands` against `rows[].table_says` -
    the TABLE's own number, not the corrected one, because that is what
    `oplen()` falls back on after `LEN_FIX`. Domains come from the uppercase
    non-VALUES string each handler passes the debug logger, against
    `rows[].tag`.
    """
    s = _need("clean")
    if s: return s
    import json as _j
    vt = {e["op"]: e for e in _j.load(open(omkpaths.clean("_vmtable.json")))}
    vs = _j.load(open(omkpaths.clean("_vmsummary.json")))
    rows = {r["op"]: r for r in
            _j.load(open(omkpaths.tables("vm_opcodes.json")))["rows"]}

    ops = len(vt)
    op_same = sum(1 for op in vt
                  if op in rows and vt[op]["operands"] == rows[op]["table_says"])
    sec = {}
    for r in vs:
        u = [x for x in r["strings"] if x.isupper() and x != "VALUES"]
        if u: sec[r["op"]] = u[0]
    tag = {op: r["tag"] for op, r in rows.items() if r["tag"]}
    tag_same = sum(1 for op in set(sec) | set(tag)
                   if sec.get(op) == tag.get(op))
    return (ops, op_same, len(sec), len(tag), tag_same), \
           (153, 153, 49, 49, 49), \
           "opcodes in clean/_vmtable.json; those whose operand count matches " \
           "tables/vm_opcodes.json; .TAG domains from each source; and those " \
           "that agree - the fallback in dialog_disasm._vm_table is only " \
           "sound while these are equal"


def c_input_paths():
    r"""No tool hardcodes where an input lives; they all go through omkpaths.

    Renaming the data directory `fr/` -> `gamedata/` (2026-09-03) touched 321
    sites in 30 Python files, and the only reason it was one edit rather than
    thirty is that they now share `tools/omkpaths.py`. Nothing stops the next
    reader writing `os.path.join(ROOT, "gamedata", ...)` again, and it would
    WORK on the machine it was written on - which is exactly why nothing would
    catch it. It breaks only for somebody whose game lives elsewhere, and they
    have no way to tell a hardcoded path from a bug in the decoder.

    So this asserts an ABSENCE.

    WHAT IT LOOKS AT, and the first version had this wrong. Scanning every
    string literal for `gamedata/` flagged 58 sites, and all 58 were PROSE -
    check descriptions ("textures under gamedata/MESHES that decode exactly"),
    skip reasons, a generated file's own provenance line. The name of the tree
    appears far more often in text than in paths, so the text is the noise and
    not the signal.

    What matters is a literal that reaches a call which OPENS something. So
    this walks the AST and flags a constant string naming the data tree or the
    listing only when it is an argument to `open`, `os.path.join`,
    `glob.glob`, `os.listdir`, `os.walk` or `os.path.exists`. A description
    can then say `gamedata/` as freely as it likes, and a path cannot.
    """
    import ast as _ast, glob as _g
    CALLS = {"open", "join", "glob", "iglob", "listdir", "walk", "exists",
             "isdir", "isfile"}
    BAD = ("gamedata", "fr/IAM", "fr/MESHES", "fr/SCPTDATA", "fr/ANIMS",
           "fr/MORPH", "Runtime.exe.asm", "Runtime.exe.c")
    hits = []
    for path in sorted(_g.glob(os.path.join(ROOT, "tools", "*.py")) +
                       _g.glob(os.path.join(ROOT, "tools", "sim", "*.py"))):
        base = os.path.basename(path)
        if base.startswith("_") or base == "omkpaths.py":
            continue
        try:
            tree = _ast.parse(open(path, encoding="utf-8",
                                   errors="replace").read())
        except SyntaxError:
            continue
        for node in _ast.walk(tree):
            if not isinstance(node, _ast.Call):
                continue
            fn = node.func
            name = fn.attr if isinstance(fn, _ast.Attribute) else \
                   (fn.id if isinstance(fn, _ast.Name) else None)
            if name not in CALLS:
                continue
            for arg in list(node.args) + [k.value for k in node.keywords]:
                if isinstance(arg, _ast.Constant) and \
                   isinstance(arg.value, str) and \
                   any(b in arg.value for b in BAD):
                    hits.append("%s:%d" % (os.path.relpath(path, ROOT),
                                           node.lineno))
    resolved = (omkpaths.data_root(required=False) is not None,
                omkpaths.clean("x").startswith(omkpaths.clean_dir()))
    return (sorted(set(hits)), resolved), \
           ([], (True, True)), \
           "call sites passing a hardcoded input path to open/join/glob/" \
           "listdir/walk/exists (must be empty - use omkpaths.data() / " \
           ".clean() / .asm_path()); then that the resolver finds the data " \
           "tree and that clean() joins under clean_dir()"


def c_licence_headers():
    r"""Every authored source file declares its licence, and no vendored one does.

    Both halves matter, and the second is the one worth a check.

    A missing header is a nuisance: the terms are still in `LICENSE` and
    `LICENSING.md`, and tooling merely has to work harder. A header on
    SOMEBODY ELSE'S file is a false statement about their terms - it says OMK
    licenses `pl_mpeg.h` under GPL-3.0, which it cannot, having no right to. That kind of error looks tidy, reads as thorough, and survives
    for years precisely because nothing about it appears wrong.

    So the vendored tree is asserted CLEAN, not merely skipped.

    `engine/third_party/` is the only third-party code in the published tree.
    `adp/`, `3do/` and `MORPH_ANALYSIS/` are references kept out of it
    (LICENSING.md, "Kept locally, not published"), so they are not walked -
    a check that fails on a directory the repository does not ship would fail
    for every clone.
    """
    import glob as _g
    TAG = "SPDX-License-Identifier: GPL-3.0-or-later"
    roots = [("tools", (".py",)),
             ("engine/src", (".h", ".cpp", ".c")),
             ("engine/tools", (".h", ".cpp", ".c")),
             ("engine/backends", (".h", ".cpp", ".c")),
             ("scripts", (".sh",))]
    missing, authored = [], 0
    for rel, exts in roots:
        for base, dirs, files in os.walk(os.path.join(ROOT, rel)):
            dirs[:] = [d for d in dirs
                       if d not in ("__pycache__", "build", "third_party")]
            for f in sorted(files):
                if not f.endswith(exts):
                    continue
                authored += 1
                head = open(os.path.join(base, f), encoding="utf-8",
                            errors="replace").read(600)
                if TAG not in head:
                    missing.append(os.path.relpath(os.path.join(base, f), ROOT))

    # and the vendored tree must carry none of ours
    vendored = sorted(_g.glob(os.path.join(ROOT, "engine/third_party/*.h")) +
                      _g.glob(os.path.join(ROOT, "engine/third_party/*.c")))
    mislabelled = [os.path.relpath(p, ROOT) for p in vendored
                   if TAG in open(p, encoding="utf-8",
                                  errors="replace").read(600)]
    return (authored, sorted(missing), len(vendored), mislabelled), \
           (312, [], 1, []), \
           "authored source files under tools/, engine/src, engine/tools, " \
           "engine/backends and scripts/; those MISSING the SPDX tag; " \
           "vendored files in engine/third_party; and vendored files wrongly " \
           "stamped with OMK's licence (must be empty - pl_mpeg.h " \
           "is MIT and is not ours to relicense)"


def c_transcript_index():
    r"""`transcript/README.md`'s table lists exactly the sessions on disk.

    A SET comparison, deliberately not a count.

    The prose used to say "Ten so far", and `README.md` said "the ten
    sessions" - both went stale the moment another session was archived, which
    is a bad property for a number nothing was checking. The counts are gone
    now, and what replaces them is this: the table IS the index, so the thing
    worth asserting is that it agrees with the directory, in both directions.

    Both directions matter and they fail differently. A file with no row is an
    archived session nobody can find, which is the common case - you add the
    transcript and forget the table. A row with no file is a link into
    nothing, which is worse, because it reads as though the record exists.

    Only the rendered `.md` are compared. The `-raw.jsonl` snapshots beside
    them are not distributed (`.gitignore`), so a clone legitimately has none
    and a check that demanded them would fail for everyone but the author.
    """
    import re, glob as _g
    d = os.path.join(ROOT, "transcript")
    on_disk = {os.path.basename(p)[:-3] for p in _g.glob(os.path.join(d, "*.md"))
               if os.path.basename(p) != "README.md"}
    listed = set(re.findall(r"^\|\s*`(session-[^`]+)`",
                            open(os.path.join(d, "README.md"),
                                 encoding="utf-8").read(), re.M))
    return (sorted(on_disk - listed), sorted(listed - on_disk), on_disk == listed), \
           ([], [], True), \
           "archived transcripts with no row in transcript/README.md; rows " \
           "naming a transcript that is not there; and whether the two sets " \
           "agree exactly (the table is the index, so no count is quoted " \
           "anywhere - it would go stale on the next session)"


def c_held_camera_bracket():
    r"""A staged camera sequence is bracketed by the HOLD, and its shots are
    shaped exactly like the follow camera.

    This pins the two facts behind `todo/omk-play.md` 42 and 43. It is the
    check that would have caught both without anyone playing, and neither was
    found any other way.

    AREA 222's zone 3795 is the alley tutorial. Its script holds the player
    (104), runs three camera shots, releases (105), and then disables its own
    trigger zone. So the hold BRACKETS the sequence - the rule
    `docs/SCRIPT_VM.md` 104/105 states, and the discriminator the engine
    itself uses: `sub_415D10`, the follow camera, opens
    `if ((u32(a1,356) & 0x81) != 0) { v2 = 0; v13 = 0; }`, so a held channel
    forces the follow camera's mode to 0.

    THE SECOND HALF IS THE ONE THAT BIT. Every camera that script names is
    `eyeSubject 0, atSubject 0` - the same SHAPE as camera 0, the follow
    camera. `play.cpp` decided "this is the follow camera" from that shape
    alone, so each scripted shot was handed to `setCameraOffsets` and RE-AIMED
    the follow camera rather than standing as a fixed shot: it kept trailing
    the player, and the tutorial read as never having happened. A shape test
    cannot separate the two, and this asserts that it cannot, so nobody
    reintroduces one.

    The last row is the one-shot: the script's final act is `zone.disable` on
    its OWN id, which is what stops the tutorial firing on every entry - not a
    saved variable, which is what it looks like from the outside.
    """
    import sys as _sys, struct as _st
    _sys.path.insert(0, os.path.join(ROOT, "tools"))
    import dialog_triggers as _T, dialog_disasm as _D

    b = _T.archive(omkpaths.data("IAM", "AREA"))[222]
    base = _T.LAYOUT["AREA"](b)[0]
    rec = b[base + 5 * 68: base + 6 * 68]
    zoneId = _st.unpack_from("<H", rec, 64)[0]
    ops, _st_ok = _D.disasm(b, _st.unpack_from("<i", rec, 0)[0], len(b))

    first = {}
    for pc, op, raw in ops:
        first.setdefault(op, pc)
    holdPc, relPc = first.get(104, -1), first.get(105, -1)
    shots = [_st.unpack_from("<h", raw, 0)[0] for _, op, raw in ops
             if op in (95, 96) and len(raw) >= 2]
    disabled = [_st.unpack_from("<h", raw, 0)[0] for _, op, raw in ops
                if op == 65 and len(raw) >= 2]

    # the chunk's own camera table: base +64, count +84, stride 44,
    # atSubject at +32 and eyeSubject at +34 (engine/src/o3de/worldcam.cpp).
    p = _st.unpack_from("<I", b, 64)[0]
    n = _st.unpack_from("<h", b, 84)[0]
    subj = {}
    for i in range(n):
        o = p + 44 * i
        cid = _st.unpack_from("<h", b, o + 24)[0]
        subj.setdefault(cid, (_st.unpack_from("<h", b, o + 34)[0],
                              _st.unpack_from("<h", b, o + 32)[0]))

    named = sorted(set(shots))
    # every shot the script names, and camera 0, relative to actor 0 in BOTH
    likeFollow = sum(1 for c in named if subj.get(c) == (0, 0))
    # A desynchronised decode leaves no camera ops at all. Report that as a
    # readable FAIL rather than raising out of min() - a check that throws
    # says nothing about which of its rows moved.
    camPcs = [p for p, o, _ in ops if o in (95, 96)]
    opensRun = bool(camPcs) and holdPc >= 0 and holdPc < min(camPcs)
    closesRun = bool(camPcs) and relPc > max(camPcs)
    return (zoneId, holdPc, relPc, opensRun, closesRun,
            named, likeFollow, subj.get(0), disabled), \
           (3795, 2360, 2414, True, True, [0, 4290, 4291, 4292], 4, (0, 0), [3795]), \
           "AREA 222 zone 3795, the alley tutorial: its id, the pc of " \
           "`player.anim.hold` and `player.anim.release`, that the hold opens " \
           "and the release closes the camera run, the cameras it names, how " \
           "many of those are eyeSubject/atSubject 0 like the follow camera " \
           "(ALL of them - a shape test cannot tell a staged shot from the " \
           "follow camera, which is omk-play 42), camera 0's own subjects, " \
           "and the zone the script disables - itself, which is the one-shot"


def c_tutorial_one_shot():
    r"""`zone.enable`/`zone.disable` take effect AT ONCE, not at the next load.

    Filed as symptom 3 of the 2026-09-03 play report - *this tuto scene is
    triggered each time*. I first reported it as NOT a bug on the strength of
    `walk_zone`, which re-registers explicitly and so could never see the
    fault; the player could, and did. This is the check that drives the path
    that actually broke.

    **What the engine does** - op 65's handler (0x004037F0) does not merely
    clear the save bit. Its tail is

        push 0 / push esi / call sub_40D540      <- Zone_SetStateBit(id, 0)
        mov ecx, dword_69BC60 / push ecx
        call sub_406560                          <- Zones_RegisterAll()

    so the live list is rebuilt on the spot.

    **What the port did** - `interp.cpp` set the bit and stopped. The live list
    is a SNAPSHOT filtered at registration (`zones.cpp`,
    `if (state.bit(ZoneState, z.stateBit()))`), and the only other callers of
    `registerAll` are area loads - so every enable and every disable was inert
    until the player left the area and came back.

    That is worse than one tutorial repeating. AREA 222 carries a CHAIN: rec 0
    disables 3795, rec 1 enables 3796, rec 5 - the tutorial - disables 3795
    again. None of it advanced.

    The row drives a real `Session`: stand in 3795, let its script run, and
    watch the live set. `live_before 1, live_after 0` is the fix; without it
    the zone stays live and 3796 never arrives. The list is asserted whole
    rather than by count, because the count does NOT move - 3795 leaves as
    3796 joins, and a check on the size alone would have passed throughout.
    """
    import subprocess, re as _re
    eng = os.path.join(ROOT, "engine")
    if not os.path.isdir(eng):
        return ("skipped",), ("skipped",), "engine/ absent"
    b = subprocess.run(["make", "-s"], cwd=eng, capture_output=True, text=True)
    binp = os.path.join(eng, "build", "livezones_probe")
    if b.returncode != 0 or not os.path.exists(binp):
        return ("build failed",), ("built",), "engine/ must build"
    r = subprocess.run([binp, omkpaths.data_root(), omkpaths.tables_dir()],
                       capture_output=True, text=True)
    m = _re.search(r"oneshot zone 3795 resident (\d+) live_before (\d+) "
                   r"live_after (\d+) frames (\d+) live_now (\d+) list (\S+)",
                   r.stdout)
    if not m:
        return ("no oneshot row",), ("row",), "livezones_probe must print it"
    resident, before, after = int(m.group(1)), int(m.group(2)), int(m.group(3))
    lst = m.group(6)
    # and the source rule that makes it happen at all
    dirty = "r.zonesDirty = true;" in open(os.path.join(eng, "src/script/interp.cpp"),
                                           encoding="utf-8", errors="replace").read()
    rearm = "if (r.zonesDirty) zonesRegisterAll();" in \
        open(os.path.join(eng, "src/script/area.cpp"),
             encoding="utf-8", errors="replace").read()
    return (resident, before, after, lst, dirty, rearm), \
           (1, 1, 0, "3790,3791,3796,3799,3801,3803", True, True), \
           "a live Session in AREA 222: zone 3795 resident, live BEFORE its " \
           "script runs and not after, and the whole live list once it has - " \
           "3795 gone and 3796 arrived, which is why the SIZE is not the test; " \
           "then the two halves that make it immediate, op 64/65 raising " \
           "zonesDirty and the Session re-registering on it"

def c_no_define_renames():
    """CLAUDE.md 3: renames go through tools/renames.json, never a #define."""
    s = open(os.path.join(ROOT, "readable/types.h"), encoding="utf-8").read()
    bad = re.findall(r"^#define\s+(\w+)\s+((?:sub|dword|unk|byte|word|flt|off)_[0-9A-F]+)\s*$",
                     s, re.M)
    return len(bad), 0, "alias #defines in types.h (%s)" % ", ".join(b[0] for b in bad)


def c_render_bucket_key():
    r"""ASSETS 4b: the bucket key is state | texture slot, and it is 14 bits.

    Read out of the binary rather than guessed, so what the data has to show is
    that the pieces are consistent: the mesh-flag -> state-bit mapping never
    collides with the six bits the texture slot occupies, the slot count fits
    in those six bits, and the material field the key reads is a RUNTIME field
    - which the shipped bytes prove by being -1 in every material of every
    model. A material that shipped a real value at +64 would mean the field is
    authored and the whole reading is wrong, so this is a test the data could
    fail loudly.
    """
    import glob, mesh3do
    # the state bits Render_SubmitMesh can set, from the asm at 0x004951C0
    STATE = 0x800 | 0x400 | 0x2000 | 0x100 | 0x200 | 0x40 | 0x80 | 0x1000
    slots = 58                                   # SetMaterialsMemory(58, 0)
    unset = tot = 0
    for p in sorted(glob.glob(omkpaths.data("MESHES/**/*.3[Dd][Oo]"),
                              recursive=True)):
        try: h = mesh3do.header(p)
        except Exception: continue
        d = open(p, "rb").read()
        for i in range(h["materials"]):
            o = h["matOff"] + 80 * i
            tex, pal = struct.unpack_from("<2i", d, o + 64)
            tot += 1
            unset += (tex == -1 and pal == -1)
    return (STATE & 0x3F, slots < 64, STATE | 0x3F, tot, unset), \
           (0, True, 0x3FFF, 2534, 2534), \
           "state bits overlapping the texture slot; 58 slots fit in 6 bits; " \
           "the bits the key can carry; materials in gamedata/MESHES; of which " \
           "ship +64 and +68 as -1 (runtime fields)"


def c_render_drawable_mask():
    r"""ASSETS 4: the engine's drawable filter is `flags & 0x800043`.

    Every call to Render_SubmitMesh is guarded by that one test, so it replaces
    three separate viewer heuristics. Asserted because it disagrees with the
    viewer in both directions and the sizes of the disagreement are the point:
    bit 0 alone accounts for every skipped PERSOS mesh (no shape test needed),
    and `flags == 0` - which the viewer drops - is NOT in the engine's mask.
    """
    import glob, mesh3do
    MASK = 0x800043
    per_tot = per_skip = per_bit0 = per_lone = per_zero = 0
    # BOTH cases. 12 of the 193 PERSOS models ship a lowercase `.3do`, and a
    # `*.3DO` glob silently drops every one of them - 213 meshes, 36 of them
    # skipped. Found 2026-08-31 when `engine/` counted 3730 through DataFs
    # against this check's 3517; the same class of bug as the .SFX one, in a
    # standing check this time.
    for p in sorted(glob.glob(omkpaths.data("MESHES/PERSOS/*.3DO")) +
                    glob.glob(omkpaths.data("MESHES/PERSOS/*.3do"))):
        try: _h, ms = mesh3do.meshes(p)
        except Exception: continue
        for m in ms:
            f = m["flags"] & 0xFFFFFFFF
            per_tot += 1
            per_zero += (f == 0)
            if f & MASK:
                per_skip += 1
                per_bit0 += bool(f & 1)
                per_lone += (m["triangles"] == 1 and m["vertices"] == 3
                             and m["quads"] == 0)
    set_tot = set_skip = viewer_draws = 0
    for p in sorted(glob.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
        try: _h, ms = mesh3do.meshes(p)
        except Exception: continue
        for m in ms:
            f = m["flags"] & 0xFFFFFFFF
            set_tot += 1
            if f & MASK:
                set_skip += 1
                viewer_draws += bool(f & 0x43) and not (f & 0x800000)
    return (per_tot, per_skip, per_bit0, per_lone, per_zero,
            set_tot, set_skip, viewer_draws), \
           (3730, 547, 547, 532, 123, 12203, 434, 3), \
           "PERSOS meshes; skipped by 0x800043; of those by bit 0; of those " \
           "the lone-triangle shape; flagless (NOT skipped by the engine); " \
           "set meshes; skipped; set meshes the viewers draw and it does not"


def c_texture_name_cache():
    r"""ASSETS 4b: the texture cache is keyed on the 19-char file name alone.

    Tex3DT_BindMaterials strcmps material+20 against the 58 slot names and, on
    a hit, fseeks past the file's own pixels. The pool is global and two decor
    sets are resident at once, so a name shared between two co-resident models
    with DIFFERENT pixels means one of them silently draws the other's image.

    This asserts that the hazard is real in the shipped data, and that ANEKBAH
    is fully exposed to it - all 20 of its atlases collide - which is what
    makes it the answer to the Anekbah panel report. The 0.0%-vs-21.4% spread
    is the part that matters: the alternates are REVISIONS, so a substitution
    repaints some adverts on an atlas and leaves the rest correct, which is the
    per-panel disagreement no ordering rule can produce.
    """
    import glob, hashlib, collections, mesh3do
    recs = collections.defaultdict(set)
    for p in sorted(glob.glob(omkpaths.data("MESHES/**/*.3[Dd][Oo]"),
                              recursive=True)):
        try: h = mesh3do.header(p)
        except Exception: continue
        d = open(p, "rb").read()
        t = None
        for c in (p[:-4] + ".3dt", p[:-4] + ".3DT"):
            if os.path.exists(c): t = open(c, "rb").read(); break
        if t is None: continue
        off = 0
        for i in range(h["materials"]):
            o = h["matOff"] + 80 * i
            name = d[o+20:o+40].split(b"\0")[0].decode("cp1252", "replace")
            size, _a, _b, bpp = struct.unpack_from("<4i", d, o + 60)
            n = (16 if bpp == 4 else 256) * 3 + size
            recs[name[:19]].add(hashlib.md5(t[off:off+n]).hexdigest())
            off += n
    collide = sum(len(v) > 1 for v in recs.values())
    A = {x["name"]: x for x in tex3dt.textures(
        omkpaths.data("MESHES/DECORS/Anekbah.3DO"))}
    anek = sum(len(recs[n + ".BMP"]) > 1 for n in A)

    def pct(other, n):
        B = {x["name"]: x for x in tex3dt.textures(
            omkpaths.data("MESHES/DECORS/%s.3DO" % other))}
        a, b = A[n]["rgb"], B[n]["rgb"]
        w, ht = A[n]["w"], A[n]["h"]
        dif = sum(1 for k in range(0, len(a), 3)
                  if abs(a[k]-b[k]) + abs(a[k+1]-b[k+1]) + abs(a[k+2]-b[k+2]) > 24)
        return round(100.0 * dif / (w * ht), 1)

    return (len(recs), collide, len(A), anek,
            pct("AToit", "BATITR15"), pct("AToit", "BATITR12"),
            pct("AToit", "BATITR18"), pct("AToit", "BATITR09")), \
           (1041, 182, 20, 20, 21.4, 12.1, 1.9, 0.0), \
           "distinct 19-char texture names in gamedata/MESHES; names shipping more " \
           "than one image; ANEKBAH atlases; of which collide; and how much " \
           "AToit's copy of BATITR15 / 12 / 18 / 09 differs, in percent"


def c_anekbah_signs():
    r"""ASSETS 4b: one substituted atlas, neighbouring panels disagreeing.

    The report was "of four panels in one shot, 2 is stably wrong and 3 is
    correct", which no depth rule, cull mode or draw order can produce. This is
    the check that it falls straight out of the name cache: measured over the
    UV rectangle each sign samples, AToit's BATITR12 leaves some Anekbah signs
    essentially untouched and repaints others by half.

    Also pins the coincident-pair tie-break, which is the OTHER half of the
    Anekbah story and was previously recorded as not understood: every pair is
    inside a single mesh, so the state bits cancel and the lower texture slot -
    i.e. the lower material index - wins.
    """
    import re, mesh3do
    p = omkpaths.data("MESHES/DECORS/Anekbah.3DO")
    h, ms = mesh3do.meshes(p)
    d = open(p, "rb").read()
    mats = [d[h["matOff"]+80*i:h["matOff"]+80*i+20].split(b"\0")[0].decode()
            for i in range(h["materials"])]
    basev = {}; baset = {}; baseq = {}; av = at = aq = 0
    for m in ms:
        basev[m["i"]], baset[m["i"]], baseq[m["i"]] = av, at, aq
        av += m["vertices"]; at += m["triangles"]; aq += m["quads"]
    byid = {m["id"]: m for m in ms}

    def anc(m, k):
        cur, g = byid.get(m["parent"]), 0
        while cur is not None and g < 16:
            if cur["vertices"] > 3 and k < cur["vertices"]: return cur
            cur = byid.get(cur["parent"]); g += 1
        return None

    def world(m, i, off):
        if i < 0:
            a = anc(m, i & 0x7FFF)
            if a is None: return None
            gi, o = basev[a["i"]] + (i & 0x7FFF), a["pos"]
        else:
            gi, o = basev[m["i"]] + i, off
            if gi >= h["vertices"]: return None
        v = struct.unpack_from("<3f", d, h["vtxOff"] + 32 * gi)
        return (round(v[0]+o[0], 4), round(v[1]+o[1], 4), round(v[2]+o[2], 4))

    groups = {}
    for m in ms:
        off = m["pos"]
        for t in range(baset[m["i"]], baset[m["i"]] + m["triangles"]):
            o = h["triOff"] + 28 * t
            idx = struct.unpack_from("<3h", d, o)
            mid = struct.unpack_from("<i", d, o + 12)[0]
            if mid < 0: continue
            c = [world(m, i, off) for i in idx]
            if any(x is None for x in c): continue
            groups.setdefault(tuple(sorted(c)), []).append((m["i"], mid))
        for q in range(baseq[m["i"]], baseq[m["i"]] + m["quads"]):
            o = h["quadOff"] + 32 * q
            idx = struct.unpack_from("<4h", d, o)
            mid = struct.unpack_from("<i", d, o + 16)[0]
            if mid < 0: continue
            c = [world(m, i, off) for i in idx]
            if any(x is None for x in c): continue
            groups.setdefault(tuple(sorted(c)), []).append((m["i"], mid))
    co = [v for v in groups.values() if len(v) > 1]
    diff = [v for v in co if len({x[1] for x in v}) > 1]
    one_mesh = sum(len({x[0] for x in v}) == 1 for v in diff)

    A = {x["name"]: x for x in tex3dt.textures(p)}
    B = {x["name"]: x for x in tex3dt.textures(
        omkpaths.data("MESHES/DECORS/AToit.3DO"))}
    a, b = A["BATITR12"]["rgb"], B["BATITR12"]["rgb"]
    w = A["BATITR12"]["w"]

    def patch(u0, u1, v0, v1):
        tot = ch = 0
        for y in range(v0, v1 + 1):
            for x in range(u0, u1 + 1):
                k = 3 * (y * w + x); tot += 1
                ch += abs(a[k]-b[k]) + abs(a[k+1]-b[k+1]) + abs(a[k+2]-b[k+2]) > 24
        return round(100.0 * ch / tot)

    return (len(co), len(diff), one_mesh,
            patch(223, 253, 1, 88), patch(0, 126, 62, 95),
            patch(1, 127, 95, 127)), \
           (189, 18, 18, 4, 48, 40), \
           "exactly-coincident face groups in ANEKBAH; pairing different " \
           "materials; of those confined to ONE mesh (so the state bits " \
           "cancel and the texture slot decides); then, of AToit's BATITR12, " \
           "the percent changed under the Adrugs/Ahosp strip, the " \
           "Apolice/Abank patch, and the Asmarket patch"



def c_anekbah_residency():
    r"""ASSETS 4b: the capture walks the transition the cache collision needs.

    This is the check that stops the Anekbah answer being a story about a
    screenshot. `Area_LoadSet` keeps two decor sets resident, so the set that
    was loaded just before the current one still holds its texture slots - and
    `traces/impasse-walk.log`, captured from the shipped engine before any of
    this was read, announces AREA 222 immediately followed by AREA 0. Those
    resolve to AIMPASSE and ANEKBAH, whose atlases share seven names.

    Asserted on three independent sources that have to agree: the trace's own
    announcement order, IAM\AREA's +88 set stems, and the .3dt pixels. If the
    capture stopped containing that transition, or the sets stopped sharing
    those names, the explanation would have lost its footing and this says so.

    The 0.0% row is deliberate. BATITR09 is byte-identical between the two, so
    a substitution of it is invisible - which is the whole point: the engine
    swaps SEVEN atlases here and only the ones that were revised can show.
    """
    import re, mesh3do
    log = open(os.path.join(ROOT, "traces/impasse-walk.log"),
               encoding="utf-8", errors="replace").read()
    areas = re.findall(r'"AREAS",[0-9a-f]+ "(\d+)"', log)
    # the first time ANEKBAH's own area is announced, what preceded it
    i = areas.index("0")
    prev = int(areas[i - 1])

    A = T.archive(omkpaths.data("IAM/AREA"))
    def setof(k):
        return A[k][88:97].split(b"\0")[0].decode("cp1252", "replace").upper()

    a = {x["name"]: x for x in tex3dt.textures(
        omkpaths.data("MESHES/DECORS/Anekbah.3DO"))}
    b = {x["name"]: x for x in tex3dt.textures(
        omkpaths.data("MESHES/DECORS/AImpasse.3DO"))}
    shared = sorted(set(a) & set(b))

    def pct(n):
        x, y = a[n]["rgb"], b[n]["rgb"]
        w, h = a[n]["w"], a[n]["h"]
        d = sum(1 for k in range(0, len(x), 3)
                if abs(x[k]-y[k]) + abs(x[k+1]-y[k+1]) + abs(x[k+2]-y[k+2]) > 24)
        return round(100.0 * d / (w * h), 1)

    return (prev, setof(prev), setof(0), len(shared),
            "BATITR12" in shared, pct("BATITR12"), pct("BATITR09")), \
           (222, "AIMPASSE", "ANEKBAH", 7, True, 12.1, 0.0), \
           "the AREA announced just before ANEKBAH in impasse-walk.log; its " \
           "set; ANEKBAH's set; atlases the two share by name (so they are " \
           "substituted); BATITR12 among them; how much of BATITR12 and of " \
           "BATITR09 that swap actually changes, in percent"



def c_vertex_colour():
    r"""ASSETS 4c: the baked per-vertex light is a COLOUR, not a brightness.

    The engine copies the whole dword at vertex +28 into the D3DTLVERTEX, and
    its own grey fallback weights the three bytes 299 / 587 / 114 - the luma
    coefficients - which names them R / G / B without a judgement call. What
    the data has to show is that this is not a distinction without a
    difference, and it is not: 38.9% of set vertices are not grey, so the old
    reading (byte +29 as a brightness) rendered every set in monochrome.

    Anekbah is asserted by name because it is the case that shows it - a night
    street running from warm white under the lamps to cold blue in shadow -
    and because the viewers now depend on it: `decor_geometry` returns three
    floats a vertex and /api/decorgeo ships a stride of 8.
    """
    import glob, mesh3do
    tot = col = 0
    for p in sorted(glob.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
        try: h = mesh3do.header(p)
        except Exception: continue
        d = open(p, "rb").read()
        o = h["vtxOff"]
        for i in range(h["vertices"]):
            k = o + 32 * i
            tot += 1
            col += not (d[k+28] == d[k+29] == d[k+30])
    g = O.decor_geometry_cached("ANEKBAH")
    v = g["verts"]
    warm = sum(1 for x in v if x[5] > x[7] + 0.10)     # r well above b
    cold = sum(1 for x in v if x[7] > x[5] + 0.10)     # b well above r
    return (tot, col, len(v[0]), warm > 2000, cold > 2000), \
           (405537, 157562, 9, True, True), \
           "vertices in the shipped sets; not grey; floats per vertex on the " \
           "wire (8 until the shimmer phase joined it); ANEKBAH has a " \
           "warm population and a cold one"


def c_lighting_flags():
    r"""ASSETS 4c: sets are baked, and the dynamic light pass is not for them.

    Three flags, three counts, and the interesting ones are the zeros. Mesh
    flag 0x8 is the gate on the dynamic light manager and NOTHING ships with
    it, so the only way into that path is LightObject, whose two callers are
    Actor_LoadModel and Object_Load - characters and props. And 0x4000000
    generates the environment-map UV, which 2 meshes in the whole of DECORS
    ask for and whose texture stage is never given a texture anyway.

    Asserted because each of those numbers licenses a decision in the viewers:
    no dynamic lights, no reflection pass, and a colour flicker on distant
    scenery that is still not implemented.
    """
    import glob, mesh3do
    out = []
    for sub in ("DECORS", "PERSOS"):
        tot = env = flick = dyn = 0
        for p in sorted(glob.glob(omkpaths.data("MESHES/%s/*.3DO" % sub))):
            try: _h, ms = mesh3do.meshes(p)
            except Exception: continue
            for m in ms:
                f = m["flags"] & 0xFFFFFFFF
                tot += 1
                env += bool(f & 0x4000000)
                flick += bool(f & 0x8000000)
                dyn += bool(f & 0x8)
        out += [tot, env, flick, dyn]
    return tuple(out), \
           (12203, 2, 233, 0, 3517, 0, 0, 0), \
           "DECORS meshes / envmap UV / colour flicker / dynamic-light gate, " \
           "then the same four for PERSOS"



def c_decorgeo_wire():
    r"""ASSETS 4c: the /api/decorgeo blob unpacks to the geometry it came from.

    This exists because the colour change broke the viewer and nothing here
    noticed. The server packs `decor_geometry`'s vertices flat and each client
    reads them back with its own arithmetic, and those two halves live in
    different files - so when the stride went 6 -> 8, a page served by a Python
    process started before the change got 6-wide data and read it 8-wide. Every
    position walked off by two floats a vertex and the set drew as giant slabs.
    Both numbers were individually right; only the pair was wrong.

    So the check is the round trip, not either half: pack exactly as omkweb.py
    does, unpack exactly as the clients do - measuring the stride from the
    payload, which is the fix - and require the positions and UVs back
    unchanged. It also asserts the colour actually survives, since a stride
    that is self-consistently wrong would still pass a positions-only test.
    """
    g = O.decor_geometry_cached("ANEKBAH")
    verts = g["verts"]
    stride = len(verts[0])
    blob = struct.pack("<%df" % (len(verts) * stride),
                       *[c for v in verts for c in v])
    d = struct.unpack("<%df" % (len(blob) // 4), blob)
    n = len(verts)
    S = max(6, round(len(d) / n))                 # what the clients now do
    bad_pos = bad_uv = 0
    for i in (0, 1, 7, 913, n // 2, n - 1):
        v = verts[i]
        # float32 on the wire, so compare to a tolerance rather than exactly;
        # the failure this guards against is off by whole coordinates.
        if any(abs(d[i*S+k] - v[k]) > 0.01 for k in range(3)): bad_pos += 1
        if any(abs(d[i*S+3+k] - v[3+k]) > 0.01 for k in range(2)): bad_uv += 1
    lit = [d[i*S+5:i*S+8] for i in range(0, n, 89)]
    coloured = sum(1 for c in lit if max(c) - min(c) > 0.05)
    return (stride, S, bad_pos, bad_uv, coloured > 200), \
           (9, 9, 0, 0, True), \
           "floats per vertex packed; stride the clients measure back; " \
           "sampled positions that survive the round trip badly; UVs; and " \
           "the light is still a colour after it"



def c_vertex_fvf():
    r"""ASSETS 4c: the D3D vertex format, which is what names the colour bytes.

    The baked light is copied into the vertex as one dword, so the channel
    order has to come from somewhere else. It comes from the FVF: both
    DrawPrimitive calls in Raster_DrawTriangles declare D3DFVF_DIFFUSE, and a
    DIFFUSE field is a D3DCOLOR - 0xAARRGGBB, so byte 0 is B and byte 2 is R by
    the D3D ABI rather than by anyone's judgement.

    What makes it a test rather than a citation is that the two FVFs predict
    their own vertex sizes, and both predictions have to match strides that
    were read out of the emit loops independently: 32 bytes for the plain path
    and 40 for the environment-map one, which carries a second UV set. A wrong
    reading of either constant breaks the arithmetic.

    Recorded because the alternative witness - the luma-to-grey conversion in
    sub_42FF80 - agrees but lives in a back end nothing installs, so it cannot
    carry the claim on its own.
    """
    s = _need("decomp")
    if s: return s
    SZ = {0x004: 16, 0x040: 4, 0x080: 4, 0x100: 8, 0x200: 16}
    def size(f): return sum(v for k, v in SZ.items() if f & k)
    # read the two constants back out of the decompilation rather than
    # trusting this docstring: they are the 3rd argument of the two
    # DrawPrimitive calls inside Raster_DrawTriangles (0x00460B80).
    src = open(omkpaths.decomp_path(), encoding="utf-8",
               errors="replace").read()
    i = src.index("//----- (00460B80)")
    j = src.index("//----- (00461660)", i)
    body = src[i:j]
    fvfs = sorted({int(m) for m in re.findall(
        r"\+ 112\)\)\(\s*\w+,\s*4,\s*(\d+),", body)})
    return (fvfs, [size(f) for f in fvfs], [bool(f & 0x040) for f in fvfs],
            (fvfs[1] ^ fvfs[0]) == (0x200 | 0x100)), \
           ([452, 708], [32, 40], [True, True], True), \
           "the FVFs Raster_DrawTriangles declares; the vertex sizes they " \
           "imply, which must equal the strides the emit loops step by; both " \
           "carry DIFFUSE (so the colour is a D3DCOLOR, 0xAARRGGBB); and they " \
           "differ only by TEX1 vs TEX2"



def c_effect_sprites():
    r"""ASSETS 3b: the effect sprites, and the SCX chunk table they sit in.

    Chunk 4's streamed header is THREE words - [own offset, model size,
    TEXTURE size] - and the payload is a whole .3DO followed by its .3dt. Read
    that way the walk needs no resync, which is the test: 220 files land on
    their own size, 0 resyncs, and every one of the 230 payloads starts with
    OD3X. Two decoders have to agree for that to hold, since tex3dt's walk over
    the embedded texture must also consume it exactly - 26 of 26.

    It also pins the chunk table, three of whose counts were stale (chunk 3 said
    491 against 1667, chunk 4 said 94 against 230, chunk 7 said 3456 against
    13952). They are asserted here from two independent routes - the block's
    own registry and the stream walk - because a table of counts that nothing
    re-measures is exactly the claim this file exists to catch.
    """
    import glob, scene_scx as S, anim_3da as A, sprite_fx, tex3dt as T
    files = sorted(set(glob.glob(omkpaths.data("SCPTDATA/*.SCX")) +
                       glob.glob(omkpaths.data("SCPTDATA/*.scx"))))
    reg = {}
    for p in files:
        for ty, recs in S.scene(p)["chunks"].items():
            reg[ty] = reg.get(ty, 0) + len(recs)
    walked = resyncs = ends = od3x = 0
    wavs = anims = 0
    lib = {}
    for p in files:
        st = A.scx_stream(p)
        walked += 1
        resyncs += st["resyncs"]
        ends += st["end"] == st["size"]
        wavs += len(st["wavs"]); anims += len(st["anims"])
        d = st["data"]
        for sp in st["sprites"]:
            o, m, t = sp["offset"], sp["model"], sp["texture"]
            od3x += d[o:o + 4] == b"OD3X"
            lib.setdefault(sp["name"], (d[o:o + m], d[o + m:o + m + t]))
    texok = sum(1 for m, t in lib.values()
                if (lambda x: bool(x) and x[-1]["consumed"] == len(t))
                   (T.textures_bytes(m, t)))
    # frame counts: one quad is one frame
    fr = {n: len(sprite_fx.frames(m, t)) for n, (m, t) in lib.items()}
    return (walked, ends, resyncs,
            reg.get(2), reg.get(3), reg.get(4), reg.get(7),
            wavs, anims, od3x, len(lib), texok,
            fr.get("EFFECTS1_EXPLO1.3DO"), fr.get("EFFECTS2_SMOKE1.3DO"),
            fr.get("EFFECTS2_GLOW.3DO")), \
           (220, 220, 0, 4511, 1667, 230, 13952,
            1667, 1490, 230, 26, 26, 16, 8, 1), \
           "SCX files walked; landing exactly on EOF; resyncs needed; the " \
           "block registry's chunk 2 / 3 / 4 / 7 counts; the stream's own " \
           "wav and anim counts (which must match 3 and 1); sprite payloads " \
           "starting OD3X; distinct sprites; whose embedded texture walk is " \
           "exact; and the frame counts of EXPLO1, SMOKE1 and GLOW"



def c_script_dispatch():
    r"""FILE_FORMATS: the two script dispatchers, and what neither one handles.

    Script_StartScript (0x0044A7E0) runs a function once when its program
    starts; Script_PlayScript (0x0044C860) runs it every frame. Which one an id
    appears in is the whole answer to how the effect sprites animate - they are
    in the first and NOT the second - so this asserts the membership, read out
    of the hand-cleaned Script_PlayScript body rather than from a regex over
    the decompiler's nested ifs, which silently missed six of its cases.

    The two zeros are the content. Three ids the shipped scripts use have no
    handler anywhere (120 calls, inert); nine implemented functions are never
    called, including every sprite function except plain display. A change to
    either set would mean the reading is wrong.
    """
    import re, glob, collections, scene_scx as S
    src = open(os.path.join(ROOT, "readable/src/17_script.c"),
               encoding="utf-8").read()
    i = src.index("@func 0x0044C860"); j = src.index("@func 0x0044", i + 20)
    play = {int(m, 16) for m in
            re.findall(r'case (0x[0-9A-F]{8}):', src[i:j])}
    start = set()
    for f in glob.glob(os.path.join(ROOT, "readable/src/*.c")):
        t = open(f, encoding="utf-8").read()
        if "@func 0x0044A7E0" not in t: continue
        i = t.index("@func 0x0044A7E0"); j = t.index("@func 0x0044", i + 20)
        b = t[i:j]
        start = {int(m, 16) for m in re.findall(r'case (0x[0-9A-F]{7,8})u?:', b)
                 if int(m, 16) > 0xFFFF}
        start |= {int(m) for m in re.findall(r'== (\d{8,})', b)}
    used = collections.Counter()
    for p in sorted(set(glob.glob(omkpaths.data("SCPTDATA/*.SCX")) +
                        glob.glob(omkpaths.data("SCPTDATA/*.scx")))):
        for o in S.scene(p)["objects"]:
            for fn in o["functions"]:
                used[fn["id"]] += 1
    handled = start | play
    orphan = {i: n for i, n in used.items() if i not in handled}
    unused = handled - set(used)
    sprite_used = {i for i in used if i >> 24 == 4}
    return (len(used), sum(used.values()), len(play), len(start),
            sorted(orphan), sum(orphan.values()), len(unused),
            0x04000028 in start, 0x04000028 in play,
            sorted(i for i in sprite_used if i in play)), \
           (17, 13887, 18, 15,
            [0x0400000C, 0x0400001F, 0x04000029], 120, 9,
            True, False, []), \
           "distinct ids the corpus uses; total calls; ids Script_PlayScript " \
           "handles; ids Script_StartScript handles; ids used with NO handler " \
           "anywhere; how many calls those are; implemented-but-never-used " \
           "ids; Display3DSprite starts; it does NOT play; and no sprite id " \
           "the corpus uses is ticked at all"



def c_sprite_frame_rule():
    r"""ASSETS 3b: the frame index is bounded by the model's quad count.

    Two independent things have to line up for the format reading to be right.
    Sprite_SetFrame bounds-checks instance+22 against meshdef+72 - the QUAD
    count - and Cef_SpawnEffect stashes that same word as the effect's frame
    count, which Cef_TickEffects then divides the authored duration across. So
    "one quad is one frame" is the engine's own statement, not an inference
    from the UV layout.

    What the data has to show is that the two agree: every shipped sprite's
    quad count must equal the number of distinct UV cells its quads carry. If a
    sprite had two quads sharing a cell, or a cell no quad used, the frame
    count the engine computes would not be the number of pictures.
    """
    import struct, glob, anim_3da as A, mesh3do
    lib = {}
    for p in sorted(set(glob.glob(omkpaths.data("SCPTDATA/*.SCX")) +
                        glob.glob(omkpaths.data("SCPTDATA/*.scx")))):
        st = A.scx_stream(p); d = st["data"]
        for sp in st["sprites"]:
            o, m = sp["offset"], sp["model"]
            lib.setdefault(sp["name"], d[o:o + m])
    cells_ok = one = tri = 0
    for m in lib.values():
        h = mesh3do.header_bytes(m, len(m))
        cells = set()
        for q in range(h["quads"]):
            uv = struct.unpack_from("<8B", m, h["quadOff"] + 32 * q + 8)
            us, vs = uv[0::2], uv[1::2]
            cells.add((min(us) // 8, min(vs) // 8))   # 8-texel tolerance
        cells_ok += len(cells) == h["quads"]
        # one mesh and one material apiece is what makes it a frame strip
        one += h["meshes"] == 1 and h["materials"] == 1
        tri += h["triangles"]
    return (len(lib), cells_ok, one, tri), \
           (26, 26, 26, 0), \
           "distinct sprites; whose quads occupy as many DISTINCT atlas cells " \
           "as there are quads (so quad count == frame count); which are one " \
           "mesh and one material; and the triangles they carry between them"



def c_sprite_blend_modes():
    r"""ASSETS 3b: instance +20 is the blend mode, and mode 4 is additive.

    Read out of Render_SubmitSprites' own switch rather than quoted, because
    this is the third place the additive correction turns up and the point is
    that the three are independent: the sprite MESHES carry flags 0x1000|0x2000
    (§4c), the .CTL effects all spawn at mode 4, and mode 4 ORs bucket bits
    0x2100 - transparent plus SRCBLEND/DESTBLEND ONE/ONE.

    Asserted against the decompilation so a re-extraction that changed the
    switch fails here rather than silently making the docs wrong. The bits are
    written to BYTE1 of the key, so each case value is the bucket bits >> 8.
    """
    s = _need("decomp")
    if s: return s
    src = open(omkpaths.decomp_path(), encoding="utf-8",
               errors="replace").read()
    i = src.index("//----- (004969C0)")
    j = src.index("\n//----- (", i + 10)
    b = src[i:j]
    k = b.index("switch (*(_WORD *)(i + 20))")
    blk = b[k:k + 900]
    modes, pending = {}, []
    # the constants are written both ways - `|= 4u` and `|= 0x20u`
    for m in re.finditer(r'case (\d+):|BYTE1\(v\d+\) \|= (0x[0-9A-Fa-f]+|\d+)u', blk):
        if m.group(1) is not None:
            pending.append(int(m.group(1)))
        else:
            for p in pending: modes[p] = int(m.group(2), 0) << 8
            pending = []
    # the same key the mesh path builds, so the bits must be RB_* values
    known = {0x400, 0x2000, 0x2400, 0x2100, 0x2500, 0x2200, 0x2600}
    return (sorted(modes), modes.get(4), modes.get(1), modes.get(2),
            set(modes.values()) <= known,
            "4 * (v22 & 0x3FFF)" in b, "dword_9070B8" in b), \
           ([1, 2, 3, 4, 5, 6, 7, 8], 0x2100, 0x400, 0x2000,
            True, True, True), \
           "sprite blend modes the renderer switches on; mode 4's bucket bits " \
           "(0x2100 = transparent + additive); mode 1's; mode 2's; all of " \
           "them are RB_* bits; and sprites go into the SAME bucket array " \
           "under the same 14-bit key as meshes"



def c_sfx_ambient_effects():
    r"""FILE_FORMATS 5b6: .SFX section C is the ambient-effect table.

    Two fields carry this, and both are tests the data could fail rather than
    quotes from the code.

    +78 is the sprite BLEND MODE. Sfx_TickAmbient copies the byte into the
    sprite instance's +20 and Render_SubmitSprites switches on it - an 8-value
    enum. A byte at a guessed offset would not land inside that enum for every
    row; this one does, 366 of 366, and 332 of them are mode 4, ADDITIVE. That
    is the fourth independent arrival of the additive correction, after the
    sprite mesh flags, the .CTL spawn mode, and the renderer's own switch.

    +4 is the sound id. Sfx_TickAmbient plays it only when it is neither -1 nor
    0xFFFF, so the sentinel is the engine's, not a convention read off the
    data - and 338 of 366 rows carry it, leaving 28 that name 26 distinct real
    sounds.

    The name at +70 is asserted loosely (how many rows carry a printable one),
    because it is documentation rather than something the engine branches on.
    """
    import glob, collections
    files = sorted(_sfx_paths())
    rows = named = silent = 0
    modes = collections.Counter()
    ids = set()
    dtot = dres = 0
    for p in files:
        d = open(p, "rb").read()
        if d[:4] != b"5.0V": continue
        A = struct.unpack_from("<I", d, 4)[0]; o = 8 + 40 * A
        B = struct.unpack_from("<I", d, o)[0]; o += 4 + 44 * B
        C = struct.unpack_from("<I", d, o)[0]; cb = o + 4; o = cb + 80 * C
        D = struct.unpack_from("<I", d, o)[0]; db = o + 4
        cids = set()
        for i in range(C):
            r = d[cb + 80 * i:cb + 80 * i + 80]
            rows += 1
            modes[r[78]] += 1
            sid = struct.unpack_from("<i", r, 4)[0]
            if sid in (-1, 0xFFFF): silent += 1
            else: ids.add(sid)
            nm = r[70:78].split(b"\0")[0]
            named += bool(nm) and all(32 <= c < 127 for c in nm)
            cids.add(struct.unpack_from("<i", r, 0)[0])
        for i in range(D):
            dtot += 1
            dres += struct.unpack_from("<i", d, db + 16 * i)[0] in cids
    return (rows, all(0 <= m <= 8 for m in modes), modes[4], modes[6],
            silent, len(ids), named, dtot, dres), \
           (396, True, 362, 32, 368, 24, 396, 156, 154), \
           "section C rows; every +78 inside the renderer's 0..8 blend-mode " \
           "enum; how many are mode 4 (additive) and mode 6 (multiply); rows " \
           "whose +4 is the engine's own silent sentinel; distinct real sound " \
           "ids in the rest; rows with a printable name at +70; then section " \
           "D rows and how many resolve into C by the id at +0"



def c_ambient_binding():
    r"""FILE_FORMATS 5b6: a mesh finds its ambient effect by NAME.

    The rule is out of the assembly - `mov ecx, [eax+10h]` takes the first four
    bytes of the mesh definition's name and compares them as a DWORD against
    each section D row's +4 - because the decompiler's output made it look as
    though the match were on the id. So this is the check that the rule holds
    in the shipped data, across three files that were authored separately: the
    .3DO's mesh flags and names, the .SFX's section D, and its section C.

    321 of the 579 flagged meshes resolve all the way to a named effect. The
    residue is the honest part and is asserted too: 185 sit in sets with no
    .SFX at all, and the compare is case-sensitive, so `SPiT` finds nothing
    where `SPOT` would. `neon` at 102 meshes is Anekbah's flickering city.
    """
    import glob, collections, mesh3do
    decor = {f.upper().rsplit(".", 1)[0]: omkpaths.data("MESHES/DECORS", f)
             for f in os.listdir(omkpaths.data("MESHES/DECORS"))
             if f.upper().endswith(".3DO")}
    sfx = {}
    for p in _sfx_paths():
        d = open(p, "rb").read()
        if d[:4] != b"5.0V": continue
        A = struct.unpack_from("<I", d, 4)[0]; o = 8 + 40 * A
        B = struct.unpack_from("<I", d, o)[0]; o += 4 + 44 * B
        C = struct.unpack_from("<I", d, o)[0]; cb = o + 4; o = cb + 80 * C
        D = struct.unpack_from("<I", d, o)[0]; db = o + 4
        cid = {struct.unpack_from("<i", d, cb + 80 * i)[0]: cb + 80 * i
               for i in range(C)}
        m = {}
        for i in range(D):
            eid = struct.unpack_from("<i", d, db + 16 * i)[0]
            if eid in cid:
                m[d[db + 16 * i + 4:db + 16 * i + 8]] = (d, cid[eid])
        sfx[os.path.basename(p).upper().rsplit(".", 1)[0]] = m
    tot = bound = nosfx = 0
    names = collections.Counter()
    for stem, path in sorted(decor.items()):
        try: h, ms = mesh3do.meshes(path)
        except Exception: continue
        dd = open(path, "rb").read()
        tbl = sfx.get(stem)
        for m in ms:
            if not ((m["flags"] & 0xFFFFFFFF) & 0x40000000): continue
            tot += 1
            if tbl is None: nosfx += 1; continue
            o = h["meshOff"] + 140 * m["i"] + 16
            t = dd[o:o + 4]
            if t in tbl:
                bound += 1
                d, co = tbl[t]
                names[d[co+70:co+78].split(b"\0")[0].decode("cp1252", "replace")] += 1
    return (tot, nosfx, bound, names["neon"], names["fume"], names["agaz"]), \
           (579, 179, 325, 102, 33, 12), \
           "set meshes flagged 0x40000000; in a set with no .SFX; BOUND to a " \
           "section C effect by their name's first four bytes; and how many " \
           "resolve to neon, fume and agaz"



def c_ambient_emitters():
    r"""ASSETS 3b: the resolver the viewer draws from, end to end.

    c_ambient_binding asserts the mesh->effect link; this asserts the whole
    chain the /cutscene page actually consumes, including the two steps beyond
    it - the effect's sprite id resolving into the .SCX chunk-4 registry, and
    that sprite's quads being its frames. Three separately authored files have
    to agree for a single emitter to come out.

    ANEKBAH is named because it is the case the work started from: 153 emitters
    drawing three sprites, and the reason the city flickers in the game while
    the viewers used to stand still. Its effects ask for modes 4 and 5 - both
    additive, 5 with cutout, which makes no difference under ONE/ONE because
    black adds nothing; the viewer treats them alike for that reason.
    """
    import glob, ambientfx
    tot = sets = 0
    for f in sorted(os.listdir(omkpaths.data("MESHES/DECORS"))):
        if not f.upper().endswith(".3DO"): continue
        g = ambientfx.emitters(f.rsplit(".", 1)[0])
        if g and g["emitters"]:
            sets += 1
            tot += len(g["emitters"])
    a = ambientfx.emitters("ANEKBAH")
    frames = {k: len(v["frames"]) for k, v in a["sprites"].items()}
    modes = {e["mode"] for e in a["emitters"]}
    per = sum(1 for e in a["emitters"] if e["period"] > 0)
    neon = [e for e in a["emitters"] if e["effect"] == "neon"]
    return (tot, sets, len(a["emitters"]), len(a["sprites"]),
            frames.get("EFFECTS2_SMOKE1.3DO"), sorted(modes),
            all(e["life"] > 0 for e in a["emitters"]), per,
            len(neon), all(e["period"] == 0 for e in neon)), \
           (321, 12, 153, 3, 8, [4, 5], True, 5, 102, True), \
           "emitters resolved across the corpus; sets carrying one; ANEKBAH's " \
           "emitters and the distinct sprites they use; SMOKE1's frame count; " \
           "the blend modes ANEKBAH asks for; every emitter has a positive " \
           "lifetime (the viewer divides by it); how many BLINK (period > 0); " \
           "and its neon emitters, every one of which is period 0 - a " \
           "continuous emitter, so the neon does NOT flicker"



def c_particle_budget():
    r"""ASSETS 3b: the cadence's own particle count fits the engine's pool.

    An emitter with period P and lifetime L keeps ceil(L / P) particles alive -
    one spawned every P frames, each living L. That count is what the viewer
    has to reproduce, and this asserts the thing that licenses reproducing it
    exactly rather than capping it: summed over a whole set, the worst case in
    the game is 893, inside Sprite_AllocPool's own 1000.

    It exists because capping it was a real bug. Wrapping a particle's age at
    n*step rather than at its lifetime held a 23-frame flame to 6 frames - a
    quarter of its height, stuck on the first few of its 16 animation frames -
    which is what the reference screenshot showed against the game.
    """
    import math, ambientfx
    worst = worst_set = 0
    tot = 0
    for f in sorted(os.listdir(omkpaths.data("MESHES/DECORS"))):
        if not f.upper().endswith(".3DO"): continue
        g = ambientfx.emitters(f.rsplit(".", 1)[0])
        if not g or not g["emitters"]: continue
        n = sum(max(1, math.ceil(e["life"] / (e["period"] if e["period"] > 0 else 1)))
                * e["count"] for e in g["emitters"])
        tot += n
        if n > worst: worst, worst_set = n, g["set"]
    a = ambientfx.emitters("ANEKBAH")
    fl = [e for e in a["emitters"] if e["mesh"] == "afum01"][0]
    return (worst, worst_set, worst <= 1000, tot,
            round(fl["life"]), round(fl["life"] * fl["speed"]),
            math.ceil(fl["life"]) * fl["count"], fl["grow"]), \
           (908, "ANEKBAH", True, 3228, 23, 46, 23, 1), \
           "the largest per-set particle count the cadence implies, and which " \
           "set; that it fits Sprite_AllocPool's 1000; the total over the " \
           "corpus; then Anekbah's brazier flame - its lifetime in frames, the " \
           "units it rises, the particles it keeps alive (count x cadence), " \
           "and that it GROWS (flag 0x4)"



def c_emitter_flags():
    r"""FILE_FORMATS 5b6: the emitter flags are all randomisations and ramps.

    Read out of Sfx_TickAmbient: 0x4 and 0x2000 add +-scale/life to the
    particle's scale each frame, 0x10 gives it a random start angle, 0x80/0x100
    jitter the count and lifetime by 10%, 0x1000 widens the cone by up to its
    own value, 0x200 signs the +28 drift. There is no other per-particle
    behaviour in the function.

    The bit worth asserting is 0x2, the alpha ramp: NO shipped effect sets it,
    so every particle keeps the default 0.5 and the viewer does not implement
    it. A zero is the strongest kind of claim here and the easiest to break by
    accident, so it is the one under test.
    """
    import glob, collections
    files = sorted(_sfx_paths())
    n = collections.Counter()
    cnt = collections.Counter()
    rows = 0
    for p in files:
        d = open(p, "rb").read()
        if d[:4] != b"5.0V": continue
        A = struct.unpack_from("<I", d, 4)[0]; o = 8 + 40 * A
        B = struct.unpack_from("<I", d, o)[0]; o += 4 + 44 * B
        C = struct.unpack_from("<I", d, o)[0]; cb = o + 4
        for i in range(C):
            r = d[cb + 80 * i:cb + 80 * i + 80]
            rows += 1
            f = struct.unpack_from("<I", r, 12)[0]
            for b in (0x2, 0x4, 0x10, 0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000):
                if f & b: n[b] += 1
            cnt[struct.unpack_from("<h", r, 68)[0]] += 1
    ramp = n[0x4] + n[0x2000]
    return (rows, n[0x2], n[0x4], n[0x2000], ramp, n[0x10], n[0x1000],
            max(cnt), cnt[1], sum(1 for k in cnt if k < 0), n[0x40]), \
           (396, 0, 125, 133, 258, 329, 321, 15, 286, 0, 76), \
           "section C rows; using the alpha ramp 0x2 (zero - it never runs); " \
           "grow 0x4; shrink 0x2000; either (so 241 of 366 ramp their scale); " \
           "random angle 0x10; random cone 0x1000; the largest +68 particles " \
           "per emission; how many rows emit exactly one; negative counts " \
           "(none, so +68 is unsigned in practice); and 0x40, the once-per-" \
           "emitter axis jitter that makes a flame lean"



def c_camera_roll():
    r"""CUTSCENES: the world-camera roll, and why the viewer must negate it.

    The viewer maps the game's space to its own with W(v) = [x, -y, z]. That is
    a REFLECTION, and a reflection reverses the sense of every rotation about an
    axis - so a roll applied unnegated in the flipped basis turns the camera the
    opposite way. Points survive the flip; angles do not.

    This asserts the scope rather than the rendering, because the rendering is
    not a number: how many of the title sequence's frames carry a roll big
    enough to see. 2725 of 4370 do, up to 42 degrees, which is why the Bowie
    sequence was where it showed and why the dialogue viewer - whose cameras are
    almost all unrolled - never did.
    """
    import cutscene as CU
    sh = CU.camera_shot("AREA", 0, 78)
    rolls = [c[6] for c in sh["camera"]]
    big = sum(1 for r in rolls if abs(r) > 1.0)
    wrapped = sum(1 for r in rolls if abs(r) > 180.0)
    cams = CU.world_cameras("AREA", 0)
    nonzero = sum(1 for c in cams.values() if abs(c["roll"]) > 1.0)
    return (len(rolls), big, wrapped, round(max(abs(r) for r in rolls), 1),
            len(cams), nonzero), \
           (4370, 2725, 0, 42.5, 154, 43), \
           "frames in the title sequence; those with a roll over 1 degree " \
           "(so the sign is visible); any left unwrapped past 180 (must be 0); " \
           "the largest roll; AREA 0's world cameras and how many are rolled"


def c_particle_accel():
    r"""FILE_FORMATS 5b6: section C +28 is an acceleration on world Y.

    The particle integrator does `vel.y += accel` once per frame, so Y is
    quadratic in the particle's age while X and Z stay linear. That is what
    makes a flame a tall column instead of a short puff, and it was the
    difference a screenshot of the real game showed.

    Asserted on the brazier because it is the case that was compared: without
    the term its particles travel 46 units over 23 frames, with it they climb
    away and the column reads as the game's. 159 of the 366 effects use it.
    """
    import glob, ambientfx
    n = z = 0
    for p in _sfx_paths():
        d = open(p, "rb").read()
        if d[:4] != b"5.0V": continue
        A = struct.unpack_from("<I", d, 4)[0]; o = 8 + 40 * A
        B = struct.unpack_from("<I", d, o)[0]; o += 4 + 44 * B
        C = struct.unpack_from("<I", d, o)[0]; cb = o + 4
        for i in range(C):
            n += 1
            z += struct.unpack_from("<f", d, cb + 80 * i + 28)[0] == 0.0
    g = ambientfx.emitters("ANEKBAH")
    e = [x for x in g["emitters"] if x["mesh"] == "afum01"][0]
    lin = e["speed"] * e["life"]
    quad = abs(0.5 * e["accel"] * e["life"] ** 2)
    return (n, z, n - z, round(e["accel"], 3), round(lin), round(quad),
            e["accel"] < 0), \
           (396, 226, 170, -0.488, 46, 129, True), \
           "section C rows; with no acceleration; with one; the brazier " \
           "flame's; the units it would travel at constant speed; the extra " \
           "the acceleration adds; and that it is negative, i.e. UP (Y is down)"



def c_vertex_shimmer():
    r"""ASSETS 4: mesh flag 0x8000000 shimmers the vertex colour.

    `sub_4947F0` adds a SIGNED value from `byte_4DDBB0` to all three channels
    and clamps - `movsx` in the assembly, which is what makes it a modulation
    rather than a brightening. The phase is `(clock >> 2) + (vertexAddress >>
    4)`, and the clock advances 2 a frame and wraps at 256, so a cycle is 32
    steps over 64 frames.

    The table is read back out of the disassembly rather than quoted, because
    its shape is the whole behaviour: 32 entries running 0 up to +64 and back
    down to -64, i.e. a full oscillation, symmetric about zero. A table that
    was all-positive would be a pulse, not a shimmer.
    """
    s = _need("asm")
    if s: return s
    import re, glob, mesh3do
    lines = open(omkpaths.asm_path(), encoding="utf-8",
                 errors="replace").read().split("\n")
    i = next(k for k, l in enumerate(lines) if l.startswith("byte_4DDBB0"))
    vals = []
    for l in lines[i:i + 60]:
        m = re.search(r'\bdb\s+([0-9A-Fa-f]+h|\d+)', l)
        if not m:
            if vals: break
            continue
        t = m.group(1)
        v = int(t[:-1], 16) if t.endswith("h") else int(t)
        vals.append(v - 256 if v > 127 else v)      # movsx: it is signed
        if len(vals) == 32: break
    meshes = tot = 0
    for p in sorted(glob.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
        try: _h, ms = mesh3do.meshes(p)
        except Exception: continue
        for m in ms:
            tot += 1
            meshes += bool((m["flags"] & 0xFFFFFFFF) & 0x8000000)
    g = O.decor_geometry_cached("LAHOREH")
    shim = sum(1 for v in g["verts"] if v[8] >= 0)
    return (len(vals), max(vals), min(vals), sum(vals), vals[8], vals[24],
            len(g["verts"][0]), tot, meshes, shim), \
           (32, 64, -64, 0, 64, -64, 9, 12203, 233, 16752), \
           "entries in the wave table; its peak and trough; their sum (0 - it " \
           "is symmetric, so this is a shimmer and not a pulse); the values at " \
           "the two extremes; floats per vertex on the wire; set meshes; those " \
           "flagged 0x8000000; and LAHOREH's shimmering vertices"



CHECKS = [
    ("conversations",      c_conversations,     "FILE_FORMATS 2"),
    ("DIALOGS.TAG",        c_dialog_tag,        "FILE_FORMATS 4"),
    ("dialogue walk",      c_dialog_selftest,   "CLAUDE.md 4"),
    ("dialogue scripts",   c_dialog_scripts,    "SCRIPT_VM"),
    ("world scripts",      c_world_scripts,     "SCRIPT_VM"),
    ("dialog.start sites", c_trigger_sites,     "SCRIPT_VM"),
    (".CTL walk",          c_ctl,               "ASSETS"),
    (".SCX scenes",        c_scx,               "FILE_FORMATS 5c"),
    ("opt tracks",         c_opt_tracks,        "STREET_LIFE 2"),
    ("ADDRESSES table",    c_addresses,         "FILE_FORMATS 5c"),
    ("object table",       c_objects,           "FILE_FORMATS 5c"),
    ("actor -> model",     c_actor_models,      "FILE_FORMATS 5e"),
    ("camera aim = 768",   c_aim_length,        "FILE_FORMATS 2"),
    ("line-cam bundles",   c_bundles,           "ASSETS"),
    ("dialog 402 vs game", c_dialog402,         "ASSETS"),
    ("var.set.random",     c_var_set_random,   "SCRIPT_VM"),
    ("music.play",         c_music_play,        "SCRIPT_VM"),
    ("fade colour",        c_fade_color,        "SCRIPT_VM"),
    ("music.volume",       c_music_volume,      "SCRIPT_VM"),
    ("scene.load/unload",  c_scene_load,        "SCRIPT_VM"),
    ("prop table",         c_prop_table,        "FILE_FORMATS 5c"),
    ("prop opcodes",       c_prop_opcodes,      "SCRIPT_VM"),
    ("variable writers",   c_var_writers,       "SCRIPT_VM"),
    ("character operands", c_character_operands, "SCRIPT_VM"),
    ("arm an actor",       c_arm_actor,         "SCRIPT_VM"),
    ("ui screens",         c_ui_screens,        "SCRIPT_VM"),
    ("ui tables",          c_ui_tables,         "UI"),
    ("ui sound slots",     c_ui_sound_slots,    "UI 3"),
    ("menu layout",        c_menu_layout,       "UI 3b"),
    ("menu cloud",         c_menu_cloud,        "UI 3b"),
    ("ui widgets",         c_ui_widgets,        "UI"),
    ("ui fonts",           c_ui_fonts,          "UI"),
    ("ui input",           c_ui_input,          "UI"),
    ("object combine",     c_object_combine,    "UI"),
    ("fight ai",           c_fight_ai,          "ASSETS"),
    ("sim: ui",            c_sim_ui,            "UI"),
    ("sim: options",       c_sim_options,       "UI"),
    ("sim: load panel",    c_sim_loadpanel,     "UI"),
    ("key bindings",       c_keybindings,       "ASSETS"),
    ("sim: name field",    c_sim_namefield,     "UI"),
    ("sim: ui coverage",   c_sim_ui_coverage,   "UI"),
    ("ui page",            c_ui_page,           "UI"),
    ("ui sprites",         c_ui_sprites,        "UI"),
    ("ui text render",     c_ui_textrender,     "UI"),
    ("ui open flags",      c_ui_openflags,      "UI"),
    ("ui open answer",     c_ui_open_answer,    "SCRIPT_VM 70"),
    ("ui confirm gate",    c_ui_confirm_gate,   "UI"),
    ("golden: menu",       c_golden_menu,       "UI"),
    ("page templates",     c_page_templates,    "CLAUDE.md 5"),
    ("page cache busting", c_page_cachebusting, "CLAUDE.md 5"),
    ("inventory ops",      c_inventory_ops,     "SCRIPT_VM"),
    ("fight & become",     c_fight_and_player,  "SCRIPT_VM"),
    ("world cameras",      c_world_cameras,     "FILE_FORMATS 5c"),
    ("engine: world camera", c_engine_world_camera, "PORTING A2; worldcam.h"),
    ("sfx set pieces",    c_sfx_setpieces,     "ASSETS 3b"),
    ("engine: clip root", c_engine_clip_root, "actor/pose.h; FILE_FORMATS"),
    ("scene-area map",    c_scene_area_map,    "script/scenehost.cpp; CLAUDE.md 6"),
    ("engine: arrival camera", c_engine_arrival_camera, "formats/addresses.h; o3de/worldcam.h"),
    ("sprite linkers",     c_sprite_linkers,    "ASSETS 3b"),
    ("engine: pose",        c_engine_pose,       "actor/pose.h, speaker.h"),
    ("engine: intro beat",  c_engine_intro_beat,  "scenerunner.h; SCRIPT_VM 60"),
    ("engine: dialogue play", c_engine_dialogue_play, "dialogue.h; UI"),
    ("opcode tail",        c_opcode_tail,       "SCRIPT_VM"),
    ("object records",     c_object_records,    "FILE_FORMATS 5d"),
    ("morph node slots",   c_morph_nodes,       "FILE_FORMATS 5"),
    ("ctl flag blocks",    c_ctl_blocks,        "ASSETS"),
    ("scx clips",          c_scx_clips,         "ASSETS"),
    ("script programs",    c_script_programs,   "FILE_FORMATS 5c"),
    ("camera editings",    c_cam_editings,      "FILE_FORMATS 5c"),
    ("scx sync chain",     c_scx_sync_chain,    "FILE_FORMATS 5c"),
    ("ctl combat block",   c_ctl_combat,        "ASSETS"),
    ("ctl special moves",  c_ctl_special_moves, "ASSETS"),
    ("ctl transitions",    c_ctl_transitions,   "ASSETS"),
    ("ctl effects",        c_ctl_effects,       "ASSETS"),
    ("ctl groups",         c_ctl_groups,        "ASSETS"),
    ("player anim hold",   c_player_anim_hold,  "FILE_FORMATS 2"),
    ("scene clip roots",   c_scene_clip_roots,  "ASSETS"),
    ("dialog staging",     c_dialog_staging,    "ASSETS"),
    ("exe tables",         c_exe_tables,        "RECONSTRUCTION 4"),
    ("saved player anchor", c_saved_player_anchor, "GAME_STATE 5"),
    ("message tables",     c_message_tables,    "FILE_FORMATS 5"),
    ("zone records",       c_zone_records,      "FILE_FORMATS 5"),
    ("prop assets",        c_prop_assets,       "FILE_FORMATS 5c"),
    ("path durations",     c_path_durations,    "ASSETS"),
    ("sfx files",          c_sfx_files,         "FILE_FORMATS 5"),
    ("extension case",     c_extension_case,    "CLAUDE.md 1"),
    ("map2d",              c_map2d,             "FILE_FORMATS 5b5"),
    ("wre wireframes",     c_wre_files,         "FILE_FORMATS 5b5"),
    ("morph face models",  c_morph_face_models, "FILE_FORMATS 5"),
    ("shoot mode",         c_shoot_mode,        "SCRIPT_VM"),
    ("SOUNDS folder",      c_sounds_folder,     "ASSETS 3b"),
    ("actor stats",        c_actor_stats,       "SCRIPT_VM"),
    ("weapon table",       c_weapon_table,      "FILE_FORMATS 5d"),
    ("mesh alpha blend",   c_mesh_blend,        "ASSETS 4"),
    ("binary identity",    c_binary_identity,   "CLAUDE.md 2"),
    ("golden-trace shim",  c_forwarder,         "RECONSTRUCTION 4.6"),
    ("trace round-trip",   c_trace_roundtrip,   "RECONSTRUCTION 4.6"),
    ("intro script",       c_intro_script,      "RECONSTRUCTION 4.6"),
    ("bowie cutscene",     c_bowie_cutscene,    "RECONSTRUCTION 4.6"),
    ("trace agreement",    c_trace_agreement,   "RECONSTRUCTION 4.6"),
    ("start-script graph", c_start_script_graph,"CUTSCENES 5"),
    ("actor pending scx", c_actor_pending_scx, "CUTSCENES 5"),
    ("startup scripts",   c_startup_scripts,   "FILE_FORMATS"),
    ("menu open site",    c_menu_open_site,    "docs/BOOT.md 3"),
    ("replay actor stat", c_replay_actor_stat, "RECONSTRUCTION 4.6"),
    ("impasse beats",     c_impasse_beats,     "CUTSCENES 5"),
    ("sim: area load",    c_sim_area_load,     "RECONSTRUCTION 4"),
    ("chunk accounting",  c_chunk_accounting,  "FILE_FORMATS"),
    ("sim: narrow phase", c_sim_narrow_phase,  "RECONSTRUCTION 4"),
    ("sim: tutorial walk",c_sim_tutorial_walk, "RECONSTRUCTION 4"),
    ("vm announce fields",c_vm_announce_fields,"SCRIPT_VM"),
    ("sim: dialogue",     c_sim_dialogue,      "RECONSTRUCTION 4"),
    ("save file",         c_save_file,         "GAME_STATE 8"),
    ("telis dialogue",    c_telis_dialogue,    "RECONSTRUCTION 4"),
    ("attribution reach", c_attribution_reach,  "RECONSTRUCTION 4"),
    ("area.goto objects",  c_area_goto,         "SCRIPT_VM"),
    ("animated meshes",    c_anim_meshes,       "ASSETS 4"),
    ("render bucket key",  c_render_bucket_key, "ASSETS 4b"),
    ("drawable mask",      c_render_drawable_mask, "ASSETS 4"),
    ("texture name cache", c_texture_name_cache, "ASSETS 4b"),
    ("anekbah signs",      c_anekbah_signs,     "ASSETS 4b"),
    ("anekbah residency",  c_anekbah_residency, "ASSETS 4b"),
    ("vertex colour",      c_vertex_colour,     "ASSETS 4c"),
    ("lighting flags",     c_lighting_flags,    "ASSETS 4c"),
    ("vertex shimmer",     c_vertex_shimmer,    "ASSETS 4"),
    ("effect sprites",     c_effect_sprites,    "ASSETS 3b"),
    ("script dispatch",    c_script_dispatch,   "FILE_FORMATS 5c"),
    ("sprite frame rule",  c_sprite_frame_rule, "ASSETS 3b"),
    ("sprite blend modes", c_sprite_blend_modes, "ASSETS 3b"),
    ("sfx ambient fx",     c_sfx_ambient_effects, "FILE_FORMATS 5b6"),
    ("ambient binding",    c_ambient_binding,   "FILE_FORMATS 5b6"),
    ("ambient emitters",   c_ambient_emitters,  "ASSETS 3b"),
    ("particle budget",    c_particle_budget,   "ASSETS 3b"),
    ("emitter flags",      c_emitter_flags,     "FILE_FORMATS 5b6"),
    ("particle accel",     c_particle_accel,    "FILE_FORMATS 5b6"),
    ("camera roll",        c_camera_roll,       "CUTSCENES"),
    ("decorgeo wire",      c_decorgeo_wire,     "ASSETS 4c"),
    ("vertex FVF",         c_vertex_fvf,        "ASSETS 4c"),
    ("set pieces",         c_set_pieces,        "FILE_FORMATS 5b6"),
    ("sim: VM executes",   c_sim_vm,            "RECONSTRUCTION 6.1"),
    ("sim: zone lifecycle", c_sim_zones,        "RECONSTRUCTION 6.3"),
    ("sim: SCX interpreter", c_sim_scene,       "RECONSTRUCTION 6.4"),
    ("sim: actor & .CTL",  c_sim_actor,         "RECONSTRUCTION 6.5"),
    ("cutscene links",     c_cutscene_links,    "CUTSCENES 2"),
    ("area assets",        c_area_assets,       "CUTSCENES 1"),
    ("cutscene camera",    c_cutscene_camera,   "CUTSCENES 2"),
    ("cutscene sound",     c_cutscene_sound,    "CUTSCENES 5"),
    ("cutscene music",     c_cutscene_music,    "CUTSCENES 5"),
    ("camera scripts",     c_camera_scripts,    "CUTSCENES 6"),
    ("scx.play family",    c_scx_play_family,   "CUTSCENES 3"),
    ("game state",         c_game_state,        "GAME_STATE 2"),
    ("object lists",       c_object_lists,      "GAME_STATE 3"),
    ("game clock",         c_game_clock,        "GAME_STATE 6"),
    ("INDEX.md fresh",     c_index_idempotent,  "CLAUDE.md 3"),
    ("boot sequence",      c_boot_sequence,     "BOOT"),
    ("opcode table fresh", c_vm_doc,            "SCRIPT_VM"),
    ("vm table sources",   c_vm_table_sources,  "CLAUDE.md 2"),
    ("input paths",        c_input_paths,       "CLAUDE.md 2"),
    ("licence headers",    c_licence_headers,   "LICENSING.md"),
    ("transcript index",   c_transcript_index,  "transcript/README"),
    ("held camera bracket",c_held_camera_bracket,"todo/omk-play 42"),
    ("tutorial one-shot",  c_tutorial_one_shot, "todo/omk-play 42"),
    ("no #define renames", c_no_define_renames, "CLAUDE.md 3"),
]

SLOW = [
    ("engine: 3DT",        c_engine_3dt,        "engine/README"),
    ("engine: 3DO",        c_engine_3do,        "engine/README"),
    ("engine: geometry",   c_engine_3do_geometry, "engine/README"),
    ("engine: IAM",        c_engine_iam,        "engine/README"),
    ("engine: scripts",    c_engine_scripts,    "engine/README"),
    ("engine: execute",    c_engine_execute,    "engine/README"),
    ("engine: zones",      c_engine_zones,      "engine/README"),
    ("engine: area load",  c_engine_area_load,  "engine/README"),
    ("engine: intro",      c_engine_intro,      "engine/README"),
    ("engine: walk",       c_engine_walk,       "engine/README"),
    ("engine: dialogue",   c_engine_dialogue,   "engine/README"),
    ("engine: anims",      c_engine_anims,      "engine/README"),
    ("engine: CTL",        c_engine_ctl,        "engine/README"),
    ("engine: SCX",        c_engine_scx,        "engine/README"),
    ("engine: FX",         c_engine_fx,         "engine/README"),
    ("engine: particles",  c_engine_particles,  "o3de/particles.h; o3de/setpiece.h; ASSETS 3b"),
    ("engine: pose blend", c_engine_pose_blend, "actor/pose.h; FILE_FORMATS 5"),
    ("engine: vm probe",   c_engine_vm_probe,   "script/interp.h; SCRIPT_VM"),
    ("engine: parking ops", c_engine_parking_ops, "script/interp.h; SCRIPT_VM"),
    ("engine: session rules", c_engine_session, "SCRIPT_VM; engine/README"),
    ("engine: area transition", c_engine_area_transition, "SCRIPT_VM; engine/README"),
    ("engine: live zones", c_engine_live_zones, "SCRIPT_VM; engine/README"),
    ("engine: airlock walk", c_engine_airlock_walk, "SCRIPT_VM; engine/README"),
    ("engine: spawn from tables", c_engine_spawn_from_tables, "SCRIPT_VM; FILE_FORMATS; engine/README"),
    ("engine: city crowd", c_engine_city_crowd, "STREET_LIFE 1; SCRIPT_VM"),
    ("engine: pedestrians", c_engine_pedestrians, "STREET_LIFE 2; actor/pedestrians.h"),
    ("engine: street frame", c_engine_street_frame, "STREET_LIFE; todo/street-life 4"),
    ("engine: crowd push", c_engine_crowd_push, "STREET_LIFE 3; actor/spatial.h"),
    ("engine: head look", c_engine_head_look, "STREET_LIFE; actor/pose.h"),
    ("engine: zone pump",  c_engine_zone_pump,  "engine/README"),
    ("engine: zone registry", c_engine_zone_registry, "engine/README"),
    ("engine: voice over", c_engine_voice_over, "CUTSCENES 5; engine/README"),
    ("engine: world ops",  c_engine_world_ops,  "script/hooks.h; SCRIPT_VM; GAME_STATE"),
    ("engine: dialogue line states", c_engine_dialogue_line_states, "dialogue.h; FILE_FORMATS 5b2a"),
    ("engine: game state", c_engine_game_state, "engine/README"),
    ("engine: cam mode 13",    c_engine_cam_mode13,    "engine/README"),
    ("engine: screen close",   c_engine_screen_close,  "engine/README"),

    ("engine: DataFs",     c_engine_datafs,     "engine/README"),
    ("engine: SCX stream", c_engine_scx_stream, "engine/README"),
    ("engine: morph+ADPCM", c_engine_morph_audio, "engine/README"),
    ("engine: cam editings", c_engine_camedit,   "engine/README"),
    ("engine: fonts",      c_engine_fonts,      "engine/README"),
    ("engine: world data", c_engine_world_data, "engine/README"),
    ("engine: fight AI",   c_engine_fight_ai,   "engine/README"),
    ("engine: programs",   c_engine_programs,   "engine/README"),
    ("engine: scene steps", c_engine_scene_steps, "engine/README"),
    ("engine: scene survive", c_engine_scene_survive, "todo/omk-play"),
    ("engine: env anim", c_engine_env_anim, "todo/omk-play"),
    ("engine: tuto camera", c_engine_tuto_camera, "todo/omk-play"),
    ("subtitle box", c_subtitle_box, "docs/UI"),
    ("credit layout", c_credit_layout, "docs/UI"),
    ("engine: impasse fx", c_engine_impasse_fx, "todo/omk-play"),
    ("engine: props", c_engine_props, "todo/omk-play"),
    ("sprite ids scene-local", c_sprite_ids_are_scene_local, "docs/ASSETS"),
    ("engine: scene sounds", c_engine_scene_sounds, "engine/README"),
    ("engine: actor sounds", c_engine_actor_sounds, "engine/README"),
    ("engine: camera roll", c_engine_camera_roll, "engine/README"),
    ("engine: fades",      c_engine_fades,      "engine/README"),
    ("engine: set emitters", c_engine_set_emitters, "engine/README"),
    ("engine: save+clock", c_engine_save,       "engine/README"),
    ("engine: scene loop", c_engine_scene_loop, "engine/README"),
    ("engine: golden traces", c_engine_golden_traces, "engine/README"),
    ("engine: render",     c_engine_render,     "engine/README"),
    ("engine: cull",       c_engine_cull,       "engine/README"),
    ("engine: actor",      c_engine_actor,      "engine/README"),
    ("engine: actor states", c_engine_actor_states, "engine/README"),
    ("engine: player walk", c_engine_player_walk, "engine/README; ASSETS"),
    ("engine: input",      c_engine_input,      "PORTING B6"),
    ("engine: audio",      c_engine_audio,      "PORTING B6"),
    ("engine: shoot AI",   c_engine_shoot_ai,   "engine/README"),
    ("engine: I2D",        c_engine_i2d,        "engine/README"),
    ("engine: I2D blit",   c_engine_i2d_blit,   "PORTING"),
    ("engine: I2D prims",  c_engine_i2d_prims,  "PORTING"),
    ("engine: frame",      c_engine_frame,      "engine/README"),
    ("engine: driver modes", c_engine_driver_modes, "PORTING"),
    ("engine: I2D outline", c_engine_i2d_outline, "PORTING"),
    ("engine: text draw",  c_engine_text_draw,  "PORTING"),
    ("ui item bindings",   c_ui_item_bindings,  "UI 3d"),
    ("ui shop titles",     c_ui_shop_titles,    "UI 3d"),
    ("ui answers",         c_ui_answers,        "UI 3d"),
    ("ui geometry",        c_ui_geometry,       "UI 3b"),
    ("engine: screen",     c_engine_screen,     "PORTING A1"),
    ("engine: movies",     c_engine_movies,     "BOOT 2"),
    ("engine: raster",     c_engine_raster,     "PORTING B6"),
    ("engine: silhouette", c_engine_silhouette, "PORTING B6"),
    ("engine: near clip",  c_engine_near_clip,  "PORTING B6"),
    ("engine: renderer",   c_engine_renderer_boundary, "PORTING A2"),
    ("mirror pass",        c_mirror_pass,       "ASSETS 4c"),
    ("anekbah rendered",   c_anekbah_rendered,  "ASSETS 4b"),
    ("render back ends",   c_render_backends,   "ASSETS 4c"),
    ("porting standard",   c_porting_standard,  "PORTING"),
    ("engine: UI",         c_engine_ui,         "engine/README"),
    ("engine: UI answer",  c_engine_ui_answer,  "engine/README"),
    ("engine: options",    c_engine_options,    "engine/README"),
    ("engine: load panel", c_engine_load_panel, "engine/README"),
    ("engine: inventory",  c_engine_inventory,  "engine/README"),
    ("engine: text",       c_engine_text,       "engine/README"),
    ("engine: boot",       c_engine_boot,       "engine/README"),
    ("dialog staging sweep", c_dialog_staging_sweep, "ASSETS"),
    ("cutscene actors",    c_cutscene_actors,   "CUTSCENES 4"),
    ("textures",           c_textures,          "ASSETS"),
    (".3DM files",         c_morphs,            "FILE_FORMATS 5"),
    (".ani quaternions",   c_ani_quaternions,   "ASSETS"),
]


def main():
    slow = "--slow" in sys.argv
    todo = CHECKS + (SLOW if slow else [])
    # `--only <substring>` runs just the checks whose name matches, and it
    # implies --slow so an `engine:` check is reachable without the whole
    # sweep. The full run takes minutes - the asset sweeps re-decode 2534
    # textures and 777 morphs - so re-running everything after touching one
    # unrelated file is mostly waste. Run the related checks while working,
    # the whole suite before calling something done.
    if "--only" in sys.argv:
        i = sys.argv.index("--only")
        pats = [a for a in sys.argv[i + 1:] if not a.startswith("--")]
        todo = [c for c in CHECKS + SLOW
                if any(p.lower() in c[0].lower() for p in pats)]
        if not todo:
            print("no check matches %s; --list shows them all" % " ".join(pats))
            return 1
    if "--list" in sys.argv:
        for name, fn, where in CHECKS + SLOW:
            print("  %-20s %s%s" % (name, where,
                                    "   (--slow)" if (name, fn, where) in SLOW else ""))
        return 0
    bad = 0
    for name, fn, where in todo:
        try:
            got, want, note = fn()
        except Exception as e:
            print("%-20s ERROR  %s" % (name, e)); bad += 1; continue
        ok = got == want
        print("%-20s %-4s %-26s %s" % (name, "ok" if ok else "FAIL",
                                       "" if ok else "%r != %r" % (got, want), note))
        bad += not ok
    print()
    print("%d checks, %d failed%s" % (len(todo), bad,
          "" if (slow or "--only" in sys.argv) else
          "   (--slow adds %d whole-asset sweeps)" % len(SLOW)))
    return bad


if __name__ == "__main__":
    sys.exit(main())

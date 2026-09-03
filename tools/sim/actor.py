#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The actor: the walker, and the .CTL animation channel - phase 6 stage 5.

Two things, both per-frame, and the plan's two tests are one each.

**The walker.** `Actor_ApplyMotion` -> `Actor_Move` (0x00469580) tries a move,
undoes it, and submits the delta; `Walk_ProbeGround` looks for a floor under
the result and `Walk_GroundResponse` (0x00465460) decides what that floor
means. The rules are all documented (ASSETS, "the walker") and each constant
below is the engine's own:

    gravity integrated on the actor's +220, clamped at 787.4 in/s = 20 m/s
    step height  11.87 in = 30 cm   - a rise up to this is climbed
    step down    refused past the same limit, unless g_IgnoreLedges
    slope limit  30 degrees          - past it the surface does not hold
    fall tiers   118.1 in = 3 m injury, 196.9 in = 5 m death
    NO FLOOR -> the position REVERTS. Nobody walks into the void.

**The .CTL channel.** `Cef_TickChannel` advances the current state's clip and
asks `Cef_FindTransition` (0x004A8BD0, CLEAN) for the edge an input opens: a
candidate must carry an input code, intersect the caller's flag masks, lie
inside its cancel window, and match the held input. A state with no edge taken
runs to the end of its clip and follows its `goto`.

The masks default to -1 (don't care) here, which the signature allows. ASSETS
records the walker passing (2, 784), but neither mask intersects the flags of
`H_STAND`'s own movement edges - `H_SD-WK` is 0x80103001 - so that pair
belongs to some other call site and using it would filter out the very
transitions this is meant to walk. Recorded rather than papered over.

    python3 tools/sim/actor.py                 # the stage-5 tests
    python3 tools/sim/actor.py --walk ARESTO14 # cross one set, step by step
    python3 tools/sim/actor.py --ctl H1Avnt.CTL

**What is NOT modelled, stated rather than buried:** the narrow phase. The
engine sweeps the model's collision spheres against world triangles and slides
along the clamped normals (`Sweep_ActorMove` / `Sweep_MeshTest` /
`Walk_ClampNormal`, 3 passes). Here the ground probe is the whole of collision,
so the actor keeps to the floor but will pass through a wall. Stage 5's stated
test is about the floor - "crosses an area without falling through" - and this
meets it honestly; the wall sweep is the reading still owed.
"""
import math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import omkdata
import anim_ctl
import omkpaths

# every one of these is the engine's, in inches (it authors in metric)
STEP_UP     = 11.87        # 30 cm
STEP_DOWN   = 11.87        # the same limit, refused unless ledges ignored
TERMINAL    = 787.4        # 20 m/s
SLOPE_LIMIT = 30.0         # degrees
FALL_HURT   = 118.1        # 3 m
FALL_KILL   = 196.9        # 5 m
FPS         = 30.0
# The sweep's radius is `f32(sweep, 192)`, and the engine derives it per model:
# the caller takes the LARGEST of the model's collision-sphere radii (the
# stride-16 array at model +260) and scales it by the tunable at 0x910358.
# Both are runtime values this tree does not ship, so 12 inches - about 30 cm,
# a person's shoulder half-width - stands in. It is a parameter of `Walker`,
# not a claim: `radius=0` disables the sweep entirely.
RADIUS      = 12.0


# ------------------------------------------------------- the narrow phase
# `Sweep_ActorMove` (0x004AD360) -> `Sweep_MeshTest` (0x004AD460) ->
# `sub_4A9AB0` -> `sub_4A9D30`, then `Walk_ClampNormal` (0x0046A020), three
# passes. What each does, read from the binary:
#
#   Sweep_ActorMove   builds the swept AABB from the start (+112..120) and end
#                     (+124..132) of the move, the radius (+192) and the two
#                     capsule heights (+376/+380); seeds the hit fraction
#                     (+136) with FLT_MAX and runs `o3de_ForEachMeshInBox`.
#                     Returns whether +136 changed - i.e. whether anything hit.
#   Sweep_MeshTest    skips meshes flagged 0x20000000 or 0x41, transforms the
#                     sweep into mesh-local space (`Matrix3x3_RotateVectorT`
#                     with the mesh matrix at +56), and records the local
#                     start/end, the local up, and the sign of
#                     dot(localUp, moveDir) at +432.
#   sub_4A9AB0        rejects faces by a 6-bit outcode per vertex against the
#                     swept box - a face survives only if the AND of its
#                     vertices' outcodes is 0 - then calls the kernel per
#                     triangle (stride 28) and per quad (stride 32).
#   sub_4A9D30        930 lines: the swept-sphere-against-polygon kernel. It
#                     writes the earliest hit fraction to +136 and the surface
#                     normal to +260.
#   Walk_ClampNormal  NOT a plane slide: a 6-bit mask says which axis
#                     directions are already blocked, the normal's components
#                     in those directions are zeroed, and the result is
#                     renormalised - returning 0 if it collapses.
#
# `Walk_ClampNormal` below is a transcription. The sweep is NOT: the 930-line
# kernel is x87 geometry with no corpus fact to prove a transcription against,
# so what is implemented here is the same algorithm SHAPE - earliest hit of a
# swept sphere, clamp, slide, three passes - over the same collision faces.
# It is enough to stop the actor crossing a wall, which is stage 5's stated
# debt, and it is not the engine's arithmetic. See RECONSTRUCTION phase 6.

SWEEP_EPS = 1e-4


def clamp_normal(mask, high, n):
    """`Walk_ClampNormal` (0x0046A020), transcribed.

    -> (ok, normal). Bits 0..5 (or 16..21 when `high`) are +x -x +y -y +z -z;
    a set bit zeroes that component when it points that way.
    """
    out = list(n)
    b = (16 if high else 0)
    for k in range(3):
        if (mask >> (b + 2 * k)) & 1 and n[k] > 0.0: out[k] = 0.0
        if (mask >> (b + 2 * k + 1)) & 1 and out[k] < 0.0: out[k] = 0.0
    L = (out[0] ** 2 + out[1] ** 2 + out[2] ** 2) ** 0.5
    if L <= 0.00999999987:                       # sqrt(1e-4), the engine's
        return 0, [0.0, 0.0, 0.0]
    return 1, [out[0] / L, out[1] / L, out[2] / L]


def _tri_normal(a, b, c):
    ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]
    vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
    nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
    L = (nx*nx + ny*ny + nz*nz) ** 0.5
    if L <= 0: return None
    return (nx/L, ny/L, nz/L)


def _inside(a, b, c, n, p):
    """Is p, already on the triangle's plane, inside it?"""
    for (u, v) in ((a, b), (b, c), (c, a)):
        ex, ey, ez = v[0]-u[0], v[1]-u[1], v[2]-u[2]
        wx, wy, wz = p[0]-u[0], p[1]-u[1], p[2]-u[2]
        cx, cy, cz = ey*wz-ez*wy, ez*wx-ex*wz, ex*wy-ey*wx
        if cx*n[0] + cy*n[1] + cz*n[2] < 0: return False
    return True


_BOXES = {}


def _boxes(tris):
    """Per-triangle AABB, cached - `Sweep_ActorMove`'s broad phase.

    The engine narrows to the meshes in the swept box (`o3de_ForEachMeshInBox`)
    and then to the faces whose vertex outcodes do not all share a side
    (`Sweep_MeshFaces`). This is the same idea one level flatter, and it is
    what makes the sweep cheap enough to run per frame.
    """
    key = id(tris)
    if key not in _BOXES:
        bs = []
        for i in range(0, len(tris), 9):
            xs = (tris[i], tris[i+3], tris[i+6])
            ys = (tris[i+1], tris[i+4], tris[i+7])
            zs = (tris[i+2], tris[i+5], tris[i+8])
            bs.append((min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
        _BOXES[key] = bs
    return _BOXES[key]


def sweep(tris, p0, delta, radius):
    """Earliest hit of a sphere swept `p0` -> `p0+delta`. -> (t, normal) or None.

    The face case is continuous, as the engine's is: solve for the fraction at
    which the sphere's surface reaches the plane and check the contact point
    lies in the triangle. Edges and vertices are covered by a static overlap
    test at the swept end, which is the part that is a simplification rather
    than the kernel's arithmetic.
    """
    best_t, best_n = None, None
    boxes = _boxes(tris)
    lo = [min(p0[k], p0[k] + delta[k]) - radius for k in range(3)]
    hi = [max(p0[k], p0[k] + delta[k]) + radius for k in range(3)]
    for i in range(0, len(tris), 9):
        bx = boxes[i // 9]                       # the broad phase
        if bx[1] < lo[0] or bx[0] > hi[0]: continue
        if bx[3] < lo[1] or bx[2] > hi[1]: continue
        if bx[5] < lo[2] or bx[4] > hi[2]: continue
        a = tris[i:i+3]; b = tris[i+3:i+6]; c = tris[i+6:i+9]
        n = _tri_normal(a, b, c)
        if n is None: continue
        s0 = sum(n[k] * (p0[k] - a[k]) for k in range(3))
        if s0 < 0:                               # collision is two-sided
            n = (-n[0], -n[1], -n[2]); s0 = -s0
        dn = sum(n[k] * delta[k] for k in range(3))
        if dn >= -SWEEP_EPS: continue            # not closing on this face
        t = (s0 - radius) / -dn
        if t > 1.0: continue
        if t < 0.0:
            if s0 > radius: continue             # behind, not penetrating
            t = 0.0
        hit = [p0[k] + t * delta[k] - n[k] * radius for k in range(3)]
        if not _inside(a, b, c, n, hit): continue
        if best_t is None or t < best_t: best_t, best_n = t, list(n)
    return None if best_t is None else (best_t, best_n)


class Walker:
    r"""One actor on one set. Y points DOWN, as everywhere in this data."""

    def __init__(self, setname, pos, ignore_ledges=False, radius=RADIUS):
        self.geo = omkdata.decor_geometry_cached(setname)
        if not self.geo: raise ValueError("no set %r" % setname)
        self.set = setname
        self.pos = list(pos)
        self.vy = 0.0
        self.fall = 0.0            # accumulated fall distance, actor +280
        self.ignore_ledges = ignore_ledges
        self.log = []
        # the narrow phase's faces: every collision face, not just the ones the
        # ground probe can answer with. radius 0 disables the sweep, which is
        # how the check shows what the sweep is worth.
        self.radius = radius
        self.blockers = omkdata.decor_walls_cached(setname) if radius else None

    def ground(self, x, y, z):
        r"""Walk_ProbeGround: the surface under a point, or None.

        The ray starts ABOVE the feet, not at them - y grows downward, so that
        is `y - STEP_UP`. Casting from the feet exactly finds nothing (the
        probe wants a surface strictly below its origin) and every step reads
        as a hole; casting from a step-height above also picks up a rise the
        actor is allowed to climb, which is the same window the engine's step
        limit describes.
        """
        return omkdata.floor_under(self.geo, [x, y - STEP_UP - 1.0, z])

    def step(self, dx, dz, dt=1.0):
        r"""One frame of Actor_Move + Walk_GroundResponse. -> a verdict string.

        The order is the engine's: try the move, probe under the result, and
        let the ground decide - revert on no floor, refuse a drop past the
        step limit, otherwise snap and keep the fall accounting.
        """
        x, y, z = self.pos
        dx, dz = self.slide(dx, dz)
        if dx == 0.0 and dz == 0.0:
            self.log.append(("swept", 0))
            return "blocked"
        nx, nz = x + dx, z + dz
        g = self.ground(nx, y, nz)
        if g is None:
            self.log.append(("void", round(nx), round(nz)))
            return "reverted"                      # nobody walks into the void

        rise = y - g                # y is DOWN: g < y means the floor is higher
        if rise > STEP_UP:
            self.log.append(("wall", round(rise, 1)))
            return "blocked"                       # too high to step onto
        drop = g - y
        if drop > STEP_DOWN and not self.ignore_ledges:
            self.log.append(("ledge", round(drop, 1)))
            return "refused"                       # a ledge, and ledges obeyed

        self.pos = [nx, g, nz]
        if drop > STEP_DOWN:
            self.fall += drop
            self.vy = min(TERMINAL, self.vy + drop)
        else:
            self.fall = 0.0
            self.vy = 0.0
        return "moved"

    def slide(self, dx, dz):
        r"""The narrow phase: sweep, clamp, slide - three passes.

        `Sweep_ActorMove` reports the earliest hit and its normal; the caller
        rotates that normal to world space and hands it to `Walk_ClampNormal`,
        and the move is retried against what is left. The engine runs the pair
        three times, which is what lets an actor take an inside corner: one
        pass to stop at the first wall, a second to slide along it, a third to
        stop at the wall the slide ran into.

        The sweep keeps the actor's own y - only the horizontal move is
        collided here, because the vertical is the ground probe's job.
        """
        if not self.blockers: return dx, dz
        p = [self.pos[0], self.pos[1] - self.radius, self.pos[2]]
        for _pass in range(3):
            if dx == 0.0 and dz == 0.0: return 0.0, 0.0
            hit = sweep(self.blockers, p, [dx, 0.0, dz], self.radius)
            if hit is None: return dx, dz
            t, n = hit
            # advance to the contact, then slide the remainder along the plane
            p = [p[0] + dx * t, p[1], p[2] + dz * t]
            rem = [dx * (1.0 - t), 0.0, dz * (1.0 - t)]
            # `Walk_ClampNormal`'s mask is the ACTOR's accumulated
            # blocked-direction state, which this does not model - synthesising
            # one per contact was wrong and is what made a single wall cancel
            # the move: marking both an x and a z direction blocked on every
            # hit collapsed the slide to nothing. Passing 0 leaves the normal
            # as the surface gave it, and the slide is the projection.
            ok, cn = clamp_normal(0, 0, n)
            if not ok: return 0.0, 0.0
            drop = sum(cn[k] * rem[k] for k in range(3))
            dx = rem[0] - drop * cn[0]
            dz = rem[2] - drop * cn[2]
            self.log.append(("slide", round(t, 3)))
        return dx, dz

    def grade_fall(self):
        """Walk_GroundResponse's landing tiers."""
        if self.fall >= FALL_KILL: return "death"
        if self.fall >= FALL_HURT: return "injury"
        return "fine"


def area_start(area):
    """An authored position inside one area: its first ADDRESSES entry.

    Somewhere the game's own `actor.goto_address` puts an actor, so it is a
    place a person can stand by construction."""
    import struct as _s, dialog_triggers as _T
    b = _T.archive(omkpaths.data("IAM/AREA")).get(area)
    if b is None or len(b) < 84: return None
    lo = _s.unpack_from("<i", b, 60)[0]
    n = _s.unpack_from("<h", b, 82)[0]
    if n <= 0 or lo <= 0: return None
    conv = lambda v: v * 100 * 0.00390625 * 0.3937007874015748 - 1.0
    x, y, z = _s.unpack_from("<3i", b, lo)
    return [conv(x), conv(y), conv(z)]


NAV_STEP = 16.0


def nav_route(setname, start, target, bounds=None, limit=4000):
    r"""A walkable route from `start` to `target`, derived not invented.

    Where the player walked is player input and is not in the data. What IS in
    the data is where he *could* walk, and this asks the walker itself: a
    breadth-first search over a 16-unit grid whose edges are legal only when
    `Walker.step` returns "moved" **and lands in the cell it aimed at**. That
    second half matters - without it a step that slides off along a wall counts
    as reaching the neighbour, and the search returns routes that walk through
    buildings. It did, before the check was added.

    So the route is the shortest one the walker can actually take, which is a
    property of the collision geometry rather than a guess about the player.
    -> [(x, z), ...] or None.
    """
    import collections
    geo = omkdata.decor_geometry_cached(setname)
    if not geo: return None
    lo, hi = geo["min"], geo["max"]
    x0, z0 = lo[0], lo[2]
    cell = lambda x, z: (int((x - x0) // NAV_STEP), int((z - z0) // NAV_STEP))
    cpos = lambda c: (x0 + c[0] * NAV_STEP + NAV_STEP / 2,
                      z0 + c[1] * NAV_STEP + NAV_STEP / 2)
    s0, g = cell(start[0], start[2]), cell(target[0], target[1])
    seen = {s0: (start[1], None)}
    q = collections.deque([s0])
    while q and len(seen) < limit:
        c = q.popleft()
        y = seen[c][0]
        if c == g: break
        px, pz = cpos(c)
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + dx, c[1] + dz)
            if n in seen: continue
            nx, nz = cpos(n)
            if not (lo[0] <= nx <= hi[0] and lo[2] <= nz <= hi[2]): continue
            w = Walker(setname, [px, y, pz], ignore_ledges=True)
            if w.step(nx - px, nz - pz) != "moved": continue
            if cell(w.pos[0], w.pos[2]) != n: continue      # it must ARRIVE
            seen[n] = (w.pos[1], c); q.append(n)
    if g not in seen: return None
    out, c = [], g
    while c is not None:
        out.append(cpos(c)); c = seen[c][1]
    return out[::-1]


def find_partition(setname, minarea=10000.0):
    """The biggest wall in a set that has FLOOR ON BOTH SIDES.

    That qualifier is the whole point. Most walls in a set have no floor
    behind them, so the ground probe alone already refuses to cross them and a
    test there would pass without a narrow phase at all. A partition standing
    on continuous floor is where the sweep is the only thing saying no.
    -> (centroid, normal) or None.
    """
    tris = omkdata.decor_blockers_cached(setname)
    geo = omkdata.decor_geometry_cached(setname)
    if not tris or not geo: return None
    best = None
    for i in range(0, len(tris), 9):
        a, b, c = tris[i:i+3], tris[i+3:i+6], tris[i+6:i+9]
        n = _tri_normal(a, b, c)
        if not n or abs(n[1]) > 0.2: continue          # want a vertical face
        cen = [(a[k] + b[k] + c[k]) / 3.0 for k in range(3)]
        f1 = omkdata.floor_under(geo, [cen[0] + n[0]*50, cen[1]-60, cen[2] + n[2]*50])
        f2 = omkdata.floor_under(geo, [cen[0] - n[0]*50, cen[1]-60, cen[2] - n[2]*50])
        if f1 is None or f2 is None or abs(f1 - f2) > 12: continue
        # and it must actually stand in the actor's way: the face's y range has
        # to contain the sphere's centre. Without this the pick can land on a
        # face BELOW the floor, which the actor walks over - and a signed
        # distance taken in x/z alone then reads that as passing through it,
        # which is the measurement lying rather than the sweep failing.
        ys = [a[1], b[1], c[1]]
        if not (min(ys) <= f1 - RADIUS <= max(ys)): continue
        ux, uy, uz = [b[k]-a[k] for k in range(3)]
        vx, vy, vz = [c[k]-a[k] for k in range(3)]
        cr = (uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx)
        area = 0.5 * sum(x*x for x in cr) ** 0.5
        if area < minarea: continue
        if best is None or area > best[0]: best = (area, cen, n, f1)
    return None if best is None else (best[1], best[2], best[3])


def wall_test(setname="ARESTO14", standoff=100.0, steps=40, stride=6.0):
    r"""Walk straight at a partition, with the sweep off and on.

    The measurement is the signed distance from the wall's plane at the end:
    positive is in front, negative means the actor is INSIDE the room it
    should not be in. Stage 5 shipped with the ground probe as the whole of
    collision, and this is exactly what that cost.

    -> {"without": d, "with": d, "verdict": the last step's verdict}
    """
    found = find_partition(setname)
    if not found: return None
    cen, n, floor = found
    start = [cen[0] + n[0]*standoff, floor, cen[2] + n[2]*standoff]
    out = {}
    for key, r in (("without", 0.0), ("with", RADIUS)):
        w = Walker(setname, start, ignore_ledges=True, radius=r)
        v = "n/a"
        for _ in range(steps):
            v = w.step(-n[0]*stride, -n[2]*stride)
        out[key] = round(sum(n[k] * (w.pos[k] - cen[k]) for k in (0, 2)), 1)
        out[key + "Verdict"] = v
    return out


def cross(setname, start=None, steps=400, stride=25.0, radius=0.0):
    r"""Walk a spiral across a set and report what the floor did.

    `start` is an authored world position - an `ADDRESSES` entry, somewhere the
    game itself teleports an actor - so the run is about the walker rather than
    about where it happened to be dropped.

    `radius` is **0 by default, deliberately**: this is stage 5's GROUND test,
    and it asks whether the probe holds the actor on the floor over a long
    traversal. Turning the narrow phase on changes the path - the actor stops
    at walls it used to cross - so it would be answering a different question
    with the same numbers. The sweep has its own test, `wall_test`.
    """
    geo = omkdata.decor_geometry_cached(setname)
    if not geo: return None
    if start is None: return None
    cx, sy, cz = start
    # Probe DOWN from the authored height. Probing from -1e6 finds the nearest
    # surface below that, which is the ceiling - the first version of this
    # started the walker on the roof of the restaurant and then refused every
    # step as a 176-unit drop onto the real floor.
    y = omkdata.floor_under(geo, [cx, sy, cz])
    if y is None: return None

    w = Walker(setname, (cx, y, cz), radius=radius)
    verdicts = {}
    ys = [y]
    for i in range(steps):
        a = 2 * math.pi * i / 64.0                 # a slow spiral, so the walk
        dx, dz = stride * math.cos(a), stride * math.sin(a)   # stays in the room
        v = w.step(dx, dz)
        verdicts[v] = verdicts.get(v, 0) + 1
        ys.append(w.pos[1])
    return {"set": setname, "steps": steps, "verdicts": verdicts,
            "start": (round(cx), round(y), round(cz)),
            "end": tuple(round(v) for v in w.pos),
            "ySpan": (round(min(ys)), round(max(ys))),
            "fell": w.grade_fall()}


# ------------------------------------------------------------ the .CTL channel
class Channel:
    r"""Cef_TickChannel over one .CTL: a current state, a frame, and edges."""

    def __init__(self, fn):
        self.w = anim_ctl.walk(omkpaths.data("ANIMS", fn))
        self.file = fn
        self.byid = {s["id"]: s for s in self.w["states"]}
        self.frames = {}
        import anim_ani
        for i, c in enumerate(self.w["clips"]):
            d = anim_ani.descriptor(self.w["data"], c["offset"])
            self.frames[i] = (d["frames"] if d else 0)
        self.state = None
        self.frame = 0.0
        self.trail = []

    def field(self, st, off, fmt="<I"):
        return struct.unpack_from(fmt, self.w["data"], st["offset"] + off)[0]

    def entry(self, st):
        """The transition fields Cef_FindTransition reads off an entry."""
        d = self.w["data"]; e = st["offset"]
        code = struct.unpack_from("<I", d, e + 4)[0]
        cs, ce = struct.unpack_from("<2f", d, e + 16)
        pri = struct.unpack_from("<H", d, e + 84)[0]
        return code, cs, ce, pri, st["flags"]

    def enter(self, st):
        self.state = st
        self.frame = 0.0
        self.trail.append(st["name"])

    def start(self, name):
        for s in self.w["states"]:
            if s["name"] == name: self.enter(s); return True
        return False

    def find_transition(self, code, must=-1, also=-1, window=True):
        """Cef_FindTransition, children first, highest priority wins."""
        st = self.state
        best = None
        kids = st["children"]
        if st["flags"] & 0x20: kids = list(reversed(kids))
        for cid in kids:
            cand = self.byid.get(cid)
            if not cand: continue
            ccode, cs, ce, pri, flags = self.entry(cand)
            if not ccode: continue
            if must != -1 and not (flags & must): continue
            if also != -1 and not (flags & also): continue
            if (cs or ce) and window:
                if self.frame < cs or self.frame > ce: continue
            hit = (ccode == code) if (flags & 0x80000) else \
                  (ccode == 0x80000000 and code == 0) or bool(ccode & code)
            if not hit: continue
            if best is None or pri > best[1]: best = (cand, pri)
        return best[0] if best else None

    def tick(self, code=0, dt=1.0):
        """One frame: advance the clip, take an edge, or follow the goto."""
        if not self.state: return None
        nxt = self.find_transition(code)
        if nxt is not None and nxt is not self.state:
            self.enter(nxt); return "transition"
        self.frame += dt
        n = self.frames.get(self.state["clip"], 0)
        if n and self.frame >= n:
            g = self.byid.get(self.state["goto"]) if self.state["goto"] else None
            if g: self.enter(g); return "goto"
            self.frame = 0.0
            return "loop"
        return "playing"


def ctl_sequence(fn="H1Avnt.CTL", start="H_STAND", press=0x04, hold=1,
                 frames=200):
    r"""Press an input, then let the channel run, and check the trail.

    `0x04` is the code on `H_STAND`'s `H_SD-WK` edge - start walking - so the
    expected trail is the graph's own: H_STAND -> H_SD-WK -> (its goto)
    H_WALK. Every consecutive pair must be an edge the file actually carries,
    either a child or the state's `goto`. That is the plan's "matches the
    state graph": not "it moved", but "it only ever moved along an edge".
    """
    ch = Channel(fn)
    if not ch.start(start): return None
    for i in range(frames):
        ch.tick(press if i < hold else 0)
    ok = True
    for a, b in zip(ch.trail, ch.trail[1:]):
        sa = next(s for s in ch.w["states"] if s["name"] == a)
        sb = next(s for s in ch.w["states"] if s["name"] == b)
        if sb["id"] not in sa["children"] and sa["goto"] != sb["id"]:
            ok = False; break
    return {"file": fn, "from": start, "trail": ch.trail, "legal": ok,
            "states": len(ch.w["states"]), "steps": len(ch.trail)}


def main():
    if "--walk" in sys.argv:
        i = sys.argv.index("--walk")
        print(cross(sys.argv[i + 1] if len(sys.argv) > i + 1 else "ARESTO14"))
        return 0
    if "--ctl" in sys.argv:
        i = sys.argv.index("--ctl")
        r = ctl_sequence(sys.argv[i + 1] if len(sys.argv) > i + 1 else "H1Avnt.CTL")
        print(r); return 0

    print("stage 5 - the actor")
    c = cross("ARESTO14", area_start(217))
    print("  walking ARESTO14: %d steps from %s to %s"
          % (c["steps"], c["start"], c["end"]))
    print("    verdicts: %s" % c["verdicts"])
    print("    floor height stayed within %s, landing: %s"
          % (c["ySpan"], c["fell"]))
    s = ctl_sequence()
    print("  %s from %s: %d states, trail %s"
          % (s["file"], s["from"], s["states"], " -> ".join(s["trail"][:6])))
    print("    every step is an edge the graph carries: %s" % s["legal"])
    ok = (c["verdicts"].get("moved", 0) > 100 and c["ySpan"][0] == c["ySpan"][1]
          and c["fell"] == "fine" and s["legal"] and s["steps"] >= 3)
    print("stage 5: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

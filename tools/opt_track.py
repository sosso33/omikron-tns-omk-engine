#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""TRAJECTOIRES\*.OPT - a city's traffic circuit: the lanes the procedural
pedestrians and the hover-taxis move on (docs/STREET_LIFE.md 2).

Read from `Slider_Init` (0x00453450), which relocates the header, and from
the movers that consume it: `sub_453B40` (the spawner along the lanes),
`sub_454F40` / `sub_455830` (the walk), `sub_455570` / `sub_4555C0` /
`sub_4554B0` (key and step stepping), `sub_455680` (lane and route changes),
`sub_453230` (the reservation groups). Every field below is named for what one
of those does with it; a field none of them reads is marked `?`.

The header is 19 dwords (76 bytes) - a magic, three lane bounds, two spacing
units, then (offset, count) pairs for seven blocks, each block starting exactly
where the previous one ends and the last ending on the file size:

    [0]  magic "V1.0"
    [1]  first pedestrian lane        [2]  end of the pedestrian lanes
    [3]  pedestrian spacing unit      [4]  vehicle spacing unit
    [5]  lane count (the vehicle lanes are [2]..[5])
    [6]  offset  lanes    x [5]   24 bytes    [7]  count / [8]  offset  keys   20 bytes
    [9]  count / [10] offset actions 20 bytes  [11] count / [12] offset routes  12 bytes
    [13] count / [14] offset steps   16 bytes  [15] count / [16] offset groups   4 bytes
    [17] count / [18] offset lists    2 bytes  [19] ?  (a stamp; differs per file)

    lane   +0 float[3] origin  +12 runtime list head (0 on disk)  +16 i16 first key
           +18 i16 first route  +20 i8 route count (0 reads as 1)  +21 i8 key count
    key    +0 runtime list head  +4 float[3] delta to the next key  +16 i16 action (-1 none)
    action +0 float[3] the point, relative to the mover when the key is reached
           +12 float facing to turn to there, degrees  +16 i16 animation TYPE (0 = none)
           +18 i8 a count 1..100 handed to the play phase (sub_456250, unread)  +19 i8 = 1
    route  +0 runtime list head  +4 i16 destination lane  +6 i16 first step
           +8 i16 reservation group (-1 none)  +10 i8 step count
    step   +0 float[3] delta  +12 i16 reservation group (-1 none)
    group  +0 i16 first list entry  +2 i8 entry count  +3 u8 runtime busy count
    list   i16 group index

A mover walks a lane key by key (each key a delta, the lane's origin the
start), and at the last key takes one of the lane's routes - chosen round-robin
over the lane's route count - whose steps are waypoints; after the last step
(or at once, for a route with none) it walks a straight line to the
destination lane's origin and snaps onto it (`sub_454F40`,
`key index == step count`). So routes owe no geometric closure, and the
self-checks are the layout, the references, the runtime fields being zero on
disk, and that no route crosses from the pedestrian lanes to the vehicle lanes
or back. A route or a step naming a reservation group waits while any group in
that group's list is busy and marks them busy while it is inside. A key naming
an action point sends every second mover reaching it (a global counter's low
bit) to the point: it walks there, turns to the facing, plays a clip of the
type, and comes back.

    python3 tools/opt_track.py                 # every shipped file, the walk
    python3 tools/opt_track.py anekbah --lanes # one file, its lanes listed
"""
import os, struct, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import omkpaths

HEADER = 19
SIZES = {"lanes": 24, "keys": 20, "actions": 20, "routes": 12, "steps": 16, "groups": 4, "lists": 2}
ORDER = ["lanes", "keys", "actions", "routes", "steps", "groups", "lists"]


def load(path):
    b = open(path, "rb").read()
    h = struct.unpack_from("<%dI" % (HEADER + 1), b, 0)
    t = {"file": path, "size": len(b), "magic": b[:4].decode("ascii", "replace"),
         "pedFirst": h[1], "pedEnd": h[2], "pedSpacing": h[3], "vehSpacing": h[4],
         "laneCount": h[5], "stamp": h[19], "blocks": {}}
    counts = {"lanes": h[5], "keys": h[7], "actions": h[9], "routes": h[11],
              "steps": h[13], "groups": h[15], "lists": h[17]}
    offs = {"lanes": h[6], "keys": h[8], "actions": h[10], "routes": h[12],
            "steps": h[14], "groups": h[16], "lists": h[18]}
    for name in ORDER:
        t["blocks"][name] = (offs[name], counts[name])
    L, o = [], offs["lanes"]
    for i in range(counts["lanes"]):
        x, y, z, head, k0, r0, nr, nk = struct.unpack_from("<3fihhbb", b, o + 24 * i)
        L.append({"origin": (x, y, z), "head": head, "firstKey": k0, "firstRoute": r0,
                  "routeCount": nr, "keyCount": nk})
    K, o = [], offs["keys"]
    for i in range(counts["keys"]):
        head, dx, dy, dz, act, pad = struct.unpack_from("<i3fhh", b, o + 20 * i)
        K.append({"head": head, "delta": (dx, dy, dz), "action": act, "pad": pad})
    A, o = [], offs["actions"]
    for i in range(counts["actions"]):
        x, y, z, ang, typ, cnt, one = struct.unpack_from("<3ffhbb", b, o + 20 * i)
        A.append({"offset": (x, y, z), "facing": ang, "type": typ, "count": cnt, "one": one})
    R, o = [], offs["routes"]
    for i in range(counts["routes"]):
        head, dest, s0, grp, ns, pad = struct.unpack_from("<ihhhbb", b, o + 12 * i)
        R.append({"head": head, "dest": dest, "firstStep": s0, "group": grp, "stepCount": ns, "pad": pad})
    S, o = [], offs["steps"]
    for i in range(counts["steps"]):
        dx, dy, dz, grp, pad = struct.unpack_from("<3fhh", b, o + 16 * i)
        S.append({"delta": (dx, dy, dz), "group": grp, "pad": pad})
    G, o = [], offs["groups"]
    for i in range(counts["groups"]):
        first, n, busy = struct.unpack_from("<hbB", b, o + 4 * i)
        G.append({"first": first, "count": n, "busy": busy})
    o = offs["lists"]
    I = list(struct.unpack_from("<%dh" % counts["lists"], b, o)) if counts["lists"] else []
    t.update(lanes=L, keys=K, actions=A, routes=R, steps=S, groups=G, lists=I)
    return t


def lane_end(t, li):
    """The lane's origin plus every key delta - where a mover leaves it."""
    L = t["lanes"][li]
    x, y, z = L["origin"]
    for k in range(L["keyCount"]):
        dx, dy, dz = t["keys"][L["firstKey"] + k]["delta"]
        x += dx; y += dy; z += dz
    return (x, y, z)


def check(t):
    """-> dict of counters; every one but `ok` and the totals must be 0."""
    c = {"ok": 0, "layout": 0, "runtime_nonzero": 0, "lane_keys_out": 0, "lane_routes_out": 0,
         "route_dest_out": 0, "route_steps_out": 0, "key_action_out": 0, "group_out": 0,
         "list_out": 0, "routes": 0, "cross_class": 0, "self_routes": 0, "leg_median": 0.0,
         "leg_max": 0.0, "ped_lanes": 0, "veh_lanes": 0, "actions_typed": 0}
    if t["magic"] != "V1.0": c["layout"] += 1
    o = HEADER * 4
    for name in ORDER:
        off, n = t["blocks"][name]
        if off != o: c["layout"] += 1
        o = off + SIZES[name] * n
    if o != t["size"]: c["layout"] += 1
    if not (0 <= t["pedFirst"] <= t["pedEnd"] <= t["laneCount"]): c["layout"] += 1
    nK, nA, nR, nS, nG, nI = (len(t[k]) for k in ("keys", "actions", "routes", "steps", "groups", "lists"))
    for L in t["lanes"]:
        if L["head"]: c["runtime_nonzero"] += 1
        if not (0 <= L["firstKey"] and L["firstKey"] + L["keyCount"] <= nK): c["lane_keys_out"] += 1
        if not (0 <= L["firstRoute"] and L["firstRoute"] + max(L["routeCount"], 1) <= nR): c["lane_routes_out"] += 1
    for K in t["keys"]:
        if K["head"]: c["runtime_nonzero"] += 1
        if not (-1 <= K["action"] < nA): c["key_action_out"] += 1
    for A in t["actions"]:
        if A["type"]: c["actions_typed"] += 1
    for R in t["routes"]:
        if R["head"]: c["runtime_nonzero"] += 1
        if not (0 <= R["dest"] < t["laneCount"]): c["route_dest_out"] += 1
        if not (0 <= R["firstStep"] and R["firstStep"] + R["stepCount"] <= nS): c["route_steps_out"] += 1
        if not (-1 <= R["group"] < nG): c["group_out"] += 1
    for S in t["steps"]:
        if not (-1 <= S["group"] < nG): c["group_out"] += 1
    for G in t["groups"]:
        if G["busy"]: c["runtime_nonzero"] += 1
        if not (0 <= G["first"] and G["first"] + G["count"] <= nI): c["list_out"] += 1
    for i in t["lists"]:
        if not (0 <= i < nG): c["list_out"] += 1
    c["ped_lanes"] = t["pedEnd"] - t["pedFirst"]
    c["veh_lanes"] = t["laneCount"] - t["pedEnd"]
    # the routes: same class both ends; and the implicit last leg's length,
    # reported so a reader can see the waypoints do most of the work
    legs = []
    for li, L in enumerate(t["lanes"]):
        ped = t["pedFirst"] <= li < t["pedEnd"]
        ex, ey, ez = lane_end(t, li)
        for r in range(max(L["routeCount"], 1)):
            ri = L["firstRoute"] + r
            if not (0 <= ri < nR): continue
            R = t["routes"][ri]
            if not (0 <= R["dest"] < t["laneCount"]): continue
            c["routes"] += 1
            if ped != (t["pedFirst"] <= R["dest"] < t["pedEnd"]): c["cross_class"] += 1
            if R["dest"] == li: c["self_routes"] += 1
            x, y, z = ex, ey, ez
            for s in range(R["stepCount"]):
                si = R["firstStep"] + s
                if not (0 <= si < nS): break
                dx, dy, dz = t["steps"][si]["delta"]
                x += dx; y += dy; z += dz
            ox, oy, oz = t["lanes"][R["dest"]]["origin"]
            legs.append(((x - ox) ** 2 + (y - oy) ** 2 + (z - oz) ** 2) ** 0.5)
    if legs:
        legs.sort(); c["leg_median"] = legs[len(legs) // 2]; c["leg_max"] = legs[-1]
    c["ok"] = int(all(c[k] == 0 for k in ("layout", "runtime_nonzero", "lane_keys_out", "lane_routes_out",
                                          "route_dest_out", "route_steps_out", "key_action_out",
                                          "group_out", "list_out", "cross_class")))
    return c


def shipped():
    return sorted(glob.glob(omkpaths.data("TRAJECTOIRES", "*.opt")) +
                  glob.glob(omkpaths.data("TRAJECTOIRES", "*.OPT")))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    files = shipped() if not args else [p for p in shipped() if os.path.basename(p).lower().startswith(args[0].lower())]
    for p in files:
        t = load(p)
        c = check(t)
        print("%-12s %6d bytes  lanes %3d (ped %d..%d, veh %d)  keys %4d actions %3d (typed %d) routes %3d steps %3d groups %3d lists %3d  spacing %d/%d  cross-class %d self %d last leg median %.0f max %.0f  %s" % (
            os.path.basename(p), t["size"], t["laneCount"], t["pedFirst"], t["pedEnd"], c["veh_lanes"],
            len(t["keys"]), len(t["actions"]), c["actions_typed"], len(t["routes"]), len(t["steps"]),
            len(t["groups"]), len(t["lists"]), t["pedSpacing"], t["vehSpacing"], c["cross_class"], c["self_routes"],
            c["leg_median"], c["leg_max"],
            "ok" if c["ok"] else "FAIL " + str({k: v for k, v in c.items() if v and k not in ("ok", "routes", "self_routes", "ped_lanes", "veh_lanes", "actions_typed", "leg_median", "leg_max")})))
        if "--lanes" in sys.argv:
            for li, L in enumerate(t["lanes"]):
                e = lane_end(t, li)
                print("   lane %3d %s origin (%.0f %.0f %.0f) keys %d from %d  end (%.0f %.0f %.0f)  routes %d from %d -> %s" % (
                    li, "ped" if t["pedFirst"] <= li < t["pedEnd"] else "veh", *L["origin"], L["keyCount"], L["firstKey"], *e,
                    max(L["routeCount"], 1), L["firstRoute"],
                    [t["routes"][L["firstRoute"] + r]["dest"] for r in range(max(L["routeCount"], 1)) if L["firstRoute"] + r < len(t["routes"])]))

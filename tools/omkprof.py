#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The PROFILER'S READER - `todo/debug-tools.md`.

`omk-play --profile run.prof` writes a capture (the format is in
`engine/src/platform/profile.h`); this reads it, outside the game, so the
analysis costs the game nothing.

    python3 tools/omkprof.py run.prof               the summary: frame times,
                                                    and the zones by total time
    python3 tools/omkprof.py run.prof --frame 120   one frame's call tree, each
                                                    zone's total and SELF time
    python3 tools/omkprof.py run.prof --slowest 5   the five slowest frames' trees

Zones nest by the depth and order the recorder wrote them: a zone's parent
is the nearest zone before it of one less depth. SECTIONS (the viewer's
marks, `OMK_SECTION`) partition the frame flat and may cross zones, so they
are summed beside the tree, never in it.
"""
import struct
import sys

MAGIC = b"OMKPROF1"


def read(path):
    """-> (names, frames). frames: a list of dicts with `frame`, `start` and
    `dur` (us), `dropped`, `zones` [(name, depth, start, dur)] and `sections`
    [(name, start, dur)], in microseconds from the frame's start. A capture cut off mid-chunk (the game
    still running, or killed) ends at the last whole chunk."""
    data = open(path, "rb").read()
    if data[:8] != MAGIC:
        raise ValueError("%s: not an OMK profiler capture" % path)
    version = struct.unpack_from("<I", data, 8)[0]
    if version != 1:
        raise ValueError("%s: capture version %d, this reader knows 1" % (path, version))
    names, frames = {}, []
    o = 12
    while o + 5 <= len(data):
        typ, n = struct.unpack_from("<BI", data, o)
        if o + 5 + n > len(data):
            break                                   # a chunk still being written
        p = data[o + 5:o + 5 + n]
        if typ == 1:
            names[struct.unpack_from("<I", p, 0)[0]] = p[4:].decode("latin-1")
        elif typ == 2:
            fr, start, dur, nz, dropped = struct.unpack_from("<iQIII", p, 0)
            zones, sections = [], []
            q = 24
            for _ in range(nz):
                nid, depth, kind, zs, zd = struct.unpack_from("<IHHII", p, q)
                name = names.get(nid, "?%d" % nid)
                if kind == 1:
                    sections.append((name, zs, zd))
                else:
                    zones.append((name, depth, zs, zd))
                q += 16
            if q != len(p):
                raise ValueError("frame %d: %d bytes of zones, %d in the chunk" % (fr, q - 24, len(p) - 24))
            frames.append({"frame": fr, "start": start, "dur": dur, "dropped": dropped,
                           "zones": zones, "sections": sections})
        o += 5 + n
    return names, frames


def tree(frame):
    """The frame's zones as a tree: -> the root node (the frame's own zone),
    each node {name, start, dur, self, kids}. A zone's parent is the nearest
    zone before it of one less depth - the order and depth the recorder saw."""
    root = None
    stack = []                                   # stack[d] = the open node at depth d
    for name, depth, start, dur in frame["zones"]:
        node = {"name": name, "start": start, "dur": dur, "kids": []}
        if root is None:
            root = node
        else:
            if depth < 1 or depth > len(stack):
                raise ValueError("frame %d: zone %r at depth %d under %d open" %
                                 (frame["frame"], name, depth, len(stack)))
            stack[depth - 1]["kids"].append(node)
        del stack[depth:]
        stack.append(node)
    def selfTime(nd):
        nd["self"] = max(0, nd["dur"] - sum(k["dur"] for k in nd["kids"]))
        for k in nd["kids"]:
            selfTime(k)
    if root:
        selfTime(root)
    return root


def printTree(node, depth=0, out=sys.stdout, minUs=0):
    if node["dur"] < minUs and depth > 0:
        return
    out.write("%s%-*s %9.2f ms  self %8.2f ms\n" % ("  " * depth, 44 - 2 * depth, node["name"],
                                                    node["dur"] / 1000.0, node["self"] / 1000.0))
    for k in node["kids"]:
        printTree(k, depth + 1, out, minUs)


def summary(frames, out=sys.stdout):
    play = [f for f in frames if f["frame"] >= 0]
    out.write("%d frames (+ the setup)\n" % len(play))
    if any(f["frame"] < 0 for f in frames):
        out.write("setup: %.1f ms\n" % (next(f for f in frames if f["frame"] < 0)["dur"] / 1000.0))
    if not play:
        return
    ds = sorted(f["dur"] for f in play)
    out.write("frame time: median %.2f ms, 90%% %.2f, max %.2f (frame %d)\n" % (
        ds[len(ds) // 2] / 1000.0, ds[int(len(ds) * 0.9)] / 1000.0, ds[-1] / 1000.0,
        max(play, key=lambda f: f["dur"])["frame"]))
    # every zone's total and self time over the frames, by name
    tot, own, cnt = {}, {}, {}
    def walk(nd):
        tot[nd["name"]] = tot.get(nd["name"], 0) + nd["dur"]
        own[nd["name"]] = own.get(nd["name"], 0) + nd["self"]
        cnt[nd["name"]] = cnt.get(nd["name"], 0) + 1
        for k in nd["kids"]:
            walk(k)
    for f in play:
        t = tree(f)
        if t:
            walk(t)
    out.write("\n%-44s %12s %12s %8s\n" % ("zone (mean per frame)", "total ms", "self ms", "calls"))
    for name in sorted(own, key=lambda k: -own[k])[:40]:
        out.write("%-44s %12.2f %12.2f %8.1f\n" % (name, tot[name] / 1000.0 / len(play),
                                                   own[name] / 1000.0 / len(play), cnt[name] / len(play)))
    secT = {}
    for f in play:
        for name, _s, d in f["sections"]:
            secT[name] = secT.get(name, 0) + d
    if secT:
        out.write("\n%-44s %12s\n" % ("section (the viewer's marks; mean per frame)", "ms"))
        for name in sorted(secT, key=lambda k: -secT[k])[:25]:
            out.write("%-44s %12.2f\n" % (name, secT[name] / 1000.0 / len(play)))
    dropped = sum(f["dropped"] for f in frames)
    if dropped:
        out.write("\n%d zones DROPPED (a frame's buffer full)\n" % dropped)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    _, frames = read(argv[1])
    if "--frame" in argv:
        want = int(argv[argv.index("--frame") + 1])
        f = next((f for f in frames if f["frame"] == want), None)
        if not f:
            print("no frame %d" % want)
            return 1
        printTree(tree(f))
    elif "--slowest" in argv:
        k = int(argv[argv.index("--slowest") + 1])
        for f in sorted((f for f in frames if f["frame"] >= 0), key=lambda f: -f["dur"])[:k]:
            print("--- frame %d" % f["frame"])
            printTree(tree(f), minUs=100)
    else:
        summary(frames)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

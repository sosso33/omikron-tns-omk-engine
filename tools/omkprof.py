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
    python3 tools/omkprof.py run.prof --serve 8753  the PAGE (tools/omkprof.html):
                                                    the frame graph, a frame's
                                                    tree, live while the game runs

Zones nest by the depth and order the recorder wrote them: a zone's parent
is the nearest zone before it of one less depth. SECTIONS (the viewer's
marks, `OMK_SECTION`) partition the frame flat and may cross zones, so they
are summed beside the tree, never in it.
"""
import os
import struct
import sys

MAGIC = b"OMKPROF1"


class Capture:
    """A capture read INCREMENTALLY: `poll()` parses the whole chunks written
    since the last call, so a live capture (the game still running) is
    followed without re-reading it - what the page's server does each request.
    `names` {id: name}; `frames` as `read` returns them."""

    def __init__(self, path):
        self.path, self.names, self.frames, self.offset = path, {}, [], 0

    def poll(self):
        with open(self.path, "rb") as f:
            f.seek(self.offset)
            data = f.read()
        if self.offset == 0:
            if len(data) < 12:
                return 0
            if data[:8] != MAGIC:
                raise ValueError("%s: not an OMK profiler capture" % self.path)
            version = struct.unpack_from("<I", data, 8)[0]
            if version != 1:
                raise ValueError("%s: capture version %d, this reader knows 1" % (self.path, version))
            used = parseChunks(data, 12, self.names, self.frames)
        else:
            used = parseChunks(data, 0, self.names, self.frames)
        before = self.offset
        self.offset += used
        return self.offset - before


def read(path):
    """-> (names, frames). frames: a list of dicts with `frame`, `start` and
    `dur` (us), `dropped`, `zones` [(name, depth, start, dur)] and `sections`
    [(name, start, dur)], in microseconds from the frame's start. A capture
    cut off mid-chunk (the game still running, or killed) ends at the last
    whole chunk."""
    c = Capture(path)
    c.poll()
    return c.names, c.frames


def parseChunks(data, o, names, frames):
    """Parse whole chunks from `o`; -> the offset after the last whole one."""
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
    return o


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


def zoneTable(frames):
    """Every zone's mean total and self time and calls a frame, over `frames`
    (the play frames): -> [(name, total us, self us, calls)], by self time."""
    play = [f for f in frames if f["frame"] >= 0]
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
    n = max(1, len(play))
    return sorted(((k, tot[k] / n, own[k] / n, cnt[k] / n) for k in own), key=lambda r: -r[2])


def serve(path, port):
    """The page and its JSON, read from the capture on every request (so a
    live run is followed) - `no-store` throughout: nothing here is worth a
    stale answer (CLAUDE.md 5, the cache traps)."""
    import json
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from urllib.parse import urlparse, parse_qs
    import threading
    cap = Capture(path)
    lock = threading.Lock()
    page = os.path.join(os.path.dirname(os.path.abspath(__file__)), "omkprof.html")

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body, ctype):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def js(self, obj):
            self.send(200, json.dumps(obj).encode(), "application/json")

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            try:
                with lock:
                    cap.poll()
                    frames = cap.frames
                    if u.path == "/":
                        self.send(200, open(page, "rb").read(), "text/html; charset=utf-8")
                    elif u.path == "/api/prof/frames":
                        since = int(q.get("since", ["0"])[0])
                        self.js({"file": os.path.basename(path), "count": len(frames),
                                 "frames": [[k, f["frame"], f["dur"], f["dropped"]]
                                            for k, f in enumerate(frames[since:], since)]})
                    elif u.path == "/api/prof/frame":
                        k = int(q.get("i", ["0"])[0])
                        f = frames[k]
                        self.js({"i": k, "frame": f["frame"], "dur": f["dur"],
                                 "dropped": f["dropped"], "tree": tree(f),
                                 "sections": f["sections"]})
                    elif u.path == "/api/prof/zones":
                        lo = int(q.get("from", ["0"])[0])
                        hi = int(q.get("to", [str(len(frames))])[0])
                        self.js({"from": lo, "to": hi,
                                 "zones": zoneTable(frames[lo:hi])})
                    else:
                        self.send(404, b"not here", "text/plain")
            except (IndexError, ValueError) as e:
                self.send(400, str(e).encode(), "text/plain")

    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    print("omkprof: %s on http://127.0.0.1:%d/ (Ctrl-C ends it)" % (path, port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    if "--serve" in argv:
        return serve(argv[1], int(argv[argv.index("--serve") + 1]))
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

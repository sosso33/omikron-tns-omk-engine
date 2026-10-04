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
                                                    tree, live while the game runs;
                                                    pause / step / resume it, and
                                                    the paused frame's picture
    python3 tools/omkprof.py run.prof --sites [BIN] the live memory at the capture's end BY
                                                    ALLOCATING FUNCTION (macOS: atos; BIN
                                                    defaults to engine/build/omk-play)
    python3 tools/omkprof.py run.prof --ctl pause   one command to the game
                                                    (pause, resume, step N, snapshot)

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
        self.tags = {}                      # memory category id -> name
        self.sites = None                   # the SITE table (chunk 6), at the capture's end

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
            used = parseChunks(data, 12, self.names, self.frames, self.tags, self)
        else:
            used = parseChunks(data, 0, self.names, self.frames, self.tags, self)
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


def parseChunks(data, o, names, frames, tags=None, cap=None):
    """Parse whole chunks from `o`; -> the offset after the last whole one.
    A MEM chunk (step 4) belongs to the FRAME chunk before it: it becomes
    that frame's `mem` - {live, peak, blocks, tags: [(name, bytes, blocks)]}."""
    if tags is None:
        tags = {}
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
                           "zones": zones, "sections": sections, "mem": None, "gpu": None})
        elif typ == 4:
            tags[struct.unpack_from("<I", p, 0)[0]] = p[4:].decode("latin-1")
        elif typ == 3:
            fr, live, peak, blocks, nt = struct.unpack_from("<iQQII", p, 0)
            q, per = 28, []
            for _ in range(nt):
                tid, by, bl = struct.unpack_from("<IQI", p, q)
                per.append((tags.get(tid, "?%d" % tid), by, bl))
                q += 16
            if q != len(p):
                raise ValueError("frame %d: memory chunk of %d bytes, %d read" % (fr, len(p), q))
            if frames and frames[-1]["frame"] == fr:
                frames[-1]["mem"] = {"live": live, "peak": peak, "blocks": blocks,
                                     "tags": sorted(per, key=lambda t: -t[1])}
        elif typ == 6:
            # THE SITES (todo/ram-vs-original.md 1): the runtime address of
            # omk::prof::open, then (site, live bytes, live blocks)
            # (a CHAIN of `depth` return addresses per site, innermost first)
            ref, n6, depth = struct.unpack_from("<QII", p, 0)
            q, rows = 16, []
            for _ in range(n6):
                chain = struct.unpack_from("<%dQ" % depth, p, q)
                by, bl = struct.unpack_from("<QI", p, q + 8 * depth)
                rows.append((chain, by, bl))
                q += 8 * depth + 12
            if cap is not None:
                cap.sites = {"ref": ref, "rows": rows}
        elif typ == 5:
            # the GPU memory (step 5): what the renderers reported holding
            fr, live, nt = struct.unpack_from("<iQI", p, 0)
            q, per = 16, []
            for _ in range(nt):
                tid, by, cnt = struct.unpack_from("<IQI", p, q)
                per.append((tags.get(tid, "?%d" % tid), by, cnt))
                q += 16
            if q != len(p):
                raise ValueError("frame %d: GPU chunk of %d bytes, %d read" % (fr, len(p), q))
            if frames and frames[-1]["frame"] == fr:
                frames[-1]["gpu"] = {"live": live, "tags": sorted(per, key=lambda t: -t[1])}
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
    last = next((f for f in reversed(frames) if f.get("mem")), None)
    if last:
        m = last["mem"]
        peak = max(f["mem"]["peak"] for f in frames if f.get("mem"))
        out.write("\nmemory at frame %d: %.1f MB live in %d blocks (the run's peak frame: %.1f MB)\n" % (
            last["frame"], m["live"] / 1048576.0, m["blocks"], peak / 1048576.0))
        out.write("%-44s %12s %10s\n" % ("category", "MB", "blocks"))
        for name, by, bl in m["tags"][:30]:
            out.write("%-44s %12.2f %10d\n" % (name, by / 1048576.0, bl))
    lastG = next((f for f in reversed(frames) if f.get("gpu")), None)
    if lastG and lastG["gpu"]["tags"]:
        g = lastG["gpu"]
        out.write("\nGPU memory at frame %d: %.1f MB, as the renderer reported it\n" % (
            lastG["frame"], g["live"] / 1048576.0))
        out.write("%-44s %12s %10s\n" % ("category", "MB", "resources"))
        for name, by, cnt in g["tags"]:
            out.write("%-44s %12.2f %10d\n" % (name, by / 1048576.0, cnt))
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


def gameState(path):
    """The game's `<capture>.state` (profile.h, the control): -> {state,
    frame, snap}, or state "none" while no game has written one."""
    try:
        parts = open(path + ".state").read().split()
        return {"state": parts[0], "frame": int(parts[1]), "snap": int(parts[2])}
    except (OSError, IndexError, ValueError):
        return {"state": "none", "frame": -1, "snap": 0}


def control(path, cmd, n=1):
    """One command to the game through `<capture>.ctl` - a seq it has not
    seen (the time in ms, so a restarted tool never repeats one), the command
    and its count. -> the seq written."""
    if cmd not in ("pause", "resume", "step", "snapshot"):
        raise ValueError("no command %r" % cmd)
    import time
    seq = int(time.time() * 1000) % 2000000000
    tmp = path + ".ctl.tmp"
    with open(tmp, "w") as f:
        f.write("%d %s %d\n" % (seq, cmd, max(1, int(n))))
    os.replace(tmp, path + ".ctl")             # the game never reads half a line
    return seq


def snapshotPng(path):
    """The game's `<capture>.snap` as a PNG: -> (bytes, frame, snap number),
    or None. RGB565 widened by bit replication, as the viewer presents it."""
    import zlib
    try:
        d = open(path + ".snap", "rb").read()
    except OSError:
        return None
    if d[:8] != b"OMKSNAP1" or len(d) < 24:
        return None
    w, h, frame, snap = struct.unpack_from("<IIII", d, 8)
    px = d[24:]
    if len(px) != w * h * 2:
        return None
    rows = []
    for y in range(h):
        r = bytearray(b"\0")
        for v in struct.unpack_from("<%dH" % w, px, y * w * 2):
            r5, g6, b5 = v >> 11, (v >> 5) & 63, v & 31
            r += bytes(((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)))
        rows.append(bytes(r))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b""))
    return png, frame, snap


NOISE = ("std::", "<deduplicated_symbol>", "operator new", "omk::prof::", "0x", "void* std::",
         "_platform_", "__")


def ownerOf(syms):
    """The first frame of a chain that is this port's own code - not the
    library's containers, not a folded template, not an unnamed address."""
    for sym in syms:
        fn = sym.split(" (in ")[0]
        if fn and not fn.startswith(NOISE) and " std::__1::" not in fn.split("(")[0]:
            return fn
    return syms[0].split(" (in ")[0] if syms else "?"


def symbolizeSites(sites, binary):
    """The site table's chains -> {owner function: [bytes, blocks, sites,
    {innermost frame}]}, by `atos` against `binary` (macOS). The slide comes
    from omk::prof::open, whose runtime address the capture carries."""
    import subprocess, tempfile
    nm = subprocess.run(["nm", "-U", binary], capture_output=True, text=True).stdout
    static = None
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3 and "4prof4open" in parts[2] and "basic_string" in parts[2]:
            static = int(parts[0], 16)
            break
    if static is None:
        raise ValueError("%s: no omk::prof::open - not the binary that wrote the capture?" % binary)
    load = 0x100000000 + sites["ref"] - static
    addrs = sorted({a for chain, _, _ in sites["rows"] for a in chain if a})
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join("0x%x" % (a - 1) for a in addrs))     # inside the call, not after it
        tmp = f.name
    out = subprocess.run(["atos", "-o", binary, "-l", "0x%x" % load, "-f", tmp],
                         capture_output=True, text=True).stdout.splitlines()
    os.unlink(tmp)
    name = dict(zip(addrs, out))
    byFn = {}
    for chain, by, bl in sites["rows"]:
        syms = [name.get(a, "0x%x" % a) for a in chain if a]
        owner = ownerOf(syms)
        cur = byFn.setdefault(owner, [0, 0, 0, set()])
        cur[0] += by; cur[1] += bl; cur[2] += 1
        cur[3].add(syms[0].split(" (in ")[0][:60] if syms else "?")
    return byFn


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
                                 "frames": [[k, f["frame"], f["dur"], f["dropped"],
                                             f["mem"]["live"] if f.get("mem") else -1,
                                             f["mem"]["peak"] if f.get("mem") else -1,
                                             f["gpu"]["live"] if f.get("gpu") else -1]
                                            for k, f in enumerate(frames[since:], since)]})
                    elif u.path == "/api/prof/frame":
                        k = int(q.get("i", ["0"])[0])
                        f = frames[k]
                        self.js({"i": k, "frame": f["frame"], "dur": f["dur"],
                                 "dropped": f["dropped"], "tree": tree(f),
                                 "sections": f["sections"], "mem": f.get("mem"),
                                 "gpu": f.get("gpu")})
                    elif u.path == "/api/prof/state":
                        self.js(gameState(path))
                    elif u.path == "/api/prof/snapshot.png":
                        got = snapshotPng(path)
                        if got:
                            self.send(200, got[0], "image/png")
                        else:
                            self.send(404, b"no snapshot", "text/plain")
                    elif u.path == "/api/prof/zones":
                        lo = int(q.get("from", ["0"])[0])
                        hi = int(q.get("to", [str(len(frames))])[0])
                        self.js({"from": lo, "to": hi,
                                 "zones": zoneTable(frames[lo:hi])})
                    else:
                        self.send(404, b"not here", "text/plain")
            except (IndexError, ValueError) as e:
                self.send(400, str(e).encode(), "text/plain")

        def do_POST(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            if u.path != "/api/prof/control":
                return self.send(404, b"not here", "text/plain")
            try:
                seq = control(path, q.get("cmd", [""])[0], int(q.get("n", ["1"])[0]))
                self.js({"seq": seq})
            except ValueError as e:
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
    if "--sites" in argv:
        k = argv.index("--sites")
        binary = argv[k + 1] if len(argv) > k + 1 and not argv[k + 1].startswith("--") else \
            os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "engine", "build", "omk-play")
        cap = Capture(argv[1])
        cap.poll()
        if not cap.sites:
            print("no site table: the capture was not closed by the game (or predates it)")
            return 1
        byFn = symbolizeSites(cap.sites, binary)
        total = sum(v[0] for v in byFn.values())
        print("live at the capture's end: %.1f MB at %d sites in %d functions" % (
            total / 1048576.0, len(cap.sites["rows"]), len(byFn)))
        for fn, (by, bl, ns, inner) in sorted(byFn.items(), key=lambda kv: -kv[1][0])[:int(argv[argv.index("--top") + 1]) if "--top" in argv else 60]:
            print("%9.2f MB %7d blocks  %s" % (by / 1048576.0, bl, fn[:150]))
        return 0
    if "--ctl" in argv:
        k = argv.index("--ctl")
        cmd = argv[k + 1]
        n = int(argv[k + 2]) if len(argv) > k + 2 and argv[k + 2].isdigit() else 1
        print("seq", control(argv[1], cmd, n))
        return 0
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

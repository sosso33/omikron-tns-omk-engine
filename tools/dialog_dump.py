#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Parse gamedata/IAM/DIALOG using the format Dialog_Load (0x00401800) implements.

Exists to check the struct layouts in readable/types.h against real data.

  dialog_dump.py            summary + layout self-check over every chunk
  dialog_dump.py <index>    dump one conversation
"""
import omkpaths
import struct, sys, collections

PATH = omkpaths.data("IAM", "DIALOG")
POS_SCALE, ANGLE_SCALE = 100 / 256 / 2.54, 360 / 4096

def chunks(d):
    """The directory is a flat array of 8-byte (offset, size) entries at the
    start of the file - the loader's `(i >> 8) * 2048 + 8 * (i & 255)` is just
    `8 * i`, written that way because it reads a whole 2048-byte sector at a
    time. The directory ends where the first payload begins."""
    n = len(d)
    first = None
    for i in range(n // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n:
            if first is None or off < first: first = off
        if first is not None and 8 * (i + 1) > first: break
    for i in range(first // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n and size >= 8:
            yield i, d[off:off + size]

def parse(b):
    speaker, nnode, ncam, spare = struct.unpack_from("<4h", b, 0)
    if nnode <= 0 or ncam <= 0 or 8 + 64 * nnode + 44 * ncam > len(b): return None
    nodes = []
    for j in range(nnode):
        o = 8 + 64 * j
        nodes.append({
            "ptr":    struct.unpack_from("<9I", b, o),
            "param":  struct.unpack_from("<4h", b, o + 36),
            "id":     struct.unpack_from("<h",  b, o + 44)[0],
            "name":   b[o + 46:o + 56].split(b"\0")[0].decode("cp1252", "replace"),
            "tail":   struct.unpack_from("<4h", b, o + 56),
        })
    cams = []
    for j in range(ncam):
        o = 8 + 64 * nnode + 44 * j
        cams.append({
            "pos":     struct.unpack_from("<6i", b, o),
            "id":      struct.unpack_from("<h",  b, o + 24)[0],
            "field26": struct.unpack_from("<h",  b, o + 26)[0],
            "angle":   struct.unpack_from("<2h", b, o + 28),
            "subject": struct.unpack_from("<2H", b, o + 32),
        })
    return speaker, nodes, cams, 8 + 64 * nnode + 44 * ncam

def strings(b, at):
    out = []
    while at < len(b):
        e = b.find(b"\0", at)
        if e < 0: break
        out.append(b[at:e].decode("cp1252", "replace")); at = e + 1
    return out

d = open(PATH, "rb").read()

if len(sys.argv) > 1:
    want = int(sys.argv[1])
    for i, b in chunks(d):
        if i != want: continue
        speaker, nodes, cams, pool = parse(b)
        print(f"chunk {i}: {len(b)} bytes, speaker object id {speaker}, "
              f"{len(nodes)} nodes, {len(cams)} cameras, strings from 0x{pool:x}")
        for j, n in enumerate(nodes):
            print(f"\n node {j}  id={n['id']}  name={n['name']!r}  "
                  f"param={n['param']}  tail={n['tail']}")
            for k, p in enumerate(n["ptr"]):
                if p: print(f"   ptr[{k}] -> 0x{p:x}")
            for s in strings(b, n["ptr"][8])[:8]:
                if s: print(f"     {s!r}")
        for c in cams:
            pos = [int(v * POS_SCALE - 1) for v in c["pos"]]
            ang = [int(a * ANGLE_SCALE) for a in c["angle"]]
            print(f"\n camera id={c['id']} subject={c['subject']} field26={c['field26']}"
                  f"\n   pos raw {c['pos']} -> {pos}"
                  f"\n   angle raw {list(c['angle'])} -> {ang} degrees")
        sys.exit()
    sys.exit(f"no chunk {want}")

ok = nodes = cams = 0
slots = collections.Counter()
for i, b in chunks(d):
    p = parse(b)
    if not p: continue
    _, ns, cs, pool = p
    ok += 1; nodes += len(ns); cams += len(cs)
    for n in ns:
        for k, v in enumerate(n["ptr"]):
            if v: slots[k] += 1
print(f"{PATH}: {len(d)} bytes")
print(f"{ok} chunks parse cleanly: {nodes} nodes, {cams} cameras")
print(f"non-null pointer slots per node: {dict(sorted(slots.items()))}")
print("ptr[8] non-null in every node" if slots[8] == nodes
      else f"ptr[8] non-null in {slots[8]}/{nodes}")

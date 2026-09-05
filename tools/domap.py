#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Account for every byte of every `.3DO`, the way `chunkmap.py` does for
`IAM\AREA`: claim what a documented structure explains and report the rest.

    python3 tools/domap.py                 # the whole tree, one summary
    python3 tools/domap.py Anekbah         # one model, its gaps listed

Written 2026-09-05 to settle a specific question. The game's 1999 spec sheet
claims a "Systeme de tri de face par Arbre BSP" (`todo/engine-spec-1999.md`)
and nothing in this repo evidences one: the shipped face sort is the 14-bit
bucket key and `Render_FlushBuckets`' single ascending walk. Three
explanations were possible, and one of them - that a tree sits in the `.3DO`
where the nine known header offsets do not reach - is the only one this tree
can test. Byte accounting is that test.

The layout it claims, all of it documented in `docs/FILE_FORMATS.md` 5b:

    +0    44 bytes   the header: a magic, a version and the nine table offsets
    +44   328 bytes  the descriptor, whose +188..+240 hold the counts
    ...   the eight tables, at the offsets the header gives:
          materials 80, vertices 32, triangles 28, quads 32,
          meshes 140, doors 28, cameras 52, lights 304

The descriptor's 328 bytes are established here rather than assumed: the
first table begins at 372 in all 635 files, and 372 - 44 is 328.
"""
import os, struct, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import omkpaths

HEADER, DESC_AT, DESC_LEN = 44, 44, 328
# name -> (offset of the table pointer in the header, offset of the count in
# the descriptor, record stride)
TABLES = {
    "materials": (12, 208, 80),
    "vertices":  (16, 196, 32),
    "triangles": (20, 188, 28),
    "quads":     (24, 192, 32),
    "meshes":    (28, 224, 140),
    "doors":     (32, 228, 28),
    "cameras":   (36, 220, 52),
    "lights":    (40, 240, 304),
}


def account(path):
    """-> (size, claimed bytes, [(start, end)] of what nothing explains)."""
    d = open(path, "rb").read()
    if len(d) < DESC_AT + DESC_LEN:
        return len(d), 0, [(0, len(d))]
    desc = struct.unpack_from("<i", d, 8)[0]
    if desc < 0 or desc + DESC_LEN > len(d):
        return len(d), 0, [(0, len(d))]
    claim = bytearray(len(d))
    claim[0:desc + DESC_LEN] = b"\1" * (desc + DESC_LEN)
    for _name, (po, co, stride) in TABLES.items():
        off = struct.unpack_from("<i", d, po)[0]
        n = struct.unpack_from("<i", d, desc + co)[0]
        if off > 0 and n > 0 and off + stride * n <= len(d):
            claim[off:off + stride * n] = b"\1" * (stride * n)
    gaps, i = [], 0
    while i < len(d):
        if not claim[i]:
            j = i
            while j < len(d) and not claim[j]:
                j += 1
            gaps.append((i, j))
            i = j
        else:
            i += 1
    return len(d), sum(claim), gaps


def models():
    root = omkpaths.data("MESHES")
    out = []
    for dirpath, _dirs, names in os.walk(root):
        for n in names:
            if n.lower().endswith(".3do"):
                out.append(os.path.join(dirpath, n))
    return sorted(out)


def main():
    want = sys.argv[1].lower() if len(sys.argv) > 1 else None
    files = [p for p in models()
             if not want or os.path.basename(p).lower().startswith(want)]
    total = claimed = exact = 0
    sizes = collections.Counter()
    for p in files:
        n, c, gaps = account(p)
        total += n
        claimed += c
        if not gaps:
            exact += 1
        for a, b in gaps:
            sizes[b - a] += 1
        if want:
            print("%-30s %8d bytes, %d unexplained" % (os.path.basename(p), n, n - c))
            for a, b in gaps:
                print("     +%-9d %4d bytes  %s" % (a, b - a,
                      " ".join("%02x" % x for x in open(p, "rb").read()[a:min(b, a + 16)])))
    print("%d models, %d bytes, %.4f%% claimed, %d unexplained, %d accounted "
          "for EXACTLY" % (len(files), total, 100.0 * claimed / max(total, 1),
                           total - claimed, exact))
    if not want and sizes:
        print("the unexplained runs:", sizes.most_common(8))


if __name__ == "__main__":
    main()

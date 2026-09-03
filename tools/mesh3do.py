#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal .3DO reader, following the notes in Omikron.txt (the Unity importer
by an earlier project - not derived from the executable, so treat the field
names as reported rather than verified)."""
import struct, sys, os

HDR = ("sig major minor matOff vtxOff triOff quadOff meshOff doorOff camOff "
       "lightOff").split()

def header(path):
    return header_bytes(open(path, "rb").read(376), os.path.getsize(path))


def header_bytes(d, size=None):
    """The header, from the bytes - so an embedded .3DO can use it too.

    The SCX effect sprites are whole .3DO files stored inside the .SCX stream
    (see sprite_fx.py), so nothing can be read off a path for them.

    Word 2 (@8) is the DESCRIPTOR offset, not a minor version - Read3DO_Init
    takes the scene descriptor from `file + hdr[2]` and every count below is
    relative to it. It is 44 in all 635 shipped models and in all 230 embedded
    sprites, which is why reading the counts at fixed file offsets worked; they
    are computed from it here so that stays a fact rather than an assumption."""
    if d[:4] != b"OD3X": raise ValueError("not a 3DO: %r" % d[:4])
    o = struct.unpack_from("<2i8i", d, 4)      # major, descOff, 8 section offsets
    desc = o[1]
    tri, quad, vtx = struct.unpack_from("<3i", d, desc + 188)
    mat = struct.unpack_from("<i", d, desc + 208)[0]
    cam, mesh, door, light = struct.unpack_from("<4i", d, desc + 220)
    return {"major": o[0], "minor": desc, "descOff": desc,
            "matOff": o[2], "vtxOff": o[3], "triOff": o[4], "quadOff": o[5],
            "meshOff": o[6], "doorOff": o[7], "camOff": o[8], "lightOff": o[9],
            "triangles": tri, "quads": quad, "vertices": vtx,
            "materials": mat, "cameras": cam, "meshes": mesh,
            "doors": door, "lights": light,
            "size": len(d) if size is None else size}

MESH_REC = 4+4+4+4+20+12+4+4+4+4+4+4+4+12+4+12+12+24   # 140; the notes say 136,
# but every .3DO in the game divides evenly by 140 ((doorOff-meshOff)/meshes on
# all 401 files) and only 140 keeps the mesh names readable past the first one.

def meshes(path):
    """-> list of dicts, one per mesh node."""
    return meshes_bytes(open(path, "rb").read())


def meshes_bytes(d):
    h = header_bytes(d, len(d))
    out = []
    for i in range(h["meshes"]):
        o = h["meshOff"] + MESH_REC * i
        if o + MESH_REC > len(d): break
        flags, _u, mid, eid = struct.unpack_from("<4i", d, o)
        name = d[o+16:o+36].split(b"\0")[0].decode("cp1252", "replace")
        pos = struct.unpack_from("<3f", d, o+36)
        parent, child, nxt = struct.unpack_from("<3i", d, o+48)
        nv, nt, nq = struct.unpack_from("<3i", d, o+64)
        out.append({"i": i, "flags": flags, "id": mid, "name": name,
                    "pos": pos, "parent": parent, "child": child, "next": nxt,
                    "vertices": nv, "triangles": nt, "quads": nq})
    return h, out

if __name__ == "__main__":
    for p in sys.argv[1:]:
        h, ms = meshes(p)
        print(f"{os.path.basename(p)}: v={h['vertices']} tri={h['triangles']} "
              f"quad={h['quads']} mat={h['materials']} meshes={h['meshes']} size={h['size']}")
        for m in ms[:40]:
            print(f"   [{m['i']:3}] {m['name']:20} v={m['vertices']:5} t={m['triangles']:5} "
                  f"q={m['quads']:5} parent={m['parent']:4} flags=0x{m['flags']&0xFFFFFFFF:08X}")


CAM_REC = 52          # name[20], pos[3], target[3], unused, fov

def cameras(path):
    """The scene cameras a .3DO carries: name, eye position, look-at target and
    field of view. 52 bytes per record, per Omikron.txt - reading them at any
    other stride turns the names to noise after the first one or two, which is
    how the size is checked."""
    h = header(path)
    d = open(path, "rb").read()
    out = []
    for i in range(h["cameras"]):
        o = h["camOff"] + CAM_REC * i
        if o + CAM_REC > len(d): break
        out.append({
            "name":   d[o:o+20].split(b"\0")[0].decode("cp1252", "replace"),
            "pos":    struct.unpack_from("<3f", d, o + 20),
            "target": struct.unpack_from("<3f", d, o + 32),
            "unknown": struct.unpack_from("<f", d, o + 44)[0],
            "fov":    struct.unpack_from("<f", d, o + 48)[0]})
    return out

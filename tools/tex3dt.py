#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Decode the .3dt texture files that sit beside each .3DO.

Container and codec follow the notes in Omikron.txt. One .3dt holds every
texture of the matching .3DO, in material order; each is a palette followed by
image data:

    palette      3 bytes per colour, 16 colours if bpp == 4 else 256
    image data   imageDataSize bytes from the material record

`imageDataSize == 65536` means the 256x256 image is stored raw. Otherwise it is
an LZ scheme: a control byte supplies 8 flags, MSB first; a clear flag copies
one literal byte, a set flag introduces a two-or-more byte sequence token
giving a run length and a back-reference.

Container sizing is verified - for all 635 .3DO/.3dt pairs in gamedata/MESHES the
file length equals sum(paletteSize*3 + imageDataSize) over the materials.

The codec is SOLVED: every one of the **2534** textures under `gamedata/MESHES`
decodes to exactly width*height indices (`verify.py: textures`, --slow). The
parameter space the notes left open - run-length base, repeat-run count, the
bytes each token type consumes, the flag bit order and polarity, the token bit
packing - is settled by that, since a wrong choice in any of them desynchronises
and lands short or long.

`exact` is therefore True on everything shipped. It is still returned, and the
trim/pad below still runs, because `textures_bytes` is also used on the .3DO
files EMBEDDED in the SCX stream (the effect sprites, sprite_fx.py) and on
anything a caller hands it; a decoder that silently returned a short buffer
would be worse than one that says so.

    (An earlier version of this note said the decoder was a best fit of 96
    combinations landing exactly on about a third of textures. That was true
    when it was written and has not been true for some time - corrected
    2026-08-30, against the check that had been asserting 2534/2534 all along.)
"""
import struct, sys, os, zlib

def decode_image(data, want):
    """LZ-decompress `data` until `want` pixel indices are produced.

    Always returns exactly `want` bytes: a run that overshoots is trimmed, and
    a stream that ends early is padded by repeating the last pixel rather than
    leaving a black band. See the note in the module docstring about why some
    streams end early."""
    out = bytearray()
    i, n = 0, len(data)
    if n:
        out.append(data[0]); i = 1        # the first byte is always a literal
    while i < n and len(out) < want:
        group = data[i]; i += 1
        for bit in range(8):
            if len(out) >= want or i >= n: break
            if (group << bit) & 0x80 == 0:
                out.append(data[i]); i += 1
                continue
            token = data[i]; i += 1
            size = (token & 0xFC) // 4 + 3
            kind = token & 3
            if kind == 0:                       # repeat the previous pixel
                if out:
                    out += bytes([out[-1]]) * (size - 1)
            elif kind == 1:                     # back-reference, 8-bit offset
                off = data[i] + 1; i += 1
                for _ in range(size):
                    out.append(out[-off] if off <= len(out) else 0)
            elif kind == 2:                     # back-reference, 16-bit offset
                off = data[i] * 256 + data[i+1] + 1; i += 2
                for _ in range(size):
                    out.append(out[-off] if off <= len(out) else 0)
            else:                               # back-reference, offset * 256
                off = 256 * data[i]; i += 1
                for _ in range(size):
                    out.append(out[-off] if 0 < off <= len(out) else 0)
    exact = len(out) == want
    if len(out) > want:
        del out[want:]
    elif len(out) < want:
        out += bytes([out[-1] if out else 0]) * (want - len(out))
    return bytes(out), exact

def textures(dopath):
    """-> list of {name, w, h, bpp, rgb} for the model's materials."""
    d = open(dopath, "rb").read()
    tpath = None
    for cand in (dopath[:-4] + ".3dt", dopath[:-4] + ".3DT"):
        if os.path.exists(cand): tpath = cand; break
    if not tpath: return []
    return textures_bytes(d, open(tpath, "rb").read())


def textures_bytes(d, t):
    """The same, from bytes - the SCX effect sprites embed both (sprite_fx.py).

    The walk over `t` is exact: it consumes palette + image data per material
    and must land on len(t). `consumed` in the result of the last entry lets a
    caller assert that."""
    import mesh3do
    h = mesh3do.header_bytes(d, len(d))
    out, off = [], 0
    for i in range(h["materials"]):
        o = h["matOff"] + 80 * i
        name = d[o:o+20].split(b"\0")[0].decode("cp1252", "replace")
        # +60 data size, then the two RUNTIME slot fields (-1 in every shipped
        # material; the loader stamps the texture slot into +64 and the palette
        # slot into +68, and +64 is the low six bits of the render bucket key -
        # ASSETS 4b), then bits per pixel.
        size, _texSlot, _palSlot, bpp = struct.unpack_from("<4i", d, o + 60)
        w, ht = struct.unpack_from("<2h", d, o + 76)
        ncol = 16 if bpp == 4 else 256
        pal = t[off:off + ncol * 3]; off += ncol * 3
        raw = t[off:off + size];     off += size
        if size == 65536:
            idx, exact = raw[:w*ht].ljust(w*ht, b"\0"), len(raw) >= w * ht
        else:
            idx, exact = decode_image(raw, w * ht)
        rgb = bytearray(w * ht * 3)
        for k in range(min(len(idx), w * ht)):
            c = idx[k] * 3
            if c + 2 < len(pal):
                rgb[k*3:k*3+3] = pal[c:c+3]
        out.append({"name": name, "w": w, "h": ht, "bpp": bpp,
                    "exact": exact, "rgb": bytes(rgb), "consumed": off})
    return out

def png(w, h, rgb):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = b"".join(b"\0" + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6))
            + chunk(b"IEND", b""))

if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    for p in sys.argv[1:]:
        for i, tx in enumerate(textures(p)):
            status = "exact" if tx["exact"] else "padded/trimmed"
            print(f"{os.path.basename(p)} [{i}] {tx['name']:14} "
                  f"{tx['w']}x{tx['h']} bpp={tx['bpp']} -> {status}")
            open(f"/tmp/{os.path.basename(p)[:-4]}_{i}.png", "wb").write(
                png(tx["w"], tx["h"], tx["rgb"]))

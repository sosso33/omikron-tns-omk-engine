#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""OTNS ADPCM decoder for Omikron: The Nomad Soul .ADP files.

Transcribed from the game's own decoder - sub_483200 (mono) and sub_483340
(stereo) at 0x00483200 / 0x00483340, using the tables at dword_4BCC50 (step)
and dword_4BCC10 (index).

It is IMA-ADPCM with two differences from the textbook version, both of which
matter:

  * the high nibble of each byte is decoded first, not the low one;
  * the delta is (4*b2 + 2*b1 + b0) * step >> 2, i.e. IMA's unconditional
    `step >> 3` bias term is absent. Leaving it in makes the predictor drift -
    about -9000 DC on a full line, with the audio buried under it.

The game writes each decoded sample twice, upsampling to its mixer rate. That
is dropped here; the useful rate is 22050 Hz, matching vgmstream's reader in
adp/pc_adp_otns.c.

    python3 tools/adp.py in.adp out.wav
"""
import struct, sys

STEP = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
    73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
    494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
    2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
    10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
INDEX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]
RATE = 22050

class _Ch:
    __slots__ = ("pred", "idx", "step")
    def __init__(self):
        self.pred = 0; self.idx = 0; self.step = STEP[0]

    def nibble(self, nib):
        self.idx = 0 if self.idx + INDEX[nib] < 0 else min(88, self.idx + INDEX[nib])
        d = 0
        if nib & 4: d = 4 * self.step
        if nib & 2: d += 2 * self.step
        if nib & 1: d += self.step
        d >>= 2
        self.pred = self.pred - d if nib & 8 else self.pred + d
        if self.pred > 0x7FFF: self.pred = 0x7FFF
        elif self.pred < -0x8000: self.pred = -0x8000
        self.step = STEP[self.idx]
        return self.pred

def decode(data, stereo=False):
    """-> (pcm bytes, channels). High nibble first; in stereo the high nibble
    is the left channel and the low nibble the right (sub_483340)."""
    if stereo:
        l, r = _Ch(), _Ch()
        out = bytearray()
        for byte in data:
            out += struct.pack("<hh", l.nibble(byte >> 4), r.nibble(byte & 0xF))
        return bytes(out), 2
    c = _Ch()
    out = bytearray()
    for byte in data:
        out += struct.pack("<hh", c.nibble(byte >> 4), c.nibble(byte & 0xF))
    return bytes(out), 1

def read(path):
    """Parse the 0x10 header documented in adp/pc_adp_otns.c and decode."""
    raw = open(path, "rb").read()
    if len(raw) < 16: raise ValueError("too short")
    size = struct.unpack("<I", raw[:4])[0] & 0xFFFFFF
    stereo = bool(raw[3])
    body = raw[16:16 + size] if 0 < size <= len(raw) - 16 else raw[16:]
    return decode(body, stereo)

def wav(pcm, channels):
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt "
            + struct.pack("<IHHIIHH", 16, 1, channels, RATE,
                          RATE * channels * 2, channels * 2, 16)
            + b"data" + struct.pack("<I", len(pcm)) + pcm)

if __name__ == "__main__":
    if len(sys.argv) != 3: sys.exit(__doc__)
    pcm, ch = read(sys.argv[1])
    open(sys.argv[2], "wb").write(wav(pcm, ch))
    print(f"{sys.argv[2]}: {len(pcm)//(2*ch)/RATE:.2f}s, {ch} channel(s)")

// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/sfx.h"

#include <cstring>

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}
std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(d[o]) | static_cast<std::uint16_t>(d[o + 1]) << 8);
}
float f32(std::span<const std::byte> d, std::size_t o) {
    const auto b = u32(d, o);
    float f; std::memcpy(&f, &b, sizeof f); return f;
}

}  // namespace

SfxFile readSfx(std::span<const std::byte> d) {
    SfxFile s;
    s.size = d.size();
    if (d.size() < 8 || std::memcmp(d.data(), "5.0V", 4) != 0) return s;

    const auto A = u32(d, 4);
    std::size_t o = 8u + 40u * A;
    const auto B = u32(d, o); o += 4u + 44u * B;
    const auto C = u32(d, o); const std::size_t cb = o + 4; o = cb + 80u * C;
    const auto D = u32(d, o); const std::size_t db = o + 4; o = db + 16u * D;
    std::uint32_t E = 0, F = 0;
    std::size_t eb = 0, fb = 0;
    if (o < d.size()) {
        E = u32(d, o); eb = o + 4; o = eb + 76u * E;
        if (o + 4 <= d.size()) {
            F = u32(d, o); o += 4; fb = o;
            for (std::uint32_t k = 0; k < F; ++k) {
                const auto n = u32(d, o + 8);
                o += 36u * n + 16u;
            }
        }
    }
    s.counts[0] = A; s.counts[1] = B; s.counts[2] = C;
    s.counts[3] = D; s.counts[4] = E; s.counts[5] = F;
    s.end = o;
    s.exact = (o == d.size());

    for (std::uint32_t i = 0; i < C; ++i) {
        const auto r = cb + 80u * i;
        if (r + 80 > d.size()) break;
        FxEffect e;
        e.id     = static_cast<std::int32_t>(u32(d, r));
        e.sound  = static_cast<std::int32_t>(u32(d, r + 4));
        e.sprite = u16(d, r + 8);
        e.flags  = u32(d, r + 12);
        e.vx = f32(d, r + 16); e.vy = f32(d, r + 20); e.vz = f32(d, r + 24);
        e.drift = f32(d, r + 28);
        e.life  = f32(d, r + 32);
        e.scale = f32(d, r + 56);
        e.cone  = f32(d, r + 60);
        e.spin  = f32(d, r + 64);
        e.count = static_cast<std::int16_t>(u16(d, r + 68));
        e.colour0 = u32(d, r + 48);
        e.colour1 = u32(d, r + 52);
        e.mode  = static_cast<std::uint8_t>(d[r + 78]);
        for (std::size_t k = 0; k < 8 && r + 70 + k < d.size(); ++k) {
            const auto c = static_cast<char>(d[r + 70 + k]);
            if (c == '\0') break;
            e.name.push_back(c);
        }
        s.effects.push_back(std::move(e));
    }
    for (std::uint32_t i = 0; i < D; ++i) {
        const auto r = db + 16u * i;
        if (r + 16 > d.size()) break;
        FxBinding b;
        b.effect = static_cast<std::int32_t>(u32(d, r));
        for (int k = 0; k < 4; ++k) b.tag[k] = static_cast<char>(d[r + 4 + static_cast<std::size_t>(k)]);
        b.period = f32(d, r + 12);
        s.bindings.push_back(b);
    }

    // Section E - the set pieces. `sub_451470` matches +8/+12 against the
    // (a1, objectId) an object start hands it; +28 is where the piece sits.
    for (std::uint32_t i = 0; i < E; ++i) {
        const std::size_t r = eb + 76u * i;
        if (r + 76u > d.size()) break;
        FxSetPiece p;
        p.id    = static_cast<std::int32_t>(u32(d, r + 0));
        p.key0  = static_cast<std::int32_t>(u32(d, r + 8));
        p.key1  = static_cast<std::int32_t>(u32(d, r + 12));
        p.block = static_cast<std::int32_t>(u32(d, r + 16));
        p.linkType = static_cast<std::int32_t>(u32(d, r + 40));
        p.linkId   = u32(d, r + 44);
        p.effectId = static_cast<std::int32_t>(u32(d, r + 52));
        p.delay = f32(d, r + 56);
        p.loops = static_cast<std::int32_t>(u32(d, r + 64));
        p.flags = u32(d, r + 72);
        for (int k = 0; k < 3; ++k)
            p.pos[k] = f32(d, r + 28u + 4u * static_cast<std::size_t>(k));
        s.pieces.push_back(p);
    }

    // Section F: `{16-byte header + n x 36}` per block, in block order. A
    // piece's `+16` names one, and each sub-record is a WAYPOINT the piece
    // travels through (`sub_450E50`) - the effect at `+4` for the indirect
    // form, the position at `+8`, the segment's frames at `+20`, the link at
    // `+24`/`+28`. They are not emitters: `sub_451600` registers ONE per shown
    // row per frame, at the row's current position.
    {
        std::vector<std::vector<FxPiecePart>> blocks;
        std::size_t p = o - 0;   // `o` is past F only if the walk got there
        p = 0;
        // re-walk to F's start: the counts above leave `fb` at its base
        p = fb;
        for (std::uint32_t k = 0; k < F && p + 16u <= d.size(); ++k) {
            const auto n = u32(d, p + 8);
            std::vector<FxPiecePart> parts;
            for (std::uint32_t j = 0; j < n; ++j) {
                const std::size_t r = p + 16u + 36u * j;
                if (r + 36u > d.size()) break;
                FxPiecePart q;
                q.effect = static_cast<std::int32_t>(u32(d, r + 4));
                for (int c = 0; c < 3; ++c)
                    q.pos[c] = f32(d, r + 8u + 4u * static_cast<std::size_t>(c));
                q.dur      = f32(d, r + 20);
                q.linkType = static_cast<std::int32_t>(u32(d, r + 24));
                q.linkId   = u32(d, r + 28);
                parts.push_back(q);
            }
            blocks.push_back(std::move(parts));
            p += 16u + 36u * n;
        }
        for (auto& pc : s.pieces)
            if (pc.block >= 0 && pc.block < static_cast<int>(blocks.size()))
                pc.parts = blocks[static_cast<std::size_t>(pc.block)];
    }
    s.valid = true;
    return s;
}

}  // namespace omk

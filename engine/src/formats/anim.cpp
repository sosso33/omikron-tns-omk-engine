// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/anim.h"

#include <cstring>

namespace omk {
namespace {

std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(d[o    ])       |
        static_cast<std::uint32_t>(d[o + 1]) <<  8 |
        static_cast<std::uint32_t>(d[o + 2]) << 16 |
        static_cast<std::uint32_t>(d[o + 3]) << 24);
}

float f32(std::span<const std::byte> d, std::size_t o) {
    const std::uint32_t bits = static_cast<std::uint32_t>(i32(d, o));
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

std::string cstr(std::span<const std::byte> d, std::size_t o, std::size_t cap) {
    std::string s;
    for (std::size_t k = 0; k < cap && o + k < d.size(); ++k) {
        const auto c = static_cast<char>(d[o + k]);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

}  // namespace

std::vector<AnimClip> animClips(std::span<const std::byte> d) {
    std::vector<AnimClip> out;
    if (d.size() < 8 || std::memcmp(d.data(), "3.0V", 4) != 0) return out;
    const auto n = i32(d, 4);
    for (int g = 0; g < n; ++g) {
        const auto o = 8u + 24u * static_cast<std::size_t>(g);
        if (o + 8 > d.size()) break;
        const auto idx  = i32(d, o);
        auto p = static_cast<std::size_t>(static_cast<std::uint32_t>(i32(d, o + 4)));
        // a singly-linked list; the guard is not decoration, a corrupt `next`
        // would loop for ever
        for (int guard = 0; p != 0 && p + 36 <= d.size() && guard < 4096; ++guard) {
            AnimClip c;
            c.group = idx;
            c.type  = i32(d, p);
            c.slot  = i32(d, p + 4);
            c.descriptor = static_cast<std::size_t>(
                static_cast<std::uint32_t>(i32(d, p + 8)));
            c.name = cstr(d, p + 28, 8);
            out.push_back(std::move(c));
            p = static_cast<std::size_t>(static_cast<std::uint32_t>(i32(d, p + 24)));
        }
    }
    return out;
}

std::optional<AnimDescriptor> animDescriptor(std::span<const std::byte> d,
                                             std::size_t off) {
    if (off + 12 > d.size()) return std::nullopt;
    AnimDescriptor a;
    a.frames = i32(d, off);
    const auto bones = i32(d, off + 4);
    if (!(bones > 0 && bones < 256)) return std::nullopt;
    a.tracks.reserve(static_cast<std::size_t>(bones));
    for (int i = 0; i < bones; ++i) {
        const auto o = off + 12u + 40u * static_cast<std::size_t>(i);
        if (o + 40 > d.size()) return std::nullopt;
        AnimTrack t;
        t.name = cstr(d, o, 20);
        t.posKeys = i32(d, o + 20);
        const auto po = i32(d, o + 24);
        t.posOffset = po ? off + static_cast<std::size_t>(po) : 0;
        t.rotKeys = i32(d, o + 28);
        const auto ro = i32(d, o + 32);
        t.rotOffset = ro ? off + static_cast<std::size_t>(ro) : 0;
        t.flags = i32(d, o + 36);
        a.tracks.push_back(std::move(t));
    }
    return a;
}

std::vector<Quat> animRotations(std::span<const std::byte> d, const AnimTrack& t) {
    std::vector<Quat> out;
    if (!t.rotOffset || t.rotKeys <= 0) return out;
    out.reserve(static_cast<std::size_t>(t.rotKeys));
    for (int k = 0; k < t.rotKeys; ++k) {
        const auto o = t.rotOffset + 16u * static_cast<std::size_t>(k);
        if (o + 16 > d.size()) break;
        out.push_back({f32(d, o), f32(d, o + 4), f32(d, o + 8), f32(d, o + 12)});
    }
    return out;
}

}  // namespace omk

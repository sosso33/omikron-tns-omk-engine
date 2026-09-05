// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/light3do.h"
#include "formats/mesh3do.h"

#include <cstring>

namespace omk {
namespace {

bool fits(std::span<const std::byte> d, std::size_t off, std::size_t n) {
    return off <= d.size() && n <= d.size() - off;
}
std::uint32_t u32at(std::span<const std::byte> d, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, d.data() + o, 4);
    return v;
}
float f32at(std::span<const std::byte> d, std::size_t o) {
    float v = 0;
    std::memcpy(&v, d.data() + o, 4);
    return v;
}

}  // namespace

std::vector<Light3do> readLights(std::span<const std::byte> d, const Mesh3doHeader& h) {
    std::vector<Light3do> out;
    if (h.lights <= 0 || h.lightOff < 0) return out;
    const auto base = static_cast<std::size_t>(h.lightOff);
    for (int i = 0; i < h.lights; ++i) {
        const std::size_t o = base + kLightRecord * static_cast<std::size_t>(i);
        if (!fits(d, o, kLightRecord)) break;
        Light3do l;
        l.flags = u32at(d, o + 0);
        // the 12-byte name, NUL-terminated - always `LIGHT...`
        for (std::size_t k = 0; k < 12; ++k) {
            const auto c = static_cast<char>(d[o + 4 + k]);
            if (!c) break;
            l.name.push_back(c);
        }
        l.radiusA = f32at(d, o + 24);
        l.radiusB = f32at(d, o + 28);
        l.f32     = f32at(d, o + 32);
        l.f36     = f32at(d, o + 36);
        l.f40     = f32at(d, o + 40);
        l.colour  = u32at(d, o + 44);
        for (int k = 0; k < 3; ++k) {
            l.pos[k]    = f32at(d, o + 48 + 4 * static_cast<std::size_t>(k));
            l.centre[k] = f32at(d, o + 80 + 4 * static_cast<std::size_t>(k));
        }
        // the four corners, 32 bytes apart from +112 - the same walk
        // `sub_48DEA0` makes when it builds the record's bounding box
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 3; ++k)
                l.corner[c][k] = f32at(d, o + 112 + 32 * static_cast<std::size_t>(c)
                                              + 4 * static_cast<std::size_t>(k));
        out.push_back(std::move(l));
    }
    return out;
}

}  // namespace omk

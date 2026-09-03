// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/morph.h"

namespace omk {
namespace {
std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}
}  // namespace

MorphLayout morphLayout(std::span<const std::byte> d) {
    MorphLayout L;
    L.size = d.size();
    if (d.size() < 16) return L;
    const auto w0 = u32(d, 0);
    L.audio    = w0 & 0xFFFFFFu;
    L.channels = (w0 >> 24) + 1;
    L.vertices = u32(d, 4);
    L.nominalFrames = u32(d, 8);
    L.nodes    = u32(d, 12);

    L.record   = L.audio + 24u * L.vertices + 16u * L.nodes + 12u;
    L.preamble = 16u + 4u * L.nodes;
    if (L.record == 0 || d.size() < L.preamble) return L;

    const auto body = d.size() - L.preamble;
    L.frames = body / L.record;
    L.tail   = body % L.record;
    if (L.tail) {
        // the only remainder that occurs is a record short of its audio block
        ++L.frames;
        L.lastFrameHasAudio = false;
    }

    L.preambleIsIota = true;
    for (std::uint32_t i = 0; i < L.nodes; ++i)
        if (u32(d, 16u + 4u * i) != i) { L.preambleIsIota = false; break; }

    L.valid = true;
    return L;
}

std::vector<std::byte> morphAudio(std::span<const std::byte> d,
                                  const MorphLayout& L) {
    std::vector<std::byte> out;
    if (!L.valid || L.audio == 0) return out;
    const auto n = L.lastFrameHasAudio ? L.frames : (L.frames ? L.frames - 1 : 0);
    out.reserve(n * L.audio);
    for (std::size_t i = 0; i < n; ++i) {
        const auto end = L.preamble + i * L.record + L.record;
        if (end > d.size()) break;
        const auto at = end - L.audio;
        out.insert(out.end(), d.begin() + static_cast<std::ptrdiff_t>(at),
                   d.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return out;
}

}  // namespace omk

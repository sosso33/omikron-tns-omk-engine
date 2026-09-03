// SPDX-License-Identifier: GPL-3.0-or-later
// The world camera table. See `worldcam.h` for where every offset comes from.
#include "o3de/worldcam.h"

#include <cmath>

namespace omk {
namespace {

std::int32_t i32at(std::span<const std::byte> b, std::size_t o) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(b[o]) |
        (static_cast<std::uint32_t>(b[o + 1]) << 8) |
        (static_cast<std::uint32_t>(b[o + 2]) << 16) |
        (static_cast<std::uint32_t>(b[o + 3]) << 24));
}
std::int16_t i16at(std::span<const std::byte> b, std::size_t o) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[o]) |
                                     (static_cast<std::uint16_t>(b[o + 1]) << 8));
}

}  // namespace

float rawToWorld(std::int32_t v) {
    // `Global_Load`: (100 * v) * (1/256) * (1/2.54) - 1, in double before the
    // truncation to int the loader then does. The engine stores the result
    // back into the int32 field, so the fraction really is discarded.
    const double d = static_cast<double>(100 * v) * 0.00390625 *
                     0.3937007874015748 - 1.0;
    return static_cast<float>(static_cast<std::int32_t>(d));
}

ResolvedCamera resolveCamera(const WorldCamera& c, const float p[3], float yaw) {
    ResolvedCamera r;
    // The game's facing convention, the one the ADDRESSES table uses:
    // forward is `(sin y, -cos y)` in (x, z), so yaw 0 faces -Z. That makes
    // the rotation about Y the ordinary one.
    const float t = yaw * 0.0174532925199433f;
    const float cs = std::cos(t), sn = std::sin(t);
    const auto place = [&](const float off[3], int subject, float out[3]) {
        if (subject == -1) {                 // an absolute point, taken as is
            for (int k = 0; k < 3; ++k) out[k] = off[k];
            return;
        }
        const float rx = off[0] * cs - off[2] * sn;
        const float rz = off[0] * sn + off[2] * cs;
        out[0] = p[0] - rx;
        out[1] = p[1] - off[1];
        out[2] = p[2] - rz;
    };
    place(c.eye, c.eyeSubject, r.eye);
    place(c.at,  c.atSubject,  r.at);
    return r;
}

float angle4096(std::int16_t raw) {
    // Wrap FIRST. `Global_Load` does not, and it does not have to: a stored
    // 4086 and a stored -10 are the same rotation for as long as nothing
    // interpolates between two of them. Anything that travels does.
    const int w = ((static_cast<int>(raw) % 4096) + 4096 + 2048) % 4096 - 2048;
    return static_cast<float>(w) * 0.087890625f;
}

void WorldCameras::read(std::span<const std::byte> b, std::size_t arrOff,
                        std::size_t cntOff, std::vector<WorldCamera>& out) {
    out.clear();
    if (b.size() < cntOff + 2 || b.size() < arrOff + 4) return;
    const std::size_t p = static_cast<std::uint32_t>(i32at(b, arrOff));
    const int n = i16at(b, cntOff);
    if (n <= 0 || p + 44u * static_cast<std::size_t>(n) > b.size()) return;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const std::size_t o = p + 44u * static_cast<std::size_t>(i);
        WorldCamera c;
        for (int k = 0; k < 3; ++k) {
            c.eye[k] = rawToWorld(i32at(b, o + 4u * static_cast<std::size_t>(k)));
            c.at[k]  = rawToWorld(i32at(b, o + 12u + 4u * static_cast<std::size_t>(k)));
        }
        c.id   = i16at(b, o + 24);
        c.mode = i16at(b, o + 26);
        c.roll = angle4096(i16at(b, o + 28));
        c.fov  = angle4096(i16at(b, o + 30));
        // `Camera_LoadParams` reads these the other way round from their file
        // order: block +38 (record +34) guards the EYE, block +40 (record +32)
        // guards the TARGET.
        c.atSubject  = i16at(b, o + 32);
        c.eyeSubject = i16at(b, o + 34);
        out.push_back(c);
    }
}

void WorldCameras::setArea(std::span<const std::byte> chunk)  { read(chunk, 64, 84, area_); }
void WorldCameras::setScene(std::span<const std::byte> chunk) { read(chunk, 32, 52, scene_); }
void WorldCameras::setGlobal(std::span<const std::byte> file) { read(file, 20, 30, global_); }

const WorldCamera* WorldCameras::find(int id) const {
    // The engine's order, and it is not the reference reader's: the chunk's
    // own tables win over GLOBAL.
    for (const auto* v : {&area_, &scene_, &global_})
        for (const auto& c : *v)
            if (c.id == id) return &c;
    return nullptr;
}

}  // namespace omk

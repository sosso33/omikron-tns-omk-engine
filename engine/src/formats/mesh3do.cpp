// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/mesh3do.h"

#include <algorithm>
#include <cstring>

namespace omk {
namespace {

// The file is little-endian; read explicitly rather than memcpy-ing a struct,
// so the layout is stated here and does not depend on the host or on padding.
std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(d[o    ]) |
        static_cast<std::uint32_t>(d[o + 1]) <<  8 |
        static_cast<std::uint32_t>(d[o + 2]) << 16 |
        static_cast<std::uint32_t>(d[o + 3]) << 24);
}

std::int16_t i16(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(d[o]) |
        static_cast<std::uint16_t>(d[o + 1]) << 8);
}

bool fits(std::span<const std::byte> d, std::size_t o, std::size_t n) {
    return o <= d.size() && n <= d.size() - o;
}

}  // namespace

std::optional<Mesh3doHeader> readHeader(std::span<const std::byte> d) {
    if (!fits(d, 0, 44)) return std::nullopt;
    if (std::memcmp(d.data(), "OD3X", 4) != 0) return std::nullopt;

    Mesh3doHeader h;
    h.major   = i32(d, 4);
    h.descOff = i32(d, 8);
    h.matOff  = i32(d, 12);
    h.vtxOff  = i32(d, 16);
    h.triOff  = i32(d, 20);
    h.quadOff = i32(d, 24);
    h.meshOff = i32(d, 28);
    h.doorOff = i32(d, 32);
    h.camOff  = i32(d, 36);
    h.lightOff = i32(d, 40);

    // the counts live off the descriptor, not at fixed file offsets
    const auto desc = static_cast<std::size_t>(h.descOff);
    if (h.descOff < 0 || !fits(d, desc + 188, 48)) return std::nullopt;
    h.triangles = i32(d, desc + 188);
    h.quads     = i32(d, desc + 192);
    h.vertices  = i32(d, desc + 196);
    h.materials = i32(d, desc + 208);
    h.cameras   = i32(d, desc + 220);
    h.meshes    = i32(d, desc + 224);
    h.doors     = i32(d, desc + 228);
    h.lights    = i32(d, desc + 232);
    return h;
}

std::optional<Material> readMaterial(std::span<const std::byte> d,
                                     const Mesh3doHeader& h, int i) {
    if (i < 0 || i >= h.materials || h.matOff < 0) return std::nullopt;
    const auto o = static_cast<std::size_t>(h.matOff) + 80u * static_cast<std::size_t>(i);
    if (!fits(d, o, 80)) return std::nullopt;

    Material m;
    for (int k = 0; k < 20; ++k) {
        const auto c = static_cast<char>(d[o + static_cast<std::size_t>(k)]);
        if (c == '\0') break;
        m.name[k] = c;
    }
    for (int k = 0; k < 20; ++k) {
        const auto c = static_cast<char>(d[o + 20u + static_cast<std::size_t>(k)]);
        if (c == '\0') break;
        m.texture[k] = c;
    }
    for (int k = 0; k < 20; ++k) {
        const auto c = static_cast<char>(d[o + 40u + static_cast<std::size_t>(k)]);
        if (c == '\0') break;
        m.palette[k] = c;
    }
    m.dataSize = i32(d, o + 60);
    m.slotOnDisk    = i32(d, o + 64);
    m.paletteOnDisk = i32(d, o + 68);
    m.bpp    = i32(d, o + 72);
    m.width  = i16(d, o + 76);
    m.height = i16(d, o + 78);
    return m;
}

namespace {

float f32(std::span<const std::byte> d, std::size_t o) {
    // bit-cast rather than reinterpret: the file's four bytes ARE the float,
    // and nothing here is allowed to round them
    const std::uint32_t bits =
        static_cast<std::uint32_t>(d[o    ])       |
        static_cast<std::uint32_t>(d[o + 1]) <<  8 |
        static_cast<std::uint32_t>(d[o + 2]) << 16 |
        static_cast<std::uint32_t>(d[o + 3]) << 24;
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

void name20(std::span<const std::byte> d, std::size_t o, char (&dst)[21]) {
    for (int k = 0; k < 20; ++k) {
        const auto c = static_cast<char>(d[o + static_cast<std::size_t>(k)]);
        if (c == '\0') break;
        dst[k] = c;
    }
}

// How many whole records of `stride` starting at `off` actually fit in `d`,
// capped at the header's own count. A truncated file yields fewer.
std::size_t fitting(std::span<const std::byte> d, std::int32_t off,
                    std::int32_t count, std::size_t stride) {
    if (off < 0 || count <= 0) return 0;
    const auto base = static_cast<std::size_t>(off);
    if (base >= d.size()) return 0;
    const std::size_t room = (d.size() - base) / stride;
    return std::min(room, static_cast<std::size_t>(count));
}

}  // namespace

std::vector<Mesh> readMeshes(std::span<const std::byte> d, const Mesh3doHeader& h) {
    std::vector<Mesh> out;
    const auto n = fitting(d, h.meshOff, h.meshes, kMeshRecord);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(h.meshOff) + kMeshRecord * i;
        Mesh m;
        m.index = static_cast<int>(i);
        m.flags = i32(d, o);
        // o+4 is unread: no consumer for it has been traced
        m.id    = i32(d, o + 8);
        name20(d, o + 16, m.name);
        m.pos[0] = f32(d, o + 36); m.pos[1] = f32(d, o + 40); m.pos[2] = f32(d, o + 44);
        m.parent = i32(d, o + 48);
        m.child  = i32(d, o + 52);
        m.next   = i32(d, o + 56);
        m.vertices  = i32(d, o + 64);
        m.triangles = i32(d, o + 68);
        m.quads     = i32(d, o + 72);
        m.radius    = f32(d, o + 88);
        m.local[0] = f32(d, o + 128); m.local[1] = f32(d, o + 132); m.local[2] = f32(d, o + 136);
        for (int k = 0; k < 3; ++k) {
            m.boxMin[k] = f32(d, o + 92u + 4u * static_cast<std::size_t>(k));
            m.boxMax[k] = f32(d, o + 104u + 4u * static_cast<std::size_t>(k));
        }
        out.push_back(m);
    }
    return out;
}

std::vector<Vertex> readVertices(std::span<const std::byte> d, const Mesh3doHeader& h) {
    std::vector<Vertex> out;
    const auto n = fitting(d, h.vtxOff, h.vertices, kVertexRecord);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(h.vtxOff) + kVertexRecord * i;
        Vertex v;
        v.p[0] = f32(d, o); v.p[1] = f32(d, o + 4); v.p[2] = f32(d, o + 8);
        v.b = static_cast<std::uint8_t>(d[o + 28]);
        v.g = static_cast<std::uint8_t>(d[o + 29]);
        v.r = static_cast<std::uint8_t>(d[o + 30]);
        v.a = static_cast<std::uint8_t>(d[o + 31]);
        out.push_back(v);
    }
    return out;
}

std::vector<Triangle> readTriangles(std::span<const std::byte> d, const Mesh3doHeader& h) {
    std::vector<Triangle> out;
    const auto n = fitting(d, h.triOff, h.triangles, kTriangleRecord);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(h.triOff) + kTriangleRecord * i;
        Triangle t;
        for (int k = 0; k < 3; ++k) t.idx[k] = i16(d, o + 2u * static_cast<std::size_t>(k));
        for (int k = 0; k < 6; ++k)
            t.uv[k] = static_cast<std::uint8_t>(d[o + 6u + static_cast<std::size_t>(k)]);
        t.material = i32(d, o + 12);
        out.push_back(t);
    }
    return out;
}

std::vector<Quad> readQuads(std::span<const std::byte> d, const Mesh3doHeader& h) {
    std::vector<Quad> out;
    const auto n = fitting(d, h.quadOff, h.quads, kQuadRecord);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(h.quadOff) + kQuadRecord * i;
        Quad q;
        for (int k = 0; k < 4; ++k) q.idx[k] = i16(d, o + 2u * static_cast<std::size_t>(k));
        for (int k = 0; k < 8; ++k)
            q.uv[k] = static_cast<std::uint8_t>(d[o + 8u + static_cast<std::size_t>(k)]);
        q.material = i32(d, o + 16);
        out.push_back(q);
    }
    return out;
}

std::vector<Camera> readCameras(std::span<const std::byte> d, const Mesh3doHeader& h) {
    std::vector<Camera> out;
    const auto n = fitting(d, h.camOff, h.cameras, kCameraRecord);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(h.camOff) + kCameraRecord * i;
        Camera c;
        name20(d, o, c.name);
        for (int k = 0; k < 3; ++k) c.pos[k]    = f32(d, o + 20u + 4u * static_cast<std::size_t>(k));
        for (int k = 0; k < 3; ++k) c.target[k] = f32(d, o + 32u + 4u * static_cast<std::size_t>(k));
        c.unknown = f32(d, o + 44);
        c.fov     = f32(d, o + 48);
        out.push_back(c);
    }
    return out;
}

}  // namespace omk

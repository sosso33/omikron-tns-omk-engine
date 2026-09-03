// SPDX-License-Identifier: GPL-3.0-or-later
// Dump every record of one .3DO in a fixed binary layout, so the differential
// against tools/mesh3do.py + tools/omkdata.py is byte-exact.
//
//     dump_mesh <model.3DO> <out.bin>
//
// Binary rather than text on purpose: the records carry floats, and a text
// dump would compare two float FORMATTERS rather than the parsed values. Here
// the four bytes are read out of the file and written back unchanged, so a
// mismatch is a parse difference and nothing else.
//
// Layout (all little-endian, matching the source files):
//   header    18 x int32, in the order Mesh3doHeader declares them
//   meshes    int32 n, then per mesh: flags,id,int32 x3 (parent,child,next),
//             int32 x3 (v,t,q), char[20] name, float[3] pos
//   vertices  int32 n, then per vertex: float[3], 4 bytes b,g,r,a
//   triangles int32 n, then per tri: int16[3], uint8[6], int32 material
//   quads     int32 n, then per quad: int16[4], uint8[8], int32 material
//   cameras   int32 n, then per cam: char[20], float[3], float[3], float, float
#include "formats/mesh3do.h"

#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {


struct Out {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void i16(std::int16_t v) {
        const auto u = static_cast<std::uint16_t>(v);
        b.push_back(static_cast<std::uint8_t>(u));
        b.push_back(static_cast<std::uint8_t>(u >> 8));
    }
    void i32(std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    }
    void f32(float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, sizeof u);
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    }
    void name(const char* s) {                    // 20 bytes, NUL-padded
        std::size_t i = 0;
        for (; i < 20 && s[i] != '\0'; ++i) b.push_back(static_cast<std::uint8_t>(s[i]));
        for (; i < 20; ++i) b.push_back(0);
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_mesh <model.3DO> <out.bin>\n");
        return 2;
    }
    const auto d = omk::DataFs::readPath(argv[1]);
    const auto h = omk::readHeader(d);
    if (!h) return 0;                 // not a .3DO: nothing to compare

    Out o;
    for (auto v : {h->major, h->descOff, h->matOff, h->vtxOff, h->triOff,
                   h->quadOff, h->meshOff, h->doorOff, h->camOff, h->lightOff,
                   h->triangles, h->quads, h->vertices, h->materials,
                   h->cameras, h->meshes, h->doors, h->lights})
        o.i32(v);

    const auto ms = omk::readMeshes(d, *h);
    o.i32(static_cast<std::int32_t>(ms.size()));
    for (const auto& m : ms) {
        o.i32(m.flags); o.i32(m.id);
        o.i32(m.parent); o.i32(m.child); o.i32(m.next);
        o.i32(m.vertices); o.i32(m.triangles); o.i32(m.quads);
        o.name(m.name);
        for (float f : m.pos) o.f32(f);
    }

    const auto vs = omk::readVertices(d, *h);
    o.i32(static_cast<std::int32_t>(vs.size()));
    for (const auto& v : vs) {
        for (float f : v.p) o.f32(f);
        o.u8(v.b); o.u8(v.g); o.u8(v.r); o.u8(v.a);
    }

    const auto ts = omk::readTriangles(d, *h);
    o.i32(static_cast<std::int32_t>(ts.size()));
    for (const auto& t : ts) {
        for (auto i : t.idx) o.i16(i);
        for (auto u : t.uv) o.u8(u);
        o.i32(t.material);
    }

    const auto qs = omk::readQuads(d, *h);
    o.i32(static_cast<std::int32_t>(qs.size()));
    for (const auto& q : qs) {
        for (auto i : q.idx) o.i16(i);
        for (auto u : q.uv) o.u8(u);
        o.i32(q.material);
    }

    const auto cs = omk::readCameras(d, *h);
    o.i32(static_cast<std::int32_t>(cs.size()));
    for (const auto& c : cs) {
        o.name(c.name);
        for (float f : c.pos) o.f32(f);
        for (float f : c.target) o.f32(f);
        o.f32(c.unknown); o.f32(c.fov);
    }

    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.b.data()),
            static_cast<std::streamsize>(o.b.size()));
    std::printf("%zu meshes %zu verts %zu tris %zu quads %zu cams -> %zu bytes\n",
                ms.size(), vs.size(), ts.size(), qs.size(), cs.size(), o.b.size());
    return 0;
}

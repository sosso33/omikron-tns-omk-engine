// SPDX-License-Identifier: GPL-3.0-or-later
// The .3DO container header - only as much as the texture reader needs.
//
// Written from the format as docs/FILE_FORMATS.md states it and as
// tools/mesh3do.py reads it, not from readable/. See engine/README.md rule 2.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <vector>

namespace omk {

// The header of a .3DO. Every count below is relative to the DESCRIPTOR at
// word 2 - which is 44 in all 635 shipped models and all 230 embedded
// sprites, but is read rather than assumed so that stays a fact.
struct Mesh3doHeader {
    std::int32_t major      = 0;
    std::int32_t descOff    = 0;
    std::int32_t matOff     = 0;   // the material table: 80 bytes a record
    std::int32_t vtxOff     = 0;
    std::int32_t triOff     = 0;
    std::int32_t quadOff    = 0;
    std::int32_t meshOff    = 0;
    std::int32_t doorOff    = 0;
    std::int32_t camOff     = 0;
    std::int32_t lightOff   = 0;
    std::int32_t triangles  = 0;
    std::int32_t quads      = 0;
    std::int32_t vertices   = 0;
    std::int32_t materials  = 0;
    std::int32_t cameras    = 0;
    std::int32_t meshes     = 0;
    std::int32_t doors      = 0;
    std::int32_t lights     = 0;
};

// -> the header, or nothing if `d` is not a .3DO ("OD3X") or is truncated.
// Returns rather than throws: an embedded sprite can be any bytes at all.
std::optional<Mesh3doHeader> readHeader(std::span<const std::byte> d);

// One material record, 80 bytes at matOff + 80*i.
struct Material {
    char         name[21]    = {};   // +0  char[20], the short name
    // +20 is the TEXTURE FILE NAME, and it is the texture cache's whole key:
    // `Tex3DT_BindMaterials` strcmps it against the 58 resident slot names and
    // on a hit skips the file's own pixels entirely. Compared and copied at
    // NINETEEN characters, so two names agreeing that far are one texture.
    char         texture[21] = {};   // +20 char[20]
    char         palette[21] = {};   // +40 char[20], the palette cache's key
    std::int32_t dataSize    = 0;    // +60: bytes of image data in the .3dt
    // +64 and +68 are the runtime texture / palette slots. They ship as -1 in
    // every material of every model, which is the proof they are runtime
    // fields - and +64's low half is the low six bits of the render bucket
    // key, so a reader that took them as data would name slot 63 of 58.
    std::int32_t slotOnDisk    = 0;  // +64
    std::int32_t paletteOnDisk = 0;  // +68
    std::int32_t bpp      = 0;    // +72: 4 => a 16-colour palette, else 256
    std::int16_t width    = 0;    // +76
    std::int16_t height   = 0;    // +78

    // How many bytes this material occupies in the `.3dt`: its palette then
    // its pixels. The walk over a `.3dt` is material by material in order.
    std::size_t bytesIn3dt() const {
        return static_cast<std::size_t>(bpp == 4 ? 16 : 256) * 3u +
               static_cast<std::size_t>(dataSize);
    }
};

// -> material `i`, or nothing if the record does not fit in `d`.
//
// +64 and +68 are skipped deliberately: they are RUNTIME slots, -1 in every
// shipped material. The loader stamps the texture slot into +64, and that is
// the low six bits of the render bucket key (docs/ASSETS.md 4b) - so a reader
// must not treat them as data.
std::optional<Material> readMaterial(std::span<const std::byte> d,
                                     const Mesh3doHeader& h, int i);

// ---------------------------------------------------------------- the nodes

// One mesh node, 140 bytes at meshOff + 140*i.
//
// 140, not the 136 the notes give: (doorOff - meshOff) / meshes divides evenly
// by 140 in every shipped .3DO, and only 140 keeps the names readable past the
// first one.
inline constexpr std::size_t kMeshRecord = 140;

struct Mesh {
    int          index    = 0;
    std::int32_t flags    = 0;
    std::int32_t id       = 0;
    char         name[21] = {};
    float        pos[3]   = {0, 0, 0};   // absolute, not relative to a parent
    std::int32_t parent   = 0;
    std::int32_t child    = 0;
    std::int32_t next     = 0;
    std::int32_t vertices = 0;
    std::int32_t triangles = 0;
    std::int32_t quads    = 0;
    // The BOUNDING VOLUME the visible-set walk culls against. `sub_48D3B0`
    // takes the sphere centre from `pos` above (+36) and the radius from +88 -
    // so what this reader calls the mesh's position IS the sphere's centre.
    //
    // The radius is `max |v|` over the mesh's own vertex block in 12039 of the
    // 16176 meshes that have one, and 11745 of DECORS' 12191 - which is what
    // identifies the field. It is not universal: 446 decor meshes store a
    // radius SMALLER than their own extent, and they are duplicated props
    // (`Tasse80` and `Tasse90` share a radius and compute different extents),
    // so the residue looks like an authoring artifact of instanced geometry
    // rather than a second rule. Neither the box diagonal nor its largest
    // half-extent explains 433 of them. Left open, and bounded by a check.
    float radius = 0.0f;      // +88
    float boxMin[3] = {0, 0, 0};   // +92,  and symmetric about the origin
    float boxMax[3] = {0, 0, 0};   // +104
};

// ------------------------------------------------------------- the geometry

// 32 bytes at vtxOff + 32*i. The dword at +28 is a D3DCOLOR and reaches the
// screen as a COLOUR, not a brightness - Raster_DrawTriangles declares
// D3DFVF_DIFFUSE (docs/ASSETS.md 4c). Byte +28 is blue, +29 green, +30 red.
inline constexpr std::size_t kVertexRecord = 32;

struct Vertex {
    float        p[3] = {0, 0, 0};
    std::uint8_t b = 0, g = 0, r = 0, a = 0;
};

// 28 bytes at triOff + 28*t: three int16 indices, six UV bytes at +6, the
// material at +12. A NEGATIVE index means the vertex belongs to an ancestor
// mesh; resolving that is the geometry builder's job, not the reader's.
inline constexpr std::size_t kTriangleRecord = 28;

struct Triangle {
    std::int16_t idx[3] = {0, 0, 0};
    std::uint8_t uv[6]  = {};
    std::int32_t material = 0;
};

// 32 bytes at quadOff + 32*q: four int16 indices, eight UV bytes at +8, the
// material at +16. The four indices are in perimeter order.
inline constexpr std::size_t kQuadRecord = 32;

struct Quad {
    std::int16_t idx[4] = {0, 0, 0, 0};
    std::uint8_t uv[8]  = {};
    std::int32_t material = 0;
};

// 52 bytes at camOff + 52*i: name[20], eye at +20, target at +32, an
// unexplained float at +44, the field of view at +48. Any other stride turns
// the names to noise after the first record, which is how 52 was checked.
inline constexpr std::size_t kCameraRecord = 52;

struct Camera {
    char  name[21]  = {};
    float pos[3]    = {0, 0, 0};
    float target[3] = {0, 0, 0};
    float unknown   = 0;
    float fov       = 0;
};

// Each returns as many whole records as `d` actually holds, stopping short
// rather than reading past the end - a truncated file yields a short vector.
std::vector<Mesh>     readMeshes(std::span<const std::byte> d, const Mesh3doHeader& h);
std::vector<Vertex>   readVertices(std::span<const std::byte> d, const Mesh3doHeader& h);
std::vector<Triangle> readTriangles(std::span<const std::byte> d, const Mesh3doHeader& h);
std::vector<Quad>     readQuads(std::span<const std::byte> d, const Mesh3doHeader& h);
std::vector<Camera>   readCameras(std::span<const std::byte> d, const Mesh3doHeader& h);

}  // namespace omk

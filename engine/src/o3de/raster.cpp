// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/raster.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace omk {
namespace {

void sub3(const float a[3], const float b[3], float o[3]) {
    for (int k = 0; k < 3; ++k) o[k] = a[k] - b[k];
}
void cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void norm3(float v[3]) {
    const float m = std::sqrt(dot3(v, v));
    if (m > 0) { v[0] /= m; v[1] /= m; v[2] /= m; }
}

struct Basis {
    float f[3], s[3], u[3];
    float tanh_ = 0, tanv_ = 0;
};

Basis basisOf(const RCamera& c) {
    Basis b;
    sub3(c.at, c.eye, b.f);
    norm3(b.f);
    float up[3] = {0.0f, -1.0f, 0.0f};       // the game's Y points DOWN
    cross3(b.f, up, b.s);
    norm3(b.s);
    if (c.mirror) for (int k = 0; k < 3; ++k) b.s[k] = -b.s[k];
    cross3(b.s, b.f, b.u);
    // THE ROLL, about the forward axis and in the engine's own sense - see
    // `RCamera::rollDeg` for where that sense comes from. Both axes turn; the
    // third term of Rodrigues drops out because each is perpendicular to `f`.
    if (c.rollDeg != 0.0f) {
        const float a = c.rollDeg * 3.14159265358979323846f / 180.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        float fs[3], fu[3];
        cross3(b.f, b.s, fs);
        cross3(b.f, b.u, fu);
        for (int k = 0; k < 3; ++k) {
            const float sk = b.s[k] * ca + fs[k] * sa;
            const float uk = b.u[k] * ca + fu[k] * sa;
            b.s[k] = sk; b.u[k] = uk;
        }
    }
    // AFTER `u`, so only x flips - see `RCamera::flipX`.
    if (c.flipX) for (int k = 0; k < 3; ++k) b.s[k] = -b.s[k];
    b.tanh_ = std::tan(c.hfovDeg * 3.14159265358979323846f / 180.0f / 2.0f);
    // camshot.py: tanv = tanh / (W/H). The horizontal fov is the record's, and
    // the vertical follows from the frame's shape - reading angle[1] as
    // vertical is one of the two errors laying this over a screenshot found.
    b.tanv_ = b.tanh_ / (static_cast<float>(c.w) / static_cast<float>(c.h));
    return b;
}

}  // namespace

namespace {

// The near cut. `camshot.py` cuts at z <= 1, not z <= 0: a vertex exactly on
// the eye plane divides by zero and one just in front produces a coordinate
// off in the millions, which rasterises as a garbage span.
constexpr float kNear = kNearCut;   // one value, shared with every backend

// World -> VIEW space: along the camera's own right, up and forward. Split out
// of `project` so the near-plane clipper has a space to work in - a polygon
// must be cut BEFORE the perspective divide, because after it the vertices
// behind the eye have already become nonsense.
void toView(const Basis& b, const RCamera& c, const float p[3], float v[3]) {
    float d[3];
    sub3(p, c.eye, d);
    v[0] = dot3(d, b.s);
    v[1] = dot3(d, b.u);
    v[2] = dot3(d, b.f);
}

// VIEW space -> pixels. The guard is `camshot.py`'s and the public `project`
// keeps it, because that boundary is what the tier-3 differential compares.
Projected toScreen(const Basis& b, const RCamera& c, const float v[3]) {
    Projected out;
    out.z = v[2];
    if (out.z <= kNear) return out;
    out.x = c.w * 0.5f * (1.0f + (v[0] / out.z) / b.tanh_);
    out.y = c.h * 0.5f * (1.0f - (v[1] / out.z) / b.tanv_);
    out.ahead = true;
    return out;
}

// The same, for a vertex the near clip has already vouched for - and the
// clamp is the whole reason this is a second function.
//
// The clipper solves `t = (kNear - a.z) / (b.z - a.z)` and then evaluates
// `a.z + (b.z - a.z) * t`, which in float does NOT come back as exactly
// kNear. A vertex a hair under it fails `toScreen`'s guard, projects to
// (0, 0) and drags its triangle to the top-left corner of the frame. That is
// not a subtle error: it painted dark wedges across half the picture, and it
// was found by LOOKING at one frame - the fix for a dropped-triangle bug
// producing a worse artefact than the bug, with every count still plausible.
Projected toScreenClipped(const Basis& b, const RCamera& c, const float v[3]) {
    Projected out;
    out.z = std::max(v[2], kNear);
    out.x = c.w * 0.5f * (1.0f + (v[0] / out.z) / b.tanh_);
    out.y = c.h * 0.5f * (1.0f - (v[1] / out.z) / b.tanv_);
    out.ahead = true;
    return out;
}

}  // namespace

void cameraBasis(const RCamera& c, float s[3], float u[3], float f[3],
                 float& tanHalfH, float& tanHalfV) {
    const Basis b = basisOf(c);
    for (int k = 0; k < 3; ++k) { s[k] = b.s[k]; u[k] = b.u[k]; f[k] = b.f[k]; }
    tanHalfH = b.tanh_;
    tanHalfV = b.tanv_;
}

Projected project(const RCamera& c, const float p[3]) {
    const Basis b = basisOf(c);
    float v[3];
    toView(b, c, p, v);
    return toScreen(b, c, v);
}

void clearDepth(std::vector<float>& depth, int w, int h) {
    depth.assign(static_cast<std::size_t>(w) * h,
                 std::numeric_limits<float>::infinity());
}

namespace {

// Texel fetch. `Corner::u/v` are in the material's own PIXEL units already
// (geom3do.h), so no scaling - only the wrap, which is what lets one atlas
// tile across a wall.
inline void sample(const Texture& t, float u, float v,
                   int& r, int& g, int& b) {
    int x = static_cast<int>(u) % t.width;
    int y = static_cast<int>(v) % t.height;
    if (x < 0) x += t.width;
    if (y < 0) y += t.height;
    const std::size_t i = (static_cast<std::size_t>(y) * t.width + x) * 3;
    r = t.rgb[i]; g = t.rgb[i + 1]; b = t.rgb[i + 2];
}

}  // namespace

namespace {

// A vertex mid-clip: view-space position and the attributes that have to be
// carried across the cut with it. `t` is the texture's V - `v` is taken by the
// position - and getting those two crossed puts a set's textures through a
// blender while every count stays right.
struct ClipVert {
    float v[3]{};
    float u = 0, t = 0;
    float r = 0, g = 0, b = 0;
};

ClipVert lerp(const ClipVert& a, const ClipVert& b, float f) {
    ClipVert o;
    for (int k = 0; k < 3; ++k) o.v[k] = a.v[k] + (b.v[k] - a.v[k]) * f;
    o.u = a.u + (b.u - a.u) * f;  o.t = a.t + (b.t - a.t) * f;
    o.r = a.r + (b.r - a.r) * f;  o.g = a.g + (b.g - a.g) * f;
    o.b = a.b + (b.b - a.b) * f;
    return o;
}

}  // namespace

RasterStats drawGeometry(Surface& fb, std::vector<float>& depth,
                         const RCamera& cam, const Geometry& g,
                         std::span<const Texture> textures) {
    RasterStats st;
    if (depth.size() != static_cast<std::size_t>(fb.w) * fb.h)
        clearDepth(depth, fb.w, fb.h);
    // One basis for the whole frame. `project` rebuilds it per call, which is
    // fine for the sampled diagnostics but is three normalises and two crosses
    // a VERTEX in here.
    const Basis basis = basisOf(cam);

    // Batches come out of `buildGeometry` in the engine's own draw order -
    // opaque, then additive, then multiply, by material within each - which is
    // `Render_FlushBuckets` walking 0x4000 buckets ascending. Walking them in
    // any other order is a different picture wherever things overlap, so the
    // order is consumed rather than re-derived.
    for (const auto& batch : g.batches) {
        const Texture* tex = nullptr;
        if (batch.material >= 0 &&
            static_cast<std::size_t>(batch.material) < textures.size()) {
            const Texture& t = textures[static_cast<std::size_t>(batch.material)];
            if (t.width > 0 && t.height > 0 && !t.rgb.empty()) tex = &t;
        }
        for (std::size_t i = batch.start; i + 2 < batch.start + batch.count; i += 3) {
            ++st.triangles;
            const Corner* src[3] = {&g.corners[i], &g.corners[i + 1], &g.corners[i + 2]};

            // NEAR-PLANE CLIP, and it is the difference between a renderer and
            // a demo. This used to reject the whole triangle when ANY vertex
            // was behind the cut, which from a camera standing inside a room
            // threw away 2498 of Aapkayl's 3419 triangles - 73% - because a
            // floor, a ceiling or a long wall almost always has one corner
            // behind you. The symptom is faces vanishing whole while plainly
            // still in shot, which is what a player reported the first time
            // this was put on a screen; every number in the tree agreed with
            // itself while it was happening, which is CLAUDE.md 1's rule
            // exactly.
            //
            // D3D clipped, so dropping is not a reading of anything - it was a
            // gap in this reference implementation. Sutherland-Hodgman against
            // the single plane z > kNear: a triangle comes back as 0, 3 or 4
            // vertices, and 4 is fanned into two triangles.
            ClipVert poly[4];
            int nPoly = 0;
            float in3[3] = {0, 0, 0};      // the three ORIGINAL view depths
            {
                ClipVert in[3];
                for (int k = 0; k < 3; ++k) {
                    const float w[3] = {src[k]->x, src[k]->y, src[k]->z};
                    toView(basis, cam, w, in[k].v);
                    in[k].u = src[k]->u; in[k].t = src[k]->v;
                    in[k].r = src[k]->r; in[k].g = src[k]->g; in[k].b = src[k]->b;
                    in3[k] = in[k].v[2];
                }
                for (int k = 0; k < 3 && nPoly < 4; ++k) {
                    const ClipVert& a = in[k];
                    const ClipVert& b2 = in[(k + 1) % 3];
                    const bool ina = a.v[2] > kNear, inb = b2.v[2] > kNear;
                    if (ina) poly[nPoly++] = a;
                    if (ina != inb && nPoly < 4) {
                        const float t = (kNear - a.v[2]) / (b2.v[2] - a.v[2]);
                        poly[nPoly++] = lerp(a, b2, t);
                    }
                }
            }
            if (nPoly < 3) { ++st.behind; continue; }
            // The old rule, for the differential: a straddling triangle was
            // dropped rather than cut. `nPoly > 3` is exactly "the clip had to
            // do something", so this reproduces it without a second code path.
            if (cam.noClip && nPoly > 3) { ++st.behind; continue; }
            if (cam.noClip) {
                bool anyBehind = false;
                for (int k = 0; k < 3; ++k)
                    anyBehind = anyBehind || in3[k] <= kNear;
                if (anyBehind) { ++st.behind; continue; }
            }

            // The fan: (0,1,2) and, when the clip produced a quad, (0,2,3).
            for (int tri = 0; tri + 2 < nPoly; ++tri) {
            const ClipVert* c[3] = {&poly[0], &poly[tri + 1], &poly[tri + 2]};
            Projected p[3];
            for (int k = 0; k < 3; ++k) p[k] = toScreenClipped(basis, cam, c[k]->v);

            int x0 = static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x})));
            int x1 = static_cast<int>(std::ceil (std::max({p[0].x, p[1].x, p[2].x})));
            int y0 = static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y})));
            int y1 = static_cast<int>(std::ceil (std::max({p[0].y, p[1].y, p[2].y})));
            if (x1 < 0 || y1 < 0 || x0 >= fb.w || y0 >= fb.h) { ++st.offscreen; continue; }
            x0 = std::max(x0, 0); y0 = std::max(y0, 0);
            x1 = std::min(x1, fb.w - 1); y1 = std::min(y1, fb.h - 1);

            const float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) -
                               (p[2].x - p[0].x) * (p[1].y - p[0].y);
            if (area == 0.0f) { ++st.offscreen; continue; }
            const float inv = 1.0f / area;
            // No back-face cull: `Raster_DrawTriangles` sets D3DCULL_NONE
            // (ASSETS 4b - it is what makes the AApub prism's coincident faces
            // a tie-break question at all), so both windings draw.
            ++st.drawn;

            // Perspective-correct interpolation needs the reciprocal depths.
            const float iz[3] = {1.0f / p[0].z, 1.0f / p[1].z, 1.0f / p[2].z};

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    float w0 = ((p[1].x - px) * (p[2].y - py) - (p[2].x - px) * (p[1].y - py)) * inv;
                    float w1 = ((p[2].x - px) * (p[0].y - py) - (p[0].x - px) * (p[2].y - py)) * inv;
                    float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;

                    const float izp = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
                    if (izp <= 0) continue;
                    const float z = 1.0f / izp;
                    const std::size_t di = static_cast<std::size_t>(y) * fb.w + x;
                    if (z >= depth[di]) { ++st.depthRejects; continue; }

                    const float a0 = w0 * iz[0] / izp, a1 = w1 * iz[1] / izp,
                                a2 = w2 * iz[2] / izp;
                    // The baked light is a COLOUR, not a brightness (ASSETS
                    // 4c): 38.9% of set vertices are not grey, and reading the
                    // green byte alone renders every set in monochrome.
                    float lr = a0 * c[0]->r + a1 * c[1]->r + a2 * c[2]->r;
                    float lg = a0 * c[0]->g + a1 * c[1]->g + a2 * c[2]->g;
                    float lb = a0 * c[0]->b + a1 * c[1]->b + a2 * c[2]->b;

                    int tr = 255, tg = 255, tb = 255;
                    if (tex) {
                        const float u = a0 * c[0]->u + a1 * c[1]->u + a2 * c[2]->u;
                        const float v = a0 * c[0]->t + a1 * c[1]->t + a2 * c[2]->t;
                        sample(*tex, u, v, tr, tg, tb);
                        // Flag 0x800: the CUTOUT path, which is a colour key
                        // and not alpha - black is the key the engine's
                        // SetRenderState(27, 1) arm uses.
                        if (batch.cutout && tr == 0 && tg == 0 && tb == 0) continue;
                    }

                    int r = static_cast<int>(tr * lr);
                    int gg = static_cast<int>(tg * lg);
                    int b = static_cast<int>(tb * lb);
                    r = std::clamp(r, 0, 255);
                    gg = std::clamp(gg, 0, 255);
                    b = std::clamp(b, 0, 255);

                    const std::uint16_t dst = fb.at(x, y);
                    std::uint16_t out;
                    if (batch.blend == Blend::Add) {
                        // additive: 0x1000|0x2000, 211 meshes (ASSETS 4b)
                        const int dr = ((dst >> 11) & 0x1F) * 255 / 31;
                        const int dg = ((dst >> 5) & 0x3F) * 255 / 63;
                        const int db = (dst & 0x1F) * 255 / 31;
                        out = quantise888(std::min(255, r + dr), std::min(255, gg + dg),
                                          std::min(255, b + db));
                    } else if (batch.blend == Blend::Mul) {
                        // multiply: 0x1000|0x4000, 6 meshes and the mode-6
                        // sprites. `Raster_DrawTriangles` sets SRCBLEND=ZERO
                        // (D3DBLEND 1) and DESTBLEND=INVSRCCOLOR (4), so the
                        // pass is `dst * (1 - src)`: it DARKENS where the
                        // source is bright and leaves the frame alone where
                        // it is black. `dst * src` - what this did until
                        // 2026-09-02 - is the complementary picture, and the
                        // intro's dark starburst is what told them apart.
                        const int dr = ((dst >> 11) & 0x1F) * 255 / 31;
                        const int dg = ((dst >> 5) & 0x3F) * 255 / 63;
                        const int db = (dst & 0x1F) * 255 / 31;
                        out = quantise888(dr * (255 - r) / 255, dg * (255 - gg) / 255,
                                          db * (255 - b) / 255);
                    } else {
                        out = quantise888(r, gg, b);
                    }
                    fb.set(x, y, out);
                    // Only an opaque pass owns the depth - a transparent one
                    // that wrote it would hide whatever comes after it in the
                    // same bucket order.
                    if (batch.blend == Blend::Opaque) depth[di] = z;
                    ++st.pixels;
                }
            }
            }   // the clip fan
        }
    }
    std::uint32_t h = 2166136261u;
    for (auto px : fb.px) {
        h = (h ^ (px & 0xFF)) * 16777619u;
        h = (h ^ (px >> 8)) * 16777619u;
    }
    st.hash = h;
    return st;
}

}  // namespace omk

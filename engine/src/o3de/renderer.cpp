// SPDX-License-Identifier: GPL-3.0-or-later
// The REFERENCE renderer behind `PORTING` A2's boundary.
//
// There is deliberately almost nothing here. `drawGeometry` already draws a
// whole `Geometry` in the engine's own batch order, so the software side of
// the boundary is bookkeeping: hold the framebuffer and the depth buffer,
// accumulate one submission's worth of triangles at a time, and sum the stats.
//
// The one thing worth stating is what a `submit` MAY NOT do: re-order. The
// caller hands submissions down in `Render_FlushBuckets`'s ascending key order
// and this draws them in the order given. A renderer that sorted by anything
// of its own - depth, texture, material - would draw a different picture
// wherever two surfaces overlap, and the Anekbah signs are the standing proof
// that this data has coincident faces whose tie-break is decided by exactly
// that order.
#include "o3de/renderer.h"

#include <algorithm>
#include <vector>

namespace omk {

bool SoftwareRenderer::init(int w, int h) {
    fb_ = Surface(w, h, 0);
    clearDepth(depth_, w, h);
    return true;
}

void SoftwareRenderer::begin(const View& v) {
    view_ = v;
    // The picture is drawn into the top-left `vw x vh` of the framebuffer -
    // its vertical fov follows from that height, which is what makes the
    // letterbox a viewport rather than two black bars. The caller places it.
    view_.cam.w = v.letterboxed() ? v.vw : fb_.w;
    view_.cam.h = v.letterboxed() ? v.vh : fb_.h;
    std::fill(fb_.px.begin(), fb_.px.end(), std::uint16_t(0));
    clearDepth(depth_, fb_.w, fb_.h);
    st_ = RasterStats{};
}

void SoftwareRenderer::submit(const Draw& d) {
    if (!d.geo || d.count == 0) return;
    // One submission is one batch, so it is handed to `drawGeometry` as a
    // Geometry of exactly that range. The corners are copied rather than
    // aliased because `Geometry` owns its vector; the batch is rebuilt with
    // the submission's own blend and cutout, which are the caller's ported
    // decisions and not re-derived here.
    Geometry one;
    one.corners.assign(d.geo->corners.begin() + static_cast<std::ptrdiff_t>(d.start),
                       d.geo->corners.begin() + static_cast<std::ptrdiff_t>(d.start + d.count));
    Batch b;
    // The texture is the bucket key's LOW SIX BITS - ASSETS 4b, and the whole
    // reason a backend is handed a key. Resolving it any other way here would
    // make this renderer disagree with the engine about which atlas a material
    // samples, which is the Anekbah mechanism.
    b.material = static_cast<std::int32_t>(d.bucketKey & 0x3F);
    b.blend = d.blend;
    b.cutout = d.cutout;
    b.start = 0;
    b.count = one.corners.size();
    one.batches.push_back(b);

    const RasterStats s = drawGeometry(fb_, depth_, view_.cam, one, tex_);
    st_.triangles += s.triangles;
    st_.drawn += s.drawn;
    st_.behind += s.behind;
    st_.offscreen += s.offscreen;
    st_.pixels += s.pixels;
    st_.depthRejects += s.depthRejects;
    st_.hash = s.hash;   // the last one wins; the frame's hash is the final image
}

namespace {

// The draws a geometry yields, split by whether the corners came from the
// MIRROR mesh. A batch groups by (blend, material, cutout), so a batch can hold
// both - the mirror shares a material with the wall around it - and the split
// has to be by RUN inside the batch, not by batch.
void splitDraws(const Geometry& g, std::vector<Draw>& scene,
                std::vector<Draw>& mirror) {
    const bool haveFlags = g.cornerMirror.size() == g.corners.size();
    for (const auto& b : g.batches) {
        std::size_t i = b.start;
        while (i < b.start + b.count) {
            const std::uint8_t m = haveFlags ? g.cornerMirror[i] : 0u;
            std::size_t j = i;
            while (j < b.start + b.count &&
                   (haveFlags ? g.cornerMirror[j] : 0u) == m) ++j;
            Draw d;
            d.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
            d.geo = &g; d.start = i; d.count = j - i;
            d.blend = b.blend; d.cutout = b.cutout;
            (m ? mirror : scene).push_back(d);
            i = j;
        }
    }
}

// The draws of `in`, restricted to the triangles IN FRONT of the mirror's
// plane.
//
// Without this the reflection pass draws the wall the mirror is set into, and
// from behind that wall occludes the entire reflected view - the reflection
// comes back black and every variation of the pass produces a byte-identical
// frame, which is how this was found. A planar mirror has to clip its
// reflected scene to its own plane; that is standard and it is not something
// the engine was traced doing, so it is flagged as reconstruction like the
// mask.
//
// The test is per TRIANGLE and keeps any triangle with a vertex more than
// `eps` in front, so geometry meeting the mirror is kept while the mirror's
// own wall - which lies ON the plane - is not.
std::vector<Draw> inFrontOf(const std::vector<Draw>& in, const Geometry& g,
                            const float n[3], float d, float eps) {
    std::vector<Draw> out;
    for (const auto& src : in) {
        std::size_t runStart = src.start;
        std::size_t runLen = 0;
        for (std::size_t t = src.start; t + 2 < src.start + src.count; t += 3) {
            float best = -1e30f;
            for (int k = 0; k < 3; ++k) {
                const Corner& c = g.corners[t + static_cast<std::size_t>(k)];
                best = std::max(best, n[0] * c.x + n[1] * c.y + n[2] * c.z + d);
            }
            if (best > eps) {
                if (!runLen) runStart = t;
                runLen += 3;
            } else if (runLen) {
                Draw o = src; o.start = runStart; o.count = runLen;
                out.push_back(o); runLen = 0;
            }
        }
        if (runLen) { Draw o = src; o.start = runStart; o.count = runLen; out.push_back(o); }
    }
    return out;
}

void runPass(Renderer& r, const View& v, const std::vector<Draw>& ds) {
    r.begin(v);
    for (const auto& d : ds) r.submit(d);
    r.end();
}

}  // namespace

MirrorStats drawWithMirror(Renderer& r, const Geometry& g,
                           std::span<const Texture> tex, const View& v,
                           const MirrorPlane& mp) {
    MirrorStats st;
    // NOT `setTextures` - that is a LOAD-time call, and calling it per frame
    // crashed the viewer. The software renderer only stores a span, so it
    // looked harmless; the Vulkan one allocates a VkImage and a descriptor set
    // per texture, so a per-frame call exhausted the 128-set pool in a handful
    // of frames and then bound a set that had failed to allocate. The caller
    // sets the textures when it loads the set, which is what the interface
    // says (`Renderer::setTextures`).
    (void)tex;

    std::vector<Draw> scene, mirror;
    splitDraws(g, scene, mirror);

    // No mirror in this set, or none visible: one pass, exactly as before.
    if (!mp.found || mirror.empty()) {
        std::vector<Draw> all = scene;
        all.insert(all.end(), mirror.begin(), mirror.end());
        runPass(r, v, all);
        return st;
    }

    // The plane, and the camera's SIGNED DISTANCE to it. `sub_440D90` gives up
    // when that is not positive - a mirror seen from behind costs nothing.
    const float* n = mp.normal;
    const float d = -(n[0] * mp.point[0] + n[1] * mp.point[1] + n[2] * mp.point[2]);
    const float distEye = n[0] * v.cam.eye[0] + n[1] * v.cam.eye[1] +
                          n[2] * v.cam.eye[2] + d;
    st.distance = distEye;
    if (distEye <= 0.0f) {
        std::vector<Draw> all = scene;
        all.insert(all.end(), mirror.begin(), mirror.end());
        runPass(r, v, all);
        return st;
    }
    st.active = true;

    // ---- the NATIVE path, when the backend has one.
    //
    // Everything below this point is the CPU fallback: three passes and a
    // difference, which works on any backend and is what the software
    // reference does. A backend that can confine the reflection itself gets
    // handed the same decisions and does it in one pass.
    {
        View rvn = v;
        const float dAt = n[0] * v.cam.at[0] + n[1] * v.cam.at[1] +
                          n[2] * v.cam.at[2] + d;
        for (int k = 0; k < 3; ++k) {
            rvn.cam.eye[k] = v.cam.eye[k] - 2.0f * distEye * n[k];
            rvn.cam.at[k]  = v.cam.at[k]  - 2.0f * dAt     * n[k];
        }
        rvn.cam.flipX = !v.cam.flipX;
        const auto clipped = inFrontOf(scene, g, n, d, 1.0f);
        if (r.drawMirrorScene(v, rvn, scene, clipped, mirror)) {
            st.native = true;     // on the GPU; nothing to composite, nothing to upload
            return st;
        }
    }

    // ---- pass 1: the REFLECTED scene.
    //
    // `p -= 2 * dist * n` for both the eye and the target, and `mirror = true`
    // for the screen-X flip `Raster_DrawTriangles` performs while the pass flag
    // is set. The mirror's own faces are left out: a mirror does not occlude
    // what it reflects.
    View rv = v;
    const float distAt = n[0] * v.cam.at[0] + n[1] * v.cam.at[1] +
                         n[2] * v.cam.at[2] + d;
    for (int k = 0; k < 3; ++k) {
        rv.cam.eye[k] = v.cam.eye[k] - 2.0f * distEye * n[k];
        rv.cam.at[k]  = v.cam.at[k]  - 2.0f * distAt  * n[k];
    }
    // The screen-X flip, not a 180-degree rotation - see `RCamera::flipX`.
    rv.cam.flipX = !v.cam.flipX;
    runPass(r, rv, inFrontOf(scene, g, n, d, 1.0f));
    const Surface reflection = r.readback();   // a copy: the next pass reuses it

    // ---- pass 2: the scene WITHOUT the mirror, and pass 3 WITH it.
    //
    // Two passes rather than drawing the mirror alone, because a mirror drawn
    // alone is not DEPTH-TESTED against the room: the plane is large and
    // mostly hidden behind the wall it is set into, so a lone render marks
    // every pixel the plane covers and the reflection then replaces the whole
    // wall. That is exactly what the first version did - the wall vanished and
    // a corridor appeared through it - and it was one look at one frame that
    // said so.
    //
    // Differencing the two passes gives the mirror's visible pixels with the
    // room's own depth already applied, using nothing but the boundary.
    runPass(r, v, scene);
    const Surface without = r.readback();
    std::vector<Draw> all = scene;
    all.insert(all.end(), mirror.begin(), mirror.end());
    runPass(r, v, all);
    const Surface& out = r.readback();

    // ---- composite.
    //
    // The mirror is BLENDED over whatever is behind it, so `with` is
    // `wall (+) mirror` and the mirror's own contribution is `with - without`.
    // Re-compositing that over the REFLECTION instead of over the wall is
    // therefore `with - without + reflection`, which is exact for the additive
    // mode - the shipped case in Aapkayl, and 211 of the game's 217 blended
    // meshes. For the multiply mode (6 meshes, `AB_mirror` among them) the
    // subtraction is not the inverse and this is an approximation; it is
    // flagged rather than hidden, and no check claims otherwise.
    Surface& fb = const_cast<Surface&>(out);
    const auto chan = [](std::uint16_t p, int k) {
        return k == 0 ? ((p >> 11) & 0x1F) * 255 / 31
             : k == 1 ? ((p >> 5) & 0x3F) * 255 / 63
                      : (p & 0x1F) * 255 / 31;
    };
    for (std::size_t i = 0; i < fb.px.size(); ++i) {
        if (fb.px[i] == without.px[i]) continue;   // the mirror drew nothing here
        ++st.maskPixels;
        int c[3];
        for (int k = 0; k < 3; ++k)
            c[k] = std::clamp(chan(fb.px[i], k) - chan(without.px[i], k) +
                              chan(reflection.px[i], k), 0, 255);
        fb.px[i] = rgb565(c[0], c[1], c[2]);
    }
    return st;
}

}  // namespace omk

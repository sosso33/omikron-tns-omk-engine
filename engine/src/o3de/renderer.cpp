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
#include "o3de/floorreflect.h"   // kWetPlaneBand
#include "platform/profile.h"
#include "platform/env.h"
#include <cstdio>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace omk {

bool SoftwareRenderer::init(int w, int h) {
    fb_ = Surface(w, h, 0);
    clearDepth(depth_, w, h);
    return true;
}

void SoftwareRenderer::begin(const View& v) {
    OMK_ZONE("renderer: begin");   // the profiler (todo/debug-tools.md 6)
    view_ = v;
    // The picture is drawn into the top-left `vw x vh` of the framebuffer -
    // its vertical fov follows from that height, which is what makes the
    // letterbox a viewport rather than two black bars. The caller places it.
    view_.cam.w = v.letterboxed() ? v.vw : fb_.w;
    view_.cam.h = v.letterboxed() ? v.vh : fb_.h;
    // the clear colour in RGB565, truncated as the 565 surface takes it
    const std::uint16_t clear565 = static_cast<std::uint16_t>(
        ((v.clearColour[0] >> 3) << 11) | ((v.clearColour[1] >> 2) << 5) | (v.clearColour[2] >> 3));
    std::fill(fb_.px.begin(), fb_.px.end(), clear565);
    clearDepth(depth_, fb_.w, fb_.h);
    st_ = RasterStats{};
}

void SoftwareRenderer::submit(const Draw& d) {
    OMK_ZONE("renderer: submit");   // the profiler (todo/debug-tools.md 6)
    if (!d.geo || d.count == 0) return;
    // One submission is one batch, so it is handed to `drawGeometry` as a
    // Geometry of exactly that range. The corners are copied rather than
    // aliased because `Geometry` owns its vector; the batch is rebuilt with
    // the submission's own blend and cutout, which are the caller's ported
    // decisions and not re-derived here.
    Geometry one;
    one.corners.assign(d.geo->corners.begin() + static_cast<std::ptrdiff_t>(d.start),
                       d.geo->corners.begin() + static_cast<std::ptrdiff_t>(d.start + d.count));
    // ...and which of them are single-sided, so the range keeps the cull.
    if (d.geo->cornerCull.size() == d.geo->corners.size())
        one.cornerCull.assign(d.geo->cornerCull.begin() + static_cast<std::ptrdiff_t>(d.start),
                              d.geo->cornerCull.begin() + static_cast<std::ptrdiff_t>(d.start + d.count));
    Batch b;
    // The texture is the bucket key's LOW SIX BITS - ASSETS 4b, and the whole
    // reason a backend is handed a key. Resolving it any other way here would
    // make this renderer disagree with the engine about which atlas a material
    // samples, which is the Anekbah mechanism.
    b.material = static_cast<std::int32_t>(drawTextureSlot(d));
    b.blend = d.blend;
    b.cutout = d.cutout;
    b.start = 0;
    b.count = one.corners.size();
    one.batches.push_back(b);

    // THE FOG's two exclusions are applied HERE, because this is where the
    // bucket key is - `renderer.h`'s View documents them. Key bits 0x2080 (the
    // near bucket and the transparent state) are not fogged at all, and key
    // bit 0x800 (the sky's state, assigned by mesh flag 0x10000 - NOT the
    // cutout, which is key 0x400; docs/ASSETS.md §4b) doubles both ends.
    Fog fog;
    if (view_.fog && !(d.bucketKey & 0x2080u)) {
        const float k = (d.bucketKey & 0x800u) ? 2.0f : 1.0f;
        fog.on = true;
        fog.start = view_.fogStart * k;
        fog.end   = view_.fogEnd * k;
        fog.r = view_.fogColour[0];
        fog.g = view_.fogColour[1];
        fog.b = view_.fogColour[2];
    }
    const RasterStats s = drawGeometry(fb_, depth_, view_.cam, one, tex_, fog,
                                       view_.shimmerClock, view_.dither, view_.grey);
    st_.triangles += s.triangles;
    st_.drawn += s.drawn;
    st_.behind += s.behind;
    st_.offscreen += s.offscreen;
    st_.pixels += s.pixels;
    st_.depthRejects += s.depthRejects;
    st_.culled += s.culled;
}

namespace {

// The draws a geometry yields, split by whether the corners came from the
// MIRROR mesh. A batch groups by (blend, material, cutout), so a batch can hold
// both - the mirror shares a material with the wall around it - and the split
// has to be by RUN inside the batch, not by batch.
// The same split over a PREPARED draw list - the game path builds one from
// several geometries (the set, the staged bodies, the props, the sprites) and
// sorts it by bucket key, and only the set's corners carry `cornerMirror`.
// Each draw is run-split exactly as a batch is, so a batch that straddles the
// mirror's faces still separates.
void splitList(std::span<const Draw> in, std::vector<Draw>& scene,
               std::vector<Draw>& mirror) {
    for (const auto& src : in) {
        const Geometry* g = src.geo;
        const bool haveFlags = g && g->cornerMirror.size() == g->corners.size();
        if (!haveFlags) { scene.push_back(src); continue; }
        std::size_t i = src.start;
        while (i < src.start + src.count) {
            const std::uint8_t m = g->cornerMirror[i];
            std::size_t j = i;
            while (j < src.start + src.count && g->cornerMirror[j] == m) ++j;
            Draw d = src;
            d.start = i; d.count = j - i;
            (m ? mirror : scene).push_back(d);
            i = j;
        }
    }
}

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
std::vector<Draw> inFrontOf(const std::vector<Draw>& in,
                            const float n[3], float d, float eps) {
    std::vector<Draw> out;
    for (const auto& src : in) {
        if (!src.geo) continue;
        const Geometry& g = *src.geo;      // each draw carries its own
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

void runPass(Renderer& r, const View& v, std::span<const Draw> ds) {
    // THE SHADOW MAP's depth pass, before the frame's own - `todo/enhancements`
    // row 6, and a no-op on every backend that does not implement it. Only the
    // batches flagged `castsShadow` go in; see `renderer.h`'s View for why
    // that is characters alone.
    if (v.shadow.on) {
        // kept across frames for its capacity (main thread only, as every
        // caller of `drawWithMirror` is; todo/optimization.md step 18)
        static std::vector<Draw> casters;
        casters.clear();
        for (const auto& d : ds) if (d.castsShadow) casters.push_back(d);
        if (!casters.empty()) r.shadowPass(v, casters);
    }
    r.begin(v);
    for (const auto& d : ds) r.submit(d);
    r.end();
}

}  // namespace

// THE WATER and the REFLECTING FLOORS' helpers (`todo/remaster.md` A3, A4's
// mist ground): the remaster's alone - compiled only with OMK_REMASTER
#if OMK_REMASTER
WaterPlane waterPlane(const Geometry& g) {
    WaterPlane out;
    std::map<int, std::pair<double, long>> byHeight;   // y / 4 -> area, triangles
    forLevelTriangles(g, [](const Batch& b) { return b.blend == Blend::Add; },
                      [&](const Corner& a, const Corner& c1, const Corner& c2) {
        const double ux = c1.x - a.x, uz = c1.z - a.z, vx = c2.x - a.x, vz = c2.z - a.z;
        const double area = std::fabs(ux * vz - uz * vx) * 0.5;
        auto& e = byHeight[static_cast<int>(std::lround(a.y / 4.0f))];
        e.first += area; ++e.second;
    });
    for (const auto& [k, e] : byHeight) {
        if (e.first > out.area) { out.area = e.first; out.y = static_cast<float>(k) * 4.0f; out.triangles = e.second; }
        if (e.first >= 1e4) out.levels.push_back({static_cast<float>(k) * 4.0f, e.first, e.second});
    }
    out.found = out.area >= 1e6;
    return out;
}

namespace {

// THE REFLECTION'S CLIP, remembered: `inFrontOf` tests every triangle of every
// draw against the plane, every frame - the SET's draws are the same frame
// after frame, so the runs a (start, count) of its geometry keeps above a
// plane are too. The set's alone: a particle's or a decal's geometry is ONE
// object rewritten every frame, and a memo of it drew a stale clip (1 pixel
// in Qalisar, 2026-10-21). Kept per plane height; forgotten when the plane
// moves or a geometry is destroyed (its address may come back for another).
// ...and per REVISION (the audit's bug 2, 2026-10-23): a world slot reloaded
// IN PLACE (`w = std::move(L.out)`) keeps the geometry's address and destroys
// nothing, so the next set - its plane at the same height, a draw of the same
// (start, count) - drew the OLD set's runs. A new revision empties the memo,
// unless it is the set motion patch's (`Geometry::dirtyCorners`, from the
// revision the memo holds): then only the draws over a moved corner go - a
// door may cross the plane.
// Main thread only, as every caller of `drawWithWater` is.
struct ClipMemo {
    using Runs = std::vector<std::pair<std::size_t, std::size_t>>;
    struct Entry {
        std::uint64_t revision = 0;
        std::unordered_map<std::uint64_t, Runs> byDraw;   // (start << 32) | count -> its runs
    };
    float y = 0.0f;
    bool listening = false;
    std::unordered_map<const Geometry*, Entry> runs;
    std::vector<std::uint32_t> moved;   // the patch's corners, SORTED (`dirtyCorners` is not)
    static void gone(void* ctx, const Geometry* g) { static_cast<ClipMemo*>(ctx)->runs.erase(g); }

    // the entry for `g` at its current revision
    Entry& at(const Geometry& g) {
        Entry& e = runs[&g];
        if (e.revision == g.revision) return e;
        if (!e.byDraw.empty() && g.dirtyTo == g.revision && g.dirtyFrom == e.revision) {
            moved.assign(g.dirtyCorners.begin(), g.dirtyCorners.end());
            std::sort(moved.begin(), moved.end());
            for (auto it = e.byDraw.begin(); it != e.byDraw.end();) {
                const std::uint64_t start = it->first >> 32, end = start + (it->first & 0xFFFFFFFFu);
                const auto m = std::lower_bound(moved.begin(), moved.end(), start);
                if (m != moved.end() && *m < end) it = e.byDraw.erase(it);
                else ++it;
            }
        } else {
            e.byDraw.clear();
        }
        e.revision = g.revision;
        return e;
    }
};

// ...TWO of them (`[Remaster] reflections = 2`): a frame with a second level
// clips at both heights, and one memo would forget each plane's runs for the
// other every frame - the memo whose height it is, else the one not used last.
void clipAbove(const std::vector<Draw>& in, float y, const Geometry* stable, std::vector<Draw>& out) {
    static ClipMemo memos[2];
    static int last = 0;
    const int k = memos[0].y == y ? 0 : memos[1].y == y ? 1 : 1 - last;
    last = k;
    ClipMemo& memo = memos[k];
    if (!memo.listening) { addGeometryListener(&ClipMemo::gone, &memo); memo.listening = true; }
    if (memo.y != y) { memo.runs.clear(); memo.y = y; }
    const float n[3] = {0.0f, -1.0f, 0.0f};
    out.clear();
    static std::vector<Draw> one;
    ClipMemo::Entry* kept = stable ? &memo.at(*stable) : nullptr;
    for (const auto& d : in) {
        if (!d.geo) continue;
        if (d.geo != stable) {
            one.assign(1, d);
            for (const auto& o : inFrontOf(one, n, y, 1.0f)) out.push_back(o);
            continue;
        }
        auto& byDraw = kept->byDraw;
        const std::uint64_t key = (static_cast<std::uint64_t>(d.start) << 32) | static_cast<std::uint64_t>(d.count);
        auto it = byDraw.find(key);
        if (it == byDraw.end()) {
            one.assign(1, d);
            std::vector<std::pair<std::size_t, std::size_t>> r;
            for (const auto& o : inFrontOf(one, n, y, 1.0f)) r.emplace_back(o.start, o.count);
            it = byDraw.emplace(key, std::move(r)).first;
        }
        for (const auto& [st, cn] : it->second) { Draw o = d; o.start = st; o.count = cn; out.push_back(o); }
    }
}

}  // namespace

float groundLevel(const Geometry& g) {
    std::map<int, double> byHeight;   // y / 4 -> level opaque area
    forLevelTriangles(g, [](const Batch& b) { return b.blend == Blend::Opaque; },
                      [&](const Corner& a, const Corner& c1, const Corner& c2) {
        byHeight[static_cast<int>(std::lround(a.y / 4.0f))] +=
            std::fabs((c1.x - a.x) * (c2.z - a.z) - (c1.z - a.z) * (c2.x - a.x)) * 0.5;
    });
    int best = 0;
    double most = 0.0;
    for (const auto& [k, area] : byHeight)
        if (area > most) { most = area; best = k; }
    return static_cast<float>(best) * 4.0f;
}

bool drawWithWater(Renderer& r, std::span<const Draw> draws, const View& v,
                   const Geometry* setGeo, const WaterPlane& wp, float strength,
                   const std::vector<float>* floorStrength,
                   const std::vector<std::uint8_t>* wetTris, float wetReflect, float wetDarken,
                   SecondPlane* second) {
    if (second) second->drawn = false;
    if (!wp.found || strength <= 0.0f || !setGeo || !r.drawsWater()) return false;
    // above the water: the game's Y points DOWN, so a smaller y
    if (v.cam.eye[1] >= wp.y - 1.0f) return false;
    static std::vector<Draw> scene, water;
    scene.clear(); water.clear();
    const auto onPlane = [&](const Corner& c) { return std::fabs(c.y - wp.y) <= 4.0f; };
    // a WET floor's triangle is on it within `kWetPlaneBand` (`o3de/floorreflect.h`);
    // `OMK_WET_PLANE_EXACT=1`, an INSTRUMENT, within 4 as a listed floor's
    // (`verify.py: engine: wet floors` part 13)
    static const float band = omk::envFlag("OMK_WET_PLANE_EXACT") ? 4.0f : kWetPlaneBand;
    const auto onWetPlane = [&](const Corner& c) { return std::fabs(c.y - wp.y) <= band; };
    // THE SECOND LEVEL (`SecondPlane`): the floors' alone, the eye above it,
    // and apart from the first by more than the wet band - a triangle within
    // it reflects in the first
    const bool two = second && second->plane.found && (floorStrength || wetTris) && r.drawsTwoReflections() &&
                     v.cam.eye[1] < second->plane.y - 1.0f && std::fabs(second->plane.y - wp.y) > 2.0f * band;
    const float y2 = two ? second->plane.y : 0.0f;
    const auto onPlane2 = [&](const Corner& c) { return std::fabs(c.y - y2) <= 4.0f; };
    const auto onWetPlane2 = [&](const Corner& c) { return std::fabs(c.y - y2) <= band; };
    // ...and of its triangles those IN SHOT alone (the view's frustum, a tenth
    // wider; out when all three corners are past one side): the level's floor
    // near the eye was off screen at Qalisar's lamp street, its pass drawn for
    // nothing (2026-10-29) - with none in shot, no second pass
    float vs[3] = {}, vu[3] = {}, vf[3] = {}, tanH = 1.0f, tanV = 1.0f;
    if (two) cameraBasis(v.cam, vs, vu, vf, tanH, tanV);
    const auto inShot = [&](const Corner* c) {
        int out[5] = {0, 0, 0, 0, 0};
        for (int k = 0; k < 3; ++k) {
            const float d[3] = {c[k].x - v.cam.eye[0], c[k].y - v.cam.eye[1], c[k].z - v.cam.eye[2]};
            const float x = d[0] * vs[0] + d[1] * vs[1] + d[2] * vs[2];
            const float y = d[0] * vu[0] + d[1] * vu[1] + d[2] * vu[2];
            const float z = d[0] * vf[0] + d[1] * vf[1] + d[2] * vf[2];
            out[0] += z < kNearCut;
            out[1] += x > z * tanH * 1.1f;
            out[2] += x < -z * tanH * 1.1f;
            out[3] += y > z * tanV * 1.1f;
            out[4] += y < -z * tanV * 1.1f;
        }
        for (const int o : out) if (o == 3) return false;
        return true;
    };
    bool floored = false, floored2 = false;
    for (const auto& d : draws) {
        if (floorStrength || wetTris) {
            // THE FLOORS: a listed floor's runs on the plane reflect, in place
            // - and a WET one's (`o3de/wetfloor.h`), triangle by triangle
            float s = 0.0f;
            const bool setOpaque = d.geo == setGeo && d.blend == Blend::Opaque && !d.meshPose;
            if (setOpaque && floorStrength) {
                const auto& bs = d.geo->batches;
                const auto b = std::upper_bound(bs.begin(), bs.end(), d.start,
                                                [](std::size_t at, const Batch& x) { return at < x.start; });
                if (b != bs.begin()) {
                    const Batch& bb = *(b - 1);
                    if (bb.material >= 0 && static_cast<std::size_t>(bb.material) < floorStrength->size())
                        s = (*floorStrength)[static_cast<std::size_t>(bb.material)];
                }
            }
            const bool wet = setOpaque && wetTris && wetReflect > 0.0f;
            if (s <= 0.0f && !wet) { scene.push_back(d); continue; }
            const Geometry& g = *d.geo;
            std::size_t runStart = d.start;
            float runValue = -1.0f, runWet = 0.0f;
            std::uint8_t runPlane = 0;
            const auto flush = [&](std::size_t end) {
                if (runValue < 0.0f || end <= runStart) return;
                Draw o = d; o.start = runStart; o.count = end - runStart;
                o.reflect = runValue;
                o.wet = runWet;
                o.reflectPlane = runPlane;
                floored |= runValue > 0.0f;
                floored2 |= runValue > 0.0f && runPlane == 1;
                scene.push_back(o);
            };
            for (std::size_t t = d.start; t + 2 < d.start + d.count; t += 3) {
                const bool open = wet && t / 3 < wetTris->size() && (*wetTris)[t / 3];
                const bool wetFloor = open && (*wetTris)[t / 3] == 1;   // a wet FLOOR (1; a wall is 2)
                const Corner* c = &g.corners[t];
                float value = 0.0f;
                std::uint8_t plane = 0;
                if (onPlane(c[0]) && onPlane(c[1]) && onPlane(c[2])) {
                    value = s;
                    if (open) value = std::max(value, wetReflect);
                } else if (wetFloor && onWetPlane(c[0]) && onWetPlane(c[1]) && onWetPlane(c[2])) {
                    value = wetReflect;   // a little off the level
                } else if (two && onPlane2(c[0]) && onPlane2(c[1]) && onPlane2(c[2])) {
                    value = open ? std::max(s, wetReflect) : s;   // ...or on the SECOND level, its rules
                    plane = 1;
                } else if (two && wetFloor && onWetPlane2(c[0]) && onWetPlane2(c[1]) && onWetPlane2(c[2])) {
                    value = wetReflect;
                    plane = 1;
                }
                if (plane == 1) {
                    // off screen: nothing to reflect, nothing to query; in
                    // shot, the level's fade - at 0 still marked, for its query
                    if (value <= 0.0f || !inShot(c)) { value = 0.0f; plane = 0; }
                    else value *= second->fade;
                } else if (value <= 0.0f) {
                    plane = 0;
                }
                const float dark = open ? wetDarken : 0.0f;
                if (value != runValue || dark != runWet || plane != runPlane) {
                    flush(t); runStart = t; runValue = value; runWet = dark; runPlane = plane;
                }
            }
            flush(d.start + d.count);
            continue;
        }
        if (d.geo != setGeo || d.blend != Blend::Add || d.meshPose) { scene.push_back(d); continue; }
        const Geometry& g = *d.geo;
        // runs of the draw: water triangles to `water`, the others to `scene`
        std::size_t runStart = d.start;
        int runKind = -1;
        const auto flush = [&](std::size_t end) {
            if (runKind < 0 || end <= runStart) return;
            Draw o = d; o.start = runStart; o.count = end - runStart;
            (runKind ? water : scene).push_back(o);
        };
        for (std::size_t t = d.start; t + 2 < d.start + d.count; t += 3) {
            const int kind = onPlane(g.corners[t]) && onPlane(g.corners[t + 1]) && onPlane(g.corners[t + 2]) ? 1 : 0;
            if (kind != runKind) { flush(t); runStart = t; runKind = kind; }
        }
        flush(d.start + d.count);
    }
    if (water.empty() && !floored) return false;
    // the reflected view: `p' = p - 2 (n.p + d) n` with n = (0, -1, 0), d = y -
    // a y' = 2 y_w - y - and the image flipped VERTICALLY, which the camera
    // says as a half turn of roll and a screen-X flip (the reflection
    // reverses the roll's sense)
    View rv = v;
    rv.cam.eye[1] = 2.0f * wp.y - v.cam.eye[1];
    rv.cam.at[1] = 2.0f * wp.y - v.cam.at[1];
    rv.cam.rollDeg = 180.0f - v.cam.rollDeg;
    rv.cam.flipX = !v.cam.flipX;
    // what the reflection may show: the triangles ABOVE the plane, and the
    // draws a backend poses (the sky, the bodies) whole - their corners are
    // at rest, so a test on them would be a test on the wrong place
    static std::vector<Draw> above, posed, clipped, flat;
    above.clear(); posed.clear(); flat.clear();
    for (const auto& d : scene) (d.meshPose ? posed : flat).push_back(d);
    clipAbove(flat, wp.y, setGeo, clipped);   // `inFrontOf`'s runs, the set's remembered
    above.assign(posed.begin(), posed.end());
    above.insert(above.end(), clipped.begin(), clipped.end());
    // `OMK_WATER_NO_CPU_CLIP=1`, an instrument: everything to the reflection,
    // so the backend's per-fragment clip alone removes what is below the
    // water - which is what it does for the posed bodies anyway
    // (`verify.py: engine: water reflection`)
    static const bool noCpuClip = omk::envFlag("OMK_WATER_NO_CPU_CLIP");
    if (noCpuClip) above.assign(scene.begin(), scene.end());
    if (v.shadow.on) {
        static std::vector<Draw> casters;
        casters.clear();
        for (const auto& d : draws) if (d.castsShadow) casters.push_back(d);
        if (!casters.empty()) r.shadowPass(v, casters);
    }
    // ...and the SECOND level's reflection: its own mirrored view and its own
    // clip, the posed draws whole as for the first
    if (floored2) {
        View rv2 = v;
        rv2.cam.eye[1] = 2.0f * y2 - v.cam.eye[1];
        rv2.cam.at[1] = 2.0f * y2 - v.cam.at[1];
        rv2.cam.rollDeg = 180.0f - v.cam.rollDeg;
        rv2.cam.flipX = !v.cam.flipX;
        static std::vector<Draw> above2, clipped2;
        clipAbove(flat, y2, setGeo, clipped2);
        above2.assign(posed.begin(), posed.end());
        above2.insert(above2.end(), clipped2.begin(), clipped2.end());
        if (noCpuClip) above2.assign(scene.begin(), scene.end());
        if (water.empty() && r.drawTwoReflections(v, rv, scene, above, strength, wp.y, rv2, above2, y2)) {
            second->drawn = true;
            return true;
        }
        // refused after all: the second level's runs reflect nothing
        for (auto& d : scene)
            if (d.reflectPlane == 1) { d.reflect = 0.0f; d.reflectPlane = 0; }
    }
    return r.drawWaterScene(v, rv, scene, above, water, strength, wp.y);
}
#endif  // OMK_REMASTER

MirrorStats drawWithMirror(Renderer& r, const Geometry& g,
                           std::span<const Texture> tex, const View& v,
                           const MirrorPlane& mp) {
    (void)tex;
    std::vector<Draw> scene, mirror;
    splitDraws(g, scene, mirror);
    std::vector<Draw> all = scene;
    all.insert(all.end(), mirror.begin(), mirror.end());
    return drawWithMirror(r, all, v, mp);
}

MirrorStats drawWithMirror(Renderer& r, std::span<const Draw> draws,
                           const View& v, const MirrorPlane& mp) {
    MirrorStats st;
    // The textures are NOT set here. That is a LOAD-time call, and calling it
    // per frame crashed the viewer: the software renderer only stores a span,
    // so it looked harmless, but the Vulkan one allocates a VkImage and a
    // descriptor set per texture and a per-frame call exhausted the 128-set
    // pool in a handful of frames, then bound a set that had failed to
    // allocate. The caller sets them when it loads the set
    // (`Renderer::setTextures`).
    //
    // THE LISTS ARE KEPT ACROSS FRAMES (todo/optimization.md step 18's
    // leftovers): this built a scene list, a mirror list and their
    // concatenation - three copies of the frame's draws - on every frame,
    // mirror or not. Main thread only, as every caller is.
    static std::vector<Draw> scene, mirror, all;
    scene.clear();
    mirror.clear();
    splitList(draws, scene, mirror);

    // No mirror in this set, or none visible: one pass, exactly as before.
    if (!mp.found || mirror.empty()) {
        // With no mirror draw, `splitList` left every draw whole and in
        // order, so the concatenation IS the input: draw it as it came.
        if (mirror.empty()) { runPass(r, v, draws); return st; }
        all.assign(scene.begin(), scene.end());
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
        all.assign(scene.begin(), scene.end());
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
        const auto clipped = inFrontOf(scene, n, d, 1.0f);
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
    runPass(r, rv, inFrontOf(scene, n, d, 1.0f));
    const Surface reflection = r.readback();   // a copy: the next pass reuses it

    // ---- pass 2: the scene WITHOUT the mirror, and pass 3 WITH it.
    //
    // Two passes rather than drawing the mirror alone, because a mirror drawn
    // alone is not DEPTH-TESTED against the room: the plane is large and
    // mostly hidden behind the wall it is set into, so a lone render marks
    // every pixel the plane covers and the reflection then replaces the whole
    // wall: the wall vanishes and a corridor appears through it, which one
    // look at one frame shows.
    //
    // Differencing the two passes gives the mirror's visible pixels with the
    // room's own depth already applied, using nothing but the boundary.
    runPass(r, v, scene);
    const Surface without = r.readback();
    all.assign(scene.begin(), scene.end());
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

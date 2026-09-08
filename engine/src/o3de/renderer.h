// SPDX-License-Identifier: GPL-3.0-or-later
// THE RENDERER BOUNDARY - at the DECISION level, which is `PORTING` A2 and
// the load-bearing design decision in Part A.
//
// A2 in one sentence: **a backend receives decisions and turns them into API
// calls; it never makes one.** What `engine/` has ported is not triangles, it
// is the decisions around them - the drawable mask `flags & 0x800043`, the
// 14-bit bucket key, the texture slot as that key's LOW SIX BITS, the two
// blend modes (`0x1000|0x2000` additive, `0x1000|0x4000` multiply, `0x800`
// cutout), the 58-slot name-keyed texture cache, and the visible-set walk
// against `sub_48D0D0`'s frustum. Every one of those is checked. Put the
// boundary at the Vulkan level instead and they leak into a backend where
// nothing can check them, which A2 says is annoying to retrofit and is exactly
// the mistake this file exists to avoid.
//
// So the interface is A2's, near enough verbatim:
//
//     init(w, h)                 the framebuffer's size
//     setTextures(span)          the resident pool, indexed by material
//     begin(View)                the camera and its frustum
//     submit(Draw)               {bucketKey, geometry, range, blend, cutout}
//     end()                      the frame is finished
//     readback() -> Surface      RGB565, for a check to look at
//
// TWO IMPLEMENTATIONS, ONE BOUNDARY (`PORTING` A1):
//
//   * `SoftwareRenderer` (below, over `raster.cpp`) is the REFERENCE - the one
//     `verify.py` checks, and the one every claim in `docs/` about the 3D path
//     is a claim about. It is not a port: the engine has no software 3D
//     rasterizer, D3D drew every triangle (`raster.h`, `PORTING` B6).
//   * the Vulkan backend is the LIVE one, in `backends/vulkan/`, and like SDL
//     it is optional - `make` and the whole suite must pass on a machine with
//     no Vulkan SDK at all, which is A1's closing sentence and A8 rule 1.
//
// **`readback()` is what makes the pair testable rather than merely parallel.**
// A GPU frame that can only be looked at is a frame nobody can difference, and
// this repo has spent two sessions learning what an unverifiable render costs.
// The Vulkan backend copies its colour attachment back to an RGB565 `Surface`
// on request - slow, and never on a frame being played, but it means the live
// renderer and the reference one can be compared with the same instrument that
// compares the reference against the engine's own captures.
//
// **What is NOT here, and deliberately.** A2's interface also lists
// `submit2d(I2dList)`. The 2D layer is not on this boundary yet: the ported
// I2D path already reproduces the engine's framebuffer exactly (66560/66560
// over the menu's deterministic region) and a Blt is a memory copy, so there
// is nothing a GPU would make more correct - only faster. The live frontend
// composites it as a texture upload, which is what `backends/sdl/play.cpp`
// already does. Moving it here is a later slice and should say what it buys.
#pragma once

#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/raster.h"
#include "o3de/render.h"
#include "ui/surface.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// The camera, and the frustum the visible-set walk already culls against.
// `RCamera` rather than a new type because it is the one both `raster.cpp` and
// `tools/camshot.py` are written against, and its conventions - world up is
// (0,-1,0) because the game's Y points DOWN, `hfovDeg` is the HORIZONTAL fov,
// `tanv = tanh / (W/H)` - are the two that laying a wireframe over a real
// screenshot corrected. A second camera type would be a second chance to get
// them wrong.
struct View {
    RCamera cam;
    // THE LETTERBOX, and it is a VIEWPORT rather than a pair of black bars
    // painted over a full frame.
    //
    // The game's camera mode is letterboxed - 640x352 of picture inside 480,
    // 1.818:1, which `traces/frames/dlg402-*.png` show and which a reader
    // confirmed belongs to conversations and cutscenes and not to free
    // roaming. What makes it a viewport and not a crop is the vertical fov:
    // the tier-4 silhouette result and `engine: raster`'s 106/106 projection
    // differential are both built on a render at 640x352 (and 800x440), where
    // `tanv = tanh / (W/H)` uses the STRIP's height. Painting bars over a
    // full-height frame would leave the vertical fov at the full frame's and
    // show a different picture inside the same bars.
    //
    // `w`/`h` of 0 means the whole framebuffer, which is what free roaming
    // and the scene viewer want. A backend renders the picture into the
    // top-left `w x h` of its target and the caller places it at `x`,`y`;
    // keeping the placement out here is what lets the Vulkan attachment stay
    // window-sized, which its swapchain present needs.
    int vx = 0, vy = 0, vw = 0, vh = 0;
    bool letterboxed() const { return vw > 0 && vh > 0; }

    // THE FOG, and it is a ported decision like everything else here.
    //
    // `20_ddraw.c` 1921-1936 sets `D3DRENDERSTATE_FOGENABLE`, `FOGTABLEMODE`
    // = **3 = D3DFOG_LINEAR**, `FOGDENSITY` = 1.0, then `FOGSTART` and
    // `FOGEND` from the scene's `+328` and `+340` - which are the options
    // menu's clip distance times 0.25 and times 1 (`platform/settings.h`).
    // `FOGCOLOR` comes from the scene's `+336`.
    //
    // **That colour is BLACK in normal play**, and this is the part that took
    // reading rather than guessing: the scene object is `memset` to 0 at load
    // and `sub_44E830` then writes `a1[84] = 0` - which IS `+336` - explicitly.
    // No other writer of the scene's `+336` exists in the decompilation. So
    // the shipped fog darkens toward the horizon rather than hazing it, which
    // is what a domed city at night wants and what the captures show.
    //
    // One mode replaces it: with `dword_93082C == 1` the scene takes colour
    // 0x00405028 and a clip distance of `flt_4C2C34` = 590.551 units = exactly
    // **15.0 m**. See `docs/ASSETS.md`, "The fog".
    //
    // Two exclusions, both keyed off the bucket key of the batch being drawn,
    // and a backend must honour them or it draws a different picture:
    //   * key bits `0x2080` - the near bucket and the transparent state - get
    //     NO fog;
    //   * key bit `0x800` - the cutout path, and the sky, which carries it
    //     through mesh flag 0x10000 - gets both start and end DOUBLED.
    bool  fog      = false;
    float fogStart = 0.0f;
    float fogEnd   = 0.0f;
    std::uint8_t fogColour[3] = {0, 0, 0};   // r, g, b

    // ------------------------------------------- THE SHADOW MAP's LIGHT
    //
    // `todo/enhancements.md` row 6, and OFF unless `shadowquality = mapped`.
    // Nothing in the engine casts a real shadow, so this is an ENHANCEMENT and
    // not a ported decision - which is why it sits behind a flag and why a
    // backend is free to ignore it. The software reference does, so its
    // picture is still the one the original drew.
    //
    // The light is NOT invented: it is the strongest of the set's own `.3DO`
    // light records reaching the casters (`o3de/vertexlight.h`), whose every
    // engine call site is street life - the lights that light the crowd. With
    // none in reach `on` stays false and nothing is drawn.
    //
    // `centre` and `radius` bound the CASTERS, not the scene: the depth map is
    // an orthographic slab fitted to them, so a fragment outside it is lit by
    // definition. That is what keeps a characters-only shadow from darkening
    // the far end of a street.
    struct ShadowLight {
        bool  on = false;
        float dir[3]  = {0, -1, 0};   // unit, pointing the way the light travels
        float centre[3] = {0, 0, 0};  // the casters' centre
        float radius  = 0.0f;         // ...and the sphere around them
        float strength = 0.6f;        // how dark the shadow is, 0..1
    };
    ShadowLight shadow;

    // ------------------------------------------- PER-PIXEL LIGHTING
    //
    // `todo/enhancements.md` row 7, and OFF unless `lighting = perpixel`.
    // NOT a new law: `sub_493E40`'s own `-(N.L)` over a linear falloff between
    // the two radii, through the engine's `(t * c) >> 8` ramp - the same
    // arithmetic `o3de/vertexlight.cpp` does per VERTEX, evaluated per
    // fragment instead. A character's thigh is one or two quads across, so
    // per-vertex the lamp's pool bends over his leg in flat facets and pops
    // as a vertex crosses the falloff boundary.
    //
    // The lights are the SET's own `.3DO` records, already read for the
    // crowd. A backend without a shader ignores this and the caller lights
    // per vertex on the CPU exactly as before, which is what the software
    // reference does.
    struct GpuLight {
        float pos[3]  = {0, 0, 0};
        float dir[3]  = {0, 0, 0};    // unit, `normalize(centre - pos)`
        float radiusA = 0.0f;         // outer: past it the light does not reach
        float radiusB = 0.0f;         // inner: inside it the falloff is clamped
        float colour[3] = {0, 0, 0};  // 0..1
        float intensity = 0.0f;       // the record's `+32`
    };
    // Up to this many, nearest first. Anekbah has 155 and a body is reached by
    // a handful; the cap is the uniform block's, and it is stated rather than
    // silently truncating in the middle of a street.
    static constexpr int kMaxGpuLights = 8;
    std::vector<GpuLight> lights;   // empty = light per vertex, as before

    // ---------------------------------------------- THE SHIMMER's clock
    //
    // Mesh flag 0x8000000, and it is the GAME's - not an enhancement. 233 set
    // meshes carry it, all distant scenery, and their vertex colour
    // oscillates about zero on a 32-step cycle. `Game_Tick` advances this by
    // `2 * frameDelta` and wraps it at 256; `o3de/shimmer.h` has the table and
    // the index, and a backend that ignores this draws a static skyline where
    // the original draws a moving one.
    float shimmerClock = 0.0f;
};

// One submission. This is the whole vocabulary a backend gets, and every field
// is a ported decision rather than a rendering choice:
//
//   * `bucketKey` is `Render_FlushBuckets`'s 14-bit key. Its LOW SIX BITS are
//     the material's runtime texture slot (ASSETS 4b) - which is why a backend
//     is handed the key and not a texture pointer: binding the wrong thing is
//     the mechanism behind the Anekbah signs, and a backend that resolved
//     textures itself could not reproduce it.
//   * `blend` and `cutout` are the mesh flags decoded, not guessed. The cutout
//     is a COLOUR KEY on black, `SetRenderState(27, 1)`, never alpha.
//   * the range is into a `Geometry` the caller owns, in the engine's own draw
//     order - opaque, then additive, then multiply, by material within each.
//     A backend that sorted differently would draw a different picture
//     wherever anything overlaps.
struct Draw {
    std::uint32_t   bucketKey = 0;
    const Geometry* geo       = nullptr;
    std::size_t     start     = 0;
    std::size_t     count     = 0;
    Blend           blend     = Blend::Opaque;
    bool            cutout    = false;
    // Row 7: this batch is LIT per pixel, so its baked vertex colour is not
    // its shading. A lit instance starts from BLACK - `sub_494E80` writes
    // `instance[+416]` into every runtime vertex's colour and every site that
    // sets +416 sets it to 0 - and the crowd models ship pure white, so there
    // is no baked light in them to keep.
    // 0 not lit; 1 lit from BLACK (the engine's own rule, for the bodies it
    // lights - their models ship white); 2 lit ADDED to the baked colour, for
    // the bodies it does not, whose models carry real baked shading.
    int             lit         = 0;
    // Row 6 again: whether this batch goes into the shadow map's depth pass.
    // Only characters do, and that is a constraint rather than a saving - a
    // set is shaded by a colour baked into every vertex which ALREADY contains
    // the artists' shadows (ASSETS 4c), so a map that darkened the set from
    // the set would double-darken every corner it painted once.
    bool            castsShadow = false;
};

class Renderer {
public:
    virtual ~Renderer() = default;

    // -> false when the backend cannot come up. A caller must handle that:
    // on a machine with no Vulkan driver this is the normal answer, not an
    // error, and the software one is always available.
    virtual bool init(int w, int h) = 0;

    // The resident texture pool, indexed the way the shipped data indexes it -
    // by the batch's material. Called when a set is loaded, not per frame.
    virtual void setTextures(std::span<const Texture> t) = 0;

    virtual void begin(const View& v) = 0;
    virtual void submit(const Draw& d) = 0;
    virtual void end() = 0;

    // The finished frame as RGB565, for a check rather than for a player.
    //
    // **IDEMPOTENT, and the caller may WRITE to what it returns.** Calling it
    // twice with no `end()` between must give the same surface, with any
    // caller's edits intact - the mirror pass composites its reflection
    // straight into it and then the frontend reads it again. The software
    // renderer gets this for free by returning its stored framebuffer; a GPU
    // backend must convert once per frame rather than per call, and the one
    // here did not, which threw the composite away and made Vulkan render a
    // nearly-black frame while software was correct.
    virtual const Surface& readback() = 0;

    // What the frame cost, in the reference's own terms. A GPU backend cannot
    // fill all of these honestly - it does not count texels the way a software
    // loop does - and must leave what it does not know at zero rather than
    // inventing a number a check would then compare.
    virtual RasterStats stats() const = 0;

    virtual const char* name() const = 0;

    // A NATIVE mirror pass, when the backend has one. -> false means "I do not
    // do this", and the boundary falls back to compositing on the CPU.
    //
    // Note what crosses and what does not. Everything handed over is a
    // DECISION the boundary computed and `verify.py` can check: the reflected
    // view (`p -= 2*dist*n`, the engine's own reflection), the scene draws,
    // the same draws CLIPPED to the mirror's front half-space, and the
    // mirror's own draws - the last two both following from the ported flag
    // `0x100000`. What the backend chooses is only HOW to confine the
    // reflection to the mirror's area: a stencil, here. That split is A2's -
    // a backend turns decisions into API calls and makes none of its own.
    // The DEPTH PASS from the light, recorded before the frame's own pass.
    // Default: do nothing, which is what a backend without a shadow map
    // answers and what the software reference answers on purpose.
    virtual void shadowPass(const View& /*v*/, std::span<const Draw> /*casters*/) {}

    virtual bool drawMirrorScene(const View& /*v*/, const View& /*reflected*/,
                                 std::span<const Draw> /*scene*/,
                                 std::span<const Draw> /*sceneClipped*/,
                                 std::span<const Draw> /*mirror*/) {
        return false;
    }

    // AN ENHANCEMENT, NOT A DECISION: multisample anti-aliasing, `samples`
    // per pixel (2/4/8; 0 or 1 is off). The original has none - `sub_4638C0`
    // sets D3D's ANTIALIAS state to FALSE and never touches EDGEANTIALIAS
    // (`docs/ASSETS.md` 4) - so this crosses the boundary as a request the
    // backend may decline, and it is OFF unless `omk-play --aa` or the
    // config's `[Enhancements]` section asks. Called BEFORE `init()`.
    // -> false when the backend does not do it; the software reference never
    // does, because it stands where D3D stood and D3D was told not to.
    virtual bool setMultisample(int /*samples*/) { return false; }

    // The second enhancement: texture filtering, `mode` 0 nearest (the
    // original: MAG/MIN POINT, MIP NONE), 1 bilinear, 2 trilinear - a mip
    // chain the backend generates at upload, the original shipping one level.
    // Same contract as `setMultisample` - a request, before `init()`, that
    // the software reference declines.
    virtual bool setTextureFilter(int /*mode*/) { return false; }
    // And anisotropic filtering, `n` samples along the footprint's long axis
    // (1 off, up to 16). Only meaningful with mode 2.
    virtual bool setAnisotropy(int /*n*/) { return false; }
};

// The REFERENCE implementation: `drawGeometry` behind the boundary, with no
// change in behaviour. It exists so that the boundary itself is exercised by
// everything that already passes - if wrapping the rasterizer moved a single
// pixel, `engine: raster` and `engine: silhouette` would say so.
class SoftwareRenderer : public Renderer {
public:
    bool init(int w, int h) override;
    void setTextures(std::span<const Texture> t) override { tex_ = t; }
    void begin(const View& v) override;
    void submit(const Draw& d) override;
    void end() override {}
    const Surface& readback() override { return fb_; }
    RasterStats stats() const override { return st_; }
    const char* name() const override { return "software"; }

private:
    Surface                  fb_{1, 1, 0};
    std::vector<float>       depth_;
    std::span<const Texture> tex_;
    View                     view_;
    RasterStats              st_;
};

// ------------------------------------------------------------ THE MIRROR PASS
//
// `docs/ASSETS.md` 4c, corrected 2026-09-01: the game HAS mirrors. Mesh flag
// `0x100000`, 6 meshes of 12203, at most one live at a time (the engine keeps
// a single global, `dword_534F48`), and `sub_440D90` - called once a frame
// from the camera setup path - reflects the camera through the mirror's plane
// and calls the scene draw AGAIN.
//
// What is traced, and each of these is implemented below rather than invented:
//
//   * the camera is reflected through the plane, `p -= 2 * dist * n`, both the
//     eye and the target;
//   * the pass is SKIPPED when the camera is behind the plane
//     (`if (v11 > 0.0)`), so a mirror seen from the back costs nothing;
//   * the reflected pass draws with screen X FLIPPED. `Raster_DrawTriangles`
//     computes `v143 - x` instead of `v140 + x` while the pass flag is set,
//     which compensates the handedness flip a reflection introduces. That is
//     exactly what `RCamera::mirror` does - the control that exists because
//     the flipped reading was this repo's own months-long bug is the operation
//     the mirror pass genuinely needs;
//   * the mirror mesh itself is BLENDED over the result, which is why one
//     shipped mirror is additive and another multiply without either being an
//     anomaly - the blend is the compositing operator, not a substitute for a
//     reflection.
//
// **What is NOT traced, and is this function's own reconstruction.** How the
// engine confines the reflected frame to the mirror's area is not established
// - the X flip in the bucket walk is global, and no clip or stencil step was
// followed. This composites through a MASK: the mirror's own corners are drawn
// alone to find which pixels it covers, and the reflection is taken there and
// nowhere else. That is a reasonable reading of a planar mirror and it is not
// evidence about the original. Anything downstream must say so.
//
// It runs on ANY backend, because it only uses the boundary: three
// `begin`/`submit`/`end`/`readback` cycles. That costs two extra readbacks a
// frame and is not how a GPU would want to do it - a stencil would - but it
// keeps the pass a DECISION rather than something a backend implements
// privately, which is A2's whole point.
struct MirrorStats {
    bool active = false;      // a mirror exists AND the camera is in front
    // The backend confined the reflection ITSELF (a stencil), so the frame
    // never touched the CPU and there is no mask to count. A caller that
    // presents has to know this: the CPU path leaves its result in a
    // `Surface` that must be uploaded, the native one leaves it on the GPU.
    bool native = false;
    long maskPixels = 0;      // CPU path only: how much of the frame it covers
    float distance = 0.0f;    // the camera's signed distance to the plane
};

// Draw `g` through `r`, with the mirror pass when `mp` names one. The finished
// frame is `r.readback()` afterwards, as usual.
MirrorStats drawWithMirror(Renderer& r, const Geometry& g,
                           std::span<const Texture> tex, const View& v,
                           const MirrorPlane& mp);

// ...and the form the GAME path needs: a draw list already built and sorted
// into the engine's bucket order out of several geometries - the resident
// set, the staged bodies, the props, the sprites. Only the set's corners
// carry `cornerMirror`, and each draw is run-split by it, so nothing else has
// to know a mirror exists.
//
// Until 2026-09-06 the pass had only the `Geometry` form and only the scene
// viewer called it, so a mirror reflected under `--scene` and was a flat
// blended pane everywhere a player would actually meet one. A reader: *the
// mirror in the chamber is displayed with transparency instead of
// reflecting.*
MirrorStats drawWithMirror(Renderer& r, std::span<const Draw> draws,
                           const View& v, const MirrorPlane& mp);

}  // namespace omk

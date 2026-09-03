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
    virtual bool drawMirrorScene(const View& /*v*/, const View& /*reflected*/,
                                 std::span<const Draw> /*scene*/,
                                 std::span<const Draw> /*sceneClipped*/,
                                 std::span<const Draw> /*mirror*/) {
        return false;
    }
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

}  // namespace omk

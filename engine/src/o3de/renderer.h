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

#include <algorithm>
#include <cmath>
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
    // 1.818:1, which `traces/frames/dlg402-*.png` show and which belongs to
    // conversations and cutscenes and not to free roaming (confirmed in
    // play). What makes it a viewport and not a crop is the vertical fov:
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
    // **That colour is NOT always black.** The load zeroes it (`memset`,
    // then `sub_44E830`'s `a1[84] = 0`), and `sub_41E7A0` writes it again
    // every frame, as `v5[84]` on
    // the decor slot's scene: the AREA's four colours at `+144` lerped by the
    // game clock (`o3de/daynight.h`) - black only where the chunk is (Kay'l's
    // flat), lava red in the cave, a dawn-to-night ramp in the cities. The
    // screen is cleared to the same colour (`clearColour`).
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
    // ...and the colour the picture is CLEARED to: `Game_Tick` clears the
    // screen every frame with `dword_90EFB0`, which `sub_41E7A0` sets to the
    // current scene's `+336` - the same colour as the fog - so the horizon
    // past the clip distance fades into it rather than into black
    // (`o3de/daynight.h`; todo/drift-audit.md L1 step 4).
    std::uint8_t clearColour[3] = {0, 0, 0};   // r, g, b
    // ...and the grey a PER-PIXEL-lit batch (`Draw::lit` 1, the enhancement of
    // row 7) starts from, 0..1: the active scene's `+416` - the set's ambient,
    // or the clock's in a day/night area - which is what the engine's own lit
    // path starts every vertex from (`sub_494E80`). It was black.
    float litBase = 0.0f;
    // THE NIGHT (`CityGrades::nightOf`, the remaster's): under per-pixel
    // lighting with the emitters' lights, the share of the SET's painted
    // light (mode 3) and of a lit body's start that stays - the point lights
    // add on top whole. 1 unless the remaster says otherwise.
    float setDim = 1.0f;
    // THE RELIEF (the remaster's B3, `[Remaster] relief = S`): the texture's
    // brightness read as height, tilting the normal the POINT lights see -
    // Vulkan's tiled module (`scene.frag`); 0 nothing.
    float relief = 0.0f;
    // THE MIST (the remaster's A4, `[Remaster] mist = S`): a height fog in the
    // set's fog colour, thickest at `mistGround` (the set's dominant floor -
    // the game's Y points DOWN) and thinning over `mistHeight` above it,
    // integrated along each ray - on what the game fogs; 0 nothing. Vulkan.
    float mist = 0.0f, mistGround = 0.0f, mistHeight = 400.0f;
    // THE RAIN on the WET FLOORS (`o3de/wetfloor.h`, the remaster's): the
    // game's shower now, 0..1 - its RIPPLES on a draw with `Draw::wet`, rings
    // timed by `shimmerClock`. 0 none. Vulkan and GLES.
    float rain = 0.0f;
    // THE SAND'S DUST (the restored weather's, the remaster's): the storm's
    // strength, 0 none; the drifting clouds' offset (x, z); the distance that
    // takes 63% at strength 1; its colour, lit as the scene. Vulkan and GLES.
    float dust = 0.0f, dustDrift[2] = {0.0f, 0.0f}, dustScale = 1400.0f;
    float dustColour[3] = {0.0f, 0.0f, 0.0f};
    // THE RAIN'S VEILS (the remaster's, with the 3D weather): far rain as
    // falling sheets - the shower's strength, 0 none; how far they have
    // fallen; the distance that takes 63%; their colour. Vulkan and GLES.
    float veil = 0.0f, veilFall = 0.0f, veilScale = 2500.0f;
    float veilColour[3] = {0.0f, 0.0f, 0.0f};
    // ...and the GUST's lean on them now: what it adds to their slant's
    // tangent, sheared about the eye's height (`remasterGust`)
    float veilGust = 0.0f;
    // THE PUDDLES on the wet floors (the remaster's): their strength, 0 an
    // even film. Vulkan and GLES.
    float puddles = 0.0f;
    // THE HALOS in the rain (the remaster's, with the 3D weather): around the
    // lights a wide soft glow of their bloom, the wet air's - the shower's
    // strength, 0 none (`post.frag`'s `kHaloGain`, `kHaloRadius`). Vulkan and
    // GLES, with the bloom.
    float halo = 0.0f;
    // THE GREYSCALE BANK (ops 150/151, `o3de/greybank.h`): the vertex colour
    // a batch ends with - after its shimmer and its lights - is taken to its
    // luma before the texture modulates it. The caller has already greyed
    // the fog and clear colours above and handed greyed palettes in
    // `setTextures`; this flag is the one part only a backend can do.
    bool grey = false;

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
    static constexpr int kMaxGpuLights = 64;
    // ...of which the SET's own lights are at most this many - the eight the
    // per-pixel enhancement always took; the rest are the remaster's point
    // lights (`o3de/emitterlight.h`), on a renderer whose `maxPixelLights`
    // says it has the room
    static constexpr int kOwnGpuLights = 8;
    std::vector<GpuLight> lights;   // empty = light per vertex, as before
    // THE TILED POINT LIGHTS (`o3de/lighttiles.h`, the remaster's): on a
    // renderer that says `tilesLights`, EVERY emitters' point light the frame
    // keeps, binned to screen tiles there - `lights` then holds the set's own
    // alone. Empty unless the remaster's emitters' light is on.
    std::vector<GpuLight> points;
    // THE LIGHTS' STREAKS on the wet floors (the remaster's, with the wet
    // floors): the emitters' lights nearest the camera within `kStreakFar`,
    // each drawn as a long highlight down every wet floor - tight across the
    // view, long along it (`scene.frag`'s `streakOf`); `streaks` their
    // strength, 0 none. Vulkan and GLES, under the emitters' per-pixel light.
    static constexpr int kMaxStreakLights = 16;
    std::vector<GpuLight> streakLights;
    float streaks = 0.0f;

    // ---------------------------------------------- THE SHIMMER's clock
    //
    // Mesh flag 0x8000000, and it is the GAME's - not an enhancement. 233 set
    // meshes carry it, all distant scenery, and their vertex colour
    // oscillates about zero on a 32-step cycle. `Game_Tick` advances this by
    // `2 * frameDelta` and wraps it at 256; `o3de/shimmer.h` has the table and
    // the index, and a backend that ignores this draws a static skyline where
    // the original draws a moving one.
    float shimmerClock = 0.0f;

    // ---------------------------------------------------- THE DITHER
    //
    // `sub_4638C0` sets D3DRENDERSTATE 26 (`DITHERENABLE`) to 1 on both device
    // arms, so this is the ENGINE's decision and defaults to on. The pattern
    // is a reconstruction - see `ui/surface.h`. `--dither 0` turns it off,
    // which is for laying two frames side by side, not for play.
    bool dither = true;
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
    // its shading. A lit instance starts from the SCENE's `+416` -
    // `sub_494E80` writes it into every runtime vertex's colour, and
    // `Read3DO_Init` sets it to the set's ambient grey (`Draw::lightBase`)
    // - and the crowd models ship pure white, so
    // there is no baked light in them to keep.
    // 0 not lit; 1 lit from the scene's `+416` (`View::litBase` - the
    // engine's own rule, for every body it lights: the crowd, the characters
    // and the props, all through `sub_494E80`); 2 lit ADDED to the baked
    // colour - kept in the shaders and used by nothing: the characters it
    // was for are lit by the engine's own rule too (todo/drift-audit.md L1).
    int             lit         = 0;
    // Row 6 again: whether this batch goes into the shadow map's depth pass.
    // Only characters do, and that is a constraint rather than a saving - a
    // set is shaded by a colour baked into every vertex which ALREADY contains
    // the artists' shadows (ASSETS 4c), so a map that darkened the set from
    // the set would double-darken every corner it painted once.
    bool            castsShadow = false;
    // A BODY POSED BY THE RENDERER (`todo/gpu-skinning.md`). Only for a
    // renderer that answers `posesBodies()`: `geo` is then a character's REST
    // geometry, whose `cornerMesh` names each corner's mesh, and every corner
    // is drawn at `M[mesh] * rest` - `meshPose` holds one affine per mesh of
    // the model, 12 floats each, the rows of a 3x4 (`world = R * rest + t`,
    // row r = R[r][0..2], t[r]). `meshPoses` is how many. The frontend folds
    // the bone's rotation and position AND the body's placement into it
    // (`omk::meshAffines`), so a corner costs the GPU one matrix and the CPU
    // nothing. Null for every other draw.
    const float*    meshPose  = nullptr;
    std::size_t     meshPoses = 0;
    // ...AND LIT BY IT (step 2): `vertexlight.h`'s law applied per corner to
    // the posed normal, with the lights `lightReach` found for the body - 8
    // floats each, `vertexLightCount` of them. `lightsFromBlack`: the corners
    // start BLACK (the crowd's rule, `sub_494E80`'s `instance[+416]`) rather
    // than from their baked colour. Only with a `meshPose`; a renderer that
    // cannot take `vertexLightCount` lights is not handed them (the frontend
    // asks `maxVertexLights()`).
    const float*    vertexLights     = nullptr;
    int             vertexLightCount = 0;
    bool            lightsFromBlack  = false;
    // ...and NOT quite black: the grey they start from, 0..1 - the scene's
    // `+416`, which `Read3DO_Init` sets from the set's AMBIENT (`.3DO`
    // desc+184 x 255) and `sub_494E80` writes into every lit vertex. The name
    // `lightsFromBlack` stays for the flag; this is its value
    // (todo/drift-audit.md L1).
    float           lightBase        = 0.0f;
    // THE BODY LIGHT (`o3de/lightprobe.h`, the remaster's A5): the start
    // colour - the grey above, or the baked colour - multiplied by this,
    // the painted light around the body. 1, 1, 1 on every draw unless the
    // option is on AND the renderer says it draws it (`drawsBodyTint`).
    float           lightTint[3]     = {1.0f, 1.0f, 1.0f};
    // A REFLECTING FLOOR's draw (`o3de/floorreflect.h`, the remaster's): its
    // triangles on the frame's reflection plane, adding the reflection as the
    // water does, at this times the pass's strength. 0 on every other draw;
    // set only by `drawWithWater`, for its own frame.
    float           reflect          = 0.0f;
    // A WET FLOOR's draw (`o3de/wetfloor.h`, the remaster's): its colour
    // darkened by this fraction, the rain's film - with or without `reflect`
    // (a wet floor off the frame's plane darkens only). Set only by
    // `drawWithWater`, for its own frame.
    float           wet              = 0.0f;
    // ...and SNOW lying on it instead (`o3de/wetfloor.h` coverRuns): `wet` is
    // then how white it turns, not how dark
    bool            snow             = false;
    // ...or a BODY in the rain (the remaster's wet characters,
    // `playremaster_draw.cpp` remasterWetBodies): `wet` how wet, the shaders'
    // body rule - a little darker, its lit highlights brighter - not a floor's
    bool            wetBody          = false;
    // ...and WHICH plane's reflection a reflecting floor's run adds: 0 the
    // frame's plane, 1 its SECOND level (`[Remaster] reflections = 2`,
    // `drawWithWater`'s `SecondPlane`) - a walkway above the street, a square
    // below it. Set only by `drawWithWater`, for its own frame.
    std::uint8_t    reflectPlane     = 0;
};

// THE TEXTURE A DRAW BINDS. The engine's is the key's low six bits and
// nothing else (ASSETS 4b), and its material cache has 58 slots
// (`SetMaterialsMemory(58, 0)`), so in the original those six bits never
// wrap. This port's pool is its OWN composition - both resident sets, every
// character model staged, the props, the sprites - and runs past 64 in a
// city after an interior (measured on the Quest: 72 slots, the three effect
// sprites wrapping onto set textures 5..7 - a street light's glow drawn in
// stone). So the full pool index rides ABOVE the 14-bit key, as `slot + 1`
// in bits 16..31 (`kKeySlotShift`), and a backend binds that when it is
// there; the low fourteen bits - the sort, the fog's bits, the six-bit slot
// itself - stay the engine's. A draw whose maker sets only the low bits
// (the interface's models, `splitDraws`) binds the six bits as before.
// the LIGHTS' STREAKS on the wet floors (`View::streakLights`): the emitters'
// lights within this of the camera (~100 m), faded over its last fifth
inline constexpr float kStreakFar = 4000.0f;
inline constexpr unsigned kKeySlotShift = 16;
inline std::uint32_t withPoolSlot(std::uint32_t key14, std::uint32_t slot) {
    return key14 | ((slot + 1u) << kKeySlotShift);
}
inline std::size_t drawTextureSlot(const Draw& d) {
    const std::uint32_t full = d.bucketKey >> kKeySlotShift;
    return full ? static_cast<std::size_t>(full - 1u) : static_cast<std::size_t>(d.bucketKey & 0x3Fu);
}

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
    // THE PRELOAD RADIUS (`todo/remaster.md`): the textures of the sets
    // prepared ahead, near doors - uploaded a few at a time by `preloadStep`
    // and KEPT (pinned) while they are listed, so a set arriving finds them
    // on the device. A new list replaces the old; what drops off it is
    // evicted by the next `setTextures` as usual. No-ops by default: a
    // backend that keeps no uploads (the software reference) has nothing to
    // hold.
    virtual void setPreloadTextures(std::span<const Texture> /*t*/) {}
    // whether it keeps them (Vulkan, GLES): without, a set prepared ahead
    // saves only its read and build, and fewer are held
    virtual bool keepsPreload() const { return false; }
    // whether this backend draws the TEXTURE PACK's replacements (Vulkan,
    // GLES): a set's load then decodes them ahead (`TexturePack::prepare`);
    // for one that never asks (the software reference) they would only wait
    virtual bool takesPack() const { return false; }
    // whether it draws the RELIEF (`View::relief`, Vulkan): the pack's normal
    // maps are decoded only for one that does (GLES never read them)
    virtual bool drawsRelief() const { return false; }
    // up to `budget` of the listed textures uploaded; -> how many still wait
    virtual int preloadStep(int /*budget*/) { return 0; }

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

    // Whether a `Draw` may carry a `meshPose` - the body posed on the GPU. The
    // software reference and Vulkan answer no, and the frontend poses on the
    // CPU for them, as it always has.
    virtual bool posesBodies() const { return false; }
    // THE DEPTH TIE, SETTLED ONCE (todo/optimization.md step 29). `order` is
    // every draw of `g` a frame could submit, UNCULLED, in the order the frame
    // submits them (stable by bucket key). A backend that answers true has
    // decided from it, once, which faces lose the engine's strict test to an
    // earlier coincident face, and draws those one depth step BACK from then
    // on instead of settling the tie per frame - the frontend calls this again
    // whenever the order can change (a set loaded, a texture base moved).
    // The GLES and Vulkan backends do this; the software reference keeps the
    // default, because its tie band (`raster.cpp` kDepthTie) is the rule itself.
    virtual bool bakeDepthTie(const Geometry* /*g*/, std::span<const Draw> /*order*/) { return false; }
    // How many lights a posed draw may carry (`Draw::vertexLights`).
    virtual int maxVertexLights() const { return 0; }
    // THE GPU'S OWN TIME for a recent frame, ms, from a timer query where the
    // backend has one (GLES: `GL_EXT_disjoint_timer_query` / `GL_EXT_timer_query`)
    // - the frame's work with no refresh in it (the Switch's swap waits for
    // one whatever the interval asked) - or -1 (none, or not yet in)
    virtual double gpuFrameMs() const { return -1.0; }
    // Whether the backend DRAWS two enhancements the frontend prepares for it:
    // `View::lights` (per-pixel lighting) and `shadowPass` (mapped shadows).
    // The frontend asks at start-up and REFUSES the enhancement when the
    // answer is no - preparing it anyway cost the Vita ~5 ms a frame for a
    // picture its GLES backend never drew, and worse, each one switches off
    // what it replaces (the crowd's per-vertex light, the classic shadows),
    // so an undrawn one left the crowd unlit and every body shadowless.
    virtual bool drawsPixelLights() const { return false; }
    virtual bool drawsShadowMap() const { return false; }

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

    // THE WATER REFLECTION (`todo/remaster.md` A3) - a REMASTER feature, not
    // the original's: the scene through `reflected` (the camera mirrored in
    // the water plane), `above` (the draws above it, the sky included) into a
    // target of the backend's own, then `scene` as usual and `water` - the
    // water's own draws - adding that reflection at their screen position
    // times `strength`. The boundary decides all of it (`drawWithWater`);
    // false where the backend has no such pass, and the caller draws as usual.
    // ...asked for when the renderer is MADE (before `init`): a backend may
    // have to build its programs for it (GLES). False where there is none.
    virtual bool setWater(float /*strength*/) { return false; }
    // THE BODY LIGHT (`o3de/lightprobe.h`): `Draw::lightTint` honoured on a
    // posed body. Asked before `init` (GLES builds a variant of its posing
    // programs); `drawsBodyTint` says afterwards whether it is drawn - the
    // frontend hands a tint only then, so a backend without it draws exactly
    // what it drew.
    virtual bool setBodyTint(bool /*on*/) { return false; }
    virtual bool drawsBodyTint() const { return false; }
    // THE EMITTERS' LIGHT (`o3de/emitterlight.h`): point lights - a negative
    // inner radius - honoured by the per-pixel lighting. Asked before `init`;
    // the frontend adds the emitters' lights to a set's table only when
    // `drawsEmitterLights`, so a backend without them lights what it lit.
    virtual bool setEmitterLights(bool /*on*/) { return false; }
    virtual bool drawsEmitterLights() const { return false; }
    // how many of `View::lights` the per-pixel lighting reads
    virtual int maxPixelLights() const { return View::kOwnGpuLights; }
    // whether it bins `View::points` to tiles itself (`o3de/lighttiles.h`)
    virtual bool tilesLights() const { return false; }
    // `planeY`: the water's height. The reflection pass DISCARDS what lies
    // below it per fragment - the CPU clip cannot see a posed body's place,
    // and a swimmer's legs were reflected above the water (a reader's play
    // test, 2026-10-20).
    // whether `drawWaterScene` can be drawn at all - asked BEFORE the frame's
    // shadow pass, which a refusal would otherwise have the mirror path run
    // again
    virtual bool drawsWater() const { return false; }
    virtual bool drawWaterScene(const View& /*v*/, const View& /*reflected*/,
                                std::span<const Draw> /*scene*/, std::span<const Draw> /*above*/,
                                std::span<const Draw> /*water*/, float /*strength*/, float /*planeY*/) {
        return false;
    }
    // THE SECOND REFLECTING LEVEL (`[Remaster] reflections = 2`): the floors'
    // frame with TWO planes, each its own reflection - `drawWaterScene`'s, and
    // the second through `reflected2` from `above2` (what lies above
    // `planeY2`), both at half size, the first in the reflection image's
    // top-left quarter and the second in its top-right; a floor's run reads
    // its own (`Draw::reflectPlane`). Floors only - no water draws.
    // `drawsTwoReflections` is asked before the runs are split: false, and
    // the frame keeps one plane.
    virtual bool drawsTwoReflections() const { return false; }
    // ...and whether the second level's floors were SEEN in the last frame
    // that drew any - an occlusion query round each of their runs in the
    // frame's pass (walls included, which the frustum test cannot see): 1
    // some sample passed, 0 none, -1 unknown (no query, or no result yet).
    // The frontend runs the second pass only while they are seen.
    virtual int secondLevelSeen() { return -1; }
    virtual bool drawTwoReflections(const View& /*v*/, const View& /*reflected*/,
                                    std::span<const Draw> /*scene*/, std::span<const Draw> /*above*/,
                                    float /*strength*/, float /*planeY*/, const View& /*reflected2*/,
                                    std::span<const Draw> /*above2*/, float /*planeY2*/) {
        return false;
    }
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

    // SUPERSAMPLING (`todo/enhancements.md` row 9): render the frame `n` times
    // larger each way and average it down. `n` of 1 is off.
    //
    // It is NOT the same enhancement as MSAA, which is why both exist. MSAA
    // samples geometry EDGES and does nothing for a texture, and this game's
    // aliasing is mostly texture: 256x256 atlases sampled POINT, and cutout
    // meshes - grilles, railings, signs - whose silhouette is a colour key
    // inside the triangle rather than at its edge, where MSAA never looks.
    //
    // Same contract as the other two: a request, before `init()`, that the
    // software reference declines.
    virtual bool setSupersample(int /*n*/) { return false; }
    // ...and its opposite, THE UPSCALE (`todo/remaster.md` A6): render the
    // frame `factor` times SMALLER each way and bring it up to the output with
    // AMD's FSR 1 (EASU, then RCAS's sharpening) before the present's dither.
    // 1 is off; it takes the place of the supersample. A REMASTER feature, for
    // the 4K frame rate - before `init()`, Vulkan only.
    virtual bool setUpscale(float /*factor*/) { return false; }
    // And anisotropic filtering, `n` samples along the footprint's long axis
    // (1 off, up to 16). Only meaningful with mode 2.
    virtual bool setAnisotropy(int /*n*/) { return false; }
    // THE REMASTER'S POST-PASS (`todo/remaster.md` step 2), both off unless
    // asked, both refused (false) where the backend has none - the software
    // reference draws what the original drew, GL1 and the 3DS have no pass.
    // The BLOOM: a glow from the texture pack's light maps, `strength` 0 off.
    virtual bool setBloom(float /*strength*/) { return false; }
    // The AMBIENT OCCLUSION (`todo/remaster.md` A4): contact shade from the
    // frame's depth, `strength` 0 off - before `init`, as it keeps the depth.
    virtual bool setAmbientOcclusion(float /*strength*/) { return false; }
    // The GRADE: a 3D colour LUT of n^3 RGB entries 0..1, red fastest (a
    // .cube file's order); null or n < 2 off. The viewer picks it per city.
    virtual bool setGrade(const float* /*rgb*/, int /*n*/) { return false; }

    // WHAT THIS BACKEND CAN DRAW AT ALL, of the requests above - a mask of the
    // `kTakes*` bits, asked by OMK's options menu (`ui/omkoptions.h`) so it
    // offers only rows the running renderer can honour. Static: "has the
    // pass", not "is it on" (that is `drawsWater` and the rest), and a device
    // may still reduce a value it cannot meet. The software reference takes
    // none of them: it draws what D3D was told to draw.
    virtual unsigned takes() const { return 0; }

    // THE TEXTURE FILTER and the ANISOTROPY asked AGAIN on a running renderer
    // (OMK's options menu, `todo/graphics-menu.md` 3d) - what `setTextureFilter`
    // / `setAnisotropy` ask before `init`. A backend that bakes them into its
    // uploads (a mip chain, a sampler in each texture's set) drops what it
    // uploaded, and the caller hands the pool over again. -> false where it
    // cannot. The default is the two setters, for a backend that re-filters
    // its textures in place (GL1, the 3DS).
    virtual bool retexture(int filter, int aniso) {
        const bool f = setTextureFilter(filter);
        setAnisotropy(aniso);
        return f;
    }
};

// `Renderer::takes()`'s bits, one per request of the boundary.
enum RendererTakes : unsigned {
    kTakesMultisample    = 1u << 0,    // setMultisample
    kTakesTextureFilter  = 1u << 1,    // setTextureFilter
    kTakesSupersample    = 1u << 2,    // setSupersample
    kTakesAnisotropy     = 1u << 3,    // setAnisotropy
    kTakesShadowMap      = 1u << 4,    // drawsShadowMap's pass (`shadowquality = mapped`)
    kTakesPixelLights    = 1u << 5,    // drawsPixelLights's (`lighting = perpixel`)
    kTakesUpscale        = 1u << 6,    // setUpscale
    kTakesBloom          = 1u << 7,    // setBloom
    kTakesGrade          = 1u << 8,    // setGrade
    kTakesWater          = 1u << 9,    // setWater, and the floors on its pass
    kTakesBodyTint       = 1u << 10,   // setBodyTint
    kTakesEmitterLights  = 1u << 11,   // setEmitterLights
    kTakesAmbientOcclusion = 1u << 12, // setAmbientOcclusion
    kTakesRelief         = 1u << 13,   // drawsRelief
    kTakesMist           = 1u << 14,   // View::mist
    kTakesTrilinear      = 1u << 15,   // setTextureFilter(2): a mip chain at upload
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
// `docs/ASSETS.md` 4c: the game HAS mirrors. Mesh flag
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
//     exactly what `RCamera::mirror` does - the control kept to show the
//     flipped camera reading differs is the operation the mirror pass
//     genuinely needs;
//   * the mirror mesh itself is BLENDED over the result, which is why one
//     shipped mirror is additive and another multiply without either being an
//     anomaly - the blend is the compositing operator, not a substitute for a
//     reflection.
//
// **How the engine confines the reflection is DRAW ORDER AND DEPTH** (read,
// `docs/ASSETS.md` 4c): `sub_440D90` draws the reflected frame, then the real
// one with nothing cleared between, so the real pass's nearer surfaces cover
// the virtual room everywhere but the mirror's opening. The GLES and Vulkan
// backends draw it exactly so (`drawMirrorScene`). THIS function - the CPU
// fallback, the software reference's path - composites through a MASK
// instead: the mirror's own corners are drawn alone to find which pixels it
// covers, and the reflection is taken there and nowhere else. The mask is a
// RECONSTRUCTION, kept because it runs on any backend; it differs from the
// engine's way by its read-back's dither and by a far object the mask lets
// through where the depths would not.
//
// It runs on ANY backend, because it only uses the boundary: three
// `begin`/`submit`/`end`/`readback` cycles, two extra readbacks a frame, while
// keeping the pass a DECISION rather than something a backend implements
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
// Without this form only the scene viewer's `Geometry` path reflects: a
// mirror reflects under `--scene` and is a flat blended pane everywhere a
// player actually meets one (seen in play: the chamber's mirror drawn with
// transparency instead of reflecting).
MirrorStats drawWithMirror(Renderer& r, std::span<const Draw> draws,
                           const View& v, const MirrorPlane& mp);

// THE WATER (`todo/remaster.md` A3) - a REMASTER feature, not the original's.
// A set's water plane, found from its own geometry: the height carrying the
// most area of HORIZONTAL faces drawn ADDITIVELY (the game's water is additive;
// so are glass and mirrors, which stand upright). `found` only when that area
// is a surface rather than a trim (at least 1e6 square units).
struct WaterPlane {
    bool   found = false;
    float  y = 0.0f;         // the plane's height (the game's Y points DOWN)
    double area = 0.0;       // the horizontal additive area at that height
    long   triangles = 0;
    // EVERY level past the floor, by height (the largest is `y`): a set may
    // hold its water at more than one (Jaunpur's canals)
    struct Level { float y; double area; long triangles; };
    std::vector<Level> levels;
};
// ...the functions are the remaster's, compiled with it alone (a shared
// caller would fail only at a `noremaster` link - the audit's cleanliness)
#if OMK_REMASTER
WaterPlane waterPlane(const Geometry& g);

// Every LEVEL triangle (its three corners within 2 units of one height) of the
// batches `want(batch)` takes: `f(a, c1, c2)`, in order. Bounded by the
// corners as well as the batch. The water's plane, the ground and the
// reflecting floors walk a set this way (one copy, where there were three).
template <class Want, class F>
void forLevelTriangles(const Geometry& g, Want&& want, F&& f) {
    for (const auto& b : g.batches) {
        if (!want(b)) continue;
        const std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(b.start) + b.count, g.corners.size());
        for (std::size_t t = b.start; t + 2 < end; t += 3) {
            const Corner& a = g.corners[t];
            const Corner& c1 = g.corners[t + 1];
            const Corner& c2 = g.corners[t + 2];
            if (std::fabs(a.y - c1.y) > 2.0f || std::fabs(a.y - c2.y) > 2.0f) continue;   // not level
            f(a, c1, c2);
        }
    }
}

// A set's GROUND: the height carrying the most LEVEL opaque area (`texpack_tool
// floors` lists them) - Qalisar's, Anekbah's and Lahoreh's street at 0,
// Jaunpur's quay at -120. The mist's floor. 0 for a geometry with none.
float groundLevel(const Geometry& g);

// The frame with the water REFLECTING, when the backend has the pass
// (`Renderer::drawWaterScene`) and the camera is above the plane: the draws
// split into the water's own triangles (additive, on the plane, of `setGeo`)
// and the rest; the reflected view (the eye and target mirrored in the plane,
// the image flipped vertically - a 180-degree roll and a screen-X flip); the
// draws above the plane for the reflection. False - and nothing drawn - when
// any of that does not hold, and the caller draws as it would have.
//
// ...or the REFLECTING FLOORS (`o3de/floorreflect.h`): `floorStrength` per
// material of `setGeo` (null for the water), and `wp` the floor's plane. The
// listed floors' triangles on it stay in the scene, IN ORDER (a decal and a
// depth tie ride on it), each run carrying its `Draw::reflect`.
// ...and the WET FLOORS (`o3de/wetfloor.h`): `wetTris` one byte per triangle
// of `setGeo`, 1 under the open sky, whose triangles on the plane reflect at
// `wetReflect` too (the larger of it and their material's), and every one of
// them, on the plane or not, darkened by `wetDarken` (`Draw::wet`). Either
// list may be null; both null is the water.
// ...and a SECOND floor level (`[Remaster] reflections = 2`): `second`'s
// plane, when found, the backend draws two (`drawsTwoReflections`) and the
// eye is above it too, takes the floors' triangles on it - within 4, a wet
// floor's within `kWetPlaneBand` - that the first plane does not; `drawn`
// says whether any did and the second reflection was drawn.
// ...`fade` scales its floors' reflection (0..1): at 0 its runs are still
// marked (`Draw::reflectPlane` 1, `reflect` 0) - the occlusion query's - but
// reflect nothing and no second pass is drawn.
struct SecondPlane {
    WaterPlane plane;
    float fade = 1.0f;
    bool drawn = false;
};
bool drawWithWater(Renderer& r, std::span<const Draw> draws, const View& v,
                   const Geometry* setGeo, const WaterPlane& wp, float strength,
                   const std::vector<float>* floorStrength = nullptr,
                   const std::vector<std::uint8_t>* wetTris = nullptr, float wetReflect = 0.0f,
                   float wetDarken = 0.0f, SecondPlane* second = nullptr);
#endif  // OMK_REMASTER

}  // namespace omk

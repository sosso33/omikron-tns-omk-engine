// SPDX-License-Identifier: GPL-3.0-or-later
// THE EMITTERS' LIGHT - the remaster's A5b (`todo/remaster.md`). NOT IN THE
// ORIGINAL: a `[Remaster]` option (`fxlight = S`, `--fx-light S`), off unless
// named.
//
// A set's flames, neon halos and lamp glows are PARTICLES (`o3de/particles.h`,
// the `.sfx` ambient effects): additive sprites that light nothing. The
// original lights a body from its scene's grey and the set's `.3DO` lights
// alone, and none of those sits at a flame - so Kay'l by Anekbah's gas
// burners is as dark as the alley (the reader's play test, 2026-10-20). This
// makes each GLOWING emitter a light of the set's own kind (`Light3do`),
// appended to the slot's table, so every path that lights a body - per vertex
// (`o3de/vertexlight.h`), per pixel (`View::lights`) - takes it unchanged.
//
// Which emitters glow is read from the effect, not its name: ADDITIVE (blend
// mode 4 or 5) and either a STANDING glow - a life of 2 frames or less, one
// sprite re-made on the spot every frame, a lamp's halo (Anekbah's 102
// `neon`, its `cac*`) - or a stream whose SPRITE is saturated, far from grey:
// a flame (`agaz`, its texture's bright texels 0.55). The steam and smoke -
// grey sprites (0.22), long-lived - are not light. The light's colour is the
// sprite's bright texels times the effect's colour ramp; a stream's
// intensity is multiplied by its particles' overlap, sqrt(count x life).
//
// They are POINT lights: the engine's lights shine one fixed direction
// (`normalize(centre - pos)`, `sub_493C30`), a flame shines every way. A
// NEGATIVE inner radius marks one - no shipped record has one - and the
// direction is then taken from the light to the lit point, per body or per
// pixel. The same linear falloff from |inner| to the outer radius.
//
// No oracle (tier 6). Drawn by Vulkan and GLES (`Renderer::setEmitterLights`);
// the software reference, GL1 and the 3DS never get them.
#pragma once

#include "formats/light3do.h"
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/texpack.h"
#include "o3de/particles.h"

#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <vector>

namespace omk {

struct EmitterLightCount {
    int glows = 0, flames = 0, dark = 0;   // dark: additive or not, no light
};

// The lights of `f`'s glowing emitters at strength `s` (0 none), each sprite's
// texture through `texOf` (null: the effect's colour alone).
std::vector<Light3do> emitterLights(const ParticleField& f,
                                    const std::function<const Texture*(int)>& texOf, float s,
                                    EmitterLightCount* count = nullptr);

// The rule's constants: a stream is a flame past this saturation of its
// sprite, (max - min) / max; a glow reaches 450 units, ~11 m, a flame 900, ~23 m
// (the world unit is the inch), both at full strength inside a fifth of that; the intensity is the
// `.3DO` record's `+32` (`sub_493E40` scales it by 256; the shipped median is
// 1.5), times the colour's brightness.
inline constexpr float kFlameSaturation = 0.4f;
inline constexpr float kGlowReach = 450.0f, kFlameReach = 900.0f, kInner = 0.2f;
inline constexpr float kGlowIntensity = 0.5f, kFlameIntensity = 1.2f, kMaxOverlap = 4.0f;
// ...and a lamp does not light its own HOUSING: a point light is nothing
// within `kSelfNear` of its emitter, whole past `kSelfFar` (a neon's glow sat
// on its disk and washed the sign's arrow out - the reader, 2026-10-20)
inline constexpr float kSelfNear = 30.0f, kSelfFar = 90.0f;

// THE SET'S GLOWING SURFACES as lights - its neon signs, lit windows, screens:
// the texels the texture pack's LIGHT MAPS mark strongly (`texpack.h`
// PackImage emissive, the bloom's), or whose colour is a NEON's - bright and
// strongly saturated - which the particles never were - a pink sign cast
// no pink light (the reader, 2026-10-20: "not visible that they emit light").
// The NEON RULE holds on a texture the curated list names only: unguarded it
// took AIMPASSE's orange wood for a tube (2026-11-26); a set the list does
// not name lights from its light maps alone.
// Each triangle of `g` with a light map, its light sampled at its corners and
// centre, weighted by its area, is summed into `kSurfaceCell` cells; a cell
// with more than `kSurfaceMin` square units of light becomes ONE point light
// at its light's centroid, coloured by its lit texels (the replacement's or
// the shipped colour), as strong as `S x min(1, sqrt(area) / kSurfaceArea)`
// times `kSurfaceIntensity`. `packs` is `replacements()` of `tex`, in order.
//
// THE CURATED LIST (`lights.txt` beside the city grades, `SurfaceCuration`):
// the data cannot tell a neon tube from a bright painted panel - both are
// bright, saturated texels - so where the list is given, ONLY the textures
// it names emit, each at its own GAIN (in place of the neon rule's boost)
// and, if it says one, its own COLOUR. Which texels glow stays the rules'.
// Lines `SET TEXTURE [U0,V0,U1,V1] = GAIN [RRGGBB]`, `*` for any set, `#` a
// comment. A texture is an ATLAS - Qalisar's `CATITR05` holds a yellow panel,
// lit windows and the pink letters - so an entry may name a RECTANGLE of it,
// in the original's texels (wrapped): a texel takes the first rectangle it
// lies in, else the texture's entry without one, else - the list being given
// - no light. (The reader, 2026-10-20: "go with the curated list".)
struct SurfaceCuration {
    struct Entry {
        float gain = 1.0f;
        bool hasColour = false; std::uint32_t colour = 0;
        bool hasRect = false; float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    };
    bool open(const std::string& path);   // false (and inactive) when absent
    bool active() const { return opened_; }
    // the entry for a texel at (u, v), already wrapped into the texture
    const Entry* find(const std::string& set, const std::string& texture, float u, float v) const;
    bool names(const std::string& set, const std::string& texture) const;
    // whether it curates `set` at all: a set it does not mention (and no `*`
    // line) keeps the automatic rules
    bool covers(const std::string& set) const;
    // ...and for a texture whatever its set (`curateBloom` - a texture is
    // decoded for the pool, which does not say which set asked)
    const Entry* findAny(const std::string& texture, float u, float v) const;
    bool namesAny(const std::string& texture) const;
    // the entry lists a lookup walks, found ONCE for a texture - a lookup by
    // name builds upper-case keys, which per texel (`curateBloom`) or per
    // lattice sample (`surfaceLights`) was a string or two each time
    struct Lists {
        const std::vector<Entry>* first = nullptr;    // the set's (or any set's)
        const std::vector<Entry>* second = nullptr;   // the `*` line's
        const Entry* at(float u, float v) const;
    };
    Lists listsFor(const std::string& set, const std::string& texture) const;
    Lists listsAny(const std::string& texture) const;
    std::size_t size() const { return count_; }
private:
    bool opened_ = false;
    std::size_t count_ = 0;
    std::unordered_map<std::string, std::vector<Entry>> entries_;   // "SET TEXTURE", upper case
    std::unordered_map<std::string, int> sets_;                       // the sets it names
    std::unordered_map<std::string, std::vector<Entry>> byTexture_;   // TEXTURE, any set
};
// the session's list (`lights.txt` beside the city grades)
SurfaceCuration& surfaceCuration();

// THE NEONS' BLOOM (`TexturePack::setLightHook`): the bloom glows where the
// light maps say, and Qalisar's pink letters have none - so in a curated
// rectangle a texel is written into the light map by its brightness (0 at a
// value of 0.3, whole at 0.7) times `kCuratedBloom x min(1, gain / 2)` ("just
// some bloom", the reader, 2026-10-20). A texture without a map gets one at
// its own size.
void curateBloom(const Texture& t, PackImage& pk);
// ...and the textures it paints into, whose LIGHT MAPS the pack decodes from
// their PNGs whatever block formats it holds (`TexturePack::setLightHook`'s
// `raw`); a colour read as blocks is read here from the shipped texels
bool bloomCurated(const Texture& t);
inline constexpr float kCuratedBloom = 1.0f;

// ...and what each texture would give, for writing the list
// (`texpack_tool lights`): its lit area, the neon rule's share of it, its
// lit texels' colour.
struct SurfaceStat {
    std::string texture;
    double lit = 0.0, neon = 0.0, r = 0.0, g = 0.0, b = 0.0;
};

std::vector<Light3do> surfaceLights(const Geometry& g, std::span<const Texture> tex,
                                    std::span<const PackImage> packs, float s, int* cells = nullptr,
                                    const std::string& set = std::string(), const SurfaceCuration* cur = nullptr,
                                    std::vector<SurfaceStat>* stats = nullptr);
inline constexpr float kSurfaceCell = 500.0f, kSurfaceMin = 300.0f, kSurfaceArea = 120.0f, kSurfaceFloor = 0.6f;
inline constexpr float kSurfaceReach = 1000.0f, kSurfaceIntensity = 0.5f;
// a NEON texel: saturation past kNeonSat and value past kNeonValue (ramped)
inline constexpr float kNeonSat = 0.55f, kNeonValue = 0.65f;
inline constexpr double kSurfaceSat = 2.0;   // the light's colour pushed from its grey
inline constexpr float kNeonBoost = 3.0f;    // a saturated light's intensity, up to 1 + this

// A point light (`emitterLights`' kind): its inner radius is negative.
inline bool pointLight(const Light3do& l) { return l.radiusB < 0.0f; }

}  // namespace omk

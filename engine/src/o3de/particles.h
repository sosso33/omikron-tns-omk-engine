// SPDX-License-Identifier: GPL-3.0-or-later
// THE PARTICLE SYSTEM - `Sfx_RegisterEmitter`, `Sfx_TickAmbient` and the
// integrator between them.
//
// An emitter is a `.SFX` section C effect placed at a point. Every frame it
// emits `count` particles; every particle carries a velocity, a lifetime, a
// scale and an angle, and is drawn as a camera-facing quad of the effect's
// sprite. This is the path the game's fire, smoke, steam, neon - and the
// intro's portal - all come out of.
//
// **Every rule here is `docs/ASSETS.md` §3b's, read from the engine:**
//
//   * the integrator adds the velocity to the position each frame, and adds
//     the effect's `+28` to the Y VELOCITY - an ACCELERATION, so a flame
//     climbs rather than drifting;
//   * the scale ramps by `+-scale/life` and the angle by the rotation rate;
//   * the frame index is `(frames - 1) * age / life`, so a sprite's quads are
//     played across its lifetime rather than at a fixed rate;
//   * the emission AXIS is jittered once per emitter under flag `0x40`, over
//     the `+x..+z` quadrant, so a flame LEANS and the lean is consistent;
//   * the period is in FRAMES with a `rand()` phase when non-zero, and 0 means
//     a particle every frame. The engine's delta is 1.0 per frame at 30 Hz
//     (`docs/BOOT.md` §4) - ticking any of this in seconds runs a set 30x too
//     slow, which is how the unit was found.
//
// **What is NOT ported.** `SetPiece_Show` is a state machine over the row's
// `+20` sub-array with flags at `+72` that chains into other pieces; only the
// trigger key and the position are used here, so a piece that would animate or
// chain does neither. And the alpha ramp (`0x0002`) is dead in the shipped
// data - no section C row sets it - so every particle keeps the default.
//
// **THE CONE, read out of `Sfx_TickAmbient` rather than approximated:**
//
//     theta = fx[+60] degrees          (+ rand * theta under flag 0x1000)
//     phi   = rand * 360 degrees
//     local = (cos phi sin theta, sin phi sin theta, -cos theta) * speed
//     dir   = basis * local
//
// so the spread is SPHERICAL POLAR about the axis, and the canonical direction
// is **-Z** - the same convention the rest of the engine heads by. The basis
// is `sub_442690`'s, two Euler angles that map -Z onto the jittered axis;
// building it from the axis directly is the same rotation and cannot disagree
// about which way -Z points. `speed` is `|vx, vy, vz|`, taken exactly as the
// engine takes it (`v60 = sqrt(vx*vx + vy*vy + vz*vz)`), and a speed of 0
// emits a STATIONARY particle, which is the engine's own `if (v60 == 0.0)`
// arm.
//
// An earlier version jittered each component of the axis instead. That was
// wrong about the distribution, and the suspicion that went with it - that
// `+16` was not really a velocity - was ALSO wrong: it is, and the engine
// throws `part01` just as far.
//
// **THE SPRITE IS AN ID, NOT AN INDEX.** `Sfx_TickAmbient` resolves an
// effect's `+8` through the SCENE - `sub_4A5800(scene + 8, sprite)` then
// `sub_4A5B60` - against the 36-byte registry rows whose `+32` carries the id.
// GRID registers ids 9..12 and its effects name exactly those; indexing the
// twenty `aventure.scx` holds instead swapped IMPACT1 with IMPACT2 and turned
// `burn`'s blue smoke into a muzzle flash, which is what made the portal come
// out fire-orange.
//
// **AND A PARTICLE HAS A COLOUR.** Section C `+48` is its colour at birth and
// `+52` its colour at death, packed `0x00RRGGBB`; `Sfx_TickAmbient` compares
// the two dwords and only builds a per-frame ramp `(c1 - c0)/life` when they
// differ. This is what makes the intro's portal BLUE: `vd` draws an orange
// impact sprite modulated by `0x2125AF`, and no reading of the texture alone
// could have got there.
//
// The same block settles two more numbers this file used to guess:
//
//   * the instance's scale starts at the effect's `+56` (`u32i(v17, 6)` and
//     `+28`), NOT at 1.0, and the `+-scale/life` ramp is applied only under
//     flag `0x4` (grow) or `0x2000` (shrink) - so `vd` is a constant 0.4 of
//     the sprite quad and `burn` doubles from 2.2 to 4.4 over nine frames;
//   * the alpha is **0.5** (`u32i(v17, 9) = 0x3F000000`), overwriting the
//     0.887 `Sprite_SpawnInstance` initialises, unless flag `0x2` ramps it
//     from 0 by `1/life` - and no shipped row sets that.
//
// **WHAT THE BLENDS DO WITH THE ALPHA - nothing, for every shipped effect.**
// `Raster_DrawTriangles` sets the additive bucket to SRCBLEND=ONE /
// DESTBLEND=ONE and the multiply bucket to ZERO / INVSRCCOLOR (D3DBLEND 4), so
// the frame becomes `dst + tex*colour` and `dst * (1 - tex*colour)`; the 0.5
// goes into the diffuse dword's HIGH BYTE (`u32(v6,108) << 24` in the bucket
// walk) and is read only by the plain transparent modes 2/3, SRCALPHA /
// INVSRCALPHA, which 0 of 396 shipped section C rows ask for. A first port
// carried the alpha as a colour scale and so drew every particle at half
// strength - the portal's cyan disc came out a dim haze - which is the
// mistake the sprite table (`tools/verify.py: sprite blend modes`) had
// already ruled out for meshes.
//
// **THE MULTIPLY IS `dst * (1 - src)`**, not `dst * src`: it darkens where the
// sprite is BRIGHT and leaves the frame alone where it is black. `ttt`, the
// intro's one multiply row, is a starburst, and the capture's dark spiky ring
// inside the disc is that starburst subtracted - the other reading paints a
// black square with a bright star cut out of it.
//
// **AND THE ORDER IS THE BUCKET KEY'S.** `Render_SubmitSprites` ORs the mode's
// bits into the same 14-bit key as meshes (0x2100 additive, 0x2200 multiply)
// and `Render_FlushBuckets` walks the keys ascending, so every additive
// particle draws before every multiply one, whatever their textures. Grouped
// by sprite first, the multiply starburst (sprite 10) drew before the puffs
// (sprite 12) and darkened only black - no ring at all.
//
// **THE LIFETIME AND THE FRAME INDEX, one step each.** The tick's per-particle
// test is `age <= life`: a particle whose age has passed its life is released,
// every other one - the newborns included - is integrated and drawn, so an
// effect of life L keeps L+1 alive and GRID's four rows hold 210, not 194.
// The frame index `(frames-1) * age / life` is taken from the age BEFORE the
// increment, so a newborn is drawn on frame 0 and the last draw is frame
// `frames-1`; computed after, `burnv`'s three-frame life never showed
// IMPACT2's bright frame 0 at all.
//
// **THE DRIFT IS NEGATED** unless flag bits 0x600 read exactly 0x200
// (`fld [ebx+1Ch]; fchs` at 0x46DD9D) - Y points down, so a positive drift
// climbs. 104 shipped rows carry a non-zero drift with the bit clear.
// 0x400/0x600 leave the value untouched in the assembly and no shipped row
// sets either, so they take the negated arm here and say so.
//
// TIER: the cone, the speed, the colour, the scale, the lifetime, the frame
// index, the blend arithmetic, the order and the integrator are transcribed
// from `Sfx_TickAmbient`, `Render_SubmitSprites` and `Raster_DrawTriangles`;
// the period is not. `tools/verify.py: engine particles` asserts each rule
// through `tools/particle_probe.cpp`, and the frame is compared against
// `traces/frames/intro-75.png` by eye.
#pragma once

#include "formats/sfx.h"
#include "o3de/geom3do.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

struct Particle {
    float pos[3] = {0, 0, 0};
    float vel[3] = {0, 0, 0};
    float age = 0, life = 0;
    // The age the FRAME INDEX is taken from: the value before this tick's
    // increment, which is when the engine computes `+22`.
    float frameAge = 0;
    // The INSTANCE scale, which multiplies the sprite quad's own size - not a
    // size in itself. It STARTS at the effect's `+56` and `dScale` is the
    // `+-scale/life` ramp, which only runs under flag 0x4 or 0x2000.
    float scale = 1, dScale = 0;
    // The colour, 0..1 per channel, and its per-frame ramp toward `+52`.
    float col[3] = {1, 1, 1}, dCol[3] = {0, 0, 0};
    // The instance's `+36`, taken by `Render_SubmitSprites` as `x * 255` into
    // the diffuse's high byte. Carried, and read by NO shipped blend mode -
    // see the header. It is not folded into the colour.
    float alpha = 0.5f, dAlpha = 0;
    float angle = 0, spin = 0;
    // The effect's `+28`, added to the Y VELOCITY once a frame - an
    // ACCELERATION, not a velocity. ASSETS 3b measured it: the brazier's
    // flame climbs 175 units over 23 frames, not 46, which is what settles
    // acceleration over a constant drift.
    float drift = 0;
    int   sprite = 0;
    std::uint8_t mode = 0;
    // The tick's own test: integrated and drawn while `age <= life`.
    bool  alive() const { return age <= life; }
};

struct Emitter {
    FxEffect fx;
    float pos[3] = {0, 0, 0};
    float axis[3] = {0, 0, 1};   // jittered once, under flag 0x40
    float phase = 0;             // frames until the next emission
    bool  live = true;
};

// The emitters a scene has registered, and the particles they have made.
class ParticleField {
public:
    // `Sfx_RegisterEmitter`: place an effect at a point. The axis jitter
    // happens HERE, once, not per particle.
    void add(const FxEffect& fx, const float pos[3]);

    // `Sfx_RegisterEmitter` + the same frame's `Sfx_TickAmbient`: a ONE-SHOT
    // emitter. Both of its countdowns (`+36` sound, `+40` spawn) are 0.0 in
    // all 396 shipped section C rows, so the emitter goes negative on the
    // tick it was registered, emits once, and is freed. This is how a set
    // piece sustains an effect: `sub_451600` registers one of these for every
    // shown row EVERY frame, so the axis jitter under flag 0x40 is re-rolled
    // per frame for a piece and once for an ambient emitter.
    void spawn(const FxEffect& fx, const float pos[3]);

    // One frame of every emitter and every particle. `dt` is in FRAMES.
    void tick(float dt = 1.0f);

    void clear() { emitters_.clear(); particles_.clear(); }
    std::size_t emitterCount() const { return emitters_.size(); }
    std::size_t count() const { return particles_.size(); }
    const std::vector<Particle>& particles() const { return particles_; }

    // A deterministic generator, so a check can assert a frame. The engine's
    // own `rand()` is the C library's and is not reproducible across hosts,
    // so what is ported is WHERE randomness enters, not which numbers come
    // out - `PORTING` B5's argument one level down.
    void seed(std::uint32_t s) { rng_ = s ? s : 1u; }

private:
    float rnd();                 // [0, 1)
    Emitter place(const FxEffect& fx, const float pos[3]);
    void  emit(Emitter& e);

    std::vector<Emitter>  emitters_;
    std::vector<Particle> particles_;
    std::uint32_t rng_ = 1u;
};

// ONE SPRITE'S FRAMES.
//
// A sprite is a `.3DO` whose QUADS are its animation frames - `quad =
// node->quads + 32 * frame`, which `Sprite_SetFrame`'s bounds check against
// the model's quad count and `Cef_SpawnEffect`'s frame count both confirm. So
// a frame is four UV pairs, in the material's own texel units, and the frame
// index is `(frames - 1) * age / life`.
struct SpriteFrames {
    // four (u, v) pairs a frame, in perimeter order - the quad's own.
    std::vector<std::array<float, 8>> frames;
    // and the quad's own HALF-EXTENT in world units, which is the billboard's
    // base size: `Render_SubmitSprites` builds the quad from its diagonal
    // CORNERS scaled by the instance's `+24`/`+28`, and `Sprite_SpawnInstance`
    // starts those at 1.0. The shipped sprites are 24.61, 34.45, 196.85 or
    // 275.59 - which are 62.5, 87.5, 500 and 700 cm in the inch unit - so a
    // particle is metres across, not units. Taking the effect's `scale` field
    // as the half-extent instead makes every particle ~75x too small, which is
    // what a first version did.
    std::vector<float> extent;
    int material = 0;
};

// Read a sprite's frames out of its `.3DO`.
SpriteFrames spriteFrames(std::span<const std::byte> model);

// The bucket-key bits `Render_SubmitSprites` ORs in for an instance mode -
// its own switch, `BYTE1(v22) |= ...`, so each value is the byte << 8:
// 1/8 -> 0x400, 2 -> 0x2000, 3 -> 0x2400, 4 -> 0x2100, 5 -> 0x2500,
// 6 -> 0x2200, 7 -> 0x2600, 0 -> nothing. Ascending key is the draw order.
int spriteModeBits(std::uint8_t mode);

// The camera-facing quads for a field, ready to submit - REBUILT into `g`,
// whose `revision` is bumped, so a backend that caches a vertex buffer by
// pointer sees every frame's change (the same rule `applyPose` follows).
//
// One batch per (mode, sprite) pair in the ENGINE'S order - ascending bucket
// key, the mode's bits above the sprite - with the batch's `material` set to
// the SPRITE INDEX: the caller owns the texture pool and maps that to a slot,
// the same way a set's batches carry a material the caller resolves.
//
// The blend is the effect's own mode, which the renderer switches on
// (ASSETS 3b): 0 opaque, 1/8 cutout, 2/3 transparent, **4/5 ADDITIVE** -
// which is what `Cef_SpawnEffect` sets and therefore what every character
// effect uses - 6/7 multiply, with 3/5/7/8 also cutout. The corner colour is
// the particle's colour, NOT scaled by its alpha: neither blend reads it.
void particleGeometry(Geometry& g, const ParticleField& f, const float eye[3],
                      const float at[3],
                      const std::vector<SpriteFrames>& sprites);

}  // namespace omk

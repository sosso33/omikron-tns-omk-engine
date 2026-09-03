// SPDX-License-Identifier: GPL-3.0-or-later
// `SCPTDATA/*.SFX` - a scene's sounds and its ambient EFFECTS.
//
// Magic "5.0V", six sections back to back, each a count then fixed-size
// records. Nothing points at the next one, so - as with a `.CTL` - the walk
// landing exactly on the file size is the proof that every stride is right:
//
//     +0   char[4] "5.0V"
//     +4   uint32 A, then A x 40 bytes
//          uint32 B, then B x 44
//          uint32 C, then C x 80      the EFFECTS
//          uint32 D, then D x 16      the emitter bindings
//          uint32 E, then E x 76      the decor pieces
//          uint32 F, then F x {16 header + 36 x n}
//
// **Section C is the effect** - sprite, velocity, lifetime, cone, scale,
// rotation, blend mode - and **section D binds it to a mesh** by a four-byte
// tag compared as a dword against the first four bytes of the mesh's name.
// That is the middle of the chain a set's neon, smoke, fire and steam come
// out of, and the three files it crosses were authored separately:
//
//     .3DO   a mesh flagged 0x40000000, and its position
//       |    the first FOUR BYTES of its name, as a dword
//     .SFX   section D -> section C: the effect
//       |
//     .SCX   chunk 4: the sprite, whose QUADS are its animation frames
//
// Read from `Sfx_BindAmbientEffects` (the name compare), `Sfx_RegisterEmitter`
// (a 100-slot table), `Sfx_TickAmbient` (the emission) and
// `Render_SubmitSprites`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace omk {

// Section C's flags, from Sfx_TickAmbient. Every one is a randomisation or a
// ramp - the engine has no other per-particle behaviour.
inline constexpr std::uint32_t kFxFade   = 0x0002;  // alpha ramp: NO shipped
                                                    // row uses it, so it never
                                                    // runs and every particle
                                                    // keeps the default 0.5
inline constexpr std::uint32_t kFxGrow   = 0x0004;  // scale += scale/life
inline constexpr std::uint32_t kFxShrink = 0x2000;  // scale -= scale/life
inline constexpr std::uint32_t kFxAngle  = 0x0010;  // random start angle
inline constexpr std::uint32_t kFxJitDir = 0x0040;  // jitter the emission AXIS
                                                    // once per emitter, +[0,2)
                                                    // on each component -
                                                    // always positive, so the
                                                    // lean is consistent
inline constexpr std::uint32_t kFxNCount = 0x0080;  // count += count*rand*0.1
// `Sfx_TickAmbient` widens the cone by `rand() * cone` under this one, which
// is why a flame's spread is not fixed.
inline constexpr std::uint32_t kFxRndCone = 0x1000;
inline constexpr std::uint32_t kFxNLife  = 0x0100;  // life  += life *rand*0.1

struct FxEffect {
    std::int32_t  id = 0;
    std::int32_t  sound = 0;
    std::uint16_t sprite = 0;
    std::uint32_t flags = 0;
    float vx = 0, vy = 0, vz = 0;
    float drift = 0;      // +28 is an ACCELERATION on world Y: the integrator
                          // does `vel.y += drift` once a frame
    float life = 0;       // in FRAMES
    float scale = 0, cone = 0, spin = 0;
    std::int16_t count = 0;
    // The particle's COLOUR, `+48` at birth ramping to `+52` over its life -
    // packed 0x00RRGGBB, the three bytes taken high to low. `Sfx_TickAmbient`
    // tests the two dwords for equality and only builds a ramp when they
    // differ, so a constant colour costs nothing. 0..255 each.
    std::uint32_t colour0 = 0xFFFFFF, colour1 = 0xFFFFFF;
    std::uint8_t mode = 0;         // the renderer's blend mode, 0..8
    std::string  name;
};

struct FxBinding {
    std::int32_t  effect = 0;
    char          tag[5] = {};     // the four bytes matched against a mesh name
    // +12 is the emitter's PERIOD in FRAMES, not seconds.
    // Sfx_BindAmbientEffects tests it against 0.0 and, when greater, seeds the
    // starting phase with rand() % (int)period. 0 means a particle every frame.
    // The engine's default frame delta is 1.0, so a lifetime of 15 is half a
    // second - ticking any of this in seconds runs a set 30x too slow.
    float period = 0;
};

// SECTION E - a SET PIECE, and what fires one.
//
// Starting a scene object does two things, adjacent in all four handlers that
// do it: `Script_StartScript(instance)` and then
// `sub_451470(a1, instanceId & 0xFFFF)`. `sub_451470` walks this section and
// calls `SetPiece_Show` for every row whose `+8` is `a1` and `+12` is that id.
// So **a row binds an effect to an object-START event**, which is the trigger
// the scene-sprite path never had.
//
// `grid.sfx` keys rows to objects 1, 8 and 20 - `1KaylArrives`, `3KaylLeaves`
// and `Wait5sec` - and AREA 118's script starts all three; the `Wait5sec` rows
// sit at (869, -133, 192), inside the tunnel `circle01`/`circle2` describe.
//
// **Rows keyed `(1, -1)` come up with the ENVIRONMENT.** Four of GRID's
// eleven, and the ones sitting where the intro's PORTAL is, cannot be fired by
// that call at all: it masks the id with `0xFFFF`, which can never equal -1.
// What shows them is `Sfx_BindAmbientEffects`, which ends by walking section E
// itself and calling `SetPiece_Show` on every `(1, -1)` row - so binding a
// set's ambient effects brings up its standing ones, and no script is
// involved. That is why a search through `Script_StartScript` found nothing.
//
// Still not ported: `SetPiece_Show` is a state machine over the row's `+20`
// sub-array (section F's 36-byte records) with flags at `+72`, chaining into
// other pieces; only the trigger key and the position are used.
// One emitter of a set piece - a section F sub-record, 36 bytes.
//
// Section F is `{16-byte header + n x 36}` per block, and a block belongs to
// the section E row whose `+4` handle its header carries (plus 4). A row's
// `+16` is that block's index. Each sub-record names an EFFECT at `+4` - a
// small int inside section C's range - and carries its own absolute position
// at `+8`. So a piece is a GROUP of emitters, which is why GRID's row 3 has
// twenty-seven of them and a single emitter looked far too sparse.
// One RECORD of a set piece's section F block - a waypoint, not an emitter.
// `sub_450E50` interpolates the piece's position between consecutive records
// over `dur` frames, and the indirect form (`FxSetPiece::effectId <= 0`) takes
// its effect from the record the walk is on.
struct FxPiecePart {
    std::int32_t effect = 0;    // +4  section C's 1-based id (the indirect form)
    float pos[3] = {0, 0, 0};   // +8  in the LINK's frame, or absolute
    float dur = 0;              // +20 frames to the next record
    // +24/+28: what this record's position is relative to. 0 absolute;
    // 1 a section E row by its +0 id; 2 an ACTOR by a three-letter tag packed
    // big-endian in the id ('HO1' = 0x484F31); 3 the PLAYER. The row's own
    // link (`FxSetPiece::linkType`) overrides this when both are set.
    std::int32_t  linkType = 0;
    std::uint32_t linkId = 0;
};

struct FxSetPiece {
    std::int32_t id = 0;        // +0   the row's own id; a type-1 link names it
    std::int32_t key0 = 0;      // +8   matched against sub_451470's a1
    std::int32_t key1 = 0;      // +12  matched against its a2 (the object id)
    std::int32_t block = 0;     // +16  which section F block holds its parts
    // +52 THE EFFECT, by section C's `+0` ID - which is 1-BASED, so it is not
    // the array index. `sub_451600` scans section C for a row whose `+0`
    // matches this; an index lookup is off by one on every effect in the game.
    // 0 means the indirect form, which reads `+24` instead and is unported.
    std::int32_t effectId = 0;
    float pos[3] = {0, 0, 0};   // +28  where the piece sits, in world units
                                //      (the shipped value; the runtime rewrites
                                //      it from the block every frame)
    // +40/+44: the row's own link, as `FxPiecePart::linkType/linkId`.
    std::int32_t  linkType = 0;
    std::uint32_t linkId = 0;
    float        delay = 0;     // +56  frames to wait before playing (flag 0x20)
    std::int32_t loops = 0;     // +64  how many times the block plays; 999 = forever
    std::uint32_t flags = 0;    // +72  0x4 start reversed, 0x10 ping-pong,
                                //      0x40 smoothed heading, 0x80 roll,
                                //      0x100 roll interpolation (see setpiece.h)
    std::vector<FxPiecePart> parts;   // its section F block, resolved
};

struct SfxFile {
    bool valid = false;
    bool exact = false;                 // the walk landed on the file size
    std::size_t size = 0, end = 0;
    std::uint32_t counts[6] = {};       // A..F
    std::vector<FxEffect>  effects;     // section C
    std::vector<FxBinding> bindings;    // section D
    std::vector<FxSetPiece> pieces;     // section E

    // Section C by its `+0` ID rather than by index - the ids are 1-based.
    const FxEffect* byId(std::int32_t id) const {
        for (const auto& e : effects) if (e.id == id) return &e;
        return nullptr;
    }
};

SfxFile readSfx(std::span<const std::byte> d);

}  // namespace omk

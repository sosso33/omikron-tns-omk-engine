// SPDX-License-Identifier: GPL-3.0-or-later
// MELEE - see fight.h for what this is and the standard it is held to.
//
// Every function below names the engine function it transcribes. Where the
// engine reaches for something this tree does not have, the call is LABELLED
// rather than approximated silently:
//
//   * the channel RATE. `Fight_Begin` scales each fighter's animation rate by
//     property 19 *Carac Fight Experience* (`sub_45ACD0(chan, exp/41)`), and
//     `CefChannel` has no rate - it advances by the caller's `dt`. Recorded in
//     `FightStats` and applied by the CALLER through `dt` if it wants it.
//   * the separation push. `Fight_KeepSeparation` moves each body with
//     `Actor_Move`, the walker's own move, so a wall still stops it. Here the
//     push is applied straight to the two `FightBody`s through `moveBody`,
//     which `omk-play` replaces with the walker in step 2.
//   * `sub_49A830`'s transition allow-list. The engine keeps a per-fighter
//     list (`sub_45B760`/`sub_45B8A0`/`sub_45B8E0`, filled by `Fight_Engage`)
//     of which cached reaction entries may interrupt the current move, keyed
//     by the combat block's flag bits. The list is derived here and recorded
//     on the context, but nothing feeds it back into `CefChannel`'s
//     transition search - the channel does not model the list at all, and
//     pretending otherwise would be the "approximation that works in one
//     case" this repo has a rule about.
//   * the screen fade, the camera and the gauges: steps 3, 4 and 5.
#include "actor/fight.h"
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace omk {

namespace {

constexpr float kDegPerRad = 57.29577951308232f;
// The two distances `sub_4452A0` switches banks at, in the engine's inches:
// 118.11024 is three metres and 59.055119 is one and a half.
constexpr float kFarSwitch  = 118.11024f;
constexpr float kNearSwitch = 59.055119f;
// `Fight_TickAI`'s closing distance: 78.740158 inches, which is two metres.
constexpr float kCloseIn    = 78.740158f;

// The combat block's ten floats, named. `CtlCombat::asInt` reinterprets the
// three that are integers wearing a float's bytes (ASSETS - the combat block).
float windowStart(const CtlCombat& b) { return b.raw[3]; }
float windowEnd(const CtlCombat& b)   { return b.raw[4]; }
std::int32_t blockFlags(const CtlCombat& b) { return b.asInt(0); }

const CtlState* stateAt(const FightContext& c, int idx) {
    if (!c.body || !c.body->channel || idx < 0) return nullptr;
    const auto& states = c.body->channel->ctl().states;
    if (static_cast<std::size_t>(idx) >= states.size()) return nullptr;
    return &states[static_cast<std::size_t>(idx)];
}

// `SetPersoBank` (0x0045A510): reset the input queue and `GoToMove` into the
// entry, from whatever is playing. The alias chase at its head is `gotoMove`'s
// own, so this is the queue reset plus the transition.
bool forceEntry(FightContext& c, int entry) {
    if (!c.body || !c.body->channel || entry < 0) return false;
    c.body->channel->resetInputQueue();
    return c.body->channel->gotoMove(c.body->channel->state(), entry, 1.0f);
}

void moveBody(FightBody& b, float dx, float dy, float dz) {
    b.x += dx; b.y += dy; b.z += dz;
}

}  // namespace

bool Fight::windowOpen(const CtlCombat& blk, float before, float after) {
    // Fight_ResolveHit's own test, one frame of slack each side, kept static
    // so the probe can re-derive a decision without this class.
    if (before > windowEnd(blk) + 1.0f) return false;
    if (before < windowStart(blk) - 1.0f && after < windowEnd(blk) + 1.0f)
        return false;
    return true;
}

int Fight::entryByRole(const FightContext& c, int role) const {
    if (!c.body || !c.body->channel) return -1;
    return c.body->channel->findEntryByRole(role);
}

int Fight::entryByLow16(const FightContext& c, int id) const {
    if (!c.body || !c.body->channel || id == -1) return -1;
    return c.body->channel->findEntryByCode(id);
}

// ---- Fight_Begin 0x004455B0 + Fight_SelectAiProfile 0x004652B0 -----------
// ---- camera mode 14: `Fight_TickCamera` 0x00446500 and its helpers -------
//
// Every distance here is an exact metre in the engine's inches, which is the
// usual sign a reading is right: 157.48032 = 4 m, 118.11024 = 3 m, 98.425201
// = 2.5 m, 59.055119 = 1.5 m, 39.370079 = 1 m, 19.68504 = 0.5 m, 9.8425198 =
// 0.25 m, 7.8740158 = 0.2 m.
namespace {
constexpr float kDegToRad = 0.0174532925199433f;
// The two float constants the decompilation prints as integer bit patterns.
float bits(std::int32_t v) { float f = 0.0f; std::memcpy(&f, &v, 4); return f; }
}  // namespace

void Fight::camMidpoint(float out[3]) const {
    out[0] = out[1] = out[2] = 0.0f;
    if (!a_.body || !b_.body) return;
    out[0] = (a_.body->x + b_.body->x) * 0.5f;
    out[1] = (a_.body->y + b_.body->y) * 0.5f;
    out[2] = (a_.body->z + b_.body->z) * 0.5f;
}

// `sub_446000`: the orbit. The eye sits on a circle about the fighters'
// midpoint whose radius grows with their separation, at a heading taken from
// the line between them; `ease` blends toward it instead of cutting.
void Fight::camOrbit(bool ease, float angleOff, float height, float atLift) {
    // "Vue de dos" forces the rig: the heading offset, no extra target lift,
    // and the eye a metre and a half lower.
    if (!combatCamera_) { angleOff = -70.0f; atLift = 0.0f; height -= 59.055119f; }
    // `++dword_906F24 % dword_906F28` - the camera is recomputed every Nth
    // frame, and N is 3 in the close state.
    if (cam_.divider > 1 && (++cam_.frames % cam_.divider) != 0) return;
    if (cam_.divider <= 1) ++cam_.frames;
    if (!a_.body || !b_.body) return;

    measureSeparation();
    const float radius = (separation_ + 157.48032f) * 0.65161264f;
    float mid[3]; camMidpoint(mid);
    const float lift = 5.9055123f + (a_.body->y + b_.body->y) * 0.5f;
    const float deg = std::atan2(b_.body->z - a_.body->z,
                                 b_.body->x - a_.body->x) * kDegPerRad
                      + 90.0f + angleOff;
    const float rad = deg * kDegToRad;
    const float ex = mid[0] - std::cos(rad) * radius;
    const float ez = mid[2] - std::sin(rad) * radius;
    const float eyeK = 0.5f, atK = 0.1f;        // flt_530C60 / flt_530C6C
    if (ease) {
        cam_.eye[0] = cam_.eye[0] * (1.0f - eyeK) + ex * eyeK;
        cam_.eye[2] = cam_.eye[2] * (1.0f - eyeK) + ez * eyeK;
        cam_.at[0]  = cam_.at[0]  * (1.0f - atK)  + mid[0] * atK;
        cam_.at[2]  = cam_.at[2]  * (1.0f - atK)  + mid[2] * atK;
    } else {
        cam_.eye[0] = ex; cam_.eye[2] = ez;
        cam_.at[0]  = mid[0]; cam_.at[2] = mid[2];
    }
    const float base = lift + 9.8425198f;
    cam_.at[1]  = base + atLift;
    cam_.eye[1] = height + base;
    cam_.heading = deg;
    cam_.radius = radius;
}

// `sub_446240`: the THROW. One random swing of up to 90 degrees, spread over
// what is left of the thrown fighter's clip, so the camera sweeps around the
// pair while the throw plays.
void Fight::camThrow(float height) {
    if (!cam_.placed) {
        // THE SWING IS SPREAD OVER THE THROWN FIGHTER'S CLIP, and this used to
        // be a hard-coded 30 frames labelled as a stand-in for `sub_45ACF0`,
        // on a reading that called it "the channel's remaining time". It is
        // not: `sub_45ACF0(chan)` is `dword_8F5928[57 * chan]`, the channel's
        // `+8`, which is the current clip's LENGTH (`CefChannel::clipLength`).
        //
        // `sub_446240` picks whose: `if (dword_906FA4 == 11)` - fighter A's
        // channel when A is the one in the throw, else B's - so the arc is
        // divided by the length of the clip the throw is actually playing.
        // With a constant instead, a short throw swung the camera a third of
        // the way round the pair and stopped, and a long one crawled; a
        // reader watching it: *"Camera switched side quickly at some point,
        // making it difficult to understand what happens"*
        // (`todo/fight-mode.md` 15.8c).
        const FightContext& thrown = (a_.state == 11) ? a_ : b_;
        const float remain =
            (thrown.body && thrown.body->channel)
                ? thrown.body->channel->clipLength() : 30.0f;
        // 0x5A = 90: the arc is 180..269 degrees, a deliberate half-turn
        // about the pair, and the heading takes the WHOLE of it at once
        // (`flt_530C20 = v10 + flt_530C20`) before swinging on by
        // `arc / length` a frame.
        const float arc = static_cast<float>(rand_() % 90) + 180.0f;
        cam_.swing = arc / (remain > 0.0f ? remain : 1.0f);
        cam_.placed = true;
        cam_.heading += arc;
    }
    float mid[3]; camMidpoint(mid);
    const float radius = cam_.radius + 39.370079f;
    cam_.heading += cam_.swing;
    const float rad = cam_.heading * kDegToRad;
    cam_.eye[0] = mid[0] - std::cos(rad) * radius;
    cam_.eye[2] = mid[2] - std::sin(rad) * radius;
    cam_.at[0]  = mid[0];
    cam_.at[2]  = mid[2];
    cam_.at[1]  = mid[1] + 9.8425198f;
    cam_.eye[1] = height + cam_.at[1];
}

// `sub_445D30`: a steady orbit, the heading advancing by `degPerFrame`.
void Fight::camSteady(float degPerFrame, float eyeUp, float atUp) {
    if (!a_.body || !b_.body) return;
    measureSeparation();
    const float radius = (separation_ + 157.48032f) * 0.65161264f;
    float mid[3]; camMidpoint(mid);
    cam_.heading += degPerFrame;
    const float rad = cam_.heading * kDegToRad;
    cam_.eye[0] = mid[0] - std::cos(rad) * radius;
    cam_.eye[2] = mid[2] - std::sin(rad) * radius;
    cam_.eye[1] = b_.body->y + eyeUp;
    cam_.at[0]  = mid[0];
    cam_.at[2]  = mid[2];
    cam_.at[1]  = (b_.body->y + a_.body->y) * 0.5f + atUp;
    cam_.radius = radius;
}

// `sub_445E30`: the ten-frame hand-over. Where the camera IS, against where
// `camOrbit` would put it, walked linearly and reporting false when it ends.
bool Fight::camTransition(float dt) {
    const float wasEye[3] = {cam_.eye[0], cam_.eye[1], cam_.eye[2]};
    const float wasAt[3]  = {cam_.at[0],  cam_.at[1],  cam_.at[2]};
    camOrbit(true, -70.0f, -39.370079f, 0.0f);
    if (!cam_.placed) {
        for (int k = 0; k < 3; ++k) {
            cam_.fromEye[k] = wasEye[k];
            cam_.fromAt[k]  = wasAt[k];
            cam_.stepEye[k] = (cam_.eye[k] - wasEye[k]) / cam_.travel;
            cam_.stepAt[k]  = (cam_.at[k]  - wasAt[k])  / cam_.travel;
        }
        cam_.clock = 0.0f;
        cam_.placed = true;
    }
    for (int k = 0; k < 3; ++k) {
        cam_.eye[k] = cam_.fromEye[k] + cam_.stepEye[k] * cam_.clock;
        cam_.at[k]  = cam_.fromAt[k]  + cam_.stepAt[k]  * cam_.clock;
    }
    cam_.clock += dt;
    return cam_.clock <= cam_.travel;
}

// `sub_445C20`: placed ONCE - the latch is what makes the KO camera hold
// still while the replay plays.
void Fight::camPlace(float radius, float headingDeg, float height,
                     float atLift, bool moveTarget) {
    if (cam_.placed) return;
    float mid[3]; camMidpoint(mid);
    const float rad = headingDeg * kDegToRad;
    cam_.eye[0] = mid[0] - std::cos(rad) * radius;
    cam_.eye[2] = mid[2] - std::sin(rad) * radius;
    cam_.eye[1] = mid[1] + height;
    if (moveTarget) {
        cam_.at[0] = mid[0];
        cam_.at[2] = mid[2];
        cam_.at[1] = mid[1] + atLift;
    }
    cam_.heading = headingDeg;
    cam_.radius = radius;
    cam_.placed = true;
}

// `sub_4463C0`: the HIT SHAKE. A decaying sine on the eye's and the target's
// height - 7.8740158 (0.2 m) of amplitude, 80 degrees of phase a frame, the
// amplitude dropping 0.39370081 each time - armed by a fighter's `+80` bit 0
// and refused once the shake has run 20 frames.
void Fight::camShake(float dt, FightContext& c) {
    if (c.frameBefore < c.stateF || cam_.shake > 20.0f) return;
    // ...AND THIS IS WHERE THE KNOCKDOWN LATCH IS CLEARED. Past the guard the
    // engine does two things before any shake maths:
    //
    //     Game_RaiseEvent(45, ...);    /* the life back onto the record */
    //     u32(a1, 128) = 0;            /* the knockdown latch           */
    //
    // The port had the shake and neither of those, so `knockdown` - set by any
    // reaction whose `+12` carries `0x10000000` - was a latch that never
    // cleared. `Fight_ResolveHit`'s three arms for a killing blow are
    // `crouched` / `reaction` / `koEntry`, chosen on `u32(def, 128)`, so once
    // ANY knock-down landed, every later killing blow took the `reaction` arm.
    // That is right when the final reaction is itself a knock-down
    // (`KOH_FRONT`, `KOL_LEFT` - they lead to the ground and role state 6/7)
    // and wrong when it is an ordinary flinch: a reader's fight ended with
    // `want entry 132 -> clipOwner 133 'IH_RIGH'`, state 0, which played out
    // over 85 frames and fell back to `HGUARD`. The loser never reached state
    // 6 or 7, the KO never fired, and the fight ran on for 46 seconds with the
    // opponent on 0 hit points (`todo/fight-mode.md` 15.1). Cleared here, that
    // blow takes the `koEntry` arm instead - role 5, `I_DEATH`,
    // `I_DEATHLOOP`, state 7 - and the fight ends.
    //
    // Event 45 is still the caller's: the life goes onto the record in
    // `play.cpp`'s teardown, which owns the DB span.
    c.knockdown = 0;
    if (cam_.shake == 0.0f) {
        cam_.shakeFrom[0] = cam_.eye[1];
        cam_.shakeFrom[1] = cam_.at[1];
        cam_.shakePhase = 0.0f;
        cam_.shake = 7.8740158f;
    }
    cam_.shake -= 0.39370081f;
    if (cam_.shake < 0.0f) return;
    cam_.shakePhase += dt * 80.0f;
    const float d = std::sin(cam_.shakePhase * kDegToRad) * 7.8740158f;
    cam_.eye[1] = cam_.shakeFrom[0] - d;
    cam_.at[1]  = cam_.shakeFrom[1] - d;
}

void Fight::tickCamera(float dt) {
    if (!a_.body || !b_.body) return;
    // The head's guard: a fighter carrying `0x200` suspends the camera unless
    // both do.
    const bool pa = (a_.flags & 0x200u) != 0, pb = (b_.flags & 0x200u) != 0;
    if (pa != pb) return;

    cam_.divider = 1;
    int state = cam_.state;
    const int was = state;
    if (state != 4) state = 1;
    if (a_.state == 11 || b_.state == 11) state = 2;      // the throw
    if (was == 2 && state == 1) {                          // 2 -> 1 hands over
        state = 4;
        cam_.travel = 10.0f;
        cam_.placed = false;
    }
    if (koCounter_) state = 7;
    if (state != cam_.state) cam_.placed = (state == 4) ? cam_.placed : false;
    cam_.state = state;

    switch (state) {
    case 1: camOrbit(true, 0.0f, -9.8425198f, bits(-1050904334)); break;
    case 2: camThrow(cam_.height); break;
    case 3: cam_.height = 19.68504f; camSteady(3.5f, 19.68504f, 0.0f); break;
    case 4: if (!camTransition(dt)) { cam_.state = 1; cam_.placed = false; } break;
    case 7: {
        // The KO. The framing changes ONCE PER REPLAY PASS, not per tick:
        // `dword_530C24` remembers which pass the camera was placed for, the
        // two nudges are guarded on it changing, and `byte_530C80 = 0`
        // re-arms `sub_445C20`'s latch for that single frame. Applying them
        // every tick ran the heading to -534, then -1884, then +2796 degrees.
        const int pass = koCounter_;
        if (camPass_ != pass) camOrbit(false, 0.0f, cam_.height, 0.0f);
        if (pass == 1) {
            if (camPass_ == 1) cam_.heading += 3.0f * dt;   // the slow drift
            else { cam_.heading -= 45.0f; cam_.radius = 118.11024f;
                   cam_.height = 19.68504f; }
        }
        if (pass == 2 && camPass_ != 2) {
            cam_.radius = 157.48032f;
            cam_.height = -59.055119f;
            cam_.heading += 135.0f;
        }
        // `dword_530C24 = dword_9070AC != v4 ? v4 : 0` - the last pass is
        // remembered unless it is the final one, which re-arms the placement.
        camPass_ = (replayPasses_ != pass) ? pass : 0;
        cam_.placed = false;
        camPlace(cam_.radius, cam_.heading, cam_.height, 0.0f, true);
        break;
    }
    case 8: cam_.divider = 3; camOrbit(true, 0.0f, 0.0f, 0.0f); break;
    default: camOrbit(false, 0.0f, 0.0f, 0.0f); break;
    }

    // ---- the shared tail --------------------------------------------------
    //
    // THE COLLISION SOLVE. `sub_413450` writes the wanted eye into the local
    // camera's +52, `sub_413480` the look-at into +64, and `sub_416570` casts
    // `sub_444810` - the bolts' world ray - from the look-at TO the eye. On a
    // hit it writes the hit point into +52 and returns 1, and the tail takes
    // `f32(+52)` and `f32(+60)` from it: the eye's X and Z. **The height is
    // not taken** - `dword_9070A4` is left as the placement wrote it.
    //
    // `sub_416570` also refuses a hit when the camera's +356 carries 0x1000
    // and the mesh 0x20000000. The camera here is a LOCAL struct `sub_413450`
    // fills field by field, and nothing in this tail writes its +356; that
    // guard is not modelled (`PlayerController::cameraCollide` has the same
    // note for the follow camera, where 0x1000 is provably never set).
    cam_.rayHit = false;
    if (cameraRay_) {
        float hit[3];
        if (cameraRay_(cam_.at, cam_.eye, hit)) {
            cam_.eye[0] = hit[0];
            cam_.eye[2] = hit[2];
            cam_.rayHit = true;
            ++cam_.rayHits;
        }
    }
    float mid[3]; camMidpoint(mid);
    {
        // The ease, 0.25 m a frame toward the look-at in the plane, measured
        // against the distance to the fighters' midpoint - from the eye AS IT
        // NOW STANDS, after the solve.
        const float dx = mid[0] - cam_.eye[0], dy = mid[1] - cam_.eye[1],
                    dz = mid[2] - cam_.eye[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > 0.0f) {
            cam_.eye[0] += (cam_.at[0] - cam_.eye[0]) / d * 9.8425198f;
            cam_.eye[2] += (cam_.at[2] - cam_.eye[2]) / d * 9.8425198f;
        }
    }
    if (cam_.rayHit) {
        // The height clamp, and the engine runs it ONLY when the solve
        // returned a point (`if (v8)`). Until 2026-09-17 this tree had no
        // solve and applied it every frame, labelled as such. The distance is
        // from the eye as it now stands to the midpoint, the drop applies only
        // inside 3 m, and the limit is 2.5 m under "Vue de côté" against
        // 1.5 m under "Vue de dos". (Computing `d` against a stale midpoint,
        // as the first version did, dropped the eye 127 units BELOW the
        // fighters - with Y down, the clamp pushing the wrong way.)
        const float dx = mid[0] - cam_.eye[0], dy = mid[1] - cam_.eye[1],
                    dz = mid[2] - cam_.eye[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        float h = cam_.eye[1];
        if (d < 118.11024f) {
            const float inner = 13950.028f - d * d;      // 3 m squared
            h = cam_.eye[1] - std::sqrt(inner > 0.0f ? inner : 0.0f);
        }
        const float limit = combatCamera_ ? 98.425201f : 59.055119f;
        if (cam_.eye[1] - h > limit) h = cam_.eye[1] - limit;
        cam_.eye[1] = h;
    }
    // ...and the hit shake, which the engine runs only outside a KO.
    if (!koCounter_) {
        if (a_.stateD & 1) camShake(dt, a_);
        else if (b_.stateD & 1) camShake(dt, b_);
        else cam_.shake = 0.0f;
    }
}

bool Fight::begin(FightBody& player, const FightStats& ps,
                  FightBody& opponent, const FightStats& os,
                  int level, int difficulty, int combatCamera) {
    a_ = FightContext{};
    b_ = FightContext{};
    a_.body = &player;
    b_.body = &opponent;
    events_.clear();
    stats_ = FightStatsCounters{};
    ring_.clear();
    replayCursor_ = 0;
    koCounter_ = 0;
    replayPasses_ = 2;            // dword_9070AC
    over_ = false; playerWon_ = false; decided_ = false;
    combatCamera_ = combatCamera;            // byte_906F20, options row 18
    cam_ = FightCamera{};
    camPrevState_ = 1;

    // The stats, through the six `Game_RaiseEvent(44, …)` calls.
    a_.hp = ps.vie;               // +124, and the gauge's maximum
    b_.hp = os.vie;
    a_.attackMul = static_cast<float>(ps.attack) * 0.0049999999f;      // +132
    b_.attackMul = static_cast<float>(os.attack) * 0.0049999999f;
    a_.dodgeMul  = static_cast<float>(ps.dodge)  * 0.0049999999f * 0.25f;  // +136
    b_.dodgeMul  = static_cast<float>(os.dodge)  * 0.0049999999f * 0.25f;
    // Options row 16 `Difficulté des combats` (`word_90E1A6`), and it touches
    // the PLAYER's defence only: a flat bonus, nothing else in the fight.
    if (difficulty == 0)      a_.dodgeMul += 0.5f;
    else if (difficulty == 1) a_.dodgeMul += 0.25f;

    // flt_906F2C: the larger of the two models' bounding radii (node +88).
    radius_ = std::max(player.radius, opponent.radius);

    // The six role entries, cached per fighter by `Cef_FindEntryByCodeGlobal`.
    for (FightContext* c : {&a_, &b_}) {
        c->hitHigh  = entryByRole(*c, 3);
        c->hitLow   = entryByRole(*c, 4);
        c->koEntry  = entryByRole(*c, 5);
        c->crouched = entryByRole(*c, 9);
        c->farEntry = entryByRole(*c, 18);
        c->nearEntry = entryByRole(*c, 20);
        if (c->body && c->body->channel) {
            c->entry = c->body->channel->state();
            c->frameAfter = c->body->channel->frame();
            c->frameBefore = c->frameAfter;
        }
    }

    // THE CHANNEL WRITES `Fight_Begin` MAKES, which the first version of this
    // port skipped - and the harness showed it: the AI-driven fighter reached
    // one entry and stayed there while the AI pressed 674 moves into a queue
    // nothing consumed.
    //
    // `sub_45A870(chan, 1)` clears the channel's bit 0 and reseeds the queue
    // with the idle word. That is all: **neither fighter's input is blocked**.
    //
    // A first version of this also set flag 0x80 on the opponent, reasoning
    // that an AI-driven channel is queue-driven. That was an invention and the
    // binary refutes it twice over: `Perso_SetInputEnabled` (0x0045A3E0) has
    // exactly three call sites, all in the dialogue enter/leave family and
    // none in the fight code; and `Cef_TickChannel` opens by RESETTING THE
    // QUEUE to a lone idle word whenever `flags & 0x81`, so blocking the
    // opponent would wipe the move `Fight_TickAI` had just injected - the AI
    // runs before the channel tick in `Actor_TickPlayerAndOpponent`.
    if (player.channel) player.channel->resetInputQueue();
    if (opponent.channel) opponent.channel->resetInputQueue();
    // **THE PRIORITY GATE IS NOT MODELLED, deliberately.** `Fight_Begin` also
    // calls `sub_45A4C0(playerChan, 1)` and `sub_45A4C0(opponentChan, 0)`,
    // which set and clear channel flag `0x400` - and that flag makes
    // `Cef_FindTransition` honour the threshold at `+212` instead of taking
    // the first match. `sub_45A4C0` writes ONLY the flag; nothing in what has
    // been read writes `+212`, and this port's `setPriorityGate` cannot set
    // the flag without also naming a threshold. Passing 0 would silently skip
    // every priority-1 and -2 candidate, which is a behaviour change invented
    // out of an unread field rather than transcribed - so the flag is left
    // off until `+212`'s writer is found. `run_actor_states` already exercises
    // both paths of the gate, and the corpus cannot tell them apart on the
    // shipped data (`engine: actor states`), which is why this can wait.

    // Both fighters turned to face each other, with flag 0x2 set across the
    // two calls so `Fight_FaceOpponent`'s state guard cannot refuse them.
    a_.flags |= 2u; b_.flags |= 2u;
    faceOpponent(a_, b_, false);
    faceOpponent(b_, a_, false);
    a_.flags &= ~2u; b_.flags &= ~2u;
    measureSeparation();

    // `Fight_SelectAiProfile`: the OPPONENT gets a profile, by level + 1. The
    // player never does - op 62's third field is the only thing that selects
    // one (`todo/fight-mode.md` §2).
    b_.ai = nullptr;
    if (opponent.channel) {
        for (const auto& p : opponent.channel->ctl().ai)
            if (p.id == static_cast<std::uint32_t>(level + 1)) { b_.ai = &p; break; }
    }
    b_.aiIntent = 10;                       // v3[23] = 10
    b_.aiMoveStart = now_();                // v3[25]
    const int span = b_.ai ? static_cast<int>(b_.ai->enterDelay[1]) -
                             static_cast<int>(b_.ai->enterDelay[0]) : 0;
    const int jitter = span > 0 ? rand_() % span : 0;
    b_.aiMoveDeadline = b_.aiMoveStart + jitter +
                        (b_.ai ? b_.ai->enterDelay[0] : 0);

    // AND THE CAMERA IS SEEDED HERE, which the first version of this missed:
    // `Fight_Begin`'s own tail calls `sub_446000(0.0, 0, 0.0, 0)` - the
    // UNEASED variant, which places the eye and the target outright. Without
    // it the first eased frame blends from a zero camera and the eye starts
    // two thousand units from the fighters, walking in over the next second.
    camOrbit(false, 0.0f, 0.0f, 0.0f);
    return b_.ai != nullptr;
}

// ---- sub_49A4F0: the separation, into flt_906F44 -------------------------
void Fight::measureSeparation() {
    if (!a_.body || !b_.body) return;
    const float dx = b_.body->x - a_.body->x;
    const float dy = b_.body->y - a_.body->y;
    const float dz = b_.body->z - a_.body->z;
    separation_ = std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- Fight_FaceOpponent 0x0049A550 ---------------------------------------
void Fight::faceOpponent(FightContext& c, const FightContext& other, bool ease) {
    if (!c.body || !other.body) return;
    // The guard: flag 0x2 forces it; otherwise a fighter who is reacting,
    // grounded, thrown or mid-special is left alone.
    const bool forced = (c.flags & 2u) != 0;
    if (!forced) {
        const int s = c.state;
        if (s == 1 || s == 2 || s == 21 || s == 6 || s == 7 || s == 9 ||
            s == 11 || s == 12) return;
        if (other.state == 9) return;
        if (c.flags & 0x400u) return;
        if (c.flags & 0x20u) return;
        if (static_cast<std::uint32_t>(c.stateD) & 0x20u) return;
    }
    float yaw = std::atan2(c.body->x - other.body->x,
                           other.body->z - c.body->z) * kDegPerRad + 180.0f;
    if (ease) yaw = c.body->yaw * 0.25f + yaw * 0.75f;
    c.body->yaw = yaw;
}

// ---- Fight_KeepSeparation 0x0049A610 -------------------------------------
void Fight::keepSeparation(FightContext& c, FightContext& other) {
    if (!c.body || !other.body) return;
    if (c.state == 11 || c.state == 12) return;
    if (c.flags & 0x20u) return;
    measureSeparation();
    if (separation_ >= radius_ || separation_ <= 0.0f) return;

    // The push is along a MINUS b, scaled by how much of the radius is
    // missing, and the engine moves the OTHER fighter through `Actor_Move`
    // and this one by the residue it reports. With no walker here both halves
    // land on the bodies; see the header's note.
    const float k = (radius_ - separation_) / separation_;
    const float dx = (a_.body->x - b_.body->x) * k;
    const float dy = (a_.body->y - b_.body->y) * k;
    const float dz = (a_.body->z - b_.body->z) * k;
    if (&c == &a_) moveBody(*a_.body,  dx,  dy,  dz);
    else           moveBody(*b_.body, -dx, -dy, -dz);
    ++stats_.separations;
    measureSeparation();
    if (separation_ + 0.001f < radius_ && &c == &b_) ++stats_.tooCloseAfterPush;
    events_.push_back({FightEvent::Kind::Separate, c.body->isPlayer, 0, -1, -1, -1, 0});
}

// ---- sub_4451F0: the step BEFORE the channel -----------------------------
void Fight::preStep(FightContext& c) {
    // The engine copies the actor's clip frames (+192 the previous, +188 the
    // current) into the context, so the hit window is tested against the pair
    // the LAST tick left. The channel keeps its own `prev`, which is private,
    // so the pair is carried here and shifted after each tick - the same two
    // numbers, one frame apart, which is what the window test consumes.
    if (!c.body || !c.body->channel) return;
    // Life back onto the record, unless the knockdown latch is up: this is
    // `Game_RaiseEvent(45, …)`, and it is what makes the scripts' 'Vie Combat
    // Après' read the damage a fight did. The caller owns the record, so the
    // value is simply left on the context for it to write.
    (void)c.knockdown;
}

// ---- sub_4452A0: the step AFTER the channel ------------------------------
void Fight::postStep(FightContext& c) {
    if (!c.body || !c.body->channel) return;
    auto& ch = *c.body->channel;

    const int cur = ch.state();
    if (cur != c.scoredEntry) c.scoredEntry = -1;   // a new move may score
    if (cur != c.entry) {
        c.prevEntry = c.entry;
        c.entry = cur;
    }

    static const bool koTrace = [] {
        const char* e = std::getenv("OMK_KOTRACE"); return e && *e == '1';
    }();
    if (koTrace && koTraceLeft_ > 0 && koTraceWho_ == &c) {
        const auto& S = c.body->channel->ctl().states;
        std::printf("  koTrace %2d: entry %d '%s' clipOwner %d "
                             "frame %.1f state %d\n",
                     90 - koTraceLeft_, cur,
                     cur >= 0 && cur < (int)S.size()
                         ? S[(std::size_t)cur].name.c_str() : "?",
                     ch.clipOwner(), (double)ch.frame(),
                     cur >= 0 && cur < (int)S.size()
                         ? (int)(S[(std::size_t)cur].flags12 & 0xFFu) : -1);
        --koTraceLeft_;
    }

    const CtlState* st = stateAt(c, cur);
    if (!st) return;

    // The state word at entry +12: the low byte is the fighter's state id,
    // then BYTE1, BYTE2 as a float, and HIBYTE.
    c.prevState = c.state;
    const std::uint32_t w = st->flags12;
    c.state  = static_cast<int>(w & 0xFFu);
    c.stateB = static_cast<int>((w >> 8) & 0xFFu);
    c.stateF = static_cast<float>((w >> 16) & 0xFFu);
    c.stateD = static_cast<int>((w >> 24) & 0xFFu);

    // The KO trigger: once a loser is recorded, his own entry into role state
    // 6 or 7 starts the replay (the engine also fades the screen here).
    if (decided_ && (&c == loser_) && (c.state == 6 || c.state == 7) &&
        koCounter_ == 0) {
        ++koCounter_;
        ++stats_.kos;
        events_.push_back({FightEvent::Kind::Ko, c.body->isPlayer, 0, -1, -1, -1, 0});
    }

    // The two distance-gated bank switches, `sub_49A4F0` then `SetPersoBank`.
    if (c.state == 13) {
        measureSeparation();
        if (separation_ > kFarSwitch) forceEntry(c, c.farEntry);
    }
    if (c.state == 19) {
        measureSeparation();
        if (separation_ <= kNearSwitch) forceEntry(c, c.nearEntry);
    }

    // `sub_49A830`: which cached reactions the current move allows to
    // interrupt it. Derived and recorded, NOT fed to the channel - see the
    // header.
    c.allowedMask = (st->flags & 0x2000000u) && st->hasCombat
                        ? static_cast<std::uint32_t>(blockFlags(st->combat))
                        : 0u;
}

// ---- Fight_ResolveHit 0x0049A960 -----------------------------------------
void Fight::applyDamage(FightContext& def, const FightContext& att,
                        double base, bool scaled, FightEvent::Kind kind,
                        int reaction) {
    double dmg = base;
    if (def.body && def.body->isPlayer) dmg += dmg;      // the player's double
    if (scaled) {
        dmg *= static_cast<double>(att.attackMul) + 1.0;
        dmg -= static_cast<double>(def.dodgeMul) * dmg;
        if (dmg <= 0.0) dmg = 1.0;
    }
    const int before = def.hp;
    def.hp -= static_cast<int>(dmg);
    def.lastDamage = static_cast<int>(dmg);
    if (def.hp > before) ++stats_.hpWentUp;
    stats_.damageDealt += static_cast<long>(dmg);
    events_.push_back({kind, def.body && def.body->isPlayer,
                       static_cast<int>(dmg), reaction, att.entry, -1, 0});
    if (def.hp <= 0) {
        def.hp = 0;
        decided_ = true;
        loser_ = &def;
        playerWon_ = !(def.body && def.body->isPlayer);
        // Both channels are frozen and their queues reset - the engine's
        // `sub_45A870(chan, 0)` and `sub_45A8D0(chan, 0)`.
        if (a_.body && a_.body->channel) a_.body->channel->resetInputQueue();
        if (b_.body && b_.body->channel) b_.body->channel->resetInputQueue();
        // Which entry the corpse falls into: the crouched one when he was
        // already down, the reaction when it knocked him down, else the KO.
        // THE LOSER'S FORCED ENTRY, always printed. Read from the CHANNEL
        // after the call, not from the argument, so it reports the OUTCOME
        // and not the intention - and `forceEntry` has one silent failure
        // (`entry < 0`) that touches no counter at all, so `FAILED` here is
        // the only thing that can ever show it. A fight that will not end is
        // diagnosed from this line plus `OMK_KOTRACE=1`
        // (`todo/fight-mode.md` 15.1).
        const int want = (def.state == 6 || def.state == 21) ? def.crouched
                       : def.knockdown                       ? reaction
                                                             : def.koEntry;
        // The arm and the reaction must agree - see `knockdownArmWithoutBit`.
        if (def.knockdown && !(def.state == 6 || def.state == 21)) {
            const CtlState* rs = stateAt(def, reaction);
            if (!rs || !(rs->flags12 & 0x10000000u))
                ++stats_.knockdownArmWithoutBit;
        }
        const int fromE = def.body && def.body->channel
                              ? def.body->channel->state() : -1;
        const bool okF = forceEntry(def, want);
        if (def.body && def.body->channel) {
            auto& dch = *def.body->channel;
            const int land = dch.state(), own = dch.clipOwner();
            const auto& S = dch.ctl().states;
            auto w12 = [&](int i) { return i >= 0 && i < (int)S.size()
                                           ? S[(std::size_t)i].flags12 : 0u; };
            auto fl = [&](int i) { return i >= 0 && i < (int)S.size()
                                          ? S[(std::size_t)i].flags : 0u; };
            std::printf(
                "fight LOSER: from entry %d '%s' (goto %d), branch %s, want "
                "entry %d (flags 0x%08x, w12 0x%x) -> %s; landed entry %d "
                "(flags 0x%08x, w12 0x%x) clipOwner %d (w12 0x%x) state %d\n",
                fromE, fromE >= 0 && fromE < (int)S.size()
                           ? S[(std::size_t)fromE].name.c_str() : "?",
                fromE >= 0 && fromE < (int)S.size()
                           ? S[(std::size_t)fromE].gotoIdx : -1,
                (def.state == 6 || def.state == 21) ? "crouched"
                    : def.knockdown ? "knockdown" : "koEntry",
                want, fl(want), w12(want), okF ? "ok" : "FAILED",
                land, fl(land), w12(land), own, w12(own),
                (int)(w12(land) & 0xFFu));
            koTraceLeft_ = 90; koTraceWho_ = &def;
        }
    } else if (reaction >= 0) {
        forceEntry(def, reaction);
    }
}

void Fight::resolveHit(FightContext& att, FightContext& def) {
    const CtlState* atkE = stateAt(att, att.entry);
    const CtlState* defE = stateAt(def, def.entry);
    if (!atkE || !defE) return;

    if (def.flags & 0x80u) return;                       // untouchable
    if (att.stateD != 8 && (def.flags & 0x200u)) return;
    if (decided_) return;                                // already settled
    if (att.entry == att.scoredEntry) return;            // scored once
    if (att.state == 11 || att.state == 12) return;      // mid-reaction
    if (!(atkE->flags & 0x2000000u) || !atkE->hasCombat) return;

    const CtlCombat& blk = atkE->combat;
    if (blk.reactionA() == 0 && blk.reactionB() == 0) return;

    const int atkHigh = atkE->playBits & 1, atkLow = atkE->playBits & 2;
    const int defHigh = defE->playBits & 1, defLow = defE->playBits & 2;
    const int lineA = blockFlags(blk) & 4, lineB = blockFlags(blk) & 2;

    if (lineA && def.state == 3) return;     // the high guard blanks line A
    if (lineB && def.state == 4) return;     // and the low guard line B
    if (!windowOpen(blk, att.frameBefore, att.frameAfter)) return;

    att.scoredEntry = att.entry;             // this move has now scored

    const int s = def.state;
    const bool inState1 = s == 1, inState2 = s == 2, inState8 = s == 8;
    const bool inState13 = s == 13;
    const bool inState6 = (s == 6 || s == 21);

    // The THROW: attacker state 10 catching a target who is standing, walking,
    // crouched or already down. The grab offset is authored in the ATTACKER's
    // OWN reaction entry - its block's knockback floats - expressed in the
    // defender's frame, and it is the ATTACKER who is teleported onto it.
    if (att.state == 10) {
        if (!(inState8 || inState1 || inState13 || inState6)) return;
        const int reactIdx = entryByLow16(att, blk.reactionA());
        const CtlState* reactE = stateAt(att, reactIdx);
        if (!reactE || !reactE->hasCombat) { ++stats_.reactionUnresolved; return; }
        const int followIdx = entryByLow16(def, reactE->combat.reactionA());

        // The engine rotates the knockback by the DEFENDER's full Euler
        // (`Matrix3x3_FromEulerAngles` on actor +416/+420/+424). A `FightBody`
        // carries only the yaw, which is the component a standing fighter
        // ever has; the pitch and roll are LABELLED as not carried here and
        // come with the actor record in step 2.
        const float rad = def.body->yaw / kDegPerRad;
        const float kx = reactE->combat.raw[7], ky = reactE->combat.raw[8],
                    kz = reactE->combat.raw[9];
        const float rx = kx * std::cos(rad) + kz * std::sin(rad);
        const float rz = -kx * std::sin(rad) + kz * std::cos(rad);
        att.body->x = def.body->x + rx;
        att.body->z = def.body->z + rz;
        att.body->y += def.body->y + ky - att.body->y;

        def.flags |= 2u;
        faceOpponent(att, def, false);
        def.flags &= ~2u;

        forceEntry(att, reactIdx);
        forceEntry(def, followIdx);
        att.state = 11;
        def.state = 12;
        ++stats_.throws;
        events_.push_back({FightEvent::Kind::Throw, def.body->isPlayer, 0,
                           followIdx, att.entry, -1, 0});
        applyDamage(def, att, static_cast<double>(reactE->combat.damage()), true,
                    FightEvent::Kind::Hit, followIdx);
        return;
    }

    // The BLOCK: the defender's guard eats it for a flat point of chip damage.
    if (def.flags & 0x40u) {
        forceEntry(def, lineB ? def.hitLow : def.hitHigh);
        ++stats_.blocks;
        applyDamage(def, att, 1.0, false, FightEvent::Kind::Block, -1);
        return;
    }

    // Does the attack line reach the defender's stance?
    bool lands = true;
    if (lineA) {
        if (lineB) { if (inState8) lands = false; }
        else {
            if (defLow || inState8 || inState1) lands = false;
            if (inState6) lands = false;
        }
    }
    if (lineB && ((defLow && inState8) || inState2)) lands = false;
    if (lands) {
        if (defHigh && blk.reactionA() == -1) lands = false;
        if (defLow  && blk.reactionB() == -1) lands = false;
    }

    if ((def.flags & 1u) || lands) {
        int reaction = -1;
        if (!inState6) {
            if (defHigh) reaction = entryByLow16(def, blk.reactionA());
            if (defLow)  reaction = entryByLow16(def, blk.reactionB());
        } else {
            reaction = def.crouched;
        }
        if (reaction < 0) ++stats_.reactionUnresolved;
        const CtlState* rs = stateAt(def, reaction);
        if (rs && (rs->flags12 & 0x10000000u)) {
            def.knockdown = 1;
            ++stats_.knockdowns;
        }
        ++stats_.hits;
        applyDamage(def, att, static_cast<double>(blk.damage()), true,
                    FightEvent::Kind::Hit, reaction);
    } else {
        // A miss by stance still flinches the guard and costs one point.
        if (atkHigh && (inState1 || (defHigh && inState8)))
            forceEntry(def, def.hitHigh);
        else if (atkLow && (inState2 || (defLow && inState8)))
            forceEntry(def, def.hitLow);
        ++stats_.grazes;
        applyDamage(def, att, 1.0, false, FightEvent::Kind::Graze, -1);
    }

    if (static_cast<std::uint32_t>(att.stateD) & 0x40u) {
        def.carriedTimer = att.stateF;
        def.carriedFlag = 0;
    }
}

// ---- Fight_TickAI 0x00464830 and its two helpers -------------------------
// The eight tables at 0x004CAD0C..0x004CADA0, read out of the executable and
// listed in fight.h. `tables/fight_ai_moves.json` carries the same values and
// `verify.py: exe tables` re-derives them from the image, so this copy cannot
// drift without the suite noticing.
FightAiTables FightAiTables::shipped() {
    FightAiTables t;
    t.m4CAD0C = {2, 2, 2, 2, 2};
    t.m4CAD30 = {kIdleInput};
    t.m4CAD38 = {8, 8, 8, 8, 8};
    t.m4CAD54 = {4, 4, 4, 4, 4};
    t.m4CAD6C = {0x48u, kIdleInput};
    t.m4CAD78 = {0x88u, kIdleInput};
    t.m4CAD88 = {0x40u, kIdleInput};
    t.m4CAD98 = {0x80u, kIdleInput};
    return t;
}

void Fight::injectWords(FightContext& c, const std::vector<std::uint32_t>& words) {
    if (!c.body || !c.body->channel || words.empty()) return;
    c.body->channel->injectInput(words, aiOr_);
    ++stats_.aiMoves;
    stats_.aiWords += static_cast<long>(words.size());
    for (const auto w : words)
        if ((w & ~kIdleInput) & ~static_cast<std::uint32_t>(kFightAiBits))
            ++stats_.aiWordsOutsideUnion;
    events_.push_back({FightEvent::Kind::AiMove, c.body->isPlayer, 0, -1, -1,
                       -1, static_cast<int>(words.size())});
}

void Fight::injectMove(FightContext& c, const CtlAiSlot& slot, int slotIndex) {
    if (!c.body || !c.body->channel || slot.moves.empty()) return;
    const auto& move = slot.moves[static_cast<std::size_t>(rand_()) % slot.moves.size()];
    c.body->channel->injectInput(move, aiOr_);
    ++stats_.aiMoves;
    stats_.aiWords += static_cast<long>(move.size());
    for (const auto w : move)
        if ((w & ~kIdleInput) & ~static_cast<std::uint32_t>(kFightAiBits))
            ++stats_.aiWordsOutsideUnion;
    events_.push_back({FightEvent::Kind::AiMove, c.body->isPlayer, 0, -1, -1,
                       slotIndex, static_cast<int>(move.size())});
}

// `sub_465160` (slots 4/5/6) and `sub_465210` (slots 1/2/3): roll once
// against the three cumulative weights and press from the slot that wins.
void Fight::pickFromFamily(FightContext& c, int base) {
    if (!c.ai) return;
    const auto& sl = c.ai->slots;
    const int roll = rand_() % 100;
    const std::uint32_t w0 = sl[static_cast<std::size_t>(base)].weight;
    const std::uint32_t w1 = sl[static_cast<std::size_t>(base) + 1].weight;
    const std::uint32_t w2 = sl[static_cast<std::size_t>(base) + 2].weight;
    int pick = base - 1;                       // the engine's 0 / 3 fallthrough
    if (static_cast<std::uint32_t>(roll) <= w0 + w1 + w2) pick = base + 2;
    if (static_cast<std::uint32_t>(roll) <= w0 + w1)      pick = base + 1;
    if (static_cast<std::uint32_t>(roll) <= w0)           pick = base;
    if (pick < 0 || pick >= 12) return;
    injectMove(c, sl[static_cast<std::size_t>(pick)], pick);
}

void Fight::tickAi(FightContext& c, FightContext& other) {
    if (decided_ || !c.ai || !c.body || !c.body->channel) return;
    const auto& sl = c.ai->slots;
    const long now = now_();

    // States 6 and 21 - down, or getting up - run the slot-7 pair on their own
    // clock, which is what fixes the slot stride in the format.
    if (c.state == 6 || c.state == 21) {
        if (!c.aiTauntStart) {
            c.aiTauntStart = now;
            const int span = static_cast<int>(c.ai->moveDelay[1]) -
                             static_cast<int>(c.ai->moveDelay[0]);
            c.aiTauntDeadline = now + (span > 0 ? rand_() % span : 0) +
                                c.ai->moveDelay[0];
            return;
        }
        if (now > c.aiTauntDeadline) {
            c.aiTauntStart = 0; c.aiTauntDeadline = 0;
            injectMove(c, sl[7], 7);
            c.aiIntent = 107;
        }
        return;
    }

    measureSeparation();
    // Too far: CLOSE THE DISTANCE, and this branch presses the engine's own
    // built-in tables rather than anything out of the profile - the idle word
    // (whose direction comes from the modifier), then the five 0x02 presses
    // with the modifier set to 2. `dword_53AE14` is that modifier.
    if (separation_ > kCloseIn && c.state != 19) {
        c.aiIntent = 104;
        injectWords(c, builtin_.m4CAD30);
        aiOr_ = 2;
        injectWords(c, builtin_.m4CAD0C);
        return;
    }

    // The opponent is DOWN and this fighter is on his feet: intent 108, the
    // modifier to 8, then one of two two-word combos, evenly.
    if ((other.state == 6) && (c.state == 1 || c.state == 2)) {
        c.aiIntent = 108;
        aiOr_ = 8;
        injectWords(c, builtin_.m4CAD30);
        injectWords(c, rand_() % 100 < 50 ? builtin_.m4CAD6C : builtin_.m4CAD78);
        return;
    }

    if (c.aiIntent == 108) {
        if (c.state == 2) {
            injectWords(c, builtin_.m4CAD30);
            c.aiIntent = 105;
            pickFromFamily(c, 4);          // sub_465160
        } else {
            injectWords(c, builtin_.m4CAD38);
        }
        return;
    }

    if (c.aiIntent == 104 ||
        ((c.aiIntent == 105 || c.aiIntent == 107) &&
         (c.state == 1 || c.state == 2 || c.state == 15))) {
        c.aiMoveStart = now;
        const int span = static_cast<int>(c.ai->enterDelay[1]) -
                         static_cast<int>(c.ai->enterDelay[0]);
        c.aiMoveDeadline = now + (span > 0 ? rand_() % span : 0) +
                           c.ai->enterDelay[0];
        injectWords(c, builtin_.m4CAD30);
        // Then roll the three INTENT weights, slots 8/9/10.
        const int roll = rand_() % 100;
        const std::uint32_t w8 = sl[8].weight, w9 = sl[9].weight, w10 = sl[10].weight;
        if (static_cast<std::uint32_t>(roll) <= w8 + w9 + w10) c.aiIntent = 10;
        if (static_cast<std::uint32_t>(roll) <= w8 + w9)       c.aiIntent = 9;
        if (static_cast<std::uint32_t>(roll) <= w8)            c.aiIntent = 8;
        return;
    }

    if (c.aiIntent != 8 && c.aiIntent != 9 && c.aiIntent != 10) return;
    if (now <= c.aiMoveDeadline) {
        // **NOT YET TRANSCRIBED, and labelled rather than approximated**: with
        // intent 9 and the pair inside 59.055119 (1.5 m), `Fight_TickAI` runs
        // a defensive block - it sets channel flag 0x100, reads the OPPONENT's
        // current combat block for its attack line, and either presses the
        // 0x08 table or raises flag 0x40 to guard. That is the AI's DEFENCE,
        // it is about sixty lines, and it wants reading in its own right;
        // until then this fighter simply waits out the delay, which is what
        // the engine does on every other intent. `todo/fight-mode.md` §7.
        return;
    }

    // The wait is over: attack, or move. Slot 0's weight is the gate, slot 11
    // the special.
    c.flags &= ~1u;
    c.aiIntent = 105;
    c.aiMoveStart = now;
    const int span = static_cast<int>(c.ai->enterDelay[1]) -
                     static_cast<int>(c.ai->enterDelay[0]);
    c.aiMoveDeadline = now + (span > 0 ? rand_() % span : 0) + c.ai->enterDelay[0];

    if (static_cast<std::uint32_t>(rand_() % 100) > sl[0].weight) {
        if (!aiOr_) c.aiIntent = 108;
        aiOr_ = 8;
    } else {
        aiOr_ = 0;
    }

    if (separation_ < kNearSwitch && !aiOr_ &&
        other.state != 6 && other.state != 21) {
        if (static_cast<std::uint32_t>(rand_() % 100) < sl[11].weight) {
            injectMove(c, sl[11], 11);
            return;
        }
    }
    if (aiOr_) pickFromFamily(c, 4);       // sub_465160
    else       pickFromFamily(c, 1);       // sub_465210
}

// ---- Fight_RecordFrame 0x0049B220 and sub_49B2E0 -------------------------
void Fight::recordFrame() {
    if (!a_.body || !b_.body) return;
    ReplayFrame f;
    const FightBody* bodies[2] = {a_.body, b_.body};
    const FightContext* ctx[2] = {&a_, &b_};
    for (int i = 0; i < 2; ++i) {
        f.x[i] = bodies[i]->x; f.y[i] = bodies[i]->y; f.z[i] = bodies[i]->z;
        f.yaw[i] = bodies[i]->yaw;
        f.entry[i] = ctx[i]->entry;
        f.frame[i] = ctx[i]->frameAfter;
    }
    if (ring_.size() >= 60) ring_.erase(ring_.begin());   // the qmemcpy slide
    ring_.push_back(f);
    ++stats_.replayFrames;
}

bool Fight::replayStep() {
    // The engine plays the ring back into both bodies, one frame a tick, and
    // when it runs out raises the KO counter; past `dword_9070AC` the fight
    // is over. So a knock-out is shown twice.
    if (replayCursor_ >= ring_.size()) {
        replayCursor_ = 0;
        ++koCounter_;
        ++stats_.replayPasses;
        if (koCounter_ > replayPasses_) return false;
        return true;
    }
    const ReplayFrame& f = ring_[replayCursor_++];
    FightBody* bodies[2] = {a_.body, b_.body};
    FightContext* ctx[2] = {&a_, &b_};
    for (int i = 0; i < 2; ++i) {
        if (!bodies[i]) continue;
        bodies[i]->x = f.x[i]; bodies[i]->y = f.y[i]; bodies[i]->z = f.z[i];
        bodies[i]->yaw = f.yaw[i];
        ctx[i]->entry = f.entry[i];
        ctx[i]->frameAfter = f.frame[i];
    }
    return true;
}

// ---- Actor_TickPlayerAndOpponent 0x00466710 + Actors_TickAll's tail ------
void Fight::tickFighter(FightContext& c, float dt, std::uint32_t input, bool isAi) {
    if (!c.body || !c.body->channel) return;
    preStep(c);
    if (isAi) tickAi(c, &c == &a_ ? b_ : a_);
    // The channel tick, or the caller's own pass in its place - see
    // `FightBody::externallyTicked`. Either way it sits between the two
    // per-fighter steps, which is where `Actor_TickPlayerAndOpponent` puts it.
    if (c.body->externallyTicked && bodyTick_) bodyTick_(dt, input);
    else                                      c.body->channel->tick(dt, input);
    // The frame pair the window test consumes: the previous tick's value and
    // this one's.
    c.frameBefore = c.frameAfter;
    c.frameAfter = c.body->channel->frame();
    postStep(c);
}

bool Fight::step(float dt, std::uint32_t playerInput) {
    if (over_) return false;
    ++stats_.frames;

    if (koCounter_) {
        // Frozen: the replay owns both bodies until the passes run out - and
        // the camera still ticks, which is what makes the KO camera swing
        // round the fallen fighter while the replay plays.
        const bool more = replayStep();
        tickCamera(dt);
        if (!more) { over_ = true; return false; }
        return true;
    }

    tickFighter(a_, dt, playerInput, false);
    // THE AI FIGHTER IS TICKED WITH THE IDLE WORD, not with `kQueueDrives`.
    //
    // `Cef_TickChannel` always polls (`sub_4A7A20`), and that function never
    // yields 0: nothing held is `0x40000000`. The injected queue is a separate
    // thing - the entry flags pop and reset it, and the `0x8001` clip-end path
    // is where a queued move opens a transition - so the search word for a
    // body with no device is the IDLE word.
    //
    // Ticking him with 0 instead skips the input pass outright (the port's own
    // `if (!(flags_ & 0x81u) && word)`), and the harness showed what that
    // costs: he walked in, reached the separation radius and then stood in
    // `HFWALK` for ever, because nothing could carry his walk's clip end back
    // to a guard - his intent stuck at 105 with a state no branch re-arms.
    tickFighter(b_, dt, kIdleInput, true);

    keepSeparation(a_, b_);
    keepSeparation(b_, a_);

    if (!koCounter_) {
        resolveHit(a_, b_);          // Fight_ResolveBoth, both ways
        resolveHit(b_, a_);
        faceOpponent(a_, b_, false);
        faceOpponent(b_, a_, false);
        recordFrame();
    }
    // The camera pass, which the engine runs from `Camera_Tick` with the two
    // contexts in hand - after the fighters have moved, so it frames where
    // they ARE rather than where they were.
    tickCamera(dt);
    return true;
}

}  // namespace omk

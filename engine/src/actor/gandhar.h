// SPDX-License-Identifier: GPL-3.0-or-later
// GANDHAR - shoot type 10, the lava cave's boss (AREA 2, character 187).
// `todo/gandhar.md` is the plan and the reading; this is the transcription of
// his arm of the shoot AI, which the port had only as a census
// (`ShootAi::tickGandhar` walks his behaviour scripts and moves nothing), so
// in the viewer he fought as the generic gunman.
//
//   `sub_47DFD0`'s type-10 arm   his ENTRY (`gandharEnter`)
//   `sub_47F6F0`                 his BRAIN, once a frame (`gandharTick`)
//   `0x004CFB98` / `0x004CFBC8`  the twelve actions' ENTER and TICK tables
//   `sub_47EBF0`                 the clip clock every tick runs
//   `sub_47FB40`                 the behaviour-script walk (`shootScriptAdvance`)
//
// The world - his sight and turn (`sub_420C70` / `sub_420EB0`), the step
// toward the player (`sub_47E5F0`), the fire (`sub_44CDF0`), the attack pick
// (`sub_421020`), the body touch (`sub_45BC50`), the box side, the strike
// (`sub_423B10`), the clips and the message post - comes in through
// `GandharWorld`, as Astaroth's does (`actor/astaroth.h`): the machine is
// here, the geometry is the frontend's.
#pragma once

#include "actor/shoot.h"

#include <functional>
#include <string>
#include <vector>

namespace omk {

inline constexpr int kGandharType = 10;
// `sub_47DFD0`: `o3de_MoveNodeBy(node, 0, -147 - node.y, 0)` and `+60` =
// 0xC3130000 - he is put at y -147 whatever his placement says
inline constexpr float kGandharEntryY = -147.0f;
// `sub_47E870` / `sub_47E960`: the RISE ends when `node.y - +64 <= -150`, the
// SINK when it is `>= 250` (Y points DOWN: rising is decreasing y)
inline constexpr float kGandharRiseTo = -150.0f;
inline constexpr float kGandharSinkTo = 250.0f;
// The 25/26 roll's percentage by health band (`+190`): `(rand() % 100 <= p) +
// 25` - 26 the strike when true, 25 the grab when false
inline constexpr int kGandharRollHealthy = 100, kGandharRollWounded = 70,
                     kGandharRollCritical = 40;

// His actor-side state: what the engine keeps on the ACTOR (the clip at
// `rec+8` and its clock at `actor+188/+192`) and the NODE (what is drawn),
// beside `ShootRecord`, which holds the record's own fields.
struct GandharActor {
    int   clip = -1;              // rec+8, as a clip SLOT of his group, -1 none
    int   clipFrames = 0;         // `Anim_Frames(rec+8)`
    float frame = 1.0f;           // actor+188
    float prev  = 0.0f;           // actor+192
    float pos[3] = {0, 0, 0};     // actor+244..252: the record's x / z
    float node[3] = {0, 0, 0};    // the node's world point, what is drawn
    float speed = 0.0f;           // record +68: `39 * property 3 / 30`
    bool  started = false;        // `gandharEnter` has run
    bool  grabbed = false;        // `dword_657A28`, raised by the grab
    bool  deathPosted = false;    // message 3 sent (the engine re-posts it
                                  // every frame until `shoot.end` removes him)
};

struct GandharWorld {
    // `List_PickRandomByType(+20, type)` / `sub_434630(+20, id)`: a clip SLOT
    // of his group, -1 when there is none
    std::function<int(int type)> pickType;
    std::function<int(int id)>   pickId;
    std::function<int(int slot)> clipFrames;
    // the clip's ROOT between two frames, in the WORLD (his facing applied):
    // what `Anim_SetFrame(node, clip, t0, t1, out)` hands back
    std::function<void(int slot, float t0, float t1, float out[3])> rootDelta;
    // `sub_434890(+20)`: his group's i16 at +16, kept at +164 by 23 and 24
    std::function<float()> listValue;
    // `sub_420C70(rec, him, target)`: the cone test - true inside - which also
    // leaves the flat squared distance `sub_421020` reads
    std::function<bool()> sight;
    std::function<void(float& facing)> turn;                // `sub_420EB0(him, 0)`
    // `sub_47E5F0(rec, him, out, speed)`: a step toward the player, gated by
    // the floor cells; WRITES his record x / z (`pos`), returns the step
    std::function<void(GandharActor& a, float speed, float out[2])> step;
    std::function<bool(int arm)> fire;                      // `sub_44CDF0(him, arm, target)`
    std::function<int()> pickAttack;                        // `sub_421020`, 0 none
    std::function<bool()> touch;                            // `sub_45BC50(him, the player)`
    std::function<int()> side;                              // the grab's message, 5..8
    std::function<int()> strikeDamage;                      // event 44: his property 22
    std::function<void(int damage, const float dir[3])> strike;   // `sub_423B10(player, ..)`
    std::function<int()> rnd;                               // the CRT's `rand()`
    // event 43: message 3 (his death) is sent from HIM, the grab's 5..8 from
    // the PLAYER (`sub_47F340`: `v17[1] = rec+96`, his target)
    std::function<void(int message, bool fromPlayer)> post;
    // a line for the log, said by the machine at the moment it decides
    std::function<void(const std::string&)> say;
};

// `sub_47DFD0`'s type-10 arm. `pos` is his record's x / z (actor+244/+252),
// his node is put at `kGandharEntryY`, `speed` is `39 * property 3 / 30`
// (integer arithmetic, as the engine does it). Enters action 23.
void gandharEnter(GandharActor& a, ShootRecord& rec, const float pos[3], int property3,
                  const GandharWorld& w);

// `sub_47EBF0(him, clip, out, a4)`: the clip clock. 1 while the clip runs, 2
// from half its frames, 0 when it ends - where the clock wraps to `1 + dt`
// and, with `a4`, nothing moves. Otherwise the root delta moves the node.
int gandharClock(GandharActor& a, float dt, bool stopAtEnd, const GandharWorld& w);

// The ENTER of action `code` (16..27, `0x004CFB98`), and `+160 &= ~0x2080`
// after it as every caller does.
void gandharEnterAction(GandharActor& a, ShootRecord& rec, int code, const GandharWorld& w);

// `sub_47F6F0`, one frame - his brain from `Hud_DrawBar` on, less the floor
// cell bookkeeping (`sub_420C10` / `sub_420B80`), which is the frontend's as
// it is the generic arm's. `pickedClipPlaying` is record flag 8's clip
// (a hit reaction), which the frontend plays: while it runs the brain
// returns, as `sub_47EBF0` makes it.
void gandharTick(GandharActor& a, ShootRecord& rec, const ShootAi::Tables& t, float dt,
                 float& facing, const GandharWorld& w);

// The behaviour script for his health, `sub_47F6F0`'s band test: a change of
// band resets the step and the repeats (`+144`, `+100`); `+190` is set for
// the two wounded bands and left alone for the healthy one, as the engine's
// inline healthy walk leaves it.
const std::vector<ShootScriptStep>& gandharScript(ShootRecord& rec, const ShootAi::Tables& t);

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shoot.h"

#include <cstring>

namespace omk {
namespace {

// The binary's own table at 0x004CFA30, kept here as well as in
// `tables/shoot_ai.json` because it is fourteen short strings and a replica
// that cannot name a type prints numbers. The JSON is the authority and
// `verify.py: exe tables` re-derives it; this must agree with it, which the
// sweep asserts rather than assumes.
const char* const kTypeNames[kCharTypeCount] = {
    "No one", "Man passer", "Woman passer", "Man enemy", "Woman enemy",
    "Mecagarde", "Mecadog", "X-Tech", "Z-Tech", "Incarnable",
    "Gandhar", "Zombie", "Spectre", "Astaroth",
};

// Astaroth's state graph, read from `sub_4800C0`'s switch on `+156`.
//
// The distances are his own literals, in world units (1 unit ~ 2.54 cm):
// 195 is where he closes to grapple, 273 the range of the throw in state 19,
// and 78 / 156 the two bands that pick its impulse. The two timers are float
// literals stored as bit patterns in the decompilation - 0x43160000 is 150.0
// and 0x42700000 is 60.0, five seconds and two at 30 Hz.
const std::vector<ShootEdge> kAstarothEdges = {
    {16, 17, "closed to within 195 units of the target"},
    {17, 18, "the grapple animation landed"},
    {18, 19, "the hold landed"},
    {19, 21, "the throw landed - and inside 273 units it also pushes the "
             "target by 3700 (<78), 2300 (<156) or 1200 units"},
    {20, 21, "recovered"},
    {20, 16, "the 150-frame timer ran out first"},
    {21, 16, "the recovery animation finished; the timer resets to 60 frames"},
    {29, 16, "and only when the global counter has reached 6"},
};
const std::vector<int> kAstarothStates = {16, 17, 18, 19, 20, 21, 27, 29};

// The generic shooter's state graph, read from `sub_424DE0`'s switch. The
// per-state geometry - the aiming, the projectile spawn, the cover search -
// is 1500 lines of decompiler output and is NOT ported; what is here is what
// the machine DECIDES. State 6 is the fight itself and carries a five-way
// sub-switch of its own; 9 and 28 share one arm.
const std::vector<ShootEdge> kGenericEdges = {
    { 1,  2, "the navigation node changed under it"},
    { 1,  6, "the node it was walking to is the one it wanted"},
    { 2,  6, "arrived"},
    { 4,  5, ""},
    { 5,  4, ""},
    { 6, 10, "out of the fight - also reached from sub-case 2"},
    {10, 11, ""},
    {11, 10, ""},
    {14,  4, ""},
};
const std::vector<int> kGenericStates = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,28};

}  // namespace

const char* charTypeName(int type) {
    return (type >= 0 && type < kCharTypeCount) ? kTypeNames[type] : "";
}

ShootBrain shootBrainFor(std::uint32_t characterType) {
    // `Shoot_ActorEnter`'s switch, and `default:` is load-bearing: 330 of the
    // 1032 shipped character records carry 0xFFFFFFFF here, and every one of
    // them lands on the generic shooter through it.
    switch (characterType) {
        case 7:  return ShootBrain::Inert;
        case 10: return ShootBrain::Gandhar;
        case 13: return ShootBrain::Astaroth;
        default: return ShootBrain::Generic;
    }
}

const char* shootBrainName(ShootBrain b) {
    switch (b) {
        case ShootBrain::Inert:    return "nullsub_9";
        case ShootBrain::Gandhar:  return "sub_47F6F0";
        case ShootBrain::Astaroth: return "sub_4800C0";
        default:                   return "sub_424DE0";
    }
}

const ShootAction* ShootAi::Tables::byCode(int code) const {
    for (const auto& a : actions) if (a.code == code) return &a;
    return nullptr;
}

// `sub_47FB40` (0x0047FB40): play the current step `repeats` times, then move
// on; a `{0, n}` step rewinds. The healthy path in `sub_47F6F0` is this
// function inlined, which is how the two were confirmed to be one walk.
int shootScriptAdvance(ShootRecord& r, const std::vector<ShootScriptStep>& s) {
    if (s.empty()) return 0;
    if (r.scriptStep < 0 || r.scriptStep >= static_cast<int>(s.size()))
        r.scriptStep = 0;
    const auto& cur = s[static_cast<std::size_t>(r.scriptStep)];
    if (r.repeats < cur.repeats) { ++r.repeats; return cur.action; }
    const int next = r.scriptStep + 1;
    if (next < static_cast<int>(s.size()) && !s[static_cast<std::size_t>(next)].rewind) {
        r.scriptStep = next;
        r.repeats = 1;
        return s[static_cast<std::size_t>(next)].action;
    }
    r.scriptStep = 0;
    r.repeats = 1;
    return s[0].action;
}

ShootAi::ShootAi(const Tables& t, std::uint32_t characterType) : t_(&t) {
    rec_.type  = characterType;
    rec_.brain = shootBrainFor(characterType);
    // Every arm starts where its own entry leaves it: Gandhar on the first
    // step of his script, Astaroth in 16, the generic shooter in 1.
    switch (rec_.brain) {
        case ShootBrain::Gandhar:  rec_.state = 0;  break;
        case ShootBrain::Astaroth: rec_.state = 16; break;
        case ShootBrain::Generic:  rec_.state = 1;  break;
        default: break;
    }
}

const std::vector<ShootScriptStep>& ShootAi::scriptForHealth() {
    // Re-read EVERY frame, and a change of band resets the walk. Note the
    // comparisons: Gandhar's are `<=` where Astaroth's speed bands are `<`.
    // That asymmetry is in the two functions and is not a transcription slip.
    if (rec_.health <= kCriticalAt) {
        if (rec_.band != 2) { rec_.band = 2; rec_.scriptStep = 0; rec_.repeats = 0; }
        return t_->critical;
    }
    if (rec_.health <= kWoundedAt) {
        if (rec_.band != 1) { rec_.band = 1; rec_.scriptStep = 0; rec_.repeats = 0; }
        return t_->wounded;
    }
    rec_.band = 0;
    return t_->healthy;
}

void ShootAi::tickGandhar(float dt) {
    (void)dt;
    if (rec_.health <= 0) {
        log_.push_back({Decision::Kind::Died, rec_.state, rec_.state, -1, 0.0f,
                        "hp reached 0 - event 43 arg 3"});
        return;
    }
    if (!actionDone_) return;           // dword_657A28 has not pulsed
    actionDone_ = false;

    const auto& script = scriptForHealth();
    const int code = shootScriptAdvance(rec_, script);
    const ShootAction* a = t_->byCode(code);
    const int from = rec_.state;
    if (!a) {                            // a code with no row: impossible in
        log_.push_back({Decision::Kind::Idle, from, code, -1, 0.0f,
                        "action code with no handler row"});
        return;                          // the shipped scripts, asserted
    }
    // the ENTER handler: set +156, set or clear channel flag 0x800, and pick
    // an animation by TYPE from this character's own list
    rec_.state = a->code;
    if (a->setsFlag) rec_.flags |= 0x800u; else rec_.flags &= ~0x800u;
    if (a->code == 16) {
        // the only action with no animation: it waits (rand() & 0x1F) + 30
        // frames. The replica takes the midpoint rather than a random draw,
        // because a sweep that has to be reproducible cannot roll dice - the
        // RANGE is the fact, and it is asserted.
        rec_.timer = 45.0f;
        log_.push_back({Decision::Kind::Wait, from, a->code, -1, rec_.timer,
                        "wait 30..61 frames"});
    } else {
        log_.push_back({Decision::Kind::Action, from, a->code, a->clipType,
                        0.0f, a->note});
    }
    rec_.flags &= 0xFFFFDF7Fu;           // the mask the original clears
}

void ShootAi::tickAstaroth(float dt) {
    rec_.timer -= dt;
    // the health bands, which scale his speed and his turn rate. `<`, not
    // `<=` - see scriptForHealth.
    float speed = 1.0f, turn = 60.0f;
    if (rec_.health < 100) { speed = 1.5f; turn = 40.0f; }
    if (rec_.health < 50)  { speed = 2.0f; turn = 30.0f; }
    (void)speed; (void)turn;

    if (rec_.health <= 0) {
        log_.push_back({Decision::Kind::Died, rec_.state, rec_.state, -1, 0.0f,
                        "hp reached 0 - event 43 arg 3"});
        return;
    }
    if (!actionDone_) return;
    actionDone_ = false;

    const int from = rec_.state;
    int to = from;
    const char* why = "";
    switch (from) {
        case 16: to = 17; why = "closed to within 195 units"; break;
        case 17: to = 18; why = "the grapple landed";         break;
        case 18: to = 19; why = "the hold landed";            break;
        case 19: to = 21; why = "the throw landed";
                 rec_.timer = 150.0f;                          break;
        case 20: if (rec_.timer <= 0.0f) { to = 16; why = "the 150-frame timer ran out"; }
                 else { to = 21; why = "recovered"; }          break;
        case 21: to = 16; why = "recovery finished"; rec_.timer = 60.0f; break;
        case 27: why = "a terminal state - the arm does nothing"; break;
        case 29: to = 16; why = "the global counter reached 6";  break;
        default: to = 16; why = "the default arm resets him";     break;
    }
    rec_.state = to;
    log_.push_back({from == to ? Decision::Kind::Idle : Decision::Kind::StateChange,
                    from, to, -1, rec_.timer, why});
}

void ShootAi::tickGeneric(float dt) {
    (void)dt;
    if (rec_.health <= 0) {
        log_.push_back({Decision::Kind::Died, rec_.state, rec_.state, -1, 0.0f,
                        "hp reached 0"});
        return;
    }
    if (!actionDone_) return;
    actionDone_ = false;
    const int from = rec_.state;
    // The graph, and only the graph. Which of a state's outgoing edges is
    // taken depends on geometry this tree does not have - the navigation node,
    // the line of sight, the weapon's range - so the replica takes the first
    // edge and RECORDS the choice rather than pretending to compute it.
    for (const auto& e : kGenericEdges)
        if (e.from == from) {
            rec_.state = e.to;
            log_.push_back({Decision::Kind::StateChange, from, e.to, -1, 0.0f, e.why});
            return;
        }
    log_.push_back({Decision::Kind::Idle, from, from, -1, 0.0f,
                    "a state with no outgoing edge in the switch"});
}

const char* ShootAi::tick(float dt) {
    rec_.flags |= 1u;                    // Shoot_TickNpc sets bit 0 around it
    switch (rec_.brain) {
        case ShootBrain::Inert:    break;             // nullsub_9
        case ShootBrain::Gandhar:  tickGandhar(dt);  break;
        case ShootBrain::Astaroth: tickAstaroth(dt); break;
        case ShootBrain::Generic:  tickGeneric(dt);  break;
    }
    rec_.flags &= ~1u;
    return shootBrainName(rec_.brain);
}

const std::vector<int>& astarothStates() { return kAstarothStates; }
const std::vector<int>& genericStates()  { return kGenericStates; }
const std::vector<ShootEdge>& astarothEdges() { return kAstarothEdges; }
const std::vector<ShootEdge>& genericEdges()  { return kGenericEdges; }

}  // namespace omk

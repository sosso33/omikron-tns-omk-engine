// SPDX-License-Identifier: GPL-3.0-or-later
// Gandhar's arm of the shoot AI - see gandhar.h and todo/gandhar.md.
#include "actor/gandhar.h"

#include <cmath>
#include <cstdio>

namespace omk {

namespace {

void say(const GandharWorld& w, const char* fmt, int a, int b = 0, int c = 0) {
    if (!w.say) return;
    char buf[200];
    std::snprintf(buf, sizeof buf, fmt, a, b, c);
    w.say(buf);
}

// `sub_421A20(him, rec, clip, 0)`: `+8 = clip`, and when there is one its
// clock from 1.0 and the node at `(+244, +60 + the clip's dy 0 -> 1, +252)`
void startClip(GandharActor& a, ShootRecord& rec, int slot, const GandharWorld& w) {
    a.clip = slot;
    if (slot < 0) { a.clipFrames = 0; return; }
    a.clipFrames = w.clipFrames ? w.clipFrames(slot) : 0;
    a.frame = 1.0f;
    a.prev = 0.0f;
    float d[3] = {0, 0, 0};
    if (w.rootDelta) w.rootDelta(slot, 0.0f, 1.0f, d);
    a.node[0] = a.pos[0];
    a.node[1] = rec.groundY + d[1];
    a.node[2] = a.pos[2];
}

void moveNode(GandharActor& a, float dx, float dy, float dz) {
    a.node[0] += dx; a.node[1] += dy; a.node[2] += dz;
}

// The four ticks that end in it (19, 20, 23, 24): once an attack reaches
// (`sub_421020` -> +108), `(rand() % 100 <= p) + 25` by band and the ENTER of
// what it rolled, then `+160 &= ~0x2080`
bool roll(GandharActor& a, ShootRecord& rec, const GandharWorld& w) {
    const int atk = w.pickAttack ? w.pickAttack() : 0;
    rec.attackSlot = atk;
    if (!atk) return false;
    int p = kGandharRollHealthy;
    if (rec.band == 1) p = kGandharRollWounded;
    else if (rec.band == 2) p = kGandharRollCritical;
    const int r = w.rnd ? w.rnd() : 0;
    const int code = (r % 100 <= p) + 25;
    say(w, "the attack reaches (slot %d): rolled %d against %d", atk, r % 100, p);
    gandharEnterAction(a, rec, code, w);
    rec.flags &= 0xFFFFDF7Fu;
    return true;
}

// The target's sight and the turn, which almost every tick opens with
bool look(ShootRecord& rec, float& facing, const GandharWorld& w, bool turn) {
    (void)rec;
    const bool in = w.sight ? w.sight() : false;
    if (turn && w.turn) w.turn(facing);
    return in;
}

void stepToward(GandharActor& a, float dt, float out[2], const GandharWorld& w) {
    gandharStep(a, a.speed, dt, out, w);
}

}  // namespace

const std::vector<ShootScriptStep>& gandharScript(ShootRecord& rec, const ShootAi::Tables& t) {
    if (rec.health <= kWoundedAt) {
        if (rec.health <= kCriticalAt) {
            if (rec.band != 2) { rec.repeats = 0; rec.scriptStep = 0; rec.band = 2; }
            return t.critical;
        }
        if (rec.band != 1) { rec.repeats = 0; rec.scriptStep = 0; rec.band = 1; }
        return t.wounded;
    }
    // the healthy walk is inline in `sub_47F6F0` and leaves +190 alone
    return t.healthy;
}

void gandharEnter(GandharActor& a, ShootRecord& rec, const float pos[3], int property3,
                  const GandharWorld& w) {
    rec.band = 0;
    rec.scriptStep = 0;
    rec.repeats = 0;
    rec.flags |= 0x4020u;
    for (int k = 0; k < 3; ++k) { a.pos[k] = pos[k]; a.node[k] = pos[k]; }
    // his own spot refused by the wall test: to the nearest standable point
    // on floor 1, node and record alike
    if (w.wall && w.wall(0.0f, 0.0f) && w.standable) {
        float x = a.node[0], z = a.node[2];
        w.standable(x, z);
        const float dx = x - a.pos[0], dz = z - a.pos[2];
        a.node[0] += dx; a.node[2] += dz;
        a.pos[0] += dx;  a.pos[2] += dz;
        say(w, "his spot refused at entry: moved %d %d to a standable point",
            static_cast<int>(dx), static_cast<int>(dz));
    }
    a.node[1] = kGandharEntryY;
    rec.groundY = kGandharEntryY;
    a.speed = static_cast<float>(39 * property3 / 30);
    // action 23, inline: 0x800, a type-19 clip, +156 = 23, +164
    rec.flags |= 0x800u;
    startClip(a, rec, w.pickType ? w.pickType(19) : -1, w);
    rec.state = 23;
    rec.clipLen = w.listValue ? w.listValue() : 0.0f;
    a.grabbed = false;
    a.started = true;
}

void gandharStep(GandharActor& a, float speed, float dt, float out[2], const GandharWorld& w) {
    out[0] = out[1] = 0.0f;
    const auto wall = [&](float dx, float dz) { return w.wall ? w.wall(dx, dz) : 0; };
    float ox = 0.0f, oz = 0.0f;
    if (wall(0.0f, 0.0f)) {
        // his own spot refused: the nearest standable point from his NODE
        // (`Actor_GetPosAndFacing(him)`), `sub_4368E0(1, ..)`, as the step
        float x = a.node[0], z = a.node[2];
        if (w.standable) w.standable(x, z);
        ox = x - a.pos[0];
        oz = z - a.pos[2];
    } else {
        float fx = 0.0f, fz = 0.0f;
        if (w.forward) w.forward(fx, fz);
        ox = -(speed * fx * dt);
        oz = -(speed * fz * dt);
    }
    const auto take = [&](float dx, float dz) {
        a.pos[0] += dx; a.pos[2] += dz;
        out[0] = dx;    out[1] = dz;
    };
    const int v = wall(ox, oz);
    if (v != a.stepCut) {
        a.stepCut = v;
        if (w.say) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "STEP %s (sub_47E5F0: the wall test answers %d) at "
                          "%.0f %.0f, the step %.2f %.2f", v ? "CUT" : "free", v,
                          double(a.pos[0]), double(a.pos[2]), double(ox), double(oz));
            w.say(buf);
        }
    }
    if (!v) { take(ox, oz); return; }
    if (v != 3) {
        // x alone, else z alone, else nothing
        if (!wall(ox, 0.0f)) { take(ox, 0.0f); return; }
        if (!wall(0.0f, oz)) { take(0.0f, oz); return; }
        return;
    }
    // A BYTE-2 WALL (the test's 3). LABEL_15 is "x alone at the probe's x,
    // else a nudge of +speed along z"; the two orders reach it differently.
    float probeX = ox;
    float xTaken = ox;                   // what LABEL_16 adds to x
    if (std::fabs(oz) > std::fabs(ox)) {
        if (!wall(0.0f, oz)) { take(0.0f, oz); return; }
        if (!wall(speed, 0.0f)) { take(speed, 0.0f); return; }
        probeX = speed;                  // `goto LABEL_15` with the step zeroed
        xTaken = 0.0f;
    }
    if (!wall(probeX, 0.0f)) { take(xTaken, 0.0f); return; }
    if (!wall(0.0f, speed)) { take(0.0f, speed); return; }
}

int gandharGrabSide(float x, float z, const float bound[6]) {
    const float a = x - bound[0], b = bound[1] - x;      // to min x, to max x
    const float c = z - bound[4], d = bound[5] - z;      // to min z, to max z
    int xs = 1;
    float xd = b;
    if (a < b) { xs = 0; xd = a; }                       // `test ah, 1` - strictly below
    int zs = 3;
    float zd = d;
    if (c < d) { zs = 2; zd = c; }
    const int side = (xd <= zd) ? xs : zs;               // `test ah, 41h` - below or equal
    static const int kMessage[4] = {7, 6, 8, 5};
    return kMessage[side];
}

int gandharClock(GandharActor& a, float dt, bool stopAtEnd, const GandharWorld& w) {
    if (a.clip < 0) return 1;           // nothing moves
    const float frames = static_cast<float>(a.clipFrames);
    a.prev = a.frame;
    a.frame += dt;
    int ret = 1;
    if (a.frame >= frames * 0.5f) ret = 2;
    if (a.frame >= frames) {
        a.prev = 1.0f;
        a.frame = dt + 1.0f;
        ret = 0;
        if (stopAtEnd) return 0;
    }
    float d[3] = {0, 0, 0};
    if (w.rootDelta) w.rootDelta(a.clip, a.prev, a.frame, d);
    moveNode(a, d[0], d[1], d[2]);
    return ret;
}

void gandharEnterAction(GandharActor& a, ShootRecord& rec, int code, const GandharWorld& w) {
    const auto pick = [&](int type) { return w.pickType ? w.pickType(type) : -1; };
    const auto byId = [&](int id) { return w.pickId ? w.pickId(id) : -1; };
    switch (code) {
        case 16:   // `sub_47E4F0`: no clip, a wait of (rand() & 0x1F) + 30
            rec.state = 16;
            rec.timer = static_cast<float>(((w.rnd ? w.rnd() : 0) & 0x1F) + 30);
            break;
        case 17:   // `sub_47E230`: the RISE, no clip
            rec.state = 17; rec.flags |= 0x800u; break;
        case 18:   // `sub_47E250`: the SINK, no clip
            rec.state = 18; rec.flags |= 0x800u; break;
        case 19: rec.flags |= 0x800u;  startClip(a, rec, pick(17), w); rec.state = 19; break;
        case 20: rec.flags &= ~0x800u; startClip(a, rec, pick(11), w); rec.state = 20; break;
        case 21: rec.flags |= 0x800u;  startClip(a, rec, pick(18), w); rec.state = 21; break;
        case 22: rec.flags |= 0x800u;  startClip(a, rec, pick(18), w); rec.state = 22; break;
        case 23:
            rec.flags |= 0x800u; startClip(a, rec, pick(19), w); rec.state = 23;
            rec.clipLen = w.listValue ? w.listValue() : 0.0f;
            break;
        case 24:
            rec.flags &= ~0x800u; startClip(a, rec, pick(11), w); rec.state = 24;
            rec.clipLen = w.listValue ? w.listValue() : 0.0f;
            break;
        case 25: rec.flags |= 0x800u; startClip(a, rec, byId(1), w); rec.state = 25; break;
        case 26: rec.flags |= 0x800u; startClip(a, rec, byId(9), w); rec.state = 26; break;
        case 27: rec.flags |= 0x800u; startClip(a, rec, byId(6), w); rec.state = 27; break;
        default:   // the ENTER table's default row is 0, action 17's
            rec.state = 17; rec.flags |= 0x800u; break;
    }
    say(w, "action %d entered (clip %d, %d frames)", rec.state, a.clip, a.clipFrames);
}

namespace {

// The TICK of the current action (`0x004CFBC8`, by `+156`; any other code
// takes row 0, action 17's). True = the action is DONE.
bool tickAction(GandharActor& a, ShootRecord& rec, float dt, float& facing,
                const GandharWorld& w) {
    float st[2];
    switch (rec.state) {
        case 16: {   // `sub_47E520`: the wait, stepping toward him
            look(rec, facing, w, true);
            stepToward(a, dt, st, w);
            moveNode(a, st[0], 0.0f, st[1]);
            rec.timer -= dt;
            return rec.timer <= 0.0f;
        }
        case 18: {   // `sub_47E960`: the SINK, until y - +64 >= 250
            const float rel = a.node[1] - rec.height;
            look(rec, facing, w, true);
            rec.groundY = a.node[1];
            stepToward(a, dt, st, w);
            if (rel >= kGandharSinkTo) { moveNode(a, st[0], 0.0f, st[1]); return true; }
            moveNode(a, st[0], a.speed * dt, 0.0f);   // the engine passes z 0 here
            return false;
        }
        case 19: {   // `sub_47EA50`: the clip, then the roll
            look(rec, facing, w, true);
            if (gandharClock(a, dt, true, w)) return false;
            return !roll(a, rec, w);
        }
        case 20: {   // `sub_47F0F0`: walking with the clip, rolling every frame
            look(rec, facing, w, true);
            stepToward(a, dt, st, w);
            if (!gandharClock(a, dt, true, w)) return true;
            moveNode(a, st[0], 0.0f, st[1]);
            roll(a, rec, w);
            return false;
        }
        case 21: {   // `sub_47F2B0`
            look(rec, facing, w, true);
            return gandharClock(a, dt, true, w) == 0;
        }
        case 22: {   // `sub_47F640` - which never fetches the target before its
                     // cone test (stack garbage in the engine); the player here,
                     // LABELLED
            look(rec, facing, w, true);
            return gandharClock(a, dt, true, w) == 0;
        }
        case 23: {   // `sub_47EF10`: FIRE once at the clip's half (arm 1)
            const bool in = look(rec, facing, w, !(rec.flags & 0x80u));
            const int c = gandharClock(a, dt, true, w);
            if (in && c == 2 && !(rec.flags & 0x80u) && w.fire && w.fire(1)) {
                rec.flags |= 0x80u;
                say(w, "FIRES (action 23, arm %d)", 1);
            }
            if (!c) return true;
            roll(a, rec, w);
            return false;
        }
        case 24: {   // `sub_47ED10`: stepping, FIRE on a coin flip (arm 0)
            const bool in = look(rec, facing, w, true);
            stepToward(a, dt, st, w);
            const int c = gandharClock(a, dt, true, w);
            if (in && w.rnd && (w.rnd() & 1) && w.fire && w.fire(0)) {
                rec.flags |= 0x80u;
                say(w, "FIRES (action 24, arm %d)", 0);
            }
            moveNode(a, st[0], 0.0f, st[1]);
            if (!c) return true;
            roll(a, rec, w);
            return false;
        }
        case 25: {   // `sub_47F340`: the GRAB, tested when its clip ends
            look(rec, facing, w, false);
            if (gandharClock(a, dt, true, w)) return false;
            if (w.touch && w.touch()) {
                const int msg = w.side ? w.side() : -1;
                say(w, "the GRAB touches: message %d", msg);
                if (msg >= 0 && w.post) w.post(msg, true);
                a.grabbed = true;
                return true;
            }
            // `off_4CFBC0`: action 27 - replaced by the brain's next action
            // the same frame, since this returns 1 (the assembly, 0x47F4CE)
            gandharEnterAction(a, rec, 27, w);
            rec.flags &= 0xFFFFDF7Fu;
            return true;
        }
        case 26: {   // `sub_47F510`: the STRIKE at the clip's half, once
            look(rec, facing, w, false);
            const int c = gandharClock(a, dt, true, w);
            if (c == 2 && !(rec.flags & 0x80u) && w.touch && w.touch()) {
                const int dmg = w.strikeDamage ? w.strikeDamage() : 11;
                if (w.strike) w.strike(dmg);
                rec.flags |= 0x80u;
                say(w, "the STRIKE lands: damage %d", dmg);
            }
            return c == 0;
        }
        case 27:     // `sub_47F6C0`
            return gandharClock(a, dt, true, w) == 0;
        case 17:
        default: {   // `sub_47E870`: the RISE, until y - +64 <= -150
            const float rel = a.node[1] - rec.height;
            look(rec, facing, w, true);
            rec.groundY = a.node[1];
            stepToward(a, dt, st, w);
            if (rel <= kGandharRiseTo) { moveNode(a, st[0], 0.0f, st[1]); return true; }
            moveNode(a, st[0], -a.speed * dt, st[1]);
            return false;
        }
    }
}

void nextAction(GandharActor& a, ShootRecord& rec, const ShootAi::Tables& t,
                const GandharWorld& w) {
    const auto& script = gandharScript(rec, t);
    const int code = shootScriptAdvance(rec, script);
    gandharEnterAction(a, rec, code, w);
    rec.flags &= 0xFFFFDF7Fu;
}

}  // namespace

void gandharTick(GandharActor& a, ShootRecord& rec, const ShootAi::Tables& t, float dt,
                 float& facing, const GandharWorld& w) {
    // `Hud_DrawBar` and `sub_420C10` are the frontend's
    if (rec.health <= 0) {
        // event 43 {3, him} - every frame in the engine, until the handler's
        // `shoot.end` takes him out of the shoot; once here
        if (!a.deathPosted && w.post) {
            a.deathPosted = true;
            say(w, "DEAD - health %d: message %d", rec.health, 3);
            w.post(3, false);
        }
        return;
    }
    if (a.grabbed) {
        a.grabbed = false;
        nextAction(a, rec, t, w);
    }
    if (tickAction(a, rec, dt, facing, w)) {
        say(w, "action %d DONE, node y %d", rec.state, static_cast<int>(a.node[1]));
        // `o3de_SetNodePos(+244, +60, +252)`, then the next action
        a.node[0] = a.pos[0];
        a.node[1] = rec.groundY;
        a.node[2] = a.pos[2];
        nextAction(a, rec, t, w);
    }
}

}  // namespace omk

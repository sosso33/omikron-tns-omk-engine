// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT A GUNMAN CAN SEE AND HOW FAR HE CAN SHOOT, read off the shipped
// character records (`todo/shoot-mode.md` 5c, 7a).
//
//     shoot_range <gamedata>
//
// `sub_422540` builds the shoot record out of the CHARACTER's own properties
// through event 44, not out of the weapon table. Properties 26 and 27 and 30
// are ranges in METRES (`39 * v` into the record) and 29 is the sight cone's
// half-angle in DEGREES (`cos(v)` into it). Those properties live in the
// 276-byte actor record - 26 is `0x1A`, an int16 at `+180`, and the rest are
// its neighbours - so the values a designer authored can simply be read.
#include "actor/shoot.h"
#include "formats/iam.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"
#include "script/props.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: shoot_range <gamedata>\n"); return 2; }
    omk::DataFs fs(argv[1]);

    long records = 0, withRange = 0, withCone = 0;
    std::map<int, long> rangeM, coneDeg;
    for (const char* arch : {"AREA", "SCENE"}) {
        const auto raw = fs.read(std::string("IAM/") + arch);
        const auto ar = omk::IamArchive::open(raw);
        const bool area = std::strcmp(arch, "AREA") == 0;
        const std::size_t pAt = area ? 56 : 24, cAt = area ? 80 : 48;
        for (std::size_t ci = 0; ci < ar.size(); ++ci) {
            const auto span = ar.chunk(ci);
            if (span.size() < cAt + 2) continue;
            std::vector<std::byte> b(span.begin(), span.end());
            std::int32_t lo = 0; int n = 0;
            std::memcpy(&lo, b.data() + pAt, 4);
            std::memcpy(&n, b.data() + cAt, 2); n &= 0xFFFF;
            if (n <= 0 || lo <= 0 ||
                static_cast<std::size_t>(lo) + 276u * n > b.size()) continue;
            for (int i = 0; i < n; ++i) {
                const std::size_t o = static_cast<std::size_t>(lo) + 276u * i;
                const std::span<const std::byte> rec(b.data() + o, 276);
                ++records;
                omk::ShootProperties p;
                std::int32_t v = 0;
                if (omk::readActorProperty(rec, 1,  v)) p.health        = v;
                if (omk::readActorProperty(rec, 26, v)) p.rangeAcquireM = v;
                if (omk::readActorProperty(rec, 27, v)) p.rangeInnerM   = v;
                if (omk::readActorProperty(rec, 30, v)) p.rangeThirdM   = v;
                if (omk::readActorProperty(rec, 29, v)) p.coneDegrees   = v;
                if (omk::readActorProperty(rec, 37, v)) p.behaviourBits = v;
                if (p.rangeAcquireM > 0) { ++withRange; ++rangeM[p.rangeAcquireM]; }
                if (p.coneDegrees   > 0) { ++withCone;  ++coneDeg[p.coneDegrees]; }
            }
        }
    }
    std::printf("actor records %ld, with an acquisition range %ld, with a cone %ld\n",
                records, withRange, withCone);
    std::printf("ranges (metres):");
    for (const auto& kv : rangeM) std::printf(" %d:%ld", kv.first, kv.second);
    std::printf("\ncones (degrees):");
    for (const auto& kv : coneDeg) std::printf(" %d:%ld", kv.first, kv.second);
    std::printf("\n");

    // ---- the three properties of the test a corpus tally cannot show ----
    //
    // (1) A CHARACTER FACES -Z at yaw 0, so the gunman acquires what is at
    //     NEGATIVE z from him and not what is at positive z. The engine
    //     expresses that as `self - target` dotted with `(0,0,1)` rotated by
    //     his own yaw - two sign conventions that cancel - and getting either
    //     one alone wrong builds a machine that shoots at whatever is behind
    //     it. That is not a hypothetical: the first transcription of
    //     `sub_420C70` had the two ends swapped and this assertion is what
    //     caught it.
    // (2) THE CONE bites: a target at the same distance but 90 degrees round
    //     is refused where one straight ahead is taken.
    // (3) `sub_420D90` DOUBLES the range - a target past the reach but inside
    //     twice it is refused by the narrow arm and taken by the wide one.
    omk::ShootRecord r;
    omk::ShootProperties p; p.rangeAcquireM = 20; p.coneDegrees = 45; p.health = 0;
    p.behaviourBits = 0x10 | 0x04;
    omk::initShootRecord(r, p);

    omk::AcquireOut o;
    const float self_[4]  = {0, 0, 0, 0};       // at the origin, yaw 0
    const float front[3]  = {0, 0, -390};       // 10 m along -Z: IN FRONT
    const float back[3]   = {0, 0,  390};       // 10 m along +Z: behind him
    const float side[3]   = {390, 0, 0};        // 10 m abeam
    const float far_[3]   = {0, 0, -1170};      // 30 m ahead: past 20, inside 40
    const bool okFront = omk::shootAcquires(r, self_, front, o, false);
    const float crossFront = o.cross;
    const bool okBack  = omk::shootAcquires(r, self_, back,  o, false);
    const bool okSide  = omk::shootAcquires(r, self_, side,  o, false);
    const float crossSide = o.cross;
    const bool okFar   = omk::shootAcquires(r, self_, far_,  o, false);
    const bool okWide  = omk::shootAcquires(r, self_, far_,  o, true);
    // turning him 180 degrees swaps which side he can see
    const float turned[4] = {0, 0, 0, 180};
    const bool turnFront = omk::shootAcquires(r, turned, front, o, false);
    const bool turnBack  = omk::shootAcquires(r, turned, back,  o, false);

    std::printf("record: range %.0f inner %.0f third %.0f coneCos %.4f health %d flags %08x\n",
                r.rangeAcquire, r.rangeInner, r.rangeThird, r.coneCos, r.health, r.flags);
    std::printf("acquire: front %d back %d side %d far %d far-wide %d; "
                "turned front %d back %d\n",
                int(okFront), int(okBack), int(okSide), int(okFar), int(okWide),
                int(turnFront), int(turnBack));
    std::printf("cross: front %.1f side %.1f\n", crossFront, crossSide);

    // ---- THE TURN (`sub_420EB0`, 7b) ---------------------------------
    //
    // Four bands and a snap, and then the thing a single frame cannot show:
    // that the turn CONVERGES. A sign error here is invisible standing still
    // - the actor rotates by the right amount in the wrong direction - and it
    // is exactly the class CLAUDE.md 1 says has to be asserted over the
    // transition rather than at rest.
    auto bandOf = [&](float yaw, const float tgt[3], bool snap, float& moved) {
        float self4[4] = {0, 0, 0, yaw};
        omk::AcquireOut aa;
        omk::shootAcquires(r, self4, tgt, aa, true);
        float e = yaw;
        const int snapped = omk::shootTurnToward(e, aa, snap, 1.0f);
        moved = e - yaw;
        return snapped;
    };
    float mv = 0.0f;
    const float d0[3]   = {0, 0, -390};                 // dead ahead
    const float d15[3]  = {-101, 0, -377};              // ~15 deg off
    const float d45[3]  = {-276, 0, -276};              // 45 deg off
    const float d135[3] = {-276, 0,  276};              // 135 deg - behind
    const float d180[3] = {0, 0, 390};                  // hard behind
    const float dR45[3] = {276, 0, -276};               // 45 deg the OTHER way
    float m0, m15, m45, m135, m180s, mR45;
    const int s0   = bandOf(0, d0,   false, m0);
    const int s15  = bandOf(0, d15,  false, m15);
    const int s45  = bandOf(0, d45,  false, m45);
    const int s135 = bandOf(0, d135, false, m135);
    const int sSnapAbeam = bandOf(0, d45, true, mv);
    const int sSnapBack  = bandOf(0, d180, true, m180s);
    const int sR45 = bandOf(0, dR45, false, mR45);
    std::printf("turn: ahead %d/%.1f  15deg %d/%.1f  45deg %d/%.1f  "
                "behind %d/%.1f  mirrored45 %.1f\n",
                s0, m0, s15, m15, s45, m45, s135, m135, mR45);
    std::printf("snap: abeam %d  hard-behind %d\n", sSnapAbeam, sSnapBack);

    // convergence: start 135 degrees off and turn until aimed
    {
        float yaw = 0.0f;
        const float* tgt = d135;
        int frames = 0, rose = 0;
        double worst = -1e9, prev = -1e9;
        for (; frames < 400; ++frames) {
            float self4[4] = {0, 0, 0, yaw};
            omk::AcquireOut aa;
            omk::shootAcquires(r, self4, tgt, aa, true);
            const double signed2 = double(aa.dotFlat) * std::fabs(double(aa.dotFlat));
            if (signed2 > double(aa.dist2d2) * 0.99000001) break;   // aimed
            if (frames && signed2 < prev - 1e-3) ++rose;            // went BACKWARDS
            prev = signed2; worst = signed2;
            if (omk::shootTurnToward(yaw, aa, false, 1.0f) != 0) break;
        }
        (void)worst;
        std::printf("converge: %d frames from 135 deg, %d frames going the wrong way, "
                    "final yaw %.1f\n", frames, rose, yaw);
    }
    // ---- THE GENERIC BRAIN's frame and the states that ARE read (7c) ----
    {
        // coverage first, and against the machine's own state list rather
        // than a number typed here
        const auto& all  = omk::genericStates();
        const auto& read = omk::genericStatesRead();
        int inRange = 0;
        for (int st : read)
            for (int a : all) if (a == st) { ++inRange; break; }
        std::printf("generic: %zu states, %zu transcribed, %d of those in the state set\n",
                    all.size(), read.size(), inRange);

        auto fresh = [&](int st) {
            omk::ShootRecord q; q.state = st;
            omk::ShootProperties pp; pp.rangeAcquireM = 20; pp.coneDegrees = 45;
            omk::initShootRecord(q, pp);
            return q;
        };
        omk::ShootFrameIn in;
        in.self[0] = 0; in.self[1] = 0; in.self[2] = 0; in.self[3] = 0;
        in.target[0] = 0; in.target[1] = 0; in.target[2] = -390; in.target[3] = 0;
        in.dt = 1.0f;

        // every UNREAD state must change nothing at all
        int unreadSeen = 0, unreadMoved = 0;
        for (int st : all) {
            bool isRead = false;
            for (int k : read) if (k == st) { isRead = true; break; }
            if (isRead) continue;
            auto q = fresh(st);
            float e = 12.0f;
            const auto step = omk::shootGenericStep(q, in, e);
            ++unreadSeen;
            if (!step.unread || q.state != st || e != 12.0f) ++unreadMoved;
        }
        std::printf("generic: %d unread states, %d of them changed something\n",
                    unreadSeen, unreadMoved);

        // 5 ends into 4; 10 ends into 11; 11 goes back to 10
        auto q5 = fresh(5);  float e5 = 0;
        in.clipFrame = 30; in.clipFrames = 30;
        omk::shootGenericStep(q5, in, e5);
        auto q10 = fresh(10); float e10 = 0;
        omk::shootGenericStep(q10, in, e10);
        auto q10b = fresh(10); float e10b = 0;
        in.clipFrame = 2; in.clipFrames = 30;          // early - before halfway
        const auto s10b = omk::shootGenericStep(q10b, in, e10b);
        auto q10c = fresh(10); float e10c = 0;
        in.clipFrame = 20;                              // past halfway
        const auto s10c = omk::shootGenericStep(q10c, in, e10c);
        auto q11 = fresh(11); float e11 = 0;
        in.clipFrame = 9; in.targetPredicate = true;
        omk::shootGenericStep(q11, in, e11);
        std::printf("generic: 5->%d  10(end)->%d  10(early) outcome %d  "
                    "10(late) outcome %d  11->%d\n",
                    q5.state, q10.state, int(s10b.outcome), int(s10c.outcome), q11.state);

        // state 8's three snaps pick the three turn clips
        auto turnClip = [&](float tz, float tx) {
            auto q = fresh(8); float e = 0;
            omk::ShootFrameIn t = in; t.targetPredicate = false;
            t.target[0] = tx; t.target[2] = tz;
            const auto st = omk::shootGenericStep(q, t, e);
            return st;
        };
        const auto back  = turnClip(390, 0);      // hard behind -> 180 -> clip 32
        const auto left  = turnClip(0, -390);     // abeam
        const auto right = turnClip(0, 390);      // the other side
        std::printf("generic: snap clips behind %d/%.0f  abeam-a %d/%.0f  abeam-b %d/%.0f\n",
                    back.clipType, back.turnTotal, left.clipType, left.turnTotal,
                    right.clipType, right.turnTotal);

        // ---- the NAVIGATE -> TRAVERSE -> hub loop (states 1, 2) --------
        //
        // The three things state 1 computes, all transcribed formulas:
        // the heading `atan2(dz, dx) * 180/pi + 90`, the step length, and the
        // occupancy swap. Then state 2 walks it out, climbing the edge's
        // slope, and lands in the hub - which is the loop a gunman actually
        // moves on.
        {
            omk::ShootFrameIn nav = in;
            nav.targetPredicate = false;
            nav.moveCode = 1;
            nav.myNode = 3; nav.targetNode = 7;
            nav.hasEdge = true;
            nav.edgeFrom[0] = 0;   nav.edgeFrom[1] = 0;   nav.edgeFrom[2] = 0;
            nav.edgeTo[0]   = 300; nav.edgeTo[1]   = 40;  nav.edgeTo[2]   = 400;
            nav.stepCellValue = 1;
            auto q1 = fresh(1); float e1 = 0;
            const auto s1 = omk::shootGenericStep(q1, nav, e1);
            // the same step onto a cell the MOVEMENT test refuses (-128, the
            // occupancy stamp read as a signed byte)
            nav.stepCellValue = -128;
            auto q1b = fresh(1); float e1b = 0;
            const auto s1b = omk::shootGenericStep(q1b, nav, e1b);
            // and standing on the target's own node is contact
            nav.stepCellValue = 1; nav.myNode = 7;
            auto q1c = fresh(1); float e1c = 0;
            omk::shootGenericStep(q1c, nav, e1c);       // step ALSO available
            omk::ShootFrameIn still = nav; still.moveCode = 0;
            auto q1d = fresh(1); float e1d = 0;
            omk::shootGenericStep(q1d, still, e1d);      // nothing to step to
            std::printf("generic: nav take %d heading %.1f len %.0f state %d; "
                        "blocked take %d state %d; samenode+step %d samenode %d\n",
                        int(s1.takeStep), s1.headingDeg, s1.stepLength, q1.state,
                        int(s1b.takeStep), q1b.state, q1c.state, q1d.state);

            // state 2 walks the edge out: 500 units of it at 100 a frame
            auto q2 = fresh(2); q2.stepRemaining = s1.stepLength; float e2 = 0;
            omk::ShootFrameIn tr = nav;
            tr.movedThisFrame = 100.0f;
            int frames = 0; float climbed = 0;
            omk::ShootStep st2;
            for (; frames < 20; ++frames) {
                st2 = omk::shootGenericStep(q2, tr, e2);
                climbed += st2.climb;
                if (st2.arrived) break;
            }
            std::printf("generic: traverse %d frames, climbed %.1f, arrived %d, "
                        "state %d, swap %d\n",
                        frames + 1, climbed, int(st2.arrived), q2.state,
                        int(st2.swapOccupancy));

            // state 4 with no route does nothing but the tail
            auto q4 = fresh(4); float e4 = 0;
            omk::ShootFrameIn pat = nav; pat.hasRoute = false;
            const auto s4 = omk::shootGenericStep(q4, pat, e4);
            auto q4b = fresh(4); float e4b = 0;
            pat.hasRoute = true; pat.routePointHasClip = true;
            const auto s4b = omk::shootGenericStep(q4b, pat, e4b);
            std::printf("generic: patrol noroute outcome %d state %d; "
                        "route state %d outcome %d unread %d\n",
                        int(s4.outcome), q4.state, q4b.state, int(s4b.outcome),
                        int(s4b.outcomeFromUnread));
        }

        // ---- THE HUB, state 6 -----------------------------------------
        //
        // Three arms, all of which turn with the snap; only the first fires.
        // The finishing arm (flag 0x8000) short-circuits the timer entirely,
        // which is the one branch that skips the shared tail.
        {
            omk::ShootFrameIn h = in;
            h.targetPredicate = true; h.canFire = true; h.holdStill = false;
            h.target[0] = 0; h.target[2] = -390;
            auto q6a = fresh(6); float e6a = 0; q6a.timer = 5.0f;
            const auto s6a = omk::shootGenericStep(q6a, h, e6a);

            omk::ShootFrameIn h2 = h; h2.canFire = false; h2.holdStill = true;
            auto q6b = fresh(6); float e6b = 0; q6b.timer = 5.0f;
            const auto s6b = omk::shootGenericStep(q6b, h2, e6b);

            // the timer running out asks for the default action
            omk::ShootFrameIn h3 = h2; h3.defaultClipType = 77;
            auto q6c = fresh(6); float e6c = 0; q6c.timer = 0.5f;
            const auto s6c = omk::shootGenericStep(q6c, h3, e6c);

            // the FINISHING arm skips the timer altogether
            omk::ShootFrameIn h4 = h; h4.scriptStep = 8; h4.defaultClipType = 77;
            auto q6d = fresh(6); float e6d = 0;
            q6d.timer = 0.5f; q6d.flags |= 0x8000u; q6d.flags |= 0x20u;
            const auto s6d = omk::shootGenericStep(q6d, h4, e6d);

            // NOTE what the two expiry arms report. Until `324ef32` they set
            // `out.clipType = in.defaultClipType`, and that was the misreading
            // trap 12 records: `sub_424DE0`'s `a2` is the ACTOR's index, and
            // both arms ask for the literal ACTION 0. So the clip is -1 and
            // the request is 0, and the probe prints both rather than only the
            // hole the fix left.
            std::printf("generic: hub fire outcome %d timer %.1f; hold outcome %d; "
                        "expiry clip %d action %d; finishing clip %d action %d "
                        "timer %.1f flag20 %d\n",
                        int(s6a.outcome), q6a.timer, int(s6b.outcome),
                        s6c.clipType, s6c.actionRequest,
                        s6d.clipType, s6d.actionRequest, q6d.timer,
                        int((q6d.flags & 0x20u) != 0));
        }

        // ---- ACQUIRE, state 15, and the two ranges it tells apart -----
        //
        // 15 needs the cone AND the grid line of sight, or the 0x20 latch it
        // sets once it has seen you. And it fires on the INNER range `+28`,
        // not the acquisition range `+32` - which is what the second of the
        // three authored distances is for, and the only place the difference
        // shows.
        {
            omk::ShootFrameIn q = in;
            q.gridLineOfSight = true; q.targetAlive = true;
            q.target[0] = 0; q.target[2] = -390;          // 10 m ahead
            auto qa = fresh(15); float ea = 0;
            qa.rangeInner = 39.0f * 15;                    // 15 m
            const auto sa = omk::shootGenericStep(qa, q, ea);   // inside 15 m

            auto qb = fresh(15); float eb = 0;
            qb.rangeInner = 39.0f * 5;                     // 5 m - too close in
            const auto sb = omk::shootGenericStep(qb, q, eb);   // outside it

            omk::ShootFrameIn nl = q; nl.gridLineOfSight = false;
            auto qc = fresh(15); float ec = 0;
            qc.rangeInner = 39.0f * 15;
            const auto sc = omk::shootGenericStep(qc, nl, ec);   // wall in the way

            // ...but once LATCHED he stays engaged with no line of sight
            omk::ShootFrameIn nl2 = nl;
            nl2.target[0] = -276; nl2.target[2] = -276;   // 45 deg off, so the
            auto qd = fresh(15); float ed = 0;           // turn actually moves
            qd.rangeInner = 39.0f * 15; qd.flags |= 0x20u;
            const auto sd = omk::shootGenericStep(qd, nl2, ed);
            (void)sd;

            std::printf("generic: acquire inner %d outer %d blind %d latched-turn %d "
                        "latch %d\n", int(sa.outcome), int(sb.outcome),
                        int(sc.outcome), int(ed != 0.0f),
                        int((qa.flags & 0x20u) != 0));
        }

        // 12 converts a world position into a cell itself; 14 drops its route
        {
            omk::ShootFrameIn m = in;
            m.moveCode = 0; m.stepCellValue = 1; m.scriptStep = 55;
            auto q12 = fresh(12); float e12 = 0;
            const auto s12 = omk::shootGenericStep(q12, m, e12);   // walkable
            omk::ShootFrameIn m2 = m; m2.stepCellValue = 0;        // a wall
            auto q12b = fresh(12); float e12b = 0;
            const auto s12b = omk::shootGenericStep(q12b, m2, e12b);
            omk::ShootFrameIn m3 = m; m3.moveCode = 1;
            auto q14 = fresh(14); float e14 = 0;
            const auto s14 = omk::shootGenericStep(q14, m3, e14);
            std::printf("generic: cell walkable clip %d, wall clip %d; "
                        "14 -> state %d release %d\n",
                        s12.clipType, s12b.clipType, q14.state, int(s14.releaseRoute));
        }

        // ---- the MOVE decision, `sub_426C20` --------------------------
        //
        // Note which way round the codes are: 180 is the ALIGNED case and
        // +/-90 the off-to-the-side ones, which is what refutes reading the
        // clip types 30/31/32 as turns.
        {
            omk::ShootRecord q; q.goalX = 0; q.goalZ = -390;   // dead ahead
            float self4[4] = {0, 0, 0, 0}, e = 0;
            const int ahead = omk::shootMoveDecision(q, self4, e, 1.0f, false);
            q.goalX = -390; q.goalZ = 0;                        // abeam
            const int abeamA = omk::shootMoveDecision(q, self4, e, 1.0f, false);
            // BEHIND and off to one side - the +/-90 band, which needs the
            // backward component positive but under 0.8 of the distance
            q.goalX = 300;  q.goalZ = 300;
            const int quartA = omk::shootMoveDecision(q, self4, e, 1.0f, false);
            q.goalX = -300; q.goalZ = 300;
            const int quartB = omk::shootMoveDecision(q, self4, e, 1.0f, false);
            q.goalX = 0;    q.goalZ = 390;                      // behind
            float turned = 0;
            const int back = omk::shootMoveDecision(q, self4, turned, 1.0f, false);
            const int there = omk::shootMoveDecision(q, self4, e, 1.0f, true);
            std::printf("generic: move ahead %d abeam %d quarter %d/%d behind %d "
                        "(yaw %.1f) arrived %d\n",
                        ahead, abeamA, quartA, quartB, back, turned, there);
        }

        // ---- ENGAGE, `sub_426E00`: the three ranges each doing a job ----
        //
        // This is the payoff of 5c. Property 26 acquires, 27 engages, 30
        // disengages - and until this function was read, only 26 and 27 had
        // any consumer at all.
        {
            auto mk = [&](float d3) {
                omk::ShootRecord q; q.node = 2; q.state = 6;
                q.rangeAcquire = 39.0f * 60;   // 60 m
                q.rangeInner   = 39.0f * 20;   // 20 m
                q.rangeThird   = 39.0f * 40;   // 40 m
                omk::AcquireOut aa; aa.dist3d = d3;
                return std::make_pair(q, aa);
            };
            omk::EngageIn e; e.sameNode = true; e.targetAlive = true;
            e.gridClear = true; e.rayHits = true;   // rayHits keeps him in 6

            auto in20 = mk(39.0f * 10);            // 10 m: inside everything
            const int r20 = omk::shootEngage(in20.first, in20.second, true, e);
            auto in30 = mk(39.0f * 30);            // 30 m: past 20, inside 40
            const int r30 = omk::shootEngage(in30.first, in30.second, true, e);
            auto in50 = mk(39.0f * 50);            // 50 m: past the third range
            omk::EngageIn e2 = e; e2.coinHeads = true;
            const int r50 = omk::shootEngage(in50.first, in50.second, true, e2);
            auto in50b = mk(39.0f * 50);
            omk::EngageIn e3 = e; e3.coinHeads = false;
            const int r50b = omk::shootEngage(in50b.first, in50b.second, true, e3);
            // dead target, and the 0x8000 gate
            auto ind = mk(39.0f * 10);
            omk::EngageIn e4 = e; e4.targetAlive = false;
            const int rd = omk::shootEngage(ind.first, ind.second, true, e4);

            std::printf("generic: engage 10m %d/state %d  30m %d/state %d  "
                        "50m-heads %d/state %d latch %d  50m-tails state %d  dead %d\n",
                        r20, in20.first.state, r30, in30.first.state,
                        r50, in50.first.state, int((in50.first.flags & 0x20u) != 0),
                        in50b.first.state, rd);
        }

        // ---- the SHOOT CAMERA's eye height (`todo/omk-play.md` 97k) ----
        //
        // `sub_414520` case 4 sets the camera's eye-offset Y to
        // `0.7 * actor[+276]`, and `+276` is `max(centre.y + radius)` over the
        // model's own sphere list (`Actor_LoadModel`). The check that the
        // constant is UNDERSTOOD rather than copied is where it lands: 0.7 of
        // the lower extent should equal the upper one, putting the eye at the
        // crown.
        {
            const auto blob = fs.read("MESHES/PERSOS/HO1_FN.3DO");
            const auto h = omk::readHeader(blob);
            const auto sph = h ? omk::readBodySpheres(blob, *h)
                               : std::vector<omk::BodySphere>{};
            float lo = 1e30f, hi = -1e30f;
            for (const auto& q : sph) {
                lo = std::min(lo, q.y - q.radius);
                hi = std::max(hi, q.y + q.radius);
            }
            std::printf("shoot eye: %zu spheres, extent below %.2f, crown %.2f, "
                        "0.7x %.2f\n", sph.size(), double(hi), double(-lo),
                        double(0.7f * hi));
        }

        // the epilogue WRAPS the euler, and it is the only place that does
        auto qw = fresh(5); float hi = 370.0f, lo = -10.0f;
        omk::shootGenericStep(qw, in, hi);
        auto qw2 = fresh(5);
        omk::shootGenericStep(qw2, in, lo);
        std::printf("generic: wrap 370 -> %.0f, -10 -> %.0f\n", hi, lo);
    }
    return 0;
}

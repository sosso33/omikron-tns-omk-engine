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
#include "platform/datafs.h"
#include "script/props.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
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
            pat.hasRoute = true; pat.routeAdvanced = true;
            const auto s4b = omk::shootGenericStep(q4b, pat, e4b);
            std::printf("generic: patrol noroute outcome %d state %d; "
                        "route state %d outcome %d unread %d\n",
                        int(s4.outcome), q4.state, q4b.state, int(s4b.outcome),
                        int(s4b.outcomeFromUnread));
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

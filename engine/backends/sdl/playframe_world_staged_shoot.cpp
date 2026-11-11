// SPDX-License-Identifier: GPL-3.0-or-later
// A STAGED BODY IN SHOOT MODE: the gunman's tick - his brain, his move, his
// fire, his clip - and Astaroth's and Gandhar's, which are their own. Part of
// `PlayState::worldStaged` (`playframe_world_staged.cpp`), split out along its
// own banner by `todo/code-readability-audit.md` §6: the code is the same,
// in the same order; what it read from the enclosing loop arrives as
// arguments. Out: the shoot tracks the body poses from, their frame, and
// Astaroth's grid when the body is his.
#include "playframe.h"
#include "platform/env.h"
#include "o3de/vecmath.h"

// Astaroth's node as this frame's walk: the body follows the grid's node
static void astPlace(Staged& s, const omk::AstarothActor& aa) {
    for (int k = 0; k < 3; ++k) s.walkMove[k] = aa.node[k] - s.at[k];
}

// SHOOT_ACTORACTION (05_sys.c 3938): an action -
// his scene action at birth, what the brain asks for, or a
// request parked under flag 8 - becomes his CURRENT clip's type,
// a state, flags and a timer (`omk::shootActorAction`)
void PlayState::shootApplyAction(Staged& s, omk::ShootRecord& ar, int action, int a3,
                                 const char* why) {
    auto& session = *session_;
    const int grpX = static_cast<int>(session.typeOfActor(s.actor));
    const auto o = omk::shootActorAction(
        ar, action, a3,
        [&](int t) {
            return grpX >= 0 && grpX < 64 && shootClipExact(grpX, t) != nullptr;
        },
        [&]() {
            gun.randSeed = gun.randSeed * 214013u + 2531011u;
            return static_cast<int>((gun.randSeed >> 16) & 0x7FFFu);
        },
        // THE PATROL's route (`sub_4354E0` + `sub_4356B0`,
        // `todo/shoot-patrol.md`): his own floor's list, by
        // id or the nearest free one, and its first point as
        // a world point. The uint8_t cast is the ENGINE's
        // and is done in `shootActorAction`.
        [&](int id) {
            omk::ShootRouteChoice c;
            if (!shootMap.valid()) return c;
            const int fl = static_cast<signed char>(ar.node & 0xFF);
            if (fl < 0) return c;
            c.route = shootMap.routeFor(fl, ar.destX, ar.destZ, id);
            if (c.route >= 0)
                shootMap.routePoint(fl, c.route, 0, c.x, c.z);
            return c;
        });
    // `sub_435650`: a patrol replaced hands its route back
    if (o.releasedRoute >= 0 && shootMap.valid())
        shootMap.routeRelease(static_cast<signed char>(ar.node & 0xFF),
                              o.releasedRoute);
    // how many points his route has, or 0 - guarded, because
    // his floor can be -1 and his route index cannot index it
    const auto routeLen = [&](const omk::ShootRecord& r) {
        if (r.route < 0 || !shootMap.valid()) return 0;
        const int f = static_cast<signed char>(r.node & 0xFF);
        if (f < 0 || f >= static_cast<int>(shootMap.floors().size())) return 0;
        const auto& wl = shootMap.floors()[static_cast<std::size_t>(f)].waypoints;
        if (r.route >= static_cast<int>(wl.size())) return 0;
        return static_cast<int>(wl[static_cast<std::size_t>(r.route)].len);
    };
    if (o.clipType == 9 && ar.state == 4) {
        static ToldOnce<int> patrolTold;
        if (patrolTold.first(s.actor, session_->areasEntered()))
            std::printf("frame %ld: actor %d %s - PATROLS (Shoot_ActorAction 1): "
                        "operand %d -> route %d on floor %d, point 0 at %.0f %.0f, "
                        "%d points%s\n", frameNo, s.actor, s.model.c_str(), a3,
                        ar.route, static_cast<signed char>(ar.node & 0xFF),
                        double(ar.goalX), double(ar.goalZ),
                        routeLen(ar),
                        ar.route < 0 ? " - NO ROUTE on his floor" : "");
    }
    if (o.turnAround) s.facing += 180.0f;
    if (o.timerProperty >= 0) {
        std::int32_t v = 0;
        session.actorProperty(s.actor, o.timerProperty, v);
        ar.timer = static_cast<float>(30 * v);
    }
    if (o.clipType >= 0) {
        gun.curType[s.actor] = o.clipType;
        gun.curSlot.erase(s.actor);
        gun.anims[s.actor].clip = nullptr;   // `sub_421A20`: from 1.0, the same clip too
    }
    // logged when the action, the clip or the state CHANGES - a
    // request repeated every tick says nothing new
    static std::map<int, std::array<int, 3>> gunActionTold;
    const std::array<int, 3> key{action, o.clipType, ar.state};
    if (auto at = gunActionTold.find(s.actor); at == gunActionTold.end() || at->second != key) {
        gunActionTold[s.actor] = key;
        std::printf("frame %ld: actor %d %s - ACTION %d (Shoot_ActorAction, %s): %s "
                    "clip type %d, state %d, timer %.0f\n", frameNo, s.actor,
                    s.model.c_str(), action, why,
                    o.parked ? "PARKED under flag 8 -" : "", o.clipType, ar.state,
                    double(ar.timer));
    }
}

void PlayState::worldStagedShoot(Staged& s, const omk::NodeTracks*& shootTracks, int& shootFrame,
                                 const omk::AstarothActor*& astGrid) {
    auto& session = *session_;
    const int act = session.shootAction(s.actor);
    // IN SHOOT MODE, whatever his action - an enter with none yet is
    // `kEnteredOnly` (-2), which `act >= 0` would read as "not in"
    // (todo/drift-audit.md S13)
    const bool inShoot = session.shootIn(s.actor);
    // ---- THE GENERIC BRAIN, ticked (todo/shoot-mode.md 7d)
    //
    // `Shoot_TickNpc` calls the arm `Shoot_ActorEnter` chose;
    // for 302 of the 306 shipped sites that is `sub_424DE0`,
    // now transcribed whole (7c). The record is built once,
    // out of the CHARACTER's own six properties, exactly as
    // `sub_422540` does.
    //
    // WHAT IS SUPPLIED RATHER THAN COMPUTED, and it is the
    // honest limit of this wiring: `sub_421020` (unread),
    // `sub_421CD0` (unread) and `sub_435900`'s "am I there
    // yet" all arrive as false, and no route or nav edge is
    // handed over because the viewer has no path-finder on
    // the grid yet. So the machine RUNS - it acquires, turns,
    // engages and disengages on the real distances - and it
    // does not yet WALK. A gunman aims at the player and
    // holds his ground.
    // a gunman a bolt KILLED thinks no more (`+160 & 8`)
    // - and DEAD is flag 8 WITH no health left. The engine's
    // flag 8 is "a picked clip is playing" (`sub_421A20` raises
    // it, `sub_421770` drops it at the clip's end); the death
    // clip is one such clip, and a gunman on a TURN clip carries
    // it alive.
    // THE WEAPON SLOT TIMERS (`actor+148..+160`, what `sub_44CDF0`
    // refuses a shot on while above 0): `Actors_TickAll` (0x4681C0,
    // the loop at 0x468221) takes the frame delta off all four for
    // every actor it ticks, every frame (without it, harmless for
    // Astaroth, whose reload reads 0)
    if (auto st = actorSlotTimer.find(s.actor); st != actorSlotTimer.end())
        for (float& t : st->second) t -= static_cast<float>(frameSec * 30.0);
    const auto deadIt = shootBrains.find(s.actor);
    const bool astarothRec = deadIt != shootBrains.end() &&
        deadIt->second.type == static_cast<std::uint32_t>(omk::kAstarothType);
    // GANDHAR's brain is his own too (`sub_47F6F0`, `todo/gandhar.md`):
    // he is never "shot dead" by the generic arm - his brain posts
    // message 3 itself - and no request moves his record
    const bool gandharRec = deadIt != shootBrains.end() &&
        deadIt->second.type == static_cast<std::uint32_t>(omk::kGandharType);
    // (Astaroth is never "shot dead" here: he has no death clip, and
    // his own prologue plays the killing hit's clip out and reports
    // it - `astarothClipOver`)
    const bool shotDead = deadIt != shootBrains.end() && !astarothRec && !gandharRec &&
                          (deadIt->second.flags & 8u) && deadIt->second.health <= 0;
    // THE OCCUPANCY, put back (`sub_424DE0`'s prologue, 05_sys.c
    // 5456): the byte his 0x80 stamp covered returns to his cell
    // before he thinks - so his own wall test and move see the
    // floor - and once he is dead nothing stamps it again
    if (deadIt != shootBrains.end() && deadIt->second.cellStamped && shootMap.valid()) {
        omk::ShootRecord& sr = deadIt->second;
        shootMap.setCell(static_cast<signed char>(sr.node & 0xFF), sr.destX, sr.destZ,
                         sr.cellSaved);
        sr.cellStamped = false;
    }
    // A REQUEST AFTER HIS ENTRY (todo/drift-audit.md S13): op 84 run
    // by a later script, a hit's reaction (`sub_423EF0`) or a noise
    // heard (`sub_4246E0`) - each `Shoot_ActorAction` called AT ONCE in
    // the engine, here on his next tick, before his brain, through the
    // same `shootApplyAction` (which PARKS it under flag 8). Applying
    // only the action standing at his entry would drop a later op
    // 84 (8 shipped sites, 4 of them after a wait in the entering
    // script), every hit reaction and every noise alert. Not Astaroth's: his record is
    // driven by his own tick, and nothing shipped asks him for one.
    if (deadIt != shootBrains.end() && !astarothRec && !gandharRec && inShoot && act >= 0) {
        const int ser = session.shootActionSerial(s.actor);
        auto seen = gun.actSerialSeen.find(s.actor);
        if (seen != gun.actSerialSeen.end() && seen->second != ser) {
            seen->second = ser;
            shootApplyAction(s, deadIt->second, act, session.shootActionArg(s.actor),
                        "a request after his entry (op 84, a hit or a noise)");
        }
    }
    // THE STAND-DOWN (`sub_423FC0`, the player's death): action 0 -
    // or, on script step 8, his 0x20 latch cleared - told in the
    // hit and applied here, on his own tick
    if (deadIt != shootBrains.end() && gun.standDown.erase(s.actor)) {
        omk::ShootRecord& sr = deadIt->second;
        if (sr.scriptStep == 8) sr.flags &= ~0x20u;
        else if (!(sr.flags & 0x4000u))
            shootApplyAction(s, sr, 0, 0, "the player's death (sub_423FC0)");
    }
    // ---- THE PICKED CLIP'S TICK: `sub_424DE0`'s prologue ----
    //   if (flags & 8) { if (sub_421770(him, rec, ..)) return; ... }
    // `sub_421770` (0x00421770): his frame `+188 += dt`, and
    // `+420 += +184 * dt` wrapped into 0..360; while the frame is
    // short of the clip's length the WHOLE BRAIN returns there -
    // no arm, no fire. At the end it drops flag 8 and the brain
    // goes on the same tick, `sub_421A20(+12, 0)` putting his
    // previous clip back. Not ported: the clip's root motion and
    // the `+460` interrupt arm.
    // no aim layer unless the fire gate runs him this tick
    if (auto gaL = gun.aims.find(s.actor); gaL != gun.aims.end()) gaL->second.live = false;
    bool clipHolds = false;
    // `Shoot_TickNpc` calls a gunman's brain callback - whose
    // prologue this clip tick is, whose body is the block below and
    // whose tail is the cell stamp after it - only `if
    // (!g_PlayerBehaviourOff)`: while the player is suspended
    // (op 116) no gunman thinks (todo/drift-audit.md S7)
    const bool brainsOff = session.shootMode().playerOff();
    // ASTAROTH's head (`sub_4800C0` 0x480150..): the health bands set
    // his ANIMATION RATE, `flt_6A062C`, before anything advances -
    // the picked clip below included (`sub_421770(.., flt_6A062C)`)
    omk::AstarothBand astBand;
    const float astDt = static_cast<float>(frameSec * 30.0);
    const bool astTicks = astarothRec && inShoot && shootMode && !brainsOff;
    // the node his functions moved, handed to the draw as `walkMove`
    // (the draw seats his pelvis at `at + walkMove`; `at[1]` is +60)
    if (astTicks) {
        omk::ShootRecord& ar = deadIt->second;
        omk::AstarothActor& aa = astarothActors[s.actor];
        // `rec+168 -= flt_4C30D8`; `Hud_DrawBar(+92, 200, 1, 0)`; the
        // yaw wrap (`< 0` and `> 359`); `actor+248` re-read from the node
        ar.timer -= astDt;
        astarothBar = ar.health;
        if (s.facing < 0.0f) s.facing += 360.0f;
        if (s.facing > 359.0f) s.facing -= 360.0f;
        if (aa.started) aa.pos[1] = aa.node[1];
        astBand = omk::astarothBand(astaroth, ar.health, astDt);
    }
    worldStagedShootClipTick(inShoot, shotDead, brainsOff, deadIt, s, astarothRec, astBand, clipHolds, astDt, gandharRec);
    if (inShoot && shootMode && !shotDead && !clipHolds && !brainsOff) {
        auto it = shootBrains.find(s.actor);
        worldStagedShootEnter(it, s, act);
        omk::ShootRecord& rec = it->second;
        // ---- THE BRAIN'S PROLOGUE, `sub_424DE0` 5456 -------
        // His cell's own byte goes back before anything reads
        // the grid: `if (state != 2 || health < 0) { saved =
        // +189; sub_47C230(floor, saved); sub_435970(floor,
        // +136, +140, saved); }`. Without it a walker's 0x80
        // stays on the cell he left and blocks it for ever -
        // invisible while no gunman moves. (`sub_47C230`,
        // the door arm, is still not ported.)
        if (shootMap.valid() && rec.cellStamped &&
            (rec.state != 2 || rec.health < 0)) {
            const int fl = static_cast<signed char>(rec.node & 0xFF);
            if (fl >= 0) shootMap.setCell(fl, rec.destX, rec.destZ, rec.cellSaved);
            rec.cellStamped = false;
        }
        // ---- `Shoot_Think` (`actor/shoot.h`) ---------------
        // His FLOOR at +188 and, unless he is traversing or
        // standing on a blocked cell, his CELL at +136/+140.
        // The memset's 0 would be right by luck on the two
        // one-floor arenas this port can reach and wrong on the
        // other eleven.
        //
        // HIS POSITION IS `s.drawAt`, the same point the brain
        // below thinks from, and that is a LABELLED LIMIT OF
        // THIS PORT rather than the engine's rule: the engine
        // asks `Actor_GetPosAndFacing`, which is the node's
        // position and always answers, while `drawAt` is
        // written only where the body is DRAWN. So a gunman the
        // camera never sees - the gallery's 240 stands behind
        // the player - has no position, gets floor -1 from
        // `Shoot_Think`, and `sub_426E00`'s own first line
        // (`if (+188 == -1) return 0`) then keeps him from
        // engaging. Feeding him his PLACEMENT instead makes it
        // worse, not better: he
        // engages and FIRES from a body that has no pose, so
        // the muzzle - the posed `tir` node - is the world
        // origin. The two have to agree, and agreeing on the
        // drawn point is the conservative half. Closing it
        // properly means posing a staged body whether or not it
        // is drawn, which is not this task's.
        // (ASTAROTH: `Shoot_Think` is his ENTRY's only - his tick
        // keeps +136/+140 itself (`sub_435770` after every move) -
        // so once his stand grid has begun it is not run for him)
        const bool astThinks = !(rec.type == static_cast<std::uint32_t>(omk::kAstarothType) &&
                                 astarothActors[s.actor].started);
        worldStagedShootEnterProps(astThinks, s, rec, act);
        // ---- ASTAROTH's tick is his own (`sub_4800C0`, step 3 of
        // `todo/astaroth.md`), not `sub_424DE0`: the generic brain
        // below is NOT his. Until his is ported he stands where his
        // setup put him and does nothing. LABELLED.
        worldStagedShootBrain(rec, s, astDt, astBand, act);
    }
    // ...and after his tick his cell is STAMPED 0x80 (`sub_420B80`,
    // and `sub_421770`'s tail while a picked clip plays): every
    // other gunman's wall test and the path field refuse it, so
    // the gunmen walk around one another. NOT PORTED, labelled:
    // the door arm `sub_47C1B0` on a 0x10 cell and the byte-1
    // memo at rec+72/76.
    // (the `!cellStamped` gate is gone with the prologue's
    // restore above: `sub_420B80` stamps EVERY tick, on the
    // cell `Shoot_Think` has just found, which is the half of
    // the cycle that makes a moving gunman's occupancy follow
    // him instead of staying where he started)
    if (auto sb = shootBrains.find(s.actor);
        shootMode && !shotDead && !brainsOff && sb != shootBrains.end() && shootMap.valid() &&
        sb->second.state != 2 && !sb->second.cellStamped && !astarothRec) {
        omk::ShootRecord& sr = sb->second;
        const int fl = static_cast<signed char>(sr.node & 0xFF);
        if (fl >= 0 && fl < static_cast<int>(shootMap.floors().size())) {
            sr.cellSaved = shootMap.floors()[static_cast<std::size_t>(fl)]
                               .cell(sr.destX, sr.destZ);
            shootMap.setCell(fl, sr.destX, sr.destZ, omk::Map2d::kOccupied);
            sr.cellStamped = true;
        }
    }
    if (inShoot) {
        const int grp = static_cast<int>(session.typeOfActor(s.actor));
        // the clip his last ACTION started, as the clock above uses
        const auto ctP = gun.curType.find(s.actor);
        const omk::PedClip* c = (grp >= 0 && grp < 64)
            ? (ctP != gun.curType.end() ? shootClipExact(grp, ctP->second)
                                       : shootClipFor(grp, act))
            : nullptr;
        // KILLED: the death clip `sub_4240E0` picked by TYPE
        // from the hit's band, played once and then held
        if (s.deathType >= 0 && grp >= 0 && grp < 64)
            if (const omk::PedClip* dc = shootClipOfType(grp, s.deathType)) {
                c = dc;
                s.deathClip = dc;
            }
        // ON A TURN CLIP: the picked clip, at his frame `+188`
        const auto gcIt = gun.clips.find(s.actor);
        const bool onTurn = gcIt != gun.clips.end() && gcIt->second.type >= 0 &&
                            s.deathType < 0 && grp >= 0 && grp < 64;
        if (onTurn)
            if (const omk::PedClip* tc = gcIt->second.slot >= 0
                    ? shootClipBySlot(grp, gcIt->second.slot)
                    : shootClipExact(grp, gcIt->second.type)) c = tc;
        // ASTAROTH's current clip is his own (`AstarothActor`): by ID,
        // at KEY `(int)+188` (the grids' `sub_4725B0` and the plain
        // clips' `Anim_SetFrame` both index the key array with the
        // truncated frame; this tree's tracks hold key k at k - 1)
        const auto astIt = grp == omk::kAstarothType ? astarothActors.find(s.actor)
                                                     : astarothActors.end();
        const bool astOwn = astIt != astarothActors.end() && astIt->second.started &&
                            !onTurn && s.deathType < 0;
        if (astOwn)
            if (const omk::PedClip* ac = shootClipBySlot(grp, astIt->second.clip)) c = ac;
        // ...and GANDHAR's (`GandharActor`, `todo/gandhar.md`) the same
        // way: his action's clip by slot at key `(int)+188`. An action
        // with no clip of its own (16, 17, 18) holds the last one.
        const auto gdIt = grp == omk::kGandharType ? gandharActors.find(s.actor)
                                                   : gandharActors.end();
        const bool gaOwn = gdIt != gandharActors.end() && gdIt->second.started &&
                           gdIt->second.clip >= 0 && !onTurn && s.deathType < 0;
        if (gaOwn)
            if (const omk::PedClip* gcl = shootClipBySlot(grp, gdIt->second.clip)) c = gcl;
        if (c) shootTracks = pedTracksFor(grp, *c, s.mo->meshes, s.model);
        if (gaOwn && shootTracks && shootTracks->frames > 0)
            shootFrame = std::clamp(static_cast<int>(gdIt->second.frame) - 1, 0,
                                    shootTracks->frames - 1);
        if (astOwn && shootTracks && shootTracks->frames > 0) {
            shootFrame = std::clamp(static_cast<int>(astIt->second.frame) - 1, 0,
                                    shootTracks->frames - 1);
            if (astIt->second.grid) astGrid = &astIt->second;
        }
        if (onTurn && shootTracks && shootTracks->frames > 0)
            shootFrame = std::min(static_cast<int>(gcIt->second.frame),
                                  static_cast<int>(shootTracks->frames) - 1);
        // ...and otherwise his CURRENT clip at the frame
        // `sub_421370` has advanced it to (above) - the same clip
        // `shootClipFor` just gave `c`, since both ask it alike
        const auto gaIt = gun.anims.find(s.actor);
        if (!astOwn && !gaOwn && !onTurn && s.deathType < 0 && gaIt != gun.anims.end() &&
            gaIt->second.clip == c && shootTracks && shootTracks->frames > 0)
            shootFrame = std::min(static_cast<int>(gaIt->second.frame),
                                  static_cast<int>(shootTracks->frames) - 1);
        if (s.deathType >= 0 && shootTracks && shootTracks->frames > 0)
            shootFrame = static_cast<int>(std::min<long>(
                static_cast<long>(gameClock - s.deathStart),
                static_cast<long>(shootTracks->frames) - 1));
        // HIS DEATH IS REPORTED - message 3. `Shoot_TickNpc`
        // calls the brain on a dead gunman too, and the generic
        // brain's first arm (`sub_424DE0`, flag 8 up) plays the
        // reaction clip through `sub_421770` and, once it has
        // played out with his health at 0, does
        // `Game_RaiseEvent(43, {3, him})` - which
        // `Message_RunHandlers` hands the resident scene's
        // subscriptions with his actor id as the sender. SCENE
        // 56's table entry 1 is where the supermarket keeps its
        // score: `Braqueur N Dead` per robber, and for actor 84
        // `zone.enable 3931` - the zone whose script runs
        // `shoot.end` and the end cutscene. Without this the
        // phase cannot be finished.
        // ONCE, as the engine does: the frame the clip plays out,
        // `sub_421770` clears flag 8 as it returns 0, so the next
        // tick takes the other dead arm instead - the tidy one
        // that puts him in ACTOR_STATE 0 - and posts nothing.
        // (not GANDHAR's: his own brain posts it, `sub_47F6F0`)
        if (s.deathType >= 0 && !s.deathPosted && shootTracks &&
            grp != omk::kGandharType && shootTracks->frames > 0 &&
            static_cast<long>(gameClock - s.deathStart) >=
                static_cast<long>(shootTracks->frames) - 1) {
            s.deathPosted = true;
            const bool ran = session.postMessage(3, s.actor);
            const auto& mr = session.messagesRun();
            std::printf("frame %ld: actor %d %s - death clip over: message 3 "
                        "(Game_RaiseEvent 43) %s", frameNo, s.actor, s.model.c_str(),
                        ran ? "- handler " : "- NO handler subscribes\n");
            if (ran && !mr.empty())
                std::printf("%s +0x%zx\n", mr.back().table.c_str(),
                            mr.back().offset);
        }
        if (!s.shootTold && shootTracks) {
            // THE INSTRUMENT omk-play 96 asked for: a staged
            // actor never reported whether its clip resolved
            // against its own model, so a body could blow
            // apart while every line of the log read healthy.
            // The unit-quaternion count is the discriminator -
            // all 243362 quaternions in the shipped `.ani`
            // corpus are unit, so a non-unit one here means
            // the track offsets are being read against the
            // WRONG BLOB, not that the pose maths is wrong.
            int bound = 0;
            for (auto id : shootTracks->ids) if (id >= 0) ++bound;
            int nonUnit = 0;
            for (const auto& row : shootTracks->quats)
                for (const auto& q : row) {
                    const double m = double(q.x) * q.x + double(q.y) * q.y
                                   + double(q.z) * q.z + double(q.w) * q.w;
                    if (m < 0.98 || m > 1.02) ++nonUnit;
                }
            std::printf("frame %ld: actor %d %s - %d/%zu tracks resolve "
                        "against %zu meshes, %d frames, %d non-unit "
                        "quaternions\n", frameNo, s.actor, s.model.c_str(),
                        bound, shootTracks->ids.size(),
                        s.mo->meshes.size(), shootTracks->frames, nonUnit);
            if (bound == 0) {
                const auto dd = omk::animDescriptor(crowd.ani, c->descriptor);
                std::printf("  clip tracks:");
                if (dd) for (std::size_t q = 0; q < dd->tracks.size() && q < 6; ++q)
                    std::printf(" %s", dd->tracks[q].name.c_str());
                std::printf("\n  model meshes:");
                for (std::size_t q = 0; q < s.mo->meshes.size() && q < 6; ++q)
                    std::printf(" %s", s.mo->meshes[q].name);
                std::printf("\n");
            }
        }
        if (!s.shootTold) {
            s.shootTold = true;
            std::printf("frame %ld: actor %d %s - shoot mode, action %d, "
                        "character type %d -> %s (%s, %zu clips in the "
                        "group)\n", frameNo, s.actor, s.model.c_str(), act, grp,
                        c ? "a clip" : "NO CLIP",
                        crowd.aniName.empty() ? "no .ani loaded"
                                           : crowd.aniName.c_str(),
                        shootClips.count(grp) ? shootClips[grp].size() : 0u);
        }
    }
}

// A GUNMAN'S FIRST TICK: his record built, `Shoot_ActorEnter`, Astaroth's and Gandhar's type arms (from `worldStagedShoot`)
void PlayState::worldStagedShootEnter(std::map<int, omk::ShootRecord>::iterator& it, Staged& s,
                                      int act) {
    auto& session = *session_;
    if (it == shootBrains.end()) {
        // a FRESH record: whatever an earlier attempt left on his
        // body - a death clip, its fall - is gone with the old one
        s.deathType = -1;
        s.deathClip = nullptr;
        s.deathPosted = false;
        s.deathFallTold = false;
        s.deathFloorTold = false;
        s.deathEl = 0;
        s.deathWalls = 0;
        for (float& d : s.deathMove) d = 0.0f;
        omk::ShootRecord fresh;
        std::int32_t props[6] = {0, 0, 0, 0, 0, 0};
        omk::ShootProperties sp;
        if (session.actorShootProperties(s.actor, props)) {
            sp.health        = props[0];
            sp.rangeAcquireM = props[1];
            sp.rangeInnerM   = props[2];
            sp.rangeThirdM   = props[3];
            sp.coneDegrees   = props[4];
            sp.behaviourBits = props[5];
        }
        omk::initShootRecord(fresh, sp, session.shootMode().frozen());
        // +80, the character type - the hit's gates test it
        // (type 11 takes the baton, 12 never reacts)
        fresh.type = session.typeOfActor(s.actor);
        // +112..+124: the slots of his group's TYPE-12 clips,
        // in list order (`sub_4347A0` / `sub_434860`), up to four
        if (!crowd.ani.empty() && fresh.type < 64u) {
            int k = 0;
            for (const auto& c : omk::animGroupClips(crowd.ani, static_cast<int>(fresh.type))) {
                if (!c.slot) break;
                if (c.type == 12 && k < 4) fresh.attacks[k++] = c.slot;
            }
            if (k)
                std::printf("frame %ld: actor %d %s - ATTACKS (Shoot_ActorEnter +112): "
                            "%d type-12 clip%s, first slot %d\n", frameNo, s.actor,
                            s.model.c_str(), k, k == 1 ? "" : "s", fresh.attacks[0]);
        }
        fresh.state = 6;          // the hub, where a
        fresh.node  = 0;          // gunman waits
        // (+188 is his FLOOR, a signed byte: `Shoot_Enter`
        // memsets the records, so a gunman's is 0 until
        // `Shoot_Think` - not wired - writes his
        // `sub_435020`; only the PLAYER's is -1, and at -1
        // `shootEngage` would refuse at once. The noise
        // reads it as his floor.)
        // his heading carries over from how he is DRAWN: an
        // entrance program left the node at its rest yaw, and
        // the engine's +420 IS that node's heading - so the
        // brain starts there rather than at the placement's
        if (s.restYawKnown) s.facing = s.restYaw;
        // ---- ASTAROTH: `sub_47DFD0`'s type-13 arm -----------
        // `Shoot_ActorEnter` runs NO action: it hands the type
        // to `sub_47DFD0`, whose arm for 13 is `sub_47FF70`
        // (`actor/astaroth.h`) - state 29, +88 = 10, 0x4020, the
        // world-hit callback, his `AstDos` mesh and the six
        // souls found by name in the scene and SHOWN
        // (`sub_436F50`). `o3de_FindNodeByName` walks the world
        // root, i.e. the LINKED sets, so the shown slots are
        // searched, the active one first.
        const bool astarothHere = fresh.type == static_cast<std::uint32_t>(omk::kAstarothType);
        const bool gandharHere = fresh.type == static_cast<std::uint32_t>(omk::kGandharType);
        const bool bossHere = astarothHere || gandharHere;
        if (astarothHere) {
            int backMesh = -1;
            if (s.mo)
                for (std::size_t mi = 0; mi < s.mo->meshes.size(); ++mi)
                    if (std::strcmp(s.mo->meshes[mi].name, "AstDos") == 0) {
                        backMesh = static_cast<int>(mi);
                        break;
                    }
            int soulSlot[omk::kAstarothSouls];
            const auto findSoul = [&](const char* name) {
                for (int pass = 0; pass < 2; ++pass)
                    for (int k = 0; k < 2; ++k) {
                        const WorldSlot& w = worldSlots[static_cast<std::size_t>(k)];
                        if (!w.shown || (pass == 0) != (w.stem == worldSet)) continue;
                        for (std::size_t mi = 0; mi < w.meshes.size(); ++mi)
                            if (std::strcmp(w.meshes[mi].name, name) == 0) {
                                for (int i = 0; i < omk::kAstarothSouls; ++i)
                                    if (std::strcmp(omk::kAstarothSoulNames[i], name) == 0)
                                        soulSlot[i] = k;
                                return static_cast<int>(mi);
                            }
                    }
                return -1;
            };
            for (int& k : soulSlot) k = -1;
            omk::astarothSetup(astaroth, fresh, findSoul, backMesh,
                               settings.v.shootDifficulty);
            int shown = 0;
            for (int i = 0; i < omk::kAstarothSouls; ++i) {
                astarothSoulSlot[i] = soulSlot[i];
                if (astaroth.soulMesh[i] < 0 || soulSlot[i] < 0) continue;
                WorldSlot& w = worldSlots[static_cast<std::size_t>(soulSlot[i])];
                const auto mi = static_cast<std::size_t>(astaroth.soulMesh[i]);
                if (mi < w.meshHidden.size()) w.meshHidden[mi] = 0;   // sub_436F50
                ++shown;
            }
            // `sub_4B2E90` - the STAND grid - waits for his first
            // tick on the grid, which is when this port knows his
            // `+60` (the generic entry's action waits the same way)
            astarothActors[s.actor] = omk::AstarothActor{};
            // HIS NODE IS HIS PELVIS (the root mesh's world point),
            // as Gandhar's is (`todo/gandhar.md` 3b): drawn
            // pelvis-on-node, not seated by his lowest posed corner
            // - which floats him (`todo/astaroth.md` 4)
            s.pelvis = true;
            // (the TEST HARNESS `--astaroth-health N`, an instrument)
            if (astarothHealth >= 0) {
                fresh.health = astarothHealth;
                std::printf("frame %ld: ASTAROTH HEALTH - the test harness "
                            "--astaroth-health writes +92 = %d\n", frameNo, astarothHealth);
            }
            actorSlotTimer[s.actor] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (s.mo) {
                int tire[4];
                omk::astarothTireSlots(s.mo->meshes, tire);
                std::printf("frame %ld: actor %d %s - weapon slots (sub_436DD0 "
                            "\"Tire\"): %s, %s\n", frameNo, s.actor, s.model.c_str(),
                            tire[0] >= 0 ? s.mo->meshes[static_cast<std::size_t>(tire[0])].name : "-",
                            tire[1] >= 0 ? s.mo->meshes[static_cast<std::size_t>(tire[1])].name : "-");
            }
            std::printf("frame %ld: actor %d %s - ASTAROTH SETUP (sub_47FF70): state "
                        "%d, +88 %d, flags 0x%x, AstDos mesh %d, %d of %d souls found "
                        "and shown at %d hits, difficulty %.1f, the world-hit "
                        "callback armed\n", frameNo, s.actor, s.model.c_str(), fresh.state,
                        fresh.actionCounter, static_cast<unsigned>(fresh.flags), backMesh, shown,
                        omk::kAstarothSouls, omk::kAstarothSoulHits,
                        double(astaroth.difficulty));
        }
        // `Shoot_Think` FIRST, exactly as `Shoot_ActorEnter`
        // runs it (05_sys.c 3741) before the scene action:
        // his floor at +188 and his cell at +136/+140. The
        // patrol needs both, since `sub_4354E0` is handed
        // them - so an entry that skipped this would look up
        // a route on floor -1 and never find one.
        bool onGridAtEntry = false;
        if (shootMap.valid()) {
            const float at[3] = {s.drawAt[0], s.drawAt[1], s.drawAt[2]};
            onGridAtEntry = omk::shootThink(fresh, shootMap, at,
                                            static_cast<int>(fresh.type), -1);
        }
        it = shootBrains.emplace(s.actor, fresh).first;
        // ---- GANDHAR: `sub_47DFD0`'s type-10 arm (`todo/gandhar.md`)
        // - his node to y -147, +68 = 39 * property 3 / 30, and
        // action 23 entered at once; no scene action, no default arm.
        // (`sub_421140` on his spot and `sub_4368E0`'s nearest free
        // cell when it is blocked: not run - LABELLED, step 2)
        if (gandharHere) {
            if (!shootTablesLoaded) {
                shootTablesLoaded = true;
                if (!shootTables.loadJson(tb + "/shoot_ai.json"))
                    std::printf("frame %ld: GANDHAR - tables/shoot_ai.json did not "
                                "load: his scripts are empty\n", frameNo);
            }
            omk::GandharActor& ga = gandharActors[s.actor];
            ga = omk::GandharActor{};
            // (the TEST HARNESS `--gandhar-health N`, an instrument)
            if (gandharHealth >= 0) {
                it->second.health = gandharHealth;
                std::printf("frame %ld: GANDHAR HEALTH - the test harness "
                            "--gandhar-health writes +92 = %d\n", frameNo, gandharHealth);
            }
            std::int32_t p3 = 0;
            session.actorProperty(s.actor, 3, p3);
            // `+64` FIRST, as `Shoot_ActorEnter` computes it for
            // every type before `sub_47DFD0` overrides `+60`: his
            // RISE and SINK end on `node.y - +64`
            it->second.height = session.modelFeetDrop(s.model);
            const float pos[3] = {s.at[0], s.at[1], s.at[2]};
            const omk::GandharWorld gw =
                gandharWorld(s, it->second, static_cast<float>(frameSec * 30.0));
            omk::gandharEnter(ga, it->second, pos, p3, gw);
            for (int k = 0; k < 3; ++k) s.walkMove[k] = ga.node[k] - s.at[k];
            // HIS NODE IS HIS PELVIS - the root mesh's world point
            // - so he is drawn pelvis-on-node, as a body an authored
            // path places is. Seated by his FEET, as a placement
            // record's body is, his lowest point - the tip of a
            // tail 280 below the pelvis - took the floor's height
            // and the node's offset went on top: his head drew
            // above the cave's ceiling, out of every bolt's reach
            s.pelvis = true;
            std::printf("frame %ld: actor %d %s - GANDHAR ENTERS (sub_47DFD0): node y "
                        "%.0f (+60 %.0f, +64 %.1f), speed %.1f (property 3 = %d), "
                        "state %d, clip %d, health %d, scripts %zu/%zu/%zu\n", frameNo,
                        s.actor, s.model.c_str(), double(ga.node[1]),
                        double(it->second.groundY), double(it->second.height),
                        double(ga.speed),
                        int(p3), it->second.state, ga.clip, it->second.health,
                        shootTables.healthy.size(), shootTables.wounded.size(),
                        shootTables.critical.size());
        }
        // `Shoot_ActorEnter` thinks FIRST and acts second, and
        // the order is load-bearing for the patrol: the route
        // lookup takes his floor and cell. This port cannot
        // always honour it on the entry tick - a body staged
        // this frame has not been drawn, so it has no position
        // (`todo/shoot-patrol.md` 4b) - so the action WAITS for
        // the first tick he lands on the grid. One frame, and
        // the alternative is a patrol with a null route.
        if (!onGridAtEntry && shootMap.valid() && !bossHere &&
            act != omk::ShootMode::kEnteredOnly) {
            gunEntryPending.insert(s.actor);
            std::printf("frame %ld: actor %d %s - entry action %d HELD: he is "
                        "not on the grid yet (Shoot_ActorEnter thinks first)\n",
                        frameNo, s.actor, s.model.c_str(), act);
        }
        // ...and put on his SCENE action, as `Shoot_ActorEnter`
        // does - for the robbers, action 3: the hub, the walking
        // clip, and +168 = 30 * property 31 frames of advance
        // (LABELLED: `Shoot_ActorEnter`'s own call is not re-read;
        // this passes the action with a3 = 0)
        // the requests made so far are what the entry applies
        gun.actSerialSeen[s.actor] = session.shootActionSerial(s.actor);
        if (act == omk::ShootMode::kEnteredOnly && !bossHere) {
            // `sub_47DFD0`'s DEFAULT ARM: `List_PickRandomByType(+20,
            // 11)` into `+8` and `sub_421A20` - his type-11 clip -
            // with `+156` left at the enter's 0. No action: the
            // generic brain has no arm for state 0, so he stands in
            // that clip until a request moves him (the fixed pick,
            // as every gunman's here)
            it->second.state = 0;
            gun.curType[s.actor] = 11;
            gun.curSlot.erase(s.actor);
            gun.anims[s.actor].clip = nullptr;
            gunEntryPending.erase(s.actor);
            std::printf("frame %ld: actor %d %s - ENTERED WITH NO ACTION "
                        "(sub_47DFD0's default arm): clip type 11, state 0\n",
                        frameNo, s.actor, s.model.c_str());
        } else if ((onGridAtEntry || !shootMap.valid()) && !bossHere)
            shootApplyAction(s, it->second, act, session.shootActionArg(s.actor),
                        "his scene action, at entry");
        std::printf("frame %ld: actor %d %s - shoot brain: "
                    "acquire %.0f engage %.0f disengage %.0f "
                    "cone %.3f health %d\n", frameNo, s.actor,
                    s.model.c_str(), it->second.rangeAcquire,
                    it->second.rangeInner, it->second.rangeThird,
                    it->second.coneCos, it->second.health);
    }
}


// `Shoot_ActorEnter`'s +64 and +60, before `Shoot_Think` (from `worldStagedShoot`)
void PlayState::worldStagedShootEnterProps(bool astThinks, Staged& s, omk::ShootRecord& rec,
                                           int act) {
    auto& session = *session_;
    if (shootMap.valid() && astThinks) {
        // his y is `+60` once he has one - the engine's node
        // y for a shoot walker, which nothing moves
        const float at[3] = {s.drawAt[0],
                             rec.groundY != 0.0f ? rec.groundY : s.drawAt[1],
                             s.drawAt[2]};
        const bool onGrid =
            omk::shootThink(rec, shootMap, at,
                            static_cast<int>(rec.type), -1);
        if (onGrid) gunCellSeeded.insert(s.actor);
        // ---- `Shoot_ActorEnter`'s +64 and +60 ------------
        // `+64` is how far his lowest collision sphere hangs
        // below his origin (`Session::modelFeetDrop`); `+60`
        // is his floor's own lower edge MINUS that, which
        // stands his feet on the floor plane.
        // `o3de_SetNodePos` puts him there and
        // `sub_421370`'s clip wrap re-pins the node's y to
        // it every loop, so a shoot gunman's y NEVER MOVES -
        // the walk applies no vertical at all (its two arms
        // pass {dx, 0, dz}, and the one that does move y
        // needs `a1+460`).
        //
        // The DRAWN y drifts with the ground probe: fed it,
        // two of the catacombs' nine spectres sink three
        // quarters of a metre through their own floor by
        // frame 72, lose it, and with it the wall test -
        // one walks out through a wall.
        //
        // It is done HERE and not at his entry because the
        // entry has no floor to subtract from: a body staged
        // this tick has not been drawn.
        const int fl0 = static_cast<signed char>(rec.node & 0xFF);
        if (onGrid && rec.groundY == 0.0f && fl0 >= 0 &&
            fl0 < static_cast<int>(shootMap.floors().size())) {
            rec.height = session.modelFeetDrop(s.model);
            rec.groundY = shootMap.floors()[
                static_cast<std::size_t>(fl0)].bound[3] - rec.height;
            s.at[1] = rec.groundY;              // `o3de_SetNodePos`
            s.drawAt[1] = rec.groundY;
            std::printf("frame %ld: actor %d %s - stands at y %.0f "
                        "(Shoot_ActorEnter: floor %d's edge %.0f minus his "
                        "%.0f) and does not leave it\n", frameNo, s.actor,
                        s.model.c_str(), double(rec.groundY), fl0,
                        double(shootMap.floors()[
                            static_cast<std::size_t>(fl0)].bound[3]),
                        double(rec.height));
        }
        if (onGrid && gunEntryPending.erase(s.actor))
            shootApplyAction(s, rec, act, session.shootActionArg(s.actor),
                        "his scene action, held until he reached the grid");
        static ToldOnce<int> floorTold;
        if (onGrid && floorTold.first(s.actor, session_->areasEntered()))
            std::printf("frame %ld: actor %d %s - Shoot_Think: floor %d, cell "
                        "(%d,%d)\n", frameNo, s.actor, s.model.c_str(),
                        static_cast<signed char>(rec.node & 0xFF),
                        rec.destX, rec.destZ);
        // ...and when he LEAVES it, which is the state the
        // engine's `sub_4368E0` exists to undo: with no
        // floor there is no wall test and no steering, so a
        // walker just keeps going. Said once per actor.
        static ToldOnce<int> offGridTold;
        if (static_cast<signed char>(rec.node & 0xFF) < 0 &&
            floorTold.has(s.actor, session_->areasEntered()) && offGridTold.first(s.actor, session_->areasEntered()))
            std::printf("frame %ld: actor %d %s - OFF THE GRID at %.0f %.0f "
                        "%.0f: Shoot_Think finds no floor, so nothing walls "
                        "him in (sub_4368E0 is the engine's recovery and runs "
                        "only at his entry)\n", frameNo, s.actor, s.model.c_str(),
                        double(at[0]), double(at[1]), double(at[2]));
    }
}


// THE BRAIN, one of three: ASTAROTH's tick is his own (`sub_4800C0`), GANDHAR's
// is `sub_47F6F0`, and every other gunman runs the generic one - its steering,
// floors, patrol, edge walk, clip and fire epilogue (from `worldStagedShoot`)
void PlayState::worldStagedShootBrain(omk::ShootRecord& rec, Staged& s, float astDt,
                                         omk::AstarothBand& astBand, int act) {
    if (rec.type == static_cast<std::uint32_t>(omk::kAstarothType)) {
        worldStagedShootAstaroth(s, rec, astDt, astBand);
    } else if (rec.type == static_cast<std::uint32_t>(omk::kGandharType) &&
               gandharActors.count(s.actor)) {
        worldStagedShootGandhar(s, rec, astDt);
    } else {
    worldStagedShootGeneric(rec, s, act);
    }   // the generic brain (`sub_424DE0`) - not Astaroth's
}

// ASTAROTH's tick is his own (`sub_4800C0`): his start, then his grid and his two weapons (from `worldStagedShootBrain`)
void PlayState::worldStagedShootAstaroth(Staged& s, omk::ShootRecord& rec, float& astDt,
                                         omk::AstarothBand& astBand) {
    omk::AstarothActor& aa = astarothActors[s.actor];
    const omk::AstarothWorld aw = astarothWorld(s, rec, astDt);
    if (!aa.started) {
        if (rec.groundY != 0.0f) {
            // `Shoot_ActorEnter`: the node at (pos.x, +60,
            // pos.z), the record at his placement; then
            // `sub_4B2E90` from `sub_47FF70`
            aa.pos[0] = s.at[0]; aa.pos[1] = rec.groundY; aa.pos[2] = s.at[2];
            for (int k = 0; k < 3; ++k) aa.node[k] = aa.pos[k];
            omk::astarothStartGrid(aa, rec, 13, s.facing, aw);
            aa.started = true;
            astPlace(s, aa);
            std::printf("frame %ld: actor %d %s - ASTAROTH STANDS (sub_4B2E90): "
                        "the stand grid, %d frames, cell %d, aim %.1f / %.1f, "
                        "weights %d / %d, keys +%d +%d +%d +%d\n", frameNo, s.actor,
                        s.model.c_str(), aa.clipFrames, (aa.clipFrames + 1) / 9,
                        double(aa.aimYaw), double(aa.aimPitch),
                        aa.blend.wYaw, aa.blend.wPitch, aa.blend.off8,
                        aa.blend.off10, aa.blend.off12, aa.blend.off14);
        }
    } else {
        const int stateWas = rec.state;
        omk::astarothTick(astaroth, aa, rec, astBand, astDt, s.facing, aw);
        // the TAIL (0x480638..): a queued clip played PICKED -
        // `sub_421A20(+16)`, flag 8, `+100 = 0`, the +184 kept
        if (aa.queued >= 0 || aa.queuedTurnType >= 0) {
            const int grpQ = static_cast<int>(rec.type);
            const omk::PedClip* qc = aa.queued >= 0
                ? shootClipBySlot(grpQ, aa.queued)
                : shootClipExact(grpQ, aa.queuedTurnType);
            GunClip& gc = gun.clips[s.actor];
            gc = GunClip{};
            if (qc) {
                gc.type = qc->type;
                gc.slot = aa.queued >= 0 ? qc->slot : -1;
                gc.frames = qc->frames;
                gc.frame = 1.0f;
                // `sub_421A20` sets the node again: the
                // record's x/z, +60 and the clip's dy 0->1
                float d01[3] = {0, 0, 0};
                if (qc->root.size() >= 3)
                    omk::pedRootDelta(*qc, 0.0f, 1.0f, nullptr, d01);
                aa.node[0] = aa.pos[0];
                aa.node[1] = rec.groundY + d01[1];
                aa.node[2] = aa.pos[2];
            }
            gc.turn = aa.queuedTurn;
            rec.flags |= 8u;
            rec.repeats = 0;
            std::printf("frame %ld: actor %d %s - ASTAROTH plays clip %d (type %d, "
                        "%d frames) picked, %.2f a frame\n", frameNo, s.actor,
                        s.model.c_str(), qc ? qc->slot : -1, qc ? qc->type : -1,
                        qc ? qc->frames : 0, double(gc.turn));
        }
        astPlace(s, aa);
        if (rec.state != stateWas)
            std::printf("frame %ld: actor %d %s - ASTAROTH state %d -> %d (clip %d), "
                        "health %d, at %.0f %.0f %.0f facing %.1f\n", frameNo, s.actor,
                        s.model.c_str(), stateWas, rec.state, aa.clip, rec.health,
                        double(aa.node[0]), double(aa.node[1]),
                        double(aa.node[2]), double(s.facing));
    }
}


// GANDHAR's brain, `sub_47F6F0` (`actor/gandhar.h`): the boss bar, his action's tick and the next action of his script (from `worldStagedShootBrain`)
void PlayState::worldStagedShootGandhar(Staged& s, omk::ShootRecord& rec, float& astDt) {
    // ---- GANDHAR's brain, `sub_47F6F0` (`actor/gandhar.h`) -
    // the boss bar out of 200, then his action's tick and,
    // when it is done, the next action of his script. The
    // floor cell either side is the generic arm's, as the
    // engine's `sub_420C10` / `sub_420B80` are.
    omk::GandharActor& ga = gandharActors[s.actor];
    astarothBar = rec.health;
    const omk::GandharWorld gw = gandharWorld(s, rec, astDt);
    const int stateWas = rec.state;
    omk::gandharTick(ga, rec, shootTables, astDt, s.facing, gw);
    for (int k = 0; k < 3; ++k) s.walkMove[k] = ga.node[k] - s.at[k];
    if (rec.state != stateWas)
        std::printf("frame %ld: actor %d %s - GANDHAR action %d -> %d (clip %d, "
                    "%d frames), health %d, band %d, step %d/%d, node %.0f %.0f "
                    "%.0f facing %.1f\n", frameNo, s.actor, s.model.c_str(), stateWas,
                    rec.state, ga.clip, ga.clipFrames, rec.health, rec.band,
                    rec.scriptStep, rec.repeats, double(ga.node[0]),
                    double(ga.node[1]), double(ga.node[2]), double(s.facing));
}


// THE GENERIC BRAIN, every gunman but the two bosses: his steering, floors, patrol, edge walk, clip and fire epilogue (from `worldStagedShootBrain`)
void PlayState::worldStagedShootGeneric(omk::ShootRecord& rec, Staged& s, int& act) {
    auto& session = *session_;
    const int before = rec.state;
    omk::ShootFrameIn fin;
    // ...and the brain thinks from the SAME point (see above)
    fin.self[0] = s.drawAt[0]; fin.self[1] = s.drawAt[1];
    fin.self[2] = s.drawAt[2]; fin.self[3] = s.facing;
    if (player) {
        fin.target[0] = float(player->pos()[0]);
        fin.target[1] = float(player->pos()[1]);
        fin.target[2] = float(player->pos()[2]);
        fin.target[3] = player->facing();
    }
    // the engine's `flt_4C30D8`, as every other tick in this
    // file reads it - and 0 under the pause, which is the
    // whole of how the game freezes a gunfight (see
    // `uiPause` above). A literal 1.0 here would keep every
    // brain, clip and walk step running behind the menu.
    fin.dt = static_cast<float>(frameSec * 30.0);
    fin.defaultClipType = act < 0 ? 0 : act;   // (no action yet: 0)
    // THE STEERING (`sub_421CD0`): the hub's
    // middle arm turns him down the path field toward the
    // player. The heading, and the CRT draws the brain makes
    // itself, are handed in LAZILY - the engine reaches them
    // only on the ticks the arm runs, and the draws are the
    // same `rand()` the bolts' jitter comes from
    auto crt = [&]() {
        gun.randSeed = gun.randSeed * 214013u + 2531011u;
        return static_cast<int>((gun.randSeed >> 16) & 0x7FFFu);
    };
    fin.rand = crt;
    fin.gridHeading = [&]() {
        // (LABELLED: until his cell has come off the grid - see
        // above - he has no cell to read the field from, and
        // the port gives no heading; the engine's is written at
        // `Shoot_ActorEnter` and always there)
        if (!gunCellSeeded.count(s.actor)) return -1;
        const int h = shootField.heading(
            shootMap, static_cast<signed char>(rec.node & 0xFF), rec.destX,
            rec.destZ, crt);
        static ToldOnce<int> steerTold;
        if (h >= 0 && steerTold.first(s.actor, session_->areasEntered()))
            std::printf("frame %ld: actor %d %s - STEERS by the path field "
                        "(sub_421CD0 -> sub_435C40): heading %d from his cell "
                        "(%d,%d), facing %.1f\n", frameNo, s.actor, s.model.c_str(), h,
                        rec.destX, rec.destZ, double(s.facing));
        return h;
    };
    fin.targetAlive = true;
    // THE GRID SIGHT, `sub_4359A0(floor, targetCell, ownCell, 1)`
    // - what `sub_426E00`'s GENERAL arm acquires on
    // (`todo/shoot-sight.md` 1; not the ray). His floor `+188`, -1
    // seeing nothing; from the PLAYER RECORD's cell - kept by
    // `Shoot_TickPlayer` since step 2 - to his own. LABELLED:
    // every door counts as open, because the engine's
    // `sub_44A0F0` asks the door OBJECTS' state and the viewer
    // cannot. The gallery's walls block two of its three
    // gunmen from their placements (`map2d gallery sight`).
    bool gridSees = false;
    {
        const int gfl = static_cast<signed char>(rec.node & 0xFF);
        int bx = -1, bz = -1;
        if (shootMap.valid() && gfl >= 0)
            gridSees = shootMap.lineOfSight(gfl, playerShootRec.destX,
                                            playerShootRec.destZ,
                                            rec.destX, rec.destZ, 0xFFFF,
                                            &bx, &bz);
        static std::map<int, std::array<int, 6>> sightTold;
        const std::array<int, 6> key{gfl, rec.destX, rec.destZ,
                                     playerShootRec.destX,
                                     gridSees ? -1 : bx, gridSees ? -1 : bz};
        if (auto st = sightTold.find(s.actor); st == sightTold.end() ||
                                               st->second != key) {
            sightTold[s.actor] = key;
            if (gfl < 0)
                std::printf("frame %ld: actor %d %s - GRID SIGHT (sub_4359A0): "
                            "none - he is on no floor\n", frameNo, s.actor,
                            s.model.c_str());
            else
                std::printf("frame %ld: actor %d %s - GRID SIGHT (sub_4359A0, "
                            "target->self) from the player's (%d,%d) to his "
                            "(%d,%d) on floor %d: %s", frameNo, s.actor,
                            s.model.c_str(), playerShootRec.destX,
                            playerShootRec.destZ, rec.destX, rec.destZ, gfl,
                            gridSees ? "CLEAR\n" : "BLOCKED");
            if (gfl >= 0 && !gridSees)
                std::printf(" at (%d,%d)\n", bx, bz);
        }
    }
    fin.gridLineOfSight = gridSees;
    omk::AcquireOut ao;
    const bool cone = omk::shootAcquires(rec, fin.self,
                                         fin.target, ao, false);
    // ...and `sub_420D90`, the same cone with the range
    // doubled: the cross-floor watcher's sight
    omk::AcquireOut aoWide;
    const bool wideCone = omk::shootAcquires(rec, fin.self,
                                             fin.target, aoWide, true);
    omk::EngageIn ein;
    // ---- WHICH FLOOR EACH OF THEM IS ON -----------------
    // `sub_426E00` compares the two records' `+188`, and
    // `sameNode` was hard-coded TRUE here - so the
    // cross-floor arm, the one that sends a gunman after a
    // player upstairs, had never run
    // (`todo/shoot-navedge.md`). The player's floor is
    // `floorAt` of his PELVIS, the same point the path field
    // is seeded from: at his feet the supermarket's player
    // stands on his grid's upper bound and `floorAt` finds
    // nothing (`todo/shoot-mode.md` 8 B3).
    // ...and it is the PLAYER'S RECORD's `+188`,
    // which the player's tick has just written with his own
    // `Shoot_Think`, the engine's own comparison - not a
    // `floorAt` taken here from a second point.
    const int playerFloor = shootMap.valid()
        ? static_cast<signed char>(playerShootRec.node & 0xFF) : -1;
    const int gunFloor = static_cast<signed char>(rec.node & 0xFF);
    ein.sameNode = playerFloor < 0 || gunFloor < 0 ||
                   playerFloor == gunFloor;
    // ...and, when they differ, the nearest STAIRCASE to
    // his floor: `sub_436BB0`, ported as `Map2d::linkTo`.
    // said when either side's floor CHANGES, not once: a
    // body staged this tick has no floor yet, so a one-shot
    // line only ever reports -1
    static std::map<int, std::pair<int, int>> floorsTold;
    if (auto ft = floorsTold.find(s.actor);
        ft == floorsTold.end() ||
        ft->second != std::make_pair(gunFloor, playerFloor)) {
        floorsTold[s.actor] = {gunFloor, playerFloor};
        std::printf("frame %ld: actor %d %s - FLOORS: he %d, the player %d, "
                    "same %d, latched %d\n", frameNo, s.actor, s.model.c_str(),
                    gunFloor, playerFloor, int(ein.sameNode),
                    int((rec.flags & 0x20u) != 0));
    }
    static ToldOnce<int> crossTold;
    if (!ein.sameNode && crossTold.first(s.actor, session_->areasEntered()))
        std::printf("frame %ld: actor %d %s - the player is on ANOTHER FLOOR "
                    "(%d against his %d)%s\n", frameNo, s.actor, s.model.c_str(),
                    playerFloor, gunFloor,
                    (rec.flags & 0x20u) ? " and he has seen him: looking for "
                                          "a staircase (sub_436BB0)"
                                        : " but he has not seen him");
    if (!ein.sameNode && shootMap.valid()) {
        ein.link = shootMap.linkTo(gunFloor, playerFloor, fin.self);
        if (ein.link >= 0) {
            const auto& L = shootMap.floors()[
                static_cast<std::size_t>(gunFloor)]
                    .links[static_cast<std::size_t>(ein.link)];
            for (int k = 0; k < 3; ++k) ein.linkFrom[k] = L.from[k];
            static ToldOnce<int> linkTold;
            if ((rec.flags & 0x20u) && linkTold.first(s.actor, session_->areasEntered()))
                std::printf("frame %ld: actor %d %s - takes the STAIR %d of "
                            "floor %d: its near end %.0f %.0f %.0f, its far "
                            "end %.0f %.0f %.0f on floor %u\n", frameNo, s.actor,
                            s.model.c_str(), ein.link, gunFloor,
                            double(L.from[0]), double(L.from[1]),
                            double(L.from[2]), double(L.to[0]),
                            double(L.to[1]), double(L.to[2]), static_cast<unsigned>(L.destFloor));
        }
    }
    ein.targetAlive = true;
    ein.gridClear = gridSees;
    // THE RAY, `sub_4449E0`, cast where `sub_426E00` casts it:
    // only once the grid sees him and he is inside HALF his
    // inner range. From the target's `+244..+252` to his own -
    // both ROOT nodes, the pelvises as drawn last frame - over
    // the meshes `o3de_ForEachMeshInBox` walks, which are the
    // linked DECOR SET's alone (`dword_530C10`, filled by
    // `sub_4195C0` from `g_DecorSlots`): no body is in it, so
    // neither end's own body can stop the ray. Here the active
    // set's shot soup, the bolts' world - still without
    // `sub_444460`'s 0x41 skip (`todo/shoot-sight.md` step 6).
    // Forced TRUE, it would keep every close gunman in state 6
    // and never let him close to 13/8.
    ein.rayHits = false;
    ein.inWideCone = wideCone;
    // THE TWO OTHER SIGHTS (`todo/shoot-sight.md` step 5): a
    // spectre (type 12) and a cross-floor watcher (0x800000,
    // the player on another floor) see by cone AND ray at
    // ANY range, so for them the ray is cast every time
    const bool spectreArm = rec.type == 12;
    const bool watchArm = !spectreArm && !ein.sameNode &&
                          (rec.flags & 0x800000u) != 0;
    if (player && s.mo &&
        (spectreArm || watchArm ||
         (gridSees && double(rec.rangeInner) * 0.5 > ao.dist3d))) {
        float from[3] = {player->pos()[0],
                         player->pos()[1] - player->cameraLift(),
                         player->pos()[2]};
        for (std::size_t i = 0; i < playerBody.meshes.size(); ++i)
            if (playerBody.meshes[i].parent < 0) {
                if (playerMeshAtKnown && playerMeshAt.size() >= i * 3 + 3)
                    for (int k = 0; k < 3; ++k)
                        from[k] = playerMeshAt[i * 3 + static_cast<std::size_t>(k)];
                break;
            }
        float to[3] = {s.drawAt[0], s.drawAt[1], s.drawAt[2]};
        for (std::size_t i = 0; i < s.mo->meshes.size(); ++i)
            if (s.mo->meshes[i].parent < 0) {
                if (s.meshAt.size() >= i * 3 + 3)
                    for (int k = 0; k < 3; ++k)
                        to[k] = s.meshAt[i * 3 + static_cast<std::size_t>(k)];
                break;
            }
        const WorldSlot* sightSlot = nullptr;
        for (const auto& ws : worldSlots)
            if (!ws.stem.empty() && ws.stem == worldSet) sightSlot = &ws;
        double hitT = -1.0;
        if (sightSlot) {
            const double p0[3] = {from[0], from[1], from[2]};
            const double d[3] = {double(to[0]) - from[0], double(to[1]) - from[1],
                                 double(to[2]) - from[2]};
            // the shot soup without its cutouts (`shotCutout`)
            if (const auto h = omk::sweepSphere(sightSlot->shotSoup,
                                                sightSlot->shotCutout, p0, d, 0.0))
                hitT = h->t;
        }
        ein.rayHits = hitT >= 0.0;
        static std::map<int, int> rayTold;
        // said when the verdict changes - for the two other
        // sights the cone's half of it too
        const int rayKey = int(ein.rayHits) +
            ((spectreArm || watchArm) && (spectreArm ? cone : wideCone) ? 2 : 0);
        if (auto rt = rayTold.find(s.actor);
            rt == rayTold.end() || rt->second != rayKey) {
            rayTold[s.actor] = rayKey;
            if (spectreArm || watchArm)
                std::printf("frame %ld: actor %d %s - RAY (sub_4449E0) at %.1f, "
                            "%s: %s, %s cone\n", frameNo, s.actor, s.model.c_str(),
                            double(ao.dist3d),
                            spectreArm ? "the SPECTRE's sight (type 12)"
                                       : "the CROSS-FLOOR watcher's sight (0x800000)",
                            !sightSlot ? "no set to cast over"
                            : ein.rayHits ? "HITS the set" : "CLEAR",
                            (spectreArm ? cone : wideCone) ? "inside his"
                                                           : "outside his");
            else
            std::printf("frame %ld: actor %d %s - RAY (sub_4449E0) at %.1f, inside "
                        "half his inner range %.1f: %s\n", frameNo, s.actor,
                        s.model.c_str(), double(ao.dist3d),
                        double(rec.rangeInner) * 0.5,
                        !sightSlot ? "no set to cast over"
                        : ein.rayHits ? "HITS the set - he holds in state 6"
                                      : "CLEAR - he may close (13 or 8)");
            if (ein.rayHits)
                std::printf("    the set stops it %.2f of the way\n", hitT);
        }
    }
    ein.coinHeads = ((s.actor * 2654435761u) >> 16) & 1;
    // `sub_424DE0` calls `sub_426E00` from states 3, 4, 6, 8,
    // 11, 13, 14 and 28 only (05_sys.c 5747..6664). This
    // viewer calls it every tick in every state - a
    // superset that is still OPEN for the general arm - but
    // the spectre's and the watcher's arms WRITE his state
    // (3 or 4) on every call, so they run only where the
    // engine would call them; elsewhere nothing reads the
    // result (the port's consumers are 3, 6, 8 and 11).
    const bool engineCalls = rec.state == 3 || rec.state == 4 ||
        rec.state == 6 || rec.state == 8 || rec.state == 11 ||
        rec.state == 13 || rec.state == 14 || rec.state == 28;
    // `sub_421020`: an attack that reaches sends the engage to
    // the 10/11 pair (`goPair`) - range by property 21 of the slot
    ein.found421020 = omk::shootPickAttack(
        rec, ao.dist2d2,
        [&](int slot) {
            std::int32_t rm = 2, dm = 1;
            session.actorAttack(s.actor, slot, rm, dm);
            return static_cast<int>(rm);
        },
        [&]() {
            gun.randSeed = gun.randSeed * 214013u + 2531011u;
            return static_cast<int>((gun.randSeed >> 16) & 0x7FFFu);
        });
    const int preEngageState = rec.state;
    // (and never from STATE 10: `sub_424DE0` has no engage call
    // in that arm, and one here would rewrite a striking gunman's
    // state mid-clip)
    // (...nor from STATE 0, a gunman `Shoot_ActorEnter` left with
    // no action: `sub_424DE0`'s switch has no arm for it - its
    // `default:` goes straight to the tail, outcome 0, fire-if-
    // ready - so nothing engages him until a request moves him
    // (todo/drift-audit.md S13))
    const int eng = (((spectreArm || watchArm) && !engineCalls) ||
                     rec.state == 10 || rec.state == 0)
                        ? 0 : omk::shootEngage(rec, ao, cone, ein);
    // THE ENGAGE IS CALLED INSIDE AN ARM in the engine, so a
    // gunman it sends to the 10/11 pair (`goPair`) does not run
    // state 10 until the NEXT tick - by which `sub_4272B0` has
    // started his attack clip. This viewer runs the engage BEFORE
    // the step (the hoist `todo/handoff-shoot-mode.md` §4 item 6
    // labels), and running state 10 on the same tick would read the
    // OLD clip's frames as spent and send him on to 11 at once (as
    // robber 77 and dog 598 show). So the tick the pair is
    // entered has no step: outcome None, and nothing else.
    const bool pairEntered = (rec.state == 10 || rec.state == 11) &&
                             preEngageState != rec.state;
    fin.targetPredicate = eng != 0;
    fin.targetPredicateBits = eng;
    fin.canFire = eng != 0;
    float yaw = s.facing;
    // ---- THE PATROL, state 4 (`todo/shoot-patrol.md` 3) ----
    // `sub_426C20` decides, and on its 1 - ARRIVED - the
    // engine advances the point INSIDE the arm. The port's
    // arm takes the outcome, so the advance is here, in the
    // engine's own order: `sub_435660` for the next index,
    // then `sub_4356B0` for its world point AND its clip.
    //
    // `sub_435900`, "am I there yet", is one CELL either
    // way in x and z - not a point - so a waypoint is
    // reached generously.
    // (his FLOOR has to still be there: `Shoot_Think` writes
    // -1 the moment a walker leaves the grid, and a route
    // index outlives it. Indexing `floors()` with that -1 is
    // what crashed the catacombs at frame 63.)
    const int patrolFloor = static_cast<signed char>(rec.node & 0xFF);
    fin.movedThisFrame = s.walkDist;
    // ---- STATE 1's move decision and its step cell -----
    // `sub_426C20` is called in states 1, 4, 12 and 14; the
    // patrol's is below. State 1 steers at the goal the
    // engage set - the staircase's near end - and takes the
    // step only if the cell it lands on is walkable.
    // LABELLED: the cell read here is the GOAL's, not a cell
    // one step along; which of the two `sub_424DE0` reads is
    // not established.
    worldStagedShootMoveDecision(rec, gunFloor, fin, yaw);
    worldStagedShootPatrol(rec, patrolFloor, fin, yaw, s);
    // ---- THE EDGE states 1 and 2 WALK ------------------
    // `u32(rec, 4)`'s two points. State 1 steers at the
    // goal and, on its step, writes the heading and length
    // from these and hands over to state 2; state 2 walks
    // the edge itself, climbing `(to.y - from.y)/dist` and
    // at the far end taking the link's own destination
    // floor as his new `+188`.
    fin.myNode = gunFloor;
    fin.targetNode = playerFloor;
    worldStagedShootEdgeWalk(rec, gunFloor, fin);
    // ...and the clip clock, which state 5 reads - and the 10/11
    // pair (the attack clip's first half is outcome 3, its end
    // hands 10 to 11; 11 goes back to 10 past five frames). Without
    // it state 10 reads 0 of 0 as a clip already spent.
    if (rec.state == 5 || rec.state == 10 || rec.state == 11) {
        if (const auto ga = gun.anims.find(s.actor);
            ga != gun.anims.end() && ga->second.clip) {
            fin.clipFrame = ga->second.frame;
            fin.clipFrames = static_cast<float>(ga->second.clip->frames);
        }
    }
    // A GUNMAN WHOSE ENTRY IS STILL PENDING HAS NO BRAIN TICK.
    // `Shoot_ActorEnter` (0x00422C10) thinks and the scene's
    // `shoot.actor.action` acts on the SAME tick, before any
    // `Shoot_TickNpc` - and when its think fails it tries
    // `sub_4368E0`'s spiral and a think off his floor, and
    // failing those returns without entering him at all. So
    // the engine never runs a brain between an entry and its
    // action. The port holds the action for a body it has not
    // drawn yet (`gunEntryPending`), and must not let the brain
    // tick meanwhile: at the gallery's frame 3 each gunman would
    // ask for ACTION 0, a 30-frame clip under flag 8, which parks
    // the whole brain - so the tick that would find him on the
    // grid would come at frame 33, and every gunman would stand
    // a second before acting instead of one frame. The step is
    // empty for him - and `ShootStep::outcome` defaults to
    // FireIfReady, so the fire arm below tests this too.
    const bool entryPending = gunEntryPending.count(s.actor) != 0;
    const auto st = entryPending ? omk::ShootStep{}
                  : pairEntered ? [] {
                                      omk::ShootStep k;
                                      k.outcome = omk::ShootOutcome::None;
                                      return k;
                                  }()
                                : omk::shootGenericStep(rec, fin, yaw);
    s.facing = yaw;
    // ---- `sub_4272B0`, THE STATE'S CLIP, on a state change ----
    // (`if (v187 != +156) sub_4272B0(him, rec, ...)`). Its first
    // line clears flag 4 - the strike's once-a-clip latch - for
    // every state; PORTED only for the 10/11 pair (the strike,
    // `todo/released-spectres.md` step 5): case 9 starts the
    // attack at +108 by SLOT (`sub_434630`), "anim ATTAQUE PROCHE
    // non existante" when the group has none; case 10 starts a
    // type-23 clip, else 11. The other states' clips stay unported.
    worldStagedShootStateClip(entryPending, rec, before, s);
    // ---- STATE 2 ARRIVING at the edge's far end ---------
    // `o3de_SetNodePos(node, to.x, to.y - +64, to.z)`, then
    // `+188 = link.destFloor` and `+60 = that y`. This is
    // the one place a gunman changes floor.
    worldStagedShootArrive(st, fin, gunFloor, s, rec);
    // `sub_435650`: state 4 or 5 left behind gives the route up
    if (st.releaseRoute && rec.route >= 0 && shootMap.valid()) {
        shootMap.routeRelease(static_cast<signed char>(rec.node & 0xFF),
                              rec.route);
        rec.route = -1;
    }
    // the ACTION it asked for (always 0 in the generic brain)
    if (st.actionRequest >= 0)
        shootApplyAction(s, rec, st.actionRequest, 0, "the brain's request");
    if (rec.state != before || st.outcome == omk::ShootOutcome::Fire) {
        // (once he stands somewhere: his entry ACTION changes his
        // state on the tick he is born, before he is first drawn,
        // and a distance from the world origin says nothing)
        if (!s.brainTold && gunCellSeeded.count(s.actor)) {
            s.brainTold = true;
            std::printf("frame %ld: actor %d %s - brain %d -> %d, "
                        "outcome %d, %.0f units away\n", frameNo,
                        s.actor, s.model.c_str(), before,
                        rec.state, int(st.outcome), ao.dist3d);
        }
    }
    // ---- LABEL_261: HIS CURRENT CLIP ADVANCES ----
    // `if (flags & 8) sub_421770(...) else sub_421370(...)`:
    // with no picked clip playing, `sub_421370` (0x00421370)
    // runs his current clip - unless `+160 & 2` - by the
    // frame delta: `+188 += dt`, and at the clip's length
    // (`Anim_Frames(+8)`) it wraps to `dt + 1.0`, key 0 being
    // the rest sentinel. The clip is his action's
    // (`shootClipFor`, the same type picks `Shoot_ActorAction`
    // makes); a change of clip restarts it at 1.0, which is
    // `sub_421A20`'s start.
    //
    // ...AND IT MOVES HIM, through the wall detection. After the frame
    // step `sub_421370` takes the clip's root delta between the
    // old frame and the new (`sub_434D30` -> `Anim_RootDelta(clip,
    // node matrix, +192, +188)`, turned by his facing), and puts
    // it through the WALL TEST, two steps (`sub_421140(rec,
    // {pos, dx, dz}, 2)`). Read off the asm at 0x421520..0x421756:
    //
    //   dx == 0 && dz == 0      -> nothing moves (the vertical
    //                              only under +460, a .3DM arm)
    //   free                    -> +244 += dx, +252 += dz,
    //                              MoveNodeBy(dx, dy, dz)
    //   blocked: len = |d|, try {0, dz > 0 ? len : -len}; +420 =
    //     dz > 0 ? 180 : 0 (not under +160 & 0x200)
    //     free  -> x SNAPS to the landing cell's centre (not
    //              under 0x200), z += dz - the ORIGINAL dz
    //     else  -> +160 |= 0x100; try {dx > 0 ? len : -len, 0};
    //              +420 = dx > 0 ? 90 : 270
    //       free -> z snaps, x += dx
    //       else -> +420 += 180 (no wrap), only dy
    //
    // and at the loop wrap (+192 = 0, +188 = dt + 1) the node is
    // SET to (x, rec+60, z) - the vertical drift of a loop goes.
    // Here the offset is `s.walkMove`, summed into the drawn
    // position with the program's own; the wrap zeroes its
    // vertical, which stands where rec+60 (not wired) would
    // put him only in so far as his placement is that height.
    // So does EVERY CLIP START: `sub_421A20` (41 callers) ends
    // both its arms in `o3de_SetNodePos(node, +244, rec+60 +
    // d(0->1).y, +252)` - the turn clip's start, the walk put
    // back when it ends (`sub_421A20(+12, 0)`), the death.
    // Without it a robber whose walk a turn keeps interrupting
    // before the wrap keeps each cycle's 0.38-a-frame rise and
    // climbs to the CEILING, higher each time the loop restarts.
    //
    // NOT PORTED, labelled: the 0x400 grid arm (`sub_47C1B0` on
    // a cell byte with bit 0x10, which FREEZES the clip); the
    // cell OCCUPANCY - `sub_420B80` writes 0x80 into his cell
    // after the move and the brain's prologue puts the saved
    // byte (+189) back before it thinks, so each gunman is a
    // wall to the others and robbers here can walk through
    // one another; the facing the root delta turns by is this
    // tick's, where the node matrix holds last tick's; the
    // walk while a scene PROGRAM drives him (it places him
    // itself); and an action re-asked for the same clip,
    // which the engine restarts and this does not.
    worldStagedShootClipAdvance(rec, s, act, fin);
    // ---- THE FIRE EPILOGUE: `sub_424DE0`'s switch on the
    // outcome the brain just set (readable 05_sys.c 6031) ----
    //
    //   case 1: if (!(flags & 0x8000) && !(flags & 2)) {
    //             +164 -= dt;
    //             if (sub_4348B0(+20)) {           // the FIRE TEST
    //               if (!+180) Shoot_InitWeapon(him, rec);
    //               if (!+180) goto LABEL_314;     // a gate with no row
    //               if (+172 <= 0) +172 = row.f0;
    //               if (sub_47C2A0(him, target, 1, rec) == 2)
    //                 { flags |= 0x80; u8(+190)++; } } }
    //   case 0: if (!(flags & 2)) {
    //             if (!sub_4348B0(+20)) goto LABEL_315;   // aim pose alone
    //             if (+180 && +172 <= 0) +172 = row.f0;   // LABEL_353
    //             sub_47C2A0(him, target, 0, rec); }      // LABEL_314
    //
    // `sub_47C2A0` with a TARGET is the gate's other arm: on
    // the tick it returns 2 it calls `Actor_TickProjectiles`
    // (him) there and then - in either case, since a pull
    // left pending by case 1 carries through case 0's gate.
    // NOT PORTED, labelled: that arm's aim ANGLES (the
    // target's node minus his `Buste`, actor +20, jittered on
    // the fired tick by `r/4 - rand() % (r/2)`), which reach
    // only the aim POSE `sub_434C30` - so a gunman does not
    // raise his arm; the `+190` count, whose one reader is
    // LABEL_359's burst against property 18 (flag 0x2000000);
    // and case 1's other arm, the `+164` countdown of a
    // character whose fire test is clear.
    // ---- THE STRIKE, the epilogue's OUTCOME 3 (05_sys.c 6187) ----
    // State 10, the first half of the attack clip. NOT PORTED,
    // labelled: the `actor+460 && flags < 0` arm before it (the
    // +460 interrupt), the aim pose `sub_434C30`, and `++u8(+190)`
    // (this port's +190 is the wound band). With `0x8000` clear
    // and flag 4 not yet up: property 21 of +108 as a range,
    // `39 *` metres, against the 3D distance between the two
    // ROOT nodes; inside it flag 4 goes up, property 22 is the
    // damage, and `sub_423B10(+96, damage, (dx, 0, dz))`.
    worldStagedShootStrike(st, rec, s, entryPending);
    const bool fullArm = st.outcome == omk::ShootOutcome::Fire;
    worldStagedShootFireGate(fullArm, rec, st, entryPending, s, fin);
    // ---- LABEL_359: THE CLIP THE ARM PICKED (`+16`) ----
    // A turn code picks a clip of type 30 / 31 / 32 out of his
    // group (`List_PickRandomByType(+20, type)`, LABEL_177) with
    // `+184 = total / Anim_Frames(clip)`, and `sub_421A20(him,
    // rec, clip, 2)` starts it at frame 1.0, the previous clip
    // kept at `+12`, flag 8 up. With no clip of the type the arm
    // turns him at the fallback rate instead, `+420 += rate *
    // dt`. ONLY THE TURNS are played: the other clips an arm or
    // `Shoot_ActorAction` picks are not - he is still posed by
    // his action - labelled.
    worldStagedShootTurnClip(st, s, rec, fin);
}



// STATE 1's move decision and its step cell (from `worldStagedShoot`)
void PlayState::worldStagedShootMoveDecision(omk::ShootRecord& rec, int gunFloor,
                                             omk::ShootFrameIn& fin, float& yaw) {
    if (rec.state == 1 && shootMap.valid() && gunFloor >= 0) {
        const float cell = static_cast<float>(shootMap.scale());
        const bool there = std::fabs(rec.goalX - fin.self[0]) < cell &&
                           std::fabs(rec.goalZ - fin.self[2]) < cell;
        fin.moveCode = omk::shootMoveDecision(rec, fin.self, yaw, fin.dt, there);
        int cx = 0, cz = 0;
        if (shootMap.cellAt(gunFloor, rec.goalX, rec.goalZ, cx, cz))
            fin.stepCellValue = static_cast<std::int8_t>(
                shootMap.floors()[static_cast<std::size_t>(gunFloor)]
                    .cell(cx, cz));
    }
}


// THE PATROL, state 4 (`todo/shoot-patrol.md` 3) (from `worldStagedShoot`)
void PlayState::worldStagedShootPatrol(omk::ShootRecord& rec, int patrolFloor,
                                       omk::ShootFrameIn& fin, float& yaw, Staged& s) {
    if (rec.state == 4 && rec.route >= 0 && shootMap.valid() &&
        patrolFloor >= 0 &&
        patrolFloor < static_cast<int>(shootMap.floors().size())) {
        fin.hasRoute = true;
        const float cell = static_cast<float>(shootMap.scale());
        const bool there = std::fabs(rec.goalX - fin.self[0]) < cell &&
                           std::fabs(rec.goalZ - fin.self[2]) < cell;
        fin.moveCode = omk::shootMoveDecision(rec, fin.self, yaw, fin.dt, there);
        if (fin.moveCode == 1) {
            const auto& wl = shootMap.floors()[
                static_cast<std::size_t>(patrolFloor)].waypoints;
            const int was = rec.repeats;
            rec.repeats = shootMap.routeNextIndex(patrolFloor, rec.route,
                                                  rec.repeats);
            const int clip = shootMap.routePoint(patrolFloor, rec.route,
                                                 rec.repeats,
                                                 rec.goalX, rec.goalZ);
            fin.routePointHasClip = clip > 0;
            std::printf("frame %ld: actor %d %s - PATROL point %d -> %d of %d, "
                        "now walking to %.0f %.0f%s\n", frameNo, s.actor,
                        s.model.c_str(), was, rec.repeats,
                        rec.route < static_cast<int>(wl.size())
                            ? int(wl[static_cast<std::size_t>(rec.route)].len)
                            : -1,
                        double(rec.goalX), double(rec.goalZ),
                        clip > 0 ? " - it carries a CLIP: state 5" : "");
        }
    }
}


// THE EDGE states 1 and 2 WALK (from `worldStagedShoot`)
void PlayState::worldStagedShootEdgeWalk(omk::ShootRecord& rec, int gunFloor,
                                         omk::ShootFrameIn& fin) {
    if (rec.link >= 0 && shootMap.valid() && gunFloor >= 0 &&
        gunFloor < static_cast<int>(shootMap.floors().size())) {
        const auto& ls = shootMap.floors()[
            static_cast<std::size_t>(gunFloor)].links;
        if (rec.link < static_cast<int>(ls.size())) {
            const auto& L = ls[static_cast<std::size_t>(rec.link)];
            fin.hasEdge = true;
            for (int k = 0; k < 3; ++k) {
                fin.edgeFrom[k] = L.from[k];
                fin.edgeTo[k]   = L.to[k];
            }
        }
    }
}


// `sub_4272B0`, THE STATE'S CLIP, on a state change (from `worldStagedShoot`)
void PlayState::worldStagedShootStateClip(bool entryPending, omk::ShootRecord& rec, int before,
                                          Staged& s) {
    auto& session = *session_;
    if (!entryPending && rec.state != before) {
        rec.flags &= ~4u;
        const int grpS = static_cast<int>(session.typeOfActor(s.actor));
        if (rec.state == 10 && grpS >= 0 && grpS < 64) {
            const omk::PedClip* atk = shootClipBySlot(grpS, rec.attackSlot);
            static std::map<int, int> atkTold;
            if (atk) {
                gun.curSlot[s.actor] = atk->slot;
                gun.anims[s.actor].clip = nullptr;
                if (atkTold[s.actor]++ < 3)
                    std::printf("frame %ld: actor %d %s - ATTACK CLIP (sub_4272B0 case 9): "
                                "slot %d '%s', %d frames\n", frameNo, s.actor, s.model.c_str(),
                                atk->slot, atk->name.c_str(), atk->frames);
            } else if (atkTold[s.actor]++ < 3) {
                std::printf("frame %ld: actor %d %s - anim ATTAQUE PROCHE non existante "
                            "dans le .ANI (slot %d)\n", frameNo, s.actor, s.model.c_str(),
                            rec.attackSlot);
            }
        } else if (rec.state == 11 && grpS >= 0 && grpS < 64) {
            const int t = shootClipExact(grpS, 23) ? 23 : 11;
            gun.curType[s.actor] = t;
            gun.curSlot.erase(s.actor);
            gun.anims[s.actor].clip = nullptr;
        }
    }
}


// STATE 2 ARRIVING at the edge's far end - the one place a gunman changes floor (from `worldStagedShoot`)
void PlayState::worldStagedShootArrive(const omk::ShootStep& st, omk::ShootFrameIn& fin,
                                       int gunFloor, Staged& s, omk::ShootRecord& rec) {
    if (st.arrived && fin.hasEdge) {
        const int was = gunFloor;
        s.at[0] = fin.edgeTo[0];
        s.at[2] = fin.edgeTo[2];
        for (float& w : s.walkMove) w = 0.0f;
        rec.groundY = fin.edgeTo[1] - rec.height;
        s.at[1] = rec.groundY;
        s.drawAt[1] = rec.groundY;
        if (shootMap.valid() && was >= 0 &&
            was < static_cast<int>(shootMap.floors().size())) {
            const auto& ls = shootMap.floors()[
                static_cast<std::size_t>(was)].links;
            if (rec.link >= 0 && rec.link < static_cast<int>(ls.size()))
                rec.node = static_cast<std::int8_t>(
                    ls[static_cast<std::size_t>(rec.link)].destFloor);
        }
        rec.link = -1;
        std::printf("frame %ld: actor %d %s - CHANGED FLOOR %d -> %d at "
                    "%.0f %.0f %.0f (sub_424DE0 state 2's far end)\n",
                    frameNo, s.actor, s.model.c_str(), was,
                    static_cast<signed char>(rec.node & 0xFF),
                    double(s.at[0]), double(rec.groundY), double(s.at[2]));
    }
}


// LABEL_261: HIS CURRENT CLIP ADVANCES (from `worldStagedShoot`)
void PlayState::worldStagedShootClipAdvance(omk::ShootRecord& rec, Staged& s, int act,
                                            omk::ShootFrameIn& fin) {
    auto& session = *session_;
    if (!(rec.flags & 8u) && !(rec.flags & 2u)) {
        const int grpA = static_cast<int>(session.typeOfActor(s.actor));
        // his clip is the TYPE his last action started
        // (`Shoot_ActorAction`), the scene action's until one has
        const auto ctA = gun.curType.find(s.actor);
        const auto csA = gun.curSlot.find(s.actor);
        const omk::PedClip* ac = (grpA >= 0 && grpA < 64)
            ? (csA != gun.curSlot.end() ? shootClipBySlot(grpA, csA->second)
               : ctA != gun.curType.end() ? shootClipExact(grpA, ctA->second)
                                         : shootClipFor(grpA, act))
            : nullptr;
        GunAnim& ga = gun.anims[s.actor];
        // `if (u32(rec, 156) != 13) flags &= ~0x100`
        if (rec.state != 13) rec.flags &= ~0x100u;
        if (ac != ga.clip) {
            ga.clip = ac;
            ga.frame = 1.0f;
            // `sub_421A20`'s last line, both arms: the node SET
            // to (x, rec+60 + the clip's frame-0->1 dy, z)
            s.walkMove[1] = 0.0f;
            if (ac && ac->root.size() >= 3) {
                float d01[3] = {0.0f, 0.0f, 0.0f};
                omk::pedRootDelta(*ac, 0.0f, 1.0f, nullptr, d01);
                s.walkMove[1] = d01[1];
                // once per gunman and clip type: the height the
                // clip's start SETS him at, over his placement
                static ToldOnce<std::pair<int, int>> d01Told;
                if (d01Told.first({s.actor, ac->type}, session_->areasEntered()))
                    std::printf("frame %ld: actor %d %s - clip type %d starts "
                                "(sub_421A20): frame 0->1 root %.2f %.2f %.2f, "
                                "%d root keys\n", frameNo, s.actor, s.model.c_str(),
                                ac->type, double(d01[0]), double(d01[1]),
                                double(d01[2]),
                                static_cast<int>(ac->root.size() / 3));
            }
        } else if (ac && ac->frames > 0) {
            float t0 = ga.frame;
            ga.frame += fin.dt;
            bool wrapped = false;
            if (ga.frame >= static_cast<float>(ac->frames)) {
                wrapped = true;
                t0 = 0.0f;                  // `+192 = 0`
                s.walkMove[1] = 0.0f;       // the node SET to rec+60
            }
            if (wrapped) {
                ga.frame = fin.dt + 1.0f;
                // once per robber: his clip has run and looped
                if (gun.looped.insert(s.actor).second)
                    std::printf("frame %ld: actor %d %s - current clip "
                                "(sub_421370): type %d slot %d, %d frames, "
                                "looped to %.1f\n", frameNo, s.actor,
                                s.model.c_str(), ac->type, ac->slot,
                                ac->frames, double(ga.frame));
            }
            // THE ROOT MOTION, `sub_421370`'s walk (above)
            if (!s.progPlaced && ac->root.size() >= 3) {
                float d[3] = {0.0f, 0.0f, 0.0f};
                omk::pedRootDelta(*ac, t0, ga.frame, nullptr, d);
                float r[3];
                omk::rotateYaw(s.facing, d, r);
                const float dx = r[0], dy = r[1], dz = r[2];
                if (dx != 0.0f || dz != 0.0f) {
                    const float x0 = s.at[0] + s.walkMove[0];
                    const float z0 = s.at[2] + s.walkMove[2];
                    const bool keepFacing = (rec.flags & 0x200u) != 0;
                    float snap[2] = {0.0f, 0.0f};
                    const char* how = "free";
                    if (omk::shootWallTest(rec, shootMap, x0, z0, dx, dz, 2,
                                           snap) == 0) {
                        s.walkMove[0] += dx;
                        s.walkMove[2] += dz;
                    } else {
                        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (!keepFacing) s.facing = dz > 0.0f ? 180.0f : 0.0f;
                        if (omk::shootWallTest(rec, shootMap, x0, z0, 0.0f,
                                               dz > 0.0f ? len : -len, 2,
                                               snap) == 0) {
                            how = "slid along z";
                            if (!keepFacing) s.walkMove[0] = snap[0] - s.at[0];
                            s.walkMove[2] += dz;
                        } else {
                            if (!keepFacing) s.facing = dx > 0.0f ? 90.0f : 270.0f;
                            rec.flags |= 0x100u;
                            if (omk::shootWallTest(rec, shootMap, x0, z0,
                                                   dx > 0.0f ? len : -len, 0.0f,
                                                   2, snap) == 0) {
                                how = "slid along x";
                                if (!keepFacing) s.walkMove[2] = snap[1] - s.at[2];
                                s.walkMove[0] += dx;
                            } else {
                                how = "turned back";
                                if (!keepFacing) s.facing += 180.0f;
                            }
                        }
                        ++s.walkWalls;
                    }
                    s.walkMove[1] += dy;
                    s.walkDist = std::sqrt(dx * dx + dz * dz);
                    if (!s.walkTold) {
                        s.walkTold = true;
                        std::printf("frame %ld: actor %d %s - walks (sub_421370): "
                                    "clip type %d, root delta %.2f %.2f %.2f "
                                    "at facing %.1f from %.0f %.0f\n", frameNo,
                                    s.actor, s.model.c_str(), ac->type,
                                    double(dx), double(dy), double(dz),
                                    double(s.facing), double(x0), double(z0));
                    }
                    if (std::strcmp(how, "free") != 0 && !s.walkWallTold) {
                        s.walkWallTold = true;
                        std::printf("frame %ld: actor %d %s - the wall test "
                                    "(sub_421140) stops his walk at %.0f %.0f: "
                                    "%s, facing %.1f, now %.0f %.0f\n", frameNo,
                                    s.actor, s.model.c_str(), double(x0),
                                    double(z0), how, double(s.facing),
                                    double(s.at[0] + s.walkMove[0]),
                                    double(s.at[2] + s.walkMove[2]));
                    }
                }
            }
        }
    }
}


// THE STRIKE, the fire epilogue's outcome 3 (05_sys.c 6187) (from `worldStagedShoot`)
void PlayState::worldStagedShootStrike(const omk::ShootStep& st, omk::ShootRecord& rec, Staged& s,
                                       bool entryPending) {
    auto& session = *session_;
    if (st.outcome == omk::ShootOutcome::Outcome3 && !(rec.flags & 0x8000u) &&
        !(rec.flags & 4u) && player && s.mo && !entryPending) {
        std::int32_t rangeM = 2, dmgA = 1;
        session.actorAttack(s.actor, rec.attackSlot, rangeM, dmgA);
        float pt[3] = {player->pos()[0], player->pos()[1] - player->cameraLift(),
                       player->pos()[2]};
        for (std::size_t i = 0; i < playerBody.meshes.size(); ++i)
            if (playerBody.meshes[i].parent < 0) {
                if (playerMeshAtKnown && playerMeshAt.size() >= i * 3 + 3)
                    for (int k = 0; k < 3; ++k)
                        pt[k] = playerMeshAt[i * 3 + static_cast<std::size_t>(k)];
                break;
            }
        float me[3] = {s.drawAt[0], s.drawAt[1], s.drawAt[2]};
        for (std::size_t i = 0; i < s.mo->meshes.size(); ++i)
            if (s.mo->meshes[i].parent < 0) {
                if (s.meshAt.size() >= i * 3 + 3)
                    for (int k = 0; k < 3; ++k)
                        me[k] = s.meshAt[i * 3 + static_cast<std::size_t>(k)];
                break;
            }
        const double sdx = double(pt[0]) - me[0], sdy = double(pt[1]) - me[1],
                     sdz = double(pt[2]) - me[2];
        const double sd = std::sqrt(sdx * sdx + sdy * sdy + sdz * sdz);
        if (sd < double(39 * rangeM)) {
            rec.flags |= 4u;
            const std::size_t recAtS = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
            const std::size_t recLenS = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
            omk::StrikeIn sin;
            sin.damage = dmgA;
            sin.victimIsPlayer = true;
            sin.victimInShoot = player->state() == omk::ActorState::Shoot;
            std::int32_t shieldS = 0;
            omk::readActorProperty(state.raw().subspan(recAtS, recLenS), 17, shieldS);
            sin.bodyShield = shieldS;
            sin.difficulty = settings.v.shootDifficulty;
            sin.victimYaw = player->facing();
            sin.dir[0] = static_cast<float>(sdx);
            sin.dir[2] = static_cast<float>(sdz);
            // `sub_423B10` wakes a freeze first - unreachable here,
            // since a frozen gunman never strikes (`0x8000` above)
            shootWake(frameNo, "a strike (sub_423B10)");
            const omk::HitOut sho = omk::shootApplyStrike(playerShootRec, sin);
            std::printf("frame %ld: actor %d %s - STRIKE (outcome 3, sub_423B10): "
                        "attack slot %d, range %d m (%d), %.1f away, damage %d, "
                        "difficulty %d\n", frameNo, s.actor, s.model.c_str(),
                        rec.attackSlot, int(rangeM), 39 * int(rangeM), sd,
                        int(dmgA), sin.difficulty);
            if (sho.refused)
                std::printf("  PLAYER strike REFUSED (`sub_423B10` returns -1)\n");
            else if (sho.healthWas <= 0)
                std::printf("frame %ld: PLAYER HIT by actor %d's strike - he is down "
                            "already (health %d): nothing\n", frameNo, s.actor, sho.healthWas);
            else
                applyPlayerDamage(frameNo, s.actor, "strike", int(dmgA), int(shieldS), sho);
        }
    }
}


// THE FIRE EPILOGUE's gate: the target arm that aims him, and the shot it fires (from `worldStagedShoot`)
void PlayState::worldStagedShootFireGate(bool fullArm, omk::ShootRecord& rec,
                                         const omk::ShootStep& st, bool entryPending, Staged& s,
                                         omk::ShootFrameIn& fin) {
    auto& session = *session_;
    if (((fullArm && !(rec.flags & 0x8000u)) ||
         st.outcome == omk::ShootOutcome::FireIfReady) &&
        !(rec.flags & 2u) && !entryPending) {
        const int grpT = static_cast<int>(session.typeOfActor(s.actor));
        const auto w8 = omk::animGroupWord8(crowd.ani, grpT);
        const bool fireTest = w8 && (*w8 & 1u);
        // the object in HIS hand (actor +164) - what
        // `Shoot_InitWeapon` asks event 46 about
        const int hs = session.heldSlotOf(s.actor);
        const int obj = hs >= 0 ? session.objectSlotId(hs) : -1;
        const auto& objs = voiceLib.objects();
        const bool known = obj >= 0 && static_cast<std::size_t>(obj) < objs.size();
        const std::string stem = known ? objs[static_cast<std::size_t>(obj)].stem
                                       : std::string();
        if (fullArm) rec.clipLen -= fin.dt;
        if (fullArm && fireTest && !rec.weapon) {
            // `Shoot_InitWeapon`: kind (property 3) and model,
            // through the OTHERS' table 0x4C36F8
            const int kind = known ? objs[static_cast<std::size_t>(obj)].kind : -1;
            const int type = omk::shootWeaponType(
                kind, known ? "MESHES\\OBJETS\\" + stem + ".3DO" : std::string());
            rec.weapon = shootWeapons.find(type, false);
            if (gun.told.insert(s.actor).second) {
                std::printf("frame %ld: actor %d %s - Shoot_InitWeapon: held slot %d, "
                            "object %d '%s' kind %d -> type %d, ", frameNo, s.actor,
                            s.model.c_str(), hs, obj, stem.c_str(), kind, type);
                if (rec.weapon)
                    std::printf("row rate %.1f speed %.1f damage %d\n",
                                double(rec.weapon->rate), double(rec.weapon->speed),
                                rec.weapon->damage);
                else
                    std::printf("NO ROW - he cannot fire\n");
            }
        }
        if (!fireTest && gun.told.insert(s.actor).second)
            std::printf("frame %ld: actor %d %s - the weapon test is clear "
                        "(sub_4348B0: ANIMS\\%s.ANI group %d, +8 %s) - his BUILT-IN "
                        "weapon: %s\n", frameNo, s.actor, s.model.c_str(),
                        crowd.aniName.c_str(), grpT,
                        w8 ? std::to_string(*w8).c_str() : "absent",
                        rec.type == 12u ? "slots 0 and 1 at the target (sub_44CDF0)"
                                        : "his Tire markers (add_Fire)");
        // ---- THE BIT-CLEAR ARM's SHOT (05_sys.c 6082, read 2026-11-11) ----
        // `+164 -= dt` above; at or under 0: a SPECTRE (`+80`, the type, 12)
        // fires slots 0 and 1 at his target (`Actor_GetPosAndFacing(v177)`);
        // anyone else `Actor_TickProjectiles(him)`, and when it returns
        // nonzero `+160 |= 0x80` and `+164 = sub_434890(group)` (its `+190`
        // increment and `byte_90E0B0 = 1` are not carried: the port's `+190`
        // is the hit band). Then `sub_434C30`, the pose, as before.
        if (fullArm && !fireTest && rec.clipLen <= 0.0f && player) {
            float tgt[3] = {player->pos()[0], player->pos()[1], player->pos()[2]};
            for (std::size_t i = 0; i < playerBody.meshes.size(); ++i)
                if (playerBody.meshes[i].parent < 0) {
                    if (playerMeshAtKnown && playerMeshAt.size() >= i * 3 + 3)
                        for (int k = 0; k < 3; ++k)
                            tgt[k] = playerMeshAt[i * 3 + static_cast<std::size_t>(k)];
                    break;
                }
            if (rec.type == 12u) {
                recordFire(s, rec, 0, tgt, "SPECTRE", -1.0f);
                recordFire(s, rec, 1, tgt, "SPECTRE", -1.0f);
            } else if (session.heldSlotOf(s.actor) >= 0) {
                // `+164` holds an object: `Actor_TickProjectiles`' RECORD path,
                // which the bit-SET arm already ports - not wired for a
                // bit-clear holder (no shipped case met yet)
                if (gun.holderTold.insert(s.actor).second)
                    std::printf("frame %ld: actor %d %s - bit clear but an object in his "
                                "hand: the record path, NOT PORTED for this arm\n",
                                frameNo, s.actor, s.model.c_str());
            } else if (s.mo &&
                       addFireSlots(s.actor, s.model.c_str(), s.mo->meshes, s.meshAt,
                                    s.meshRot, actorSlotTimer[s.actor], "GUNMAN")) {
                rec.flags |= 0x80u;
                rec.clipLen = static_cast<float>(
                    omk::animGroupRate16(crowd.ani, grpT).value_or(0));
                hudEventOnce(1);          // `if (!byte_90E0B0) byte_90E0B0 = 1`
            }
        }
        omk::FireGate gate = omk::FireGate::Released;
        if (fireTest) {
            // both arms reload an empty countdown (case 0's
            // LABEL_353, case 1's own test)
            if (rec.weapon && rec.weaponTimer <= 0.0f)
                rec.weaponTimer = rec.weapon->rate;
            gate = omk::shootFireGate(rec, fullArm, fin.dt);
            if (fullArm && gate == omk::FireGate::Fired) rec.flags |= 0x80u;
        }
        // ---- the gate's TARGET ARM, the part that AIMS HIM ----
        // (0x0047C820 on): his target's node (+36..+44 - the
        // player's root mesh as drawn) minus HIS `Buste` (actor
        // +20), jittered ON THE FIRED TICK by `r/4 - rand() %
        // int(r/2)` per axis, r the target node's +88 - three
        // `rand()`s drawn BEFORE `Actor_TickProjectiles` draws its
        // own; the yaw is `acos` of the flat cosine against his
        // forward, `(0, 0, -1)` turned by +420, NEGATED when
        // `fx*dz - fz*dx > 0` (`fcomp flt_4BCAEC` - 0.0 - and
        // `test ah, 41h`); the pitch is `-atan2(dy, 2 * |d|)`.
        // Released, both are 0 and `sub_434C30` still bends him -
        // which is the arm LOWERING by +176. With no row the gate
        // returns before any of it (`if (!+180) return`), and a
        // clear fire test takes LABEL_315, whose `sub_434C30`
        // has no angles: no layer either way.
        {
            GunAim& gm = gun.aims[s.actor];
            gm.live = fireTest && rec.weapon;
            gm.yaw = gm.pitch = 0.0f;
            if (gm.live && gate != omk::FireGate::Released && player &&
                s.mo && s.meshAt.size() == s.mo->meshes.size() * 3) {
                int buste = -1;     // the LAST strstr hit
                for (std::size_t i = 0; i < s.mo->meshes.size(); ++i)
                    if (std::strstr(s.mo->meshes[i].name, "Buste"))
                        buste = static_cast<int>(i);
                float tgt[3] = {player->pos()[0], player->pos()[1],
                                player->pos()[2]};
                float tr = 0.0f;
                for (std::size_t i = 0; i < playerBody.meshes.size(); ++i)
                    if (playerBody.meshes[i].parent < 0) {
                        tr = playerBody.meshes[i].radius;
                        if (playerMeshAtKnown && playerMeshAt.size() >= i * 3 + 3)
                            for (int k = 0; k < 3; ++k)
                                tgt[k] = playerMeshAt[i * 3 + static_cast<std::size_t>(k)];
                        break;
                    }
                if (buste >= 0) {
                    const std::size_t b = static_cast<std::size_t>(buste);
                    double d[3];
                    for (int k = 0; k < 3; ++k)
                        d[k] = double(tgt[k]) - s.meshAt[b * 3 + static_cast<std::size_t>(k)];
                    if (gate == omk::FireGate::Fired) {
                        const double half = double(tr) * 0.5;
                        const int span = static_cast<int>(half);
                        for (int k = 0; k < 3 && span > 0; ++k) {
                            gun.randSeed = gun.randSeed * 214013u + 2531011u;
                            const int rr = static_cast<int>((gun.randSeed >> 16) & 0x7FFFu) % span;
                            d[k] = d[k] + half * 0.5 - rr;
                        }
                    }
                    const double yawR = double(s.facing) * omk::kDegToRad;
                    const double fx = std::sin(yawR), fz = -std::cos(yawR);
                    const double flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
                    double cosv = flat > 0.0 ? (fz * d[2] + fx * d[0]) / flat : 1.0;
                    cosv = cosv > 1.0 ? 1.0 : (cosv < -1.0 ? -1.0 : cosv);
                    const double cross = fx * d[2] - fz * d[0];
                    gm.yaw = static_cast<float>(cross > 0.0 ? -std::acos(cosv)
                                                            : std::acos(cosv));
                    const double dist = std::sqrt(flat * flat + d[1] * d[1]);
                    gm.pitch = static_cast<float>(-std::atan2(d[1], dist + dist));
                }
            }
            if (gm.live && gate != omk::FireGate::Released &&
                gun.aimTold.insert(s.actor).second)
                std::printf("frame %ld: actor %d %s - AIM LAYER (sub_434C30): yaw "
                            "%.3f pitch %.3f rad, lowered %.2f\n", frameNo, s.actor,
                            s.model.c_str(), double(gm.yaw), double(gm.pitch),
                            double(rec.weaponLowered));
        }
        if (gate == omk::FireGate::Fired && rec.weapon && player) {
            // ---- `Actor_TickProjectiles(him)`, the record
            // path's OTHER arm (readable 17_script.c 1375) ----
            omk::RecordShot rs;
            // THE MUZZLE: the held object's +12 node, `tir`,
            // which `o3de_LinkObjectToParent` hangs under his
            // `Maing` (actor +44) - here the hand as it was
            // DRAWN last frame, `tir`'s own +128 local turned
            // by the hand's world rotation.
            for (int k = 0; k < 3; ++k) rs.muzzle[k] = s.drawAt[k];
            const char* from = "his position (not drawn)";
            const std::size_t nm = s.mo ? s.mo->meshes.size() : 0;
            int hand = -1;      // the LAST strstr hit
            for (std::size_t i = 0; i < nm; ++i)
                if (std::strstr(s.mo->meshes[i].name, "Maing")) hand = static_cast<int>(i);
            if (hand >= 0 && s.meshAt.size() == nm * 3 && s.meshRot.size() == nm * 9) {
                const std::size_t h = static_cast<std::size_t>(hand);
                const GunFacts* gf = stem.empty() ? nullptr : &gunFactsFor(stem);
                for (int k = 0; k < 3; ++k) {
                    rs.muzzle[k] = s.meshAt[h * 3 + static_cast<std::size_t>(k)];
                    if (gf && gf->ok)
                        for (int ax = 0; ax < 3; ++ax)
                            rs.muzzle[k] += s.meshRot[h * 9 + static_cast<std::size_t>(ax * 3 + k)] *
                                            gf->tirLocal[ax];
                }
                from = gf && gf->ok ? "the tir node" : "the Maing node";
            }
            // THE AIM is this function's own, not the gate's
            // (`shootGunmanAim`, actor/shootfire.h): at the
            // player's +244..+252, each axis jittered by his
            // root node's radius, the three `rand() % 100`
            // drawn x, y, z. +244..+252 is the ROOT NODE's
            // position, not the feet: `sub_4800C0` copies +248
            // from the root node's +40 every tick, and the
            // brain's edge walk moves the two together
            // (`o3de_MoveNodeBy(node, 0, climb, 0)`, then
            // `+248 += climb`). The root is the pelvis, so the
            // target is his root mesh as DRAWN last frame, and
            // `pos()` - the feet - only until he has been.
            float pp[3] = {player->pos()[0], player->pos()[1], player->pos()[2]};
            float r = 0.0f;
            int rootMesh = -1;
            for (std::size_t i = 0; i < playerBody.meshes.size(); ++i)
                if (playerBody.meshes[i].parent < 0) {
                    r = playerBody.meshes[i].radius;
                    rootMesh = static_cast<int>(i);
                    break;
                }
            const bool atRoot = playerMeshAtKnown && rootMesh >= 0 &&
                playerMeshAt.size() >= static_cast<std::size_t>(rootMesh) * 3 + 3;
            if (atRoot)
                for (int k = 0; k < 3; ++k)
                    pp[k] = playerMeshAt[static_cast<std::size_t>(rootMesh) * 3 +
                                         static_cast<std::size_t>(k)];
            int jit[3];
            for (int k = 0; k < 3; ++k) {
                gun.randSeed = gun.randSeed * 214013u + 2531011u;
                jit[k] = static_cast<int>((gun.randSeed >> 16) & 0x7FFFu) % 100;
            }
            const omk::GunmanAim aim = omk::shootGunmanAim(pp, rs.muzzle, r, jit);
            // WHERE HIS GUN POINTS, so a gun that seems not to aim
            // at the player can be judged: the barrel - his
            // hand to the `tir` node, flat - against the line
            // from the muzzle to the player, once per gunman at
            // his SECOND shot. Measured over three gunmen at
            // medians -0.2 to -4.1 degrees with the aim as ported
            // and 12 to 13 off with its yaw negated. Not at the
            // first: `meshAt` is last frame's, drawn before his
            // aim layer first went live, so his first bolt leaves
            // an arm still at his side (-73 to -102 degrees) -
            // where the engine bends the arm (`sub_434C30`) in the
            // same gate call, before `Actor_TickProjectiles`. A
            // one-frame lag of this port's, labelled.
            if (hand >= 0 && s.meshAt.size() == nm * 3 &&
                gun.shots[s.actor] >= 1 && gun.barrelTold.insert(s.actor).second) {
                const std::size_t hh = static_cast<std::size_t>(hand);
                const double bx = rs.muzzle[0] - s.meshAt[hh * 3],
                             bz = rs.muzzle[2] - s.meshAt[hh * 3 + 2];
                const double tx = pp[0] - rs.muzzle[0], tz = pp[2] - rs.muzzle[2];
                double dd = (std::atan2(bx, bz) - std::atan2(tx, tz)) * omk::kRadToDeg;
                while (dd > 180.0) dd -= 360.0;
                while (dd < -180.0) dd += 360.0;
                std::printf("frame %ld: actor %d %s - HIS BARREL points %.1f degrees off "
                            "the line to the player\n", frameNo, s.actor, s.model.c_str(), dd);
            }
            rs.yawDeg = aim.yawDeg;
            rs.pitchDeg = aim.pitchDeg;
            const double dist = aim.dist;
            // the SHOT SPRITE by his gun's root name, as his
            int muzzleFx = 0;
            if (!stem.empty())
                if (const omk::FxShotSprite* sp =
                        shootSfx.shotSprite(gunFactsFor(stem).root)) {
                    muzzleFx = sp->muzzleEffect;
                    rs.impactEffect = sp->impactEffect;
                    rs.sprite = true;
                    rs.windUp = sp->windUp;
                    rs.grow = sp->grow;
                    for (int k = 0; k < 3; ++k) rs.growStep[k] = sp->growStep[k];
                }
            // no magazine pointer: an npc's count never gates
            // the round (only the player's asks for event 48),
            // and the write-back of HIS property 35 is not kept
            const omk::RecordShotOut out = projectiles.fireFromRecord(s.actor, *rec.weapon, rs);
            if (out.entry >= 0) {
                const long k = ++gun.shots[s.actor];
                shotSound(frameNo, muzzleFx, rs.muzzle, player->pos(), "a gunman's fire");
                // `sub_4246E0(him, the muzzle)` as the entry is made
                shootNoise(frameNo, s.actor, rs.muzzle, "a gunman's shot");
                const omk::Projectile& e =
                    projectiles.entries()[static_cast<std::size_t>(out.entry)];
                const float spd = e.speed != 0.0f ? e.speed : 1.0f;
                std::printf("frame %ld: GUNMAN SHOT %ld - actor %d %s, "
                            "Actor_TickProjectiles: entry %d, speed %.1f, damage %d, "
                            "dir %.3f %.3f %.3f (yaw %.1f pitch %.1f), from %s "
                            "%.1f %.1f %.1f, at the player's %s %.0f away, jitter %d %d %d "
                            "of r %.1f, %d live\n", frameNo, k, s.actor, s.model.c_str(),
                            out.entry, double(e.speed), e.kind,
                            double(e.vel[0] / spd), double(e.vel[1] / spd),
                            double(e.vel[2] / spd), double(rs.yawDeg),
                            double(rs.pitchDeg), from, double(rs.muzzle[0]),
                            double(rs.muzzle[1]), double(rs.muzzle[2]),
                            atRoot ? "root mesh" : "feet", dist,
                            jit[0], jit[1], jit[2], double(r), projectiles.live());
            } else {
                std::printf("frame %ld: GUNMAN SHOT refused - actor %d, the pool "
                            "is full (%d live)\n", frameNo, s.actor, projectiles.live());
            }
        }
    }
}


// LABEL_359: THE CLIP THE ARM PICKED (`+16`) - a turn clip 30..32 (from `worldStagedShoot`)
void PlayState::worldStagedShootTurnClip(const omk::ShootStep& st, Staged& s,
                                         omk::ShootRecord& rec, omk::ShootFrameIn& fin) {
    auto& session = *session_;
    if (st.turnClip >= 30 && st.turnClip <= 32) {
        const int grpT = static_cast<int>(session.typeOfActor(s.actor));
        const omk::PedClip* tc = (grpT >= 0 && grpT < 64)
                               ? shootClipExact(grpT, st.turnClip) : nullptr;
        if (tc && tc->frames > 0) {
            GunClip& gc = gun.clips[s.actor];
            gc.type = st.turnClip;
            s.walkMove[1] = 0.0f;   // `sub_421A20` sets the height
            gc.frames = tc->frames;
            gc.frame = 1.0f;
            gc.turn = st.turnTotal / static_cast<float>(tc->frames);
            rec.flags |= 8u;
            std::printf("frame %ld: actor %d %s - TURN CLIP (sub_421A20): type %d, "
                        "%d frames, %.2f a frame, from facing %.1f\n", frameNo, s.actor,
                        s.model.c_str(), gc.type, gc.frames, double(gc.turn),
                        double(s.facing));
        } else {
            s.facing += st.turnRate * fin.dt;
            if (s.facing < 0.0f) s.facing += 360.0f;
            if (s.facing >= 360.0f) s.facing -= 360.0f;
        }
    }
}


// THE PICKED CLIP'S TICK - `sub_424DE0`'s prologue: the gunman's clip, Astaroth's and
// Gandhar's played out (from `worldStagedShoot`)
void PlayState::worldStagedShootClipTick(bool inShoot, bool shotDead, bool brainsOff,
                                         const std::map<int, omk::ShootRecord>::iterator& deadIt,
                                         omk::play::Staged& s, bool astarothRec,
                                         omk::AstarothBand& astBand, bool& clipHolds, float astDt,
                                         bool gandharRec) {
    auto& session = *session_;
    if (inShoot && shootMode && !shotDead && !brainsOff && deadIt != shootBrains.end() &&
        (deadIt->second.flags & 8u)) {
        GunClip& gc = gun.clips[s.actor];
        // THE FRAME DELTA, `flt_4C30D8` - which the pause screen
        // forces to 0.0 (screen 31's open callback, `dword_4E9728`).
        // A literal 1.0 would keep a turn clip playing and turning
        // behind the pause menu.
        const float gunDt = astarothRec ? astBand.rate
                                        : static_cast<float>(frameSec * 30.0);
        const float pickedPrev = gc.frame;     // actor+192 = +188
        const float facingWas = s.facing;      // the node matrix is last tick's
        gc.frame += gunDt;
        s.facing += gc.turn * gunDt;
        if (s.facing < 0.0f) s.facing += 360.0f;
        if (s.facing > 360.0f) s.facing -= 360.0f;
        if (gc.type >= 0 && gc.frame < static_cast<float>(gc.frames)) {
            clipHolds = true;
            // ASTAROTH: `sub_421770` moves him by the picked clip's
            // root delta through the WALL TEST (`sub_421140(rec, {pos,
            // dx, dz}, 1)`: against a wall only the vertical) and
            // stamps his cell - the generic port leaves both out
            if (astarothRec) {
                omk::ShootRecord& ar = deadIt->second;
                omk::AstarothActor& aa = astarothActors[s.actor];
                const int grpP = static_cast<int>(ar.type);
                const omk::PedClip* pc = gc.slot >= 0 ? shootClipBySlot(grpP, gc.slot)
                                                      : shootClipExact(grpP, gc.type);
                if (aa.started && pc && pc->root.size() >= 3) {
                    float d0[3] = {0, 0, 0}, d[3];
                    omk::pedRootDelta(*pc, pickedPrev, gc.frame, nullptr, d0);
                    omk::rotateYaw(facingWas, d0, d);
                    float snap[2];
                    const bool wall = shootMap.valid() &&
                        omk::shootWallTest(ar, shootMap, aa.node[0], aa.node[2], d[0],
                                           d[2], 1, snap) != 0;
                    aa.node[1] += d[1];
                    if (!wall) {
                        aa.node[0] += d[0]; aa.node[2] += d[2];
                        aa.pos[0] += d[0];  aa.pos[2] += d[2];
                    }
                    astPlace(s, aa);
                }
                if (ar.state != 2) astarothStamp(ar);
            }
        } else if (astarothRec) {
            // ---- `sub_4800C0` 0x4801FC.., his clip played out ----
            omk::ShootRecord& ar = deadIt->second;
            ar.flags &= ~8u;                    // `sub_421770` cleared it
            const omk::AstarothClipOver co = omk::astarothClipOver(ar);
            if (co == omk::AstarothClipOver::Replay) {
                gc.frame = 1.0f;                // `+188 = 1.0; +192 = 0`
                clipHolds = true;
            } else if (co == omk::AstarothClipOver::Dead) {
                // `Game_RaiseEvent(43, {3, idx})` and return - the
                // sender mapped to his CHARACTERS id (message < 13)
                const bool ran = session.postMessage(3, s.actor);
                std::printf("frame %ld: actor %d %s - ASTAROTH DEAD (sub_4800C0 "
                            "0x480245): health %d, his clip played out - message 3 "
                            "%s\n", frameNo, s.actor, s.model.c_str(), ar.health,
                            ran ? "handled" : "unsubscribed");
                gc = GunClip{};
                clipHolds = true;
            } else {
                // ...his state's own clip back (`astarothResume`) and
                // on into the body the same tick
                gc = GunClip{};
                omk::AstarothActor& aa = astarothActors[s.actor];
                if (aa.started) {
                    const omk::AstarothWorld aw = astarothWorld(s, ar, astDt);
                    omk::astarothResume(aa, ar, s.facing, aw);
                    astPlace(s, aa);
                }
                std::printf("frame %ld: actor %d %s - his picked clip over (sub_4800C0): "
                            "state %d resumes with clip %d, health %d\n", frameNo, s.actor,
                            s.model.c_str(), ar.state, aa.clip, ar.health);
            }
        } else if (gandharRec && gandharActors.count(s.actor)) {
            // `sub_47F6F0`'s prologue, the picked clip played out:
            // `sub_421A20(+12, 0)` - his action's clip again from 1.0 -
            // and `o3de_SetNodePos(+244, +60, +252)`; the brain goes on
            // the same tick
            deadIt->second.flags &= ~8u;
            gc = GunClip{};
            omk::GandharActor& ga = gandharActors[s.actor];
            ga.frame = 1.0f;
            ga.prev = 0.0f;
            ga.node[0] = ga.pos[0];
            ga.node[1] = deadIt->second.groundY;
            ga.node[2] = ga.pos[2];
            for (int k = 0; k < 3; ++k) s.walkMove[k] = ga.node[k] - s.at[k];
            std::printf("frame %ld: actor %d %s - GANDHAR's picked clip over "
                        "(sub_47F6F0): action %d's clip %d again\n", frameNo, s.actor,
                        s.model.c_str(), deadIt->second.state, ga.clip);
        } else {
            deadIt->second.flags &= ~8u;
            std::printf("frame %ld: actor %d %s - picked clip over (sub_421770): "
                        "type %d after %.0f frames, facing %.1f\n", frameNo, s.actor,
                        s.model.c_str(), gc.type, double(gc.frame), double(s.facing));
            gc = GunClip{};
            // `sub_421A20(+12, 0)`: his previous clip back, at 1.0
            // - and the node's height SET again (below) - unless an
            // action was PARKED meanwhile (05_sys.c 5497: `if (flags &
            // 0x8000000) Shoot_ActorAction(him, +152, +132)`)
            if (deadIt->second.flags & 0x8000000u) {
                deadIt->second.flags &= ~0x8000000u;
                shootApplyAction(s, deadIt->second, deadIt->second.pendingAction,
                            deadIt->second.pendingArg, "parked, the picked clip over");
            } else {
                gun.anims[s.actor].frame = 1.0f;
            }
            s.walkMove[1] = 0.0f;
        }
    }
}


// SPDX-License-Identifier: GPL-3.0-or-later
// EVERY STAGED BODY, posed by whatever drives it.
// Parts of `PlayState::phaseWorld`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// THE MORPH ROOT TURNED BY A WORLD YAW, the engine's own composition.
// `08_wave.c` 963-965: `sub_442B50(v40, 0, X, 0)` builds the yaw and
// `sub_442940(&root, &yaw, out)` - the Hamilton product `root ⊗ yaw`
// (16_o3de.c 1704) - composes it onto the .3DM's root, per frame, BEFORE the
// fade against the node's current pose. Adjudicated against the port's own
// heading extractor on a real root: of the four candidate products only
// `stored ⊗ Ry(+H)` moves the heading by exactly H (338.0 for H = 338).
static void turnRootBy(omk::NodeTracks& t, float yawDeg) {
    if (!t.valid() || t.rootTrack < 0) return;
    const float h = yawDeg * 0.0174532925199433f * 0.5f;
    const omk::Quatf ry{std::cos(h), 0.0f, std::sin(h), 0.0f};
    for (auto& frame : t.quats)
        if (static_cast<std::size_t>(t.rootTrack) < frame.size())
            frame[static_cast<std::size_t>(t.rootTrack)] =
                omk::qmul(frame[static_cast<std::size_t>(t.rootTrack)], ry);
}

// Every staged body, posed by whatever drives it
void PlayState::worldStaged() {
    OMK_ZONE("world: staged");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    omk::Renderer& world = *world_;
    // ---- EVERY STAGED BODY, POSED BY WHATEVER DRIVES IT --------
    //
    // Three sources in the engine's own precedence - the program that
    // names HIM, then the conversation line, then the bank's idle -
    // and the whole point of issue 41 is that the first is resolved
    // PER ACTOR. The file used to take the last running actor program
    // for its one body, so while `A_2_DemonLook` ran that body wore
    // the Demon's clip whoever it was.
    const omk::SceneRunner& sc = session.scene();
    const int convSpeaker = session.dialogOpen()
        ? session.dialogue().conversation().speaker : -1;
    bool convOwned = false;
    for (const auto& up : staged) if (up->actor == convSpeaker) convOwned = true;
    // A running program whose actor is nobody on screen. AREA 118's
    // arrival is one: the shown list carries Kay'l as 310 and the
    // program names another id, so with no fallback the intro would
    // lose its pose. It drives the frame's ONLY body, and can never
    // mis-assign once there is more than one - which is the case the
    // issue is about. LABELLED: a reconstruction, not a rule read out
    // of the engine.
    // WHICH BODY A PROGRAM DRIVES. `scx.play.actor` names a
    // CHARACTERS id in its first field (`ScriptObject_StartOnActor`);
    // `scx.play.player` names none and drives the PLAYER - and the
    // Impasse's `A_1_KaylArrives` is one of those, so keying on
    // `how == "actor"` alone left Kay'l standing in his idle through
    // his own arrival.
    const int playerId = session.playerActor();
    const auto drivenBy = [&](const omk::SceneRunner::Started& t, int actorId) {
        if (t.clip < 0) return false;
        if (t.how == "actor")  return t.actor == actorId;
        if (t.how == "player") return actorId == playerId;
        return false;
    };
    int loneProg = -1, loneProgs = 0;
    for (std::size_t k = 0; k < sc.started().size(); ++k) {
        const auto& t = sc.started()[k];
        if (t.clip < 0 || (t.how != "actor" && t.how != "player")) continue;
        if (!sc.programRunning(static_cast<int>(k))) continue;
        bool owned = false;
        for (const auto& up : staged) if (drivenBy(t, up->actor)) owned = true;
        if (owned) continue;
        loneProg = static_cast<int>(k);
        ++loneProgs;
    }
    bool firstBody = true;
    int stagedFar = 0;                 // beyond the clip distance: not skinned
    int stagedOff = 0;                 // outside the view's side planes: not skinned
    // THE VIEW'S FOUR SIDE PLANES - the second half of the visible-set
    // walk's reject. `sub_48D3B0` (scene objects: the set's meshes and
    // the staged actors) and `sub_48D7F0` (the street crowd's
    // instances) both follow the distance test with
    // `n . c + d > r` on the four planes `sub_48D0D0` builds, `c` the
    // node's centre and `r` its `+88` radius, and skip the node whole -
    // no animation bind, no matrices, no vertices. This port took the
    // distance half only, so everything behind the camera inside the
    // clip distance was posed and drawn (`optimization.md` step 28 j).
    //
    // Conservative where the engine's cannot be matched exactly: a
    // ROLLED camera gets a frustum whose both half-extents are the view
    // rectangle's half-diagonal, which contains the rolled view at any
    // angle; the height is the larger of the camera's and the
    // letterbox strip's. And OFF where a pass draws the scene from
    // elsewhere: a live MIRROR re-draws the same draws reflected, and
    // mapped shadows (an enhancement) draw casters the eye cannot see.
    // A correct cull leaves every frame byte-identical - the test.
    // `OMK_NO_SIDECULL=1` turns it off, for that comparison.
    static const bool noSideCull = std::getenv("OMK_NO_SIDECULL") != nullptr;
    bool mirrorShown = false;
    for (const auto& ws : worldSlots) if (ws.shown && ws.mirror.found) mirrorShown = true;
    sideCullSet = !noSideCull && !mirrorShown && view.cam.hfovDeg < 179.0f &&
                             view.cam.w > 0 && view.cam.h > 0;
    sideCullBodies = sideCullSet && !(drawShadows && shadowQuality >= 2);
    sideFr = omk::Frustum{};
    if (sideCullSet) {
        const int fw = view.cam.w, fh = std::max(view.cam.h, view.vh);
        if (view.cam.rollDeg == 0.0f) {
            sideFr = omk::frustumFromFov(view.cam.eye, view.cam.at, view.cam.hfovDeg, fw, fh, 1000.0f);
        } else {
            const double tanH = std::tan(view.cam.hfovDeg * 3.14159265358979 / 360.0);
            const double diag = tanH * std::sqrt(1.0 + double(fh) * fh / (double(fw) * fw));
            const float proj = static_cast<float>(1.0 / (2.0 * diag));
            sideFr = omk::frustumFromCamera(view.cam.eye, view.cam.at, proj, proj, 1, 1, 1000.0f);
        }
    }
    for (auto& up : staged) {
        Staged& s = *up;
        // everything before the skinning: the pose SOURCE, the scene
        // runner's queries, the clip, the look-at, the ground probe
        const double stagedResolve0 = phaseNow();
        s.drawn = false;
        if (!s.mo || !s.mo->ready) {
            // A model with nothing to draw. `PA1_FN.3DO` is 1236
            // bytes: ONE mesh, `PBassin`, 8 vertices and 12 triangles,
            // flags 0x1 - which the engine's own drawable test
            // (`flags & 0x800043`, ASSETS 4) rejects. The alley's
            // three passers-by are that model, so they have no body in
            // the shipped data either; this is not a gap in the
            // viewer.
            if (!s.placeTold) {
                s.placeTold = true;
                std::printf("frame %ld: actor %d %s has no drawable geometry "
                            "(%zu meshes, %zu corners after the drawable mask) - "
                            "nothing to stage\n", n, s.actor, s.model.c_str(),
                            s.mo ? s.mo->meshes.size() : 0u,
                            s.mo ? s.mo->rest.corners.size() : 0u);
            }
            continue;
        }
        // (a) THE PROGRAM THAT NAMES HIM - IN EITHER RESIDENT POOL.
        //
        // `Session::reloadScene` records why: an area transition does
        // not call `Scene_LoadSCX`, so every running program survives
        // it and the OUTGOING pool goes on being ticked. This file
        // already reads set-mesh MOTIONS from both (the tunnel door,
        // which closes behind the player out of `sceneOut()`), and
        // read the POSE from the active one alone - so the moment the
        // resident `.SCX` swapped, every body the outgoing scene was
        // animating fell to its bank idle, or with no bank to the REST
        // pose. Walking Anekbah -> the restaurant that is 17 of
        // Anekbah's street bodies frozen while both sets are still
        // drawn. The active pool is tried FIRST, so a body both pools
        // could claim keeps the incoming scene's program.
        const omk::SceneRunner* run = &sc;
        int prog = -1;
        static const bool activeOnly =
            omk::envSet("OMK_ACTIVE_POOL_ONLY");   // the old, wrong lookup
        for (const omk::SceneRunner* cand : {&sc, &session.sceneOut()}) {
            if (activeOnly && cand != &sc) break;
            if (!cand->loaded()) continue;
            for (std::size_t k = 0; k < cand->started().size(); ++k) {
                const auto& t = cand->started()[k];
                if (!drivenBy(t, s.actor)) continue;
                if (!cand->programRunning(static_cast<int>(k))) continue;
                prog = static_cast<int>(k);
                run = cand;
            }
            if (prog >= 0) break;
        }
        // ...AND THE FALLBACK IS THE PLAYER'S ALONE.
        //
        // It used to fire for whoever the frame's one body was, on the
        // reasoning quoted above that it "can never mis-assign once
        // there is more than one". A reader dying in the supermarket
        // found the case that reasoning misses: the loss branch hides
        // twenty-five characters (`todo/shoot-phase-end.md`), so ONE
        // is exactly what is left - actor 65, the cashier the intro
        // cutscene stages - and the frame's one running program, clip
        // 18 `BOIPATH2.3DA`, which names somebody else entirely, was
        // handed to him. It teleported him from 13283 -92 996 to
        // 13046 -140 1149 and posed him from a stranger's clip, and
        // since V5H_FNM carries no bank he fell to the model's REST
        // pose - a T-posed body standing in a corridor nothing put
        // him in. It is in the reader's own session log three times.
        //
        // It is kept for the PLAYER alone, because that is the case
        // it was written for and the one where an id mismatch is
        // plausible - `scx.play.player` names no actor at all. Note
        // what the measurement says about the case the comment above
        // cites: AREA 118's beat is `character.show 310, 1` then
        // `scx.play.actor.wait 310, 1`, so the program DOES name the
        // body and `drivenBy` matches it without any fallback -
        // `engine: intro beat` and `engine: intro` are both green with
        // the narrowing in. Nobody but the player gets it.
        bool byLone = false;
        if (prog < 0 && loneProgs == 1 && staged.size() == 1 &&
            s.actor == playerId) {
            prog = loneProg;
            run = &sc;                 // the lone-program fallback is the ACTIVE pool's
            byLone = true;
        }
        // Which pool answered, said once per change - a body that
        // swaps pools mid-transition is worth seeing in the log.
        if (run != s.poolWas) {
            s.poolWas = run;
            if (prog >= 0 && stagedProbe)
                std::printf("    pool %ld actor %d %s driven by the %s pool (%s)\n",
                            n, s.actor, s.model.c_str(),
                            run == &sc ? "ACTIVE" : "OUTGOING", run->file().c_str());
        }
        int sceneClip = -1, scenePath = -1;
        float sceneFrame = 0.0f;
        const omk::SceneRunner::Started* stt = nullptr;
        if (prog >= 0) {
            stt = &run->started()[static_cast<std::size_t>(prog)];
            // Nothing is posed or placed before the program's first
            // body-animation step RUNS (`Started::animReached`): an
            // object that opens with a wait leaves its actor where
            // the world has him - Kay'l under the alley at address
            // 654 for the 60 frames before his jump out of the portal.
            sceneClip = stt->animReached ? stt->clip : -1;
            scenePath = stt->animReached ? stt->path : -1;
            // The frame of THAT clip, not of the program: a program
            // walks its steps and each may name a different animation,
            // so the clock the pose is sampled at counts from the step
            // (`SceneRunner::programAnimClock`).
            sceneFrame = run->programAnimClock(prog);
            // THE FACING, from the step the pc is on - and from BOTH
            // kinds of body animation, because both end in
            // `Actor_SetEuler(node, p4, p5, p6)`. It is not sticky:
            // the waiter's walk clips author 0 and turn him back to 0,
            // and reading only the relative call's Euler rotated his
            // whole route by the 145 his standing step had left.
            static const bool stickyEuler =
                omk::envSet("OMK_STICKY_EULER");   // the old, wrong reading
            if (stt->animReached && (!stickyEuler || stt->relative)) {
                s.progYaw = stt->euler[1];
                s.progYawKnown = true;
            }
        }
        if (sceneClip != s.sceneClipWas) {
            // THE HAND-OVER GAP. A program's steps are authored to
            // chain: each clip's placement is where the previous one
            // left the body, so a correct run re-places him on the
            // spot he already occupies and nothing visibly moves.
            // Where the accumulated root motion is wrong the two
            // disagree and the next step YANKS him - "he is teleported
            // sometimes" (a reader, 2026-09-05). The gap is therefore
            // a measure of the root motion, independent of the walk
            // mesh: two chains that must agree.
            const float wasAt[3] = {s.drawAt[0], s.drawAt[1], s.drawAt[2]};
            const bool hadOne = s.progRan && s.sceneClipWas >= 0;
            s.sceneClipWas = sceneClip;
            s.sceneTracks = omk::NodeTracks{};
            if (sceneClip >= 0 && run->loaded())
                s.sceneTracks = omk::clipTracks(run->scene().clipData(sceneClip));
            if (scenePath >= 0 && run->loaded() &&
                scenePath < static_cast<int>(run->scene().paths().size())) {
                const auto& pa = run->scene().paths()[static_cast<std::size_t>(scenePath)];
                if (!pa.keys.empty()) {
                    // `Path_Sample(path, 1.0, ..., 1)` - the engine's
                    // own call, mode 1 being LINEAR: find the key span
                    // containing t and lerp. Not the first key.
                    const float t = 1.0f;
                    float p[3] = {pa.keys.front().pos[0],
                                  pa.keys.front().pos[1],
                                  pa.keys.front().pos[2]};
                    for (std::size_t i = 0; i + 1 < pa.keys.size(); ++i) {
                        const float a = static_cast<float>(pa.keys[i].frame);
                        const float b = static_cast<float>(pa.keys[i + 1].frame);
                        if (t < a || t > b) continue;
                        const float u = b > a ? (t - a) / (b - a) : 0.0f;
                        for (int k = 0; k < 3; ++k)
                            p[k] = pa.keys[i].pos[k] +
                                   (pa.keys[i + 1].pos[k] - pa.keys[i].pos[k]) * u;
                        break;
                    }
                    // ...plus the call's own offset, params 9/10/11 in
                    // INCHES, which `placementOf` has already divided.
                    for (int k = 0; k < 3; ++k)
                        s.at[k] = p[k] + (stt ? stt->offset[k] : 0.0f);
                    s.placed = true;
                    s.pelvis = true;    // an authored path names the PELVIS
                    s.progPlaced = true; s.progPelvis = true;
                    s.progRan = true;
                    // ...and the FACING is the call's Euler, written to the
                    // node every tick; the clip's root quaternion sits under
                    // it. Anekbah's Kiss couples say -70, the walkers 180 -
                    // without it a couple placed right still stood turned
                    // away and intersecting (a reader's frame, 2026-09-03).
                    // Pitch and roll (params 4 and 6) are rarely non-zero
                    // (three beggars carry -7 of pitch) and are not applied.
                    for (int k = 0; k < 3; ++k) s.progBase[k] = s.at[k];
                    for (float& w : s.walkMove) w = 0.0f;   // the program places him
                }
                std::printf("  pose: actor %d %s - clip %d '%s' (%d frames) on path "
                            "%d '%s' at %.0f %.0f %.0f%s\n", s.actor, s.model.c_str(),
                            sceneClip, run->scene().clipName(sceneClip).c_str(),
                            s.sceneTracks.frames, scenePath, pa.name.c_str(),
                            s.at[0], s.at[1], s.at[2],
                            byLone ? "  [the program names another actor; this is "
                                     "the frame's only body - LABELLED]" : "");
            } else if (sceneClip >= 0 && run->loaded()) {
                // No path: the plain `Script_SelectBodyAnimation`
                // (0x02000004) SNAPS the node to the clip's root key 0
                // - the authored placement, a PELVIS point whose
                // height is kept - and `Anim_RootDelta` sums the keys
                // after it (CLAUDE.md 6; `Session::trackPlayer` is the
                // same rule for the player). Until 2026-09-03 the body
                // stayed on its 20-byte placement record instead, and
                // the Impasse's beat cameras, which frame the clip
                // roots, framed nobody: Kay'l's arrival clip starts at
                // 6462 -121 3215 while his record puts him at 7217.
                float base[3] = {0, 0, 0};
                if (omk::clipRootStart(run->scene().clipData(sceneClip), base)) {
                    // ...plus the call's own offset, params 7/8/9 in
                    // inches: `x = node.x - GetParamFloatB(fn, 7) *
                    // -0.39370078`. Zero at every shipped site read so
                    // far, but it is the field the function reads.
                    for (int k = 0; k < 3; ++k)
                        base[k] += stt ? stt->offset[k] : 0.0f;
                    for (int k = 0; k < 3; ++k) s.at[k] = base[k];
                    s.placed = true;
                    s.pelvis = true;
                    s.progPlaced = true; s.progPelvis = true;
                    s.progRan = true;
                    for (int k = 0; k < 3; ++k) s.progBase[k] = base[k];
                    for (float& w : s.walkMove) w = 0.0f;   // the program places him
                    std::printf("  pose: actor %d %s - clip %d '%s' (%d frames), no "
                                "path: snapped to its root key 0 at %.0f %.0f %.0f%s\n",
                                s.actor, s.model.c_str(), sceneClip,
                                run->scene().clipName(sceneClip).c_str(),
                                s.sceneTracks.frames, s.at[0], s.at[1], s.at[2],
                                byLone ? "  [the program names another actor; this "
                                         "is the frame's only body - LABELLED]" : "");
                }
            } else if (sceneClip < 0 && stt && !stt->animReached) {
                // NOT YET: the program has not reached a body-animation
                // step, so the engine has not touched the actor. He is
                // where the world has him - for the player, the DB
                // position (`actor.goto_address` parks Kay'l at address
                // 654, under the alley, before his jump); for anyone
                // else, the placement record.
                s.progPlaced = false;
                if (stt->how == "player") {
                    const float* pp = session.playerPos();
                    for (int k = 0; k < 3; ++k) s.at[k] = pp[k];
                    s.placed = true;
                    s.pelvis = false;
                }
                std::printf("  pose: actor %d %s - its program has not reached an "
                            "animation yet; he stays where the world has him "
                            "(%.0f %.0f %.0f)\n", s.actor, s.model.c_str(),
                            s.at[0], s.at[1], s.at[2]);
            } else if (sceneClip < 0 && s.progRan) {
                // The program ended. `Script_SelectBodyAnimation` never
                // resets the node, so the accumulated offset STANDS
                // (CLAUDE.md 6): he stays where the clip left him and
                // falls back to the bank's idle there.
                s.progPlaced = false;
                if (s.placed) {  // drawAt is last frame's; `drawn` was just cleared
                    for (int k = 0; k < 3; ++k) s.at[k] = s.drawAt[k];
                    for (float& w : s.walkMove) w = 0.0f;   // drawAt carries it
                }
                std::printf("  pose: actor %d %s - its program ended; he stays where "
                            "it left him (%.0f %.0f %.0f) and falls back to the "
                            "bank's idle\n", s.actor, s.model.c_str(),
                            s.at[0], s.at[1], s.at[2]);
            } else if (sceneClip < 0) {
                // NO program ever drove this body. `drawAt` is not
                // "where the program left him" - on the first frame it
                // has never been written - so the placement record the
                // chunk gives him STANDS, untouched. Gandhar's cave is
                // the case: twelve ZOH_FN whose actor records carry no
                // .CTL at all and whom only `shoot.actor.enter` names,
                // every one of them teleported to 0 0 0 on frame 0.
                s.progPlaced = false;
            }
            if (stagedProbe && hadOne && sceneClip >= 0) {
                const float dx = s.at[0] - wasAt[0], dz = s.at[2] - wasAt[2];
                std::printf("    handover %ld actor %d %s  %.0f %.0f -> %.0f %.0f"
                            "   gap %.0f\n", n, s.actor, s.model.c_str(),
                            wasAt[0], wasAt[2], s.at[0], s.at[2],
                            std::sqrt(dx * dx + dz * dz));
            }
        }
        // (b) THE CONVERSATION'S LINE, for its speaker only. The
        // DIALOG chunk's word 0 is the speaker's actor id; when no
        // staged body carries it the model name is the fallback,
        // which is what the file matched on before there were ids.
        const bool isSpeaker = session.dialogOpen() && speakerReady &&
            (s.actor == convSpeaker ||
             (!convOwned && !speakerModel.empty() && s.model == speakerModel));
        const float lineT = (isSpeaker && speakerTracks.valid())
            ? static_cast<float>(session.dialogue().elapsed() * 30.0) : -1.0f;
        const bool useLine = lineT >= 0.0f &&
                             lineT < static_cast<float>(speakerTracks.frames);
        if (isSpeaker) sceneFrameLast = sceneFrame;
        // (c) THE IDLE, built once: the default group's default entry's
        // clip at FRAME 0.
        if (!s.idleBuilt) {
            s.idleBuilt = true;
            if (s.bk && s.bk->ready) s.idle = idleTracksFor(*s.bk, s.mo->meshes);
        }
        // (c0) SHOOT MODE, ahead of the bank's idle: `Shoot_ActorEnter`
        // puts him in ACTOR_STATE 3 and his clips come from the area's
        // `.ani`, not from a `.CTL` he does not have.
        const omk::NodeTracks* shootTracks = nullptr;
        int shootFrame = 0;    // a death clip plays through; the rest hold frame 0
        {
            const int act = session.shootAction(s.actor);
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
            const auto deadIt = shootBrains.find(s.actor);
            const bool shotDead = deadIt != shootBrains.end() &&
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
            // SHOOT_ACTORACTION (05_sys.c 3938, read 2026-09-11): an action -
            // his scene action at birth, what the brain asks for, or a
            // request parked under flag 8 - becomes his CURRENT clip's type,
            // a state, flags and a timer (`omk::shootActorAction`)
            const auto applyAction = [&](omk::ShootRecord& ar, int action, int a3,
                                         const char* why) {
                const int grpX = static_cast<int>(session.typeOfActor(s.actor));
                const auto o = omk::shootActorAction(
                    ar, action, a3,
                    [&](int t) {
                        return grpX >= 0 && grpX < 64 && shootClipExact(grpX, t) != nullptr;
                    },
                    [&]() {
                        gunRandSeed = gunRandSeed * 214013u + 2531011u;
                        return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
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
                    static std::set<int> patrolTold;
                    if (patrolTold.insert(s.actor).second)
                        std::printf("frame %ld: actor %d %s - PATROLS (Shoot_ActorAction 1): "
                                    "operand %d -> route %d on floor %d, point 0 at %.0f %.0f, "
                                    "%d points%s\n", n, s.actor, s.model.c_str(), a3,
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
                    gunCurType[s.actor] = o.clipType;
                    gunCurSlot.erase(s.actor);
                    gunAnims[s.actor].clip = nullptr;   // `sub_421A20`: from 1.0, the same clip too
                }
                // logged when the action, the clip or the state CHANGES - a
                // request repeated every tick says nothing new
                static std::map<int, std::array<int, 3>> actionTold;
                const std::array<int, 3> key{action, o.clipType, ar.state};
                if (auto at = actionTold.find(s.actor); at == actionTold.end() || at->second != key) {
                    actionTold[s.actor] = key;
                    std::printf("frame %ld: actor %d %s - ACTION %d (Shoot_ActorAction, %s): %s "
                                "clip type %d, state %d, timer %.0f\n", n, s.actor,
                                s.model.c_str(), action, why,
                                o.parked ? "PARKED under flag 8 -" : "", o.clipType, ar.state,
                                double(ar.timer));
                }
            };
            // THE STAND-DOWN (`sub_423FC0`, the player's death): action 0 -
            // or, on script step 8, his 0x20 latch cleared - told in the
            // hit and applied here, on his own tick
            if (deadIt != shootBrains.end() && gunStandDown.erase(s.actor)) {
                omk::ShootRecord& sr = deadIt->second;
                if (sr.scriptStep == 8) sr.flags &= ~0x20u;
                else if (!(sr.flags & 0x4000u))
                    applyAction(sr, 0, 0, "the player's death (sub_423FC0)");
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
            if (auto gaL = gunAims.find(s.actor); gaL != gunAims.end()) gaL->second.live = false;
            bool clipHolds = false;
            if (act >= 0 && shootMode && !shotDead && deadIt != shootBrains.end() &&
                (deadIt->second.flags & 8u)) {
                GunClip& gc = gunClips[s.actor];
                // THE FRAME DELTA, `flt_4C30D8` - which the pause screen
                // forces to 0.0 (screen 31's open callback, `dword_4E9728`).
                // This was a literal 1.0, so a turn clip went on playing
                // and turning behind the pause menu (a reader, 2026-09-12:
                // "the ennemies continue moving in the pause menu").
                const float gunDt = static_cast<float>(frameSec * 30.0);
                gc.frame += gunDt;
                s.facing += gc.turn * gunDt;
                if (s.facing < 0.0f) s.facing += 360.0f;
                if (s.facing > 360.0f) s.facing -= 360.0f;
                if (gc.type >= 0 && gc.frame < static_cast<float>(gc.frames)) {
                    clipHolds = true;
                } else {
                    deadIt->second.flags &= ~8u;
                    std::printf("frame %ld: actor %d %s - picked clip over (sub_421770): "
                                "type %d after %.0f frames, facing %.1f\n", n, s.actor,
                                s.model.c_str(), gc.type, double(gc.frame), double(s.facing));
                    gc = GunClip{};
                    // `sub_421A20(+12, 0)`: his previous clip back, at 1.0
                    // - and the node's height SET again (below) - unless an
                    // action was PARKED meanwhile (05_sys.c 5497: `if (flags &
                    // 0x8000000) Shoot_ActorAction(him, +152, +132)`)
                    if (deadIt->second.flags & 0x8000000u) {
                        deadIt->second.flags &= ~0x8000000u;
                        applyAction(deadIt->second, deadIt->second.pendingAction,
                                    deadIt->second.pendingArg, "parked, the picked clip over");
                    } else {
                        gunAnims[s.actor].frame = 1.0f;
                    }
                    s.walkMove[1] = 0.0f;
                }
            }
            if (act >= 0 && shootMode && !shotDead && !clipHolds) {
                auto it = shootBrains.find(s.actor);
                if (it == shootBrains.end()) {
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
                    omk::initShootRecord(fresh, sp);
                    // +80, the character type - the hit's gates test it
                    // (type 11 takes the baton, 12 never reacts)
                    fresh.type = session.typeOfActor(s.actor);
                    // +112..+124: the slots of his group's TYPE-12 clips,
                    // in list order (`sub_4347A0` / `sub_434860`), up to four
                    if (!pedAni.empty() && fresh.type < 64u) {
                        int k = 0;
                        for (const auto& c : omk::animGroupClips(pedAni, static_cast<int>(fresh.type))) {
                            if (!c.slot) break;
                            if (c.type == 12 && k < 4) fresh.attacks[k++] = c.slot;
                        }
                        if (k)
                            std::printf("frame %ld: actor %d %s - ATTACKS (Shoot_ActorEnter +112): "
                                        "%d type-12 clip%s, first slot %d\n", n, s.actor,
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
                    // `Shoot_ActorEnter` thinks FIRST and acts second, and
                    // the order is load-bearing for the patrol: the route
                    // lookup takes his floor and cell. This port cannot
                    // always honour it on the entry tick - a body staged
                    // this frame has not been drawn, so it has no position
                    // (`todo/shoot-patrol.md` 4b) - so the action WAITS for
                    // the first tick he lands on the grid. One frame, and
                    // the alternative is a patrol with a null route.
                    if (!onGridAtEntry && shootMap.valid()) {
                        gunEntryPending.insert(s.actor);
                        std::printf("frame %ld: actor %d %s - entry action %d HELD: he is "
                                    "not on the grid yet (Shoot_ActorEnter thinks first)\n",
                                    n, s.actor, s.model.c_str(), act);
                    }
                    // ...and put on his SCENE action, as `Shoot_ActorEnter`
                    // does - for the robbers, action 3: the hub, the walking
                    // clip, and +168 = 30 * property 31 frames of advance
                    // (LABELLED: `Shoot_ActorEnter`'s own call is not re-read;
                    // this passes the action with a3 = 0)
                    if (onGridAtEntry || !shootMap.valid())
                        applyAction(it->second, act, session.shootActionArg(s.actor),
                                    "his scene action, at entry");
                    std::printf("frame %ld: actor %d %s - shoot brain: "
                                "acquire %.0f engage %.0f disengage %.0f "
                                "cone %.3f health %d\n", n, s.actor,
                                s.model.c_str(), it->second.rangeAcquire,
                                it->second.rangeInner, it->second.rangeThird,
                                it->second.coneCos, it->second.health);
                }
                omk::ShootRecord& rec = it->second;
                // ---- THE BRAIN'S PROLOGUE, `sub_424DE0` 5456 -------
                // His cell's own byte goes back before anything reads
                // the grid: `if (state != 2 || health < 0) { saved =
                // +189; sub_47C230(floor, saved); sub_435970(floor,
                // +136, +140, saved); }`. Without it a walker's 0x80
                // stays on the cell he left and blocks it for ever -
                // which is what this port did until 2026-09-12, and it
                // was invisible while no gunman moved. (`sub_47C230`,
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
                // The floor was the memset's 0 until 2026-09-12, which
                // is right by luck on the two one-floor arenas this
                // port can reach and wrong on the other eleven.
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
                // engaging. Feeding him his PLACEMENT instead was tried
                // on 2026-09-12 and makes it worse, not better: he
                // engages and FIRES from a body that has no pose, so
                // the muzzle - the posed `tir` node - is the world
                // origin. The two have to agree, and agreeing on the
                // drawn point is the conservative half. Closing it
                // properly means posing a staged body whether or not it
                // is drawn, which is not this task's.
                if (shootMap.valid()) {
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
                    // The port had fed `Shoot_Think` the DRAWN y, which
                    // drifts with the ground probe: two of the
                    // catacombs' nine spectres sank three quarters of a
                    // metre through their own floor by frame 72, lost
                    // it, and with it the wall test - one walked out
                    // through a wall, which a reader saw.
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
                                    "%.0f) and does not leave it\n", n, s.actor,
                                    s.model.c_str(), double(rec.groundY), fl0,
                                    double(shootMap.floors()[
                                        static_cast<std::size_t>(fl0)].bound[3]),
                                    double(rec.height));
                    }
                    if (onGrid && gunEntryPending.erase(s.actor))
                        applyAction(rec, act, session.shootActionArg(s.actor),
                                    "his scene action, held until he reached the grid");
                    static std::set<int> floorTold;
                    if (onGrid && floorTold.insert(s.actor).second)
                        std::printf("frame %ld: actor %d %s - Shoot_Think: floor %d, cell "
                                    "(%d,%d)\n", n, s.actor, s.model.c_str(),
                                    static_cast<signed char>(rec.node & 0xFF),
                                    rec.destX, rec.destZ);
                    // ...and when he LEAVES it, which is the state the
                    // engine's `sub_4368E0` exists to undo: with no
                    // floor there is no wall test and no steering, so a
                    // walker just keeps going. Said once per actor.
                    static std::set<int> offGridTold;
                    if (static_cast<signed char>(rec.node & 0xFF) < 0 &&
                        floorTold.count(s.actor) && offGridTold.insert(s.actor).second)
                        std::printf("frame %ld: actor %d %s - OFF THE GRID at %.0f %.0f "
                                    "%.0f: Shoot_Think finds no floor, so nothing walls "
                                    "him in (sub_4368E0 is the engine's recovery and runs "
                                    "only at his entry)\n", n, s.actor, s.model.c_str(),
                                    double(at[0]), double(at[1]), double(at[2]));
                }
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
                // `uiPause` above). A literal 1.0 here kept every brain,
                // clip and walk step running behind the menu.
                fin.dt = static_cast<float>(frameSec * 30.0);
                fin.defaultClipType = act;
                // THE STEERING (`sub_421CD0`, read 2026-09-11): the hub's
                // middle arm turns him down the path field toward the
                // player. The heading, and the CRT draws the brain makes
                // itself, are handed in LAZILY - the engine reaches them
                // only on the ticks the arm runs, and the draws are the
                // same `rand()` the bolts' jitter comes from
                auto crt = [&]() {
                    gunRandSeed = gunRandSeed * 214013u + 2531011u;
                    return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
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
                    static std::set<int> steerTold;
                    if (h >= 0 && steerTold.insert(s.actor).second)
                        std::printf("frame %ld: actor %d %s - STEERS by the path field "
                                    "(sub_421CD0 -> sub_435C40): heading %d from his cell "
                                    "(%d,%d), facing %.1f\n", n, s.actor, s.model.c_str(), h,
                                    rec.destX, rec.destZ, double(s.facing));
                    return h;
                };
                fin.targetAlive = true;
                // THE GRID SIGHT, `sub_4359A0(floor, targetCell, ownCell, 1)`
                // - what `sub_426E00`'s GENERAL arm acquires on
                // (`todo/shoot-sight.md` 1; the 2026-09-12 note that called
                // the ray this arm's sight was wrong). His floor `+188`, -1
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
                                        "none - he is on no floor\n", n, s.actor,
                                        s.model.c_str());
                        else
                            std::printf("frame %ld: actor %d %s - GRID SIGHT (sub_4359A0, "
                                        "target->self) from the player's (%d,%d) to his "
                                        "(%d,%d) on floor %d: %s", n, s.actor,
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
                // ...and since 2026-09-13 it is the PLAYER'S RECORD's `+188`,
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
                                "same %d, latched %d\n", n, s.actor, s.model.c_str(),
                                gunFloor, playerFloor, int(ein.sameNode),
                                int((rec.flags & 0x20u) != 0));
                }
                static std::set<int> crossTold;
                if (!ein.sameNode && crossTold.insert(s.actor).second)
                    std::printf("frame %ld: actor %d %s - the player is on ANOTHER FLOOR "
                                "(%d against his %d)%s\n", n, s.actor, s.model.c_str(),
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
                        static std::set<int> linkTold;
                        if ((rec.flags & 0x20u) && linkTold.insert(s.actor).second)
                            std::printf("frame %ld: actor %d %s - takes the STAIR %d of "
                                        "floor %d: its near end %.0f %.0f %.0f, its far "
                                        "end %.0f %.0f %.0f on floor %u\n", n, s.actor,
                                        s.model.c_str(), ein.link, gunFloor,
                                        double(L.from[0]), double(L.from[1]),
                                        double(L.from[2]), double(L.to[0]),
                                        double(L.to[1]), double(L.to[2]), L.destFloor);
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
                // Until 2026-09-13 this was forced TRUE, which kept every
                // close gunman in state 6 and never let him close to 13/8.
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
                    for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                        if (playerMeshes[i].parent < 0) {
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
                                        "%s: %s, %s cone\n", n, s.actor, s.model.c_str(),
                                        double(ao.dist3d),
                                        spectreArm ? "the SPECTRE's sight (type 12)"
                                                   : "the CROSS-FLOOR watcher's sight (0x800000)",
                                        !sightSlot ? "no set to cast over"
                                        : ein.rayHits ? "HITS the set" : "CLEAR",
                                        (spectreArm ? cone : wideCone) ? "inside his"
                                                                       : "outside his");
                        else
                        std::printf("frame %ld: actor %d %s - RAY (sub_4449E0) at %.1f, inside "
                                    "half his inner range %.1f: %s\n", n, s.actor,
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
                        gunRandSeed = gunRandSeed * 214013u + 2531011u;
                        return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
                    });
                const int preEngageState = rec.state;
                // (and never from STATE 10: `sub_424DE0` has no engage call
                // in that arm, and one here would rewrite a striking gunman's
                // state mid-clip)
                const int eng = (((spectreArm || watchArm) && !engineCalls) ||
                                 rec.state == 10)
                                    ? 0 : omk::shootEngage(rec, ao, cone, ein);
                // THE ENGAGE IS CALLED INSIDE AN ARM in the engine, so a
                // gunman it sends to the 10/11 pair (`goPair`) does not run
                // state 10 until the NEXT tick - by which `sub_4272B0` has
                // started his attack clip. This viewer runs the engage BEFORE
                // the step (the hoist `todo/handoff-shoot-mode.md` §4 item 6
                // labels), and running state 10 on the same tick read the OLD
                // clip's frames as spent and sent him on to 11 at once - robber
                // 77 and dog 598 both did (2026-09-13). So the tick the pair is
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
                                    "now walking to %.0f %.0f%s\n", n, s.actor,
                                    s.model.c_str(), was, rec.repeats,
                                    rec.route < static_cast<int>(wl.size())
                                        ? int(wl[static_cast<std::size_t>(rec.route)].len)
                                        : -1,
                                    double(rec.goalX), double(rec.goalZ),
                                    clip > 0 ? " - it carries a CLIP: state 5" : "");
                    }
                }
                // ---- THE EDGE states 1 and 2 WALK ------------------
                // `u32(rec, 4)`'s two points. State 1 steers at the
                // goal and, on its step, writes the heading and length
                // from these and hands over to state 2; state 2 walks
                // the edge itself, climbing `(to.y - from.y)/dist` and
                // at the far end taking the link's own destination
                // floor as his new `+188`.
                fin.myNode = gunFloor;
                fin.targetNode = playerFloor;
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
                // ...and the clip clock, which state 5 reads - and the 10/11
                // pair (the attack clip's first half is outcome 3, its end
                // hands 10 to 11; 11 goes back to 10 past five frames). Until
                // 2026-09-13 only state 5 had it, and state 10 read 0 of 0 as
                // a clip already spent.
                if (rec.state == 5 || rec.state == 10 || rec.state == 11) {
                    if (const auto ga = gunAnims.find(s.actor);
                        ga != gunAnims.end() && ga->second.clip) {
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
                // drawn yet (`gunEntryPending`), and used to let the brain
                // tick anyway: at the gallery's frame 3 each gunman asked
                // for ACTION 0, a 30-frame clip under flag 8, which parks
                // the whole brain - so the tick that would have found him
                // on the grid came at frame 33, and every gunman stood a
                // second before acting instead of one frame. The step is
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
                if (!entryPending && rec.state != before) {
                    rec.flags &= ~4u;
                    const int grpS = static_cast<int>(session.typeOfActor(s.actor));
                    if (rec.state == 10 && grpS >= 0 && grpS < 64) {
                        const omk::PedClip* atk = shootClipBySlot(grpS, rec.attackSlot);
                        static std::map<int, int> atkTold;
                        if (atk) {
                            gunCurSlot[s.actor] = atk->slot;
                            gunAnims[s.actor].clip = nullptr;
                            if (atkTold[s.actor]++ < 3)
                                std::printf("frame %ld: actor %d %s - ATTACK CLIP (sub_4272B0 case 9): "
                                            "slot %d '%s', %d frames\n", n, s.actor, s.model.c_str(),
                                            atk->slot, atk->name.c_str(), atk->frames);
                        } else if (atkTold[s.actor]++ < 3) {
                            std::printf("frame %ld: actor %d %s - anim ATTAQUE PROCHE non existante "
                                        "dans le .ANI (slot %d)\n", n, s.actor, s.model.c_str(),
                                        rec.attackSlot);
                        }
                    } else if (rec.state == 11 && grpS >= 0 && grpS < 64) {
                        const int t = shootClipExact(grpS, 23) ? 23 : 11;
                        gunCurType[s.actor] = t;
                        gunCurSlot.erase(s.actor);
                        gunAnims[s.actor].clip = nullptr;
                    }
                }
                // ---- STATE 2 ARRIVING at the edge's far end ---------
                // `o3de_SetNodePos(node, to.x, to.y - +64, to.z)`, then
                // `+188 = link.destFloor` and `+60 = that y`. This is
                // the one place a gunman changes floor.
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
                                n, s.actor, s.model.c_str(), was,
                                static_cast<signed char>(rec.node & 0xFF),
                                double(s.at[0]), double(rec.groundY), double(s.at[2]));
                }
                // `sub_435650`: state 4 or 5 left behind gives the route up
                if (st.releaseRoute && rec.route >= 0 && shootMap.valid()) {
                    shootMap.routeRelease(static_cast<signed char>(rec.node & 0xFF),
                                          rec.route);
                    rec.route = -1;
                }
                // the ACTION it asked for (always 0 in the generic brain)
                if (st.actionRequest >= 0)
                    applyAction(rec, st.actionRequest, 0, "the brain's request");
                if (rec.state != before || st.outcome == omk::ShootOutcome::Fire) {
                    // (once he stands somewhere: his entry ACTION changes his
                    // state on the tick he is born, before he is first drawn,
                    // and a distance from the world origin says nothing)
                    if (!s.brainTold && gunCellSeeded.count(s.actor)) {
                        s.brainTold = true;
                        std::printf("frame %ld: actor %d %s - brain %d -> %d, "
                                    "outcome %d, %.0f units away\n", n,
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
                // ...AND IT MOVES HIM (2026-09-11, a reader: "continue with
                // robbers walking and the wall detection"). After the frame
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
                // Without it a robber whose walk a turn kept interrupting
                // before the wrap kept each cycle's 0.38-a-frame rise and
                // climbed to the CEILING (a reader's screenshots,
                // 2026-09-11: "going higher each time the loop restart").
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
                if (!(rec.flags & 8u) && !(rec.flags & 2u)) {
                    const int grpA = static_cast<int>(session.typeOfActor(s.actor));
                    // his clip is the TYPE his last action started
                    // (`Shoot_ActorAction`), the scene action's until one has
                    const auto ctA = gunCurType.find(s.actor);
                    const auto csA = gunCurSlot.find(s.actor);
                    const omk::PedClip* ac = (grpA >= 0 && grpA < 64)
                        ? (csA != gunCurSlot.end() ? shootClipBySlot(grpA, csA->second)
                           : ctA != gunCurType.end() ? shootClipExact(grpA, ctA->second)
                                                     : shootClipFor(grpA, act))
                        : nullptr;
                    GunAnim& ga = gunAnims[s.actor];
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
                            static std::set<std::pair<int, int>> d01Told;
                            if (d01Told.insert({s.actor, ac->type}).second)
                                std::printf("frame %ld: actor %d %s - clip type %d starts "
                                            "(sub_421A20): frame 0->1 root %.2f %.2f %.2f, "
                                            "%d root keys\n", n, s.actor, s.model.c_str(),
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
                            if (gunLooped.insert(s.actor).second)
                                std::printf("frame %ld: actor %d %s - current clip "
                                            "(sub_421370): type %d slot %d, %d frames, "
                                            "looped to %.1f\n", n, s.actor,
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
                                                "at facing %.1f from %.0f %.0f\n", n,
                                                s.actor, s.model.c_str(), ac->type,
                                                double(dx), double(dy), double(dz),
                                                double(s.facing), double(x0), double(z0));
                                }
                                if (std::strcmp(how, "free") != 0 && !s.walkWallTold) {
                                    s.walkWallTold = true;
                                    std::printf("frame %ld: actor %d %s - the wall test "
                                                "(sub_421140) stops his walk at %.0f %.0f: "
                                                "%s, facing %.1f, now %.0f %.0f\n", n,
                                                s.actor, s.model.c_str(), double(x0),
                                                double(z0), how, double(s.facing),
                                                double(s.at[0] + s.walkMove[0]),
                                                double(s.at[2] + s.walkMove[2]));
                                }
                            }
                        }
                    }
                }
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
                if (st.outcome == omk::ShootOutcome::Outcome3 && !(rec.flags & 0x8000u) &&
                    !(rec.flags & 4u) && player && s.mo && !entryPending) {
                    std::int32_t rangeM = 2, dmgA = 1;
                    session.actorAttack(s.actor, rec.attackSlot, rangeM, dmgA);
                    float pt[3] = {player->pos()[0], player->pos()[1] - player->cameraLift(),
                                   player->pos()[2]};
                    for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                        if (playerMeshes[i].parent < 0) {
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
                        const omk::HitOut sho = omk::shootApplyStrike(playerShootRec, sin);
                        std::printf("frame %ld: actor %d %s - STRIKE (outcome 3, sub_423B10): "
                                    "attack slot %d, range %d m (%d), %.1f away, damage %d, "
                                    "difficulty %d\n", n, s.actor, s.model.c_str(),
                                    rec.attackSlot, int(rangeM), 39 * int(rangeM), sd,
                                    int(dmgA), sin.difficulty);
                        if (sho.refused)
                            std::printf("  PLAYER strike REFUSED (`sub_423B10` returns -1)\n");
                        else if (sho.healthWas <= 0)
                            std::printf("frame %ld: PLAYER HIT by actor %d's strike - he is down "
                                        "already (health %d): nothing\n", n, s.actor, sho.healthWas);
                        else
                            applyPlayerDamage(n, s.actor, "strike", int(dmgA), int(shieldS), sho);
                    }
                }
                const bool fullArm = st.outcome == omk::ShootOutcome::Fire;
                if (((fullArm && !(rec.flags & 0x8000u)) ||
                     st.outcome == omk::ShootOutcome::FireIfReady) &&
                    !(rec.flags & 2u) && !entryPending) {
                    const int grpT = static_cast<int>(session.typeOfActor(s.actor));
                    const auto w8 = omk::animGroupWord8(pedAni, grpT);
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
                        if (gunTold.insert(s.actor).second) {
                            std::printf("frame %ld: actor %d %s - Shoot_InitWeapon: held slot %d, "
                                        "object %d '%s' kind %d -> type %d, ", n, s.actor,
                                        s.model.c_str(), hs, obj, stem.c_str(), kind, type);
                            if (rec.weapon)
                                std::printf("row rate %.1f speed %.1f damage %d\n",
                                            double(rec.weapon->rate), double(rec.weapon->speed),
                                            rec.weapon->damage);
                            else
                                std::printf("NO ROW - he cannot fire\n");
                        }
                    }
                    if (!fireTest && gunTold.insert(s.actor).second)
                        std::printf("frame %ld: actor %d %s - the fire test is clear "
                                    "(sub_4348B0: ANIMS\\%s.ANI group %d, +8 %s) - he aims "
                                    "and never shoots\n", n, s.actor, s.model.c_str(),
                                    pedAniName.c_str(), grpT,
                                    w8 ? std::to_string(*w8).c_str() : "absent");
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
                        GunAim& gm = gunAims[s.actor];
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
                            for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                                if (playerMeshes[i].parent < 0) {
                                    tr = playerMeshes[i].radius;
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
                                        gunRandSeed = gunRandSeed * 214013u + 2531011u;
                                        const int rr = static_cast<int>((gunRandSeed >> 16) & 0x7FFFu) % span;
                                        d[k] = d[k] + half * 0.5 - rr;
                                    }
                                }
                                const double yawR = double(s.facing) * 0.0174532925199433;
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
                            gunAimTold.insert(s.actor).second)
                            std::printf("frame %ld: actor %d %s - AIM LAYER (sub_434C30): yaw "
                                        "%.3f pitch %.3f rad, lowered %.2f\n", n, s.actor,
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
                        for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                            if (playerMeshes[i].parent < 0) {
                                r = playerMeshes[i].radius;
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
                            gunRandSeed = gunRandSeed * 214013u + 2531011u;
                            jit[k] = static_cast<int>((gunRandSeed >> 16) & 0x7FFFu) % 100;
                        }
                        const omk::GunmanAim aim = omk::shootGunmanAim(pp, rs.muzzle, r, jit);
                        // WHERE HIS GUN POINTS (a reader, 2026-09-11: *"their
                        // weapon is not aiming at me"*): the barrel - his
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
                            gunShots[s.actor] >= 1 && gunBarrelTold.insert(s.actor).second) {
                            const std::size_t hh = static_cast<std::size_t>(hand);
                            const double bx = rs.muzzle[0] - s.meshAt[hh * 3],
                                         bz = rs.muzzle[2] - s.meshAt[hh * 3 + 2];
                            const double tx = pp[0] - rs.muzzle[0], tz = pp[2] - rs.muzzle[2];
                            double dd = (std::atan2(bx, bz) - std::atan2(tx, tz)) * 57.29577951308232;
                            while (dd > 180.0) dd -= 360.0;
                            while (dd < -180.0) dd += 360.0;
                            std::printf("frame %ld: actor %d %s - HIS BARREL points %.1f degrees off "
                                        "the line to the player\n", n, s.actor, s.model.c_str(), dd);
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
                            const long k = ++gunShots[s.actor];
                            shotSound(n, muzzleFx, rs.muzzle, player->pos(), "a gunman's fire");
                            // `sub_4246E0(him, the muzzle)` as the entry is made
                            shootNoise(n, s.actor, rs.muzzle, "a gunman's shot");
                            const omk::Projectile& e =
                                projectiles.entries()[static_cast<std::size_t>(out.entry)];
                            const float spd = e.speed != 0.0f ? e.speed : 1.0f;
                            std::printf("frame %ld: GUNMAN SHOT %ld - actor %d %s, "
                                        "Actor_TickProjectiles: entry %d, speed %.1f, damage %d, "
                                        "dir %.3f %.3f %.3f (yaw %.1f pitch %.1f), from %s "
                                        "%.1f %.1f %.1f, at the player's %s %.0f away, jitter %d %d %d "
                                        "of r %.1f, %d live\n", n, k, s.actor, s.model.c_str(),
                                        out.entry, double(e.speed), e.kind,
                                        double(e.vel[0] / spd), double(e.vel[1] / spd),
                                        double(e.vel[2] / spd), double(rs.yawDeg),
                                        double(rs.pitchDeg), from, double(rs.muzzle[0]),
                                        double(rs.muzzle[1]), double(rs.muzzle[2]),
                                        atRoot ? "root mesh" : "feet", dist,
                                        jit[0], jit[1], jit[2], double(r), projectiles.live());
                        } else {
                            std::printf("frame %ld: GUNMAN SHOT refused - actor %d, the pool "
                                        "is full (%d live)\n", n, s.actor, projectiles.live());
                        }
                    }
                }
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
                if (st.turnClip >= 30 && st.turnClip <= 32) {
                    const int grpT = static_cast<int>(session.typeOfActor(s.actor));
                    const omk::PedClip* tc = (grpT >= 0 && grpT < 64)
                                           ? shootClipExact(grpT, st.turnClip) : nullptr;
                    if (tc && tc->frames > 0) {
                        GunClip& gc = gunClips[s.actor];
                        gc.type = st.turnClip;
                        s.walkMove[1] = 0.0f;   // `sub_421A20` sets the height
                        gc.frames = tc->frames;
                        gc.frame = 1.0f;
                        gc.turn = st.turnTotal / static_cast<float>(tc->frames);
                        rec.flags |= 8u;
                        std::printf("frame %ld: actor %d %s - TURN CLIP (sub_421A20): type %d, "
                                    "%d frames, %.2f a frame, from facing %.1f\n", n, s.actor,
                                    s.model.c_str(), gc.type, gc.frames, double(gc.turn),
                                    double(s.facing));
                    } else {
                        s.facing += st.turnRate * fin.dt;
                        if (s.facing < 0.0f) s.facing += 360.0f;
                        if (s.facing >= 360.0f) s.facing -= 360.0f;
                    }
                }
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
                shootMode && !shotDead && sb != shootBrains.end() && shootMap.valid() &&
                sb->second.state != 2 && !sb->second.cellStamped) {
                omk::ShootRecord& sr = sb->second;
                const int fl = static_cast<signed char>(sr.node & 0xFF);
                if (fl >= 0 && fl < static_cast<int>(shootMap.floors().size())) {
                    sr.cellSaved = shootMap.floors()[static_cast<std::size_t>(fl)]
                                       .cell(sr.destX, sr.destZ);
                    shootMap.setCell(fl, sr.destX, sr.destZ, omk::Map2d::kOccupied);
                    sr.cellStamped = true;
                }
            }
            if (act >= 0) {
                const int grp = static_cast<int>(session.typeOfActor(s.actor));
                // the clip his last ACTION started, as the clock above uses
                const auto ctP = gunCurType.find(s.actor);
                const omk::PedClip* c = (grp >= 0 && grp < 64)
                    ? (ctP != gunCurType.end() ? shootClipExact(grp, ctP->second)
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
                const auto gcIt = gunClips.find(s.actor);
                const bool onTurn = gcIt != gunClips.end() && gcIt->second.type >= 0 &&
                                    s.deathType < 0 && grp >= 0 && grp < 64;
                if (onTurn)
                    if (const omk::PedClip* tc = shootClipExact(grp, gcIt->second.type)) c = tc;
                if (c) shootTracks = pedTracksFor(grp, *c, s.mo->meshes);
                if (onTurn && shootTracks && shootTracks->frames > 0)
                    shootFrame = std::min(static_cast<int>(gcIt->second.frame),
                                          static_cast<int>(shootTracks->frames) - 1);
                // ...and otherwise his CURRENT clip at the frame
                // `sub_421370` has advanced it to (above) - the same clip
                // `shootClipFor` just gave `c`, since both ask it alike
                const auto gaIt = gunAnims.find(s.actor);
                if (!onTurn && s.deathType < 0 && gaIt != gunAnims.end() &&
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
                // phase could not be finished (a reader, 2026-09-10).
                // ONCE, as the engine does: the frame the clip plays out,
                // `sub_421770` clears flag 8 as it returns 0, so the next
                // tick takes the other dead arm instead - the tidy one
                // that puts him in ACTOR_STATE 0 - and posts nothing.
                if (s.deathType >= 0 && !s.deathPosted && shootTracks &&
                    shootTracks->frames > 0 &&
                    static_cast<long>(gameClock - s.deathStart) >=
                        static_cast<long>(shootTracks->frames) - 1) {
                    s.deathPosted = true;
                    const bool ran = session.postMessage(3, s.actor);
                    const auto& mr = session.messagesRun();
                    std::printf("frame %ld: actor %d %s - death clip over: message 3 "
                                "(Game_RaiseEvent 43) %s", n, s.actor, s.model.c_str(),
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
                                "quaternions\n", n, s.actor, s.model.c_str(),
                                bound, shootTracks->ids.size(),
                                s.mo->meshes.size(), shootTracks->frames, nonUnit);
                    if (bound == 0) {
                        const auto dd = omk::animDescriptor(pedAni, c->descriptor);
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
                                "group)\n", n, s.actor, s.model.c_str(), act, grp,
                                c ? "a clip" : "NO CLIP",
                                pedAniName.empty() ? "no .ani loaded"
                                                   : pedAniName.c_str(),
                                shootClips.count(grp) ? shootClips[grp].size() : 0u);
                }
            }
        }
        // The program's placement wins over a `fromTable` reset every
        // frame it is driving, not only on the clip-change frame.
        if (s.progPlaced && sceneClip >= 0) {
            for (int k = 0; k < 3; ++k) s.at[k] = s.progBase[k];
            s.pelvis = s.progPelvis;
        }
        // SCRATCH KEPT ACROSS BODIES AND FRAMES (todo/optimization.md step 18's
        // leftovers): cleared here, so every body starts from an empty pose as it
        // did when these were fresh locals. Main thread only, like this loop.
        static std::vector<omk::MeshPose> stagedPoseScratch;
        static std::vector<float> stagedFvScratch;
        std::vector<omk::MeshPose>& pose = stagedPoseScratch;
        std::vector<float>& fv = stagedFvScratch;
        pose.clear();
        fv.clear();
        float rootW = 0.0f;      // how much of the position the scene owns
        int rootFrame = 0;
        // Whether this frame is holding the last beat's pose. It
        // decides the YAW too: that pose already carries the clip's
        // root rotation, so the branch below must not add the world
        // heading over it - which spun Kay'l 147 degrees for exactly
        // the held frame and back again on the next.
        bool poseHeld = false;
        const char* src = "the rest pose (no bank clip)";
        if (useLine) {
            const int frame = static_cast<int>(lineT);
            // THE LATCH, on the line's first frame - what `Morph_Play`
            // reads off the node once and `sub_42BE00` hands the morph
            // for its whole length. `heading` is the node's world yaw
            // less the Euler: the scene clip's root heading at the
            // frame the line began. The clip's root goes INTO the
            // morph's root here (`turnRootBy`), the Euler is kept
            // beside it, and neither is re-read while the line plays -
            // the program may end under it, as the goodbye's does.
            // ...and it must be THIS line's tracks. The Session enters
            // a line on its tick and the frontend rebuilds
            // `speakerTracks` on the next frame's `lineChanged`, so
            // the first `useLine` frame still holds the PREVIOUS
            // line's. Latched then, a 451-frame line ran on a
            // 135-frame copy and `composePose` clamped at 135: Telis
            // frozen but for her mouth for the last two thirds of
            // "Il y a quatre jours..." - a reader's report. The latch
            // is re-taken when the tracks' line changes under it.
            if (s.lineYawLatched && s.lineVoice != speakerVoice) s.lineYawLatched = false;
            if (!s.lineYawLatched) {
                s.lineVoice = speakerVoice;
                const bool haveClip = s.sceneTracks.valid() && sceneClip >= 0 && run && run->loaded();
                // ...at the frame the FADE blends from. `Morph_Play`
                // hands the morph `rec[47]` (`sub_42BDD0`), and the
                // fade-in slerps from the clip at that frame -
                // `lineIdleFrame` here - toward the morph. The latch
                // must read the clip at the SAME frame, or the two
                // ends of the fade sit at different headings: read at
                // the live `sceneFrame` instead, 402's greeting
                // latched 73 against an idle end at 85 and she stepped
                // 12 degrees on the line's first frame.
                s.lineRootYaw = haveClip
                    ? omk::headingFromClipRoot(run->scene().clipData(sceneClip),
                                               static_cast<int>(lineIdleFrame))
                    : (s.restYawKnown ? s.restYaw - (s.progYawKnown ? progYawSign * s.progYaw : 0.0f) : 0.0f);
                s.lineYaw = s.progYawKnown ? progYawSign * s.progYaw : 0.0f;   // the Euler
                s.lineTracks = speakerTracks;
                turnRootBy(s.lineTracks, s.lineRootYaw);
                s.lineYawLatched = true;
            }
            // THE FADE at both ends of the line - `sub_42D120`'s,
            // pose.h has the read - and the root is NOT cancelled when
            // a scene clip stages him: the engine plays the root as
            // `root x yaw` (key 1 of the pair `sub_42D120` builds), which
            // is `turnRootBy` above.
            float w = 1.0f;
            int idleFrame = static_cast<int>(sceneFrame);
            if (s.sceneTracks.valid()) {
                const float total = static_cast<float>(speakerTracks.frames);
                const float bl = omk::morphBlendFrames(speakerTracks.frames);
                const bool blendOut = speakerVoice.find("02E19A") == std::string::npos;
                if (bl > 0.0f && lineT < bl) {
                    w = lineT / bl;                          // from rec[47]
                    idleFrame = static_cast<int>(lineIdleFrame);
                } else if (bl > 0.0f && blendOut && lineT > total - bl) {
                    w = 1.0f - (lineT - (total - bl)) / bl;  // to KEY 1
                    idleFrame = 0;
                }
            }
            const bool cancelLineRoot = !s.sceneTracks.valid();
            if (w < 1.0f) {
                const auto mixed = omk::blendTracks(s.sceneTracks, idleFrame, false,
                                                    s.lineTracks, frame,
                                                    cancelLineRoot, w);
                omk::composePose(s.mo->meshes, mixed, 0, false, pose);
            } else {
                omk::composePose(s.mo->meshes, s.lineTracks, frame,
                                        cancelLineRoot, pose);
            }
            // THE PLACEMENT IS THE SCENE CLIP'S, WHOLE, WHILE A LINE
            // PLAYS. A line changes the POSE, never where the body
            // stands: the morph player (08_wave.c) never writes the
            // line's root translation into the node - the write behind
            // `track == g_MorphRootTrack` is the root TRACK's position-key
            // pointer, which nothing applying a frame reads - and instead
            // rotates that translation
            // by the Y euler and adds it to `g_MorphOrigin`, latched
            // once at the start of the line (`dword_4EA8FC`). So a
            // line's root is a DELTA from where the body already
            // stands, and the scene clip's own root motion is still
            // under it.
            //
            // This weighted it by the blend (`rootW = 1 - w`), so a
            // speaking body drifted back to its bare staged placement
            // and the fade lerped it there and back: a reader watching
            // Telis in the restaurant saw her FLY between the line and
            // the idle, and said which end was right - the idle's,
            // because her face leaves the (correct) camera while she
            // speaks. The 2026-09-02 reading that put the weight here
            // had the same report ("alternate between flying and
            // landing") and treated the position as something the fade
            // owns; it is not (`todo/omk-play.md` 81).
            rootW = 1.0f;
            rootFrame = static_cast<int>(sceneFrame);
            // The FACE has no bone track: its vertices come straight
            // out of the line's own frame, which is what moves the lips.
            if (speakerMorph && !speakerMorph->empty()) fv = omk::faceFrame(*speakerMorph, frame);
            src = "the line's .3DM";
        } else if (s.sceneTracks.valid()) {
            s.inertAfterFight = false;     // a program owns him again
            rootFrame = static_cast<int>(sceneFrame);
            // A SCENE CLIP keeps its root rotation - it is the
            // character's real orientation, lying on the floor and
            // getting up (`Anim_ApplyNodeFrame` applies every node's
            // quaternion, the root's included).
            // the program's FLOAT clock, so enhancement 12 can blend
            // between keys; off, it floors to `rootFrame` exactly
            omk::composePose(s.mo->meshes, s.sceneTracks, sceneFrame, false, pose);
            rootW = 1.0f;
            src = "a scene program's clip";
        } else if (shootTracks && shootTracks->valid()) {
            // ...bent by his aim layer while the gate runs him
            pose = gunmanPoseNow(s.actor, s.deathType, s.mo, *shootTracks, shootFrame);
            src = "shoot mode: the area's .ani, by character type";
        } else if (!s.lastPose.empty() && session.parkedOnProgram()) {
            // BETWEEN TWO BEATS OF ONE CUTSCENE the body holds the
            // pose its last step left it in. The engine never resets a
            // node - `Script_SelectBodyAnimation` writes the node's
            // animation and nothing clears it when the program ends -
            // and the chain is still in flight, so the next beat is
            // about to pose him again. Falling back to the bank's idle
            // here put Kay'l in his standing stance for the one frame
            // between every pair of the Impasse's beats, which is what
            // a reader saw once the camera stopped cutting away at the
            // same moment (todo/omk-play.md 78).
            //
            // Scoped to the gap deliberately: a body with a bank and
            // NOTHING coming IS driven by its channel
            // (`Cef_TickChannel`), so the idle below stays the right
            // answer everywhere else.
            pose = s.lastPose;
            poseHeld = true;
            src = "the pose the last beat left (the chain is mid-flight)";
        } else if (fightRun.active && fightRun.body == &s &&
                   fightRun.foePose.valid() && fightRun.foeChannel) {
            // A FIGHTER IS POSED BY HIS OWN CHANNEL. Everything else
            // here is driven by a program, a line or the shoot gate;
            // a melee opponent is driven by `Cef_TickChannel`, so his
            // clip and his frame come from it. Without this branch he
            // lands on the idle below and stands in the bank's default
            // entry, frame 0, while he walks in and throws punches -
            // which is what step 3's first half left him doing.
            //
            // Key 0 is the rest sentinel, so frame `f` reads key
            // `f + 1` and `clipTracks` is indexed from 0 by frame.
            int ff = static_cast<int>(fightRun.foeChannel->frame()) - 1;
            if (ff < 0) ff = 0;
            if (ff >= fightRun.foePose.frames) ff = fightRun.foePose.frames - 1;
            omk::composePose(s.mo->meshes, fightRun.foePose, ff, false, pose);
            src = "the fight channel's own clip";
        } else if (s.inertAfterFight && !s.lastPose.empty()) {
            pose = s.lastPose;
            src = "the fight's last pose (ACTOR_STATE 0 after sub_445AC0 - no tick)";
        } else if (s.idle.valid()) {
            if (s.idlePoseFor != s.mo || s.idlePoseModel != s.model) {
                omk::composePose(s.mo->meshes, s.idle, 0, false, s.idlePose);
                s.idlePoseFor = s.mo;
                s.idlePoseModel = s.model;
            }
            pose = s.idlePose;   // a copy: the head look below bends `pose`
            src = "the bank's default entry, frame 0";
        } else if (!s.lastPose.empty()) {
            pose = s.lastPose;
            src = "the last pose it was given (nothing drives it now)";
        } else {
            omk::composePose(s.mo->meshes, omk::NodeTracks{}, 0, false, pose);
        }
        if (useLine || s.sceneTracks.valid() || (shootTracks && shootTracks->valid()) ||
            (fightRun.active && fightRun.body == &s && fightRun.foePose.valid()) ||
            s.idle.valid())
            s.lastPose = pose;
        // THE HEAD LOOK: an actor a script pointed at the player turns
        // his head toward him every frame (`Actors_TickAll` -> `Actor_
        // SetHeadLook`), the target being the player's head. His
        // world position is the frontend's; back into the pose's own
        // space through the placement below (facing, pelvis, at).
        static const bool lookAll = std::getenv("OMK_LOOK_ALL") != nullptr;   // a diagnostic: everyone looks
        if ((lookAll || session.looksAtPlayer(s.actor)) && s.placed && s.mo->root >= 0) {
            const int head = s.mo->headOf();
            if (head >= 0) {
                const float* pp = (adventure && player) ? player->pos() : session.playerPos();
                // THE TARGET'S HEAD NODE, as the engine reads it: `th =
                // *(tgt + 16)`, the player's cached `Tete`, its world
                // position at node +44..+52 (`Actors_TickAll`, read
                // 2026-10-02). `playerHeadAt` is that node as the
                // player was last drawn - one frame old here, since
                // the player is posed later in the frame. Until he has
                // been drawn once, his feet less a standing head height.
                //
                // ...AND WHILE A PROGRAM OWNS HIM, HIS STAGED BODY'S. A
                // `scx.play.player` program poses the player as a staged
                // body and the walker stops drawing him, so `playerHeadAt`
                // froze where his head was before he sat down: Telis, at
                // lunch, aimed at the spot Kay'l stood in when he pressed
                // action, a head higher and 21 units off his seat.
                const Staged* asPlayer = nullptr;
                if (const int pid = session.playerActor(); pid >= 0)
                    for (const auto& up : staged)
                        if (up->actor == pid && up.get() != &s && up->headKnown) { asPlayer = up.get(); break; }
                const float world[3] = {asPlayer ? asPlayer->headAt[0] : playerHeadKnown ? playerHeadAt[0] : pp[0],
                                        asPlayer ? asPlayer->headAt[1] : playerHeadKnown ? playerHeadAt[1] : pp[1] - 60.0f,
                                        asPlayer ? asPlayer->headAt[2] : playerHeadKnown ? playerHeadAt[2] : pp[2]};
                float pelvis[3] = {0, 0, 0};
                if (static_cast<std::size_t>(s.mo->root) < pose.size())
                    for (int k = 0; k < 3; ++k) pelvis[k] = pose[static_cast<std::size_t>(s.mo->root)].pos[k];
                // ...THROUGH WHERE THE PELVIS IS DRAWN, the inverse of the
                // placement below (`R(p - pelvis) + pelvis + off`). This
                // used `s.at`, which is the program's root key 0: under a
                // clip whose root DROPS (Telis sitting, TELRES05 19 units)
                // the seated Kay'l came out 19 units below where he is in
                // her frame, and the look tipped her head down at a man
                // whose head is level with hers.
                const float* base = s.pelvisDrawnKnown ? s.pelvisDrawnAt : s.at;
                const float rel[3] = {world[0] - base[0], world[1] - base[1], world[2] - base[2]};
                float local[3];
                // ...IN THE FRAME THE BODY WILL ACTUALLY BE DRAWN IN.
                // A body a scene program drives is turned by the
                // call's Euler (`progYaw`), not by its placement
                // record's facing, and the aim was always backed out
                // through `s.facing` - 0 for every one of the
                // restaurant's diners while their Euler is 90. So the
                // target sat 90 degrees off, the yaw pinned at
                // `Actor_SetHeadLook`'s own +-70 clamp, and a diner
                // the player is standing almost in front of stared
                // sideways for as long as his zone held (a reader's
                // frames, 2026-09-05: "head at 90 when I think they
                // should be at 0"). Diner 71 wants -13 degrees.
                // ...AND WHILE A LINE PLAYS: the body is drawn turned by
                // the Euler latched with the line (`s.lineYaw`, the body
                // yaw below - the clip's root heading is inside the
                // morph's root, already in `pose`), so the aim is backed
                // out through the same yaw. `s.facing` here was the same
                // 90-degree trap for a program-turned speaker. (Telis at
                // lunch has an Euler of 0, so it changes nothing for her.)
                const bool progTurned = s.sceneTracks.valid() && !useLine &&
                                        s.progYawKnown;
                const float bodyYaw = useLine ? s.lineYaw
                                    : progTurned ? progYawSign * s.progYaw : s.facing;
                omk::rotateYaw(-bodyYaw, rel, local);
                const float target[3] = {local[0] + pelvis[0], local[1] + pelvis[1], local[2] + pelvis[2]};
                // No snap: the angles live in the actor record (+432/+436)
                // and ease from wherever they are; with no target they
                // ease back to 0 (`Actor_SetHeadLook(a, 0, 0)`), which the
                // zeroing below stands in for.
                omk::aimHead(pose, s.mo->meshes, head, target, s.look,
                             static_cast<float>(frameSec * 30.0), false);
            }
        } else {
            s.look = omk::HeadLook{};
        }
        if (!s.placed) {
            if (!s.placeTold) {
                s.placeTold = true;
                std::printf("frame %ld: actor %d %s is shown but nothing says WHERE - "
                            "no placement record, no program path, no camera solve; "
                            "not drawn\n", n, s.actor, s.model.c_str());
            }
            continue;
        }
        if (s.src != src) {
            s.src = src;
            std::printf("frame %ld: actor %d %s - pose source: %s\n",
                        n, s.actor, s.model.c_str(), src);
        }
        // `OMK_TRACE_ACTOR=<id>` - one line a frame for one body:
        // where the port thinks he stands and where it last drew him,
        // with the three flags that decide between them. A pose
        // source changes hands rarely and the position between two
        // such changes is exactly what a per-change report cannot
        // show, which is how "he stays where the program left him"
        // could be printed and then not happen.
        static const char* traceEnv = std::getenv("OMK_TRACE_ACTOR");
        if (traceEnv && s.actor == std::atoi(traceEnv))
            std::printf("  [trace] frame %ld actor %d  at %.0f %.0f %.0f"
                        "  drawAt %.0f %.0f %.0f  placed %d progPlaced %d"
                        "  progRan %d  sceneClip %d  yaw %.0f  src %s\n",
                        n, s.actor, s.at[0], s.at[1], s.at[2],
                        s.drawAt[0], s.drawAt[1], s.drawAt[2],
                        s.placed ? 1 : 0, s.progPlaced ? 1 : 0,
                        s.progRan ? 1 : 0, sceneClip,
                        static_cast<double>(s.drawnYawKnown ? s.drawnYaw : 0.0f), src);
        // A crowd model (the PSH/FSH family the city extras wear) is
        // four LOD skeletons in one file; posing one left the other
        // three at rest - a T-pose inside every couple and beggar.
        // The rest geometry is cut to the skeleton the tracks name.
        const omk::NodeTracks& posingTracks = useLine ? s.lineTracks
                                             : s.sceneTracks.valid() ? s.sceneTracks : s.idle;
        const int skel = skeletonRootOf(*s.mo, posingTracks);
        const omk::Geometry& restUsed = skel == s.mo->root && s.mo->root >= 0 &&
                                         !hasSeveralSkeletons(*s.mo)
                                         ? s.mo->rest : lodRestFor(s.model, *s.mo, skel);
        s.shadowRoot = skel;
        // A BODY BEYOND THE CLIP DISTANCE IS NOT SKINNED (2026-09-22).
        // The engine's visible set is the clip distance (row 3) around
        // the camera and it skins only what it draws; this loop posed
        // every staged body every frame - Anekbah's 26 extras, most of
        // them hundreds of metres from the player - which was 46 ms of
        // a console's frame. His program, his placement, his facing and
        // his nodes (`pose`) are all still computed above; what is
        // skipped is the skinning, the corner transform, the lights and
        // the upload, none of which anything reads for a body that is
        // not drawn - except the FEET latch, `s.seatFeet`, taken once
        // per clip from the posed corners, so a body whose clip changed
        // out there is skinned once for it. His last drawn position
        // (`drawAt`) is the distance measured; a newly staged body has
        // none and is skinned. The root radius pads the reach, as the
        // set's runs are padded by their own bounds.
        {
            const float* at = s.drawAtKnown ? s.drawAt : s.at;
            const float rr = (s.mo->root >= 0 && static_cast<std::size_t>(s.mo->root) < s.mo->meshes.size())
                                 ? s.mo->meshes[static_cast<std::size_t>(s.mo->root)].radius : 0.0f;
            const float ex = at[0] - view.cam.eye[0], ey = at[1] - view.cam.eye[1],
                        ez = at[2] - view.cam.eye[2];
            const double reach = clipInches + rr;
            const bool seatHeld = s.seatKnown && s.seatClip == s.sceneClipWas && s.seatSrc == src;
            // (and under the two logs about every staged body - the
            // staging probe's head twists, hand-over gaps and floor,
            // and `OMK_BODYLOG`'s per-frame body line - which are about
            // every body and not only the ones in view: culled, the
            // restaurant's diners went silent after frame 0 and the
            // Impasse's Kay'l lost frames from his body line, and
            // `engine: scene facing` and `beat handover` read short,
            // 2026-09-30..10-01)
            static const bool skinAll = std::getenv("OMK_SKIN_FAR") != nullptr ||
                                        stagedProbe || omk::envSet("OMK_BODYLOG");
            if (!skinAll && seatHeld && std::isfinite(reach) &&
                double(ex) * ex + double(ey) * ey + double(ez) * ez > reach * reach) {
                ++stagedFar;
                continue;
            }
            // ...and outside the view, on the same terms (the feet
            // latch taken, his last drawn position, the root radius) -
            // but NOT while a shoot phase or a melee is on. There the
            // game reads a body's bones off the staging (a gunman's
            // aim and muzzle, the hit sweeps against his meshes), and
            // a gunman behind the camera still fires: culled, robber
            // 238 never hit the gallery's player and 237 took 238's
            // place in the hub (`engine: shoot restart`, `shoot brain`
            // and three more, red from 30d3d2e). The engine's own cull
            // (`sub_48D3B0`) is the DRAW's; its actors tick whatever
            // the camera sees.
            const bool bodiesInPlay = session.shootMode().active() || fightRun.active;
            if (!skinAll && !bodiesInPlay && seatHeld && outsideView(at, rr, true)) {
                ++stagedOff;
                continue;
            }
        }
        phSpan["staged resolve"] += phaseNow() - stagedResolve0;
        // THE GPU PATH (todo/gpu-skinning.md step 4), a walker's: where
        // the renderer poses bodies, the body is its rest and one affine
        // a mesh. NOT for the frame a clip changes - the feet latch
        // below reads the posed corners once per clip - NOT while a
        // line MORPHS the face (the speaker: its 130 vertices are
        // per-corner, not per mesh), and not under the two logs that
        // print corners, so what they print stays true.
        {
            const bool latchHeld = s.seatKnown && s.seatClip == s.sceneClipWas &&
                                   s.seatSrc == src;
            static const bool cornerLogs = omk::envSet("OMK_BODYLOG") || stagedProbe;
            s.gpu = !cpuBodiesFlag && world.posesBodies() && latchHeld && fv.empty() &&
                    !cornerLogs;
            s.restGeo = &restUsed;
        }
        if (!s.gpu)
            spanned("staged skin", [&] {
                omk::applyPose(s.posed, restUsed, s.mo->meshes, pose, &s.mo->face, &fv);
            });
        if (!s.gpu) s.lastSkinned = n;
        // THE ROOT MOTION: `Anim_RootDelta`'s running sum, weighted by
        // how much of the pose is the scene's, so a line stands where
        // it was staged and a fade lerps the position too.
        float rootMove[3] = {0, 0, 0};
        if (rootW > 0.0f && !s.sceneTracks.trans.empty()) {
            int fi = rootFrame;
            if (fi < 0) fi = 0;
            if (fi >= static_cast<int>(s.sceneTracks.trans.size()))
                fi = static_cast<int>(s.sceneTracks.trans.size()) - 1;
            for (int k = 0; k < 3; ++k)
                rootMove[k] = rootW *
                    s.sceneTracks.trans[static_cast<std::size_t>(fi)]
                                       [static_cast<std::size_t>(k)];
            // ...TURNED BY THE ACTOR'S FACING, which is what
            // `Anim_RootDelta` (0x004711D0) does and this file did
            // not. Its second argument is `node+156`, and
            // `Actor_LoadModel` binds that to `actor+288` -
            // `sub_437140(node, actor + 288)` - which `Actors_TickAll`
            // rebuilds every frame from the Euler at `actor+416..424`:
            //
            //     Matrix3x3_FromEulerAngles(a[104]*rad, a[105]*rad,
            //                               a[106]*rad, a + 288);
            //
            // and the Euler is what `Script_SelectRelativeBodyAnimation`
            // writes with `Actor_SetEuler(node, p4, p5, p6)` on the tick
            // BEFORE it calls `Actor_MoveBy(node, delta)`. The tail of
            // `Anim_RootDelta` is the row-vector multiply
            //
            //     out.x = m0*dx + m3*dy + m6*dz
            //     out.y = m1*dx + m4*dy + m7*dz
            //     out.z = m2*dx + m5*dy + m8*dz
            //
            // so for a yaw-only Euler it is the same `rotateYaw` the
            // body is turned by. Without it the restaurant's waiter -
            // `Serveur00`, Euler y 145 - played a walk cycle facing one
            // way and travelled the other: a reader's report, "animation
            // of walking forward, character moving backward".
            // Note the Euler is STICKY, as it is in the engine: only
            // the relative call writes it, `Script_SelectBodyAnimation`
            // never does, and the actor record keeps the last one - so
            // the waiter's ABS walk clips travel under the 145 his
            // preceding REL wait left there.
            static const bool noRootSpin =
                omk::envSet("OMK_NO_ROOTSPIN");
            // ...and the SENSE is its own question: the body's spin and
            // `Anim_RootDelta`'s multiply are different code paths in the
            // engine and need not share it. `OMK_ROOTSPIN_NEG` turns the
            // delta the other way while leaving the body alone.
            static const float rootSpinSign =
                omk::envSet("OMK_ROOTSPIN_NEG") ? -1.0f : 1.0f;
            if (!noRootSpin && s.progYawKnown && std::fabs(s.progYaw) > 0.01f) {
                float r[3];
                omk::rotateYaw(rootSpinSign * progYawSign * s.progYaw, rootMove, r);
                for (int k = 0; k < 3; ++k) rootMove[k] = r[k];
            }
        }
        // A DEATH CLIP'S ROOT MOTION (a reader, 2026-09-11: the dead
        // *"float in the air"*). `sub_421770` moves the node by the picked
        // clip's root delta every tick - `o3de_MoveNodeBy(node, dx, dy,
        // dz)`, the vertical included - and the robbers' death clips carry
        // the FALL there: braqueur.ani's root keys sum 32.9 to 39.9 DOWN
        // (a standing pelvis is ~42 above the feet) and slide 28 to 110
        // across. The seat latched on the upright first frame and nothing
        // added the drop, so the body lay flat at waist height. Summed from
        // frame 1 as the clip starts there (`sub_421A20`), the horizontal
        // part turned by his heading - the one he is drawn at - and held
        // once it has played out. ONE CLIP FRAME A TICK, as the engine
        // steps it: each tick's delta goes through the WALL TEST
        // (`sub_421140(rec, {pos, dx, dz}, 1)`), and against a wall only
        // the vertical is kept - `o3de_MoveNodeBy(node, 0, dy, 0)`.
        // ...and the WALK his current clip made while alive (`sub_421370`,
        // in the brain above) - where he fell from
        for (int k = 0; k < 3; ++k) rootMove[k] += s.walkMove[k];
        if (s.deathType >= 0 && s.deathClip && !s.deathClip->root.empty() &&
            s.deathClip->frames > 1) {
            const long el = std::max<long>(static_cast<long>(gameClock - s.deathStart), 0);
            const long upto = std::min<long>(el, static_cast<long>(s.deathClip->frames) - 1);
            const auto bi = shootBrains.find(s.actor);
            while (s.deathEl < upto) {
                const float ta = 1.0f + static_cast<float>(s.deathEl);
                float d[3] = {0.0f, 0.0f, 0.0f};
                omk::pedRootDelta(*s.deathClip, ta, ta + 1.0f, nullptr, d);
                float r[3];
                omk::rotateYaw(s.facing, d, r);
                bool wall = false;
                if (bi != shootBrains.end() && shootMap.valid()) {
                    float snap[2];
                    wall = omk::shootWallTest(bi->second, shootMap,
                                              s.at[0] + s.walkMove[0] + s.deathMove[0],
                                              s.at[2] + s.walkMove[2] + s.deathMove[2],
                                              r[0], r[2], 1, snap) != 0;
                }
                if (wall) {
                    ++s.deathWalls;
                } else {
                    s.deathMove[0] += r[0];
                    s.deathMove[2] += r[2];
                }
                s.deathMove[1] += r[1];
                ++s.deathEl;
            }
            for (int k = 0; k < 3; ++k) rootMove[k] += s.deathMove[k];
            if (!s.deathFallTold && el >= static_cast<long>(s.deathClip->frames) - 1) {
                s.deathFallTold = true;
                std::printf("frame %ld: actor %d %s - death clip's root motion (sub_421770): "
                            "%.1f %.1f %.1f over %d frames, down %.1f, %d ticks against a "
                            "wall\n", n, s.actor, s.model.c_str(), double(s.deathMove[0]),
                            double(s.deathMove[1]), double(s.deathMove[2]),
                            s.deathClip->frames, double(s.deathMove[1]), s.deathWalls);
            }
        }
        // THE ANCHOR. An authored PATH names the pelvis - the
        // hierarchy root, whose height is authored - so the model
        // goes there as it is. A placement record and a camera solve
        // name a spot on the GROUND, so the FEET go there, and the
        // game's Y points DOWN so the feet are the largest y.
        float feet = -1e9f;
        if (!s.gpu) for (const auto& c : s.posed.corners) if (c.y > feet) feet = c.y;
        if (!s.seatKnown || s.seatClip != s.sceneClipWas || s.seatSrc != src) {
            s.seatKnown = true;
            s.seatClip = s.sceneClipWas;
            s.seatSrc = src;
            s.seatFeet = feet;
        }
        feet = s.seatFeet;
        float ground = s.at[1];
        if (!s.pelvis && !playerSoup.empty()) {
            // `Walk_ProbeGround`'s own direction: down from just above
            // the authored point (walk_set.cpp's `seatOnFloor`).
            // THROUGH THE GRID, which the header's own contract makes
            // the same answer bit for bit ("the same answers as the
            // linear versions, visiting only the candidates"; the
            // `OMK_VERIFY_SPLIT` probe compares the two every moving
            // frame). A linear probe is the whole city's walkable soup
            // - 15137 triangles in Anekbah - and there are three of
            // them per staged body per frame (todo/vita-port.md).
            if (const auto g = omk::floorUnder(playerSoup, playerGrid, s.at[0],
                                               s.at[1] - 1.0, s.at[2])) {
                const float drop = static_cast<float>(*g) - s.at[1];
                // A body height. Further than that and the authored
                // point is not standing on this floor at all - a
                // balcony or a window the walk mesh does not carry -
                // so the authored y is kept. LABELLED: the cut-off is
                // this file's, not the engine's.
                if (drop < 100.0f) ground = static_cast<float>(*g);
                else if (!s.groundTold) {
                    s.groundTold = true;
                    std::printf("frame %ld: actor %d %s stands at y %.0f with the "
                                "walkable floor %.0f BELOW him - kept where he is "
                                "authored (a ledge the walk mesh does not carry)\n",
                                n, s.actor, s.model.c_str(), s.at[1], drop);
                }
            }
        }
        // ...and the PELVIS is what the placement names, so the
        // model's authored root offset comes out first. The height
        // still follows the anchor: an authored path names the pelvis
        // and its height is kept, a placement record and a camera
        // solve name the ground and the FEET go there.
        float pelvis[3] = {0, 0, 0};
        if (s.mo->root >= 0 && static_cast<std::size_t>(s.mo->root) < pose.size())
            for (int k = 0; k < 3; ++k)
                pelvis[k] = pose[static_cast<std::size_t>(s.mo->root)].pos[k];
        const float off[3] = {s.at[0] - pelvis[0] + rootMove[0],
                              (s.pelvis ? s.at[1] - pelvis[1] : ground - feet)
                                  + rootMove[1],
                              s.at[2] - pelvis[2] + rootMove[2]};
        // where the pelvis is DRAWN - `pelvis + off` - for next frame's
        // head look, which backs its target out into the pose's space
        for (int k = 0; k < 3; ++k) s.pelvisDrawnAt[k] = pelvis[k] + off[k];
        s.pelvisDrawnKnown = true;
        // ...and WHERE THE FALL LEFT HIM: his pelvis over the floor under
        // his placement, once the death clip has played out (a standing
        // pelvis is ~42 above it; the reader's floating corpses were there)
        if (s.deathFallTold && !s.deathFloorTold) {
            s.deathFloorTold = true;
            // a PELVIS-anchored body (a program's path placed him) has
            // `ground` = that authored pelvis, not a floor - so the floor
            // is probed under it here, for the report only
            float floorY = ground;
            if (s.pelvis && !playerSoup.empty())
                if (const auto g = omk::floorUnder(playerSoup, playerGrid, s.at[0],
                                                   s.at[1] - 1.0, s.at[2]))
                    floorY = static_cast<float>(*g);
            std::printf("frame %ld: actor %d %s - dead: his pelvis %.1f above the floor\n",
                        n, s.actor, s.model.c_str(),
                        double(floorY - (pelvis[1] + off[1])));
        }
        // THE FACING, for a body no scene clip is turning:
        // `Actor_SetEuler` is what a placement authors, while a scene
        // clip carries its own root orientation.
        //
        // A SPOKEN LINE DOES NOT CANCEL IT, and that was the fault a
        // reader met as *"she's looking at the wrong direction then
        // goes back to the correct one for the idle"*. Both this and
        // `progSpin` below carried `!useLine`, so while a line played
        // a body was drawn with NO yaw at all and turned to its real
        // one the moment the line ended. The line's own `.3DM` root
        // rotation is RELATIVE to the actor's frame - it is the
        // whole-body bow (CLAUDE.md 4) - so the frame still has to be
        // applied under it, and `tools/omkdata.py` says the same from
        // the other side, written when 387 was staged: "the clip's
        // root yaw is the character's facing for the WHOLE
        // conversation - the game keeps the scene orientation during
        // spoken lines too (compared frame-by-frame against a longplay
        // of 387)".
        const bool spin = !s.sceneTracks.valid() &&
                          std::fabs(s.facing) > 0.01f;
        // a program's body turns by the call's Euler y about its pelvis
        // A DIAGNOSTIC, because a still frame is what decides this:
        // `node+156` (the actor's facing matrix, `Actor_LoadModel` ->
        // `sub_437140(node, actor+288)`) has exactly three readers in
        // the binary and all three are the ROOT-MOTION path
        // (`Anim_RootDelta`, `sub_471460`). Nothing in the draw walk
        // reads it - `Anim_ApplyNodeFrame` orients every node,
        // the pelvis included, from the CLIP's own quaternion. So the
        // call's Euler may well turn the motion and not the body.
        static const bool noProgSpin = std::getenv("OMK_NO_PROGSPIN") != nullptr;
        const bool progSpin = !noProgSpin && s.sceneTracks.valid() &&
                              s.progYawKnown && std::fabs(s.progYaw) > 0.01f;
        // A LINE PLAYS IN THE FRAME THE NODE WAS IN WHEN IT STARTED.
        // `Morph_Play` (0x0041AFC0, CLEAN):
        //
        //     Matrix3x3_RotateVector(0,0,-1, node+92, &dir)   // the WORLD matrix
        //     heading = atan2(dirZ, dirX)*180/pi + 90 - actor[ACTOR_FACING]
        //     sub_42BE00(heading)                             // the morph's yaw
        //
        // and the morph player composes the .3DM root's rotation with
        // yaw(heading) (`08_wave.c` 963-965) under the node's own
        // facing matrix. `ACTOR_FACING` is +420, the Euler y, so
        // `heading` is the node's world yaw LESS the Euler - which is
        // the scene clip's own root orientation, the one
        // `Anim_ApplyNodeFrame` left on the pelvis. A line therefore
        // keeps the clip's root yaw plus the Euler, and its own root
        // goes on top. This viewer drew the .3DM alone and lost the
        // clip's yaw for the length of the line: Telis in profile
        // through "Oh Kay'l, je croyais ne plus jamais te revoir"
        // where the engine's own capture has her facing the lens,
        // then turning to face it when the idle came back.
        // THE BODY'S YAW, one decision. A scene clip's own root
        // quaternion is already in the pose (`Anim_ApplyNodeFrame`
        // orients the pelvis from it), so under a clip only the
        // step's Euler is added; a LINE's .3DM carries no world yaw,
        // so the node's heading at the moment `Morph_Play` ran - the
        // clip's root yaw plus the Euler - is latched and applied for
        // the whole line; and once the program is over the last
        // heading it left is kept.
        const float rootYawNow = (s.sceneTracks.valid() && sceneClip >= 0 && run && run->loaded())
            ? omk::headingFromClipRoot(run->scene().clipData(sceneClip),
                                       static_cast<int>(sceneFrame)) : 0.0f;
        if (s.sceneTracks.valid() && !useLine) {
            s.restYaw = (progSpin ? progYawSign * s.progYaw : 0.0f) + rootYawNow;
            s.restYawKnown = true;
        }
        if (!useLine) s.lineYawLatched = false;
        // THE BODY'S YAW, one decision. Under a scene clip the clip's
        // own root quaternion is in the pose, so only the Euler is
        // added; under a LINE the clip's root heading is in the
        // morph's root (the latch above), so again only the Euler -
        // the latched one; after the program the last heading it
        // left is kept whole; a placement-only body spins by its
        // record about the origin.
        float bodyYaw = 0.0f;
        bool aboutPelvis = true;
        if (useLine)                      bodyYaw = s.lineYaw;
        else if (s.sceneTracks.valid())   bodyYaw = progSpin ? progYawSign * s.progYaw : 0.0f;
        // A GUNMAN WHOSE SHOOT BRAIN RUNS is drawn at ITS heading, the
        // +420 the brain and its turn clips write every tick - not at the
        // rest heading his entrance program left, which is what this took
        // until 2026-09-11 (a reader: robbers *"not always turned to the
        // correct position"* - their brains faced and aimed at him while
        // the body stayed where the program had parked it)
        // A FIGHTER is drawn at the heading the fight writes him -
        // `Fight_FaceOpponent` writes `+420` every frame, and the
        // frontend copies it onto the staged body - for the same
        // reason a gunman with a running brain is, just below: the
        // rest heading his entrance left is stale the moment he turns
        // to face his opponent.
        else if (fightRun.active && fightRun.body == &s) {
            bodyYaw = s.facing;
            aboutPelvis = true;
        }
        else if (shootTracks && shootTracks->valid() && shootBrains.count(s.actor)) {
            bodyYaw = s.facing;
            aboutPelvis = true;
        }
        else if (poseHeld && s.lastYawKnown) {
            bodyYaw = s.lastBodyYaw;      // held whole, with its pose
            aboutPelvis = s.lastAboutPelvis;
        }
        else if (s.restYawKnown)          bodyYaw = s.restYaw;
        // A PLACEMENT-ONLY BODY TURNS ABOUT ITS PELVIS, not its model
        // origin (corrected 2026-09-10). `0139205` wrote "spins by its
        // record about the origin" with no function behind it, and the
        // engine places a body the other way: the NODE - the hierarchy
        // root, the pelvis - is put at the placement and every child
        // hangs off it, so the facing turns the body about that point.
        // For a model authored near the origin (HO1_FN's pelvis at x
        // 2.9) the two differ by a few units; VIR_FN is authored at x
        // 546, and turned about the origin its pelvis was drawn 770
        // units from its placement at a facing of 89 - where its own
        // shoot brain, which uses the placement, did not think it was,
        // and where the projectile sweep, matching the drawing, could
        // not be hit from the placement's side.
        else if (spin)                  { bodyYaw = s.facing; aboutPelvis = true; }
        // ...and the WORLD heading the body ends up drawn with, which
        // is what a camera hanging off him reads back.
        s.drawnYaw = poseHeld && s.lastYawKnown ? s.lastDrawnYaw
                   : useLine ? s.lineYaw + s.lineRootYaw
                   : s.sceneTracks.valid() ? bodyYaw + rootYawNow
                   : aboutPelvis ? bodyYaw : s.facing;
        s.drawnYawKnown = true;
        // ...and remember it, so the next held frame can hold it.
        s.lastBodyYaw = bodyYaw;
        s.lastAboutPelvis = aboutPelvis;
        s.lastDrawnYaw = s.drawnYaw;
        s.lastYawKnown = true;
        if (const int hd = s.mo->headOf();
            hd >= 0 && static_cast<std::size_t>(hd) < pose.size()) {
            const float hp[3] = {pose[static_cast<std::size_t>(hd)].pos[0] - pelvis[0],
                                 pose[static_cast<std::size_t>(hd)].pos[1] - pelvis[1],
                                 pose[static_cast<std::size_t>(hd)].pos[2] - pelvis[2]};
            float r[3];
            const bool spins = std::fabs(bodyYaw) > 0.01f;
            if (spins && !aboutPelvis) {
                // the corners' transform, `R * pos + off` - see the
                // mesh loop below for what the old order did
                const float head[3] = {hp[0] + pelvis[0], hp[1] + pelvis[1],
                                       hp[2] + pelvis[2]};
                omk::rotateYaw(bodyYaw, head, r);
                for (int k = 0; k < 3; ++k) s.headAt[k] = r[k] + off[k];
            } else {
                omk::rotateYaw(spins ? bodyYaw : 0.0f, hp, r);
                for (int k = 0; k < 3; ++k) s.headAt[k] = r[k] + pelvis[k] + off[k];
            }
            s.headKnown = true;
        }
        // ...and every mesh, on the same transform, for the shadow.
        s.meshAt.assign(pose.size() * 3, 0.0f);
        // ONE `cos`/`sin` A BODY for this loop too: it turns every
        // mesh's origin and each of its three axes, so a crowd model's
        // 76 meshes cost FOUR of them apiece. The two angles it ever
        // turns by are the body's and zero, and `cos`/`sin` of zero are
        // exactly 1 and 0, so both pairs are known up front.
        float bcs, bsn;
        omk::yawSinCos(bodyYaw, bcs, bsn);
        const bool spins = std::fabs(bodyYaw) > 0.01f;
        const float scs = spins ? bcs : 1.0f, ssn = spins ? bsn : 0.0f;
        for (std::size_t mi = 0; mi < pose.size(); ++mi) {
            float r[3];
            if (spins && !aboutPelvis) {
                // ...ON THE CORNERS' OWN TRANSFORM: a body turned
                // about its MODEL origin is `R * pos + off` (the loop
                // below). This used to add `off` first and turn the
                // sum about the WORLD origin, which put every mesh of
                // such a body - a gunman facing by his record - its
                // own distance from the world origin away, rotated by
                // his facing: the projectile sweep found actor 240's
                // pelvis at (2875, 4466) with him drawn at (4516,
                // -2797), and his shadow's bones were there too.
                omk::rotateYawCS(bcs, bsn, pose[mi].pos, r);
                for (int k = 0; k < 3; ++k) r[k] += off[k];
            } else {
                const float mp[3] = {pose[mi].pos[0] - pelvis[0],
                                     pose[mi].pos[1] - pelvis[1],
                                     pose[mi].pos[2] - pelvis[2]};
                omk::rotateYawCS(scs, ssn, mp, r);
                for (int k = 0; k < 3; ++k) r[k] += pelvis[k] + off[k];
            }
            for (int k = 0; k < 3; ++k) s.meshAt[mi * 3 + static_cast<std::size_t>(k)] = r[k];
            // its world rotation: the pose's own, then the body's yaw -
            // the same yaw whichever point it turns about
            if (s.meshRot.size() != pose.size() * 9) s.meshRot.assign(pose.size() * 9, 0.0f);
            for (int ax = 0; ax < 3; ++ax) {
                const float e[3] = {ax == 0 ? 1.0f : 0.0f, ax == 1 ? 1.0f : 0.0f,
                                    ax == 2 ? 1.0f : 0.0f};
                float qv[3], wv[3];
                omk::qrot(pose[mi].q, e, qv);
                omk::rotateYawCS(scs, ssn, qv, wv);
                for (int k = 0; k < 3; ++k)
                    s.meshRot[mi * 9 + static_cast<std::size_t>(ax * 3 + k)] = wv[k];
            }
        }
        const bool turn = spins;
        const double stagedPlace0 = phaseNow();
        if (s.gpu) {
            // the placement below as a 3x4, folded into each mesh's
            // affine: turned about the model's origin, or about the
            // pelvis (t = pelvis - R pelvis), then moved by `off`
            float place[12] = {1, 0, 0, off[0],  0, 1, 0, off[1],  0, 0, 1, off[2]};
            if (turn) {
                place[0] = bcs; place[2] = -bsn; place[8] = bsn; place[10] = bcs;
                if (aboutPelvis) {
                    place[3] = pelvis[0] - (bcs * pelvis[0] - bsn * pelvis[2]) + off[0];
                    place[11] = pelvis[2] - (bsn * pelvis[0] + bcs * pelvis[2]) + off[2];
                }
            }
            omk::meshAffines(s.mo->meshes, pose, place, s.affine);
        }
        if (!s.gpu) for (auto& c : s.posed.corners) {
            if (turn && !aboutPelvis) {
                const float in[3] = {c.x, c.y, c.z};
                float r[3];
                omk::rotateYawCS(bcs, bsn, in, r);
                c.x = r[0]; c.y = r[1]; c.z = r[2];
            } else if (turn) {
                const float in[3] = {c.x - pelvis[0], c.y - pelvis[1], c.z - pelvis[2]};
                float r[3];
                omk::rotateYawCS(bcs, bsn, in, r);
                c.x = r[0] + pelvis[0]; c.y = r[1] + pelvis[1]; c.z = r[2] + pelvis[2];
            }
            c.x += off[0]; c.y += off[1]; c.z += off[2];
        }
        phSpan["staged place"] += phaseNow() - stagedPlace0;
        // Where he ENDED UP, which is the placement plus the clip's
        // root motion - not the offset, which carries the model's own
        // authoring origin.
        s.drawAt[0] = s.at[0] + rootMove[0];
        s.drawAt[1] = (s.pelvis ? s.at[1] : ground) + rootMove[1];
        s.drawAt[2] = s.at[2] + rootMove[2];
        s.drawAtKnown = true;
        // HIS BODY IN THE SPATIAL INDEX: `Actor_TickShoot` ends a
        // gunman's tick in `SpatialIndex_Update`, and the player's
        // query (the crowd push above) shoves him out of it. At a FLOOR
        // point, the convention the player's query is made in
        // (`player->pos()`): his FEET, `off[1] + feet` - the lowest
        // point of his standing pose carried where the vertices went,
        // right for a pelvis-anchored robber and a floor-anchored one
        // alike. At the model's origin (`off`) the entry sat ~40 above
        // the floor and the reach box, `max(|dx|,|dy|,|dz|) <= the two
        // root radii` (~28), refused every gunman on the height alone.
        // NOT PORTED, labelled: every other actor, which `Actor_Attach`
        // registers too; the dead stay registered, as nothing read
        // removes them.
        // His spheres are the model's meshes', which a scene actor's
        // file authors far off its origin (VIR_FN's first at x 564.7),
        // so they are RE-HUNG from the model-space point that stands
        // there - his pelvis x/z, his feet y - once, on registration
        // (the pelvis of that frame's pose; LABELLED: it moves a
        // little with the clip and the spheres do not). The reach is
        // his ROOT mesh's radius, the sweep's and the model's `+88`
        // (docs/STREET_LIFE.md 3) - not `meshes.front()`, which for
        // VIR_FN is a 7-unit mesh and shut the box at 14 units.
        if (shootMode && shootBrains.count(s.actor)) {
            const float bodyAt[3] = {s.drawAt[0], off[1] + feet, s.drawAt[2]};
            // (since B5 his list is the model's own, already hung from
            // the feet by `Session::modelSpheres` - node-relative in x/z,
            // so nothing to re-hang: the base is the origin)
            const float bodyBase[3] = {0.0f, 0.0f, 0.0f};
            (void)pelvis;
            const float bodyReach =
                (s.mo->root >= 0 && static_cast<std::size_t>(s.mo->root) < s.mo->meshes.size())
                    ? s.mo->meshes[static_cast<std::size_t>(s.mo->root)].radius : 0.0f;
            session.actorBody(s.actor, s.model, bodyAt, s.facing, bodyBase, bodyReach);
        }
        s.posed.revision = ++worldGeoRev;
        s.drawn = true;
        // ---- A BODY THAT JUMPS, measured on the body and not on the
        // picture. `OMK_BODYLOG` prints, per staged body per frame,
        // where it was DRAWN and how far its posed vertices moved
        // since the last frame. A pop - a beat hand-over that changes
        // the owner, a placement record clobbering a program's
        // position, a pose falling back to an idle - shows as a step
        // in one or both; walking and animating show as a small
        // steady value. A diagnostic: nothing draws differently.
        if (omk::envSet("OMK_BODYLOG")) {
            double moved = 0.0;
            std::size_t nc = s.posed.corners.size();
            if (s.poseWas.size() == nc * 3 && nc) {
                for (std::size_t k = 0; k < nc; ++k) {
                    const double dx = s.posed.corners[k].x - s.poseWas[k * 3];
                    const double dy = s.posed.corners[k].y - s.poseWas[k * 3 + 1];
                    const double dz = s.posed.corners[k].z - s.poseWas[k * 3 + 2];
                    moved += std::sqrt(dx * dx + dy * dy + dz * dz);
                }
                moved /= static_cast<double>(nc);
            }
            s.poseWas.resize(nc * 3);
            for (std::size_t k = 0; k < nc; ++k) {
                s.poseWas[k * 3]     = s.posed.corners[k].x;
                s.poseWas[k * 3 + 1] = s.posed.corners[k].y;
                s.poseWas[k * 3 + 2] = s.posed.corners[k].z;
            }
            std::printf("  [body] frame %ld actor %d  at %.1f %.1f %.1f"
                        "  yaw %.1f  vertsMoved %.2f  src %s\n",
                        n, s.actor, s.drawAt[0], s.drawAt[1], s.drawAt[2],
                        bodyYaw * 57.2957795f, moved, src ? src : "-");
        }
        // ON THE FLOOR? A scene program's body walks its clip's root
        // motion, and the one invariant that motion has to satisfy is
        // that it stays on the set's own walkable mesh - which is what
        // decides the SENSE the delta is turned in, over a corpus
        // rather than over one waiter.
        if (stagedProbe && (n % 20) == 0 && !playerSoup.empty() &&
            s.sceneTracks.valid()) {
            const auto g = omk::floorUnder(playerSoup, playerGrid, s.drawAt[0],
                                           s.drawAt[1] - 120.0, s.drawAt[2]);
            std::printf("    floor %ld actor %d %s at %.0f %.0f %.0f  %s\n", n,
                        s.actor, s.model.c_str(), s.drawAt[0], s.drawAt[1],
                        s.drawAt[2], g ? "on the walk mesh" : "OFF the walk mesh");
        }
        // THE HEAD'S TWIST against the shoulders, in degrees - a
        // number for the reader's "head at 90 degrees". The engine
        // clamps its own head look to +-70 of yaw and +-40 of pitch
        // (`Actor_SetHeadLook`, 0x00468B50, and the player's free look
        // in `Actors_TickAll` uses the same two), so anything past 70
        // cannot have come from that path and is the CLIP's or the
        // composition's.
        if (stagedProbe && (n % 100) == 0 && s.mo->root >= 0) {
            const int hd = s.mo->headOf();
            if (hd >= 0 && static_cast<std::size_t>(hd) < pose.size() &&
                static_cast<std::size_t>(s.mo->root) < pose.size()) {
                const float fwd[3] = {0.0f, 0.0f, -1.0f};
                float hf[3], rf[3];
                omk::qrot(pose[static_cast<std::size_t>(hd)].q, fwd, hf);
                omk::qrot(pose[static_cast<std::size_t>(s.mo->root)].q, fwd, rf);
                const float d = std::atan2(rf[0] * hf[2] - rf[2] * hf[0],
                                           rf[0] * hf[0] + rf[2] * hf[2]) * 57.29578f;
                std::printf("    head %ld actor %d %s twist %.0f deg from the pelvis"
                            "%s\n", n, s.actor, s.model.c_str(), d,
                            std::fabs(d) > 70.0f ? "   <- past the engine's own "
                                                   "+-70 clamp" : "");
            }
        }
        if (stagedProbe && (n % 100) == 0) {
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (const auto& c : s.posed.corners) {
                const float p[3] = {c.x, c.y, c.z};
                for (int k = 0; k < 3; ++k) {
                    if (p[k] < lo[k]) lo[k] = p[k];
                    if (p[k] > hi[k]) hi[k] = p[k];
                }
            }
            std::printf("    probe %ld actor %d %s box %.0f %.0f %.0f .. %.0f %.0f "
                        "%.0f (%zu corners, %zu batches) cam eye %.0f %.0f %.0f at "
                        "%.0f %.0f %.0f fov %.0f\n", n, s.actor, s.model.c_str(),
                        lo[0], lo[1], lo[2], hi[0], hi[1], hi[2],
                        s.posed.corners.size(), s.posed.batches.size(),
                        view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                        view.cam.at[0], view.cam.at[1], view.cam.at[2],
                        view.cam.hfovDeg);
        }
        if (firstBody) {
            firstBody = false;
            for (int k = 0; k < 3; ++k) actorAt[k] = off[k];
            actorKnown = true;
        }
    }
    {
        static long farTold = -1000;
        int stagedDrawn = 0, stagedGpu = 0;
        for (const auto& up : staged) if (up->drawn) { ++stagedDrawn; stagedGpu += up->gpu; }
        if (n - farTold >= 300 && !staged.empty()) {
            farTold = n;
            long fxBuilt = 0, fxGated = 0;
            omk::particleGateCounts(fxBuilt, fxGated);
            std::printf("frame %ld: particles since the last line - %ld made into quads, %ld "
                        "left out by the depth gate\n", n, fxBuilt, fxGated);
            std::printf("frame %ld: staged bodies - %zu staged, %d skinned and drawn, "
                        "%d beyond the clip distance and %d outside the view (not "
                        "skinned), %d posed by the renderer\n",
                        n, staged.size(), stagedDrawn, stagedFar, stagedOff, stagedGpu);
        }
    }
    releaseIdleStaged();
    mark("staged bodies");
}

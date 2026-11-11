// SPDX-License-Identifier: GPL-3.0-or-later
// EVERY STAGED BODY, posed by whatever drives it.
// Parts of `PlayState::phaseWorld`, split out by `todo/play-split.md`;
// the phase calls them in this order.
#include "playframe.h"
#include "platform/events.h"
#include "platform/env.h"
#include "o3de/vecmath.h"
#include "o3de/shimmer.h"
#include <cstring>

// THE MORPH ROOT TURNED BY A WORLD YAW, the engine's own composition.
// `08_wave.c` 963-965: `sub_442B50(v40, 0, X, 0)` builds the yaw and
// `sub_442940(&root, &yaw, out)` - the Hamilton product `root ⊗ yaw`
// (16_o3de.c 1704) - composes it onto the .3DM's root, per frame, BEFORE the
// fade against the node's current pose. Adjudicated against the port's own
// heading extractor on a real root: of the four candidate products only
// `stored ⊗ Ry(+H)` moves the heading by exactly H (338.0 for H = 338).
// From frame `from` on: a line still streaming is turned as its frames arrive.
static void turnRootBy(omk::NodeTracks& t, float yawDeg, int from) {
    if (!t.valid() || t.rootTrack < 0) return;
    const float h = yawDeg * omk::kDegToRadF * 0.5f;
    const omk::Quatf ry{std::cos(h), 0.0f, std::sin(h), 0.0f};
    for (std::size_t f = static_cast<std::size_t>(std::max(0, from)); f < t.quats.size(); ++f) {
        auto& frame = t.quats[f];
        if (static_cast<std::size_t>(t.rootTrack) < frame.size())
            frame[static_cast<std::size_t>(t.rootTrack)] =
                omk::qmul(frame[static_cast<std::size_t>(t.rootTrack)], ry);
    }
}

// Whether program start `t` drives actor `actorId`: an actor start its own
// actor, a player start the actor record it was bound to (the player's, unless
// a `player.become` has since moved the soul on)
static bool drivenBy(const omk::SceneRunner::Started& t, int actorId, int playerId) {
    if (t.clip < 0) return false;
    return omk::SceneRunner::drivenActor(t, playerId) == actorId;
}

// The staged body whose model has a mesh named `name`, by its actor id
int PlayState::nodeOwner(const std::string& name) const {
    for (const auto& up : staged) {
        if (!up || !up->mo) continue;
        for (const auto& m : up->mo->meshes)
            if (name == m.name) return up->actor;
    }
    return omk::SceneRunner::kNodeUnset;
}

// Every staged body, posed by whatever drives it
void PlayState::worldStaged() {
    OMK_ZONE("world: staged");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    // ---- EVERY STAGED BODY, POSED BY WHATEVER DRIVES IT --------
    //
    // Three sources in the engine's own precedence - the program that
    // names HIM, then the conversation line, then the bank's idle -
    // and the whole point of issue 41 is that the first is resolved
    // PER ACTOR. Taking the last running actor program for one body
    // would have that body wear the Demon's clip while `A_2_DemonLook`
    // runs, whoever it is.
    const omk::SceneRunner& sc = session.scene();
    const int convSpeaker = session.dialogOpen()
        ? session.dialogue().conversation().speaker : -1;
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
    // WHO WEARS A NODE NAME - `o3de_FindNodeByName(scene, name)` over the
    // attached bodies (todo/drift-audit.md M2b). The engine's traversal
    // (`o3de_TraverseNodes` with `sub_436C90`, a `strcmp` on every node's
    // mesh name) keeps the LAST match, and `Actor_Attach` links a body as
    // the scene's FIRST child (`o3de_LinkObjectToParent` with no parent
    // prepends), so of several bodies wearing the name the one found is the
    // one attached EARLIEST - the first in `staged`, which is filled in
    // attach order and re-appends a re-shown body.
    int loneProg = -1, loneProgs = 0;
    for (std::size_t k = 0; k < sc.started().size(); ++k) {
        const auto& t = sc.started()[k];
        if (t.clip < 0 || (t.how != "actor" && t.how != "player")) continue;
        if (!sc.programRunning(static_cast<int>(k))) continue;
        bool owned = false;
        for (const auto& up : staged) if (drivenBy(t, up->actor, playerId)) owned = true;
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
    static const bool noSideCull = omk::envFlag("OMK_NO_SIDECULL");
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
            const double tanH = std::tan(view.cam.hfovDeg * omk::kPi / 360.0);
            const double diag = tanH * std::sqrt(1.0 + double(fh) * fh / (double(fw) * fw));
            const float proj = static_cast<float>(1.0 / (2.0 * diag));
            sideFr = omk::frustumFromCamera(view.cam.eye, view.cam.at, proj, proj, 1, 1, 1000.0f);
        }
    }
    for (auto& up : staged) {
        worldStagedBody(up, sc, playerId, loneProgs, loneProg, convSpeaker, stagedFar, stagedOff,
                        firstBody);
    }
    {
        // `worldStagedBody`'s steps before the skin gate, every body's
        static const char* const kSub[kStagedSubs] = {
            "staged 1 program", "staged 2 drive+clip", "staged 3 idle+shoot", "staged 4 pose",
            "staged 5 head look", "staged 6 skeleton+LOD", "staged 7 gate"};
        for (int k = 0; k < kStagedSubs; ++k) { phases.span[kSub[k]] += stagedSub_[k]; stagedSub_[k] = 0.0; }
    }
    {
        static long farTold = -1000;
        int stagedDrawn = 0, stagedGpu = 0;
        for (const auto& up : staged) if (up->drawn) { ++stagedDrawn; stagedGpu += up->gpu; }
        if (frameNo - farTold >= 300 && !staged.empty()) {
            farTold = frameNo;
            long fxBuilt = 0, fxGated = 0;
            omk::particleGateCounts(fxBuilt, fxGated);
            std::printf("frame %ld: particles since the last line - %ld made into quads, %ld "
                        "left out by the depth gate\n", frameNo, fxBuilt, fxGated);
            // the moving collision (`lazyPlace`): meshes the frames recorded,
            // and how many a query needed placed - counted by the placer
            std::printf("frame %ld: moving collision since the last line - %ld mesh placements "
                        "recorded, %ld made\n", frameNo, lazy.recordedTotal, lazy.placedFrame);
            lazy.recordedTotal = 0; lazy.placedFrame = 0;
            std::printf("frame %ld: staged bodies - %zu staged, %d skinned and drawn, "
                        "%d beyond the clip distance and %d outside the view (not "
                        "skinned), %d posed by the renderer\n",
                        frameNo, staged.size(), stagedDrawn, stagedFar, stagedOff, stagedGpu);
        }
    }
    releaseIdleStaged();
    mark("staged bodies");
}


// ONE STAGED BODY, posed by whatever drives it - the program that names him, then the conversation line, then the bank's idle - and placed (the body of `worldStaged`'s loop)
void PlayState::worldStagedBody(std::unique_ptr<Staged>& up, const omk::SceneRunner& sc,
                                int playerId, int& loneProgs, int& loneProg, int convSpeaker,
                                int& stagedFar, int& stagedOff, bool& firstBody) {
    auto& session = *session_;
    omk::Renderer& world = *world_;
    Staged& s = *up;
    // everything before the skinning: the pose SOURCE, the scene
    // runner's queries, the clip, the look-at, the ground probe
    const double stagedResolve0 = phaseNow();
    // THE STEPS BEFORE THE SKIN GATE, each timed for EVERY body (a far one
    // included, which `staged resolve` below does not count) and summed into
    // the frame's spans by `worldStaged` - the iMac G3 spent 100-190 ms a
    // frame here in Anekbah, 25 bodies, and no other machine shows it
    // (`todo/handoff-classic-mac.md` 3i)
    double subT = stagedResolve0;
    const auto subMark = [&](int k) { const double t = phaseNow(); stagedSub_[k] += t - subT; subT = t; };
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
                        "nothing to stage\n", frameNo, s.actor, s.model.c_str(),
                        s.mo ? s.mo->meshes.size() : 0u,
                        s.mo ? s.mo->rest.corners.size() : 0u);
        }
        return;
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
        omk::envFlag("OMK_ACTIVE_POOL_ONLY");   // the old, wrong lookup
    for (const omk::SceneRunner* cand : {&sc, &session.sceneOut()}) {
        if (activeOnly && cand != &sc) break;
        if (!cand->loaded()) continue;
        for (std::size_t k = 0; k < cand->started().size(); ++k) {
            const auto& t = cand->started()[k];
            if (!drivenBy(t, s.actor, playerId)) continue;
            if (!cand->programRunning(static_cast<int>(k))) continue;
            prog = static_cast<int>(k);
            run = cand;
        }
        if (prog >= 0) break;
    }
    // A BODY STEP THAT NAMES ONE OF HIS MESHES (todo/drift-audit.md
    // M2b). Every body step finds its node through the object's table
    // (`ObjectTable_Cached`, else `o3de_FindNodeByName`, then cached),
    // whichever opcode started the object; the actor start's pinned
    // index is the `drivenBy` case above, and this is every other one -
    // all of a plain `scx.play`'s (the snake on Grotte's bridge, the
    // Morgue's `Cadavre`), and the second body of a two-body object
    // (`2_K+G_Dial`'s Gandhar). The slot is filled the first time a
    // body wearing the name is attached and stays filled, as the
    // engine's cache does. LABELLED: the engine looks once, when the
    // step first runs, and a miss fails that step; this retries every
    // frame until the name resolves. The PLAYER is not taken by a name:
    // his channel owns his body, and no shipped case was measured.
    const omk::SceneRunner::Started::Drive* nameDrive = nullptr;
    worldStagedFindProgram(run, prog, sc, activeOnly, playerId, s, nameDrive);
    subMark(0);
    // ...AND THE FALLBACK IS THE PLAYER'S ALONE.
    //
    // Not for whoever the frame's one body is: the reasoning
    // quoted above, that it "can never mis-assign once there is
    // more than one", misses the supermarket death. The loss branch
    // hides twenty-five characters (`todo/shoot-phase-end.md`), so
    // ONE is exactly what is left - actor 65, the cashier the intro
    // cutscene stages - and the frame's one running program, clip
    // 18 `BOIPATH2.3DA`, names somebody else entirely. Handed to
    // him, it teleports him from 13283 -92 996 to 13046 -140 1149
    // and poses him from a stranger's clip, and since V5H_FNM
    // carries no bank he falls to the model's REST pose - a T-posed
    // body standing in a corridor nothing put him in.
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
                        frameNo, s.actor, s.model.c_str(),
                        run == &sc ? "ACTIVE" : "OUTGOING", run->file().c_str());
    }
    int sceneClip = -1, scenePath = -1;
    float sceneFrame = 0.0f;
    const omk::SceneRunner::Started* stt = nullptr;
    omk::SceneRunner::Started driveView;
    const omk::SceneRunner::Started::Drive* drv = nameDrive;
    worldStagedProgramDrive(run, prog, stt, drv, driveView, sceneClip, scenePath, sceneFrame, s);
    worldStagedSceneClipChange(run, sceneClip, s, scenePath, stt, byLone);
    subMark(1);
    // (b) THE CONVERSATION'S LINE, for its speaker only - the body
    // whose actor id is the DIALOG chunk's word 0. The model name
    // is no fallback: it would make EVERY body wearing the
    // speaker's model mouth the line; the engine plays
    // the line on one actor (`Morph_Play`), and with no speaker (-1)
    // on the street's stand-in, which nothing draws
    // (`todo/drift-audit.md` M3).
    const bool isSpeaker = session.dialogOpen() && speaker.ready &&
        s.actor == convSpeaker && convSpeaker >= 0;
    const float lineT = (isSpeaker && speaker.tracks.valid())
        ? static_cast<float>(session.dialogue().elapsed() * 30.0) : -1.0f;
    const bool useLine = lineT >= 0.0f &&
                         lineT < static_cast<float>(speaker.tracks.frames);
    if (isSpeaker) sceneFrameLast = sceneFrame;
    // (c) THE IDLE, built once: the default group's default entry's
    // clip at FRAME 0.
    if (!s.idleBuilt) {
        s.idleBuilt = true;
        if (s.bk && s.bk->ready) {
            s.idle = idleTracksFor(*s.bk, s.mo->meshes);
            // from the tracks BUILT, not the rule that built them
            int bound = 0;
            for (const auto id : s.idle.ids) bound += id >= 0;
            std::printf("frame %ld: actor %d %s - idle tracks bound by key: %d of %zu\n",
                        session.frameNo(), s.actor, s.model.c_str(), bound, s.idle.ids.size());
        }
    }
    // (c0) SHOOT MODE, ahead of the bank's idle: `Shoot_ActorEnter`
    // puts him in ACTOR_STATE 3 and his clips come from the area's
    // `.ani`, not from a `.CTL` he does not have.
    const omk::NodeTracks* shootTracks = nullptr;
    int shootFrame = 0;    // a death clip plays through; the rest hold frame 0
    const omk::AstarothActor* astGrid = nullptr;   // Astaroth on a 9-cell grid
    worldStagedShoot(s, shootTracks, shootFrame, astGrid);
    subMark(2);
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
    worldStagedLinePose(run, useLine, lineT, s, sceneClip, sceneFrame, pose, rootW, rootFrame,
                        fv, src, shootTracks, astGrid, shootFrame, poseHeld, playerId);
    subMark(3);
    if (useLine || s.sceneTracks.valid() || (shootTracks && shootTracks->valid()) ||
        (fightRun.active && fightRun.body == &s && fightRun.foePose.valid()) ||
        s.idle.valid())
        s.lastPose = pose;
    // THE HEAD LOOK: an actor a script pointed at the player turns
    // his head toward him every frame (`Actors_TickAll` -> `Actor_
    // SetHeadLook`), the target being the player's head. His
    // world position is the frontend's; back into the pose's own
    // space through the placement below (facing, pelvis, at).
    static const bool lookAll = omk::envFlag("OMK_LOOK_ALL");   // a diagnostic: everyone looks
    worldStagedHeadLook(lookAll, s, pose, useLine);
    subMark(4);
    if (!s.placed) {
        if (!s.placeTold) {
            s.placeTold = true;
            std::printf("frame %ld: actor %d %s is shown but nothing says WHERE - "
                        "no placement record, no program path, no camera solve; "
                        "not drawn\n", frameNo, s.actor, s.model.c_str());
        }
        return;
    }
    if (s.src != src) {
        s.src = src;
        std::printf("frame %ld: actor %d %s - pose source: %s\n",
                    frameNo, s.actor, s.model.c_str(), src);
    }
    // `OMK_TRACE_ACTOR=<id>` - one line a frame for one body:
    // where the port thinks he stands and where it last drew him,
    // with the three flags that decide between them. A pose
    // source changes hands rarely and the position between two
    // such changes is exactly what a per-change report cannot
    // show, which is how "he stays where the program left him"
    // could be printed and then not happen.
    static const char* traceEnv = omk::envStr("OMK_TRACE_ACTOR");
    if (traceEnv && s.actor == std::atoi(traceEnv))
        std::printf("  [trace] frame %ld actor %d  at %.0f %.0f %.0f"
                    "  drawAt %.0f %.0f %.0f  placed %d progPlaced %d"
                    "  progRan %d  sceneClip %d  yaw %.0f  src %s\n",
                    frameNo, s.actor, s.at[0], s.at[1], s.at[2],
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
    const omk::Geometry& restModel = skel == s.mo->root && s.mo->root >= 0 &&
                                     !hasSeveralSkeletons(*s.mo)
                                     ? s.mo->rest : lodRestFor(s.model, *s.mo, skel);
    // AN OLD PLAYER BODY is drawn from his own copy of the rest: every batch
    // ADDITIVE (mesh 0x1000|0x2000 -> bucket 0x2100, which wins over a
    // multiply sub-mode in `Raster_DrawTriangles`) and SHIMMERING
    // (0x08000000) - `Player_SetActor(-1)`'s `sub_437220(node, 268, 8)`. A
    // character is LIT, so the shimmer is the lit path's, `sub_494180`: his
    // colour is overwritten from the authored white into a pulsing pale
    // green, the set's lights lost (`omk::shimmerLit`; the reader's frames
    // of the original, 2026-11-11: the old bodies glow green-white). That is
    // done per frame on the posed corners (below), so the rest's own corners
    // carry no SET shimmer (`phase` -1) for a backend to add on top.
    if (s.ghost && s.ghostFrom != &restModel) {
        s.ghostRest = restModel;
        for (auto& b : s.ghostRest.batches) b.blend = omk::Blend::Add;
        double mean[3] = {0, 0, 0};
        for (auto& c : s.ghostRest.corners) {
            c.phase = -1.0f;
            mean[0] += c.r; mean[1] += c.g; mean[2] += c.b;
        }
        const double k = s.ghostRest.corners.empty()
            ? 0.0 : 255.0 / static_cast<double>(s.ghostRest.corners.size());
        s.ghostRest.revision = ++worldGeoRev;
        s.ghostFrom = &restModel;
        std::printf("frame %ld: actor %d %s drawn as an OLD PLAYER BODY - %zu batches "
                    "additive, %zu corners shimmering from the authored colour (mean %.0f "
                    "%.0f %.0f) by the lit path's sub_494180 (Player_SetActor's "
                    "0x08000000|0x2000|0x1000)\n", frameNo, s.actor, s.model.c_str(),
                    s.ghostRest.batches.size(), s.ghostRest.corners.size(),
                    mean[0] * k, mean[1] * k, mean[2] * k);
        omk::event("body.ghost", {{"frame", frameNo}, {"actor", s.actor}, {"batches", static_cast<long>(s.ghostRest.batches.size())},
                                  {"corners", static_cast<long>(s.ghostRest.corners.size())}});
    }
    const omk::Geometry& restUsed = s.ghost ? s.ghostRest : restModel;
    s.shadowRoot = skel;
    // A BODY BEYOND THE CLIP DISTANCE IS NOT SKINNED.
    // The engine's visible set is the clip distance (row 3) around
    // the camera and it skins only what it draws; posing every
    // staged body every frame - Anekbah's 26 extras, most of them
    // hundreds of metres from the player - is 46 ms of a console's
    // frame. His program, his placement, his facing and
    // his nodes (`pose`) are all still computed above; what is
    // skipped is the skinning, the corner transform, the lights and
    // the upload, none of which anything reads for a body that is
    // not drawn - except the FEET latch, `s.seatFeet`, taken once
    // per clip from the posed corners, so a body whose clip changed
    // out there is skinned once for it. His last drawn position
    // (`drawAt`) is the distance measured; a newly staged body has
    // none and is skinned. The root radius pads the reach, as the
    // set's runs are padded by their own bounds.
    subMark(5);
    const bool gated = worldStagedSkinGate(s, src, stagedFar, stagedOff);
    subMark(6);
    if (gated) return;
    phases.span["staged resolve"] += phaseNow() - stagedResolve0;
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
        static const bool cornerLogs = omk::envFlag("OMK_BODYLOG") || stagedProbe;
        // ...and not an old player body: his colours are the lit
        // shimmer's, written per corner below (`sub_494180`)
        s.gpu = !cpuBodiesFlag && world.posesBodies() && latchHeld && fv.empty() &&
                !cornerLogs && !s.ghost;
        s.restGeo = &restUsed;
    }
    if (!s.gpu)
        spanned("staged skin", [&] {
            omk::applyPose(s.posed, restUsed, s.mo->meshes, pose, &s.mo->face, &fv);
        });
    if (!s.gpu) s.lastSkinned = frameNo;
    // THE ROOT MOTION: `Anim_RootDelta`'s running sum, weighted by
    // how much of the pose is the scene's, so a line stands where
    // it was staged and a fade lerps the position too.
    float rootMove[3] = {0, 0, 0};
    worldStagedRootMotion(rootW, s, rootFrame, rootMove);
    // A DEATH CLIP'S ROOT MOTION, without which the dead float in
    // the air. `sub_421770` moves the node by the picked
    // clip's root delta every tick - `o3de_MoveNodeBy(node, dx, dy,
    // dz)`, the vertical included - and the robbers' death clips carry
    // the FALL there: braqueur.ani's root keys sum 32.9 to 39.9 DOWN
    // (a standing pelvis is ~42 above the feet) and slide 28 to 110
    // across. The seat latches on the upright first frame, so without
    // the drop the body lies flat at waist height. Summed from
    // frame 1 as the clip starts there (`sub_421A20`), the horizontal
    // part turned by his heading - the one he is drawn at - and held
    // once it has played out. ONE CLIP FRAME A TICK, as the engine
    // steps it: each tick's delta goes through the WALL TEST
    // (`sub_421140(rec, {pos, dx, dz}, 1)`), and against a wall only
    // the vertical is kept - `o3de_MoveNodeBy(node, 0, dy, 0)`.
    // ...and the WALK his current clip made while alive (`sub_421370`,
    // in the brain above) - where he fell from
    for (int k = 0; k < 3; ++k) rootMove[k] += s.walkMove[k];
    worldStagedDeathRoot(s, rootMove);
    // THE ANCHOR. An authored PATH names the pelvis - the
    // hierarchy root, whose height is authored - so the model
    // goes there as it is. So does a PLACEMENT RECORD (CORRECTED
    // 2026-11-08, todo/drift-audit.md M6; this said it names the ground):
    // `Actor_SetPlacement` puts the node exactly at the record, and
    // `Actor_LoadModel` leaves him in ACTOR_STATE 0, `nullsub_6`, so no
    // probe ever moves him. The reader's frames of the original's Z-Tech
    // room say the same: the T-posed `ZTK_FN`'s rune block on the floor and
    // its claws sunk under it, where a feet seat stood it on the claws.
    // `OMK_RECORD_FEET` is the old reading. And a GUNMAN too (read the same
    // day; `OMK_GUNMAN_FEET` the old reading): `Shoot_ActorEnter` puts the
    // node at `+60` - `o3de_SetNodePos(node, x, +60, z)`, his floor's edge
    // less `+64`, the greatest `y + r` of the model's spheres - and
    // `sub_421770` sets and moves it from there by the clip's root keys, all
    // of which `at` and `walkMove` already carry. A feet seat threw that
    // height away for the pose's lowest corner. What still names a spot on
    // the GROUND, the FEET going there (the game's Y points DOWN, so the
    // largest y): a camera solve, and the player's own position.
    static const bool recordFeet = omk::envFlag("OMK_RECORD_FEET");
    static const bool gunmanFeet = omk::envFlag("OMK_GUNMAN_FEET");
    const bool onShootClip = shootTracks && shootTracks->valid();
    s.nodeSeat = s.pelvis || (!recordFeet && s.recordNode && s.actor != playerId &&
                              !(gunmanFeet && onShootClip));
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
    worldStagedGround(s, ground);
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
                          (s.nodeSeat ? s.at[1] - pelvis[1] : ground - feet)
                              + rootMove[1],
                          s.at[2] - pelvis[2] + rootMove[2]};
    // where the pelvis is DRAWN - `pelvis + off` - for next frame's
    // head look, which backs its target out into the pose's space
    for (int k = 0; k < 3; ++k) s.pelvisDrawnAt[k] = pelvis[k] + off[k];
    // `OMK_FEETLOG=<actor>`: an INSTRUMENT - every 30 frames, that body's
    // lowest posed corner as DRAWN against the floor under him, to tell
    // a float from a sink without a screenshot (todo/astaroth.md 4)
    static const int feetLog = omk::envInt("OMK_FEETLOG", -1);
    if (feetLog == s.actor && frameNo % 30 == 0) {
        float low = -1e9f;
        if (!s.gpu) for (const auto& c : s.posed.corners) if (c.y > low) low = c.y;
        float under = s.at[1];
        if (!walkSoup.soup.empty())
            if (const auto g = omk::floorUnder(walkSoup.soup, walkSoup.grid,
                                               pelvis[0] + off[0], pelvis[1] + off[1],
                                               pelvis[2] + off[2]))
                under = static_cast<float>(*g);
        std::printf("FEET %ld actor %d: lowest drawn %.1f, floor under %.1f, gap %.1f "
                    "(pelvis %.1f, %s)\n", frameNo, s.actor, double(low + off[1]),
                    double(under), double(under - (low + off[1])),
                    double(pelvis[1] + off[1]), s.nodeSeat ? "pelvis seat" : "feet seat");
    }
    // `body.seat`: said once per body a placement record put (todo/drift-
    // audit.md M6). `delta` is the drawn pelvis less the record, in Y (down):
    // 0 under the node seat; what a feet seat moves him by otherwise
    if (s.recordNode && !s.pelvis && !s.seatTold) {
        s.seatTold = true;
        const float drawnY = pelvis[1] + off[1];
        const float nodeY = s.at[1] + rootMove[1];
        omk::event("body.seat", {{"frame", frameNo}, {"actor", s.actor},
                                 {"model", s.model},
                                 {"at", static_cast<double>(s.at[1])},
                                 {"ground", static_cast<double>(ground)},
                                 {"drawn", static_cast<double>(drawnY)},
                                 {"delta", static_cast<double>(drawnY - nodeY)},
                                 {"seat", s.nodeSeat ? "node" : "feet"},
                                 {"src", std::string(src)}});
    }
    s.pelvisDrawnKnown = true;
    // ...and WHERE THE FALL LEFT HIM: his pelvis over the floor under
    // his placement, once the death clip has played out (a standing
    // pelvis is ~42 above it, where a floating corpse would be)
    if (s.deathFallTold && !s.deathFloorTold) {
        s.deathFloorTold = true;
        // a PELVIS-anchored body (a program's path placed him) has
        // `ground` = that authored pelvis, not a floor - so the floor
        // is probed under it here, for the report only
        float floorY = ground;
        if (s.pelvis && !walkSoup.soup.empty())
            if (const auto g = omk::floorUnder(walkSoup.soup, walkSoup.grid, s.at[0],
                                               s.at[1] - 1.0, s.at[2]))
                floorY = static_cast<float>(*g);
        std::printf("frame %ld: actor %d %s - dead: his pelvis %.1f above the floor\n",
                    frameNo, s.actor, s.model.c_str(),
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
    // `node+156` (the actor's facing matrix, `Actor_LoadModel` ->
    // `sub_437140(node, actor+288)`) IS READ BY THE DRAW - CORRECTED
    // 2026-11-08 (todo/drift-audit.md M7). This said it had three
    // readers, all on the root-motion path (`Anim_RootDelta`,
    // `sub_471460`), and nothing in the draw walk; that search missed
    // `sub_4942A0`, the per-node WORLD-TRANSFORM callback `sub_440C80`
    // hands `sub_48CFE0`'s walk:
    //
    //     esi = [node+9Ch]                     // the +288 pointer
    //     if (esi && [node+A0h] != -1.0f)      // compose it with
    //         sub_442290(node+38h, esi, tmp)   //   the local rotation
    //     ... the world matrix at node+5Ch from esi (or from tmp)
    //
    // so the call's Euler - all three angles - turns the drawn body, as
    // the reader's frames of the Kiss couples already said of the yaw.
    // `OMK_NO_PROGSPIN` stays as the A/B it was.
    static const bool noProgSpin = omk::envFlag("OMK_NO_PROGSPIN");
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
    // the PITCH and ROLL beside it - only a scene clip's own Euler carries
    // them (todo/drift-audit.md M7); every other arm turns by a yaw alone
    float tiltPitch = 0.0f, tiltRoll = 0.0f;
    bool heldArm = false;
    if (useLine)                      bodyYaw = s.lineYaw;
    else if (s.sceneTracks.valid()) {
        bodyYaw = progSpin ? progYawSign * s.progYaw : 0.0f;
        if (progSpin) {
            // the SIGN AS WRITTEN, read (todo/drift-audit.md M7):
            // `sub_4942A0` makes the root node's world matrix `local . E`
            // and hangs every child off it, so the Euler turns the posed
            // body in game space, after the clip's root, about the root
            // node - the pelvis the path names - which is what `tiltR`
            // does below. `OMK_TILT_NEG` turns both the other way, an A/B
            static const float tiltSign = omk::envFlag("OMK_TILT_NEG") ? -1.0f : 1.0f;
            tiltPitch = tiltSign * s.progPitch;
            tiltRoll = tiltSign * s.progRoll;
        }
    }
    // A GUNMAN WHOSE SHOOT BRAIN RUNS is drawn at ITS heading, the
    // +420 the brain and its turn clips write every tick - not at the
    // rest heading his entrance program left, which would leave the
    // body where the program parked it while the brain faces and aims
    // at the player
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
        heldArm = true;               // ...the tilt with it
    }
    else if (s.restYawKnown)          bodyYaw = s.restYaw;
    // A PLACEMENT-ONLY BODY TURNS ABOUT ITS PELVIS, not its model
    // origin: the engine places a body by the NODE - the hierarchy
    // root, the pelvis - is put at the placement and every child
    // hangs off it, so the facing turns the body about that point.
    // For a model authored near the origin (HO1_FN's pelvis at x
    // 2.9) the two differ by a few units; VIR_FN is authored at x
    // 546, and turned about the origin its pelvis would be drawn 770
    // units from its placement at a facing of 89 - where its own
    // shoot brain, which uses the placement, does not think it is,
    // and where the projectile sweep, matching the drawing, cannot
    // be hit from the placement's side.
    else if (spin)                  { bodyYaw = s.facing; aboutPelvis = true; }
    // ...and the WORLD heading the body ends up drawn with, which
    // is what a camera hanging off him reads back.
    s.drawnYaw = poseHeld && s.lastYawKnown ? s.lastDrawnYaw
               : useLine ? s.lineYaw + s.lineRootYaw
               : s.sceneTracks.valid() ? bodyYaw + rootYawNow
               : aboutPelvis ? bodyYaw : s.facing;
    s.drawnYawKnown = true;
    // ...said as the body FALLS to its last pose: how far that frame turned
    // him against the last one - 0, since the node keeps its transform
    // (`body.held`; it was the clip's root heading again, Kay'l's -134)
    {
        const bool heldNow = src && std::strncmp(src, "the last pose", 13) == 0;
        if (heldNow && !s.heldWas && s.lastYawKnown)
            omk::event("body.held", {{"frame", frameNo}, {"actor", s.actor},
                                     {"turn", static_cast<double>((bodyYaw - s.lastBodyYaw) *
                                                                  omk::kRadToDegF)}});
        s.heldWas = heldNow;
    }
    // ...and remember it, so the next held frame can hold it.
    s.lastBodyYaw = bodyYaw;
    s.lastAboutPelvis = aboutPelvis;
    s.lastDrawnYaw = s.drawnYaw;
    s.lastYawKnown = true;
    // THE TILT: `Matrix3x3_FromEulerAngles(p4, yaw, p6)` as the body's
    // rotation, built once - `rotateEuler` is that matrix applied as the
    // engine's row vector, so its images of the three axes are the COLUMNS
    // of the same rotation for a column vector. With pitch and roll at 0 it
    // is the yaw turn term for term, and the yaw-only path below is kept
    // for that case so an untilted body is drawn exactly as before.
    static const bool noTilt = omk::envFlag("OMK_NO_TILT");   // A/B: the yaw alone
    if (!heldArm) {
        s.tilted = !noTilt && (std::fabs(tiltPitch) > 0.001f || std::fabs(tiltRoll) > 0.001f);
        if (s.tilted && !s.tiltTold) {
            s.tiltTold = true;
            std::printf("frame %ld: actor %d %s drawn TILTED - pitch %.1f roll %.1f under yaw %.1f "
                        "(the scene call's Euler, todo/drift-audit.md M7)\n", frameNo, s.actor,
                        s.model.c_str(), tiltPitch, tiltRoll, bodyYaw);
            omk::event("body.tilt", {{"frame", frameNo}, {"actor", s.actor},
                                     {"pitch", static_cast<double>(tiltPitch)},
                                     {"roll", static_cast<double>(tiltRoll)},
                                     {"yaw", static_cast<double>(bodyYaw)}});
        }
        if (s.tilted) {
            const float eul[3] = {tiltPitch, bodyYaw, tiltRoll};
            for (int c = 0; c < 3; ++c) {
                const float e[3] = {c == 0 ? 1.0f : 0.0f, c == 1 ? 1.0f : 0.0f, c == 2 ? 1.0f : 0.0f};
                float col[3];
                omk::rotateEuler(eul, e, col);
                for (int r = 0; r < 3; ++r) s.tiltR[r * 3 + c] = col[r];
            }
        }
    }
    const auto tiltRot = [&](const float in[3], float out[3]) {
        for (int r = 0; r < 3; ++r)
            out[r] = s.tiltR[r * 3] * in[0] + s.tiltR[r * 3 + 1] * in[1] + s.tiltR[r * 3 + 2] * in[2];
    };
    if (const int hd = s.mo->headOf();
        hd >= 0 && static_cast<std::size_t>(hd) < pose.size()) {
        const float hp[3] = {pose[static_cast<std::size_t>(hd)].pos[0] - pelvis[0],
                             pose[static_cast<std::size_t>(hd)].pos[1] - pelvis[1],
                             pose[static_cast<std::size_t>(hd)].pos[2] - pelvis[2]};
        float r[3];
        const bool spins = std::fabs(bodyYaw) > 0.01f;
        if (s.tilted) {
            // about the pelvis, as every scene-clip body turns
            tiltRot(hp, r);
            for (int k = 0; k < 3; ++k) s.headAt[k] = r[k] + pelvis[k] + off[k];
        } else if (spins && !aboutPelvis) {
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
    worldStagedTurnPose(pose, spins, aboutPelvis, bcs, bsn, off, pelvis, scs, ssn, s);
    // ---- THE SET'S LIGHTS ON A CHARACTER (todo/drift-audit.md L1)
    //
    // `Actor_LoadModel` (and `Object_Load`) call `LightObject`
    // (0x00436F80), which registers the model in the lights' spatial
    // structure and keeps the slot at `node+180`; the scene walk
    // `sub_48D3B0` then hands every top-level object to `sub_440CA0`,
    // whose lit arm is the crowd's own sequence - `sub_494E80` writes
    // the scene's `+416` (0) into every runtime vertex's colour,
    // `sub_48E590` gathers the lights reaching the root's world point
    // and `+88` radius, and `sub_493E40` adds each per vertex. So a
    // CHARACTER is lit exactly as a walker is: from the set's ambient
    // grey, by the set's lights - his model's baked colour is not kept
    // (characters ship a flat white) - in the cave Gandhar is tinted
    // the lava's red, not white. Same declared deviation as the crowd: reach and
    // falloff per BODY, from his root's world point, where the engine
    // tests per mesh. A body the GPU cannot light (more lights reach
    // than its program takes) goes the CPU way, as a walker does.
    //
    // NOT FROM BLACK: `sub_494E80` writes the SCENE's `+416` into every
    // lit vertex, and `Read3DO_Init` sets that from the set's ambient
    // (`.3DO` desc+184 x 255, a grey: 63 in the cave, 51 in Kay'l's
    // flat). The crowd's first port took it for 0. The body's scene is
    // the active slot's - `Actor_Attach` links him to `dword_93076C`.
    const bool actorLit = lightActors && lighting == 0;
    float litAt[3] = {0, 0, 0};
    const int baseGrey = std::clamp(
        activeAmbientGrey, 0, 255);
    s.lightCount = 0;
    s.lightsBlack = false;
    s.lightBase = static_cast<float>(baseGrey) / 255.0f;
    {
        const std::size_t ri = s.mo->root >= 0 &&
            static_cast<std::size_t>(s.mo->root) * 3 + 2 < s.meshAt.size()
            ? static_cast<std::size_t>(s.mo->root) : 0;
        if (ri * 3 + 2 < s.meshAt.size())
            for (int k = 0; k < 3; ++k) litAt[k] = s.meshAt[ri * 3 + static_cast<std::size_t>(k)];
    }
    bodyTint(litAt, s.lightTint);   // the remaster's body light, 1 when off
    if (actorLit) {
        s.lights.clear();
        int reachN = 0;
        for (const WorldSlot& ws2 : worldSlots)
            if (!ws2.lights.empty()) reachN += omk::lightReach(litAt, ws2.lights, s.lights);
        if (s.gpu && reachN > world.maxVertexLights()) {
            s.gpu = false;
            spanned("staged skin", [&] {
                omk::applyPose(s.posed, restUsed, s.mo->meshes, pose, &s.mo->face, &fv);
            });
            s.lastSkinned = frameNo;
        }
        if (s.gpu) { s.lightCount = reachN; s.lightsBlack = true; }
    }
    const bool turn = spins;
    const double stagedPlace0 = phaseNow();
    if (s.gpu) {
        // the placement below as a 3x4, folded into each mesh's
        // affine: turned about the model's origin, or about the
        // pelvis (t = pelvis - R pelvis), then moved by `off`
        float place[12] = {1, 0, 0, off[0],  0, 1, 0, off[1],  0, 0, 1, off[2]};
        if (s.tilted) {
            // the full rotation, about the pelvis
            float rp[3];
            tiltRot(pelvis, rp);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) place[r * 4 + c] = s.tiltR[r * 3 + c];
                place[r * 4 + 3] = pelvis[r] - rp[r] + off[r];
            }
        } else if (turn) {
            place[0] = bcs; place[2] = -bsn; place[8] = bsn; place[10] = bcs;
            if (aboutPelvis) {
                place[3] = pelvis[0] - (bcs * pelvis[0] - bsn * pelvis[2]) + off[0];
                place[11] = pelvis[2] - (bsn * pelvis[0] + bcs * pelvis[2]) + off[2];
            }
        }
        omk::meshAffines(s.mo->meshes, pose, place, s.affine);
    }
    if (!s.gpu) for (auto& c : s.posed.corners) {
        if (s.tilted) {
            const float in[3] = {c.x - pelvis[0], c.y - pelvis[1], c.z - pelvis[2]};
            float r[3];
            tiltRot(in, r);
            c.x = r[0] + pelvis[0]; c.y = r[1] + pelvis[1]; c.z = r[2] + pelvis[2];
        } else if (turn && !aboutPelvis) {
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
    phases.span["staged place"] += phaseNow() - stagedPlace0;
    // ...and on the CPU path the corners themselves: black, then every
    // light that reaches him (`o3de/vertexlight.h`), from both resident
    // slots, as the crowd's are
    if (!actorLit && !s.gpu && bodyTintPerPixel())   // the body light per pixel: grey x tint
        for (auto& c : s.posed.corners) {
            c.r = s.lightBase * s.lightTint[0]; c.g = s.lightBase * s.lightTint[1]; c.b = s.lightBase * s.lightTint[2];
        }
    if (actorLit && !s.gpu) {
        for (auto& c : s.posed.corners) {
            c.r = s.lightBase * s.lightTint[0]; c.g = s.lightBase * s.lightTint[1]; c.b = s.lightBase * s.lightTint[2];
        }
        int reached = 0;
        for (const WorldSlot& ws2 : worldSlots)
            if (!ws2.lights.empty())
                reached += omk::applyLights(s.posed, 0, s.posed.corners.size(), litAt, ws2.lights);
        // said ONCE a body, from the corners the draw will take - the
        // consumer's value, not the decision's
        if (!s.litTold && reached > 0 && !s.posed.corners.empty()) {
            s.litTold = true;
            double m[3] = {0, 0, 0};
            for (const auto& c : s.posed.corners) { m[0] += c.r; m[1] += c.g; m[2] += c.b; }
            const double k = 255.0 / static_cast<double>(s.posed.corners.size());
            std::printf("frame %ld: actor %d %s LIT by the set (sub_440CA0) - base %d, "
                        "%d lights reach, corners' mean %.0f %.0f %.0f\n", frameNo, s.actor,
                        s.model.c_str(), baseGrey, reached, m[0] * k, m[1] * k, m[2] * k);
        }
    }
    // AN OLD PLAYER BODY: `sub_494180` last, over whatever the lights
    // wrote - his colour from the authored one and the wave alone
    if (s.ghost && !s.gpu) {
        omk::shimmerLitBody(s.posed, restUsed, shimmerClock);
        // ...said once, from the corners the draw takes
        if (!s.ghostColourTold && !s.posed.corners.empty()) {
            s.ghostColourTold = true;
            double m[3] = {0, 0, 0};
            for (const auto& c : s.posed.corners) { m[0] += c.r; m[1] += c.g; m[2] += c.b; }
            const double k = 255.0 / static_cast<double>(s.posed.corners.size());
            omk::event("body.ghost.colour", {{"frame", frameNo}, {"actor", s.actor},
                                             {"r", m[0] * k}, {"g", m[1] * k}, {"b", m[2] * k}});
        }
    }
    // Where he ENDED UP, which is the placement plus the clip's
    // root motion - not the offset, which carries the model's own
    // authoring origin.
    s.drawAt[0] = s.at[0] + rootMove[0];
    // (`ground` for a record's node-seated body too: `worldStagedShootEnter`
    // reads this y as a gunman's FLOOR as he enters. Only what is DRAWN moved.)
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
    // alike. At the model's origin (`off`) the entry would sit ~40
    // above the floor and the reach box, `max(|dx|,|dy|,|dz|) <= the
    // two root radii` (~28), refuse every gunman on the height alone.
    // And EVERY OTHER ACTOR - the Mecaguards and the Sham included:
    // `Actor_Attach` (0x0041CCA0)
    // registers every attached actor, `sub_45DFF0(node, x, y, z)`,
    // and `Actor_Detach` removes it (`dropActorBody`, where the
    // viewer parks or drops a body). Not the player's own body - a
    // program-driven player is staged here and his query would
    // shove him out of himself. The dead stay registered, as
    // nothing read removes them. LABELLED: a body is registered the
    // first frame it is posed, where the engine registers it at the
    // attach - a body never yet in view has no entry.
    // His spheres are the model's meshes', which a scene actor's
    // file authors far off its origin (VIR_FN's first at x 564.7),
    // so they are RE-HUNG from the model-space point that stands
    // there - his pelvis x/z, his feet y - once, on registration
    // (the pelvis of that frame's pose; LABELLED: it moves a
    // little with the clip and the spheres do not). The reach is
    // his ROOT mesh's radius, the sweep's and the model's `+88`
    // (docs/STREET_LIFE.md 3) - not `meshes.front()`, which for
    // VIR_FN is a 7-unit mesh and shut the box at 14 units.
    // ...but not an OLD PLAYER BODY: `Player_SetActor` left him with no
    // collider until something attaches him again (`Session::bodyless`)
    if (s.actor != session.playerActor() && !session.bodyless(s.actor)) {
        const float bodyAt[3] = {s.drawAt[0], off[1] + feet, s.drawAt[2]};
        // (since B5 his list is the model's own, already hung from
        // the feet by `Session::modelSpheres` - node-relative in x/z,
        // so nothing to re-hang: the base is the origin)
        // Every OTHER body is drawn turned about its PELVIS
        // (`drawAt + R(p - pelvis)`, above), and a scene actor's
        // pelvis is not at its model origin - the Sham's is 25 units
        // off - so its spheres are hung from the pelvis and turned
        // by the yaw it is DRAWN with. At the node with `facing`
        // they sat behind the drawn Sham and the player walked
        // through its flank.
        const bool gunman = shootMode && shootBrains.count(s.actor);
        const float bodyBase[3] = {gunman ? 0.0f : pelvis[0], 0.0f,
                                   gunman ? 0.0f : pelvis[2]};
        const float bodyYawNow = gunman ? s.facing : s.drawnYaw;
        const float bodyReach =
            (s.mo->root >= 0 && static_cast<std::size_t>(s.mo->root) < s.mo->meshes.size())
                ? s.mo->meshes[static_cast<std::size_t>(s.mo->root)].radius : 0.0f;
        session.actorBody(s.actor, s.model, bodyAt, bodyYawNow, bodyBase, bodyReach);
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
    worldStagedBodyReport(s, bodyYaw, src, pose, firstBody, off);
}

// THE BODY'S INSTRUMENT LINES: a jump measured on the body and not on the
// clip, `OMK_BODYLOG`, `OMK_STAGE_PROBE`'s reports, the first body's summary
// (from `worldStagedBody`)
void PlayState::worldStagedBodyReport(Staged& s, float& bodyYaw, const char*& src,
                                       std::vector<omk::MeshPose>& pose, bool& firstBody,
                                       const float (&off)[3]) {
    if (omk::envFlag("OMK_BODYLOG")) {
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
                    frameNo, s.actor, s.drawAt[0], s.drawAt[1], s.drawAt[2],
                    bodyYaw * omk::kRadToDegF, moved, src ? src : "-");
    }
    // ON THE FLOOR? A scene program's body walks its clip's root
    // motion, and the one invariant that motion has to satisfy is
    // that it stays on the set's own walkable mesh - which is what
    // decides the SENSE the delta is turned in, over a corpus
    // rather than over one waiter.
    if (stagedProbe && (frameNo % 20) == 0 && !walkSoup.soup.empty() &&
        s.sceneTracks.valid()) {
        const auto g = omk::floorUnder(walkSoup.soup, walkSoup.grid, s.drawAt[0],
                                       s.drawAt[1] - 120.0, s.drawAt[2]);
        std::printf("    floor %ld actor %d %s at %.0f %.0f %.0f  %s\n", frameNo,
                    s.actor, s.model.c_str(), s.drawAt[0], s.drawAt[1],
                    s.drawAt[2], g ? "on the walk mesh" : "OFF the walk mesh");
    }
    // THE HEAD'S TWIST against the shoulders, in degrees - a
    // number for a "head at 90 degrees" seen in play. The engine
    // clamps its own head look to +-70 of yaw and +-40 of pitch
    // (`Actor_SetHeadLook`, 0x00468B50, and the player's free look
    // in `Actors_TickAll` uses the same two), so anything past 70
    // cannot have come from that path and is the CLIP's or the
    // composition's.
    if (stagedProbe && (frameNo % 100) == 0 && s.mo->root >= 0) {
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
                        "%s\n", frameNo, s.actor, s.model.c_str(), d,
                        std::fabs(d) > 70.0f ? "   <- past the engine's own "
                                               "+-70 clamp" : "");
        }
    }
    if (stagedProbe && (frameNo % 100) == 0) {
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
                    "%.0f %.0f %.0f fov %.0f\n", frameNo, s.actor, s.model.c_str(),
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


// NO PROGRAM BY INDEX: one that names this body is looked up by NAME, every frame until it resolves (from `worldStagedBody`)
void PlayState::worldStagedFindProgram(const omk::SceneRunner*& run, int& prog,
                                       const omk::SceneRunner& sc, bool activeOnly, int& playerId,
                                       Staged& s,
                                       const omk::SceneRunner::Started::Drive*& nameDrive) {
    auto& session = *session_;
    if (prog < 0) {
        for (const omk::SceneRunner* cand : {&sc, &session.sceneOut()}) {
            if (activeOnly && cand != &sc) break;
            if (!cand->loaded()) continue;
            for (std::size_t k = 0; k < cand->started().size(); ++k) {
                const auto& t = cand->started()[k];
                if (!cand->programRunning(static_cast<int>(k))) continue;
                for (const auto& d : t.drives) {
                    if (!d.reached || (d.pinned && t.how != "scene")) continue;
                    int who = cand->cachedNode(t.objectIndex, d.table);
                    if (who == omk::SceneRunner::kNodeUnset) {
                        who = nodeOwner(d.node);
                        if (who == omk::SceneRunner::kNodeUnset) continue;
                        cand->cacheNode(t.objectIndex, d.table, who);
                        std::printf("frame %ld: node '%s' of object %d '%s' (%s start) "
                                    "resolves BY NAME to actor %d\n", frameNo, d.node.c_str(),
                                    t.object, t.name.c_str(), t.how.c_str(), who);
                    }
                    if (who == omk::SceneRunner::kNodePlayer) who = playerId;
                    if (who != s.actor) continue;
                    if (who == playerId) {
                        static bool said = false;
                        if (!said) std::printf("frame %ld: node '%s' of object '%s' names the "
                                               "PLAYER - not taken (his channel owns him)\n",
                                               frameNo, d.node.c_str(), t.name.c_str());
                        said = true;
                        continue;
                    }
                    prog = static_cast<int>(k);
                    run = cand;
                    nameDrive = &d;
                }
            }
            if (prog >= 0) break;
        }
    }
}


// A BODY BEYOND THE CLIP DISTANCE, or outside the view's side planes, IS NOT SKINNED (from `worldStagedBody`)
bool PlayState::worldStagedSkinGate(Staged& s, const char*& src, int& stagedFar, int& stagedOff) {
    auto& session = *session_;
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
        // restaurant's diners go silent after frame 0 and the
        // Impasse's Kay'l loses frames from his body line, and
        // `engine: scene facing` and `beat handover` read short)
        static const bool skinAll = omk::envFlag("OMK_SKIN_FAR") ||
                                    stagedProbe || omk::envFlag("OMK_BODYLOG");
        if (!skinAll && seatHeld && std::isfinite(reach) &&
            double(ex) * ex + double(ey) * ey + double(ez) * ez > reach * reach) {
            ++stagedFar;
            return true;
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
            return true;
        }
    }
    return false;
}


// THE FLOOR UNDER A BODY with no pelvis anchor: `Walk_ProbeGround`'s probe down
// from just above the authored point; a drop past a body height keeps the
// authored y (from `worldStagedBody`)
void PlayState::worldStagedGround(Staged& s, float& ground) {
    if (!s.pelvis && !walkSoup.soup.empty()) {
        // `Walk_ProbeGround`'s own direction: down from just above
        // the authored point (walk_set.cpp's `seatOnFloor`).
        // THROUGH THE GRID, which the header's own contract makes
        // the same answer bit for bit ("the same answers as the
        // linear versions, visiting only the candidates"; the
        // `OMK_VERIFY_SPLIT` probe compares the two every moving
        // frame). A linear probe is the whole city's walkable soup
        // - 15137 triangles in Anekbah - and there are three of
        // them per staged body per frame (todo/vita-port.md).
        // From ONE UNIT ABOVE, which is `- 2` (`floorUnder` wants a surface
        // more than a unit below its origin): `- 1` asked for one strictly
        // below the point, so a body put on a floor - `at` copied from the
        // player's feet - missed it or kept it on the rounding of its height
        // and was dropped onto whatever lay under it, up to a body height.
        if (const auto g = omk::floorUnder(walkSoup.soup, walkSoup.grid, s.at[0],
                                           s.at[1] - 2.0, s.at[2])) {
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
                            frameNo, s.actor, s.model.c_str(), s.at[1], drop);
            }
        }
    }
}


// THE PROGRAM drives this body: its clip, its frame and its placement (from `worldStaged`)
void PlayState::worldStagedProgramDrive(const omk::SceneRunner*& run, int& prog,
                                        const omk::SceneRunner::Started*& stt,
                                        const omk::SceneRunner::Started::Drive*& drv,
                                        omk::SceneRunner::Started& driveView, int& sceneClip,
                                        int& scenePath, float& sceneFrame, Staged& s) {
    if (prog >= 0) {
        stt = &run->started()[static_cast<std::size_t>(prog)];
        // The node this body IS: a name's drive, or the pinned one of
        // an object that animates more than one - whose own step, and
        // own clock, is his (the fields of `Started` follow it).
        if (!drv && stt->drives.size() > 1)
            for (const auto& d : stt->drives) if (d.pinned) drv = &d;
        if (drv) {
            driveView = *stt;
            driveView.clip = drv->clip;
            driveView.path = drv->path;
            driveView.relative = drv->relative;
            driveView.animReached = drv->reached;
            for (int k = 0; k < 3; ++k) {
                driveView.offset[k] = drv->offset[k];
                driveView.euler[k] = drv->euler[k];
            }
            stt = &driveView;
        }
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
        sceneFrame = drv ? drv->clock : run->programAnimClock(prog);
        // THE FACING, from the step the pc is on - and from BOTH
        // kinds of body animation, because both end in
        // `Actor_SetEuler(node, p4, p5, p6)`. It is not sticky:
        // the waiter's walk clips author 0 and turn him back to 0,
        // and reading only the relative call's Euler rotated his
        // whole route by the 145 his standing step had left.
        static const bool stickyEuler =
            omk::envFlag("OMK_STICKY_EULER");   // the old, wrong reading
        if (stt->animReached && (!stickyEuler || stt->relative)) {
            s.progYaw = stt->euler[1];
            s.progPitch = stt->euler[0];        // p4 -> +416
            s.progRoll = stt->euler[2];         // p6 -> +424
            s.progYawKnown = true;
        }
    }
}


// A DEAD GUNMAN: his death clip's root, the vertical kept, from where his last walk put him (from `worldStaged`)
void PlayState::worldStagedDeathRoot(Staged& s, float (&rootMove)[3]) {
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
                        "wall\n", frameNo, s.actor, s.model.c_str(), double(s.deathMove[0]),
                        double(s.deathMove[1]), double(s.deathMove[2]),
                        s.deathClip->frames, double(s.deathMove[1]), s.deathWalls);
        }
    }
}


// THE BODY TURNED: each mesh's pose by the body's yaw, the cos/sin pairs known up front (from `worldStaged`)
void PlayState::worldStagedTurnPose(std::vector<omk::MeshPose>& pose, bool spins,
                                    bool& aboutPelvis, float& bcs, float& bsn,
                                    const float (&off)[3], float (&pelvis)[3], float scs,
                                    float ssn, Staged& s) {
    const auto tiltRot = [&](const float in[3], float out[3]) {
        for (int r = 0; r < 3; ++r)
            out[r] = s.tiltR[r * 3] * in[0] + s.tiltR[r * 3 + 1] * in[1] + s.tiltR[r * 3 + 2] * in[2];
    };
    for (std::size_t mi = 0; mi < pose.size(); ++mi) {
        float r[3];
        if (s.tilted) {
            // the full rotation (todo/drift-audit.md M7), about the pelvis
            const float mp[3] = {pose[mi].pos[0] - pelvis[0],
                                 pose[mi].pos[1] - pelvis[1],
                                 pose[mi].pos[2] - pelvis[2]};
            tiltRot(mp, r);
            for (int k = 0; k < 3; ++k) r[k] += pelvis[k] + off[k];
        } else if (spins && !aboutPelvis) {
            // ...ON THE CORNERS' OWN TRANSFORM: a body turned
            // about its MODEL origin is `R * pos + off` (the loop
            // below). Adding `off` first and turning the sum about
            // the WORLD origin would put every mesh of such a body -
            // a gunman facing by his record - its own distance from
            // the world origin away, rotated by his facing: the
            // projectile sweep finding actor 240's pelvis at (2875,
            // 4466) with him drawn at (4516, -2797), his shadow's
            // bones there too.
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
            if (s.tilted) tiltRot(qv, wv);
            else omk::rotateYawCS(scs, ssn, qv, wv);
            for (int k = 0; k < 3; ++k)
                s.meshRot[mi * 9 + static_cast<std::size_t>(ax * 3 + k)] = wv[k];
        }
    }
}


// A SCENE CLIP CHANGED for this body: its placement from the clip's root or the call's path (from `worldStaged`)
void PlayState::worldStagedSceneClipChange(const omk::SceneRunner*& run, int& sceneClip, Staged& s,
                                           int& scenePath, const omk::SceneRunner::Started*& stt,
                                           bool& byLone) {
    auto& session = *session_;
    if (sceneClip != s.sceneClipWas) {
        // THE HAND-OVER GAP. A program's steps are authored to
        // chain: each clip's placement is where the previous one
        // left the body, so a correct run re-places him on the
        // spot he already occupies and nothing visibly moves.
        // Where the accumulated root motion is wrong the two
        // disagree and the next step YANKS him - a teleport. The gap
        // is therefore
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
                // away and intersecting.
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
            // same rule for the player). Left on its 20-byte
            // placement record instead, the body would be framed by
            // none of the Impasse's beat cameras, which frame the
            // clip roots: Kay'l's arrival clip starts at 6462 -121
            // 3215 while his record puts him at 7217.
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
            if (omk::SceneRunner::drivesPlayer(*stt, session_->playerActor()) && s.actor == session_->playerActor()) {
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
            // (CLAUDE.md 6): he stays where the clip left him, and in
            // the pose it left him in - the node keeps its last frame
            // (the player alone falls back to his channel's idle;
            // todo/drift-audit.md M2).
            s.progPlaced = false;
            if (s.placed) {  // drawAt is last frame's; `drawn` was just cleared
                for (int k = 0; k < 3; ++k) s.at[k] = s.drawAt[k];
                for (float& w : s.walkMove) w = 0.0f;   // drawAt carries it
            }
            std::printf("  pose: actor %d %s - its program ended; he stays where "
                        "it left him (%.0f %.0f %.0f), in the pose it left\n",
                        s.actor, s.model.c_str(),
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
                        "   gap %.0f\n", frameNo, s.actor, s.model.c_str(),
                        wasAt[0], wasAt[2], s.at[0], s.at[2],
                        std::sqrt(dx * dx + dz * dz));
        }
    }
}


// THE CONVERSATION LINE drives the pose: the line's tracks, faded into and out of the idle (from `worldStaged`)
void PlayState::worldStagedLinePose(const omk::SceneRunner*& run, bool useLine, float lineT,
                                    Staged& s, int& sceneClip, float& sceneFrame,
                                    std::vector<omk::MeshPose>& pose, float& rootW, int& rootFrame,
                                    std::vector<float>& fv, const char*& src,
                                    const omk::NodeTracks*& shootTracks,
                                    const omk::AstarothActor*& astGrid, int& shootFrame,
                                    bool& poseHeld, int playerId) {
    auto& session = *session_;
    if (useLine) {
        // (never past what the stream has read - it keeps two seconds
        // ahead of the clock, so this holds only through a stall)
        const int frame = std::min(static_cast<int>(lineT),
                                   std::max(0, (speaker.tracks.built > 0 ? speaker.tracks.built
                                                                        : speaker.tracks.frames) - 1));
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
        // `speaker.tracks` on the next frame's `lineChanged`, so
        // the first `useLine` frame still holds the PREVIOUS
        // line's. Latched then, a 451-frame line runs on a
        // 135-frame copy and `composePose` clamps at 135: Telis
        // frozen but for her mouth for the last two thirds of
        // "Il y a quatre jours...". The latch
        // is re-taken when the tracks' line changes under it.
        if (s.lineYawLatched && s.lineVoice != speaker.voice) s.lineYawLatched = false;
        if (!s.lineYawLatched) {
            s.lineVoice = speaker.voice;
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
            s.lineTracks = speaker.tracks;
            turnRootBy(s.lineTracks, s.lineRootYaw, 0);
            s.lineYawLatched = true;
        }
        // ...and the frames the STREAM has read since (`formats/
        // morphstream.h`): copied in and turned the same way, so the
        // latched copy is the whole line once the line is read
        if (s.lineTracks.built < speaker.tracks.built) {
            const int from = s.lineTracks.built;
            for (int f = from; f < speaker.tracks.built; ++f) {
                s.lineTracks.quats[static_cast<std::size_t>(f)] = speaker.tracks.quats[static_cast<std::size_t>(f)];
                s.lineTracks.trans[static_cast<std::size_t>(f)] = speaker.tracks.trans[static_cast<std::size_t>(f)];
            }
            s.lineTracks.built = speaker.tracks.built;
            turnRootBy(s.lineTracks, s.lineRootYaw, from);
        }
        // THE FADE at both ends of the line - `sub_42D120`'s,
        // pose.h has the read - and the root is NOT cancelled when
        // a scene clip stages him: the engine plays the root as
        // `root x yaw` (key 1 of the pair `sub_42D120` builds), which
        // is `turnRootBy` above.
        float w = 1.0f;
        int idleFrame = static_cast<int>(sceneFrame);
        if (s.sceneTracks.valid()) {
            const float total = static_cast<float>(speaker.tracks.frames);
            const float bl = omk::morphBlendFrames(speaker.tracks.frames);
            const bool blendOut = speaker.voice.find("02E19A") == std::string::npos;
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
        // NOT weighted by the blend (`rootW = 1 - w`): weighted, a
        // speaking body drifts back to its bare staged placement and
        // the fade lerps it there and back - Telis in the restaurant
        // FLYING between the line and the idle, the idle's end being
        // the right one, because her face leaves the (correct)
        // camera while she speaks. The position is not something the
        // fade owns (`todo/omk-play.md` 81).
        rootW = 1.0f;
        rootFrame = static_cast<int>(sceneFrame);
        // The FACE has no bone track: its vertices come straight
        // out of the line's own frame, which is what moves the lips.
        if (speaker.morph && !speaker.morph->empty()) fv = omk::faceFrame(*speaker.morph, frame);
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
        pose = astGrid ? astarothPoseNow(s.mo, *shootTracks, *astGrid)
                       : gunmanPoseNow(s.actor, s.deathType, s.mo, *shootTracks, shootFrame);
        src = "shoot mode: the area's .ani, by character type";
    } else if (!s.lastPose.empty() && session.parkedOnProgram()) {
        // BETWEEN TWO BEATS OF ONE CUTSCENE the body holds the
        // pose its last step left it in. The engine never resets a
        // node - `Script_SelectBodyAnimation` writes the node's
        // animation and nothing clears it when the program ends -
        // and the chain is still in flight, so the next beat is
        // about to pose him again. Falling back to the bank's idle
        // here would put Kay'l in his standing stance for the one
        // frame between every pair of the Impasse's beats, visible
        // whenever the camera does not cut away at the same moment
        // (todo/omk-play.md 78).
        //
        // Scoped to the gap deliberately: with NOTHING coming the
        // PLAYER is driven by his channel (`Cef_TickChannel`) and
        // takes his idle below; an NPC holds his last pose there
        // anyway (todo/drift-audit.md M2).
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
    } else if (s.idle.valid() && s.actor == playerId) {
        // THE PLAYER'S IDLE ALONE (todo/drift-audit.md M2). His
        // channel runs (`SetPersoBankList` - the player's and a melee
        // opponent's are the only ones the engine ever gives a bank),
        // so his default entry stands. An NPC no program drives gets
        // NO clip in the original - `Actor_LoadModel` leaves ACTOR_STATE
        // 0, which does not tick, and neither the spawn nor the load
        // applies one - so he holds the last pose he was given, or the
        // model's REST pose when he was never given one: the branches
        // below - not on frame 0 of a bank's default entry.
        if (s.idlePoseFor != s.mo || s.idlePoseModel != s.model) {
            omk::composePose(s.mo->meshes, s.idle, 0, false, s.idlePose);
            s.idlePoseFor = s.mo;
            s.idlePoseModel = s.model;
        }
        pose = s.idlePose;   // a copy: the head look below bends `pose`
        src = "the bank's default entry, frame 0";
    } else if (!s.lastPose.empty()) {
        // HELD WHOLE - the pose AND the yaw it was drawn with, as the
        // mid-flight branch above: the node keeps its last world
        // transform. Not held, the yaw fell to `restYaw`, the clip's root
        // heading, over a pose that already carries that root rotation -
        // Kay'l's body left at the vats turned a further -134 degrees the
        // frame `player.become` stopped his J_SUICSD (2026-11-11, against
        // the reader's frame of the original), and every body whose
        // program ends with the same double turn.
        pose = s.lastPose;
        poseHeld = true;
        src = "the last pose it was given (nothing drives it now)";
    } else {
        omk::composePose(s.mo->meshes, omk::NodeTracks{}, 0, false, pose);
    }
}

// THE HEAD LOOK: an actor a script pointed at the player turns his head toward the player's head (from `worldStaged`)
void PlayState::worldStagedHeadLook(bool lookAll, Staged& s, std::vector<omk::MeshPose>& pose,
                                    bool useLine) {
    auto& session = *session_;
    if ((lookAll || session.looksAtPlayer(s.actor)) && s.placed && s.mo->root >= 0) {
        const int head = s.mo->headOf();
        if (head >= 0) {
            const float* pp = (adventure && player) ? player->pos() : session.playerPos();
            // THE TARGET'S HEAD NODE, as the engine reads it: `th =
            // *(tgt + 16)`, the player's cached `Tete`, its world
            // position at node +44..+52 (`Actors_TickAll`).
            // `playerHeadAt` is that node as the
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
                for (const auto& other : staged)
                    if (other->actor == pid && other.get() != &s && other->headKnown) { asPlayer = other.get(); break; }
            const float playerHead[3] = {asPlayer ? asPlayer->headAt[0] : playerHeadKnown ? playerHeadAt[0] : pp[0],
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
            const float rel[3] = {playerHead[0] - base[0], playerHead[1] - base[1], playerHead[2] - base[2]};
            float local[3];
            // ...IN THE FRAME THE BODY WILL ACTUALLY BE DRAWN IN.
            // A body a scene program drives is turned by the
            // call's Euler (`progYaw`), not by its placement
            // record's facing. Backed out through `s.facing` - 0
            // for every one of the restaurant's diners while their
            // Euler is 90 - the target would sit 90 degrees off,
            // the yaw pinned at `Actor_SetHeadLook`'s own +-70
            // clamp, and a diner the player is standing almost in
            // front of would stare sideways for as long as his
            // zone held. Diner 71 wants -13 degrees.
            // ...AND WHILE A LINE PLAYS: the body is drawn turned by
            // the Euler latched with the line (`s.lineYaw`, the body
            // yaw below - the clip's root heading is inside the
            // morph's root, already in `pose`), so the aim is backed
            // out through the same yaw. `s.facing` here would be the
            // same 90-degree trap for a program-turned speaker. (Telis at
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
            // ...HELD while he flies Manuelle: the look is `Actors_
            // TickAll`'s, which `Slider_TickRide` replaces (05_sys.c:2170,
            // drift audit M5), so the angles at +432/+436 do not move
            omk::aimHead(pose, s.mo->meshes, head, target, s.look,
                         ride ? 0.0f : static_cast<float>(frameSec * 30.0), false);
        }
    } else if (!ride) {
        s.look = omk::HeadLook{};
    }
}


// THE ROOT MOTION: `Anim_RootDelta`'s running sum, weighted by how much of the pose is the scene's (from `worldStaged`)
void PlayState::worldStagedRootMotion(float& rootW, Staged& s, int& rootFrame,
                                      float (&rootMove)[3]) {
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
        // `Serveur00`, Euler y 145 - plays a walk cycle facing one
        // way and travels the other: walking forward, moving
        // backward.
        // Note the Euler is STICKY, as it is in the engine: only
        // the relative call writes it, `Script_SelectBodyAnimation`
        // never does, and the actor record keeps the last one - so
        // the waiter's ABS walk clips travel under the 145 his
        // preceding REL wait left there.
        static const bool noRootSpin =
            omk::envFlag("OMK_NO_ROOTSPIN");
        // ...and the SENSE is its own question: the body's spin and
        // `Anim_RootDelta`'s multiply are different code paths in the
        // engine and need not share it. `OMK_ROOTSPIN_NEG` turns the
        // delta the other way while leaving the body alone.
        static const float rootSpinSign =
            omk::envFlag("OMK_ROOTSPIN_NEG") ? -1.0f : 1.0f;
        // ...and that multiply is by the WHOLE `+288`, pitch and roll
        // included (todo/drift-audit.md M7): a tilted body's root motion
        // travels along the tilted axes - `rotateEuler` is that row-vector
        // product. A12C1's 33 units of lift under a -7 pitch move him ~4
        // units forward as well.
        static const bool noTiltRoot = omk::envFlag("OMK_NO_TILT");
        const bool rootTilted = !noTiltRoot &&
            (std::fabs(s.progPitch) > 0.001f || std::fabs(s.progRoll) > 0.001f);
        if (!noRootSpin && s.progYawKnown && rootTilted) {
            static const float tiltSignRoot = omk::envFlag("OMK_TILT_NEG") ? -1.0f : 1.0f;
            const float eul[3] = {tiltSignRoot * s.progPitch, rootSpinSign * progYawSign * s.progYaw,
                                  tiltSignRoot * s.progRoll};
            float r[3];
            omk::rotateEuler(eul, rootMove, r);
            for (int k = 0; k < 3; ++k) rootMove[k] = r[k];
        } else if (!noRootSpin && s.progYawKnown && std::fabs(s.progYaw) > 0.01f) {
            float r[3];
            omk::rotateYaw(rootSpinSign * progYawSign * s.progYaw, rootMove, r);
            for (int k = 0; k < 3; ++k) rootMove[k] = r[k];
        }
    }
}


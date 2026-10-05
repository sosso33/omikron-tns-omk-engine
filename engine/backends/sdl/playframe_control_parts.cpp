// SPDX-License-Identifier: GPL-3.0-or-later
// THE CONTROLLER'S FRAME: the binding, the bolts' flight, adventure mode.
// Parts of `PlayState::phaseControl`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// Who is bound to him and who poses him, the shoot camera, the player made, the screens that stop the world, a teleport
void PlayState::controlBinding() {
    OMK_ZONE("control: binding");   // the profiler (todo/debug-tools.md 6)
    const auto& fs = *fs_;
    auto& session = *session_;
    const auto& sc = session.scene();
    // ---- BOUND TO HIM, AND POSING HIM, ARE TWO QUESTIONS ---------
    //
    // Op 46 ends `ScriptObject_StartOnActor(Actor_Player(), object,
    // scene, caller)`, and that function binds the object's program to
    // the player's actor record and puts him in ACTOR_STATE 4:
    //
    //     v18 = u32i(v5, 101);        // +404, his ACTOR_STATE
    //     if (v18 != 4) { u32i(v5, 102) = v18;   // saved at +408
    //                     sub_436D20(node); u32i(v5, 101) = 4; }
    //
    // A CUTSCENE BEAT starts that way, and the flat's DOORS do not:
    // they are `scx.play.wait` (op 58), which is `how` "scene". So
    // this loop never saw them, and the door report of 2026-09-09
    // reaches the frontend through `parkedOnProgram` alone (see
    // `script/area.h`). A `clip >= 0` guard was added here in the same
    // breath as that fix, defensively, and it was WRONG: a beat's clip
    // is its FIRST body animation, so a chain on a step that animates
    // nothing stopped counting and the walker took the body back for a
    // frame. `engine: player program` and `line facing` caught it, one
    // frame out each. The binding is the test, and the clip is only
    // read for the facing below.
    bool playerDriven = false;      // a program is bound to him
    if (sc.loaded()) {
        const auto& started = sc.started();
        for (std::size_t k = 0; k < started.size(); ++k) {
            const auto& stt = started[k];
            if (stt.how != "player" || !sc.programRunning(static_cast<int>(k)))
                continue;
            playerDriven = true;
            if (stt.clip < 0) continue;   // no body step to read a facing off
            const float t = sc.programClock(static_cast<int>(k));
            // `Actor_SetEuler(param 4/5/6)` composes over the clip's
            // root; the Impasse authors 0 there (Started::euler), so
            // the sum is the only reading exercised.
            handoverFacing = omk::headingFromClipRoot(
                sc.scene().clipData(stt.clip), t < 0 ? 0 : static_cast<int>(t))
                + stt.euler[1];
            handoverFacingKnown = true;
        }
    }
    if (session.areasEntered() != playerDrivenArea) {
        playerDrivenArea = session.areasEntered();
        if (!player) {
            // nobody at the keys yet: the new area's beats decide
            playerDrivenSeen = false;
            playerReady = false; adventure = false;
        } else {
            // He IS at the keys. The engine's actor is one record
            // that walks from one decor onto the other; the walker's
            // soup follows the shown slots (`rebuildWorld`) and
            // nothing here rebuilds him. Until 2026-09-03 this reset
            // the controller, and the gate below never rebuilt it -
            // SCENE 55 stayed resident with its programs - so the
            // walk into the airlock froze him in the alley.
            std::printf("frame %ld: area transition %d with the player at the keys - "
                        "he keeps walking\n", n, playerDrivenArea);
        }
    }
    if (playerDriven) playerDrivenSeen = true;
    // ---- WHO A `scx.play.player` PROGRAM POSES --------------------
    //
    // Op 46 (0x00402C30) ends
    // `ScriptObject_StartOnActor(Actor_Player(), object, scene,
    // caller)` - the three pushes before `call sub_419E00` are that
    // call's own last three arguments, since `Actor_Player` takes
    // none - and `ScriptObject_StartOnActor` binds the program to
    // `&g_Actors + 1312 * a1`, the player's actor record, exactly as
    // 59/60 bind one to the actor they name. So the body a player
    // program poses and places is an ACTOR like any other, and the
    // engine draws it through the same walk.
    //
    // This viewer had no path for that: `staged` is built from
    // `Session::shown()`, which the player is not in (no placement
    // record puts him anywhere - the save does), and the controller
    // draws him only in adventure mode, which a program suspends. So
    // during the flat's goodbye the program ran, posed NOBODY, and
    // Kay'l was simply absent from his own cutscene.
    playerProgram = playerDriven;
    // ...and the OUTGOING pool drives bodies too (`poolFor` below
    // searches both), so a program that survives a scene swap keeps
    // him staged. `playerDriven` itself stays the active scene's
    // question, because the camera hold and the adventure gate are
    // about who is at the keys now.
    if (!playerProgram && session.sceneOut().loaded()) {
        const auto& so = session.sceneOut();
        for (std::size_t k = 0; k < so.started().size(); ++k)
            if (so.started()[k].how == "player" &&
                so.programRunning(static_cast<int>(k)))
                playerProgram = true;
    }
    hc = session.cameraTarget();
    // A HELD PLAYER MEANS THE SCRIPT OWNS THE CAMERA.
    //
    // `followCam` used to be a test on the camera's SHAPE alone, and
    // that cannot tell the area's follow camera from a scripted shot:
    // AREA 222's tutorial names 4290/4291/4292, and all three are
    // eyeSubject 0 / atSubject 0 exactly like camera 0. So each shot
    // was fed to `setCameraOffsets` and RE-AIMED THE FOLLOW CAMERA -
    // it kept trailing the player with the follow lag instead of
    // standing as a staged shot, which reads as "the camera never
    // left adventure mode".
    //
    // The engine's own discriminator is the hold: `sub_415D10` (the
    // follow camera) opens `if ((u32(a1,356) & 0x81) != 0) { v2 = 0;
    // v13 = 0; }` - while the player's channel is held its camera mode
    // is forced to 0. And the scripts bracket every staged sequence
    // with 104/105 (SCRIPT_VM 104/105; AREA 222 holds at pc 2360 and
    // releases at 2414, after its last `camera.set`). So: held means
    // the follow controller stands down and the requested camera is
    // resolved as a fixed shot.
    followCam = hc && !hc->absolute() && hc->eyeSubject == 0 &&
                hc->atSubject == 0 && !session.playerAnimHeld();
    const bool beatsOver = playerDrivenSeen || !sc.loaded() || sc.programCount() == 0;
    const bool feetSetLoaded =
        !worldSlots[static_cast<std::size_t>(session.activeSlot() & 1)].geo.corners.empty();
    // ---- SHOOT MODE REPLACES THE CAMERA, it does not decorate it -
    //
    // `Shoot_Enter` sets **both camera actors to the player** and then
    // `Camera_Request(4, ...)`. That is not a modifier on whatever the
    // area was using - it installs a player-relative mode-4 camera
    // over the top of it. The port kept the area's own camera, and in
    // the supermarket that camera (4380) is ABSOLUTE, so `followCam`
    // was false, the view resolved the fixed shot and ignored the
    // player completely.
    //
    // A reader hit exactly that: the aim was running - the diagnostic
    // reported `pitch -47.3, facing 303.1` and the player ended at
    // `facing 283.5` - and the picture never moved, because the
    // camera being aimed was not the camera being drawn
    // (`todo/omk-play.md` 97g).
    if (shootMode && shootCameraLive) followCam = true;
    if (forceAdventure && !hc) followCam = true;   // a street start: no camera asked, the follow one
    const bool wantAdventure = forceAdventure
        ? (session.playerPlaced() && !session.dialogOpen() && !walk && feetSetLoaded)
        : (followCam && !playerDriven && beatsOver &&
           session.playerPlaced() && !session.dialogOpen() && !walk &&
           !sc.activeEditing() && feetSetLoaded);
    if (wantAdventure && !player && !worldSet.empty()) {
        // WHO he is: the DB's player record (+60; `player.become`
        // copies the actor record into it and a save carries it),
        // +144 the model and +72 the bank - then the resident actor
        // record, then the character on screen.
        const auto nameAt = [&](std::size_t off) {
            std::string out;
            const auto raw = state.raw();
            for (std::size_t k = 0; k < 20 && off + k < raw.size(); ++k) {
                const char c = static_cast<char>(raw[off + k]);
                if (!c) break;
                out.push_back(c);
            }
            return out;
        };
        const auto rec = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
        playerModel = nameAt(rec + 144);
        playerCtlName = nameAt(rec + 72);
        std::string modelSrc = "the DB player record";
        if (playerModel.empty()) {
            playerModel = session.modelOfActor(session.playerActor());
            modelSrc = "the resident actor record";
        }
        if (playerModel.empty() && !session.shown().empty()) {
            playerModel = session.shown().front().model;
            modelSrc = "the character on screen";
        }
        std::string ctlSrc = "the DB player record";
        if (playerCtlName.empty()) {
            // RECONSTRUCTION: AREA 118's record for Kay'l (310) names
            // no bank, and the resident record is not reachable from
            // here. The two adventure banks are H1Avnt / F1Avnt.
            playerCtlName = (!playerModel.empty() && playerModel[0] == 'F') ? "F1AVNT" : "H1AVNT";
            ctlSrc = "a LABELLED FALLBACK (the record names none)";
        }
        const auto mo = fs.resolve("MESHES/PERSOS/" + playerModel + ".3DO");
        const auto mt = fs.resolve("MESHES/PERSOS/" + playerModel + ".3DT");
        const auto cp = fs.resolve("ANIMS/" + playerCtlName + ".CTL");
        playerReady = false;
        if (mo && cp && !playerSoup.empty()) {
            const auto md = omk::DataFs::readPath(*mo);
            playerRest = omk::buildGeometry(md, omk::DrawFilter::Engine);
            playerRest.revision = ++worldGeoRev;
            playerMeshes.clear();
            if (const auto mh = omk::readHeader(md)) playerMeshes = omk::readMeshes(md, *mh);
            playerBoneIdx.build(playerMeshes, omk::MeshNameIndex::shadowNames());
            // his PUSH spheres: the model's own list, hung from the feet
            // (`pushSpheresOf` - HO1_FN's four of 10.9), which is what
            // `sub_45E390` reads for the querying body too; the per-mesh
            // spheres (one 42.5 across the whole body) only if it has none
            playerSpheres = omk::pushSpheresOf(md);
            if (playerSpheres.empty()) playerSpheres = omk::collisionSpheresOf(playerMeshes);
            playerReach = playerMeshes.empty() ? 0.0f : playerMeshes.front().radius;
            playerTex = mt ? omk::textures(md, omk::DataFs::readPath(*mt))
                           : std::vector<omk::Texture>{};
            playerCtlData = omk::DataFs::readPath(*cp);
            playerCtl = omk::readCtl(playerCtlData);
            // `playerSoup` is the shown slots' walkable soups, merged
            // by `rebuildWorld` - the floor of BOTH decors.
            if (playerCtl.valid && playerCtl.exact && !playerMeshes.empty() &&
                !playerSoup.empty()) {
                omk::PlayerController::Setup su;
                su.ctl = &playerCtl; su.ctlData = playerCtlData;
                su.meshes = &playerMeshes; su.soup = &playerSoup;
                su.steep = &playerSteep;
                // THE NARROW PHASE (issues 68/75, HANDOFF "the walker
                // does not block on walls"): the same steep faces,
                // swept; the radius is his largest collision sphere
                // (`Actor_Move`: max r over the model's spheres, x
                // dword_910358 = 1.0), and 12 - the sim's stand-in -
                // when the model carries none.
                su.blockers = &playerSteep;
                {
                    // the model's OWN list (descriptor +244/+248), not
                    // the per-mesh crowd-push spheres: HO1_FN's four
                    // are 10.9 each, the crowd list's largest is 42.5
                    const auto sph = omk::modelSweepSpheres(md);
                    float r = 0.0f;
                    for (const auto& c : sph) r = std::max(r, c.radius);
                    su.sweepRadius = r > 0.0f ? r : 12.0f;
                    su.sweepSpheres = sph;
                    std::printf("adventure: the walker sweeps a sphere of radius %.1f "
                                "(the model's %zu sweep spheres%s) against %zu wall faces\n",
                                su.sweepRadius, sph.size(),
                                sph.empty() ? " - none, the sim's 12 stands in" : "",
                                playerSteep.size() / 9);
                }
                for (int k = 0; k < 3; ++k) su.pos[k] = session.playerPos()[k];
                su.facing = handoverFacingKnown ? handoverFacing : session.playerYaw();
                player = std::make_unique<omk::PlayerController>(su);
                // THE CAMERA'S SOLIDS. `sub_417070` casts from the
                // camera's target to 1.2x its eye distance and pulls
                // the eye in to the first hit; the engine's ray
                // (`sub_444810`) walks the scene's meshes, and the
                // nearest thing here is the two soups the walker
                // already keeps - the walkable faces and the steep
                // complement, which between them are the set's solid
                // surfaces. Both are refilled in place by
                // `rebuildWorld`, so the pointers survive a
                // transition the way the walker's do.
                static const bool noCamCollide =
                    omk::envSet("OMK_NO_CAM_COLLIDE");
                if (!noCamCollide)
                    player->setCameraSolids(&playerSteep, &playerSoup);
                // THE GROUND GRID (todo/optimization.md step 9): the
                // walker's floor probe through `playerGrid`, which is
                // rebuilt wherever `playerSoup` is written.
                // `OMK_NO_GROUND_GRID=1` keeps the linear scan;
                // `OMK_VERIFY_GROUND=1` runs both and counts.
                static const bool noGroundGrid = std::getenv("OMK_NO_GROUND_GRID") != nullptr;
                omk::setGroundVerify(omk::envSet("OMK_VERIFY_GROUND"));
                player->setGroundGrid(noGroundGrid ? nullptr : &playerGrid);
                player->setFloorFlags(&playerSoupFlags);
                // the body and camera sweeps through the steep and
                // walkable grids (step 11); `OMK_NO_SWEEP_GRID=1` scans
                static const bool noSweepGrid = std::getenv("OMK_NO_SWEEP_GRID") != nullptr;
                player->setBlockerGrid(noSweepGrid ? nullptr : &playerSteepGrid);
                player->setCameraGrids(noSweepGrid ? nullptr : &playerSteepGrid,
                                       noSweepGrid ? nullptr : &playerGrid);
                // `Walk_ProbeGround` raises event 9 when the decor
                // under his feet changes to a slot in state 2 - and
                // from here on the feet are probed every tick
                // (`decorUnder`, actor/walk.h), so the ACTIVE row
                // follows him across a transition. The teleport
                // raise in `placeActorAt` stays the other source.
                {
                    const float* pp = player->pos();
                    int under = noGroundGrid
                        ? omk::decorUnder(worldDecors, pp[0], pp[1], pp[2])
                        : omk::decorUnder(worldDecors, playerSoup, playerGrid, pp[0], pp[1], pp[2]);
                    if (under >= 0) {                  // state 2 only, as above
                        bool drawn = false;
                        for (int sl = 0; sl < 2; ++sl)
                            if (worldSlots[static_cast<std::size_t>(sl)].area == under &&
                                worldSlots[static_cast<std::size_t>(sl)].shown) drawn = true;
                        if (!drawn) under = -1;
                    }
                    if (under >= 0) session.playerOnArea(under);
                }
                if (haveStand) {
                    player->placeAt(standAt, standAt[3]);
                    std::printf("street start: --stand puts the player at %.0f %.0f %.0f facing %.0f\n",
                                standAt[0], standAt[1], standAt[2], standAt[3]);
                }
                placementSeen = session.placementSeq();
                playerReady = !playerRest.corners.empty();
                playerFeetKnown = false;
                playerCamId = -2;
                handoverFrame = n;
                std::printf("frame %ld: ADVENTURE MODE - the player is %s (%s) on "
                            "%s (%s); %d/%d tracks resolve; %zu walkable "
                            "triangles of %s; standing at %.0f %.0f %.0f facing "
                            "%.0f (%s); arrows walk and turn, RSHIFT runs\n",
                            n, playerModel.c_str(), modelSrc.c_str(),
                            playerCtlName.c_str(), ctlSrc.c_str(),
                            player->tracksMatched(), player->tracksTotal(),
                            playerSoup.size() / 9, worldSet.c_str(),
                            player->pos()[0], player->pos()[1], player->pos()[2],
                            player->facing(),
                            handoverFacingKnown ? "from the last player clip's root"
                                                : "the Session's yaw");
            } else {
                std::printf("adventure: cannot build the player (%s / %s / %s)\n",
                            playerModel.c_str(), playerCtlName.c_str(), worldSet.c_str());
            }
        } else {
            std::printf("adventure: no model/bank/set for %s / %s / %s\n",
                        playerModel.c_str(), playerCtlName.c_str(), worldSet.c_str());
        }
    }
    // WHICH SCREENS STOP THE WORLD, and it is not "any screen".
    //
    // This used to read `!walk` - a screen open, of any kind - and a
    // player found it by opening the sneak in Anekbah and watching
    // the city freeze: 19 world frames in 1924. The engine does not
    // do that. `Game_Tick` (0x004200F0) runs `Script_SetFrameTime`,
    // the per-slot `Script_PlayAllScripts` loop, `Projectiles_Tick`,
    // `Sliders_Tick` and `Slider_TickRide` with NO test for an open
    // screen anywhere in it - the block at its head that looks like
    // one is the start menu's ATTRACT-MODE timeout (screen 29 idle
    // past 1800 units -> `FLIS\GAME.mpg` -> `Game_Start`).
    //
    // The only thing that stops the world is the pause flag
    // `dword_4E9728`, and it has exactly TWO writes in the whole
    // image: screen 31 PAUSE GAME's open callback (0x004ADDB0) sets
    // it and its close (0x004ADEB0) clears it. It works by forcing
    // the frame delta to 0.0, which is why `Slider_TickRide` sits
    // behind `flt_4C30D8 != 0.0` two lines below it in `Game_Tick`.
    //
    // This also REFUTES a banner: `readable/src/05_sys.c` says of
    // `Game_Tick` that "a playing FLIS or interface screen
    // short-circuits the world tick". It does not, and that reading
    // was `NAMED` - read and named, never tested.
    // ...AND NOT BETWEEN TWO BEATS OF ONE CUTSCENE.
    // `!playerDriven && !activeEditing()` asks "is a program driving
    // him THIS FRAME", which is false for the one or two frames
    // between a beat ending and the script starting the next - and
    // for those frames the controller took his body, drew his .CTL
    // idle and the hand-back's `facing 0`. `Session::parkedOnProgram`
    // is the chain still being in flight: the beat's script is parked
    // on the object it started and will start the next when case 3
    // resumes it. See the accessor for why the engine cannot have
    // this fault at all (todo/omk-play.md 78).
    // In SHOOT MODE only a program bound to the player's own body
    // counts (`Session::parkedOnPlayerProgram` has why): an enemy's
    // entrance is gameplay. Outside it the wider test stands - the
    // engine's adventure tick does not gate on parked scripts either,
    // but the cutscene hand-overs this was written for (omk-play 78)
    // have not been re-read against that, so it is left as it was.
    const bool shootLive = session.shootMode().active();
    const bool parked = shootLive ? session.parkedOnPlayerProgram()
                                  : session.parkedOnProgram();
    adventure = player && !playerDriven && !session.dialogOpen() &&
                !uiPause && !sc.activeEditing() && !parked;
    // GAMEPLAY EVENTS ARE NOT CUTSCENES (the reader, 2026-09-10: *"some
    // events (like some ennemie appearing with a special animation) are
    // considered as cutscenes, stops move and change camera, even
    // though they are just gameplay events"*). In the engine only
    // `player.anim.hold` blocks the player's input, and a script
    // camera does not stop the shoot mover. So in shoot mode, say
    // whenever this gate flips and which term flipped it - a freeze a
    // reader meets in play then names its cause in the log.
    if (shootLive) {
        static bool advTold = true;
        if (adventure != advTold) {
            advTold = adventure;
            std::printf("frame %ld: adventure %s in shoot mode -%s%s%s%s%s%s\n", n,
                        adventure ? "ON" : "OFF", player ? "" : " no player",
                        playerDriven ? " playerDriven" : "",
                        session.dialogOpen() ? " dialog" : "",
                        uiPause ? " uiPause" : "",
                        sc.activeEditing() ? " editing" : "",
                        parked ? " parked on the player's program" : "");
        }
    }
    // A TELEPORT IS CONSUMED WHATEVER THE MODE. `sub_41BF50` writes the
    // actor's position outright; nothing about it waits for adventure
    // mode. This sat inside `if (adventure)` below, and Kay'l's flat
    // runs `actor.goto_address 678` and `dialog.start 402` on the
    // SAME pump frame - so by the time the frontend looked, the
    // conversation was open, `adventure` was false, and the body the
    // shot is framed against stayed 18 units short of the address.
    // Camera 4555 is an over-the-shoulder POV authored 15 units in
    // front of 678's face; with him short of it the eye sat inside
    // his head and the close-up drew the inside of it. The engine's
    // own frame of that shot (`traces/frames/dlg402-44`) has no Kay'l.
    if (player && session.placementSeq() != placementSeen) {
        placementSeen = session.placementSeq();
        player->placeAt(session.playerPos(), session.playerYaw());
        std::printf("frame %ld: actor.goto_address %d - the player put down at "
                    "%.0f %.0f %.0f facing %.0f\n", n, session.playerAddress(),
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    player->facing());
    }
    // ...AND THE CHANNEL KEEPS TICKING THROUGH A CONVERSATION.
    //
    // `Game_Tick` runs `Actors_TickAll` whatever is on screen, and
    // ACTOR_STATE 16/17 is in its dispatch - `Actor_TickDialogue`,
    // whose last line is `return Actor_ScanZones(a1)`. So the
    // player's channel goes on running while he talks, and that is
    // what carries the gait he arrived on into the group-400 stance
    // `Actor_EnterDialogueMode` selected.
    //
    // This viewer ticked him only in adventure mode, which a
    // conversation turns off, so he froze on whatever frame the walk
    // left him - a reader's shot of Kay'l standing mid-stride beside
    // Telis for the length of the conversation, arms out, one leg
    // lifted. It only became visible once he was drawn in
    // conversations at all. Nothing pressed, exactly as the `walk`
    // case below and `player.anim.hold` do it.
    // ...and WHETHER HE TICKED, because `specialMoves()` is the list
    // this tick produced and `PlayerController::tick` is the only
    // thing that clears it. A frame that skips the tick - which
    // riding and boarding both do, ACTOR_STATE 7 and 8 not being
    // walkers - therefore re-reads the PREVIOUS tick's list, and on
    // 2026-09-08 that fired `MDSLIDIN` a second time one frame after
    // boarding: he was put aboard, then put aboard again, and the
    // journey's arrival landed on a body that had just been re-seated.
    // Every handler in those two loops is edge-shaped, so the guard
    // belongs on the read and not on each of them.
}

// The bolts' flight, before the actors tick
void PlayState::controlFlight() {
    OMK_ZONE("control: flight");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    // ---- THE FLIGHT - `Projectiles_Tick`, BEFORE `Actors_TickAll` --
    //
    // (`actor/projectile.h`) The frame loop runs it ahead of the actor
    // tick, so a shot fired this frame first moves on the next. The
    // world ray is the ACTIVE set's shot soup - the resident but
    // unlinked neighbour is not in the scene graph the engine's ray
    // walks. The actor sweep (`sub_45E9C0`) is step 2b and not here,
    // so a bolt passes through a gunman and stops at the wall behind.
    // ---- `Shoot_Leave` EMPTIES THE POOL: its first call is
    // `sub_44CDB0(0)`, which frees every live entry of `0x531348`, the
    // 256 x 60 projectile pool - so no bolt outlives the mode. The
    // script pass that ran `shoot.end` comes BEFORE this flight, while
    // the frame's `shootMode` copy is refreshed only further down, so
    // the mode's end is seen here as "was on, is off". Until 2026-09-13
    // the pool was never emptied, and in the gallery a gunman's bolt
    // still in the air killed the player a SECOND time on the restart
    // frame. LABELLED: a player shot fired later on the leaving frame
    // is not caught by this.
    if (projectiles.live() && shootMode && !session.shootMode().active()) {
        std::printf("frame %ld: the projectile pool emptied (Shoot_Leave's "
                    "sub_44CDB0(0)) - %d bolt%s in flight\n", n, projectiles.live(),
                    projectiles.live() == 1 ? "" : "s");
        projectiles.clear();
    }
    if (projectiles.live()) {
        const omk::TriangleSoup* shotSoup = nullptr;
        const WorldSlot* shotSlot = nullptr;
        int shotSlotIdx = -1;
        for (std::size_t k = 0; k < worldSlots.size(); ++k) {
            const auto& ws = worldSlots[k];
            if (!ws.stem.empty() && ws.stem == worldSet) {
                shotSoup = &ws.shotSoup;
                shotSlot = &ws;
                shotSlotIdx = static_cast<int>(k);
            }
        }
        const auto ray = [&](const float a[3], const float b[3], float hit[3], int& mesh) {
            mesh = -1;
            if (!shotSoup) return false;
            const double p0[3] = {a[0], a[1], a[2]};
            const double d[3] = {double(b[0]) - a[0], double(b[1]) - a[1],
                                 double(b[2]) - a[2]};
            const auto h = omk::sweepSphere(*shotSoup, p0, d, 0.0);
            if (!h) return false;
            for (int k = 0; k < 3; ++k) hit[k] = static_cast<float>(p0[k] + h->t * d[k]);
            mesh = shotSlot->shotMeshOf(h->tri);    // the node `sub_444BB0` was visiting
            return true;
        };
        // THE BODIES (`actor/shoothit.h`): every staged actor as he was
        // DRAWN last frame, bystanders included - which is the engine's own
        // order, since the flight runs before the actors tick. Each
        // mesh at its `meshAt` in its `meshRot`, bounded by its own
        // record's +76 centre, +88 radius and +92/+104 box. The
        // player's body is not in the list yet: only his bolts fly.
        std::vector<omk::HitBody> bodies;
        for (const auto& up : staged) {
            // EVERY drawn actor, not only the gunmen: the engine's list
            // is every ATTACHED actor (`Actor_Attach` -> `sub_45DFF0`),
            // so a bolt that meets a bystander stops on him, and
            // `sub_4240E0` refuses the damage because he is not in
            // ACTOR_STATE 3. The supermarket's `V5H_FNM` stands beside
            // its gunman. (The street crowd and the vehicles are the
            // slider system's nodes, not attached actors - not here.)
            omk::HitBody hb;
            if (!up || !hitBodyOf(*up, hb)) continue;
            bodies.push_back(std::move(hb));
        }
        // THE PLAYER'S BODY (the gunmen's shots, step 2): the engine's
        // list is every ATTACHED actor and the player is one, so a
        // gunman's bolt meets him. His own pass him by - the sweep skips
        // a bolt's owner, -1 for his. His meshes as drawn last frame,
        // like everyone's; the first-person frame draws only a few of
        // them, and all of them are posed.
        if (shootMode) {
            omk::HitBody hb;
            if (playerHitBody(hb))
                bodies.push_back(std::move(hb));
        }
        // what the sweep is given, once per shoot mode - the first
        // question when a bolt passes through somebody
        static long bodiesTold = -1;
        if (bodiesTold < 0 || (!shootMode && bodiesTold >= 0)) {
            if (shootMode) {
                bodiesTold = n;
                std::printf("frame %ld: sweep - %zu bodies (staged %zu):", n,
                            bodies.size(), staged.size());
                for (const auto& hb : bodies) {
                    const bool ok = hb.root >= 0 &&
                                    static_cast<std::size_t>(hb.root) < hb.meshes.size();
                    const omk::HitMesh& r0 = ok ? hb.meshes[static_cast<std::size_t>(hb.root)]
                                                : hb.meshes.front();
                    std::printf(" [actor %d root %d at %.0f %.0f %.0f r %.1f, %zu meshes]",
                                hb.actor, hb.root, double(r0.pos[0]), double(r0.pos[1]),
                                double(r0.pos[2]), double(r0.radius), hb.meshes.size());
                }
                std::printf("\n");
                for (const auto& up : staged)
                    if (up && session.shootIn(up->actor) &&
                        (!up->drawn || up->meshRot.empty()))
                        std::printf("  actor %d left out: drawn %d, meshAt %zu, meshRot %zu\n",
                                    up->actor, int(up->drawn), up->meshAt.size(),
                                    up->meshRot.size());
            } else {
                bodiesTold = -1;
            }
        }
        const auto sweep = [&](const float a[3], const float b[3], int owner,
                               float hit[3], int& victim) {
            omk::BodyHit bh;
            if (!omk::shootSweepBodies(a, b, bodies, owner, bh)) return false;
            for (int k = 0; k < 3; ++k) hit[k] = bh.at[k];
            victim = bh.actor;
            return true;
        };
        std::vector<omk::FlightEvent> flown;
        projectiles.fly(static_cast<float>(frameSec * 30.0), ray, &flown, sweep);
        for (const auto& ev : flown) {
            // WHOSE BOLT: the player's lines keep their words and count
            // his own bolts; a gunman's say so, since both fire into
            // the one pool now (`Actor_TickProjectiles`' other arm)
            const bool hisBolt = ev.owner == -1;
            const std::string boltWho = hisBolt ? std::string("SHOT")
                : "GUNMAN BOLT (actor " + std::to_string(ev.owner) + ")";
            const int boltLive = hisBolt ? projectiles.liveOf(-1) : projectiles.live();
            if (ev.why != omk::FlightEvent::Why::Actor) {
                // ...and WHICH set mesh (`sub_4449E0`'s sixth argument)
                const char* meshName = ev.why == omk::FlightEvent::Why::World && shotSlot &&
                        ev.mesh >= 0 && static_cast<std::size_t>(ev.mesh) < shotSlot->meshes.size()
                    ? shotSlot->meshes[static_cast<std::size_t>(ev.mesh)].name : "-";
                std::printf("frame %ld: %s retired - entry %d %s at %.1f %.1f %.1f "
                            "after %.1f, %d live, mesh %d %s\n", n, boltWho.c_str(), ev.entry,
                            ev.why == omk::FlightEvent::Why::World ? "hit the world"
                                                                 : "out of range",
                            double(ev.at[0]), double(ev.at[1]), double(ev.at[2]),
                            double(ev.travelled), boltLive, ev.mesh, meshName);
                // `off_4C8444(meshNode)` at 0x44DDDF, BEFORE the impact
                // effect: the world-hit callback, which only Astaroth's
                // setup installs (`sub_47FCF0`, `actor/astaroth.h`) - any
                // bolt, any owner, any damage
                if (ev.why == omk::FlightEvent::Why::World && astaroth.callbackArmed)
                    astarothWorldHit(n, ev.mesh, shotSlotIdx, ev.at);
                // `sub_44F0D0`: the IMPACT effect where the world
                // stopped it - the range running out calls nothing
                if (ev.why == omk::FlightEvent::Why::World) {
                    shotSound(n, ev.impactEffect, ev.at,
                              player ? player->pos() : nullptr,
                              hisBolt ? "impact" : "impact of a gunman's bolt");
                    // the NOISE where it stopped, the maker excluded
                    // (0x44DE15, after `sub_44F0D0`)
                    shootNoise(n, -1, ev.at, "a bolt on the world");
                }
                continue;
            }
            std::printf("frame %ld: %s retired - entry %d HIT ACTOR %d at %.1f %.1f "
                        "%.1f after %.1f, %d live\n", n, boltWho.c_str(), ev.entry,
                        ev.victim, double(ev.at[0]), double(ev.at[1]), double(ev.at[2]),
                        double(ev.travelled), boltLive);
            // ...and on a BODY, whatever `sub_4240E0` then decides
            shotSound(n, ev.impactEffect, ev.at, player ? player->pos() : nullptr,
                      hisBolt ? "impact on a body" : "impact of a gunman's bolt on a body");
            // the NOISE there, the VICTIM excluded - 0x44DD0E / 0x44DD40
            // hand `sub_4246E0` the index `sub_4240E0` was given
            shootNoise(n, ev.victim, ev.at, "a bolt on a body");
            // ---- `sub_4240E0`'s PLAYER ARM: a gunman's bolt on him ----
            //   the damage through his Body Shield (property 17), C's
            //   truncation and never 0; +92 -= it; `dword_90E100 = +92`,
            //   the gauge; `Game_RaiseEvent(45, {1, him, +92})` -
            //   property 1 through `Actor_SetProperty`'s UNSIGNED clamp
            //   at 200, so a health below 0 is STORED as 200; at +92 <= 0
            //   the death `sub_423FC0`; the hurt shove `sub_47D1F0`; then
            //   `Game_RaiseEvent(43, {0, him})` - MESSAGE 0, the scene's
            //   hurt handler (the supermarket's uses a carried kit below
            //   40, and its heal comes back through `shootStatSet`).
            // NOT PORTED, labelled: the death and the shove.
            if (ev.victim == -1) {
                shootWake(n, "a bolt's hit on the player (sub_4240E0)");
                const std::size_t recAt = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                const std::size_t recLen = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
                omk::HitIn hin;
                hin.damage = ev.damage;
                hin.shooterIsPlayer = false;
                hin.victimIsPlayer = true;
                hin.victimInShoot = player && player->state() == omk::ActorState::Shoot;
                std::int32_t shield = 0;
                omk::readActorProperty(state.raw().subspan(recAt, recLen), 17, shield);
                hin.bodyShield = shield;
                hin.victimYaw = player ? player->facing() : 0.0f;
                for (int k = 0; k < 3; ++k) hin.boltVel[k] = ev.vel[k];
                const omk::HitOut ho = omk::shootApplyHit(playerShootRec, hin);
                if (ho.refused) {
                    std::printf("  PLAYER hit REFUSED (`sub_4240E0` returns -1) - the bolt "
                                "stops anyway\n");
                    continue;
                }
                if (ho.healthWas <= 0) {
                    // `if (+92 <= 0) return v30;` - down already: nothing
                    std::printf("frame %ld: PLAYER HIT by actor %d's bolt - he is down "
                                "already (health %d): nothing\n", n, ev.owner, ho.healthWas);
                    continue;
                }
                applyPlayerDamage(n, ev.owner, "bolt", ev.damage, int(shield), ho);
                continue;
            }
            // ---- `sub_4240E0`, the damage, on his shoot record ----
            shootWake(n, "a bolt's hit (sub_4240E0)");
            Staged* vs = nullptr;
            for (auto& up : staged)
                if (up && up->actor == ev.victim) { vs = up.get(); break; }
            const auto bit = shootBrains.find(ev.victim);
            if (!vs || bit == shootBrains.end()) {
                std::printf("  (no shoot record for actor %d: nothing to damage)\n",
                            ev.victim);
                continue;
            }
            omk::HitIn hin;
            hin.damage = ev.damage;
            hin.shooterIsPlayer = ev.owner == -1;
            hin.victimIsPlayer = false;
            hin.victimInShoot = session.shootIn(ev.victim);
            std::int32_t p24 = 0;
            session.actorProperty(ev.victim, 24, p24);
            hin.reactAt = p24;
            hin.victimYaw = vs->facing;
            for (int k = 0; k < 3; ++k) hin.boltVel[k] = ev.vel[k];
            // ASTAROTH's gate, `sub_47FD90` (`actor/astaroth.h`): nothing
            // reaches his body while one of the six souls stands; then a
            // bolt travelling WITH his facing whose segment meets his
            // `AstDos` (the sweep over every body but the PLAYER's, that
            // one mesh only) goes through and he reacts; any other hit
            // runs down his `+88` towards the flinch.
            if (bit->second.type == static_cast<std::uint32_t>(omk::kAstarothType))
                hin.typeGate = [&](int dmg) {
                    omk::ShootRecord& ar = bit->second;
                    const int stateWas = ar.state;
                    omk::BodyHit bh;
                    const auto backSweep = [&]() {
                        return omk::shootSweepBodies(ev.seg, ev.seg + 3, bodies, -1, bh,
                                                     ev.victim, astaroth.backMesh);
                    };
                    const omk::AstarothGate g = omk::astarothGate(astaroth, ar, dmg, vs->facing,
                                                                  ev.seg, backSweep);
                    using R = omk::AstarothGate::Reaction;
                    // where his AstDos is drawn, for the reader of a miss
                    float back[3] = {0.0f, 0.0f, 0.0f};
                    for (const auto& hb : bodies)
                        if (hb.actor == ev.victim && astaroth.backMesh >= 0 &&
                            static_cast<std::size_t>(astaroth.backMesh) < hb.meshes.size())
                            for (int k = 0; k < 3; ++k)
                                back[k] = hb.meshes[static_cast<std::size_t>(astaroth.backMesh)].pos[k];
                    std::printf("  ASTAROTH's gate (sub_47FD90): %d of 6 souls down, state %d, "
                                "%s, %s - +88 %d (AstDos at %.0f %.0f %.0f)\n",
                                astaroth.destroyed, stateWas,
                                !((stateWas < 17 || stateWas > 19) &&
                                  astaroth.destroyed >= omk::kAstarothSouls)
                                    ? "direction untested"
                                    : g.fromBehind ? "from behind" : "from in front",
                                g.reaction == R::Back ? "IN THE BACK (AstDos)"
                                : g.reaction == R::Flinch ? "refused, he FLINCHES"
                                : "refused", ar.actionCounter, double(back[0]),
                                double(back[1]), double(back[2]));
                    if (g.reaction != R::None) {
                        // `sub_44DEB0`: his bolts still waiting at the muzzle
                        const int gone = projectiles.cancelWaiting(ev.victim);
                        // the reaction: a random TYPE-4 clip, or clip ID 14
                        // (`List_PickRandomByType` is `rand()`; this port's
                        // pick is the fixed one, as for every gunman)
                        const int grpR = static_cast<int>(ar.type);
                        const omk::PedClip* rc = g.reaction == R::Back
                            ? shootClipExact(grpR, 4) : shootClipBySlot(grpR, 14);
                        GunClip& gc = gunClips[ev.victim];
                        gc = GunClip{};
                        if (rc) {
                            gc.type = rc->type;
                            gc.slot = rc->slot;
                            gc.frames = rc->frames;
                            gc.frame = 1.0f;          // `sub_421A20`
                        }
                        gc.turn = 0.0f;               // `+184 = 0`
                        vs->walkMove[1] = 0.0f;
                        std::printf("  ASTAROTH reacts - clip id %d (type %d, %d frames), "
                                    "played %d time%s, %d waiting bolt%s cancelled "
                                    "(sub_44DEB0)\n", rc ? rc->slot : -1, rc ? rc->type : -1,
                                    rc ? rc->frames : 0, ar.repeats > 0 ? ar.repeats : 1,
                                    ar.repeats > 1 ? "s" : "", gone, gone == 1 ? "" : "s");
                        // `sub_44EF00(20, the AstDos node, 0, 1)`
                        if (g.reaction == R::Back && bh.body >= 0 &&
                            static_cast<std::size_t>(bh.body) < bodies.size()) {
                            const omk::HitBody& hb = bodies[static_cast<std::size_t>(bh.body)];
                            if (bh.mesh >= 0 && static_cast<std::size_t>(bh.mesh) < hb.meshes.size())
                                shotSound(n, 20, hb.meshes[static_cast<std::size_t>(bh.mesh)].pos,
                                          player ? player->pos() : nullptr,
                                          "Astaroth hit in the back (sub_44EF00 20)");
                        }
                    }
                    return g.damage;
                };
            // GANDHAR's gate, `sub_47DF60` (`todo/gandhar.md` 3b): once the
            // 0x4000 rule has let the BATON's bolt through, `sub_45E9C0`
            // sweeps its segment again over every body but the player's,
            // accepting his `D3Tete` mesh alone (`dword_657A24`, found by
            // name at his entry) - a miss refuses the damage, a hit plays
            // effect 19 at `*(D3Tete)+36` (his node plus the head's rest
            // offset) and passes it. He is hurt only through his HEAD.
            if (bit->second.type == static_cast<std::uint32_t>(omk::kGandharType))
                hin.typeGate = [&](int dmg) {
                    int head = -1;
                    if (vs->mo)
                        for (std::size_t mi = 0; mi < vs->mo->meshes.size(); ++mi)
                            if (std::strcmp(vs->mo->meshes[mi].name, "D3Tete") == 0) {
                                head = static_cast<int>(mi);
                                break;
                            }
                    omk::BodyHit bh;
                    const bool inHead = head >= 0 &&
                        omk::shootSweepBodies(ev.seg, ev.seg + 3, bodies, -1, bh, ev.victim, head);
                    float at[3] = {0.0f, 0.0f, 0.0f};
                    if (const auto ga = gandharActors.find(ev.victim); ga != gandharActors.end()) {
                        float rest[3] = {0.0f, 0.0f, 0.0f};
                        if (vs->mo) omk::headRestOffset(vs->mo->meshes, rest);
                        for (int k = 0; k < 3; ++k) at[k] = ga->second.node[k] + rest[k];
                    }
                    // where his D3Tete is DRAWN, for the reader of a miss
                    float hd[3] = {0.0f, 0.0f, 0.0f};
                    for (const auto& hb : bodies)
                        if (hb.actor == ev.victim && head >= 0 &&
                            static_cast<std::size_t>(head) < hb.meshes.size())
                            for (int k = 0; k < 3; ++k)
                                hd[k] = hb.meshes[static_cast<std::size_t>(head)].pos[k];
                    std::printf("  GANDHAR's gate (sub_47DF60): the bolt %s his D3Tete (mesh %d, "
                                "drawn at %.0f %.0f %.0f; the bolt %.0f %.0f %.0f -> %.0f %.0f "
                                "%.0f) - damage %d %s\n", inHead ? "MEETS" : "misses", head,
                                double(hd[0]), double(hd[1]), double(hd[2]), double(ev.seg[0]),
                                double(ev.seg[1]), double(ev.seg[2]), double(ev.seg[3]),
                                double(ev.seg[4]), double(ev.seg[5]), dmg,
                                inHead ? "passes" : "refused");
                    if (!inHead) return 0;
                    shotSound(n, 19, at, player ? player->pos() : nullptr,
                              "Gandhar hit in the head (sub_44EF00 19)");
                    return dmg;
                };
            const omk::HitOut ho = omk::shootApplyHit(bit->second, hin);
            if (ho.refused) {
                std::printf("  hit REFUSED (`sub_4240E0` returns -1) - the bolt stops "
                            "anyway\n");
                continue;
            }
            std::printf("  hit: damage %d, health %d -> %d, band %d, reaction %d "
                        "(threshold %d)\n", ho.damage, ho.healthWas, ho.health,
                        ho.band, ho.action, int(p24));
            if (ho.action >= 0)
                session.shootModeMutable().actorAction(ev.victim, ho.action);
            // ...and `sub_423EF0`'s tail, whether or not he reacted: MESSAGE 2,
            // "a gunman was hit and lives", his index as the sender - which
            // `Message_RunHandlers` maps to his CHARACTERS id, as the handlers
            // compare it (AREA 144: sender 403, the X-Tech sentinel, retires
            // zone 2349 'ztech sentinelle attaque'). A KILLED gunman posts
            // nothing (todo/drift-audit.md S8). `byte_90E0B0 = 4`, the same
            // tail's other write, has no reader found and is not modelled.
            if (!ho.killed) {
                const bool ran = session.postMessage(2, ev.victim);
                std::printf("  message 2 (sub_423EF0) - actor %d hit and alive: %s\n",
                            ev.victim, ran ? "handled" : "unsubscribed");
            }
            if (ho.killed && bit->second.type == static_cast<std::uint32_t>(omk::kAstarothType)) {
                // no type 5..8 clip in his group, so `sub_4240E0`'s kill arm
                // starts nothing: the back-hit clip plays on, and his own
                // prologue reports the death when it ends (message 3)
                std::printf("  KILLED - Astaroth has no death clip: the back hit's clip plays "
                            "out first%s\n", ho.enemyCountDrop ? ", the enemy count drops" : "");
            } else if (ho.killed) {
                vs->deathType = ho.deathType;
                vs->deathStart = gameClock;
                vs->walkMove[1] = 0.0f;   // `sub_421A20` sets the height
                std::printf("  KILLED - death clip type %d%s\n", ho.deathType,
                            ho.enemyCountDrop ? ", the enemy count drops" : "");
            }
        }
    }
}

// The player ticked under a conversation, and adventure mode's controller frame
void PlayState::controlAdventure() {
    OMK_ZONE("control: adventure");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    playerTicked = false;
    if (player && !adventure && session.dialogOpen()) {
        player->tick(static_cast<float>(frameSec * 30.0), 0);
        playerTicked = true;
    }
    if (adventure) {
        adventureAim();
        adventurePathField();
        adventureDeath();
        adventureCrowdPush();
        adventureScreenInput();
        adventureSeated();
        adventureRide();
        adventureWalls();
        adventureTake();
        adventureShot();
        adventureAction();
    }
}

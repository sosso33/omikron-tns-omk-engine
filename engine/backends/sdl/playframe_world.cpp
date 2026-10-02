// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S WORLD PHASE - whoever is on screen, and the world when no screen is over it.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
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

int PlayFrame::phaseWorld() {
    bool done = false;
    do {
        // THE SET'S OWN EMITTERS, into the pool that owns them: whenever the
        // resident runner has attached a `.sfx` and not yet been bound, and the
        // set of ITS area is loaded. `Area_LoadScx` binds as the `.SCX` lands,
        // against that area's set - which is the pairing this keeps, whichever
        // of the two a transition brings in first.
        //
        // Tried twice a frame: here, for a `.SCX` the Session's tick has just
        // made resident over a set already in, and again below once the
        // frame's sets are in - so a load that brings the set and its `.SCX`
        // in the SAME frame (the boot, a save) binds on that frame, as
        // `Area_LoadScx` does, and not one frame late. Bound from here alone,
        // a save's street started its neon and steam a frame behind: 918
        // particles at frame 20 for 952, and no sprite in frame 1's pool
        // (`engine: sprite table`, red from 2026-09-30 to 2026-10-01).
        const auto bindSetEmittersNow = [&]() {
            if (!session.scene().loaded() || session.scene().setEmittersBound()) return;
            for (const WorldSlot& ws : worldSlots) {
                if (ws.stem.empty() || ws.area != session.sceneArea()) continue;
                const int bound = session.sceneMutable().bindSetEmitters(ws.emitters);
                if (bound > 0)
                    std::printf("world: frame %ld  %s binds %d ambient emitters into %s\n",
                                n, ws.stem.c_str(), bound, session.scene().file().c_str());
                break;
            }
        };
        bindSetEmittersNow();

        // The absolute world cameras the script has set, as rays. Collected
        // BEFORE the character is staged, because `character.show` and the two
        // `camera.set`s before it land in the same VM run - AREA 118 sets 2172
        // at pc 1170, 2148 at 1177 and shows Kay'l at 1184 - so a solve that
        // ran first would have nothing to solve over.
        if (const omk::WorldCamera* tc = session.cameraTarget()) {
            if (tc->absolute() && tc->id != lastRayCam) {
                lastRayCam = tc->id;
                omk::CameraRay r;
                for (int k = 0; k < 3; ++k) { r.eye[k] = tc->eye[k]; r.at[k] = tc->at[k]; }
                worldRays.push_back(r);
            }
        }

        // ---- WHOEVER IS ON SCREEN --------------------------------------
        //
        // `character.show` is what puts a character in the world, and AREA 118
        // does it about six seconds before `dialog.start` - so loading the
        // model when a conversation opens drew the arrival as a black screen.
        // `Session::shown()` is now EVERY attached actor of both resident
        // slots (the alley's passers-by among them) followed by whatever a
        // script showed, so this stages them all rather than `front()`.
        {
            for (auto& up : staged) up->seen = false;
            const int playerId = session.playerActor();
            // ---- REINCARNATION: `player.become` moves the player into ----
            // another actor's body (`Session::becomePlayer`, the DB record's
            // +144 naming the new `.3DO`). The controller was built once, for
            // one model's meshes, `.CTL` bank and collision soup, and the
            // hand-over gate that builds it runs only on `!player` - so a
            // become mid-game kept the old body. The reader: *"different
            // characters can be played in the game, not just Kay'l, so if you
            // just force loading his model it will not work anymore when
            // switching characters."* The rule is: rebuild the controller
            // when the player ACTOR changes, keep it otherwise (an area load
            // does not change him).
            static int lastPlayerActor = -2;
            if (lastPlayerActor == -2) lastPlayerActor = playerId;
            if (playerId != lastPlayerActor) {
                if (player) {
                    std::printf("frame %ld: player.become - the player is actor %d now, was %d; "
                                "the controller is rebuilt for the new body\n", n, playerId, lastPlayerActor);
                    player.reset();
                    playerReady = false; adventure = false;
                    forceAdventure = true;
                }
                lastPlayerActor = playerId;
            }
            for (const auto& sh : session.shown()) {
                // In adventure mode the CONTROLLER owns the player's body; a
                // second one here would draw him twice.
                if ((adventure || session.dialogOpen()) && player && sh.actor == playerId) continue;
                Staged* s = nullptr;
                for (auto& up : staged) if (up->actor == sh.actor) { s = up.get(); break; }
                if (!s) {
                    staged.push_back(std::make_unique<Staged>());
                    s = staged.back().get();
                    s->actor = sh.actor;
                    s->model = sh.model;
                    s->bank  = sh.bank;
                    s->mo = charModelFor(sh.model);
                    s->bk = charBankFor(sh.bank);
                    ++stagedEver;
                    stagedIds.push_back(sh.actor);
                    std::printf("frame %ld: staged actor %d %s (bank %s, %zu meshes, "
                                "%zu textures) at %.0f %.0f %.0f facing %.0f - %s\n",
                                n, sh.actor,
                                sh.model.empty() ? "(no model)" : sh.model.c_str(),
                                sh.bank.empty() ? "none" : sh.bank.c_str(),
                                s->mo ? s->mo->meshes.size() : 0u,
                                s->mo ? s->mo->tex.size() : 0u,
                                sh.pos[0], sh.pos[1], sh.pos[2], sh.facing,
                                sh.fromTable ? "a placement record puts him here"
                                             : "shown by a script, no placement of his own");
                }
                s->seen = true;
                // A placement record names a spot on the GROUND; a script
                // show carries none (`Session::showCharacter` pushes a bare
                // record), so such a body waits for a program or a camera
                // solve to say where he is.
                // ...BUT NOT ONCE A PROGRAM HAS MOVED HIM. This ran every
                // frame and overwrote both the position and the FACING from
                // the 20-byte record. While a program drives, the placement
                // below puts its own back (`progPlaced && sceneClip >= 0`),
                // so the clobber was invisible; the frame the program ENDS,
                // `progPlaced` goes false and this wins - the body snaps back
                // to where the chunk parked him, which for Kay'l's flat is
                // 3635/1278/-656 against a floor at 1040, and vanishes.
                //
                // `Script_SelectBodyAnimation` never resets the node
                // (CLAUDE.md 6), so the accumulated placement STANDS; the
                // branch that carries `drawAt` over on the clip-change frame
                // is already written for exactly that and was being undone
                // one frame later. The facing half is the same bug seen from
                // the side: a reader watched Telis "look the wrong way, then
                // go back to the right one for the idle" - the record's
                // facing and the program's Euler alternating.
                // ...NOR ONCE THE FIGHT OWNS HIM. A melee opponent no program
                // ever moved - the flat's training partner, CHARACTERS 331 -
                // was put back here every frame after the fight had moved
                // him: drawn standing at his record (7957, -852) while his
                // walker, his blows and the AI fought the player 200 units
                // away (a reader, 2026-10-02: "hit by nothing visible next to
                // me, the partner moving in the background"). `Fight_Begin`
                // keeps one actor record; its position is the fight's.
                // ...and not AFTER it either: `sub_445AC0` leaves the loser's
                // actor record where he fell, so the record must not take
                // him back to the chunk's spot once the fight has ended.
                const bool fightOwns = (fightRun.active && fightRun.body == s) || s->fightPlaced;
                if (sh.fromTable && !s->progRan && !fightOwns) {
                    for (int k = 0; k < 3; ++k) s->at[k] = sh.pos[k];
                    // ...and NOT the facing once a SHOOT BRAIN owns him: his
                    // heading is then the record's `+420`, which the brain and
                    // its turn clips write every tick. Put back from the
                    // placement each frame, a turn kept only its last tick - a
                    // reader's robber 519 restarted a turn clip every 14 frames
                    // at 68.0 for the rest of the phase, and 521 turned 7.2 of
                    // his 180. (77 escaped it: a program had moved him.)
                    if (!shootBrains.count(sh.actor)) s->facing = sh.facing;
                    if (!s->placed) { s->placed = true; s->pelvis = false; }
                }
            }
            // A conversation's speaker need not be in `shown()` at all -
            // AREA 118's is, but the camera solve is the only thing that says
            // where a speaker no table places and no script shows is standing.
            if (session.dialogOpen() && speakerReady && !speakerModel.empty()) {
                const int sp = session.dialogue().conversation().speaker;
                Staged* s = nullptr;
                for (auto& up : staged) if (up->actor == sp) { s = up.get(); break; }
                if (!s)
                    for (auto& up : staged)
                        if (up->model == speakerModel) { s = up.get(); break; }
                if (!s) {
                    staged.push_back(std::make_unique<Staged>());
                    s = staged.back().get();
                    s->actor = sp;
                    s->model = speakerModel;
                    s->mo = charModelFor(speakerModel);
                    ++stagedEver;
                    stagedIds.push_back(sp);
                    std::printf("frame %ld: staged actor %d %s for conversation %d - "
                                "no table places him and no script shows him\n",
                                n, sp, speakerModel.c_str(), speakerConv);
                }
                s->seen = true;
                if (!s->placed && speakerSolved) {
                    for (int k = 0; k < 3; ++k) s->at[k] = speakerAt[k];
                    s->placed = true;
                    s->pelvis = false;   // the solve names a spot on the GROUND
                }
            }
            // ...and neither need the PLAYER, when a `scx.play.player`
            // program owns him. The engine has no separate player body: op 46
            // starts the program on `Actor_Player()`'s own actor record, so it
            // is that actor the program poses, places and turns. He joins the
            // staged list exactly while such a program runs - which is also
            // exactly when the adventure controller stands down, so the two
            // can never both draw him - and the sweep below drops him again
            // when it ends.
            const int progPid = session.playerActor();
            if (playerProgram && player && progPid >= 0 && !playerModel.empty()) {
                const int pid = progPid;
                Staged* s = nullptr;
                for (auto& up : staged) if (up->actor == pid) { s = up.get(); break; }
                if (!s) {
                    staged.push_back(std::make_unique<Staged>());
                    s = staged.back().get();
                    s->actor = pid;
                    s->model = playerModel;
                    s->bank  = playerCtlName;
                    s->mo = charModelFor(playerModel);
                    s->bk = charBankFor(playerCtlName);
                    ++stagedEver;
                    stagedIds.push_back(pid);
                    // Where he stands until the program's first body step
                    // RUNS: the walker's own position, which is where the
                    // world last had him. `pelvis` is false because that is a
                    // point on the GROUND (`Walk_ProbeGround`'s anchor), not
                    // an authored hip height.
                    for (int k = 0; k < 3; ++k) s->at[k] = player->pos()[k];
                    s->at[1] -= playerFeetKnown ? playerFeet : 0.0f;
                    s->facing = player->facing();
                    s->placed = true;
                    s->pelvis = false;
                    std::printf("frame %ld: staged the PLAYER as actor %d %s "
                                "(bank %s) at %.0f %.0f %.0f facing %.0f - a "
                                "scene program owns his body (scx.play.player "
                                "starts on Actor_Player())\n",
                                n, pid, playerModel.c_str(),
                                playerCtlName.empty() ? "none" : playerCtlName.c_str(),
                                s->at[0], s->at[1], s->at[2], s->facing);
                }
                s->seen = true;
            } else if (playerProgramWas && player) {
                // ---- AND THE HAND BACK -------------------------------
                //
                // The engine has one body: the program moved the player's own
                // actor, so when it ends he is standing where it left him and
                // the walker carries on from there. Handing back the
                // CONTROLLER's stale position instead would snap him across
                // the room the frame the cutscene ends - the same shape as the
                // placement-record clobber, one level up.
                for (const auto& up : staged)
                    if (up->actor == session.playerActor() && up->progRan) {
                        // ...and the yaw the body was DRAWN with, which for a
                        // program-driven one is the step's own Euler and not
                        // the placement record's facing (`bodyYaw` below).
                        const float yaw = up->progYawKnown ? up->progYaw : up->facing;
                        player->placeAt(up->drawAt, yaw);
                        session.setPlayerPosition(up->drawAt, yaw);
                        std::printf("frame %ld: the program ended - the player "
                                    "keeps the body's place, %.0f %.0f %.0f "
                                    "facing %.0f\n", n, up->drawAt[0],
                                    up->drawAt[1], up->drawAt[2], yaw);
                        break;
                    }
            }
            playerProgramWas = playerProgram;
            for (std::size_t k = 0; k < staged.size(); ) {
                if (staged[k]->seen) { ++k; continue; }
                std::printf("frame %ld: dropped actor %d %s\n", n,
                            staged[k]->actor, staged[k]->model.c_str());
                staged.erase(staged.begin() + static_cast<long>(k));
                ++poolComposition;
            }
            // A model no staged body wears any more leaves the pool, so the
            // 64 slots a bucket key can address are not spent on the last
            // area's cast.
            for (auto it = charModels.begin(); it != charModels.end(); ) {
                bool used = (it->first == playerModel && playerReady) ||
                            it->first == speakerModel;
                for (const auto& up : staged) if (up->model == it->first) used = true;
                // ...AND the models the traffic circuit's own bodies wear.
                // The crowd's walkers and the road traffic's two vehicles are
                // not `staged` actors, so this loop was erasing their models
                // every frame while `PedStaged::mo` and `VehStaged::mo` went
                // on pointing at the freed node. The crowd never showed it
                // because a city's authored extras wear the SAME PERSOS
                // models and kept them resident by accident; `sli_fn` and
                // `moto` are worn by nothing else, so the traffic staged
                // itself once and then vanished - which is how this was
                // found (2026-09-04).
                if (!used) {
                    const auto& circuit = session.sliders();
                    for (const auto& w : circuit.movers())
                        if (w.live && w.model == it->first) { used = true; break; }
                    if (!used)
                        for (const auto& v : circuit.vehicles())
                            if (v.live && v.model == it->first) { used = true; break; }
                }
                if (used) { ++it; continue; }
                if (it->first == playerModel)
                    std::printf("frame %ld: the PLAYER's model %s evicted - playerReady %d, "
                                "nobody staged wears it\n", n, it->first.c_str(), playerReady ? 1 : 0);
                it = charModels.erase(it);
                ++poolComposition;
            }
        }

        mark("screens");
        // ---- the world, when no screen is over it -----------------------
        //
        // The sets follow the resident slots' STATE: a slot whose decor the
        // Session has in state 2 is loaded, one it took back to state 1 is
        // dropped. During a transition that is two sets at once.
        {
            bool changed = false, skyChanged = false;
            for (int slot = 0; slot < 2; ++slot) {
                const auto& rs = session.residentSlot(slot);
                const bool shown = session.slotShown(slot) && rs.area != -1;
                // THE SET STAYS WHILE THE SLOT HOLDS ITS AREA - hidden or not.
                // This used to load only a SHOWN slot, so hiding a set dropped
                // its floor and walls along with its picture. A reader,
                // 2026-09-18: *"many times I went through the security center
                // barriers and fall (not possible in the original game)"* -
                // `area.arrive -1` hides the level the lift has just brought in
                // (`ACSLEV-N`), and every railing, wall and floor of it went.
                const std::string want = rs.area != -1 ? rs.set : std::string();
                const int wantArea = rs.area;
                WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
                if (want != slotAsked[slot] || wantArea != slotAskedArea[slot])
                    changed |= askSet(slot, want, wantArea, n);
                // ...and IN on the frame BEFORE the Session's last slice is
                // served, so the set is in the world when `Area_TickLoad`'s
                // cases 2..9 run and the frame loop binds its emitters to the
                // arriving `.SCX` - the frame they bound on before. At once for
                // a load the engine does not stream (mode 0) or of one slice.
                if (setLoads[slot]) {
                    const bool streaming = !syncSets && session.loading() &&
                                           session.loadingSlot() == slot &&
                                           session.loadSlicesLeft() > 1;
                    // ...unless the Session's load is held for it (the gate):
                    // then neither waits, and the game draws on
                    SetLoad& L = *setLoads[slot];
                    const bool held = loadGate && L.job && !L.job->ready() &&
                                      session.loading() && session.loadingSlot() == slot;
                    if (!streaming && held) ++L.heldFrames;
                    if (!streaming && !held) {
                        changed |= integrateSet(L, n);
                        setLoads[slot].reset();
                    }
                }
                w.shown = shown && !w.stem.empty();
                // THE SKY IS GLOBAL AND EITHER SLOT CAN BRING IT.
                //
                // `Area_LoadMiscModel` keeps ONE model rather than one per
                // slot: `Area_TickLoad` case 4 calls it for the area being
                // loaded, so an area naming a sky REPLACES whatever is there
                // and one naming none (`*a1` false) leaves the previous
                // standing rather than clearing it. Loaded here so it arrives
                // with the set.
                //
                // **This asked slot 0 only, and that is a fault a reader
                // met**: *it happens when I load a save located in a
                // building*. A load puts its own area in slot 0, so walking
                // out of it lands the city in SLOT 1 - their own session log
                // has `SHOW area 217 in slot 0` and then `SHOW area 0 in slot
                // 1` - and Anekbah's `ASKY` was never asked for. Load in the
                // street instead and the same area arrives in slot 0 and the
                // sky is there, which is why it looked like a black sky "in
                // some cases".
                {
                    const std::string wantSky = shown ? rs.sky : std::string();
                    if (!wantSky.empty() && wantSky != sky.stem) {
                        sky = Sky{};
                        const auto o = fs.resolve("MESHES/MISC/" + wantSky + ".3DO");
                        if (!o) std::printf("sky: no model %s.3DO\n", wantSky.c_str());
                        else {
                            const auto d = omk::DataFs::readPath(*o);
                            sky.stem = wantSky;
                            sky.geo  = omk::buildGeometry(d, omk::DrawFilter::Engine);
                            sky.base = sky.geo.corners;
                            const auto t = fs.resolve("MESHES/MISC/" + wantSky + ".3DT");
                            if (t) sky.tex = omk::textures(d, omk::DataFs::readPath(*t));
                            const auto hdr = omk::readHeader(d);
                            if (hdr) {
                                const auto ms = omk::readMeshes(d, *hdr);
                                if (!ms.empty())
                                    for (int k = 0; k < 3; ++k) sky.origin[k] = ms[0].pos[k];
                            }
                            sky.rest = sky.geo;
                            for (auto& c : sky.rest.corners) {
                                c.x = (c.x - sky.origin[0]) * kSkyScale;
                                c.y = (c.y - sky.origin[1]) * kSkyScale;
                                c.z = (c.z - sky.origin[2]) * kSkyScale;
                            }
                            sky.rest.revision = ++worldGeoRev;
                            sky.placed = false;
                            std::printf("sky: %s - %zu corners, %zu texture(s), origin"
                                        " %.1f %.1f %.1f, drawn at y %.1f\n",
                                        wantSky.c_str(), sky.base.size(), sky.tex.size(),
                                        sky.origin[0], sky.origin[1], sky.origin[2],
                                        sky.origin[1] - kSkyLift);
                            skyChanged = true;
                        }
                    }
                }
            }
            // A NEW SKY is a new section of the texture pool and nothing else:
            // it used to take the whole rebuild below - both sets' soups and
            // grids again, and a new `worldGen`, which bakes the depth tie
            // again - one frame after the set that names it had just done so.
            if (skyChanged && !changed) ++poolComposition;
            if (changed) {
                ++poolComposition;      // the sky section of the pool changed
                rebuildWorld();
                std::printf("world: rebuilt - slot 0 '%s' (area %d, %s), slot 1 '%s' (area %d, %s); "
                            "walkable %zu tris, walls %zu tris\n",
                            worldSlots[0].stem.c_str(), worldSlots[0].area,
                            session.slotShown(0) ? "shown" : "hidden",
                            worldSlots[1].stem.c_str(), worldSlots[1].area,
                            session.slotShown(1) ? "shown" : "hidden",
                            playerSoup.size() / 9, playerSteep.size() / 9);
            }
            const WorldSlot& act = worldSlots[static_cast<std::size_t>(session.activeSlot() & 1)];
            const WorldSlot& oth = worldSlots[static_cast<std::size_t>(1 - (session.activeSlot() & 1))];
            worldSet = !act.stem.empty() ? act.stem : oth.stem;
            if (changed) bindSetEmittersNow();
        }

        std::fill(fb.px.begin(), fb.px.end(), std::uint16_t(0));
        const omk::WorldCamera* wc = session.camera();

        // THE DIALOGUE CAMERAS. A line uses two and travels between them:
        // `sub_4013B0` issues camera command 12 twice, the first with duration
        // -1.0 (a snap) and the second with 160.0, so the view cuts to the
        // node's first camera and moves to its second over 160 frames - 5.3 s
        // at 30, which is why the move stops well before a long line ends. The
        // menu has its own pair and its own cut.
        //
        // The clock is `DialogPlayer`'s and belongs to the LINE, not to any
        // animation - CLAUDE.md 5 has why that matters.
        omk::View dlgView;
        bool haveDlgCam = false;
        if (session.dialogOpen()) {
            const auto& dlg = session.dialogue();
            const omk::DialogCamera* ca = dlg.cameraA();
            const omk::DialogCamera* cb = dlg.cameraB();
            if (!ca) ca = cb;
            if (ca) {
                const omk::DialogCamera* cbb = cb ? cb : ca;
                const float u = dlg.cameraProgress();
                // ---- A DIALOGUE CAMERA'S POINTS CAN HANG OFF A SPEAKER ----
                //
                // `Camera_LoadParams` (0x004146C0): a point whose subject is
                // -1 is absolute and lands at +20/+32; any other subject makes
                // it an OFFSET at +124, and the block's subject resolver
                // (`sub_415A10` -> kind 2 is `sub_4151E0`) fills the anchor -
                // the actor NODE's world origin and its heading, `atan2` of
                // the node matrix's forward, +90 - and the solver places the
                // point at `anchor - R(heading) * offset`, the very sign
                // `resolveCamera` carries. `dialog_issue_camera` maps the
                // code to an actor: 0/1 the first speaker, 2/3 the second,
                // 6 both. The first is the player and the second the
                // conversation's speaker - read off the data rather than the
                // driver's write: every camera of DIALOG 39 is [2,2] and the
                // engine's own frame of it is a close-up of the hologram it
                // speaks with, and 401's camera 11 is [1,1].
                //
                // This viewer drew ONLY absolute dialogue cameras and marked
                // the rest `[relative - not drawn]`, so the transcan's advert
                // - the only camera the conversation has - fell through to
                // the follow camera and framed the room from across it. A
                // reader, with the original beside it: *transcan camera is
                // not good*.
                const auto anchorFor = [&](std::uint16_t code, float pos[3], float& yaw) -> bool {
                    if (code == 0xFFFF) return false;               // absolute: no anchor
                    const auto speakerBody = [&]() -> const Staged* {
                        const int sp = session.dialogue().conversation().speaker;
                        for (const auto& up : staged) if (up->actor == sp) return up.get();
                        return nullptr;
                    };
                    // THE CODE IS THE RESOLVER KIND, not merely which actor.
                    // `sub_415A10` switches on it - 0 -> `sub_414F30`,
                    // 1 -> `sub_415050`, 2 -> `sub_4151E0`, 3 -> `sub_415320` -
                    // and the four differ in WHERE on the actor they anchor:
                    //
                    //   0  the actor record's +244/+248/+252
                    //   1  the `Tete` node (actor+16), `Actor_LoadModel`'s cache
                    //   2  the body node's world origin
                    //   3  the head node's world origin
                    //
                    // while `dialog_issue_camera` picks the actor: 0/1 the
                    // first speaker, 2/3 the second, 6 both. Applying kind 2's
                    // body anchor to all of them put camera 11 of dialog 401 -
                    // `[1,1]`, on the player's HEAD - a foot in front of
                    // Kay'l's chest, and 176 of the 253 relative cameras the
                    // file ships are one of the two head kinds.
                    const auto playerBody = [&](float out[3], float& y) {
                        const float lift = player ? player->cameraLift() : 0.0f;
                        const float* pp = session.playerPos();
                        out[0] = pp[0]; out[1] = pp[1] - lift; out[2] = pp[2];
                        y = session.playerYaw();
                    };
                    const auto playerHead = [&](float out[3], float& y) {
                        if (!playerHeadKnown) { playerBody(out, y); return; }
                        for (int k = 0; k < 3; ++k) out[k] = playerHeadAt[k];
                        y = session.playerYaw();
                    };
                    const auto npcBody = [&](float out[3], float& y) -> bool {
                        const Staged* sb = speakerBody();
                        if (!sb) return false;
                        const float* at = sb->progRan ? sb->drawAt : sb->at;
                        for (int k = 0; k < 3; ++k) out[k] = at[k];
                        y = sb->drawnYawKnown ? sb->drawnYaw : sb->facing;
                        return true;
                    };
                    const auto npcHead = [&](float out[3], float& y) -> bool {
                        const Staged* sb = speakerBody();
                        if (!sb) return false;
                        y = sb->drawnYawKnown ? sb->drawnYaw : sb->facing;
                        if (!sb->headKnown) return npcBody(out, y);
                        for (int k = 0; k < 3; ++k) out[k] = sb->headAt[k];
                        return true;
                    };
                    switch (code) {
                        case 0: playerBody(pos, yaw); return true;
                        case 1: playerHead(pos, yaw); return true;
                        case 2: return npcBody(pos, yaw);
                        case 3: return npcHead(pos, yaw);
                        case 6: {                                   // the two-shot: both
                            float a[3], b[3], ya, yb;
                            playerBody(a, ya);
                            if (!npcBody(b, yb)) return false;
                            for (int k = 0; k < 3; ++k) pos[k] = 0.5f * (a[k] + b[k]);
                            yaw = ya;
                            return true;
                        }
                        default: return false;                      // detached: unresolvable
                    }
                };
                const auto placePoint = [&](const float off[3], std::uint16_t code, float out[3]) -> bool {
                    float p[3], yaw;
                    if (!anchorFor(code, p, yaw)) {
                        if (code != 0xFFFF) return false;
                        for (int k = 0; k < 3; ++k) out[k] = off[k];
                        return true;
                    }
                    const float t = yaw * 0.0174532925199433f;
                    const float cs = std::cos(t), sn = std::sin(t);
                    const float rx = off[0] * cs - off[2] * sn;
                    const float rz = off[0] * sn + off[2] * cs;
                    out[0] = p[0] - rx; out[1] = p[1] - off[1]; out[2] = p[2] - rz;
                    return true;
                };
                float ea[3], aa[3], eb[3], ab[3];
                const bool okA = placePoint(ca->eye, ca->subject[0], ea) &&
                                 placePoint(ca->at,  ca->subject[1], aa);
                const bool okB = placePoint(cbb->eye, cbb->subject[0], eb) &&
                                 placePoint(cbb->at,  cbb->subject[1], ab);
                for (int k = 0; k < 3; ++k) {
                    dlgView.cam.eye[k] = ea[k] + (eb[k] - ea[k]) * u;
                    dlgView.cam.at[k]  = aa[k] + (ab[k] - aa[k]) * u;
                }
                // THE FOV AND THE ROLL TRAVEL WITH THE POINTS. `sub_418410`
                // lerps four things across a move, not two:
                //
                //     out[52..60] = C[20..28]*u + prev[20..28]*(1-u)   // eye
                //     out[64..72] = C[32..40]*u + prev[32..40]*(1-u)   // target
                //     out[44]     = prev[11]*(1-u) + C[11]*u           // ROLL
                //     out[48]     = C[12]*u + prev[12]*(1-u)           // FOV
                //
                // and `Camera_LoadParams` (0x004146C0) settles which is which:
                // `+44` is the roll, WRAPPED to (-180, 180] as it is loaded
                // (`if (v14 > 180) v14 -= 360; if (v14 <= -180) v14 += 360`),
                // and `+48` the fov. `angle4096` already wraps on load here,
                // so a plain lerp of each is the engine's own arithmetic.
                //
                // This viewer SNAPPED the fov at the halfway point and dropped
                // the roll entirely. Six of 402's sixteen pairs change fov
                // across the move and one changes it by 15 degrees
                // (4572 -> 4574, 84 -> 99): a 15-degree widening in one frame
                // in the middle of a travel reads exactly as a reader
                // described it - *the camera suddenly returns to a previous
                // position while continuing the interpolation*. Eleven pairs
                // are rolled, up to 12 degrees, and were drawn upright.
                dlgView.cam.hfovDeg = ca->fov  + (cbb->fov  - ca->fov)  * u;
                dlgView.cam.rollDeg = ca->roll + (cbb->roll - ca->roll) * u;
                dlgView.cam.w = dispW; dlgView.cam.h = dispH;
                haveDlgCam = okA && okB;
                if (ca->id != lastDlgCam) {
                    lastDlgCam = ca->id;
                    std::printf("  dialogue camera %d -> %d (%s), fov %.0f, "
                                "roll %.0f%s\n", ca->id, cbb->id,
                                dlg.phase() == omk::DialogPhase::Menu
                                    ? "reply pair" : "line pair",
                                ca->fov, ca->roll,
                                haveDlgCam ? (ca->absolute() ? "" : "  [hangs off a speaker]")
                                           : "  [unresolvable subject - not drawn]");
                    // ...AND WHERE IT ENDED UP. A dialogue camera is resolved
                    // against the two speakers, so the id says nothing about
                    // the place: a reader reporting a camera inside the scenery
                    // had nothing in the log to name it by, while every world
                    // camera prints its eye and target two blocks below.
                    // Printed from the view the frame will be drawn with.
                    std::printf("    ...resolved: eye %.0f %.0f %.0f  at %.0f %.0f %.0f"
                                "  (%.0f from its target)\n",
                                double(dlgView.cam.eye[0]), double(dlgView.cam.eye[1]),
                                double(dlgView.cam.eye[2]), double(dlgView.cam.at[0]),
                                double(dlgView.cam.at[1]), double(dlgView.cam.at[2]),
                                std::sqrt(
                                    (dlgView.cam.eye[0] - dlgView.cam.at[0]) *
                                        (dlgView.cam.eye[0] - dlgView.cam.at[0]) +
                                    (dlgView.cam.eye[1] - dlgView.cam.at[1]) *
                                        (dlgView.cam.eye[1] - dlgView.cam.at[1]) +
                                    (dlgView.cam.eye[2] - dlgView.cam.at[2]) *
                                        (dlgView.cam.eye[2] - dlgView.cam.at[2])));
                }
            }
        }
        // Report the camera the script asked for, not the interpolated one -
        // mid-travel the position is still near the OUTGOING camera and
        // printing that beside the incoming id reads as a decode fault.
        if (const omk::WorldCamera* tc = session.cameraTarget()) {
            if (tc->id != lastCamera) {
                lastCamera = tc->id;
                std::printf("camera %d: eye %.0f %.0f %.0f  at %.0f %.0f %.0f  "
                            "fov %.1f roll %.1f  %s%s\n", tc->id,
                            tc->eye[0], tc->eye[1], tc->eye[2],
                            tc->at[0], tc->at[1], tc->at[2], tc->fov, tc->roll,
                            session.cameraTravel() > 0
                                ? "travelling" : "cut",
                            tc->absolute() ? ""
                              : session.playerPlaced()
                                  ? "  [relative - resolved against the player]"
                                  : "  [RELATIVE to an actor - not drawn]");
            }
        }
        // A camera whose eye or target is an OFFSET from an actor needs to
        // know where that actor IS, and until 2026-09-02 nothing here did -
        // so 1443 of the 5384 world cameras were skipped and the screen went
        // black. `actor.goto_address` now places the player (see
        // `formats/addresses.h`), so a relative camera resolves as long as he
        // has been placed. He still does not MOVE - the actor runtime is not
        // driven by this loop - so what this buys is a correct static camera,
        // not a following one.
        const bool haveRelCam = wc && !wc->absolute() && session.playerPlaced();
        // THE EDITING, if one is driving. `activeEditing` is the object whose
        // `Script_PlayScript` set the scene's active camera this frame, and
        // `editingCamera` is `Cam_PlayEditing` at its clock.
        const omk::SceneRunner::ActiveEditing* edit = session.scene().activeEditing();
        omk::CamSample editCam;
        const bool haveEdit = edit && session.scene().editingCamera(editCam);
        if (edit && edit->program != editingShown) {
            editingShown = edit->program;
            // The camera the travel starts FROM is whatever was on screen -
            // `Camera_Request` swaps the live block into `g_CameraPrev` and
            // the move interpolates away from it.
            editFromKnown = haveLastDrawn;
            for (int k = 0; k < 3; ++k) { editFromEye[k] = lastEye[k]; editFromAt[k] = lastAt[k]; }
            editFromFov = lastFov;
            editFromRoll = lastRoll;
            std::printf("frame %ld: editing %d '%s' takes the camera (mode 13): object %d '%s', "
                        "%u frames, travel %.0f%s\n",
                        n, edit->editing, edit->editingName.c_str(), edit->object,
                        edit->objectName.c_str(), edit->duration, edit->travel,
                        editFromKnown ? "" : " (nothing on screen to travel from: a cut)");
        } else if (!edit && editingShown >= 0) {
            editingShown = -1;
            // Mode 13 stays installed and the scene sets no active camera, so
            // the camera holds what it last drew. `holdEditCam` is cleared by
            // the next thing that requests one - a script's `camera.set`, a
            // conversation, or the hand-over to adventure mode, all of which
            // are a real `Camera_Request` in the engine.
            holdEditCam = haveLastDrawn;
            heldUnderRequests = session.cameraRequests();
            std::printf("frame %ld: editing over - the camera HOLDS its last frame "
                        "(mode 13, no active camera, autocameraplayer 0)%s\n", n,
                        holdEditCam ? "" : " - nothing drawn yet, so the world camera stands");
        }
        if (edit) holdEditCam = false;      // a new editing takes it back
        // ...and any other `Camera_Request` ends mode 13, because mode 13 is
        // what it replaces: `Camera_RequestChanged` (0x004147F0) tests the
        // MODE before anything else (`if (*mode != u32(C, 12)) return 1`), so
        // a `camera.set`'s mode 12 arriving under an editing always takes the
        // camera - even when it names the id that is already installed.
        //
        // This used to compare `session.cameraId()` against the id the hold
        // began under, which cannot see that case, and AREA 245's supermarket
        // fight is exactly it: the fight's aftermath asks for camera 0
        // ('Camera Player') three times over, the medical editing had already
        // been entered under camera 0 from the same script, so the id never
        // moved and the held last frame stayed on screen for the rest of the
        // game. Counting the REQUEST subsumes the id test - `cameraId()` is
        // `camTo_.id` and only `applyCamera` writes it - and it costs no new
        // special case (`todo/fight-mode.md` §13).
        if (holdEditCam && session.cameraRequests() != heldUnderRequests)
            holdEditCam = false;
        // ...and so are the other two the frontend issues itself: a
        // conversation is `Camera_Request(12)` (`Dialog_ApplyLineCameras`) and
        // the take is mode 1 out of `MDGETOBJ`. Either installs a mode that is
        // not 13, so the hold is over.
        if (holdEditCam && (haveDlgCam || takeCam)) holdEditCam = false;
        // ...AND SO IS ENTERING SHOOT MODE, which is the one this list was
        // missing. `Shoot_Enter` ends with `Camera_Request(4, ...)`, a real
        // request like any other, so mode 13's hold is over the moment the
        // mode begins. Without this the port installed the first-person
        // camera and then drew the cutscene's held last frame over the top of
        // it, which is what a reader saw at the supermarket: the mode on, the
        // pointer captured, and the view refusing to move
        // (`todo/omk-play.md` 97e).
        if (holdEditCam && shootMode) holdEditCam = false;
        // ...AND SO IS A FIGHT, for exactly the same reason and found the same
        // way. `fight.begin` ends with `Camera_Request(0Eh, ...)` - mode 14,
        // the fight camera (see the mode-14 block below) - so mode 13's hold
        // ends the moment the fight does.
        //
        // The symptom was NOT a frozen view this time, because the fight
        // camera writes `view.cam` itself: it was the LETTERBOX. The bars are
        // suppressed only when the player has control, and `holdEditCam` is
        // one of that test's terms, so a hold left standing from the approach
        // cutscene put 64-row black bars across every fight. A reader reported
        // them as "black stripes" and they were mis-attributed once to the
        // screen fade, which by then is mode 0 and draws nothing
        // (`todo/fight-mode.md` 15.4).
        if (holdEditCam && fightRun.active) holdEditCam = false;
        const bool anyWorld = !worldSlots[0].geo.corners.empty() ||
                              !worldSlots[1].geo.corners.empty();
        // ...and the same for DRAWING it: the screen composes over the
        // world afterwards, and every sneak page's background is an opaque
        // tile map, so nothing shows through that should not.
        //
        // THE PAUSE SCREEN IS THE EXCEPTION, and it was excluded here on an
        // assumption that nothing had tested, because nothing had ever
        // opened screen 31. Its panel 0x004E26C8 takes the FOURTH background
        // arm - `+76` is 0x40001800, so neither 0x2000 nor 0x4000, and
        // `+20`, the 80-tile array, is null - which is the arm that paints
        // nothing at all (`exetables.py: panels by background arm`). All it
        // draws is one fill item and three lines of text. So the world has
        // to be behind it, frozen: `uiPause` above already stops it
        // advancing, and stopping the DRAW as well left a pause menu on
        // black.
        //
        // **AND THE RULE IS THE SCREEN'S OWN FLAG** (2026-09-07), which
        // subsumes that: `UI_LoadScreen` (0x00429BB0) reads the screen
        // record's `+112` and, if bit `0x40000` is CLEAR, calls `sub_466B30`
        // - `byte_90E155 = 0`, so `Game_Frame` stops submitting the
        // full-screen 3D view through `sub_479C20`; `sub_46C290`, so the
        // sound bank is suspended; and the player's `+194` goes to
        // ACTOR_STATE 9. `Ui_CloseScreenDefault` calls the partner
        // `sub_466B60` to undo all three. Exactly THREE of the 37 screens
        // carry the bit - PAUSE GAME, SHOOT MECA and SHOOT HUMAN - the three
        // that must show the live world.
        //
        // Every other screen turns it off, and the sneak is one of them: the
        // sentence above ("every sneak page's background is an opaque tile
        // map") is not true of the shipped data. `sneak.bmp` is a SHEET the
        // page tiles 1:1, 31.8% of it is the colour key, and 21 of the 80
        // cells are more than 90% key - the middle of the device. That hole
        // is deliberate, and what belongs behind it is the device's own 3D
        // view, which the UI submits itself through `I2D_Submit3DView` (seven
        // call sites, all in the `Ui_*` range). Drawing the street there is
        // what a reader saw as *the sneak background is transparent*
        // (todo/omk-play.md 82).
        // ...AND A PANEL CAN TURN IT BACK ON. `Ui_DrawPanelDim` (0x00476290),
        // run every frame for the current panel, sets `byte_90E155 = 1` when
        // the panel carries bank B `0x800` - the ten shops, SAVE GAME, PAUSE
        // GAME, SHOOT HUMAN, HIGH-SCORE - and the composer then dims it. So
        // the world shows behind a shop even though its screen record says
        // hide, which is what a reader expected: "the background is not
        // supposed to be opaque". The sneak's panels do not carry the bit.
        const bool panelShowsWorld =
            walk && walk->panel() && (walk->panel()->flagsB & 0x800u) != 0;
        const bool screenKeepsWorld = !walk || openScreen < 0 ||
                                      w.worldBehind(openScreen) || panelShowsWorld;
        // ...UNLESS THE PANEL CARRIES A 3D VIEWPORT ITEM. `byte_90E155` only
        // guards `Game_Frame`'s full-screen submit; a UI item whose draw
        // callback is `sub_4782B0` submits the SAME world through the SAME
        // live camera (`I2D_Submit3DView(&rect, dword_93076C, flt_90E120, 0,
        // layer - 1)`) as a display-list node with its own rectangle, and
        // nothing gates it. The VIDEOPHONE's panel has one at (105, 85)
        // 500x280 on layer 6, and it is how the caller's face reaches the
        // device: the world is rendered into that rectangle - its own
        // viewport, its own aspect - and composed between the sheet and the
        // panel's items (`docs/UI.md` 3i, todo/omk-play.md 83).
        const omk::UiItem* vpItem = (walk && openScreen >= 0)
            ? omk::ScreenComposer::viewportItem(walk->panel() ? walk->panel()
                                                              : w.screen(openScreen))
            : nullptr;
        comp.attachView3D(nullptr);
        bool screenReadsPicture = false;
        if (walk && openScreen >= 0) {
            constexpr std::uint32_t kDrawInterference = 0x00477ED0u;   // `ui/interference.h`
            screenReadsPicture = openScreen == 30;                     // SAVE GAME
            if (const omk::UiPanel* rp = walk->panel() ? walk->panel() : w.screen(openScreen))
                for (const auto& l : rp->lists)
                    for (const auto& it : l.items)
                        if (it.drawFn == kDrawInterference) screenReadsPicture = true;
        }
        // THE PRESENT PASS (todo/optimization.md step 4b): set where the world
        // is placed into `fb`, read where the frame is presented.
        gpuFrame = false;
        overlayFrame = false, softGate = false;   // G6 step 2, the GLES window
        ovFade[0] = ovFade[1] = ovFade[2] = ovFade[3] = 0;   // the colour fade, for its shader
        g_ov.on = false;
        (void)softGate;
        gpuVy = 0, gpuVh = 0;
        gpuKeep = "no world";   // the first gate that kept the frame on the CPU path
        drawWorld = (screenKeepsWorld || vpItem) &&
                               worldReady && anyWorld &&
                               (haveDlgCam || haveEdit || holdEditCam ||
                                (wc && (wc->absolute() || haveRelCam)));
        if (drawWorld) {
            omk::View view = dlgView;
            // THE LETTERBOX. Camera mode - conversations and cutscenes - is
            // 1.818:1, which the dialogue captures measure and which a reader
            // confirmed does NOT belong to free roaming. Everything the
            // replica draws in 3D so far is camera mode: a scripted world
            // camera or a dialogue camera, never a player-controlled one. The
            // strip's height follows from the display's WIDTH, so no
            // resolution is baked in, and the bands are simply what the
            // framebuffer was cleared to.
            view.vw = dispW;
            view.vh = static_cast<int>(dispW / 1.8181818 + 0.5);
            // ...and adventure mode is NOT camera mode: a reader confirmed
            // the strip belongs to conversations and cutscenes, so the
            // walk is drawn full-frame.
            // (`|| uiPause`: a pause does not change the camera mode, so it
            // does not put bars on a walk either.)
            //
            // **AND IT IS THE PLAYER'S CONTROL THAT DECIDES, NOT THE SHAPE OF
            // THE CAMERA** (next-tasks 2, "black stripes entering/leaving a
            // building"). This also required `followCam` - the area's own
            // camera 0, relative to actor 0 - and a great many areas roam
            // under a FIXED camera instead: leaving Kay'l's flat runs
            // `player.anim.hold` / `fade.to_black` / `camera.set 4418` and
            // hands over to Hall 27, whose script leaves absolute camera 4353
            // installed. Measured on that walk: by frame 400 `adventure` is 1
            // and `animHeld` is 0 - the player has control - while `followCam`
            // is still 0, so the bars went on at frame 3 and never came off
            // again in 900 frames. That is the report.
            //
            // The captures say the same thing from the other side. Every
            // letterboxed one is a frame the player does NOT control -
            // `dlg402-32..41` at 64/64 rows, and `intro-75`, a CUTSCENE shot
            // with a scripted world camera, at 64/65 - so the strip is not
            // "a conversation" either, it is camera mode. Nothing establishes
            // it for a frame he does control, whatever camera is up.
            //
            // ...AND "HE HAS CONTROL" IS THE HOLD, not `adventure` alone.
            // A reader, on the end of the Telis lunch: *the stripes were not
            // displayed on the zoom on the talisman*. SCENE 53's beat holds
            // the player at pc 1090 and does not release him until 1220, so
            // the whole of it - the conversation, `object.show 27` and the
            // 4215/4216 zoom that follows - is camera mode; but a game
            // resumed from the LOAD PANEL sets `forceAdventure`, whose
            // `wantAdventure` asks only that he is placed with no dialogue
            // and no screen up. Between the conversation closing and the
            // release, that is true, and the bars came off over the zoom.
            //
            // `player.anim.hold` is the engine's own marker for it, and the
            // two traced `Screen_Fade` sites pair with exactly that:
            // `Screen_Fade(1)` with `Actor_HoldAnimation(player, 1)` on the
            // way into a slider travel and a fight, `Screen_Fade(0)` with
            // `Actor_HoldAnimation(player, 0)` on the way out.
            //
            // ...AND THE STRIP OUTLASTS THE RELEASE BY THE FADE. SCENE 53's
            // beat brackets itself
            //
            //     1089  fade.to_black       ; Screen_Fade(1) -> state 3
            //     1090  player.anim.hold
            //      ...  387, the sneak call, 388, the talisman zoom
            //     1220  player.anim.release
            //     1221  fade.from_black     ; Screen_Fade(0) -> state 4
            //
            // - the release comes BEFORE the fade, so a strip that ends with
            // the hold ends one frame early and vanishes instead of fading.
            // A reader: *there was fade to show the black stripes then they
            // suddenly disappeared*.
            //
            // **The test is `bandsDark`, not `running`**, and that is a
            // second report: *the stripes of the loading screen are not
            // removed when I can actually play*. Mode 3 HOLDS once its clock
            // is spent - armed for ever, drawing no bands at all - so a strip
            // keyed on the fade being armed never lifts after a load.
            // `bandsDark` asks whether it is darkening the bands THIS frame,
            // which is the engine's own two quads, `(h << 6) / 480` tall -
            // the 64 the captures measure.
            if ((adventure || uiPause) && !holdEditCam &&
                !session.playerAnimHeld() && !session.blackFade().bandsDark())
                view.vh = dispH;
            {
                // `OMK_LBLOG=1`: the letterbox decision and EVERY term of it,
                // printed when any of them changes. The bars are the report a
                // reader files as "black stripes", and the useful question is
                // never whether they are there but WHICH term is holding them
                // on - a stale `holdEditCam` and a running fade look identical
                // on screen (`todo/fight-mode.md` 15.4).
                static const bool lbLog = [] {
                    const char* e = std::getenv("OMK_LBLOG"); return e && *e == '1';
                }();
                static std::string told;
                if (lbLog) {
                    char buf[200];
                    std::snprintf(buf, sizeof buf,
                        "vh %d/%d adventure %d uiPause %d holdEditCam %d "
                        "animHeld %d bandsDark %d parked %d editing %d",
                        view.vh, dispH, adventure ? 1 : 0, uiPause ? 1 : 0,
                        holdEditCam ? 1 : 0, session.playerAnimHeld() ? 1 : 0,
                        session.blackFade().bandsDark() ? 1 : 0,
                        session.parkedOnProgram() ? 1 : 0,
                        session.scene().activeEditing() ? 1 : 0);
                    if (told != buf) { told = buf;
                        std::printf("frame %ld: letterbox %s\n", n, buf); }
                }
            }
            if (view.vh > dispH) view.vh = dispH;
            view.vx = 0;
            view.vy = (dispH - view.vh) / 2;
            if (vpItem) {
                // The viewport item's rectangle, scaled the way `I2D_ScaleX/Y`
                // scale it (`v * screen / 640`), and it replaces the
                // letterbox: `sub_45FA20` sets the D3D viewport to exactly
                // this rect and the vertical fov follows its aspect.
                view.vx = vpItem->x * dispW / 640;
                view.vy = vpItem->y * dispH / 480;
                view.vw = vpItem->w * dispW / 640;
                view.vh = vpItem->h * dispH / 480;
            }
            // THE FOG (todo/options-config.md step 4). Linear, over the range
            // the clip distance sizes - `sub_440BE0` writes `+328 = D * 0.25`
            // as the start and `+340 = D` as the end, so it ENDS at the clip
            // distance and not at the 0.95 bucket split. The colour is the
            // scene's `+336`, which `Scene_Load3DO`'s caller sets to ZERO and
            // nothing else in the decompilation writes: the shipped fog
            // DARKENS toward the horizon rather than hazing it, which is what
            // a domed city at night wants - and it is what hides the hard edge
            // at the clip distance. `--fog 0` turns it off, `--fog-colour
            // r,g,b` overrides it (the one mode that colours it uses
            // 40,80,64 with a 15 m clip - `docs/ASSETS.md`, "The fog").
            // With an unlimited clip there is no range to fade over, so the
            // fog is OFF rather than infinite - an infinite start would reach
            // the shader as a comparison against inf, a value no check could
            // read back and no driver need agree about.
            view.fog      = drawFog && !unlimitedClip;
            view.fogStart = unlimitedClip ? 0.0f : static_cast<float>(clipInches * 0.25);
            view.fogEnd   = unlimitedClip ? 0.0f : static_cast<float>(clipInches);
            for (int k = 0; k < 3; ++k) view.fogColour[k] = fogRGB[k];
            // CAMERA MODE 14 OUTRANKS THE EDITING, including its HOLD.
            //
            // `fight.begin` ends with `Camera_Request(0Eh, …)`, and a mode
            // request REPLACES the installed mode - so a fight supersedes the
            // mode-13 editing the scripted approach was using. This arm sat
            // after the editing arms until 2026-09-16 and a reader watched the
            // consequence: AREA 245's approach ends with "editing over - the
            // camera HOLDS its last frame", that hold then won every frame of
            // the fight, and the view never moved again. The fight camera was
            // being computed correctly and thrown away.
            if (!haveDlgCam && fightRun.active && fightRun.fight) {
                const omk::FightCamera& fc = fightRun.fight->camera();
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = fc.eye[k];
                    view.cam.at[k]  = fc.at[k];
                }
                view.cam.hfovDeg = 75.0f;       // row 14's own fov is 0
                view.cam.rollDeg = 0.0f;
                view.cam.w = dispW; view.cam.h = dispH;
                if (!fightCamTold) {
                    fightCamTold = true;
                    std::printf("frame %ld: the FIGHT CAMERA (mode 14) has the view - "
                                "state %d, options row 18 'Caméra de combat' = %d (%s)\n",
                                n, fc.state, settings.v.combatCamera,
                                settings.v.combatCamera ? "Vue de côté" : "Vue de dos");
                }
            } else if (!haveDlgCam && haveEdit) {
                // MODE 13: the editing's camera, at the object's own clock -
                // so the shot and the animation cannot drift apart, they are
                // one clock. The travel is the request's +24, `max(field, 0)`
                // frames, blended linearly from the camera last on screen the
                // way `sub_414A90` sets up the move; with no previous camera
                // (nothing drawn yet) it is a cut, which is what the engine
                // does with travel 0. ROLL is sampled and NOT applied -
                // `RCamera` carries none - and the fov is the editing's own
                // (Aapkayl's `sdb` opens at 37 and the Impasse's `intro` at
                // 90, so it is not a constant to leave alone).
                float u = 1.0f;
                if (editFromKnown && edit->travel > 0.0f)
                    u = std::min(1.0f, session.scene().editingClock() / edit->travel);
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = editFromKnown
                        ? editFromEye[k] + (editCam.eye[k] - editFromEye[k]) * u
                        : editCam.eye[k];
                    view.cam.at[k]  = editFromKnown
                        ? editFromAt[k] + (editCam.at[k] - editFromAt[k]) * u
                        : editCam.at[k];
                }
                const float efov = editCam.fov > 1.0f ? editCam.fov : 75.0f;
                view.cam.hfovDeg = editFromKnown ? editFromFov + (efov - editFromFov) * u : efov;
                // THE ROLL, blended on the SHORT ARC. An angle that wraps is
                // the class of error CLAUDE.md 1 keeps: +359 and 0 are the
                // same rotation standing still and a whole turn apart once
                // interpolated, and the title sequence span its camera
                // through them.
                view.cam.rollDeg = editFromKnown
                    ? editFromRoll + shortArc(editCam.roll - editFromRoll) * u
                    : editCam.roll;
                // A rolled shot is worth one line, once: the roll was DROPPED
                // by the renderer until 2026-09-03 and a still frame cannot
                // show it, so seeing the number is how a reader knows it is
                // being applied at all.
                if (std::fabs(view.cam.rollDeg) > 0.5f && !rollTold) {
                    rollTold = true;
                    std::printf("  camera ROLL %.1f degrees is being applied "
                                "(224 of the 1073 editing cameras carry one)\n",
                                static_cast<double>(view.cam.rollDeg));
                }
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && holdEditCam) {
                // MODE 13 WITH NO ACTIVE CAMERA: the block is not written, so
                // the last frame stands. Right after the editing branch,
                // because the engine's hold outranks everything the frontend
                // would otherwise pick - the follow camera included, since the
                // mode is still 13 and nothing has requested another.
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = lastEye[k];
                    view.cam.at[k]  = lastAt[k];
                }
                view.cam.hfovDeg = lastFov;
                view.cam.rollDeg = lastRoll;
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && !ride &&
                       session.sliders().calledVehicle() >= 0 &&
                       (session.sliders().callMachine().state == 2 ||
                        session.sliders().callMachine().state == 6 ||
                        (boarding && boardCam > 0) ||
                        (boarded && session.sliders().callMachine().state == 4))) {
                // ---- THE CAMERA THAT WATCHES IT COME ------------------
                //
                // `sub_456530` case 2 asks for **camera mode 8 on the
                // SLIDER** the moment a call is armed, and the only guard on
                // it is `if (sub_413360(C) != 8)` - which stops it RE-asking
                // when it is already there, not from asking at all. So the
                // camera cuts to the vehicle and follows it in every time,
                // which is what a reader described as seeing the slider on
                // its road; this port kept the follow camera on the player
                // and showed none of it.
                //
                // Same preset and same resolution as the ride's, because it
                // is the same mode - only the subject differs, and in both
                // cases the subject is the VEHICLE.
                float at[3];
                session.sliders().calledAt(at);
                const float t = session.sliders().calledYaw() * 0.0174532925199433f;
                const float cs = std::cos(t), sn = std::sin(t);
                const auto place = [&](const float off[3], float out[3]) {
                    const float rx = off[0] * cs - off[2] * sn;
                    const float rz = off[0] * sn + off[2] * cs;
                    out[0] = at[0] - rx;
                    out[1] = at[1] - off[1];
                    out[2] = at[2] - rz;
                };
                static constexpr float kComeEye[3] = {0.0f, 118.1102f, -275.5905f};
                static constexpr float kComeAt[3]  = {0.0f, 78.7402f, 0.0f};
                // ...and preset 9 for the BOARDING, which is what
                // `MDACTION`'s arm asks for over 60 frames
                // (`dword_930818 = 42700000h`). Its eye is 157.4803 - 4.00 m,
                // the same distance as the gate's reach - along the slider's
                // -X, which is the door side and the side the man had to be
                // standing on, and 59.0551 (1.50 m) up. Placed from the
                // matrix ROWS and not from `calledYaw`, because the door
                // geometry is what showed the pool's yaw and the actor's
                // euler to be mirror conventions.
                static constexpr float kBoardEye[3] = {157.4803f, 59.0551f, 0.0f};
                if (boarding && boardCam > 0) {
                    float bat[3], bx[3], bz[3];
                    if (session.sliders().calledFrame(bat, bx, bz)) {
                        for (int k = 0; k < 3; ++k) {
                            view.cam.eye[k] = bat[k] - kBoardEye[0] * bx[k]
                                                     - kBoardEye[2] * bz[k];
                            view.cam.at[k]  = bat[k];
                        }
                        view.cam.eye[1] -= kBoardEye[1];
                    }
                    boardCam -= frameSec * 30.0;   // by the delta (todo/sixty-fps.md 2)
                } else {
                    place(kComeEye, view.cam.eye);
                    place(kComeAt,  view.cam.at);
                }
                view.cam.hfovDeg = 75.0f;      // the preset's own fov
                view.cam.rollDeg = 0.0f;
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && ride) {
                // CAMERA MODE 8, the ride camera, and its subject is the
                // SLIDER and not the player: `camera_presets.json`'s row 8 is
                // eye (0, 118.1102, -275.5905), target (0, 78.7402, 0) with
                // `eyeSubject` and `targetSubject` both **5**, `f42` 0 (so
                // the target does not lag) and `f44`/`f46` 8. Those offsets
                // are exact metres - 3.00 up, 7.00 back and 2.00 up - which
                // is what says they were authored rather than tuned.
                //
                // Resolved the way every subject-relative camera is:
                // `out = subject - rotateYaw(offset)`, with `out[1] =
                // subject[1] - offset[1]` (`o3de/worldcam.cpp`).
                const float t = static_cast<float>(ride->yaw) * 0.0174532925199433f;
                const float cs = std::cos(t), sn = std::sin(t);
                const float sub[3] = {static_cast<float>(ride->x),
                                      static_cast<float>(ride->y),
                                      static_cast<float>(ride->z)};
                const auto place = [&](const float off[3], float out[3]) {
                    const float rx = off[0] * cs - off[2] * sn;
                    const float rz = off[0] * sn + off[2] * cs;
                    out[0] = sub[0] - rx;
                    out[1] = sub[1] - off[1];
                    out[2] = sub[2] - rz;
                };
                static constexpr float kRideEye[3] = {0.0f, 118.1102f, -275.5905f};
                static constexpr float kRideAt[3]  = {0.0f, 78.7402f, 0.0f};
                place(kRideEye, view.cam.eye);
                place(kRideAt,  view.cam.at);
                view.cam.hfovDeg = 75.0f;      // the preset's own fov
                view.cam.rollDeg = 0.0f;
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam && takeCam && player) {
                // THE TAKE CAMERA (omk-play 69): mode 1's preset resolved
                // against him every frame, travelled linearly over 30 frames
                // from the camera that was on screen at the request - the
                // same blend the editings use, `sub_414A90`'s setup being one
                // mechanism for both - then held; and mode 16 travels the
                // same 30 frames back to the follow camera and hands over.
                // Full-frame, not letterboxed: nothing read ties the strip
                // to this mode, and the walk it interrupts is full-frame.
                const int wcs = static_cast<int>(player->state());
                const bool swimCam = !waterCamPreset && wcs >= 11 && wcs <= 14;
                omk::FollowCamera tc =
                    swimCam ? player->resolveOffsetsYaw(takeCamEye, takeCamAt, takeCamFov)
                            : player->resolveOffsets(takeCamEye, takeCamAt, takeCamFov);
                // THE SWIM CAMERA STAYS INSIDE THE SET: a ray from what it looks
                // at to where it would stand, through the shown set's `shotSoup`
                // - the fight camera's own test (`sub_416570` -> `sub_444810`,
                // CollisionOnly skipped, 0x41 untested) - and the eye is brought
                // in to nine tenths of the way to the wall. A reader, 2026-09-17:
                // *"the camera does not respect collision and often goes outside
                // the environment"*. RECONSTRUCTION, labelled: the engine's swim
                // variant (`sub_413CD0`) sets camera flags 4 | 0x4800 and NOT the
                // land camera's flag 8, whose pass is the wall rule `sub_417070`;
                // what 0x4000 and 0x800 run is unread, so which rule keeps the
                // original's lens inside is not established - only that a canal
                // three metres wide cannot hold a camera three metres behind him.
                if (swimCam) {
                    const omk::TriangleSoup* shot = nullptr;
                    for (const auto& ws : worldSlots)
                        if (!ws.stem.empty() && ws.stem == worldSet) shot = &ws.shotSoup;
                    if (shot && !shot->empty()) {
                        const double p0[3] = {tc.at[0], tc.at[1], tc.at[2]};
                        const double d[3] = {double(tc.eye[0]) - tc.at[0], double(tc.eye[1]) - tc.at[1],
                                             double(tc.eye[2]) - tc.at[2]};
                        const auto h = omk::sweepSphere(*shot, p0, d, 0.0);
                        if (h && h->t < 1.0) {
                            const double t = h->t * 0.9;
                            for (int k = 0; k < 3; ++k)
                                tc.eye[k] = static_cast<float>(p0[k] + t * d[k]);
                            static long swimCamTold = -1000;
                            if (n - swimCamTold >= 60) {
                                swimCamTold = n;
                                std::printf("frame %ld: the swim camera - a wall at %.0f%% of the way to "
                                            "the eye; brought in to %.0f %.0f %.0f\n", n, h->t * 100.0,
                                            double(tc.eye[0]), double(tc.eye[1]), double(tc.eye[2]));
                            }
                        }
                    }
                }
                const omk::FollowCamera& fc = player->followCamera();
                const omk::FollowCamera& to = takeCamPhase == 3 ? fc : tc;
                float u = 1.0f;
                if (takeCamPhase == 1 || takeCamPhase == 3) {
                    takeCamClock += static_cast<float>(frameSec * 30.0);
                    u = haveLastDrawn ? std::min(1.0f, takeCamClock / takeCamTravel) : 1.0f;
                }
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = takeCamFromEye[k] + (to.eye[k] - takeCamFromEye[k]) * u;
                    view.cam.at[k]  = takeCamFromAt[k]  + (to.at[k]  - takeCamFromAt[k])  * u;
                }
                view.cam.hfovDeg = takeCamFromFov + (to.fov - takeCamFromFov) * u;
                view.cam.rollDeg = 0.0f;
                view.cam.w = dispW; view.cam.h = dispH;
                if (u >= 1.0f) {
                    if (takeCamPhase == 1) takeCamPhase = 2;
                    else if (takeCamPhase == 3) { takeCam = false; takeCamPhase = 0; }
                }
            } else if (!haveDlgCam && (adventure || uiPause) && followCam &&
                       player) {
                // The controller's follow camera: the world camera's offsets
                // resolved against HIS position and facing every frame, with
                // the engine's lag (player.h quotes sub_415D10/sub_415E60).
                //
                // `|| uiPause` because OPENING A SCREEN DOES NOT MOVE THE
                // CAMERA. Nothing in the pause screen's open callback touches
                // the camera mode, and `Game_Frame` renders with whatever is
                // installed, so the view behind the menu is the view that was
                // on screen. `adventure` alone is a per-frame mode that any
                // screen takes false, so pausing used to swap the follow
                // camera for the area's own camera 0 - the shot jumped the
                // moment the menu came up.
                // THE CAMERA THROUGH A WALL, measured: the one invariant
                // `sub_417070` exists to keep is that nothing solid lies
                // between the camera's target and its eye. Cast the segment
                // and say so.
                // THE OBSTRUCTION PASS, per frame and at full precision -
                // `sub_417070`'s `+208`, `+328` and the height it is pushing
                // the eye to. `todo/camera-obstruction.md` 5 is the
                // transcription this reports on; the staged probe below is a
                // different question (does a solid face lie in the segment)
                // and stays every tenth frame so its own check is unmoved.
                if (obstructProbe) {
                    const omk::FollowCamera& c = player->followCamera();
                    const float* p = player->pos();
                    // `+312 + +156` = -0.7 x the pelvis height, plus the
                    // subject's own y - the height a fully pinched eye rides.
                    const float lift = p[1] - 1.7f * player->cameraLift();
                    std::printf("obstruct %ld block %d kept %.4f eye %.4f %.4f %.4f "
                                "at %.4f %.4f %.4f lift %.4f\n",
                                n, player->cameraBlockState(),
                                double(player->cameraKeptDistance()),
                                double(c.eye[0]), double(c.eye[1]), double(c.eye[2]),
                                double(c.at[0]), double(c.at[1]), double(c.at[2]),
                                double(lift));
                }
                if (stagedProbe && (n % 10) == 0) {
                    const omk::FollowCamera& c = player->followCamera();
                    const double at[3] = {c.at[0], c.at[1], c.at[2]};
                    const double d[3]  = {c.eye[0] - c.at[0], c.eye[1] - c.at[1],
                                          c.eye[2] - c.at[2]};
                    bool through = false;
                    for (const omk::TriangleSoup* sp : {&playerSteep, &playerSoup})
                        if (!sp->empty())
                            if (const auto h = omk::sweepSphere(*sp, at, d, 1.0))
                                if (h->t < 0.98) through = true;
                    std::printf("    cam %ld eye %.0f %.0f %.0f  at %.0f %.0f %.0f  "
                                "dist %.0f  %s  (soups %zu steep / %zu walk tris)\n", n,
                                c.eye[0], c.eye[1], c.eye[2],
                                c.at[0], c.at[1], c.at[2],
                                std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]),
                                through ? "THROUGH a solid face" : "clear",
                                playerSteep.size() / 9, playerSoup.size() / 9);
                }
                // ---- MODE 4 HAS NO LAG, and the follow camera is all lag -
                //
                // `camera_presets.json` row 4's three smoothing divisors are
                // ZERO. `followCamera()` is `cam_`, the SMOOTHED one - the
                // mode-0 preset's 3/8/8 - so aiming through it drags the view
                // behind the mouse, which is what a reader described as
                // turning "but not correctly". `resolveOffsets` is the same
                // resolve with NO lag, and the header says exactly what it is
                // for: "what `Camera_Request(mode)` gives a preset whose
                // three smoothing divisors are 0". The TAKE camera (mode 1)
                // already uses it (`todo/omk-play.md` 69, 97i).
                omk::FollowCamera fc = player->followCamera();
                if (shootMode && shootCameraLive) {
                    const float rad = shootPitch * 3.14159265f / 180.0f;
                    // Y POINTS DOWN, so raising the eye is a NEGATIVE offset -
                    // the same sign `cameraLift` uses. 0 is the preset's own
                    // value and the faithful one; `--shoot-eye` departs from
                    // it deliberately.
                    // The lift: `--shoot-eye N` if given, otherwise
                    // `sub_414520` case 4's `0.7 * the model's extent`.
                    //
                    // MIND THE SIGN, because it is the opposite of what "Y
                    // grows down" suggests: `resolveOffsets` computes
                    // `eye = subject - R(yaw) * offset`, so it SUBTRACTS, and
                    // a POSITIVE y offset therefore raises the eye - the same
                    // way `camLift_` is subtracted from `pos` to lift the
                    // subject off the feet in the first place.
                    //
                    // Written as `-lift` this put the eye 29.8 BELOW the
                    // pelvis, which is 6.6 above the feet: ankle height, and
                    // exactly what a reader photographed when the view still
                    // looked low after the height itself was read correctly.
                    const float lift = shootEyeSet ? shootEyeLift : player->headLift();
                    const float eye[3] = {0.0f, lift, 0.0f};
                    const float at[3]  = {0.0f, lift + 787.4016f * std::sin(rad),
                                          787.4016f * std::cos(rad)};
                    fc = player->resolveOffsets(eye, at, 75.0f);
                }
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = fc.eye[k];
                    view.cam.at[k]  = fc.at[k];
                }
                view.cam.hfovDeg = fc.fov;
                view.cam.rollDeg = 0.0f;      // the follow camera carries none
                view.cam.w = dispW; view.cam.h = dispH;
            } else if (!haveDlgCam) {
                // A relative point is `subjectPos - R(yaw) * offset`, which is
                // what `sub_415D10`/`sub_415E60` do; an absolute one is passed
                // through. `resolveCamera` handles both per point, because the
                // engine decides per point and 959 of the 1443 relative
                // cameras are relative in ONE of their two.
                //
                // ...AGAINST THE SAME SUBJECT POINT THE FOLLOW CAMERA USES.
                // `session.playerPos()` is his FEET - the ground point the
                // walker keeps - and a relative camera's offset is measured
                // from the pelvis, which is why `resolveSteady` subtracts
                // `camLift_` (Y points down, so subtracting RAISES). The
                // follow path did that from issue 49 and this one did not, so
                // every scripted shot naming a subject sat a whole lift too
                // low - about 42 units for HO1_FNM, and visibly so on AREA
                // 222's tutorial shots 4290/4291/4292
                // (`todo/omk-play.md` 57).
                const float lift = player ? player->cameraLift() : 0.0f;
                // ...and the Session solves a TRAVEL's two ends at the same
                // point, so it has to know the lift too
                session.setCameraSubjectLift(lift);
                const float* pp0 = session.playerPos();
                const float subj[3] = {pp0[0], pp0[1] - lift, pp0[2]};
                const omk::ResolvedCamera rc = omk::resolveCamera(
                    *wc, subj, session.playerYaw());
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = rc.eye[k];
                    view.cam.at[k]  = rc.at[k];
                }
                view.cam.hfovDeg = wc->fov > 1.0f ? wc->fov : 75.0f;
                view.cam.rollDeg = wc->roll;   // already wrapped to (-180,180]
                view.cam.w = dispW; view.cam.h = dispH;
            }
            // AN INSTRUMENT OVERRIDE, and nothing the engine does: `--eye`
            // and `--at` (and `--fov`) replace whatever camera the frame
            // chose, so a shot can be framed on a body the game's own camera
            // is not looking at. `--scene` has taken the same three since it
            // was written; this makes them work with the Session running.
            if (haveEye && haveAt) {
                for (int k = 0; k < 3; ++k) {
                    view.cam.eye[k] = eyeA[k];
                    view.cam.at[k]  = atA[k];
                }
                if (fovA > 1.0f) view.cam.hfovDeg = fovA;
                view.cam.w = dispW; view.cam.h = dispH;
            }
            // What is on screen this frame, for the next editing to travel
            // from.
            for (int k = 0; k < 3; ++k) { lastEye[k] = view.cam.eye[k]; lastAt[k] = view.cam.at[k]; }
            // ...and the RESOLVED eye, which is the quantity a camera fault
            // is actually about. A frame counts lit pixels and cannot tell a
            // correct shot from a wrong one that happens to see the sky; the
            // eye's distance from the player can (`verify.py: camera travel`).
            if (omk::envSet("OMK_CAMEYE")) {
                if (session.dialogOpen()) {
                    const auto& dg = session.dialogue();
                    const omk::DialogCamera* qa = dg.cameraA();
                    const omk::DialogCamera* qb = dg.cameraB();
                    std::printf("  [dlgcam] frame %ld  pair %d -> %d  u %.3f  phase %d  node %d"
                                "  inForce %d  fov %.2f  roll %.2f\n", n,
                                qa ? qa->id : -1, qb ? qb->id : -1,
                                static_cast<double>(dg.cameraProgress()),
                                static_cast<int>(dg.phase()), dg.node(), haveDlgCam ? 1 : 0,
                                static_cast<double>(dlgView.cam.hfovDeg),
                                static_cast<double>(dlgView.cam.rollDeg));
                }
                std::printf("  [cameye] frame %ld  eye %.0f %.0f %.0f  at %.0f %.0f %.0f"
                            "  player %.0f %.0f %.0f\n", n,
                            view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                            view.cam.at[0], view.cam.at[1], view.cam.at[2],
                            session.playerPos()[0], session.playerPos()[1],
                            session.playerPos()[2]);
            }
            lastFov = view.cam.hfovDeg;
            lastRoll = view.cam.rollDeg;
            haveLastDrawn = true;
            // ---- THE TEXTURE POOL ------------------------------------
            //
            // The set's textures, then one section per staged MODEL, then the
            // player's, then the sprites'; a batch's slot is its material plus
            // its owner's base, which is the engine's own indexing (the bucket
            // key's low six bits, ASSETS 4b) and not a second mechanism.
            // Rebuilt on a COMPOSITION change rather than a size change: two
            // models with the same texture count swapping is exactly what a
            // size test cannot see.
            // ...AND IN A CONVERSATION. `Actor_EnterDialogueMode` puts the
            // player's channel on group 400, the dialogue stance, and he goes
            // on being drawn like any actor - the reverse shots of 402 frame
            // him across the room. This viewer drew him only in adventure
            // mode, so every cut to Kay'l during a conversation showed an
            // empty floor. A reader: *Kay'l is not visible when the camera
            // changes*. The controller ticks through the conversation, so
            // its pose is the stance.
            // ...and the THIRD time this mode test has been too narrow: a
            // PAUSE also takes `adventure` false, and the pause screen draws
            // over the live scene, so without `uiPause` the menu came up on
            // a street with the player deleted from it. Same shape as the
            // conversation above.
            // SHOOT MODE IS FIRST PERSON, so the player's own body is not
            // drawn - the reader's word, 2026-09-09, and the engine agrees:
            // `Shoot_Enter` (0x004222D0) calls `sub_436CE0` on the player's
            // node, which is `o3de_Traverse` setting flag bit 2 on every node
            // that does not carry 0x200000, and bit 2 is inside the
            // not-drawable mask 0x800043. `sub_436D20` is its exact inverse
            // (`& 0xFD`) and is what shows him again on the way out.
            //
            // Without this the camera - preset row 4, eye offset (0,0,0),
            // which is the PELVIS in all 181 character models - sits inside
            // his own mesh, and from some facings the whole view is the
            // inside of his back. That is what the first play-test render of
            // shoot mode showed, and it did not move when the player moved,
            // which is the tell for something drawn in camera space.
            //
            // NOT MODELLED, and labelled rather than dropped: the 0x200000
            // exemption, which leaves some nodes of the tree visible. The
            // port draws the player as one body with no per-node flags, so
            // it can only take him out whole. Whatever the exemption is for
            // in first person - the weapon in his hands is the obvious
            // candidate - is not reproduced here.
            const bool drawPlayer = playerReady && player &&
                                    !(session.shootMode().active() && shootCameraLive) &&
                                    (adventure || uiPause ||
                                     (session.dialogOpen() && !playerProgram));
            // ...AND THE EXEMPTION, ported 2026-09-10 (`todo/shoot-mode.md`
            // 8.0). `Shoot_Enter` hides the player's tree with `sub_436CE0`,
            // which sets the hidden bit on every node WITHOUT 0x200000 - and in
            // HO1_FN exactly three carry it: `UAvantg`, `UBrasg`, `UMaing`, the
            // LEFT forearm, upper arm and hand, the arm the gun hangs on. So in
            // first person the engine draws that arm and nothing else of him;
            // `Shoot_Leave` (`sub_436D20`) clears the bit. A reader's frames of
            // the original show it: the arm and the gun, low at the right.
            const bool drawArm = playerReady && player && !drawPlayer &&
                                 session.shootMode().active() && shootCameraLive;
            mark("world begin, set");
            // ---- THE WORLD'S PROPS -----------------------------------
            //
            // Every prop of the resident chunks whose DB state has bit 1 -
            // `object.show` sets it, `object.hide` clears it - drawn at the
            // placement `Area_Load` converted: position in inches, rotation
            // in degrees off a 4096-per-turn integer. `Object_SetPlacement`
            // gives the node `o3de_SetNodePos(pos)` and
            // `Matrix3x3_FromEulerAngles(rot)`, so a corner is `M * local +
            // pos` with M applied as a ROW vector, the convention
            // `rotateYaw` and `resolveCamera` already use.
            propGeo.corners.clear();
            propGeo.batches.clear();
            propGeo.cornerMesh.clear();
            propBatchOwner.clear();
            {
                const auto shown = session.props();
                for (const auto& pr : shown) {
                    // THE OBJECT IN HIS HAND (omk-play 69). `sub_41C490`, the
                    // MDGETOBJ hand-over, unlinks the prop's node from the
                    // world and re-links it under the actor's node at +44
                    // with its local transform zeroed - and +44 is
                    // `o3de_FindMeshByName(model, "Maing")` (04_sys.c 5506,
                    // a strstr: the model's `UMaing`), the LEFT hand. So from
                    // the grab until `sub_41C540` puts it back or the bank
                    // hides it, the object is drawn riding the left hand's
                    // composed pose, through the same model-to-world the
                    // player's own corners take. A reader described the
                    // original: "the camera movement shows the object in the
                    // hand of the player, when they have to confirm the grab
                    // or not" - which is what the mode-1 camera frames.
                    // `shown` is object-state bit 2, which the hold leaves
                    // set (the bank clears bit 0 later), so a held prop is
                    // still "shown" - the engine simply draws its node where
                    // the hierarchy now puts it, the hand, and so does this.
                    const bool held = player && heldInHand >= 0 && pr.id == heldInHand;
                    if (!pr.shown && !held) continue;
                    const auto& objs = voiceLib.objects();
                    if (pr.id < 0 || static_cast<std::size_t>(pr.id) >= objs.size()) continue;
                    const PropModel* pm = propModelFor(objs[static_cast<std::size_t>(pr.id)].stem);
                    if (!pm || !pm->ready) continue;
                    if (held) {
                        const omk::NodeTracks* pt = player->poseTracks();
                        const std::vector<omk::MeshPose> pose = pt
                            ? omk::composePose(playerMeshes, *pt, player->poseFrameF(), false)
                            : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
                        int hand = -1;      // the LAST strstr hit, as o3de_Traverse leaves it
                        for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                            if (std::strstr(playerMeshes[i].name, "Maing")) hand = static_cast<int>(i);
                        if (hand < 0 || static_cast<std::size_t>(hand) >= pose.size()) continue;
                        const omk::MeshPose& hp = pose[static_cast<std::size_t>(hand)];
                        const float* pp = player->pos();
                        const float yaw = player->facing();
                        // WHERE IN THE HAND - a RECONSTRUCTION, labelled. The
                        // node's origin is the wrist joint, and a 6 cm object
                        // (ANNEAU's extent is 2.6 units) placed there sits
                        // inside the hand mesh: measured, the drawn rings
                        // centred within 0.2 of the node and nobody could see
                        // them. What offset the engine gives a re-linked prop
                        // is not read (`sub_41C490` leaves the node's +36
                        // alone and `Anim_ApplyNodeFrame` skips a node whose
                        // header +12 is -1). Until it is, the object is
                        // carried at the hand mesh's own centre, which is the
                        // palm.
                        // THE ENGINE'S RULE, read 2026-09-05. The transform pass
                        // (`sub_4942A0`, 26_ole.c) composes a child as
                        // `world = parent.world + parent.worldMatrix * local`,
                        // `local` being the mesh record's +128..+136 - which
                        // `sub_41C490` leaves as the prop's file carries it
                        // (ANNEAU: -2.24, -0.17, -2.01, three units from the
                        // wrist toward the knuckles) - and the child's rotation
                        // is `matrix(+56) x facing(+156)` under the parent's.
                        // A prop's placement ROTATION lives in +156, not +56:
                        // `Object_SetPlacement` builds its Euler matrix into the
                        // slot record and points +156 at it, while +56 stays
                        // identity - and the grab's `sub_437140(node, 0)` clears
                        // +156. So a held object turns with the HAND ALONE. The
                        // release (`sub_41C540(actor, 0)`) resets +56, re-links
                        // the node under the scene root and restores the saved
                        // placement, position and angles both, which is what
                        // drawing a released prop from its record already does.
                        const float handOff[3] = {pm->localOff[0], pm->localOff[1], pm->localOff[2]};
                        const std::size_t base = propGeo.corners.size();
                        for (const auto& c : pm->rest.corners) {
                            omk::Corner w = c;
                            const float local[3] = {c.x - pm->origin[0] + handOff[0],
                                                    c.y - pm->origin[1] + handOff[1],
                                                    c.z - pm->origin[2] + handOff[2]};
                            float r[3];
                            omk::qrot(hp.q, local, r);              // the hand's rotation, alone
                            const float in[3] = {hp.pos[0] + r[0] - playerRootXZ[0], hp.pos[1] + r[1],
                                                 hp.pos[2] + r[2] - playerRootXZ[1]};
                            float o[3];
                            omk::rotateYaw(yaw, in, o);             // the player's model-to-world
                            w.x = o[0] + pp[0];
                            w.y = o[1] + pp[1] - playerFeet + lastRootDrop;
                            w.z = o[2] + pp[2];
                            propGeo.corners.push_back(w);
                        }
                        for (const auto& b : pm->rest.batches) {
                            omk::Batch nb = b;
                            nb.start += static_cast<int>(base);
                            propGeo.batches.push_back(nb);
                            propBatchOwner.push_back(pm);
                        }
                        static long heldToldFrame = -1000;
                        if (n - heldToldFrame >= 30) {
                            heldToldFrame = n;
                            double cx = 0, cy = 0, cz = 0; const std::size_t cnt = propGeo.corners.size() - base;
                            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                            for (std::size_t c = base; c < propGeo.corners.size(); ++c) {
                                const auto& w = propGeo.corners[c];
                                cx += w.x; cy += w.y; cz += w.z;
                                lo[0] = std::min(lo[0], w.x); hi[0] = std::max(hi[0], w.x);
                                lo[1] = std::min(lo[1], w.y); hi[1] = std::max(hi[1], w.y);
                                lo[2] = std::min(lo[2], w.z); hi[2] = std::max(hi[2], w.z);
                            }
                            if (cnt) { cx /= cnt; cy /= cnt; cz /= cnt; }
                            float hin[3] = {hp.pos[0] - playerRootXZ[0], hp.pos[1], hp.pos[2] - playerRootXZ[1]}, ho[3];
                            omk::rotateYaw(yaw, hin, ho);
                            // the FIST as drawn: the hand mesh's corners in playerPosed, world
                            float flo[3] = {1e9f, 1e9f, 1e9f}, fhi[3] = {-1e9f, -1e9f, -1e9f}; std::size_t fc = 0;
                            for (std::size_t c = 0; playerPosedFrame == n - 1 && c < playerPosed.corners.size(); ++c) {
                                if (c >= playerPosed.cornerMesh.size() || playerPosed.cornerMesh[c] != hand) continue;
                                const auto& w = playerPosed.corners[c]; ++fc;
                                flo[0] = std::min(flo[0], w.x); fhi[0] = std::max(fhi[0], w.x);
                                flo[1] = std::min(flo[1], w.y); fhi[1] = std::max(fhi[1], w.y);
                                flo[2] = std::min(flo[2], w.z); fhi[2] = std::max(fhi[2], w.z);
                            }
                            std::printf("held %d: hand node %.1f %.1f %.1f q(%.2f %.2f %.2f %.2f); object centre "
                                        "%.1f %.1f %.1f box [%.1f..%.1f %.1f..%.1f %.1f..%.1f] %zu corners %zu batches "
                                        "(mat %d texBase %zu); fist box [%.1f..%.1f %.1f..%.1f %.1f..%.1f] %zu corners "
                                        "(as drawn LAST frame; 0 when the renderer posed him); player %.1f %.1f %.1f yaw %.0f; camera eye %.0f %.0f %.0f\n",
                                        pr.id, ho[0] + pp[0], ho[1] + pp[1] - playerFeet + lastRootDrop, ho[2] + pp[2],
                                        hp.q.w, hp.q.x, hp.q.y, hp.q.z,
                                        cx, cy, cz, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], cnt,
                                        pm->rest.batches.size(),
                                        pm->rest.batches.empty() ? -1 : pm->rest.batches[0].material, pm->texBase,
                                        flo[0], fhi[0], flo[1], fhi[1], flo[2], fhi[2], fc,
                                        pp[0], pp[1], pp[2], yaw, lastEye[0], lastEye[1], lastEye[2]);
                        }
                        continue;
                    }
                    const double rx = pr.rotDeg[0] * 0.0174532925199433;
                    const double ry = pr.rotDeg[1] * 0.0174532925199433;
                    const double rz = pr.rotDeg[2] * 0.0174532925199433;
                    const double cx = std::cos(rx), sx = std::sin(rx);
                    const double cy = std::cos(ry), sy = std::sin(ry);
                    const double cz = std::cos(rz), sz = std::sin(rz);
                    // `Matrix3x3_FromEulerAngles` (0x00441EB0, CLEAN in readable/),
                    // TRANSCRIBED term for term - `Object_SetPlacement` builds a
                    // prop's facing matrix with it, the transform pass composes
                    // it under the scene root, and the vertex pass applies it
                    // row-vector (`v . M`, as `Matrix3x3_RotateVector`). The
                    // block this replaces claimed to be that function and was
                    // its INVERSE: numerically it equals the engine's with all
                    // three angles negated, so every prop on the floor was
                    // turned the wrong way - unnoticed on symmetric props until
                    // the rings came back from a correctly turned hand
                    // (2026-09-05).
                    const double m00 = cz * cy,                 m01 = -(sz * cy),               m02 = sy;
                    const double m10 = sy * sx * cz + sz * cx,  m11 = cz * cx - sx * sz * sy,   m12 = -(sx * cy);
                    const double m20 = sz * sx - sy * cz * cx,  m21 = cx * sz * sy + sx * cz,   m22 = cy * cx;
                    const std::size_t base = propGeo.corners.size();
                    for (const auto& c : pm->rest.corners) {
                        omk::Corner w = c;
                        // relative to the model's own root, then placed
                        const double lx = c.x - pm->origin[0];
                        const double ly = c.y - pm->origin[1];
                        const double lz = c.z - pm->origin[2];
                        w.x = static_cast<float>(lx * m00 + ly * m10 + lz * m20 + pr.pos[0]);
                        w.y = static_cast<float>(lx * m01 + ly * m11 + lz * m21 + pr.pos[1]);
                        w.z = static_cast<float>(lx * m02 + ly * m12 + lz * m22 + pr.pos[2]);
                        propGeo.corners.push_back(w);
                    }
                    for (const auto& b : pm->rest.batches) {
                        omk::Batch nb = b;
                        nb.start += static_cast<int>(base);
                        propGeo.batches.push_back(nb);
                        propBatchOwner.push_back(pm);
                    }
                    if (propsTold.insert(pr.id).second)
                        std::printf("prop %d SHOWN at %.1f %.1f %.1f rot %.1f %.1f %.1f\n",
                                    pr.id, static_cast<double>(pr.pos[0]),
                                    static_cast<double>(pr.pos[1]), static_cast<double>(pr.pos[2]),
                                    static_cast<double>(pr.rotDeg[0]),
                                    static_cast<double>(pr.rotDeg[1]),
                                    static_cast<double>(pr.rotDeg[2]));
                }
            }
            // THE PROPS' GEOMETRY CHANGES, AND THE GPU MUST HEAR OF IT. The Vulkan
            // backend keys a vertex buffer on the Geometry's pointer and
            // `revision` and returns the cached buffer while they match - and
            // `propGeo`'s revision was never bumped, so the props uploaded on the
            // first frame were drawn for ever: the rings stayed on the floor and
            // never appeared in the hand however right the CPU-side placement
            // was (the log showed it exactly on the hand node for three days of
            // reports). Every other per-frame geometry here bumps its revision;
            // this one does now, whenever a prop is held or the set of shown
            // props changes size.
            // ...and on EVERY rebuild, not "while held or when the count changes":
            // that rule missed the first frame after a release, so the GPU kept
            // the last held frame's buffer - the rings standing where the hand
            // let them go - while the CPU had them back on the floor (a reader's
            // before/after screenshots, 2026-09-05). A few hundred corners a
            // frame is nothing.
            // ---- THE GUN IN HIS HAND (`todo/shoot-mode.md` 8.0) -----------
            //
            // `Shoot_Enter`'s event 48 hands the weapon object to `sub_41C490`,
            // which links its node under actor +44 - `Maing` - with the local
            // transform cleared, exactly as a take does; `Object_Load` has
            // already unlinked `tir` (the bolt) and set 0x200000 on the node,
            // so the first-person hide spares it. Drawn here the way the held
            // prop is ("THE OBJECT IN HIS HAND"), without `tir`'s corners.
            if (session.shootMode().active() && player && !shotGunStem.empty()) {
                const GunFacts& gf = gunFactsFor(shotGunStem);
                PropModel* pm = propModelFor(shotGunStem);
                const omk::NodeTracks* pt = player->poseTracks();
                if (pm && pm->ready && pt &&
                    pm->rest.cornerMesh.size() == pm->rest.corners.size()) {
                    const std::vector<omk::MeshPose> pose =
                        playerPoseNow(&*player, player->state() == omk::ActorState::Shoot,
                                      *pt, player->poseFrameF());
                    int hand = -1;      // the LAST strstr hit, as o3de_Traverse leaves it
                    for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                        if (std::strstr(playerMeshes[i].name, "Maing")) hand = static_cast<int>(i);
                    if (hand >= 0 && static_cast<std::size_t>(hand) < pose.size()) {
                        const omk::MeshPose& hp = pose[static_cast<std::size_t>(hand)];
                        const float* pp = player->pos();
                        const float yaw = player->facing();
                        for (const auto& b : pm->rest.batches) {
                            const std::size_t base = propGeo.corners.size();
                            for (std::size_t c = b.start; c < b.start + b.count; ++c) {
                                if (gf.ok && pm->rest.cornerMesh[c] == gf.tirMesh) continue;
                                omk::Corner w = pm->rest.corners[c];
                                const float local[3] = {w.x - pm->origin[0] + pm->localOff[0],
                                                        w.y - pm->origin[1] + pm->localOff[1],
                                                        w.z - pm->origin[2] + pm->localOff[2]};
                                float r[3];
                                omk::qrot(hp.q, local, r);
                                const float in[3] = {hp.pos[0] + r[0] - playerRootXZ[0],
                                                     hp.pos[1] + r[1],
                                                     hp.pos[2] + r[2] - playerRootXZ[1]};
                                float o[3];
                                omk::rotateYaw(yaw, in, o);
                                w.x = o[0] + pp[0];
                                w.y = o[1] + pp[1] - playerFeet + lastRootDrop;
                                w.z = o[2] + pp[2];
                                propGeo.corners.push_back(w);
                            }
                            const std::size_t cnt = propGeo.corners.size() - base;
                            if (!cnt) continue;
                            omk::Batch nb = b;
                            nb.start = base;
                            nb.count = cnt;
                            propGeo.batches.push_back(nb);
                            propBatchOwner.push_back(pm);
                        }
                    }
                }
            }
            // ---- EACH GUNMAN'S GUN (a reader, 2026-09-11: *"they don't have
            // any weapons in their hands"*). The same `sub_41C490` link as the
            // player's: the object in HIS hand (actor +164, his held slot), its
            // node under his `Maing` (actor +44), `tir` left out. Placed from
            // his hand as it was DRAWN last frame (`meshAt` / `meshRot`, which
            // the staged pass below fills) - so it lags his arm by one frame.
            if (session.shootMode().active()) {
                const auto& objs = voiceLib.objects();
                for (const auto& up : staged) {
                    if (!up || up->actor < 0 || !up->mo || !up->drawn) continue;
                    if (session.shootAction(up->actor) < 0) continue;
                    const int hs = session.heldSlotOf(up->actor);
                    const int obj = hs >= 0 ? session.objectSlotId(hs) : -1;
                    if (obj < 0 || static_cast<std::size_t>(obj) >= objs.size()) continue;
                    const std::string stem = objs[static_cast<std::size_t>(obj)].stem;
                    if (stem.empty()) continue;
                    const std::size_t nm = up->mo->meshes.size();
                    if (up->meshAt.size() != nm * 3 || up->meshRot.size() != nm * 9) continue;
                    int hand = -1;      // the LAST strstr hit
                    for (std::size_t i = 0; i < nm; ++i)
                        if (std::strstr(up->mo->meshes[i].name, "Maing")) hand = static_cast<int>(i);
                    if (hand < 0) continue;
                    const GunFacts& gf = gunFactsFor(stem);
                    PropModel* pm = propModelFor(stem);
                    if (!pm || !pm->ready || pm->rest.cornerMesh.size() != pm->rest.corners.size())
                        continue;
                    const std::size_t h = static_cast<std::size_t>(hand);
                    const float* hp = &up->meshAt[h * 3];
                    const float* hm = &up->meshRot[h * 9];   // column-major: local axes in the world
                    for (const auto& b : pm->rest.batches) {
                        const std::size_t base = propGeo.corners.size();
                        for (std::size_t c = b.start; c < b.start + b.count; ++c) {
                            if (gf.ok && pm->rest.cornerMesh[c] == gf.tirMesh) continue;
                            omk::Corner w = pm->rest.corners[c];
                            const float l[3] = {w.x - pm->origin[0] + pm->localOff[0],
                                                w.y - pm->origin[1] + pm->localOff[1],
                                                w.z - pm->origin[2] + pm->localOff[2]};
                            w.x = hp[0] + hm[0] * l[0] + hm[3] * l[1] + hm[6] * l[2];
                            w.y = hp[1] + hm[1] * l[0] + hm[4] * l[1] + hm[7] * l[2];
                            w.z = hp[2] + hm[2] * l[0] + hm[5] * l[1] + hm[8] * l[2];
                            propGeo.corners.push_back(w);
                        }
                        const std::size_t cnt = propGeo.corners.size() - base;
                        if (!cnt) continue;
                        omk::Batch nb = b;
                        nb.start = base;
                        nb.count = cnt;
                        propGeo.batches.push_back(nb);
                        propBatchOwner.push_back(pm);
                    }
                    if (gunDrawnTold.insert(up->actor).second)
                        std::printf("frame %ld: actor %d %s - HIS GUN drawn: object %d '%s' on his "
                                    "Maing (mesh %d), tir left out\n", n, up->actor,
                                    up->model.c_str(), obj, stem.c_str(), hand);
                }
            }
            // ---- THE BOLTS (`actor/projectile.h`) ------------------------
            //
            // Each live entry is a clone of the held gun's `tir` node, drawn
            // in the node's own matrix at its position and with its three
            // scales - so the Waver's streak grows along its length for its
            // first eight frames (shot sprite +20/+28). `tir` is flagged
            // 0x3000, the ADDITIVE bucket, and the batch carries that from the
            // model; its textures come through the gun's own pool section.
            if (projectiles.live() && !shotGunStem.empty()) {
                const GunFacts& gf = gunFactsFor(shotGunStem);
                PropModel* pm = gf.ok ? propModelFor(shotGunStem) : nullptr;
                if (pm && pm->ready && pm->rest.cornerMesh.size() == pm->rest.corners.size()) {
                    for (const auto& e : projectiles.entries()) {
                        if (!e.node) continue;
                        for (const auto& b : pm->rest.batches) {
                            const std::size_t base = propGeo.corners.size();
                            for (std::size_t c = b.start; c < b.start + b.count; ++c) {
                                if (pm->rest.cornerMesh[c] != gf.tirMesh) continue;
                                omk::Corner w = pm->rest.corners[c];
                                const float local[3] = {(w.x - gf.tirPos[0]) * e.scale[0],
                                                        (w.y - gf.tirPos[1]) * e.scale[1],
                                                        (w.z - gf.tirPos[2]) * e.scale[2]};
                                float r[3];
                                omk::shootRotateRow(local, e.rot, r);
                                w.x = e.pos[0] + r[0];
                                w.y = e.pos[1] + r[1];
                                w.z = e.pos[2] + r[2];
                                propGeo.corners.push_back(w);
                            }
                            const std::size_t cnt = propGeo.corners.size() - base;
                            if (!cnt) continue;
                            omk::Batch nb = b;
                            nb.start = base;
                            nb.count = cnt;
                            propGeo.batches.push_back(nb);
                            propBatchOwner.push_back(pm);
                        }
                    }
                }
            }
            propGeo.revision = ++worldGeoRev;
            refreshSprites();
            const bool wantSprites = (session.scene().effects().count() || !ctlSprites.empty() ||
                                      !foeSprites.empty()) &&
                                     !spriteTab.empty();
            // Which sprite ids the resident scene can name. Computed BEFORE the
            // rebuild test and compared, because a scene that starts asking for
            // an id it was not asking for before needs a slot for it - a
            // composition counter cannot see that.
            spriteWanted.clear();
            for (const auto& e : session.scene().sfx().effects)
                spriteWanted.insert(static_cast<int>(e.sprite));
            for (const auto& pa : session.scene().effects().particles())
                spriteWanted.insert(pa.sprite);
            for (const auto& c : ctlSprites) spriteWanted.insert(c.sprite);
            // ...AND THE MELEE OPPONENT'S. His records were spawned and placed
            // on his bones from 15.10 on - 482 placements in one run - and
            // never drew: a batch whose sprite has no POOL SLOT is skipped
            // ("a sprite with no texture: not drawn"), and only the player's
            // ids were pooled. A reader: *"no visual effect when I touched the
            // ennemy"* (`todo/fight-mode.md` 15.16).
            for (const auto& c : foeSprites) spriteWanted.insert(c.sprite);
            if (poolBuiltFor != poolComposition || poolHasSprites != wantSprites ||
                poolHasPlayer != (drawPlayer || drawArm) || spritePooled != spriteWanted) {
                pool = worldTex;
                for (auto& cm : charModels) {
                    cm.second.texBase = pool.size();
                    pool.insert(pool.end(), cm.second.tex.begin(), cm.second.tex.end());
                }
                // ...then each PROP model's, so a prop batch's slot is its
                // material plus its own base, the same rule every other
                // section follows.
                for (auto& pm : propModels) {
                    pm.second.texBase = pool.size();
                    pool.insert(pool.end(), pm.second.tex.begin(), pm.second.tex.end());
                }
                // the sky's one texture, on the same rule as every other
                // section: a batch's slot is its material plus its own base
                sky.texBase = pool.size();
                pool.insert(pool.end(), sky.tex.begin(), sky.tex.end());
                // THE SHADOW's one texture, on the same rule. It is resident
                // for the whole run rather than per set, because the engine
                // loads the model once at game start and never frees it.
                shadowTexBase = pool.size();
                pool.insert(pool.end(), shadowModel.tex.begin(), shadowModel.tex.end());
                playerTexBase = pool.size();
                if (drawPlayer || drawArm) pool.insert(pool.end(), playerTex.begin(), playerTex.end());
                spriteTexBase = pool.size();
                // THE SPRITES GO IN DENSELY, and that is the whole point.
                // `spriteTex` is indexed BY SPRITE ID, because an effect names
                // its sprite by id (`sub_4A5800`) - so 24 decoded sprites
                // spread over ids 0..137 make a 138-entry array of which 114
                // are EMPTY. Inserting it whole put the pool at 154 slots
                // against the **64** a bucket key's low six bits can address,
                // and every slot above 63 wrapped onto another texture: a
                // particle drawing at the right size, in the right blend, with
                // the wrong picture. That is the shape a reader reported as
                // "visible but does not render correctly", and only some of
                // them wrong, because which wrap depends on the id mod 64.
                //
                // So only the sprites that HAVE a texture go in, and
                // `spriteSlot` maps an id to its place. The id-keyed arrays
                // stay as they are - `particleGeometry` needs them for the
                // frame walk and the quad extent.
                //
                // ...and only the ones this SCENE can ask for. Every decoded
                // sprite used to go in - 24 of them, the global library's 20
                // plus the scene's - and with ANEKBAH's 20 set textures, a
                // second resident set, the staged characters and the player
                // ahead of them the pool ran past **64**, which is all a
                // bucket key's low six bits can address (`slot & 0x3F`). Past
                // that a sprite aliases onto another slot and draws someone
                // else's picture, which is a street light and a fire both
                // coming out as smoke: `EFFECTS2_GLOW` and `EFFECTS2_SMOKE1`
                // are different sprites sharing one atlas, so an aliased slot
                // lands on the neighbour and looks exactly like it.
                //
                // A scene asks for very few: Anekbah's twenty effects name
                // THREE sprites (49589 smoke, 49590 glow, 49591 explo). So
                // take the ids its own `.sfx` names, plus any a live particle
                // is already carrying, and pool those alone.
                spriteSlot.clear();
                if (wantSprites)
                    for (int id : spriteWanted) {
                        if (id < 0 || static_cast<std::size_t>(id) >= spriteTab.idCount) continue;
                        const omk::Texture* st = spriteTab.texOf(id);
                        if (!st || st->rgb.empty()) continue;   // an id nothing decoded
                        spriteSlot[id] = static_cast<int>(pool.size() - spriteTexBase);
                        pool.push_back(*st);
                    }
                if (pool.size() > 64)
                    std::printf("WARNING: texture pool is %zu, past the 64 a bucket key "
                                "can address - slots will alias\n", pool.size());
                poolSize = pool.size();
                poolBuiltFor = poolComposition;
                poolHasSprites = wantSprites;
                poolHasPlayer = drawPlayer || drawArm;   // the first-person arm needs them too
                spritePooled = spriteWanted;
                world.setTextures(pool);
                // The sprite section comes and goes with the effects, several
                // times a second; only a change of CAST is worth a line.
                if (poolBuiltFor != poolTold) {
                    poolTold = poolBuiltFor;
                    std::printf("frame %ld: texture pool - %zu set + %zu character (%zu "
                                "models) + %zu player + %zu sprite = %zu slots\n", n,
                                worldTex.size(), playerTexBase - worldTex.size(),
                                charModels.size(), spriteTexBase - playerTexBase,
                                pool.size() - spriteTexBase, pool.size());
                }
                if (pool.size() > 64 && !poolOverflowTold) {
                    poolOverflowTold = true;
                    std::printf("  ...which is over the 64 a bucket key's low six bits can "
                                "address (ASSETS 4b): every slot above 63 WRAPS\n");
                }
            }

            mark("props, guns");
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
            const bool sideCullSet = !noSideCull && !mirrorShown && view.cam.hfovDeg < 179.0f &&
                                     view.cam.w > 0 && view.cam.h > 0;
            const bool sideCullBodies = sideCullSet && !(drawShadows && shadowQuality >= 2);
            omk::Frustum sideFr{};
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
            // -> true when the sphere is wholly outside one side plane
            const auto outsideView = [&](const float c[3], float r, bool bodies) {
                if (!(bodies ? sideCullBodies : sideCullSet)) return false;
                for (const auto& pl : sideFr.side)
                    if (pl.n[0] * c[0] + pl.n[1] * c[1] + pl.n[2] * c[2] + pl.d > r) return true;
                return false;
            };
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
                            const omk::TriangleSoup* soup = nullptr;
                            for (const auto& ws : worldSlots)
                                if (!ws.stem.empty() && ws.stem == worldSet) soup = &ws.sightSoup;
                            double hitT = -1.0;
                            if (soup) {
                                const double p0[3] = {from[0], from[1], from[2]};
                                const double d[3] = {double(to[0]) - from[0], double(to[1]) - from[1],
                                                     double(to[2]) - from[2]};
                                if (const auto h = omk::sweepSphere(*soup, p0, d, 0.0)) hitT = h->t;
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
                                                !soup ? "no set to cast over"
                                                : ein.rayHits ? "HITS the set" : "CLEAR",
                                                (spectreArm ? cone : wideCone) ? "inside his"
                                                                               : "outside his");
                                else
                                std::printf("frame %ld: actor %d %s - RAY (sub_4449E0) at %.1f, inside "
                                            "half his inner range %.1f: %s\n", n, s.actor,
                                            s.model.c_str(), double(ao.dist3d),
                                            double(rec.rangeInner) * 0.5,
                                            !soup ? "no set to cast over"
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
                    // a scene clip stages him, because the engine's
                    // cancellation never fires (`g_MorphRootTrack` is only
                    // ever -2). See the note this replaces.
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
                    // line's root translation into the node - `g_MorphRootTrack`
                    // is -2, so the `f32(node + 28) = frameTranslation` write
                    // matches no track - and instead rotates that translation
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
                    omk::composePose(s.mo->meshes, s.idle, 0, false, pose);
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
                        const float world[3] = {playerHeadKnown ? playerHeadAt[0] : pp[0],
                                                playerHeadKnown ? playerHeadAt[1] : pp[1] - 60.0f,
                                                playerHeadKnown ? playerHeadAt[2] : pp[2]};
                        float pelvis[3] = {0, 0, 0};
                        if (static_cast<std::size_t>(s.mo->root) < pose.size())
                            for (int k = 0; k < 3; ++k) pelvis[k] = pose[static_cast<std::size_t>(s.mo->root)].pos[k];
                        const float rel[3] = {world[0] - s.at[0], world[1] - s.at[1], world[2] - s.at[2]};
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
                        const bool progTurned = s.sceneTracks.valid() && !useLine &&
                                                s.progYawKnown;
                        const float bodyYaw = progTurned ? progYawSign * s.progYaw : s.facing;
                        omk::rotateYaw(-bodyYaw, rel, local);
                        const float target[3] = {local[0] + pelvis[0], local[1] + pelvis[1], local[2] + pelvis[2]};
                        omk::aimHead(pose, s.mo->meshes, head, target, s.look,
                                     static_cast<float>(frameSec * 30.0), s.lookSnap);
                        s.lookSnap = false;
                    }
                } else {
                    s.lookSnap = true;
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
                    std::printf("frame %ld: staged bodies - %zu staged, %d skinned and drawn, "
                                "%d beyond the clip distance and %d outside the view (not "
                                "skinned), %d posed by the renderer\n",
                                n, staged.size(), stagedDrawn, stagedFar, stagedOff, stagedGpu);
                }
            }
            mark("staged bodies");
            // ---- THE PEDESTRIANS ---------------------------------------
            pedDrawn = pedLive = pedInAction = pedIdle = pedOffView = 0;
            pedLit = 0;
            vehDrawn = vehLive = vehStopped = 0;
            {
                const auto& pd = session.sliders();
                const auto& rs = session.residentSlot(session.activeSlot());
                // `Area_TickLoad` case 7 loads `ANIMS\<+124>.ANI` for the area
                // whether or not it has a slider circuit - the crowd is only
                // one of its consumers, and a shoot-mode character with no
                // `.CTL` is another. So this no longer waits on `pd.loaded()`.
                if (!rs.ani.empty() && rs.ani != pedAniName) {
                    pedAniName = rs.ani;
                    pedAni = fs.read("ANIMS/" + rs.ani + ".ANI");
                    pedTracks.clear();
                    pedLodTracks.clear();
                    ++pedCacheGen;
                }
                const auto& ws = pd.movers();
                if (pedStaged.size() != ws.size()) {
                    pedStaged.clear();
                    for (std::size_t i = 0; i < ws.size(); ++i) pedStaged.push_back(std::make_unique<PedStaged>());
                    pedTracks.clear();
                    pedLodTracks.clear();
                    ++pedCacheGen;
                }
                const float reach = omk::kLodDistances[3];
                // the view axis, for the LOD's depth (`sub_48D7F0`'s is the
                // camera matrix's third row: the depth along it)
                float viewFwd[3] = {view.cam.at[0] - view.cam.eye[0], view.cam.at[1] - view.cam.eye[1],
                                    view.cam.at[2] - view.cam.eye[2]};
                {
                    const float l = std::sqrt(viewFwd[0] * viewFwd[0] + viewFwd[1] * viewFwd[1] +
                                              viewFwd[2] * viewFwd[2]);
                    if (l > 0.0f) for (float& c : viewFwd) c /= l;
                }
                // `OMK_NO_PED_LOD=1`: every walker on its largest skeleton, as
                // before - for laying the two side by side
                static const bool noPedLod = std::getenv("OMK_NO_PED_LOD") != nullptr;
                // `OMK_PED_LOD_MAX=n`: no level past n - at 0 the skeleton mask
                // is on and every walker on level 0, which must draw exactly
                // what `OMK_NO_PED_LOD` draws
                static const int pedLodMax = std::getenv("OMK_PED_LOD_MAX") ? std::atoi(std::getenv("OMK_PED_LOD_MAX")) : 3;
                pedFootOffMax = 0.0f;
                pedJobs.clear();
                const double pedSerial0 = phaseNow();
                for (std::size_t i = 0; i < ws.size(); ++i) {
                    const auto& w = ws[i];
                    PedStaged& p = *pedStaged[i];
                    p.drawn = false;
                    if (!w.live || !w.clip || pedAni.empty()) continue;
                    ++pedLive;
                    if (w.flags & 0x80u) ++pedInAction;
                    if (w.flags & 0x100u) ++pedIdle;
                    const float dx = w.body[0] - view.cam.eye[0], dy = w.body[1] - view.cam.eye[1],
                                dz = w.body[2] - view.cam.eye[2];
                    if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
                    if (!p.mo) p.mo = charModelFor(w.model);
                    if (!p.mo || !p.mo->ready) continue;
                    // outside the view: `sub_48D7F0` skips the instance whole
                    // on its model root's radius at its position
                    if (p.mo->root >= 0 && static_cast<std::size_t>(p.mo->root) < p.mo->meshes.size() &&
                        outsideView(w.body, p.mo->meshes[static_cast<std::size_t>(p.mo->root)].radius, true)) {
                        ++pedOffView;
                        continue;
                    }
                    if (w.clip != p.clipWas) {
                        p.clipWas = w.clip;
                        p.tracks = pedTracksFor(w.sex, *w.clip, p.mo->meshes);
                    }
                    if (p.cacheGen != pedCacheGen || p.cacheMo != p.mo || p.cacheTracks != p.tracks ||
                        p.cacheModel != w.model) {
                        p.cacheGen = pedCacheGen;
                        p.cacheMo = p.mo;
                        p.cacheTracks = p.tracks;
                        p.cacheModel = w.model;
                        p.cacheRoot = p.tracks ? skeletonRootOf(*p.mo, *p.tracks) : p.mo->root;
                        p.cacheRest = &lodRestFor(w.model, *p.mo, p.cacheRoot);
                        p.lodFilled = 0;
                        for (int f = 0; f < 2; ++f) {
                            const char* bone = f == 0 ? "Piedg" : "Piedd";
                            p.cacheFoot[f] = p.mo->boneIdx.built() ? p.mo->boneIdx.find(bone, p.cacheRoot)
                                                                   : omk::findMeshContaining(p.mo->meshes, bone, p.cacheRoot);
                        }
                    }
                    // THE LEVEL, `sub_48D7F0`'s walk of the chain: start one
                    // level down when row 7's detail is 0, then step on while
                    // the VIEW DEPTH is at or past the level's distance; the
                    // last level holds past 40 m. Only for a model whose
                    // chain the index rule holds for, and whose tracks were
                    // bound on the chain's first (largest) skeleton.
                    PedJob job;
                    job.i = i;
                    job.tracks = p.tracks;
                    job.rest = p.cacheRest;
                    job.lodRoot = p.cacheRoot;
                    job.foot[0] = p.cacheFoot[0]; job.foot[1] = p.cacheFoot[1];
                    const int nL = noPedLod ? 0 : lodChainOf(*p.mo);
                    if (nL > 1 && p.tracks && p.cacheRoot == p.mo->lodRootAt[0]) {
                        const float depth = (w.body[0] - view.cam.eye[0]) * viewFwd[0] +
                                            (w.body[1] - view.cam.eye[1]) * viewFwd[1] +
                                            (w.body[2] - view.cam.eye[2]) * viewFwd[2];
                        int level = std::min(shadowDetail <= 0 ? 1 : 0, nL - 1);
                        while (level + 1 < nL && depth >= omk::kLodDistances[level]) ++level;
                        level = std::max(0, std::min(level, pedLodMax));
                        job.level = level;
                        job.only = p.mo->lodMask[level].data();
                        if (level > 0) {
                            if (!(p.lodFilled & (1u << level))) {
                                p.lodFilled |= static_cast<std::uint8_t>(1u << level);
                                const int rk = p.mo->lodRootAt[level];
                                p.lodRestL[level] = &lodRestFor(w.model, *p.mo, rk);
                                for (int f = 0; f < 2; ++f) {
                                    const char* bone = f == 0 ? "Piedg" : "Piedd";
                                    p.lodFoot[level][f] = p.mo->boneIdx.built() ? p.mo->boneIdx.find(bone, rk)
                                                        : omk::findMeshContaining(p.mo->meshes, bone, rk);
                                }
                            }
                            job.tracks = lodTracksFor(p.tracks, level, p.mo->lodCount);
                            job.rest = p.lodRestL[level];
                            job.lodRoot = p.mo->lodRootAt[level];
                            job.foot[0] = p.lodFoot[level][0]; job.foot[1] = p.lodFoot[level][1];
                        }
                    }
                    // `floor(clock) - 1` as before, with the clock's FRACTION
                    // kept for enhancement 12: `composePose` floors it when
                    // smoothing is off, so the pose is the same key
                    float frame = static_cast<float>(w.clock) - 1.0f;
                    if (frame < 0.0f) frame = 0.0f;
                    if (p.tracks && frame >= static_cast<float>(p.tracks->frames - 1))
                        frame = static_cast<float>(p.tracks->frames - 1);
                    job.frame = frame;
                    pedJobs.push_back(job);
                }
                phSpan["ped serial"] += phaseNow() - pedSerial0;
                // ---- THE BODIES, which may run on several cores -----------
                //
                // The pass above is the SERIAL half and it is serial for a
                // reason: it resolves the shared caches (`charModelFor` loads
                // a model, `pedTracksFor` binds a clip's tracks,
                // `lodRestFor` cuts and keeps a rest geometry) and counts the
                // crowd. Everything below writes only its OWN walker's
                // `PedStaged` and reads the rest, so the chunks are disjoint
                // and `omk::Threads`' contract holds: no ordering, no
                // reduction, bit-identical whatever the split
                // (`platform/threads.h`; todo/vita-port.md P4).
                //
                // What is deliberately NOT in here: the geometry REVISION and
                // the counters, which are assigned in index order after the
                // pass so that a threaded frame numbers its buffers exactly as
                // a serial one does.
                const bool gpuPoseOn = !cpuBodiesFlag && world.posesBodies();
                const int gpuMaxLights = world.maxVertexLights();
                {
                    static bool told = false;
                    if (!told) {
                        told = true;
                        std::printf("bodies: the walkers are posed and lit %s\n",
                                    gpuPoseOn ? "by the RENDERER (one affine a mesh; "
                                                "todo/gpu-skinning.md)"
                                              : "on the CPU");
                    }
                }
                const auto pedBodies = [&](std::size_t from, std::size_t to) {
                    for (std::size_t k = from; k < to; ++k) {
                        PedJob& j = pedJobs[k];
                        const auto& w = ws[j.i];
                        PedStaged& p = *pedStaged[j.i];
                        const omk::Geometry& rest = *j.rest;
                        const float frame = j.frame;
                        // The per-body times are the JOB's own, summed in index
                        // order after the pass: `spanned` writes a shared map and
                        // this body may be running on another thread.
                        const double tc0 = phaseNow();
                        std::vector<omk::MeshPose>& pose = p.pose;
                        if (j.tracks) omk::composePose(p.mo->meshes, *j.tracks, frame, false, pose, j.only);
                        else omk::composePose(p.mo->meshes, omk::NodeTracks{}, 0, false, pose, j.only);
                        const double tc1 = phaseNow();
                        // THE GPU PATH (todo/gpu-skinning.md step 3): where the
                        // renderer poses bodies and the lights that reach this
                        // one fit its program, nothing below touches a corner -
                        // the body is its rest, one affine a mesh and a list of
                        // lights. The per-pixel lighting enhancement stays on
                        // the CPU path, whose corners it needs.
                        p.gpu = false;
                        p.lightCount = 0;
                        p.restGeo = &rest;
                        if (gpuPoseOn) {
                            p.lights.clear();
                            int reachN = 0;
                            const bool lit = lightCrowd && lighting == 0;
                            if (lit) {
                                const float at[3] = {w.body[0], w.body[1], w.body[2]};
                                for (const WorldSlot& ws2 : worldSlots)
                                    if (!ws2.lights.empty())
                                        reachN += omk::lightReach(at, ws2.lights, p.lights);
                            }
                            if (lighting == 0 && reachN <= gpuMaxLights) {
                                p.gpu = true;
                                p.lightCount = reachN;
                                p.lightsBlack = lit;
                            }
                        }
                        if (!p.gpu) omk::applyPose(p.posed, rest, p.mo->meshes, pose);
                        j.tCompose += tc1 - tc0;
                        j.tApply += phaseNow() - tc1;
                        // THE HEIGHT is the engine's rule, `sub_437F80(inst, x, body.y
                        // + footY - radius, z)`: the model origin stands one root
                        // radius (41.9 for PSH_FN - the pelvis-to-feet height) above
                        // the body point and the root track's summed y moves it.
                        // Written here as the REST pose's feet on the body point plus
                        // that summed y, which is the same constant for a model
                        // whose radius is its height - and NOT the feet of the clip's
                        // first frame, which for the seated clip are the folded legs
                        // at pelvis level and sank every sitter into the street (a
                        // reader's frame, 2026-09-03). The sit's root drops 20.7 over
                        // its enter clip; that is what puts him on the ground.
                        // ...per LOD level: each skeleton is its own rest
                        if (j.level == 0 ? !p.feetKnown : !p.lodFeetKnown[j.level]) {
                            const auto restPose = omk::composePose(p.mo->meshes, omk::NodeTracks{}, 0, false);
                            omk::Geometry restPosed;
                            omk::applyPose(restPosed, rest, p.mo->meshes, restPose);
                            float feet = -1e9f;
                            for (const auto& c : restPosed.corners) if (c.y > feet) feet = c.y;
                            if (j.level == 0) { p.feetLevel0 = feet; p.feetKnown = true; }
                            else { p.lodFeet[j.level] = feet; p.lodFeetKnown[j.level] = true; }
                        }
                        p.feet = j.level == 0 ? p.feetLevel0 : p.lodFeet[j.level];
                        // the pelvis of the skeleton DRAWN: the four are
                        // authored side by side (PSH_FN's at x -12.6, -91.2,
                        // -170.2, -248.9), so level 0's would stand a lower
                        // level ~79 units off per step
                        float rootXZ[2] = {0.0f, 0.0f};
                        const int drawRoot = j.level == 0 ? p.mo->root : j.lodRoot;
                        if (drawRoot >= 0 && static_cast<std::size_t>(drawRoot) < p.mo->meshes.size()) {
                            rootXZ[0] = p.mo->meshes[static_cast<std::size_t>(drawRoot)].pos[0];
                            rootXZ[1] = p.mo->meshes[static_cast<std::size_t>(drawRoot)].pos[2];
                        }
                        // one `cos`/`sin` for the whole body instead of two a
                        // corner - the same bits, since the angle does not change
                        float wcs, wsn;
                        omk::yawSinCos(w.facing, wcs, wsn);
                        const double pedPlace0 = phaseNow();   // the job's own
                        if (p.gpu) {
                            // the placement below, as a 3x4 folded into each
                            // mesh's affine: x' = cs (x - rx) - sn (z - rz) + bx,
                            // z' = sn (x - rx) + cs (z - rz) + bz, y' = y + lift
                            const float place[12] = {
                                wcs, 0.0f, -wsn, w.body[0] - (wcs * rootXZ[0] - wsn * rootXZ[1]),
                                0.0f, 1.0f, 0.0f, w.body[1] + w.footY - p.feet,
                                wsn, 0.0f, wcs, w.body[2] - (wsn * rootXZ[0] + wcs * rootXZ[1])};
                            omk::meshAffines(p.mo->meshes, pose, place, p.affine);
                        }
                        if (!p.gpu) for (auto& c : p.posed.corners) {
                            const float in[3] = {c.x - rootXZ[0], c.y, c.z - rootXZ[1]};
                            float r[3];
                            omk::rotateYawCS(wcs, wsn, in, r);
                            c.x = r[0] + w.body[0];
                            c.y = r[1] + w.body[1] + w.footY - p.feet;
                            c.z = r[2] + w.body[2];
                            // the normal turns with the walker and does not move
                            const float n[3] = {c.nx, c.ny, c.nz};
                            float rn[3];
                            omk::rotateYawCS(wcs, wsn, n, rn);
                            c.nx = rn[0]; c.ny = rn[1]; c.nz = rn[2];
                        }
                        j.tPlace += phaseNow() - pedPlace0;
                        // `Slider_PlaceShadow`'s two nodes, on the same transform.
                        p.footKnown = false;
                        {
                            // the serial pass's cache, for this model and root
                            const int fi[2] = {j.foot[0], j.foot[1]};
                            if (fi[0] >= 0 && fi[1] >= 0 &&
                                static_cast<std::size_t>(fi[0]) < pose.size() &&
                                static_cast<std::size_t>(fi[1]) < pose.size()) {
                                for (int f = 0; f < 2; ++f) {
                                    const auto& mp = pose[static_cast<std::size_t>(fi[f])].pos;
                                    const float in[3] = {mp[0] - rootXZ[0], mp[1], mp[2] - rootXZ[1]};
                                    float r[3];
                                    omk::rotateYawCS(wcs, wsn, in, r);
                                    p.footAt[f][0] = r[0] + w.body[0];
                                    p.footAt[f][1] = r[1] + w.body[1] + w.footY - p.feet;
                                    p.footAt[f][2] = r[2] + w.body[2];
                                }
                                p.footKnown = true;
                                // THE INVARIANT THAT WOULD HAVE CAUGHT THE ORPHANS.
                                // A walker's feet are within a stride of his own
                                // body point; a bone taken off the wrong LOD
                                // skeleton is 240 units away. Measured every frame
                                // and reported, because "a shadow with no origin"
                                // is only visible to a person and this is not.
                                const float mx = (p.footAt[0][0] + p.footAt[1][0]) * 0.5f - w.body[0];
                                const float mz = (p.footAt[0][2] + p.footAt[1][2]) * 0.5f - w.body[2];
                                const float off = std::sqrt(mx * mx + mz * mz);
                                if (off > j.footOff) j.footOff = off;
                            }
                        }
                        // THE DYNAMIC LIGHTS (`o3de/vertexlight.h`). `sub_4380B0`
                        // registers a walker in the same structure the set's
                        // lights went into and `sub_48D7F0` applies every one that
                        // reaches it, per frame, before submitting. Both resident
                        // slots contribute, because during a transition two sets
                        // are drawn and the engine's structure holds both.
                        // ...unless the GPU is going to do it per pixel, in which
                        // case the corners keep their black base and `Draw::lit`
                        // carries the decision to the shader.
                        const double pedLight0 = phaseNow();
                        if (p.gpu) j.lit += p.lightCount;
                        if (!p.gpu && lightCrowd && lighting == 0) {
                            // THE BASE IS BLACK, and that is the part that had to
                            // be read rather than assumed. A lit instance does not
                            // start from the model's baked vertex colour: the lit
                            // path `sub_494E80` writes `instance[+416]` into every
                            // runtime vertex's colour, and every site that sets
                            // +416 sets it to 0. The crowd models ship pure white
                            // (all 446 of PSH_FN's vertices are 255,255,255), so
                            // there is no baked light in them to keep - the .3DO
                            // lights ARE their lighting, and adding to white is
                            // what made this port's first attempt change exactly
                            // zero pixels.
                            for (auto& c : p.posed.corners) { c.r = 0.0f; c.g = 0.0f; c.b = 0.0f; }
                            const float at[3] = {w.body[0], w.body[1], w.body[2]};
                            for (const WorldSlot& ws2 : worldSlots)
                                if (!ws2.lights.empty())
                                    j.lit += omk::applyLights(p.posed, 0, p.posed.corners.size(),
                                                                  at, ws2.lights);
                        }
                        j.tLight += phaseNow() - pedLight0;
                    }
                };
                {
                    const double pedAll0 = phaseNow();
                    if (threadBodies && pedJobs.size() > 1)
                        omk::Threads::shared().parallelFor(0, pedJobs.size(), 1, pedBodies);
                    else
                        pedBodies(0, pedJobs.size());
                    phSpan["ped bodies (wall)"] += phaseNow() - pedAll0;
                }
                for (PedJob& j : pedJobs) {
                    PedStaged& p = *pedStaged[j.i];
                    p.posed.revision = ++worldGeoRev;
                    p.drawn = true;
                    ++pedDrawn;
                    pedLit += j.lit;
                    if (j.footOff > pedFootOffMax) pedFootOffMax = j.footOff;
                    phSpan["ped compose"] += j.tCompose;
                    phSpan["ped apply"] += j.tApply;
                    phSpan["ped place"] += j.tPlace;
                    phSpan["ped light"] += j.tLight;
                }

                // ...and on a headless run's LAST frame, which is the one its
                // `--dump` shows: since the side planes (optimization step 28 j)
                // a frame-0 count can be all `outside the view` while the frame
                // a check compares has walkers on it
                if (pedLive && (pedTold < 0 || n - pedTold >= 300 ||
                                (frames > 0 && n + 1 == static_cast<long>(frames)))) {
                    pedTold = n;
                    int pedGpu = 0;
                    for (const auto& up : pedStaged) pedGpu += up->drawn && up->gpu;
                    std::printf("frame %ld: pedestrians - %d live, %d drawn within %.0f of the eye "
                                "(%d outside the view), %d at an action point, %d idling, %d light "
                                "hits, %d posed by the renderer\n",
                                n, pedLive, pedDrawn, reach, pedOffView, pedInAction, pedIdle, pedLit, pedGpu);
                }
                // ...and the ROAD TRAFFIC on the same circuit's vehicle lanes.
                const auto& vs = pd.vehicles();
                if (vehStaged.size() != vs.size()) {
                    vehStaged.clear();
                    for (std::size_t i = 0; i < vs.size(); ++i) vehStaged.push_back(std::make_unique<VehStaged>());
                }
                // `dword_4C8860`, the VEHICLE LOD distances - 20/30/40/50 m
                // where the crowd's are 10/20/30/40, so a slider is still
                // drawn a good way past the last walker.
                const float vreach = omk::kVehLodDistances[3];
                const double veh0 = phaseNow();
                for (std::size_t i = 0; i < vs.size(); ++i) {
                    const auto& v = vs[i];
                    VehStaged& sv = *vehStaged[i];
                    sv.drawn = false;
                    // ...and SAY why the player's own slider is not staged, once
                    // per reason: after a load into another city it vanished
                    // from the picture with the log silent about it.
                    const bool mine = static_cast<int>(i) == pd.calledVehicle();
                    static int calledSkipTold = -1;
                    auto skipMine = [&](int why, const char* what) {
                        if (mine && calledSkipTold != why) {
                            calledSkipTold = why;
                            std::printf("frame %ld: the called vehicle (slot %zu, model '%s') is NOT staged: %s\n",
                                        n, i, v.model.c_str(), what);
                        }
                    };
                    if (!v.live || v.mover < 0) { skipMine(1, "not live / no mover"); continue; }
                    const auto& m = pd.movers()[static_cast<std::size_t>(v.mover)];
                    ++vehLive;
                    if (m.flags & 0x100u) ++vehStopped;
                    const float vx = m.body[0] - view.cam.eye[0], vy = m.body[1] - view.cam.eye[1],
                                vz = m.body[2] - view.cam.eye[2];
                    if (vx * vx + vy * vy + vz * vz > vreach * vreach) { skipMine(2, "beyond the vehicle LOD reach"); continue; }
                    if (!sv.mo) sv.mo = charModelFor(v.model);
                    if (!sv.mo || !sv.mo->ready) { skipMine(3, "its model did not load"); continue; }
                    if (mine && calledSkipTold != 0) { calledSkipTold = 0; std::printf("frame %ld: the called vehicle (slot %zu, model '%s') is staged at %.0f %.0f %.0f\n", n, i, v.model.c_str(), m.body[0], m.body[1], m.body[2]); }
                    // ---- `sub_4521E0`, THE MODEL SWAP ----------------------
                    // The slider model TABLE (`dword_538E28`, 88-byte rows)
                    // holds its four root sub-objects at +4..+16 sorted
                    // heaviest first by `sub_453A70` (vertices + faces):
                    // SlBassin 1527 corners, slider_fl 750, SlBasA 366, SlBasB
                    // 144. The reserved slider is created on +8 (slider_fl -
                    // `CharModel::root` here, a shell with no interior and NO
                    // DOOR: both door meshes hang off SlBassin) and
                    // `sub_4521E0` toggles the sub-node to +4, SlBassin, the
                    // COCKPIT, at `MDACTION`'s snap - which is what puts a
                    // door under the clip - and `sub_4570F0` toggles it back
                    // before the exit clip. A reader saw the shell: *"the
                    // current model when Kay'l enters the slider has no
                    // modelised interior"*.
                    const bool swapped = (boarding || boarded) && static_cast<int>(i) == pd.calledVehicle();
                    const int wantRoot = swapped ? heaviestRootOf(*sv.mo)
                                                 : (v.lodBase == 0 ? heaviestRootOf(*sv.mo) : sv.mo->root);
                    if (sv.built && sv.lodRoot != wantRoot) sv.built = false;
                    if (!sv.built) {
                        sv.lodRoot = wantRoot;
                        if (static_cast<int>(i) == pd.calledVehicle() &&
                            wantRoot >= 0 && static_cast<std::size_t>(wantRoot) < sv.mo->meshes.size())
                            std::printf("slider: the called vehicle is staged on sub-object "
                                        "'%s'%s\n", sv.mo->meshes[static_cast<std::size_t>(wantRoot)].name,
                                        swapped ? " - the COCKPIT, sub_4521E0's swap" : "");
                        const omk::Geometry& rest = lodRestFor(v.model, *sv.mo, sv.lodRoot);
                        const auto pose = omk::composePose(sv.mo->meshes, omk::NodeTracks{}, 0, false);
                        omk::applyPose(sv.atRest, rest, sv.mo->meshes, pose);
                        // The sub-objects of one model are laid out APART in
                        // model space - SLI_FN's four roots sit at x 351..550 -
                        // so each is re-centred on its own root, which is what
                        // makes the four LOD variants land in one place. The
                        // walkers do the same for x and z; a vehicle takes y
                        // too, because it has no feet rule to stand on.
                        if (sv.lodRoot >= 0 && static_cast<std::size_t>(sv.lodRoot) < sv.mo->meshes.size())
                            for (int k = 0; k < 3; ++k)
                                sv.origin[k] = sv.mo->meshes[static_cast<std::size_t>(sv.lodRoot)].pos[k];
                        sv.radius = 0.0f;
                        for (const auto& c : sv.atRest.corners) {
                            const float dx = c.x - sv.origin[0], dy = c.y - sv.origin[1], dz = c.z - sv.origin[2];
                            sv.radius = std::max(sv.radius, std::sqrt(dx * dx + dy * dy + dz * dz));
                        }
                        sv.built = true;
                    }
                    // OUTSIDE THE VIEW: `sub_48D7F0` rejects the instance whole
                    // before any matrix or vertex; the radius here is the
                    // composed model's own, never smaller than the node's
                    {
                        const float at[3] = {m.body[0], m.body[1] - omk::kVehNodeLift, m.body[2]};
                        if (!mine && outsideView(at, sv.radius, true)) continue;
                    }
                    // ...unless this is the slider he is CLIMBING INTO, in
                    // which case its door is posed from the clip at the
                    // character's own frame - the engine's shared clock.
                    bool doorPosed = false;
                    if ((boarding || leaving) && static_cast<int>(i) == pd.calledVehicle() && player) {
                        if (!doorClipsRead) {
                            doorClipsRead = true;
                            const auto a = fs.read("ANIMS/SLF_112.3DA");
                            const auto b = fs.read("ANIMS/SLF_113.3DA");
                            if (!a.empty()) doorIn  = omk::clipTracks(a);
                            if (!b.empty()) doorOut = omk::clipTracks(b);
                        }
                        const omk::NodeTracks& dt0 = boarding ? doorIn : doorOut;
                        // THE TRACKS ARE KEYED BY NODE ID, NOT MESH INDEX. The
                        // clip names its five tracks with the bytes 0,3,1,4,2
                        // and `SLI_FN.3DO`'s ids run SlBassin 0, SlPorteZG 1,
                        // SlPorteG 2, SlPorteZD 3, SlPorteD 4 - the cockpit and
                        // its four door parts, exactly a door clip's set. Read
                        // as indices they land on two SHELLS, and the one
                        // moving track fell on the wrong panel. `clipTracks`
                        // fills `ids` as indices (right for the scene clips it
                        // was written for); remapped through the model's ids.
                        omk::NodeTracks dt = dt0;
                        // BY NAME (+4 of the track header), not by id or index:
                        // the mover is named `SlPorteZG`, the parent copy on the
                        // G side; its child `SlPorteG` follows it, so the two
                        // coincident copies swing together and nothing stays
                        // over the hole. Read by index the swing landed on
                        // `SlPorteG` alone; by id on `SlPorteD`, the far door.
                        for (std::size_t ti = 0; ti < dt.ids.size(); ++ti) {
                            int idx = -1;
                            const std::string& nm = ti < dt.names.size() ? dt.names[ti] : std::string();
                            for (std::size_t k = 0; k < sv.mo->meshes.size(); ++k)
                                if (nm == sv.mo->meshes[k].name) { idx = static_cast<int>(k); break; }
                            dt.ids[ti] = idx;
                        }
                        static bool doorTold = false;
                        if (!doorTold && dt.valid()) {
                            doorTold = true;
                            std::printf("slider: the door clip's tracks by name ->");
                            for (auto id : dt.ids)
                                std::printf(" %s", id >= 0 ? sv.mo->meshes[static_cast<std::size_t>(id)].name : "?");
                            std::printf("\n");
                        }
                        if (dt.valid()) {
                            int f = player->poseFrame();
                            if (f < 0) f = 0;
                            if (f >= dt.frames) f = dt.frames - 1;
                            const omk::Geometry& rest = lodRestFor(v.model, *sv.mo, sv.lodRoot);
                            const auto dp = omk::composePose(sv.mo->meshes, dt, f, false);
                            omk::applyPose(sv.posed, rest, sv.mo->meshes, dp);
                            doorPosed = true;
                        }
                    }
                    // `sub_437F80(inst, x, y - 30.75, z)`: the instance sits
                    // 30.75 units ABOVE the body point (y is down), turned to
                    // the heading `sub_453330` built from the direction to its
                    // mover - which for a vehicle is where it is going.
                    float vcs, vsn;
                    omk::yawSinCos(m.facing, vcs, vsn);   // once a vehicle, not a corner
                    sv.gpu = !doorPosed && !cpuBodiesFlag && world.posesBodies() &&
                             sv.atRest.cornerMesh.size() == sv.atRest.corners.size();
                    if (sv.gpu) {
                        // x' = R (x - origin) + body - lift, R the yaw as
                        // `rotateYawCS` applies it: x' = c x - s z, z' = s x + c z
                        const float tx = m.body[0] - (vcs * sv.origin[0] - vsn * sv.origin[2]);
                        const float ty = m.body[1] - omk::kVehNodeLift - sv.origin[1];
                        const float tz = m.body[2] - (vsn * sv.origin[0] + vcs * sv.origin[2]);
                        const float a[12] = {vcs, 0.0f, -vsn, tx,
                                             0.0f, 1.0f, 0.0f, ty,
                                             vsn, 0.0f, vcs, tz};
                        sv.affine.resize(12 * sv.mo->meshes.size());
                        for (std::size_t k = 0; k < sv.mo->meshes.size(); ++k)
                            std::memcpy(&sv.affine[12 * k], a, sizeof a);
                        sv.drawn = true;
                        ++vehDrawn;
                        continue;
                    }
                    if (!doorPosed) sv.posed = sv.atRest;
                    for (auto& c : sv.posed.corners) {
                        const float in[3] = {c.x - sv.origin[0], c.y - sv.origin[1], c.z - sv.origin[2]};
                        float r[3];
                        omk::rotateYawCS(vcs, vsn, in, r);
                        c.x = r[0] + m.body[0];
                        c.y = r[1] + m.body[1] - omk::kVehNodeLift;
                        c.z = r[2] + m.body[2];
                    }
                    sv.posed.revision = ++worldGeoRev;
                    sv.drawn = true;
                    ++vehDrawn;
                }
                phSpan["vehicles"] += phaseNow() - veh0;
                if (vehLive && (vehTold < 0 || n - vehTold >= 300)) {
                    vehTold = n;
                    int brakes = 0, bumps = 0;
                    float closest = 1e9f;
                    const float* ppos = player ? player->pos() : session.playerPos();
                    for (const auto& vv : session.sliders().vehicles()) {
                        brakes += vv.brakes; bumps += vv.bumps;
                        if (!vv.live || vv.mover < 0) continue;
                        const auto& mm = session.sliders().movers()[static_cast<std::size_t>(vv.mover)];
                        const float dx = mm.body[0] - ppos[0], dz = mm.body[2] - ppos[2];
                        closest = std::min(closest, std::sqrt(dx * dx + dz * dz));
                    }
                    std::printf("frame %ld: traffic - %d live, %d drawn within %.0f of the eye, "
                                "%d stopped; braked for the player %d frames, touched him %d times, "
                                "nearest now %.0f\n", n, vehLive, vehDrawn, vreach, vehStopped,
                                brakes, bumps, double(closest));
                }
            }
            const double player0 = phaseNow();
            bool playerOffView = false;    // his corners not built, his body not drawn
            bool playerGpu = false;        // his corners not built, his rest drawn by the renderer
            if (drawPlayer || drawArm) {
                // THE PLAYER, posed by his channel's clip - the quaternions
                // alone, since the root motion is the position the walker
                // integrated - turned by his facing (the row-vector rotation
                // `Matrix3x3_FromEulerAngles(0, yaw, 0)` gives, the same one
                // his root delta and the camera use) and stood with his FEET
                // on `pos()`, which the walker keeps on the floor. The feet
                // offset is the rest pose's, taken once, so a clip's bob does
                // not move the ground under him.
                // `player.anim.hold` neither resets to rest nor freezes: the
                // channel keeps ticking with no input (see the tick above), so
                // the pose comes from it exactly as it does unheld. This code
                // has now been wrong twice in the other two directions - first
                // `composePose(meshes, {}, 0)`, the REST SENTINEL, which drew a
                // T-pose; then a latched pose, which froze him mid-stride.
                const omk::NodeTracks* pt = player->poseTracks();
                std::vector<omk::MeshPose> pose = pt
                    ? playerPoseNow(&*player, session.shootMode().active() &&
                                                  player->state() == omk::ActorState::Shoot,
                                    *pt, player->poseFrameF())
                    : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
                // OUTSIDE THE VIEW, as any actor: `sub_48D3B0` skips a node
                // outside the four side planes before its matrices or its
                // vertices, and the Bowie sequence flies the camera round the
                // city while he stands still at his arrival point. Only his
                // CORNERS and his draw are skipped - the pose, the head, the
                // bones and the shadow bones below come from `pose` - and only
                // while nothing this frame reads the corners: the feet latch,
                // a clip with variants, melee's fist, shoot mode's arm, and
                // the two enhancements that bound or light him by them. The
                // sphere is generous: 150 units about a point 35 up his body.
                {
                    const float* p0 = player->pos();
                    const float centre[3] = {p0[0], p0[1] - 35.0f, p0[2]};
                    const bool standingNow = player->clipName() == "H_STAND";
                    const bool cornersUnread =
                        playerFeetKnown && !(standingNow && !playerStandLatched) &&
                        player->variantCount() <= 1 && !omk::envSet("OMK_PLY") &&
                        !fightRun.active && !session.shootMode().active() && !drawArm &&
                        lighting == 0 && !(drawShadows && shadowQuality >= 2);
                    playerOffView = cornersUnread && outsideView(centre, 150.0f, true);
                    // IN THE VIEW, AS THE ORIGINAL DRAWS ANY ACTOR: `sub_48D3B0`
                    // builds one 3x3 a mesh (`sub_494650`: the node's rotation
                    // times its parent's, the position beside it) and
                    // `sub_4947F0` runs each vertex through it once, on the way
                    // to the card - nothing keeps a posed copy of the body. On
                    // a renderer that poses bodies that is his rest, uploaded
                    // once, and one affine a mesh with his placement folded in
                    // (`todo/gpu-skinning.md` step 5); the frames that READ his
                    // corners - the list above - keep the CPU path.
                    static const bool cpuPlayer = omk::envSet("OMK_CPU_PLAYER");   // A/B only
                    playerGpu = cornersUnread && !playerOffView && drawPlayer && !cpuBodiesFlag &&
                                !cpuPlayer && world.posesBodies() &&
                                playerRest.cornerMesh.size() == playerRest.corners.size();
                }
                if (!playerOffView && !playerGpu) {
                    omk::applyPose(playerPosed, playerRest, playerMeshes, pose);
                    playerPosedFrame = n;
                }
                // THE ANCHOR IS THE FLOOR, NOT THE HIPS (omk-play 69).
                //
                // `playerFeet` used to be latched from the FIRST pose - the
                // standing one - and subtracted for ever after. Every later
                // pose was then placed by where the STANDING feet were, so a
                // pose that lowers the body relative to its feet is drawn with
                // the hips pinned and the legs coming up instead: a reader,
                // watching a take, "the character anchor is their hips and not
                // the floor... their legs go up, like the character is
                // floating".
                //
                // The crouch itself is a root TRANSLATION, and this port has no
                // path for one: `poseTracks` assigns `t.trans` all zeroes and
                // `composePose` never reads it. Re-measuring the lowest corner
                // each frame supplies the same result from the other side - the
                // body's lowest point sits on the walker's floor point, so the
                // planted foot stays down and the pelvis drops.
                //
                // **LABELLED, a port decision rather than a transcription**:
                // the engine seats the actor by his own origin and lets root
                // motion move him, which is a different mechanism. This is
                // equivalent while some part of him is on the ground and would
                // be WRONG for a pose where both feet leave it - a jump. The
                // port has no jump or fall state yet (omk-play 68), so nothing
                // today can tell them apart; when one arrives, this is the
                // line that has to become the real root-motion path.
                // THE ANCHOR AND THE DROP MUST SHARE ONE ORIGIN
                // (`todo/player-vertical.md` step 1, 2026-09-09).
                //
                // The reader: *"walking or running seems to rise its y
                // position and stopping (and so returning to the idle
                // position) reset the y position to a normal one"*. It is not
                // the clips: measured with `tools/vertical_probe`, `H_WALK`
                // f0 lifts the body's lowest corner 2.06 above the standing
                // one while its pelvis track drops 2.09, so the authored pair
                // CANCELS to 0.03 - the planted foot stays planted, which is
                // the "authored motion netting out" the engine relies on.
                //
                // What floated was this port's bookkeeping. `playerFeet` is a
                // POSE's lowest corner, latched from `H_STAND` - whose pelvis
                // trans is +0.68, not zero - while `rootAccum` below re-bases
                // to the ENTERED clip's first frame: +2.09 for `H_WALK`,
                // +3.68 for `H_RUN`. The two halves therefore measured from
                // different origins, and the body was drawn a CONSTANT
                // 2.09-0.68 = 1.41 too high walking and 3.68-0.68 = 3.00 too
                // high running, snapping back the moment `H_STAND` released
                // the sum. That is the report exactly, running rising more.
                //
                // So the anchor records the pelvis trans it was taken at
                // (`playerRootRef`) and the drop is measured from it.
                //
                // AND IT IS LATCHED FROM `H_STAND` SPECIFICALLY, not from
                // whatever pose happened to be up on the first frame: the
                // anchor is meant to be a model constant, and one taken off an
                // arbitrary pose carries that pose's own vertical into every
                // frame afterwards. A provisional latch draws until the idle
                // first comes round, and is then replaced once.
                //
                // Cross-checked against the engine's own constant, which is a
                // different quantity and does not substitute for this one:
                // `Walk_ProbeGround` (0x00467030) seats by the model's
                // COLLISION SPHERES - `Collision_BodySphere`'s largest radius
                // (10.91) plus `sub_4443B0`'s second-lowest centre.y (14.98) -
                // and HO1_FN's lowest sphere reaches 41.81 below the node
                // against a standing visual foot at 38.76 + 0.68 = 39.44. The
                // sphere hangs 2.37 BELOW the feet, and where the engine
                // absorbs that is `actor+276`, the reference its clearance
                // `actor[264] - actor[276]` is taken against - which is
                // UNTRACED. So the sphere constant is not used as the seat
                // here; it is quoted because it corroborates the scale.
                const bool standing = player->clipName() == "H_STAND";
                if (!playerFeetKnown || (standing && !playerStandLatched)) {
                    playerFeet = -1e9f;
                    for (const auto& c : playerPosed.corners)
                        if (c.y > playerFeet) playerFeet = c.y;
                    playerRootRef = (pt && !pt->trans.empty())
                                        ? pt->trans[static_cast<std::size_t>(
                                              player->poseFrame() > 0 ? player->poseFrame() : 0)][1]
                                        : 0.0f;
                    // the hierarchy root: the one mesh with no parent. A model
                    // constant, so this half stays latched.
                    playerRootXZ[0] = playerRootXZ[1] = 0.0f;
                    for (const auto& m : playerMeshes)
                        if (m.parent < 0) {
                            playerRootXZ[0] = m.pos[0];
                            playerRootXZ[1] = m.pos[2];
                            break;
                        }
                    playerFeetKnown = true;
                    if (standing) playerStandLatched = true;
                }
                // THE ROOT TRANSLATION'S VERTICAL, which is the crouch.
                //
                // `Walk_ProbeGround` (0x00467030) anchors the actor by a
                // CONSTANT: `actor[264] = actor[236] - actor[248] + groundY +
                // sphere[12]`, where `sphere[12]` comes from
                // `Collision_BodySphere` and does not change with the pose. So
                // the engine's anchor never moves, and the crouch is carried
                // entirely by the animation's ROOT MOTION - the pelvis track's
                // summed position keys, 24.3 units (62 cm) inside one cell of
                // H_TAKL12.
                //
                // Re-measuring the lowest corner each frame, which is what
                // this did for one build, gets a similar picture and STUTTERS,
                // because it re-derives the anchor from a pose that changes
                // every frame. The anchor is constant again and the drop comes
                // from `trans` instead.
                //
                // Only the VERTICAL is taken: X and Z would double-count
                // against the walker, which already moves him.
                // AND IT ACCUMULATES ACROSS STATES. `Anim_RootDelta(prev,
                // cur)` adds the movement between the previous frame and the
                // current one every tick and "the accumulated offset stands"
                // (CLAUDE.md 6) - there is no reset per clip. Each clip's own
                // sum starts at zero, so taking it as an absolute made
                // H_TAKL22 run 0 -> -24.6: from STANDING to 62 cm above it,
                // instead of from crouched back down to standing. A reader:
                // "animation from up to down => ok / animation from down to up
                // => character suddenly way higher than they should be."
                //
                // Carried, the pair nets out: +24.3 then -24.6 is -0.3.
                static float rootAccum = 0.0f, rootLast = 0.0f;
                static int   rootState = -12345;
                float rootDrop = 0.0f;
                if (pt && !pt->trans.empty()) {
                    const int rf = player->poseFrame();
                    const std::size_t ri = static_cast<std::size_t>(
                        rf < 0 ? 0 : (rf < static_cast<int>(pt->trans.size())
                                          ? rf : static_cast<int>(pt->trans.size()) - 1));
                    const float cur = pt->trans[ri][1];
                    // a new state re-bases the delta, it does not reset the sum
                    if (player->ctlState() != rootState) {
                        rootState = player->ctlState();
                        rootLast  = cur;
                    }
                    // A WINDOW'S LAST FRAME IS A DISCONTINUITY, NOT MOTION.
                    // Measured across a take: the blend runs 0 -> +21.90 over
                    // H_TAKL12's 21 frames (the crouch, against 19.08 of feet
                    // lifted by the rotations - they very nearly cancel) and
                    // then snaps to -0.22 at f 20. That one bogus delta
                    // poisons the sum, so H_TAKL22 starts from a corrupted
                    // base and ends 22 units up - "it stays higher until I
                    // release the object".
                    //
                    // **A PORT GUARD, labelled**: real root motion is a few
                    // units a frame at most (H_WALK's whole cycle spans 2).
                    // Anything larger is a seam between variants, not the
                    // pelvis moving, so it is not accumulated. The engine has
                    // no such guard because `Anim_RootDelta` never crosses a
                    // seam: it indexes the position keys by the RAW frame and
                    // so reads cell 0 only. Reading the blended cells is this
                    // port's choice, and this is its cost.
                    rootAccum += cur - rootLast;
                    rootLast   = cur;
                    // **A PORT GUARD, labelled**: the engine relies on the
                    // authored motion netting out and has a ground probe under
                    // it every frame; this has neither, so any residue would
                    // live for ever. Back in the idle, the body is standing by
                    // definition, so the sum is released there.
                    if (player->clipName() == "H_STAND") rootAccum = 0.0f;
                    // ...and NOT while the slider clips carry him. In ACTOR_STATE
                    // 6 and 8 the clip's root y is applied to his POSITION
                    // (`Actor_MoveBy`, the channel-only tick), so adding the
                    // same travel here as a drop put him one clip's descent
                    // (12.5) too low in the seat - the reader: *"he is just a
                    // bit too low"*.
                    if (boarding || leaving) rootAccum = 0.0f;
                    rootDrop = rootAccum;
                    // ...EXCEPT WHERE THE BODY IS ON THE GROUND BY DEFINITION,
                    // which is every LOOPING clip - the idle, the walk, the
                    // run (`todo/player-vertical.md` step 1).
                    //
                    // The accumulator above exists for the TAKE, whose clips
                    // CHAIN: H_TAKL12 crouches +24.3 and H_TAKL22 carries that
                    // crouch back up, so each reads as a delta on the last and
                    // an absolute reading of either is meaningless (it is the
                    // regression recorded in the note above - H_TAKL22 run as
                    // an absolute goes 0 -> -24.6, from standing to 62 cm
                    // ABOVE it). A locomotion clip is the opposite: it is one
                    // self-contained cycle that begins and ends on the floor,
                    // so its pelvis trans is an OFFSET FROM THE STANDING POSE
                    // and must be read against the origin the anchor was
                    // latched at, never against whatever the last state left
                    // in the sum.
                    //
                    // The discriminator is structural, not a list of names:
                    // `variantCount() > 1` is what marks the grid/take states
                    // (it is what selects `gridTracks`, and what the crouch
                    // trace below is gated on). Everything else loops.
                    //
                    // H_STAND lands on cur 0.68 - ref 0.68 = 0 by
                    // construction, which is why the explicit release below it
                    // was able to look correct while the walk was 1.41 out.
                    if (player->variantCount() <= 1) rootDrop = cur - playerRootRef;
                    // ...AND NOT IN THE WATER, for the slider's reason above. In
                    // ACTOR_STATEs 11..14 the clip's root y reaches his POSITION
                    // (`sub_4A9470` moves him by the whole delta, vertical
                    // included), and for a SWIM clip that y is not a bob at all:
                    // the clips are authored upright and stroke along the body's
                    // own axis, which his pitch then lays along the water. Drawn
                    // as a drop as well, the stroke's travel went onto the body a
                    // second time, in WORLD y and unturned - so on screen he
                    // climbed through every loop whatever way he pointed and
                    // snapped back at the wrap. A reader, 2026-09-17: *"the
                    // character always goes up, whatever is his actual direction
                    // ... each time the animation loop restarts, the character's
                    // position is reset"*.
                    {
                        const int ws = static_cast<int>(player->state());
                        if (ws >= 11 && ws <= 14) { rootDrop = 0.0f; rootAccum = 0.0f; }
                    }
                    // MEASURING, not fixing: how far does the model's own
                    // lowest point travel across a take? If the rotations
                    // lower the body, a CONSTANT anchor is right and the
                    // float came from somewhere else; if it barely moves
                    // while the legs bend, the body never crouches at all.
                    // OMK_PLY=N prints every Nth frame (1 for every frame, which is what a
                    // jump needs - its whole arc is 14 frames and a stride of 4
                    // steps straight over the apex).
                    static const long plyEvery = [] {
                        const char* e = std::getenv("OMK_PLY");
                        const long v = e ? std::atol(e) : 0;
                        return v > 1 ? v : 1;
                    }();
                    if (omk::envSet("OMK_PLY") && (n % plyEvery) == 0) {
                        float lo = -1e9f, hi = 1e9f;
                        for (const auto& c : playerPosed.corners) {
                            if (c.y > lo) lo = c.y;
                            if (c.y < hi) hi = c.y;
                        }
                        // `foot` is the one that matters and the one this
                        // print did NOT carry: `lo` is a MODEL-space corner
                        // (this runs before the offset loop below), so its
                        // `gap` against the ground is not a distance on
                        // screen. The drawn foot sits at
                        // `lo + pos.y - playerFeet + rootDrop`, so its height
                        // above the floor is `lo - playerFeet + rootDrop` -
                        // NEGATIVE is above, y growing down. That is the
                        // number `verify.py: engine player vertical` asserts
                        // and the one the walk float showed up in.
                        std::printf("DBG ply f%ld %-9s pf %2d  ground %+8.2f  lowest %+8.2f"
                                    "  gap %+7.2f  head %+8.2f  rootDrop %+6.2f  foot %+7.2f"
                                    "  at %+9.2f %+9.2f  air %d\n",
                                    n, player->clipName().c_str(), player->poseFrame(),
                                    player->pos()[1], lo, lo - player->pos()[1], hi,
                                    rootDrop, lo - playerFeet + rootDrop,
                                    player->pos()[0], player->pos()[2],
                                    player->walker().airborne() ? 1 : 0);
                    }
                    if (player->variantCount() > 1) {
                        float lo = -1e9f;
                        for (const auto& c : playerPosed.corners)
                            if (c.y > lo) lo = c.y;
                        const auto& lf = player->last();
                        std::printf("  crouch: %-9s f %2d  lowest %+8.2f  "
                                    "(latched %+8.2f, delta %+7.2f)  root %+6.2f"
                                    "  at %.1f %.1f  dxz %+.2f %+.2f  step %d%s\n",
                                    player->clipName().c_str(), player->poseFrame(),
                                    lo, playerFeet, lo - playerFeet, rootAccum,
                                    player->pos()[0], player->pos()[2],
                                    lf.rootDelta[0], lf.rootDelta[2],
                                    static_cast<int>(lf.step),
                                    lf.stepped ? "" : " (no step asked)");
                    }
                }
                const float* pp = player->pos();
                float yaw = player->facing();
                // ---- ...AND THE SLIDER OWNS HIM WHILE HE BOARDS ----------
                //
                // `MDACTION` and `sub_468FA0` both end with
                // `sub_437140(node, M_slider)`, which writes the SLIDER's
                // matrix into the actor node's `+156` - and `+156` is read as
                // an ORIENTATION (21_d3d.c 2854 takes
                // `Matrix3x3_RotateVector(0, 0, -1, node+156)` as the node's
                // heading). So for the whole of ACTOR_STATE 6 and 8 the body
                // is drawn in the vehicle's frame, which is what squares him
                // to the door however he walked up to it.
                //
                // The engine's forward is `-row2`: `sub_456530` case 7 tests
                // `(sin y, 0, -cos y)` against the player's own +420, and the
                // vehicle matrix's row 2 is the negated travel direction. So
                // the drawn yaw is `atan2(-row2.x, row2.z)`.
                //
                // THIS is the line that had to change. Writing the same yaw
                // into `Session::setPlayerPosition` instead - which is what
                // the first attempt did - updates the logical player record
                // and NOTHING on screen, because the model is posed from
                // `player->facing()` right here. A reader saw exactly that:
                // *"I don't see any change"*.
                if (boarding || leaving) {
                    float bat[3], bx[3], bz[3];
                    if (session.sliders().calledFrame(bat, bx, bz))
                        yaw = static_cast<float>(
                            std::atan2(-bz[0], bz[2]) * 57.29577951308232);
                }
                // THE WHOLE EULER, not the yaw: the node's `+156` is actor+288,
                // `Matrix3x3_FromEulerAngles(+416, +420, +424)` rebuilt each
                // frame by `Actors_TickAll`, so the pitch `sub_4A8F30` turns a
                // swimmer by - and the shove's lean - reach the drawn body as
                // they reach his root motion (`todo/swimming.md` step 3b).
                const float drawEuler[3] = {player->euler()[0], yaw, player->euler()[2]};
                // ROTATE ABOUT THE PELVIS, not the model's origin. A `.3DO`'s
                // meshes carry ABSOLUTE positions and the body is not built
                // around (0,0,0): `HO1_FN`'s root `UBassin` sits at
                // x 2.87, z **17.94**, and its whole bounding box spans
                // z 10.5..21.3. Spinning the raw corners about the origin
                // therefore swings the character around a point about 18
                // inches - half a metre - away from himself, which is what a
                // reader described as "the pivot is placed about a metre
                // ahead of the character". The actor's own origin is the
                // pelvis (`player.h`, settled with the camera lift), so that
                // is what must stay put.
                if (playerGpu) {
                    // the loop below as a 3x4: turned about the pelvis's x/z
                    // (`t = T - R root`), then stood at `pp` on his feet
                    float place[12];
                    for (int j = 0; j < 3; ++j) {
                        const float e[3] = {j == 0 ? 1.0f : 0.0f, j == 1 ? 1.0f : 0.0f,
                                            j == 2 ? 1.0f : 0.0f};
                        float col[3];
                        omk::rotateEuler(drawEuler, e, col);
                        for (int r = 0; r < 3; ++r) place[4 * r + j] = col[r];
                    }
                    const float T[3] = {pp[0], pp[1] - playerFeet + rootDrop, pp[2]};
                    for (int r = 0; r < 3; ++r)
                        place[4 * r + 3] = T[r] - (place[4 * r] * playerRootXZ[0] +
                                                   place[4 * r + 2] * playerRootXZ[1]);
                    omk::meshAffines(playerMeshes, pose, place, playerAffine);
                }
                if (!playerOffView && !playerGpu) for (auto& c : playerPosed.corners) {
                    const float in[3] = {c.x - playerRootXZ[0], c.y,
                                         c.z - playerRootXZ[1]};
                    float r[3];
                    omk::rotateEuler(drawEuler, in, r);
                    c.x = r[0] + pp[0];
                    c.y = r[1] + pp[1] - playerFeet + rootDrop;
                    c.z = r[2] + pp[2];
                }
                // the head mesh, looked up again only when the player's model
                // changes (`player.become`) - it was an O(n^2) walk and a string
                // a mesh, every frame
                static const void* headFor = nullptr;
                static std::size_t headForN = 0;
                static int headCached = -1;
                if (headFor != playerMeshes.data() || headForN != playerMeshes.size()) {
                    headFor = playerMeshes.data();
                    headForN = playerMeshes.size();
                    headCached = omk::headMeshOf(playerMeshes);
                }
                if (const int hd = headCached;
                    hd >= 0 && static_cast<std::size_t>(hd) < pose.size()) {
                    const float in[3] = {pose[static_cast<std::size_t>(hd)].pos[0] - playerRootXZ[0],
                                         pose[static_cast<std::size_t>(hd)].pos[1],
                                         pose[static_cast<std::size_t>(hd)].pos[2] - playerRootXZ[1]};
                    float r[3];
                    omk::rotateEuler(drawEuler, in, r);
                    playerHeadRise = -r[1];
                    playerHeadAt[0] = r[0] + pp[0];
                    playerHeadAt[1] = r[1] + pp[1] - playerFeet + rootDrop;
                    playerHeadAt[2] = r[2] + pp[2];
                    playerHeadKnown = true;
                }
                playerMeshAt.assign(pose.size() * 3, 0.0f);
                playerMeshRot.assign(pose.size() * 9, 0.0f);
                for (std::size_t mi = 0; mi < pose.size(); ++mi) {
                    const float in[3] = {pose[mi].pos[0] - playerRootXZ[0],
                                         pose[mi].pos[1],
                                         pose[mi].pos[2] - playerRootXZ[1]};
                    float r[3];
                    omk::rotateEuler(drawEuler, in, r);
                    playerMeshAt[mi * 3 + 0] = r[0] + pp[0];
                    playerMeshAt[mi * 3 + 1] = r[1] + pp[1] - playerFeet + rootDrop;
                    playerMeshAt[mi * 3 + 2] = r[2] + pp[2];
                    // its world rotation: the pose's own, then his yaw - the
                    // same composition the corners get
                    for (int ax = 0; ax < 3; ++ax) {
                        const float e[3] = {ax == 0 ? 1.0f : 0.0f, ax == 1 ? 1.0f : 0.0f,
                                            ax == 2 ? 1.0f : 0.0f};
                        float qv[3], wv[3];
                        omk::qrot(pose[mi].q, e, qv);
                        omk::rotateEuler(drawEuler, qv, wv);
                        for (int k = 0; k < 3; ++k)
                            playerMeshRot[mi * 9 + static_cast<std::size_t>(ax * 3 + k)] = wv[k];
                    }
                }
                playerMeshAtKnown = true;
                lastRootDrop = rootDrop;
                if (!playerOffView && !playerGpu) playerPosed.revision = ++worldGeoRev;
                for (int k = 0; k < 3; ++k) actorAt[k] = pp[k];
                actorAt[1] -= playerFeet;
                actorKnown = true;
            }

            // THE PARTICLES. A section C effect names its sprite by an
            // index into the GLOBAL library `aventure.scx` registers, and its
            // texture goes into the pool after the set's and the speaker's -
            // the batch then carries that slot in the bucket key's low six
            // bits, which is the engine's own indexing and not a second
            // mechanism.
            // Each batch carries the SPRITE index as its material; the pool
            // holds every sprite after the set's and the speaker's textures,
            // so a batch's slot is `spriteBase + sprite`.
            int spriteBase = -1;
            for (int k = 0; k < 3; ++k) spriteAnchor[k] = view.cam.at[k];
            spriteAnchorSet = true;
            const bool scriptSprites = !noScriptSprites && session.scene().loaded() && !session.scene().sprites().empty();
            if ((session.scene().effects().count() || !ctlSprites.empty() || !foeSprites.empty() ||
                 scriptSprites) && !spriteTab.empty()) {
                omk::particleGeometry(fxGeo, session.scene().effects(),
                                      view.cam.eye, view.cam.at, spriteLookup);
                // The `.CTL` sprites, each on its bone THIS frame (flag 1,
                // "follow the bone every frame" - all shipped records here
                // carry it; the others are placed once and this moves them
                // too, labelled). The frame is `(clock - from) / duration`,
                // as Cef_TickEffects writes it; flag 8's doubled sprite
                // clock is not modelled.
                if (!ctlSprites.empty() && player && drawPlayer) {
                    const omk::NodeTracks* pt = player->poseTracks();
                    const std::vector<omk::MeshPose> pose = pt
                        ? omk::composePose(playerMeshes, *pt, player->poseFrameF(), false)
                        : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
                    const float* pp = player->pos();
                    const float spriteEuler[3] = {player->euler()[0], player->facing(),
                                                  player->euler()[2]};
                    const float fr = player->channelFrame();
                    ctlField.clear();
                    for (const auto& c : ctlSprites) {
                        const char* want = c.attach < 18 ? kAttachName[c.attach] : "Buste";
                        int node = -1;
                        for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                            if (std::strstr(playerMeshes[i].name, want)) node = static_cast<int>(i);
                        if (node < 0 || static_cast<std::size_t>(node) >= pose.size()) continue;
                        const omk::MeshPose& hp = pose[static_cast<std::size_t>(node)];
                        const float in[3] = {hp.pos[0] - playerRootXZ[0], hp.pos[1],
                                             hp.pos[2] - playerRootXZ[1]};
                        float o[3];
                        omk::rotateEuler(spriteEuler, in, o);
                        omk::Particle p;
                        p.pos[0] = o[0] + pp[0];
                        p.pos[1] = o[1] + pp[1] - playerFeet + lastRootDrop;
                        p.pos[2] = o[2] + pp[2];
                        p.life = c.duration > 0.0f ? c.duration : 1.0f;
                        p.frameAge = fr - c.from;
                        if (p.frameAge < 0.0f) p.frameAge = 0.0f;
                        if (p.frameAge > p.life) p.frameAge = p.life;
                        p.age = p.frameAge;
                        p.scale = c.scale > 0.0f ? c.scale : 1.0f;
                        p.sprite = c.sprite;
                        p.mode = 4;                        // Cef_SpawnEffect: +10 = 4, additive
                        if (fr < c.from) continue;         // not in its window yet
                        ctlField.addParticle(p);
                    }
                    omk::particleGeometry(ctlGeo, ctlField, view.cam.eye, view.cam.at, spriteLookup);
                    const std::size_t base = fxGeo.corners.size();
                    for (omk::Batch b : ctlGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
                    fxGeo.corners.insert(fxGeo.corners.end(), ctlGeo.corners.begin(), ctlGeo.corners.end());
                    fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), ctlGeo.cornerMirror.begin(), ctlGeo.cornerMirror.end());
                    fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), ctlGeo.cornerMesh.begin(), ctlGeo.cornerMesh.end());
                    fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), ctlGeo.cornerVertex.begin(), ctlGeo.cornerVertex.end());
                    fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), ctlGeo.cornerDeclared.begin(), ctlGeo.cornerDeclared.end());
                    ++fxGeo.revision;
                }
                // THE OPPONENT'S, on his bones as DRAWN last frame (`meshAt`,
                // the same frame the bolts' hit test reads), found by the
                // attach table's name - the last match, as the player's.
                if (!foeSprites.empty() && fightRun.active && fightRun.body &&
                    fightRun.body->mo && fightRun.foeChannel) {
                    const Staged& fb2 = *fightRun.body;
                    const auto& ms = fb2.mo->meshes;
                    const float fr = fightRun.foeChannel->frame();
                    ctlField.clear();
                    long placed = 0;
                    for (const auto& c : foeSprites) {
                        const char* want = c.attach < 18 ? kAttachName[c.attach] : "Buste";
                        int node = -1;
                        for (std::size_t i = 0; i < ms.size(); ++i)
                            if (std::strstr(ms[i].name, want)) node = static_cast<int>(i);
                        if (node < 0 || fb2.meshAt.size() < (static_cast<std::size_t>(node) + 1) * 3) continue;
                        if (fr < c.from) continue;
                        omk::Particle p;
                        for (int k = 0; k < 3; ++k)
                            p.pos[k] = fb2.meshAt[static_cast<std::size_t>(node) * 3 + static_cast<std::size_t>(k)];
                        p.life = c.duration > 0.0f ? c.duration : 1.0f;
                        p.frameAge = std::clamp(fr - c.from, 0.0f, p.life);
                        p.age = p.frameAge;
                        p.scale = c.scale > 0.0f ? c.scale : 1.0f;
                        p.sprite = c.sprite;
                        p.mode = 4;
                        ctlField.addParticle(p);
                        ++placed;
                        // what can actually DRAW: a batch whose sprite has no
                        // pool slot is skipped at submission, which is how 482
                        // placements once drew nothing (15.16)
                        if (spriteSlot.count(c.sprite)) ++foeSpritesPooled;
                    }
                    if (placed) {
                        foeSpritesDrawn += placed;
                        omk::particleGeometry(ctlGeo, ctlField, view.cam.eye, view.cam.at, spriteLookup);
                        const std::size_t base = fxGeo.corners.size();
                        for (omk::Batch b : ctlGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
                        fxGeo.corners.insert(fxGeo.corners.end(), ctlGeo.corners.begin(), ctlGeo.corners.end());
                        fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), ctlGeo.cornerMirror.begin(), ctlGeo.cornerMirror.end());
                        fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), ctlGeo.cornerMesh.begin(), ctlGeo.cornerMesh.end());
                        fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), ctlGeo.cornerVertex.begin(), ctlGeo.cornerVertex.end());
                        fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), ctlGeo.cornerDeclared.begin(), ctlGeo.cornerDeclared.end());
                        ++fxGeo.revision;
                    }
                }
                // THE SCRIPTED SPRITES - `Script_Display3DSprite` and its
                // family (program.h). An instance the scene has linked is
                // drawn every frame from its own fields: frame `+22`, type
                // `+20` (the blend mode), the two scales, the roll. The
                // engine walks the scene's `+36` list after the effects, and
                // `Sprite_SetFrame` hides an instance whose frame is past the
                // sprite's count by writing 0xFFFF, which the clamp here
                // stands in for.
                if (scriptSprites) {
                    omk::ParticleField scField;
                    for (const auto& kv : session.scene().sprites()) {
                        const auto& sp = kv.second;
                        const omk::SpriteFrames* spf = sp.id < 0 ? nullptr : spriteTab.framesOf(sp.id);
                        if (!sp.linked || !spf) continue;
                        const int n = static_cast<int>(spf->frames.size());
                        if (sp.frame < 0 || sp.frame >= n) continue;   // 0xFFFF: not drawn
                        if (spriteLogged[sp.row] != sp.linkedAt + 1) {
                            spriteLogged[sp.row] = sp.linkedAt + 1;
                            std::printf("sprite: LINKED row %d id %d type %d frame %d/%d scale %.2f/%.2f at %.0f,%.0f,%.0f (scene tick %ld)\n",
                                        sp.row, sp.id, sp.type, sp.frame, n, sp.sx, sp.sy,
                                        sp.pos[0], sp.pos[1], sp.pos[2], sp.linkedAt);
                        }
                        omk::Particle p;
                        for (int k = 0; k < 3; ++k) p.pos[k] = sp.pos[k];
                        p.life = 1.0f;
                        p.frame = sp.frame;
                        p.scale = sp.sx;
                        p.scaleY = sp.sy;
                        p.angle = sp.roll;
                        p.sprite = sp.id;
                        p.mode = static_cast<std::uint8_t>(sp.type & 0xF);
                        scField.addParticle(p);
                    }
                    omk::Geometry scGeo;
                    omk::particleGeometry(scGeo, scField, view.cam.eye, view.cam.at, spriteLookup);
                    const std::size_t base = fxGeo.corners.size();
                    for (omk::Batch b : scGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
                    fxGeo.corners.insert(fxGeo.corners.end(), scGeo.corners.begin(), scGeo.corners.end());
                    fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), scGeo.cornerMirror.begin(), scGeo.cornerMirror.end());
                    fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), scGeo.cornerMesh.begin(), scGeo.cornerMesh.end());
                    fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), scGeo.cornerVertex.begin(), scGeo.cornerVertex.end());
                    fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), scGeo.cornerDeclared.begin(), scGeo.cornerDeclared.end());
                    ++fxGeo.revision;
                }
                // The pool already carries them: it is built above, in one
                // place, with a section per model.
                spriteBase = static_cast<int>(spriteTexBase);
            }
            // ONE bucket order for the set, the speaker and the particles.
            // `Render_FlushBuckets` walks a single 14-bit key ascending and
            // meshes and sprites share it (`Render_SubmitSprites` ORs its
            // mode bits into the same array), so an additive particle draws
            // after every opaque mesh and before every multiply one, whatever
            // was submitted first. Submitting the three geometries in turn
            // put `ttt`'s multiply starburst before `burn`'s additive puffs
            // and the intro's dark ring darkened only black. The state bits
            // are the mesh path's own (render.h: 0x2100 additive, 0x2200
            // multiply, 0x400 cutout); the per-face depth bits 0x80/0x1000
            // are not modelled here, as they are not for the set.
            std::vector<omk::Draw> draws;
            const auto keyOf = [](omk::Blend bl, bool cutout, std::uint32_t slot) {
                std::uint32_t state = 0;
                if (bl == omk::Blend::Add)      state = 0x2100;
                else if (bl == omk::Blend::Mul) state = 0x2200;
                else if (cutout)                state = 0x400;
                return state | (slot & 0x3Fu);
            };
            // THE SKY, placed and submitted before anything else.
            //
            // `sub_41CF10` sets the node to the camera's x and z and its own
            // y, so the plane slides with you and never approaches; the
            // corners are rebuilt from the loaded ones about node 0's origin,
            // scaled 12.5x. Options row 4 turns it off by UNLINKING the node,
            // which is simply not submitting it.
            //
            // Its bucket state is 0x800 and not computed from the blend flags:
            // `Area_LoadMiscModel` sets mesh flag 0x10000, and 0x10000's line
            // in `Render_SubmitMesh` is an ASSIGNMENT - `state = 0x800` - which
            // wipes everything else. That is also why the sky never picks up
            // the far-bucket bit (`!(key & 0x800)` guards it) and why its fog
            // range is DOUBLED. It clears 0x3000 too, so the sky is opaque
            // whatever the material says.
            if (drawSky && !sky.base.empty() && !sky.geo.batches.empty()) {
                const float sx = view.cam.eye[0], sz = view.cam.eye[2];
                const float sy = sky.origin[1] - kSkyLift;
                static const bool cpuSky = omk::envSet("OMK_CPU_SKY");   // A/B only
                const bool skyGpu = !cpuSky && !cpuBodiesFlag && world.posesBodies() &&
                                    sky.rest.cornerMesh.size() == sky.rest.corners.size() &&
                                    !sky.rest.corners.empty();
                static int skyPathWas = -1;
                if (skyPathWas != (skyGpu ? 1 : 0)) {
                    skyPathWas = skyGpu ? 1 : 0;
                    std::printf("frame %ld: the sky %s - %zu corners\n", n,
                                skyGpu ? "MOVED BY THE RENDERER (static, one translation a frame)"
                                       : "rewritten on the CPU when the camera moves",
                                sky.base.size());
                }
                if (skyGpu) {
                    std::int32_t meshes = 0;
                    for (const std::int32_t m : sky.rest.cornerMesh) meshes = std::max(meshes, m + 1);
                    sky.affine.assign(12u * static_cast<std::size_t>(meshes), 0.0f);
                    for (std::int32_t m = 0; m < meshes; ++m) {
                        float* a = sky.affine.data() + 12 * m;
                        a[0] = a[5] = a[10] = 1.0f;
                        a[3] = sx; a[7] = sy; a[11] = sz;
                    }
                    for (const auto& b : sky.rest.batches) {
                        draws.push_back({0x800u | ((static_cast<std::uint32_t>(b.material) +
                                                    static_cast<std::uint32_t>(sky.texBase)) & 0x3Fu),
                                         &sky.rest, b.start, b.count, omk::Blend::Opaque, false});
                        draws.back().meshPose = sky.affine.data();
                        draws.back().meshPoses = static_cast<std::size_t>(meshes);
                    }
                } else {
                    // ...and on the CPU only when the camera's x or z CHANGED:
                    // the same corners are the same bytes, and a revision left
                    // alone is a buffer not sent again
                    if (!sky.placed || sky.placedAt[0] != sx || sky.placedAt[1] != sz) {
                        for (std::size_t k = 0; k < sky.base.size(); ++k) {
                            const omk::Corner& b0 = sky.base[k];
                            omk::Corner& c = sky.geo.corners[k];
                            c = b0;
                            c.x = sx + (b0.x - sky.origin[0]) * kSkyScale;
                            c.y = sy + (b0.y - sky.origin[1]) * kSkyScale;
                            c.z = sz + (b0.z - sky.origin[2]) * kSkyScale;
                        }
                        sky.geo.revision = ++worldGeoRev;
                        sky.placed = true;
                        sky.placedAt[0] = sx;
                        sky.placedAt[1] = sz;
                    }
                    for (const auto& b : sky.geo.batches)
                        draws.push_back({0x800u | ((static_cast<std::uint32_t>(b.material) +
                                                    static_cast<std::uint32_t>(sky.texBase)) & 0x3Fu),
                                         &sky.geo, b.start, b.count, omk::Blend::Opaque, false});
                }
            }

            // THE VISIBLE SET, and it is the CLIP DISTANCE that sizes it.
            //
            // `sub_48D3B0` walks the scene's meshes and keeps one when
            // `radius + dword_6A2B9C` exceeds its distance from the camera -
            // and `dword_6A2B9C` is the scene's `+340`, which is options row
            // 3 in metres times 39.37 (`platform/settings.h`). So the option
            // is exactly this radius, and at 25 m ("Tres proche") a street
            // ends a few buildings away.
            //
            // ...and then the four SIDE planes, built from the same global at
            // `25_sys.c` 11598 (`outsideView`, above the staged bodies). They
            // were left out until 2026-09-30 because a wrong plane sign
            // deletes the world silently; what guards that now is the test
            // that a correct cull leaves the frame byte-identical
            // (`OMK_NO_SIDECULL=1` for the other side of it).
            const float clipReach = static_cast<float>(clipInches);
            std::size_t runsDrawn = 0, runsCulled = 0, movingDrawn = 0, movingCulled = 0;
            for (int slot = 0; slot < 2; ++slot) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
                // loaded and solid, but not in the RENDER list (state 1)
                if (!w.shown) continue;
                const std::uint32_t texBase = static_cast<std::uint32_t>(worldTexBase[slot]);
                // THE DEPTH TIE, decided once per set and texture base: the
                // set's whole draw order as this loop builds it - batches in
                // order, keyed alike, stable by key like the sort below - with
                // nothing culled (todo/optimization.md step 29)
                if (w.tieBakedGen != worldGen) {
                    worldSlots[static_cast<std::size_t>(slot)].tieBakedGen = worldGen;
                    std::vector<omk::Draw> order;
                    order.reserve(w.geo.batches.size());
                    for (const auto& b : w.geo.batches)
                        order.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material) + texBase),
                                         &w.geo, b.start, b.count, b.blend, b.cutout});
                    std::stable_sort(order.begin(), order.end(),
                                     [](const omk::Draw& a, const omk::Draw& b) {
                                         return (a.bucketKey & 0x3FFFu) < (b.bucketKey & 0x3FFFu);
                                     });
                    world.bakeDepthTie(&w.geo, order);
                }
                if (w.runs.empty()) {          // no per-corner mesh: whole batches
                    for (const auto& b : w.geo.batches)
                        draws.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material) + texBase),
                                         &w.geo, b.start, b.count, b.blend, b.cutout});
                    continue;
                }
                // One test per mesh, cached across its runs.
                std::vector<std::uint8_t> vis(w.meshes.size(), 2);   // 2 = untested
                // a mesh drawn from its own geometry (`MovingMesh`) is out of
                // the set's draw - never side-culled, so never bridged over
                for (const auto& kv : w.moving)
                    if (kv.first >= 0 && static_cast<std::size_t>(kv.first) < vis.size())
                        vis[static_cast<std::size_t>(kv.first)] = 0;
                const auto visible = [&](std::int32_t mi) -> bool {
                    if (mi < 0 || static_cast<std::size_t>(mi) >= w.meshes.size()) return true;
                    std::uint8_t& v = vis[static_cast<std::size_t>(mi)];
                    if (v != 2) return v == 1;
                    const omk::Mesh& m = w.meshes[static_cast<std::size_t>(mi)];
                    const float dx = m.pos[0] - view.cam.eye[0];
                    const float dy = m.pos[1] - view.cam.eye[1];
                    const float dz = m.pos[2] - view.cam.eye[2];
                    const float reach = m.radius + clipReach;
                    // 1 drawn, 0 beyond the distance, 3 outside the side planes
                    v = !(reach * reach > dx * dx + dy * dy + dz * dz) ? 0
                      : outsideView(m.pos, m.radius, false) ? 3 : 1;
                    return v == 1;
                };
                // A run culled by the SIDE PLANES lies wholly outside the view
                // and cannot put a pixel on the screen, so drawing it costs
                // only vertices - and dropping it SPLITS the batch around it,
                // one draw becoming two (Bowie: 175 draws -> 221, on a backend
                // where each draw is CPU in the driver). So a short run of them
                // between two drawn runs of one batch is drawn through. Never
                // one culled by DISTANCE: that one would show what the engine
                // does not.
                const auto sideCulledOnly = [&](std::int32_t mi) {
                    if (mi < 0 || static_cast<std::size_t>(mi) >= w.meshes.size()) return false;
                    (void)visible(mi);
                    return vis[static_cast<std::size_t>(mi)] == 3;
                };
                constexpr std::uint32_t kBridgeCorners = 300;
                // Emit, merging adjacent surviving runs so a fully visible
                // batch still costs one draw.
                std::size_t i = 0;
                while (i < w.runs.size()) {
                    if (!visible(w.runs[i].mesh)) { ++runsCulled; ++i; continue; }
                    const auto& first = w.runs[i];
                    std::uint32_t start = first.start, count = first.count;
                    std::size_t j = i + 1;
                    std::size_t bridged = 0;
                    for (;;) {
                        while (j < w.runs.size() && w.runs[j].batch == first.batch &&
                               w.runs[j].start == start + count && visible(w.runs[j].mesh)) {
                            count += w.runs[j].count; ++j;
                        }
                        // a gap of side-culled runs, short, and a drawn run after it
                        std::size_t k = j;
                        std::uint32_t gap = 0;
                        while (k < w.runs.size() && w.runs[k].batch == first.batch &&
                               w.runs[k].start == start + count + gap && sideCulledOnly(w.runs[k].mesh) &&
                               gap + w.runs[k].count <= kBridgeCorners) {
                            gap += w.runs[k].count; ++k;
                        }
                        if (k == j || k >= w.runs.size() || w.runs[k].batch != first.batch ||
                            w.runs[k].start != start + count + gap || !visible(w.runs[k].mesh))
                            break;
                        count += gap; bridged += k - j; j = k;
                    }
                    runsDrawn += j - i - bridged;
                    runsCulled += bridged;
                    const auto& b = w.geo.batches[first.batch];
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material) + texBase),
                                     &w.geo, start, count, b.blend, b.cutout});
                    i = j;
                }
                // ...and the moving meshes, where they ARE: the clip distance
                // and the side planes about their placed origin
                for (const auto& kv : w.moving) {
                    const WorldSlot::MovingMesh& mm = kv.second;
                    const float dx = mm.at[0] - view.cam.eye[0], dy = mm.at[1] - view.cam.eye[1],
                                dz = mm.at[2] - view.cam.eye[2];
                    const float reach = mm.reach + clipReach;
                    if (!(reach * reach > dx * dx + dy * dy + dz * dz) ||
                        outsideView(mm.at, mm.reach, false)) { ++movingCulled; continue; }
                    ++movingDrawn;
                    for (const auto& b : mm.rest.batches) {
                        draws.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material) + texBase),
                                         &mm.rest, b.start, b.count, b.blend, b.cutout});
                        draws.back().meshPose = mm.affine;
                        draws.back().meshPoses = 1;
                    }
                }
            }
            if (clipReport) {
                if (unlimitedClip)
                    std::printf("clip: unlimited - %zu mesh runs drawn, %zu culled\n",
                                runsDrawn, runsCulled);
                else
                    std::printf("clip: %.0f in - %zu mesh runs drawn, %zu culled\n",
                                clipInches, runsDrawn, runsCulled);
                if (movingDrawn + movingCulled)
                    std::printf("clip: and %zu moving meshes drawn by the renderer from their "
                                "own corners, %zu culled\n", movingDrawn, movingCulled);
                clipReport = false;
            }
            // A FRAME WHERE THE SET IS CULLED AND THE BODIES ARE NOT draws a
            // character on black, which is what a reader sees as a flicker:
            // the visible-set walk above tests every mesh against the clip
            // distance FROM THE CAMERA EYE, and a staged body is added below
            // with no such test. One line a frame, so a run can be read for
            // the frames where the two disagree.
            std::size_t litBodies = 0;
            for (const auto& up : staged) if (up->drawn && up->mo) ++litBodies;
            if (omk::envSet("OMK_CLIPLOG")) {
                std::printf("  [clip] frame %ld  runs %zu drawn %zu culled  bodies %zu"
                            "  eye %.0f %.0f %.0f\n", n, runsDrawn, runsCulled, litBodies,
                            view.cam.eye[0], view.cam.eye[1], view.cam.eye[2]);
            }
            // ...and the same facts as one line for the flicker catcher, which
            // needs to say WHY a frame went dark, not just that it did.
            if (!flickerDir.empty()) {
                char buf[320];
                std::snprintf(buf, sizeof buf,
                    "3D  cam %s  eye %.0f %.0f %.0f  at %.0f %.0f %.0f  fov %.1f"
                    "  set runs %zu drawn / %zu culled  bodies %zu  area %d scx '%s'",
                    haveDlgCam ? "dialogue" : haveEdit ? "editing" : holdEditCam ? "held"
                        : (takeCam && player) ? "take"
                        : (adventure && followCam) ? "follow" : "world",
                    view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                    view.cam.at[0], view.cam.at[1], view.cam.at[2],
                    static_cast<double>(view.cam.hfovDeg),
                    runsDrawn, runsCulled, litBodies,
                    session.currentArea(), session.scene().file().c_str());
                frameNote = buf;
            }
            phSpan["player"] += phaseNow() - player0;
            mark("pedestrians, traffic");
            // ---- THE LIGHTS, per pixel (`todo/enhancements.md` 7) -------
            //
            // The same `.3DO` records the crowd is lit by, handed to the
            // backend instead of applied on the CPU. Nearest first and capped,
            // because a uniform block is finite and a body is reached by a
            // handful: Anekbah ships 155 and its lamps reach 700-900 units.
            // THE SHIMMER's clock, advanced the way `Game_Tick` does it:
            // `clock += 2 * frameDelta`, wrapping at 256, where one frame's
            // delta is 1.0 at 30 Hz (`docs/BOOT.md` 4). 233 set meshes ride
            // it - the far skyline of every city - and it is the GAME's, not
            // an enhancement, so both backends draw it.
            // ...by the DELTA, not the presented frame: `frameSec * 30` is 1.0
            // at 30 and in every `--frames` run, 0.5 at 60 (todo/sixty-fps.md 2)
            shimmerClock += 2.0f * static_cast<float>(frameSec * 30.0);
            while (shimmerClock >= omk::kShimmerWrap) shimmerClock -= omk::kShimmerWrap;
            view.shimmerClock = shimmerClock;
            view.dither = dither;
            const bool litPerPixel = lighting > 0;
            // TWO BASES, and the difference is a property of the models.
            //
            // `1` starts from BLACK, which is the engine's own rule for the
            // bodies it lights: `sub_494E80` writes `instance[+416]` into
            // every runtime vertex colour and every site that sets +416 sets
            // it to 0, and the crowd models ship pure white (all 446 of
            // PSH_FN's vertices are 255,255,255), so there is no baked light
            // in them to lose.
            //
            // `2` ADDS to the baked colour, and that is this port's decision
            // for the bodies the engine never lights. HO1_FN is not a white
            // model - it carries real baked shading - so black-plus-lamps
            // throws away everything the artist put in and leaves him a
            // silhouette wherever no lamp reaches. Stated here because it is
            // the one place row 7 departs from transcription.
            const int litCrowd  = litPerPixel ? 1 : 0;
            const int litStaged = litPerPixel ? 2 : 0;
            view.lights.clear();
            if (litPerPixel) {
                float at[3] = {view.cam.eye[0], view.cam.eye[1], view.cam.eye[2]};
                if (drawPlayer && !playerPosed.corners.empty()) {
                    at[0] = playerPosed.corners.front().x;
                    at[1] = playerPosed.corners.front().y;
                    at[2] = playerPosed.corners.front().z;
                }
                std::vector<std::pair<float, const omk::Light3do*>> near;
                for (const WorldSlot& ws2 : worldSlots)
                    for (const auto& l : ws2.lights) {
                        const float dx = at[0] - l.pos[0], dy = at[1] - l.pos[1],
                                    dz = at[2] - l.pos[2];
                        const float d2 = dx * dx + dy * dy + dz * dz;
                        if (d2 > l.radiusA * l.radiusA) continue;   // out of reach
                        if (!(l.radiusA > l.radiusB)) continue;
                        near.emplace_back(d2, &l);
                    }
                std::sort(near.begin(), near.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
                if (near.size() > static_cast<std::size_t>(omk::View::kMaxGpuLights))
                    near.resize(static_cast<std::size_t>(omk::View::kMaxGpuLights));
                for (const auto& [d2, l] : near) {
                    omk::View::GpuLight g{};
                    for (int k = 0; k < 3; ++k) { g.pos[k] = l->pos[k]; g.dir[k] = l->dir[k]; }
                    g.radiusA = l->radiusA; g.radiusB = l->radiusB;
                    g.colour[0] = static_cast<float>((l->colour >> 16) & 0xFF) / 255.0f;
                    g.colour[1] = static_cast<float>((l->colour >> 8) & 0xFF) / 255.0f;
                    g.colour[2] = static_cast<float>(l->colour & 0xFF) / 255.0f;
                    g.intensity = l->f32;
                    view.lights.push_back(g);
                }
                if (!lightsTold) {
                    lightsTold = true;
                    std::printf("frame %ld: per-pixel lighting - %zu of the set's lights "
                                "reach the player, nearest first\n", n, view.lights.size());
                }
            }
            // ---- THE MAPPED SHADOW's LIGHT (`todo/enhancements.md` 6) --
            //
            // An ENHANCEMENT: nothing in the engine casts a real shadow. What
            // keeps it from being an invented sun is where the direction comes
            // from - the strongest of the SET's own `.3DO` light records
            // reaching the casters, the same table and the same reach and
            // falloff rules `applyLights` uses to light the crowd. With no
            // light in reach nothing is drawn, which is honest: the port does
            // not know where the light is, so it does not guess.
            //
            // Only characters cast. A set is shaded by a colour baked into
            // every vertex and that colour ALREADY contains the artists'
            // shadows (ASSETS 4c), so a map that darkened the set from the set
            // would paint a second shadow over the first.
            const bool castShadows = drawShadows && shadowQuality >= 2;
            view.shadow = omk::View::ShadowLight{};
            if (castShadows) {
                float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                bool any = false;
                int bodies = 0;
                // ...and only the bodies NEAR THE CAMERA. The slab is one
                // 1024-texel map, so its width is its resolution: bounding
                // every staged body in the city made it 7484 units across -
                // 15 units a texel, which is a shadow the size of a torso.
                // 1200 units is 30 m, past which a character's shadow is
                // sub-pixel anyway. LABELLED as this port's number.
                constexpr float kShadowRange = 1200.0f;
                const auto bound = [&](const omk::Geometry& g) {
                    if (g.corners.empty()) return;
                    const auto& c0 = g.corners.front();
                    const float dx = c0.x - view.cam.eye[0], dy = c0.y - view.cam.eye[1],
                                dz = c0.z - view.cam.eye[2];
                    if (dx * dx + dy * dy + dz * dz > kShadowRange * kShadowRange) return;
                    for (const auto& c : g.corners) {
                        const float p3[3] = {c.x, c.y, c.z};
                        for (int k = 0; k < 3; ++k) {
                            lo[k] = std::min(lo[k], p3[k]);
                            hi[k] = std::max(hi[k], p3[k]);
                        }
                    }
                    ++bodies;
                    any = true;
                };
                if (drawPlayer && !playerPosed.corners.empty()) bound(playerPosed);
                for (const auto& up : staged) if (up->drawn && up->mo && !up->gpu) bound(up->posed);
                // (a body the renderer poses has no world corners here; the
                // mapped shadows are Vulkan's, and Vulkan does not pose)
                for (const auto& up : pedStaged) if (up->drawn && up->mo && !up->gpu) bound(up->posed);
                if (any) {
                    // ...CENTRED ON THE PLAYER, not on the bodies' midpoint.
                    // A street's lamps reach about 700 units and eleven
                    // scattered bodies put their midpoint out in the road
                    // beyond all of them, so the light picked there was the
                    // one distant fill that reaches everywhere - and its
                    // shadow lands off the frame. One map, centred on the
                    // character you are looking at; a body outside the slab
                    // casts nothing, which is stated rather than hidden.
                    float centre[3];
                    if (drawPlayer && !playerPosed.corners.empty()) {
                        float plo[3] = {1e30f, 1e30f, 1e30f}, phi[3] = {-1e30f, -1e30f, -1e30f};
                        for (const auto& c : playerPosed.corners) {
                            const float q[3] = {c.x, c.y, c.z};
                            for (int k = 0; k < 3; ++k) {
                                plo[k] = std::min(plo[k], q[k]);
                                phi[k] = std::max(phi[k], q[k]);
                            }
                        }
                        for (int k = 0; k < 3; ++k) centre[k] = (plo[k] + phi[k]) * 0.5f;
                    } else {
                        for (int k = 0; k < 3; ++k) centre[k] = (lo[k] + hi[k]) * 0.5f;
                    }
                    // Enough for a body and the throw of its shadow under a
                    // steep light; this port's number, and the resolution is
                    // its width (1024 texels over the slab).
                    float rad = 220.0f;
                    const omk::Light3do* best = nullptr;
                    float bestK = 0.0f;
                    for (const WorldSlot& ws2 : worldSlots)
                        if (!ws2.lights.empty()) {
                            float k = 0.0f;
                            const omk::Light3do* l =
                                omk::strongestLightAt(centre, ws2.lights, &k);
                            if (l && k > bestK) { bestK = k; best = l; }
                        }
                    if (best) {
                        view.shadow.on = true;
                        for (int k = 0; k < 3; ++k) {
                            view.shadow.dir[k] = best->dir[k];
                            view.shadow.centre[k] = centre[k];
                        }
                        view.shadow.radius = rad;
                        view.shadow.strength = 0.6f;
                        static const bool shLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                        if (!shadowLightTold || (shLog && (n % 30) == 0)) {
                            shadowLightTold = true;
                            std::printf("frame %ld: mapped shadows - light '%s' at "
                                        "%.0f %.0f %.0f, direction %.2f %.2f %.2f, "
                                        "slab radius %.0f over %d bodies, asked at "
                                        "%.0f %.0f %.0f\n", n, best->name.c_str(),
                                        static_cast<double>(best->pos[0]),
                                        static_cast<double>(best->pos[1]),
                                        static_cast<double>(best->pos[2]),
                                        static_cast<double>(best->dir[0]),
                                        static_cast<double>(best->dir[1]),
                                        static_cast<double>(best->dir[2]),
                                        static_cast<double>(rad), bodies,
                                        static_cast<double>(centre[0]),
                                        static_cast<double>(centre[1]),
                                        static_cast<double>(centre[2]));
                        }
                    } else if (!shadowLightTold) {
                        shadowLightTold = true;
                        std::printf("frame %ld: mapped shadows asked for, but no set light "
                                    "reaches the cast - nothing drawn (the port does not "
                                    "invent a sun)\n", n);
                    }
                }
            }
            mark("lights");
            // ---- THE SHADOWS ------------------------------------------
            //
            // `Actors_TickAll` calls `Actor_DrawShadow(detail, actor)` for
            // every live actor and `Sliders_Tick` places the crowd's, both
            // behind option row 5. Built here, after every body has been
            // placed and before the draw list is sorted, because the blobs
            // are FRAME GEOMETRY - the engine writes them straight into the
            // scene's vertex and triangle pools each frame, not into a node.
            //
            // The probe is the walkable soup, which is what the bodies are
            // seated on; the engine's `World_ProbePoint(-1, ...)` runs on the
            // same collision geometry.
            shadowGeo.corners.clear();
            shadowGeo.batches.clear();
            shadowGeo.cornerMirror.clear();
            shadowGeo.cornerMesh.clear();
            shadowGeo.cornerVertex.clear();
            shadowGeo.cornerDeclared.clear();
            // ...and only up to `fitted`: a MAPPED frame replaces the blobs
            // with a real shadow rather than drawing both.
            if (drawShadows && shadowQuality < 2 && shadowModel.loaded && !playerSoup.empty()) {
                // Option row 7. The player and the fight opponent get it
                // whole; every other actor gets it MINUS ONE, so an npc always
                // casts one tier coarser.
                const int detail = shadowDetail;
                // FITTED gathers the triangles under a body ONCE and probes
                // its grids against those - see `o3de/shadow.h`. Gathered per
                // body inside `castBones`.
                const bool fitted = shadowQuality >= 1;
                shadowSpreadMax = 0.0f; shadowSpreadPlayer = 0.0f;
                long blobs = 0, nPlayer = 0, nActor = 0, nCrowd = 0;
                long nPedDrawn = 0, nPedFeet = 0;
                // ...and the same detector over the BONE path: a bone taken
                // off the wrong LOD skeleton is ~240 units from the body it
                // belongs to, so measure every blob against where the body was
                // actually drawn (its corners' horizontal centre) and report
                // the worst. `ref` is null for the player, whose model has one
                // skeleton and cannot meet this.
                const auto castBones = [&](const std::vector<omk::Mesh>& meshes,
                                           const std::vector<float>& at, int lvl,
                                           int root, const omk::Geometry* ref,
                                           const omk::MeshNameIndex* idx) {
                    // the index when it is built; the scan otherwise, since an
                    // unbuilt index answers -1 for everything and would draw
                    // NO shadows silently
                    const auto boneMesh = [&](const char* bn) {
                        return idx && idx->built() ? idx->find(bn, root)
                                                   : omk::findMeshContaining(meshes, bn, root);
                    };
                    if (at.empty()) return;
                    float rx = 0.0f, rz = 0.0f;
                    if (ref && !ref->corners.empty()) {
                        float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                        for (const auto& c : ref->corners) {
                            lo[0] = std::min(lo[0], c.x); hi[0] = std::max(hi[0], c.x);
                            lo[1] = std::min(lo[1], c.z); hi[1] = std::max(hi[1], c.z);
                        }
                        rx = (lo[0] + hi[0]) * 0.5f; rz = (lo[1] + hi[1]) * 0.5f;
                    }
                    const auto& bones = omk::shadowBonesFor(lvl);
                    // THE BODY'S OWN PATCH OF GROUND, gathered once. Fitted
                    // probes 25 vertices a blob; rescanning the whole set for
                    // each would be thousands of city-wide scans a frame.
                    omk::TriangleSoup local;
                    if (fitted) {
                        float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                        bool any = false;
                        for (int bi : bones) {
                            const int mi = boneMesh(omk::kShadowBones[static_cast<std::size_t>(bi)].bone);
                            if (mi < 0 || static_cast<std::size_t>(mi) * 3 + 2 >= at.size()) continue;
                            const float* q = &at[static_cast<std::size_t>(mi) * 3];
                            lo[0] = std::min(lo[0], q[0]); hi[0] = std::max(hi[0], q[0]);
                            lo[1] = std::min(lo[1], q[2]); hi[1] = std::max(hi[1], q[2]);
                            any = true;
                        }
                        if (!any) return;
                        // inflated by more than the widest blob's half-width
                        // (275.59 x 0.07 x 1.5 = 28.9)
                        local = omk::soupInBox(playerSoup, playerGrid, lo[0] - 40.0, hi[0] + 40.0,
                                               lo[1] - 40.0, hi[1] + 40.0);
                    }
                    const omk::TriangleSoup& soup = fitted ? local : playerSoup;
                    for (int bi : bones) {
                        const auto& sb = omk::kShadowBones[static_cast<std::size_t>(bi)];
                        const int mi = boneMesh(sb.bone);
                        if (mi < 0 || static_cast<std::size_t>(mi) * 3 + 2 >= at.size()) continue;
                        const float* p3 = &at[static_cast<std::size_t>(mi) * 3];
                        if (ref && !ref->corners.empty()) {
                            const float dx = p3[0] - rx, dz = p3[2] - rz;
                            const float d = std::sqrt(dx * dx + dz * dz);
                            if (d > shadowFootOffMax) shadowFootOffMax = d;
                        }
                        const auto f = fitted
                            ? omk::floorUnder(soup, p3[0], p3[1], p3[2])
                            : omk::floorUnder(playerSoup, playerGrid, p3[0], p3[1], p3[2]);
                        if (!f) continue;
                        const std::size_t was = shadowGeo.corners.size();
                        const float rad = meshes[static_cast<std::size_t>(mi)].radius;
                        const bool drew = fitted
                            ? omk::shadowBlobFitted(shadowGeo, shadowModel, p3, rad,
                                                    sb.divisor, sb.reach,
                                                    static_cast<float>(*f), soup)
                            : omk::shadowBlob(shadowGeo, shadowModel, p3, rad,
                                              sb.divisor, sb.reach, static_cast<float>(*f));
                        if (drew) {
                            ++blobs;
                            // THE PROPERTY `fitted` EXISTS FOR: how far a
                            // single blob's own vertices spread vertically.
                            // A classic blob is a flat quad, so this is 0 for
                            // every one of them at every camera and on every
                            // surface; a fitted blob on a slope or a stair
                            // follows the ground and it is not. That is the
                            // measurement, and a pixel count is not - the fan
                            // and the grid interpolate the disc's UVs
                            // differently, so they differ a little even on
                            // perfectly flat ground.
                            float lo = 1e30f, hi = -1e30f;
                            for (std::size_t ci = was; ci < shadowGeo.corners.size(); ++ci) {
                                lo = std::min(lo, shadowGeo.corners[ci].y);
                                hi = std::max(hi, shadowGeo.corners[ci].y);
                            }
                            if (hi - lo > shadowSpreadMax) shadowSpreadMax = hi - lo;
                        }
                    }
                };
                // `Actors_TickAll` skips the shadow in ACTOR_STATE 7 and
                // 11..14 - the slider mount, the ladder, the two scripted
                // states and the water. Read off the same `if` the mark's
                // enable/disable sits under.
                const auto castsIn = [](omk::ActorState st) {
                    const int v = static_cast<int>(st);
                    return !(v == 7 || (v > 10 && v <= 14));
                };
                // The player's model has ONE skeleton, so -1 is the whole of it.
                shadowFootOffMax = pedFootOffMax;
                if (drawPlayer && playerMeshAtKnown && player && castsIn(player->state()))
                    castBones(playerMeshes, playerMeshAt, detail, -1, nullptr, &playerBoneIdx);
                nPlayer = blobs;
                shadowSpreadPlayer = shadowSpreadMax;   // his alone, before the rest
                for (const auto& up : staged)
                    if (up->drawn && up->mo)
                        castBones(up->mo->meshes, up->meshAt, detail - 1, up->shadowRoot,
                                  up->gpu ? nullptr : &up->posed, &up->mo->boneIdx);
                nActor = blobs - nPlayer;
                // The crowd's, which is the other mechanism entirely.
                for (const auto& up : pedStaged) {
                    if (up->drawn) ++nPedDrawn;
                    if (up->drawn && up->footKnown) ++nPedFeet;
                    if (!up->drawn || !up->footKnown) continue;
                    const float mid[3] = {(up->footAt[0][0] + up->footAt[1][0]) * 0.5f,
                                          (up->footAt[0][1] + up->footAt[1][1]) * 0.5f,
                                          (up->footAt[0][2] + up->footAt[1][2]) * 0.5f};
                    const auto h = omk::surfaceUnder(playerSoup, playerGrid, mid[0], mid[1], mid[2]);
                    if (!h) continue;
                    const float nrm[3] = {static_cast<float>(h->n[0]),
                                          static_cast<float>(h->n[1]),
                                          static_cast<float>(h->n[2])};
                    const std::size_t before = shadowGeo.corners.size();
                    if (omk::shadowFootBlob(shadowGeo, shadowModel, up->footAt[0],
                                            up->footAt[1], static_cast<float>(h->y), nrm)) {
                        ++blobs; ++nCrowd;
                        static const bool pedLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                        if (pedLog && nCrowd == 1 && (n % 60) == 0)
                            std::printf("  [shadow] crowd blob: feet %.0f %.0f %.0f / "
                                        "%.0f %.0f %.0f  floor %.0f  corner0 %.0f %.0f %.0f "
                                        "extent %.1f\n",
                                        static_cast<double>(up->footAt[0][0]),
                                        static_cast<double>(up->footAt[0][1]),
                                        static_cast<double>(up->footAt[0][2]),
                                        static_cast<double>(up->footAt[1][0]),
                                        static_cast<double>(up->footAt[1][1]),
                                        static_cast<double>(up->footAt[1][2]), h->y,
                                        static_cast<double>(shadowGeo.corners[before].x),
                                        static_cast<double>(shadowGeo.corners[before].y),
                                        static_cast<double>(shadowGeo.corners[before].z),
                                        static_cast<double>(
                                            shadowGeo.corners[before].x - up->footAt[0][0]));
                    }
                }
                if (!shadowGeo.corners.empty()) {
                    // ONE batch: every blob goes through the same material and
                    // the same mesh flags 0x5000, so the whole frame's shadow
                    // is one submission.
                    shadowGeo.batches.push_back({0, false, omk::Blend::Mul, 0,
                                                 shadowGeo.corners.size()});
                    shadowGeo.revision = ++worldGeoRev;
                    draws.push_back({keyOf(omk::Blend::Mul, false,
                                           static_cast<std::uint32_t>(shadowTexBase)),
                                     &shadowGeo, 0, shadowGeo.corners.size(),
                                     omk::Blend::Mul, false});
                }
                shadowBlobsDrawn = blobs;
                if (!shadowTold && blobs > 0) {
                    shadowTold = true;
                    std::printf("frame %ld: shadows ON (row 7 detail %d, quality %s) - "
                                "%ld blobs, %zu corners, texture slot %zu\n",
                                n, detail, omk::shadowQualityName(shadowQuality),
                                blobs, shadowGeo.corners.size(), shadowTexBase);
                }
                static const bool shadowLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                if (shadowLog && (n % 30) == 0)
                    std::printf("  [shadow] frame %ld: %ld player + %ld actor + %ld crowd"
                                " = %ld blobs (%zu peds staged, %ld drawn, %ld with feet)\n",
                                n, nPlayer, nActor, nCrowd, blobs, pedStaged.size(),
                                nPedDrawn, nPedFeet);
                if (shadowLog && (n % 30) == 0)
                    std::printf("  [shadow] worst blob vertical spread %.2f units, "
                                "the player's %.2f (a flat quad is 0 by construction)\n",
                                static_cast<double>(shadowSpreadMax),
                                static_cast<double>(shadowSpreadPlayer));
                if (shadowLog && (n % 30) == 0)
                    std::printf("  [shadow] worst foot-to-body offset %.1f units "
                                "(a bone off the wrong LOD skeleton is ~240)\n",
                                static_cast<double>(shadowFootOffMax));
            }
            // EVERY staged body, each with its own model's base - the change
            // issue 41 asks for. One geometry per actor, so two bodies wearing
            // the same model still draw at their own two places.
            for (const auto& up : staged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                if (up->gpu && up->restGeo) {
                    for (const auto& b : up->restGeo->batches) {
                        draws.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material + base)),
                                         up->restGeo, b.start, b.count, b.blend, b.cutout,
                                         litStaged, castShadows});
                        draws.back().meshPose = up->affine.data();
                        draws.back().meshPoses = up->mo->meshes.size();
                    }
                    continue;
                }
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout,
                                     litStaged, castShadows});
            }
            for (const auto& up : pedStaged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                if (up->gpu && up->restGeo) {
                    for (const auto& b : up->restGeo->batches) {
                        draws.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material + base)),
                                         up->restGeo, b.start, b.count, b.blend, b.cutout,
                                         litCrowd, castShadows});
                        omk::Draw& dr = draws.back();
                        dr.meshPose = up->affine.data();
                        dr.meshPoses = up->mo->meshes.size();
                        dr.vertexLights = up->lightCount ? up->lights.data() : nullptr;
                        dr.vertexLightCount = up->lightCount;
                        dr.lightsFromBlack = up->lightsBlack;
                    }
                    continue;
                }
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout,
                                     litCrowd, castShadows});
            }
            for (const auto& up : vehStaged) {
                if (!up->drawn || !up->mo) continue;
                const int base = static_cast<int>(up->mo->texBase);
                if (up->gpu) {
                    for (const auto& b : up->atRest.batches) {
                        draws.push_back({keyOf(b.blend, b.cutout,
                                               static_cast<std::uint32_t>(b.material + base)),
                                         &up->atRest, b.start, b.count, b.blend, b.cutout,
                                         litCrowd, castShadows});
                        draws.back().meshPose = up->affine.data();
                        draws.back().meshPoses = up->mo->meshes.size();
                    }
                    continue;
                }
                for (const auto& b : up->posed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(b.material + base)),
                                     &up->posed, b.start, b.count, b.blend, b.cutout,
                                     litCrowd, castShadows});
            }
            // which path drew him, said when it changes - and counted, for the
            // runs that compare the two
            {
                static int pathWas = -1;
                static long gpuFrames = 0, cpuFrames = 0;
                const int path = !drawPlayer ? 0 : playerGpu ? 1 : playerOffView ? 2 : 3;
                gpuFrames += path == 1;
                cpuFrames += path == 3;
                if (path != pathWas) {
                    pathWas = path;
                    static const char* const names[4] = {
                        "not drawn", "posed by the RENDERER (his rest, one affine a mesh)",
                        "outside the view, not drawn", "posed on the CPU"};
                    std::printf("frame %ld: the player %s - %zu meshes, %zu corners; so far %ld "
                                "frames by the renderer, %ld on the CPU\n", n, names[path],
                                playerMeshes.size(), playerRest.corners.size(), gpuFrames, cpuFrames);
                }
            }
            if (drawPlayer && playerGpu) {
                for (const auto& b : playerRest.batches) {
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(playerTexBase))),
                                     &playerRest, b.start, b.count, b.blend, b.cutout,
                                     litStaged, castShadows});
                    draws.back().meshPose = playerAffine.data();
                    draws.back().meshPoses = playerMeshes.size();
                }
            } else if (drawPlayer && !playerOffView)
                for (const auto& b : playerPosed.batches)
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(playerTexBase))),
                                     &playerPosed, b.start, b.count, b.blend, b.cutout,
                                     litStaged, castShadows});
            // FIRST PERSON: only the meshes the hide spared - flag 0x200000,
            // the left arm - cut out of the posed body batch by batch.
            static omk::Geometry playerArm;
            if (drawArm) {
                playerArm.corners.clear();
                playerArm.batches.clear();
                const std::vector<std::int32_t>& cm =
                    playerPosed.cornerMesh.size() == playerPosed.corners.size()
                        ? playerPosed.cornerMesh : playerRest.cornerMesh;
                if (cm.size() == playerPosed.corners.size()) {
                    for (const auto& b : playerPosed.batches) {
                        const std::size_t base = playerArm.corners.size();
                        for (std::size_t c = b.start; c < b.start + b.count; ++c) {
                            const std::int32_t mi = cm[c];
                            if (mi < 0 || static_cast<std::size_t>(mi) >= playerMeshes.size()) continue;
                            if (!(static_cast<std::uint32_t>(playerMeshes[static_cast<std::size_t>(mi)].flags)
                                  & 0x200000u)) continue;
                            playerArm.corners.push_back(playerPosed.corners[c]);
                        }
                        const std::size_t cnt = playerArm.corners.size() - base;
                        if (!cnt) continue;
                        omk::Batch nb = b;
                        nb.start = base;
                        nb.count = cnt;
                        playerArm.batches.push_back(nb);
                    }
                }
                playerArm.revision = ++worldGeoRev;
                for (const auto& b : playerArm.batches)
                    draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                               b.material + static_cast<int>(playerTexBase))),
                                     &playerArm, b.start, b.count, b.blend, b.cutout,
                                     litStaged, false});
                static long armTold = -1;
                if (armTold < 0) {
                    armTold = n;
                    std::printf("frame %ld: first person - the arm the hide spares: %zu corners "
                                "of %zu, %zu batches (meshes flagged 0x200000)\n", n,
                                playerArm.corners.size(), playerPosed.corners.size(),
                                playerArm.batches.size());
                }
            }
            // The props, each batch through its own model's pool section.
            for (std::size_t bi = 0; bi < propGeo.batches.size(); ++bi) {
                const auto& b = propGeo.batches[bi];
                const PropModel* owner = bi < propBatchOwner.size() ? propBatchOwner[bi] : nullptr;
                if (!owner) continue;
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material +
                                           static_cast<int>(owner->texBase))),
                                 &propGeo, b.start, b.count, b.blend, b.cutout});
            }
            if (spriteBase >= 0)
                for (const auto& b : fxGeo.batches) {
                    // `b.material` is the sprite's ID; the pool is packed, so
                    // it has to be looked up rather than added to the base
                    const auto slIt = spriteSlot.find(b.material);
                    const int sl = slIt == spriteSlot.end() ? -1 : slIt->second;
                    if (sl < 0) continue;      // a sprite with no texture: not drawn
                    draws.push_back({keyOf(b.blend, b.cutout,
                                           static_cast<std::uint32_t>(spriteBase + sl)),
                                     &fxGeo, b.start, b.count, b.blend, b.cutout});
                }
            std::stable_sort(draws.begin(), draws.end(),
                             [](const omk::Draw& a, const omk::Draw& b) {
                                 return (a.bucketKey & 0x3FFFu) < (b.bucketKey & 0x3FFFu);
                             });
            mark("shadows");
            // ---- AND THE MIRROR, which until now only `--scene` ever got.
            //
            // `drawWithMirror` has been on the renderer boundary since
            // 2026-09-01 and the free-fly viewer was its only caller, so a
            // mirror reflected there and was a flat blended pane in adventure
            // mode and in every cutscene - which is where a player meets one.
            // A reader: *the mirror in the chamber is displayed with
            // transparency instead of reflecting.* The plane was already
            // being read per set (`w.mirror`) and used for nothing but a word
            // in a log line.
            //
            // The engine keeps a SINGLE global (`dword_534F48`), so at most
            // one mirror is live at a time; the shown slot's is the one.
            static const bool noMirror = std::getenv("OMK_NO_MIRROR") != nullptr;
            const omk::MirrorPlane& wmp =
                worldSlots[static_cast<std::size_t>(session.shownSlot() & 1)].mirror;
            {
                // `OMK_CAMLOG=1`: the frame's camera as DRAWN, every frame,
                // after every writer above. A creep of a hundredth of a unit
                // is invisible in any still and is what makes a point-sampled
                // texture twinkle (todo/omk-play 88).
                static const bool camLog = [] { const char* e = std::getenv("OMK_CAMLOG"); return e && *e == '1'; }();
                if (camLog)
                    std::fprintf(stderr, "[cam] frame %ld eye %.4f %.4f %.4f at %.4f %.4f %.4f fov %.3f\n",
                                 n, view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                                 view.cam.at[0], view.cam.at[1], view.cam.at[2], view.cam.hfovDeg);
            }
            mark("shadows, hud models");
            const auto mst = omk::drawWithMirror(world, draws, view,
                                                 noMirror ? omk::MirrorPlane{} : wmp);
            mark("world begin..end (submit, GL)");
            if (mst.active != mirrorLive || (mst.maskPixels > 0 && !mirrorSeen)) {
                mirrorLive = mst.active;
                if (mst.maskPixels > 0) mirrorSeen = true;
                std::printf("frame %ld: the set's mirror is %s%s (%ld px, camera "
                            "%.0f in front)\n", n,
                            mst.active ? "REFLECTING" : "out of view - the camera is behind it",
                            mst.native ? " [native: the two passes]" : "",
                            mst.maskPixels, static_cast<double>(mst.distance));
            }
            // The backend drew the picture into the top-left `vw x vh`; place
            // it, leaving the bands as the black `fb` was cleared to.
            // THE PRESENT PASS (todo/optimization.md step 4b). On a frame that
            // draws NOTHING over the 3D, the readback below, its dither and the
            // upload in `present` only reproduce the attachment's own pixels,
            // so the frame is dithered and presented on the GPU instead
            // (`present.frag`). Everything that draws over the picture or reads
            // `fb` afterwards is excluded here by the test that gates it further
            // down: a screen, the shoot HUD, a media bitmap or line, a
            // conversation, either screen fade, the flicker and clip logs, the
            // snapshots and the final `--dump`. `OMK_NO_GPU_PRESENT=1` turns it
            // off; `OMK_VERIFY_GPU_PRESENT=1` composes the CPU frame as well and
            // compares the two - which is also how a gate missing from this list
            // would show.
#if defined(OMK_VULKAN) || defined(OMK_GLES)
            {
                static const bool noGpuPresent = std::getenv("OMK_NO_GPU_PRESENT") != nullptr;
                const bool lastDumped = !dump.empty() && frames && n + 1 >= frames;
                // THE GLES WINDOW TAKES THE SAME PATH (`todo/vita-port.md` G6
                // step 1, 2026-09-18). A console log put 90 of a cutscene
                // frame's 106 ms in exactly the round trip this skips -
                // glReadPixels 36, the 888 -> 565 dither 35, the upload 16 -
                // against 9 ms for the game itself. `GlesRenderer::presentWorld`
                // dithers on the GPU as `present.frag` does on Vulkan.
                bool onVulkan = false;
#if defined(OMK_VULKAN)
                onVulkan = (vkRen && &world == vkRen) ||
                           (verifyGpuPresent && worldVk && &world == worldVk);
#endif
#if defined(OMK_GLES)
                if (glRen && &world == glRen) onVulkan = true;
#endif
                // the gates in order; the first that holds names why the frame
                // stays on the CPU path (`OMK_GPU_PRESENT_STATS` counts them)
                const char* keep = nullptr;
                if (noGpuPresent) keep = "off";
                else if (!onVulkan) keep = "not vulkan";
                // an open screen is a HARD gate only where something reads the
                // picture by a law the overlay cannot carry (G6 step 4): the
                // viewport item (the world goes INTO the screen), SAVE GAME
                // (the slot's thumbnail is taken from `fb` itself) and a panel
                // with the monitors' INTERFERENCE (row shifts and an OR mask).
                else if (vpItem || (walk && screenReadsPicture)) keep = "screen";
                else if (mst.active && !mst.native) keep = "cpu mirror";
                else if (!flickerDir.empty() || !snapsDir.empty() || omk::envSet("OMK_CLIPLOG")) keep = "instrument";
                else if (lastDumped) keep = "dump";
                // THE SOFT GATES, last: what these three draw goes OVER the
                // picture without reading it (a full-screen bitmap, the text's
                // opaque ramp) or reads it by one of two fixed laws (the
                // dialogue boxes). On the GLES window such a frame is composed
                // over a KEY and blended on the GPU (`presentOverlay`, G6 step
                // 2) - no readback. `OMK_NO_OVERLAY=1` turns that off.
                // a fade is a gate only while it would CHANGE a pixel: the black
                // one is `running()` for whole scenes with both bands at 255,
                // which held every frame of play off the direct path
                else if (session.colourFade().running() && session.colourFade().weight() > 0.0f) { keep = "colour fade"; softGate = true; }
                else if (session.blackFade().running() &&
                         (session.blackFade().bandGrey(false) < 255 || session.blackFade().bandGrey(true) < 255)) { keep = "black fade"; softGate = true; }
                else if (mediaBmp.w > 0 && mediaBmp.h > 0) { keep = "media bitmap"; softGate = true; }
                else if (mediaTextFrames > 0) { keep = "media line"; softGate = true; }
                else if (session.dialogOpen()) { keep = "conversation"; softGate = true; }
                // G6 step 3: both gauges are `Hud_DrawBar`, whose only reads of
                // the picture are `fillQuadD3d`'s three blends - affine, so they
                // run on the overlay's planes. SOFT, and therefore after every
                // hard gate: a soft gate that answered first would hide a dump.
                // ...and the FIGHT HUD, under the same test that draws it: the
                // gauges go into `fb` every melee frame outside a KO replay. It
                // was missing from this list, so on the Vulkan window the gauges
                // showed only while another gate (the fight's opening fade) held
                // the frame on the CPU - a reader: *"The health disappear after
                // some time (only the stats should disappear)"*.
                else if (fightRun.active && fightRun.fight && fightRun.fight->koCounter() == 0 &&
                         !omk::envSet("OMK_NOUI")) { keep = "fight hud"; softGate = true; }
                // ...and the BREATH gauge, under the test that draws it - the
                // same GPU-present gap the fight's gauges fell into
                else if (player && player->breathLeftMs() >= 0.0 &&
                         !omk::envSet("OMK_NOUI")) { keep = "breath gauge"; softGate = true; }
                // G6 step 4: the SHOOT HUD. Its readers are the composer's alpha
                // fill and 50% quad, the gauge's and the radar's `fillQuadD3d` -
                // all on the planes; the turning models, the crosshair and the
                // text write opaque pixels.
                else if (shootMode && hudWalk) { keep = "shoot hud"; softGate = true; }
                // ...and every other OPEN SCREEN: the composer's readers are the
                // alpha fill (the panel dim among them) and the 50% quad
                else if (walk) { keep = "open screen"; softGate = true; }
#if defined(OMK_GLES)
                {
                    static const bool noOverlay = std::getenv("OMK_NO_OVERLAY") != nullptr;
                    overlayFrame = softGate && !noOverlay && !verifyGpuPresent &&
                                   glRen && &world == glRen;
                    if (overlayFrame) {
                        // the statistics name the soft gate behind the overlay
                        static std::string overlayWhy;
                        overlayWhy = std::string("overlay (") + keep + ")";
                        keep = overlayWhy.c_str();
                    }
                }
#endif
                gpuFrame = keep == nullptr;
                gpuKeep = keep ? keep : "";
                gpuVy = view.vy;
                gpuVh = view.vh;
            }
#endif
            if (overlayFrame) {
                // the world's rows are the KEY: "the GPU's picture shows here"
                g_ov.begin(fb.w, fb.h);
                for (int y = 0; y < view.vh; ++y) {
                    const int dy = view.vy + y;
                    if (dy < 0 || dy >= fb.h) continue;
                    std::fill(fb.px.begin() + static_cast<long>(dy) * fb.w,
                              fb.px.begin() + static_cast<long>(dy + 1) * fb.w,
                              std::uint16_t(0xF81F));
                }
            } else if (!gpuFrame || verifyGpuPresent) {
            phRb0 = phaseNow();
            mark("world end, submit");
            const omk::Surface& pic = world.readback();
            phRb1 = phaseNow();
            if (vpItem && pic.w == fb.w) {
                // The picture is the viewport item's; the composer places it
                // at the item's layer, not the frame.
                view3dPic = omk::Surface(view.vw, view.vh, 0);
                for (int y = 0; y < view.vh && y < pic.h; ++y)
                    std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                              pic.px.begin() + static_cast<long>(y) * pic.w + view.vw,
                              view3dPic.px.begin() + static_cast<long>(y) * view.vw);
                comp.attachView3D(&view3dPic);
            } else if (pic.w == fb.w) {
                for (int y = 0; y < view.vh && y < pic.h; ++y) {
                    const int dy = view.vy + y;
                    if (dy < 0 || dy >= fb.h) continue;
                    std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                              pic.px.begin() + static_cast<long>(y + 1) * pic.w,
                              fb.px.begin() + static_cast<long>(dy) * fb.w);
                }
            }
            }   // the CPU composite
            ++worldFrames;
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S WORLD PHASE - whoever is on screen, and the world when no screen is over it.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
#include "playframe.h"

int PlayState::phaseWorld() {
    const auto& fs = *fs_;
    auto& comp = *comp_;
    auto& session = *session_;
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
        wc = session.camera();

        // THE DIALOGUE CAMERAS. A line uses two and travels between them:
        // `sub_4013B0` issues camera command 12 twice, the first with duration
        // -1.0 (a snap) and the second with 160.0, so the view cuts to the
        // node's first camera and moves to its second over 160 frames - 5.3 s
        // at 30, which is why the move stops well before a long line ends. The
        // menu has its own pair and its own cut.
        //
        // The clock is `DialogPlayer`'s and belongs to the LINE, not to any
        // animation - CLAUDE.md 5 has why that matters.
        dlgView = omk::View{};
        haveDlgCam = false;
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
        edit = session.scene().activeEditing();
        editCam = omk::CamSample{};
        haveEdit = edit && session.scene().editingCamera(editCam);
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
        vpItem = (walk && openScreen >= 0)
            ? omk::ScreenComposer::viewportItem(walk->panel() ? walk->panel()
                                                              : w.screen(openScreen))
            : nullptr;
        comp.attachView3D(nullptr);
        screenReadsPicture = false;
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
            worldCamera();
            worldTexturePool();
            worldProps();
            worldGuns();
            worldBolts();
            worldStaged();
            worldCrowd();
            worldDrawLists();
            worldMirror();
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}

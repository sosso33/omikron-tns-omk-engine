// SPDX-License-Identifier: GPL-3.0-or-later
// THE INSTRUMENTS - every harness the viewer carries, in one file
// (`todo/play-split.md` S6, 2026-10-02): the writes into the DB a check
// needs (`--money`, `--give`, `--var`...), the opcodes fired by hand
// (`--zone-enable`, `--scene-load`, `--scx-play`), the entries the game
// reaches through its scripts (`--fight`, `--board`, `--ride`, `--call`,
// `--shoot-end`), the end-of-run save, the flicker catcher and the snaps.
// None of it is the game: each says so where it acts.
//
// Each was a block of the code that calls it, moved byte for byte; the
// call stands where the block stood. A build made with `INSTRUMENTS=0`
// links `playharness_off.cpp` instead, where every one is an empty stub -
// the flags still parse and do nothing, and the run says so once.
#include "playframe.h"

// --newgame-world: a new game's world under the save's player
void PlayState::harnessNewWorld() {
    if (newWorld) {
        // The save brought its own world - doors opened, addresses
        // enabled. Put a new game's back, keeping the player record the
        // save is loaded FOR.
        if (state.debugCopyWorldFrom(omk::GameState::fromFile(fr + "/IAM/START")))
            std::printf("--newgame-world: the six state arrays and the "
                        "three object lists reset to IAM/START (a harness "
                        "write, not `Game_NewGame`)\n");
    }
}

// --money, --rings, --give, --var: harness writes into the DB
void PlayState::harnessStateWrites() {
    // `--money N`: the player record's `+172`, the seteks, written before the
    // first frame so a purchase can be driven from a save that has none (the
    // shipped fixture reads 0). A HARNESS write like `--give`, not anything
    // the game does.
    if (moneyArg >= 0) {
        state.setMoney(std::min(moneyArg, 0xFFFF));
        std::printf("--money: the player record's +172 set to %d (a harness write)\n",
                    state.money());
    }
    // `--rings N`: the ANNEAUX at `+174`, the other half of the same pair.
    // A save costs one and a hint three, and both shipped fixtures carry
    // two - so neither purchase can be driven from them without this. A
    // HARNESS write, like `--money` and `--give`.
    if (ringsArg >= 0) {
        state.setRings(std::min(ringsArg, 0xFFFF));
        std::printf("--rings: the player record's +174 set to %d (a harness write)\n",
                    state.rings());
    }
    if (!giveList.empty()) {
        int placed = 0, refused = 0;
        std::string cur;
        for (char ch : giveList + ",") {
            if (ch != ',') { cur.push_back(ch); continue; }
            if (cur.empty()) continue;
            // `LIST:ID` names the list, a bare `ID` means list 0. Op 49
            // `var.set.has_object`'s FIELD 0 is the list and field 1 the
            // object, and the lists are not interchangeable: the flat's lift
            // gate asks `has_object 1, 3, 20` - list ONE for the police card
            // - so a bag written only into list 0 never satisfies it, and the
            // cutscene behind that gate could not be reached at all.
            int list = 0;
            std::string idPart = cur;
            const auto colon = cur.find(':');
            if (colon != std::string::npos) {
                list = std::atoi(cur.substr(0, colon).c_str());
                idPart = cur.substr(colon + 1);
            }
            const int id = std::atoi(idPart.c_str());
            cur.clear();
            if (id <= 0) continue;
            // `debugPutObject` fills the FIRST free slot, so the ids land in
            // the order they are given - which is the reverse of what the
            // game's own `ObjectList_InsertFront` would do, and is fine for a
            // harness whose point is to have a bag at all.
            if (state.debugPutObject(list, id)) ++placed;
            else { ++refused; std::printf("--give: no free slot for object %d "
                                          "in list %d\n", id, list); }
        }
        std::printf("--give: %d object%s put in the named list%s, %d refused "
                    "(a harness write, not `inventory.add`)\n",
                    placed, placed == 1 ? "" : "s",
                    giveList.find(':') == std::string::npos ? " (0, carried)" : "",
                    refused);
    }
    if (!varList.empty()) {
        std::string cur;
        int wrote = 0;
        for (char ch : varList + ",") {
            if (ch != ',') { cur.push_back(ch); continue; }
            const auto eq = cur.find('=');
            if (eq != std::string::npos) {
                const int id = std::atoi(cur.substr(0, eq).c_str());
                const int v  = std::atoi(cur.substr(eq + 1).c_str());
                state.setVar(id, v);
                std::printf("--var: VARIABLES[%d] = %d\n", id, v);
                ++wrote;
            }
            cur.clear();
        }
        std::printf("--var: %d variable%s written straight into the DB "
                    "(a harness write, not a script)\n", wrote,
                    wrote == 1 ? "" : "s");
    }
}

// --bank-reject: every bank refused (DEBUG)
void PlayState::harnessBankReject() {
    auto& session = *session_;
    if (bankReject) {
        session.setBankReject(true);
        std::printf("DEBUG --bank-reject: every bank refused, the object stays in hand - "
                    "NOT the original's rule\n");
    }
}

// --scene-load, --zone-disable, --zone-enable: the opcodes by hand
void PlayState::harnessScriptForcing() {
    auto& session = *session_;
    // A HARNESS, and the narrowest one in this viewer: `zone.enable N`, the
    // opcode itself, on a zone the STORY would have enabled. Everything after
    // it is the game's own path - the player walks in, the zone fires its own
    // enter script, and that script is what runs `shoot.begin` and the
    // `shoot.actor.enter` / `.action` calls. AREA 141's 'Start Shoot' (2295)
    // is enabled by the Nout book cutscene, which a headless run cannot reach;
    // with it enabled the catacombs' ten spectres enter on ACTION 1 and
    // PATROL (`todo/shoot-patrol.md` 5a).
    // `--zone-disable N`: the mirror, for a start that skipped the script which
    // would have disabled it - the supermarket harness never runs AREA 231's
    // record 1, the airlock cutscene in, so its zone 3949 stays enabled from a
    // save made before the supermarket and walking OUT re-fires that cutscene.
    // `--scene-load A,S`: opcode 71, `scene.load`, and nothing else. Over an
    // area that is not resident it only RECORDS the scene in the DB, and
    // `Area_Load` brings it in when the area loads - which is how the story
    // puts SCENE 56 over the supermarket before the player walks in from the
    // airlock. `--scene-chunk` starts INSIDE the area instead, skipping the
    // airlock cutscene (AREA 231 record 1) the real path plays.
    for (const auto& [sa, ss] : sceneLoads) {
        session.sceneLoad(sa, ss);
        std::printf("--scene-load: SCENE %d recorded over AREA %d (the `scene.load` "
                    "opcode, nothing else)\n", ss, sa);
    }
    for (const int z : zoneDisable) {
        session.disableZoneById(z);
        std::printf("--zone-disable: ZONE %d disabled (the `zone.disable` opcode, nothing "
                    "else)\n", z);
    }
    for (const int z : zoneEnable) {
        session.enableZoneById(z);
        std::printf("--zone-enable: ZONE %d enabled (the `zone.enable` opcode, nothing "
                    "else) - walk into it and its own script runs\n", z);
    }
}

// --ride: mounted where he stands, without MDSLIDIN
void PlayState::harnessRide() {
    auto& session = *session_;
    // `--ride`: MOUNT him where he now stands. `MDSLIDIN`'s own gate is
    // ACTOR_STATE 6 plus a slider standing OPEN (its mode 3), and neither
    // exists here - this is the harness, and it says so.
    if (rideArg) {
        omk::SliderRide r;
        r.x = session.playerPos()[0];
        r.y = session.playerPos()[1];
        r.z = session.playerPos()[2];
        r.yaw = session.playerYaw();
        ride = r;
        std::printf("ride: mounted at %.0f %.0f %.0f facing %.0f - the "
                    "harness, NOT `MDSLIDIN` (which wants ACTOR_STATE 6 "
                    "and a slider in mode 3)\n", r.x, r.y, r.z, r.yaw);
    }
}

// --save-slot: a save written when the run ends, without the panel
void PlayState::harnessSaveSlot() {
    auto& session = *session_;
    // ------------------------------------------------- WRITING A SAVE
    //
    // `Game_WriteSave` (0x00408EF0), with `State_Save`'s snapshot half in
    // front of it (GAME_STATE 5a, 8b).  The order is the engine's: take the
    // snapshot the block does not otherwise keep, then the four copies into
    // the slot, then the whole file back.
    //
    // WHERE the scene comes from matters.  `State_Save` reads the LIVE
    // resident slot (`dword_69BC4C[4 * dword_69BC60]`), not the scene-per-area
    // table - and `State_Apply` copies the header back INTO that table before
    // `Area_Load` reads it, so the header is what a load believes.  Taking it
    // from the table here would make the port unable to express a divergence
    // the engine can.
    if (saveSlotArg >= 0) {
        state.setCurrentArea(static_cast<std::int16_t>(session.activeArea()));
        state.setCurrentScene(static_cast<std::int16_t>(
            session.residentSlot(session.activeSlot()).scene));
        state.setPlacement(session.playerPos(), session.playerYaw());

        omk::SaveSlot out;
        out.name = !saveNameArg.empty() ? saveNameArg
                 : (!loadedName.empty() ? loadedName : std::string("omk-play"));
        out.day  = state.clockDay();
        out.time = state.clock();
        out.state = state;

        // The picture the load panel draws beside the selected row: the back
        // buffer scaled into a 128 x 96 rect and repacked to X1R5G5B5
        // (GAME_STATE 8b).  `fb` is what the window was shown, so this is the
        // frame the player was looking at - which is what the engine's blit
        // takes too.
        const auto thumb = omk::thumbFromRgb565(fb.px, fb.w, fb.h);

        auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
        if (file.size() < omk::kSaveFileSize) {
            // `sub_4092A0`'s create arm - the settings, then 256 empty slots.
            file = omk::blankSaveFile(saveSettings ? *saveSettings
                                                   : omk::defaultSettingsBlock());
            std::printf("save: %s did not exist - created, %zu bytes\n",
                        savesPath.c_str(), file.size());
        }
        // ...and `Game_WriteSave`'s first copy: the settings over the head, on
        // EVERY slot save.  Saving a game saves the options (GAME_STATE 8a).
        if (saveSettings) omk::putSettings(file, *saveSettings);
        if (!omk::writeSaveSlot(file, saveSlotArg, out, thumb))
            std::fprintf(stderr, "save: slot %d is out of range\n", saveSlotArg);
        else if (omk::writeSaveFile(savesPath, file))
            std::printf("save: slot %d written to %s - '%s', %s %s, area %d "
                        "scene %d, standing at %.0f %.0f %.0f facing %.0f, "
                        "with a %dx%d picture\n",
                        saveSlotArg, savesPath.c_str(), out.name.c_str(),
                        omk::formatDate(out.day).c_str(),
                        omk::formatTime(out.time).c_str(),
                        state.currentArea(), state.currentScene(),
                        session.playerPos()[0], session.playerPos()[1],
                        session.playerPos()[2], session.playerYaw(),
                        omk::kThumbW, omk::kThumbH);
    }
}

// --anim-hold and --call: a held player, the sneak call, fired once
void PlayState::harnessHoldAndCall() {
    auto& session = *session_;
    // name, press again to bank it, another button to put it back
    // - is these four rows and nothing else.
    // `--sneak`: the same request the special move makes, once.
    // `--call N`: the sneak-call idiom, fired once. The screen
    // is requested the way a SCRIPT requests it (so it answers
    // itself) and the conversation started right after, which is
    // exactly `ui.open 0` / `dialog.start N`.
    if (animHoldHarness) {
        animHoldHarness = false;
        session.harnessHoldPlayer(true);
        std::printf("--anim-hold: the player is held (a harness "
                    "for `player.anim.hold`)\n");
    }
    if (callDialog >= 0 && !walk && playerScreen < 0) {
        std::printf("--call: ui.open 0 + dialog.start %d (a "
                    "harness for the sneak call, UI 3i)\n",
                    callDialog);
        playerScreen = kScreenVideophone;
        callHarness  = true;
        callPending  = callDialog;
        callDialog   = -1;
    }
}

// --shoot-end: shoot.end 1 at a frame
void PlayState::harnessShootEnd() {
    auto& session = *session_;
    // --shoot-end N: the harness's way OUT - `shoot.end 1` at frame
    // N, what op 81 does for a script, so the RETURN to adventure
    // mode (`todo/shoot-mode.md` 8.5e) can be driven headless.
    if (shootEndAt >= 0 && n >= shootEndAt && session.shootMode().active()) {
        shootEndAt = -1;
        session.shootEnd(1);
        std::printf("--shoot-end: shoot.end 1\n");
    }
}

// --board: put at the called slider's door and the action pressed
void PlayState::harnessBoard(float (&at)[3], float (&door)[3]) {
    auto& session = *session_;
    if (boardArg && !boarded && !boarding) {
        float feet[3] = {door[0], door[1] + player->cameraLift(), door[2]};
        const float yaw = static_cast<float>(
            std::atan2(at[0] - door[0], -(at[2] - door[2])) * 57.29577951308232);
        player->placeAt(feet, yaw);
        session.setPlayerPosition(player->pos(), yaw);
        boardPress = true;
        std::printf("harness: --board put him at the door point %.0f %.0f %.0f "
                    "facing the slider, and presses the action button\n",
                    feet[0], feet[1], feet[2]);
    }
}

// --fight: op 62's entry by hand
void PlayState::harnessFight() {
    // THE HARNESS, and it is one: the engine reaches a fight
    // through a script's op 62, and this fires the same entry
    // by hand once the world and the player exist, so a fight
    // can be watched without walking the story to it.
    if (fightArg >= 0 && player && !fightRun.active &&
        !fightRun.harnessFired) {
        fightRun.harnessFired = true;
        // ...and it PLACES him, which the engine does not:
        // `fight.begin` teleports nobody, because the script
        // that runs it has already staged the two face to
        // face. Two metres ahead of the player, inside the
        // AI's own closing distance, facing him. Forward is
        // (sin yaw, -cos yaw) - `rotateYaw`'s convention.
        for (auto& up : staged) {
            if (up->actor != fightArg) continue;
            const float rad = player->facing() * 3.14159265f / 180.0f;
            up->at[0] = player->pos()[0] + std::sin(rad) * 78.74f;
            up->at[1] = player->pos()[1];
            up->at[2] = player->pos()[2] - std::cos(rad) * 78.74f;
            up->facing = player->facing() + 180.0f;
            up->placed = true;
            std::printf("frame %ld: harness --fight places CHARACTERS %d "
                        "two metres in front of the player, at "
                        "%.0f %.0f %.0f facing %.0f (the engine stages "
                        "them with a script instead)\n",
                        n, fightArg, double(up->at[0]), double(up->at[1]),
                        double(up->at[2]), double(up->facing));
            break;
        }
        std::printf("frame %ld: harness --fight %d level %d - "
                    "standing in for a script's fight.begin\n",
                    n, fightArg, fightLevelArg);
        beginMelee(fightArg, fightLevelArg);
    }
}

// --shoot-health: property 1 written at shoot entry
void PlayState::harnessShootHealth(std::int32_t& hp) {
    // (the TEST HARNESS `--shoot-health N`: property 1 written as N
    // first, so the record, the gauge and the property agree as
    // a save carrying N would make them)
    if (shootHealth >= 0) {
        omk::writeActorProperty(
            state.rawMutable().subspan(
                static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
            1, shootHealth);
        hp = shootHealth;
        std::printf("frame %ld: SHOOT HEALTH - the test harness --shoot-health "
                    "writes property 1 = %d\n", n, shootHealth);
    }
}

// --fight-foe-at: the opponent started elsewhere
void PlayState::harnessFoeAt(const float *& foeAt) {
    // THE HARNESS, and it is one: the engine starts him where the script
    // left him. This moves him so a check can put a wall in his way - the
    // supermarket's real fight is short and never reaches one.
    if (foeAtSet) {
        fightRun.foe.x = foeAtXZ[0]; fightRun.foe.z = foeAtXZ[1];
        std::printf("fight: harness --fight-foe-at starts the opponent at %.0f %.0f "
                    "(the script left him at %.0f %.0f)\n", double(foeAtXZ[0]),
                    double(foeAtXZ[1]), double(foeAt[0]), double(foeAt[2]));
    }
}

// --fight-health: the player's Vie at Fight_Begin
void PlayState::harnessFightHealth(omk::FightStats& ps) {
    // THE HARNESS, and it is one: the engine reads property 1 as it is.
    if (fightHealth >= 0) {
        std::printf("fight.begin: harness --fight-health gives the player Vie %d "
                    "(his record says %d)\n", fightHealth, ps.vie);
        ps.vie = fightHealth;
    }
}

// --scx-play: scene objects started by handle, once
void PlayState::harnessScxPlay() {
    auto& session = *session_;
    if (!scxPlayed && !scxPlay.empty() && adventure && session.scene().loaded()) {
        scxPlayed = true;
        for (const int h : scxPlay) {
            omk::Call c;
            c.op = 58;
            c.fields = {static_cast<std::int16_t>(h), 0, 0};
            const int idx = session.sceneMutable().handle({c});
            std::printf("--scx-play: frame %ld  object handle %d -> program %d (a harness start)\n",
                        frames, h, idx);
        }
    }
}

// the frame's facts for the flicker catcher
void PlayState::harnessFlickerNote(std::size_t& runsDrawn, std::size_t& runsCulled, std::size_t& litBodies) {
    auto& session = *session_;
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
}

// --snaps: the framebuffer every N frames from the hand-over
void PlayState::harnessSnaps() {
    if (!snapsDir.empty() && handoverFrame >= 0 && ((n - handoverFrame) % snapEvery) == 0) {
        const std::string path = snapsDir + "/snap-" + std::to_string(n) + ".bin";
        if (omk::safeOutputPath(path)) {
            std::ofstream o(path, std::ios::binary);
            for (auto v : fb.px) {
                const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
                o.write(b2, 2);
            }
        }
    }
}

// The flicker catcher
void PlayState::screensFlicker() {
    [[maybe_unused]] omk::Renderer& world = *world_;   // read under OMK_VULKAN only
    // ---- THE FLICKER CATCHER -------------------------------------
    if (!flickerDir.empty()) {
        long lit = 0;
        for (std::uint16_t v : fb.px) if (v) ++lit;
        if (frameNote.empty()) frameNote = "2D only - the world was not drawn";
        auto write = [&](long fno, const std::vector<std::uint16_t>& px,
                         const std::string& note) {
            const std::string path = flickerDir + "/flick-" + std::to_string(flickEvent) +
                                     "-" + std::to_string(fno) + ".bin";
            if (!omk::safeOutputPath(path)) return;
            std::ofstream o(path, std::ios::binary);
            for (auto v : px) {
                const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
                o.write(b2, 2);
            }
            std::ofstream t(flickerDir + "/flick-" + std::to_string(flickEvent) + ".txt",
                            std::ios::app);
            t << "frame " << fno << "  " << note << "\n";
        };
        // The baseline is the MEDIAN of a window, not the previous frame:
        // the fault lasts up to five frames, so its neighbours are inside
        // it and a neighbour test cannot see it.
        long base = 0;
        if (flickLit.size() >= static_cast<std::size_t>(kFlickWindow)) {
            std::vector<long> w(flickLit.end() - kFlickWindow, flickLit.end());
            std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
            base = w[w.size() / 2];
        }
        if (flickAfter > 0) {                       // still writing an event
            write(n, fb.px, frameNote);
            --flickAfter;
        } else if (base > 0 && lit < base * 3 / 5 && n > flickQuietUntil) {
            flickEvent = n;
            flickQuietUntil = n + 60;               // one dump per two seconds
            std::printf("frame %ld: FLICKER - %ld lit against a median of %ld; "
                        "writing %d frames to %s/flick-%ld-*.bin\n",
                        n, lit, base, kFlickPre + 1 + kFlickPost,
                        flickerDir.c_str(), flickEvent);
            for (std::size_t k = 0; k < flickRing.size(); ++k)
                write(n - static_cast<long>(flickRing.size() - k), flickRing[k],
                      k < flickNote.size() ? flickNote[k] : std::string());
            write(n, fb.px, frameNote);
            flickAfter = kFlickPost;
        }
        flickLit.push_back(lit);
        if (flickLit.size() > static_cast<std::size_t>(kFlickWindow)) flickLit.erase(flickLit.begin());
        flickRing.push_back(fb.px);
        flickNote.push_back(frameNote);
        if (flickRing.size() > static_cast<std::size_t>(kFlickPre)) {
            flickRing.erase(flickRing.begin());
            flickNote.erase(flickNote.begin());
        }
        frameNote.clear();
    }
#if defined(OMK_VULKAN)
    if (gpuFrame && verifyGpuPresent) {
        // the GPU's picture against the CPU frame, as the upload would
        // expand it - every byte of every pixel
        static std::vector<unsigned char> gpuPic;
        static long compared = 0, differing = 0, badPixels = 0;
        if (omk::vulkanWorldPicture(&world, gpuVy, gpuVh, gpuPic) &&
            gpuPic.size() == fb.px.size() * 4) {
            const unsigned char* lut = omk::expand565Rgba();
            long diff = 0;
            for (std::size_t i = 0; i < fb.px.size(); ++i)
                if (std::memcmp(lut + 4 * static_cast<std::size_t>(fb.px[i]), &gpuPic[4 * i], 4) != 0) ++diff;
            ++compared;
            if (diff) {
                ++differing; badPixels += diff;
                // at once, so a run with only a few GPU frames still says so
                std::printf("gpu present verify: frame %ld DIFFERS in %ld pixels\n", n, diff);
            }
        }
        if (compared > 0 && compared % 30 == 0)
            std::printf("gpu present verify: frame %ld, %ld frames compared, %ld differ (%ld pixels)\n",
                        n, compared, differing, badPixels);
    }
#endif
    {
        static long gpuPresented = 0, framesSeen = 0;
        static std::map<std::string, long> keptBy;
        ++framesSeen;
        if (gpuFrame) ++gpuPresented;
        else ++keptBy[gpuKeep];
        if (omk::envSet("OMK_GPU_PRESENT_STATS") && framesSeen % 30 == 0) {
            std::printf("gpu present: frame %ld, %ld of %ld frames stayed on the GPU; on the CPU:",
                        n, gpuPresented, framesSeen);
            for (const auto& [why, count] : keptBy) std::printf(" %s %ld,", why.c_str(), count);
            std::printf("\n");
        }
        if (gpuFrame) ++phGpu; else ++phKept[gpuKeep];
    }
}

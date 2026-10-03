// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the live Session, the settings, the save, the sneak call.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupSession() {
    const auto& fs = *fs_;
    auto& comp = *comp_;
    // ---- THE LIVE SESSION -----------------------------------------------
    //
    // `boot()` runs the chain and reports; this RUNS it, frame by frame, with
    // a person at the keyboard. The difference is where the menu's answer
    // comes from: `attachUi` DERIVES one by walking the tree, which is right
    // for a headless check and wrong for a game. `answerUiFromPerson` parks
    // the script instead - `Game_HandleEvent` case 5 - and nothing releases it
    // until somebody presses a key.
    //
    // So the menu is not opened by this file. AREA 118's own startup script
    // reaches `ui.open(29, -1, -> variable 19)` and parks; the frontend asks
    // the Session which screen is waiting and opens THAT. A build that opened
    // screen 29 itself would still show a menu and would be a different
    // program.
    opcodes = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    // The ONE that cannot be worked around: without the operand lengths the
    // VM cannot even step an instruction, and a hand-written table is not an
    // option - CLAUDE.md records one being wrong three ways in an hour. It is
    // in the repo as `tables/vm_opcodes.json`.
    if (!opcodes.valid()) {
        std::fprintf(stderr,
            "no VM opcode table: looked for %s/vm_opcodes.json\n"
            "  this one is required - the world scripts cannot be decoded "
            "without the operand lengths.\n"
            "  pass the directory as the second argument, e.g. "
            "omk-play <gamedata> tables\n",
            tb.empty() ? "<no tables dir>" : tb.c_str());
        return 1;
    }
    state = omk::GameState::fromFile(fr + "/IAM/START");
    // THE SETTINGS, from the three sources in their own order: the engine's
    // defaults (`sub_41F4C0`), then the ini, then the save file's 3496-byte
    // header - which is what the options MENU last wrote, and so is later
    // than the ini and wins (`platform/settings.h`, GAME_STATE 8a). The
    // header is read from the same bytes the slot comes out of, because it
    // IS the head of that file.
    //
    // WHICH FILE, and it is one decision made once so the settings and the
    // slot cannot come from two different places: an explicit `--save` names
    // a file directly (the fixtures do), otherwise a `--slot` reads the saves
    // file - the writable one if it exists, else the shipped `IAM/GAMES`.
    if (!saveFile.empty()) {
        saveBytes = omk::DataFs::readPath(saveFile);
        saveFrom = saveFile;
    } else if (slotArg >= 0) {
        saveFrom = savesPath;
        saveBytes = omk::readSaveFile(savesPath, fr + "/IAM/GAMES", &saveFrom);
        if (saveBytes.empty())
            std::fprintf(stderr, "--slot: no save file at %s, and none in the "
                                 "game tree either\n", savesPath.c_str());
    }
    if (!saveBytes.empty())
        saveSettings = omk::readSettingsBlock(saveBytes);
    // ...AND WITH NEITHER, THE SAVES FILE'S HEADER ALL THE SAME. The game's
    // boot calls `SaveDir_Load` unconditionally (`03_win32.c`, after
    // `sub_41F4C0`'s defaults): the header of its saves file IS its settings,
    // whether or not a game is loaded. So a plain start takes what the options
    // menu last saved - the writable file, else the shipped `IAM/GAMES` - and
    // loads no slot (that is still `--slot`'s, through `saveBytes` above).
    if (!saveSettings && saveFile.empty() && slotArg < 0) {
        const auto head = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
        if (!head.empty()) saveSettings = omk::readSettingsBlock(head);
    }
    ini = configFile.empty() ? omk::OptionsFile{} : omk::loadOptionsFile(configFile);
    if (!configFile.empty() && !ini.loaded)
        std::fprintf(stderr, "%s: no such config file - using defaults\n", configFile.c_str());
    // not const: the options menu writes `settings.v` as the game's apply
    // hooks write `byte_90E180`, and what reads it later reads the new value
    settings = omk::resolveSettings(ini, saveSettings);
    // THE DISPLAY SIZE: `--res`, else row 2 as the settings resolved it - the
    // save header (`+12`/`+14`, what the menu last saved) over the ini's
    // `screen_x` / `screen_y` - else 800x600. A run that must be a given size
    // says so with `--res`; the checks do (`verify.py`'s viewer wrapper).
    if (!resFlag && settings.screen != omk::Settings::Source::Default &&
        settings.v.screenX >= 320 && settings.v.screenY >= 240 &&
        settings.v.screenX <= 8192 && settings.v.screenY <= 8192) {
        dispW = settings.v.screenX;
        dispH = settings.v.screenY;
    }
    // ...and the block carries the mode that RUNS, as `g_ScreenSize` does:
    // row 2 reads it back and a settings save writes it
    settings.v.screenX = dispW;
    settings.v.screenY = dispH;
    // THE SHOOTING RANGE'S HIGH SCORES, out of the save header's +724 - the
    // screen's own draw hook bases its rows there, so they travel with the
    // OPTIONS and not with a game. Empty in both shipped saves, which is a
    // table nobody has played into.
    if (saveSettings)
        for (std::size_t k = 0; k < highScores.size(); ++k)
            highScores[k] = {saveSettings->highScores[k].name,
                             saveSettings->highScores[k].ms};
    // A flag typed on the command line is the most recent word of all.
    if (!densityFlag) density = settings.v.streetActivity;
    // UNLIMITED DRAW DISTANCE (`todo/enhancements.md` 4). `--clip 0` says it
    // on the command line, `[Enhancements] clipdistance = 0` in the file, and
    // `--enhance-all` includes it. What it lifts is the visible-set walk's
    // distance test and the fog, whose range IS the clip distance - so with
    // no distance there is nothing to fade over and the fog goes off.
    unlimitedClip = (clipFlag && clipArg <= 0)
                            || (!clipFlag && (enhanceAll || settings.unlimitedDrawDistance));
    clipInches = unlimitedClip ? std::numeric_limits<double>::infinity()
                      : clipFlag ? static_cast<double>(clipArg) * omk::kInchesPerMetre
                                 : settings.clipInches();
    if (unlimitedClip)
        std::printf("clip: UNLIMITED - an ENHANCEMENT the original never had "
                    "(the option's five values stop at %d m); the fog goes with it, "
                    "its range being the clip distance's own\n", omk::kMaxOptionClipMetres);
    // one line the first frame that draws, so a run says what the option did
    clipReport = true;
    // THE FLICKER CATCHER (--flicker <dir>). A fault a player sees for one to
    // five frames cannot be screenshotted and cannot be found by choosing a
    // frame to render: a reader reported "a Kay'l with a black background" and
    // could not say where. So the viewer watches its own output - a frame far
    // darker than its neighbours is a body drawn on a set that is not there -
    // and writes the frames AROUND the dip, with a line of context each, so
    // the event arrives on disk instead of in a description.
    //
    // It is an INSTRUMENT and no part of the port: nothing here changes a
    // pixel, and with the flag absent none of it runs.
    flickAfter = 0;                                 // frames still to write
    flickEvent = -1, flickQuietUntil = -1;
    drawSky = skyFlag >= 0 ? skyFlag != 0 : settings.v.sky;
    // Options rows 5 and 7, with a flag beating the save the way --sky does.
    drawShadows = shadowFlag >= 0 ? shadowFlag != 0 : settings.v.shadows;
    // THREADED BODIES (`todo/vita-port.md` P4). ON by default since 2026-09-23:
    // the posing is bit-identical either way (`omk::Threads`' chunks are
    // disjoint and the merge is in index order), so this is not an enhancement
    // in the off-by-default sense - nothing the player sees changes - and the
    // one reason it started off, a Vita half that had never run on a device,
    // is gone: `omk_bench` on a real console said `threads: EXACT` with the
    // inline pass's own hash, 2.71x on three runners. `--no-thread-bodies`
    // puts it back on one core, for an A/B.
    threadBodies = threadFlag > 0;
    if (threadBodies)
        std::printf("bodies: posed over %d runners (the default; --no-thread-bodies for one); the frame is "
                    "bit-identical to one runner\n", omk::Threads::shared().runners());
    // not const: a renderer that cannot draw an enhancement refuses it, once
    // the renderer exists (below `world`)
    shadowQuality = enh(shadowQFlag, settings.shadowQuality,
                            omk::kMaxShadowQuality);
    shadowDetail = detailFlag >= 0 ? detailFlag : settings.v.levelOfDetail;
    // Row 7. Per pixel ALSO widens who receives: the engine lights the
    // procedural crowd and nothing else, and this lets every character.
    lighting = enh(lightingFlag, settings.lighting, omk::kMaxLighting);
    if (lighting > 0)
        std::printf("lighting: per pixel - an ENHANCEMENT the original never had "
                    "(it lights the crowd alone, per vertex); every character receives\n");
    // The enhancement: OFF unless --aa or [Enhancements] said otherwise.
    aaSamples = enh(aaFlag, settings.antiAliasing, omk::kMaxAntiAliasing);
    texFilter = enh(filterFlag, settings.textureFilter, omk::kMaxTextureFilter);
    texAniso = enh(anisoFlag, settings.anisotropy, omk::kMaxAnisotropy);
    // Supersampling is NOT under `--enhance-all` (settings.h, kMaxSupersample):
    // only `--ssaa` or `supersampling =` turn it on.
    ssaa = ssaaFlag >= 0 ? ssaaFlag : settings.supersample;
    // The RADAR (`ui/radar.h`): the game draws shoot mode's minimap only when
    // a script's op 146 has turned it on; `always` draws it in every shoot
    // phase whose area has a radar file.
    radarAlways = enh(radarFlag, settings.radarAlways ? 1 : 0, omk::kMaxRadar) > 0;
    // Row 11: the PACER's rate. Not the simulation's - that steps on the
    // measured delta whatever this is - and never a `--frames` run's, which
    // steps a fixed 1/30 and never reaches the pacer.
    frameRate = enh(frameRateFlag, settings.frameRate, omk::kMaxFrameRate);
    // Row 12, set once for every `composePose` that takes a float frame
    smoothAnim = enh(smoothAnimFlag, settings.smoothAnimation ? 1 : 0,
                                omk::kMaxAnimation) > 0;
    omk::setPoseSmoothing(smoothAnim);
    if (smoothAnim)
        std::printf("animation: smooth - an ENHANCEMENT: bodies are slerped between "
                    "two keys; the original truncates to one (`Anim_ApplyNodeFrame`'s _ftol)\n");
    if (frameRate != 30)
        std::printf("framerate: %d - an ENHANCEMENT: the port presents at 30 by default; "
                    "the original had no cap and stepped on 30/fps, as this does\n", frameRate);
    if (radarAlways)
        std::printf("radar: always - an ENHANCEMENT: the game shows shoot mode's minimap "
                    "only when a script turns it on, and in seven of its nine arenas only "
                    "for object 980, which nothing in the shipped game gives\n");
    // The INTERFACE's own, and the one enhancement here that is not the
    // Vulkan backend's: the 640x480 layer is composed on the CPU for both, so
    // a filtered stretch reaches the software renderer too.
    uiScaling = enh(uiScaleFlag, settings.uiScaling, omk::kMaxUiScaling);
    comp.setScaling(uiScaling);
    // TEXT SCALING (settings.h `textScaling`): the glyphs at the smaller of the
    // layout's two scales, so a line keeps its proportion to its box whatever
    // the display. Re-applied wherever the display size changes.
    textScaling = enh(textScaleFlag, settings.textScaling, omk::kMaxTextScaling);
    if (textScaling > 0)
        std::printf("text scaling: fit - an ENHANCEMENT: the original draws glyphs at their "
                    "native size whatever the display (`I2D_ScaleX/Y` move them, never "
                    "enlarge them); here they scale with the layout%s\n",
                    uiScaling > 0 ? ", filtered" : "");
    if (uiScaling > 0)
        std::printf("ui scaling: linear - an ENHANCEMENT the original never had "
                    "(DirectDraw's Blt takes one texel); it changes nothing at "
                    "640x480, where nothing stretches\n");
    if (enhanceAll || settings.enhanceAll)
        std::printf("enhancements: all on - %dx MSAA, %s filtering, anisotropy %d, "
                    "%s shadows, %s lighting, %s interface, %s draw distance. As high as each goes "
                    "unless a specific "
                    "setting said otherwise; none of it is what the original drew, and "
                    "the device reduces what it cannot meet. Supersampling is separate "
                    "(--ssaa N / supersampling = N): %dx\n",
                    aaSamples, omk::textureFilterName(texFilter), texAniso,
                    omk::shadowQualityName(shadowQuality), omk::lightingName(lighting),
                    omk::uiScalingName(uiScaling), unlimitedClip ? "unlimited" : "capped", ssaa);
    // The clip half of the line reads differently when it is unlimited:
    // "0 m = inf in" is arithmetic rather than a report.
    if (unlimitedClip)
        std::snprintf(clipText, sizeof clipText,
                      "clip UNLIMITED (enhancement), no fog, no distance cull");
    else
        std::snprintf(clipText, sizeof clipText,
                      "clip %d m (%s) = %.0f in, near/far split %.0f/%.0f",
                      clipFlag ? clipArg : settings.v.clipDistance,
                      clipFlag ? "flag" : omk::sourceName(settings.clipDistance),
                      clipInches, clipInches * 0.25, clipInches * 0.95);
    std::printf("settings: %s;"
                " crowd %d (%s); sky %d (%s), shadows %d (%s), detail %d (%s);"
                " aa %d (%s, enhancement), filter %s (%s%s),"
                " anisotropy %d (%s, enhancement), interface %s (%s, enhancement),"
                " text %s (%s, enhancement)\n",
                clipText,
                density, densityFlag ? "flag" : omk::sourceName(settings.streetActivity),
                drawSky ? 1 : 0, skyFlag >= 0 ? "flag" : omk::sourceName(settings.sky),
                settings.v.shadows ? 1 : 0, omk::sourceName(settings.shadows),
                settings.v.levelOfDetail, omk::sourceName(settings.levelOfDetail),
                aaSamples, aaFlag >= 0 ? "flag" : omk::sourceName(settings.antiAliasingSource),
                omk::textureFilterName(texFilter),
                filterFlag >= 0 ? "flag" : omk::sourceName(settings.textureFilterSource),
                texFilter == 2 ? ", enhancement" : "",
                texAniso, anisoFlag >= 0 ? "flag" : omk::sourceName(settings.anisotropySource),
                omk::uiScalingName(uiScaling),
                uiScaleFlag >= 0 ? "flag" : omk::sourceName(settings.uiScalingSource),
                omk::textScalingName(textScaling),
                textScaleFlag >= 0 ? "flag" : omk::sourceName(settings.textScalingSource));
    if (!ini.unknown.empty()) {
        std::printf("settings: %zu key(s) under [Preferences] the engine never reads:",
                    ini.unknown.size());
        for (const auto& k : ini.unknown) std::printf(" %s", k.c_str());
        std::printf("\n");
    }
    if (!saveBytes.empty()) {
        const int slotNo = slotArg >= 0 ? slotArg : 0;
        const auto slot = omk::readSaveSlot(saveBytes, slotNo);
        if (!slot) { std::fprintf(stderr, "%s: not a save file, or it has no slot %d\n",
                                  saveFrom.c_str(), slotNo); return 1; }
        // AN EMPTY SLOT IS NOT A SAVE.  `SaveDir_Build` skips a slot whose
        // name begins with a zero and the load panel never offers one, so
        // loading it would put the player in area 0 out of a zeroed DB and
        // read as a fault in the port.  The distinction worth keeping is
        // between a slot that was CLEARED - `SaveDir_ClearSlot` writes one
        // byte and leaves the DB on disk - and one that was never written,
        // where there is nothing behind the name either.
        if (slot->name.empty()) {
            bool anything = false;
            for (const auto b : slot->state.raw())
                if (b != std::byte{0}) { anything = true; break; }
            if (!anything) {
                std::fprintf(stderr, "%s: slot %d is empty. Occupied slots:",
                             saveFrom.c_str(), slotNo);
                int shown = 0;
                for (int k = 0; k < static_cast<int>(omk::kSaveSlots); ++k) {
                    const auto o = omk::readSaveSlot(saveBytes, k);
                    if (!o || o->name.empty()) continue;
                    if (shown++ < 12) std::fprintf(stderr, " %d (%s)", k, o->name.c_str());
                }
                std::fprintf(stderr, shown ? "\n" : " none\n");
                return 1;
            }
            std::printf("save: slot %d's name is empty - `SaveDir_ClearSlot` "
                        "writes one byte and leaves the rest, so this slot was "
                        "DELETED and its game is still on disk. Loading it\n",
                        slotNo);
        }
        state = slot->state;
        // THE CLOCK COMES FROM THE SLOT, and this used only to print it.
        //
        // `gamestate.h`: the clock and the timer are engine globals and NOT
        // part of the 8192-byte image, so restoring the DB restores
        // everything EXCEPT the date and time - and the slot header is the
        // only place they exist. Loading a save therefore left the game at
        // day 0, 00:00:00 while the loader printed the save's real date one
        // line above.
        //
        // Nothing noticed because nothing DREW the clock. The sneak's own
        // clock row (`sub_0049E090`) is the first thing in this port to show
        // it, and it showed "1 Aqed 7216 - 0:00:00" against a save the same
        // function had just printed as a different date.
        harnessNewWorld();
        state.setClockDay(slot->day);
        state.setClock(slot->time);
        // `State_Apply`'s FIRST use of the header pair, and the port had only
        // ever read its second:
        //
        //     u16(u32(g_GameDB, 12), 2 * i16(g_GameDB, 1414)) = u16(g_GameDB, 1416);
        //     result = Area_Load(i16(g_GameDB, 1414), 0);
        //
        // `+12` is the scene-per-area table, so the header's scene is copied
        // INTO it for the header's area before the area is loaded - and
        // `Area_Load` reads exactly that entry at its top (`v35 = i16(u32(
        // g_GameDB, 12), 2 * a1)`) and ends `Scene_Load(slot, v35)`.  So the
        // header is what decides which SCENE goes over the area a save
        // resumes in, and the table merely carries it there.
        //
        // In all four save slots this tree has the two already agree, which
        // is why nothing has broken; they can disagree, because they have
        // different writers - the table is written by opcode 71 `scene.load`
        // and the header by `State_Save` out of the live resident slot - and
        // when they do, the engine's answer is the HEADER's.
        // `verify.py: engine: save write` measures the agreement so a
        // divergence cannot pass unnoticed.
        state.setSceneOfArea(state.currentArea(), state.currentScene());
        // WHERE THE SAVE SAYS HE WAS.  `State_Apply` converts +44..+56 back to
        // world units and stands the player there; the port had never read
        // those four fields at all, so a loaded save came up wherever the
        // harness put him.  Kept here and applied at the hand-over, because
        // that is when there is a player to place.
        state.placementWorld(savedAt, savedYaw);
        // ...and `State_Apply`'s own guard on it.  The whole spawn half -
        // the model, the bank list, `Player_SetActor` and the placement with
        // them - sits inside `if (u16(g_PlayerRecord, 272) != 0xFFFF)`, so a
        // block whose player record names no actor has a placement that means
        // nothing and the engine never applies it.  `IAM\START` is exactly
        // that block, which is why a new game stands wherever the opening
        // puts him.
        loadedName = slot->name;
        haveSavedPlacement = state.playerActorId() != -1;
        if (!haveSavedPlacement)
            std::printf("save: slot %d's player record names no actor (+272 "
                        "is 0xFFFF), so its placement is not applied - which "
                        "is `State_Apply`'s own guard\n", slotNo);
        // The row the load panel would draw for this slot, which is the
        // directory's four fields in the order the original puts them
        // (GAME_STATE 8): the character name, the date, the time - over a
        // heading that is the profile name.
        std::printf("save: the load panel's row for it is \"%s - %s - %s\" "
                    "under \"Joueur : %s\"\n",
                    state.characterName().c_str(),
                    omk::formatDate(slot->day).c_str(),
                    omk::formatTime(slot->time).c_str(), slot->name.c_str());
        std::printf("save: slot %d '%s', %s %s, area %d scene %d, standing at "
                    "%.0f %.0f %.0f facing %.0f\n", slotNo, slot->name.c_str(),
                    omk::formatDate(slot->day).c_str(), omk::formatTime(slot->time).c_str(),
                    state.currentArea(), state.currentScene(),
                    savedAt[0], savedAt[1], savedAt[2], savedYaw);
    }
    // Asking for a SLOT is asking to resume: adventure mode in the save's own
    // area, at its own placement, which is what `Game_LoadSave` does.  A bare
    // `--save FILE` keeps its old meaning - the DB as a starting state, the
    // intro still playing - because every street-start recipe in the tree is
    // written that way and pairs it with `--area`.
    // ...and a LOAD sets it too, below: loading a save is resuming, so the
    // hand-over should not wait on the scene's beats any more than `--slot`
    // does. Not const for that reason.
    forceAdventure = areaArg >= 0 || slotArg >= 0;
    // THE INVENTORY, out of the game data: `IAM\OBJECT`'s 1002 records and
    // `IAM\GLOBAL +12`'s eleven combination recipes. `script/inventory.h` was
    // written, checked and never consumed by anything that runs - the sneak
    // is what the channel exists for, so this is where it is loaded.
    harnessStateWrites();
    objectRecords = omk::loadObjects(fs);
    globalFile = fs.read("IAM/GLOBAL");
    recipes = omk::globalRecipes(globalFile);
    inv_.emplace(objectRecords, recipes);
    // `GLOBAL +16` - the sneak's slider destinations, 39 of them.
    destinations = omk::globalDestinations(globalFile);
    sliderTold = false;
    // The memo the body box was fed this frame, reported AFTER the draw with
    // the lines the composer actually laid out - a line printed beside the
    // fill says what was intended, not what was drawn, and a mutation that
    // cut the text off from the composer passed exactly that way.
    if (objectRecords.empty())
        std::printf("no IAM/OBJECT - the sneak's inventory page will be "
                    "empty\n");
    auto& session = session_.emplace(fr + "/IAM", state, opcodes);
    harnessBankReject();
    if (!session.loadAnnounceMap(tb + "/vm_announce.json"))
        std::printf("tables: no vm_announce.json - the log will name fewer "
                    "operands\n");
    // A person answers the screens only when there ARE screens to draw.
    session.answerUiFromPerson(w.valid());
    // There is a frame clock here, so `camera.set.wait` can do what the
    // handler does: hold the script for the length of the move it started.
    // Without it AREA 118's six intro cameras and its `area.goto` all happen
    // in one frame and the introduction is never seen.
    session.setCameraWait(true);
    // And the waiting `scx.play*` variants, which is the beat before the
    // intro's conversation: AREA 118 shows Kay'l and starts his animation with
    // `scx.play.actor.wait`, holding the script while the camera travels.
    session.setObjectWait(true);
    // And a conversation takes as long as its own voice does. Without this the
    // frontend closed each one on the next frame - a labelled stand-in that
    // got the intro's SHAPE wrong, because the grid tunnel AREA 118 cuts to
    // AFTER `dialog.start 272` then arrived two seconds after the menu instead
    // of three minutes. `src/script/dialogue.h` carries the reasoning; the
    // decision is on the ported side and this file only plays the audio.
    session.attachDialogue(fr + "/MORPH");
    // STREET LIFE: the pedestrians of an area naming a circuit spawn at its
    // load, at the density the options menu will one day hand in.
    session.setStreetActivity(density);
    session.setMusicOption(settings.v.volumeMusic);   // options row 11
    session.setDataRoot(fr);   // IAM\OBJECT for the kind ladder, crowd or not
    if (!noCrowd) session.loadTraffic(fr);
    // A movie chain is the intro's; a street start skips it.
    if (forceAdventure) playMovies = false;
    startArea = areaArg >= 0 ? areaArg : state.currentArea();
    if (startArea < 0) { std::fprintf(stderr, "IAM/START names no area\n"); return 1; }
    session.loadArea(startArea);
    // ...with the SET it names, because an area whose chunk did not arrive
    // (an unreadable IAM\AREA) still "loads" - with no script, no set and no
    // scene, and a run that never starts (a console, 2026-09-18)
    std::printf("session: area %d loaded, set '%s', waiting for its script\n",
                startArea, session.setName().c_str());
    harnessScriptForcing();
    if (sceneChunk >= 0) {
        session.sceneLoad(startArea, sceneChunk);
        std::printf("--scene-chunk: SCENE %d over AREA %d - its startup script "
                    "runs, which is what SHOWS an area's props\n",
                    sceneChunk, startArea);
    }
    if (forceAdventure) {
        const auto& rs = session.residentSlot(session.activeSlot());
        for (const auto& ad : rs.addresses)
            std::printf("address %d at %.0f %.0f %.0f yaw %.0f\n", ad.id, ad.pos[0], ad.pos[1], ad.pos[2], ad.yaw);
        // THE PRECEDENCE, and the save's own placement is now in it: an
        // explicit `--stand` or `--address` is the reader's word and wins;
        // otherwise a save that was made IN THIS AREA says where he stood;
        // otherwise the area's first ADDRESSES record, which is the harness
        // default and was previously the only one.  The area test matters -
        // `--save X --area 0` deliberately drops a save's player record into
        // a city he was never in, and his apartment coordinates mean nothing
        // there.
        const bool useSaved = haveSavedPlacement && !haveStand && addressArg < 0 &&
                              state.currentArea() == startArea;
        if (useSaved) {
            session.setPlayerPosition(savedAt, savedYaw);
            std::printf("save: the player stands where the save left him, "
                        "%.0f %.0f %.0f facing %.0f\n",
                        savedAt[0], savedAt[1], savedAt[2], savedYaw);
        }
        if (!useSaved && addressArg < 0 && !rs.addresses.empty())
            addressArg = rs.addresses.front().id;
        if (addressArg < 0 && haveStand) {
            // an area with no ADDRESSES table (the Impasse airlock, 142):
            // `--stand` is the only placement there is, so it is the one
            session.setPlayerPosition(standAt, standAt[3]);
            std::printf("street start: no address in area %d - --stand places the player at "
                        "%.0f %.0f %.0f facing %.0f\n", startArea, standAt[0], standAt[1], standAt[2], standAt[3]);
        }
        if (addressArg >= 0) {
            if (session.placeActorAt(addressArg))
                std::printf("street start: the player at address %d, %.0f %.0f %.0f facing %.0f\n",
                            addressArg, session.playerPos()[0], session.playerPos()[1],
                            session.playerPos()[2], session.playerYaw());
            else
                std::printf("street start: address %d is not in area %d\n", addressArg, startArea);
        }
        harnessRide();
        // ...and the camera a hand-over ends on: `Camera Player` (0), the
        // follow preset, which the intro's scripts request and this has to.
        session.requestCamera(0, 0);
        const auto& pd = session.sliders();
        std::string models;
        for (const auto& m : pd.models()) { if (!models.empty()) models += ","; models += m.name; }
        std::printf("street life: circuit %s, %d walkers at density %d (%s)\n",
                    rs.opt.empty() ? "none" : rs.opt.c_str(), pd.liveCount(), pd.streetActivity(),
                    models.empty() ? "no models" : models.c_str());
    }
    // The area's own `.SCX`, so `scx.play*` has objects to start - and so the
    // WAITING variants (46, 58, 60) have something to wait ON. Without it
    // AREA 118's `character.show 310` + `scx.play.actor.wait 310, 1` starts
    // nothing and the script runs straight into `dialog.start`, which is the
    // ~5 second beat before the conversation going missing.
    // The scene's `.SFX` too - `AREA +97` names both, and starting an object
    // fires the set pieces keyed to it.
    if (session.loadScene(fr + "/SCPTDATA", omk::ChunkKind::Area, startArea))
    {
        std::string sfxName = session.scene().file();
        const auto dot = sfxName.rfind('.');
        if (dot != std::string::npos) sfxName = sfxName.substr(0, dot) + ".sfx";
        session.sceneMutable().attachSfx(fr + "/SCPTDATA", sfxName);
        const auto& sf = session.scene().sfx();
        std::printf("scene: %s resident, %zu objects; %s: %zu effects, "
                    "%zu set pieces (%d of them keyed so this trigger cannot "
                    "reach them)\n",
                    session.scene().file().c_str(),
                    session.scene().scene().scene().objects.size(),
                    sfxName.c_str(), sf.effects.size(), sf.pieces.size(),
                    session.scene().standingPieces());
    }
    else
        std::printf("scene: area %d names no .SCX\n", startArea);


    // The screen currently on the player's hands, if any. `walk` is only
    // constructed once a script has actually asked for a screen - or, since
    // the sneak, once the PLAYER has.
    openScreen = -1, conversations = 0, lastArea = -1;
    OMK_HEAPCHECK("before sneak call");
    pendingLoadSlot = -1;       // `dword_4C09B4`
    quitRequested = false;      // `dword_4E6C9C`, the pause screen's Oui
    exitProgram = false;        // the start menu's Oui - WM_QUIT
    // `dword_670BF0`: a sneak CALL is up. Set by the videophone's open arm,
    // cleared by the first close attempt.
    videophoneCall = false;
    videophoneSpoke = false;   // a line or a voice-over has played
    callHarness = false;       // `--call` opened this one
    callPending = -1;          // ...and the conversation it owes
    // THE LOADING SEQUENCE IS NOT WIRED HERE, and a first version of it
    // was. `Charger` answers **0**, and AREA 118's parked startup script
    // has an arm for exactly that: the Grid fly-through - cameras
    // 2152/2153/2154/2158 over `scx.play 20` with a `media.play 753` - after
    // which the script ends. So the engine covers a load with its own
    // script, and the port only has to answer the right number.
    // The sneak's three turning previews. Loaded once - the engine loads them
    // in the sneak's OPEN callback and frees them in its close, which for a
    // viewer that opens the device repeatedly is the same three files each
    // time.
    if (const int n = uiModels.load(fs))
        std::printf("sneak: %d of 3 preview models loaded (%s...)\n",
                    n, uiModels.name(0).c_str());
    // WHO asked for the open screen, because the two ends differ. `ui.open`
    // parks its caller at status 6 and every close path posts event 5 with
    // the answer, so leaving IS an answer and the script resumes. The sneak
    // has no caller at all: `sub_0046ADF0` calls `UI_OpenScreen(9, -1, ...)`
    // and the `-1` is the waiting-context argument, so `dword_930744` is
    // never written and there is nothing to resume. Answering the Session for
    // it would release whatever script happened to be parked.
    screenFromScript = true;
    // The input word as it stood when the current screen opened - see the
    // edge gate below. Cleared once those bits are released.
    screenOpenBits = 0;
    // A screen the PLAYER asked for this frame, before it is opened below -
    // so the open, its sounds and its bookkeeping stay in one place.
    playerScreen = -1;
    // The world's action button is spent until it is RELEASED - see the
    // one-activation-per-press gate below. Cleared where `bits` is taken, so
    // a frame that never reaches the gate cannot leave it stale.
    actionSpent = false;
    // The sneak's inventory ROWS - item address -> what that row shows. The
    // nine slots of list 0x004DE6F0 ship `+28` as -1 and are never bound,
    // because their text is the carried object list read through the channel
    // (`Game_HandleEvent` 29 and 33), not a string in `IAM\Sneak`.
    // THE ADDRESS MAP, watched. A screen whose script OPENS A PLACE is the whole
    // point of reading it - a reader: *"some scenes can be triggered only if an
    // entry on a terminal has been read"* - and ops 87/88 write a 791-bit map
    // that nothing announced. Kay'l's terminal enables ADDRESSES 33, 'Anekbah -
    // Bar Zone 52', when his fourth dossier has been read, and the memo that
    // goes with it names the mission. Watched per frame and said once per
    // change, so a play log shows what a screen actually unlocked.
    // The row widgets `sub_42AAE0` switches off - past the object count, so
    // tag -1 and `0x40000001` set. Without it every one of the nine rows
    // draws its fill and the page is striped.
    sneakTold = 0;             // one line per run, not one per frame
    replySel = 0;            // which reply the player is on
    actionTold = 0;          // one line for a press that reaches nothing
    // THE ACTION BUTTON IS AN EDGE (omk-play 74). Bit 0x10 arrives as a LEVEL
    // - the world's repeat mask is 0, so a held key is set every frame - and
    // this remembers the previous frame's so the press fires once. See the
    // long note at the dispatch for why the engine needs no such variable.
    // THE PRESS BITS ARE EDGES (omk-play 74). In the world the repeat mask is
    // 0, so every bit arrives as a LEVEL and a held key is set every frame -
    // right for a walk, wrong for anything that COUNTS presses. This is the
    // previous frame's word; `edgeBits` below is the difference.
    prevBits = 0;
    // `tab_special_move[]` as a TABLE rather than a string compare: a fired
    // move resolves to its ROW - the index the engine dispatches on and the
    // handler address it calls - so an unknown name comes back nullptr
    // instead of falling off the end of an if-chain. The shipped `.CTL` files
    // use 54 distinct names against the table's 66 rows.
    specialMoves = omk::SpecialMoves::loadJson(tb.empty() ? std::string() : tb + "/special_moves.json");
    // The sneak's CITY MAP tables - the four map rectangles and the fifteen
    // place overrides, both `.data` in the executable (`ui/citymap.h`).
    cityMaps = omk::CityMaps::loadJson(tb.empty() ? std::string() : tb + "/city_maps.json");
    if (!cityMaps.valid())
        std::printf("sneak map: tables/city_maps.json not read - `Lire plan` will "
                    "bounce back, because no city can be matched\n");
    if (!specialMoves.valid())
        std::printf("special moves: tables/special_moves.json not read - the take will "
                    "still work, but a fired move cannot name its row\n");
    // THE WEAPON TABLES `Shoot_InitWeapon` picks a row from - the rate, the
    // projectile's speed and what it deals (`actor/shootfire.h`).
    shootWeapons = omk::ShootWeaponTable::loadJson(tb.empty() ? std::string() : tb + "/shoot_weapons.json");
    if (!shootWeapons.loaded())
        std::printf("shoot weapons: tables/shoot_weapons.json not read - shoot mode "
                    "will aim and never fire\n");
    takeCandidate = -1;      // `dword_53AF6C`, MDACTION's pick
    // Which HEIGHT the take was, kept from MDACTION so the put-back can match
    // it. The engine keeps the same thing in `dword_53AE5C` - `(ret == 2) ? 3
    // : 0`, which `sub_46B530` turns back into a group (omk-play 69).
    takeWasLow = true;
    heldInHand = -1;         // the object drawn on the left hand: from MDGETOBJ to the release
    // The spoken line's SCROLL, in pixels, and the overflow it is clamped to -
    // `dword_6A52C0` and `dword_53AE24`. One pixel a tick while held, which is
    // what `Dialog_TickUI` does with input bits 4 (up) and 8 (down).
    lineScroll = 0, lineOverflow = 0;
    menuShown = false;
    lastDlgCam = -2;
    // The absolute world cameras the script has set, as rays: during the
    // beat before a conversation they are what aims at the character.
    lastRayCam = -2;
    // THE CAMERA EDITING - camera mode 13. Every `scx.play*` handler ends by
    // asking `ScriptObject_HasCamEditing` and, when the object has a chunk-10
    // editing linked, requests mode 13 with the call's last field as the
    // travel (`SceneRunner::ActiveEditing` quotes the assembly). Mode 13 is
    // "follow the scene's active camera": the camera tick copies eye, target,
    // fov and roll out of `dword_9103D4` every frame, which is what
    // `Script_PlayScript` sampled from the editing at the OBJECT'S clock. It
    // supersedes the mode-12 world camera `camera.set` chose for as long as
    // the editing runs; when nothing sets an active camera and the mode is
    // still 13, the frame loop requests mode 0 with travel 0 (05_sys.c 2140)
    // - a cut back. The Session knows which editing is driving and what it
    // says; the travel FROM the previous camera is here, because this is
    // where the previous camera is: the one last drawn.
    editingShown = -1;                  // the announced editing's program
    haveLastDrawn = false;              // a 3D camera has been drawn
    lastFov = 75.0f;
    // (struct CtlSpriteInst: `backends/sdl/playtypes.h`, todo/play-split.md)
    ctlFxState = -1; ctlFxFrame = -1.0f;
    // ...and the MELEE OPPONENT's, from his own channel (`todo/fight-mode.md`
    // 15.10): `Cef_TickEffects` runs on every channel the engine ticks, and
    // `Actor_TickPlayerAndOpponent` ticks two.
    foeFxState = -1; foeFxFrame = -1.0f;
    foeSpritesDrawn = 0;       // particle-frames placed on his bones
    foeSpritesPooled = 0;      // ...of which the sprite had a texture slot
    takeCam = false;            // `C+12 == 1`: the mode-1 camera is live
    takeCamPhase = 0;           // 1 travelling in, 2 holding, 3 travelling back
    takeCamClock = 0.0f;        // frames since the request
    takeCamFromFov = 75.0f;
    // preset 1: 62 cm to his side, 75 cm above the pelvis, 12 cm back, looking
    // 12 cm up and 50 cm ahead of him. In the mode-0 convention the follow
    // camera uses (`point = subject - R(yaw) * offset`, mode 0 = 3 m behind).
    // ...and the same machinery serves any PLAYER-subject preset asked for
    // with a travel: `Camera_Request(mode, block)` with `block+24` the
    // frames. Preset 17 is what `sub_4570F0` asks for when the ride ends -
    // eye (-39.3701, 78.7402, 0) = 1.00 m and 2.00 m, target the actor,
    // subjects 0 and 0, over `dword_930818 = 60.0` frames. The take's
    // preset 1 was the only one wired, as constants.
    takeCamEye[0] = kTakeCamEye[0], takeCamEye[1] = kTakeCamEye[1], takeCamEye[2] = kTakeCamEye[2];
    takeCamAt[0] = kTakeCamAt[0], takeCamAt[1] = kTakeCamAt[1], takeCamAt[2] = kTakeCamAt[2];
    takeCamFov = kTakeCamFov;
    takeCamTravel = kTakeCamTravel;
    // THE FALL CAMERAS, `sub_414DE0(actor, mode, flag)` (`todo/falls.md` 1): a
    // player-subject request gated on the mode already up. 18 and 19 are the
    // overhead presets (eye 3 m above him, fov 75); 16 and 0 travel back to the
    // follow camera, the take's own way home. `fallCamMode` is `C+12` for these
    // four; the `C+140` test on 18 is not modelled (untraced) and reads 0.
    fallCamMode = 0;
    fallBanded = false;   // `+1304` set this fall (1, 3 or 4) - once per fall
    lastRoll = 0.0f;              // the camera ROLL, blended like the fov
    // THE CAMERA HOLDS WHEN AN EDITING ENDS, and the fall-back this used to do
    // is a PREFERENCE that ships off. `Game_Frame` (05_sys.c 2144) requests
    // mode 0 on the player only under
    //
    //     else if (Camera_GetMode(C) == 13 && byte_910322 && g_PlayerActorRec)
    //
    // and `byte_910322` is the `[Preferences]` key `autocameraplayer`, read
    // with a default of "0" (Runtime.exe.asm:23252; the only other write in
    // the image is the defaults block that zeroes it, so nothing else can turn
    // it on). With it off the branch never runs, and the camera tick's own
    // mode-13 arm is `if (dword_9103D4) { copy eye/at/fov/roll }` (sub_417CF0,
    // 04_sys.c 3701): a null active camera copies NOTHING, so the block keeps
    // the values it had. The view therefore FREEZES on the editing's last
    // frame until something else requests a camera.
    //
    // Measured before this landed, on the intro path: the Impasse's eight
    // gaps were 0 of 480000 pixels lit - a black frame after every beat and a
    // 73-frame black stretch mid-cutscene - because the Session still held
    // camera 2158, AREA 118's, from the area the player had just left
    // (next-tasks 5).
    musicPaused = false;          // the pause screen suspends the sound
    holdEditCam = false;          // mode 13 with no active camera: hold
    heldUnderRequests = 0;  // the Session's request count when the hold began
    editFromKnown = false;              // ...and it was captured for the travel
    editFromFov = 75.0f;
    editFromRoll = 0.0f;
    rollTold = false;
    fxSpriteWas = -2;

    OMK_HEAPCHECK("before adventure mode");
    return -1;
}

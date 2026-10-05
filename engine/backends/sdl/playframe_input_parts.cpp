// SPDX-License-Identifier: GPL-3.0-or-later
// THE INPUT PHASE: the pump, the pause, the game's tick, scripted motion, the sound effects.
// Parts of `PlayState::phaseInput`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

const std::array<float, 3> * PlayState::motionAtFind(const std::string& name) {
    for (const auto& kv : motionAt) if (kv.first == name) return &kv.second;
    return nullptr;
}

int PlayState::inputPump() {
    OMK_ZONE("input: pump");   // the profiler (todo/debug-tools.md 6)
    auto& comp = *comp_;
    bool done = false;
    do {
        phTop = phaseNow();
        phRb0 = phRb1 = -1.0;
        phMarks.clear();
        phMarks.emplace_back("top", phTop);
        profMarkT = omk::prof::now();
        bool pumpOk = true;
        spanned("pump", [&] { pumpOk = front.pump(host); });
        mark("pump");
        if (!pumpOk || exitProgram) break;
        // ---- OPTIONS ROW 2: A NEW DISPLAY SIZE, between frames -----------
        //
        // `Opt_ApplyResolution` (0x0048FA50) stops the menu cloud, has
        // `sub_43AC70` tear down and rebuild the device at the new mode, and
        // starts the cloud again. Here the framebuffer, the interface's scale
        // and the 3D target are remade at the new size, and the window follows
        // it (fullscreen keeps the desktop and scales). The Vulkan swapchain is
        // sized once, at start, so that backend takes it at the next start.
        if (pendingDisplay.first > 0) {
            const auto [nw, nh] = pendingDisplay;
            pendingDisplay = {0, 0};
            bool ok = true;
            gpuResize(nw, nh, ok);
            if (!ok) {
                std::printf("options: resolution %dx%d - the Vulkan target could not be "
                            "remade; --res %dx%d next time\n", nw, nh, nw, nh);
            } else {
                dispW = nw; dispH = nh;
                fb = omk::Surface(dispW, dispH, 0);
                comp.setDisplay(dispW, dispH);
                applyTextScale(dispW, dispH);
                if (glRen) glRen->init(dispW, dispH);
                else if (!vkRen && !worldVk && worldReady) worldSw.init(dispW, dispH);
                front.resize(dispW, dispH);
                std::printf("options: resolution %dx%d - framebuffer, interface scale "
                            "and 3D target remade\n", dispW, dispH);
            }
        }
        // ---- one line when the mouse first moves in shoot mode -----------
        //
        // Placed HERE, before any gate, so it can tell the three links apart:
        // whether the frontend reports motion at all, whether the shoot
        // camera is still live, and whether the aim's own block is reached.
        // The first version of this sat inside `if (adventure)` and could not
        // distinguish "no motion" from "never got there", which is exactly
        // the ambiguity that makes a diagnostic worthless.
        if (shootMode && (host.mouseDX != 0.0f || host.mouseDY != 0.0f)) {
            static long mouseTold = -1;
            if (mouseTold < 0 || n - mouseTold > 120) {
                mouseTold = n;
                std::printf("frame %ld: MOUSE dx %.1f dy %.1f - shootMode %d, "
                            "cameraLive %d, pitch %.1f\n", n, host.mouseDX,
                            host.mouseDY, int(shootMode), int(shootCameraLive),
                            shootPitch);
            }
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}

// ESC opens the pause screen; the last screen's close flushes the input
void PlayState::inputPause() {
    OMK_ZONE("input: pause");   // the profiler (todo/debug-tools.md 6)
    auto& in = *in_;
    auto& session = *session_;
    // ---- ESC OPENS THE PAUSE SCREEN (next-tasks 3) -------------------
    //
    // It is not a binding, and `Game_Frame` never sees it. `Game_RunLoop`
    // (0x00439310) polls it itself, one line before the frame:
    //
    //     if ((((uint16_t)GetAsyncKeyState(27) >> 8) & 0x80) != 0
    //         && !dword_4E9728)
    //         UI_LoadScreen(31, -1, -1);
    //     Game_Frame(dword_4C5944, word_90EF2E);
    //
    // Three things follow, and all three are the engine's:
    //
    //  * VK_ESCAPE is read LEVEL-triggered - `GetAsyncKeyState`'s bit 15
    //    is "currently down", not an edge. Nothing debounces it except
    //    the guard;
    //  * the guard is `dword_4E9728`, the PAUSE FLAG, and it has exactly
    //    two writes in the image: screen 31's open callback (0x004ADDB0)
    //    sets it, its close (0x004ADEB0) clears it. So ESC held opens the
    //    screen once and cannot open it twice, and ESC held THROUGH the
    //    close reopens it at once - which is the same shape as the held
    //    action button of next-tasks 1, and is what the original does;
    //  * it is `UI_LoadScreen`, not `UI_OpenScreen`, so no answer
    //    variable is written and no script is parked on it. The port
    //    therefore asks for it the way the PLAYER asks for the sneak,
    //    not the way a script asks.
    //
    // AND `UI_LoadScreen` REFUSES IT OVER TWO SCREENS BY NAME. Its slot
    // scan opens
    //
    //     if (slot->screen == a1) return 1;              // already up
    //     ...
    //     if (v5 || a1 == 31 && UI_TestScreenFlag(slot, 0x20000400))
    //         return 1;                                  // refused
    //
    // and 0x20000400 is set on exactly two screens, `OMK START MENU` and
    // `SAVE GAME` (`tables/ui.json`, flags 0x20000400). So ESC at the
    // start menu or over the save panel does NOTHING - which is the
    // guard this port needs most, since the boot parks on screen 29.
    // (`docs/UI.md` 2 read that flag the other way round - "opening
    // either fires screen 31's open callback" - and the branch says the
    // opposite: it is what makes 31 decline. Corrected there.)
    //
    // ONE DEPARTURE, and it is this frontend's rather than a reading:
    // the engine has THREE slots and would put the pause screen up over
    // a screen that does not carry that flag - the sneak, say - while
    // `omk-play` holds one `walk`. So any screen already up refuses the
    // pause here, which is the engine's answer for two screens and a
    // limitation of this frontend for the rest. Nothing here models the
    // three slots.
    //
    // This used to `break`, ending the run: the viewer quit on the one
    // key everybody presses, so a reader could not stop to look at
    // anything. Quitting is now what the screen's own `Quitter le jeu`
    // does, behind its Oui/Non confirm, exactly as in the game.
    // A scripted key is fed on its own frame and then RELEASED, which is
    // the only way it produces an edge - `Game_Frame`'s filter is why
    // holding a direction does not scroll (docs/UI.md 3c), and a driver
    // that held them would send one word and see one move.
    st = omk::DeviceState{};
    for (int dik : host.held) st.keyboard.push_back(dik);
    // The mouse BUTTONS are a device like any other - the scheme's own
    // table decides what they do, and in shoot mode 12 is `Tir`. Fed
    // unconditionally rather than only in shoot mode: the binding tables
    // are what gate a device per group, and second-guessing them here is
    // how a port ends up with its own control scheme.
    for (int b : host.mouse) st.mouse.push_back(b);
    // ...and a GAMEPAD, as the engine's own JOYSTICK device
    // (`input/pad.h`): buttons 48 + k, the stick on slots 0..3 through
    // `Input_Poll`'s hardwired axes. Zero when there is no pad.
    omk::pad::toDevices(host.pad, st);
    // the `--hold` stream: held, not tapped, and only once he can walk
    // ...and it keeps feeding while a SCREEN is up. Gated on `adventure`
    // alone it stopped the moment the sneak opened - opening a screen is
    // exactly what takes `adventure` false - so a stream could press TAB
    // and then never press anything again, and the screen could not be
    // driven headless at all.
    // ...and a CONVERSATION, which is neither `adventure` nor `walk` and
    // so used to swallow the whole stream: every scripted press after the
    // one that opened a dialogue was dropped, and a headless run could
    // never reach the end of one. `--hold` is a harness, and a harness
    // that cannot press ENTER through a conversation cannot test what
    // happens when the conversation ends.
    if ((adventure || walk || session.dialogOpen()) && !holds.empty()) {
        for (int k : holds.front().keys) st.keyboard.push_back(k);
        if (--holds.front().frames <= 0) holds.erase(holds.begin());
    }
    if (!scripted.empty() && (n % keyEvery) == 0) {
        const int k = scripted.front();
        scripted.erase(scripted.begin());
        if (k == kTypeMarker) host.text += typeText;   // as if it were typed
        else if (k <= kCharMarker)                    // `cN`: one character
            host.text += static_cast<char>(kCharMarker - k);
        else st.keyboard.push_back(k);
    }
    // ...and ESC is read off THAT, once the scripted streams have been
    // merged in, rather than off `host.held`. The engine reads it from
    // Windows (`GetAsyncKeyState`) and not through DirectInput, so
    // either is faithful to "is the key down"; taking it here is what
    // lets `--keys`/`--hold` drive it, and a screen the harness cannot
    // open is a screen nothing can check.
    const bool esc = std::find(st.keyboard.begin(), st.keyboard.end(), 0x01)
                     != st.keyboard.end();
    if (esc && !walk && playerScreen < 0) {
        playerScreen = kScreenPause;
        std::printf("frame %ld: ESC -> screen %d PAUSE GAME "
                    "(`Game_RunLoop`'s own GetAsyncKeyState(27), guarded "
                    "by the pause flag)\n", n, kScreenPause);
    }
    // ---- THE LAST SCREEN'S CLOSE FLUSHES THE INPUT ------------------
    //
    // `Ui_CloseScreenDefault`, when no screen is left, zeroes the repeat
    // mask and the input word (`dword_4E9720`, `dword_4E971C`) and calls
    // `sub_43E4F0`: the input words `dword_52F440..` to 0, and each
    // DirectInput device's `GetDeviceData(INFINITE, NULL)` - its buffer
    // discarded - "so the release of the button that closed it cannot leak
    // into the game". Without it, TAB that closed the sneak reopened it
    // (`MDSNEAK0` 20 frames later) and SPACE that closed it made him jump.
    //
    // A RECONSTRUCTION of the effect, labelled: `Input_Poll` reads the
    // keyboard as a STATE array (`GetDeviceState`, vtable +36), which a
    // buffer flush does not touch, so the reading alone does not show what
    // keeps a key still held from counting. What the reader sees in the
    // game - nothing leaks - is ported as its plainest form: every key,
    // button and pad button held through the close is ignored until it is
    // released. ESC is left out, as the engine does: it is polled with
    // `GetAsyncKeyState`, outside DirectInput, and holding it through the
    // pause's close reopens the pause (docs/UI.md 3h).
    {
        static bool hadScreen = false;
        static std::set<int> flushed[3];
        if (hadScreen && !walk) {
            flushed[0].insert(st.keyboard.begin(), st.keyboard.end());
            flushed[0].erase(0x01);
            flushed[1].insert(st.mouse.begin(), st.mouse.end());
            flushed[2].insert(st.joystick.begin(), st.joystick.end());
            if (!flushed[0].empty() || !flushed[1].empty() || !flushed[2].empty())
                std::printf("frame %ld: the last screen closed - %zu key(s) held "
                            "through it ignored until released (the input flush)\n",
                            n, flushed[0].size() + flushed[1].size() + flushed[2].size());
        }
        hadScreen = static_cast<bool>(walk);
        std::vector<int>* devs[3] = {&st.keyboard, &st.mouse, &st.joystick};
        for (int d = 0; d < 3; ++d) {
            auto& v = *devs[d];
            for (auto it = flushed[d].begin(); it != flushed[d].end();)
                it = std::find(v.begin(), v.end(), *it) == v.end() ? flushed[d].erase(it)
                                                                    : std::next(it);
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [&](int k) { return flushed[d].count(k) != 0; }),
                    v.end());
        }
    }
    // The world's repeat mask is 0 - closing the last screen sets it
    // back, so the `.CTL` channel sees HELD keys and a walk is a walk
    // rather than a step per press. A screen over the world restores
    // `Ui_BeginScreen`'s 0x203F.
    // Keyed on the SCREEN, not on adventure mode. `Ui_BeginScreen` sets
    // 0x203F when a screen opens and the last close puts it back to 0, and
    // this used to do that only while `adventure` was true - which is
    // exactly when it is NOT: a screen over the world takes `adventure`
    // false in the same breath, so the mask stayed at the world's 0 and
    // every held key repeated every frame. Harmless while the only
    // screens came from `ui.open` during the boot, where `adventure` is
    // false and the mask is still the 0x203F set at start-up; the sneak
    // is the first screen opened from inside the world.
    // THE REPEAT MASK IS EXPRESSED IN *AVENTURE*'S SLOTS, and that is a
    // second place the per-group slot numbering bites (`todo/omk-play.md`
    // 97f). `Ui_BeginScreen` sets 0x203F - slots 0..5 and 13 - which in
    // Aventure covers the turns, the moves, `Action / Utiliser` (slot 4),
    // `Annuler` and the sneak. In *Tirer* `Action / Utiliser` has moved to
    // slot 8, which 0x203F does NOT contain - so it is never
    // edge-filtered and arrives as a LEVEL, every frame it is held.
    //
    // On its own that was harmless, because nothing read slot 8. Once the
    // confirm was re-mapped onto it, holding ENTER confirmed on every
    // frame: a reader pressed it on `Quitter le jeu` and came straight
    // back to the menu, and said exactly what it was - "maybe the input
    // is counted twice". So the mask has to follow the group too.
    if (walk) in.setRepeatMask(omk::kUiRepeatMask | (shootMode ? 0x100u : 0u));
    else if (adventure) in.setRepeatMask(0);
    bits = in.frame(st);
    // ...and the word HELD this frame, before any edge filter - what a
    // "while held" rule reads (the dialogue line's scroll, below).
    heldBits = in.poll(st);
    if (boardPress) { bits |= 0x10u; boardPress = false; }   // `--board`, one press
    // The EDGES, taken here rather than at each consumer so a frame that
    // never reaches one - a dialogue, a cutscene, a screen - cannot leave
    // the latch stale and manufacture a press on the way back. This is
    // `Game_Frame`'s `dword_4E971C` with every bit masked: the engine's
    // own `held & (held ^ (mask & last))` at mask = all ones.
    edgeBits = bits & ~prevBits;
    prevBits = bits;
    // ...and the world's action, which is not an edge: it is HELD, and
    // stays spent until the bit goes up. `kUiConfirm` is 0x10, the same
    // bit `H1AVNT` entry 24 (MDACTION) matches on.
    if (!(bits & omk::kUiConfirm)) actionSpent = false;
    // THE WORLD'S ACTION BUTTON IS NOT READ HERE. `Game_RaiseEvent(6, 4)`
    // is raised from the ACTOR tick (21_d3d.c:3460, :3513, :3962 - the
    // `.CTL` state handlers), so what stands for it in this loop is
    // `MDACTION` firing; see `actionFromMove` below. Reading it off this
    // edge instead was wrong twice over - it fired while a SCREEN had the
    // input, and it was spent by the time the sneak's own confirm let go.

    // THE DIALOGUE CLOCK IS REAL TIME, not a frame count.
    //
    // A line is timed by its AUDIO (CLAUDE.md 5), and the audio device
    // plays at its own rate whatever this loop does. Advancing the line by
    // 1/30 s per FRAME assumes the loop runs at exactly 30 - and it does
    // not, because a software-rasterised character costs more than the
    // 33 ms budget - so the pose and the voice drift apart, which is what
    // a reader saw. The web viewer makes the same choice for the camera
    // move and says why: wall clock, so losing or pausing the audio cannot
    // rewind it.
    //
    // A frame-bounded run keeps the fixed 1/30 so the headless checks stay
    // deterministic. The clamp is for a hitch: a long stall must not jump
    // the pose forward by however long the window was dragged.
    if (!frames) {
        const std::uint32_t nowMs = front.ticksMs();
        double dt = (nowMs - lastMs) / 1000.0;
        lastMs = nowMs;
        // THE ENGINE'S OWN CLAMP (docs/BOOT.md 4): `flt_4C30D8 = 30 / fps`,
        // capped at 3.0 - three frames, 0.1 s - so below 10 fps the game
        // SLOWS DOWN. The port used to fall back to 1/30 above 0.25 s,
        // and a console at 200-300 ms a frame (2026-09-22) then alternated
        // six frames of motion with one, which a reader saw as the camera
        // "shaking". Every frame is now a delta the engine could produce.
        if (dt < 0.0) dt = 1.0 / 30.0;
        if (dt > 3.0 / 30.0) dt = 3.0 / 30.0;
        dt *= speed;                     // --speed, the engine's own trick
        session.setFrameSeconds(dt);
        frameSec = dt;
    } else {
        // A frame-bounded run keeps the fixed 1/30 so the headless checks
        // stay deterministic; asking for a speed scales that too. Set EVERY
        // frame: the pause below writes 0 and only this puts it back - it
        // was set only for a speed other than 1, so after any pause a
        // `--frames` run stayed at a delta of 0 for good, and a quit to the
        // start menu stayed WHITE, its fade never advancing
        // (todo/drift-audit.md S15 - a harness fault, never a player's)
        frameSec = speed / 30.0;
        session.setFrameSeconds(frameSec);
    }
}

// The pause flag, one frame of the game, the Session under a screen
void PlayState::inputTick() {
    OMK_ZONE("input: tick");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    // ---- THE PAUSE FLAG, and it is a DELTA and nothing else ----------
    //
    // `dword_4E9728` has two writes in the image - screen 31's open
    // callback sets it, its close clears it - and what it does is force
    // the frame delta to 0.0. That is why `Slider_TickRide` sits behind
    // `flt_4C30D8 != 0.0` two lines down in `Game_Tick`, and why
    // `Game_Tick` needs no test for an open screen anywhere in it
    // (docs/UI.md 2a: it has none - that reading was corrected once
    // already, when the sneak froze the city).
    //
    // So the pause is not "skip the tick": every subsystem still runs,
    // on a delta of zero. Modelled here for the same reason - the port
    // used to express it as `adventure = false`, which stops the PLAYER
    // and leaves the crowd walking behind the menu.
    uiPause = walk && openScreen == kScreenPause;
    if (uiPause) { frameSec = 0.0; session.setFrameSeconds(0.0); }
    gameClock += session.frameDelta();   // 1.0 exactly at 30 and under --frames

    // ---- one frame of the GAME -------------------------------------
    //
    // The script runs unless a screen is up. That is the engine: a script
    // parked at `ui.open` is waiting on a person, and `Game_HandleEvent`
    // case 5 is the only thing that releases it.
    harnessScxPlay();
    harnessHideShow();
    harnessGameRestart();
    if (spriteAnchorSet) session.sceneMutable().setSpriteAnchor(spriteAnchor);
    // ---- AND THE SESSION RUNS UNDER A SCREEN, which it did not ------
    //
    // `Game_Tick` (0x004200F0) has NO test for an open screen anywhere in
    // it - `Script_SetFrameTime`, the per-slot `Script_PlayAllScripts`
    // loop, `Projectiles_Tick`, `Sliders_Tick` and `Slider_TickRide` all
    // run whatever is on screen. The only thing that stops the world is
    // the pause flag, and that is a DELTA of zero (docs/UI.md 2a), which
    // is already applied above.
    //
    // This is the THIRD time that assumption has had to come out. It was
    // corrected once for the world DRAW (a player opened the sneak and
    // watched Anekbah freeze, 19 frames in 1924), once for the player's
    // own tick, and this is the line that still held it for the SCRIPTS.
    // The sneak call is what needs it: `ui.open 0` answers itself and the
    // script's very next instruction is `dialog.start`, so a session that
    // stops while the screen is up can never play the call.
    //
    // A script parked at `ui.open` does not run either way - its status
    // is 6 and only `answerUi` clears it - so what this releases is
    // everything ELSE: the scene programs, the crowd, the other slots.
    //
    // ...and before it, `Game_Tick`'s release of a `player.move.wait`:
    // the channel's current group against the one the move entered.
    if (moveWaitCtx >= 0 && player && player->ctlGroup() != moveWaitGroup) {
        std::printf("frame %ld: player.move.wait ended - the channel left group %d "
                    "for %d, context %d resumes\n", n, moveWaitGroup,
                    player->ctlGroup(), moveWaitCtx);
        session.playerMoveEnded(moveWaitCtx);
        moveWaitCtx = -1; moveWaitGroup = -1;
    }
    mark("session");
    spanned("session", [&] { session.frame(); });
    // `dword_4E9760` as this frame's scripts left it, onto the records the
    // brains read below (todo/drift-audit.md S7)
    shootFreezeSync(n);
    // `g_IgnoreLedges` as this frame's scripts left it, for the walker that
    // `Actors_TickAll` steps after the pump (todo/drift-audit.md S5)
    if (player && player->walker().ignoreLedges != session.ignoreLedges()) {
        player->setIgnoreLedges(session.ignoreLedges());
        std::printf("frame %ld: the walker %s ledges (walk.ledges.%s)\n", n,
                    player->walker().ignoreLedges ? "IGNORES" : "obeys",
                    player->walker().ignoreLedges ? "ignore" : "obey");
    }

    mark("game frame");
}

// Scripted object motion - the crates, the doors, the lifts
void PlayState::inputMotion() {
    OMK_ZONE("input: motion");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    omk::Renderer& world = *world_;
    // ---- SCRIPTED OBJECT MOTION - the crates, the doors, the lifts ---
    //
    // `Script_MoveObjectOnPath` ends in `o3de_SetNodePos(node, x, y, z)`
    // with the path sample OUTRIGHT, so a moving object is a set MESH
    // placed at a world position, named through the object's own first
    // string table. 4841 sites - the most-used script function there is -
    // and nothing moved until 2026-09-03.
    //
    // The mesh's corners are offset by (target - the mesh's authored
    // position), which is what moving its origin means; `cornerMesh` says
    // which corners belong to it, and the base positions are kept so the
    // patch is applied to the ORIGINAL each frame rather than accumulated.
    // Bumping `revision` is what tells a caching backend the buffer moved.
    bool soupsMoved = false;
    std::vector<std::uint32_t> movedSoup[2], movedSteep[2];   // triangles re-placed, per slot
    // ...AND ITS SCALE. `Script_ScaleObjectX/Y/Z` (program.h) writes the
    // node's `+128..+136`, and `sub_494E80` multiplies each row of the
    // node's 3x3 by its axis's scale before the vertices go through it:
    // a scale in the node's LOCAL axes, about its origin, ahead of the
    // rotation a motion may have set. So a corner is (base - origin),
    // scaled per axis, rotated, placed. The apartment's transfer tube is
    // every shipped site: its beam grows along Y from 1 to 35.
    // BOTH POOLS. A transition's door resolves in the OUTGOING pool
    // (`engine: tunnel doors`: 93% of the corpus's door objects live
    // there), and its program runs in `sceneOut()` - so a motion read
    // from the active pool alone never moved the tunnel's doors, drawn
    // or collided. The meshes are matched by name across both slots.
    // WHERE EACH MOVED MESH ENDED UP, by name - filled by the patch
    // below and read by the scene-sound attenuation further down, which
    // needs the same answer. It used the raw path sample, and a sample is
    // not a position (`NodeMotion::placeOn`): the flat's entrance door
    // reports 632/-43/34, so the listener was measured against a point
    // 3400 units outside the building and the door slid in silence at
    // gain 0.03. Computed once here rather than twice.
    // `motionAt` - where each moved mesh was PLACED this frame, by name,
    // the last write winning, as the map it was (todo/optimization.md step
    // 18: a map node a motion every frame). Kept across frames for its
    // capacity; mesh names fit a short string, so a reused slot allocates
    // nothing.
    motionAt.clear();
    std::vector<omk::Program::NodeMotion> allMotions;
    std::map<std::string, omk::SceneRunner::NodeScale> allScales;
    const double motionGather0 = phaseNow();
    for (const omk::SceneRunner* sr : {&session.sceneOut(), &session.scene()}) {
        if (!sr->loaded()) continue;
        for (const auto& mo : sr->motions()) {
            // say once which pool moved which mesh - a door that resolved
            // in the outgoing pool is the case this line exists for
            const std::string key = mo.name + (sr == &session.scene() ? "/active" : "/out");
            if (motionLogged.insert(key).second)
                std::printf("motion: mesh '%s' moved by the %s pool (%s) - first sample %.0f %.0f %.0f%s\n",
                            mo.name.c_str(), sr == &session.scene() ? "ACTIVE" : "OUTGOING",
                            sr->file().c_str(), mo.pos[0], mo.pos[1], mo.pos[2],
                            mo.rotated ? ", rotated" : "");
        }
        // THE PATCH IS BUILT FROM WHERE THE NODES REST, not from what moved
        // this tick. `Script_MoveObjectOnPath` ends in `o3de_SetNodePos`
        // and leaves the node there, so a program that has finished still
        // holds its node out of place - `SceneRunner::placements()` is that
        // state and it outlives the program. Read from `motions()`, a mesh
        // that is also SCALED kept its patch entry after its move ended,
        // that entry had no motion, and the mesh was re-placed at its
        // AUTHORED origin every frame from then on: a node a finished
        // program had displaced, put back. 35 shipped mesh names are both
        // scaled and moved by their scene (`node_rest`).
        // `OMK_TRANSIENT_MOTIONS=1` takes the per-tick record instead -
        // the old, wrong reading, kept so the two can be run a variable
        // apart (`verify.py: engine: node rest`).
        static const bool transientMotions =
            omk::envSet("OMK_TRANSIENT_MOTIONS");
        if (transientMotions) {
            for (const auto& mo : sr->motions()) allMotions.push_back(mo);
        } else {
            for (const auto& kv : sr->placements()) allMotions.push_back(kv.second);
        }
        for (const auto& ns : sr->nodeScales()) allScales[ns.first] = ns.second;
    }
    phSpan["motion gather"] += phaseNow() - motionGather0;
    // the GPU path for the moving meshes: a renderer that poses bodies, and
    // nothing this frame that reads their corners out of the set's buffer
    // (`OMK_MESH_AT`); `OMK_CPU_MOTION=1` for the comparison
    static const bool cpuMotion = omk::envSet("OMK_CPU_MOTION") ||
                                  std::getenv("OMK_MESH_AT") != nullptr;
    const bool gpuMotion = !cpuMotion && !cpuBodiesFlag && world.posesBodies();
    if (!allMotions.empty() || !allScales.empty()) {
        struct Patch {
            float s[3] = {1.0f, 1.0f, 1.0f};
            bool  hasMotion = false;
            float pos[3] = {0, 0, 0};
            omk::Quatf q{1, 0, 0, 0};
            bool  rotated = false;
        };
        for (int sl = 0; sl < 2; ++sl) {
            WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
            if (w.geo.corners.empty() || w.meshes.empty()) continue;
            const auto lowerName = [](const std::string& n) {
                std::string l(n);
                for (auto& ch : l) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return l;
            };
            if (!w.patchIndexReady) {
                w.patchIndexReady = true;
                const std::size_t nm = w.meshes.size();
                for (std::size_t k = 0; k < nm; ++k)
                    w.meshByLowerName.emplace(lowerName(w.meshes[k].name), static_cast<int>(k));
                w.cornersOfMesh.assign(nm, {});
                const std::size_t nc = std::min(w.geo.corners.size(), w.geo.cornerMesh.size());
                for (std::size_t c = 0; c < nc; ++c) {
                    const std::int32_t mi = w.geo.cornerMesh[c];
                    if (mi >= 0 && static_cast<std::size_t>(mi) < nm)
                        w.cornersOfMesh[static_cast<std::size_t>(mi)].push_back(static_cast<std::uint32_t>(c));
                }
                const auto trisOf = [&](const std::vector<int>& meshOf, std::size_t floats) {
                    std::vector<std::vector<std::uint32_t>> out(nm);
                    for (std::size_t t = 0; t < meshOf.size() && 9 * t + 9 <= floats; ++t)
                        if (meshOf[t] >= 0 && static_cast<std::size_t>(meshOf[t]) < nm)
                            out[static_cast<std::size_t>(meshOf[t])].push_back(static_cast<std::uint32_t>(t));
                    return out;
                };
                w.soupTrisOfMesh = trisOf(w.soupMesh, w.soup.size());
                w.steepTrisOfMesh = trisOf(w.steepMesh, w.steep.size());
            }
            const auto meshIndex = [&](const std::string& name) {
                const auto it = w.meshByLowerName.find(lowerName(name));
                return it == w.meshByLowerName.end() ? -1 : it->second;
            };
            // THE PATCHES, a mesh each - a map node each every frame, until
            // step 18. A scratch vector found by mesh index and SORTED by
            // it before use, so they are walked in the map's own order
            // (which is the order the dirty list is built in).
            static std::vector<std::pair<int, Patch>> patches;
            patches.clear();
            const auto patchOf = [&](int mi) -> Patch& {
                for (auto& kv : patches) if (kv.first == mi) return kv.second;
                patches.emplace_back(mi, Patch{});
                return patches.back().second;
            };
            for (const auto& ns : allScales) {
                const int mi = meshIndex(ns.first);
                if (mi < 0) continue;
                Patch& p = patchOf(mi);
                for (int c = 0; c < 3; ++c) p.s[c] = ns.second.s[c];
            }
            for (const auto& mo : allMotions) {
                if (!mo.placed) continue;
                const int mi = meshIndex(mo.name);
                if (mi < 0) continue;
                Patch& p = patchOf(mi);
                p.hasMotion = true;
                // THE PATH IS A DISPLACEMENT, and the anchor is the mesh
                // itself. `Script_MoveObjectOnPath` places the node at
                // `sample(t) - sample(t0) + the node's position when the
                // move began`; for a set mesh that anchor is where the
                // set authored it, which is `mp` below. Taking `pos`
                // outright is right only where a path happens to be
                // authored on its mesh - `AHALL40`'s are, `AAPKAYL`'s are
                // not, and that is the whole of the apartment door bug.
                mo.placeOn(w.meshes[static_cast<std::size_t>(mi)].pos, p.pos);
                {
                    const std::array<float, 3> at{p.pos[0], p.pos[1], p.pos[2]};
                    bool found = false;
                    for (auto& kv : motionAt) if (kv.first == mo.name) { kv.second = at; found = true; break; }
                    if (!found) motionAt.emplace_back(mo.name, at);
                }
                if (omk::envSet("OMK_TRACE_MOTION")) {
                    const float* mp = w.meshes[static_cast<std::size_t>(mi)].pos;
                    std::printf("  [motion] %-12s sample %.0f %.0f %.0f  from %.0f %.0f %.0f"
                                "  anchor %.0f %.0f %.0f  ->  %.0f %.0f %.0f  (%s)\n",
                                mo.name.c_str(), mo.pos[0], mo.pos[1], mo.pos[2],
                                mo.hasFrom ? mo.from[0] : 0.0f,
                                mo.hasFrom ? mo.from[1] : 0.0f,
                                mo.hasFrom ? mo.from[2] : 0.0f,
                                mp[0], mp[1], mp[2], p.pos[0], p.pos[1], p.pos[2],
                                mo.hasFrom ? "param 5 set: a displacement"
                                           : "param 5 zero: the sample OUTRIGHT");
                }
                // THE CONJUGATE, the engine's sense (2026-10-02). `Path_Sample`
                // hands `sub_437160` `Matrix3x3_FromQuaternion(q)` - the
                // standard R(q), row-major - and every vertex the node
                // carries is turned by its TRANSPOSE: `sub_4947F0` computes
                // `m[0]x + m[3]y + m[6]z`, as `Matrix3x3_RotateVector` does.
                // So a path's key turns the mesh by conj(q). Taking q as it
                // stands turned Kay'l's chest's two lid pieces the wrong
                // way about their centres, so they crossed in a V instead
                // of opening on the hinge (a reader, 2026-10-01). The same
                // rule a scene clip's root obeys (CLAUDE.md 6).
                p.q = omk::Quatf{mo.quat[0], -mo.quat[1], -mo.quat[2], -mo.quat[3]};
                p.rotated = mo.rotated;
            }
            std::sort(patches.begin(), patches.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            bool moved = false;
            std::vector<std::uint32_t> dirty;   // the corners this frame's patch rewrote
            const double motionPatch0 = phaseNow();
            for (const auto& kv : patches) {
                const int mi = kv.first;
                const Patch& pa = kv.second;
                // already in the buffer? then nothing to write - see
                // `appliedPatch`. Compared bit for bit, not by tolerance:
                // the question is whether the CORNERS would change.
                const std::array<float, 11> want{
                    pa.s[0], pa.s[1], pa.s[2],
                    pa.hasMotion ? pa.pos[0] : 0.0f,
                    pa.hasMotion ? pa.pos[1] : 0.0f,
                    pa.hasMotion ? pa.pos[2] : 0.0f,
                    pa.q.w, pa.q.x, pa.q.y, pa.q.z,
                    static_cast<float>((pa.hasMotion ? 1 : 0) | (pa.rotated ? 2 : 0))};
                const auto seen = w.appliedPatch.find(mi);
                if (seen != w.appliedPatch.end() && seen->second == want) continue;
                w.appliedPatch[mi] = want;
                // THIS MESH's rest - x y z of its corners, 9 floats of each
                // of its triangles - taken the first time it moves, and
                // written back before every placement (`playtypes.h`)
                const auto& meshCornersIdx = w.cornersOfMesh[static_cast<std::size_t>(mi)];
                const auto& meshSoupTris = w.soupTrisOfMesh[static_cast<std::size_t>(mi)];
                const auto& meshSteepTris = w.steepTrisOfMesh[static_cast<std::size_t>(mi)];
                auto restXyz = w.restXyzOfMesh.find(mi);
                if (restXyz == w.restXyzOfMesh.end()) {
                    std::vector<float> r;
                    r.reserve(3 * meshCornersIdx.size());
                    for (const std::uint32_t c : meshCornersIdx) {
                        const omk::Corner& k = w.geo.corners[c];
                        r.push_back(k.x); r.push_back(k.y); r.push_back(k.z);
                    }
                    restXyz = w.restXyzOfMesh.emplace(mi, std::move(r)).first;
                    const auto takeTris = [](const omk::TriangleSoup& soup,
                                             const std::vector<std::uint32_t>& tris) {
                        std::vector<float> r;
                        r.reserve(9 * tris.size());
                        for (const std::uint32_t t : tris)
                            r.insert(r.end(), soup.begin() + 9 * t, soup.begin() + 9 * t + 9);
                        return r;
                    };
                    w.restSoupOfMesh.emplace(mi, takeTris(w.soup, meshSoupTris));
                    w.restSteepOfMesh.emplace(mi, takeTris(w.steep, meshSteepTris));
                }
                const auto restoreCorners = [&] {
                    const std::vector<float>& r = restXyz->second;
                    for (std::size_t k = 0; k < meshCornersIdx.size(); ++k) {
                        omk::Corner& c = w.geo.corners[meshCornersIdx[k]];
                        c.x = r[3 * k]; c.y = r[3 * k + 1]; c.z = r[3 * k + 2];
                    }
                };
                const float* mp = w.meshes[static_cast<std::size_t>(mi)].pos;
                // The motion's orientation: the path sample's 3x3 goes to
                // node `+56` through `sub_437160` beside the
                // `o3de_SetNodePos`, and an object that turns in place
                // has ONLY the rotation - the Impasse's fan `Ventilo`
                // (omk-play 53). A `Mesh` record carries no orientation,
                // so the node's matrix starts as identity and the
                // sample's applies directly, about the authored origin.
                const float* at = pa.hasMotion ? pa.pos : mp;
                // `o3de/pointplace.h`: the same (in - origin) * scale,
                // `qrot`, + at, point by point - four at a time on NEON
                omk::PointPlace pp;
                for (int k = 0; k < 3; ++k) {
                    pp.origin[k] = mp[k];
                    pp.scale[k] = pa.s[k];
                    pp.at[k] = at[k];
                }
                pp.rotated = pa.rotated;
                pp.q = pa.q;
                static_assert(sizeof(omk::Corner) == 12 * sizeof(float) &&
                              offsetof(omk::Corner, x) == 0,
                              "a Corner is 12 floats with x, y, z first");
                const auto& meshCorners = w.cornersOfMesh[static_cast<std::size_t>(mi)];
                const double motionCorners0 = phaseNow();
                if (gpuMotion) {
                    auto mit = w.moving.find(mi);
                    if (mit == w.moving.end()) {
                        // the set's own corners for it go BACK to where they
                        // were built first, in case a CPU patch moved them
                        // before - its own copy is made from them
                        restoreCorners();
                        // its own geometry, batch by batch in the set's
                        // order, made once: the corners as BUILT, less the
                        // mesh's origin
                        WorldSlot::MovingMesh mm;
                        const omk::Geometry& g = w.geo;
                        for (const auto& b : g.batches) {
                            const std::size_t first = mm.rest.corners.size();
                            for (std::uint32_t c = b.start; c < b.start + b.count; ++c) {
                                if (c >= g.cornerMesh.size() || g.cornerMesh[c] != mi) continue;
                                omk::Corner k = w.geo.corners[c];
                                k.x = k.x - mp[0]; k.y = k.y - mp[1]; k.z = k.z - mp[2];
                                mm.rest.corners.push_back(k);
                            }
                            const std::size_t cnt = mm.rest.corners.size() - first;
                            if (!cnt) continue;
                            omk::Batch nb = b;
                            nb.start = static_cast<std::uint32_t>(first);
                            nb.count = static_cast<std::uint32_t>(cnt);
                            mm.rest.batches.push_back(nb);
                        }
                        mm.rest.cornerMesh.assign(mm.rest.corners.size(), 0);
                        mm.rest.revision = ++worldGeoRev;
                        mit = w.moving.emplace(mi, std::move(mm)).first;
                        std::printf("motion: mesh '%s' drawn by the RENDERER from its own %zu corners "
                                    "(%zu batches) - its %zu in the set's buffer stay at rest\n",
                                    w.meshes[static_cast<std::size_t>(mi)].name,
                                    mit->second.rest.corners.size(), mit->second.rest.batches.size(),
                                    meshCorners.size());
                        // (the set's own corners for it were put back at
                        // rest above, before the copy)
                        dirty.insert(dirty.end(), meshCorners.begin(), meshCorners.end());
                        moved = true;
                    }
                    // the placement as a 3x4 on (corner - origin): the scale,
                    // then the rotation - `placeOne`'s order - then `at`
                    WorldSlot::MovingMesh& mm = mit->second;
                    float R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                    if (pa.rotated)
                        for (int j = 0; j < 3; ++j) {
                            const float e[3] = {j == 0 ? 1.0f : 0.0f, j == 1 ? 1.0f : 0.0f,
                                                j == 2 ? 1.0f : 0.0f};
                            float col[3];
                            omk::qrot(pa.q, e, col);
                            for (int r = 0; r < 3; ++r) R[r][j] = col[r];
                        }
                    for (int r = 0; r < 3; ++r) {
                        for (int j = 0; j < 3; ++j) mm.affine[4 * r + j] = R[r][j] * pa.s[j];
                        mm.affine[4 * r + 3] = at[r];
                        mm.at[r] = at[r];
                    }
                    const float smax = std::max({std::fabs(pa.s[0]), std::fabs(pa.s[1]),
                                                 std::fabs(pa.s[2]), 1.0f});
                    mm.reach = w.meshes[static_cast<std::size_t>(mi)].radius * smax;
                } else {
                    dirty.insert(dirty.end(), meshCorners.begin(), meshCorners.end());
                    // the rest written back, then placed IN PLACE: the same
                    // input values, so the same bits as placing from a copy
                    restoreCorners();
                    omk::placePoints(pp, &w.geo.corners[0].x, 12, &w.geo.corners[0].x, 12,
                                     meshCorners.data(), meshCorners.size());
                }
                phSpan["motion corners"] += phaseNow() - motionCorners0;
                // the collision soups follow the mesh exactly as the
                // render corners above (`Sweep_MeshTest` collides
                // against the mesh's CURRENT matrix)
                const auto patchSoup = [&](omk::TriangleSoup& soup, const std::vector<float>& rest,
                                           const std::vector<std::uint32_t>& tris,
                                           std::vector<std::uint32_t>& movedOut) {
                    movedOut.insert(movedOut.end(), tris.begin(), tris.end());
                    // the mesh's triangles back at rest, then placed in place
                    for (std::size_t k = 0; k < tris.size(); ++k)
                        std::copy(rest.begin() + 9 * k, rest.begin() + 9 * k + 9,
                                  soup.begin() + 9 * tris[k]);
                    // a triangle is three packed points; a run of them
                    // is what NEON loads four at a time
                    // main thread only, so a plain static (on the Vita
                    // `thread_local` is not per thread anyway)
                    static std::vector<std::uint32_t> pts;
                    pts.clear();
                    for (const std::uint32_t t : tris) {
                        pts.push_back(3 * t); pts.push_back(3 * t + 1); pts.push_back(3 * t + 2);
                    }
                    if (!pts.empty())
                        omk::placePoints(pp, soup.data(), 3, soup.data(), 3, pts.data(), pts.size());
                };
                const double motionSoups0 = phaseNow();
                static const bool eagerSoups = omk::envSet("OMK_EAGER_SOUPS");
                if (eagerSoups) {
                    patchSoup(w.soup, w.restSoupOfMesh[mi], meshSoupTris, movedSoup[sl]);
                    patchSoup(w.steep, w.restSteepOfMesh[mi], meshSteepTris, movedSteep[sl]);
                } else {
                    // RECORDED, placed when a query reaches it (`lazyPlace`)
                    movedSoup[sl].insert(movedSoup[sl].end(), meshSoupTris.begin(), meshSoupTris.end());
                    movedSteep[sl].insert(movedSteep[sl].end(), meshSteepTris.begin(), meshSteepTris.end());
                    const int key = sl * 1000000 + mi;
                    LazyMesh& lm = lazyMeshes[key];
                    lm.sl = sl; lm.mi = mi; lm.pp = pp; lm.gen = worldGen;
                    if (!lm.pending) { lm.pending = true; ++lazyPending; }
                    ++lazyRecordedTotal;
                    if (!lm.boxKnown) {
                        // the rest triangles' box, each layer - what the
                        // conservative extent is carried from
                        const auto boxOf = [](const std::vector<float>& r, float* b) {
                            b[0] = b[1] = b[2] = 1e30f; b[3] = b[4] = b[5] = -1e30f;
                            for (std::size_t k = 0; k + 2 < r.size(); k += 3)
                                for (int a = 0; a < 3; ++a) {
                                    b[a] = std::min(b[a], r[k + static_cast<std::size_t>(a)]);
                                    b[3 + a] = std::max(b[3 + a], r[k + static_cast<std::size_t>(a)]);
                                }
                        };
                        boxOf(w.restSoupOfMesh[mi], lm.restBox[0]);
                        boxOf(w.restSteepOfMesh[mi], lm.restBox[1]);
                        lm.boxKnown = true;
                    }
                }
                if (std::find(w.soupMovers.begin(), w.soupMovers.end(), mi) == w.soupMovers.end())
                    w.soupMovers.push_back(mi);
                phSpan["motion soups"] += phaseNow() - motionSoups0;
                soupsMoved = true;
                if (!gpuMotion) moved = true;
            }
            phSpan["motion patch"] += phaseNow() - motionPatch0;
            if (moved) {
                // only the patched meshes' corners changed since the last
                // revision - the backend's vertex upload and depth tie take
                // just those (Geometry::dirtyCorners). `OMK_NO_DIRTY=1`
                // leaves the list unset, so everything is re-done as before.
                static const bool noDirty = std::getenv("OMK_NO_DIRTY") != nullptr;
                if (!noDirty) w.geo.dirtyFrom = w.geo.revision;
                w.geo.revision = ++worldGeoRev;
                if (!noDirty) {
                    w.geo.dirtyTo = w.geo.revision;
                    w.geo.dirtyCorners.swap(dirty);
                }
            }
        }
    }
    // `OMK_MESH_AT=<mesh name>`: where that set mesh actually IS, read out
    // of `w.geo.corners` - the vertex buffer this frame hands the renderer
    // - rather than out of any motion record. A scene program's motion is
    // gone from `SceneRunner::motions()` the tick it ends, so a line
    // derived from a motion cannot say anything at all about where the
    // node RESTS; this one is the drawn geometry and answers on every
    // frame, moving or still. `verify.py: engine: node rest`.
    {
        static const char* meshAtName = std::getenv("OMK_MESH_AT");
        if (meshAtName && *meshAtName) {
            const auto lower = [](std::string n) {
                for (auto& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return n;
            };
            const std::string want = lower(meshAtName);
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.geo.corners.empty() || w.meshes.empty()) continue;
                int mi = -1;
                for (std::size_t k = 0; k < w.meshes.size(); ++k)
                    if (lower(std::string(w.meshes[k].name)) == want) { mi = static_cast<int>(k); break; }
                if (mi < 0) continue;
                // the DRAWN centroid and the AS-BUILT one, so the line
                // carries the displacement itself and not a number that
                // has to be compared against a mesh origin elsewhere.
                // the AS-BUILT one is the mesh's own rest copy, taken by its
                // first patch; before any patch its corners ARE as built.
                // (Summed in `cornersOfMesh` order, ascending - the order
                // the whole-set walk took.)
                double c[3] = {0, 0, 0}, b[3] = {0, 0, 0};
                long n = 0;
                const auto rit = w.restXyzOfMesh.find(mi);
                // THE INDEX IS BUILT BY THE FIRST PLACEMENT (`patchIndexReady`,
                // above): before it - frame 0 of a scene, or a set nothing has
                // moved - the mesh's corners are found by `cornerMesh`, in the
                // same ascending order. Reading the unbuilt index crashed every
                // run with this instrument on from `f9fad39` (`engine: slot pool`).
                static std::vector<std::uint32_t> scan;
                const std::vector<std::uint32_t>* idxp = nullptr;
                if (static_cast<std::size_t>(mi) < w.cornersOfMesh.size()) {
                    idxp = &w.cornersOfMesh[static_cast<std::size_t>(mi)];
                } else {
                    scan.clear();
                    const std::size_t nc = std::min(w.geo.corners.size(), w.geo.cornerMesh.size());
                    for (std::size_t c = 0; c < nc; ++c)
                        if (w.geo.cornerMesh[c] == mi) scan.push_back(static_cast<std::uint32_t>(c));
                    idxp = &scan;
                }
                const auto& idxs = *idxp;
                for (std::size_t k = 0; k < idxs.size(); ++k) {
                    const omk::Corner& cc = w.geo.corners[idxs[k]];
                    c[0] += cc.x; c[1] += cc.y; c[2] += cc.z;
                    if (rit != w.restXyzOfMesh.end()) {
                        b[0] += rit->second[3 * k]; b[1] += rit->second[3 * k + 1]; b[2] += rit->second[3 * k + 2];
                    } else {
                        b[0] += cc.x; b[1] += cc.y; b[2] += cc.z;
                    }
                    ++n;
                }
                if (!n) continue;
                std::printf("mesh at: frame %ld  slot %d  %s  drawn %.1f %.1f %.1f"
                            "  built %.1f %.1f %.1f  moved %.1f %.1f %.1f  corners %ld\n",
                            static_cast<long>(session.frameNo()), sl,
                            w.meshes[static_cast<std::size_t>(mi)].name,
                            c[0] / n, c[1] / n, c[2] / n, b[0] / n, b[1] / n, b[2] / n,
                            (c[0] - b[0]) / n, (c[1] - b[1]) / n, (c[2] - b[2]) / n, n);
            }
        }
    }
    mark("scripted motion: meshes placed");
    if (soupsMoved) {
        // the walker holds REFERENCES to the merged copies, so they are
        // refilled in place rather than rebuilt (rebuildWorld's merge)
        //
        // ...and only the triangles that moved are copied in, at their
        // slot's offset, while the merge is known to match the slots
        // (todo/optimization.md step 7). The full merge stays for anything
        // else: a slot loaded since, or sizes that no longer add up.
        std::size_t wantSoup = 0, wantSteep = 0;
        for (int sl = 0; sl < 2; ++sl) {
            const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
            if (w.stem.empty()) continue;
            wantSoup += w.soup.size(); wantSteep += w.steep.size();
        }
        bool newlyMoving = false;
        bool newlySteep = false;
        const bool inPlace = mergedValid && playerSoup.size() == wantSoup && playerSteep.size() == wantSteep;
        if (inPlace) {
            std::size_t offSoup = 0, offSteep = 0;
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                static const bool eagerMerge = omk::envSet("OMK_EAGER_SOUPS");
                for (const std::uint32_t t : movedSoup[sl]) {
                    // a recorded mesh is copied in when it is placed (`lazyPlace`)
                    if (eagerMerge)
                        std::copy_n(w.soup.data() + 9 * static_cast<std::size_t>(t), 9,
                                    playerSoup.data() + offSoup + 9 * static_cast<std::size_t>(t));
                    const std::size_t gt = offSoup / 9 + t;
                    if (gt < playerMovingTri.size() && !playerMovingTri[gt]) {
                        playerMovingTri[gt] = 1;
                        playerMovingIds.push_back(static_cast<std::uint32_t>(gt));
                        newlyMoving = true;
                    }
                }
                for (const std::uint32_t t : movedSteep[sl]) {
                    if (eagerMerge)
                        std::copy_n(w.steep.data() + 9 * static_cast<std::size_t>(t), 9,
                                    playerSteep.data() + offSteep + 9 * static_cast<std::size_t>(t));
                    const std::size_t gt = offSteep / 9 + t;
                    if (gt < steepMovingTri.size() && !steepMovingTri[gt]) {
                        steepMovingTri[gt] = 1;
                        steepMovingIds.push_back(static_cast<std::uint32_t>(gt));
                        newlySteep = true;
                    }
                }
                offSoup += w.soup.size(); offSteep += w.steep.size();
            }
        } else {
            lazyPlaceAll();       // the slots' soups are copied whole: every mesh placed first
            playerSoup.clear(); playerSteep.clear();
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                playerSoup.insert(playerSoup.end(), w.soup.begin(), w.soup.end());
                playerSteep.insert(playerSteep.end(), w.steep.begin(), w.steep.end());
            }
            mergedValid = true;
            // a full merge: the sets' layout may have changed, so every moved
            // triangle is re-marked against the new offsets
            playerMovingTri.assign(playerSoup.size() / 9, 0);
            std::size_t off = 0;
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                for (const std::uint32_t t : movedSoup[sl])
                    if (off / 9 + t < playerMovingTri.size()) playerMovingTri[off / 9 + t] = 1;
                off += w.soup.size();
            }
            playerMovingIds.clear();
            for (std::size_t t = 0; t < playerMovingTri.size(); ++t)
                if (playerMovingTri[t]) playerMovingIds.push_back(static_cast<std::uint32_t>(t));
            newlyMoving = true;
            // ...and the steep faces the same way
            steepMovingTri.assign(playerSteep.size() / 9, 0);
            std::size_t offS = 0;
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                for (const std::uint32_t t : movedSteep[sl])
                    if (offS / 9 + t < steepMovingTri.size()) steepMovingTri[offS / 9 + t] = 1;
                offS += w.steep.size();
            }
            steepMovingIds.clear();
            for (std::size_t t = 0; t < steepMovingTri.size(); ++t)
                if (steepMovingTri[t]) steepMovingIds.push_back(static_cast<std::uint32_t>(t));
            newlySteep = true;
        }
        // `OMK_VERIFY_PATCH=1`: the full merge beside the in-place one, every
        // moving frame, compared bit for bit (`verify.py: engine: patch index`).
        static const bool verifyPatch = std::getenv("OMK_VERIFY_PATCH") != nullptr;
        if (verifyPatch) {
            lazyPlaceAll();   // the instrument compares every triangle
            static long compared = 0, mismatched = 0;
            omk::TriangleSoup refSoup, refSteep;
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                refSoup.insert(refSoup.end(), w.soup.begin(), w.soup.end());
                refSteep.insert(refSteep.end(), w.steep.begin(), w.steep.end());
            }
            ++compared;
            const bool same = refSoup.size() == playerSoup.size() && refSteep.size() == playerSteep.size() &&
                (refSoup.empty() || std::memcmp(refSoup.data(), playerSoup.data(), refSoup.size() * sizeof(float)) == 0) &&
                (refSteep.empty() || std::memcmp(refSteep.data(), playerSteep.data(), refSteep.size() * sizeof(float)) == 0);
            if (!same) ++mismatched;
            if (compared % 30 == 0)
                std::printf("patch verify: %ld moving frames compared, %ld mismatched, %zu + %zu triangles re-placed this frame\n",
                            compared, mismatched,
                            movedSoup[0].size() + movedSoup[1].size(), movedSteep[0].size() + movedSteep[1].size());
        }
        // THE TWO LAYERS: the fixed one only when the moving set changed, the
        // moving one - a few hundred triangles - every moving frame.
        mark("scripted motion: soups patched");
        if (newlyMoving) std::sort(playerMovingIds.begin(), playerMovingIds.end());
        spanned("grid fixed", [&] {
            if (newlyMoving || !playerGrid.fixed.matches(playerSoup)) rebuildFixedGrid();
        });
        // THE MOVING LAYER AS WHOLE MESHES (todo/optimization.md step 38):
        // `o3de_ForEachMeshInBox` tests a moving mesh by its extent and
        // rebuilds nothing, so in place of a grid rebuilt every frame the
        // moving triangles stand in groups, one a mesh, each with the
        // extent it has now - the ids re-gathered only when the moving set
        // or the merge changed, the extents every moving frame.
        // `OMK_MOVING_GRID=1` rebuilds the grid as before.
        static const bool movingGrid = omk::envSet("OMK_MOVING_GRID");
        const auto partsOf = [&](omk::SplitSoupGrid& grid, const omk::TriangleSoup& merged,
                                 const std::vector<std::uint8_t>& movingTri, bool steep,
                                 bool regather) {
            if (regather || !grid.useParts) {
                grid.parts.clear();
                std::size_t off = 0;
                for (int sl = 0; sl < 2; ++sl) {
                    const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                    if (w.stem.empty()) continue;
                    const auto& byMesh = steep ? w.steepTrisOfMesh : w.soupTrisOfMesh;
                    for (const int mi : w.soupMovers) {
                        if (mi < 0 || static_cast<std::size_t>(mi) >= byMesh.size()) continue;
                        omk::MovingPart part;
                        part.owner = sl * 1000000 + mi;
                        for (const std::uint32_t t : byMesh[static_cast<std::size_t>(mi)]) {
                            const std::size_t gt = off / 9 + t;
                            // only what the fixed layer leaves out, so each
                            // triangle is in one layer
                            if (gt < movingTri.size() && movingTri[gt])
                                part.ids.push_back(static_cast<std::uint32_t>(gt));
                        }
                        if (!part.ids.empty()) grid.parts.push_back(std::move(part));
                    }
                    off += steep ? w.steep.size() : w.soup.size();
                }
                grid.moving = omk::SoupGrid{};
                grid.useParts = true;
            }
            // THE EXTENT: a placed part's own, as before; a RECORDED one's
            // carried from its rest box through the placement and padded by
            // a unit - a superset of where its triangles will be, so a query
            // gathers what it would have, and the answers do not move
            static const bool eager = omk::envSet("OMK_EAGER_SOUPS");
            if (!lazyPlaceOneFn) lazyPlaceOneFn = [this](int key) { lazyPlace(key); };
            grid.place = eager ? std::function<void(int)>{} : lazyPlaceOneFn;
            for (auto& part : grid.parts) {
                const auto lit = eager ? lazyMeshes.end() : lazyMeshes.find(part.owner);
                if (lit == lazyMeshes.end()) { omk::measurePart(merged, part); part.pending = false; continue; }
                const LazyMesh& lm = lit->second;
                const float* b = lm.restBox[steep ? 1 : 0];
                float c[24], o[24];
                std::uint32_t idx[8];
                for (int k = 0; k < 8; ++k) {
                    c[3 * k] = b[(k & 1) ? 3 : 0];
                    c[3 * k + 1] = b[(k & 2) ? 4 : 1];
                    c[3 * k + 2] = b[(k & 4) ? 5 : 2];
                    idx[k] = static_cast<std::uint32_t>(k);
                }
                omk::placePoints(lm.pp, c, 3, o, 3, idx, 8);
                double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
                for (int k = 0; k < 8; ++k) {
                    lo[0] = std::min(lo[0], double(o[3 * k])); hi[0] = std::max(hi[0], double(o[3 * k]));
                    lo[1] = std::min(lo[1], double(o[3 * k + 2])); hi[1] = std::max(hi[1], double(o[3 * k + 2]));
                }
                part.minX = lo[0] - 1.0; part.maxX = hi[0] + 1.0;
                part.minZ = lo[1] - 1.0; part.maxZ = hi[1] + 1.0;
                part.pending = lm.pending;
            }
            grid.partsData = merged.data();
            grid.partsSize = merged.size();
        };
        if (!lazyPlaceAllFn) lazyPlaceAllFn = [this] { lazyPlaceAll(); };
        if (movingGrid) lazyPlaceAll();   // a grid built over the triangles reads them all
        spanned("grid moving (floor)", [&] {
            if (movingGrid) rebuildMovingGrid();
            else partsOf(playerGrid, playerSoup, playerMovingTri, false, newlyMoving);
        });
        if (newlySteep) std::sort(steepMovingIds.begin(), steepMovingIds.end());
        spanned("grid fixed", [&] {
            if (newlySteep || !playerSteepGrid.fixed.matches(playerSteep)) rebuildSteepFixedGrid();
        });
        spanned("grid moving (steep)", [&] {
            if (movingGrid) rebuildSteepMovingGrid();
            else partsOf(playerSteepGrid, playerSteep, steepMovingTri, true, newlySteep);
        });
        // the soups the linear readers must not see stale (`ensurePlaced`)
        lazyRegister();
        mark("scripted motion: grids rebuilt");
        // `OMK_VERIFY_SPLIT=1`: the moved triangles' centres from above and a
        // fixed lattice over the street, probed through the two-layer grid and
        // through the linear scan, every moving frame
        // (`verify.py: engine: split grid`).
        static const bool verifySplit = std::getenv("OMK_VERIFY_SPLIT") != nullptr;
        if (verifySplit) {
            lazyPlaceAll();   // it reads triangle centres straight out of the soup
            static long frames = 0, probes = 0, mismatched = 0;
            ++frames;
            const auto check = [&](double x, double y, double z) {
                ++probes;
                const auto a = omk::floorUnder(playerSoup, playerGrid, x, y, z);
                const auto b = omk::floorUnder(playerSoup, x, y, z);
                if (a.has_value() != b.has_value() || (a && std::memcmp(&*a, &*b, sizeof(double)) != 0))
                    ++mismatched;
                const auto c = omk::surfaceUnder(playerSoup, playerGrid, x, y, z);
                const auto d = omk::surfaceUnder(playerSoup, x, y, z);
                if (c.has_value() != d.has_value() ||
                    (c && std::memcmp(&*c, &*d, sizeof(omk::GroundHit)) != 0)) ++mismatched;
            };
            std::size_t off = 0;
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& w = worldSlots[static_cast<std::size_t>(sl)];
                if (w.stem.empty()) continue;
                for (std::size_t k = 0; k < movedSoup[sl].size(); k += 7) {
                    const std::size_t o = off + 9 * static_cast<std::size_t>(movedSoup[sl][k]);
                    check((double(playerSoup[o]) + playerSoup[o + 3] + playerSoup[o + 6]) / 3.0,
                          (double(playerSoup[o + 1]) + playerSoup[o + 4] + playerSoup[o + 7]) / 3.0 - 200.0,
                          (double(playerSoup[o + 2]) + playerSoup[o + 5] + playerSoup[o + 8]) / 3.0);
                }
                off += w.soup.size();
            }
            for (int gx = -4; gx <= 4; ++gx)
                for (int gz = -4; gz <= 4; ++gz)
                    check(1804.0 + gx * 900.0, -2000.0, -6890.0 + gz * 900.0);
            if (frames % 30 == 0)
                std::printf("split verify: %ld moving frames, %ld probes, %ld mismatched, fixed %d x %d, moving %zu entries "
                            "in %d x %d (steep moving %d x %d, %zu entries)%s\n",
                            frames, probes, mismatched, playerGrid.fixed.nx, playerGrid.fixed.nz,
                            playerGrid.moving.index.size(), playerGrid.moving.nx, playerGrid.moving.nz,
                            playerSteepGrid.moving.nx, playerSteepGrid.moving.nz,
                            playerSteepGrid.moving.index.size(),
                            playerGrid.useParts ? (" - the moving layer is " +
                                std::to_string(playerGrid.parts.size()) + " and " +
                                std::to_string(playerSteepGrid.parts.size()) +
                                " whole meshes, no grid").c_str() : "");
        }
    }

    mark("scripted motion");
}

// Adventure mode's and the scene's own sound effects
void PlayState::inputSounds() {
    OMK_ZONE("input: sounds");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    // ---- ADVENTURE MODE'S SOUND EFFECTS -----------------------------
    //
    // A cutscene's sound rides on a scene object's program; the player's
    // rides on the `.CTL` state machine, and the two use OPPOSITE lookups.
    // `Cef_TickEffects` resolves its `+22` with `Scene_FindSoundIndex` -
    // a search of the resident scene's chunk-3 records for a matching
    // `+24` ID - where a scene program's param 0 is a bounds-checked
    // INDEX. So `H_WALK`'s 203/199 name `STPR`/`STPL` in the Impasse and
    // may name nothing at all in a scene that does not carry them, which
    // is the engine's behaviour and not a gap here.
    if (player) {
        // the sprite half of the records, spawned as Cef_UpdateStateEffects does
        const int   st = player->ctlState();
        const float fr = player->channelFrame();
        if (st != ctlFxState || fr < ctlFxFrame) {
            // A NEW STATE (or a wrap) kills what the old one spawned - a
            // PORT SIMPLIFICATION, labelled: the engine keeps an instance
            // across states unless flag 0x10 is set, on a clock that keeps
            // running; every shipped record here dies inside its window.
            ctlSprites.clear();
            ctlFxState = st;
            for (const auto& e : player->stateEffects()) {
                if (!e.sprite || (e.flags & 2)) continue;
                ctlSprites.push_back({e.sprite, e.duration, e.from, e.to, e.scale,
                                      e.flags, e.attach, st});
                if (!spriteTab.texOf(e.sprite) || !spriteTab.texOf(e.sprite)->hasPixels())
                    std::printf("ctl-effect: sprite %d is not registered by the library or the "
                                "scene - nothing will draw\n", e.sprite);
                std::printf("ctl-effect: state %d '%s' spawns sprite %d on attach %d "
                            "('%s') for %.0f frames from %.0f, scale %.2f, flags 0x%02x\n",
                            st, player->clipName().c_str(), e.sprite, e.attach,
                            e.attach < 18 ? kAttachName[e.attach] : "Buste",
                            e.duration, e.from, e.scale, e.flags);
            }
        }
        ctlFxFrame = fr;
        for (std::size_t i = 0; i < ctlSprites.size();) {
            const auto& c = ctlSprites[i];
            const float to = c.to == 0.0f ? 10000.0f : c.to;
            if (fr > c.from + c.duration || fr > to) ctlSprites.erase(ctlSprites.begin() + static_cast<long>(i));
            else ++i;
        }
    }
    // THE OPPONENT'S SPRITE RECORDS, by the player's rule above: spawned on
    // entering a state (or a wrap), each dying out of its window.
    if (fightRun.active && fightRun.foeChannel) {
        const auto& fctl = fightRun.foeChannel->ctl();
        const int   st = fightRun.foeChannel->state();
        const float fr = fightRun.foeChannel->frame();
        if (st != foeFxState || fr < foeFxFrame) {
            foeSprites.clear();
            foeFxState = st;
            if (st >= 0 && st < static_cast<int>(fctl.states.size()))
                for (const auto& e : fctl.states[static_cast<std::size_t>(st)].effects) {
                    if (!e.sprite || (e.flags & 2)) continue;
                    foeSprites.push_back({e.sprite, e.duration, e.from, e.to, e.scale,
                                          e.flags, e.attach, st});
                    std::printf("frame %ld: ctl-effect: OPPONENT state %d '%s' spawns sprite %d on "
                                "attach %d ('%s') for %.0f frames from %.0f\n", n, st,
                                fctl.states[static_cast<std::size_t>(st)].name.c_str(),
                                e.sprite, e.attach,
                                e.attach < 18 ? kAttachName[e.attach] : "Buste",
                                e.duration, e.from);
                }
        }
        foeFxFrame = fr;
        for (std::size_t i = 0; i < foeSprites.size();) {
            const auto& c = foeSprites[i];
            const float to = c.to == 0.0f ? 10000.0f : c.to;
            if (fr > c.from + c.duration || fr > to) foeSprites.erase(foeSprites.begin() + static_cast<long>(i));
            else ++i;
        }
    } else if (!foeSprites.empty()) {
        foeSprites.clear();
        foeFxState = -1;
    }
    if (player && globalRt) {
        // The library, see its construction - and `fight.scx` OVER it
        // while a fight is running, because that is what `Fight_Begin`'s
        // own `Game_Start` installs and the two id spaces do not overlap
        // (the fight's are 402..423).
        // BOTH FIGHTERS, not just the player. `Cef_TickEffects` runs on
        // every channel the engine ticks, and
        // `Actor_TickPlayerAndOpponent` ticks two - so the opponent's hit
        // reactions carry their own cries and impacts. This drained only
        // the player's, which is why a reader heard and saw a blow *"only
        // when the player is touched"* (`todo/fight-mode.md` 15.10).
        std::vector<omk::CefChannel::EffectSound> chanSounds = player->sounds();
        if (fightRun.active && fightRun.foeChannel)
            for (const auto& es : fightRun.foeChannel->sounds())
                chanSounds.push_back(es);
        for (const auto& es : chanSounds) {
            const omk::ScxRuntime* rt = nullptr;
            int i = -1;
            if (fightRt && fightRun.active) {
                i = fightRt->wavBydId(es.id);
                if (i >= 0) rt = fightRt.get();
            }
            if (i < 0) {
                i = globalRt->wavBydId(es.id);
                if (i >= 0) rt = globalRt.get();
            }
            if (!rt) {
                std::printf("ctl-effect: sound id %d is in neither the global "
                            "library nor fight.scx\n", es.id);
                continue;
            }
            const SfxSample& sm = sfxPcm(rt->wavData(i));
            if (sm.pcm->size) { sfxLog("ctl-effect", sm.pcm->size, es.id, i, 1.0f, &sm.peak);
                                    front.playSound(sm.pcm, false, fxGain()); }
        }
    }

    // ---- THE SCENE'S OWN SOUND EFFECTS ------------------------------
    //
    // An object's animation carries its sound: `Script_PlaySound` and
    // `Script_PlaySyncSound` hang off the body animation through the
    // `+12` sync link and run in the same chain walk, which is why the
    // Impasse's arrival clip fires STPR/STPL at frames 170, 200, 210 and
    // 280 - Kay'l's footsteps. `SceneRunner` reports what each frame
    // started; the payload is a whole RIFF sitting in the `.SCX` stream.
    //
    // POSITION IS NOT APPLIED. `Sound_Play3D` takes the node's world
    // position and a pair of distances, and the attenuation and pan law
    // beyond that point is DirectSound's - `PORTING` B5: it has no
    // reachable tier and imitating it precisely would be invention. The
    // cue, its timing and its loop flag are the decisions, and those are
    // what this plays.
    {
        const auto& sc = session.scene();
        if (sc.file() != sfxSceneWas) {          // the scene's library changed
            sfxCache.clear();
            sfxSceneWas = sc.file();
        }
        for (const auto& fs : sc.sounds()) {
            // `Script_StopSound` (omk-play 71): the voice playing this
            // wav on this node is silenced, not every voice of the wav -
            // the handler matches on BOTH, so the key is the pair.
            const auto key = std::make_pair(fs.cue.wav, fs.cue.node);
            if (fs.cue.stop) {
                const auto it = sceneVoices.find(key);
                if (it != sceneVoices.end()) {
                    front.stopSound(it->second);
                    sceneVoices.erase(it);
                    std::printf("scene sound: STOP wav %d node %d - %zu still looping\n",
                                fs.cue.wav, fs.cue.node, sceneVoices.size());
                } else {
                    std::printf("scene sound: stop asked for wav %d node %d, "
                                "which is not looping (%zu are)\n",
                                fs.cue.wav, fs.cue.node, sceneVoices.size());
                }
                continue;
            }
            // A LOOPING cue is started ONCE. Its program re-reaches the
            // function every cycle - wav 23 came round 33 times in 521
            // frames, a 1.76 s sample overlapping itself three deep and
            // thrashing the 8-slot pool - and the engine does not restart
            // a sound that is already looping: the loop lives in the
            // mixer, which is what the flag is FOR.
            if (fs.cue.loop && sceneVoices.count(key)) continue;
            const auto raw = sc.scene().wavData(fs.cue.wav);
            if (raw.empty()) continue;      // 186 of 5425 name a sound
                                            // their scene does not carry;
                                            // `sub_48CB30` returns -1 and
                                            // the engine plays nothing
            const SfxSample& sm = sfxPcm(raw);
            if (!sm.pcm->size) continue;
            // ---- POSITIONAL, because `Script_PlaySound` is 3D ------
            //
            // The handler (0x004A12D0) ends in three calls to
            // `Sound_Play3D` (0x0046CDC0), so a scene sound is placed and
            // attenuated by distance from the listener. Played flat, an
            // ambience is as loud across the city as beside it and goes on
            // through a cutscene whose camera is nowhere near it - which
            // is exactly what a reader reported (omk-play 73).
            //
            // **RECONSTRUCTION, and labelled in three places** (this, the
            // frontend, and the entry): WHERE the sound is comes from the
            // program's own motions this frame - the cue names a NODE and
            // the port cannot resolve a scene node to a world point, so
            // the object being animated stands in for it. And the CURVE is
            // this port's own: DirectSound owned the attenuation law and
            // `PORTING`'s audio row records that it has NO reachable tier,
            // so a plausible inverse-distance is the honest most that can
            // be done. What IS the engine's is the unit - the listener is
            // told the world unit is an INCH.
            float gain = 1.0f;
            float sceneSoundDist = -1.0f;
            {
                const auto mo = sc.motionsOf(fs.program);
                if (!mo.empty() && player) {
                    const float* L = player->pos();
                    float best = 1e30f;
                    for (const auto& m : mo) {
                        // the PLACED position, not the raw sample
                        const auto* pl = motionAtFind(m.name);
                        const float* w = pl ? pl->data() : m.pos;
                        const float dx = w[0] - L[0], dy = w[1] - L[1],
                                    dz = w[2] - L[2];
                        best = std::min(best, dx * dx + dy * dy + dz * dz);
                    }
                    const float d = std::sqrt(best);
                    // full within a room's width, then 1/d out to silence
                    constexpr float kNear = 120.0f;    // inches: ~3 m
                    constexpr float kFar  = 4000.0f;   // ~100 m
                    gain = d <= kNear ? 1.0f
                         : d >= kFar  ? 0.0f
                         : kNear / d;
                    sceneSoundDist = d;
                }
            }
            sfxLog(fs.cue.loop ? "scene-LOOP" : "scene-sound",
                   sm.pcm->size, fs.cue.wav, fs.object, gain, &sm.peak);
            if (sceneSoundDist >= 0.0f)
                std::printf("audio:   ...at %.0f units from the nearest motion of object %d\n",
                            static_cast<double>(sceneSoundDist), fs.object);
            if (gain <= 0.01f) continue;      // too far to hear at all
            const int h = front.playSound(sm.pcm, fs.cue.loop, gain * fxGain());
            // only a LOOPING cue needs remembering - a one-shot ends by
            // itself and the handle would go stale
            if (h >= 0 && fs.cue.loop) {
                sceneVoices[key] = h;
                // A stop can only be judged against what is PLAYING, so
                // say what is looping and keep the list current. One-shots
                // are not listed: they end on their own and nothing can
                // stop them.
                std::printf("scene sound: LOOP wav %d node %d (object %d, "
                            "program %d) - %zu looping now:",
                            fs.cue.wav, fs.cue.node, fs.object, fs.program,
                            sceneVoices.size());
                for (const auto& v : sceneVoices)
                    std::printf(" [wav %d node %d]", v.first.first, v.first.second);
                std::printf("\n");
            }
        }
    }

    mark("sounds");
}

// ---- THE MOVING COLLISION, PLACED ON DEMAND (todo/cpu-vs-original.md tier C)
//
// A moving mesh's collision triangles used to be re-placed every frame it
// moved - ~2730 in Anekbah, most of them far from anything that probes. Now
// the frame RECORDS the placement (`lazyMeshes`) and the triangles are placed
// the first time a query reaches the mesh: a grid query through its part
// (`SplitSoupGrid::place`), a linear one through `omk::ensurePlaced`, which
// places all. The same rest, the same `placePoints`, the same placement - so
// the same bits as the eager patch, only later or never.

std::size_t PlayState::slotSoupOffset(int sl, bool steep) const {
    std::size_t off = 0;
    for (int k = 0; k < sl; ++k) {
        const WorldSlot& w = worldSlots[static_cast<std::size_t>(k)];
        if (w.stem.empty()) continue;
        off += steep ? w.steep.size() : w.soup.size();
    }
    return off;
}

void PlayState::lazyPlace(int key) {
    auto it = lazyMeshes.find(key);
    if (it == lazyMeshes.end() || !it->second.pending) return;
    LazyMesh& lm = it->second;
    if (lm.gen != worldGen) {             // recorded against soups since replaced
        lm.pending = false;
        if (--lazyPending <= 0) { lazyPending = 0; lazyRegister(); }
        return;
    }
    WorldSlot& w = worldSlots[static_cast<std::size_t>(lm.sl)];
    const auto mi = static_cast<std::size_t>(lm.mi);
    const auto placeLayer = [&](omk::TriangleSoup& soup, const std::vector<float>& rest,
                                const std::vector<std::uint32_t>& tris, omk::TriangleSoup& merged,
                                std::size_t off) {
        if (tris.empty() || rest.size() != 9 * tris.size()) return;
        // the rest written back, then placed in place - `patchSoup`'s steps
        for (std::size_t k = 0; k < tris.size(); ++k)
            std::copy(rest.begin() + 9 * k, rest.begin() + 9 * k + 9, soup.begin() + 9 * tris[k]);
        static std::vector<std::uint32_t> pts;   // main thread only
        pts.clear();
        for (const std::uint32_t t : tris) {
            pts.push_back(3 * t); pts.push_back(3 * t + 1); pts.push_back(3 * t + 2);
        }
        omk::placePoints(lm.pp, soup.data(), 3, soup.data(), 3, pts.data(), pts.size());
        // ...and into the merged soup at the slot's offset, as the merge does
        if (mergedValid)
            for (const std::uint32_t t : tris)
                if (off + 9 * static_cast<std::size_t>(t) + 9 <= merged.size())
                    std::copy_n(soup.data() + 9 * static_cast<std::size_t>(t), 9,
                                merged.data() + off + 9 * static_cast<std::size_t>(t));
    };
    if (mi < w.soupTrisOfMesh.size())
        placeLayer(w.soup, w.restSoupOfMesh[lm.mi], w.soupTrisOfMesh[mi], playerSoup,
                   slotSoupOffset(lm.sl, false));
    if (mi < w.steepTrisOfMesh.size())
        placeLayer(w.steep, w.restSteepOfMesh[lm.mi], w.steepTrisOfMesh[mi], playerSteep,
                   slotSoupOffset(lm.sl, true));
    lm.pending = false;
    for (auto* g : {&playerGrid, &playerSteepGrid})
        for (const auto& p : g->parts)
            if (p.owner == key) p.pending = false;
    ++lazyPlacedFrame;
    if (--lazyPending <= 0) { lazyPending = 0; lazyRegister(); }
}

void PlayState::lazyPlaceAll() {
    for (auto& kv : lazyMeshes)
        if (kv.second.pending) lazyPlace(kv.first);
}

void PlayState::lazyRegister() {
    // the table cleared and refilled with the soups' CURRENT addresses: a
    // vector that reallocated would otherwise leave its old one behind
    omk::clearPendingSoups();
    if (lazyPending <= 0) return;
    const std::function<void()>* fn = &lazyPlaceAllFn;
    omk::setPendingSoup(playerSoup.data(), fn);
    omk::setPendingSoup(playerSteep.data(), fn);
    for (const WorldSlot& w : worldSlots) {
        omk::setPendingSoup(w.soup.data(), fn);
        omk::setPendingSoup(w.steep.data(), fn);
    }
}

void PlayState::lazyForget() {
    lazyPending = 0;
    lazyRegister();          // unregister while the soups are still the old ones
    lazyMeshes.clear();
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: adventure mode.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupAdventure() {
    auto& session = *session_;
    // ---- ADVENTURE MODE ------------------------------------------------
    //
    // The Impasse's cutscene ends `camera.set 0,0,2` + `scene.load 237,57` +
    // `player.anim.release`: the hand-over to the person at the keyboard.
    // Until 2026-09-02 nothing here moved him after it. The controller
    // (`actor/player.h`) is the engine's own chain - the input word into the
    // `.CTL` channel, the clip's root motion out of it, the walker under it,
    // the follow camera behind - and this file only builds it, feeds it the
    // word `Input::frame` makes, and draws where it says.
    //
    // WHEN: the Session's camera is a world camera whose two subjects are
    // actor 0 (SCENE 55's camera 0 - an eye 119 behind and 26 above him,
    // the follow shape `worldcam.h` describes), no scene program is playing
    // the player, he has been placed, and no conversation, screen or
    // editing owns the frame. `player.anim.release` (op 105) is a no-op in
    // the Session, so the program ending is the signal it leaves - which is
    // also what `Actor_TickScxDriven` keys on (the program's `IsBusy`).
    // `Player_GoToMove` behind `player.move` (63) and `player.move.wait` (89)
    // - `PlayerController::goToMove`. Neither reached the walker until
    // 2026-09-08: op 63 was recorded and dropped, and 89 ran on because no
    // hook was installed, so the `player.move 100` in front of every staged
    // sequence never stopped him and he played his walk out under the
    // cutscene camera. In AREA 222's tutorial that is 19 units short of the
    // zone he had just been teleported out of.
    //
    // `moveWaitCtx` / `moveWaitGroup` are `dword_930744` / `dword_91068C`:
    // the context a 89 must report to, and the group it entered. `Game_Tick`
    // (0x004200F0) checks them after `Actors_TickAll` and raises event 3 the
    // frame the channel's current entry is in some other group - the release
    // below, before the next pump.
    moveWaitCtx = -1, moveWaitGroup = -1;
    session.setMoveHook([&](int groupId, int ctx) -> bool {
        if (!player) return false;
        if (!player->goToMove(groupId)) {
            std::printf("frame %ld: player.move%s %d - no group %d in the player's bank, "
                        "the script runs on\n", session.frameNo(),
                        ctx >= 0 ? ".wait" : "", groupId, groupId);
            return false;
        }
        const int g = player->ctlGroup();
        const int st = player->ctlState();
        const char* nm = (st >= 0 && st < static_cast<int>(playerCtl.states.size()))
                             ? playerCtl.states[static_cast<std::size_t>(st)].name.c_str()
                             : "?";
        if (ctx >= 0) { moveWaitCtx = ctx; moveWaitGroup = g; }
        std::printf("frame %ld: player.move%s %d - the channel put on group %d (index %d)'s "
                    "entry '%s' at %.1f %.1f %.1f%s\n", session.frameNo(),
                    ctx >= 0 ? ".wait" : "", groupId, groupId, g, nm,
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    ctx >= 0 ? ", the script parked until the channel leaves it" : "");
        return true;
    });
    // ...or, where the renderer poses bodies, his REST drawn with one affine a
    // mesh (`todo/gpu-skinning.md` step 5). `playerPosedFrame` is the last
    // frame his corners were built on the CPU - what a corner reader must check.
    playerPosedFrame = -1;
    playerReach = 0.0f;                           // his model's +88
    // ...and each of its triangles' MESH FLAGS, through the slots' `soupMesh`
    // (`todo/swimming.md`): what the step refusal and the water entry read.
    // Rebuilt whenever the soup it was built for is not the one there now.
    soupFlagsFor = nullptr;
    soupFlagsSize = 0;
    // True while `playerSoup` / `playerSteep` are exactly the shown slots' soups
    // concatenated - set by a full merge, cleared by a slot load - so a moving
    // mesh can be copied in at its offset instead of re-merging everything.
    mergedValid = false;
    playerReady = false, adventure = false, followCam = false;
    // A `scx.play.player` program owns his body right now (op 46/90).
    playerProgram = false;
    playerProgramWas = false;   // ...and did last frame, for the hand-back
    mirrorLive = false;         // the set's mirror is reflecting, said once
    mirrorSeen = false;         // ...and has actually covered a pixel
    playerHeadRise = 0.0f;         // the DRAWN head over the pelvis (Y up), the swim log
    playerHeadKnown = false;
                                         // floats a mesh like `Staged::meshRot` -
                                         // what the hit sweep turns his box by
    playerMeshAtKnown = false;
    placementSeen = 0;      // Session::placementSeq() as last consumed
    heldFrames = 0;         // frames under player.anim.hold
    // The `media.play` SUBTITLE: `Subtitle_Show(unk_4E6268)` is step 13 of
    // the handler (todo/pending/E1.md 1) - the ZVO record's +280 description,
    // `{C}`-prefixed when the player is in ACTOR_STATE 3 or 15, on screen for
    // 80 ms a character and never less than two seconds (`Subtitle_Show`,
    // readable/src/05_sys.c). The airlock's line 410 is the first the port
    // shows: its voice is a JINGOFF3 substitute, and the TEXT is what the
    // player reads.
    // In FRAMES AT 30 HZ, run down by the delta (todo/sixty-fps.md 2)
    mediaTextFrames = 0;
    // THE MEDIA BITMAP - `media.play` on a kind-16 DOCUMENT.
    //
    // `if (rec[+2] == 16)` takes the other arm entirely: build
    // `IMAGES\<stem>.BMP`, `I2D_LoadBitmap` it, put the player in ACTOR_STATE
    // **10** (`ImageScreen`, "a full-screen bitmap holds it") and play NO
    // audio. It stays up until the NEXT `media.play`, which frees it (step 7,
    // `I2D_FreeBitmap` then ACTOR_STATE 1).
    //
    // That is the game's TITLE CARD: object 715 `ZVO G001 TITRE` is kind 16
    // with stem `ZVOG001`, so its `+280` description is `{X030040}{f3}` and
    // nothing else - the words are in `IMAGES/ZVOG001.BMP`, 640x480 with the
    // logo on black. Nothing here drew it, which is why the Bowie opening
    // came up without its title (`todo/omk-play.md` 59).
    playerFeet = 0.0f;
    playerFeetKnown = false;
    // ...AND THE PELVIS TRANS THE ANCHOR WAS LATCHED AT, which is the whole of
    // `todo/player-vertical.md` step 1. `playerFeet` is a POSE's lowest corner
    // and every pose carries its own pelvis translation, so an anchor latched
    // without recording that translation has no shared origin with the drop
    // measured below - see the long note at the latch.
    playerRootRef = 0.0f;
    playerStandLatched = false;   // the anchor came from H_STAND, not from whatever was up
    // The model-space x/z of the hierarchy root - the PELVIS - which is what
    // a turn must pivot about. `HO1_FN`'s is (2.87, 17.94); rotating about
    // (0,0) instead swings him around a point half a metre away.
    lastRootDrop = 0.0f;         // the crouch's root drop as drawn last frame (the held prop rides it)
    playerCamId = -2;
    // The facing at the hand-over. `Actor_TickScxDriven` sets +1308 when
    // the player's program ends and `Actor_TickNpc` then derives the facing
    // from the node's matrix - which after a scene clip is the clip's root
    // rotation at the frame reached. Tracked while the program runs, since
    // `scene.load` replaces the runner and its clips with it.
    handoverFacing = 0.0f;
    handoverFacingKnown = false;
    // A player program has RUN in this area: the hand-over is its ending,
    // not its absence. SCENE 55's startup script opens with `camera.set 0`
    // before any beat starts, so a camera-only signal fired a frame into
    // the Impasse with GRID's floor and the DB record from before
    // `player.become 49` - the first headless run showed exactly that.
    // Reset on every area change; an area whose scene has no programs at
    // all (a plain arrival) needs no beat to end.
    playerDrivenSeen = false;
    playerDrivenArea = -1;
    frameSec = 1.0 / 30.0;
    // GAME TIME in frames at 30 Hz: the sum of the deltas, so it stands still
    // under the pause and runs at the same speed at any presentation rate.
    // `n` counts PRESENTED frames and is the clock of the harness and the
    // log, never of anything the game times (todo/sixty-fps.md 2).
    gameClock = 0.0;
    // (struct HoldRun: `backends/sdl/playtypes.h`, todo/play-split.md)
    {
        std::string cur;
        for (char c : holdStream + ",") {
            if (c != ',') { cur.push_back(c); continue; }
            if (cur.empty()) continue;
            HoldRun r;
            const auto star = cur.find('*');
            const std::string head = star == std::string::npos ? cur : cur.substr(0, star);
            r.frames = star == std::string::npos ? 1 : std::atoi(cur.c_str() + star + 1);
            if (!head.empty() && head[0] == 'k') {
                std::string k;
                for (char h : head.substr(1) + "+") {
                    if (h == '+') { if (!k.empty()) r.keys.push_back(std::atoi(k.c_str())); k.clear(); }
                    else k.push_back(h);
                }
            }
            holds.push_back(r);
            cur.clear();
        }
    }
    handoverFrame = -1;

    OMK_HEAPCHECK("before interface sounds");
    return -1;
}

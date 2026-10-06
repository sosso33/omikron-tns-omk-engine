// SPDX-License-Identifier: GPL-3.0-or-later
// ADVENTURE MODE'S CONTROLLER FRAME, in its parts.
// Parts of `PlayState::controlAdventure`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// The follow camera's offsets, first-person aim
void PlayState::adventureAim() {
    // The follow camera is the world camera the script named -
    // SCENE 55's camera 0 carries its own offsets and travels
    // the same resolve as the mode-0 preset. Its three trailing
    // shorts (the smoothing divisors) are not lifted into
    // `WorldCamera`; the preset's 3/8/8 stand in, labelled.
    if (followCam && hc && hc->id != playerCamId) {
        playerCamId = hc->id;
        // ...AND THAT TAKES THE SHOOT CAMERA AWAY. `Shoot_Enter`
        // does `Camera_Request(4, ...)` with both camera actors
        // set to the player; whether mode 4 outranks a script's
        // own camera is NOT read, so this port lets the script
        // win and simply records that the first-person camera is
        // no longer in force. The player's body follows that flag
        // rather than the shoot-mode flag - see `drawPlayer`.
        shootCameraLive = false;
        player->setCameraOffsets(hc->eye, hc->at, hc->fov);
        std::printf("frame %ld: follow camera %d - eye offset %.0f %.0f %.0f, "
                    "target offset %.0f %.0f %.0f, fov %.0f (smoothing 3/8/8 "
                    "from the mode-0 preset)\n", n, hc->id,
                    hc->eye[0], hc->eye[1], hc->eye[2],
                    hc->at[0], hc->at[1], hc->at[2], hc->fov);
    }
    // ---- FIRST-PERSON AIM, while the shoot camera holds ----
    //
    // The mouse turns the BODY in yaw and the CAMERA in pitch.
    // Yaw goes through the player because the shoot scheme's own
    // `Tourner a gauche/droite` do the same thing, so the two
    // agree; pitch is the camera's alone, because nothing in the
    // 14-slot input word carries a pitch - the scheme's
    // `Regarder En-Haut/En-Bas` are two more bits, not an axis.
    // Second half of the mouse diagnostic. The first line is at
    // the top of the loop, before any gate; this one is INSIDE
    // `if (adventure)`, so the two together say which link is
    // broken: no MOUSE line at all means the frontend reports no
    // motion; a MOUSE line with no AIM line means `adventure` is
    // false and the block is never reached; both, with
    // `cameraLive 0`, means something clears the shoot camera
    // after entry.
    if (shootMode) {
        static long aimTold = -1;
        if (aimTold < 0 || n - aimTold > 120) {
            aimTold = n;
            std::printf("frame %ld: AIM reached - cameraLive %d, "
                        "pitch %.1f, facing %.1f\n", n,
                        int(shootCameraLive), shootPitch,
                        player->facing());
        }
    }
    if (shootMode && shootCameraLive &&
        (host.mouseDX != 0.0f || host.mouseDY != 0.0f || shootPitchDirty)) {
        // `sub_47D370`, reached by `sub_45D1D0(player, dx, dy)` from
        // the mouse's own arm (0x4A8278: the cursor's offset from
        // the window centre, which it then re-centres; 0x4A82AA:
        // the DirectInput deltas). YAW `+420 -= row23 * 0.01 * dx`,
        // no frame delta; PITCH `+= row24 * 0.01 * dy * delta`, dy
        // negated unless row 25 is set, clamped at +-45
        // (`actor/shootmove.h`). The pitch's absolute SIGN is this
        // viewer's camera's, anchored on the reader's confirmed
        // sense at the default (row 25 off) - so `shootPitch` is the
        // engine's pitch negated. `--invert-x` is this port's own:
        // the engine has no yaw invert.
        const int dx = static_cast<int>(std::lround(host.mouseDX));
        const int dy = static_cast<int>(std::lround(host.mouseDY));
        if (dx != 0)
            player->aimYawBy(omk::shootTurnDegrees(dx, mouseSensX) *
                             (mouseInvertX ? -1.0f : 1.0f));
        // `sub_47D370`'s own guard: a Mecagarde's pitch is PINNED
        // at 0 (mover flag 0x1000, written by `sub_47CC70`), and
        // `sub_47C260` is not called at all on that arm.
        if (dy != 0 && omk::shootMovePitches(shootMover))
            shootPitch = -omk::shootPitchStep(-shootPitch, dy, mouseSensY,
                                              mouseInverted,
                                              static_cast<float>(frameSec * 30.0));
        else if (dy != 0)
            shootPitch = 0.0f;                // `dword_657A10 = 0.0`
        shootPitchDirty = false;
        // `camera_presets.json` row 4: eye (0,0,0) on the player,
        // target (0, 0, 787.4016) - 20.00 m in front. Pitching it
        // swings that target up and down about the eye.
        const float rad = shootPitch * 3.14159265f / 180.0f;
        const float eye[3] = {0.0f, 0.0f, 0.0f};
        const float at[3]  = {0.0f, 787.4016f * std::sin(rad),
                              787.4016f * std::cos(rad)};
        player->setCameraOffsets(eye, at, 75.0f);
    }
    // A TELEPORT under him: `actor.goto_address` wrote the
    // Session's position outright (the airlock beat's 653,
    // 'Tutorial'). Without this the next line writes the walker's
    // old position straight back over it.
    // (the placement itself is consumed above `if (adventure)`,
    // whatever mode the frame is in - see there)
}

// The path field
void PlayState::adventurePathField() {
    // THE PATH FIELD - `Shoot_TickPlayer` (05_sys.c 7519), which the
    // player's tick runs before any gunman thinks: seeded at his cell
    // when his floor changes, grown 100 cells a tick, and reseeded
    // the moment it runs dry, so it follows him. LABELLED: his cell
    // is `cellAt` on the floor `floorAt` finds under him; the engine
    // takes `Shoot_Think`'s, and when that refuses the cell, one from
    // `sub_4368E0` (unread).
    if (shootMode && shootMap.valid()) {
        // his POSITION as the engine holds it, +244..252 - the root
        // node, the pelvis, as drawn last frame (the bolts' aim point
        // below) - and `pos()`, the feet, only until he has been. At
        // the feet the supermarket's player stands at y -89, exactly
        // on its grid's upper bound, and `floorAt` found no floor
        float pp[3] = {player->pos()[0], player->pos()[1], player->pos()[2]};
        for (std::size_t i = 0; i < playerMeshes.size(); ++i)
            if (playerMeshes[i].parent < 0) {
                if (playerMeshAtKnown && playerMeshAt.size() >= i * 3 + 3)
                    for (int k = 0; k < 3; ++k) pp[k] = playerMeshAt[i * 3 + static_cast<std::size_t>(k)];
                break;
            }
        // THE PLAYER'S OWN SHOOT RECORD, kept as `Shoot_TickPlayer`
        // keeps it (05_sys.c 7498): `Shoot_Think` on HIM every
        // frame - his floor `+188` always, his cell `+136/+140` when
        // the cell is standable - and when it refuses, `sub_4368E0`
        // walks the point to the nearest standable cell and the cell
        // is that point's. Until 2026-09-13 the viewer seeded the
        // path field from a cell of its own and left the record at
        // `node = -1`, so the engage's GRID sight - which walks from
        // the TARGET record's cell - had no start (`todo/shoot-sight.md`).
        const int oldFloor = static_cast<signed char>(playerShootRec.node & 0xFF);
        const bool thought = omk::shootThink(playerShootRec, shootMap, pp, 0, -1);
        playerShootRec.flags &= ~0x1000u;          // `BYTE1(v11) &= ~0x10`
        const int fl = static_cast<signed char>(playerShootRec.node & 0xFF);
        int cx = 0, cz = 0;
        bool haveCell = false;
        float snapX = pp[0], snapZ = pp[2];
        bool snapped = false;
        if (fl >= 0) {
            if (thought) {
                cx = playerShootRec.destX;
                cz = playerShootRec.destZ;
            } else {
                snapped = shootMap.snapToStandable(fl, snapX, pp[1], snapZ);
                shootMap.cellAt(fl, snapX, snapZ, cx, cz);
            }
            haveCell = true;
        }
        {
            static std::array<int, 4> recTold{-9, -9, -9, -9};
            const std::array<int, 4> key{fl, cx, cz, thought ? 1 : (snapped ? 2 : 3)};
            if (key != recTold) {
                recTold = key;
                std::printf("frame %ld: the player's shoot record (Shoot_TickPlayer): "
                            "floor %d (was %d), cell (%d,%d) - %s at %.0f %.0f\n", n, fl,
                            oldFloor, cx, cz,
                            thought ? "Shoot_Think took it"
                                    : (snapped ? "Shoot_Think refused, sub_4368E0 snapped"
                                               : (fl >= 0 ? "Shoot_Think refused, no "
                                                            "standable cell in reach"
                                                          : "no floor")),
                            double(snapX), double(snapZ));
            }
        }
        if (haveCell) {
            static long fieldFrom = -1, fieldTold = -1;
            if (!shootField.seeded() || shootField.floor() != fl) {
                shootField.seed(shootMap, fl, cx, cz);
                if (fieldFrom < 0) fieldFrom = n;
            }
            if (shootField.expand(shootMap)) {
                if (fieldTold < 0) {
                    fieldTold = n;
                    std::printf("frame %ld: the path field (sub_436260 / sub_436350) ran "
                                "dry %ld ticks after it was seeded at the player's cell "
                                "(%d,%d) on floor %d\n", n, n - fieldFrom + 1, cx, cz, fl);
                }
                shootField.seed(shootMap, fl, cx, cz);
            }
        } else {
            // once: no floor of the grid under him, so no field
            static bool noFloorTold = false;
            if (!noFloorTold) {
                noFloorTold = true;
                std::printf("frame %ld: the path field has no floor under the player at "
                            "%.0f %.0f %.0f (floor %d); the grid's floors span y:", n,
                            double(pp[0]), double(pp[1]), double(pp[2]), fl);
                for (const auto& gf : shootMap.floors())
                    std::printf(" [%.0f..%.0f x %.0f..%.0f z %.0f..%.0f]",
                                double(gf.bound[2]), double(gf.bound[3]),
                                double(gf.bound[0]), double(gf.bound[1]),
                                double(gf.bound[4]), double(gf.bound[5]));
                std::printf("\n");
            }
        }
    }
}

// The death's countdown
void PlayState::adventureDeath() {
    auto& session = *session_;
    // THE DEATH'S COUNTDOWN - `Shoot_TickPlayer`'s first arm (05_sys.c
    // 7454): in ACTOR_STATE 15 `dword_4E975C` runs down by the frame
    // delta while his death clip plays; at 0 he is put back in 3,
    // MESSAGE 1 goes out (SCENE 56's is the phase LOST), property 1 is
    // read back into +92 (event 44), and .CTL group 200's default entry
    // - the stance - is his again. NOT PORTED, labelled: his body hidden
    // again (`sub_436CE0` - this port draws it throughout), camera 4
    // requested (the shoot camera is this port's own in shoot mode),
    // `sub_47CC70`, and the held weapon re-attached and re-inited.
    if (shootMode && player && player->state() == omk::ActorState::Shoot15) {
        if (playerDeathCountdown > 0.0f) {
            playerDeathCountdown -= static_cast<float>(frameSec * 30.0);   // `flt_4C30D8`
        } else {
            player->setActorState(omk::ActorState::Shoot, "Shoot_TickPlayer");
            const bool ran1 = session.postMessage(1, session.playerActor());
            std::int32_t h = 0;
            const std::size_t recAt = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
            const std::size_t recLen = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
            omk::readActorProperty(state.raw().subspan(recAt, recLen), 1, h);
            playerShootRec.health = h;
            const bool g200 = player->enterGroupById(200);
            std::printf("frame %ld: the player's death clip is over (Shoot_TickPlayer): "
                        "ACTOR_STATE 3, message 1 %s, health read back %d, .CTL group "
                        "200 %s\n", n, ran1 ? "to its handler" : "- NO handler subscribes",
                        int(h), g200 ? "on" : "NOT FOUND");
        }
    }
}

// The crowd push
void PlayState::adventureCrowdPush() {
    auto& session = *session_;
    // THE CROWD PUSH - `Actor_TickNpc`, before `Actor_ApplyMotion`:
    // the spatial index's answer for his spheres, added to his
    // position outright (docs/STREET_LIFE.md 3). The Session
    // posts the bump message when a walker was touched.
    {
        float push[3];
        // ...and NOT while he boards or leaves. ACTOR_STATE 6 and
        // 8 tick through `Actor_TickChannelOnly`, and the push is
        // `Actor_TickNpc`'s - it never runs for them. It ran here:
        // `MDACTION` snaps him INSIDE the vehicle's own body
        // sphere, so the pool shoved him out every frame faster
        // than `H_SLDIN` walked him in - 25 units in the first
        // five frames, before the clip had moved him at all - and
        // he finished the clip beside the slider with the door
        // open above him. Traced frame by frame 2026-09-08.
        if (!boarding && !leaving && !playerSpheres.empty() &&
            session.crowdPush(playerSpheres, playerReach, player->pos(), player->facing(), push)) {
            const float was[3] = {player->pos()[0], player->pos()[1], player->pos()[2]};
            player->nudge(push);
            // once: a push the SWEEP cut short - a wall in the way
            // (`Actor_ApplyMotion` hands it to `Actor_Move`)
            static bool pushWallTold = false;
            const float gx = player->pos()[0] - was[0], gz = player->pos()[2] - was[2];
            const float asked = std::sqrt(push[0] * push[0] + push[2] * push[2]);
            if (!pushWallTold && asked - std::sqrt(gx * gx + gz * gz) > 0.5f) {
                pushWallTold = true;
                std::printf("frame %ld: the push %.2f %.2f met a wall - the sweep "
                            "(Actor_ApplyMotion -> Actor_Move) moved him %.2f %.2f\n",
                            n, double(push[0]), double(push[2]), double(gx), double(gz));
            }
            // once per gunman: his body shoved the player
            static std::set<int> bodyPushTold;
            for (const int sl : session.spatial().lastTouched()) {
                const int who = session.actorOfBodySlot(sl);
                if (who >= 0 && bodyPushTold.insert(who).second)
                    std::printf("frame %ld: the player is pushed out of actor %d's "
                                "body (SpatialIndex_Query, sub_45E390): %.2f %.2f\n",
                                n, who, double(push[0]), double(push[2]));
            }
        }
    }
}

// A screen has the input, and the world still runs
void PlayState::adventureScreenInput() {
    const auto& fs = *fs_;
    auto& in = *in_;
    auto& session = *session_;
    // A SCREEN HAS THE INPUT, and the world still runs.
    //
    // Removing the old `!walk` gate (which froze all of Anekbah
    // behind the sneak) also handed the player the arrow keys
    // while the menu had them - a session log shows MDWALK,
    // MDROT000 and MDACTION firing after "screen 9 opened",
    // so moving the selection walked him down the street.
    //
    // The two are separate: `Game_Tick` runs `Actors_TickAll`
    // whatever is on screen, so the channel must keep ticking -
    // it is what carries a gait to its stand state - but the
    // INPUT WORD is the interface's while a screen is up.
    // `Ui_BeginScreen` installs its own repeat mask over the
    // device for exactly that reason. So this is the same shape
    // as `player.anim.hold` right below: tick with nothing
    // pressed rather than not ticking.
    if (walk) {
        player->tick(static_cast<float>(frameSec * 30.0), 0);
        playerTicked = true;
    } else if (session.playerAnimHeld()) {
        // `Actor_HoldAnimation(player, 1)` does NOT stop the
        // channel - it feeds it a lone IDLE word every tick and
        // cuts the device off, and the channel then keeps running
        // with no input at all.
        //
        // `sub_45A870` writes `queue[0] = 0x40000000; n = 1`, and
        // those two arrays are the input QUEUE and its LENGTH -
        // `Perso_InjectInput` (0x0045A9F0) is the proof, it fills
        // exactly them from its `a3`/`a2`. `Cef_TickChannel`
        // re-asserts both every tick while `flags & 0x81`, and the
        // channel's own rule then DROPS it: `if (n == 1 &&
        // (queue[0] & 0x40000000)) { queue[0] = 0; n = 0; }`
        // (ASSETS "a lone idle word is DROPPED").
        //
        // So the state machine ticks on with nothing pressed, which
        // is what carries a GAIT to its stand state. Not ticking at
        // all - what this did - leaves him mid-stride with one leg
        // forward for the whole held sequence (omk-play 43).
        ++heldFrames;
        player->tick(static_cast<float>(frameSec * 30.0), 0);
        playerTicked = true;
    } else {
        // `sub_4A7A20` NEVER YIELDS 0 - nothing held remaps to the
        // idle word - and the channel reads a literal 0 as
        // `kQueueDrives`, the "no device this tick" contract, which
        // SKIPS the input pass entirely. Passing the raw `bits`
        // therefore stopped the 20-slot latch from ever being
        // dropped once nothing was pressed (`latch_[i] = 0` lives
        // inside that pass), so a button spent by one press stayed
        // spent for ever and the SECOND press did nothing at all.
        // Invisible until the latch was made to survive
        // `SetPersoBankGroup` the way the engine's does.
        // The call's own arrival, announced once: state 2
        // drives until the 117 units are met, and then the slot
        // goes OPEN (3) - `MDSLIDIN`'s "slider is not in open
        // mode !" is the refusal that guards this.
        if (session.sliders().calledIsOpen() && !calledOpenTold) {
            calledOpenTold = true;
            float at[3] = {0, 0, 0};
            session.sliders().calledAt(at);
            // ...and WHERE, because "walk to it" is not the rule:
            // `MDACTION` wants him within 4.00 m AND on the
            // slider's own -X, and the door point is the placement
            // the arm would snap him to.
            float ax[3], az[3];
            if (player && session.sliders().calledFrame(at, ax, az)) {
                if (!doorOffState) {
                    const auto ref = fs.read("ANIMS/slf_112.3da");
                    doorOffState = (!ref.empty() &&
                                    player->boardOffset(ref, 60, doorOff)) ? 1 : -1;
                }
                float door[3] = {at[0], at[1] - omk::kBoardSeatY, at[2]};
                if (doorOffState == 1) {
                    for (int k = 0; k < 3; ++k)
                        door[k] += doorOff[0] * ax[k] + doorOff[2] * az[k];
                    door[1] += doorOff[1];
                }
                std::printf("slider: its DOOR is at %.0f %.0f %.0f - stand "
                            "within 4.00 m of that side (its -X, heading "
                            "%.2f %.2f) and press the action button\n",
                            door[0], door[1], door[2], az[0], az[2]);
                harnessBoard(at, door);
            }
            std::printf("slider: OPEN at %.0f %.0f %.0f - walk to "
                        "it and press the action button\n",
                        at[0], at[1], at[2]);
        }
        // ---- MDACTION'S SLIDER ARM -----------------------
        //
        // Boarding begins HERE and not at `MDSLIDIN`, which is a
        // correction: this port fired `MDSLIDIN` straight off the
        // action button, so there was no animation, no side test
        // and no camera. `MDACTION` (0x0046AEC0) takes its slider
        // arm at `loc_46AFF8` and does all of it -
        //
        //   sub_438240()           the active slider, or nothing
        //   [+404] != 3            not while shooting
        //   sub_438290(slider)     its node+180 bit 4 set
        //   dot(d, localX) >= 0    THE CORRECT SIDE: the man must
        //                          stand on the slider's -X, the
        //                          side its door and camera 9 are
        //   |d| < 157.48032        FOUR METRES
        //   sub_438420(slider, 3)  the slider goes OPEN - the arm
        //                          SETS the mode, it does not ask
        //   the snap              slider + M . (root0(H_SLDIN) -
        //                          root0(slf_112.3da)), y - 33.15
        //   sub_437140(node, M)    the root frame becomes the
        //                          SLIDER's, so the clip's step
        //                          runs in the vehicle's frame
        //   [+404] = 6             ACTOR_STATE 6
        //   SetPersoBankGroup(60)  H_SLDIN: the door opens, he
        //                          steps in, the door shuts
        //   Camera_Request(9, ..)  60 frames on the slider
        //
        // - and the `MDSLIDIN` half is what the CHANNEL fires at
        // the end of that clip (group 60's entry [160], no input,
        // goto H_SLIDER), handled with the other special moves.
        if (!ride && !boarded && !boarding && (bits & 0x10u) && !mountSpent) {
            const float me[3] = {session.playerPos()[0],
                                 session.playerPos()[1],
                                 session.playerPos()[2]};
            float at[3], ax[3], az[3];
            // ...and when it refuses, SAY WHICH TEST. Both are
            // geometric and neither is visible from the seat of a
            // keyboard: "nothing happened" is the same picture for
            // standing 5 m away and for standing at the wrong door.
            if (player && !session.sliders().canMount(me) &&
                session.sliders().calledVehicle() >= 0 &&
                session.sliders().calledFrame(at, ax, az)) {
                const float dx = at[0] - me[0];
                const float dy = at[1] + omk::kBoardSeatY - me[1];
                const float dz = at[2] - me[2];
                const float dot = dx * ax[0] + dy * ax[1] + dz * ax[2];
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const bool side = dot < 0.0f;
                const bool far_ = len >= omk::kBoardReach;
                const int  st   = session.sliders().callMachine().state;
                const bool shut = !session.sliders().calledIsOpen();
                std::printf("MDACTION: the slider refuses - %s%s%s%s (dot %+.0f, "
                            "%.2f m of the 4.00 it allows, ride state %d)\n",
                            shut ? "it is not standing OPEN" : "",
                            (shut && (side || far_)) ? "; " : "",
                            side ? "he is on the WRONG SIDE; its door is on "
                                   "its own -X" : "",
                            (side && far_) ? ", and he is too far away"
                                           : (far_ ? "he is too far away" : ""),
                            dot, len * 0.0254f, st);
            }
            if (player && session.sliders().canMount(me) &&
                session.sliders().calledFrame(at, ax, az)) {
                if (!doorOffState) {
                    // `dword_90EF28`, which `Game_Init` fills with
                    // `anims\slf_112.3da` for exactly this.
                    const auto ref = fs.read("ANIMS/slf_112.3da");
                    doorOffState = (!ref.empty() &&
                                    player->boardOffset(ref, 60, doorOff)) ? 1 : -1;
                }
                float door[3] = {at[0], at[1] - omk::kBoardSeatY, at[2]};
                if (doorOffState == 1)
                    for (int k = 0; k < 3; ++k)
                        door[k] += doorOff[0] * ax[k] + doorOff[2] * az[k];
                if (doorOffState == 1) door[1] += doorOff[1];
                // ...AND THAT Y IS A PELVIS, THIS CLASS TAKES FEET.
                // The engine writes the actor's +244..+252, which
                // is his ORIGIN and is the PELVIS (`player.h`,
                // settled with the camera lift - 41.9 for
                // `HO1_FNM`), while `PlayerController`'s position
                // is the walker's, at the feet. Handing the
                // engine's number straight over left him standing
                // 0.8 m in the air with his feet at the slider's
                // waistline - which is what a render of the
                // boarding beside the original's screenshot shows
                // at once and no amount of reading the listing
                // was going to say. Y points down, so the feet are
                // BELOW the pelvis by the lift.
                door[1] += player->cameraLift();
                const float dd = std::sqrt(
                    (at[0] - me[0]) * (at[0] - me[0]) +
                    (at[2] - me[2]) * (at[2] - me[2]));
                // The euler is NOT written by this arm - only the
                // position and the root frame are - so he keeps
                // the way he was facing and the clip turns him.
                player->rideAt(door, player->facing());
                session.sliders().boardCalled();     // mode 3, the 0x200 bit off from 7
                player->setActorState(omk::ActorState::ChannelOnly6, "MDACTION");
                player->setRootFrame(ax, az);
                player->setChannelOnly(true);
                boarding = true;
                mountSpent = true;
                boardCam = 60;
                const bool got = player->enterGroupById(60);
                std::printf("MDACTION: the slider's door at %.0f %.0f %.0f, "
                            "%.1f m away on the right side - snapped to "
                            "%.0f %.0f %.0f (offset %.1f %.1f %.1f in its "
                            "frame%s), ACTOR_STATE 6, %s, camera 9\n",
                            at[0], at[1], at[2], dd * 0.0254f,
                            door[0], door[1], door[2],
                            doorOff[0], doorOff[1], doorOff[2],
                            doorOffState == 1 ? "" : " - UNREAD, at the slider",
                            got ? "H_SLDIN plays" : "but the bank has no group 60");
            }
        }
        if (!(bits & 0x10u)) mountSpent = false;
        // ...UNLESS HE IS RIDING. ACTOR_STATE 7 and 8 do not
        // walk - `Actors_TickAll`'s row for each has `walks`
        // false - and the ride owns the body: `sub_457F50` writes
        // his position outright every frame. Ticking the walker
        // as well made the two fight, and the walker won.
        // ...and while BOARDING he still ticks: ACTOR_STATE 6 is
        // `Actor_TickChannelOnly`, the channel and nothing else,
        // which is what `setChannelOnly` models - so `H_SLDIN`
        // plays and its root motion carries him in.
        harnessFight();
        if (fightRun.active) {
            // ACTOR_STATE 2, and **THE WALKER MUST NOT TICK
            // BESIDE IT** - the lesson states 7 and 8 taught with
            // the slider. `Actor_TickPlayerAndOpponent` ticks each
            // fighter's channel itself, inside the combat step,
            // and the fight owns both bodies.
            // THE FIGHT CAMERA'S RAY (`sub_416570` -> `sub_444810`): the bolts'
            // world, the shown set's `shotSoup` - CollisionOnly skipped and a mesh
            // with either bit of 0x41 untested, the rule `sub_444460` applies.
            if (!fightRun.camRaySet && !noFightCamRay)
                fightRun.fight->setCameraRay([&](const float a[3], const float b[3], float hit[3]) {
                    const omk::TriangleSoup* shot = nullptr;
                    for (const auto& ws : worldSlots)
                        if (!ws.stem.empty() && ws.stem == worldSet) shot = &ws.shotSoup;
                    if (!shot || shot->empty()) return false;
                    const double p0[3] = {a[0], a[1], a[2]};
                    const double d[3] = {double(b[0]) - a[0], double(b[1]) - a[1],
                                         double(b[2]) - a[2]};
                    const auto h = omk::sweepSphere(*shot, p0, d, 0.0);
                    if (!h || h->t > 1.0) return false;
                    for (int k = 0; k < 3; ++k) hit[k] = static_cast<float>(p0[k] + h->t * d[k]);
                    return true;
                });
            fightRun.camRaySet = true;
            // THE AI'S CLOCK IS THE WALL'S (todo/drift-audit.md T3):
            // `Fight_TickAI` (0x00464830) waits on `Sys_GetTimeMs` - five
            // calls, no reference to the delta - so a wait runs in real time
            // whatever the frame rate, the `--speed` or a pause: below 10 fps
            // the original attacks more often per GAME second, and after a
            // pause its waits have already run out. This read the clamped,
            // speed-scaled game delta. A frame-bounded run keeps the fixed
            // delta, so every `--frames` fight check stays deterministic.
            if (frames) fightRun.ms += frameSec * 1000.0;
            else fightRun.ms = static_cast<double>(front.ticksMs() - fightRun.wallStart);
            const float was[3] = {player->pos()[0], player->pos()[1],
                                  player->pos()[2]};
            // ...on the PELVIS, the opponent's convention (above).
            const float lift = player->cameraLift();
            fightRun.player.x = was[0];
            fightRun.player.y = was[1] - lift;
            fightRun.player.z = was[2];
            fightRun.player.yaw = player->facing();
            const bool on = fightRun.fight->step(
                static_cast<float>(frameSec * 30.0),
                bits ? bits : omk::kIdleInput);
            // THE KNOCK-OUT'S BANDS. `sub_4452A0` calls
            // `Screen_Fade(1)` the frame the loser reaches state 6
            // or 7 and the KO counter first rises, and
            // `sub_4453F0` calls `Screen_Fade(0)` when the replay
            // passes run out, just before `Fight_Engage(-1, 1)`.
            // `Screen_Fade` is the letterbox bands' machine
            // (`Session::startBlackFade`: 1 -> state 3, 0 -> state
            // 4 and only from 3), so the replay is shown between
            // cinema bars.
            if (fightRun.koSeen == 0 && fightRun.fight->koCounter() > 0) {
                session.startBlackFade(true);
                std::printf("frame %ld: KO - Screen_Fade(1), the bands in\n", n);
            }
            if (!on && fightRun.fight->koCounter() > 0) {
                session.startBlackFade(false);
                std::printf("frame %ld: KO replay over - Screen_Fade(0), the bands out\n", n);
            }
            fightRun.koSeen = fightRun.fight->koCounter();
            // The bodies back: the player through `nudge` so the
            // walker keeps its own idea of where he stands, the
            // opponent onto his staged placement.
            // ...and back to the feet, so the walker keeps its own
            // idea of where he stands.
            const float d[3] = {fightRun.player.x - was[0],
                                (fightRun.player.y + lift) - was[1],
                                fightRun.player.z - was[2]};
            if (d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f)
                player->nudge(d);
            player->setFacing(fightRun.player.yaw);
            // THE OPPONENT'S MOTION PASS, which is the half of
            // `Actor_ApplyMotion` that matters here.
            // `Actor_TickPlayerAndOpponent` ends each fighter with
            // it, and a clip's root motion is how a fighter closes,
            // steps back or is carried by his own move. The player
            // gets it through his controller; the opponent has no
            // controller, so it is applied from the clip's own
            // accumulated root track.
            //
            // Without this he can never close, so `Fight_TickAI`
            // takes its APPROACH branch every single tick - which
            // is exactly what the harness showed: 674 injected
            // moves, a channel thrashing between `HGUARD`, `HRUN`
            // and a clipless pass-through, and a gap that never
            // changed.
            if (fightRun.foeBank && fightRun.foeChannel) {
                const auto& ctl = fightRun.foeChannel->ctl();
                // THE CLIP IS THE OWNER'S, not the current entry's.
                // A `.CTL` entry carrying 0x8002 is an alias or a
                // pass-through: it plays nothing and hands the clip
                // on through its GoTo. Reading `states[state()]`
                // gives -1 on those frames, and the first version
                // of this did - which made the opponent flicker
                // between his move and the bank's default stance,
                // and contributed no root motion on those frames.
                const int fs = fightRun.foeChannel->clipOwner();
                const int clip =
                    (fs >= 0 && fs < static_cast<int>(ctl.states.size()))
                        ? ctl.states[static_cast<std::size_t>(fs)].clip : -1;
                if (clip != fightRun.foeClip) {
                    fightRun.foeClip = clip;
                    fightRun.foeRoot.clear();
                    fightRun.foeFrame = fightRun.foeChannel->frame();
                    fightRun.foePose = omk::NodeTracks{};
                    if (clip >= 0 && clip < static_cast<int>(ctl.clips.size())) {
                        const auto& c = ctl.clips[static_cast<std::size_t>(clip)];
                        const auto& d = fightRun.foeBank->data;
                        if (c.offset + c.length <= d.size()) {
                            const auto bytes = std::span<const std::byte>(d)
                                                   .subspan(c.offset, c.length);
                            fightRun.foeRoot = omk::clipRootMotion(bytes);
                            // ...and the whole clip, for the POSE.
                            // Built here rather than per frame:
                            // `clipTracks` decodes every rotation
                            // of the clip, and a fighter changes
                            // entry a few times a second.
                            fightRun.foePose = omk::clipTracks(bytes);
                        }
                    }
                }
                const float nowF = fightRun.foeChannel->frame();
                if (!fightRun.foeRoot.empty()) {
                    const int last =
                        static_cast<int>(fightRun.foeRoot.size()) - 1;
                    const auto at = [&](float f) {
                        // key 0 is the REST SENTINEL: frame f reads
                        // key f + 1, and `clipRootMotion` is already
                        // indexed by frame, so frame 1 is entry 0.
                        int i = static_cast<int>(f) - 1;
                        if (i < 0) i = 0;
                        if (i > last) i = last;
                        return fightRun.foeRoot[static_cast<std::size_t>(i)];
                    };
                    // A wrap applies nothing, the way
                    // `Anim_RootDelta`'s `ceil(prev) <= cur` guard
                    // drops the frame a loop turns over on.
                    if (nowF >= fightRun.foeFrame) {
                        const auto a = at(fightRun.foeFrame);
                        const auto b = at(nowF);
                        const float local[3] = {b[0] - a[0], b[1] - a[1],
                                                b[2] - a[2]};
                        float world[3] = {0.0f, 0.0f, 0.0f};
                        omk::rotateYaw(fightRun.foe.yaw, local, world);
                        // **THE VERTICAL IS DROPPED, and it is
                        // labelled rather than silently kept.**
                        // `Actor_ApplyMotion` is what puts a body
                        // back on the floor after its clip has
                        // moved it, and this tree has that only
                        // for the player (his controller). Adding
                        // the clip's own `y` with no ground pass
                        // let the opponent climb - a reader
                        // watched him rise 34 units out of frame
                        // and vanish (his y went -29 to -63 while
                        // the player stood at +9.8, and Y points
                        // DOWN). Until a non-player body has a
                        // ground response, he keeps the height
                        // his placement gave him.
                        fightRun.foe.x += world[0];
                        fightRun.foe.z += world[2];
                    }
                }
                fightRun.foeFrame = nowF;
            }
            // `Actor_ApplyMotion`: EVERYTHING that moved him this
            // frame - the separation push and a knockback inside
            // `step`, and the root motion above - is one try,
            // undone and handed to the walker.
            if (fightRun.foeWalker) {
                auto& w = *fightRun.foeWalker;
                const double dx = double(fightRun.foe.x) - w.pos()[0];
                const double dz = double(fightRun.foe.z) - w.pos()[2];
                const double fdt = frameSec * 30.0;
                if (dx != 0.0 || dz != 0.0) {
                    const auto r = w.step(dx, dz, fdt);
                    ++fightRun.foeSteps;
                    if (r == omk::StepResult::Blocked) ++fightRun.foeBlocked;
                    if (w.lastSlides() > 0) ++fightRun.foeSlid;
                } else {
                    w.tick(fdt);
                }
                fightRun.foe.x = static_cast<float>(w.pos()[0]);
                fightRun.foe.z = static_cast<float>(w.pos()[2]);
                // ...AND THE HEIGHT: `Actor_ApplyMotion` ends with
                // the ground probe and `Walk_GroundResponse`, so the
                // body's +248 follows the floor - the walker's feet,
                // lifted back to the pelvis this fight step reads.
                // The clip's own vertical is still dropped (the note
                // above): the pose, not the node, carries a fall.
                // UNOBSERVABLE in the supermarket: its floor is flat
                // at y 10 wherever a fighter can stand (the crate tops
                // at -54..-61 cannot be reached), so this moves the
                // robber 4 units onto it and nothing more there.
                fightRun.foe.y = static_cast<float>(w.pos()[1]) - fightRun.foeLift;
            }
            // THE PELVIS TRACK, for the DRAW. A `.CTL` clip's root
            // position keys are an ABSOLUTE pelvis height in model
            // space, not a delta - measured over H1CMBT: the guard
            // at 2.3, the low guard 13.5 and held there, a
            // knock-down (`KOH_FRONT`, `I_DEATH`) falling to 35..39,
            // a jump going negative. The engine moves the pelvis
            // BONE with them and leaves the actor's +248 alone, so
            // the body's placement is his fight position plus the
            // track's offset from the stance he opened in. Without
            // it a knocked-down robber lay flat 36 units in the air
            // - a reader's screenshot (`todo/fight-mode.md` 15.16).
            float foeDrop = 0.0f;
            if (fightRun.foePose.valid() && !fightRun.foePose.trans.empty() &&
                fightRun.foeChannel) {
                if (fightRun.foeRootRef < -1e29f) fightRun.foeRootRef = fightRun.foePose.trans[0][1];
                int ff = static_cast<int>(fightRun.foeChannel->frame()) - 1;
                const int last = static_cast<int>(fightRun.foePose.trans.size()) - 1;
                ff = ff < 0 ? 0 : (ff > last ? last : ff);
                foeDrop = fightRun.foePose.trans[static_cast<std::size_t>(ff)][1] -
                          fightRun.foeRootRef;
            }
            if (fightRun.body) {
                fightRun.body->at[0] = fightRun.foe.x;
                fightRun.body->at[1] = fightRun.foe.y + foeDrop;
                fightRun.body->at[2] = fightRun.foe.z;
                fightRun.body->facing = fightRun.foe.yaw;
                fightRun.body->placed = true;
                // ...and ANCHORED AT THE PELVIS, which is what the
                // fight's y is (his walker less his lift). A body
                // the placement record put down is anchored at the
                // FLOOR, by a fixed feet offset, so the pelvis
                // track above never reached it: the flat's
                // training partner, knocked down, lay flat a
                // metre in the air (a reader, 2026-10-02). The
                // supermarket's robber had a program's pelvis
                // anchor already.
                fightRun.body->pelvis = true;
                fightRun.body->fightPlaced = true;
                fightRun.body->fightDrop = foeDrop;
            }
            playerTicked = true;
            // Every second of the fight, so a headless run can be
            // judged rather than guessed at: the two hit points,
            // how far apart they are, which `.CTL` state each is
            // in, and what the runtime has scored so far.
            if ((n - fightRun.startedAt) % 30 == 0) {
                const auto& st = fightRun.fight->stats();
                // ...naming the ENTRY each channel sits on, not
                // just its state id: "state 0" on its own says
                // nothing about which move is playing, and the
                // entry's name is what a `.CTL` reader can check.
                const auto entryName = [](const omk::CefChannel& ch) {
                    const int s = ch.state();
                    const auto& sts = ch.ctl().states;
                    return (s >= 0 && s < static_cast<int>(sts.size()))
                               ? sts[static_cast<std::size_t>(s)].name.c_str()
                               : "?";
                };
                std::printf("  fight +%ld: Vie %d vs %d, %.0f apart "
                            "(%.2f m), states %d/%d, entries %d '%s' / "
                            "%d '%s', hits %ld, grazes %ld, blocks %ld, "
                            "AI moves %ld\n",
                            n - fightRun.startedAt,
                            fightRun.fight->player().hp,
                            fightRun.fight->opponent().hp,
                            double(fightRun.fight->separation()),
                            double(fightRun.fight->separation()) * 0.0254,
                            fightRun.fight->player().state,
                            fightRun.fight->opponent().state,
                            player->channel().state(),
                            entryName(player->channel()),
                            fightRun.foeChannel->state(),
                            entryName(*fightRun.foeChannel),
                            st.hits, st.grazes, st.blocks, st.aiMoves);
                // ...and the OPPONENT's channel counters, so a
                // machine that is stuck says WHY: a landing the
                // link pass could not resolve, a chain that did
                // not terminate, and how many clip ends and
                // transitions it has actually made.
                // The AI's own clock and decision, because "he
                // stopped pressing" has two causes that look
                // identical from outside: a wait that never
                // expires (the ms clock not advancing the way the
                // profile's millisecond delays expect) and an
                // intent no branch re-arms.
                // WHERE THE TWO BODIES ACTUALLY ARE. The line
                // above carries their separation, which is a
                // scalar and hides the fault a reader reported as
                // "the enemy disappears": his clip's root motion
                // was moving him vertically with no ground pass,
                // so he rose out of frame while the separation -
                // measured in three dimensions - still read
                // plausibly. A height that walks away from its
                // placement is the discriminator, so print both.
                std::printf("    bodies: player %.0f %.0f %.0f facing %.0f, "
                            "opponent %.0f %.0f %.0f facing %.0f\n",
                            double(fightRun.player.x), double(fightRun.player.y),
                            double(fightRun.player.z), double(fightRun.player.yaw),
                            double(fightRun.foe.x), double(fightRun.foe.y),
                            double(fightRun.foe.z), double(fightRun.foe.yaw));
                // THE CAMERA's own state machine, so a run shows
                // it working rather than merely existing: 1 the
                // orbit, 2 the throw swing, 4 the ten-frame hand
                // over, 7 the KO, 8 close.
                {
                    const omk::FightCamera& fc = fightRun.fight->camera();
                    std::printf("    fight camera: state %d, eye %.0f %.0f %.0f, "
                                "at %.0f %.0f %.0f, heading %.0f, radius %.0f, "
                                "ray hits %ld%s\n",
                                fc.state, double(fc.eye[0]), double(fc.eye[1]),
                                double(fc.eye[2]), double(fc.at[0]),
                                double(fc.at[1]), double(fc.at[2]),
                                double(fc.heading), double(fc.radius),
                                fc.rayHits, fc.rayHit ? " (pulled in now)" : "");
                }
                // The KO counter, because "he is down and nothing
                // happens" has two causes that look the same from
                // outside: the trigger in `sub_4452A0` never
                // firing, and the replay firing but not finishing.
                std::printf("    fight state: KO counter %d, over %d, "
                            "player won %d\n",
                            fightRun.fight->koCounter(),
                            fightRun.fight->over() ? 1 : 0,
                            fightRun.fight->playerWon() ? 1 : 0);
                if (fightRun.foeWalker)
                    std::printf("    foe walker: %ld steps, %ld swept into a wall, "
                                "%ld blocked outright\n", fightRun.foeSteps,
                                fightRun.foeSlid, fightRun.foeBlocked);
                std::printf("    foe AI: clock %.0f ms, intent %d, "
                            "deadline %ld, taunt %ld/%ld\n",
                            fightRun.ms,
                            fightRun.fight->opponent().aiIntent,
                            fightRun.fight->opponent().aiMoveDeadline,
                            fightRun.fight->opponent().aiTauntStart,
                            fightRun.fight->opponent().aiTauntDeadline);
                const auto& cs = fightRun.foeChannel->stats();
                std::printf("    foe channel: ticks %ld, transitions %ld, "
                            "landings %ld, clipEnds %ld, badLanding %ld, "
                            "chainAborted %ld, queue %zu\n",
                            cs.ticks, cs.transitions, cs.landings,
                            cs.clipEnds, cs.badLanding, cs.chainAborted,
                            fightRun.foeChannel->queueSize());
            }
            if (!on) {
                // `sub_445AC0`: both channels restored, the
                // adventure bank back on the player, scheme 0, and
                // event 2 - which releases the FIRST script parked
                // at status 3. The life goes back on both records
                // first, because that is what the scripts read as
                // `'Vie Combat Après'`.
                const int myHp = fightRun.fight->player().hp;
                const int hisHp = fightRun.fight->opponent().hp;
                auto rec = state.rawMutable().subspan(
                    static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                    static_cast<std::size_t>(omk::GameState::kPlayerRecordSize));
                omk::writeActorProperty(rec, 1, myHp);
                session.setActorProperty(fightRun.opponent, 1, hisHp);
                player->setBank(playerCtl, playerCtlData);
                // THE STATE THE FIGHT LEAVES HIM IN - 1, win or lose,
                // and it takes two writes to see why. `sub_445AC0`
                // writes the player's +404 from `dword_906F34`, the
                // WINNER (`Fight_ResolveHit` stores `att` there): 1 if
                // he won and 0 if he lost. Then it raises event 2,
                // whose handler calls `Actor_LoadBankList` on the
                // player - and that ends `dword_910834[328*i] = 1`,
                // the +404 alias. So a loser's 0 lasts for the rest
                // of one call and he leaves the fight in 1 either way
                // (`todo/fight-mode.md` 15.12). This viewer's
                // `setBank` rebuilds the runtime, which lands on 1
                // through the same two rows, and the write below
                // states the result rather than relying on that.
                const bool won = fightRun.fight->playerWon();
                player->setActorState(omk::ActorState::Normal, "Actor_LoadBankList");
                std::printf("frame %ld: FIGHT GATE - the player's channel turned away "
                            "%ld candidate moves above priority %d\n", n,
                            player->channel().gateSkips(), fightRun.fight->gateThreshold());
                std::printf("frame %ld: sub_445AC0 - the player %s, ACTOR_STATE %d\n",
                            n, won ? "WON" : "LOST",
                            static_cast<int>(player->state()));   // the CONSUMER's, not the bool
                in.installScheme(0);
                session.fightEnded();
                std::printf("frame %ld: FIGHT ENDS after %ld frames - "
                            "%s won, Vie %d vs %d, the script resumes "
                            "and the adventure bank is back\n",
                            n, n - fightRun.startedAt,
                            fightRun.fight->playerWon() ? "the player"
                                                        : "the opponent",
                            myHp, hisHp);
                fightRun.active = false;
                dropLibrary(fightRt);     // `aventure.scx` back (`loadLibrary`)
                fightRun.fight.reset();
                fightRun.foeChannel.reset();
                if (fightRun.body) {
                    fightRun.body->inertAfterFight = true;
                    // where the loser LIES: his placement (the pelvis)
                    // and his drawn head, both from the draw's own
                    // record (`meshAt`), not from the fight's intent
                    const Staged& lb = *fightRun.body;
                    float headY = 0.0f;
                    const int hm = lb.mo ? lb.mo->headOf() : -1;
                    if (hm >= 0 && lb.meshAt.size() >= (static_cast<std::size_t>(hm) + 1) * 3)
                        headY = lb.meshAt[static_cast<std::size_t>(hm) * 3 + 1];
                    std::printf("frame %ld: the fight's OPPONENT actor %d is drawn with his "
                                "pelvis at y %.0f and his head at y %.0f (placement %.0f)\n",
                                n, lb.actor,
                                (lb.mo && lb.mo->root >= 0 && lb.meshAt.size() >= (static_cast<std::size_t>(lb.mo->root) + 1) * 3)
                                    ? lb.meshAt[static_cast<std::size_t>(lb.mo->root) * 3 + 1] : 0.0f,
                                headY, lb.at[1]);
                }
                fightRun.body = nullptr;
            }
        } else if (!ride && !boarded) {
            // `Actor_TickShoot` runs `sub_47D4D0` BEFORE the channel
            // tick: last frame's intents become this frame's step,
            // and the step meets the ground in the same try as a
            // clip's root motion (`actor/shootmove.h`). Logged once
            // when a run of movement starts and once when it ends.
            if (shootMode && shootMover.active &&
                player->state() == omk::ActorState::Shoot) {
                // ...and the shove's four frames, which write the
                // actor's own +416 and +424 (`actor/shootmove.h`)
                const float shoveWas[2] = {player->eulerPitch(),
                                           player->eulerRoll()};
                const omk::ShootMoveStep st = omk::shootMoveTick(
                    shootMover, player->facing(),
                    static_cast<float>(frameSec * 30.0),
                    &player->eulerPitch(), &player->eulerRoll());
                if (shoveWas[0] != player->eulerPitch() ||
                    shoveWas[1] != player->eulerRoll())
                    std::printf("frame %ld: THE SHOVE (sub_47D1F0) - %.2f left, "
                                "pitch %+.2f roll %+.2f degrees\n", n,
                                double(shootMover.shoveTimer),
                                double(player->eulerPitch()),
                                double(player->eulerRoll()));
                player->addShootMotion(st.dx, st.dz);
                const bool moving = st.dx != 0.0f || st.dz != 0.0f;
                if (moving && shootMoveFrames == 0) {
                    for (int k = 0; k < 3; ++k) shootMoveFrom[k] = player->pos()[k];
                    shootMoveDist = 0.0f;
                    std::printf("frame %ld: SHOOT MOVE starts at %.1f %.1f %.1f, "
                                "facing %.1f\n", n, double(shootMoveFrom[0]),
                                double(shootMoveFrom[1]), double(shootMoveFrom[2]),
                                double(player->facing()));
                }
                if (moving) {
                    ++shootMoveFrames;
                    shootMoveDist += std::sqrt(st.dx * st.dx + st.dz * st.dz);
                } else if (shootMoveFrames > 0) {
                    const float* p = player->pos();
                    std::printf("frame %ld: SHOOT MOVE stops after %ld frames - "
                                "asked %.2f, went %.2f %.2f %.2f\n", n,
                                shootMoveFrames, double(shootMoveDist),
                                double(p[0] - shootMoveFrom[0]),
                                double(p[1] - shootMoveFrom[1]),
                                double(p[2] - shootMoveFrom[2]));
                    shootMoveFrames = 0;
                }
            }
            const bool wasAir = player->walker().airborne();
            const bool wasSlide = player->walker().sliding();
            player->tick(static_cast<float>(frameSec * 30.0),
                         bits ? bits : omk::kIdleInput);
            playerTicked = true;
            // THE FALL, said as it happens - a reader in shoot mode:
            // "the character fall very slowly, in an not natural
            // way". `Actor_ApplyMotion` accelerates `+220` by
            // `kGravity` a frame and descends `+220/30 * dt`
            // (`actor/walk.h`); this prints the curve so a slow one
            // can be told from a wrong dt.
            //
            // A FALL and a SLIDE are the two ways down and they do
            // not look alike: a fall ACCELERATES (`+220 += kGravity`
            // a frame), a slide is a CONSTANT `kSlideSpeed` the
            // ground response writes every frame it is on a face
            // past the slope limit - 11.8 a frame, about 0.3 m/s,
            // which is what a slow unnatural descent reads like.
            // The two are told apart here so a report does not have
            // to guess which it saw.
            // ---- A LEDGE, `Walk_GroundResponse`'s airborne arm ----
            // (`todo/falls.md` 1). EVERY airborne tick until the fall
            // is banded (`+1304` 0 or 2), not only the tick he leaves
            // the ground: a slide off a ramp becomes a fall frames
            // later, and the catacombs' 5 m drop starts exactly so.
            // Not in a jump (`dword_6A52CC`); by the CLEARANCE to the
            // ground below: 1.5 m or more puts him in bank group 2
            // (`H_FALL`) unless ACTOR_STATE is 2, 3 or 15, and asks
            // for the overhead camera 18 over 30 frames. Nothing below
            // him at all reads as the longest band.
            // ...and NOT in the water. `Actor_ApplyMotion` sends ACTOR_STATEs
            // 11..14 to `sub_4A8F30` INSTEAD of gravity and the ground probe,
            // so `Walk_GroundResponse` - the whole of this reaction - is never
            // reached while he is in the canal.
            const int groundSt = static_cast<int>(player->state());
            const bool inWaterState = groundSt >= 11 && groundSt <= 14;
            if (!inWaterState && player->walker().airborne() &&
                !player->walker().jumping() && !fallBanded) {
                const auto* w = &player->walker();
                const auto g = omk::floorUnder(w->soup(), w->pos()[0],
                                               w->pos()[1] - 12.81, w->pos()[2]);
                const double clear = g ? *g - w->pos()[1] : 1e9;
                if (clear >= 59.055119) {
                    fallBanded = true;
                    const auto st = static_cast<int>(player->state());
                    const bool grouped = !(st == 2 || st == 3 || st == 15) &&
                                         player->enterGroupById(2);
                    std::printf("frame %ld: a LEDGE - clearance %.1f (%.2f m): "
                                "%s\n", n, clear, clear / 39.37,
                                grouped ? "bank group 2, H_FALL"
                                        : "no group change");
                    fallCamRequest(18, false, "the ledge", st);
                }
            }
            if (!player->walker().airborne() && !player->walker().sliding())
                fallBanded = false;
            // ---- THE WATER'S MESSAGES and a line a second while he swims ----
            for (const int m : player->takeWaterMessages()) {
                const bool ran = session.postMessage(m, session.playerActor());
                std::printf("frame %ld: water (sub_4A8F30) - MESSAGE %d %s; ACTOR_STATE %d\n",
                            n, m, ran ? "to its handler" : "- no handler subscribes",
                            static_cast<int>(player->state()));
            }
            {
                const int ws = static_cast<int>(player->state());
                // an instrument: every tick, for a stroke that loses its travel
                if (ws >= 11 && ws <= 14 && omk::envSet("OMK_SWIMTRACE"))
                    std::printf("  swimtrace %ld: state %d '%s' frame %.2f -> %.2f, local y %+.2f, "
                                "pitch %.1f, y %.2f\n", n, player->ctlState(),
                                player->ctlStateName().c_str(),
                                double(player->tickFrameBefore()),
                                double(player->tickFrameAfter()),
                                double(player->last().rootLocal[1]),
                                double(player->eulerPitch()), player->pos()[1]);
                if (ws >= 11 && ws <= 14 && n % 30 == 0)
                    std::printf("frame %ld: swimming - ACTOR_STATE %d, .CTL '%s' group %d, at "
                                "%.0f %.1f %.0f, pitch %.0f, drawn head %.0f over the pelvis, root local %+.2f %+.2f %+.2f -> world %+.2f %+.2f %+.2f, breath %s\n", n, ws,
                                player->ctlStateName().c_str(), player->ctlGroupId(),
                                player->pos()[0], player->pos()[1], player->pos()[2],
                                double(player->eulerPitch()),
                                double(playerHeadRise),
                                double(player->last().rootLocal[0]), double(player->last().rootLocal[1]), double(player->last().rootLocal[2]),
                                double(player->last().rootDelta[0]), double(player->last().rootDelta[1]), double(player->last().rootDelta[2]),
                                player->breathLeftMs() < 0.0 ? "-" :
                                    (std::to_string(int(player->breathLeftMs())) + " ms").c_str());
            }
            // ---- INTO THE WATER, `Actor_ApplyMotion` (`todo/swimming.md` 1) ----
            // In ACTOR_STATE 1, a ground mesh flagged 0x8000000 - the
            // canal's steps and bed - puts him in bank group 300
            // (`H_HFL-IN`), installs control scheme 1, writes
            // ACTOR_STATE 11 and clears the fall accumulators, and asks
            // for camera 21 on him over 50 frames.
            // THE ENGINE TAKES THIS ARM *OR* THE GROUND RESPONSE, NOT BOTH
            // (`21_d3d.c` 3830: `if (state == 1 && mesh & 0x8000000) { ...this... }
            // else Walk_GroundResponse(...)`). The port ran both, and on the tick
            // where the landing and the entry coincide - which is what real-time
            // play produces, and what `--nodelay` happened to separate by one
            // frame - the landing's group 4 overwrote group 300: he stood in the
            // canal on the WALK bank, in ACTOR_STATE 11, laid flat by the swim
            // pitch. A reader, 2026-09-17: *"The character is not swimming, he
            // just walking at 90 degrees"*.
            bool enteredWater = false;
            // AND THE MESH IS PROBED, not read off the walker's stand
            // flags, which only a STEP writes: after a fall onto the
            // canal bed he takes no step at all - the landing reaction
            // that would have put him back on a locomotion clip is the
            // arm this one replaces - so the cached flags stayed the
            // LEDGE's and he stood on the bed in `H_FALL` for ever.
            // The engine reads `Walk_ProbeGround`'s own mesh every
            // frame and has no such dependency. Grounded only: a body
            // still falling has not reached the water.
            std::uint32_t standFl = 0;
            if (!player->walker().airborne())
                player->walker().probeFlags(player->pos()[0],
                                            player->pos()[1] - 12.81,
                                            player->pos()[2], standFl);
            if (player->state() == omk::ActorState::Normal &&
                (standFl & 0x8000000u)) {
                const bool grouped = player->enterGroupById(300);
                enteredWater = true;
                player->setActorState(omk::ActorState::WaterIn11, "Actor_ApplyMotion");
                in.installScheme(1);
                fallBanded = false;
                // THE SWIM CAMERA IS THE CHASE CAMERA, NOT A FIXED OFFSET
                // (2026-09-17, after a reader: *"it should be behind him"*).
                // `sub_414520` gives mode 21 its own setup, `sub_413EF0`,
                // and gives mode 0 - the ordinary follow camera - a SWIM
                // VARIANT, `sub_413CD0`, whenever the player is in state 11,
                // 13 or 14; both write the same chase tunables (+228 = -39.37,
                // one metre; +300 = 1.5, +304 = 1.2, +308 = -1.0, +284 = 5,
                // +288 = 8, flags 0x4800) and probe for the water line above
                // him (`flt_4E7D0C`). So what films a swimmer is the camera
                // that films a walker, retuned - not preset 21's eye
                // (0, 78.74, -19.685) turned by his pitch, which is what this
                // did and which put the lens in FRONT of a prone body.
                // RECONSTRUCTION, labelled: the flagged passes those tunables
                // feed are not ported (`player.h` says the same of the land
                // camera's), so this is mode 0's own 3 m behind him, the
                // tunables' 1 m above, by the YAW alone. `--water-cam preset`
                // brings the old reading back to lay beside it.
                static constexpr float kWaterPresetEye[3] = {0.0f, 78.7402f, -19.685f};
                static constexpr float kWaterChaseEye[3]  = {0.0f, 39.370079f, -118.1102f};
                const float* kWaterEye = waterCamPreset ? kWaterPresetEye : kWaterChaseEye;
                static constexpr float kWaterAt[3]  = {0.0f, 0.0f, 0.0f};
                playerCamRequest(kWaterEye, kWaterAt, 75.0f, 50.0f);
                // ...and the WATER LINE, as `sub_413CD0` sets the swim camera
                // up: `World_ProbePoint` from 98.425 above him (2.5 m, Y down)
                // and, when the surface it meets carries 0x20000000, that
                // height is `flt_4E7D0C`. Kept until the next setup finds one.
                {
                    std::uint32_t fl = 0;
                    const auto h = player->walker().probeFlags(
                        player->pos()[0], player->pos()[1] - 98.425194, player->pos()[2], fl);
                    if (h && (fl & 0x20000000u)) {
                        waterLine = static_cast<float>(*h);
                        waterLineKnown = true;
                        std::printf("frame %ld: the water line (sub_413CD0's probe) at y %.1f\n",
                                    n, double(waterLine));
                    }
                }
                std::printf("frame %ld: INTO THE WATER at %.0f %.0f %.0f - %s, scheme 1, "
                            "ACTOR_STATE %d, camera 21 over 50 frames\n", n,
                            player->pos()[0], player->pos()[1], player->pos()[2],
                            grouped ? "bank group 300 (H_HFL-IN)" : "NO group 300",
                            static_cast<int>(player->state()));
            }
            {
                const bool air = player->walker().airborne();
                const bool slide = player->walker().sliding();
                if ((air || slide) && !(wasAir || wasSlide)) {
                    std::printf("frame %ld: the player %s from y %.1f\n", n,
                                slide ? "SLIDES (a face past the slope limit - a "
                                        "CONSTANT 11.8 a frame, not gravity)"
                                      : "FALLS", player->pos()[1]);
                }
                else if ((air || slide) && n % 5 == 0) {
                    // ...and what the GROUND PROBE sees under him,
                    // because "he went through the ground" has two
                    // very different causes: nothing in the soup
                    // below him, or a surface the landing test
                    // stepped over.
                    const auto* w = &player->walker();
                    const auto g = omk::floorUnder(w->soup(), w->pos()[0],
                                                   w->pos()[1] - 12.81, w->pos()[2]);
                    char gs[48];
                    if (g) std::snprintf(gs, sizeof gs, "%.1f (%.1f below)",
                                         *g, *g - w->pos()[1]);
                    else   std::snprintf(gs, sizeof gs, "NOTHING under him");
                    std::printf("frame %ld:   %s: y %.1f, descended %.1f, at %.0f %.0f,"
                                " ground %s\n", n, slide ? "sliding" : "falling",
                                player->pos()[1], player->walker().fall(),
                                double(player->pos()[0]), double(player->pos()[2]), gs);
                }
                else if (!air && !slide && (wasAir || wasSlide)) {
                    std::printf("frame %ld: the player LANDS at y %.1f - dropped "
                                "%.1f (%.2f m), tier %d\n", n, player->pos()[1],
                                player->walker().lastLandingDrop(),
                                player->walker().lastLandingDrop() / 39.37,
                                player->walker().lastLandingTier());
                    // ---- THE LANDING MESSAGES, `Walk_GroundResponse` ----
                    // (`todo/released-spectres.md` step 6). On the landing
                    // tick, by the accumulated fall `+280`: at 196.85 (5 m)
                    // and over MESSAGE 11, group 5 and `sub_414DE0(him, 19)`;
                    // from 118.11 (3 m) MESSAGE 10, group 4 and
                    // `sub_414DE0(him, 16)`; below, no message. Both are
                    // posted for the PLAYER whatever his ACTOR_STATE - only
                    // the group changes skip states 2, 3 and 15, and
                    // `sub_414DE0` returns at once in 3. NOT PORTED here,
                    // labelled: the group changes and the landing camera
                    // requests, which the walker's own tiers stand for; and
                    // the short band's second test on `+284`. The catacombs'
                    // handler for 11 costs 45 health with a red flash.
                    const double lf = player->walker().lastLandingFall();
                    // THE REACTION (`todo/falls.md` 1): the group by the
                    // fall, skipped in ACTOR_STATEs 2, 3 and 15, and the
                    // camera. The short band's second test on `+284` is
                    // not modelled; the fall alone decides.
                    // ...and the floor he has just landed ON, because it is ONE
                    // probe in the engine: the mesh `Walk_ProbeGround` returns
                    // decides which arm runs, so a body landing on the canal's
                    // bed never reaches the landing reaction or its message at
                    // all. The port sees the landing a tick before the flags,
                    // which is why all three tests are here.
                    // ...and the mesh he has just landed ON, PROBED here rather
                    // than read off the walker, because the port notices the
                    // landing one tick before the stand's flags follow it: at
                    // the landing tick `floorFlags()` is still the floor he
                    // left. The engine has no such gap - `Walk_ProbeGround`
                    // returns one mesh and the if/else at `21_d3d.c` 3830
                    // chooses on it - so the probe is what must decide.
                    std::uint32_t landFlags = 0;
                    player->walker().probeFlags(player->pos()[0],
                                                player->pos()[1] - 12.81,
                                                player->pos()[2], landFlags);
                    const bool waterArm = enteredWater || inWaterState ||
                                          (landFlags & 0x8000000u);
                    if (waterArm)
                        std::printf("frame %ld: the landing's reaction SKIPPED - the "
                                    "water arm took this tick (ACTOR_STATE %d), and the "
                                    "engine calls one or the other\n", n,
                                    static_cast<int>(player->state()));
                    if (!waterArm) {
                        const auto st = static_cast<int>(player->state());
                        const bool may = !(st == 2 || st == 3 || st == 15);
                        int grp = -1;
                        if (lf >= 196.85039)      grp = 5;       // H_HFL -> H_SOL
                        else if (lf >= 59.055119) grp = 4;       // H_LFL
                        else if (player->ctlGroupId() == 2) grp = 100;
                        const bool went = may && grp >= 0 && player->enterGroupById(grp);
                        if (grp >= 0)
                            std::printf("frame %ld: the landing's reaction - fall %.1f: "
                                        "bank group %d%s\n", n, lf, grp,
                                        went ? "" : " REFUSED (state or bank)");
                        if (lf >= 196.85039) fallCamRequest(19, false, "the landing", st);
                        else                 fallCamRequest(16, false, "the landing", st);
                    }
                    const int msg = waterArm ? -1
                                  : lf >= 196.85039 ? 11 : lf >= 118.11024 ? 10 : -1;
                    if (msg >= 0) {
                        const bool ran = session.postMessage(msg, session.playerActor());
                        std::printf("frame %ld: the landing (Walk_GroundResponse) - fall "
                                    "%.1f (%.2f m): MESSAGE %d %s\n", n, lf, lf / 39.37,
                                    msg, ran ? "to its handler"
                                             : "- NO handler subscribes");
                    }
                }
            }
            // While he BOARDS or LEAVES, say where the clip is
            // carrying him - the door snap is one number and the
            // carry-in is seventy-two more, and a render at the
            // end of the clip showed him beside the vehicle.
            if ((boarding || leaving) && player->ticks() % 12 == 0) {
                const auto& lf = player->last();
                std::printf("%s: clip frame %d at %.1f %.1f %.1f (root delta "
                            "%+.2f %+.2f %+.2f this tick, .CTL %s)\n",
                            boarding ? "boarding" : "leaving",
                            player->poseFrame(), player->pos()[0],
                            player->pos()[1], player->pos()[2],
                            lf.rootDelta[0], lf.rootDelta[1], lf.rootDelta[2],
                            player->clipName().c_str());
            }
        }
    }
}

// Seated, not driving; where the slider is after he gets out
void PlayState::adventureSeated() {
    const auto& fs = *fs_;
    auto& session = *session_;
    // ---- SEATED, not driving ------------------------------
    //
    // Aboard between the mount and either "Manuelle" or the end
    // of a journey: `sub_457F50` writes his position from the
    // slider's every frame, and the slider is where the pool's
    // drive put it.
    // ...and SAY when the slider rejoins the traffic, which is
    // `sub_456530` case 7's `sub_438420(slider, 0)`.
    // `sub_456530` case 7 releases the slider only once he is
    // 300 clear of it and in front of it, so it needs him.
    {
        const float me[3] = {session.playerPos()[0], session.playerPos()[1],
                             session.playerPos()[2]};
        // THE FLOOR'S MESH FLAGS, kept in step with the soup
        if (soupFlagsFor != playerSoup.data() || soupFlagsSize != playerSoup.size()) {
            playerSoupFlags.clear();
            for (int sl = 0; sl < 2; ++sl) {
                const WorldSlot& ws = worldSlots[static_cast<std::size_t>(sl)];
                if (ws.stem.empty()) continue;
                const std::size_t cnt = ws.soup.size() / 9;
                for (std::size_t t = 0; t < cnt; ++t) {
                    const int mi = t < ws.soupMesh.size() ? ws.soupMesh[t] : -1;
                    playerSoupFlags.push_back(
                        (mi >= 0 && static_cast<std::size_t>(mi) < ws.meshes.size())
                            ? ws.meshes[static_cast<std::size_t>(mi)].flags : 0u);
                }
            }
            if (playerSoupFlags.size() * 9 != playerSoup.size()) playerSoupFlags.clear();
            soupFlagsFor = playerSoup.data();
            soupFlagsSize = playerSoup.size();
        }
        // THE VEHICLES MUST KNOW WHERE HE IS (`todo/falls.md` 4).
        // `Sliders_Tick` probes the player's ground every frame and
        // raises `dword_8F5E38` when the mesh under him is the ROAD -
        // a name at `+16` starting with the byte 'X' or the word "OP",
        // exactly and case-sensitively. A vehicle brakes for a player
        // on the road and runs over one it touches. `setPlayer` had
        // no caller in this tree, so traffic did neither. The ride
        // exception (`dword_8F5E44 +8 == 6`) is `Sliders::setPlayer`'s.
        if (player) {
            const float* pp = player->pos();
            bool onRoad = false;
            std::uint32_t tri = 0;
            if (omk::floorUnder(playerSoup, playerGrid, pp[0], pp[1] - 12.81, pp[2], tri)) {
                std::size_t off = 0;
                for (int sl = 0; sl < 2; ++sl) {
                    const WorldSlot& ws = worldSlots[static_cast<std::size_t>(sl)];
                    if (ws.stem.empty()) continue;
                    const std::size_t cnt = ws.soup.size() / 9;
                    if (tri < off + cnt) {
                        const std::size_t t = tri - off;
                        if (t < ws.soupMesh.size()) {
                            const int mi = ws.soupMesh[t];
                            if (mi >= 0 && static_cast<std::size_t>(mi) < ws.meshes.size()) {
                                const char* nm = ws.meshes[static_cast<std::size_t>(mi)].name;
                                onRoad = nm[0] == 'X' || (nm[0] == 'O' && nm[1] == 'P');
                            }
                        }
                        break;
                    }
                    off += cnt;
                }
            }
            session.sliders().setPlayer(pp, onRoad);
            static int roadTold = -1;
            if (int(onRoad) != roadTold) {
                roadTold = int(onRoad);
                std::printf("frame %ld: the player is %s (dword_8F5E38 = %d)\n", n,
                            onRoad ? "ON THE ROAD" : "off the road", int(onRoad));
            }
        }
        session.sliders().setRider(me, player ? player->facing()
                                              : session.playerYaw());
    }
    // WHERE THE SLIDER IS after he gets out - a reader lost it at
    // Qalisar's kerb while the staging said "staged".
    if (leaving || (session.sliders().calledVehicle() >= 0 && !boarded && !boarding)) {
        static long lastVehTold = -1000;
        if (n - lastVehTold >= 45) {
            lastVehTold = n;
            float vat[3] = {0, 0, 0};
            const bool have = session.sliders().calledAt(vat);
            std::printf("frame %ld: after the ride - the called vehicle %s at %.0f %.0f %.0f, "
                        "ride state %d; he is at %.0f %.0f %.0f\n", n,
                        have ? "is" : "is GONE", vat[0], vat[1], vat[2],
                        session.sliders().callMachine().state,
                        session.playerPos()[0], session.playerPos()[1], session.playerPos()[2]);
        }
    }
    // ...and FOLLOW the released vehicle for ten seconds after,
    // since a reader lost it at Qalisar's kerb: once it is
    // traffic again the ambient drive owns it, and where that
    // drive puts it on its first step is the question.
    static int  releasedSlot = -1;
    static long releasedAt = -1;
    if (session.sliders().calledVehicle() >= 0) releasedSlot = session.sliders().calledVehicle();
    if (const int how = session.sliders().takeReleasedNotice()) {
        releasedAt = n;
        std::printf("slider: RELEASED - %s, so it goes back to mode 0 and "
                    "drives as ordinary traffic again\n",
                    how == 1 ? "nobody boarded it in 600 frames (case 1)"
                    : how == 2 ? "the journey is over and he is out (case 7, no "
                                 "0x200: at once)"
                               : "he is 300 clear and in front of it (case 7, "
                                 "a manual ride)");
        if (how != 1 && slidOutAt >= 0)
            std::printf("slider: released %ld frame(s) after MDSLIDOU\n", n - slidOutAt);
    }
    if (releasedAt >= 0 && n - releasedAt <= 300 && (n - releasedAt) % 30 == 0 &&
        releasedSlot >= 0 && static_cast<std::size_t>(releasedSlot) < session.sliders().vehicles().size()) {
        const auto& rv = session.sliders().vehicles()[static_cast<std::size_t>(releasedSlot)];
        if (rv.live && rv.mover >= 0) {
            const auto& rm = session.sliders().movers()[static_cast<std::size_t>(rv.mover)];
            std::printf("frame %ld: the released vehicle (slot %d, '%s', state %d) at %.0f %.0f %.0f, "
                        "lane %d seg %d remaining %.0f; he is at %.0f %.0f %.0f\n", n, releasedSlot,
                        rv.model.c_str(), rv.state, rm.body[0], rm.body[1], rm.body[2], rm.lane, rm.seg,
                        rm.remaining, session.playerPos()[0], session.playerPos()[1], session.playerPos()[2]);
        } else {
            std::printf("frame %ld: the released vehicle (slot %d) is DEAD\n", n, releasedSlot);
        }
    }
    if (boarded && !ride) {
        float at[3];
        if (session.sliders().calledAt(at)) {
            // `sub_457F50`: the rider's +248 = the RIDE's y + 10,
            // and the ride's y is the vehicle's HOVERING height -
            // `SliderRide` flies `kHover` (30.75) above the floor -
            // so his PELVIS sits 10 below the hovering node, 20.75
            // above the road: a seat. `calledAt` is the body point
            // at ROAD level (the node is drawn 30.75 above it), and
            // this class takes FEET, so: road - 30.75 + 10 + lift.
            // It read `at.y + 10` in feet-space until 2026-09-08,
            // which sat him 11 units (28 cm) too high.
            const float seat[3] = {at[0],
                                   at[1] - static_cast<float>(omk::SliderRide::kHover)
                                         + static_cast<float>(omk::SliderRide::kNodeUp)
                                         + (player ? player->cameraLift() : 0.0f),
                                   at[2]};
            const float yaw = session.sliders().calledYaw();
            session.setPlayerPosition(seat, yaw);
            if (player) player->rideAt(seat, yaw);
        }
        // ...and THE JOURNEY'S END: state 6 -> 4, camera 10. He
        // gets out at the destination, the slider leaves
        // (state 7, released once he is 300 clear and ahead).
        if (session.sliders().journeyArrived() && journeyTo >= 0) {
            // ---- HE GETS OUT WHERE THE SLIDER STOPPED --------
            //
            // NOT at the destination's address, which is what this
            // did and what a reader reported as being *"teleported
            // instead of just leaving the slider where it
            // arrives"*. `sub_4570F0` (the stop) writes only the
            // actor's Y - `sliderY - 33.149605` - and copies +244
            // and +252 through UNCHANGED, then hands over to
            // `sub_468FA0`, which does the real placement and is
            // the exact mirror of `MDACTION`'s entry:
            //
            //   slider mode 4, its speed zeroed, then mode 5
            //   off = root0(group 61's clip) - root0(dword_9103D8)
            //   actor = slider + M . off, y -= 33.149605
            //   +260 = FLT_MAX, o3de_MoveNodeBy, sub_437140(M)
            //   ACTOR_STATE 8 (both +404 and +408)
            //   SetPersoBankGroup(group 61) - H_SLDOUT, 51 frames
            //
            // and back in `sub_4570F0`: `sub_438420(slider, 7)`,
            // the slider leaves, and `Camera_Request(17, ..., 60)`.
            // The reference clip is `slf_113.3da` and NOT the
            // entry's `slf_112.3da` - two different globals, and
            // measuring group 61 against 112 was wrong even though
            // the two clips' root keys turn out to be identical.
            float at[3], ax[3], az[3];
            bool out = false;
            if (player && session.sliders().calledFrame(at, ax, az)) {
                if (!exitOffState) {
                    const auto ref = fs.read("ANIMS/slf_113.3da");
                    exitOffState = (!ref.empty() &&
                                    player->boardOffset(ref, 61, exitOff)) ? 1 : -1;
                }
                float o[3] = {at[0], at[1] - omk::kBoardSeatY, at[2]};
                if (exitOffState == 1) {
                    for (int k = 0; k < 3; ++k)
                        o[k] += exitOff[0] * ax[k] + exitOff[2] * az[k];
                    o[1] += exitOff[1];
                }
                o[1] += player->cameraLift();   // pelvis -> feet, as above
                player->rideAt(o, player->facing());
                player->setActorState(omk::ActorState::SliderRide, "sub_468FA0");
                player->setRootFrame(ax, az);
                player->setChannelOnly(true);
                session.setPlayerPosition(o, player->facing());
                // ...and the RELEASE test's rider, NOW. `case 7` is
                // armed by `dismountCalled` below and tests "300
                // clear and in front" on the next tick; after a
                // load the rider it had was his position in the
                // OLD city, nine kilometres away, so the slider was
                // handed back to the traffic one frame after he
                // got out and was gone before he stood up (traced:
                // "the called vehicle is GONE at 0 0 0, ride state
                // 0" at ARRIVED+1). The engine's ride writes +244
                // before mode 7 is set; this is that write.
                session.sliders().setRider(o, player->facing());
                out = player->enterGroupById(61);
                leaving = true;
                std::printf("slider: ARRIVED - he gets OUT WHERE IT "
                            "STOPPED, %.0f %.0f %.0f (offset %.1f %.1f "
                            "%.1f in its frame%s), ACTOR_STATE 8, %s\n",
                            o[0], o[1], o[2], exitOff[0], exitOff[1],
                            exitOff[2],
                            exitOffState == 1 ? "" : " - UNREAD",
                            out ? "H_SLDOUT plays" : "but the bank has "
                                  "no group 61");
            }
            // `Camera_Request(17, {player, player, 60.0f, 1, .., -1})`
            // - `sub_4570F0`'s last act. Preset 17: eye
            // (-39.3701, 78.7402, 0), target (0, 0, 0), fov 75,
            // subjects 0/0 - a metre behind and two up, on him, over
            // sixty frames. The same blend the take camera uses.
            {
                static constexpr float kExitEye[3] = {-39.3701f, 78.7402f, 0.0f};
                static constexpr float kExitAt[3]  = {0.0f, 0.0f, 0.0f};
                playerCamRequest(kExitEye, kExitAt, 75.0f, 60.0f);
                std::printf("slider: camera 17 requested - preset 17 on him over 60 frames\n");
            }
            // `sub_468FA0`: mode 5 while H_SLDOUT plays; MDSLIDOU makes
            // it 7 when the clip ends, and case 7 then lets it go
            session.sliders().exitCalled();
            boarded = false;
            journeyTo = -1;
            calledDestination = -1;
        }
    }
}

// The ride
void PlayState::adventureRide() {
    auto& session = *session_;
    // ---- THE RIDE ---------------------------------------
    //
    // `Slider_TickRide` (0x00458150), with the pool and the
    // arrival left out: `--ride` mounts him where he stands, and
    // what runs from there is the engine's own model.
    //
    // The delta is HALVED, because that function's first act is
    // `flt_4C30D8 *= 0.5` for all three helpers - the whole ride
    // advances at half a frame per frame - and the input word is
    // the same one the walker takes, which is what
    // `dword_8F5DE0 = dword_4E9718` hands the flight model.
    //
    // After the helpers the engine drops the player onto the
    // surface under him and runs `Actor_ScanZones`, so riding
    // still triggers zones; `setPlayerPosition` is what tells the
    // Session, and the zone scan is its own.
    if (ride) {
        const auto probe = [&](double px, double py, double pz,
                               double& drop, bool& braking) {
            braking = false;      // the "OP" surfaces are not
                                  // identified here - step 3b
            if (const auto h = omk::surfaceUnder(playerSoup, px, py, pz)) {
                drop = h->y - py;
                return true;
            }
            return false;
        };
        const double dt = frameSec * 30.0 * 0.5;
        ride->fly(bits, dt, probe);
        ride->hover(dt, probe);
        if (ride->stopped) {
            // `sub_4570F0`: the ride ends, the slider is dropped
            // onto the ground and the camera hands back at mode
            // **17** with the PLAYER as both subjects - not mode
            // 0, which is why `sub_452570`'s arrive arm guards
            // its own `Camera_Request(0, ...)` on the mode not
            // already being 17.
            std::printf("ride: stopped at %.0f %.0f %.0f - "
                        "`sub_4570F0`, camera mode 17\n",
                        ride->x, ride->y, ride->z);
            const float at[3] = {static_cast<float>(ride->x),
                                 static_cast<float>(ride->y
                                     + omk::SliderRide::kRiderUp),
                                 static_cast<float>(ride->z)};
            session.setPlayerPosition(at, static_cast<float>(ride->yaw));
            if (player) player->placeAt(at, static_cast<float>(ride->yaw));
            // `sub_456530` state 7: it drives off once he is 300
            // clear and in front of it.
            session.sliders().dismountCalled();
            ride.reset();
        } else {
            // `sub_457F50`: the rider takes the slider's x and z,
            // and the actor's own `+248` its y plus 10.
            double at[3];
            ride->riderAt(at);
            const float p3[3] = {static_cast<float>(at[0]),
                                 static_cast<float>(at[1]),
                                 static_cast<float>(at[2])};
            session.setPlayerPosition(p3, static_cast<float>(ride->yaw));
            if (player) player->rideAt(p3, static_cast<float>(ride->yaw));
            // ...and the VEHICLE goes where the ride is, so the
            // model the crowd pool draws is under him rather than
            // left on the road. `sub_457F50` writes the slider's
            // node from the ride's own position for the same
            // reason.
            const float vp[3] = {static_cast<float>(ride->x),
                                 static_cast<float>(ride->y),
                                 static_cast<float>(ride->z)};
            // The pool draws its nose along `(sin t, cos t)`; the ride's
            // yaw is that heading plus 180 (`sub_457270`), so the nose it
            // flies along is the yaw minus 180.
            session.sliders().placeCalled(vp, static_cast<float>(ride->yaw - 180.0));
            // ...and SAY which way it went, against the nose the pool
            // DRAWS (its mover heading, `-row 2` of `calledFrame`), from
            // where the manual drive began: a slider driven forward moves
            // along its nose, not tail-first (drift audit M2)
            static long rideTold = -1000;
            static float rideFrom[3] = {0, 0, 0};
            static bool rideStarted = false;
            if (!rideStarted) {
                rideStarted = true;
                for (int k = 0; k < 3; ++k) rideFrom[k] = vp[k];
            }
            float sat[3], sx[3], sz[3];
            if (n - rideTold >= 30 && session.sliders().calledFrame(sat, sx, sz)) {
                rideTold = n;
                const float dx = vp[0] - rideFrom[0], dz = vp[2] - rideFrom[2];
                std::printf("slider: manual ride frame %ld - moved %.0f along its "
                            "drawn nose, %.0f across\n", n,
                            dx * -sz[0] + dz * -sz[2], dx * sx[0] + dz * sx[2]);
            }
        }
    }
}

// Stuck between walls; where the floor ends
void PlayState::adventureWalls() {
    // STUCK BETWEEN WALLS. A move blocked for a whole second while a
    // direction is being asked for is the sweep's own failure to
    // slide out of a corner - a reader was held between the alley's
    // wall and the crates (2026-09-05). Printed with the position,
    // the heading and the move asked, so the corner can be probed.
    {
        static int blockedRun = 0; static long stuckTold = -1000;
        const auto& lf = player->last();
        const bool asked = std::fabs(lf.rootDelta[0]) + std::fabs(lf.rootDelta[2]) > 0.05f;
        if (lf.stepped && asked && lf.step == omk::StepResult::Blocked) ++blockedRun;
        else blockedRun = 0;
        if (blockedRun >= 30 && n - stuckTold >= 30) {
            stuckTold = n;
            std::printf("walker: STUCK - blocked %d frames running at %.1f %.1f %.1f facing %.0f, "
                        "asking %+.2f %+.2f, %d wall passes hit last step\n",
                        blockedRun, player->pos()[0], player->pos()[1], player->pos()[2],
                        player->facing(), lf.rootDelta[0], lf.rootDelta[2],
                        player->walker().lastSlides());
        }
    }
    // WHERE THE FLOOR ENDS. A step that finds no floor under its
    // destination (`Reverted`), or a fall that begins, is the moment
    // the walker leaves the geometry - which a reader did through the
    // Impasse alley's wall on 2026-09-05. Printed with the position,
    // the heading and the sweep's verdict, once a second at most.
    {
        static long voidTold = -1000;
        const auto& lf = player->last();
        const bool off = lf.stepped && (lf.step == omk::StepResult::Reverted ||
                                        lf.step == omk::StepResult::Fell);
        if (off && n - voidTold >= 30) {
            voidTold = n;
            const auto gp = player->walker().ground(player->pos()[0], player->pos()[1], player->pos()[2]);
            std::printf("walker: %s at %.1f %.1f %.1f facing %.0f - move %+.2f %+.2f, "
                        "%d wall passes hit, ground %s (probe: %s, soup %zu tris)\n",
                        lf.step == omk::StepResult::Reverted ? "NO FLOOR ahead (reverted)"
                                                              : "FALLING",
                        player->pos()[0], player->pos()[1], player->pos()[2],
                        player->facing(), lf.rootDelta[0], lf.rootDelta[2],
                        player->walker().lastSlides(),
                        lf.onGround ? "under him" : "NOT under him",
                        gp ? (std::to_string(*gp)).c_str() : "none",
                        player->walker().soup().size() / 9);
        }
    }
    // omk-play 69: WATCH THE WHOLE TAKE, INCLUDING THE WAIT.
    //
    // The take is not one animation, it is a CONVERSATION with the
    // player - the reader's own account: "grabbing an object means
    // taking it in the hand, waiting for the user confirmation or
    // cancellation, and triggering the right anim for each case".
    // H1Avnt says the same thing:
    //
    //   group 41   H_TAKL12 -> MDGETOBJ -> H_TAKL22 -> goto ...
    //   group 4    H_WAITOB   (the group's DEFAULT entry - the hold)
    //                +- MDPUTSNK  flags 80000013 -> H_GETOBJ -> H_STAND
    //                +- MDNOTAKE  flags 80000013 -> H_GETOBJ -> H_STAND
    //
    // Neither child of `H_WAITOB` carries the default bit `0x20`
    // and both carry `0x80000000`, so the machine is meant to SIT
    // in H_WAITOB until an input picks a branch. A channel that
    // falls through to a child when nothing matches would play the
    // confirm or the cancel immediately - "it plays all the
    // animations", which is the report.
    //
    // The old window was 120 TICKS, and a tick advances the clip by
    // `frameSeconds * 30`, so under `--speed 3` it expired just as
    // H_WAITOB began and the interesting half was never logged.
    // This runs until the machine is back in the idle, so the wait
    // and whatever leaves it are both on the record - and prints
    // the INPUT WORD, because "did a transition fire with no input"
    // is the whole question.
    {
        static int  watch = 0;
        static bool leftIdle = false;
        static std::string lastClip;
        for (const auto& mv : (playerTicked ? player->specialMoves() : kNoMoves))
            if (mv == "MDACTION") {
                watch = 1200; leftIdle = false; lastClip.clear();
            }
        if (watch > 0) {
            --watch;
            const std::string c = player->clipName();
            if (c != lastClip) {
                lastClip = c;
                std::printf("  anim: frame %ld  .CTL state %d '%s'  clip '%s' "
                            "f %.1f  input %04x%s\n", n, player->ctlState(),
                            player->ctlStateName().c_str(), c.c_str(),
                            player->clipFrame(), bits,
                            bits ? "" : "   <- NO INPUT");
                // Stop only once the machine has LEFT the idle and
                // come back. Testing for H_STAND alone ended the
                // watch on its first tick every time, because the
                // press is seen while the idle clip is still up -
                // six takes logged one line each and none of the
                // interesting half.
                if (c != "H_STAND") leftIdle = true;
                else if (leftIdle) watch = 0;
            }
        }
    }
    // omk-play 67: does the new fall/slide path actually engage
    // while someone plays? Before the fix every drop past the step
    // limit came back Refused and the actor stood on it, so a
    // count of Fell/Slid against Refused is the whole question.
    {
        static long nFell = 0, nSlid = 0, nRefused = 0, nBlocked = 0;
        static long toldAt = -1;
        const auto r = player->last().step;
        if (r == omk::StepResult::Fell)         ++nFell;
        else if (r == omk::StepResult::Slid)    ++nSlid;
        else if (r == omk::StepResult::Refused) ++nRefused;
        else if (r == omk::StepResult::Blocked) ++nBlocked;
        const long tot = nFell + nSlid + nRefused;
        if (tot > 0 && tot != toldAt && (tot % 25) == 0) {
            toldAt = tot;
            std::printf("walk: %ld fell, %ld slid, %ld refused (a drop past "
                        "the no-damage tier), %ld blocked - at %.0f %.0f %.0f\n",
                        nFell, nSlid, nRefused, nBlocked,
                        player->pos()[0], player->pos()[1], player->pos()[2]);
        }
    }
}

// The world take; where the action raise comes from
void PlayState::adventureTake() {
    auto& inv = *inv_;
    auto& session = *session_;
    // ---- THE WORLD TAKE: tab_special_move[] 3..7 -------------
    //
    // omk-play 66. Pressing action fires MDACTION (H1AVNT entry
    // 24, group 0, input 0x10) and that entry has NO CHILDREN:
    // the HANDLER carries the machine on, by finding group id 45
    // and installing it (`loc_46AFD0`: `Cef_FindGroupById(actor+
    // 180, 0x2D)` -> `SetPersoBankGroup`). Group 45's entry is
    // MDGETOBJ, so the take follows from the group switch alone.
    //
    //   MDACTION  scan for an object within 150 cm; found ->
    //             group 45, else nothing (the press falls through
    //             to Script_Pump's "nothing here")
    //   MDGETOBJ  `sub_41C490(player, slot)` - the node is linked
    //             to the hand and actor+164 points at the object's
    //             96-byte record - then the object's NAME through
    //             `Subtitle_Show`
    //   MDPUTSNK  entry 55, group 4, input 0x10: press action
    //             AGAIN and it goes in the sack - `sub_41C720`
    //             raises event 10 with the slot and clears +164
    //   MDLETOBJ  reached from entry 56 (input 0x20, the CANCEL
    //             bit): `sub_41C540(player, 0)` - released with
    //             remove=0, so the prop returns to its placement
    //
    // The three-press shape a reader described - take and see the
    harnessHoldAndCall();
    if (startShoot && !walk && player) {
        // The harness way in. The engine's own is a script's
        // `shoot.begin`, which is what `verify.py: engine: shoot
        // mode` drives; this exists so a PERSON can stand in an
        // arena and look at the mode, the way `--ride` does for
        // the slider. -1 is the operand 27 of the 30 shipped
        // sites pass, so the weapon is the Gun Waver.
        startShoot = false;
        session.shootBegin(-1);
        // The camera is NOT installed here: entering shoot mode
        // installs it, wherever the entry came from. Having it
        // here as well is what let a script-driven entry go
        // without one for a whole session (`todo/omk-play.md`
        // 97d), because the harness was the only thing ever
        // tested.
        std::printf("--shoot: shoot.begin -1\n");
    }
    harnessShootEnd();
    harnessAstaroth();      // --player-at, --astaroth-souls (instruments)
    if (openSneak && !walk && playerScreen < 0) {
        openSneak = false;
        inv.openList(0);
        playerScreen = omk::kScreenSneak;
        std::printf("--sneak: event %d opens object list 0, "
                    "screen %d\n", omk::kEventSneakOpen,
                    omk::kScreenSneak);
    }
    // WHERE THE ACTION RAISE COMES FROM, corrected 2026-09-04.
    //
    // `Game_RaiseEvent(6, 4)` has three sites in the image
    // (21_d3d.c:3460, :3513, :3962) and every one of them is an
    // ACTOR STATE HANDLER - the `.CTL` machine reaching the action
    // state, which is `MDACTION`. It is not read off the input
    // word at all. Taking it from the input EDGE instead put the
    // press one release out of step with the game: the ENTER that
    // confirms `Utiliser` is still held when the sneak closes, so
    // the .CTL enters the action state and fires MDACTION while
    // the edge has already been spent - the player pressed at the
    // lift with the key in his hand and the world never heard it.
    // A player hit exactly that and had to use a SECOND key.
    //
    // Entering a state is once per press by construction (the
    // state persists while the button is held), which is also why
    // the engine can raise from here without the six-presses-per
    // -press problem the raw LEVEL had.
    actionFromMove = false;
    // ...AND WHETHER MDACTION'S OWN OBJECT ARM CONSUMED THE PRESS.
    // See the gate on `session.pressAction()` below: the press
    // only reaches the zone system on the arm where the scan
    // found NOTHING.
    actionTookObject = false;
}

// The shot
void PlayState::adventureShot() {
    auto& in = *in_;
    auto& inv = *inv_;
    auto& session = *session_;
    // ---- THE SHOT (`actor/shootfire.h`, todo/shoot-mode.md 7h) --
    //
    // The engine's order, and it is why this sits ABOVE the move
    // loop: the channel tick - `sub_45C680` case 3, whose SHOOT
    // branch runs whenever the current entry's +12 is -1, which
    // every one of group 200's 24 entries is - consumes the latch
    // LAST frame's `MDSHOOT0` set, and only after the state tick
    // does `Actors_TickAll` drain this frame's queue, which is the
    // loop below. Then the frame loop services the request. So a
    // press reaches the gate one frame after its state, and the
    // gate fires only with the weapon fully UP.
    if (playerTicked && shootMode && player->state() == omk::ActorState::Shoot) {
        const omk::FireGate gate = omk::shootChannelTick(
            playerShootRec, shotLatch, player->ctlEntryFlags12() == 0xFFFFFFFFu,
            true, static_cast<float>(frameSec * 30.0));
        // the arm's angles move only in the gate's PULLED arm: the
        // yaw toward 0 (the body turns with the look) and the pitch
        // toward the look's (`sub_47C260`, positive up)
        if (gate != omk::FireGate::Released)
            omk::shootAimSlew(shootAim, 0.0f, shootPitch * 0.017453279f,
                              static_cast<float>(frameSec * 30.0));
        const omk::ShootWeaponRow* row = playerShootRec.weapon;
        if (shotLatch.request && row) {
            shotLatch.request = false;          // dword_4E9744 = 0
            // `Actor_TickProjectiles(player)`, the record path.
            //
            // THE MUZZLE is a RECONSTRUCTION, labelled: the engine
            // places the shot at the held object's `+12` node,
            // which rides actor `+44` - `Maing`, the left hand.
            // No weapon model is loaded here, so the shot leaves
            // from the hand node itself, placed exactly the way
            // the held prop is (the render pass, "THE OBJECT IN
            // HIS HAND").
            const float* pp = player->pos();
            const float yaw = player->facing();
            omk::RecordShot rs;
            rs.muzzle[0] = pp[0]; rs.muzzle[1] = pp[1]; rs.muzzle[2] = pp[2];
            const char* from = "his position (no pose)";
            if (const omk::NodeTracks* pt = player->poseTracks()) {
                const std::vector<omk::MeshPose> pose =
                    playerPoseNow(&*player, true, *pt, player->poseFrameF());
                int hand = -1;      // the LAST strstr hit, as o3de_Traverse leaves it
                for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                    if (std::strstr(playerMeshes[i].name, "Maing"))
                        hand = static_cast<int>(i);
                if (hand >= 0 && static_cast<std::size_t>(hand) < pose.size()) {
                    const omk::MeshPose& hp = pose[static_cast<std::size_t>(hand)];
                    // `tir`, linked under the hand with its own
                    // +128 local: the hand's rotation applied to it
                    float off[3] = {0.0f, 0.0f, 0.0f};
                    const GunFacts* gf = shotGunStem.empty() ? nullptr
                                                             : &gunFactsFor(shotGunStem);
                    if (gf && gf->ok) omk::qrot(hp.q, gf->tirLocal, off);
                    const float hin[3] = {hp.pos[0] + off[0] - playerRootXZ[0],
                                          hp.pos[1] + off[1],
                                          hp.pos[2] + off[2] - playerRootXZ[1]};
                    float ho[3];
                    omk::rotateYaw(yaw, hin, ho);
                    rs.muzzle[0] = ho[0] + pp[0];
                    rs.muzzle[1] = ho[1] + pp[1] - playerFeet + lastRootDrop;
                    rs.muzzle[2] = ho[2] + pp[2];
                    from = gf && gf->ok ? "the tir node" : "the Maing node";
                }
            }
            // the SHOT SPRITE: section A by the gun's root name
            int muzzleFx = 0;
            if (!shotGunStem.empty())
                if (const omk::FxShotSprite* sp =
                        shootSfx.shotSprite(gunFactsFor(shotGunStem).root)) {
                    muzzleFx = sp->muzzleEffect;
                    rs.impactEffect = sp->impactEffect;
                    rs.sprite = true;
                    rs.windUp = sp->windUp;
                    rs.grow = sp->grow;
                    for (int k = 0; k < 3; ++k) rs.growStep[k] = sp->growStep[k];
                }
            rs.yawDeg = yaw;
            rs.pitchDeg = shootPitch;
            harnessAimAt(rs);       // --aim-at (an instrument)
            // THE MAGAZINE: property 35, slot `index - 1`, on the
            // DB player record. A row with index 0 has none.
            std::int32_t count = 0;
            const std::size_t recAt = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
            const std::size_t recLen = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
            const bool mag = row->ammoIndex &&
                omk::readAmmoSlot(state.raw().subspan(recAt, recLen),
                                  row->ammoIndex - 1, count);
            int left = static_cast<int>(count);
            if (mag) rs.ammo = &left;
            const omk::RecordShotOut out = projectiles.fireFromRecord(-1, *row, rs);
            if (mag && out.ammoSpent)
                omk::writeActorProperty(state.rawMutable().subspan(recAt, recLen), 0x23,
                                        ((row->ammoIndex - 1) << 16) | (left & 0xFFFF));
            if (mag && out.ammoSpent) hudAmmo = out.hudAmmo;   // `dword_90E11C`
            if (out.entry >= 0) {
                ++shotsFired;
                // `sub_44EF80(row, node, ..., 0.0)` as the entry is
                // made: the MUZZLE effect, its sound armed
                shotSound(n, muzzleFx, rs.muzzle, player->pos(), "fire");
                // ...and the NOISE at the muzzle (0x44D5CC / 0x44D7A5)
                shootNoise(n, -1, rs.muzzle, "a shot");
                const omk::Projectile& e =
                    projectiles.entries()[static_cast<std::size_t>(out.entry)];
                const float sp = e.speed != 0.0f ? e.speed : 1.0f;
                std::printf("frame %ld: SHOT %ld - Actor_TickProjectiles(player): "
                            "entry %d, speed %.1f, damage %d, dir %.3f %.3f %.3f "
                            "(yaw %.1f pitch %.1f), from %s %.1f %.1f %.1f, "
                            "magazine %s, %d live\n", n, shotsFired, out.entry,
                            double(e.speed), e.kind, double(e.vel[0] / sp),
                            double(e.vel[1] / sp), double(e.vel[2] / sp),
                            double(yaw), double(shootPitch), from,
                            double(rs.muzzle[0]), double(rs.muzzle[1]),
                            double(rs.muzzle[2]),
                            mag ? (std::to_string(count) + " -> " +
                                   std::to_string(left)).c_str() : "none",
                            projectiles.liveOf(-1));
            } else {
                std::printf("frame %ld: SHOT refused - the pool is full (%d live), "
                            "nothing spent\n", n, projectiles.live());
            }
        }
    }
    for (const auto& mv : (playerTicked ? player->specialMoves() : kNoMoves)) {
        // IN THE WATER `MDACTION` IS THE WAY OUT (`sub_4A9580`, `actor/player.h`):
        // groups 301 and 302 both carry it, and its arm looks for a
        // platform ahead of him instead of for something to use.
        if (mv == "MDACTION") {
            const int ws = static_cast<int>(player->state());
            if (ws >= 11 && ws <= 14) {
                float over = 0.0f;
                const int why = player->waterClimbOut(&over);
                static const char* kWhy[5] = {"", "not at the surface",
                    "no platform within 80 cm ahead", "the platform is too high or under water",
                    "no water edge on the way back"};
                if (why == 0) {
                    in.installScheme(0);
                    std::printf("frame %ld: OUT OF THE WATER (sub_4A9580) - a platform %.1f over "
                                "the water; ACTOR_STATE %d, bank group 303 (H_WO_SD), scheme 0, "
                                "at %.0f %.1f %.0f\n", n, double(over),
                                static_cast<int>(player->state()), player->pos()[0],
                                player->pos()[1], player->pos()[2]);
                } else {
                    std::printf("frame %ld: the climb out refused - %s%s\n", n, kWhy[why],
                                why == 3 ? (" (" + std::to_string(over) + " over the water)").c_str() : "");
                }
                continue;
            }
        }
        if (mv == "MDACTION") actionFromMove = true;
        // `MDSHOOT0` (0x0046B610): the latch, in ACTOR_STATE 3
        // only - the next frame's channel tick hands it to the gate.
        if (mv == "MDSHOOT0" &&
            omk::mdShoot0(shotLatch, static_cast<int>(player->state())))
            std::printf("frame %ld: MDSHOOT0 - the latch (dword_53AE3C) armed\n", n);
        // MOVING IN FIRST PERSON (`actor/shootmove.h`). Group 200's
        // movement entries play no clip: they queue these, and the
        // handlers only raise INTENTS for next frame's mover. What
        // refuses them is the actor's fall byte (+1304), which is
        // the walker's airborne flag here - the landing's codes are
        // not kept. Each is a no-op outside the mode, as the
        // engine's are with `dword_6579CC` null.
        if (mv == "MDAV" || mv == "MDAR") {
            float headPitch = 0.0f;   // HEAD mode: action 7 has no key
            omk::shootMoveForward(shootMover, mv == "MDAV",
                                  player->walker().airborne(),
                                  static_cast<float>(frameSec * 30.0), headPitch);
        }
        if (mv == "MDDG" || mv == "MDDD")
            omk::shootMoveStrafe(shootMover, mv == "MDDD",
                                 player->walker().airborne());
        if (mv == "MDDO" || mv == "MDUP")
            omk::shootMoveCrouch(shootMover, mv == "MDDO");
        if ((mv == "MDRG" || mv == "MDRD") && shootMover.active)
            player->aimYawBy(omk::shootTurnDegrees(mv == "MDRG" ? -50 : 50,
                                                   mouseSensX));
        // `Regarder En-Haut` / `En-Bas`: MDLUP and MDLDO are
        // `sub_45D1D0(0, 0, -/+25)` (0x0046B6E0 / 0x0046B6F0) - the
        // mouse's pitch, stepped 25 units; the camera takes it at
        // the aim block's next pass
        if ((mv == "MDLUP" || mv == "MDLDO") && shootMover.active) {
            if (omk::shootMovePitches(shootMover))
                shootPitch = -omk::shootPitchStep(-shootPitch,
                                                  mv == "MDLUP" ? -25 : 25,
                                                  mouseSensY, mouseInverted,
                                                  static_cast<float>(frameSec * 30.0));
            else
                shootPitch = 0.0f;            // the Mecagarde arm, as above
            shootPitchDirty = true;
        }
        // THE JUMP'S IMPULSE (`todo/player-vertical.md` step 2).
        // `MDJUMP01` is the one that writes the three velocity
        // fields; `MDJUMP0A`/`0B` only prepare them, and the
        // controller does that arithmetic (`player.cpp`). Before
        // this the walker's `vy_`/`airborne_` existed for FALLING
        // and nothing ever pushed into them, which is the reader's
        // *"jump is broken (animation play, but y position is too
        // low so the jump become useless)"* - the clip played and
        // the body never left the floor.
        if (mv == "MDJUMP0A" || mv == "MDJUMP0B") {
            if (player->jumpPrepare())
                std::printf("jump: armed on '%s'\n",
                            player->ctlStateName().c_str());
        }
        if (mv == "MDJUMP03") {
            const double d = player->walker().lastLandingDrop();
            const int band = player->jumpLand();
            std::printf("jump: landed, drop %.2f -> band %d%s\n",
                        d, band,
                        band == 2 ? " (short, no reaction)"
                                  : " (bank group 2, camera 18)");
            if (band != 2) fallCamRequest(18, true, "MDJUMP03", static_cast<int>(player->state()));
        }
        // `MDRAISE0` (0x0046BED0, `H_SOL-SD` - getting up from a
        // 5 m landing): `sub_414DE0(actor, 0, 0)`, home from 19
        if (mv == "MDRAISE0") fallCamRequest(0, false, "MDRAISE0", static_cast<int>(player->state()));
        // THE WATER MOVES (`todo/swimming.md` 1, read from the raw
        // image). `MDDIVEND` 0x0046BEF0: ACTOR_STATE 14 and message 22.
        // `MDSW2SD` 0x0046BF20: ACTOR_STATE 1 and, if a camera is
        // up, camera 0 on him over 50 frames. `RSTAVNT` 0x0046C120:
        // ACTOR_STATE 1, pitch 0, `Input_InstallScheme(0)`.
        // `RSTNAGE` 0x0046C150: ACTOR_STATE 14, pitch 0. `MDDIVBEG`
        // 0x0046C180: `+1288 |= 2`, the dive.
        if (mv == "MDDIVEND") {
            player->setActorState(omk::ActorState::Swim, "MDDIVEND");
            const bool ran = session.postMessage(22, session.playerActor());
            std::printf("water: MDDIVEND - ACTOR_STATE %d, message 22 %s\n",
                        static_cast<int>(player->state()),
                        ran ? "to its handler" : "- no handler subscribes");
        }
        if (mv == "MDSW2SD") {
            player->setActorState(omk::ActorState::Normal, "MDSW2SD");
            if (takeCam) { takeCamRequest(3); takeCamTravel = 50.0f; }
            std::printf("water: MDSW2SD - out of the water, ACTOR_STATE %d%s\n",
                        static_cast<int>(player->state()),
                        takeCam ? ", camera 0 over 50 frames" : "");
        }
        if (mv == "RSTAVNT") {
            player->setActorState(omk::ActorState::Normal, "RSTAVNT");
            player->eulerPitch() = 0.0f;
            in.installScheme(0);
            std::printf("water: RSTAVNT - ACTOR_STATE %d, pitch 0, scheme 0\n",
                        static_cast<int>(player->state()));
        }
        if (mv == "RSTNAGE") {
            player->setActorState(omk::ActorState::Swim, "RSTNAGE");
            player->eulerPitch() = 0.0f;
            std::printf("water: RSTNAGE - ACTOR_STATE %d, pitch 0\n",
                        static_cast<int>(player->state()));
        }
        if (mv == "MDDIVBEG") {
            player->waterFlags() |= 2u;
            std::printf("water: MDDIVBEG - the dive flag (+1288 |= 2)\n");
        }
        if (mv == "MDJUMP01") {
            const bool went = player->jumpLaunch();
            std::printf("jump: %s\n", went
                ? "launched"
                : "refused (nothing armed, or already off the ground)");
        }
        const omk::SpecialMoves::Row* row = specialMoves.find(mv);
        if (row)
            std::printf("special move: %s (tab_special_move[%d] = 0x%08x)\n",
                        row->name.c_str(), row->index, row->handler);
        // ...NOT in ACTOR_STATE 3: MDACTION scans (`sub_41C810`) and then
        // `cmp [esi+194h], 3; je 0x46AFF8` - a shoot phase takes nothing,
        // and the press goes on to the zones below (todo/drift-audit.md S7)
        if ((mv == "MDACTION" || mv == "MDADJSTP") &&
            !(player && player->state() == omk::ActorState::Shoot)) {
            // ---- `sub_465D30(actor, obj, fromAdjust)` ----------
            //
            // ONE function decides both stages of the take, and
            // until 2026-09-04 this ported only its last twenty
            // lines (which group) and guessed the rest. Read whole:
            //
            //   dx, dy, dz  = object node pos - actor node pos.
            //                 The actor's node is the PELVIS (the
            //                 same +244..+252 the follow camera
            //                 targets), so dy here is the port's
            //                 feet-relative dy plus the pelvis
            //                 height - and it counts DOWN, like
            //                 every y in this world. An object on
            //                 the floor is +40 below the pelvis;
            //                 `dy <= 27.47` (less than 70 cm below
            //                 it) is the HIGH take, group 143.
            //   D           = hypot(dx, dz); angle = the signed
            //                 bearing off his facing.
            //   LOW arm:      angle -= 10 (a bias, authored into
            //                 the clips); target = 40 cm / cos;
            //                 second = dy - 27.47 (scaled by
            //                 1/29.53 inside sub_466390).
            //   HIGH arm:     target = 60 cm / cos; second = the
            //                 PITCH of the object seen from the
            //                 target point, asin(-dy / hypot(
            //                 target, dy)) in degrees - which
            //                 answers what reaches +0x1C8 there.
            //   refuse if     |angle| > 50 and (fromAdjust or
            //                 D < target), or |D - target| > 120cm.
            //   from MDACTION: if D/target is within 10%, no step:
            //                 take at once. Else the step SCALE
            //                 `dword_6A5380 = |D - target|/19.69`.
            //   target point = object - target * dir; the move
            //                 to it is `Actor_Move`d OUTRIGHT
            //                 before a take (both from MDADJSTP
            //                 and in the no-step case), and only
            //                 PROBED before a step: if the probe
            //                 moves under 25 cm the step is
            //                 dropped and it takes at once (and
            //                 fails if still > 125 cm off).
            //   angle stored  flipped by 180 when D < target and a
            //                 step is coming: he steps BACK.
            //
            // The step's DIRECTION and DISTANCE were the two
            // faults a reader saw across 16 presses: he stepped
            // 50 cm whatever the distance and walked through the
            // rings twice. Both are this function.
            const bool fromAdjust = mv == "MDADJSTP";
            constexpr float kTakeHigh  = 27.472441f;   // flt_4BC7E0, 70 cm
            constexpr float kReachLow  = 15.748032f;   // 40 cm
            constexpr float kReachHigh = 23.622047f;   // 60 cm
            constexpr float kStepLen   = 19.685039f;   // 1 / 0.0508, the 50 cm step
            constexpr float kMaxError  = 47.244096f;   // 120 cm
            constexpr float kNoStep    = 9.8425198f;   // 25 cm
            constexpr float kMaxProbe  = 49.212598f;   // 125 cm
            player->setAdjustStep(false);
            float dyFeet = 0.0f;
            const int obj = session.scanTakeable(player->pos(),
                                                 player->facing(), &dyFeet);
            // `sub_41C810` answered with something inside
            // `flt_4BC918`, so MDACTION takes its object arm and
            // RETURNS - see the `pressAction` gate below. Set from
            // the scan and not from whether the take succeeded,
            // because `sub_465D30` refusing lands at `loc_46AFB2`,
            // which returns too.
            if (obj >= 0) actionTookObject = true;
            float op[3] = {0, 0, 0};
            bool  ok = obj >= 0 && session.propPos(obj, op);
            float D = 0.0f, angle = 0.0f, target = 0.0f, second = 0.0f;
            float dyP = 0.0f, mx = 0.0f, mz = 0.0f, scale = 1.0f;
            bool  low = true, takeNow = fromAdjust, moved = false;
            const char* why = "no object in reach";
            if (ok) {
                const float dx = op[0] - player->pos()[0];
                const float dz = op[2] - player->pos()[2];
                dyP = dyFeet + player->cameraLift();
                D = std::sqrt(dx * dx + dz * dz);
                if (!(D > 0.0f)) { ok = false; why = "standing on it"; }
                else {
                    // the port's facing convention, as before:
                    // `headingFromClipRoot` ends `atan2(z, x) + 90`
                    const float bearing = std::atan2(dz, dx) *
                                          57.29577951308232f + 90.0f;
                    float rel = bearing - player->facing();
                    while (rel < -180.0f) rel += 360.0f;
                    while (rel >  180.0f) rel -= 360.0f;
                    // THE ENGINE'S SIGN IS THE OPPOSITE OF `rel`.
                    // `sub_465D30`: `v41 = acos(cos); if (fx*dz -
                    // fz*dx > 0) v41 = -v41`, and with the heading
                    // recipe both facings use (`atan2(z, x) + 90`,
                    // so f = (sin F, -cos F)) that cross product is
                    // sin(bearing - facing) = sin(rel). Positive
                    // rel is a NEGATIVE engine angle. Measured
                    // before the flip (2026-09-04, run 4): the
                    // step's side cell pushed him AWAY from the
                    // object line - lateral offset 15.4 -> 26.8
                    // and 17.7 -> 20.5 across two steps - because
                    // the quadrant table was fed the mirrored
                    // sign.
                    angle = -rel;
                    const float cosA = std::cos(rel * 0.017453292f);
                    low = dyP > kTakeHigh;
                    if (low) { angle -= 10.0f; target = kReachLow / cosA;
                               second = dyP - kTakeHigh; }
                    else     { target = kReachHigh / cosA; }
                    const float err = std::fabs(D - target);
                    if (std::fabs(angle) > 50.0f && (fromAdjust || D < target)) {
                        ok = false; why = "outside the 50 degree cone";
                    } else if (err > kMaxError) {
                        ok = false; why = "more than 120 cm off the target";
                    } else {
                        if (!fromAdjust) {
                            const float r = std::fabs(D / target);
                            if (r > 0.9f && r < 1.1f) takeNow = true;
                            else scale = err / kStepLen;
                        }
                        mx = dx - target * dx / D;
                        mz = dz - target * dz / D;
                        if (!low)
                            second = std::asin(-dyP / std::sqrt(target * target + dyP * dyP)) *
                                     57.29577951308232f;
                        if (!takeNow) {
                            // **PORT SHORTCUT, labelled**: the
                            // engine PROBES the move (Actor_Move,
                            // then puts him back) and measures
                            // what the collision let through;
                            // this takes the requested length.
                            const float probe = std::sqrt(mx * mx + mz * mz);
                            if (probe < kNoStep) {
                                takeNow = true; scale = 1.0f;
                                if (std::fabs(D - probe) > kMaxProbe) {
                                    ok = false; why = "cannot close on it";
                                }
                            }
                        }
                        if (ok && takeNow) moved = player->moveBy(mx, mz);
                        if (!(D >= target || takeNow))
                            angle = angle < 0.0f ? 180.0f - angle : angle - 180.0f;
                    }
                }
            }
            if (!ok) {
                if (obj >= 0)
                    std::printf("take: %s - object %d '%s' refused (%s): D %.1f "
                                "target %.1f angle %+.1f dy(pelvis) %+.1f\n",
                                mv.c_str(), obj, session.objectName(obj).c_str(),
                                why, D, target, angle, dyP);
                else if (fromAdjust)
                    std::printf("take: MDADJSTP - nothing in reach after the step\n");
                // engine: dword_53AE1C = 0, nothing installed
            } else if (takeNow) {
                const int g = low ? 41 : 143;
                player->setTakeGeometry(angle, low ? second * 0.033866666f : second);
                takeCandidate = obj;
                takeWasLow = low;
                std::printf("take: %s - object %d '%s' D %.1f target %.1f -> moved %s "
                            "%+.1f %+.1f; %s take group %d, angle %+.1f second %+.2f\n",
                            mv.c_str(), obj, session.objectName(obj).c_str(), D, target,
                            moved ? "to" : "BLOCKED toward", mx, mz,
                            low ? "LOW H_TAKL" : "HIGH H_TAKH", g, angle, second);
                if (!player->enterGroupById(g))
                    std::printf("take: the bank has no group %d\n", g);
            } else {
                // ---- STAGE ONE: THE ADJUST STEP ---------------
                // `sub_466210`: group 600, H_ADJSTP, the cells
                // picked by the angle's quadrant and sign, the
                // root motion scaled by `dword_6A5380`.
                player->setTakeGeometry(angle, low ? second * 0.033866666f : second);
                player->setAdjustStep(true);
                player->setStepScale(scale);
                takeCandidate = obj;
                takeWasLow = low;
                std::printf("take: MDACTION - object %d '%s' D %.1f target %.1f: "
                            "adjust step of %.1f (scale %.2f) at angle %+.1f, "
                            "then the %s take\n",
                            obj, session.objectName(obj).c_str(), D, target,
                            std::fabs(D - target), scale, angle,
                            low ? "LOW" : "HIGH");
                if (!player->enterGroupById(600))
                    std::printf("take: the bank has no group 600\n");
            }
        } else if (mv == "MDGETOBJ") {
            if (takeCandidate >= 0 && session.takeObject(takeCandidate)) {
                // `sub_4083F0(46, ...)` then `Subtitle_Show`
                // (0x0041E040): the object's NAME at the bottom of
                // the screen, which is what a reader described
                // seeing on the take. It rides the same subtitle
                // the voice-overs use.
                mediaText = session.objectName(takeCandidate);
                mediaTextFrames = mediaText.empty() ? 0 : 90;
                std::printf("take: MDGETOBJ - holding %d '%s'\n",
                            takeCandidate, mediaText.c_str());
                // `Camera_Request(1, dword_930800)`: the take
                // camera, travelling 30 frames from what is on
                // screen (`sub_414A90` keeps the outgoing block as
                // g_CameraPrev, request+24 = 30 the frames).
                heldInHand = takeCandidate;       // `sub_41C490`: it rides the hand from here
                {
                    float op[3] = {0, 0, 0};
                    session.propPos(takeCandidate, op);
                    const auto& objs = voiceLib.objects();
                    const PropModel* pm = takeCandidate >= 0 &&
                        static_cast<std::size_t>(takeCandidate) < objs.size()
                        ? propModelFor(objs[static_cast<std::size_t>(takeCandidate)].stem) : nullptr;
                    std::printf("take: GRAB object %d - placement %.1f %.1f %.1f, model %s "
                                "origin %.2f %.2f %.2f local(+128) %.2f %.2f %.2f, %zu rest corners\n",
                                takeCandidate, op[0], op[1], op[2],
                                pm ? "ready" : "MISSING",
                                pm ? pm->origin[0] : 0.f, pm ? pm->origin[1] : 0.f, pm ? pm->origin[2] : 0.f,
                                pm ? pm->localOff[0] : 0.f, pm ? pm->localOff[1] : 0.f, pm ? pm->localOff[2] : 0.f,
                                pm ? pm->rest.corners.size() : 0u);
                }
                takeCam = true;
                takeCamRequest(1);
                std::printf("take: camera mode 1 requested - preset 1 over 30 frames\n");
            }
        } else if (mv == "MDPUTSNK") {
            // `if (C+12 == 1) Camera_Request(16)`: back to the
            // camera the take displaced, 30 frames.
            if (takeCam) {
                takeCamRequest(3);
                std::printf("take: camera mode 16 requested - back to the follow camera over 30 frames\n");
            }
            const int was = static_cast<int>(
                omk::objectList(state, omk::ObjectList::Carried).size());
            const auto arm = session.bankHeldObject(takeCandidate);
            const int now = static_cast<int>(
                omk::objectList(state, omk::ObjectList::Carried).size());
            // NAME THE ARM. `Inventory_Insert`'s four outcomes are
            // not interchangeable and a count alone cannot tell
            // them apart - a merge and a full list both leave it
            // unchanged, and the first is correct where the second
            // is a refusal. Saying "REFUSED (full, or a kind
            // Inventory_Insert would merge)" made a reader guess,
            // and it was wrong: the kind-13 rings were neither.
            using B = omk::Session::Banked;
            const char* what =
                arm == B::Row      ? "a row at the front of list 0"
              : arm == B::Consumed ? "kind 12/13: Object_ApplyEffect applied and "
                                     "CONSUMED, no row - the count is the take"
              : arm == B::Merged   ? "merged into an existing row of a related "
                                     "kind, no new row - and the quantity is in "
                                     "the 56-byte cache this port does not model, "
                                     "so nothing counts up"
                                   : "REFUSED - list 0 is full, case 10 returns 0 "
                                     "and it stays in his hand";
            heldInHand = -1;
            std::printf("take: RELEASE object %d into the bank\n", takeCandidate);
            std::printf("take: MDPUTSNK - object %d '%s' (kind %d) -> "
                        "carried list %d -> %d: %s%s%s\n",
                        takeCandidate,
                        session.objectName(takeCandidate).c_str(),
                        session.objectKind(takeCandidate), was, now, what,
                        session.lastObjectEffect().empty() ? "" : "; effect: ",
                        session.lastObjectEffect().c_str());
            takeCandidate = -1;
        } else if (mv == "MDNOTAKE") {
            // `sub_46B530` (0x0046B530): a four-case switch on
            // `dword_53AE5C`, the code MDACTION kept - 0 -> group
            // 0x8C = 140 (H_PUTL12 -> MDLETOBJ -> H_PUTL22), 3 ->
            // group 9 (H_PUTH12/22); 1 and 2 are groups 6 and 7.
            // The cancel PLAYS THE PUT-BACK, and MDLETOBJ inside
            // that group is what releases the object and swaps
            // the camera. Until 2026-09-04 the port had no
            // MDNOTAKE handler at all, so a cancel went straight
            // to standing with the object still held and the
            // take camera still up (a reader: "if I cancel the
            // grab, the camera doesn't return").
            const int putGroup = takeWasLow ? 140 : 9;
            const bool got = player->enterGroupById(putGroup);
            std::printf("take: MDNOTAKE - cancel: the put-back plays (%s, group %d%s)\n",
                        takeWasLow ? "LOW H_PUTL" : "HIGH H_PUTH", putGroup,
                        got ? "" : " - not in this bank");
        } else if (mv == "MDLETOBJ") {
            if (takeCam) {                // the same mode-16 swap
                takeCamRequest(3);
                std::printf("take: camera mode 16 requested - back to the follow camera over 30 frames\n");
            }
            // The put-back matches the take's HEIGHT, the same way
            // and from the same decision: the engine keeps
            // `dword_53AE5C = (ret == 2) ? 3 : 0` at MDACTION and
            // `sub_46B530` turns it back into a group - case 0 ->
            // 140, case 3 -> 9. H1Avnt.CTL: 140 is H_PUTL12/22 and
            // 9 is H_PUTH12/22, the mirrors of the two takes. The
            // switch is best-effort; a bank without the group
            // leaves the machine where it is, which is what
            // `Cef_FindGroupById` returning nothing does.
            // Inside the put group already (MDNOTAKE entered it):
            // `sub_41C540(actor, 0)` - the object back to the world.
            std::printf("take: MDLETOBJ - RELEASE object %d, put back where it was\n",
                        takeCandidate);
            session.putHeldObjectBack();
            heldInHand = -1;
            takeCandidate = -1;
        } else if (mv == "MDSLIDIN") {
            // 0x0046B7F0, and the channel is what fires it: group
            // 60's entry [160] is a child of `H_SLDIN` with no
            // input at all, so it is taken when the door clip
            // ends, and its GoTo lands on `H_SLIDER` - the riding
            // pose. The handler's own body is
            // `sub_438420(slider, 4); player[+404] = 7;
            // UI_OpenScreen(7, -1, -1, -1)`: he is ABOARD, and the
            // slider page opens as SCREEN 7, whose param is 1.
            // What happens next is that page's hook (0x0049D4D0):
            // with a destination remembered from the sneak the
            // journey starts at once; with none, the bar reads
            // Automatique / Manuelle and he chooses.
            boarding = false;
            boarded = true;
            boardCam = 0;
            player->setActorState(omk::ActorState::SliderMount, "MDSLIDIN");
            player->setChannelOnly(false);
            player->clearRootFrame();
            session.sliders().mountCalled();
            playerScreen = 7;
            float at[3] = {0, 0, 0};
            session.sliders().calledAt(at);
            std::printf("MDSLIDIN: aboard at %.0f %.0f %.0f - H_SLDIN "
                        "ended and the channel took its no-input child "
                        "to H_SLIDER; ACTOR_STATE 7, screen 7 opens\n",
                        at[0], at[1], at[2]);
        } else if (mv == "MDSLIDOU") {
            // 0x0046B890, the mirror of `MDSLIDIN`: group 61's
            // entry [162] is a child of `H_SLDOUT` with no input,
            // so it fires when that clip ends, and [163] gotos
            // `H_STAND`. The handler refuses from anything but
            // ACTOR_STATE 8 ("bad mode getting out of the slider
            // !") and leaves the actor at 1.
            leaving = false;
            session.sliders().slidOutCalled();   // 5 -> 7 (or 7 -> 5 -> 7)
            slidOutAt = n;
            if (takeCam) takeCamRequest(3);      // back to the follow camera
            player->setActorState(omk::ActorState::Normal, "MDSLIDOU");
            player->setChannelOnly(false);
            player->clearRootFrame();
            std::printf("MDSLIDOU: out and standing - H_SLDOUT ended, "
                        "the channel gotos H_STAND, ACTOR_STATE 1\n");
        } else if (mv == omk::kMoveOpenSneak) {
            // ROW 0, and the other end of the same table. TAB is
            // the Aventure scheme's "Ouvrir sneak" (bit 0x2000);
            // H1Avnt/F1Avnt group 0 has an entry waiting on
            // exactly that bit whose flag-2 alias redirects
            // through its GoTo into group 6, and group 6's child
            // names `MDSNEAK0` (`actor/moves.h` quotes the whole
            // chain and `sub_0046ADF0` with it).
            //
            // The handler's own gate is `sub_41A350(actor)`: when
            // the actor carries a pending target at `+164` it
            // raises event 10 for that object and does NOT open
            // the sneak - which is the SAME `+164` MDGETOBJ links
            // an object to just above. Not modelled, so the sneak
            // always opens; with nothing held that is what the
            // engine does, and holding something is the case to
            // come back to.
            //
            // What IS reproduced is the pair around it: event 25
            // opening object list 0 on the way in, and event 26
            // if the open fails or when the close comes.
            if (walk || playerScreen >= 0) continue;   // already up
            inv.openList(0);          // Game_RaiseEvent(25, 0)
            playerScreen = omk::kScreenSneak;
            std::printf("MDSNEAK0: event %d opens object list 0, "
                        "screen %d\n", omk::kEventSneakOpen,
                        omk::kScreenSneak);
        }
    }
    // `Actor_TickNpc`: `Actor_ApplyMotion`, then `Actor_ScanZones`
    // at the position it left - the Session's scan reads this on
    // its next frame (wave B, T15). Facing in the +420 degrees.
    // NOTE: his EULER is deliberately not touched while he boards.
    // `MDACTION` and `sub_468FA0` write node+156 and never +420,
    // so the zone scan and everything else that reads his facing
    // keep the way he walked up; only the DRAWN orientation
    // belongs to the vehicle, and that is applied where the model
    // is posed - putting it here moved nothing on screen at all.
    session.setPlayerPosition(player->pos(), player->facing());
    // ...and his BOX for the zone index: the pelvis above the feet and the
    // root mesh's radius (`Actor_ScanZones`, todo/drift-audit.md S10)
    if (player->cameraLift() > 0.0f && player->rootRadius() > 0.0f)
        session.setPlayerZoneBox(player->cameraLift(), player->rootRadius());
}

// The action button, MDACTION, one activation per press
void PlayState::adventureAction() {
    auto& session = *session_;
    // ---- THE ACTION BUTTON -------------------------------
    //
    // `Game_RaiseEvent(6, 4)` from the input handler, which is
    // `Game_HandleEvent` case 6:
    //
    //     if (g_DialogState == 3 || a2 != 4 || !dword_4E6B24)
    //         return 0;
    //     dword_4E6C90 = 1;
    //
    // so a press with no prompt slot taken never reaches the pump,
    // and the pump clears the flag at the end of its slot loop.
    // `Session::pressAction` models all of that, including handing
    // the tracked position to `talkToPedestrian` for the crowd.
    //
    // **AND IT FIRES ON THE EDGE**, which is a correction: this
    // used to press on the LEVEL, reasoning that "the pump clears
    // the flag each frame, so a held button is one press per
    // frame". A reader measured what that does - a normal 0.2 s
    // press counted as **six** presses - and the reasoning was
    // wrong about where the raise comes from.
    //
    // `Game_RaiseEvent(6, 4)` is NOT raised by the input handler.
    // Its three sites are all ACTOR functions - `sub_466B60`,
    // `Actor_TickUiHeld` (the ACTOR_STATE 9/17 tick) and
    // `sub_467950` - and every one uses the RETURN as a VETO:
    // `if (!a1[41] || Game_RaiseEvent(6, 4)) goto ...`. So the
    // engine never reads an action BIT here at all: the press
    // reaches the actor through the `.CTL` channel, whose
    // transition matching fires once per press because the second
    // frame of a held button finds the actor already in the state.
    // The channel is the edge filter, and the port does not route
    // the action through it - so the viewer must supply the edge
    // itself or the world sees a press a frame.
    //
    // It is also what the take needs to be usable at all: the
    // mechanic is press -> the take animation and the title, press
    // AGAIN -> the inventory (omk-play 69), and six presses inside
    // one keystroke makes those two steps unreachable.
    //
    // Bit 0x10 is "Action / Utiliser" - group 0 action 4 of
    // `tables/key_bindings.json`, keyboard 28, DIK_RETURN. The
    // Session was modelling the press and the zone registry was
    // arming its slots, and nothing in the viewer ever pressed it,
    // so no object could be taken and no pedestrian talked to
    // (`todo/omk-play.md` 65).
    // ---- `tab_special_move[3]` = 0x0046AEC0, the MDACTION
    // handler, and it is what makes a HELD action button fire
    // ONCE (todo/next-tasks.md 1).
    //
    // It has no `proc` label - read it with `asmfn.py`. It opens
    // by switching on `[esi+194h]`, the actor's `+404`
    // ACTOR_STATE, over eleven cases indexed `state - 4` through
    // `byte_46B2BC[] = {0,4,4,4,4,4,4,1,4,2,3}`:
    //
    //     state 4, 11   -> arm at loc_46B29F        (unported)
    //     state 13      -> sub_4A9580(actor)        (unported)
    //     state 14      -> Game_RaiseEvent(6, 4)    (unported)
    //     everything else, including 0..3 and 15+ which fall past
    //                      the `ja` -> the object-search arm
    //
    // and the object-search arm refuses on `ACTOR_STATE == 3`.
    //
    // WHY THE ENGINE DOES NOT REPEAT, traced rather than assumed:
    // it is NOT an edge filter. `Cef_TickChannel` reads DirectInput
    // STATE (`sub_43D920`, bit 0x80 = down) and `sub_4A7A20` is a
    // pure bit remap, so the engine queues MDACTION every frame
    // while the button is held, exactly as this port does. The
    // guard is at the END of a SUCCESSFUL action: `sub_465D30`
    // calls `SetPersoBankGroup`, whose memset clears the channel's
    // queue and latches and seeds them with the idle word - so the
    // held word must change before anything matches again.
    //
    // Ported here as the two gates that can be transcribed exactly
    // plus that input memset. What is NOT ported and is labelled
    // so: the three special arms above, and the BANK the engine
    // switches to (it comes from the object, in `sub_465D30`).
    const int actorState = player ? static_cast<int>(player->state()) : -1;
    // ACTOR_STATE 3 IS NOT A REFUSAL (corrected 2026-10-05,
    // todo/drift-audit.md S7). The object-search arm skips the TAKE in
    // state 3 (0x46AF3F) and the slider arm (0x46B007), and its tail does
    // `cmp [esi+194h], 3; je 0x46B296` past the pedestrian talk
    // (`sub_452280`) straight to `sub_467950` - `Game_RaiseEvent(6, 4)`, the
    // ZONE PRESS. This refused state 3 outright, so no zone could be
    // activated in a shoot phase: the ladders (SCENE 62's Echelles), the
    // doors and every activate script a phase hangs on a press.
    const bool shootPress = actorState == 3;
    const bool stateAllowsAction =
        actorState != 3 &&                       // the object arm's own refusal
        actorState != 4 && actorState != 11 &&   // loc_46B29F
        actorState != 13 && actorState != 14;    // the other two arms
    if (actionFromMove && !stateAllowsAction && !shootPress)
        std::printf("action: refused - ACTOR_STATE %d takes another arm of "
                    "tab_special_move[3]\n", actorState);
    // ---- ONE ACTIVATION PER PRESS ----------------------
    //
    // `MDACTION` fires EVERY frame the button is held - that is
    // the engine, not a fault, and `Cef_TickChannel`'s chain loop
    // (`readable/src/29_win32.c`, the `(v17 & 0x10) && (v17 &
    // 0x200000)` arm) re-applies it from the raw device word on
    // every tick. What the engine does NOT do is run the zone's
    // activate script every one of those frames: `Game_RaiseEvent
    // (6, 4)` is raised by the ACTOR's state handlers, once on the
    // way INTO the action state, and `Script_Pump`'s slot machine
    // then runs the arm once.
    //
    // The slot machine is not modelled to that depth here, so this
    // stands in for it and is labelled as the RECONSTRUCTION it
    // is: the world's action is honoured once per press, and the
    // bit must go up before another is. The take's own second and
    // third presses are unaffected - `MDPUTSNK` (entry 55, input
    // 0x10) and `MDNOTAKE` (entry 56, 0x20) are transitions inside
    // the take bank, reached by the `.CTL` machine and not by this
    // gate.
    //
    // This replaces an earlier guard that installed the take bank
    // (group 41) on ANY successful press. That was wrong twice
    // over: `sub_465D30` reaches `SetPersoBankGroup` only when its
    // object scan FOUND something - a press that merely activates
    // a zone never gets there - and the take pipeline above
    // already installs 41/143/600 when it does. Its symptom was a
    // reader finishing a shop seller's conversation and watching
    // the whole take graph run on nothing: reach, wait, put back.
    // ---- AND THE PRESS ONLY REACHES THE ZONES IF MDACTION
    // ---- FOUND NOTHING TO TAKE.
    //
    // `Game_RaiseEvent(6, 4)` - the thing `Script_Pump` sees as
    // `dword_4E6C90` - is raised from INSIDE `MDACTION`
    // (0x0046AEC0), on one arm and one only. The handler's exits:
    //
    //     scan hit, in reach -> sub_465D30 ... retn   (the take)
    //     sub_465D30 refused -> loc_46AFB2   ... retn
    //     something HELD     -> loc_46AFD0   ... retn
    //     scan MISS / out of reach / state 3 -> loc_46AFF8, the
    //         slider arm; no slider -> loc_46B281:
    //             sub_452280 (talk to a walker) ... else
    //             sub_467950 -> Game_RaiseEvent(6, 4)
    //
    // So a press that finds an object never becomes a zone press
    // at all, and `Script_Pump` step 2 - "a press was registered,
    // no zone activated, the hand is empty" - cannot fire on it.
    //
    // THIS IS `todo/omk-play.md` 92, and it is the link that was
    // never on the list: seven others were read and cleared while
    // this one was assumed, because the port raised the press off
    // the special-move list rather than off the arm. The symptom
    // was a reader taking food out of Kay'l's kitchen cupboard and
    // hearing *"Je ne vois pas quoi faire avec ca"* every time -
    // one of GLOBAL script 10's seven, posted because the cupboard
    // zone is a spent ONE-SHOT and nothing else could consume the
    // press. With the arm modelled, the line goes back to what it
    // is for: a press with nothing in front of you.
    if (actionFromMove && (stateAllowsAction || shootPress) && !session.dialogOpen() &&
        !actionSpent && !actionTookObject) {
        const int armed = session.zones().armedCount();
        const std::int16_t z = session.zones().armedZone();
        // omk-play 66: EVERY press is reported, with where he
        // stood, because the question is whether anything arms at
        // the anneaux (7288 -80 3015) at all. The rings are
        // OBJECTS 162 and no zone with an activate script covers
        // them, so a press there that arms NOTHING is the result
        // that confirms a second, object-proximity scan.
        const float* pp = player ? player->pos() : nullptr;
        // ...and WHAT IS IN THE HAND, because that is what the
        // pump's dry run and `var.set.used_object` both read, and
        // a press that finds it empty takes a different arm of the
        // zone's script entirely.
        const int hs = session.heldSlotOf(-1);
        // THE REPEAT GUARD, and it fires on the SUCCESSFUL press
        // only - which is the engine's rule, not a convenience:
        // `sub_465D30` reaches `SetPersoBankGroup` only when the
        // action actually did something, so a press that finds
        // nothing leaves the latch alone and the next frame tries
        // again. That is why holding the button at a door opens it
        // once, while holding it in the open street keeps looking.
        const bool did = session.pressAction(!shootPress);   // state 3: no talk, the zones only
        // THE REPEAT GUARD IS NOT PORTED, and the attempt is
        // recorded because it half-worked, which is the dangerous
        // kind. `sub_465D30` ends with
        //
        //     SetPersoBankGroup(channel,
        //         Cef_FindGroupById(bank, dy <= 27.472441 ? 143 : 41))
        //
        // and it is that STATE CHANGE - not the memset beside it -
        // that stops the `H_STAND -> 24` per-tick entry matching.
        // Doing only the switch takes the count from 20 holds to 1
        // - and PARKS the player in `.CTL` state 54 `H_WAITOB` for
        // ever, because the engine leaves that bank again when the
        // action completes and this port has no such path. A
        // second press then never works at all, which is worse
        // than the repeat. Measured both ways; see
        // `todo/next-tasks.md` 1.
        // The press is spent whether or not it found anything:
        // the engine's event is raised once on the way into the
        // action state, and a second one needs the button to go up
        // and come down again. A press that reaches nothing still
        // costs the press - which is also why holding the button
        // in the open street does not keep re-running a script.
        actionSpent = true;
        if (did)
            std::printf("action: zone %d activated (%d slot%s armed) at %.0f %.0f %.0f"
                        " - hand slot %d, object %d\n",
                        z, armed, armed == 1 ? "" : "s",
                        pp ? pp[0] : 0.0f, pp ? pp[1] : 0.0f, pp ? pp[2] : 0.0f,
                        hs, hs >= 0 ? session.objectSlotId(hs) : -1);
        else
            std::printf("action: pressed at %.0f %.0f %.0f - %d slots armed, "
                        "nothing interactable in reach\n",
                        pp ? pp[0] : 0.0f, pp ? pp[1] : 0.0f, pp ? pp[2] : 0.0f,
                        armed);
        (void)actionTold;
    }
    // `Walk_ProbeGround` -> `Game_HandleEvent` case 9: the decor
    // under his feet, over every shown slot. The active row - the
    // zone tables, the area's music - follows the FEET, not the
    // load (T11 finding 2).
    {
        const float* pp = player->pos();
        // through the merged soup's grid unless `OMK_NO_GROUND_GRID`
        static const bool noGroundGridHere = std::getenv("OMK_NO_GROUND_GRID") != nullptr;
        int under = noGroundGridHere
            ? omk::decorUnder(worldDecors, pp[0], pp[1], pp[2])
            : omk::decorUnder(worldDecors, playerSoup, playerGrid, pp[0], pp[1], pp[2]);
        // The probe finds the floor across EVERY loaded set - the
        // engine's collision array - and what happens next is
        // `Walk_ProbeGround`'s tail (0x00467030), read whole:
        //
        //   the floor's scene == the actor's own    -> nothing
        //   a DIFFERENT scene, its slot in state 2  -> relink the
        //       actor to it and `Game_RaiseEvent(9, slot)`
        //   a DIFFERENT scene, its slot in state 1  ->
        //       `o3de_MoveNodeBy(node, -(this frame's move))`: the
        //       STEP IS UNDONE
        //
        // So a hidden set is solid for walls and REFUSES to be
        // stood on. In the security centre that is the whole
        // design: each level's corridor is the SHAFT's set, the
        // offices behind their doors are `ACSLEV-N`, loaded and
        // hidden until a door zone's `area.goto 181, 22, 23`
        // brings them in - and until then their walls hold you in
        // the corridor and their floor turns you back.
        static float lastGood[3] = {0.0f, 0.0f, 0.0f};
        static bool  haveLastGood = false;
        static int   refusedTold = -1;
        bool drawn = false;
        if (under >= 0)
            for (int sl = 0; sl < 2; ++sl)
                if (worldSlots[static_cast<std::size_t>(sl)].area == under &&
                    worldSlots[static_cast<std::size_t>(sl)].shown) drawn = true;
        if (under >= 0 && !drawn && under != session.activeArea() && haveLastGood) {
            player->rideAt(lastGood, player->facing());
            if (refusedTold != under) {
                refusedTold = under;
                std::printf("frame %ld: the floor ahead is AREA %d's, a HIDDEN set - "
                            "the step is undone (`Walk_ProbeGround`'s state-1 arm)\n",
                            n, under);
            }
            under = -1;
        } else {
            if (under >= 0 && !drawn) under = -1;   // his own hidden set: no event
            for (int k = 0; k < 3; ++k) lastGood[k] = player->pos()[k];
            haveLastGood = true;
            if (under == session.activeArea()) refusedTold = -1;
        }
        static const bool verifyGround = std::getenv("OMK_VERIFY_GROUND") != nullptr;
        if (verifyGround && n % 30 == 0) {
            const auto& gv = omk::groundVerify();
            std::printf("ground verify: frame %ld, walker %ld probes %ld mismatched, "
                        "decor %ld probes %ld mismatched, sweep %ld sweeps %ld mismatched\n",
                        static_cast<long>(n), gv.walker, gv.walkerBad, gv.decor, gv.decorBad,
                        gv.sweep, gv.sweepBad);
        }
        if (under >= 0 && under != session.activeArea()) {
            session.playerOnArea(under);
            std::printf("frame %ld: event 9 - his feet are on AREA %d's decor; "
                        "the active row follows (zones re-registered: %zu)\n",
                        n, under, session.zones().registered().size());
        }
    }
}

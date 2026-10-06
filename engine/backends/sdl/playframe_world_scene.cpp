// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORLD'S SCENE: the camera, the texture pool, the props, the guns, the bolts.
// Parts of `PlayState::phaseWorld`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// The letterbox, the camera, the instrument override
void PlayState::worldCamera() {
    swimCamNow = false;
    OMK_ZONE("world: camera");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    view = dlgView;
    feedCameraHead();
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
    // THE DAY/NIGHT CYCLE (`sub_41E7A0`, `o3de/daynight.h`): the fog colour
    // and the clear colour are the active area's four colours lerped by the
    // game clock, every area - black only where the chunk says black. The
    // reading above that `+336` is always 0 missed this writer (`v5[84]`,
    // the slot's scene). `--fog-colour` still overrides.
    {
        const auto& rs = session.residentSlot(session.activeSlot());
        dayNight = omk::dayNightAt(state.clock(), rs.areaChunk);
        if (rs.area != dayNightToldArea || dayNight.phase != dayNightToldPhase) {
            dayNightToldArea = rs.area;
            dayNightToldPhase = dayNight.phase;
            std::printf("frame %ld: day/night (sub_41E7A0) - area %d, clock %d, phase %d: "
                        "fog and clear colour %d %d %d%s\n", n, rs.area,
                        static_cast<int>(state.clock()), dayNight.phase,
                        dayNight.rgb[0], dayNight.rgb[1], dayNight.rgb[2],
                        dayNight.floorByClock ? ", ambient floor by the clock" : "");
        }
    }
    // ...AND THE AMBIENT FLOOR, in the 19 areas whose chunk sets +178: the
    // scene's `+416` follows the clock (0..128 grey), so every lit body
    // starts from it this frame and the set is RE-FLOORED from its original
    // colours whenever the grey moves (`sub_4947F0` reads `+416` every frame;
    // the port bakes it, so a moving floor is re-baked - only the corners
    // that change go to the GPU).
    activeAmbientGrey = dayNight.floorByClock
        ? dayNight.floorGrey
        : worldSlots[static_cast<std::size_t>(session.activeSlot() & 1)].ambientGrey;
    if (!omk::envSet("OMK_NO_AMBIENT_CLAMP"))
        for (std::size_t k = 0; k < worldSlots.size(); ++k) {
            WorldSlot& w = worldSlots[k];
            if (w.bakedColour.empty() || w.bakedColour.size() != w.geo.corners.size()) continue;
            int grey = -1;
            for (int r = 0; r < 2; ++r)
                if (session.residentSlot(r).area == w.area) {
                    const omk::DayNight dn = omk::dayNightAt(state.clock(), session.residentSlot(r).areaChunk);
                    if (dn.floorByClock) grey = dn.floorGrey;
                }
            if (grey < 0 || grey == w.clampGrey) continue;
            std::vector<std::uint32_t> dirty;
            const std::size_t floored = omk::reclampToAmbient(w.geo, w.bakedColour, grey, dirty);
            if (w.clampGrey < 0 || std::abs(grey - w.clampGrey) >= 16 || dirty.size() > 0)
                std::printf("frame %ld: set %s re-floored at grey %d by the clock (sub_41E7A0): "
                            "%zu corners floored, %zu changed\n", n, w.stem.c_str(), grey,
                            floored, dirty.size());
            w.clampGrey = grey;
            if (!dirty.empty()) {
                w.geo.dirtyFrom = w.geo.revision;
                w.geo.revision = ++worldGeoRev;
                w.geo.dirtyTo = w.geo.revision;
                w.geo.dirtyCorners.swap(dirty);
            }
        }
    for (int k = 0; k < 3; ++k) {
        view.fogColour[k] = fogRGBSet ? fogRGB[k] : dayNight.rgb[k];
        view.clearColour[k] = view.fogColour[k];
    }
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
    } else if (!haveDlgCam && !ride && player && sliderCamMode >= 0) {
        // ---- THE SLIDER'S OWN CAMERAS -------------------------------
        //
        // The modes the slider's code requests (`sliderCamRequest`), each
        // preset resolved on its own subject every frame and blended into
        // from the camera on screen at the request:
        //
        //   8   the SLIDER from behind - case 2 while it comes, case 6
        //       while it drives him; preset 8, eye (0, 118.11, -275.59)
        //   9   the BOARDING side - MDACTION, over 60, and HELD through
        //       H_SLDIN, the seat and screen 7; preset 9, eye 4.00 m out
        //       on the door side (-X), 1.50 m up, on the slider
        //   10  the ARRIVAL - case 6's end; preset 10, eye 8.00 m to the
        //       side (the side `sub_4141F0` picks) and 4.00 m up on the
        //       slider, looking at the destination's ADDRESS
        //   17  the MANUAL STOP - preset 17 resolved on him ONCE, the eye
        //       then fixed in the world, the target following him with
        //       `f42` 5's lag; released to mode 0 over 60 the moment he is
        //       236.22 units (6.00 m) from that eye (`sub_4187B0`)
        //
        // Every slider-relative eye is resolved through the node's ROWS
        // (`out = subject - RotateVector(offset, node+140)`), row 2 being
        // -forward - which is what put the coming camera BEHIND the
        // slider (drift audit A2).
        float tEye[3] = {0, 0, 0}, tAt[3] = {0, 0, 0};
        bool ok = true;
        float at[3], rx0[3], rz2[3];
        const bool frame = session.sliders().calledFrame(at, rx0, rz2);
        const auto place = [&](const float off[3], float out[3]) {
            for (int k = 0; k < 3; ++k)
                out[k] = at[k] - off[0] * rx0[k] - off[2] * rz2[k];
            out[1] -= off[1];
        };
        static constexpr float kComeEye[3] = {0.0f, 118.1102f, -275.5905f};
        static constexpr float kComeAt[3]  = {0.0f, 78.7402f, 0.0f};
        static constexpr float kBoardEye[3] = {157.4803f, 59.0551f, 0.0f};
        static constexpr float kNoOff[3] = {0.0f, 0.0f, 0.0f};
        // THE LAG, `sub_415D10` / `sub_415E60` as the player's follow
        // camera transcribes them: the subject's yaw chased at dt/f46
        // (snapping inside 0.1 degrees), the preset resolved on THAT, then
        // the target chasing at dt/f42 and the eye at dt/f44 (1 reads as 2).
        // Rows 8 and 10 lag 0/8/8 and 0/8/16; row 9 is rigid. Without it the
        // eye 276 behind swung with every change of the slider's heading -
        // 112, 118, 194 units in ONE frame at a lane change, against 10 a
        // frame between: a reader's "the camera stutters sometimes when
        // following the slider".
        const auto lagged = [&](const float eyeOff[3], const float atOff[3], int f42, int f44, int f46) {
            const float fx = -rz2[0], fz = -rz2[2];
            const float yawNow = static_cast<float>(std::atan2(fx, fz) * 57.29577951308232);
            const float dtl = static_cast<float>(frameSec * 30.0);
            const auto wrap = [](float d) { while (d > 180.0f) d -= 360.0f; while (d < -180.0f) d += 360.0f; return d; };
            const int k46 = f46 == 1 ? 2 : f46, k44 = f44 == 1 ? 2 : f44, k42 = f42 == 1 ? 2 : f42;
            if (sliderCamFresh || !k46) sliderCamLagYaw = yawNow;
            else {
                const float d = wrap(yawNow - sliderCamLagYaw);
                if (std::fabs(d) <= 0.1f) sliderCamLagYaw = yawNow;
                else sliderCamLagYaw += d * std::min(1.0f, dtl / static_cast<float>(k46));
            }
            const float t = sliderCamLagYaw * 0.0174532925199433f;
            const float gx = std::sin(t), gz = std::cos(t);
            const float r0[3] = {-gz, 0.0f, gx}, r2[3] = {-gx, 0.0f, -gz};
            float e[3], a[3];
            for (int k = 0; k < 3; ++k) {
                e[k] = at[k] - eyeOff[0] * r0[k] - eyeOff[2] * r2[k];
                a[k] = at[k] - atOff[0] * r0[k] - atOff[2] * r2[k];
            }
            e[1] -= eyeOff[1]; a[1] -= atOff[1];
            for (int k = 0; k < 3; ++k) {
                if (sliderCamFresh || !k44) sliderCamLagEye[k] = e[k];
                else sliderCamLagEye[k] += (e[k] - sliderCamLagEye[k]) * std::min(1.0f, dtl / static_cast<float>(k44));
                if (sliderCamFresh || !k42) sliderCamLagAt[k] = a[k];
                else sliderCamLagAt[k] += (a[k] - sliderCamLagAt[k]) * std::min(1.0f, dtl / static_cast<float>(k42));
                tEye[k] = sliderCamLagEye[k];
                tAt[k] = sliderCamLagAt[k];
            }
            sliderCamFresh = false;
        };
        if (sliderCamMode == 8 && frame) {
            lagged(kComeEye, kComeAt, 0, 8, 8);
        } else if (sliderCamMode == 9 && frame) {
            place(kBoardEye, tEye);
            place(kNoOff, tAt);
        } else if (sliderCamMode == 10 && frame) {
            const float e10[3] = {sliderCamEyeX10, 157.4803f, 0.0f};
            lagged(e10, kNoOff, 0, 8, 16);
            for (int k = 0; k < 3; ++k) tAt[k] = sliderCamAddr[k];   // subject 9, the address
        } else if (sliderCamMode == 17) {
            static constexpr float kEye17[3] = {-39.3701f, 78.7402f, 0.0f};
            static constexpr float kAt17[3]  = {0.0f, 0.0f, 0.0f};
            const omk::FollowCamera c = player->resolveOffsets(kEye17, kAt17, 75.0f);
            // `f42` = 5: the target closes a fifth of the gap a frame (by
            // the delta) - the follow camera's lag form, a LABELLED reading
            const float lag = std::min(1.0f, static_cast<float>(frameSec * 30.0) / 5.0f);
            for (int k = 0; k < 3; ++k) {
                sliderCamAt17[k] += (c.at[k] - sliderCamAt17[k]) * lag;
                tEye[k] = sliderCamEye17[k];
                tAt[k] = sliderCamAt17[k];
            }
        } else {
            ok = false;                    // the slider is gone: the follow camera
        }
        if (ok) {
            float u = 1.0f;
            if (sliderCamDur > 0.0f && haveLastDrawn) {
                sliderCamClock += static_cast<float>(frameSec * 30.0);
                u = std::min(1.0f, sliderCamClock / sliderCamDur);
            }
            for (int k = 0; k < 3; ++k) {
                view.cam.eye[k] = sliderCamFromEye[k] + (tEye[k] - sliderCamFromEye[k]) * u;
                view.cam.at[k]  = sliderCamFromAt[k]  + (tAt[k]  - sliderCamFromAt[k])  * u;
            }
            view.cam.hfovDeg = sliderCamFromFov + (75.0f - sliderCamFromFov) * u;
            view.cam.rollDeg = 0.0f;
            view.cam.w = dispW; view.cam.h = dispH;
            if (sliderCamMode == 8 && u >= 1.0f) {
            // ...and SAY where the eye ended up, from the camera this
                // frame draws with and the vehicle's own heading (its mover
                // direction, `-row 2`): behind > 0 is behind it
                static long comeTold = -1000;
                if (n - comeTold >= 30) {
                    comeTold = n;
                    const float ex = view.cam.eye[0] - at[0], ez = view.cam.eye[2] - at[2];
                    const float behind = -(ex * -rz2[0] + ez * -rz2[2]);
                    const float across = ex * rx0[0] + ez * rx0[2];
                    std::printf("slider: come camera frame %ld - the eye %.0f behind, "
                                "%.0f across, heading %.2f %.2f\n", n, behind, across,
                                -rz2[0], -rz2[2]);
                }
            }
            // ...one line per mode change and every 30 frames, from the
            // camera this frame draws with: where the eye stands in the
            // SLIDER's frame (side = along its local X, behind = along
            // -forward, up) - or for 17, how far the target is from the
            // fixed eye
            static long camTold = -1000; static int camToldMode = -2;
            if (n - camTold >= 30 || camToldMode != sliderCamMode) {
                camTold = n; camToldMode = sliderCamMode;
                if (frame) {
                    const float ex = view.cam.eye[0] - at[0], ey = view.cam.eye[1] - at[1],
                                ez = view.cam.eye[2] - at[2];
                    std::printf("slider cam %d frame %ld blend %.2f - eye side %.0f behind %.0f "
                                "up %.0f; at %.0f %.0f %.0f\n", sliderCamMode, n, double(u),
                                double(ex * rx0[0] + ez * rx0[2]), double(ex * rz2[0] + ez * rz2[2]),
                                double(-ey), double(view.cam.at[0]), double(view.cam.at[1]),
                                double(view.cam.at[2]));
                } else {
                    const float dx = sliderCamAt17[0] - sliderCamEye17[0], dy = sliderCamAt17[1] - sliderCamEye17[1],
                                dz = sliderCamAt17[2] - sliderCamEye17[2];
                    std::printf("slider cam %d frame %ld blend %.2f - eye fixed at %.0f %.0f %.0f, "
                                "target %.0f from it\n", sliderCamMode, n, double(u),
                                double(sliderCamEye17[0]), double(sliderCamEye17[1]), double(sliderCamEye17[2]),
                                double(std::sqrt(dx * dx + dy * dy + dz * dz)));
                }
            }
            // mode 17's release, `sub_4187B0`: the target 236.22 from the eye
            if (sliderCamMode == 17) {
                const float dx = sliderCamAt17[0] - sliderCamEye17[0], dy = sliderCamAt17[1] - sliderCamEye17[1],
                            dz = sliderCamAt17[2] - sliderCamEye17[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) > 236.22047f) sliderCamRequest(0, 60.0f);
            }
        } else {
            sliderCamMode = -1;
            const omk::FollowCamera& fc = player->followCamera();
            for (int k = 0; k < 3; ++k) { view.cam.eye[k] = fc.eye[k]; view.cam.at[k] = fc.at[k]; }
            view.cam.hfovDeg = fc.fov;
            view.cam.rollDeg = 0.0f;
            view.cam.w = dispW; view.cam.h = dispH;
        }
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
        // ...through the node's ROWS, as the coming camera is (drift
        // audit A2): the ride moves along `f = (-sin yaw, 0, -cos yaw)`
        // (`x -= sin(yaw) * v`), and a node facing `f` has row 0 =
        // `(-f.z, 0, f.x)` and row 2 = `-f` - so the eye is 7.00 m
        // behind the way it flies. Rotating the offset by the yaw put it
        // ahead or beside on every heading but one.
        const float t = static_cast<float>(ride->yaw) * 0.0174532925199433f;
        const float fx = -std::sin(t), fz = -std::cos(t);
        const float r0[3] = {-fz, 0.0f, fx}, r2[3] = {-fx, 0.0f, -fz};
        const float sub[3] = {static_cast<float>(ride->x),
                              static_cast<float>(ride->y),
                              static_cast<float>(ride->z)};
        const auto place = [&](const float off[3], float out[3]) {
            for (int k = 0; k < 3; ++k)
                out[k] = sub[k] - off[0] * r0[k] - off[2] * r2[k];
            out[1] -= off[1];
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
        swimCamNow = swimCam;
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
        (void)subj;
        // EACH POINT BY ITS SUBJECT KIND, through the Session's resolver -
        // the one the travel solves both ends with (`Session::solveCamera`,
        // `sub_415A10`'s switch): 0 the pelvis above, 9 the request's
        // ADDRESS (todo/drift-audit.md S14: `sub_415850` reads the in-memory
        // integers and the integer heading, no pelvis lift; only
        // `camera.set.at_address`, op 126, hands one over, and the four
        // kind-9 cameras, 4781..4784 on the rooftops' ladders, are the only
        // ones it names), 1 and 3 the player's HEAD (`feedCameraHead`).
        const omk::ResolvedCamera rc = session.solveCamera(*wc);
        if (wc->eyeSubject == 9 || wc->atSubject == 9) {
            if (const omk::Address* ad = session.findAddress(session.cameraSubjectAddress())) {
                static int addrTold = -1000;
                if (addrTold != wc->id * 10000 + ad->id) {
                    addrTold = wc->id * 10000 + ad->id;
                    std::printf("frame %ld: camera %d framed on ADDRESS %d (subject kind 9, "
                                "sub_415850) at %d %d %d heading %d - eye %.0f %.0f %.0f\n",
                                n, wc->id, ad->id, ad->memPos[0], ad->memPos[1],
                                ad->memPos[2], ad->memYaw, double(rc.eye[0]),
                                double(rc.eye[1]), double(rc.eye[2]));
                }
            }
        }
        if (wc->eyeSubject == 1 || wc->atSubject == 1 ||
            wc->eyeSubject == 3 || wc->atSubject == 3) {
            static int headTold = -1;
            if (headTold != wc->id) {
                headTold = wc->id;
                const int kind = (wc->eyeSubject == 1 || wc->eyeSubject == 3) ? wc->eyeSubject
                                                                              : wc->atSubject;
                float an[3], yaw;
                session.cameraAnchor(kind, an, yaw);
                std::printf("frame %ld: camera %d framed on the player's HEAD (subject kinds %d/%d, "
                            "%s) at %.1f %.1f %.1f, %.1f above the kind-0 point%s - eye %.1f %.1f %.1f "
                            "at %.1f %.1f %.1f\n",
                            n, wc->id, wc->eyeSubject, wc->atSubject,
                            kind == 3 ? "sub_415320, the rest offset" : "sub_415050, the Tete node",
                            double(an[0]), double(an[1]), double(an[2]), double(subj[1] - an[1]),
                            cameraHeadFrom, double(rc.eye[0]), double(rc.eye[1]), double(rc.eye[2]),
                            double(rc.at[0]), double(rc.at[1]), double(rc.at[2]));
            }
        }
        for (int k = 0; k < 3; ++k) {
            view.cam.eye[k] = rc.eye[k];
            view.cam.at[k]  = rc.at[k];
        }
        view.cam.hfovDeg = wc->fov > 1.0f ? wc->fov : 75.0f;
        view.cam.rollDeg = wc->roll;   // already wrapped to (-180,180]
        view.cam.w = dispW; view.cam.h = dispH;
    }
    // THE CAMERA SHAKE (`sub_418030`, `Session::cameraShakeStep`): the
    // camera tick runs it in every mode but 13 - the camera editings,
    // which this file draws in the `haveEdit` / `holdEditCam` arms - after
    // the mode has placed the eye and the aim and before anything else
    // reads them, and it moves both Ys by the same `dy`, so the view
    // TRANSLATES. A frame in mode 13 does not advance its clock.
    // (Whether a CONVERSATION's camera is mode 13 is not read; nothing
    // shipped shakes during one - LABELLED.)
    if (!(!haveDlgCam && (haveEdit || holdEditCam)) && session.cameraShaking()) {
        const float eyeWas = view.cam.eye[1], atWas = view.cam.at[1];
        const float dy = session.cameraShakeStep(static_cast<float>(frameSec * 30.0));
        view.cam.eye[1] += dy;
        view.cam.at[1] += dy;
        // told from what the VIEW now holds, not from `dy`
        static long shakeTold = -1;
        if (shakeTold != session.cameraShakes()) {
            shakeTold = session.cameraShakes();
            std::printf("frame %ld: CAMERA SHAKE (sub_418030) - shake %ld, first dy %.3f "
                        "(aim %.3f)\n", n, session.cameraShakes(),
                        double(view.cam.eye[1] - eyeWas), double(view.cam.at[1] - atWas));
        }
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
    // ---- UNDERWATER (`dword_93082C`; todo/drift-audit.md L1 step 5)
    //
    // `sub_4187B0`, the camera tick: a camera carrying flag 0x800 - the swim
    // variant `sub_413CD0` sets up in states 11/13/14 (flags 0x4800) - whose
    // EYE y has gone past the water line (`C+24 > flt_4E7D0C`, Y down: under
    // the surface) turns the mode ON, and sets mesh flag 0x8000000 - the
    // SHIMMER - on the player's whole hierarchy (`sub_437220(node, 0x100, 8)`);
    // back above the line (`<`), or any camera without 0x800, turns it OFF
    // and clears the flag (`sub_4372D0`). While it is on, `sub_41E7A0` gives
    // every shown scene the fog colour `0x405028` - (40, 80, 64) - over a
    // 590.551-inch (15 m) range, and the screen is cleared to it; and
    // `sub_417CF0` runs `sub_417FC0` on a camera with flag 0x4000, which
    // SWAYS it: fov `70 + 20 cos(3v) sin(v)`, roll `5 sin(2v) cos(v)`, with
    // `v = clock * 3.14 * 14` on `dword_4E9750`, which `Game_Tick` advances
    // by `dt x 0.0004` and wraps at 1.
    {
        swayClock += static_cast<float>(frameSec * 30.0) * 0.0004f;
        if (swayClock > 1.0f) swayClock -= 1.0f;
        if (underwater && swimCamNow) {
            // the sway first, as the tick runs it before the 0x800 test
            const double v = static_cast<double>(swayClock) * 3.1400001 * 14.0;
            view.cam.rollDeg = static_cast<float>(std::sin(v + v) * std::cos(v) * 5.0);
            view.cam.hfovDeg = static_cast<float>(70.0 - std::cos(v * 3.0) * std::sin(v) * -20.0);
        }
        const bool was = underwater;
        if (!swimCamNow) underwater = false;
        else if (waterLineKnown) {
            if (!underwater && view.cam.eye[1] > waterLine) underwater = true;
            else if (underwater && view.cam.eye[1] < waterLine) underwater = false;
        }
        if (underwater != was) {
            // the shimmer on (phase from the vertex, as `buildGeometry` gives
            // a flagged set mesh) or off, on the rest every pose copies from
            for (std::size_t i = 0; i < playerRest.corners.size(); ++i)
                playerRest.corners[i].phase =
                    underwater && i < playerRest.cornerVertex.size() && playerRest.cornerVertex[i] >= 0
                        ? static_cast<float>((2u * static_cast<std::uint32_t>(playerRest.cornerVertex[i])) % 32u)
                        : -1.0f;
            playerRest.revision = ++worldGeoRev;
            std::printf("frame %ld: UNDERWATER %s (dword_93082C) - the eye at y %.1f, the water "
                        "line %.1f\n", n, underwater ? "ON" : "OFF", double(view.cam.eye[1]),
                        double(waterLine));
        }
        if (underwater) {
            constexpr double kUnderwaterFog = 590.5512085;   // flt_4C2C34 = 0x4413A347
            view.fogStart = static_cast<float>(kUnderwaterFog * 0.25);
            view.fogEnd = static_cast<float>(kUnderwaterFog);
            view.fogColour[0] = 40; view.fogColour[1] = 80; view.fogColour[2] = 64;   // 0x405028
            for (int k = 0; k < 3; ++k) view.clearColour[k] = view.fogColour[k];
            // said on the frame the mode comes on, from the view handed on
            if (!was)
                std::printf("frame %ld: underwater view - fog %d %d %d over %.1f..%.1f, clear "
                            "%d %d %d, fov %.1f roll %.2f\n", n, view.fogColour[0],
                            view.fogColour[1], view.fogColour[2], double(view.fogStart),
                            double(view.fogEnd), view.clearColour[0], view.clearColour[1],
                            view.clearColour[2], double(view.cam.hfovDeg), double(view.cam.rollDeg));
        }
    }
    lastFov = view.cam.hfovDeg;
    lastRoll = view.cam.rollDeg;
    haveLastDrawn = true;
}

// The texture pool
void PlayState::worldTexturePool() {
    OMK_ZONE("world: texture pool");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
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
    // ...and NOT WHILE HE SITS IN A SLIDER. `MDSLIDIN` ends with
    // `push 8; push [esi+8]; call sub_436E70` - `o3de_DisableObject(his
    // node, 8)`, the hidden bit on his whole tree - and `sub_457040`
    // (Manuelle) does the same; `sub_468FA0`, the exit's start, is what
    // `o3de_EnableObject`s him again. The door is shut over him, so the
    // seat was never something the original drew (drift audit A1, the
    // reader: "once he's seated, the door closes so we can't see what's
    // inside the slider").
    const bool seatedHidden = boarded && !leaving;
    drawPlayer = playerReady && player && !seatedHidden &&
                            !(session.shootMode().active() && shootCameraLive) &&
                            (adventure || uiPause ||
                             (session.dialogOpen() && !playerProgram));
    {
        static bool wasSeatedHidden = false;
        if (seatedHidden != wasSeatedHidden && player) {
            wasSeatedHidden = seatedHidden;
            std::printf("frame %ld: player %s\n", n, seatedHidden
                        ? "HIDDEN in the slider - MDSLIDIN's o3de_DisableObject(node, 8); drawn: no"
                        : (drawPlayer ? "SHOWN again - sub_468FA0's o3de_EnableObject(node, 8); drawn: yes"
                                      : "SHOWN again - sub_468FA0's o3de_EnableObject(node, 8); drawn: no"));
        }
    }
    // ...AND THE EXEMPTION, ported 2026-09-10 (`todo/shoot-mode.md`
    // 8.0). `Shoot_Enter` hides the player's tree with `sub_436CE0`,
    // which sets the hidden bit on every node WITHOUT 0x200000 - and in
    // HO1_FN exactly three carry it: `UAvantg`, `UBrasg`, `UMaing`, the
    // LEFT forearm, upper arm and hand, the arm the gun hangs on. So in
    // first person the engine draws that arm and nothing else of him;
    // `Shoot_Leave` (`sub_436D20`) clears the bit. A reader's frames of
    // the original show it: the arm and the gun, low at the right.
    drawArm = playerReady && player && !drawPlayer &&
                         session.shootMode().active() && shootCameraLive;
    mark("world begin, set");
}

// The world's props
void PlayState::worldProps() {
    OMK_ZONE("world: props");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
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
    // THE SET'S LIGHTS ON A PROP (todo/drift-audit.md L1 step 3).
    // `Object_Load` calls `LightObject` on the object's node as
    // `Actor_LoadModel` does on a character's, so `sub_440CA0` lights it
    // the same way: from the scene's `+416` - the set's ambient grey - plus
    // every set light reaching its root, per vertex on its turned normal.
    // A held prop rides the actor's hierarchy and is lit with it, from
    // where it is drawn. Same per-body deviation as the crowd's.
    const bool propLit = lightActors && lighting == 0;
    const float propBase = static_cast<float>(std::clamp(
        activeAmbientGrey, 0, 255)) / 255.0f;
    const auto lightProp = [&](std::size_t first, const float at[3]) {
        if (!propLit) return;
        const std::size_t cnt = propGeo.corners.size() - first;
        for (std::size_t c = first; c < propGeo.corners.size(); ++c) {
            omk::Corner& w = propGeo.corners[c];
            w.r = w.g = w.b = propBase;
        }
        int reached = 0;
        for (const WorldSlot& ws2 : worldSlots)
            if (!ws2.lights.empty()) reached += omk::applyLights(propGeo, first, cnt, at, ws2.lights);
        // said once a prop position, from the corners the draw takes
        static std::set<long> litTold;
        const long key = static_cast<long>(at[0]) * 1000003L + static_cast<long>(at[2]);
        if (cnt && litTold.insert(key).second) {
            double m[3] = {0, 0, 0};
            for (std::size_t c = first; c < propGeo.corners.size(); ++c) {
                m[0] += propGeo.corners[c].r; m[1] += propGeo.corners[c].g; m[2] += propGeo.corners[c].b;
            }
            const double k = 255.0 / static_cast<double>(cnt);
            std::printf("frame %ld: prop at %.0f %.0f %.0f LIT by the set (sub_440CA0) - base %.0f, "
                        "%d lights reach, corners' mean %.0f %.0f %.0f\n", n, double(at[0]),
                        double(at[1]), double(at[2]), double(propBase) * 255.0, reached,
                        m[0] * k, m[1] * k, m[2] * k);
        }
    };
    // OP 98, `object.place_at character, prop` (0x404DB0): the prop moved
    // under the character - every shipped site is the ring a beaten character
    // drops. The handler, read 2026-10-06:
    //
    //     Actor_GetPosAndFacing(actor, p)        his position (actor +0xF4)
    //     p.y += actor[+0x114]                   `+276`, origin to lowest point
    //     p.y -= box.max.y                       the prop root mesh's `+108`
    //     Object_SetPlacement(prop, {p, rot})    the node moved there
    //
    // so the prop's lowest point is seated where his feet would be, under his
    // STANDING origin whatever pose he lies in. The three ROTATION words of
    // that block are never written by the handler: they are whatever its
    // stack frame held - in all six sites `object.show`'s three saved
    // registers if nothing ran between, which read as floats are denormals.
    // **0 here is a RECONSTRUCTION, labelled.** The position is the Session's
    // to hold (`setPropPlacement`), where drawing and the take scan both read
    // it; the character's position is the frontend's to know.
    {
        const auto& ev = session.propEvents();
        for (; propEventsSeen < ev.size(); ++propEventsSeen) {
            const auto& e = ev[propEventsSeen];
            if (std::strcmp(e.what, "place") != 0) continue;
            const Staged* body = nullptr;
            for (const auto* list : {&staged, &parked})
                for (const auto& s : *list)
                    if (s && s->actor == e.actor && s->mo) { body = s.get(); break; }
            int propId = -1;
            for (const auto& pr : session.props())
                if (pr.stateIndex == e.slot) { propId = pr.id; break; }
            const auto& objs = voiceLib.objects();
            const PropModel* pm = (propId >= 0 && static_cast<std::size_t>(propId) < objs.size())
                ? propModelFor(objs[static_cast<std::size_t>(propId)].stem) : nullptr;
            if (!body || !pm || !pm->ready) {
                std::printf("frame %ld: object.place_at - prop state %d under CHARACTERS %d: "
                            "%s - left at its chunk placement\n", n, e.slot, e.actor,
                            !body ? "no body of his is staged" : "no model for the prop");
                continue;
            }
            const float origin[3] = {body->at[0],
                                     body->at[1] - (body->fightPlaced ? body->fightDrop : 0.0f),
                                     body->at[2]};
            const float pos[3] = {origin[0], origin[1] + body->mo->extentBelow - pm->rootBoxMaxY,
                                  origin[2]};
            const float rot[3] = {0.0f, 0.0f, 0.0f};
            const bool ok = session.setPropPlacement(e.slot, pos, rot);
            std::printf("frame %ld: object.place_at - prop %d (state %d) PLACED under CHARACTERS "
                        "%d at %.1f %.1f %.1f: his origin %.1f %.1f %.1f, +276 %.2f, the prop's "
                        "box bottom %.2f (Object_SetPlacement)%s\n", n, propId, e.slot, e.actor,
                        double(pos[0]), double(pos[1]), double(pos[2]), double(origin[0]),
                        double(origin[1]), double(origin[2]), double(body->mo->extentBelow),
                        double(pm->rootBoxMaxY), ok ? "" : " - REFUSED");
        }
    }
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
                    // ...and its normal on the same turn, for the light
                    const float nl[3] = {c.nx, c.ny, c.nz};
                    float nr[3], no[3];
                    omk::qrot(hp.q, nl, nr);
                    omk::rotateYaw(yaw, nr, no);
                    w.nx = no[0]; w.ny = no[1]; w.nz = no[2];
                    propGeo.corners.push_back(w);
                }
                {
                    float hin2[3] = {hp.pos[0] - playerRootXZ[0], hp.pos[1], hp.pos[2] - playerRootXZ[1]}, ho2[3];
                    omk::rotateYaw(yaw, hin2, ho2);
                    const float at[3] = {ho2[0] + pp[0], ho2[1] + pp[1] - playerFeet + lastRootDrop,
                                         ho2[2] + pp[2]};
                    lightProp(base, at);
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
                // its normal turned by the same matrix (no translation)
                w.nx = static_cast<float>(c.nx * m00 + c.ny * m10 + c.nz * m20);
                w.ny = static_cast<float>(c.nx * m01 + c.ny * m11 + c.nz * m21);
                w.nz = static_cast<float>(c.nx * m02 + c.ny * m12 + c.nz * m22);
                propGeo.corners.push_back(w);
            }
            {
                const float at[3] = {pr.pos[0], pr.pos[1], pr.pos[2]};
                lightProp(base, at);
            }
            for (const auto& b : pm->rest.batches) {
                omk::Batch nb = b;
                nb.start += static_cast<int>(base);
                propGeo.batches.push_back(nb);
                propBatchOwner.push_back(pm);
            }
            // ...and where its corners went, from the corners THEMSELVES - a
            // prop op 98 moves says so once it is drawn there
            {
                double sum[3] = {0, 0, 0};
                const std::size_t cn = propGeo.corners.size() - base;
                for (std::size_t i = base; i < propGeo.corners.size(); ++i) {
                    sum[0] += propGeo.corners[i].x; sum[1] += propGeo.corners[i].y;
                    sum[2] += propGeo.corners[i].z;
                }
                if (cn > 0) {
                    const std::array<float, 3> c = {float(sum[0] / cn), float(sum[1] / cn),
                                                    float(sum[2] / cn)};
                    auto [it, fresh] = propsDrawnAt.try_emplace(pr.id, c);
                    if (!fresh && std::hypot(c[0] - it->second[0], c[1] - it->second[1],
                                             c[2] - it->second[2]) > 1.0f) {
                        std::printf("frame %ld: prop %d DRAWN at %.1f %.1f %.1f (its corners' "
                                    "mean; was %.1f %.1f %.1f)\n", n, pr.id, double(c[0]),
                                    double(c[1]), double(c[2]), double(it->second[0]),
                                    double(it->second[1]), double(it->second[2]));
                        it->second = c;
                    }
                }
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
}

// The gun in his hand, each gunman's gun
void PlayState::worldGuns() {
    OMK_ZONE("world: guns");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    // ---- THE GUN IN HIS HAND (`todo/shoot-mode.md` 8.0) -----------
    //
    // `Shoot_Enter`'s event 48 hands the weapon object to `sub_41C490`,
    // which links its node under actor +44 - `Maing` - with the local
    // transform cleared, exactly as a take does; `Object_Load` has
    // already unlinked `tir` (the bolt) and set 0x200000 on the node,
    // so the first-person hide spares it. Drawn here the way the held
    // prop is ("THE OBJECT IN HIS HAND"), without `tir`'s corners.
    // ...not while he is SUSPENDED: `shoot.player.suspend` drops the object
    // in his hand and `.resume`'s event 48 loads it back (todo/drift-audit S7)
    if (session.shootMode().active() && !session.shootMode().playerOff() && player &&
        !shotGunStem.empty()) {
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
            if (!session.shootIn(up->actor)) continue;
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
}

// The bolts
void PlayState::worldBolts() {
    OMK_ZONE("world: bolts");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    omk::Renderer& world = *world_;
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
        poolHasPlayer != (drawPlayer || drawArm) || spritePooled != spriteWanted ||
        poolGrey != session.renderGrey()) {
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
                if (!st || !st->hasPixels()) continue;   // an id nothing decoded
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
        // THE GREYSCALE BANK's textures (`o3de/greybank.h`): while it is on,
        // every slot goes over with its palette greyed - `sub_42FE80` greys
        // every resident texture when the bank comes on, and the uploads it
        // installs grey every new one - and the colour pool goes back when
        // it goes off, which is `sub_42FC10`'s re-upload from the source.
        // The indices are shared; a grey palette is made once per source.
        if (poolGrey != session.renderGrey())
            std::printf("frame %ld: texture pool handed over %s (%zu slots)\n", n,
                        session.renderGrey() ? "GREYED (sub_42FE80)" : "in colour (sub_42FC10)",
                        pool.size());
        poolGrey = session.renderGrey();
        if (poolGrey)
            for (auto& t : pool) {
                if (t.pal.size() != 768) continue;
                auto it = greyPalettes.find(t.pal.data());
                if (it == greyPalettes.end())
                    it = greyPalettes.emplace(t.pal.data(),
                                              std::make_pair(t.pal, omk::greyPalette(t.pal))).first;
                t.pal = it->second.second;
            }
        else
            greyPalettes.clear();
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
}

// THE PLAYER'S HEAD for camera subject kinds 1 and 3, handed to the Session
// each frame so the travel and the standing shot resolve it alike
// (todo/drift-audit.md, the follow-up to S14). Kind 1 (`sub_415050`) is the
// `Tete` node's POSED world position, turned by the actor's Euler plus his
// head look (+432..+440 - zero for the player, whom no look target turns;
// LABELLED); kind 3 (`sub_415320`) is `*(Tete)+36`, which `sub_4942A0`
// accumulates as the root's world point plus the parent chain's LOCALS with
// no rotation (`omk::headRestOffset`), turned by (0, the body node's heading,
// 0). The head is the one the look-at code aims at: a program's staged body
// when a program owns the player, else the walker's - one frame old, since
// the camera runs before the bodies are posed. Until the player has been
// drawn once nothing is handed over and both kinds take kind 0's point.
void PlayState::feedCameraHead() {
    auto& session = *session_;
    const Staged* asPlayer = nullptr;
    if (const int pid = session.playerActor(); pid >= 0)
        for (const auto& up : staged)
            if (up->actor == pid && up->headKnown && up->mo) { asPlayer = up.get(); break; }
    if (!asPlayer && !playerHeadKnown) { cameraHeadFrom = " (not drawn yet: kind 0's point)"; return; }
    const float lift = player ? player->cameraLift() : 0.0f;
    const float* pp = session.playerPos();
    // OFFSETS from where he stands (the Session adds its own `playerPos_`):
    // the posed head is a frame old, and an absolute point would leave a
    // teleport's first frame framing the spot he left
    float head[3], base[3];
    float yaw = session.playerYaw();
    for (int k = 0; k < 3; ++k) {
        head[k] = asPlayer ? asPlayer->headAt[k] - pp[k] : playerHeadRel[k];
        base[k] = asPlayer ? (asPlayer->progRan ? asPlayer->drawAt[k] : asPlayer->at[k]) - pp[k]
                           : (k == 1 ? -lift : 0.0f);
    }
    if (asPlayer) yaw = asPlayer->drawnYawKnown ? asPlayer->drawnYaw : asPlayer->facing;
    float rest[3] = {0, 0, 0};
    omk::headRestOffset(asPlayer ? asPlayer->mo->meshes : playerMeshes, rest);
    const float h3[3] = {base[0] + rest[0], base[1] + rest[1], base[2] + rest[2]};
    session.setCameraHeadAnchors(head, yaw, h3, yaw);
    cameraHeadFrom = asPlayer ? " (a program's body)" : "";
}

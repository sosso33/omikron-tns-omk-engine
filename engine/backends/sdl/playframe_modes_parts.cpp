// SPDX-License-Identifier: GPL-3.0-or-later
// THE MODES: the pause's sound, dialogue mode, shoot mode, the quit and the pending load.
// Parts of `PlayState::phaseModes`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// The pause screen stops the sound; the voices
void PlayState::modesSound() {
    OMK_ZONE("modes: sound");   // the profiler (todo/debug-tools.md 6)
    const auto& fs = *fs_;
    auto& session = *session_;

    // The script asked for a track. A LEVEL, not an event: the engine's
    // own handler skips `music.play` when the track is already going, so
    // this only acts on a change.
    // A LEVEL, not an event: the engine's own handler skips `music.play`
    // when the track is already going, so this acts only on a change.
    {
        // the engine's music level, every frame (the ramp, the dialogue
        // duck, the fade-in) - logged when it moves by a dB or more
        static double musicDbTold = -1.0;
        const double db = session.musicAttenuationDb();
        front.setMusicGain(session.musicGain());
        if (std::fabs(db - musicDbTold) >= 1.0) {
            musicDbTold = db;
            std::printf("audio: music attenuation %.0f dB (gain %.2f)%s\n", db,
                        static_cast<double>(session.musicGain()),
                        session.dialogOpen() ? " - a conversation ducks it 10" : "");
        }
    }
    if (session.musicTrack() != playingTrack) {
        playingTrack = session.musicTrack();
        std::printf("audio: music switch to %d - the stream is flushed; "
                    "one-shots already playing are NOT\n", playingTrack);
        front.flushAudio();
        if (music.play(fs, adpcmTables, playingTrack, session.musicLoops()))
            std::printf("music: track %d, %.1f s%s\n", music.track(),
                        music.seconds(), music.looping() ? ", looping" : "");
    }
    mark("controller");
    // the `audio` section's own parts (2026-09-24: 14 ms a city frame on a
    // console, and nothing in it looked like 14 ms)
    const double auA = phaseNow();
    // ---- AND THE PAUSE SCREEN STOPS THE SOUND ------------------------
    //
    // Read out of screen 31's own open and close callbacks (0x004ADDB0 /
    // 0x004ADEB0), which do far more than set the pause flag: after
    // `mov dword_4E9728, 1` the open calls FOUR suspend routines, and the
    // close calls their four partners in the same order.
    //
    //     sub_46C290 / sub_46C2C0   walk the sound bank at unk_53B36C and
    //                               stop / restart every buffer in it
    //     sub_42BA70 / sub_42BA90   one streaming handle (word_4EB5F8):
    //                               `sub_46CAE0(h)` to stop, and
    //                               `sub_46CBB0(h, dword_4EB614)` to
    //                               restart FROM THE SAVED POSITION
    //     sub_42BB10 / sub_42BB30   the same for word_4EA7B8, saved in
    //                               dword_4EB610
    //     sub_412120 / sub_412140   `timeGetTime` re-baselined, so the
    //                               paused interval is never integrated
    //
    // Both stream pairs guard on `handle != 0xFFFF` and both restart from
    // a position stored on the way in - so it is a SUSPEND, not a stop:
    // the music resumes where it was rather than from the top.
    //
    // Here that is two things, because the port pulls a second of music
    // ahead into the device: stop pulling, and FLUSH what is already
    // queued - otherwise the pause is silent-eventually rather than
    // silent. `MusicPlayer::pos_` is untouched while nothing pulls, so it
    // is already the engine's saved position and the resume needs nothing.
    if (uiPause != musicPaused) {
        musicPaused = uiPause;
        if (musicPaused) front.flushAudio();
        std::printf("audio: the pause screen %s the sound (screen 31's own "
                    "open/close callbacks suspend the bank and both streams)\n",
                    musicPaused ? "SUSPENDS" : "resumes");
    }
    // Keep about a second of it in the device. The player decides what
    // comes next - including whether the track wraps - so all this does
    // is ask for more and hand it over.
    // IN QUARTER SECONDS (2026-09-27): a whole second decoded at once cost a
    // console 25-34 ms on the frame it happened - the city's "audio"
    // section, one frame in seven. The same work in smaller pulls, a whole
    // refill only when the queue is close to running dry.
    const double musicQueued = !musicPaused && music.playing() ? front.queuedSeconds() : 9.0;
    if (musicQueued < 1.0) {
        const double want = musicQueued < 0.3 ? 1.0 - musicQueued : std::min(0.25, 1.0 - musicQueued);
        const std::size_t frames44 = static_cast<std::size_t>(want * kDeviceRate) + 1;
        std::vector<float> chunk;
        spanned("music", [&] { music.pull(chunk, frames44); });
        {
            static double musicPeakTold = 0.0; static float musicPeak = 0.0f; static std::size_t musicSamples = 0;
            for (const float v : chunk) musicPeak = std::max(musicPeak, std::fabs(v));
            musicSamples += chunk.size();
            if (musicSamples >= static_cast<std::size_t>(kDeviceRate) * 2u * 10u) {   // every ten seconds
                std::printf("audio: music peak %.3f over the last %.0f s\n",
                            static_cast<double>(musicPeak), musicSamples / (2.0 * kDeviceRate));
                musicPeak = 0.0f; musicSamples = 0; (void)musicPeakTold;
            }
        }
        front.queueAudio(chunk);
    }
    // `media.play` is an EVENT, not a level like music: each announcement
    // is one play, and the handler's `Morph_Stop()` means a second one
    // CUTS the first. That is the whole of the engine's policy here.
    phSpan["audio: music top-up"] += phaseNow() - auA;
    const double auB = phaseNow();
    for (const int mediaId : voices.poll(session.announced())) {
        const auto vo  = voiceLib.resolve(fs, mediaId);
        const auto pcm = voiceLib.decode(fs, adpcmTables, vo);
        std::printf("media.play %d (%s) -> %s%s, %.2f s\n", mediaId,
                    vo.objectName.c_str(),
                    vo.file.empty() ? (vo.image ? "IMAGES\\" : "nothing")
                                    : vo.file.c_str(),
                    vo.substituted ? " [JINGOFF3 substitution]" : "",
                    static_cast<double>(pcm.size()) / omk::kAdpcmRate);
        front.stopSound(voiceOverShot);
        voiceOverShot = pcm.empty() ? -1
            : front.playSound(resampleToDevice(pcm, 1, omk::kAdpcmRate, kDeviceRate),
                              false, dialogueGain());
        // ...and the TEXT. Step 13 of the handler is `Subtitle_Show` of
        // the buffer step 6 built: the record's +280 description, with a
        // `{C}` (centre) prefix when the player's ACTOR_STATE is 3 or
        // 15. Shown whatever the voice was - the JINGOFF3 substitute is
        // still followed by the line the player reads. An IMAGE (kind
        // 16) takes the other arm and shows a bitmap instead, which this
        // file does not draw.
        // A new media.play frees whatever bitmap was up, then this one
        // either loads its own or speaks.
        mediaBmp = omk::Surface{};
        if (vo.image && !vo.stem.empty()) {
            if (const auto bp = fs.resolve("IMAGES/" + vo.stem + ".BMP")) {
                mediaBmp = omk::surfaceFromBmp(omk::DataFs::readPath(*bp));
                std::printf("media.play %d is a DOCUMENT: IMAGES/%s.BMP %dx%d\n",
                            mediaId, vo.stem.c_str(), mediaBmp.w, mediaBmp.h);
            }
        }
        const auto& objs = voiceLib.objects();
        if (!vo.image && mediaId >= 0 && static_cast<std::size_t>(mediaId) < objs.size()) {
            std::string text = objs[static_cast<std::size_t>(mediaId)].description;
            const int st = player ? static_cast<int>(player->state()) : -1;
            if (!text.empty() && (st == 3 || st == 15)) text = "{C}" + text;
            mediaText = text;
            const long ms = std::max<long>(2000L, 80L * static_cast<long>(text.size()));
            mediaTextFrames = text.empty() ? 0 : static_cast<double>((ms * 30 + 999) / 1000);
            if (!text.empty())
                std::printf("media.play %d subtitle for %ld frames: %s\n", mediaId,
                            static_cast<long>(mediaTextFrames), text.c_str());
        }
    }

    // A conversation started. The world stops around one - that is the
    // engine (`Dialog_TickUI` owns the frame) - and the frontend has NO
    // dialogue UI yet, so there is nothing here that can play it. Ending
    // it is what the headless boot does to keep the chain moving, and it
    // is a STAND-IN: the line is not spoken, the replies are not offered,
    // and saying so is the point.
    // A conversation is on screen. The world stops around one - that is
    // the engine, `Dialog_TickUI` owns the frame - and NOTHING here
    // advances it. The voice plays; the player presses ENTER for NEXT,
    // then picks a reply with the arrows and ENTER. That is the same rule
    // the start menu follows and the same one `tools/omkweb.html` plays a
    // conversation by: the game never shows the NPC line and the menu
    // together, and the click that ends the line is what reveals the menu.
    //
    // Still missing, and said rather than hidden: the line and the replies
    // are printed here, not DRAWN, and the dialogue cameras and the
    // speaker's pose the web viewer carries are not wired in - so what is
    // on screen during a conversation is the world camera the script last
    // set.
    phSpan["audio: voice-overs"] += phaseNow() - auB;
    const double auC = phaseNow();
    if (session.dialogOpen()) {
        // THE PRESS BELONGS TO THE CONVERSATION. While a dialogue is up
        // the action bit is the interface's - `Dialog_TickUI` takes it -
        // and the world must not see the same press again when the
        // conversation closes. Without this the ENTER that dismisses the
        // last line was still down on the next frame, `MDACTION` matched
        // `H_STAND -> 24`, and the seller was talked to a second time the
        // instant he had finished. Marking it spent here means the button
        // has to go up first, which is the same rule the gate above
        // applies to two presses in the world.
        if (bits & omk::kUiConfirm) actionSpent = true;
        const auto& dlg = session.dialogue();
        // A new conversation: stage the speaker (the model itself is
        // loaded on `character.show`, which comes first).
        if (session.dialogue().conversation().id != speakerConv) {
            speakerConv = session.dialogue().conversation().id;
            speakerReady = false;
            speakerSolved = false;
            speakerModel = session.speakerModel();
            speakerMeshes.clear();
            // The BODY is a `Staged` like any other - one `CharModel` per
            // model name, shared with whoever else wears it. The meshes
            // are copied here only for `rootTrackOf` and the report.
            CharModel* cm = charModelFor(speakerModel);
            if (cm && cm->ready) {
                speakerMeshes = cm->meshes;
                // Where he stands: the line cameras' rays, dropped onto
                // the set's own walkable floor.
                omk::TriangleSoup soup;
                if (const auto so = fs.resolve("MESHES/DECORS/" + worldSet + ".3DO"))
                    soup = omk::collisionSoup(omk::DataFs::readPath(*so),
                                              omk::SoupKind::Walkable);
                const auto st = omk::stageSpeaker(
                    session.dialogue().conversation().cams,
                    omk::lineCameraIds(session.dialogue().conversation()),
                    soup.empty() ? nullptr : &soup);
                // The authored PATH wins when the scene gives one: it is
                // where the object actually puts him, and a camera solve
                // is the fallback for a conversation whose speaker no
                // scene object drives. The staging below applies it only
                // to a body nothing else has placed.
                for (int k = 0; k < 3; ++k) speakerAt[k] = st.pos[k];
                speakerSolved = st.valid;
                // READY MEANS THE MODEL IS LOADED, NOT THAT A CAMERA SOLVE
                // FOUND HIM. `Morph_Play` runs the line on the speaker's
                // own node whatever the cameras are; the solve exists
                // only to PLACE a speaker nothing else places, and
                // `speakerSolved` carries that for the staging below.
                // Gating readiness on the solve meant a conversation
                // framed entirely off its speaker - the transcan's
                // advert, whose one camera is [2,2] - cast no rays,
                // solved nothing, and never loaded the line's .3DM: the
                // hologram played its scene clip through the whole
                // advert, "frame 95 of 0". A reader: *the character on
                // transcan is not animated correctly*.
                speakerReady = true;
                std::printf("speaker: %s (%zu meshes, %zu corners), %d rays "
                            "converge scatter %.1f, stands at %.0f %.0f %.0f%s\n",
                            speakerModel.c_str(), speakerMeshes.size(),
                            cm->rest.corners.size(), st.rays, st.scatter,
                            st.pos[0], st.pos[1], st.pos[2],
                            st.onFloor ? " on the walkable floor" : "");
            } else {
                std::printf("speaker: no model for the conversation - "
                            "its cameras will look at nothing\n");
            }
        }
        if (dlg.lineChanged()) {
            session.clearDialogLineChanged();
            // The line's own `.3DM` drives the pose.
            speakerVoice = dlg.voice();
            speakerTracks = omk::NodeTracks{};
            speakerMorph.reset();
            lineIdleFrame = sceneFrameLast;
            const auto lineT0 = std::chrono::steady_clock::now();
            const auto lineMs = [](std::chrono::steady_clock::time_point a) {
                return std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - a).count();
            };
            double tracksMs = 0.0;
            if (speakerReady && !speakerVoice.empty())
                if (const auto ma = fs.resolve("MORPH/" + speakerVoice + ".3DM")) {
                    // THE BYTES THE CONVERSATION ALREADY READ for the
                    // voice: a line's .3DM runs to 3.5 MB, and reading it
                    // a second time here cost a console ~700 ms on the
                    // frame the line started (2026-09-23)
                    speakerMorph = dlg.morph().empty()
                        ? std::make_shared<const std::vector<std::byte>>(
                              omk::DataFs::readPath(*ma))
                        : dlg.morphShared();
                    const auto tt0 = std::chrono::steady_clock::now();
                    speakerTracks = omk::nodeTracks(
                        *speakerMorph, omk::rootTrackOf(speakerMeshes));
                    tracksMs = lineMs(tt0);
                    const CharModel* fm = charModels.count(speakerModel)
                        ? &charModels.at(speakerModel) : nullptr;
                    std::printf("  face: mesh %d, %d verts; the line supplies %s\n",
                                fm ? fm->face.mesh : -1, fm ? fm->face.count : 0,
                                fm && fm->face.valid() ? "them" : "none");
                }
            if (!conversations++)
                std::printf("--- conversation: ENTER for next, "
                            "arrows + ENTER to reply ---\n");
            std::printf("[%s, %.1f s] %s\n",
                        dlg.voice().empty() ? "no voice" : dlg.voice().c_str(),
                        dlg.lineSeconds(), cp1252ToUtf8(dlg.lineText()).c_str());
            front.stopSound(voiceOverShot); voiceOverShot = -1;   // one streamer
            front.stopSound(voiceShot);
            const auto rs0 = std::chrono::steady_clock::now();
            // (prepared with the line when it was read ahead - then this
            // is a hand-over and the `resample` below reads 0)
            std::vector<float> voiceDev = session.takeVoiceDevicePcm();
            if (voiceDev.empty() && !dlg.pcm().empty())
                voiceDev = resampleToDevice(dlg.pcm(), dlg.channels(), 22050, kDeviceRate);
            const double resampleMs = lineMs(rs0);
            const auto ps0 = std::chrono::steady_clock::now();
            voiceShot = voiceDev.empty() ? -1
                      : front.playSound(std::move(voiceDev), false, dialogueGain());
            // WHERE A LINE'S START GOES (2026-09-23: "it is a bit long to
            // load the dialog" on a console) - the conversation's read and
            // decode, then this frontend's face tracks, resample, hand-over
            std::printf("line load: %s - read %.0f ms (%zu KB%s), voice decode %.0f ms, "
                        "face tracks %.0f ms, resample %.0f ms, into the mixer %.0f ms; "
                        "%.0f ms here in all; %zu next line(s) reading ahead",
                        dlg.voice().empty() ? "no voice" : dlg.voice().c_str(),
                        dlg.loadReadMs(), dlg.morph().size() / 1024,
                        dlg.loadPrefetched() ? ", read and decoded AHEAD" : "",
                        dlg.loadDecodeMs(),
                        tracksMs, resampleMs, lineMs(ps0), lineMs(lineT0),
                        dlg.aheadCount());
            if (dlg.loadPrefetched())
                std::printf("; its own thread spent %.0f ms on it", dlg.loadAheadMs());
            std::printf("\n");
            replySel = 0; menuShown = false; lineScroll = 0;
        }
        if (dlg.phase() == omk::DialogPhase::Menu && !menuShown) {
            menuShown = true;
            replySel = -1;
            for (std::size_t k = 0; k < dlg.replies().size(); ++k) {
                const auto& r = dlg.replies()[k];
                if (!r.available) continue;
                if (replySel < 0) replySel = static_cast<int>(k);
                std::printf("   %s %zu. %s\n",
                            static_cast<int>(k) == replySel ? "->" : "  ",
                            k + 1, cp1252ToUtf8(r.text).c_str());
            }
        }
        // THE DIALOGUE MENU IS SILENT. `Dialog_TickUI` (0x0046A200, 388
        // lines) makes no sound call, and neither does the dispatch that
        // drives it in `Actors_TickAll`; the per-screen move/confirm/open
        // slots belong to the interface SCREENS (UI 3), and a conversation
        // is not one. Whatever a reply plays is its branch ACTION's own
        // script. A first version borrowed the open screen's blips here,
        // which a reader heard as wrong.
        // THE MENU COUNTS PRESSES, so it reads the EDGES (omk-play 74).
        // A reader on a bench: one press of Enter answered several times
        // over and one tap of a direction ran the selection round the
        // list. `Game_Frame` hands the dialogue `dword_90E0E0` - a word
        // ZEROED EVERY FRAME and rebuilt by the per-action callbacks (the
        // label-less `or al, N` family at 0x42B8F0, table entries nothing
        // calls directly) - and the call site splices the RAW word in for
        // exactly two bits:
        //
        //     case 2: Dialog_TickUI(2, dword_90E0E0 | dword_4E9718 & 0xC);
        //
        // 0xC is bits 2 and 3, which elsewhere nudge a camera by 6.0 a
        // frame. Splicing the raw word in by name for those two is only
        // meaningful if `dword_90E0E0` is NOT raw - so the confirm and the
        // menu steps are edges, and the two camera bits are the exception
        // the engine had to write out.
        if (edgeBits & omk::kUiConfirm) {
            if (dlg.phase() == omk::DialogPhase::Menu) {
                if (replySel >= 0 &&
                    replySel < static_cast<int>(dlg.replies().size())) {
                    session.dialogChoose(dlg.replies()[
                        static_cast<std::size_t>(replySel)].branch);
                }
            } else {
                // The press that leaves a line: `Dialog_TickUI` case
                // 2/7/8 -> `Morph_Stop`, which stops the voice buffer.
                front.stopSound(voiceShot);
                voiceShot = -1;
                session.dialogNext();
            }
        } else if (dlg.phase() == omk::DialogPhase::Menu &&
                   (edgeBits & (omk::kUiUp | omk::kUiDown))) {
            // Step to the next AVAILABLE reply, the way `Ui_MoveSelection`
            // steps over an unselectable row.
            const int n = static_cast<int>(dlg.replies().size());
            const int dir = (edgeBits & omk::kUiDown) ? 1 : -1;
            for (int step = 1; step <= n; ++step) {
                const int c = ((replySel + dir * step) % n + n) % n;
                if (dlg.replies()[static_cast<std::size_t>(c)].available) {
                    if (c != replySel) replySel = c;
                    break;
                }
            }
        }
        if (!session.dialogOpen())
            std::printf("--- conversation over ---\n");
    }

    phSpan["audio: conversation"] += phaseNow() - auC;
    mark("audio");
}

// Dialogue mode
void PlayState::modesDialogue() {
    OMK_ZONE("modes: dialogue");   // the profiler (todo/debug-tools.md 6)
    // ---- DIALOGUE MODE, and the bug its absence caused ---------------
    //
    // `Actor_EnterDialogueMode` (0x00468DE0) and `Actor_LeaveDialogueMode`
    // (0x00468E80) bracket every conversation, and both end with a BANK
    // SWITCH on the player's channel:
    //
    //     enter: Perso_SetInputEnabled(channel, 0)
    //            SetPersoBankGroup(channel, Cef_FindGroupById(bank, 400))
    //     leave: SetPersoBankGroup(channel, Cef_FindGroupById(bank, 100))
    //            Perso_SetInputEnabled(channel, 1)
    //
    // H1AVNT has both (`engine/tools/dialog_bank.cpp`): group id 400 is
    // the 15-entry dialogue group, and id 100 is the bank's DEFAULT group
    // (`flags & 1`), 44 entries, default `H_STAND`/`MDSTAND` - adventure
    // mode. `SetPersoBankGroup`'s memset clears the channel's queue and
    // latches and its `gotoMove` re-enters that default, which is the
    // SAME guard `sub_465D30` uses at the end of a successful take: the
    // held word must CHANGE before anything matches again.
    //
    // That is what stops the ENTER which dismisses a conversation's last
    // line from falling straight through into the take. `ActorRuntime`
    // has carried both functions since the ACTOR_STATE machine was
    // ported, but NOTHING here called them - so the viewer never entered
    // dialogue mode, never left it, and a reader who finished a shop
    // seller's dialogue with Enter still held watched the take graph run
    // (MDACTION -> MDGETOBJ -> H_WAITOB -> MDPUTSNK) on the next frame.
    //
    // The transition is tested HERE, after the block that may have ended
    // the conversation, so the leave lands on the SAME frame the dialogue
    // closes - one frame later would be one frame too late, because the
    // MDACTION gate above runs before this point in the next frame.
}

int PlayState::modesShoot() {
    OMK_ZONE("modes: shoot");   // the profiler (todo/debug-tools.md 6)
    const auto& fs = *fs_;
    auto& optMenu = *optMenu_;
    auto& in = *in_;
    auto& inv = *inv_;
    auto& session = *session_;
    bool done = false;
    do {
        // ---- SHOOT MODE, the same shape one subsystem over ---------------
        //
        // `shoot.begin` / `shoot.end` (ops 80/81) are decisions the Session
        // makes (`actor/shootmode.h`); what a frontend owes them is the three
        // installs `Shoot_Enter` does and `Shoot_Leave` undoes: the player's
        // `ACTOR_STATE` 3 with `.CTL` group 200, input scheme 2, and camera
        // mode 4. Nothing here calls the AI - `actor/shoot.h`'s brains are
        // still unwired for the generic arm (`todo/standing-unknowns.md` 2) -
        // so what this draws is the MODE, not a gunfight.
        if (session.shootMode().active() != shootMode) {
            shootMode = session.shootMode().active();
            if (player) {
                if (shootMode) player->enterShootMode();
                else           player->leaveShootMode();
            }
            if (!shootMode) dropLibrary(shootRt);   // `aventure.scx` back
            in.installScheme(shootMode ? omk::ShootMode::kInputScheme : 0);
            // ---- THE CAMERA, and it belongs HERE ------------------------
            //
            // `Shoot_Enter` ends with `Camera_Request(4, ...)` and both
            // camera actors set to the player, so ENTERING SHOOT MODE
            // installs the first-person camera full stop - it is not
            // something the `--shoot` harness does.
            //
            // It used to live in that harness alone, and a reader found what
            // that costs. Reaching the supermarket phase through the game's
            // own script, the log read:
            //
            //   frame 3219: editing over - the camera HOLDS its last frame
            //               (mode 13, no active camera, autocameraplayer 0)
            //   frame 3221: SHOOT MODE ENTER - ACTOR_STATE 3, scheme 2
            //
            // - the mode on, the scheme installed, and the camera never asked
            // for, so the view stayed on the cutscene's last framing with the
            // player standing in it (`todo/omk-play.md` 97d).
            //
            // `camera_presets.json` row 4: eye (0,0,0) - ON the player - and
            // target (0, 0, 787.4016), the aim 20.00 m ahead, fov 75, with
            // every smoothing divisor ZERO so it does not lag him.
            if (shootMode && player) {
                const float eye[3] = {0.0f, 0.0f, 0.0f};
                const float at[3]  = {0.0f, 0.0f, 787.4016f};
                player->setCameraOffsets(eye, at, 75.0f);
                shootCameraLive = true;
                shootPitch = 0.0f;
                // ...AND THE CAMERA ALREADY THERE IS NOT A NEW ONE. The follow
                // arm below gives the view away whenever the script's camera
                // id differs from the last one APPLIED - its labelled reading
                // of a script camera outranking mode 4. But an ABSOLUTE camera
                // is never applied as a follow camera, so its id can be resident
                // and still "differ": the supermarket's airlock cutscene
                // (AREA 231 record 1) ends on camera 4380, a fixed shot OUTSIDE
                // the building, `playerCamId` still holds its 4379, and forcing
                // `followCam` for the first-person camera made the next frame
                // read 4380 as newly named - it cleared this flag and loaded
                // 4380's absolute eye as offsets. A reader, from a real save:
                // "the camera was still and outside the supermarket", the
                // log's `AIM reached - cameraLive 0`. The harness never showed
                // it: it names only camera 0, already applied. Only a camera a
                // script names AFTER `shoot.begin` may take the view now.
                if (const omk::WorldCamera* rc = session.cameraTarget())
                    playerCamId = rc->id;
                front.setRelativeMouse(true);
                std::printf("frame %ld: shoot camera - mode %d, eye %.1f above the "
                            "pelvis (%s), aim 20 m ahead\n", n,
                            omk::ShootMode::kCameraMode,
                            double(shootEyeSet ? shootEyeLift : player->headLift()),
                            shootEyeSet ? "--shoot-eye"
                                        : "sub_414520 case 4: 0.7 * the model's extent");
            }
            if (!shootMode) {
                shootCameraLive = false;
                front.setRelativeMouse(false);
                omk::shootMoveLeave(shootMover);          // `sub_47CE70`
                hudWalk.reset();                          // the HUD screen closes
                // THE FOLLOW CAMERA IS OWED ITS OFFSETS BACK. The mode wrote
                // preset row 4's (the eye ON him) into the controller, and the
                // follow camera re-applies a world camera's offsets only when
                // the script names a DIFFERENT one - and the supermarket's
                // ending names camera 0, the one already applied before the
                // phase, so the eye stayed inside him: a weird camera and a
                // body that looked gone (a reader, 2026-09-10). `Shoot_Leave`
                // requests no camera; the script's next `camera.set` is what
                // brings the view back, and forgetting which one was applied
                // is what lets it.
                playerCamId = -2;
            }
            // ---- `Shoot_Enter` 1, 2 and 9, for the SHOT (`actor/shootfire.h`)
            //
            // The 100 records are ZEROED - so the weapon starts UP - the
            // player's gets `+188 = -1` and `+160 |= 2`, and step 9 hands
            // `Shoot_InitWeapon` the object in his hand: event 46 property 3
            // is its KIND, and the kind is the key into the PLAYER's table.
            if (shootMode) {
                playerShootRec = omk::ShootRecord{};
                playerShootRec.node = -1;
                playerShootRec.flags |= 2u;
                // `Shoot_Enter` step 6's own read: event 44 property 7, the
                // player's CHARACTER TYPE. It picks the HUD screen (33 for the
                // Mecagarde, 34 for anyone else) and, in `sub_47CC70` below,
                // the mover's three sounds and its pitch flag. The port left it
                // at -1 until 2026-09-12, which gave the right screen for the
                // wrong reason - every value but 5 gives 34.
                {
                    std::int32_t ctype = 0;
                    omk::readActorProperty(
                        state.raw().subspan(
                            static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                            static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                        7, ctype);
                    session.shootModeMutable().setPlayerType(static_cast<int>(ctype));
                    playerShootRec.type = static_cast<std::uint32_t>(ctype);
                    std::printf("frame %ld: SHOOT TYPE - property 7 = %d -> HUD screen %d\n",
                                n, int(ctype), session.shootMode().hudScreen());
                }
                // `sub_422540(player)`, `Shoot_Enter`'s own call: his property
                // 1 into +92 - the health `Shoot_SyncHudHealth` hands the HUD,
                // a 0 rewritten to 10 - and `Hud_Refresh`'s
                // `sub_446C40(0, 22)` / `(1, 22)`, the gauge's sparks seeded
                {
                    std::int32_t hp = 0;
                    omk::readActorProperty(
                        state.raw().subspan(
                            static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                            static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                        1, hp);
                    harnessShootHealth(hp);
                    playerShootRec.health = hp != 0 ? static_cast<int>(hp) : 10;
                    hudHealth = playerShootRec.health;   // `Shoot_SyncHudHealth`, `dword_90E100`
                    std::printf("frame %ld: SHOOT HEALTH (sub_422540) - property 1 = %d "
                                "-> record +92 = %d\n", n, int(hp), playerShootRec.health);
                    hudBar.refresh(0, 22, fb.w);
                    hudBar.refresh(1, 22, fb.w);
                }
                // THE RADAR (`ui/radar.h`): `Map2D_Load`'s tail ran when the
                // area loaded - its +106 with ".MPT", rewritten to ".WRE" and
                // matched against nine names - and screen 34's open callback
                // 0x42E3A0 enables it. Loaded here, at the one place anything
                // reads it; the engine's clears the flag on every area load.
                {
                    const std::string& mp = session.mapName();
                    radar.load(fs, mp.empty() ? std::string() : mp + ".MPT");
                    // ...and the HUD's open callback: the ROBOT's (0x42E3A0)
                    // turns the switch on itself; the human's (0x42E4A0)
                    // leaves it to the scripts' op 146
                    if (session.shootMode().hudScreen() == 33) {
                        session.setRadarOn(true);
                        radar.openMeca();
                    } else {
                        radar.openHuman();
                    }
                    // ...and the MAP2D grid of the same name (`Map2D_Load`),
                    // whose floors the NOISE reads (`sub_4246E0`)
                    shootMap = omk::Map2d{};
                    shootField = omk::ShootField{};   // its field belongs to the old grid
                    if (!mp.empty() && shootMap.load(fs.read("MAP2D/" + mp + ".MPT")))
                        std::printf("frame %ld: MAP2D %s.MPT - %zu floors, cell %u\n", n,
                                    mp.c_str(), shootMap.floors().size(), shootMap.scale());
                    // ...AND `Shoot_Enter`'S LAST CALL, `Shoot_TickPlayer(player)`:
                    // the player's record thinks ONCE, here, before any gunman's
                    // brain runs. The per-frame think above ran before this frame's
                    // entry and found shoot mode still off, and the record was just
                    // zeroed - so without this the gunmen's first grid sight walked
                    // from cell (0,0): the supermarket's robber 77 read CLEAR at frame
                    // 394 from a cell the player was never on.
                    //
                    // NOT the per-frame think's point. That one reads the root
                    // mesh as the CONTROLLER last drew it (`playerMeshAt`), and a
                    // phase begun at the end of a scene program has not been drawn
                    // by the controller since before the program: the intro drew
                    // his body as a staged actor, so on this frame `playerMeshAt`
                    // still held his street-start spot (13058, 1089) - a counter
                    // cell `Shoot_Think` refuses - and the record stayed (0,0).
                    // The hand-back at the program's end has already put the
                    // CONTROLLER where the body stood, so its x and z are the true
                    // ones; the height is the pelvis, `pos - cameraLift`, because
                    // at the feet the supermarket's player stands on his grid's
                    // upper bound and `floorAt` finds nothing. LABELLED: the engine
                    // has one body and reads its node.
                    if (shootMap.valid() && player) {
                        const float ep[3] = {player->pos()[0],
                                             player->pos()[1] - player->cameraLift(),
                                             player->pos()[2]};
                        const bool took = omk::shootThink(playerShootRec, shootMap, ep, 0, -1);
                        playerShootRec.flags &= ~0x1000u;
                        std::printf("frame %ld: Shoot_Enter's own Shoot_TickPlayer - the player's "
                                    "record at entry: floor %d, cell (%d,%d)%s\n", n,
                                    static_cast<signed char>(playerShootRec.node & 0xFF),
                                    playerShootRec.destX, playerShootRec.destZ,
                                    took ? "" : " (Shoot_Think refused the cell)");
                    }
                    if (radar.loaded())
                        std::printf("frame %ld: RADAR - AREA +106 '%s' -> %s, height %.2f, "
                                    "%d vertices, %d edges\n", n, mp.c_str(),
                                    radar.file().c_str(), double(radar.height()),
                                    radar.wire().vertices(), radar.wire().edges());
                    else
                        std::printf("frame %ld: RADAR - AREA +106 '%s' -> none: not one of "
                                    "0x42EE70's nine names, so the item stays hidden\n",
                                    n, mp.c_str());
                }
                shotLatch = omk::ShotLatch{};
                shootAim = omk::ShootAim{};
                // `sub_47CC70`, `Shoot_Enter`'s own call: the mover's block
                // zeroed and its three speeds from property 3 - SPEED, the
                // player record's +158 - through the table at 0x004CF7D0; its
                // period is `sub_45AC80` of group 200's default entry, the
                // stance clip's length (only the bob reads it, not modelled)
                {
                    std::int32_t speed = 0;
                    omk::readActorProperty(
                        state.raw().subspan(
                            static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                            static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                        3, speed);
                    float period = 0.0f;
                    if (player)
                        if (const omk::NodeTracks* st =
                                player->clipTracks(player->groupDefaultClip(200)))
                            period = static_cast<float>(st->frames);
                    // ...and its three SOUND ids and flag 0x1000 off the
                    // player's character type (property 7), the same test that
                    // picked the HUD screen above.
                    omk::shootMoveInit(shootMover, speed, period,
                                       session.shootMode().playerType());
                    shootMoveFrames = 0;
                    std::printf("frame %ld: SHOOT MOVER (sub_47CC70) - Speed %d -> row %d: "
                                "top %.3f, accel %.4f, brake %.3f a frame, period %.0f; "
                                "type %d -> hurt sound %d, steps %d/%d%s\n",
                                n, int(speed), shootMover.row, double(shootMover.top),
                                double(shootMover.accel), double(shootMover.brake),
                                double(period), session.shootMode().playerType(),
                                shootMover.hurtSound, shootMover.stepLeft,
                                shootMover.stepRight,
                                omk::shootMovePitches(shootMover)
                                    ? "" : " - a Mecagarde: HE CANNOT PITCH");
                }
                // `Shoot_InitWeapon`, step 9 (`PlayState::shootInitWeapon`,
                // which `shoot.player.resume` calls too): the row and the
                // magazine; `objs`/`known` are kept for the gun's stem below
                int obj = -1, kind = -1, type = 0;
                shootInitWeapon(obj, kind, type);
                const auto& objs = voiceLib.objects();
                const bool known = obj >= 0 && static_cast<std::size_t>(obj) < objs.size();
                // `Shoot_Enter` loads `scptdata\shoot2.sfx` and hands it to
                // `sub_44EDF0`, whose section A is the shot sprites
                if (!shootSfx.valid && shootSfx.shotSprites.empty())
                    shootSfx = omk::readSfx(fs.read("SCPTDATA/shoot2.sfx"));
                // ...and `Game_Start("shoot2.scx")`, the library the shot's
                // sounds resolve in: loaded on every entry and released on the
                // way out, as the engine puts `aventure.scx` back - kept, it
                // was 4 MB for the rest of the run (`loadLibrary`)
                if (!shootRt) loadLibrary(shootRt, "SCPTDATA/shoot2.scx");
                shotGunStem = known ? objs[static_cast<std::size_t>(obj)].stem : std::string();
                if (!shotGunStem.empty()) {
                    const GunFacts& gf = gunFactsFor(shotGunStem);
                    const omk::FxShotSprite* sp = shootSfx.shotSprite(gf.root);
                    std::printf("frame %ld: the gun %s - root '%s', tir %s (local %.1f %.1f "
                                "%.1f), shot sprite %s (grow %.0f, wind-up %.0f), %zu in "
                                "shoot2.sfx\n", n, shotGunStem.c_str(), gf.root.c_str(),
                                gf.ok ? "found" : "MISSING", double(gf.tirLocal[0]),
                                double(gf.tirLocal[1]), double(gf.tirLocal[2]),
                                sp ? "found" : "none", sp ? double(sp->grow) : 0.0,
                                sp ? double(sp->windUp) : 0.0, shootSfx.shotSprites.size());
                }
                hudWalk = std::make_unique<omk::UiWalk>(w);
                if (!hudWalk->open(session.shootMode().hudScreen())) hudWalk.reset();
                hudTold.clear();
                if (const omk::ShootWeaponRow* w = playerShootRec.weapon)
                    std::printf("frame %ld: Shoot_InitWeapon - object %d kind %d -> type %d: "
                                "rate %.0f frames, speed %.1f, damage %d, magazine %d\n", n,
                                obj, kind, type, double(w->rate), double(w->speed),
                                w->damage, w->ammoIndex);
                else
                    std::printf("frame %ld: Shoot_InitWeapon - object %d kind %d -> type %d: "
                                "NO ROW, so the gate will never fire\n", n, obj, kind, type);
            } else {
                playerShootRec.weapon = nullptr;
                shotLatch = omk::ShotLatch{};
            }
            std::printf("frame %ld: SHOOT MODE %s - weapon slot %d (object %d), "
                        "HUD screen %d, library %s, ACTOR_STATE %d, scheme %d\n",
                        n, shootMode ? "ENTER (Shoot_Enter)" : "LEAVE (Shoot_Leave)",
                        session.shootMode().weaponSlot(),
                        session.shootMode().weaponObject(),
                        session.shootMode().hudScreen(),
                        session.shootMode().library(),
                        player ? static_cast<int>(player->state()) : -1,
                        in.group());
        }

        // ---- THE PLAYER'S SUSPEND AND RESUME (todo/drift-audit.md S7) ----
        //
        // `shoot.player.suspend` (op 116, 0x4050E0 -> 0x422950, read from the
        // image), the player half of `Shoot_Leave` and no more:
        //   the held object dropped (`ObjectSlot_Free`, `Actor_ReleaseObject`)
        //   sub_44CDB0(0)                     the bolts in flight freed
        //   sub_436D20(node)                  his body SHOWN again
        //   SetPersoBankGroup(default group)  - and NOT ACTOR_STATE 1
        //   sub_47CE70()                      the shooter cleared
        //   dword_910358 = 1.0                (already 1.0; not modelled)
        //   UI_CloseAllScreens()              the HUD screen closed
        //   g_PlayerBehaviourOff = 1          every gunman's brain stops
        //   Input_InstallScheme(0)
        // with no camera request: the script's own cameras follow (the
        // ladders' `camera.set.at_address`). NOT the gunmen's teardown, NOT
        // `aventure.scx`, NOT `g_ShootMode` - the phase goes on.
        //
        // `shoot.player.resume` (op 117, 0x405130 -> 0x4229C0), the player
        // half of `Shoot_Enter`: ACTOR_STATE 3 and group 200, the HUD screen
        // by property 7 (33, or 34 with `Hud_Refresh`), his body hidden again
        // (`sub_436CE0`), `Camera_Request(4)`, `sub_47CC70` the mover, event
        // 48 and `Shoot_InitWeapon` (the gun back in his hand: the wrapper
        // sets `dword_4E6C84` when the hand is empty, which suspend made it),
        // `g_PlayerBehaviourOff = 0`, scheme 2. NOT the record zeroed, NOT
        // the type or health re-read, NOT `shoot2.sfx`, NOT the radar or the
        // MAP2D load, NOT `Shoot_Enter`'s closing `Shoot_TickPlayer`.
        //
        // Outside a phase the engine's resume writes through a null record
        // table (`g_ShootRecords + idx * 192 + 0x54`); the port applies the
        // flag only and does nothing to the player.
        if (session.shootMode().suspends() != shootSuspendsSeen) {
            shootSuspendsSeen = session.shootMode().suspends();
            if (shootMode && player) {
                const int bolts = projectiles.live();
                projectiles.clear();
                player->suspendShootMode();
                omk::shootMoveLeave(shootMover);
                hudWalk.reset();
                shootCameraLive = false;              // his body drawn again
                front.setRelativeMouse(false);
                playerCamId = -2;                     // as `Shoot_Leave`: the next script camera applies
                in.installScheme(0);
                std::printf("frame %ld: shoot.player.suspend - %d bolt%s freed, the bank's "
                            "default group, HUD closed, scheme %d, every gunman's brain OFF "
                            "(ACTOR_STATE stays %d)\n", n, bolts, bolts == 1 ? "" : "s",
                            in.group(), static_cast<int>(player->state()));
            }
        }
        if (session.shootMode().resumes() != shootResumesSeen) {
            shootResumesSeen = session.shootMode().resumes();
            if (shootMode && player) {
                player->enterShootMode();
                hudWalk = std::make_unique<omk::UiWalk>(w);
                if (!hudWalk->open(session.shootMode().hudScreen())) hudWalk.reset();
                hudTold.clear();
                if (session.shootMode().hudScreen() == 34) {   // `Hud_Refresh` on 34 only
                    hudBar.refresh(0, 22, fb.w);
                    hudBar.refresh(1, 22, fb.w);
                }
                const float eye[3] = {0.0f, 0.0f, 0.0f};
                const float at[3]  = {0.0f, 0.0f, 787.4016f};
                player->setCameraOffsets(eye, at, 75.0f);
                shootCameraLive = true;
                shootPitch = 0.0f;
                if (const omk::WorldCamera* rc = session.cameraTarget())
                    playerCamId = rc->id;
                front.setRelativeMouse(true);
                {
                    std::int32_t speed = 0;
                    omk::readActorProperty(
                        state.raw().subspan(
                            static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                            static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                        3, speed);
                    float period = 0.0f;
                    if (const omk::NodeTracks* st =
                            player->clipTracks(player->groupDefaultClip(200)))
                        period = static_cast<float>(st->frames);
                    omk::shootMoveInit(shootMover, speed, period,
                                       session.shootMode().playerType());
                    shootMoveFrames = 0;
                }
                int obj = -1, kind = -1, type = 0;
                shootInitWeapon(obj, kind, type);
                in.installScheme(omk::ShootMode::kInputScheme);
                std::printf("frame %ld: shoot.player.resume - ACTOR_STATE %d, group 200, HUD "
                            "screen %d, camera mode %d, weapon type %d (%s), scheme %d, the "
                            "gunmen's brains ON\n", n, static_cast<int>(player->state()),
                            session.shootMode().hudScreen(), omk::ShootMode::kCameraMode, type,
                            playerShootRec.weapon ? "a row" : "NO ROW", in.group());
            }
        }

        if (session.dialogOpen() != dialogMode) {
            dialogMode = session.dialogOpen();
            if (player) {
                if (dialogMode) player->enterDialogueMode();
                else            player->leaveDialogueMode();
            }
            std::printf("frame %ld: dialogue mode %s - bank group %d, ACTOR_STATE %d\n",
                        n,
                        dialogMode ? "ENTER (Actor_EnterDialogueMode)"
                                   : "LEAVE (Actor_LeaveDialogueMode)",
                        dialogMode ? 400 : 100,
                        player ? static_cast<int>(player->state()) : -1);
        }

        {
            if (addrSeen.empty()) {
                addrSeen.assign(791, 0);
                for (int k = 0; k < 791; ++k)
                    addrSeen[static_cast<std::size_t>(k)] =
                        static_cast<std::uint8_t>(state.bit(omk::StateArray::AddressEnabled, k));
            } else {
                for (int k = 0; k < 791; ++k) {
                    const auto now = static_cast<std::uint8_t>(
                        state.bit(omk::StateArray::AddressEnabled, k));
                    if (now == addrSeen[static_cast<std::size_t>(k)]) continue;
                    addrSeen[static_cast<std::size_t>(k)] = now;
                    std::printf("frame %ld: ADDRESS %d %s\n", n, k,
                                now ? "ENABLED - a place the scripts can send him now"
                                    : "disabled");
                }
            }
        }
        // A script asked for a screen, or the PLAYER did. Which screen is the
        // SESSION's answer or the special move's, never this file's.
        if (!walk && (session.pendingUiScreen() >= 0 || playerScreen >= 0)) {
            const bool fromScript = playerScreen < 0;
            const int want = fromScript ? session.pendingUiScreen() : playerScreen;
            playerScreen = -1;
            // ...with the interface's OWN selections, which outlive the
            // walk. `list+2` is a field of a static record in the engine's
            // data segment: seeded by the linker, written by
            // `Ui_MoveSelection`, overwritten only where an open callback
            // writes it, and never reset. So the device remembers the verb
            // you last used and the row you were on across closing and
            // reopening it, and a walk built fresh each open would forget.
            auto fresh = std::make_unique<omk::UiWalk>(w, uiLists);
            // The load panel's directory, so its rows have something to be.
            // `Ui_BuildLoadPanel` calls `SaveDir_Build` itself in the OPEN
            // callback, which is why it is read here and not once at start-up:
            // a save written during this run has to show up next time the
            // panel opens.
            loadPanelState = omk::buildLoadPanel(
                omk::saveDirectory(omk::readSaveFile(savesPath, fr + "/IAM/GAMES").empty()
                                       ? fr + "/IAM/GAMES" : savesPath, w));
            loadPanelState.path =
                omk::readSaveFile(savesPath, fr + "/IAM/GAMES").empty()
                    ? fr + "/IAM/GAMES" : savesPath;
            fresh->attachLoadPanel(&loadPanelState);
            // `Ui_BuildLoadPanel` runs in the OPEN callback and lays the
            // shared panel out differently for each of its two screens - 29
            // puts `Charger une partie` in the top slot and hides `Nouvelle
            // partie`, 30 does the opposite. Re-applied per open for that
            // reason.
            omk::applyLoadPanelLayout(w, want);
            // The player's ANNEAUX, which the save screen's `Sauvegarde`
            // refuses without. `Actor_GetProperty` case 5 is the player
            // record's +174, and `GameState::rings` reads it.
            fresh->setRings(state.rings());
            // ...and whether `Images\\<resident set>.bmp` EXISTS, which is
            // what `sub_49D9E0` tests with `fopen` and what decides whether
            // `Lire plan` opens a page or bounces straight back to the
            // Inventaire tab. The walk cannot reach a file, so it is told.
            //
            // THE BITMAP ALONE. The city-table lookup is a separate step and a
            // miss there is not a refusal - it leaves `dword_4DECFC` at -1 and
            // the page stands, showing the bitmap with no pin on it. Only the
            // `fopen` bounces.
            {
                const std::string stem = session.setName();
                const bool have = fs.exists("IMAGES/" + stem + ".bmp");
                fresh->setCityMap(have);
                // Said once per open of the SNEAK, because a map that refuses
                // looks exactly like a map that is broken: the page bounces
                // with the refusal sound either way. Only four sets ship a
                // bitmap - ANEKBAH, QALISAR, JAUNPUR, LAHOREH, one AREA each,
                // the four cities' main streets - so on a rooftop, in the
                // Impasse or inside any building the engine bounces too.
                if (want == omk::kScreenSneak)
                    std::printf("sneak map: standing in set '%s' - %s\n", stem.c_str(),
                                have ? "Images/" "<set>.bmp exists, `Lire plan` opens"
                                     : "no Images/<set>.bmp, `Lire plan` bounces "
                                       "back (only the four cities' main-street "
                                       "sets ship one)");
            }
            // whatever is held on the frame it opens does not count as input
            // to it (see the gate in the walk's dispatch below)
            screenOpenBits = bits;
            if (!fresh->open(want)) {
                // A script's screen must be in the tree - the boot depends on
                // it. The PLAYER's need not be fatal: `sub_0046ADF0`'s own
                // failure arm raises event 26, logs "cant start sneak" and
                // returns, and the game carries on.
                if (fromScript) {
                    std::fprintf(stderr, "screen %d has no panel in the tree\n", want);
                    return 1;
                }
                std::printf("cant start sneak: screen %d has no panel in the "
                            "tree - event %d\n", want, omk::kEventSneakClose);
                inv.closeList();
            } else {
                walk = std::move(fresh);
                openScreen = want;
                screenFromScript = fromScript;
                // `ui.open`'s own `dword_4C0B64 = param` when it is not -1
                // (SCRIPT_VM 70) - the inventory channel's OPEN LIST, which
                // every shop site sets to 3, its stock. The bank's binder then
                // overrides it with event 25 on list 0 (`sub_42ADD0`).
                if (fromScript && session.pendingUiParam() != -1)
                    inv.openList(session.pendingUiParam());
                // THE SLIDER PAGE'S HOOK, 0x0049D4D0, on the frame screen 7
                // opens: `if (dword_6A17CC != -1)` resolve it with
                // `sub_40E630` and call `sub_452570` at once - the journey
                // starts without the menu being used. That is what "called
                // by a destination -> transported directly" is.
                if (want == 7 && boarded && calledDestination >= 0) {
                    walk->requestTravel(calledDestination);
                    std::printf("slider: screen 7 - a destination was "
                                "remembered (row %d), the journey starts\n",
                                calledDestination);
                }
                std::printf("frame %ld: screen %d %s - arrows move, ENTER confirms, "
                            "TAB closes\n", n, openScreen,
                            fromScript ? "is asking" : "opened by the player");
                // The screen's own sounds, by slot. Which slot is which is
                // `sub_482FE0`'s answer - it dispatches on the INPUT BIT - not
                // a guess from the file names. Nothing plays when a screen
                // opens: the engine has no such slot, and the one this used to
                // play was the MOVE sound fired at the wrong moment.
                sndMove    = loadSlot(openScreen, omk::UiWidgets::kSoundMove);
                sndConfirm = loadSlot(openScreen, omk::UiWidgets::kSoundConfirm);
                sndBack    = loadSlot(openScreen, omk::UiWidgets::kSoundBack);
                // ...and the screen's OWN sound, slot 4, played as it comes
                // up. Only SAVE GAME and PAUSE GAME carry one.
                if (const auto own = loadSlot(openScreen, omk::UiWidgets::kSoundScreen);
                    !own.empty()) {
                    blip(own);
                    std::printf("screen %d: its own sound (slot 4, `%s`)\n",
                                openScreen,
                                w.soundName(openScreen, omk::UiWidgets::kSoundScreen).c_str());
                }
                // ...AND THE SNEAK CALL ANSWERS ITSELF ON THE WAY IN.
                // `Ui_OpenSneakFamily`'s param-2 arm ends `call sub_42B560`,
                // so the screen hands its preset -1 back the moment it is up
                // and the parked script runs on with the device still on
                // screen. Without this the port waited for a person to close
                // it and the call's own `dialog.start` never ran - the game
                // showed an empty videophone.
                if ((fromScript || callHarness) && openScreen == kScreenVideophone) {
                    std::printf("screen %d VIDEOPHONE answers itself (-1, "
                                "`UI_SendAnswer` in its own open callback) - "
                                "the script runs on with the call up\n",
                                openScreen);
                    session.answerUi(-1);
                    videophoneCall = true;
                    callHarness = false;
                    if (callPending >= 0) {
                        session.harnessStartDialogue(callPending);
                        callPending = -1;
                    }
                }
            }
        }

        if (walk) {
            // What the person typed goes to the field before the navigation
            // bits, because `Confirmer` opens by testing the field's cursor
            // and writes nothing when it is empty (docs/UI.md) - so a confirm
            // in the same frame as the last letter must see the letter.
            // ...and the field may CONSUME the frame. `Ui_DispatchInput`
            // returns at the first hook that answers 1, so a key that is both
            // a character and a bit - RETURN is 13 and confirm - must not do
            // both. Without this gate ENTER moved the focus to the buttons
            // and then confirmed one of them in the same press, which starts
            // a game the player never asked for.
#if defined(__vita__)
            // THE NAME FIELD ON A VITA, which has no keyboard: the system's
            // on-screen one opens when the focus lands on the field - once per
            // arrival, so a cancel does not reopen it every frame; move off the
            // field and back to type again - and its answer goes in through
            // the field's own character channel: a BACKSPACE for every
            // character already there, the new name, and RETURN, which moves
            // the focus to the buttons exactly as a typed RETURN does. The
            // field stays the engine's; only the keys are the Vita's.
            {
                static bool imeOpenedHere = false;
                const bool onField = walk->nameFieldFocused();
                if (onField && !imeOpenedHere) {
                    imeOpenedHere = true;
                    std::string typed;
                    if (omk::vita::imeEdit("Nom", walk->name(), walk->nameMaxLength(), typed)) {
                        host.text = std::string(walk->name().size(), '\b') + typed + "\r";
                        std::printf("name field: the Vita keyboard gave \"%s\"\n", typed.c_str());
                    } else {
                        std::printf("name field: the Vita keyboard was cancelled\n");
                    }
                }
                if (!onField) imeOpenedHere = false;
            }
#endif
            const bool ate = !host.text.empty() && walk->typeName(host.text);
            // A KEY ALREADY DOWN WHEN THE SCREEN OPENED IS NOT A PRESS.
            //
            // `Ui_BeginScreen` installs the 0x203F repeat mask and
            // `Game_Frame` edge-filters against it, so the interface sees
            // EDGES. The action button that activates a save point is still
            // held on the frame `ui.open 30` puts the screen up, and without
            // this it is read again as that screen's confirm - so the save
            // menu opened and descended into the slot list in one press,
            // which is what a reader met ("when I interact with the save
            // point I have directly this"). The bits are swallowed until they
            // are RELEASED.
            // ---- THE HINT SHOP'S TWO INPUTS, before anything is pressed ----
            //
            // `sub_4AE120` reads both at BUILD time, and a build happens
            // inside a confirm - so they have to be in the walk's hands
            // before the press, not beside the drawing below.
            //
            // The PRICE is `Game_HandleEvent(42)` and is two steps:
            // `Message_RunHandlers(25, area, -1)` - which runs inline and
            // lets the world set the number - and then `Var_Get(GLOBAL+72)`.
            // Broadcast once per open rather than once per build, which is
            // the same value either way: the only message-25 handler in the
            // shipped data is `IAM\GLOBAL`'s `set.var.i8 198, 3`.
            //
            // The ROW COUNT is the list's `+24`, `sub_42ADD0`'s event 29 on
            // OBJECT LIST 2 - the memo journal, the same list the sneak's
            // `Memoire` page binds. It cannot change while the screen is up,
            // so it is read straight out of the DB.
            if (openScreen == 30) {
                static int hintTold = -1;
                const int priceVar = omk::globalHintPriceVar(globalFile);
                session.postMessage(25, -1);
                walk->setHintPrice(state.var(priceVar));
                const int rows = static_cast<int>(
                    omk::objectList(state, omk::ObjectList::Memos).size());
                walk->setHintRows(rows);
                if (hintTold != walk->hintPrice() * 1000 + rows) {
                    hintTold = walk->hintPrice() * 1000 + rows;
                    std::printf("indices: variable %d = %d anneaux, object list 2 "
                                "holds %d hint%s\n", priceVar, walk->hintPrice(),
                                rows, rows == 1 ? "" : "s");
                }
            }
            std::uint32_t uiBits = bits;
            // ---- "ACTION / UTILISER" IS NOT THE SAME BIT IN EVERY GROUP ---
            //
            // The interface reads slot 4 (`kUiConfirm`, 0x10) as confirm, and
            // in *Aventure* slot 4 IS `Action / Utiliser`. In *Tirer* it is
            // **`Tir`**, and `Action / Utiliser` has moved to slot 8 (0x100).
            // So while the shoot scheme is installed the raw word confirms a
            // menu when you pull the TRIGGER and does nothing when you press
            // ENTER - which is what a reader hit in the pause screen after
            // the supermarket cutscene (`todo/omk-play.md` 97f).
            //
            // Normalised here, at the one boundary the interface reads,
            // rather than in eleven call sites: the group's own
            // `Action / Utiliser` becomes the confirm the UI expects, and the
            // trigger is taken out of the UI's word entirely.
            //
            // A RECONSTRUCTION, and labelled as one. Nothing traced says the
            // engine re-maps anything - no screen open installs a scheme, and
            // its UI reads the same raw word - so on the face of it the
            // original would collide the same way. The reader says it does
            // not: the click shoots and ENTER validates. Their testimony about
            // the game outranks a reading that only shows nobody has found
            // the mechanism yet.
            // ---- AND *COMBAT* BINDS NO `Action / Utiliser` AT ALL -------
            //
            // The same fault as the shoot one below, one group over and a
            // degree worse. In *Combat* slot 4 is **`Coup de poing 1`** (key
            // 16, Q), so a punch confirms a menu; and unlike *Tirer*, which
            // merely MOVES `Action / Utiliser` to slot 8, *Combat* binds it
            // nowhere - slots 8, 9, 12 and 13 are all empty. So ENTER reaches
            // no bit whatever and the interface can never see a confirm. A
            // reader, with the pause screen up during a fight: *"the pause
            // menu doesn't work in fight mode (opens, but pressing enter does
            // nothing)"*.
            //
            // There is nothing to promote, so the confirm is taken from the
            // key *Aventure* binds to `Action / Utiliser` - read from the
            // table, not hard-coded, so a rebind still follows it - and
            // edge-filtered here, because it is not coming through
            // `Input::frame`'s own mask.
            //
            // A RECONSTRUCTION and labelled as one, on the same footing as
            // the shoot arm below: nothing traced says the engine re-maps
            // anything, and the reader's testimony that ENTER validates
            // outranks a reading that only shows nobody has found the
            // mechanism (`todo/fight-mode.md` 15.9).
            if (fightRun.active) {
                uiBits &= ~0x10u;                  // slot 4: `Coup de poing 1`
                const int k = in.schemes().code(0, 4, omk::Device::Keyboard);
                const bool down = k > 0 && st.holds(omk::Device::Keyboard, k);
                static bool advConfirmWasDown = false;
                if (down && !advConfirmWasDown) uiBits |= omk::kUiConfirm;
                advConfirmWasDown = down;
            }
            if (shootMode) {
                // ORDER MATTERS, and the first version of this got it wrong:
                // it OR-ed the confirm in and then AND-ed the same bit back
                // out on the next line, so `0100` came through as `0100` and
                // NEITHER the click nor ENTER did anything. Read the action
                // first, clear the trigger, then set the confirm.
                const bool action = (uiBits & 0x100u) != 0;   // slot 8
                uiBits &= ~0x10u;                             // slot 4: Tir
                if (action) uiBits |= omk::kUiConfirm;
            }
            if (screenOpenBits & uiBits) {
                screenOpenBits &= uiBits;    // still down: keep swallowing
                uiBits = 0;
            } else {
                screenOpenBits = 0;
            }
            // ...AND A PANEL CAN OPT OUT OF INPUT ALTOGETHER. `panel+72 & 8`,
            // which `Ui_ScreenInput` tests before it dispatches anything, and
            // the VIDEOPHONE's panel is the one in the tree that sets it. So
            // a sneak CALL swallows nothing: every press goes past the device
            // to the conversation running over it.
            if (!walk->takesInput()) uiBits = 0;
            // ---- SCREEN 35 UNDER THE START MENU (`todo/options-menu.md`) ----
            //
            // Screen 29's open callback `UI_LoadScreen(35, -1, -1)`s it, so the
            // options are RESIDENT for as long as the menu is up: opened here
            // once per walk of 29, with param 1, its rows read back from the
            // live settings. `Options` descends into panel 0x004CF420, whose
            // enter hook 0x0047BB40 FOCUSES 35 - input goes to it from then
            // until its root's BACK or `Retour` focuses 29 again, leaving the
            // menu on that panel (one more BACK returns to the four buttons).
            // ...and UNDER THE SNEAK (param 0, `todo/sneak.md` 5c): its open
            // loads 35 hidden; the Options tab's builder 0x0049D8F0 SHOWS it,
            // the tab's hook 0x0049D960 focuses it on LEFT / RIGHT.
            const bool optHost = (openScreen == 29 || openScreen == omk::kScreenSneak) && w.valid();
            if (optHost && optWalkSeen != walk.get()) {
                optWalkSeen = walk.get();
                optMenu.open(openScreen == 29 ? 1 : 0, settings.v);
                std::vector<std::string> modes;
                const int cur = front.displayModes(dispW, dispH, modes);
                optMenu.setDevices(2, modes, cur);
                // row 8's drivers: the GPU backend running, then string 68,
                // "Rendu logiciel", which `0x00493380` draws for the last one
                std::vector<std::string> drivers;
                if (!gpuDriverRow(drivers) && glRen)
                    drivers.push_back(std::string("OpenGL - ") + glRen->name());
                drivers.push_back(optMenu.str(68));
                optMenu.setDevices(8, drivers, (vkRen || glRen) ? 0 : static_cast<int>(drivers.size()) - 1);
                optEntered = false;
                if (optSndMove.empty() && optSndConfirm.empty()) {
                    optSndMove    = loadSlot(35, omk::UiWidgets::kSoundMove);
                    optSndConfirm = loadSlot(35, omk::UiWidgets::kSoundConfirm);
                    optSndBack    = loadSlot(35, omk::UiWidgets::kSoundBack);
                }
            }
            const bool optOnSneakTab = optHost && openScreen == omk::kScreenSneak &&
                walk->panel() && walk->panel()->addr == omk::kPanelSneakOptions;
            if (uiBits && optOnSneakTab && !optMenu.focused() && (uiBits & (omk::kUiLeft | omk::kUiRight))) {
                optMenu.focus();
                blip(sndMove);
                std::printf("frame %ld: options: screen 35 focused from the sneak's "
                            "Options tab (hook 0x0049D960), page %d\n", n, optMenu.page());
                uiBits = 0;
            }
            const bool optActive = optHost && optMenu.focused();
            if (uiBits && optActive) {
                // `Ui_ScreenInput` on the FOCUSED screen, and its sound
                if (!ate) optMenu.press(uiBits);
                if (bits & omk::kUiConfirm)      blip(optSndConfirm);
                else if (bits & omk::kUiBack)    blip(optSndBack);
                else if (optMenu.moved())        blip(optSndMove);
                if (!optMenu.focused())
                    std::printf("frame %ld: options: focus back to screen %d\n", n, openScreen);
                // ...AND, UNDER THE START MENU, ON TO ITS FOUR BUTTONS. A
                // RECONSTRUCTION, labelled as one: read alone, the root's
                // `Retour` and BACK only focus 29 (`Opt_RowInput` case 6,
                // 0x00492AD0), and `UI_TickScreens` visits slot 0 - screen 29 -
                // before slot 1, so 29 cannot take the same key that frame.
                // That leaves the menu on the bare `Options` heading until a
                // second BACK, which the reader played and called broken
                // (2026-10-01). Their account of the game outranks a reading
                // that has found no mechanism; the second BACK is sent here.
                if (!optMenu.focused() && openScreen == 29 &&
                    walk->panel() && walk->panel()->addr == 0x004CF420u) {
                    walk->press(omk::kUiBack);
                    std::printf("frame %ld: options: the start menu back on its buttons "
                                "(a second BACK - the reader's account, a reconstruction)\n", n);
                }
                // `UI_CloseScreen(9)`: the device's own close, as TAB does it
                if (optMenu.takeHostClose()) walk->press(omk::kUiClose);
            } else if (uiBits) {
                const int wasSel = walk->selection(), wasList = walk->currentList();
                // THE SOUND IS NOT GATED ON IT, and that is read rather than
                // assumed: `Ui_ScreenInput` (0x0042A0F0) calls
                // `Ui_DispatchInput` and then `sub_482FE0(screen)`
                // UNCONDITIONALLY, so the slot is picked from the live input
                // word whatever the dispatch did. A frame the field ate still
                // clicks - RETURN in the name box plays the confirm sound and
                // presses nothing.
                if (!ate) walk->press(uiBits);
                // The MOVE sound fires when the selection actually MOVED, not
                // on every press: `Ui_MoveSelection` steps over unselectable
                // rows and a pinned list stops at its ends, so a key that
                // changes nothing must make no sound either.
                // One sound per press, chosen by the BIT, which is what
                // `sub_482FE0` does. The move is still gated on the selection
                // having actually moved - `Ui_MoveSelection` steps over
                // unselectable rows and a pinned list stops at its ends.
                if (bits & omk::kUiConfirm)      blip(sndConfirm);
                else if (bits & omk::kUiBack)    blip(sndBack);
                else if (walk->selection() != wasSel ||
                         walk->currentList() != wasList) blip(sndMove);
            }
            if (optHost) {
                const bool onPanel = openScreen == 29 && walk->panel() &&
                                     walk->panel()->addr == 0x004CF420u;
                // the sneak's tab LEFT: the leave hook 0x0049D940 hides 35
                if (openScreen == omk::kScreenSneak && !optOnSneakTab && optMenu.focused())
                    optMenu.unfocus();
                if (onPanel && !optEntered) {
                    optEntered = true;
                    optMenu.focus();
                    std::printf("frame %ld: options: screen 35 focused (panel 0x004CF420's "
                                "enter hook 0x0047BB40), page %d\n", n, optMenu.page());
                }
                if (!onPanel) optEntered = false;
                // ---- WHAT THE APPLY HOOKS WROTE, reaching the viewer ----
                //
                // The engine's hooks write `byte_90E180` and every consumer
                // reads it when it next runs; here the block is `settings.v`
                // and the viewer's own copies are refreshed from it - unless a
                // flag on the command line holds one, which outranks the menu
                // the way it outranks the save.
                for (const int item : optMenu.takeApplied()) {
                    settings.v = optMenu.settings();
                    // a slot save writes the header too (`Game_WriteSave`'s
                    // first copy), so it must carry what the menu changed
                    if (saveSettings) saveSettings = settings.v;
                    const omk::SettingsBlock& v = settings.v;
                    switch (item) {
                    case 2:
                        if (v.screenX != dispW || v.screenY != dispH) {
                            pendingDisplay = {v.screenX, v.screenY};
                        }
                        break;
                    case 3:
                        if (clipFlag || unlimitedClip)
                            std::printf("options: clip distance %d m - held at the "
                                        "command line's / the enhancement's\n", v.clipDistance);
                        else {
                            clipInches = static_cast<double>(v.clipDistance) * omk::kInchesPerMetre;
                            clipReport = true;
                            std::printf("options: clip distance %d m = %.0f in "
                                        "(sub_41D570 re-applies it to the scene)\n",
                                        v.clipDistance, clipInches);
                        }
                        break;
                    case 4:
                        if (skyFlag < 0) drawSky = v.sky;
                        std::printf("options: sky %s%s\n", v.sky ? "on" : "off",
                                    skyFlag >= 0 ? " - held at --sky" : "");
                        break;
                    case 5:
                        if (shadowFlag < 0) drawShadows = v.shadows;
                        std::printf("options: shadows %s%s\n", v.shadows ? "on" : "off",
                                    shadowFlag >= 0 ? " - held at --shadows" : "");
                        break;
                    case 6:
                        if (!densityFlag) {
                            density = v.streetActivity;
                            session.setStreetActivity(density);
                        }
                        std::printf("options: street activity %d%s - `Slider_Init` reads it "
                                    "when the next area loads, as the game's does\n",
                                    v.streetActivity, densityFlag ? " - held at --density" : "");
                        break;
                    case 7:
                        if (detailFlag < 0) shadowDetail = v.levelOfDetail;
                        std::printf("options: level of detail %d%s\n", v.levelOfDetail,
                                    detailFlag >= 0 ? " - held at --detail" : "");
                        break;
                    case 10: case 11: case 12:
                        // read where they are played: the music every frame,
                        // the other two by the next sound to start
                        session.setMusicOption(v.volumeMusic);
                        std::printf("options: volumes - attenuation dialogue %d, music %d, "
                                    "effects %d dB\n", v.volumeDialogue, v.volumeMusic,
                                    v.volumeEffects);
                        break;
                    case 8:
                        std::printf("options: Accélération 3D -> %d - NOT switched live: the "
                                    "viewer picks its renderer at start (--software, "
                                    "--vulkan)\n", optMenu.current(8));
                        break;
                    default:
                        std::printf("options: row %d applied\n", item);
                        break;
                    }
                }
                // `Oui` on "Sauvegarder les options": `sub_4092A0`, the
                // settings-only save - the 3496 bytes over the file's head,
                // or a new file of 256 empty slots behind them
                if (optMenu.takeSaveRequest()) {
                    omk::SettingsBlock out = settings.v;
                    if (settings.bindings != omk::Settings::Source::Save) {
                        // the header carries the three binding tables verbatim,
                        // and a block that never came from a save has none
                        for (int g = 0; g < 4; ++g)
                            for (int k = 0; k < 14; ++k) {
                                const std::size_t i = static_cast<std::size_t>(g * 14 + k);
                                out.keyboard[i] = static_cast<std::uint32_t>(
                                    std::max(0, in.schemes().code(g, k, omk::Device::Keyboard)));
                                out.mouse[i] = static_cast<std::uint32_t>(
                                    std::max(0, in.schemes().code(g, k, omk::Device::Mouse)));
                                out.joystick[i] = static_cast<std::uint32_t>(
                                    std::max(0, in.schemes().code(g, k, omk::Device::Joystick)));
                            }
                    }
                    auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
                    if (file.size() < omk::kSaveFileSize) file = omk::blankSaveFile(out);
                    else omk::putSettings(file, out);
                    if (omk::writeSaveFile(savesPath, file))
                        std::printf("options: saved - the settings header written to %s\n",
                                    savesPath.c_str());
                    else
                        std::fprintf(stderr, "options: could not write %s\n", savesPath.c_str());
                }
            }
            // CLOSING THE SNEAK is not closing a script's screen, and the
            // difference is the whole reason `screenFromScript` exists.
            // `Ui_CloseSneakFamily`'s parameter-0 arm - the one that serves
            // SNEAK - closes screen 35, frees the three `.3DO` previews its
            // open loaded (`setek`, `anneau`, `imager`), raises event 26 and
            // falls into the generic close. No answer is posted anywhere,
            // because `sub_0046ADF0` opened it with a waiting context of -1:
            // nothing is parked on it. Handing `session.answerUi(-1)` to a
            // sneak close would release whatever script happened to be
            // suspended elsewhere.
            //
            // (`docs/UI.md` attributes that arm and the closing animation the
            // other way round - it reads the scene-freeing arm as VIDEOPHONE
            // and the oscillator refusal as SNEAK. The branch decides it:
            // parameter 0 is SNEAK and takes `loc_49B6A5`, the scene-freeing
            // one; the refusal is parameter 2's. Corrected there too.)
            // `dword_4C09B4`, taken before the walk is dropped.  `Charger`
            // does not load - it records a request and closes the screen, and
            // `sub_408410` consumes it at the top of the NEXT script pump.
            // Kept here and served below for the same reason: a load in the
            // middle of a screen's own dispatch would free the panel it is
            // standing in.
            if (const int req = walk->takePendingLoad(); req >= 0)
                pendingLoadSlot = req;
            // ...and the QUIT the pause screen's `Oui` asks for, taken here
            // for the same reason: `dword_4E6C9C` is a request the engine
            // serves at the top of the next `Script_Pump(1)`, not work the
            // callback does.
            if (walk->takeQuitRequest()) quitRequested = true;
            if (walk->takeExitRequest()) {
                exitProgram = true;
                std::printf("frame %ld: start menu: Quitter -> Oui - PostQuitMessage, "
                            "the program ends\n", n);
            }
            // ...and the SAVE, which the callback performs itself rather than
            // deferring: `Game_WriteSave(slot)` right after the charge, and
            // then the screen closes. So this is served here and not at the
            // pump.
            // `Detruire`'s confirm: `SaveDir_ClearSlot` (0x004090A0), which
            // is ONE zero byte over the slot's name. The day, the DB and the
            // picture stay on disk - an empty slot is an empty NAME and
            // nothing else (GAME_STATE 8b).
            if (const int slot = walk->takePendingClear(); slot >= 0) {
                auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
                if (omk::clearSaveSlot(file, slot) &&
                    omk::writeSaveFile(savesPath, file)) {
                    std::printf("detruire: slot %d cleared\n", slot);
                    loadPanelState = omk::buildLoadPanel(
                        omk::saveDirectory(loadPanelState.path, w));
                } else {
                    std::fprintf(stderr, "detruire: slot %d not cleared\n", slot);
                }
            }
            // ---- `Acheter`'s PAYMENT, carried out where the channel is ----
            //
            // `Game_HandleEvent(38, {price})` with object list 2 open is not
            // a purchase at all - it is `u16(player + 174) -= price`, with a
            // refusal when there is not enough. The walk did the test and the
            // arithmetic on its own copy; the DB is written here, the way
            // every other channel action in this file is.
            if (const int paid = walk->takeHintPurchase(); paid > 0) {
                state.setRings(std::max(0, state.rings() - paid));
                std::printf("indices: paid %d anneaux, %d left on the player "
                            "record's +174\n", paid, state.rings());
            }
            // ---- THE SLIDER'S TRAVEL --------------------------------
            //
            // `sub_40E630(row)` then `sub_452570(&point)`, and the pair is
            // three things the walk cannot do. `sub_40E630` counts ENABLED
            // destinations to the row, and when the record's `+2` AREA is not
            // the resident one it frees both slots' contexts, `Area_Load`s
            // that area, re-attaches the player, rebinds his facing matrix
            // and raises event 9 - a SYNCHRONOUS load, the same shape the
            // save path takes here, not the staged `area.goto` transition.
            // Only then does it look for a position, and it looks in the
            // newly resident chunk's ADDRESS table for the entry whose `+14`
            // equals the record's own bit.
            //
            // `sub_452570`'s ARRIVE arm is what places him: the position,
            // velocities zeroed, the facing rebuilt from his own Euler (so
            // the address's heading is NOT used), `Walk_ProbeGround`,
            // ACTOR_STATE 1, camera mode 0 and `Screen_Fade(0)` - which is
            // `Screen_StartColorFade` mode 4 over 60 frames.
            //
            // Its OTHER arm, the one that runs when a slider POOL exists,
            // reserves a real slider and fades the other way instead. That is
            // the RIDE, and it is step 2 of `todo/slider.md`; what is here is
            // the arm the engine itself takes wherever there is no circuit.
            // ---- "Appel du slider" ----------------------------------
            //
            // 0x0049D400: `sub_452570` on the player's own position, and
            // `dword_6A17CC` untouched - a call with NO destination. The only
            // reset of that global is in the new-game path (`sub_49B400`),
            // so a destination chosen earlier would still be remembered by
            // the engine; the port keeps that faithfully rather than
            // clearing it here.
            if (walk->takeCallHere()) {
                float me[3] = {session.playerPos()[0], session.playerPos()[1],
                               session.playerPos()[2]};
                if (session.sliders().callSlider(me))
                    std::printf("slider: Appel du slider - a slider is COMING "
                                "to %.0f %.0f %.0f, no destination\n",
                                me[0], me[1], me[2]);
                else
                    std::printf("slider: Appel du slider, but no vehicle lane "
                                "here - the call FAILS (text 42)\n");
            }
            // ---- "Manuelle" -----------------------------------------
            //
            // 0x0049D4A0: `sub_457040(slider, player)` - ACTOR_STATE 7 and
            // `Slider_TickRide` takes the body. The flight model, from where
            // the vehicle stands.
            if (walk->takeManual() && boarded) {
                float at[3];
                if (session.sliders().calledAt(at)) {
                    omk::SliderRide r;
                    r.x = at[0]; r.y = at[1]; r.z = at[2];
                    r.yaw = session.sliders().calledYaw();
                    ride = r;
                    std::printf("slider: Manuelle - `sub_457040`, the controls "
                                "are his\n");
                }
            }
            if (const int row = walk->takeTravel(); row >= 0) {
                std::vector<const omk::Destination*> known;
                for (const auto& d : destinations)
                    if (state.bit(omk::StateArray::AddressEnabled, d.bit))
                        known.push_back(&d);
                if (row >= static_cast<int>(known.size())) {
                    std::printf("slider: row %d is past the %zu enabled "
                                "destinations\n", row, known.size());
                } else if (!walk->travelIsJourney()) {
                    // ---- THE SNEAK'S PAGE CALLS ONE ---------------------
                    //
                    // Screen 9's `param` is 0, so `sub_49BC60` takes the arm
                    // that uses the PLAYER'S own position: a slider is armed
                    // and comes to him, and the row he chose is remembered
                    // (`dword_6A17CC = tag`) for the arrival camera. The
                    // JOURNEY is a second confirm, on screen 7, once he is
                    // aboard - which is why `MDSLIDIN` ends in
                    // `UI_OpenScreen(7, ...)`.
                    const auto* d = known[static_cast<std::size_t>(row)];
                    calledDestination = row;
                    float me[3] = {session.playerPos()[0], session.playerPos()[1],
                                   session.playerPos()[2]};
                    if (session.sliders().callSlider(me))
                        std::printf("slider: '%s' chosen - a slider is COMING "
                                    "to %.0f %.0f %.0f. Wait for it, then walk "
                                    "to it and press the action button\n",
                                    d->name.c_str(), me[0], me[1], me[2]);
                    else
                        std::printf("slider: '%s' chosen, but there is no "
                                    "vehicle lane here - the call FAILS, which "
                                    "is what the engine does too (text 42)\n",
                                    d->name.c_str());
                } else if (boarded && known[static_cast<std::size_t>(row)]->area
                                       == session.residentSlot(session.activeSlot()).area) {
                    // ...the RESIDENT area, not `state.currentArea()`: the
                    // drive is possible exactly when the destination is in
                    // the world the circuit belongs to, and the harness's
                    // `--area` leaves the DB's current area at the save's
                    // (237) while the world is Anekbah (0). Compared against
                    // the DB, every journey in the fixture took the
                    // other-area arm and was placed instead of driven.
                    // ---- THE JOURNEY, inside this area --------------------
                    //
                    // Screen 7's row confirm: `sub_40E630(tag)` finds the
                    // address, `sub_452570` with a slider already assigned
                    // sets it to state 6, and `sub_456530` drives it to the
                    // lane nearest that address. He is on it the whole way.
                    const auto* d = known[static_cast<std::size_t>(row)];
                    const auto& rs = session.residentSlot(session.activeSlot());
                    const omk::Address* ad = nullptr;
                    for (const auto& x : rs.addresses) if (x.id == d->bit) ad = &x;
                    if (ad && session.sliders().sendCalledTo(ad->pos)) {
                        journeyTo = d->bit;
                        std::printf("slider: JOURNEY to '%s' - state 6, driving "
                                    "to the lane nearest address %d\n",
                                    d->name.c_str(), d->bit);
                    } else {
                        std::printf("slider: '%s' has no road within reach - "
                                    "the journey FAILS (text 42)\n", d->name.c_str());
                    }
                } else {
                    // ---- THE JOURNEY, to another AREA -------------------
                    //
                    // `sub_40E630` loads the area first and only then looks
                    // for a lane; the circuit changes under the vehicle. Not
                    // driven here: the port loads the area and places him,
                    // which is the arrive arm, and says so.
                    const auto* d = known[static_cast<std::size_t>(row)];
                    const int wasArea = state.currentArea();
                    // ...FROM ABOARD, and it is not a bare placement: after
                    // `sub_40E630`'s load, `sub_452570` runs against the NEW
                    // pool - the lane nearest the destination, `sub_452CC0`
                    // relinking a vehicle THERE (not at the top of the lane),
                    // state 6 - so `sub_456530` case 6 finds it within its 117
                    // at once and it ARRIVES: the stop, the exit clip, camera
                    // 17, the release. This dropped him at the address bare.
                    const bool wasAboard = boarded;
                    if (d->area != wasArea) {
                        state.setCurrentArea(static_cast<std::int16_t>(d->area));
                        session.loadArea(d->area);
                    }
                    bool arrived = false;
                    if (wasAboard) {
                        const auto& rs2 = session.residentSlot(session.activeSlot());
                        const omk::Address* ad2 = nullptr;
                        for (const auto& x : rs2.addresses) if (x.id == d->bit) ad2 = &x;
                        if (ad2 && session.sliders().arriveAt(ad2->pos)) {
                            journeyTo = d->bit;
                            arrived = true;
                            std::printf("slider: JOURNEY to '%s' in area %d - loaded, "
                                        "the slider relinked at the lane nearest address "
                                        "%d with him aboard, state 6\n",
                                        d->name.c_str(), d->area, d->bit);
                        } else {
                            session.sliders().dismountCalled();
                            boarded = false;
                            std::printf("slider: '%s' - area %d has no road within reach "
                                        "of address %d; placing him instead\n",
                                        d->name.c_str(), d->area, d->bit);
                        }
                    }
                    // The address whose `+14` is this record's own bit - the
                    // one number that joins the two tables.
                    const bool changed = d->area != wasArea;
                    const bool placed = arrived ? true : session.placeActorAt(d->bit);
                    session.requestCamera(0, 0);
                    // `Screen_Fade(0)` is `fade.from_black` - it CLEARS the
                    // load's black at the exit (case 8 calls it every tick of
                    // H_SLDOUT), not a sixty-frame dip. The dip was the old
                    // bare placement's, and a reader saw it on the aboard
                    // path: "a fade effect that shouldn't be here".
                    if (!arrived) session.startColourFade(4, 0u, 60.0f);
                    // ONLY WHEN THE AREA ACTUALLY CHANGED. Dropping
                    // `playerReady` asks the hand-over gate to build the
                    // player again for a new area's set, which is right after
                    // a load and WRONG without one: nothing rebuilds him, so
                    // there is no player left at all and control goes with
                    // him. A reader met exactly that - *"calling a slider
                    // with the sneak teleports me and makes the character
                    // disappear"* - and every headless test of this path had
                    // taken the other branch, because the fixture save's area
                    // is 237 and every destination is in 0, 1, 64 or 101.
                    // Travelling INSIDE the city you are standing in is the
                    // common case and was the untested one.
                    // ONLY WHEN THERE IS NO CONTROLLER YET. The hand-over gate
                    // that rebuilds him and sets `playerReady` runs on
                    // `!player`; with a controller already alive an area
                    // change keeps him ("he keeps walking", the walk-through
                    // path above). Dropping `playerReady` here with `player`
                    // alive left it false for ever, and the model eviction
                    // keeps his model only while `playerReady` - so in a city
                    // where no staged actor wears HO1_FN (Qalisar) Kay'l was
                    // thrown away on the first frame. The reader, twice:
                    // *"the character disappearing"*.
                    if (changed && !player) {
                        playerReady = false; adventure = false;
                        forceAdventure = true;
                    }
                    if (player && !arrived) {
                        const float at[3] = {session.playerPos()[0],
                                             session.playerPos()[1],
                                             session.playerPos()[2]};
                        player->placeAt(at, session.playerYaw());
                    }
                    if (!arrived)
                    std::printf("slider: '%s' - area %d -> %d, address %d %s"
                                " at %.0f %.0f %.0f facing %.0f\n",
                                d->name.c_str(), wasArea, d->area, d->bit,
                                placed ? "placed him" : "IS NOT IN THAT AREA",
                                session.playerPos()[0], session.playerPos()[1],
                                session.playerPos()[2], session.playerYaw());
                }
            }
            if (const int slot = walk->takePendingSave(); slot >= 0) {
                // ONE RING, through `Actor_GetProperty` / `Actor_SetProperty`
                // (events 44 and 45, property 5) exactly as the callback
                // does - and only when it has one to spend, which is the
                // `jz` that skips the decrement without skipping the write.
                const int had = state.rings();
                if (had > 0) state.setRings(had - 1);
                state.setCurrentArea(static_cast<std::int16_t>(session.activeArea()));
                state.setCurrentScene(static_cast<std::int16_t>(
                    session.residentSlot(session.activeSlot()).scene));
                state.setPlacement(session.playerPos(), session.playerYaw());
                omk::SaveSlot out;
                out.name = loadedName.empty() ? std::string("OMK") : loadedName;
                out.day  = state.clockDay();
                out.time = state.clock();
                out.state = state;
                const auto thumb = omk::thumbFromRgb565(fb.px, fb.w, fb.h);
                auto file = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
                if (file.size() < omk::kSaveFileSize)
                    file = omk::blankSaveFile(saveSettings ? *saveSettings
                                                           : omk::defaultSettingsBlock());
                if (saveSettings) omk::putSettings(file, *saveSettings);
                if (omk::writeSaveSlot(file, slot, out, thumb) &&
                    omk::writeSaveFile(savesPath, file))
                    std::printf("save: slot %d written - '%s', %s %s, area %d "
                                "scene %d; %d anneau%s left\n", slot,
                                out.name.c_str(), omk::formatDate(out.day).c_str(),
                                omk::formatTime(out.time).c_str(),
                                state.currentArea(), state.currentScene(),
                                state.rings(), state.rings() == 1 ? "" : "x");
                else
                    std::fprintf(stderr, "save: slot %d could not be written\n", slot);
            }
            const bool leaving = walk->answer() >= 0 || walk->closed();
            // THE KEY THAT CLOSED THE SCREEN IS NOT THE WORLD'S ACTION.
            //
            // The mirror of the gate at the open. The world's action button
            // is HELD rather than edged - "spent until the bit goes up" - but
            // while a screen is up the world never reaches that gate, so
            // `actionSpent` is still false when the screen closes. The Enter
            // that pressed `Annuler` is then read as a fresh press on the
            // save point the player is standing on, and the menu reopens at
            // once. A reader met this here and had met it elsewhere in the
            // game: "it is like the enter pressed event continues to be
            // triggered while the button is not released".
            //
            // Spending it costs nothing when the key is already up: the gate
            // above clears `actionSpent` on the first frame the bit is not
            // held.
            if (leaving || walk->answer() >= 0 || walk->closed())
                actionSpent = true;
            // THE PAUSE SCREEN CLOSES ON ITS OWN TERMS, and they are not
            // the sneak's. Its close callback (0x004ADEB0) clears the pause
            // flag, restores the sound volume it saved at the open, undoes
            // the four subsystem pauses and `Sleep`s 500 ms; it raises no
            // event 26 and frees no object list, because nothing was opened.
            // Sending it through the branch below would have raised the
            // sneak's close event and shut a list that was never open.
            if (leaving && openScreen == kScreenPause) {
                std::printf("screen %d PAUSE GAME closed - the world runs "
                            "again\n", openScreen);
                walk.reset();
                openScreen = -1;
                screenFromScript = true;
            } else if (leaving && !screenFromScript) {
                std::printf("screen %d closed by the player - event %d, object "
                            "list %d\n", openScreen, omk::kEventSneakClose,
                            inv.openedList());
                inv.closeList();          // Game_RaiseEvent(26, 0)
                walk.reset();
                openScreen = -1;
                screenFromScript = true;
            } else if (walk->answer() >= 0) {
                const int uiVar = session.pendingUiVar();
                session.answerUi(walk->answer());
                // printed from what the SESSION stored, not from the answer
                // handed over: the two differ whenever the write misses
                std::printf("screen %d answered %d -> the script resumes"
                            " (variable %d now %d)\n", openScreen, walk->answer(),
                            uiVar, uiVar >= 0 ? int(state.var(uiVar)) : -1);
                walk.reset();
                openScreen = -1;
            } else if (walk->closed()) {
                // The player LEFT the screen without choosing. `UI_OpenScreen`
                // parks the caller and presets the answer `dword_930750` to
                // -1; every close path - `UI_SendAnswer` and the two ESC/TAB
                // closes in 21_d3d.c (0x4034xx, 0x4035xx) - posts event 5
                // with whatever it holds, and `Game_HandleEvent` case 5 does
                // `Var_Set(var, answer)` and writes status 1 regardless. So
                // leaving IS an answer, -1, and the script resumes with it -
                // AREA 118's takes that branch as the unseeded opening. This
                // used to `break` out of the loop, a quit the engine has no
                // counterpart for.
                std::printf("screen %d closed without an answer -> -1, "
                            "the script resumes\n", openScreen);
                // A SHOP's close callback is `Ui_CloseShop` (0x004AE7E0):
                // `Game_RaiseEvent(26, 0)` and then the generic close. Case 26
                // clears the open list only when it is list 0 - the bank's.
                // MULTIPLAN's close `0x004B02D0` is the same two calls - and
                // nothing on screen 2 writes `dword_930750`, so its thirteen
                // answer-keeping sites always read back this -1.
                if ((openScreen >= 20 && openScreen <= 28) || openScreen == 32 ||
                    openScreen == 2) {
                    const int was = inv.openedList();
                    inv.closeList();          // Game_RaiseEvent(26, 0)
                    std::printf("screen %d close: event 26, open list %d -> %d\n",
                                openScreen, was, inv.openedList());
                }
                session.answerUi(-1);
                walk.reset();
                openScreen = -1;
            }
            // ...AND THE SNEAK CALL CLOSES WHEN THE CALL IS OVER.
            //
            // What is READ: `Ui_CloseSneakFamily`'s param-2 arm refuses the
            // first attempt - it clears `dword_670BF0`, resumes the player
            // (`sub_466B60`) and starts oscillator 5 for 100 ms, a closing
            // ANIMATION - and closes on the next one. What is NOT read is
            // what makes the attempt: no VM opcode closes a screen, no other
            // screen's open closes this one (only `UI_LoadScreen(34)`, SHOOT
            // HUMAN, does), and the function has no direct caller because it
            // is a dword in the screen table.
            //
            // So this is a RECONSTRUCTION, and it is labelled as one: the
            // call closes when the conversation opened over it ends. It is
            // what the script requires - SCENE 53 runs `dialog.start 386`
            // over the device and then `dialog.start 388` in the world, and
            // 388 cannot play through a videophone - and what a capture
            // shows. The alternative, that the player presses a key to hang
            // up, is not excluded by anything here.
            //
            // A CALL IS A CONVERSATION **OR** A VOICE-OVER. Two of the ten
            // sites are `media.play 534` rather than `dialog.start`, and a
            // close keyed on the dialogue alone would leave those two on
            // screen for ever.
            if (walk && openScreen == kScreenVideophone && videophoneCall) {
                if (session.dialogOpen() || mediaTextFrames > 0)
                    videophoneSpoke = true;
                else if (videophoneSpoke) {
                    std::printf("screen %d VIDEOPHONE: the call is over - "
                                "closing (reconstruction: the engine's own "
                                "trigger is not read)\n", openScreen);
                    walk.reset();
                    openScreen = -1;
                    screenFromScript = true;
                    videophoneCall = videophoneSpoke = false;
                }
            }
        }

        mark("input, ui, shoot");
        done = true;
    } while (false);
    return done ? -1 : -2;
}

int PlayState::modesQuitLoad() {
    OMK_ZONE("modes: quit load");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    bool done = false;
    do {
        // ---- THE QUIT `Quitter le jeu` ASKED FOR, served between pumps
        //
        // `Script_Pump(1)` (0x00407DC0) opens with
        //
        //     if (dword_4E6C9C) { Script_Pump(3);      // tear the game down
        //                         dword_4E6C9C = 0;
        //                         Script_Pump(2);      // Game_NewGame
        //                         Screen_FadeFromColor(0xFFFFFF, 15, 0); }
        //
        // and `Script_Pump(2)` is `Game_NewGame` (0x0040E060) - reset the
        // session, load `IAM\START` over a zeroed DB, apply it. So the
        // engine does NOT exit here: `Quitter le jeu` ends the GAME and
        // starts a fresh one, which walks straight back out through AREA
        // 118's startup script into `ui.open 29`, the start menu. Quitting
        // the PROGRAM is the start menu's own `Quitter`, a different item on
        // a different screen.
        //
        // **And so does the port, since 2026-10-05** (`todo/drift-audit.md`
        // S3 and T1). It ended the run here, because `omk-play`'s boot was not
        // something that could run twice; `game.restart` (op 152) needed the
        // same restart and `Session::restart` now serves it - both pools freed,
        // `IAM\START`, AREA 118 - while the frontend drops the old world
        // (`Session::restarts()`). This is the SAME flag op 152 writes, so the
        // pause screen's `Oui` and the sneak's quit tab are `requestRestart()`
        // and the next pump does the rest.
        //
        // It is also the ONLY way the original loads in the middle of a
        // session: screen 30 hides `Charger` (`Ui_BuildLoadPanel`) and the
        // pause menu has no load, so a load mid-game is quit -> the start
        // menu -> `Charger`, after the restart has already dropped the player.
        // The pending load below therefore never meets a live controller -
        // the state `todo/drift-audit.md` T1 was written about.
        if (quitRequested) {
            quitRequested = false;
            session.requestRestart();
            std::printf("frame %ld: `Quitter le jeu` confirmed - dword_4E6C9C = 1, the "
                        "restart served at the next pump\n", n);
        }

        // ---- THE PENDING LOAD, served the way `sub_408410` serves it
        //
        //     if (!dword_4E6C7C && dword_4C09B4 != -1) {
        //         v1 = playerActorRec[+396];
        //         Game_LoadSave(dword_4C09B4);
        //         dword_4C09B4 = -1;
        //         playerActorRec[+396] = v1;
        //         Screen_FadeFromColor(0xFFFFFF, 15, 0);
        //     }
        //     Script_Pump(...)
        //
        // - so it happens BETWEEN pumps, one-shot, and the screen is already
        // gone by then.  `Game_LoadSave` is the slot's name, day, time and DB
        // into `State_Apply`, which relocates, copies the header's scene into
        // the scene-per-area table and calls `Area_Load` (GAME_STATE 5a).
        // `if (!dword_4E6C7C && dword_4C09B4 != -1)` - and `dword_4E6C7C` is
        // the BOOT STARTUP CONTEXT (`bootCtx_`, AREA 118's own `+4` script).
        // So a load WAITS while that script is still running, which is what
        // gives the Grid sequence time to play: `Charger` answers 0, the
        // script's `var19 == 0` arm flies the cameras and ends, the context
        // frees, and only then does the save land. Serve it any earlier and
        // the fly-through is replaced by the apartment a frame later, which
        // is what a first version did.
        if (pendingLoadSlot >= 0 && session.bootContext() < 0) {
            const int slotNo = pendingLoadSlot;
            pendingLoadSlot = -1;
            const auto bytes = omk::readSaveFile(savesPath, fr + "/IAM/GAMES");
            if (const auto sl = omk::readSaveSlot(bytes, slotNo)) {
                state = sl->state;
                state.setClockDay(sl->day);
                state.setClock(sl->time);
                // `State_Apply`'s first act, before `Area_Load` reads it back
                state.setSceneOfArea(state.currentArea(), state.currentScene());
                state.placementWorld(savedAt, savedYaw);
                haveSavedPlacement = state.playerActorId() != -1;
                loadedName = sl->name;
                session.loadArea(state.currentArea());
                if (haveSavedPlacement)
                    session.setPlayerPosition(savedAt, savedYaw);
                session.requestCamera(0, 0);
                // the hand-over gate rebuilds the player for the new area,
                // on the same terms `--slot` gets: a resume does not wait for
                // the scene's beats to finish before handing over
                playerReady = false; adventure = false;
                forceAdventure = true;
                // `Screen_FadeFromColor(0xFFFFFF, 15, 0)` is
                // `Screen_StartColorFade(2, 0xFFFFFF, 15, 0)` - mode 2, the
                // "from" arm, fifteen frames, and in WHITE rather than the
                // black every other fade in the game uses.
                session.startColourFade(2, 0xFFFFFFu, 15.0f);
                std::printf("load: slot %d '%s', %s %s, area %d scene %d, "
                            "standing at %.0f %.0f %.0f facing %.0f\n",
                            slotNo, sl->name.c_str(),
                            omk::formatDate(sl->day).c_str(),
                            omk::formatTime(sl->time).c_str(),
                            state.currentArea(), state.currentScene(),
                            savedAt[0], savedAt[1], savedAt[2], savedYaw);
            } else {
                std::fprintf(stderr, "load: slot %d cannot be read\n", slotNo);
            }
        }

        // A NEW GAME UNDER US - `game.restart` (op 152) served by the pump:
        // the Session has freed both slots and booted `IAM\START` again, so
        // nothing this frontend holds for the old world stands. The player
        // controller, every staged and parked body, a fight, a ride and a
        // parked move go; the hand-over rebuilds the player when the new
        // world gives one, as at boot (todo/drift-audit.md S3).
        if (session.restarts() != restartsSeen) {
            restartsSeen = session.restarts();
            if (fightRun.active) {
                fightRun.active = false;
                dropLibrary(fightRt);
                fightRun.fight.reset();
                fightRun.foeChannel.reset();
            }
            fightRun.body = nullptr;
            ride.reset();
            player.reset();
            playerReady = false; adventure = false; forceAdventure = false;
            staged.clear();
            parked.clear();
            moveWaitCtx = -1; moveWaitGroup = -1;
            in_->installScheme(0);
            ++poolComposition;
            std::printf("frame %ld: restart - the frontend dropped the player, the "
                        "bodies and any mode; area %d boots\n", n, session.currentArea());
        }

        if (session.areasEntered() != lastArea) {
            lastArea = session.areasEntered();
            if (lastArea > 0) std::printf("area transition %d\n", lastArea);
        }

        // WHICH `.SCX` IS RESIDENT, every time it changes. A transition is
        // supposed to swap it (`Session::reloadScene`), and if it does not
        // the destination's `scx.play*` have no objects to start - every one
        // of its bodies then falls back to the bank idle, or for a character
        // with no bank to the REST pose, standing on his 20-byte placement
        // record instead of his program's path.
        {
            static std::string scxWas = "-";
            const std::string now = session.scene().loaded() ? session.scene().file() : "(none)";
            if (now != scxWas) {
                scxWas = now;
                std::printf("[scx] frame %ld  resident scene is now %s (%zu objects)\n", n,
                            now.c_str(),
                            session.scene().loaded()
                                ? session.scene().scene().scene().objects.size() : 0u);
            }
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}

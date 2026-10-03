// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the scripted objects start to play.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupPlay() {
    const auto& fs = *fs_;
    // ---- and now they PLAY -------------------------------------------
    //
    // Three MPEG-1 program streams at 320x240, doubled to the framebuffer.
    // `movies` is the boot chain's own order. LMENU skips them, which is the
    // engine's key and not this frontend's invention.
    // A frame budget is for the headless checks, and there the budget must
    // reach the SCREEN - otherwise the dump is a movie frame and the
    // live-vs-reference comparison compares the wrong thing. `--nofmv` is the
    // engine's own switch and is what the checks pass; this only guards the
    // case where someone bounds the frames and forgets.
    if (playMovies && !frames) {
        const char* movies[3] = {"FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg",
                                 "FLIS/GAME.mpg"};
        omk::Surface mv(dispW, dispH, 0);
        bool skipAll = false, bounded = false;
        for (const char* name : movies) {
            if (skipAll) break;
#if defined(__vita__)
            // THE HARDWARE PATH (`backends/vita/avmovie.h`): the film converted
            // to H.264 by `scripts/vita-movies.sh` and copied to
            // ux0:data/omk/movies/<NAME>.mp4 plays on the Vita's decoder -
            // same skip rules, same audio queue, same present. Without the
            // file, the software decoder below, as before.
            {
                std::string stem = name;
                if (const auto sl = stem.find_last_of('/'); sl != std::string::npos) stem = stem.substr(sl + 1);
                if (const auto dt = stem.rfind('.'); dt != std::string::npos) stem = stem.substr(0, dt);
                for (auto& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                std::string where;
                const std::string mp4 = omk::vita::avFind(stem, fr, where);
                if (omk::vita::AvFilm* av = mp4.empty() ? nullptr : omk::vita::avOpen(mp4)) {
                    std::printf("  %s: hardware decoder, %s\n", name, mp4.c_str());
                    front.openAudio(44100, 2);
                    omk::Surface film(320, 240, 0);
                    std::vector<float> pcm;
                    int rate = 0;
                    long shownAv = 0;
                    // NOT ACTIVE YET is not FINISHED: on a console the player
                    // buffers after `sceAvPlayerAddSource` and only then
                    // reports active, so testing `avActive` first ended every
                    // film at once - "0 frames shown, sound at 0 Hz" for all
                    // three (console log 2026-09-18). Vita3K is active at once,
                    // which is why it played there. Wait up to 3 s for the
                    // start; after that, inactive means the end.
                    const std::uint32_t avStart = front.ticksMs();
                    bool avStarted = false;
                    const char* avEnd = "the film ended";
                    for (;;) {
                        if (omk::vita::avActive(av)) avStarted = true;
                        else if (avStarted || front.ticksMs() - avStart > 3000) {
                            if (!avStarted) avEnd = "the player never became active (3 s)";
                            break;
                        }
                        omk::HostInput h;
                        if (!front.pump(h)) { skipAll = true; avEnd = "the window closed"; break; }
                        if (!h.held.empty() || h.pad.buttons != 0) {
                            if (h.held.count(0x38)) skipAll = true;      // DIK_LMENU
                            avEnd = h.pad.buttons != 0 ? "skipped by a pad button" : "skipped by a key";
                            break;
                        }
                        // THE SOUND IS THE PLAYER'S CLOCK. SceAvPlayer times
                        // its pictures by the sound taken from it, and this
                        // took up to sixteen chunks a pass as fast as they
                        // came - so the clock ran ahead, the picture with it,
                        // and the sound queued here and played at its own
                        // speed: "the video is played accelerated while the
                        // audio is played at normal speed" (the reader,
                        // 2026-09-23, on the first console run that played a
                        // film). Taken only while less than a quarter second
                        // waits in the device, it is taken as fast as it plays.
                        pcm.clear();
                        if (front.queuedSeconds() < 0.25 &&
                            omk::vita::avAudio(av, pcm, rate) && !pcm.empty())
                            front.queueAudio(pcm);
                        if (omk::vita::avVideo(av, film)) { present(film); ++shownAv; }
                        else front.delayMs(2);
                    }
                    omk::vita::avClose(av);
                    front.flushAudio();
                    for (int guard = 0; guard < 300; ++guard) {
                        omk::HostInput h;
                        if (!front.pump(h)) break;
                        if (h.held.empty() && h.pad.buttons == 0) break;
                        front.delayMs(10);
                    }
                    std::printf("  %s: %ld frames shown, sound at %d Hz - %s\n", name, shownAv, rate, avEnd);
                    continue;
                }
                // SAID: a console log that showed only the software path gave
                // no way to tell a missing copy from a player that refused it
                // (avOpen names its own failures)
                if (mp4.empty())
                    std::printf("  %s: no %s.mp4 found - decoding the MPEG-1 in software "
                                "(scripts/vita-movies.sh makes one). Looked in:%s\n",
                                name, stem.c_str(), where.c_str());
            }
#endif
            const auto real = fs.resolve(name);
            omk::Movie mov;
            if (!real || !mov.open(*real)) {
                std::printf("  %s: not decodable, skipped\n", name);
                continue;
            }
            std::printf("  %s %dx%d %.2f s\n", name, mov.info().width,
                        mov.info().height, mov.info().duration);
            // 44100 stereo, the stream's own rate - NOT the engine's 22050
            // primary, which these never went through (the world reopens the
            // device at that rate after them).
            const bool audioOk =
                front.openAudio(mov.info().sampleRate ? mov.info().sampleRate : 44100, 2);
            const double fps = mov.info().framerate > 0 ? mov.info().framerate : 30.0;
            // THE CLOCK THE PICTURE FOLLOWS: what the audio device has
            // PLAYED - decoded minus still queued - when there is a device,
            // and the wall clock when there is none. Without that second
            // half a run with no audio device measures "heard" as the
            // DECODER's position (nothing is queued, so nothing is
            // subtracted), which races ahead of real time, and the
            // frame-dropping below would discard nearly every frame.
            const std::uint32_t movieStart = front.ticksMs();
            const auto heardSeconds = [&] {
                return audioOk ? mov.audioSeconds() - front.queuedSeconds()
                               : (front.ticksMs() - movieStart) / 1000.0;
            };
            long shown = 0;
            // THE FILM AT ITS OWN SIZE on a GPU present: the GLES pass fits a
            // surface to the window itself, so scaling 320x240 up to the
            // display on the CPU first was pure cost - on a Vita, most of a
            // frame (`todo/vita-port.md`). The SDL upload path keeps the
            // display-sized surface: its texture is the window's size.
            omk::Surface film(mov.info().width, mov.info().height, 0);
            omk::Surface& target = glRen ? film : mv;
            // A FRAME THAT IS LATE IS DECODED AND DROPPED. The sound runs at
            // its own rate on the audio device; the loop used to wait when the
            // picture was early and never catch up when it was late, so on a
            // slow CPU the picture crawled behind a sound at normal speed.
            const auto late = [&] {
                if (frames) return false;
                return heardSeconds() - (shown + 1) / fps > 1.0 / fps;
            };
            long dropped = 0;
            int droppedInARow = 0;
            for (;;) {
                // At most THREE drops in a row. When decoding one frame costs
                // more than a frame lasts - a Vita, where only the SOUND of the
                // films came through - "late" never clears, and dropping every
                // late frame dropped them all. This shows one frame in four at
                // worst: a slower picture, still in step with the sound.
                const bool behind = droppedInARow < 3 && late();
                if (behind ? !mov.skipFrame() : !mov.nextFrame(target)) break;
                omk::HostInput h;
                if (!front.pump(h)) { skipAll = true; break; }
                // `docs/BOOT.md` 2: ANY key ends the movie playing, and left
                // ALT latches and ends all three. Accepting only ALT and ESC -
                // which is what this did - means a player pressing space or
                // return sits through the whole thing.
                // ...and ANY PAD BUTTON too: a pad's buttons are the game's
                // joystick, not keys in `held` (only START is), so on a Vita
                // nothing but START could skip - and a film drawing slowly
                // polls it seldom.
                if (!h.held.empty() || h.pad.buttons != 0) {
                    if (h.held.count(0x38)) skipAll = true;      // DIK_LMENU
                    break;
                }
                // THE SOUND IS DECODED HALF A SECOND AHEAD OF THE PICTURE, not
                // to the end of the film (2026-09-23). This drained
                // `nextAudio` until it came back empty, and pl_mpeg's audio
                // decoder reads on through the whole stream - so the FIRST
                // frame decoded all 107 s of `GAME.mpg`'s sound into the
                // device queue, and the demuxer buffered every video packet it
                // stepped over on the way. A desktop has the memory to hide
                // that; the Vita, on its MPEG-1 fallback (no H.264 copy), asked
                // for 72 MB in one piece and died of `bad_alloc` in Vita3K.
                // `heardSeconds` is decoded minus still queued, so the pacing
                // reads the same clock either way.
                constexpr double kAudioAhead = 0.5;
                while (mov.audioSeconds() < (shown + 1) / fps + kAudioAhead) {
                    const auto blk = mov.nextAudio();
                    if (blk.empty()) break;
                    front.queueAudio(blk);
                }
                ++shown;
                if (behind) { ++dropped; ++droppedInARow; continue; }
                droppedInARow = 0;
                present(target);

                // PACE BY THE AUDIO, not by a fixed delay. Sleeping 1000/fps
                // after each frame adds the DECODE time to every frame, so the
                // picture falls steadily behind a soundtrack that plays at its
                // own rate. The audio device is the only clock running at the
                // rate a person hears: what has been decoded, minus what is
                // still queued, is the moment being heard now. Wait only while
                // the picture is ahead of it - and when it is behind, `late`
                // above drops frames until it is not.
                if (!frames) {
                    const double ahead = shown / fps - heardSeconds();
                    if (ahead > 0.001 && ahead < 1.0)
                        front.delayMs(static_cast<std::uint32_t>(ahead * 1000.0));
                }
                if (frames && mov.framesDecoded() >= frames) {
                    skipAll = bounded = true; break;
                }
            }
            // Whatever the decoder ran ahead into is still in the device, and
            // a skipped movie must not go on playing under what follows.
            front.flushAudio();
            // the button that skipped this film must come UP before the next
            // one starts, or one press skips them all
            for (int guard = 0; guard < 300; ++guard) {
                omk::HostInput h;
                if (!front.pump(h)) break;
                if (h.held.empty() && h.pad.buttons == 0) break;
                front.delayMs(10);
            }
            if (dropped)
                std::printf("  %s: %ld of %ld frames dropped to keep up with the sound\n",
                            name, dropped, shown);
        }
        std::printf("movies %s\n",
                    !skipAll        ? "played (any key skips one, ALT skips all)"
                    : bounded       ? "cut short by --frames"
                                    : "skipped (ALT)");
    }
    // THE AUDIO DEVICE IS THE WORLD'S TOO. `openAudio` was called in one
    // place - the movie player, at the movie's own rate - so a run with
    // `--nofmv` (or a street start, which skips the movies) never opened it
    // and every world sound was dropped without a word: no music, no
    // effects, no voices. A reader on 2026-09-04: "there is absolutely no
    // sound at all". The world plays at the primary's 22050 (`kDeviceRate`),
    // and a device the films opened at their 44100 is REOPENED at it.
    if (!frames) {
        if (front.reopenAudio(kDeviceRate, 2))
            std::printf("audio: device open at %d Hz stereo for the world\n", kDeviceRate);
        else
            std::printf("audio: NO DEVICE (%s) - the world will be silent\n", front.lastError().c_str());
    }


    return -1;
}

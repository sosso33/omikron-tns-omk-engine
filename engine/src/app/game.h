// SPDX-License-Identifier: GPL-3.0-or-later
// THE GAME'S STATE - `play.cpp`'s `main`'s ~550 locals, gathered by `todo/play-split.md` S3 one group at a time, each group the
// state of one of the setup's own banners. `main` keeps a REFERENCE under
// every old name, so the code that uses them did not change; the phases that
// later move out of the loop take a `Game&` instead of a hundred arguments.
//
// No SDL here, and nothing that draws: a group is plain engine state.
#pragma once

#include "audio/music.h"
#include "audio/voiceover.h"
#include "formats/adpcm.h"

#include <vector>

namespace omk {

// THE DEVICE RATE, and it is the ORIGINAL'S: `Sound_Init` (`sub_46C3A0`) sets
// the DirectSound primary buffer to 22050 Hz / 16-bit / stereo, and what the
// game ships is 22050 too - 61 of its 63 WAVs (the other two 22080), the voice
// lines' ADPCM, the music tracks. Left at the films' 44100 (they are 44100
// MP2 and open it first), the device would stretch every world sound 2x on
// the way in, music per output frame and each voice line whole on the main
// thread (a console's 91-132 ms at a line's start,
// `optimization.md` step 28, rows g/i/m). The films keep their 44100 - the
// original played them through DirectShow, whose output never met that
// primary (`docs/RECONSTRUCTION.md` 2026-09-01) - and the device is REOPENED
// at 22050 for the world after them (`Frontend::reopenAudio`).
constexpr int kDeviceRate = 22050;

// AUDIO - the setup's "INTERFACE SOUNDS" and "THE MUSIC" banners: the screens'
// positional sounds (by `sub_482FE0`'s input bits: 0 the confirm, 1 the back,
// 2 the selection move, 3 the close - `UiWidgets::kSound*`), the
// music stream, and the cutscene voices (`media.play`, one at a time).
// Not copyable: `voices` points at `voiceLib`.
struct AudioState {
    std::vector<float> sndMove, sndConfirm, sndBack, sndClose;
    std::vector<float> optSndMove, optSndConfirm, optSndBack;   // screen 35's own
    AdpcmTables adpcmTables;
    MusicPlayer music{kDeviceRate};
    int playingTrack = -1;
    VoiceOverLibrary voiceLib;
    VoiceOverPlayer voices{voiceLib};
    int voiceOverShot = -1;

    AudioState() = default;
    AudioState(const AudioState&) = delete;
    AudioState& operator=(const AudioState&) = delete;
};

struct Game {
    AudioState audio;
};

}  // namespace omk

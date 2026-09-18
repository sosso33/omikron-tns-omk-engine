// SPDX-License-Identifier: GPL-3.0-or-later
// THE INTRO FILMS ON THE VITA'S HARDWARE DECODER - SceAvPlayer
// (`todo/vita-port.md`).
//
// The films are MPEG-1 and `pl_mpeg` decodes them in software, which on a
// Vita is far slower than real time (a console log: 41 of EIDOS's 386 frames
// shown). The Vita decodes H.264 in hardware, so `scripts/vita-movies.sh`
// converts the three films once, on the desktop, and this plays the result:
// the player decodes into GPU memory, a frame is converted from its YUV planes
// to the game's RGB565 on the CPU (the films are 320x240 - 77k pixels, cheap)
// and handed to the same present every other frame takes; the sound comes out
// as PCM for the same audio queue. So nothing downstream of the frame knows
// which decoder made it.
//
// Declared in `play.cpp` rather than included (the pattern the other Vita
// hooks use), and only ever called under `__vita__`.
#pragma once

#include "ui/surface.h"

#include <string>
#include <vector>

namespace omk::vita {

struct AvFilm;

// Where a film's hardware copy is: `<dir>/<stem>.mp4` in ux0:data/omk/movies,
// then the data tree's FLIS folder, then the package's app0:movies, the NAME
// matched case-insensitively (a copy tool may change its case). Empty when
// none holds it - and then `report` says what each folder DID hold, or the
// kernel's error for it, so a log tells a wrong folder from a wrong name.
std::string avFind(const std::string& stem, const std::string& dataRoot,
                   std::string& report);
// Open a film. nullptr when it cannot be played (no file, no decoder, no
// memory) - the caller then falls back to the software path.
AvFilm* avOpen(const std::string& path);
// Still playing?
bool avActive(AvFilm* f);
// A new frame, when the player's clock says one is due: written into `out`
// (resized to the film's size) as RGB565. -> false when there is none yet.
bool avVideo(AvFilm* f, Surface& out);
// The sound decoded since the last call, as interleaved float stereo appended
// to `pcm`; `rate` is its sample rate. -> false when there was none.
bool avAudio(AvFilm* f, std::vector<float>& pcm, int& rate);
void avClose(AvFilm* f);

}  // namespace omk::vita

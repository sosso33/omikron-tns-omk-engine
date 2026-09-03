// SPDX-License-Identifier: GPL-3.0-or-later
// THE INTRO MOVIES - `gamedata/FLIS/`'s three MPEG-1 program streams, decoded.
//
// **Why this is allowed to use a library at all**, since a decoder is exactly
// the shape of thing `docs/PORTING.md` A8 rule 3 forbids. It was checked, not
// assumed: the engine's own import table names `CoCreateInstance` (ole32 - a
// DirectShow filter graph) and `mciSendCommandA` (winmm - MCI), and contains
// no MPEG decoder. The original handed the file to the operating system, so a
// vendored decoder is the EQUIVALENT of what it did, not a substitute for code
// that could have been ported. `engine/third_party/README.md` records that.
//
// **And why it sits in `src/` rather than behind the frontend boundary.** A8
// rule 2 keeps dependency headers in backend files; that is about SYSTEM
// dependencies, whose absence must not break the build. `pl_mpeg.h` is
// VENDORED - checked in, always present - so code here can use it and `make`
// still works on a machine with nothing installed, which is rule 1's actual
// property. Keeping it here is what lets `verify.py` test the decode at all;
// behind the boundary it would be untestable.
//
// This file is not a port of anything. Like the mixer's `render()`, it is the
// reference implementation of a boundary the original crossed into the OS.
#pragma once

#include "ui/surface.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace omk {

struct MovieInfo {
    int   width = 0, height = 0;
    double framerate = 0.0, duration = 0.0;
    int   audioStreams = 0, sampleRate = 0;
    bool  ok = false;
};

class Movie {
public:
    Movie();
    ~Movie();
    Movie(const Movie&) = delete;
    Movie& operator=(const Movie&) = delete;

    // `path` is a REAL path - resolve it through `DataFs` first, because the
    // disc spells the extension `.MPG` and the image spells it `.mpg`
    // (docs/BOOT.md), and only the case-insensitive resolver bridges that.
    bool open(const std::string& path);
    const MovieInfo& info() const { return info_; }

    // Decode one frame into `dst`, which must be the framebuffer's size.
    // -> false at the end of the stream.
    //
    // The films are 320x240 and are scaled to FIT `dst`, keeping their ratio:
    // the factor is the largest that fits in both axes, so a 4:3 display fills
    // completely and anything else gets even bars instead of a stretched
    // picture. At the 640x480 framebuffer that factor is exactly 2 and every
    // destination pixel lands on its own source pixel - the same relationship
    // `tools/frame.py` recovers a Retina capture through, one direction
    // reversed. Nearest-neighbour, because a filter is a resampler nobody here
    // can check.
    bool nextFrame(Surface& dst);
    long framesDecoded() const { return frames_; }

    // One block of interleaved stereo float samples, or an empty span at the
    // end. MP2 blocks are 1152 samples; the streams are 44100 Hz, which is
    // TWICE the rate `Sound_Init` gives the game's own primary buffer - the
    // original played these through DirectShow, which had its own output and
    // never went through that 22050 mixer.
    std::span<const float> nextAudio();
    long audioBlocks() const { return blocks_; }
    // Seconds of audio DECODED so far, from the stream's own timestamps. The
    // player subtracts what is still sitting in the device to get the moment
    // a listener is actually hearing, and paces the picture to that.
    double audioSeconds() const { return audioAt_; }

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    MovieInfo info_;
    long frames_ = 0, blocks_ = 0;
    double audioAt_ = 0.0;
};

}  // namespace omk

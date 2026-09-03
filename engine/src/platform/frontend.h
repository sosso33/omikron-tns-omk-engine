// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRONTEND BOUNDARY - where a device would be, and what must not cross it.
//
// `docs/PORTING.md` A1 puts two implementations behind one interface for every
// output subsystem: a REFERENCE one that `verify.py` checks, and a LIVE one
// that makes the replica playable. This is that interface for the window,
// keyboard and presentation.
//
// **Three rules from A8, and only the third is not hygiene:**
//
//   1. `make` with nothing installed builds every tool and passes the suite.
//      Nothing under `src/` or `tools/` may need a library, which is why this
//      header declares an interface and implements only the null case.
//   2. No ported source includes a dependency header. SDL appears in exactly
//      one file, `backends/sdl/play.cpp`, on the far side of this line.
//   3. **No dependency may perform work a reference implementation is a port
//      of.** The frontend is handed an already-composed RGB565 `Surface` and
//      uploads it. It must never blit, scale, blend or draw text - the ported
//      `blt`, `fillQuad` and `drawRun` do that, and letting SDL do it would
//      make the checks test SDL while staying exactly as green.
//
// So the frontend's whole job is: show these pixels, and tell me which keys
// are down. Everything else is the engine's.
#pragma once

#include "ui/surface.h"

#include <cstdint>
#include <set>
#include <span>
#include <string>

namespace omk {

// Keys are reported as the engine's OWN codes - DirectInput scan codes - not
// as anything a library invents, because `Input::poll` matches them against
// the live binding tables `Input_InstallScheme` filled. The translation from
// whatever the host uses happens inside the backend, which is the only place
// that knows what host it is on.
//
// This is the same stream `goldentrace run --keys` and `tools/sim/ui.py`
// speak, which is A6's point: the ported code never sees a device either way,
// so the reference and the live implementation differ only in where the set
// comes from.
struct HostInput {
    std::set<int> held;          // DIK scan codes currently down
    bool quit = false;           // window closed, or the quit key
    // Text the host reported this frame, already decoded from whatever the
    // keyboard layout is. A scan code is not a character - the name field on
    // the start menu takes what the PERSON typed, and deriving letters from
    // DIK codes would be inventing a layout. The frontend reports; the engine
    // decides what to do with it.
    std::string text;
};

class Frontend {
public:
    virtual ~Frontend() = default;

    virtual bool open(int w, int h, const std::string& title) = 0;
    // -> false once the host wants to stop.
    virtual bool pump(HostInput& out) = 0;
    // Upload and show. The surface is RGB565 and 640x480; A3 fixes both, and
    // a frontend that presents anything else has changed the framebuffer the
    // captures are compared against.
    virtual void present(const Surface& fb) = 0;

    // The movie soundtrack. A DEVICE, and nothing more: the samples arrive
    // already decoded and the frontend queues them. It is not the engine's
    // mixer - `Sound_Init` sets a 22050/16/stereo DirectSound primary and
    // DirectSound sums into it, which `src/audio/mixer.h` establishes has no
    // portable half at all. These streams are 44100 and the original played
    // them through DirectShow, which had its own output, so they never met
    // that mixer and must not be fed through the ported one.
    virtual bool openAudio(int /*rate*/, int /*channels*/) { return false; }
    // The STREAM: the movies' soundtrack, and the game's music. Pushed in as
    // it is decoded and consumed at the device's own rate.
    virtual void queueAudio(std::span<const float>) {}
    // A ONE-SHOT, mixed OVER the stream rather than queued behind it. The
    // interface blips are these. Before there was a mixer they were queued and
    // the queue flushed first, which is fine when a blip is the only sound and
    // silences the music the moment there is any.
    // -> a handle for `stopSound`, or -1 when the frontend has no mixer.
    virtual int  playSound(std::span<const float>) { return -1; }
    // Silence one shot before it ends. `Dialog_TickUI` case 2/7/8 calls
    // `Morph_Stop` on the press that leaves a line, and `Morph_Stop` stops the
    // voice buffer (`sub_46CAE0`): a line cut short by NEXT falls silent at
    // once. A reader heard the previous line run on under the menu.
    virtual void stopSound(int /*handle*/) {}
    // Drop whatever is still queued. Skipping a movie has to silence it: the
    // device holds seconds of audio the decoder ran ahead into, and without
    // this the soundtrack of a skipped movie plays on over the menu - which
    // is what a player reported.
    virtual void flushAudio() {}
    // How many SECONDS are still queued. The movie loop paces its video by
    // this, because the audio device is the only clock in the room that runs
    // at the rate a person hears.
    virtual double queuedSeconds() { return 0.0; }

    virtual void close() = 0;
};

// The reference implementation: no window, no keys, and it never quits on its
// own. It exists so that everything above this line can be built, linked and
// tested on a machine with nothing installed - the property A8 rule 1 is
// about - and so a headless run is a frontend rather than a special case.
class NullFrontend : public Frontend {
public:
    bool open(int, int, const std::string&) override { return true; }
    bool pump(HostInput& out) override { out.held.clear(); return !out.quit; }
    void present(const Surface& fb) override { ++frames_; last_ = fb.w * fb.h; }
    void close() override {}
    long frames() const { return frames_; }
    long lastPixels() const { return last_; }
private:
    long frames_ = 0, last_ = 0;
};

}  // namespace omk

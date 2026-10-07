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
//   2. No ported source includes a dependency header. SDL appears only in
//      the SDL-side files of `backends/sdl/` - the frontend `sdlfront.*`, the
//      GPU glue `playgpu_*.cpp`, the `--scene` instrument - on the far side
//      of this line (and not in the viewer's game code: the gateway below).
//   3. **No dependency may perform work a reference implementation is a port
//      of.** The frontend is handed an already-composed RGB565 `Surface` and
//      uploads it. It must never blit, scale, blend or draw text - the ported
//      `blt`, `fillQuad` and `drawRun` do that, and letting SDL do it would
//      make the checks test SDL while staying exactly as green.
//
// So the frontend's whole job is: show these pixels, and tell me which keys
// are down. Everything else is the engine's.
//
// **And it is the GATEWAY** (`todo/classic-mac-port-1999.md` step 5,
// 2026-10-03): the viewer's game code - every `backends/sdl/play*.cpp` but
// the frontend's own files, the per-backend GPU glue (`playgpu_*.cpp`) and
// the `--scene` instrument - reaches the host through this class and nothing
// else, and compiles without an SDL header (`verify.py: engine: frontend
// gateway` proves it with a poisoned one). That is what lets a Carbon
// frontend stand where SDL stands, on Mac OS 9 and on Tiger: it implements
// this class and `makeHostFrontend`, and the game code does not change.
#pragma once

#include "audio/hostmix.h"
#include "input/pad.h"
#include "ui/surface.h"

#include <cstdint>
#include <set>
#include <span>
#include <memory>
#include <vector>
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
    // A GAMEPAD, in platform-free terms (`input/pad.h`): the frontend fills
    // it from whatever the host has - SDL's game controller, the Vita's own
    // pad through SDL - and `pad::toDevices` turns it into the engine's
    // JOYSTICK device. All zero when there is none.
    pad::Pad pad;
    // Text the host reported this frame, already decoded from whatever the
    // keyboard layout is. A scan code is not a character - the name field on
    // the start menu takes what the PERSON typed, and deriving letters from
    // DIK codes would be inventing a layout. The frontend reports; the engine
    // decides what to do with it.
    std::string text;

    // ---- the mouse -------------------------------------------------------
    // Buttons in the ENGINE's own code space, which `Input_ReadOneControl`
    // (0x0043E360) fixes by testing a `DIMOUSESTATE`'s button bytes in order:
    // `& 0x80` -> 12 (left), `& 0x8000` -> 13 (right), `& 0x800000` -> 14
    // (middle). The shoot scheme binds `Tir` to 12.
    std::set<int> mouse;
    // Relative motion since the last frame, in host pixels. NOT a binding:
    // the engine's mouse arm reads buttons only, so motion never enters the
    // 14-slot input word - it aims the first-person camera directly.
    float mouseDX = 0.0f, mouseDY = 0.0f;
};

#if OMK_VR
namespace vr { struct HeadPose; }   // `vr/xrspace.h`, a VR build's only
#endif

class Frontend {
public:
    virtual ~Frontend() = default;

    // Does the HOST pace the frame? A headset's `xrWaitFrame` does (step 5b):
    // the game's own 30/60 deadline is then skipped, and the simulation steps
    // on the measured delta as it always does.
    virtual bool paces() const { return false; }
#if OMK_VR
    // THE HEADSET, for a frontend that has one (`todo/quest-port.md` §5): the
    // head and each eye's pose and field of view this frame. false = none, and
    // the frame draws the authored camera as the flat game does.
    virtual bool headPose(vr::HeadPose& /*out*/) { return false; }
#endif

    virtual bool open(int w, int h, const std::string& title) = 0;
    // -> false once the host wants to stop.
    virtual bool pump(HostInput& out) = 0;

    // Grab the pointer for first-person aiming (and hide it), or let it go.
    // A frontend that cannot is free to do nothing.
    virtual void setRelativeMouse(bool /*on*/) {}
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
    // ...and the WORLD's device, at the primary's 22050: the films open it at
    // their 44100, and the world then REOPENS it at its own rate - closed
    // first, with every queued sample and voice dropped, so nothing of a film
    // plays on under the world (the fault a reopen per film once had).
    virtual bool reopenAudio(int rate, int channels) { return openAudio(rate, channels); }
    // The STREAM: the movies' soundtrack, and the game's music. Pushed in as
    // it is decoded and consumed at the device's own rate.
    virtual void queueAudio(std::span<const float>) {}
    // The MUSIC stream's gain, 0..1 - `Music_SetVolume`'s attenuation applied
    // by the mixer; the one-shots keep their own gains.
    virtual void setMusicGain(float) {}
    // A ONE-SHOT, mixed OVER the stream rather than queued behind it. The
    // interface blips are these. Before there was a mixer they were queued and
    // the queue flushed first, which is fine when a blip is the only sound and
    // silences the music the moment there is any.
    // -> a handle for `stopSound`, or -1 when the frontend has no mixer.
    // `loop` makes the shot WRAP rather than end - `Script_PlaySound`'s own
    // flag, which the port recorded and never honoured (omk-play 72). A
    // looping shot is reclaimed only by `stopSound`.
    // `gain` is a linear 0..1 volume. `Script_PlaySound` ends in
    // `Sound_Play3D`, so a scene sound is POSITIONAL and gets quieter with
    // distance; playing every one at full volume is what made an ambience
    // audible across a city and through a cutscene (omk-play 73). The CURVE
    // is this port's, not the engine's - DirectSound owned the attenuation and
    // pan law and it has no reachable tier (PORTING, the audio row).
    virtual int  playSound(std::span<const float>, bool loop = false,
                           float gain = 1.0f) { return -1; }
    // The same, TAKING the samples: a conversation line's voice is ~11 MB of
    // device-rate floats, and copying it on the frame it starts was one more
    // pass over it on a console's memory bus (2026-09-23).
    virtual int  playSound(std::vector<float>&& s, bool loop = false, float gain = 1.0f) {
        return playSound(std::span<const float>(s), loop, gain);
    }
    // The same, SHARING them, which is what the engine does: `Sound_Play3D`
    // (0x0046CDC0) plays a `DuplicateSoundBuffer` of the bank's buffer - a
    // second voice on the SAME sample memory. An effect played from a cache
    // was copied whole on every play, a megabyte for a three-second sample,
    // with the mixer locked for the copy (todo/optimization.md step 33).
    virtual int  playSound(std::shared_ptr<const std::vector<float>> s, bool loop = false,
                           float gain = 1.0f) {
        return s ? playSound(std::span<const float>(*s), loop, gain) : -1;
    }
    // A sound kept in its file's own form (`audio/hostmix.h`), read at the
    // device rate as it plays. A frontend without the host mixer gets the
    // float samples it would have had.
    virtual int  playSound(std::shared_ptr<const DeviceSound> s, bool loop = false,
                           float gain = 1.0f) {
        if (!s || !s->size) return -1;
        std::vector<float> f(s->size);
        for (std::size_t i = 0; i < f.size(); ++i) f[i] = s->at(i);
        return playSound(std::move(f), loop, gain);
    }
    // Silence one shot before it ends. `Dialog_TickUI` case 2/7/8 calls
    // `Morph_Stop` on the press that leaves a line, and `Morph_Stop` stops the
    // voice buffer (`sub_46CAE0`): a line cut short by NEXT falls silent at
    // once. A reader heard the previous line run on under the menu.
    virtual void stopSound(int /*handle*/) {}
    // ...and change a playing one's gain - a looped source that moves (a
    // vehicle's engine, `sub_456B40` -> `sub_46CFC0` every frame).
    virtual void setSoundGain(int /*handle*/, float /*gain*/) {}
    // How many SECONDS of a one-shot have played, or a negative number once
    // it has ended or when there is no device - a headless run has none, so
    // nothing that follows an audio clock can make a `--frames` run differ.
    // A conversation line's voice is the clock `Game_Frame` syncs the
    // simulation to while the line plays (todo/drift-audit.md T2).
    virtual double soundPlayedSeconds(int /*handle*/) { return -1.0; }
    // Drop whatever is still queued. Skipping a movie has to silence it: the
    // device holds seconds of audio the decoder ran ahead into, and without
    // this the soundtrack of a skipped movie plays on over the menu - which
    // is what a player reported.
    virtual void flushAudio() {}
    // How many SECONDS are still queued. The movie loop paces its video by
    // this, because the audio device is the only clock in the room that runs
    // at the rate a person hears.
    virtual double queuedSeconds() { return 0.0; }

    // ---- THE CLOCKS. What the viewer measures wall time with: a tick in
    // milliseconds that wraps as a 32-bit one does (SDL_GetTicks, the
    // original's timeGetTime), a high-resolution counter and its rate, and a
    // sleep. None of it reaches a decision a headless run makes - a
    // `--frames` run steps on the frame clock - so the reference frontend
    // returns zeros and never sleeps.
    virtual std::uint32_t ticksMs() { return 0; }
    virtual std::uint64_t perfCounter() { return 0; }
    virtual std::uint64_t perfFrequency() { return 1; }
    virtual void delayMs(std::uint32_t) {}

    // ---- THE WINDOW. Whichever is up: the frontend's own, or a GPU
    // backend's it was handed. A frontend with no window does nothing.
    virtual void setFullscreen(bool) {}
    virtual bool fullscreen() const { return false; }
    // The frame size changed (options row 2); -> false if it could not.
    virtual bool resize(int, int) { return false; }
    // OPTIONS ROW 2's list - the host's display modes, named as the
    // engine labels them ("W x H x 16 bpp"),
    // the running size among them; -> the running size's index.
    virtual int displayModes(int curW, int curH, std::vector<std::string>& names) {
        names.assign(1, std::to_string(curW) + " x " + std::to_string(curH) + " x 16 bpp");
        return 0;
    }
    // An opaque identity for the window shown, null without one: a caller
    // that labels a window keeps its base title per window.
    virtual const void* windowId() const { return nullptr; }
    virtual std::string windowTitle() const { return {}; }
    virtual void setWindowTitle(const std::string&) {}
    // Ask the host for typed characters (`HostInput::text`).
    virtual void startTextInput() {}
    // The interface's NAME FIELD has the focus (true) or not, said every
    // frame: a host with no keyboard of its own shows its system one while it
    // does (the Quest's overlay keyboard, `todo/quest-port.md` step 6c).
    // Everywhere else typing is already on (`startTextInput`) or the field
    // asks for itself (the Vita's modal IME), and this does nothing.
    virtual void fieldKeyboard(bool) {}
    // The host library's last error, for a message - "" when it has none.
    virtual std::string lastError() const { return {}; }

    virtual void close() = 0;
};

// THE HOST'S frontend - the one function a frontend backend defines besides
// its class: `backends/sdl/sdlfront.cpp` makes an `SdlFrontend`, and a
// Carbon frontend would make its own. The viewer calls it once.
std::unique_ptr<Frontend> makeHostFrontend();

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

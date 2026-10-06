// SPDX-License-Identifier: GPL-3.0-or-later
// THE 3DS FRONTEND - see `n3dsfront.h` for what it does and why.
#include "n3dsfront.h"

#include "input/pad.h"
#include "n3dshost.h"
#include "platform/datafs.h"
#include "platform/profile.h"

#include <3ds.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace omk {

// ---- the sound --------------------------------------------------------------

struct N3dsFrontend::Wave {
    ndspWaveBuf buf{};
    std::int16_t* pcm = nullptr;     // linear memory: the DSP reads it directly
};

bool N3dsFrontend::openAudio(int rate, int channels) {
    if (rate <= 0 || (channels != 1 && channels != 2)) return false;
    if (dspFailed_) return false;
    if (!dsp_) {
        // ndsp needs the DSP's firmware, which a console only has once it has
        // been DUMPED to the card (`sdmc:/3ds/dspfirm.cdc`, by the DSP1
        // homebrew). Without it there is no sound at all - said once, and the
        // game runs silent.
        if (R_FAILED(ndspInit())) {
            lastError_ = "ndspInit failed - no DSP firmware at sdmc:/3ds/dspfirm.cdc "
                         "(dump it with DSP1); the game runs without sound";
            std::printf("audio: %s\n", lastError_.c_str());
            dspFailed_ = true;
            return false;
        }
        dsp_ = true;
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    }
    closeAudio();
    arate_ = rate;
    achan_ = channels;
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, static_cast<float>(rate));
    ndspChnSetFormat(0, channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    float mix[12] = {};
    mix[0] = mix[1] = 1.0f;          // front left, front right at full
    ndspChnSetMix(0, mix);
    waveFrames_ = static_cast<std::size_t>(rate / 20);
    wave_ = std::make_unique<Wave[]>(kBuffers);
    for (int i = 0; i < kBuffers; ++i) {
        auto* p = static_cast<std::int16_t*>(
            linearAlloc(waveFrames_ * static_cast<std::size_t>(channels) * sizeof(std::int16_t)));
        if (!p) {
            lastError_ = "audio: no linear memory for the sound buffers";
            std::printf("%s\n", lastError_.c_str());
            closeAudio();
            return false;
        }
        wave_[i].pcm = p;
        wave_[i].buf.data_pcm16 = p;
        wave_[i].buf.nsamples = static_cast<u32>(waveFrames_);
        wave_[i].buf.status = NDSP_WBUF_FREE;
    }
    chan_ = true;
    std::printf("audio: ndsp, %d Hz, %d channel(s), %d buffers of %u frames\n",
                rate, channels, kBuffers, static_cast<unsigned>(waveFrames_));
    return true;
}

// The world's device after the films': everything queued is dropped, as the
// gateway asks - nothing of a film plays on under the world.
bool N3dsFrontend::reopenAudio(int rate, int channels) {
    mix_.clear();
    return openAudio(rate, channels);
}

void N3dsFrontend::closeAudio() {
    if (dsp_) ndspChnWaveBufClear(0);
    if (wave_) {
        for (int i = 0; i < kBuffers; ++i)
            if (wave_[i].pcm) linearFree(wave_[i].pcm);
        wave_.reset();
    }
    chan_ = false;
}

void N3dsFrontend::refillAudio() {
    if (!chan_) return;
    const std::size_t n = waveFrames_ * static_cast<std::size_t>(achan_);
    mixed_.resize(n);
    for (int i = 0; i < kBuffers; ++i) {
        ndspWaveBuf& b = wave_[i].buf;
        if (b.status != NDSP_WBUF_FREE && b.status != NDSP_WBUF_DONE) continue;
        mix_.mix(mixed_.data(), n);
        std::int16_t* dst = wave_[i].pcm;
        for (std::size_t k = 0; k < n; ++k) {
            const float v = std::clamp(mixed_[k], -1.0f, 1.0f);
            dst[k] = static_cast<std::int16_t>(v * 32767.0f);
        }
        DSP_FlushDataCache(dst, n * sizeof(std::int16_t));
        ndspChnWaveBufAdd(0, &b);
    }
}

void N3dsFrontend::queueAudio(std::span<const float> s) {
    if (s.empty()) return;
    if (!chan_) {
        if (!droppedToldOnce_) {
            droppedToldOnce_ = true;
            std::printf("audio: no device - the stream is dropped, not queued\n");
        }
        return;
    }
    mix_.queue(s);
    refillAudio();
}

int N3dsFrontend::playSound(std::span<const float> s, bool loop, float gain) {
    if (s.empty()) return -1;
    return playSound(std::make_shared<const std::vector<float>>(s.begin(), s.end()), loop, gain);
}
int N3dsFrontend::playSound(std::vector<float>&& s, bool loop, float gain) {
    if (s.empty()) return -1;
    return playSound(std::make_shared<const std::vector<float>>(std::move(s)), loop, gain);
}
int N3dsFrontend::playSound(std::shared_ptr<const std::vector<float>> s, bool loop, float gain) {
    return mix_.play(std::move(s), loop, gain);
}
int N3dsFrontend::playSound(std::shared_ptr<const omk::DeviceSound> s, bool loop, float gain) {
    return mix_.play(std::move(s), loop, gain);
}

// The films pace their video by this (the audio clock), so it refills first.
double N3dsFrontend::queuedSeconds() {
    refillAudio();
    return arate_ > 0 ? static_cast<double>(mix_.queued()) / (arate_ * achan_) : 0.0;
}

// ---- the screen and the buttons ---------------------------------------------

bool N3dsFrontend::open(int, int, const std::string&) {
    // RGB565 is the engine's own frame format, so the top screen takes the
    // frame's words as they are; double-buffered, so a frame being written is
    // never the one on show.
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    // The bottom screen, the instrument panel: single-buffered - it is
    // redrawn a few times a second, and a swapped pair would show the stale
    // half every other frame.
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    stats_.newModel = n3ds;
    panelOk_ = panel_.open();
    // AN INSTRUMENT: with `sdmc:/omk/panel-dump` on the card, every redraw is
    // also written to `sdmc:/omk/panel.bin` (raw LE RGB565, 320x240) - how
    // the bottom screen is looked at where it cannot be photographed.
    // ...and with `sdmc:/omk/capture-at` holding a number, that present is
    // CAPTUREd as the panel's button would - how a frame is taken where
    // nobody presses the button (an emulator run)
    if (std::FILE* f = std::fopen((std::string(n3ds::kHome) + "/capture-at").c_str(), "r")) {
        long at = -1;
        if (std::fscanf(f, "%ld", &at) == 1) captureAt_ = at;
        std::fclose(f);
        std::printf("panel: capture-at present - present %ld will be captured\n", captureAt_);
    }
    if (std::FILE* f = std::fopen((std::string(n3ds::kHome) + "/panel-dump").c_str(), "r")) {
        std::fclose(f);
        panelDump_ = true;
        std::printf("panel: panel-dump present - each redraw also written to %s/panel.bin\n", n3ds::kHome);
    }
    panel_.draw(panelSurf_, stats_);
    n3ds::Panel::toScreen(panelSurf_);
    winStart_ = lastPresent_ = svcGetSystemTick();
    opened_ = true;
    return true;
}

namespace {

// The circle pad reads about +-156 at its rim; the engine's stick is
// -1000..1000 with +y DOWN (DirectInput's sense), the pad's +y is UP.
constexpr int kCircleRim = 150;
int stick(int v) { return std::clamp(v * 1000 / kCircleRim, -1000, 1000); }

// The buttons as the engine's joystick buttons (`input/pad.h`). A CHOICE, as
// that header says every pad layout is: by LABEL, Nintendo's way - A is the
// confirm (slot 4, the action button) and B the back (slot 5), though on the
// 3DS A sits where SDL's EAST is. To be play-tested.
struct Bind { u32 key; std::uint32_t button; };
constexpr Bind kBinds[] = {
    {KEY_A, pad::South},  {KEY_B, pad::East},  {KEY_Y, pad::West},  {KEY_X, pad::North},
    {KEY_L, pad::LeftShoulder}, {KEY_R, pad::RightShoulder},
    {KEY_SELECT, pad::Back},    {KEY_START, pad::Start},
    {KEY_DUP, pad::DpadUp},     {KEY_DDOWN, pad::DpadDown},
    {KEY_DLEFT, pad::DpadLeft}, {KEY_DRIGHT, pad::DpadRight},
};

}  // namespace

bool N3dsFrontend::pump(HostInput& out) {
    refillAudio();
    out.text.clear();
    if (!aptMainLoop() && !quit_) {          // the HOME menu's "close", or power off
        std::printf("quit: the system asked the program to close\n");
        quit_ = true;
    }
    hidScanInput();
    const u32 held = hidKeysHeld();
    if ((held & KEY_START) && (held & KEY_SELECT) && !quit_) {
        std::printf("quit: START + SELECT\n");
        quit_ = true;
    }
    out.quit = quit_;

    out.held.clear();
    out.mouse.clear();
    out.mouseDX = out.mouseDY = 0.0f;
    out.pad = pad::Pad{};
    for (const Bind& b : kBinds)
        if (held & b.key) out.pad.buttons |= b.button;
    circlePosition c{};
    hidCircleRead(&c);
    out.pad.lx = stick(c.dx);
    out.pad.ly = -stick(c.dy);
    // The New 3DS's C-stick is the right stick; the old model reads zero.
    circlePosition cs{};
    hidCstickRead(&cs);
    out.pad.rx = stick(cs.dx);
    out.pad.ry = -stick(cs.dy);
    // START is the menu key, read straight from `held` (`pad::kEscape`)
    if (out.pad.buttons & pad::Start) out.held.insert(pad::kEscape);
    // the panel's buttons, on a touch going down
    if (hidKeysDown() & KEY_TOUCH) {
        touchPosition tp{};
        hidTouchRead(&tp);
        act(panel_.hit(tp.px, tp.py));
    }
    // Every change of the pad, logged with the pump it came on: the sticks
    // only past the dead zone's edge (`pad::kDeadZone`), so a stick's jitter
    // at rest says nothing.
    auto zone = [](int v) { return v > pad::kDeadZone ? 1 : (v < -pad::kDeadZone ? -1 : 0); };
    if (out.pad.buttons != lastButtons_ || zone(out.pad.lx) != zone(lastLx_) ||
        zone(out.pad.ly) != zone(lastLy_) || zone(out.pad.rx) != zone(lastRx_) ||
        zone(out.pad.ry) != zone(lastRy_)) {
        std::printf("pad: pump %ld buttons 0x%04X (keys 0x%08lX) left %d,%d right %d,%d\n",
                    pumps_, static_cast<unsigned>(out.pad.buttons), static_cast<unsigned long>(held),
                    out.pad.lx, out.pad.ly, out.pad.rx, out.pad.ry);
        lastButtons_ = out.pad.buttons;
        lastLx_ = out.pad.lx; lastLy_ = out.pad.ly; lastRx_ = out.pad.rx; lastRy_ = out.pad.ry;
    }
    ++pumps_;
    return !out.quit;
}

namespace {

// Four RGB565 pixels averaged, each channel on its own: the word spread so
// that red, green and blue each have two spare bits above them, summed,
// shifted and folded back. Exact (the floor of the four's mean per channel).
inline std::uint32_t spread(std::uint16_t p) {
    return (static_cast<std::uint32_t>(p) | (static_cast<std::uint32_t>(p) << 16)) & 0x07E0F81Fu;
}
inline std::uint16_t box4(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d) {
    const std::uint32_t s = ((spread(a) + spread(b) + spread(c) + spread(d)) >> 2) & 0x07E0F81Fu;
    return static_cast<std::uint16_t>(s | (s >> 16));
}

}  // namespace

// The top screen's framebuffer is the panel turned a quarter: 240 pixels a
// COLUMN, 400 columns, a column running bottom to top. Screen pixel (x, y)
// is word `x * 240 + (239 - y)`, so the loop runs down a column to write
// memory in order.
void N3dsFrontend::present(const Surface& fb) {
    if (!opened_ || fb.w <= 0 || fb.h <= 0) return;
    u16 fw = 0, fh = 0;
    auto* dst = reinterpret_cast<std::uint16_t*>(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fw, &fh));
    if (!dst || fw != 240 || fh != 400) return;
    constexpr int kW = 400, kH = 240;
    // fitted, aspect kept
    int ow = kW, oh = fb.h * kW / fb.w;
    if (oh > kH) { oh = kH; ow = fb.w * kH / fb.h; }
    const int ox = (kW - ow) / 2, oy = (kH - oh) / 2;
    const bool half = fb.w == ow * 2 && fb.h == oh * 2;
    const std::uint16_t* src = fb.px.data();
    for (int x = 0; x < kW; ++x) {
        std::uint16_t* col = dst + x * 240;
        if (x < ox || x >= ox + ow) {
            std::memset(col, 0, 240 * sizeof(std::uint16_t));
            continue;
        }
        const int u = x - ox;
        for (int y = 0; y < kH; ++y) {
            const int v = y - oy;
            std::uint16_t p = 0;
            if (v >= 0 && v < oh) {
                if (half) {
                    const std::uint16_t* r0 = src + static_cast<std::size_t>(2 * v) * fb.w + 2 * u;
                    const std::uint16_t* r1 = r0 + fb.w;
                    p = box4(r0[0], r0[1], r1[0], r1[1]);
                } else {
                    p = src[static_cast<std::size_t>(v * fb.h / oh) * fb.w + u * fb.w / ow];
                }
            }
            col[239 - y] = p;
        }
    }
    if (captureAt_ >= 0 && stats_.frames == captureAt_) captureOwed_ = true;
    if (captureOwed_) {
        captureOwed_ = false;
        writeCapture(fb);
    }
    // THE FRAME, measured where it is presented: the interval since the last
    // present, and of it what the pacer slept. Over a window of a second.
    const std::uint64_t now = svcGetSystemTick();
    const std::uint64_t interval = now - lastPresent_;
    lastPresent_ = now;
    winFrames_ += 1;
    winSleep_ += std::min(sleepTicks_, interval);
    winWorst_ = std::max(winWorst_, interval);
    sleepTicks_ = 0;
    stats_.frames += 1;
    if (now - winStart_ >= SYSCLOCK_ARM11 / 2) {        // twice a second
        const double span = static_cast<double>(now - winStart_);
        stats_.fps = winFrames_ * static_cast<double>(SYSCLOCK_ARM11) / span;
        stats_.frameMs = span * 1000.0 / SYSCLOCK_ARM11 / winFrames_;
        stats_.worstMs = static_cast<double>(winWorst_) * 1000.0 / SYSCLOCK_ARM11;
        stats_.busyPct = 100.0 * (1.0 - static_cast<double>(winSleep_) / span);
        winStart_ = now;
        winFrames_ = 0;
        winSleep_ = winWorst_ = 0;
        if (panelOk_) {
            panel_.draw(panelSurf_, stats_);
            n3ds::Panel::toScreen(panelSurf_);
            if (panelDump_) {
                std::vector<unsigned char> le(panelSurf_.px.size() * 2);
                for (std::size_t i = 0; i < panelSurf_.px.size(); ++i) {
                    le[2 * i] = static_cast<unsigned char>(panelSurf_.px[i] & 0xFF);
                    le[2 * i + 1] = static_cast<unsigned char>(panelSurf_.px[i] >> 8);
                }
                writeWholeFile(std::string(n3ds::kHome) + "/panel.bin", le.data(), le.size());
            }
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}

// ---- the panel's buttons ----------------------------------------------------

void N3dsFrontend::act(n3ds::Panel::Action a) {
    switch (a) {
    case n3ds::Panel::Action::None: return;
    case n3ds::Panel::Action::Capture:
        captureOwed_ = true;                         // written at the next present
        if (!n3ds::capturePath().empty()) writeControl("snapshot");
        break;
    case n3ds::Panel::Action::Pause:  writeControl("pause"); break;
    case n3ds::Panel::Action::Step:   writeControl("step"); break;
    case n3ds::Panel::Action::Resume: writeControl("resume"); break;
    }
}

// The profiler's control (`platform/profile.cpp` control()): "<seq> <cmd>
// <n>" in `<capture>.ctl`, a seq the game has not seen. Written to a side
// file and renamed into place, so the game never reads half a line; FAT
// does not rename over a file, so the old one goes first.
void N3dsFrontend::writeControl(const char* cmd) {
    const std::string& base = n3ds::capturePath();
    if (base.empty()) {
        stats_.note = std::string(cmd) + ": no profiler capture - add --profile <path> to args.txt";
        std::printf("panel: %s\n", stats_.note.c_str());
        return;
    }
    const long seq = static_cast<long>(ticksMs() % 2000000000u);
    const std::string tmp = base + ".ctl.tmp", ctl = base + ".ctl";
    if (std::FILE* f = std::fopen(tmp.c_str(), "w")) {
        std::fprintf(f, "%ld %s 1\n", seq, cmd);
        std::fclose(f);
        std::remove(ctl.c_str());
        std::rename(tmp.c_str(), ctl.c_str());
        stats_.note = std::string("sent ") + cmd + " (" + std::to_string(seq) + ")";
    } else {
        stats_.note = std::string(cmd) + ": could not write " + tmp;
    }
    std::printf("panel: %s\n", stats_.note.c_str());
}

// The frame on show, as `--dump` writes it: raw little-endian RGB565, the
// size said in the log since the file carries none.
void N3dsFrontend::writeCapture(const Surface& fb) {
    makeDirectories(n3ds::kCaptures);
    char name[96];
    const std::time_t t = std::time(nullptr);
    char when[32] = "undated";
    if (const std::tm* lt = std::localtime(&t)) std::strftime(when, sizeof when, "%Y%m%d-%H%M%S", lt);
    std::snprintf(name, sizeof name, "%s/frame-%s-%ld.bin", n3ds::kCaptures, when, stats_.frames);
    std::vector<unsigned char> le(fb.px.size() * 2);
    for (std::size_t i = 0; i < fb.px.size(); ++i) {
        le[2 * i] = static_cast<unsigned char>(fb.px[i] & 0xFF);
        le[2 * i + 1] = static_cast<unsigned char>(fb.px[i] >> 8);
    }
    const bool ok = writeWholeFile(name, le.data(), le.size());
    stats_.note = ok ? std::string("captured ") + name : std::string("capture FAILED: ") + name;
    std::printf("panel: %s (%dx%d RGB565, %zu bytes)\n", stats_.note.c_str(), fb.w, fb.h, le.size());
}

void N3dsFrontend::close() {
    closeAudio();
    if (dsp_) { ndspExit(); dsp_ = false; }
    opened_ = false;
}

N3dsFrontend::~N3dsFrontend() { close(); }

// ---- the clocks ---------------------------------------------------------------

std::uint32_t N3dsFrontend::ticksMs() {
    return static_cast<std::uint32_t>(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000));
}
std::uint64_t N3dsFrontend::perfCounter() { return svcGetSystemTick(); }
std::uint64_t N3dsFrontend::perfFrequency() { return SYSCLOCK_ARM11; }
void N3dsFrontend::delayMs(std::uint32_t ms) {
    const std::uint64_t t0 = svcGetSystemTick();
    svcSleepThread(static_cast<s64>(ms) * 1000000);
    sleepTicks_ += svcGetSystemTick() - t0;      // the panel's BUSY share
}

std::unique_ptr<Frontend> makeHostFrontend() { return std::make_unique<N3dsFrontend>(); }

}  // namespace omk

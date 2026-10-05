// SPDX-License-Identifier: GPL-3.0-or-later
// THE 3DS FRONTEND - see `n3dsfront.h` for what it does and why.
#include "n3dsfront.h"

#include "input/pad.h"

#include <3ds.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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
    gfxFlushBuffers();
    gfxSwapBuffers();
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
    svcSleepThread(static_cast<s64>(ms) * 1000000);
}

std::unique_ptr<Frontend> makeHostFrontend() { return std::make_unique<N3dsFrontend>(); }

}  // namespace omk

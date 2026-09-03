// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/movie.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

namespace omk {

struct Movie::Impl {
    plm_t* plm = nullptr;
    std::vector<std::uint8_t> rgb;      // one decoded frame, 24-bit
    ~Impl() { if (plm) plm_destroy(plm); }
};

Movie::Movie() : p_(std::make_unique<Impl>()) {}
Movie::~Movie() = default;

bool Movie::open(const std::string& path) {
    p_->plm = plm_create_with_filename(path.c_str());
    if (!p_->plm) return false;

    // **PROBE WIDE, and this line is here because of a wrong answer.** The
    // default probe reads a short prefix, and every one of these files puts
    // its first audio PES packet at offset **160368** - past it. So
    // `plm_get_num_audio_streams` returned 0 and this code recorded "the
    // movies have no audio", which went into a check and two documents as a
    // property of the FILES. It is a property of the probe window: a raw scan
    // for stream id 0xC0 finds 270-283 packets in the first 6 MB of each, and
    // with a 1 MB probe pl_mpeg reports one 44100 Hz stream and decodes a
    // 1152-sample block immediately.
    plm_probe(p_->plm, 1024 * 1024);
    info_.width        = plm_get_width(p_->plm);
    info_.height       = plm_get_height(p_->plm);
    info_.framerate    = plm_get_framerate(p_->plm);
    info_.duration     = plm_get_duration(p_->plm);
    info_.audioStreams = plm_get_num_audio_streams(p_->plm);
    info_.sampleRate   = plm_get_samplerate(p_->plm);
    info_.ok = info_.width > 0 && info_.height > 0;
    p_->rgb.assign(static_cast<std::size_t>(info_.width) * info_.height * 3, 0);
    return info_.ok;
}

bool Movie::nextFrame(Surface& dst) {
    if (!p_->plm || !dst.valid()) return false;
    plm_frame_t* f = plm_decode_video(p_->plm);
    if (!f) return false;
    plm_frame_to_rgb(f, p_->rgb.data(), info_.width * 3);

    // Into the framebuffer's own space, and never the other way round: A3's
    // rule. `quantise888` rounds, which A3 keeps because it is the correct
    // inverse in general - not because a test prefers it, since none here can
    // tell rounding from truncation.
    const int sw = info_.width, sh = info_.height;
    const int ox = (dst.w - sw * 2) / 2, oy = (dst.h - sh * 2) / 2;
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            const std::uint8_t* s = &p_->rgb[(static_cast<std::size_t>(y) * sw + x) * 3];
            const std::uint16_t v = quantise888(s[0], s[1], s[2]);
            const int dx = ox + x * 2, dy = oy + y * 2;
            if (dx < 0 || dy < 0 || dx + 1 >= dst.w || dy + 1 >= dst.h) continue;
            dst.set(dx,     dy,     v);
            dst.set(dx + 1, dy,     v);
            dst.set(dx,     dy + 1, v);
            dst.set(dx + 1, dy + 1, v);
        }
    }
    ++frames_;
    return true;
}

std::span<const float> Movie::nextAudio() {
    if (!p_->plm) return {};
    plm_samples_t* s = plm_decode_audio(p_->plm);
    if (!s) return {};
    ++blocks_;
    // The decoder's own timestamp for this block. The movie loop paces the
    // picture against it, because a fixed per-frame sleep adds the decode time
    // to every frame and the picture drifts behind the sound.
    audioAt_ = s->time;
    return {s->interleaved, static_cast<std::size_t>(s->count) * 2};
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// The host's mix - see hostmix.h.
#include "audio/hostmix.h"

#include <algorithm>

namespace omk {

int HostMixer::play(std::shared_ptr<const std::vector<float>> s, bool loop, float gain,
                    std::shared_ptr<const std::vector<float>>* dropped) {
    if (!s || s->empty()) return -1;
    if (shots_.size() >= 8) {
        if (dropped) *dropped = std::move(shots_.front().pcm);
        shots_.erase(shots_.begin());
    }
    const int id = nextShot_++;
    shots_.push_back({std::move(s), 0, id, loop, gain});
    return id;
}

void HostMixer::stop(int handle) {
    if (handle < 0) return;
    std::erase_if(shots_, [handle](const Shot& o) { return o.id == handle; });
}

void HostMixer::mix(float* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float v = 0.0f;
        if (head_ < stream_.size()) v += stream_[head_++] * musicGain_;
        for (auto& one : shots_) {
            const std::vector<float>& pcm = *one.pcm;
            if (one.pos >= pcm.size()) {
                if (!one.loop || pcm.empty()) continue;
                one.pos = 0;                  // a looping shot wraps
            }
            v += pcm[one.pos++] * one.gain;
        }
        dst[i] = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }
    if (head_ > (1u << 20)) {
        stream_.erase(stream_.begin(), stream_.begin() + static_cast<std::ptrdiff_t>(head_));
        head_ = 0;
    }
    std::erase_if(shots_, [](const Shot& o) {
        return !o.loop && o.pos >= o.pcm->size();  // a loop ends only on stop
    });
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/program.h"

#include "formats/anim.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace omk {
namespace {

// A parameter is stored as a dword and read as whichever type the handler
// wants; `Wait` wants a float out of the same bits.
float asFloat(std::int32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

}  // namespace

ScxRuntime::ScxRuntime(std::span<const std::byte> file)
    : data_(file.begin(), file.end()) {
    scene_  = readScx(data_);
    stream_ = readScxStream(data_);
}

const ScxObject* ScxRuntime::byName(const std::string& n) const {
    for (const auto& o : scene_.objects)
        if (o.name == n) return &o;
    return nullptr;
}

int ScxRuntime::clipFrames(int i) const {
    const auto it = frames_.find(i);
    if (it != frames_.end()) return it->second;
    int n = 0;
    if (i >= 0 && static_cast<std::size_t>(i) < stream_.anims.size()) {
        const auto& a = stream_.anims[static_cast<std::size_t>(i)];
        if (const auto d = animDescriptor(data_, a.offset))
            n = std::max(1, d->frames);
    }
    frames_[i] = n;
    return n;
}

std::string ScxRuntime::clipName(int i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= stream_.anims.size()) return "?";
    return stream_.anims[static_cast<std::size_t>(i)].name;
}

std::span<const std::byte> ScxRuntime::clipData(int i) const {
    if (i < 0 || i >= static_cast<int>(stream_.anims.size())) return {};
    const auto& a = stream_.anims[static_cast<std::size_t>(i)];
    if (a.offset + a.size > data_.size()) return {};
    return {data_.data() + a.offset, a.size};
}

Program::Program(const ScxRuntime& rt, const ScxObject& obj)
    : rt_(&rt), obj_(&obj) {
    start();
}

void Program::start() {
    running_ = true;
    pc_      = 0;
    loops_   = 0;
    clock_   = 0.0f;
    runs_.assign(obj_->functions.size(), 0);
    busyUntil_.clear();
    ++restarts_;
}

std::vector<int> Program::chain(int i) const {
    std::vector<int> out;
    std::set<int> seen;
    while (i >= 0 && static_cast<std::size_t>(i) < obj_->functions.size() &&
           !seen.count(i)) {
        seen.insert(i);
        out.push_back(i);
        i = obj_->functions[static_cast<std::size_t>(i)].sync;
    }
    return out;
}

float Program::busySpan(int k) const {
    const auto& f = obj_->functions[static_cast<std::size_t>(k)];
    if ((f.id == kFnSelectBodyAnimation || f.id == kFnSelectRelativeAnim) &&
        f.params.size() > 1)
        return static_cast<float>(rt_->clipFrames(f.params[1]));
    if (f.id == kFnWait && !f.params.empty())
        return std::max(0.0f, asFloat(f.params[0]));
    // PlaySyncSound has its own cue time, handled in tick; a path's duration
    // belongs to the walker and is not modelled here.
    return 0.0f;
}

bool Program::tick(float dt) {
    if (!running_) return false;
    bool busy = false;
    if (obj_->nfn > 0) {
        for (int k : chain(pc_)) {
            const auto& f = obj_->functions[static_cast<std::size_t>(k)];
            if (f.repeat != -1 && runs_[static_cast<std::size_t>(k)] >= f.repeat)
                continue;                      // this one has run its count out
            if (f.id == kFnPlaySyncSound) {
                // its handler holds the chain while param 1 > the clock
                const float at = f.params.size() > 1 ? asFloat(f.params[1]) : 0.0f;
                if (clock_ < at) busy = true;
                continue;
            }
            auto it = busyUntil_.find(k);
            if (it == busyUntil_.end()) {
                it = busyUntil_.emplace(k, clock_ + busySpan(k)).first;
                if (f.id == kFnSelectBodyAnimation || f.id == kFnSelectRelativeAnim)
                    trace_.push_back(rt_->clipName(
                        f.params.size() > 1 ? f.params[1] : -1));
            }
            if (clock_ < it->second) {
                busy = true;
            } else {
                // The run is over. `Script_SelectBodyAnimation`'s tail
                // (28_script.c ~1590): `runCounter += 1; if (runCounter >=
                // repeatLimit && repeatLimit != -1) return 0;` - and
                // otherwise it WRAPS the frame (`v26 -= frames` until it
                // fits) and returns 1, busy: the clip plays again from the
                // leftover, and the function is done only when its `+16`
                // count is spent. A first port ended after one run, so the
                // Impasse's `C_2_MecaSpeaks` (31-frame clip, repeat 18 =
                // the editing's 558 frames) released its camera editing at
                // frame 31 - a reader saw the cutscene's last cameras go
                // wrong. -1 repeats for ever (the object's loop ends it).
                const int n = ++runs_[static_cast<std::size_t>(k)];
                if (f.repeat != -1 && n >= f.repeat) {
                    busyUntil_.erase(it);
                } else {
                    it->second += busySpan(k);   // from the leftover, not a restart
                    busy = true;
                }
            }
        }
        if (!busy) {
            if (pc_ + 1 < obj_->nfn) {
                ++pc_;
                busy = true;
            } else {
                ++loops_;
                if (obj_->loop == -1 || loops_ < obj_->loop) {
                    const auto keep = trace_;   // instrumentation survives the
                    start();                    // rewind; engine state does not
                    trace_ = keep;
                    busy = true;
                }
            }
        }
    }
    clock_ += dt;
    if (!busy) running_ = false;
    return running_;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/program.h"

#include <cmath>

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

std::string ScxRuntime::wavName(int i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= stream_.wavs.size()) return "?";
    return stream_.wavs[static_cast<std::size_t>(i)].name;
}

std::string ScxRuntime::objectName(const ScxObject& o, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= o.tables[0].size()) return {};
    return o.tables[0][static_cast<std::size_t>(index)];
}

const ScxPath* ScxRuntime::pathIn(int file, int index) const {
    for (const auto& p : stream_.paths)
        if (p.file == file && p.index == index) return &p;
    return nullptr;
}

int ScxRuntime::wavId(int i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= stream_.wavs.size()) return -1;
    return stream_.wavs[static_cast<std::size_t>(i)].id;
}

int ScxRuntime::wavBydId(int id) const {
    for (std::size_t i = 0; i < stream_.wavs.size(); ++i)
        if (stream_.wavs[i].id == id) return static_cast<int>(i);
    return -1;
}

std::span<const std::byte> ScxRuntime::wavData(int i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= stream_.wavs.size()) return {};
    const auto& w = stream_.wavs[static_cast<std::size_t>(i)];
    if (w.offset + w.size > data_.size()) return {};
    return {data_.data() + w.offset, w.size};
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
    entryAt_.clear();
    fired_.clear();          // the sound latches, cleared with everything else
    sounds_.clear();
    ++restarts_;
}

bool Program::isAnim(int k) const {
    const auto id = obj_->functions[static_cast<std::size_t>(k)].id;
    return id == kFnSelectBodyAnimation || id == kFnSelectRelativeAnim;
}

int Program::animFn() const {
    if (!running_ || obj_->nfn <= 0) return -1;
    for (int k : chain(pc_)) {
        const auto& f = obj_->functions[static_cast<std::size_t>(k)];
        if (f.repeat != -1 && runs_[static_cast<std::size_t>(k)] >= f.repeat) continue;
        if (isAnim(k)) return k;
    }
    return -1;
}

const ScxFunction* Program::animFunction() const {
    const int k = animFn();
    return k < 0 ? nullptr : &obj_->functions[static_cast<std::size_t>(k)];
}

float Program::animClock() const {
    const int k = animFn();
    if (k < 0) return 0.0f;
    const auto it = entryAt_.find(k);
    return it == entryAt_.end() ? 0.0f : clock_ - it->second;
}

// The `+12` sync field is an index into the object's SYNC array, not into the
// flattened function list - `scene_read_objects` (0x00449750) resolves it as
// `obj->syncFunctions + fn->sync` and refuses the file when that lands past
// `syncFunctions + syncCount` ("Address of SyncFunction isn't valid."), and
// `Script_FunctionsIndexesToAdresses` does the same arithmetic for the sync
// records' own links. `functions` here holds both arrays end to end, main
// first, so the sync array starts at `nfn`.
//
// Read flat, every one of the 6308 shipped links lands one array too early.
// Mostly that is harmless - it turns a leading `sync = 0` into a self-loop
// and drops whatever hung off it (a link points at `Script_PlaySound` 37% of
// the time, `MoveObjectOnPath` 36%, `PlaySyncSound` 21%, and the first is
// never busy) - but at **20 sites** it lands on a different MAIN
// step and MERGES two program steps into one tick, so the object finishes at
// the longer of the two instead of their sum. `Impasse.SCX`'s `A_2_DemonLook`
// is one: clip 15 (91 frames) then clip 17 (41) is 132, and 132 is exactly the
// duration of `sautdemon`, the camera editing linked to it - the demon's jump
// off the wall, the shot before "Te voilà, enfin ! Je t'attendais...". Flat it
// runs 92, so the shot was cut 40 frames short and the line came in early.
//
// The editings are the corpus test, and a weak one: over the 95 that name an
// object, matching their own `+24` duration goes 65 -> 66, and of the 11 rows
// where the two readings disagree the sync-array one is exact on 4 against 3.
// (66 -> 69 over 8 rows when first measured - but that was taken while this
// class still spent a frame per program step, and the margin does not survive
// correcting it.) The loader is what settles the reading; what the corpus
// shows decisively is `sautdemon` alone, 132 authored against the flat
// reading's 92.
std::vector<int> Program::chain(int i) const {
    std::vector<int> out;
    std::set<int> seen;
    while (i >= 0 && static_cast<std::size_t>(i) < obj_->functions.size() &&
           !seen.count(i)) {
        seen.insert(i);
        out.push_back(i);
        const int s = obj_->functions[static_cast<std::size_t>(i)].sync;
        // -1 is "no sync" (the loader stores a null pointer); out of range is
        // a file the engine would refuse, and 0 of the 220 shipped have one
        i = (s < 0 || s >= obj_->nsync) ? -1 : obj_->nfn + s;
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
    // `Script_MoveObjectOnPath` is busy for **param 6**, its playback length
    // in frames - NOT the path's own duration, which was the first reading and
    // was wrong. The handler advances `t` by
    //
    //     v79 = Script_GetFrameTime();  v79 = duration * v79;
    //     if (param6 != 0) v79 /= param6;      // t += that
    //     ... if (t < duration) return 1;      // busy
    //
    // so the path's `duration` frames are played across `param6` ticks and the
    // busy window is param6. The corpus is unanimous: **4837 of the 4841**
    // sites author a non-zero param 6, and the commonest values are 36, 45,
    // 120 and 180 - round frame counts, 1.2 s, 1.5 s, 4 s and 6 s at 30 Hz.
    // The four zeros divide nothing, so their step is the whole duration and
    // they finish in one tick.
    //
    // Returning 0 here (which this did until 2026-09-03) makes every scripted
    // object motion instantaneous: the Impasse's crates ran 40 frames against
    // the 185 their editing `boxblow` declares, so the shot cut away before
    // they moved. 4841 uses - the most-used script function in the game.
    if (f.id == kFnMoveObjectOnPath && f.params.size() > 6) {
        const float over = asFloat(f.params[6]);
        return over > 0.0f ? over : 1.0f;
    }
    // PlaySyncSound has its own cue time, handled in tick.
    return 0.0f;
}

bool Program::tick(float dt) {
    sounds_.clear();
    motions_.clear();
    if (!running_) return false;
    bool busy = false;
    if (obj_->nfn > 0) {
        for (int k : chain(pc_)) {
            const auto& f = obj_->functions[static_cast<std::size_t>(k)];
            if (f.repeat != -1 && runs_[static_cast<std::size_t>(k)] >= f.repeat)
                continue;                      // this one has run its count out
            if (f.id == kFnPlaySyncSound) {
                // its handler holds the chain while param 1 > the clock, and
                // starts the sound on the tick the clock reaches it - once,
                // because of the latch.
                const float at = f.params.size() > 1 ? asFloat(f.params[1]) : 0.0f;
                if (clock_ < at) { busy = true; continue; }
                if (fired_.insert(k).second) {
                    SoundCue c;
                    c.wav  = f.params.empty() ? -1 : f.params[0];
                    c.loop = f.params.size() > 2 && (f.params[2] & 1);
                    c.node = f.params.size() > 4 ? f.params[4] : -1;
                    c.sync = true;
                    c.at   = at;
                    sounds_.push_back(c);
                }
                continue;
            }
            if (f.id == kFnPlaySound) {
                // no cue and never busy: it fires when the chain first reaches
                // it, which for a cutscene object is the start of its step
                if (fired_.insert(k).second) {
                    SoundCue c;
                    c.wav  = f.params.empty() ? -1 : f.params[0];
                    c.loop = f.params.size() > 1 && (f.params[1] & 1);
                    c.node = f.params.size() > 3 ? f.params[3] : -1;
                    sounds_.push_back(c);
                }
                continue;
            }
            if (f.id == kFnMoveObjectOnPath) {
                // `o3de_SetNodePos(node, Path_Sample(path, t))` - the sample
                // is placed OUTRIGHT, so this reports a world position. `t`
                // runs 0..duration across the function's own busy window, and
                // `entryAt_` is where that window began.
                const ScxPath* pa = f.params.size() > 2
                    ? rt_->pathIn(f.params[1], f.params[2]) : nullptr;
                if (pa && !pa->keys.empty()) {
                    const auto ea = entryAt_.find(k);
                    const float since = ea == entryAt_.end() ? 0.0f : clock_ - ea->second;
                    const float over = f.params.size() > 6 ? asFloat(f.params[6]) : 0.0f;
                    const float span = over > 0.0f ? over : 1.0f;
                    float u = span > 0.0f ? since / span : 1.0f;
                    if (u < 0.0f) u = 0.0f;
                    if (u > 1.0f) u = 1.0f;
                    // param 4 is the DIRECTION: 1 runs the path backwards
                    const bool back = f.params.size() > 4 && f.params[4] == 1;
                    const float t = (back ? 1.0f - u : u) *
                                    static_cast<float>(pa->duration);
                    NodeMotion m;
                    m.name = f.params.empty() ? std::string()
                                              : rt_->objectName(*obj_, f.params[0]);
                    m.t = t;
                    // Position AND orientation: the handler sets both, and an
                    // object that turns in place has only the second.
                    m.placed = pathSampleQuat(*pa, t, m.pos, m.quat);
                    if (!m.placed && !pa->keys.empty()) {
                        // past the last span: hold the final key, which is
                        // where the engine's own last sample leaves it
                        const auto& kk = back ? pa->keys.front() : pa->keys.back();
                        for (int c = 0; c < 3; ++c) m.pos[c] = kk.pos[c];
                        for (int c = 0; c < 4; ++c) m.quat[c] = kk.quat[c];
                        m.placed = true;
                    }
                    m.rotated = std::fabs(m.quat[0]) < 0.999999f;
                    if (!m.name.empty()) motions_.push_back(std::move(m));
                }
            }
            auto it = busyUntil_.find(k);
            if (it == busyUntil_.end()) {
                // THE LAST FRAME IS DRAWN ON THE TICK THAT REPORTS DONE, so a
                // function of `span` frames occupies exactly `span` ticks and
                // the pc advance costs none. `Script_SelectBodyAnimation`'s
                // tail (0x004A35D0, verified in the listing):
                //
                //     fld dt / fadd cur          ; next = cur + dt
                //     call sub_4721B0            ; Anim_Frames(clip)
                //     fcomp / test ah,41h / jnz  ; next <= frames -> al = 1
                //       call sub_4715B0          ; else CLAMP-draw the last
                //       inc [edi+14h]            ; ++runCounter
                //       cmp / jb / cmp -1 / jz   ; spent?
                //       xor al, al               ; -> NOT BUSY, this tick
                //
                // and `Script_PlayScript` does `++obj->pc; busy = 1` inside
                // that same tick, so the next step begins on the very next
                // one. Ending the window a tick later - which this did until
                // 2026-09-03 - spends a whole frame per step on nothing: the
                // Impasse demon's 132-frame beat ran 134, starting its second
                // step a frame late and outliving `sautdemon`'s 132 frames by
                // two, which is where the gap between beats came from.
                const float span = busySpan(k);
                it = busyUntil_.emplace(k, clock_ + std::max(0.0f, span - 1.0f)).first;
                entryAt_[k] = clock_;
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
                    entryAt_.erase(k);
                } else {
                    // the wrap tick is the last of this run, so the next
                    // begins on the tick after it
                    entryAt_[k] = it->second + 1.0f;
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

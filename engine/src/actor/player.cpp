// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/player.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <set>
#include <cstring>

namespace omk {
namespace {

std::uint32_t u32at(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) << 8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}
float f32at(std::span<const std::byte> d, std::size_t o) {
    const std::uint32_t b = u32at(d, o);
    float f; std::memcpy(&f, &b, 4); return f;
}

std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

const std::string kEmpty;

float wrap360(float a) {
    // Cef_ApplyTurn's own loops: subtract while > 360, add while < 0.
    while (a > 360.0f) a -= 360.0f;
    while (a < 0.0f) a += 360.0f;
    return a;
}
float wrap180(float a) {
    while (a < -180.0f) a += 360.0f;
    while (a > 180.0f) a -= 360.0f;
    return a;
}

}  // namespace

void rotateYaw(float yawDeg, const float in[3], float out[3]) {
    // Matrix3x3_FromEulerAngles(0, y, 0) is [[cy,0,sy],[0,1,0],[-sy,0,cy]] and
    // Matrix3x3_RotateVector multiplies as a ROW vector: x' = x cy - z sy,
    // z' = x sy + z cy. The same two lines `resolveCamera` carries.
    const float t = yawDeg * 0.0174532925199433f;
    const float cs = std::cos(t), sn = std::sin(t);
    const float x = in[0], y = in[1], z = in[2];
    out[0] = x * cs - z * sn;
    out[1] = y;
    out[2] = x * sn + z * cs;
}

float headingFromClipRoot(std::span<const std::byte> clip, int frame) {
    // A `.3DA` clip: +0 frames, +4 tracks, 40-byte tracks at +8 with the
    // rotation keys at +32/+36 (pose.cpp, clipTracks). The root is the track
    // that carries position keys (+24/+28) - the pelvis, in every shipped
    // model - and its quaternion at key `frame + 1` is the node's own
    // rotation.
    if (clip.size() < 8) return 0.0f;
    const int n = static_cast<int>(u32at(clip, 4));
    const int frames = static_cast<int>(u32at(clip, 0));
    if (n <= 0 || n >= 512 || frames <= 0) return 0.0f;
    for (int i = 0; i < n; ++i) {
        const std::size_t o = 8u + 40u * static_cast<std::size_t>(i);
        const int pk = static_cast<int>(u32at(clip, o + 24));
        const std::size_t po = u32at(clip, o + 28);
        const int rk = static_cast<int>(u32at(clip, o + 32));
        const std::size_t ro = u32at(clip, o + 36);
        if (!po || pk <= 0 || !ro || rk <= 0) continue;
        int key = frame + 1;
        if (key < 0) key = 0;
        if (key >= rk) key = rk - 1;
        const std::size_t q = ro + 16u * static_cast<std::size_t>(key);
        if (q + 16 > clip.size()) return 0.0f;
        // stored as the conjugate of the rotation to apply (pose.h)
        const Quatf rot{f32at(clip, q), -f32at(clip, q + 4),
                        -f32at(clip, q + 8), -f32at(clip, q + 12)};
        const float fwd[3] = {0.0f, 0.0f, -1.0f};
        float v[3];
        qrot(rot, fwd, v);
        return wrap360(std::atan2(v[2], v[0]) * 57.29577951308232f + 90.0f);
    }
    return 0.0f;
}

// --------------------------------------------------------------- setup

// `walk_set.cpp`: start on the floor UNDER the authored position, probed
// downward from one unit above it - probing up from far below finds the
// ceiling. A pelvis point (a scene clip's root) and a feet point (an
// ADDRESSES record) both land on the same floor this way.
static double seatOnFloor(const TriangleSoup& soup, const float p[3]) {
    if (const auto g = floorUnder(soup, p[0], p[1] - 1.0, p[2])) return *g;
    return p[1];
}

void PlayerController::placeAt(const float pos[3], float facing) {
    walker_.moveTo(pos[0], seatOnFloor(walker_.soup(), pos), pos[2]);
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
    for (int k = 0; k < 3; ++k) start_[k] = pos_[k];
    euler_[1] = wrap360(facing);
}

// `Cef_FindGroupById(actor+180, id)` then `SetPersoBankGroup(actor+396, g)` -
// the pair MDACTION ends on (`loc_46AFD0`: id 0x2D = 45) to carry the machine
// out of the action state and into the group whose entry is MDGETOBJ. It is
// the handler that switches the group, not a transition: entry 24, the
// MDACTION state, has NO children at all.
bool PlayerController::enterGroupById(int id) {
    const int g = rt_.channel().findGroupById(id);
    if (g < 0) return false;
    return rt_.channel().setBankGroup(g);
}

void PlayerController::nudge(const float d[3]) {
    walker_.moveTo(walker_.pos()[0] + d[0], walker_.pos()[1] + d[1], walker_.pos()[2] + d[2]);
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
}

PlayerController::PlayerController(const Setup& s)
    : ctl_(s.ctl), data_(s.ctlData), meshes_(s.meshes),
      rt_(*s.ctl, true),
      walker_(*s.soup, s.pos[0], seatOnFloor(*s.soup, s.pos), s.pos[2]) {
    // Actor_LoadModel -> Actor_LoadBankList: state 1 on the bank's default
    // group; then the cutscene had him (ScriptObject_StartOnActor -> 4) and
    // its program ended (Actor_TickScxDriven -> 1, +1308 set), which is the
    // hand-over this controller starts at.
    rt_.loadModel();
    rt_.scxStart();
    rt_.scxDrivenDone();
    walker_.setSteep(s.steep);
    // Actor_TickNpc's +1308 pass: the facing is already derived by the caller
    // (headingFromClipRoot); `SetPersoBankGroup(channel, Cef_DefaultGroup)`
    // resets the machine to the default group's default entry.
    const int g = rt_.channel().defaultGroup();
    if (g >= 0) rt_.channel().setBankGroup(g);

    // Onto the floor: the walker was seated by `seatOnFloor`.
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
    for (int k = 0; k < 3; ++k) start_[k] = pos_[k];
    euler_[0] = 0.0f; euler_[1] = wrap360(s.facing); euler_[2] = 0.0f;

    frameBefore_ = frameAfter_ = rt_.channel().frame();
    stateBefore_ = rt_.channel().state();

    // The track table, counted once: every track of every clip the bank
    // names must resolve to a mesh of the model by name.
    if (meshes_) {
        for (std::size_t c = 0; c < ctl_->clips.size(); ++c) {
            const auto d = animDescriptor(data_, ctl_->clips[c].offset);
            if (!d) continue;
            for (const auto& t : d->tracks) {
                ++total_;
                const std::string want = lower(t.name);
                for (const auto& m : *meshes_)
                    if (lower(m.name) == want) { ++matched_; break; }
            }
        }
    }
    // THE CAMERA'S SUBJECT IS A BODY POINT, not the floor point `pos_` is.
    // The model's hierarchy root is the pelvis (`pose.cpp`: track 2,
    // `UBassin`), and this is its height above the model's lowest extent -
    // 41.9 for `HO1_FNM`, the same number the dialogue staging measured from
    // the other side. Y points DOWN, so the feet are the LARGER y.
    if (meshes_ && !meshes_->empty()) {
        float feet = -1e30f;
        for (const auto& m : *meshes_)
            feet = std::max(feet, m.pos[1] + m.boxMax[1]);
        int root = 0;
        for (std::size_t i = 0; i < meshes_->size(); ++i)
            if ((*meshes_)[i].parent < 0) { root = static_cast<int>(i); break; }
        camLift_ = feet - (*meshes_)[static_cast<std::size_t>(root)].pos[1];
        if (!(camLift_ > 0.0f) || camLift_ > 200.0f) camLift_ = 0.0f;  // refuse a wild one
    }
    // the first camera frame snaps (flags & 1 after Camera_LoadParams)
    for (int k = 0; k < 3; ++k) camEuler_[k] = euler_[k];
    resolveSteady(cam_, euler_);
    camFresh_ = false;
    last_.ground = pos_[1];
    last_.onGround = true;
}

// ----------------------------------------------------------- the clip

const std::string& PlayerController::ctlStateName() const {
    const int s = rt_.channel().state();
    if (s < 0 || s >= static_cast<int>(ctl_->states.size())) return kEmpty;
    return ctl_->states[static_cast<std::size_t>(s)].name;
}

int PlayerController::clipOwner() const {
    int s = rt_.channel().state();
    const auto& S = ctl_->states;
    for (int guard = 0; guard < 64; ++guard) {
        if (s < 0 || s >= static_cast<int>(S.size())) return -1;
        if (!(S[static_cast<std::size_t>(s)].flags & 0x8002u)) return s;
        s = S[static_cast<std::size_t>(s)].gotoIdx;
    }
    return -1;
}

int PlayerController::clip() const {
    const int s = clipOwner();
    if (s < 0 || s >= static_cast<int>(ctl_->states.size())) return -1;
    const int c = ctl_->states[static_cast<std::size_t>(s)].clip;
    return (c >= 0 && c < static_cast<int>(ctl_->clips.size())) ? c : -1;
}

const std::string& PlayerController::clipName() const {
    const int c = clip();
    return c < 0 ? kEmpty : ctl_->clips[static_cast<std::size_t>(c)].name;
}

int PlayerController::clipFrames() const {
    const int c = clip();
    return c < 0 ? 0 : ctl_->clips[static_cast<std::size_t>(c)].frames;
}

const PlayerController::RootTrack* PlayerController::rootTrackOf(int clip) {
    if (clip < 0) return nullptr;
    auto it = roots_.find(clip);
    if (it != roots_.end()) return it->second.keys > 0 ? &it->second : nullptr;
    RootTrack r;
    if (const auto d = animDescriptor(data_, ctl_->clips[static_cast<std::size_t>(clip)].offset)) {
        // the one track with position keys - `sub_437FE0` walks the tracks
        // for the first whose position pointer is set, and the shipped
        // clips have exactly one
        for (const auto& t : d->tracks) {
            if (!t.posOffset || t.posKeys <= 0) continue;
            if (t.posOffset + 12u * static_cast<std::size_t>(t.posKeys) > data_.size()) continue;
            r.offset = t.posOffset;
            r.keys   = t.posKeys;
            break;
        }
    }
    it = roots_.emplace(clip, r).first;
    return it->second.keys > 0 ? &it->second : nullptr;
}

// Anim_RootDelta (0x004711D0), transcribed. `key(k)` is the 12-byte position
// key k; the sum runs over the keys whose interval (k-1, k] the span
// (prev, cur] covers, with the two fractional ends scaled.
void PlayerController::rootDelta(const RootTrack& t, float prev, float cur,
                                 float out[3]) const {
    const auto key = [&](int k, float* v) {
        if (k < 0) k = 0;
        if (k >= t.keys) k = t.keys - 1;
        const std::size_t o = t.offset + 12u * static_cast<std::size_t>(k);
        for (int c = 0; c < 3; ++c) v[c] = f32at(data_, o + 4u * static_cast<std::size_t>(c));
    };
    float acc[3] = {0, 0, 0}, kv[3];
    const float c0 = std::ceil(prev);
    if (c0 <= cur) {
        if (c0 > prev) {                       // the fractional head
            key(static_cast<int>(c0), kv);
            for (int c = 0; c < 3; ++c) acc[c] += (c0 - prev) * kv[c];
        }
        const float f1 = std::floor(cur);
        float k = c0;
        while (k < f1) {                       // whole keys c0+1 .. f1
            key(static_cast<int>(k) + 1, kv);
            for (int c = 0; c < 3; ++c) acc[c] += kv[c];
            k += 1.0f;
        }
        if (cur > k) {                         // the fractional tail
            key(static_cast<int>(k) + 1, kv);
            for (int c = 0; c < 3; ++c) acc[c] += kv[c] * (cur - k);
        }
    } else {
        // both ends inside one key's interval
        key(static_cast<int>(c0), kv);
        for (int c = 0; c < 3; ++c) acc[c] = (cur - prev) * kv[c];
    }
    for (int c = 0; c < 3; ++c) out[c] = acc[c];
}

void PlayerController::rotateByFacing(const float in[3], float out[3]) const {
    rotateYaw(euler_[1], in, out);
}

void PlayerController::applyTurn(const float d[3]) {
    // Cef_ApplyTurn: add, then wrap +416 and +420 into 0..360 (+424 is
    // added and not wrapped - the original wraps v2[104] and v2[105] only).
    euler_[0] = wrap360(euler_[0] + d[0]);
    euler_[1] = wrap360(euler_[1] + d[1]);
    euler_[2] = euler_[2] + d[2];
}

// Cef_TickChannel 29_win32.c 330-410: how much of a state's window
// [start, end) this tick's frame advance (prev -> cur) covered.
bool PlayerController::windowPortion(float prev, float cur, float start,
                                     float end, float& portion) {
    bool a = false, b = false;
    if (prev >= cur) {
        // a wrap or a fresh entry: the advance is measured from frame 1, or
        // the window is closed out whole
        if (cur >= start && cur < end) { prev = 1.0f; a = true; }
        if (cur >= end) { prev = start; cur = end; a = true; }
    } else {
        if (prev >= start && cur < end) a = true;
        if (prev < end && cur > end) b = true;
    }
    if (a) portion = cur - prev;
    if (b) portion = end - prev;
    return a || b;
}

// ---------------------------------------------------------------- tick

void PlayerController::tick(float dt, std::uint32_t word) {
    ++ticks_;
    last_ = Frame{};
    const auto& S = ctl_->states;

    // sub_4A7A20 (0x004A7A20): `*word = 0; if (!held) *word = 0x40000000;` -
    // nothing held is the IDLE word, not zero, and that is what opens the
    // stop edges (an entry whose +4 only FORBIDS bits, 0x20000 = "forward
    // released", opens on it).
    if (word == 0) word = kIdleInput;

    // ---- Cef_TickChannel's window passes, on LAST tick's advance -------
    //
    // The engine reads the channel's +12/+16 - the previous and current
    // frame as the previous tick left them - against the CURRENT entry's
    // window, before it searches for a transition.
    const int cur = rt_.channel().state();
    if (cur >= 0 && cur < static_cast<int>(S.size()) &&
        !(rt_.channel().flags() & 0x200u)) {
        const CtlState& st = S[static_cast<std::size_t>(cur)];
        float portion = 0.0f;
        if ((st.flags & 0x80u) && st.hasShift &&
            windowPortion(frameBefore_, frameAfter_, st.shift[0], st.shift[1], portion)) {
            const float local[3] = {portion * st.shift[2], portion * st.shift[3],
                                    portion * st.shift[4]};
            float w[3];
            rotateByFacing(local, w);              // Cef_ApplyRootShift: by +288
            for (int k = 0; k < 3; ++k) last_.shift[k] = w[k];
        }
        if ((st.flags & 0x40u) && st.hasTurn &&
            windowPortion(frameBefore_, frameAfter_, st.turn[0], st.turn[1], portion)) {
            const float d[3] = {portion * st.turn[2], portion * st.turn[3],
                                portion * st.turn[4]};
            applyTurn(d);                          // Cef_ApplyTurn
            last_.turn = d[1];
        }
    }

    // ---- the machine ---------------------------------------------------
    const float f0 = rt_.channel().frame();
    const int   s0 = rt_.channel().state();
    rt_.tick(dt, word);
    const float f1 = rt_.channel().frame();
    const int   s1 = rt_.channel().state();
    frameBefore_ = f0;
    frameAfter_  = f1;
    stateBefore_ = s0;
    // The whole-on-transition blocks (0x100 turn / 0x200 shift) arrive as
    // channel events; apply the ones this tick produced.
    moves_.clear();
    for (const auto& e : rt_.channel().events()) {
        // `Cef_QueueSpecialMove` -> `tab_special_move[]`, the binary's own
        // 66-row table of engine callbacks. The channel has always emitted
        // these and nothing consumed them, which is why the world TAKE never
        // worked: it IS one of those handlers (MDACTION scans for an object,
        // MDGETOBJ takes it, MDPUTSNK banks it, MDLETOBJ puts it back). The
        // handlers need the world, which this class does not have, so the
        // names are collected and the frontend runs them.
        if (e.kind == ChannelEvent::Kind::Move && !e.name.empty())
            moves_.push_back(e.name);
        if (e.kind == ChannelEvent::Kind::Turn && e.from >= 0 &&
            e.from < static_cast<int>(S.size())) {
            const CtlState& f = S[static_cast<std::size_t>(e.from)];
            const float d[3] = {f.turn[2], f.turn[3], f.turn[4]};
            applyTurn(d);
            last_.turn += d[1];
        } else if (e.kind == ChannelEvent::Kind::Shift && e.from >= 0 &&
                   e.from < static_cast<int>(S.size())) {
            const CtlState& f = S[static_cast<std::size_t>(e.from)];
            const float local[3] = {f.shift[2], f.shift[3], f.shift[4]};
            float w[3];
            rotateByFacing(local, w);
            for (int k = 0; k < 3; ++k) last_.shift[k] += w[k];
        } else if (e.kind == ChannelEvent::Kind::TurnRate && e.to >= 0 &&
                   e.to < static_cast<int>(S.size())) {
            // sub_45C080: the CANDIDATE's block, times the frame dt, with the
            // state unchanged. This is how the game turns while walking - the
            // gait keeps playing and only the facing moves - and it is what
            // makes DIAGONAL movement work. H1Avnt group 0 carries the turn on
            // four aliases (input 0x01/0x02, +-5 deg/frame and +-3), never on
            // H_WALK itself, so reading `from` here applies zero.
            const CtlState& c = S[static_cast<std::size_t>(e.to)];
            const float d[3] = {c.turn[2] * dt, c.turn[3] * dt, c.turn[4] * dt};
            applyTurn(d);
            last_.turn += d[1];
        } else if (e.kind == ChannelEvent::Kind::ShiftRate && e.to >= 0 &&
                   e.to < static_cast<int>(S.size())) {
            const CtlState& c = S[static_cast<std::size_t>(e.to)];
            const float local[3] = {c.shift[2] * dt, c.shift[3] * dt,
                                    c.shift[4] * dt};
            float w[3];
            rotateByFacing(local, w);
            for (int k = 0; k < 3; ++k) last_.shift[k] += w[k];
        }
    }
    rt_.channel().clearEvents();

    // ---- sub_45C680 case 1: the clip's root delta over the advance -------
    //
    // A transition resets both frames to the start (Actor_PlayClip zeroes
    // the previous frame), so the advance that crossed it applies nothing -
    // which is what keeps a looping walk from jumping back on every wrap.
    float local[3] = {0, 0, 0};
    if (s1 == s0 && f1 >= f0) {
        if (const RootTrack* rt = rootTrackOf(clip())) rootDelta(*rt, f0, f1, local);
    }
    float world[3];
    rotateByFacing(local, world);                  // Anim_RootDelta's 3x3 = +288
    for (int k = 0; k < 3; ++k) last_.rootDelta[k] = world[k];

    // ---- Actor_ApplyMotion: try the move, let the ground decide ----------
    //
    // The position delta since the last safe stance is what Actor_Move gets:
    // the root delta and any root shift, x and z only in state 1 (+248 takes
    // the delta only in states 11..14). The walker port is the probe half:
    // no floor under the destination -> revert; a rise past the step -> a
    // wall; a drop past it -> a ledge. Actor_Move's collide-and-slide is
    // not ported (README, `engine walk`), so a blocked step stops instead of
    // sliding.
    const double dx = static_cast<double>(world[0] + last_.shift[0]);
    const double dz = static_cast<double>(world[2] + last_.shift[2]);
    if (std::fabs(dx) > 1e-6 || std::fabs(dz) > 1e-6) {
        last_.stepped = true;
        last_.step = walker_.step(dx, dz, dt);
    } else {
        // Standing still horizontally is not standing still. An actor who
        // stepped off a ledge is FALLING, and `Walk_GroundResponse` runs every
        // frame whatever `Actor_Move` was handed - so the vertical half is
        // owed a tick even when the clip produces no root delta. Without it he
        // hangs in the air until he asks to move again.
        last_.step = walker_.tick(dt);
    }
    pos_[0] = static_cast<float>(walker_.pos()[0]);
    pos_[1] = static_cast<float>(walker_.pos()[1]);
    pos_[2] = static_cast<float>(walker_.pos()[2]);
    // Actor_ApplyMotion's 1/8 steer toward the motion direction: the delta
    // was rotated by the facing, so the two headings coincide and the turn
    // is 0. Written out rather than skipped so the reason is on the page.
    if (last_.stepped && last_.step == StepResult::Moved) {
        // `mx, mz` would be what Actor_Move let through; with no slide it is
        // the request itself.
        const double mx = dx, mz = dz;
        if (std::fabs(dx) > 0.0001 && std::fabs(mx) > 0.0001) {
            double turn = (std::atan2(mz, mx) - std::atan2(dz, dx)) * 57.29577951308232;
            if (turn < -180.0) turn += 360.0;
            if (turn > 180.0) turn -= 360.0;
            euler_[1] = wrap360(euler_[1] + static_cast<float>(turn * 0.125));
        }
    }
    if (const auto g = walker_.ground(pos_[0], pos_[1], pos_[2])) {
        last_.ground = *g;
        last_.onGround = std::fabs(*g - pos_[1]) <= kStepUp + 1.0;
    }

    // ---- the camera tick, sub_417CF0 -> sub_415D10 / sub_415E60 ---------
    FollowCamera target;
    if (camFresh_) {
        for (int k = 0; k < 3; ++k) camEuler_[k] = euler_[k];
        resolveSteady(cam_, camEuler_);
        camFresh_ = false;
    } else {
        // the lagged Euler triple: chase, snapping inside 0.1 degrees
        const int k46 = camF46_ == 1 ? 2 : camF46_;
        for (int k = 0; k < 3; ++k) {
            if (!k46) { camEuler_[k] = euler_[k]; continue; }
            const float d = wrap180(euler_[k] - camEuler_[k]);
            if (std::fabs(d) <= 0.1f) camEuler_[k] = euler_[k];
            else camEuler_[k] += d * dt / static_cast<float>(k46);
        }
        resolveSteady(target, camEuler_);
        const int k42 = camF42_ == 1 ? 2 : camF42_;
        const int k44 = camF44_ == 1 ? 2 : camF44_;
        for (int k = 0; k < 3; ++k) {
            if (k42) cam_.at[k] += (target.at[k] - cam_.at[k]) * dt / static_cast<float>(k42);
            else cam_.at[k] = target.at[k];
            if (k44) cam_.eye[k] += (target.eye[k] - cam_.eye[k]) * dt / static_cast<float>(k44);
            else cam_.eye[k] = target.eye[k];
        }
        cam_.fov = camFov_;
    }
}

// ------------------------------------------------------------- posing

const NodeTracks* PlayerController::poseTracks() {
    const int c = clip();
    if (c < 0 || !meshes_) return nullptr;
    auto it = tracks_.find(c);
    if (it != tracks_.end()) return it->second.valid() ? &it->second : nullptr;
    NodeTracks t;
    const auto d = animDescriptor(data_, ctl_->clips[static_cast<std::size_t>(c)].offset);
    if (d && d->frames > 0) {
        t.count = static_cast<int>(d->tracks.size());
        t.frames = d->frames;
        t.rootTrack = -1;
        for (const auto& tr : d->tracks) {
            int mi = -1;
            const std::string want = lower(tr.name);
            for (const auto& m : *meshes_)
                if (lower(m.name) == want) { mi = m.index; break; }
            t.ids.push_back(mi);
        }
        t.quats.assign(static_cast<std::size_t>(d->frames), {});
        t.trans.assign(static_cast<std::size_t>(d->frames), {0.0f, 0.0f, 0.0f});
        for (int f = 0; f < d->frames; ++f) {
            auto& row = t.quats[static_cast<std::size_t>(f)];
            row.resize(d->tracks.size());
            for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                const AnimTrack& tr = d->tracks[i];
                if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                // key 0 is the REST SENTINEL: frame f reads key f + 1
                int key = f + 1;
                if (key >= tr.rotKeys) key = tr.rotKeys - 1;
                const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
                if (o + 16 > data_.size()) continue;
                row[i] = {f32at(data_, o), f32at(data_, o + 4),
                          f32at(data_, o + 8), f32at(data_, o + 12)};
            }
        }
    }
    it = tracks_.emplace(c, std::move(t)).first;
    return it->second.valid() ? &it->second : nullptr;
}

int PlayerController::poseFrame() const {
    // the channel's frame runs 1 .. clipLen; the tracks are 0-based
    int f = static_cast<int>(std::floor(rt_.channel().frame())) - 1;
    const int n = clipFrames();
    if (f < 0) f = 0;
    if (n > 0 && f >= n) f = n - 1;
    return f;
}

// ------------------------------------------------------------- camera

void PlayerController::setCameraOffsets(const float eyeOff[3], const float atOff[3],
                                        float fov, int f42, int f44, int f46) {
    for (int k = 0; k < 3; ++k) { camEyeOff_[k] = eyeOff[k]; camAtOff_[k] = atOff[k]; }
    camFov_ = fov > 1.0f ? fov : kFollowFov;
    camF42_ = f42; camF44_ = f44; camF46_ = f46;
    camFresh_ = true;      // Camera_LoadParams sets flag 1: the next frame snaps
}

void PlayerController::resolveSteady(FollowCamera& c, const float e[3]) const {
    // point = subjectPos - R(euler) * offset, per point (sub_415D10 for the
    // target with the SUBJECT's euler, sub_415E60 for the eye with the lagged
    // copy). Only the yaw is non-zero here - see the header.
    float r[3];
    // The subject, which is the actor's ORIGIN and not the floor point - see
    // `cameraLift()`. Y points down, so raising it is a subtraction.
    const float sub[3] = {pos_[0], pos_[1] - camLift_, pos_[2]};
    rotateYaw(e[1], camEyeOff_, r);
    for (int k = 0; k < 3; ++k) c.eye[k] = sub[k] - r[k];
    rotateYaw(euler_[1], camAtOff_, r);
    for (int k = 0; k < 3; ++k) c.at[k] = sub[k] - r[k];
    c.fov = camFov_;
}

FollowCamera PlayerController::followCameraSteady() const {
    FollowCamera c;
    resolveSteady(c, euler_);
    return c;
}

double PlayerController::distanceWalked() const {
    const double dx = pos_[0] - start_[0], dz = pos_[2] - start_[2];
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace omk

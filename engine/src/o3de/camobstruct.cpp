// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/camobstruct.h"

#include <cmath>

namespace omk {

void obstructCamera(CamObstruct& st, float eyeIo[3], float atIo[3],
                    double lift312, double lift316, float dt, bool justChanged,
                    const std::function<double(const double from[3], const double ray[3])>& cast,
                    const std::function<bool(float eye[3], float at[3])>& unlagged) {
    const double eye[3] = {eyeIo[0], eyeIo[1], eyeIo[2]};
    const double at[3]  = {atIo[0],  atIo[1],  atIo[2]};
    double D[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
    const double d = std::sqrt(D[0]*D[0] + D[1]*D[1] + D[2]*D[2]);
    if (d < 1e-3) return;
    constexpr double kOver = 1.2;      // +300
    constexpr double kOut  = 8.0;      // +320
    constexpr double kLift = 4.0;      // +324
    // the over-reach ray, target -> 1.2x the eye distance
    double ray[3] = {D[0] * kOver, D[1] * kOver, D[2] * kOver};
    const double best = cast(at, ray);
    double r = d;
    if (best <= 1.0) {
        if (!st.block) st.dist = static_cast<float>(d);
        st.block = 1;
        // |hit - target| is `best` of the 1.2x ray, and the engine divides
        // that by +300 to undo the over-reach - so the free distance is
        // simply `best * d`.
        r = best * d;
        if (r > st.dist && !justChanged) r = (r - st.dist) * dt / kOut + st.dist;
        st.dist = static_cast<float>(r);
    } else if (st.block == 1) {
        // THE SECOND RAY. The engine does not go straight to recovery: with
        // no hit on the over-reach ray and `+208 == 1` it rebuilds both ends
        // from the SUBJECT's own euler and offsets - `+100..+120` and
        // `+152..+172`, which `sub_414F30` fills with the actor's position
        // and Euler triple, i.e. the camera with no lag in it - and casts
        // again. Only if THAT misses does it go to state 2. So a camera whose
        // lagged ray has swung clear of the wall its unlagged one is still
        // behind does not start recovering yet.
        float ue[3], ua[3];
        double best2 = 2.0;
        if (unlagged && unlagged(ue, ua)) {
            const double at2[3] = {ua[0], ua[1], ua[2]};
            const double D2[3] = {ue[0] - ua[0], ue[1] - ua[1], ue[2] - ua[2]};
            const double d2 = std::sqrt(D2[0]*D2[0] + D2[1]*D2[1] + D2[2]*D2[2]);
            if (d2 > 1e-3) {
                const double ray2[3] = {D2[0] * kOver, D2[1] * kOver, D2[2] * kOver};
                best2 = cast(at2, ray2);
            }
        }
        if (best2 <= 1.0) {
            // STILL BLOCKED, and this arm is a RETURN in the engine - it does
            // not reach the height push at all. The eye goes back to `+328`
            // along the current direction, and BOTH heights are frozen at
            // last frame's answer shifted by the subject's own rise this
            // frame (`+24 + (+340 - +344)`, `+36 + (+340 - +344)`). `+208`
            // stays 1, so the next frame's first ray decides again.
            const double kept = st.dist;
            const double dy   = st.prevValid
                                ? static_cast<double>(st.subjY - st.prevSubjY) : 0.0;
            eyeIo[0] = static_cast<float>(at[0] + D[0] * kept / d);
            eyeIo[2] = static_cast<float>(at[2] + D[2] * kept / d);
            if (st.prevValid) {
                eyeIo[1] = static_cast<float>(st.prevEyeY + dy);
                atIo[1]  = static_cast<float>(st.prevAtY  + dy);
            } else {
                eyeIo[1] = static_cast<float>(at[1] + D[1] * kept / d);
            }
            return;
        } else {
            st.block = 2;
            if (d <= st.dist) { st.block = 0; return; }
            st.dist = static_cast<float>((d - st.dist) * dt / kOut + st.dist);
            r = st.dist;
        }
    } else if (st.block == 2) {
        if (d <= st.dist) { st.block = 0; return; }
        st.dist = static_cast<float>((d - st.dist) * dt / kOut + st.dist);
        r = st.dist;
    } else {
        return;                                   // clear and was clear
    }
    const double k = r / d;
    float e[3];
    for (int i = 0; i < 3; ++i) e[i] = static_cast<float>(at[i] + D[i] * k);
    // THE LIFT, blended in as the camera closes past half its free distance.
    // `+156` is the eye's SUBJECT position - the actor's own origin, which is
    // his pelvis, `pos_[1] - camLift_` (the walker keeps `pos_` on the floor
    // and Y grows downward). `+312`/`+316` are -0.7x the pelvis height, so a
    // pinched camera rises ABOVE him rather than dropping through the floor.
    const double half = d * 0.5;
    const double t = r <= half ? 0.0 : (r - half) / (d - half);
    const double subjY = static_cast<double>(st.subjY);   // +156, latched by the tick
    const double liftE = lift312 + subjY;                  // +312 + +156
    const double liftA = lift316 + subjY;                  // +316 + +156
    // THE FAR END OF THE BLEND IS THE UNPULLED Y. The engine holds the pulled
    // eye in a scratch vector and only stores it at the very end, so `+56`
    // and `+68` here are still what the resolvers left - `eyeIo[1]` and
    // `atIo[1]`, not `e[1]`.
    double ey = liftE * (1.0 - t) + static_cast<double>(eyeIo[1]) * t;
    double ty = liftA * (1.0 - t) + static_cast<double>(atIo[1])  * t;
    // ...each then eased toward `+24` / `+36` - LAST FRAME'S FINAL eye and
    // target Y, which is what makes this a filter that converges on the push
    // instead of a fixed `dt/+324` fraction of it. Flag 1 (the camera changed
    // this frame) skips the ease and snaps.
    if (!justChanged && st.prevValid) {
        ey = (ey - st.prevEyeY) * dt / kLift + st.prevEyeY;
        ty = (ty - st.prevAtY)  * dt / kLift + st.prevAtY;
    }
    atIo[1] = static_cast<float>(ty);
    e[1]       = static_cast<float>(ey);
    for (int i = 0; i < 3; ++i) eyeIo[i] = e[i];
}

}  // namespace omk

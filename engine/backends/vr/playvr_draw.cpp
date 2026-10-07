// SPDX-License-Identifier: GPL-3.0-or-later
// THE EYES, DRAWN (`todo/quest-port.md` §5 step 1). Called by
// `PlayState::worldMirror()` where the frame would draw its one picture; true
// means the eyes were drawn here and the flat draw, its GPU present and its
// readback are skipped. The fake headset puts them side by side in `fb`; a
// headset frontend (step 5) will take each eye into its own swapchain image.
//
// Each eye is a whole pass through the SAME draw list - culled once, by the
// camera `playvr_camera.cpp` left in `view` - and through `drawWithMirror`,
// so a mirror reflects for each eye from that eye.
#if OMK_VR

#include "../sdl/playframe.h"

#include <algorithm>

#if OMK_OPENXR
#include "../openxr/xrhost.h"
namespace omk {
bool glesPresentEye(Renderer*, int ew, int eh, int winW, int winH);
void glesSetWindowTarget(Renderer*, unsigned fbo);
}
#endif

bool PlayState::vrWorldDraw(const omk::View& drawn, const omk::MirrorPlane& plane) {
    if (!vr.on || vr.mono || !vr.haveEyes) return false;
    omk::Renderer& world = *world_;
#if OMK_OPENXR
    omk::xr::setQuadOverEyes(openScreen >= 0);
#endif
    phRb0 = phaseNow();
    for (int e = 0; e < 2; ++e) {
        omk::View ev = drawn;
        ev.cam = vr.eye[e];
        ev.vx = ev.vy = 0;
        ev.vw = vr.eyeW;
        ev.vh = vr.eyeH;
        omk::drawWithMirror(world, draws, ev, plane);
#if OMK_OPENXR
        // THE HEADSET (step 5b): the eye's picture presented straight into its
        // swapchain image, no readback; the frame then submits the eyes
        {
            unsigned fbo = 0;
            int w = 0, h = 0;
            if (omk::xr::eyeTarget(e, fbo, w, h)) {
                omk::glesSetWindowTarget(&world, fbo);
                omk::glesPresentEye(&world, vr.eyeW, vr.eyeH, w, h);
                omk::glesSetWindowTarget(&world, 0);
                omk::xr::eyeDone(e);
                continue;
            }
        }
#endif
        const omk::Surface& pic = world.readback();
        // the backend drew the eye into the top-left `eyeW x eyeH`
        const int w = std::min(vr.eyeW, pic.w), h = std::min({vr.eyeH, pic.h, fb.h});
        const int x0 = e * vr.eyeW;
        for (int y = 0; y < h; ++y)
            std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                      pic.px.begin() + static_cast<long>(y) * pic.w + std::min(w, fb.w - x0),
                      fb.px.begin() + static_cast<long>(y) * fb.w + x0);
    }
    phRb1 = phaseNow();
    mark("vr: both eyes");
    ++worldFrames;
    return true;
}

#endif  // OMK_VR

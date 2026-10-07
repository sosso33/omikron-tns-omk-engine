// SPDX-License-Identifier: GPL-3.0-or-later
// THE VR SETUP - the `--vr-*` flags, read here and not in `PlayOptions`, so
// the VR build's command line lives with the VR code (`todo/quest-port.md` §5).
// Every flag is ONE word (`--vr-head=10,0,0`): the main parser ignores what it
// does not know, but it would take a separate value word for a screen number.
#if OMK_VR

#include "../sdl/playframe.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

bool takeTriple(const std::string& a, const char* flag, float out[3]) {
    const std::size_t n = std::strlen(flag);
    if (a.compare(0, n, flag) != 0) return false;
    float v[3] = {0.0f, 0.0f, 0.0f};
    if (std::sscanf(a.c_str() + n, "%f,%f,%f", &v[0], &v[1], &v[2]) < 1) return false;
    for (int k = 0; k < 3; ++k) out[k] = v[k];
    return true;
}

void vrUsage() {
    std::printf(
        "VR (a VR build: the main Makefile's VR=1; todo/quest-port.md):\n"
        "  --vr-sim             the desktop's fake headset: both eyes side by side\n"
        "  --vr-sim=mono        one eye over the whole frame (the frame check's form)\n"
        "  --vr-camera=level|full  the authored camera's pitch and roll dropped\n"
        "                       (level, the default) or kept\n"
        "  --vr-head=Y,P,R      the fake head in degrees: yaw right, pitch up, roll\n"
        "                       to the right shoulder; the numpad turns it live\n"
        "                       (4/6 yaw, 8/2 pitch, 7/9 roll, 5 recentres)\n"
        "  --vr-headpos=X,Y,Z   the fake head's offset in metres (x right, y up, z back)\n"
        "  --vr-ipd=MM          the distance between the eyes (64)\n"
        "  --vr-fov=quest2      a nominal asymmetric eye instead of the authored fov\n"
        "  --vr-adventure=first|authored  adventure in first person, moving where\n"
        "                       the head looks (first, the default) or the game's camera\n"
        "  --vr-fight=calm|authored  the fight camera at a frozen distance, turning\n"
        "                       slowly (calm, the default) or as the game moves it\n"
        "  --vr-fight-turn=DEG  the calm fight camera's turn a frame at 30 fps (1)\n"
        "  --vr-recentre=cut|scene  the head re-zeroed at every cut (the default)\n"
        "                       or only when the camera's kind changes\n"
        "  numpad 1/3           first person and shoot mode: a 30-degree snap turn\n"
        "  --vr-aim=Y,P         the fake right controller, degrees in the headset's\n"
        "                       space (yaw right, pitch up); the mouse moves it in shoot\n"
        "                       mode, where it AIMS - the body and the look pitch follow it\n"
        "  --vr-head-after=F:Y,P,R  the fake head turned at frame F (after a recentre)\n"
        "  OMK_VRLOG=1          a line a frame: the kind, the authored and drawn eye, him\n");
}

}  // namespace

void PlayState::vrSetup(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.compare(0, 4, "--vr") != 0) continue;
        float t[3];
        if (a == "--vr-help") { vrUsage(); std::exit(0); }
        else if (a == "--vr-sim") { vr.on = vr.sim = true; }
        else if (a == "--vr-sim=mono") { vr.on = vr.sim = vr.mono = true; }
        else if (a == "--vr-camera=level") vr.orient = omk::vr::CameraOrientation::Level;
        else if (a == "--vr-camera=full") vr.orient = omk::vr::CameraOrientation::Full;
        else if (a == "--vr-fov=quest2") vr.questFov = true;
        else if (a == "--vr-adventure=first") vr.adventureFirst = true;
        else if (a == "--vr-adventure=authored") vr.adventureFirst = false;
        else if (a == "--vr-fight=calm") vr.fightCalm = true;
        else if (a == "--vr-fight=authored") vr.fightCalm = false;
        else if (a.compare(0, 16, "--vr-fight-turn=") == 0) vr.fightTurnDeg = static_cast<float>(std::atof(a.c_str() + 16));
        else if (a == "--vr-recentre=cut") vr.recentreEachCut = true;
        else if (a == "--vr-recentre=scene") vr.recentreEachCut = false;
        else if (a.compare(0, 9, "--vr-aim=") == 0) {
            float y = 0.0f, p = 0.0f;
            if (std::sscanf(a.c_str() + 9, "%f,%f", &y, &p) >= 1) { vr.ctlYaw = y; vr.ctlPitch = p; }
        }
        else if (a.compare(0, 16, "--vr-head-after=") == 0) {
            long f = -1;
            float y = 0.0f, p = 0.0f, r = 0.0f;
            if (std::sscanf(a.c_str() + 16, "%ld:%f,%f,%f", &f, &y, &p, &r) >= 2) {
                vr.headAfterFrame = f;
                vr.headAfter[0] = y; vr.headAfter[1] = p; vr.headAfter[2] = r;
            }
        }
        else if (a.compare(0, 9, "--vr-ipd=") == 0) vr.ipdMm = static_cast<float>(std::atof(a.c_str() + 9));
        else if (takeTriple(a, "--vr-headpos=", t)) for (int k = 0; k < 3; ++k) vr.headPos[k] = t[k];
        else if (takeTriple(a, "--vr-head=", t)) { vr.yaw = t[0]; vr.pitch = t[1]; vr.roll = t[2]; }
        else { std::fprintf(stderr, "%s: not a VR flag (--vr-help)\n", a.c_str()); std::exit(2); }
    }
    if (vr.on)
        std::printf("vr: %s, camera %s, ipd %.0f mm, head yaw %.1f pitch %.1f roll %.1f, "
                    "head at %.3f %.3f %.3f m%s\n",
                    vr.mono ? "fake headset, ONE eye (mono)" : "fake headset, both eyes side by side",
                    vr.orient == omk::vr::CameraOrientation::Level ? "LEVEL" : "FULL",
                    static_cast<double>(vr.ipdMm), static_cast<double>(vr.yaw),
                    static_cast<double>(vr.pitch), static_cast<double>(vr.roll),
                    static_cast<double>(vr.headPos[0]), static_cast<double>(vr.headPos[1]),
                    static_cast<double>(vr.headPos[2]),
                    vr.questFov ? ", a nominal Quest eye" : "");
    if (vr.on)
        std::printf("vr: adventure %s, fight %s (%.1f deg a frame), recentre at %s, seated\n",
                    vr.adventureFirst ? "FIRST PERSON, moving where the head looks" : "on the game's camera",
                    vr.fightCalm ? "CALM - frozen distance" : "on the game's camera",
                    static_cast<double>(vr.fightTurnDeg),
                    vr.recentreEachCut ? "every cut" : "a change of camera kind");
}

#endif  // OMK_VR

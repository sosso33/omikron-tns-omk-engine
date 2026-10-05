// SPDX-License-Identifier: GPL-3.0-or-later
// THE CAMERA SHAKE (`todo/astaroth.md` step 4): `Camera_SetShake` and the
// camera tick's `sub_418030`, through the Session.
//
//     shake_probe <gamedata> <tables>
//
// Three lines:
//
//   direct  <frames> <dy0> <dy1> <sum>        Camera_SetShake(30, 10), stepped
//                                             at dt 1 until it stops
//   renewed <frames>                          a second (30, 10) at frame 10 -
//                                             the elapsed clock is NOT reset
//   script  <shakes> <frames>                 AREA 175's message 0 (its hurt
//                                             handler runs `camera.shake 20, 20`)
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: shake_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    {
        s.cameraShake(30.0f, 10);
        int frames = 0;
        float dy0 = 0.0f, dy1 = 0.0f;
        double sum = 0.0;
        while (s.cameraShaking() && frames < 1000) {
            const float dy = s.cameraShakeStep(1.0f);
            if (frames == 0) dy0 = dy;
            if (frames == 1) dy1 = dy;
            sum += dy;
            ++frames;
        }
        std::printf("direct %d %.4f %.4f %.4f\n", frames, double(dy0), double(dy1), sum);
    }
    {
        s.cameraShake(30.0f, 10);
        int frames = 0;
        while (s.cameraShaking() && frames < 1000) {
            if (frames == 10) s.cameraShake(30.0f, 10);
            s.cameraShakeStep(1.0f);
            ++frames;
        }
        std::printf("renewed %d\n", frames);
    }
    {
        s.loadArea(175);
        for (int k = 0; k < 2; ++k) {
            if (s.residentSlot(k).areaCtx >= 0) s.freeContext(s.residentSlot(k).areaCtx);
            if (s.residentSlot(k).sceneCtx >= 0) s.freeContext(s.residentSlot(k).sceneCtx);
        }
        s.frame();
        const long before = s.cameraShakes();
        s.postMessage(0, -1);
        for (int f = 0; f < 3; ++f) s.frame();
        int frames = 0;
        while (s.cameraShaking() && frames < 1000) {
            s.cameraShakeStep(1.0f);
            ++frames;
        }
        std::printf("script %ld %d\n", s.cameraShakes() - before, frames);
    }
    return 0;
}

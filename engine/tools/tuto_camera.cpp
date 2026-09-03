// SPDX-License-Identifier: GPL-3.0-or-later
// AREA 222's tutorial shots, resolved the way the viewer resolves them.
//
// A relative camera's offset is measured from the actor's PELVIS, not from
// the ground point the walker keeps: `resolveSteady` subtracts `camLift_`
// before calling `resolveCamera`, and Y points DOWN so subtracting raises.
// The follow path has done that since issue 49; the scripted path did not,
// so every shot naming a subject sat one lift too low.
//
//     tuto_camera <gamedata> <vm_opcodes.json> <START> <lift>
#include "script/area.h"
#include "o3de/worldcam.h"
#include "platform/datafs.h"
#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: tuto_camera <gamedata> <vm_opcodes.json> <START> <lift> [out.bin]\n");
        return 2;
    }
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    const float lift = static_cast<float>(std::atof(argv[4]));
    omk::Session s(std::string(argv[1]) + "/IAM", state, table);
    s.loadArea(222);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    const omk::WorldCameras& cams = s.cameras();
    // The alley tutorial's three shots, from AREA 222 zone 3795's script.
    const int ids[3] = {4290, 4291, 4292};
    const float foot[3] = {6753.0f, 397.0f, 3021.0f};      // the arrival address
    const float subj[3] = {foot[0], foot[1] - lift, foot[2]};
    std::int32_t out[6];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const omk::WorldCamera* c = cams.find(ids[i]);
        if (!c) { std::printf("camera %d: absent\n", ids[i]); continue; }
        const omk::ResolvedCamera a = omk::resolveCamera(*c, foot, 0.0f);
        const omk::ResolvedCamera b = omk::resolveCamera(*c, subj, 0.0f);
        std::printf("camera %d  eyeSubject %d atSubject %d | eye.y feet %8.2f  "
                    "pelvis %8.2f  raised %6.2f\n",
                    ids[i], c->eyeSubject, c->atSubject, a.eye[1], b.eye[1],
                    a.eye[1] - b.eye[1]);
        if (n < 6) out[n++] = static_cast<std::int32_t>((a.eye[1] - b.eye[1]) * 100.0f + 0.5f);
    }
    if (argc > 5 && omk::safeOutputPath(argv[5])) {
        std::ofstream f(argv[5], std::ios::binary);
        f.write(reinterpret_cast<const char*>(out), sizeof(std::int32_t) * static_cast<std::size_t>(n));
    }
    return 0;
}

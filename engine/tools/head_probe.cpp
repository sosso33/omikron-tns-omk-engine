// SPDX-License-Identifier: GPL-3.0-or-later
// head_probe - `Actor_SetHeadLook` over a real model (docs/STREET_LIFE.md,
// step 6; actor/pose.h `aimHead`).
//
//     head_probe <gamedata> [model]
//
// Composes the model's rest pose, finds its head, and aims it at targets
// placed around it: in front, 45 degrees left and right, 120 degrees (past
// the +-70 clamp), behind, and 60 degrees up (past the +-40 clamp). Prints
// the wanted and the applied angles and the head's forward after the aim,
// snapped and eased.
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: head_probe <gamedata> [model]\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const std::string model = argc > 2 ? argv[2] : "DE3_FN";
    const auto d = fs.read("MESHES/PERSOS/" + model + ".3DO");
    const auto h = omk::readHeader(d);
    if (!h) { std::printf("no model %s\n", model.c_str()); return 1; }
    const auto meshes = omk::readMeshes(d, *h);
    const int head = omk::headMeshOf(meshes);
    std::printf("model %s meshes %zu head %d %s\n", model.c_str(), meshes.size(), head,
                head >= 0 ? meshes[static_cast<std::size_t>(head)].name : "-");
    if (head < 0) return 1;
    const auto rest = omk::composePose(meshes, omk::NodeTracks{}, 0, false);
    const float kDeg = 57.29577951308232f;
    auto forwardOf = [&](const std::vector<omk::MeshPose>& pose, float out[3]) {
        const float l[3] = {0, 0, -1};
        omk::qrot(pose[static_cast<std::size_t>(head)].q, l, out);
    };
    float f0[3]; forwardOf(rest, f0);
    const auto& hp = rest[static_cast<std::size_t>(head)].pos;
    struct Case { const char* name; float az, el; };   // azimuth from the forward (deg), elevation (deg)
    const Case cases[] = {{"front", 0, 0}, {"left45", -45, 0}, {"right45", 45, 0}, {"right120", 120, 0},
                          {"behind", 180, 0}, {"up60", 0, 60}, {"down60", 0, -60}};
    for (const auto& c : cases) {
        // the target 200 units away in the head's own ground frame: rotate the
        // rest forward by the azimuth, lift by the elevation (Y down)
        const float fh = std::sqrt(f0[0] * f0[0] + f0[2] * f0[2]);
        const float fx = f0[0] / fh, fz = f0[2] / fh;
        const float a = c.az / kDeg, e = c.el / kDeg;
        const float dx = fx * std::cos(a) + fz * std::sin(a), dz = -fx * std::sin(a) + fz * std::cos(a);
        const float t[3] = {hp[0] + 200.0f * std::cos(e) * dx, hp[1] - 200.0f * std::sin(e), hp[2] + 200.0f * std::cos(e) * dz};
        auto pose = rest;
        omk::HeadLook look;
        float wp = 0, wy = 0;
        omk::aimHead(pose, meshes, head, t, look, 1.0f, true, &wp, &wy);
        float f1[3]; forwardOf(pose, f1);
        // the turn the head actually made, measured on its forward
        const float f1h = std::sqrt(f1[0] * f1[0] + f1[2] * f1[2]);
        const float turned = std::atan2(fx * f1[2] / f1h - fz * f1[0] / f1h, fx * f1[0] / f1h + fz * f1[2] / f1h) * kDeg;
        const float lifted = (std::atan2(-f1[1], f1h) - std::atan2(-f0[1], fh)) * kDeg;
        // and eased from rest over one frame, then eight
        auto pose2 = rest; omk::HeadLook look2;
        omk::aimHead(pose2, meshes, head, t, look2, 1.0f, false);
        const float afterOne = look2.yaw;
        for (int i = 0; i < 40; ++i) { pose2 = rest; omk::aimHead(pose2, meshes, head, t, look2, 1.0f, false); }
        std::printf("case %s wanted_yaw %.1f wanted_pitch %.1f applied_yaw %.1f applied_pitch %.1f turned %.1f lifted %.1f eased_one %.2f eased_forty %.1f\n",
                    c.name, wy, wp, look.yaw, look.pitch, turned, lifted, afterOne, look2.yaw);
    }
    return 0;
}

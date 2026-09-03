// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORLD CAMERA TABLE, DUMPED - `Camera_FindWorld`'s three tables, read by
// the port, in a form a check can difference against `tools/cutscene.py`.
//
//     dump_world_cameras <gamedata> <out.txt>
//
// Two things this emits that a per-record dump would not, because both are
// where the reading went wrong:
//
//   * the SUBJECT split. 1440 of the 5381 records give at least one of their
//     two points as an OFFSET from an actor rather than as a world coordinate
//     (`Camera_LoadParams` decides per point), and reading those as absolute
//     draws an empty frame with nothing to say why.
//   * the COLLISION count against GLOBAL. `Camera_FindWorld` searches the
//     resident chunk's tables before GLOBAL, and this dump reports how many
//     ids appear in both and how many of those actually DIFFER - because the
//     answer is 280 and 0, so the order is read out of the code and no shipped
//     file can tell the two orders apart. Asserting the count keeps that a
//     measured statement instead of an implied one.
#include "formats/iam.h"
#include "o3de/worldcam.h"
#include "platform/datafs.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_world_cameras <gamedata> <out.txt>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[2])) return 2;
    const std::string iam = std::string(argv[1]) + "/IAM";
    const auto areaFile  = omk::DataFs::readPath(iam + "/AREA");
    const auto sceneFile = omk::DataFs::readPath(iam + "/SCENE");
    const auto globFile  = omk::DataFs::readPath(iam + "/GLOBAL");
    const auto areas  = omk::IamArchive::open(areaFile);
    const auto scenes = omk::IamArchive::open(sceneFile);

    long total = 0, absolute = 0, eyeRel = 0, atRel = 0, wideFov = 0;
    std::ofstream f(argv[2]);
    f << "# where chunk id eye at roll fov eyeSubject atSubject\n";

    const auto emit = [&](const char* where, int chunk,
                          const std::vector<omk::WorldCamera>& v) {
        for (const auto& c : v) {
            ++total;
            if (c.absolute()) ++absolute;
            if (c.eyeSubject != -1) ++eyeRel;
            if (c.atSubject  != -1) ++atRel;
            if (c.fov > 105.0f) ++wideFov;
            f << where << ' ' << chunk << ' ' << c.id << ' '
              << static_cast<long>(c.eye[0]) << ' ' << static_cast<long>(c.eye[1])
              << ' ' << static_cast<long>(c.eye[2]) << ' '
              << static_cast<long>(c.at[0]) << ' ' << static_cast<long>(c.at[1])
              << ' ' << static_cast<long>(c.at[2]) << ' '
              << c.roll << ' ' << c.fov << ' '
              << c.eyeSubject << ' ' << c.atSubject << '\n';
        }
    };

    for (std::size_t i = 0; i < areas.size(); ++i) {
        const auto b = areas.chunk(i);
        if (b.empty()) continue;
        omk::WorldCameras w; w.setArea(b);
        emit("AREA", static_cast<int>(i), w.area());
    }
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        const auto b = scenes.chunk(i);
        if (b.empty()) continue;
        omk::WorldCameras w; w.setScene(b);
        emit("SCENE", static_cast<int>(i), w.scene());
    }
    omk::WorldCameras g; g.setGlobal(globFile);
    emit("GLOBAL", -1, g.global());

    // THE ORDER, and whether anything could see it. Count the ids a chunk
    // shares with GLOBAL, and how many of those resolve to a DIFFERENT camera
    // depending on which table is searched first.
    long collisions = 0, differing = 0;
    const auto sameRecord = [](const omk::WorldCamera& a, const omk::WorldCamera& b) {
        for (int k = 0; k < 3; ++k)
            if (a.eye[k] != b.eye[k] || a.at[k] != b.at[k]) return false;
        return a.roll == b.roll && a.fov == b.fov &&
               a.eyeSubject == b.eyeSubject && a.atSubject == b.atSubject;
    };
    const auto against = [&](const std::vector<omk::WorldCamera>& v) {
        for (const auto& c : v)
            for (const auto& gc : g.global())
                if (gc.id == c.id) {
                    ++collisions;
                    if (!sameRecord(c, gc)) ++differing;
                }
    };
    for (std::size_t i = 0; i < areas.size(); ++i) {
        const auto b = areas.chunk(i);
        if (b.empty()) continue;
        omk::WorldCameras w; w.setArea(b); against(w.area());
    }
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        const auto b = scenes.chunk(i);
        if (b.empty()) continue;
        omk::WorldCameras w; w.setScene(b); against(w.scene());
    }
    // And the camera the Impasse's own startup script cuts to, which is the
    // one that had to be resolved before anything could be drawn there.
    omk::WorldCameras look;
    look.setArea(areas.chunk(222));
    look.setScene(scenes.chunk(55));
    look.setGlobal(globFile);
    const omk::WorldCamera* c0 = look.find(0);
    f << "# impasse0 " << (c0 ? c0->eyeSubject : -9999) << ' '
      << (c0 ? c0->atSubject : -9999) << ' '
      << (c0 ? static_cast<long>(c0->eye[2]) : -9999) << '\n';
    f << "# global " << g.global().size() << ' ' << collisions << ' '
      << differing << '\n';
    f << "# totals " << total << ' ' << absolute << ' ' << eyeRel << ' '
      << atRel << ' ' << wideFov << '\n';
    std::printf("%ld cameras, %ld absolute, %ld eye-relative, %ld target-relative,"
                " %ld wider than 105 deg\n", total, absolute, eyeRel, atRel, wideFov);
    std::printf("GLOBAL holds %zu; %ld chunk ids collide with it, %ld differ\n",
                g.global().size(), collisions, differing);
    return 0;
}

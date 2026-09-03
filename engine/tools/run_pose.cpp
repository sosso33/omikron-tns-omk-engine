// SPDX-License-Identifier: GPL-3.0-or-later
// A CHARACTER, POSED - the model through one frame of a `.3DM` line.
//
//     run_pose <gamedata> <model> <asset> <frame> <eye> <at> <fov> <out.bin> [WxH]
//
// `model` is a stem in `MESHES/PERSOS` and `asset` one in `MORPH`; for the
// conversation a new game opens with those are `HO1_FNM` and `125338`.
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/raster.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

static void triple(const char* s, float v[3]) {
    std::sscanf(s, "%f,%f,%f", &v[0], &v[1], &v[2]);
}

int main(int argc, char** argv) {
    if (argc < 9) {
        std::fprintf(stderr, "usage: run_pose <gamedata> <model> <asset> <frame> "
                             "<x,y,z> <x,y,z> <fov> <out.bin> [WxH]\n");
        return 2;
    }
    const std::string fr = argv[1], model = argv[2], asset = argv[3];
    const int frame = std::atoi(argv[4]);
    if (!omk::safeOutputPath(argv[8])) return 2;

    const omk::DataFs fs(fr);
    const auto mo = fs.resolve("MESHES/PERSOS/" + model + ".3DO");
    if (!mo) { std::fprintf(stderr, "no model %s\n", model.c_str()); return 1; }
    const auto d = omk::DataFs::readPath(*mo);
    const auto mt = fs.resolve("MESHES/PERSOS/" + model + ".3DT");
    const auto rest = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = mt ? omk::textures(d, omk::DataFs::readPath(*mt))
                        : std::vector<omk::Texture>{};
    const auto h = omk::readHeader(d);
    if (!h) return 1;
    const auto meshes = omk::readMeshes(d, *h);

    const auto ma = fs.resolve("MORPH/" + asset + ".3DM");
    omk::NodeTracks tracks;
    if (ma) tracks = omk::nodeTracks(omk::DataFs::readPath(*ma),
                                     omk::rootTrackOf(meshes));
    const auto pose = omk::composePose(meshes, tracks, frame);
    const auto face = omk::faceMeshOf(meshes);
    std::vector<float> fv;
    if (ma) fv = omk::faceFrame(omk::DataFs::readPath(*ma), frame);
    omk::Geometry posed;
    // An 11th argument of `noface` draws the face from its BIND vertices
    // instead of the line's, which is the control that isolates the morph:
    // the two renders must differ, and only around the head.
    const bool noFace = argc > 11 && std::string(argv[11]) == "noface";
    if (noFace) fv.clear();
    omk::applyPose(posed, rest, meshes, pose, &face, &fv);
    std::printf("face mesh %d '%s' base %zu, %d verts; stream supplies %zu\n",
                face.mesh,
                face.mesh >= 0 ? meshes[static_cast<std::size_t>(face.mesh)].name : "-",
                face.base, face.count, fv.size() / 3);

    int w = 800, h2 = 600;
    if (argc > 9) std::sscanf(argv[9], "%dx%d", &w, &h2);
    omk::RCamera cam;
    triple(argv[5], cam.eye);
    triple(argv[6], cam.at);
    cam.hfovDeg = static_cast<float>(std::atof(argv[7]));
    cam.w = w; cam.h = h2;

    omk::SoftwareRenderer sw;
    sw.init(w, h2);
    sw.setTextures(tex);
    omk::View v; v.cam = cam;
    sw.begin(v);
    for (const auto& b : posed.batches)
        sw.submit({static_cast<std::uint32_t>(b.material), &posed,
                   b.start, b.count, b.blend, b.cutout});
    sw.end();
    const auto& pic = sw.readback();

    std::ofstream o(argv[8], std::ios::binary);
    for (auto px : pic.px) {
        const char b2[2] = {static_cast<char>(px & 0xFF), static_cast<char>(px >> 8)};
        o.write(b2, 2);
    }
    // THE REVISION CONTRACT. `applyPose` rewrites the SAME `Geometry` every
    // frame, so a backend that caches by pointer - the Vulkan one keys its
    // vertex buffer on exactly that - must be told the content moved. Pose the
    // same object at two frames and report whether the corners changed and
    // whether the revision did: a backend re-uploads on the second, and
    // without it a posed character freezes at the first pose the GPU saw,
    // which is what happened.
    {
        omk::Geometry again;
        omk::applyPose(again, rest, meshes, pose, &face, &fv);
        const std::uint64_t r1 = again.revision;
        const auto p2 = omk::composePose(meshes, tracks, frame + 60);
        auto fv2 = ma ? omk::faceFrame(omk::DataFs::readPath(*ma), frame + 60)
                      : std::vector<float>{};
        omk::applyPose(again, rest, meshes, p2, &face, &fv2);
        long moved = 0;
        for (std::size_t i = 0; i < again.corners.size() &&
                                i < posed.corners.size(); ++i)
            if (again.corners[i].x != posed.corners[i].x ||
                again.corners[i].y != posed.corners[i].y ||
                again.corners[i].z != posed.corners[i].z) ++moved;
        std::printf("revision %llu -> %llu; re-posing moved %ld of %zu corners\n",
                    static_cast<unsigned long long>(r1),
                    static_cast<unsigned long long>(again.revision),
                    moved, again.corners.size());
    }

    // THE SKINNING CONTROL, rendered every run rather than reconstructed.
    // A corner whose face index is negative is skinned to an ANCESTOR and is
    // built from that ancestor's vertex and offset, so the ancestor's
    // transform is what poses it. Posing it by the mesh that DECLARED the face
    // instead tears the model open at the shoulders - visibly, and a reader
    // saw it before any number here did. Both readings are computed so the
    // difference is a number and not a memory.
    {
        omk::Geometry alt = posed;
        long skinned = 0;
        float worst = 0;
        for (std::size_t i = 0; i < rest.corners.size() &&
                                i < rest.cornerDeclared.size(); ++i) {
            const std::int32_t own = rest.cornerMesh[i];
            const std::int32_t dec = rest.cornerDeclared[i];
            if (own == dec) continue;
            ++skinned;
            if (dec < 0 || static_cast<std::size_t>(dec) >= pose.size()) continue;
            const omk::Mesh& dm = meshes[static_cast<std::size_t>(dec)];
            const omk::MeshPose& dp = pose[static_cast<std::size_t>(dec)];
            const float local[3] = {rest.corners[i].x - dm.pos[0],
                                    rest.corners[i].y - dm.pos[1],
                                    rest.corners[i].z - dm.pos[2]};
            float r[3];
            omk::qrot(dp.q, local, r);
            const float dx = dp.pos[0] + r[0] - posed.corners[i].x;
            const float dy = dp.pos[1] + r[1] - posed.corners[i].y;
            const float dz = dp.pos[2] + r[2] - posed.corners[i].z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > worst) worst = d;
        }
        std::printf("skinned corners %ld; posing them by the declaring mesh "
                    "moves one as far as %.1f units\n", skinned, worst);
    }

    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (const auto& c : posed.corners) {
        const float p3[3] = {c.x, c.y, c.z};
        for (int k = 0; k < 3; ++k) {
            if (p3[k] < lo[k]) lo[k] = p3[k];
            if (p3[k] > hi[k]) hi[k] = p3[k];
        }
    }
    // An optional TEXT sidecar, so a check can difference the pose itself
    // rather than a picture of it: per mesh, the composed quaternion and
    // position at this frame, times 1000 and truncated.
    if (argc > 10) {
        if (!omk::safeOutputPath(argv[10])) return 2;
        std::ofstream t(argv[10]);
        t << "tracks " << tracks.count << ' ' << tracks.frames << ' '
          << tracks.rootTrack << '\n';
        for (std::size_t i = 0; i < pose.size(); ++i)
            t << "mesh " << i << ' '
              << static_cast<long>(pose[i].q.w * 1000) << ' '
              << static_cast<long>(pose[i].q.x * 1000) << ' '
              << static_cast<long>(pose[i].q.y * 1000) << ' '
              << static_cast<long>(pose[i].q.z * 1000) << ' '
              << static_cast<long>(pose[i].pos[0] * 1000) << ' '
              << static_cast<long>(pose[i].pos[1] * 1000) << ' '
              << static_cast<long>(pose[i].pos[2] * 1000) << '\n';
    }
    std::printf("posed bounds  x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
                lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    std::printf("%s: %zu meshes, %zu corners; %s: %d tracks, %d frames, root %d\n",
                model.c_str(), meshes.size(), rest.corners.size(), asset.c_str(),
                tracks.count, tracks.frames, tracks.rootTrack);
    return 0;
}

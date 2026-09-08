// SPDX-License-Identifier: GPL-3.0-or-later
// THE PLAYER'S VERTICAL - what anchors the drawn body, measured.
//
//     vertical_probe <gamedata> <model> <ctl> [clip ...]
//
// Prints, for the model's OWN collision spheres (descriptor +244/+248):
//
//   * `Collision_BodySphere` (0x00444360): the LARGEST-radius sphere, whose
//     radius `Walk_ProbeGround` adds into the clearance at actor+264;
//   * `sub_4443B0` (0x004443B0): the same list ordered by `centre.y + radius`
//     - the LOWEST-reaching sphere and, as its out-param, the second-lowest,
//     whose centre.y is the probe's vertical offset.
//
// and then, per named `.CTL` clip, the posed body's LOWEST CORNER per frame
// against the standing pose's - which is what `omk-play` currently latches as
// `playerFeet`. The question this answers is whether a walk cycle's vertical
// excursion is AUTHORED (so a constant anchor floats in the engine too) or
// whether it nets out over the cycle.
//
// Y GROWS DOWN, so the larger y is the lower point and a SMALLER lowest-corner
// number means the body is drawn HIGHER.
#include "actor/pose.h"
#include "actor/spatial.h"
#include "formats/anim.h"
#include "formats/ctl.h"
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

float f32at(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0.0f;
    std::uint32_t v = 0;
    for (int k = 0; k < 4; ++k)
        v |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(d[o + static_cast<std::size_t>(k)])) << (8 * k);
    float f; std::memcpy(&f, &v, 4); return f;
}

// player.cpp's `poseTracks`, lifted so the probe needs no soup and no
// controller. Same reads, same key-0-is-the-rest-sentinel rule.
omk::NodeTracks tracksOf(std::span<const std::byte> data, std::size_t offset,
                         const std::vector<omk::Mesh>& meshes) {
    omk::NodeTracks t;
    const auto d = omk::animDescriptor(data, offset);
    if (!d || d->frames <= 0) return t;
    t.count = static_cast<int>(d->tracks.size());
    t.frames = d->frames;
    t.rootTrack = -1;
    for (const auto& tr : d->tracks) {
        int mi = -1;
        const std::string want = lower(tr.name);
        for (const auto& m : meshes)
            if (lower(m.name) == want) { mi = m.index; break; }
        t.ids.push_back(mi);
    }
    t.quats.assign(static_cast<std::size_t>(d->frames), {});
    t.trans.assign(static_cast<std::size_t>(d->frames), {0.0f, 0.0f, 0.0f});
    for (const auto& tr : d->tracks) {
        if (!tr.posOffset || tr.posKeys <= 1) continue;
        float acc[3] = {0.0f, 0.0f, 0.0f};
        for (int f = 0; f < d->frames; ++f) {
            const int key = f + 1 < tr.posKeys ? f + 1 : tr.posKeys - 1;
            const std::size_t o = tr.posOffset + 12u * static_cast<std::size_t>(key);
            if (o + 12 > data.size()) break;
            for (int k = 0; k < 3; ++k) acc[static_cast<std::size_t>(k)] += f32at(data, o + 4u * static_cast<std::size_t>(k));
            for (int k = 0; k < 3; ++k)
                t.trans[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)] = acc[static_cast<std::size_t>(k)];
        }
        break;                                  // the pelvis is the only root track
    }
    for (int f = 0; f < d->frames; ++f) {
        auto& row = t.quats[static_cast<std::size_t>(f)];
        row.resize(d->tracks.size());
        for (std::size_t i = 0; i < d->tracks.size(); ++i) {
            const omk::AnimTrack& tr = d->tracks[i];
            if (!tr.rotOffset || tr.rotKeys <= 0) continue;
            const int key = f + 1 < tr.rotKeys ? f + 1 : tr.rotKeys - 1;   // key 0 is the rest sentinel
            const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
            if (o + 16 > data.size()) continue;
            row[i] = {f32at(data, o), f32at(data, o + 4), f32at(data, o + 8), f32at(data, o + 12)};
        }
    }
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: vertical_probe <gamedata> <model> <ctl> [clip ...]\n");
        return 2;
    }
    const std::string root = argv[1], model = argv[2], bank = argv[3];
    const omk::DataFs fs(root);

    const auto mo = fs.resolve("MESHES/PERSOS/" + model + ".3DO");
    if (!mo) { std::fprintf(stderr, "no model %s\n", model.c_str()); return 1; }
    const auto md = omk::DataFs::readPath(*mo);
    const auto h = omk::readHeader(md);
    if (!h) { std::fprintf(stderr, "bad model header\n"); return 1; }
    const auto meshes = omk::readMeshes(md, *h);
    const auto rest = omk::buildGeometry(md, omk::DrawFilter::Engine);

    // ---- THE SPHERES ------------------------------------------------
    const auto sph = omk::modelSweepSpheres(md);
    std::printf("%s: %zu collision spheres (descriptor +244/+248)\n", model.c_str(), sph.size());
    int big = -1, low = -1, low2 = -1;
    for (std::size_t i = 0; i < sph.size(); ++i) {
        const auto& c = sph[i];
        std::printf("  [%zu] centre %+8.2f %+8.2f %+8.2f  r %6.2f   centre.y+r %+8.2f\n",
                    i, c.pos[0], c.pos[1], c.pos[2], c.radius, c.pos[1] + c.radius);
        if (big < 0 || c.radius > sph[static_cast<std::size_t>(big)].radius) big = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < sph.size(); ++i) {
        const float e = sph[i].pos[1] + sph[i].radius;
        if (low < 0 || e > sph[static_cast<std::size_t>(low)].pos[1] + sph[static_cast<std::size_t>(low)].radius)
            low = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < sph.size(); ++i) {
        if (static_cast<int>(i) == low) continue;
        const float e = sph[i].pos[1] + sph[i].radius;
        if (low2 < 0 || e > sph[static_cast<std::size_t>(low2)].pos[1] + sph[static_cast<std::size_t>(low2)].radius)
            low2 = static_cast<int>(i);
    }
    if (low2 < 0) low2 = low;
    if (big >= 0)
        std::printf("Collision_BodySphere -> [%d] r %.2f (the largest radius)\n",
                    big, sph[static_cast<std::size_t>(big)].radius);
    if (low >= 0)
        std::printf("sub_4443B0 -> lowest [%d] extent %+.2f ; second-lowest [%d] centre.y %+.2f\n",
                    low, sph[static_cast<std::size_t>(low)].pos[1] + sph[static_cast<std::size_t>(low)].radius,
                    low2, sph[static_cast<std::size_t>(low2)].pos[1]);
    if (big >= 0 && low2 >= 0)
        std::printf("engine seat term (second-lowest centre.y + largest radius) = %+.2f\n",
                    sph[static_cast<std::size_t>(low2)].pos[1] + sph[static_cast<std::size_t>(big)].radius);

    // ---- THE CLIPS --------------------------------------------------
    const auto cp = fs.resolve("ANIMS/" + bank + ".CTL");
    if (!cp) { std::fprintf(stderr, "no bank %s\n", bank.c_str()); return 1; }
    const auto cd = omk::DataFs::readPath(*cp);
    const auto ctl = omk::readCtl(cd);
    if (!ctl.valid) { std::fprintf(stderr, "bank did not parse\n"); return 1; }

    std::vector<std::string> want;
    for (int i = 4; i < argc; ++i) want.push_back(lower(argv[i]));
    if (want.empty()) { want = {"h_stand", "h_walk", "h_run"}; }

    float standLowest = 0.0f;
    bool  standKnown = false;
    for (const auto& w : want) {
        int ci = -1;
        for (std::size_t i = 0; i < ctl.clips.size(); ++i)
            if (lower(ctl.clips[i].name) == w) { ci = static_cast<int>(i); break; }
        if (ci < 0) { std::printf("\n%s: not in the bank\n", w.c_str()); continue; }
        const auto tr = tracksOf(cd, ctl.clips[static_cast<std::size_t>(ci)].offset, meshes);
        if (!tr.valid()) { std::printf("\n%s: no tracks\n", w.c_str()); continue; }
        std::printf("\n%s: %d frames, %d tracks\n", w.c_str(), tr.frames, tr.count);
        float lo = 1e30f, hi = -1e30f;
        omk::Geometry posed;
        for (int f = 0; f < tr.frames; ++f) {
            const auto pose = omk::composePose(meshes, tr, f, false);
            omk::applyPose(posed, rest, meshes, pose);
            float bottom = -1e30f;
            for (const auto& c : posed.corners) bottom = std::max(bottom, c.y);
            lo = std::min(lo, bottom); hi = std::max(hi, bottom);
            if (!standKnown) { standLowest = bottom; standKnown = true; }
            if (tr.frames <= 40 || (f % 4) == 0)
                std::printf("  f%-3d lowest %+8.2f  (vs stand %+7.2f)  pelvis trans.y %+7.2f\n",
                            f, bottom, bottom - standLowest, tr.trans[static_cast<std::size_t>(f)][1]);
        }
        std::printf("  SPAN %+.2f .. %+.2f  = %.2f units; against the standing %+.2f "
                    "the body floats up to %.2f\n",
                    lo, hi, hi - lo, standLowest, standLowest - lo);
    }
    return 0;
}

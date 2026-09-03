// SPDX-License-Identifier: GPL-3.0-or-later
// THE LINE AGAINST THE IDLE - where the body lands, frame by frame.
//
//     dump_lineblend <gamedata> <areaChunk> <model> <morph> <clipIndex> <out.txt>
//
// Composes the character's pose from the line's .3DM, from the scene clip,
// and from `blendTracks` between them, and prints for each the root's
// position, the lowest point (the head top, Y down), the highest (the feet)
// and the pitch of pelvis->head from vertical. Written 2026-09-02 when a
// reader saw the fade "alternate between flying and landing" and the torso
// not bowing where the game's does.
#include "platform/datafs.h"
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "script/scenerunner.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Stat { float root[3]; float minY, maxY; float pitch; float headY; };
Stat stat(const std::vector<omk::Mesh>& meshes, const omk::Geometry& rest,
          const std::vector<omk::MeshPose>& pose) {
    omk::Geometry g = rest;
    omk::applyPose(g, rest, meshes, pose);
    Stat s{}; s.minY = 1e9f; s.maxY = -1e9f;
    for (const auto& c : g.corners) { if (c.y < s.minY) s.minY = c.y; if (c.y > s.maxY) s.maxY = c.y; }
    int root = -1, head = -1;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        bool hasParent = false;
        for (const auto& m : meshes) if (m.id == meshes[i].parent) hasParent = true;
        if (!hasParent && root < 0) root = static_cast<int>(i);
        std::string n = meshes[i].name;
        for (auto& ch : n) ch = static_cast<char>(std::tolower(ch));
        if (n.find("tete") != std::string::npos) head = static_cast<int>(i);
    }
    if (root >= 0) for (int k = 0; k < 3; ++k) s.root[k] = pose[static_cast<std::size_t>(root)].pos[k];
    if (root >= 0 && head >= 0) {
        const auto& h = pose[static_cast<std::size_t>(head)].pos;
        const float dx = h[0] - s.root[0], dy = h[1] - s.root[1], dz = h[2] - s.root[2];
        const float horiz = std::sqrt(dx * dx + dz * dz);
        s.pitch = std::atan2(horiz, -dy) * 57.29578f;   // 0 = straight up (Y down)
        s.headY = h[1];
    }
    return s;
}
void line(std::ofstream& f, const char* tag, int frame, const Stat& s) {
    f << tag << ' ' << frame << " root " << s.root[0] << ' ' << s.root[1] << ' ' << s.root[2]
      << " top " << s.minY << " feet " << s.maxY << " headY " << s.headY
      << " pitch " << s.pitch << "\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: dump_lineblend <gamedata> <areaChunk> <model> <morph> <clipIndex> <out.txt>\n"); return 2; }
    if (!omk::safeOutputPath(argv[6])) return 2;
    const std::string fr = argv[1];
    omk::OpcodeTable tab;
    omk::SceneRunner sc;
    if (!sc.load(fr + "/SCPTDATA", fr + "/IAM", tab, omk::ChunkKind::Area, std::atoi(argv[2]))) {
        std::fprintf(stderr, "no scene\n"); return 1;
    }
    const omk::DataFs fs(fr);
    const auto mp = fs.resolve(std::string("MESHES/PERSOS/") + argv[3] + ".3DO");
    if (!mp) { std::fprintf(stderr, "no model\n"); return 1; }
    const auto md = omk::DataFs::readPath(*mp);
    const auto mh = omk::readHeader(md);
    if (!mh) return 1;
    const auto meshes = omk::readMeshes(md, *mh);
    const auto rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
    const auto morph = fs.read(std::string("MORPH/") + argv[4] + ".3DM");
    const auto lineT = omk::nodeTracks(morph, omk::rootTrackOf(meshes));
    const auto clipT = omk::clipTracks(sc.scene().clipData(std::atoi(argv[5])));
    std::ofstream f(argv[6]);
    f << "model " << argv[3] << " meshes " << meshes.size() << " rootTrack " << omk::rootTrackOf(meshes) << "\n";
    for (std::size_t i = 0; i < meshes.size(); ++i)
        f << "mesh " << i << ' ' << meshes[i].name << " id " << meshes[i].id << " parent " << meshes[i].parent << "\n";
    f << "line " << argv[4] << " frames " << lineT.frames << " tracks " << lineT.count << "\n";
    f << "clip " << argv[5] << ' ' << sc.scene().clipName(std::atoi(argv[5])) << " frames " << clipT.frames
      << " tracks " << clipT.count << " rootTrack " << clipT.rootTrack << "\n";
    for (std::size_t i = 0; i < clipT.ids.size(); ++i) f << "clipid " << i << ' ' << clipT.ids[i] << "\n";
    // the idle at frame 0 and 15, the line every 60 frames, both roots
    line(f, "idle", 0, stat(meshes, rest, omk::composePose(meshes, clipT, 0, false)));
    line(f, "idle", 15, stat(meshes, rest, omk::composePose(meshes, clipT, 15, false)));
    line(f, "idle-upright", 0, stat(meshes, rest, omk::composePose(meshes, clipT, 0, true)));
    for (int fr2 = 0; fr2 < lineT.frames; fr2 += 60) {
        line(f, "line-upright", fr2, stat(meshes, rest, omk::composePose(meshes, lineT, fr2, true)));
        line(f, "line-rootkept", fr2, stat(meshes, rest, omk::composePose(meshes, lineT, fr2, false)));
    }
    // the fade-in at w = 0, .25, .5, .75, 1 between idle frame 0 and line frame w*30
    for (int k = 0; k <= 4; ++k) {
        const float w = k * 0.25f;
        const auto mixed = omk::blendTracks(clipT, 0, false, lineT, static_cast<int>(w * 30), true, w);
        line(f, "fade", k, stat(meshes, rest, omk::composePose(meshes, mixed, 0, false)));
    }
    return 0;
}

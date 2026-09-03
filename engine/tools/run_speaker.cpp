// SPDX-License-Identifier: GPL-3.0-or-later
// WHERE A CONVERSATION'S SPEAKER STANDS, and which model it is.
//
//     run_speaker <gamedata> <tables> <conversation> <area> <set> <out.txt>
//
// The two halves a frontend needs before it can draw anybody:
//
//   * the MODEL - the DIALOG chunk's word 0 is the speaker's actor id, and the
//     actor record with that id at +272 (276-byte records at AREA +56 / count
//     +80, SCENE +24 / +48) names the model at +144 and the .CTL at +72.
//     `sub_40B190` is the scan.
//   * the PLACE - the least-squares convergence of the line cameras' rays,
//     dropped onto the walkable floor beneath it (`actor/speaker.h`).
#include "actor/speaker.h"
#include "formats/iam.h"
#include "o3de/collision.h"
#include "platform/datafs.h"
#include "script/dialogue.h"
#include "script/script.h"

#include <cstdio>
#include <cstring>
#include <fstream>

// `sub_40B190`: scan the chunk's 276-byte actor records for one whose id
// matches, and take its model name.
static std::string modelOfActor(std::span<const std::byte> chunk, int actor,
                                std::size_t arrOff, std::size_t cntOff) {
    if (chunk.size() < cntOff + 2) return {};
    const auto i32 = [&](std::size_t o) {
        return static_cast<std::int32_t>(
            static_cast<std::uint32_t>(chunk[o]) |
            (static_cast<std::uint32_t>(chunk[o + 1]) << 8) |
            (static_cast<std::uint32_t>(chunk[o + 2]) << 16) |
            (static_cast<std::uint32_t>(chunk[o + 3]) << 24));
    };
    const auto i16 = [&](std::size_t o) {
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(chunk[o]) |
               (static_cast<std::uint16_t>(chunk[o + 1]) << 8));
    };
    const std::size_t p = static_cast<std::uint32_t>(i32(arrOff));
    const int n = i16(cntOff);
    if (n <= 0 || p + 276u * static_cast<std::size_t>(n) > chunk.size()) return {};
    for (int i = 0; i < n; ++i) {
        const std::size_t o = p + 276u * static_cast<std::size_t>(i);
        if (i16(o + 272) != actor) continue;
        std::string s;
        for (int k = 0; k < 20; ++k) {
            const char c = static_cast<char>(chunk[o + 144u + static_cast<std::size_t>(k)]);
            if (!c) break;
            s.push_back(c);
        }
        return s;
    }
    return {};
}

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: run_speaker <gamedata> <tables> <conv> <area> "
                             "<set> <out.txt>\n");
        return 2;
    }
    const std::string fr = argv[1];
    const int conv = std::atoi(argv[3]), area = std::atoi(argv[4]);
    const std::string setName = argv[5];
    if (!omk::safeOutputPath(argv[6])) return 2;

    const auto dlgFile = omk::DataFs::readPath(fr + "/IAM/DIALOG");
    const auto arch = omk::IamArchive::open(dlgFile);
    const auto chunk = arch.chunk(static_cast<std::size_t>(conv));
    const auto c = omk::parseConversation(conv, chunk);

    const auto areaFile = omk::DataFs::readPath(fr + "/IAM/AREA");
    const auto areas = omk::IamArchive::open(areaFile);
    const std::string model =
        modelOfActor(areas.chunk(static_cast<std::size_t>(area)), c.speaker, 56, 80);

    const omk::DataFs fs(fr);
    omk::TriangleSoup walk;
    const auto so = fs.resolve("MESHES/DECORS/" + setName + ".3DO");
    if (so) walk = omk::collisionSoup(omk::DataFs::readPath(*so),
                                      omk::SoupKind::Walkable);
    const auto ids = omk::lineCameraIds(c);
    const auto st = omk::stageSpeaker(c.cams, ids, walk.empty() ? nullptr : &walk);

    std::ofstream f(argv[6]);
    f << "speaker " << c.speaker << ' ' << model << '\n';
    f << "rays " << st.rays << ' ' << static_cast<long>(st.scatter * 10) << '\n';
    f << "converge " << static_cast<long>(st.converge[0]) << ' '
      << static_cast<long>(st.converge[1]) << ' '
      << static_cast<long>(st.converge[2]) << '\n';
    f << "stand " << static_cast<long>(st.pos[0]) << ' '
      << static_cast<long>(st.pos[1]) << ' ' << static_cast<long>(st.pos[2])
      << ' ' << (st.onFloor ? 1 : 0) << '\n';
    std::printf("conversation %d: actor %d is %s; %d rays converge at "
                "(%.1f %.1f %.1f) scatter %.1f; stands at (%.1f %.1f %.1f)%s\n",
                conv, c.speaker, model.c_str(), st.rays, st.converge[0],
                st.converge[1], st.converge[2], st.scatter, st.pos[0], st.pos[1],
                st.pos[2], st.onFloor ? " on the walkable floor" : " (no floor found)");
    return 0;
}

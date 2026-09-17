// SPDX-License-Identifier: GPL-3.0-or-later
// A conversation's cameras as the PORT loads them - roll and fov in degrees.
//     dlgcam <gamedata> <dialogId>
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/dialogue.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: dlgcam <gamedata> <id>\n"); return 2; }
    const int id = std::atoi(argv[2]);
    const auto file = omk::DataFs::readPath(std::string(argv[1]) + "/IAM/DIALOG");
    const auto arch = omk::IamArchive::open(file);
    const auto conv = omk::parseConversation(id, arch.chunk(static_cast<std::size_t>(id)));
    if (!conv.valid) { std::fprintf(stderr, "no such conversation\n"); return 1; }
    for (const auto& c : conv.cams)
        std::printf("cam %5d  roll %8.3f  fov %8.3f  eye %8.1f %8.1f %8.1f  at %8.1f %8.1f %8.1f"
                    "  subjects %d %d %s\n",
                    c.id, c.roll, c.fov, c.eye[0], c.eye[1], c.eye[2], c.at[0], c.at[1], c.at[2],
                    int(std::int16_t(c.subject[0])), int(std::int16_t(c.subject[1])),
                    c.absolute() ? "ABSOLUTE" : "relative");
    return 0;
}

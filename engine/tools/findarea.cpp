// SPDX-License-Identifier: GPL-3.0-or-later
// Which AREA/SCENE chunk names a given set or map?  findarea <gamedata> <name>
#include "formats/iam.h"
#include "platform/datafs.h"
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: findarea <gamedata> <name>\n"); return 2; }
    omk::DataFs fs(argv[1]);
    const std::string want = argv[2];
    for (const char* a : {"AREA", "SCENE"}) {
        const auto raw = fs.read(std::string("IAM/") + a);
        const auto ar = omk::IamArchive::open(raw);
        for (std::size_t i = 0; i < ar.size(); ++i) {
            const auto sp = ar.chunk(i);
            if (sp.empty()) continue;
            const std::string b(reinterpret_cast<const char*>(sp.data()), sp.size());
            if (b.find(want) != std::string::npos)
                std::printf("%s chunk %zu (size %zu)\n", a, i, sp.size());
        }
    }
    return 0;
}

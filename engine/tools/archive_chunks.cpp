// SPDX-License-Identifier: GPL-3.0-or-later
// A CHUNK READ ALONE IS THE CHUNK (`verify.py: engine: archive chunks`,
// todo/ram-vs-original.md tier C). For every flat archive under IAM - AREA,
// SCENE and DIALOG - every directory index read through
// `archiveChunk` (the kept directory, one ranged read) against the same index
// of the whole file through `IamArchive::open`: present in both or neither,
// and byte-identical. Also how many bytes the reads took against the files.
//
//     archive_chunks <gamedata>   -> one line an archive, then the total
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/area.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: archive_chunks <gamedata>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    long differ = 0, archives = 0;
    for (const char* name : {"AREA", "SCENE", "DIALOG"}) {
        const auto path = fs.resolve(std::string("IAM/") + name);
        if (!path) continue;
        const auto whole = omk::readWholeFile(*path);
        const auto ar = omk::IamArchive::open(whole);
        long chunks = 0, bad = 0;
        std::size_t bytes = 0;
        // past the directory too: an index beyond it must read as nothing
        for (std::size_t i = 0; i < ar.size() + 4; ++i) {
            const auto a = ar.chunk(i);
            const auto b = omk::archiveChunk(*path, static_cast<int>(i));
            chunks += !a.empty();
            bytes += b.size();
            if (a.size() != b.size() || (!a.empty() && std::memcmp(a.data(), b.data(), a.size())))
                ++bad;
        }
        ++archives;
        differ += bad;
        std::printf("%s: %zu entries, %ld chunks, %ld differ; %zu KB read a chunk at a time "
                    "from a %zu KB file\n", name, ar.size(), chunks, bad, bytes / 1024,
                    whole.size() / 1024);
    }
    std::printf("archives %ld, total differing %ld\n", archives, differ);
    return differ ? 1 : 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// WHERE THE DEPTH TIE'S MEMORY IS - measured, by group, per geometry.
//
//     tie_mem <set.3DO> <body.3DO> [bodies]
//
// `todo/handoff-vita.md` §1 ranks "the depth tie's tables ~9.7 MB" among the
// largest heap sites of a street run, and §2 item 1 asks for the per-frame
// allocations across bodies to go. Neither says WHICH of the twenty-odd vectors
// in `DepthTie` holds it, and a struct layout cannot say either, because every
// one of them is sized from the geometry at run time. So this drives the real
// pass over real geometry and asks the class (`DepthTie::bytes`).
//
// There is ONE `DepthTie` per geometry: the resident sets have one each and
// every posed body has its own, so the street's total is the set plus a body's
// times however many are drawn. `bodies` (default 45, the figure §1's profile
// was taken at) scales the body row; the set row is whole.
//
// **This is a MODEL of the street's residency, not a measurement of it.** The
// running viewer holds two sets (hidden is not unloaded), a player, the crowd's
// bodies at four LOD sizes and the shadow, effect and sky geometries, and it is
// `play.cpp` that owns them. What is measured here is one geometry's tie at a
// time, exactly; the multiplication is arithmetic and is labelled as such.
//
// Prints only; writes nothing.
#include "o3de/depthtie.h"
#include "o3de/geom3do.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<char> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
}

struct Row {
    std::string stem;
    std::size_t tris = 0, draws = 0;
    long dropped = 0;
    omk::DepthTie::Bytes b;
};

// One full pass, the way the backend makes it: every batch in order, with its
// own depth-write flag, then a second identical revision so the tables are in
// the state a steady frame leaves them.
Row run(const char* path, int revisions) {
    Row r;
    const auto raw = slurp(path);
    r.stem = path;
    if (const auto s = r.stem.find_last_of("/\\"); s != std::string::npos)
        r.stem = r.stem.substr(s + 1);
    if (raw.empty()) return r;
    const std::span<const std::byte> d(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
    omk::Geometry g = omk::buildGeometry(d, omk::DrawFilter::Engine);
    r.tris = g.corners.size() / 3;
    if (r.tris == 0 || g.batches.empty()) return r;

    omk::DepthTie tie;
    std::vector<std::size_t> losers, restore;
    for (int rev = 0; rev < revisions; ++rev) {
        // A new revision is what a pose or a moved mesh gives the class, and it
        // is what resets and refills the tables.
        g.revision = static_cast<std::uint64_t>(rev) + 1;
        r.draws = 0;
        for (const auto& b : g.batches) {
            losers.clear();
            restore.clear();
            // `writes` is the backend's depth-write flag for the batch - the
            // opaque bucket writes and the blended ones do not, the same test
            // `tie_equiv` uses.
            tie.resolve(g, b.start, b.count, b.blend == omk::Blend::Opaque, losers, restore);
            // The class does NOT keep this - `DepthTie::dropped` is the
            // BACKEND's counter and the class only ever zeroes it
            // (`vkrender.cpp`: `t.dropped += losers.size()`), so a probe that
            // reads the field instead of the returned list reads 0 for ever.
            if (rev == revisions - 1) r.dropped += static_cast<long>(losers.size());
            ++r.draws;
        }
    }
    r.b = tie.bytes();
    return r;
}

void print(const Row& r, double scale, const char* note) {
    const double k = 1024.0;
    std::printf("%-14s %7zu tris %4zu draws %5ld dropped | claimed %8.1f perTri %7.1f "
                "log %8.1f table %8.1f scratch %7.1f | TOTAL %8.1f KB%s\n",
                r.stem.c_str(), r.tris, r.draws, r.dropped,
                r.b.claimed * scale / k, r.b.perTri * scale / k, r.b.log * scale / k,
                r.b.table * scale / k, r.b.scratch * scale / k,
                r.b.total() * scale / k, note);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tie_mem <set.3DO> <body.3DO> [bodies]\n");
        return 2;
    }
    const int bodies = argc > 3 ? std::atoi(argv[3]) : 45;

    const Row set = run(argv[1], 2);
    const Row body = run(argv[2], 2);
    print(set, 1.0, "");
    print(body, 1.0, "  (one)");
    print(body, static_cast<double>(bodies), bodies == 1 ? "" : "  (x bodies)");

    const double totalKB = (set.b.total() + body.b.total() * static_cast<std::size_t>(bodies)) / 1024.0;
    std::printf("set %zu bytes\nbody %zu bytes\nbodies %d\nmodelled total %.0f KB\n",
                set.b.total(), body.b.total(), bodies, totalKB);
    return 0;
}

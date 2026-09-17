// SPDX-License-Identifier: GPL-3.0-or-later
// `findMeshContaining` STILL ANSWERS WHAT THE QUADRATIC WALK ANSWERED.
//
//     meshidx_equiv <character.3DO>...
//
// `findMeshContaining` (o3de/shadow.cpp) resolves a shadow bone by NAME, on
// the LAST match, scoped to one LOD skeleton by an ancestry walk - and the
// walk turned every parent ID into an index by scanning the whole mesh array,
// so one call was O(matches x depth x meshes). `todo/handoff-vita.md` §2 item 1
// asks for that to stop; the walk now goes through a sorted id -> first-index
// map (`IdIndex`), built once per call.
//
// This keeps the OLD function verbatim as `reference` and drives both over
// every input that could separate them:
//
//   * the real models, every bone name `kShadowBones` asks for plus `Piedg` /
//     `Piedd` (the crowd's feet), against EVERY root: -1, each mesh index in
//     turn, and two out-of-range roots;
//   * synthetic arrays built to break the tie rules - DUPLICATE ids (the
//     lowest index must win), a parent id present at several indices, a parent
//     id present at NONE, a mesh that is its own parent, a two-mesh CYCLE, and
//     a chain 70 deep so the 64-step guard decides;
//   * names that are the whole name, a substring, the EMPTY string (which
//     `string_view::find` matches at 0, so every mesh matches), a string
//     longer than the 21-byte field, and a name no mesh carries.
//
// Every call is compared as an index. One line a model, then `mismatches
// <total>`. It also TIMES three ways of answering, because the handoff
// attributes the 0.09 ms a frame to `memchr`/`strlen`/`memcmp` - the name scan,
// not the walk - and that decides whether a per-model index is worth its
// lifetime problem:
//
//     reference   the quadratic walk
//     current     the sorted map, what shadow.cpp does now
//     index       `omk::MeshNameIndex`, built ONCE per model, looked up
//
// Prints only; writes nothing.
#include "formats/mesh3do.h"
#include "o3de/shadow.h"
#include "platform/datafs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------- the old one
// `findMeshContaining` and its `underSkeleton` as they stood at 238d316.
bool referenceUnder(const std::vector<omk::Mesh>& meshes, int i, int root) {
    if (root < 0) return true;
    for (int guard = 0; guard < 64 && i >= 0; ++guard) {
        if (i == root) return true;
        const std::int32_t pid = meshes[static_cast<std::size_t>(i)].parent;
        int next = -1;
        for (std::size_t j = 0; j < meshes.size(); ++j)
            if (meshes[j].id == pid) { next = static_cast<int>(j); break; }
        i = next;
    }
    return false;
}

int reference(const std::vector<omk::Mesh>& meshes, const char* wanted, int underRoot) {
    int found = -1;
    for (std::size_t i = 0; i < meshes.size(); ++i)
        if (std::string_view(meshes[i].name).find(wanted) != std::string_view::npos &&
            referenceUnder(meshes, static_cast<int>(i), underRoot))
            found = static_cast<int>(i);
    return found;
}

// --------------------------------------------------- what is under test now
// `omk::MeshNameIndex` (o3de/shadow.h) - the shipped type, not a copy of it.
// Built once per mesh array for the whole name set; every `find` below is
// compared against `reference`, which is the only function here that is a
// transcription.

// --------------------------------------------------------------- the corpus
std::vector<std::string> wantedNames() {
    // The SHIPPED list first, so a name the viewer asks for and the index is
    // not built for would show here as a `missing` rather than as an answer.
    std::vector<std::string> w = omk::MeshNameIndex::shadowNames();
    for (const char* extra : {"Buste", "Tete", "",
                              "AnExtremelyLongBoneNameNoFieldHolds", "NoSuchBone"})
        w.emplace_back(extra);
    std::sort(w.begin(), w.end());
    w.erase(std::unique(w.begin(), w.end()), w.end());
    return w;
}

omk::Mesh mk(const char* name, std::int32_t id, std::int32_t parent) {
    omk::Mesh m;
    std::snprintf(m.name, sizeof(m.name), "%s", name);
    m.id = id;
    m.parent = parent;
    return m;
}

// Arrays the shipped models cannot produce, each aimed at one rule.
std::vector<std::pair<std::string, std::vector<omk::Mesh>>> synthetic() {
    std::vector<std::pair<std::string, std::vector<omk::Mesh>>> out;

    // DUPLICATE ids: 7 names three meshes, so the parent lookup must take the
    // lowest index (2) and not any of the others.
    out.push_back({"duplicate-ids", {
        mk("URoot", 1, -1), mk("UBustea", 7, 1), mk("UBusteb", 7, 1),
        mk("UPiedg", 9, 7), mk("UPiedd", 10, 7), mk("UBustec", 7, 1)}});

    // A parent id NO mesh carries (99): the walk must end, not loop.
    out.push_back({"absent-parent", {
        mk("URoot", 1, -1), mk("UBuste", 2, 99), mk("UPiedg", 3, 2)}});

    // A mesh that is ITS OWN parent, and a two-mesh cycle: only the guard ends
    // these, so both sides must burn the same 64 steps and agree.
    out.push_back({"self-parent", {
        mk("URoot", 1, -1), mk("UPiedg", 2, 2), mk("UPiedd", 3, 1)}});
    out.push_back({"two-cycle", {
        mk("URoot", 1, -1), mk("UPiedg", 2, 3), mk("UPiedd", 3, 2)}});

    // A chain 70 deep - DEEPER than the 64-step guard, so the guard decides the
    // answer and the two must cut it at the same place.
    {
        std::vector<omk::Mesh> deep{mk("URoot", 1, -1)};
        for (int k = 1; k < 70; ++k)
            deep.push_back(mk(("UBone" + std::to_string(k)).c_str(),
                              k + 1, k));
        deep.push_back(mk("UPiedg", 200, 70));
        out.push_back({"deep-chain-70", deep});
    }

    // Four LOD skeletons side by side, the crowd's shape: every bone name
    // matches four times and only the root separates them.
    {
        std::vector<omk::Mesh> lod;
        const char* pre[4] = {"Ph", "Pi", "Pm", "Pw"};
        for (int s = 0; s < 4; ++s) {
            const std::int32_t base = 100 * (s + 1);
            lod.push_back(mk((std::string(pre[s]) + "Buste").c_str(), base, -1));
            lod.push_back(mk((std::string(pre[s]) + "Tete").c_str(), base + 1, base));
            lod.push_back(mk((std::string(pre[s]) + "Piedg").c_str(), base + 2, base));
            lod.push_back(mk((std::string(pre[s]) + "Piedd").c_str(), base + 3, base));
        }
        out.push_back({"four-lods", lod});
    }

    out.push_back({"empty", {}});
    out.push_back({"one-mesh", {mk("UPiedg", 5, -1)}});
    return out;
}

struct Counts { long calls = 0, bad = 0, missing = 0; };

// Every (wanted, root) pair, including roots off the end of the array.
Counts sweep(const std::vector<omk::Mesh>& meshes, const std::vector<std::string>& wants,
             bool verbose, const char* label) {
    Counts c;
    omk::MeshNameIndex idx;
    idx.build(meshes, wants);
    const int n = static_cast<int>(meshes.size());
    std::vector<int> roots{-1, -2, n, n + 5};
    for (int i = 0; i < n; ++i) roots.push_back(i);
    for (const auto& w : wants)
        for (int r : roots) {
            // A root off the end indexes nothing in either version - the walk
            // only ever reads `meshes[i]` for an i it already found - but a
            // root of -2 must behave as "no scope" exactly as -1 does.
            const int a = reference(meshes, w.c_str(), r);
            const int b = omk::findMeshContaining(meshes, w.c_str(), r);
            const int c2 = idx.find(w, r);
            ++c.calls;
            if (a != b || a != c2) {
                ++c.bad;
                if (verbose && c.bad <= 8)
                    std::printf("  %s: wanted '%s' root %d: reference %d, "
                                "findMeshContaining %d, index %d\n",
                                label, w.c_str(), r, a, b, c2);
            }
        }
    c.missing = idx.missing();   // a name the index was not built for: a tool fault
    return c;
}

double msFor(const std::vector<omk::Mesh>& meshes, const std::vector<std::string>& wants,
             int mode, const omk::MeshNameIndex& pre, int reps, long long& sink) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < reps; ++k)
        for (const auto& w : wants) {
            // root 0 - the scoped case, which is the one that walks
            const int r = mode == 0 ? reference(meshes, w.c_str(), 0)
                        : mode == 1 ? omk::findMeshContaining(meshes, w.c_str(), 0)
                                    : pre.find(w, 0);
            sink += r;
        }
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    const auto wants = wantedNames();
    long total = 0, calls = 0, missingTotal = 0;
    long long sink = 0;

    for (const auto& [name, meshes] : synthetic()) {
        const Counts c = sweep(meshes, wants, true, name.c_str());
        std::printf("%-16s %3zu meshes  %5ld calls  %ld mismatches  %ld missing\n",
                    name.c_str(), meshes.size(), c.calls, c.bad, c.missing);
        total += c.bad;
        calls += c.calls;
        missingTotal += c.missing;
    }

    for (int a = 1; a < argc; ++a) {
        std::string path = argv[a];
        std::vector<std::byte> d;
        {
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { std::printf("%-16s  unreadable\n", path.c_str()); continue; }
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            d.resize(static_cast<std::size_t>(n < 0 ? 0 : n));
            if (!d.empty() && std::fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
            std::fclose(f);
        }
        const auto h = omk::readHeader(d);
        if (!h) { std::printf("%-16s  not a .3DO\n", path.c_str()); continue; }
        const auto meshes = omk::readMeshes(d, *h);
        const Counts c = sweep(meshes, wants, true, path.c_str());
        total += c.bad;
        calls += c.calls;
        missingTotal += c.missing;

        omk::MeshNameIndex pre;
        pre.build(meshes, wants);
        const int reps = 2000;
        const double t0 = msFor(meshes, wants, 0, pre, reps, sink);
        const double t1 = msFor(meshes, wants, 1, pre, reps, sink);
        const double t2 = msFor(meshes, wants, 2, pre, reps, sink);
        const double per = static_cast<double>(reps) * static_cast<double>(wants.size());
        const std::size_t slash = path.find_last_of('/');
        std::printf("%-16s %3zu meshes  %5ld calls  %ld mismatches  %ld missing   "
                    "reference %6.0f ns  current %6.0f ns  index %5.0f ns   a call\n",
                    path.substr(slash == std::string::npos ? 0 : slash + 1).c_str(),
                    meshes.size(), c.calls, c.bad, c.missing,
                    t0 * 1e6 / per, t1 * 1e6 / per, t2 * 1e6 / per);
    }

    std::printf("calls %ld\nmismatches %ld\nmissing %ld\n", calls, total, missingTotal);
    return sink == 0x7fffffff ? 1 : 0;   // keep the sink alive
}

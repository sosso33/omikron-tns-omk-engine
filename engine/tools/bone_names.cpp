// SPDX-License-Identifier: GPL-3.0-or-later
// HOW AN ANIMATION TRACK FINDS ITS BONE, measured over the whole corpus.
//
//     bone_names <gamedata>
//
// Every character bone name is a PREFIX plus the bone: `ViBassin` on the model
// VIR_FN, `UBassin` in `braqueur.ani`, `PhBassin` in `passantH.ani`. The
// engine never strips the prefix - `o3de_FindMeshByName` (0x00436D90) is
// `o3de_Traverse` running a `strstr` over the node names and keeping the LAST
// match, and its callers pass the BARE bone: "Bassin", "Tete", "Buste",
// "Cuisseg", "Piedd" and twelve more, in a row at 04_sys.c 5497-5513.
//
// The port cannot hard-code seventeen names and stay honest about the ones it
// has not met, so it derives the split: every prefix in the corpus is a
// capital followed by lower case and every bone starts with a capital, so THE
// BONE BEGINS AT THE SECOND UPPERCASE LETTER. This tool measures that claim
// two ways - the prefixes it implies, and whether the bone names collapse to
// one small shared vocabulary - and it prints what the old fixed `substr(2)`
// did, which was right only while every prefix happened to be two letters.
#include "formats/anim.h"
#include "formats/mesh3do.h"
#include "platform/datafs.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace {

// `DataFs::list` hands back FULL paths, so read them as files rather than
// re-resolving a name that is already resolved.
std::vector<std::byte> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> v(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n));
    return v;
}

std::string lower(std::string v) {
    for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return v;
}
// the rule under test
std::string boneOf(const std::string& n) {
    for (std::size_t i = 1; i < n.size(); ++i)
        if (n[i] >= 'A' && n[i] <= 'Z') return lower(n.substr(i));
    return n.size() > 2 ? lower(n.substr(2)) : lower(n);
}
// what the port did before, and what the crowd's four libraries got away with
std::string fixed2(const std::string& n) {
    return n.size() > 2 ? lower(n.substr(2)) : lower(n);
}
std::string prefixOf(const std::string& n) {
    const std::string b = boneOf(n);
    return b.size() < n.size() ? n.substr(0, n.size() - b.size()) : std::string();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: bone_names <gamedata>\n"); return 2; }
    omk::DataFs fs(argv[1]);

    std::map<std::string, int> prefixes;      // prefix -> how many names carry it
    std::map<std::string, int> bones;         // bone   -> how many names reduce to it
    std::set<std::string> aniBones, meshBones;
    int trackNames = 0, meshNames = 0, libs = 0, models = 0;

    for (const auto& n : fs.list("ANIMS", ".ani")) {
        const auto blob = slurp(n);
        if (blob.empty()) continue;
        ++libs;
        for (const auto& c : omk::animClips(blob)) {
            const auto d = omk::animDescriptor(blob, c.descriptor);
            if (!d) continue;
            for (const auto& t : d->tracks) {
                ++trackNames;
                ++prefixes[prefixOf(t.name)];
                ++bones[boneOf(t.name)];
                aniBones.insert(boneOf(t.name));
            }
        }
    }
    for (const auto& n : fs.list("MESHES/PERSOS", ".3DO")) {
        const auto blob = slurp(n);
        if (blob.empty()) continue;
        const auto h = omk::readHeader(blob);
        if (!h) continue;
        ++models;
        for (const auto& mesh : omk::readMeshes(blob, *h)) {
            ++meshNames;
            ++prefixes[prefixOf(mesh.name)];
            ++bones[boneOf(mesh.name)];
            meshBones.insert(boneOf(mesh.name));
        }
    }

    // How many prefixes are NOT two letters?  Every one of those is a name the
    // old fixed strip cut in the wrong place.
    int shortPrefix = 0, twoPrefix = 0, longPrefix = 0;
    for (const auto& kv : prefixes) {
        if (kv.first.size() == 2) twoPrefix += kv.second;
        else if (kv.first.size() < 2) shortPrefix += kv.second;
        else longPrefix += kv.second;
    }
    // and how many names the two rules DISAGREE on
    int disagree = 0;
    std::set<std::string> shared;
    for (const auto& b : aniBones) if (meshBones.count(b)) shared.insert(b);

    std::printf("libraries %d  models %d  track names %d  mesh names %d\n",
                libs, models, trackNames, meshNames);
    std::printf("prefixes %zu:", prefixes.size());
    for (const auto& kv : prefixes) std::printf(" %s(%d)", kv.first.empty() ? "-" : kv.first.c_str(), kv.second);
    std::printf("\n");
    std::printf("names by prefix length: 1 %d, 2 %d, 3+ %d\n", shortPrefix, twoPrefix, longPrefix);
    std::printf("distinct bones %zu  shared by .ani and .3DO %zu\n", bones.size(), shared.size());

    // THE PAIRING THAT FAILED: `braqueur.ani`, the Shooting gallery's library,
    // against VIR_FN, the model its gunmen wear. Under the old fixed strip
    // NOT ONE of the nineteen tracks bound; under the rule above every one
    // does. Reported as a pair of counts rather than a boolean so a wrong
    // answer says what it found.
    {
        const auto ani = slurp(std::string(argv[1]) + "/ANIMS/braqueur.ani");
        const auto mdl = slurp(std::string(argv[1]) + "/MESHES/PERSOS/VIR_FN.3DO");
        const auto h = omk::readHeader(mdl);
        std::set<std::string> meshNew, meshOld;
        int nm = 0;
        if (h) for (const auto& mesh : omk::readMeshes(mdl, *h)) {
            meshNew.insert(boneOf(mesh.name));
            meshOld.insert(fixed2(mesh.name));
            ++nm;
        }
        int tracks = 0, hitNew = 0, hitOld = 0;
        for (const auto& c : omk::animClips(ani)) {
            const auto d = omk::animDescriptor(ani, c.descriptor);
            if (!d) continue;
            for (const auto& t : d->tracks) {
                ++tracks;
                if (meshNew.count(boneOf(t.name)))  ++hitNew;
                if (meshOld.count(fixed2(t.name)))  ++hitOld;
            }
            break;                      // the first clip is enough - 19 tracks
        }
        std::printf("braqueur.ani -> VIR_FN: %d meshes, %d tracks, "
                    "%d resolve by bone, %d by the old fixed strip\n",
                    nm, tracks, hitNew, hitOld);
    }
    (void)disagree; (void)fixed2;
    return 0;
}

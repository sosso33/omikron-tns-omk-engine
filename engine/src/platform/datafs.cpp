// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/datafs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace omk {
namespace {

std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(std::string_view rel) {
    std::vector<std::string> parts;
    std::string cur;
    const auto flush = [&] {
        // "" and "." both mean the directory itself; ".." is not accepted -
        // nothing in the game's data references a parent, and allowing it
        // would let a data string escape the root
        if (!cur.empty() && cur != ".") parts.push_back(cur);
        cur.clear();
    };
    for (char c : rel) {
        if (c == '/' || c == '\\') { flush(); }   // the data uses backslashes
        else                         { cur.push_back(c); }
    }
    flush();
    return parts;
}

std::vector<std::byte> slurp(const std::string& real) {
    std::ifstream f(real, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n));
    return d;
}

}  // namespace

const std::map<std::string, std::string>* DataFs::indexOf(
        const std::string& dir) const {
    if (const auto it = index_.find(dir); it != index_.end()) return &it->second;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return nullptr;
    std::map<std::string, std::string> m;
    for (const auto& e : fs::directory_iterator(dir, ec))
        m.emplace(lower(e.path().filename().string()),
                  e.path().filename().string());
    return &index_.emplace(dir, std::move(m)).first->second;
}

std::optional<std::string> DataFs::resolve(std::string_view rel) const {
    std::string at = root_;
    for (const auto& part : split(rel)) {
        const auto* idx = indexOf(at);
        if (!idx) return std::nullopt;
        const auto it = idx->find(lower(part));
        if (it == idx->end()) return std::nullopt;
        at = (fs::path(at) / it->second).string();
    }
    return at;
}

std::vector<std::byte> DataFs::read(std::string_view rel) const {
    const auto real = resolve(rel);
    return real ? slurp(*real) : std::vector<std::byte>{};
}

std::optional<std::string> DataFs::resolveSibling(std::string_view rel,
                                                  std::string_view ext) const {
    fs::path p{std::string(rel)};
    std::string e(ext);
    if (!e.empty() && e.front() != '.') e.insert(e.begin(), '.');
    p.replace_extension(e);
    return resolve(p.string());
}

std::vector<std::byte> DataFs::readSibling(std::string_view rel,
                                           std::string_view ext) const {
    const auto real = resolveSibling(rel, ext);
    return real ? slurp(*real) : std::vector<std::byte>{};
}

std::vector<std::string> DataFs::list(std::string_view relDir,
                                      std::string_view ext) const {
    std::vector<std::string> out;
    const auto dir = resolve(relDir);
    if (!dir) return out;
    const auto* idx = indexOf(*dir);
    if (!idx) return out;
    std::string want = lower(std::string(ext));
    if (!want.empty() && want.front() != '.') want.insert(want.begin(), '.');
    for (const auto& [lc, real] : *idx) {
        const auto dot = lc.rfind('.');
        if (dot == std::string::npos) continue;
        if (lc.substr(dot) == want) out.push_back((fs::path(*dir) / real).string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> DataFs::subdirs(std::string_view relDir) const {
    std::vector<std::string> out;
    const auto dir = resolve(relDir);
    if (!dir) return out;
    const auto* idx = indexOf(*dir);
    if (!idx) return out;
    std::error_code ec;
    for (const auto& [lc, real] : *idx) {
        (void)lc;
        const auto p = (fs::path(*dir) / real).string();
        if (fs::is_directory(p, ec)) out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::byte> DataFs::readPath(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) return slurp(path);
    // fall back to a case-insensitive lookup of the last component
    const fs::path p(path);
    const auto dir = p.has_parent_path() ? p.parent_path().string() : std::string(".");
    DataFs fsys(dir);
    const auto r = fsys.resolve(p.filename().string());
    return r ? slurp(*r) : std::vector<std::byte>{};
}

bool safeOutputPath(const std::string& path) {
    // Two independent tests, because either alone has a hole: an extension
    // test misses a data file with an unusual name, and a location test misses
    // a shipped asset copied somewhere else.
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const char* kData[] = {".3DO", ".3DT", ".3DM", ".3DA", ".3DP",
                                  ".SCX", ".SFX", ".CTL", ".ANI", ".ADP",
                                  ".FNT", ".TAG", ".EXE", ".DLL", ".MPG"};
    for (const char* d : kData) {
        if (ext == d) {
            std::fprintf(stderr,
                "refusing to write %s: %s is a shipped game-data extension, so "
                "this is almost certainly an argument in the wrong position\n",
                path.c_str(), ext.c_str());
            return false;
        }
    }
    // And anything inside a directory that looks like the shipped tree.
    std::error_code ec;
    const auto abs = fs::weakly_canonical(fs::path(path), ec).string();
    for (const char* dir : {"/MESHES/", "/IAM/", "/SCPTDATA/", "/FONTS/",
                            "/TRACKS/", "/I2D/", "/IMAGES/", "/FLIS/"}) {
        if (abs.find(dir) != std::string::npos) {
            std::fprintf(stderr,
                "refusing to write %s: it is inside the shipped data tree\n",
                path.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace omk

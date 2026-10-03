// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/datafs.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdio>

#if defined(__vita__)
#  include <psp2/io/dirent.h>
#  include <psp2/io/fcntl.h>
#  include <psp2/io/stat.h>
#elif defined(macintosh)
#  include <Files.h>
#  include <TextUtils.h>
#else
#  include <filesystem>
namespace fs = std::filesystem;
#endif

namespace omk {

std::string hostPath(const std::string& p) {
#if defined(macintosh)
    if (p.empty()) return p;
    const bool hfsRoot = p.find(':') != std::string::npos;   // the caller wrote HFS
    const bool absolute = hfsRoot || p[0] == '/';
    std::string out = absolute ? "" : ":";
    std::size_t i = 0;
    while (i <= p.size()) {
        const std::size_t j = p.find_first_of("/\\", i);
        const std::string part = p.substr(i, (j == std::string::npos ? p.size() : j) - i);
        i = (j == std::string::npos) ? p.size() + 1 : j + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") {                                  // "::" climbs one
            if (out.empty() || out.back() != ':') out += ':';
            out += ':';
            continue;
        }
        if (!out.empty() && out.back() != ':') out += ':';
        out += part;
    }
    // a bare volume name is a file in the default folder until it has its ':'
    if (absolute && out.find(':') == std::string::npos) out += ':';
    return out;
#else
    return p;
#endif
}

namespace {

#if defined(macintosh)
// The File Manager's view of a path: its FSSpec, and its catalog record.
// -> false when the path does not name something that exists.
bool catalogOf(const std::string& p, FSSpec& spec, CInfoPBRec& pb) {
    const std::string h = hostPath(p);
    if (h.empty() || h.size() > 255) return false;
    Str255 name;
    c2pstrcpy(name, h.c_str());
    if (FSMakeFSSpec(0, 0, name, &spec) != noErr) return false;
    std::memset(&pb, 0, sizeof pb);
    pb.hFileInfo.ioNamePtr = spec.name;
    pb.hFileInfo.ioVRefNum = spec.vRefNum;
    pb.hFileInfo.ioDirID = spec.parID;
    pb.hFileInfo.ioFDirIndex = 0;
    return PBGetCatInfoSync(&pb) == noErr;
}
#endif

// ---- the platform layer (see `datafs.h`): four questions and a join ------
bool isDirectory(const std::string& p) {
#if defined(__vita__)
    SceIoStat st;
    return sceIoGetstat(p.c_str(), &st) >= 0 && SCE_S_ISDIR(st.st_mode);
#elif defined(macintosh)
    FSSpec spec; CInfoPBRec pb;
    return catalogOf(p, spec, pb) && (pb.hFileInfo.ioFlAttrib & kioFlAttribDirMask) != 0;
#else
    std::error_code ec;
    return fs::is_directory(p, ec);
#endif
}
bool pathExists(const std::string& p) {
#if defined(__vita__)
    SceIoStat st;
    return sceIoGetstat(p.c_str(), &st) >= 0;
#elif defined(macintosh)
    FSSpec spec; CInfoPBRec pb;
    return catalogOf(p, spec, pb);
#else
    std::error_code ec;
    return fs::exists(p, ec);
#endif
}
// The names in a directory, without "." and "..".
std::vector<std::string> listNames(const std::string& dir) {
    std::vector<std::string> out;
#if defined(__vita__)
    const SceUID d = sceIoDopen(dir.c_str());
    if (d < 0) return out;
    SceIoDirent e;
    while (sceIoDread(d, &e) > 0) {
        const std::string n = e.d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    sceIoDclose(d);
#elif defined(macintosh)
    // the folder's entries by index, 1 up, until the File Manager runs out
    FSSpec spec; CInfoPBRec pb;
    if (!catalogOf(dir, spec, pb) || !(pb.hFileInfo.ioFlAttrib & kioFlAttribDirMask)) return out;
    const long dirID = pb.dirInfo.ioDrDirID;
    for (short k = 1;; ++k) {
        Str255 name;
        std::memset(&pb, 0, sizeof pb);
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = spec.vRefNum;
        pb.hFileInfo.ioDirID = dirID;        // reset: an indexed call overwrites it
        pb.hFileInfo.ioFDirIndex = k;
        if (PBGetCatInfoSync(&pb) != noErr) break;
        out.emplace_back(reinterpret_cast<const char*>(name + 1), name[0]);
    }
#else
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        out.push_back(e.path().filename().string());
#endif
    return out;
}
std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\' || last == ':') ? a + b : a + "/" + b;
}
// The last component and the rest, on either separator.
std::string baseName(const std::string& p) {
    const auto cut = p.find_last_of("/\\");
    return cut == std::string::npos ? p : p.substr(cut + 1);
}
std::string parentOf(const std::string& p) {
    const auto cut = p.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : p.substr(0, cut);
}
// ".ext" of the last component, empty when it has none.
std::string extensionOf(const std::string& p) {
    const std::string b = baseName(p);
    const auto dot = b.rfind('.');
    return (dot == std::string::npos || dot == 0) ? std::string() : b.substr(dot);
}

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

std::vector<std::byte> slurp(const std::string& real) { return readWholeFile(real); }

}  // namespace

const std::map<std::string, std::string>* DataFs::indexOf(
        const std::string& dir) const {
    if (const auto it = index_.find(dir); it != index_.end()) return &it->second;
    if (!isDirectory(dir)) return nullptr;
    std::map<std::string, std::string> m;
    for (const auto& name : listNames(dir))
        m.emplace(lower(name), name);
    return &index_.emplace(dir, std::move(m)).first->second;
}

std::optional<std::string> DataFs::resolve(std::string_view rel) const {
    std::string at = root_;
    for (const auto& part : split(rel)) {
        const auto* idx = indexOf(at);
        if (!idx) return std::nullopt;
        const auto it = idx->find(lower(part));
        if (it == idx->end()) return std::nullopt;
        at = join(at, it->second);
    }
    return at;
}

std::vector<std::byte> DataFs::read(std::string_view rel) const {
    const auto real = resolve(rel);
    return real ? slurp(*real) : std::vector<std::byte>{};
}

std::optional<std::string> DataFs::resolveSibling(std::string_view rel,
                                                  std::string_view ext) const {
    std::string p(rel);
    std::string e(ext);
    if (!e.empty() && e.front() != '.') e.insert(e.begin(), '.');
    // replace_extension: drop the last component's extension, add this one
    const std::string old = extensionOf(p);
    if (!old.empty()) p.erase(p.size() - old.size());
    return resolve(p + e);
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
        if (lc.substr(dot) == want) out.push_back(join(*dir, real));
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
    for (const auto& [lc, real] : *idx) {
        (void)lc;
        const auto p = join(*dir, real);
        if (isDirectory(p)) out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::byte> DataFs::readPath(const std::string& path) {
    if (pathExists(path)) return slurp(path);
    // fall back to a case-insensitive lookup of the last component
    const std::string parent = parentOf(path);
    const auto dir = parent.empty() ? std::string(".") : parent;
    DataFs fsys(dir);
    const auto r = fsys.resolve(baseName(path));
    return r ? slurp(*r) : std::vector<std::byte>{};
}

bool safeOutputPath(const std::string& path) {
    // Two independent tests, because either alone has a hole: an extension
    // test misses a data file with an unusual name, and a location test misses
    // a shipped asset copied somewhere else.
    std::string ext = extensionOf(path);
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
#if defined(__vita__) || defined(macintosh)
    const std::string abs = path;   // no canonical form to take (the Vita's device paths are absolute)
#else
    std::error_code ec;
    const auto abs = fs::weakly_canonical(fs::path(path), ec).string();
#endif
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

bool makeDirectories(const std::string& dir) {
    if (dir.empty()) return true;
#if defined(__vita__)
    // each prefix in turn - `sceIoMkdir` makes one level, and an existing one
    // is not an error for this
    for (std::size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            const std::string p = dir.substr(0, i);
            if (!p.empty() && p.back() != ':' && !isDirectory(p)) sceIoMkdir(p.c_str(), 0777);
        }
    }
    return isDirectory(dir);
#elif defined(macintosh)
    // each prefix in turn, as on the Vita: `FSpDirCreate` makes one level
    for (std::size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/' || dir[i] == '\\') {
            const std::string p = dir.substr(0, i);
            if (p.empty() || isDirectory(p)) continue;
            const std::string h = hostPath(p);
            if (h.size() > 255) return false;
            Str255 name;
            c2pstrcpy(name, h.c_str());
            FSSpec spec;
            if (FSMakeFSSpec(0, 0, name, &spec) == fnfErr) {
                long made = 0;
                FSpDirCreate(&spec, smSystemScript, &made);
            }
        }
    }
    return isDirectory(dir);
#else
    std::error_code ec;
    fs::create_directories(dir, ec);
    return fs::is_directory(dir, ec);
#endif
}

long long fileSize(const std::string& path) {
#if defined(__vita__)
    SceIoStat st;
    if (sceIoGetstat(path.c_str(), &st) < 0) return -1;
    return static_cast<long long>(st.st_size);
#elif defined(macintosh)
    FSSpec spec; CInfoPBRec pb;
    if (!catalogOf(path, spec, pb) || (pb.hFileInfo.ioFlAttrib & kioFlAttribDirMask)) return -1;
    return static_cast<long long>(pb.hFileInfo.ioFlLgLen);   // the data fork
#else
    std::error_code ec;
    const auto n = fs::file_size(path, ec);
    return ec ? -1 : static_cast<long long>(n);
#endif
}

std::vector<std::byte> readWholeFile(const std::string& path) {
    const long long size = fileSize(path);
    if (size < 0) return {};
    std::vector<std::byte> d(static_cast<std::size_t>(size));
    std::size_t got = 0;
#if defined(__vita__)
    const SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return {};
    while (got < d.size()) {
        // at most 1 MiB a call: a kernel read need not deliver all it is asked
        const std::size_t want = std::min<std::size_t>(d.size() - got, 1u << 20);
        const int r = sceIoRead(fd, d.data() + got, static_cast<SceSize>(want));
        if (r <= 0) break;
        got += static_cast<std::size_t>(r);
    }
    sceIoClose(fd);
#elif defined(macintosh)
    // READ-ONLY, through the File Manager: the C library's `open` asks for
    // read-WRITE whatever the mode, which a locked file or a CD refuses
    FSSpec spec; CInfoPBRec pb;
    if (!catalogOf(path, spec, pb)) return {};
    short ref = 0;
    if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) return {};
    while (got < d.size()) {
        long want = static_cast<long>(std::min<std::size_t>(d.size() - got, 1u << 20));
        const OSErr e = FSRead(ref, &want, d.data() + got);
        if (want <= 0) break;
        got += static_cast<std::size_t>(want);
        if (e != noErr) break;
    }
    FSClose(ref);
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    while (got < d.size()) {
        const std::size_t r = std::fread(d.data() + got, 1, d.size() - got, f);
        if (r == 0) break;
        got += r;
    }
    std::fclose(f);
#endif
    if (got < d.size()) {
        std::printf("datafs: SHORT READ %s - %lu of %lu bytes\n", path.c_str(),
                    static_cast<unsigned long>(got), static_cast<unsigned long>(d.size()));
        d.resize(got);
    }
    return d;
}

std::string readTextFile(const std::string& path) {
    const auto d = readWholeFile(path);
    return std::string(reinterpret_cast<const char*>(d.data()), d.size());
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t j = text.find('\n', i);
        if (j == std::string::npos) { out.push_back(text.substr(i)); break; }
        out.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

bool writeWholeFile(const std::string& path, const void* data, std::size_t n) {
    std::FILE* f = std::fopen(hostPath(path).c_str(), "wb");
    if (!f) return false;
    const bool all = n == 0 || std::fwrite(data, 1, n, f) == n;
    return std::fclose(f) == 0 && all;
}

}  // namespace omk

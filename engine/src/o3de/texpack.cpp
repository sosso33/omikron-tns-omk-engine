// SPDX-License-Identifier: GPL-3.0-or-later
// The texture pack - see texpack.h. Not a port of anything in the original.
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "o3de/texpack.h"

#include "o3de/greybank.h"
#include "formats/blockdecode.h"
#include "platform/datafs.h"
#include "platform/events.h"
#include "platform/hostfs.h"
#include "platform/packkey.h"
#include "platform/pngread.h"
#include "platform/threads.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace omk {
namespace {

bool hexKey(const std::string& s, std::uint64_t& k) {
    if (s.size() != 16) return false;
    k = 0;
    for (char c : s) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return false;
        k = (k << 4) | static_cast<std::uint64_t>(v);
    }
    return true;
}

// 1, 2, 4 ... 16
bool powerOfTwoFactor(int big, int small) {
    if (small <= 0 || big % small) return false;
    const int f = big / small;
    return f >= 1 && f <= 16 && !(f & (f - 1));
}

bool blockPixels(const std::string* path, const Texture& t, bool light, std::vector<std::uint8_t>& px,
                 int& w, int& h, std::string& why);

// the normal map's PNG - or, with none (`path` empty), its BC7 decoded - at
// the colour's size or a power of two of the original's; its alpha (the
// height) kept at 1 or more - 0 reads "none"
// THE ARCHIVE (`kPackArchiveName`, texpack.h): the one the pack opened, kept
// open; its files are indexed as `omkp:<name>` and read here, one at a time
// (a descriptor's offset is shared - newlib's pread on the Switch seeks)
struct Archive {
    Mutex m;
    long handle = -1;
    void* file = nullptr;
    std::string path;
    std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> at;   // name -> offset, size
};
Archive& archive() {
    static auto* a = new Archive;   // never destroyed: a reader may outlive the statics
    return *a;
}
constexpr char kArchivePath[] = "omkp:";

// a pack file's bytes: from the archive, or the file itself
std::vector<std::byte> packRead(const std::string& path) {
    if (path.rfind(kArchivePath, 0) != 0) return readWholeFile(path);
    Archive& a = archive();
    const auto it = a.at.find(path.substr(sizeof kArchivePath - 1));
    if (it == a.at.end()) return {};
    std::vector<std::byte> d(static_cast<std::size_t>(it->second.second));
    ScopedLock l(a.m);
    if (a.handle < 0 && !a.file) return {};
    const long long r = hostfs::readAt(a.handle, a.file, it->second.first, d.data(), d.size());
    if (r != static_cast<long long>(d.size())) return {};
    return d;
}

// open DIR's archive, if it has one: its names, or nothing (no archive, or
// one whose index does not hold - said)
std::vector<std::string> openArchive(const std::string& dir) {
    Archive& a = archive();
    {
        ScopedLock l(a.m);
        if (a.handle >= 0 || a.file) hostfs::closeFile(a.handle, a.file);
        a.handle = -1;
        a.file = nullptr;
        a.at.clear();
    }
    const std::string path = dir + "/" + kPackArchiveName;
    const long long size = fileSize(path);
    if (size < 16) return {};
    const auto head = readFileRange(path, 0, 16);
    if (head.size() != 16 || std::memcmp(head.data(), kPackArchiveMagic, 8) != 0) {
        std::fprintf(stderr, "texture pack: %s is not an archive this reads\n", path.c_str());
        return {};
    }
    const auto u32 = [](const std::byte* p) {
        return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
               static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
    };
    const auto u64 = [&](const std::byte* p) {
        return static_cast<std::uint64_t>(u32(p)) | static_cast<std::uint64_t>(u32(p + 4)) << 32;
    };
    const std::uint32_t count = u32(head.data() + 8);
    const std::uint64_t indexEnd = 16 + static_cast<std::uint64_t>(count) * kPackArchiveEntryBytes;
    if (count == 0 || indexEnd > static_cast<std::uint64_t>(size)) {
        std::fprintf(stderr, "texture pack: %s - %u entries do not fit its %lld bytes\n", path.c_str(), count, size);
        return {};
    }
    const auto index = readFileRange(path, 16, static_cast<std::size_t>(indexEnd - 16));
    if (index.size() != indexEnd - 16) return {};
    // SELF-CHECKING: every file inside the archive, past the index, in order,
    // none overlapping the next, every name ended
    std::vector<std::string> names;
    std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> at;
    std::uint64_t after = indexEnd;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::byte* e = index.data() + static_cast<std::size_t>(i) * kPackArchiveEntryBytes;
        const char* n = reinterpret_cast<const char*>(e);
        const std::size_t len = strnlen(n, kPackArchiveNameBytes);
        const std::uint64_t off = u64(e + kPackArchiveNameBytes), sz = u64(e + kPackArchiveNameBytes + 8);
        if (len == 0 || len == kPackArchiveNameBytes || off < after || sz > static_cast<std::uint64_t>(size) ||
            off > static_cast<std::uint64_t>(size) - sz) {
            std::fprintf(stderr, "texture pack: %s - entry %u does not hold (its name, or its bytes past the "
                                 "file or over the one before)\n", path.c_str(), i);
            return {};
        }
        after = off + sz;
        names.emplace_back(n, len);
        at[names.back()] = {off, sz};
    }
    ScopedLock l(a.m);
    if (!hostfs::openRead(path, a.handle, a.file)) return {};
    a.path = path;
    a.at = std::move(at);
    std::printf("texture pack: %s - %u files in one archive, %lld bytes\n", path.c_str(), count, size);
    return names;
}

void pngNormal(const std::string& path, const std::string* bc, const Texture& t, PackImage& o) {
    PngImage ni;
    std::string why;
    if (path.empty()) {
        if (!blockPixels(bc, t, false, ni.rgba, ni.w, ni.h, why)) return;
    } else {
        const auto nb = packRead(path);
        if (nb.empty() || !decodePng(reinterpret_cast<const std::uint8_t*>(nb.data()), nb.size(), ni) ||
            !powerOfTwoFactor(ni.w, t.width) || !powerOfTwoFactor(ni.h, t.height)) return;
    }
    for (std::size_t q = 3; q < ni.rgba.size(); q += 4) ni.rgba[q] = std::max<std::uint8_t>(1, ni.rgba[q]);
    o.nw = ni.w; o.nh = ni.h; o.normal = std::move(ni.rgba);
}

// a pack KTX2 of `t`'s: read, its format right for its kind (`light`: BC4 or
// ASTC; else BC7 or ASTC), its size a power of two of t's
bool readBlocks(const std::string& path, const Texture& t, bool light, PackBlocks& z, std::string& why) {
    const auto b = packRead(path);
    std::vector<std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(b.data()),
                                    reinterpret_cast<const std::uint8_t*>(b.data()) + b.size());
    Ktx2 k;
    if (!readKtx2(std::move(bytes), k, &why)) return false;
    const BlockFormat f = blockFormat(k.vkFormat);
    // the format its NAME promised (`.astc.ktx2`, else `.bc7` / `.bc4`): the
    // backend that took it from that slot samples that codec alone (the
    // remaster audit's item 7)
    const bool astcSlot = path.size() >= 10 && path.compare(path.size() - 10, 10, ".astc.ktx2") == 0;
    if (f.astc != astcSlot) { why = "not the codec its name says"; return false; }
    if (!f.astc && (light ? k.vkFormat != kVkBc4Unorm : k.vkFormat != kVkBc7Unorm)) { why = "the wrong block format"; return false; }
    if (!powerOfTwoFactor(k.w, t.width) || !powerOfTwoFactor(k.h, t.height)) { why = "not a power of two of the original"; return false; }
    z.vkFormat = k.vkFormat; z.w = k.w; z.h = k.h;
    z.levels = std::move(k.levels);
    z.bytes = std::move(k.bytes);
    return true;
}

// the pixels of a pack BC KTX2 (`path`: BC7, or BC4 for a `light`) - level 0
// decoded on the CPU (`formats/blockdecode.h`), where no PNG stands beside it
bool blockPixels(const std::string* path, const Texture& t, bool light, std::vector<std::uint8_t>& px,
                 int& w, int& h, std::string& why) {
    PackBlocks z;
    if (!path) { why = "no PNG, and no BC KTX2 to decode"; return false; }
    if (!readBlocks(*path, t, light, z, why)) return false;
    if (z.levels.empty() || blockFormat(z.vkFormat).astc) { why = "no BC level to decode"; return false; }
    const std::span<const std::uint8_t> l(z.bytes.data() + z.levels[0].first, z.levels[0].second);
    if (!(light ? decodeBc4(l, z.w, z.h, px) : decodeBc7(l, z.w, z.h, px))) { why = "its blocks cut short"; return false; }
    w = z.w; h = z.h;
    return true;
}

// what a parked result holds
std::size_t imageBytes(const PackImage& p) {
    return p.rgba.size() + p.emissive.size() + p.normal.size() + p.colourZ.bytes.size() +
           p.emissiveZ.bytes.size() + p.normalZ.bytes.size();
}

}  // namespace

std::uint64_t textureKey(const Texture& t) {
    Fnv1a h;
    for (const int v : {t.width, t.height}) h.mixLe(static_cast<std::uint32_t>(v), 4);
    const std::size_t n = static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height);
    const std::uint8_t* idx = t.idx.data();
    for (std::size_t i = 0; i < n && i < t.idx.size(); ++i) h.mix(idx[i]);
    const std::uint8_t* pal = t.pal.data();
    for (std::size_t i = 0; i < 768; ++i) h.mix(i < t.pal.size() ? pal[i] : 0);
    return h.h;
}

std::string textureKeyHex(std::uint64_t key) { return keyHex(key); }

bool TexturePack::open(const std::string& dir) {
    files_.clear();
    lights_.clear();
    normals_.clear();
    for (auto& c : ktx_) for (auto& m : c) m.clear();
    {
        std::lock_guard<std::mutex> lk(m_);
        refused_.clear();
        greyOf_.clear();
        parked_.clear();
        parkedRefs_.clear();
        parkedBytes_ = 0;
        parkedOrder_.clear();
    }
    opened_ = false;
    dir_ = dir;
    if (!hostfs::isDirectory(dir)) {
        std::fprintf(stderr, "texture pack: %s is not a directory - the textures stay as shipped\n",
                     dir.c_str());
        return false;
    }
    // ...its ARCHIVE's names when it has one, else the folder's
    std::vector<std::string> names = openArchive(dir);
    const bool archived = !names.empty();
    if (!archived) names = hostfs::listNames(dir);
    const auto at = [&](const std::string& name) {
        return archived ? std::string(kArchivePath) + name : dir + "/" + name;
    };
    for (const auto& name : names) {
        // `<key>[_e|_n].<bc7|bc4|astc>.ktx2` - the block formats
        if (name.size() > 21 && name.compare(name.size() - 5, 5, ".ktx2") == 0) {
            std::uint64_t k;
            if (!hexKey(name.substr(0, 16), k)) continue;
            std::string rest = name.substr(16, name.size() - 21);   // [_e|_n].codec
            int kind = 0;
            if (rest.compare(0, 2, "_e") == 0) { kind = 1; rest = rest.substr(2); }
            else if (rest.compare(0, 2, "_n") == 0) { kind = 2; rest = rest.substr(2); }
            const int codec = rest == ".astc" ? 1 : (rest == ".bc7" && kind != 1) || (rest == ".bc4" && kind == 1) ? 0 : -1;
            if (codec >= 0) ktx_[codec][kind][k] = at(name);
            continue;
        }
        if (name.size() != 20 && name.size() != 22) continue;
        const std::string ext = name.substr(name.size() - 4);
        if (ext != ".png" && ext != ".PNG") continue;
        const bool suffixed = name.size() == 22;
        const bool light = suffixed && name.compare(16, 2, "_e") == 0;
        const bool normal = suffixed && name.compare(16, 2, "_n") == 0;
        if (suffixed && !light && !normal) continue;
        std::uint64_t k;
        if (hexKey(name.substr(0, 16), k)) (light ? lights_ : normal ? normals_ : files_)[k] = at(name);
    }
    // A KTX2 NEEDS NO PNG BESIDE IT: a key with blocks and no PNG is indexed
    // with an EMPTY path - drawn from its blocks, and where pixels are wanted
    // (a GL without BPTC, a grey alias, a raw light map) its BC decoded on
    // the CPU (`blockPixels`). A pack shipped for play drops the PNGs, half
    // its size; the PNG, when there, is still the source a person edits.
    for (int kind = 0; kind < 3; ++kind) {
        auto& idx = kind == 0 ? files_ : kind == 1 ? lights_ : normals_;
        for (const auto& c : ktx_)
            for (const auto& kv : c[kind]) idx.emplace(kv.first, std::string());
    }
    // THE CURATION (`todo/remaster.md` B2): `exclude.txt` in the pack, a key a
    // line (`<16 hex>  # NAME why`) - what the upscaler hurt (lettering the AI
    // garbles): its colour and its normal map left out, so the SHIPPED texture
    // draws; its light map kept (the bloom's, read off the original's
    // texels). `omktexpack.py curate` drafts one beside the pack, unapplied.
    excluded_ = 0;
    if (std::FILE* f = std::fopen((dir + "/exclude.txt").c_str(), "rb")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            std::string ln(line);
            if (const auto h = ln.find('#'); h != std::string::npos) ln.resize(h);
            const auto a = ln.find_first_not_of(" \t\r\n");
            std::uint64_t k;
            if (a == std::string::npos || ln.size() < a + 16 || !hexKey(ln.substr(a, 16), k)) continue;
            excluded_ += files_.erase(k);
            normals_.erase(k);
            for (auto& c : ktx_) { c[0].erase(k); c[2].erase(k); }
        }
        std::fclose(f);
    }
    opened_ = true;
    return true;
}

std::vector<PackImage> TexturePack::replacements(std::span<const Texture* const> ts, bool colour,
                                                bool normals, bool pooled) const {
    std::vector<PackImage> out(ts.size());
    if (!opened_ || (files_.empty() && lights_.empty())) return out;
    // the lookups, here: the key and the file of every texture the pack holds
    struct Job { std::size_t at; std::uint64_t key; const std::string* file; const std::string* light;
                 std::string why; bool grey; const std::string* normal = nullptr;
                 // the KTX2s, when the backend samples their codec: colour, light, normal
                 const std::string* z[3] = {nullptr, nullptr, nullptr};
                 // ...and the BC ones whatever it samples: the pixels where no PNG is
                 const std::string* bc[3] = {nullptr, nullptr, nullptr}; };
    std::vector<Job> jobs;
    // the light maps: always when they alone are asked; with the colours only
    // when a bloom will read them
    const bool lights = !colour || wantLights_;
    // the KEYS first, outside the lock: `textureKey` hashes every texel, and
    // the preload's jobs and the frame's pool ask at once - the lock is for
    // `refused_` and `greyOf_` alone (the audit's performance item 1,
    // 2026-10-23: six preload jobs held the frame's pool behind their hashing)
    std::vector<std::uint64_t> keys(ts.size(), 0);
    for (std::size_t i = 0; i < ts.size(); ++i)
        if (ts[i] && ts[i]->hasPixels()) keys[i] = textureKey(*ts[i]);
    std::unique_lock<std::mutex> lk(m_);
    // a PREPARED result, when this asks for the colours: the parking asked
    // them with the normal maps as `wantNormals_` had them - dropped here for
    // a pool that takes none (GLES)
    const bool takeParked = colour && !parked_.empty();
    for (std::size_t i = 0; i < ts.size(); ++i) {
        const Texture* t = ts[i];
        if (!t || !t->hasPixels()) continue;
        std::uint64_t key = keys[i];
        if (takeParked) {
            if (const auto pk = parked_.find(key); pk != parked_.end()) {
                parkedBytes_ -= imageBytes(pk->second);
                out[i] = std::move(pk->second);
                parked_.erase(pk);
                parkedRefs_.erase(key);
                ++parkedTaken_;
                if (!normals) {
                    out[i].normal = {};
                    out[i].normalZ = {};
                    out[i].nw = out[i].nh = 0;
                }
                continue;
            }
        }
        // (a texture the pack holds nothing for is still a job for the hook)
        bool grey = false;
        if (!files_.count(key) && !lights_.count(key))
            if (const auto g = greyOf_.find(key); g != greyOf_.end()) { key = g->second; grey = true; }
        // a REFUSED colour is not read again - but its light map still is
        const auto it = refused_.count(key) ? files_.end() : files_.find(key);
        const auto lt = lights ? lights_.find(key) : lights_.end();
        if ((it == files_.end() && lt == lights_.end()) && !lightHook_) continue;
        const auto nt = normals && wantNormals_ && !grey ? normals_.find(key) : normals_.end();
        jobs.push_back({i, key, it != files_.end() ? &it->second : nullptr,
                        lt != lights_.end() ? &lt->second : nullptr, {}, grey,
                        nt != normals_.end() ? &nt->second : nullptr});
        for (int kind = 0; kind < 3; ++kind)
            if (const auto f = ktx_[0][kind].find(key); f != ktx_[0][kind].end()) jobs.back().bc[kind] = &f->second;
        // the block formats: with the colours, never for a grey alias (its
        // pixels are recomputed) nor what the light hook paints
        if (colour && codecs_ && !grey) {
            Job& jb = jobs.back();
            for (const int c : {0, 1}) {
                if (!(codecs_ & (c == 0 ? kCodecBc : kCodecAstc))) continue;
                const auto pick = [&](int kind, bool want) -> const std::string* {
                    if (!want) return nullptr;
                    const auto f = ktx_[c][kind].find(key);
                    return f != ktx_[c][kind].end() ? &f->second : nullptr;
                };
                // a light map the hook paints into stays its PNG: the hook
                // works on its pixels (the colour it reads falls back to the
                // shipped texels when the replacement is blocks)
                const bool rawLight = rawFilter_ && rawFilter_(*t);
                for (int kind = 0; kind < 3; ++kind)
                    if (!jb.z[kind]) jb.z[kind] = pick(kind, kind == 0 ? jb.file != nullptr
                                                           : kind == 1 ? jb.light != nullptr && !rawLight
                                                                       : jb.normal != nullptr);
            }
        }
    }
    lk.unlock();
    // the reads and the decodes, on the pool: each job writes only its own
    // result and its own reason
    const auto across = [&](auto&& fn) {
        if (pooled) Threads::shared().parallelFor(0, jobs.size(), 1, fn);
        else fn(std::size_t{0}, jobs.size());
    };
    across([&](std::size_t from, std::size_t to) {
        for (std::size_t j = from; j < to; ++j) {
            Job& jb = jobs[j];
            const Texture& t = *ts[jb.at];
            PackImage& o = out[jb.at];
            if (jb.light && jb.z[1] && readBlocks(*jb.z[1], t, true, o.emissiveZ, jb.why)) {
                o.ew = o.emissiveZ.w; o.eh = o.emissiveZ.h;
            } else if (jb.light && jb.light->empty()) {
                // no PNG: its BC4 decoded (a raw light map the hook paints into)
                std::string why;
                if (!blockPixels(jb.bc[1], t, true, o.emissive, o.ew, o.eh, why)) {
                    o.emissive.clear();
                    o.ew = o.eh = 0;
                }
            } else if (jb.light) {
                // the light map: its red channel, any size the rule allows
                const auto lb = packRead(*jb.light);
                PngImage li;
                if (!lb.empty() &&
                    decodePng(reinterpret_cast<const std::uint8_t*>(lb.data()), lb.size(), li) &&
                    powerOfTwoFactor(li.w, t.width) && powerOfTwoFactor(li.h, t.height)) {
                    o.ew = li.w; o.eh = li.h;
                    o.emissive.resize(static_cast<std::size_t>(li.w) * li.h);
                    for (std::size_t q = 0; q < o.emissive.size(); ++q) o.emissive[q] = li.rgba[4 * q];
                }
            }
            if (!jb.file || !colour) continue;
            if (jb.z[0] && readBlocks(*jb.z[0], t, false, o.colourZ, jb.why)) {
                o.ok = true; o.w = o.colourZ.w; o.h = o.colourZ.h;
                if (jb.normal && jb.z[2] && readBlocks(*jb.z[2], t, false, o.normalZ, jb.why)) {
                    o.nw = o.normalZ.w; o.nh = o.normalZ.h;
                    continue;
                }
                // a normal map without its KTX2 is decoded below as before
            }
            if (o.ok) {
                if (jb.normal) pngNormal(*jb.normal, jb.bc[2], t, o);
                continue;
            }
            jb.why.clear();   // a KTX2 refused falls back to the PNG
            PngImage img;
            if (jb.file->empty()) {
                // no PNG: the BC7 decoded - a GL without BPTC, a grey alias
                if (!blockPixels(jb.bc[0], t, false, img.rgba, img.w, img.h, jb.why)) continue;
            } else {
                const auto bytes = packRead(*jb.file);
                if (bytes.empty()) { jb.why = "unreadable"; continue; }
                if (!decodePng(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), img, &jb.why))
                    continue;
            }
            if (!powerOfTwoFactor(img.w, t.width) || !powerOfTwoFactor(img.h, t.height)) {
                char b[96];
                std::snprintf(b, sizeof b, "%dx%d, not %dx%d times a power of two (1..16) each way",
                              img.w, img.h, t.width, t.height);
                jb.why = b;
                continue;
            }
            if (jb.grey) {
                std::uint8_t* p = img.rgba.data();
                for (std::size_t k = 0; k + 3 < img.rgba.size(); k += 4) {
                    const int y = lumaGrey(p[k], p[k + 1], p[k + 2]);
                    p[k] = p[k + 1] = p[k + 2] = static_cast<std::uint8_t>(y);
                    if (y == 0) p[k + 3] = 0;
                }
            }
            o.ok = true; o.w = img.w; o.h = img.h; o.rgba = std::move(img.rgba);
            if (jb.normal) pngNormal(*jb.normal, jb.bc[2], t, o);
        }
    });
    if (lightHook_)
        across([&](std::size_t from, std::size_t to) {
            for (std::size_t j = from; j < to; ++j) lightHook_(*ts[jobs[j].at], out[jobs[j].at]);
        });
    // the refusals, here, once per key
    lk.lock();
    for (const Job& jb : jobs) {
        if (out[jb.at].ok || !jb.file || !colour) continue;
        const std::string& named = !jb.file->empty() ? *jb.file : jb.bc[0] ? *jb.bc[0] : textureKeyHex(jb.key);
        std::fprintf(stderr, "texture pack: %s - %s; %s drawn as shipped\n", named.c_str(),
                     jb.why.c_str(), ts[jb.at]->name.c_str());
        refused_.insert(jb.key);
    }
    return out;
}

void TexturePack::aliasGrey(const Texture& colour, const Texture& grey) {
    if (!opened_ || !colour.hasPixels() || !grey.hasPixels()) return;
    const std::uint64_t c = textureKey(colour), g = textureKey(grey);
    std::lock_guard<std::mutex> lk(m_);
    if (files_.count(c) || lights_.count(c)) greyOf_[g] = c;
}

void TexturePack::prepare(std::span<const Texture* const> ts) {
    if (!opened_ || files_.empty()) return;
    // what a backend's pool asks: the colours, the normal maps as wanted -
    // on this thread alone
    auto rs = replacements(ts, true, true, false);
    std::vector<std::uint64_t> keys(ts.size(), 0);
    for (std::size_t i = 0; i < ts.size(); ++i)
        if (ts[i] && ts[i]->hasPixels() && imageBytes(rs[i]) > 0) keys[i] = textureKey(*ts[i]);
    std::lock_guard<std::mutex> lk(m_);
    for (std::size_t i = 0; i < ts.size(); ++i) {
        if (!keys[i]) continue;
        if (parked_.count(keys[i])) { ++parkedRefs_[keys[i]]; continue; }
        const std::size_t b = imageBytes(rs[i]);
        if (b > kParkedBytes) continue;
        // the oldest let go to make room
        while (parkedBytes_ + b > kParkedBytes && !parkedOrder_.empty()) {
            if (const auto old = parked_.find(parkedOrder_.front()); old != parked_.end()) {
                parkedBytes_ -= imageBytes(old->second);
                parked_.erase(old);
                parkedRefs_.erase(parkedOrder_.front());
            }
            parkedOrder_.pop_front();
        }
        parkedBytes_ += b;
        parked_.emplace(keys[i], std::move(rs[i]));
        parkedRefs_[keys[i]] = 1;
        parkedOrder_.push_back(keys[i]);
    }
}

void TexturePack::forget(std::span<const Texture* const> ts) {
    std::vector<std::uint64_t> keys;
    for (const Texture* t : ts)
        if (t && t->hasPixels()) keys.push_back(textureKey(*t));
    std::lock_guard<std::mutex> lk(m_);
    for (const std::uint64_t k : keys)
        if (const auto pk = parked_.find(k); pk != parked_.end()) {
            if (--parkedRefs_[k] > 0) continue;   // another set's too
            parkedBytes_ -= imageBytes(pk->second);
            parked_.erase(pk);
            parkedRefs_.erase(k);
        }
}

std::size_t TexturePack::parkedBytes() const {
    std::lock_guard<std::mutex> lk(m_);
    return parkedBytes_;
}

std::size_t TexturePack::parkedTaken() const {
    std::lock_guard<std::mutex> lk(m_);
    return parkedTaken_;
}

TexturePack& texturePack() {
    static TexturePack p;
    return p;
}

bool openTexturePack(const std::string& dir) {
    if (!texturePack().open(dir)) return false;
    std::printf("texture pack: %s - %zu replacement textures, %zu light maps. A REMASTER feature: it "
                "replaces the shipped pixels, drawn by Vulkan and GLES only (the software "
                "reference draws what the original drew)\n",
                dir.c_str(), texturePack().size(), texturePack().lightMaps());
    if (texturePack().excluded())
        std::printf("texture pack: %zu textures left as shipped by %s/exclude.txt (the curation)\n",
                    texturePack().excluded(), dir.c_str());
    event("texpack.open", {{"dir", dir}, {"entries", static_cast<long>(texturePack().size())},
                           {"lights", static_cast<long>(texturePack().lightMaps())},
                           {"excluded", static_cast<long>(texturePack().excluded())}});
    return true;
}

}  // namespace omk

#endif  // OMK_REMASTER

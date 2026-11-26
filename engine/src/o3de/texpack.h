// SPDX-License-Identifier: GPL-3.0-or-later
// THE TEXTURE PACK - the remaster's first feature (`todo/remaster.md` B4).
//
// NOT IN THE ORIGINAL, and not an `[Enhancements]` row either: it replaces
// authored pixels, so it lives under `[Remaster]` (`texturepack = DIR`, or
// `--texture-pack DIR`), OFF unless named. With no pack nothing here runs and
// every backend uploads exactly what it did before.
//
// A pack is a DIRECTORY of `<key>.png` files, `key` the sixteen hex digits of
// `textureKey` below - one file per texture, so a person can open any one of
// them in an image editor and fix it (the curation `todo/remaster.md` B2 and
// B5 need). `tools/omktexpack.py` makes them from the shipped data; nothing a
// pack holds is ever committed (the repo is public, the pixels are not ours).
//
// The KEY is the texture's own content - FNV-1a 64 over its width and height
// (little-endian 32 bits each), its width*height palette indices and its 768
// palette bytes - not its name: 182 names ship with different pixels in
// different files (CLAUDE.md §6). The same bytes in two models are one file.
//
// A replacement must be the original's size times a POWER OF TWO each way
// (1, 2, 4 ... 16), because the shipped UVs are in the ORIGINAL's texels: the
// GLES backend keeps dividing by the original size, the Vulkan shader is told
// the factor. Its ALPHA is the colour key, as `Texture::toRgbaKeyed` makes it
// (0 where the original is cut out, and the colour there 0 too), so a cutout
// mesh (flag 0x800) keeps its silhouette rule.
//
// Drawn by the Vulkan and GLES backends only. The software reference, the
// GL1 and the 3DS backends never consult it: the reference is what the
// original drew, and the two small consoles' textures are the original's size
// rule (`texscale.h`).
#pragma once

#include "formats/ktx2.h"
#include "formats/tex3dt.h"
#include "platform/sync.h"   // Mutex: none on the classic Mac, whose libstdc++ has no thread model

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace omk {

std::uint64_t textureKey(const Texture& t);
std::string textureKeyHex(std::uint64_t key);

// THE BLOCK FORMATS (`todo/remaster.md`, the pack's KTX2 files): beside each
// `<key>.png` the pack may hold `<key>.bc7.ktx2` (BC7, desktop GPUs) and
// `<key>.astc.ktx2` (ASTC, Android and the Quest) - and `_e` / `_n` the same,
// a light map BC4 or ASTC - each with its whole mip chain
// (`tools/tex_encode.cpp`, `omktexpack.py compress`). A backend says which
// it can sample (`setCodecs`); a texture that has one is then READ, not
// decoded - no PNG inflate on the frame it arrives - and uploaded as blocks,
// a quarter of RGBA's memory or less. The PNG, where there is one, stays the
// fallback and the source a person edits; a pack shipped for play may DROP
// its colour and normal PNGs (half its size): the KTX2 then indexes the key,
// and where pixels are wanted (a GL without BPTC, a grey alias) the BC is
// decoded on the CPU (`formats/blockdecode.h`). An ASTC-only pack keeps them.
enum PackCodec : unsigned { kCodecBc = 1, kCodecAstc = 2 };

// THE PACK'S ARCHIVE (`pack.omkp` in the pack's folder, `texpack_tool
// packarchive`): its files in ONE, for a console's card - the Switch's opens
// 2.3 ms a file in a folder of 2088 names, its reads 12 MB/s a small file,
// its listing 83 ms, and a copy one file at a time 3.3 MB/s (2026-11-26).
// Opened once and kept open; when present its index REPLACES the folder's
// names (`exclude.txt` is still read beside it). The layout: the magic (8
// bytes), the count (u32 LE), 0 (u32); then the index, an entry a file - its
// name NUL-padded to kPackArchiveNameBytes, its offset and its size (u64 LE
// each); then the files, each at a multiple of kPackArchiveAlign.
inline constexpr char kPackArchiveName[] = "pack.omkp";
inline constexpr char kPackArchiveMagic[9] = "OMKPACK1";
inline constexpr std::size_t kPackArchiveNameBytes = 40;
inline constexpr std::size_t kPackArchiveEntryBytes = kPackArchiveNameBytes + 16;
inline constexpr std::size_t kPackArchiveAlign = 512;
struct PackBlocks {
    std::uint32_t vkFormat = 0;   // `formats/ktx2.h`'s numbers
    int w = 0, h = 0;
    std::vector<std::uint8_t> bytes;                          // the whole file
    std::vector<std::pair<std::size_t, std::size_t>> levels;  // offset, length; level 0 first
    bool empty() const { return bytes.empty(); }
};

// One texture's replacement, decoded: RGBA8 of w x h; `ok` false when the
// pack has none for it, or its file was unusable. With the block formats on,
// `colourZ` / `emissiveZ` / `normalZ` hold the blocks INSTEAD of `rgba` /
// `emissive` / `normal` (w, h and ew, eh, nw, nh as before).
struct PackImage {
    bool ok = false;
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    // THE LIGHT MAP (`<key>_e.png`, `todo/remaster.md` step 2): how much of
    // each texel is light, 0..255, `ew x eh` (the original's size times a
    // power of two). Present with or without a colour replacement; empty
    // when the pack has none. Only the BLOOM reads it.
    int ew = 0, eh = 0;
    std::vector<std::uint8_t> emissive;
    // THE NORMAL MAP (`<key>_n.png`, `todo/remaster.md` B3, `o3de/normalmap.h`):
    // RGBA8 `nw x nh` - the normal in OpenGL's convention, the height in A
    // (kept at 1 or more: the shader reads alpha 0 as "none"). Decoded only
    // when the session asked (`setNormals`) and with the colour; only the
    // RELIEF reads it.
    int nw = 0, nh = 0;
    std::vector<std::uint8_t> normal;
    PackBlocks colourZ, emissiveZ, normalZ;
};

class TexturePack {
public:
    // Index DIR's `<16 hex>.png` files (nothing is decoded yet). False, and
    // said on stderr, when DIR is not a directory. An empty directory opens.
    bool open(const std::string& dir);
    bool active() const { return opened_; }
    // THE ORIGINAL/REMASTER SWITCH (`todo/graphics-menu.md` B): a pack open but
    // hidden is not drawn - the backends' uploads ask `shown`, and draw the
    // shipped texels - while the surface lights' light maps (`active`) are
    // still read, so a set loaded in the original mode has them for the switch
    void setShown(bool on) { shown_ = on; }
    bool shown() const { return opened_ && shown_; }
    std::size_t size() const { return files_.size(); }
    std::size_t lightMaps() const { return lights_.size(); }
    std::size_t normalMaps() const { return normals_.size(); }
    // the replacements `exclude.txt` left out (the curation, B2)
    std::size_t excluded() const { return excluded_; }
    // decode the NORMAL MAPS too (the relief's; each another PNG the size of
    // its colour) - off unless asked
    void setNormals(bool on) { wantNormals_ = on; }
    bool normals() const { return wantNormals_; }
    // decode the LIGHT MAPS with the colours (`colour` true): the bloom's -
    // on unless the session says no bloom will read them. The light maps
    // alone (`colour` false, the surface lights') are decoded either way.
    void setLights(bool on) { wantLights_ = on; }
    // the block formats the backend samples (`PackCodec` bits, 0 none - the
    // default: PNGs only)
    void setCodecs(unsigned c) { codecs_ = c; }
    unsigned codecs() const { return codecs_; }
    // colour textures with a KTX2 of `codec`
    std::size_t blockFiles(PackCodec codec) const { return ktx_[codec == kCodecAstc ? 1 : 0][0].size(); }
    const std::string& dir() const { return dir_; }
    // Each texture's replacement, decoded: RGBA8 of w x h, not `ok` when the
    // pack has none or the file is unusable (not a PNG this reads, not a
    // power-of-two multiple of the texture) - said once per key, and the
    // texture is then drawn as shipped. For a whole POOL at once (the single
    // `replacement` had no caller left - removed 2026-10-26), one result per
    // texture in order: the
    // files read and decoded on the shared thread pool (`platform/threads.h`),
    // the lookups and the refusals on the caller's thread. A null entry is
    // skipped. What a backend calls when a pool reaches it: decoding 31
    // replacements one after another took 634 ms on the street (M3).
    // `colour` false: the LIGHT MAPS alone (and the hook) - the colour PNGs, a
    // texture's 1024x1024, are not decoded. The surface lights' way
    // (`o3de/emitterlight.h`): a set's load had cost ~110 ms more for them.
    // `normals` false: none of the normal maps, whatever `setNormals` said -
    // a backend that draws no relief (GLES).
    // THREAD-SAFE against itself and `aliasGrey`: a set loads on a
    // BackgroundJob while the renderer's pool is decoded on the main thread.
    // `pooled` false: decoded on the CALLER's thread alone - `prepare`'s, from
    // a load's background job, which must not take the shared pool's workers
    // from the frame (the first pools of a street decoded 5x slower beside
    // six preload jobs, 2026-10-24)
    std::vector<PackImage> replacements(std::span<const Texture* const> ts, bool colour = true,
                                        bool normals = true, bool pooled = true) const;
    // THE GREYSCALE BANK (ops 150/151, `o3de/greybank.h`): the viewer greys a
    // pool's PALETTES, which makes each texture another key. Told which colour
    // texture a grey one came from, the pack answers the grey key with the
    // colour replacement taken to the engine's luma - and keyed (alpha 0)
    // wherever that luma is 0, as `toRgbaKeyed` keys the grey palette's
    // black - so a black-and-white cutscene draws the pack too.
    void aliasGrey(const Texture& colour, const Texture& grey);

    // THE PREPARED RESULTS (the remaster audit's performance item 1,
    // 2026-10-23): `prepare` decodes on the CALLER's thread - a set's load,
    // a `BackgroundJob` - what a backend's pool would decode on the frame
    // the set arrives, and PARKS each result that holds anything by its
    // texture's key; `replacements` (the colours asked, with the normal maps
    // as the parking had them) hands a parked one over, once, instead of
    // reading and decoding it again. `forget` drops what a released set
    // parked - COUNTED, a key as many times as it was prepared: two sets
    // sharing a texture park it once, and one set's release must not drop it
    // from the other (2026-10-26). At most `kParkedBytes` parked, the OLDEST
    // let go to make room (a set released while still preparing parks after
    // its `forget`: it ages out rather than holding the cap).
    static constexpr std::size_t kParkedBytes = std::size_t{256} << 20;
    void prepare(std::span<const Texture* const> ts);
    void forget(std::span<const Texture* const> ts);
    std::size_t parkedBytes() const;
    std::size_t parkedTaken() const;   // results handed over from `prepare` so far

    // THE LIGHT MAPS AMENDED after the decode (`o3de/emitterlight.h`
    // curateBloom, the remaster's curated light sources): called once a
    // texture, on the pool - it must be thread-safe - with the result so far.
    using LightHook = void (*)(const Texture&, PackImage&);
    // `raw`: the textures the hook paints into - their LIGHT MAPS decoded
    // from the PNGs whatever the codecs, the hook working on pixels (their
    // colours may still be blocks: the hook then reads the shipped texels)
    using RawFilter = bool (*)(const Texture&);
    void setLightHook(LightHook h, RawFilter raw = nullptr) { lightHook_ = h; rawFilter_ = raw; }

private:
    LightHook lightHook_ = nullptr;
    RawFilter rawFilter_ = nullptr;
    unsigned codecs_ = 0;
    // [bc, astc][colour, light, normal]: key -> its KTX2
    std::unordered_map<std::uint64_t, std::string> ktx_[2][3];
    bool wantNormals_ = false;
    bool wantLights_ = true;
    std::size_t excluded_ = 0;
    // guards `refused_`, `greyOf_` and `parked_`, what the callers' threads touch
    mutable Mutex m_;
    std::unordered_map<std::uint64_t, std::string> normals_;  // `<key>_n.png`
    std::string dir_;
    bool opened_ = false;
    bool shown_ = true;
    std::unordered_map<std::uint64_t, std::string> files_;
    std::unordered_map<std::uint64_t, std::string> lights_;   // `<key>_e.png`
    std::unordered_map<std::uint64_t, std::uint64_t> greyOf_;   // grey key -> colour key
    mutable std::unordered_set<std::uint64_t> refused_;
    // `prepare`'s results (under `m_`), and their bytes
    mutable std::unordered_map<std::uint64_t, PackImage> parked_;
    mutable std::unordered_map<std::uint64_t, int> parkedRefs_;   // `prepare`s a parked key, less `forget`s
    mutable std::size_t parkedBytes_ = 0;
    mutable std::size_t parkedTaken_ = 0;
    std::deque<std::uint64_t> parkedOrder_;   // the keys as parked, oldest first (some since taken)
};

// The process's one pack, empty unless the session opened it.
TexturePack& texturePack();

// The viewer's way in: open DIR as the process's pack and SAY so (a prose
// line and `@evt texpack.open dir= entries=`). False, said, when it is not a
// directory.
bool openTexturePack(const std::string& dir);

}  // namespace omk

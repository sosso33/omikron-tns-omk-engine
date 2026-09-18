// SPDX-License-Identifier: GPL-3.0-or-later
// The intro films on SceAvPlayer - see `avmovie.h`.
#include "avmovie.h"

#include <psp2/avplayer.h>
#include <psp2/gxm.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/sysmodule.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <map>
#include <set>

#include <vitaGL.h>

namespace omk::vita {

namespace {

// ---- the player's memory -------------------------------------------------
//
// Generic data from the newlib heap. VIDEO FRAMES from their own CDRAM blocks,
// mapped for the GPU: the decoder writes them through the GPU's view, which
// is why a plain heap block will not do. A CDRAM block is 256 KiB-aligned.
void* memAlloc(void*, uint32_t alignment, uint32_t size) {
    return memalign(alignment < 16 ? 16 : alignment, size);
}
void memFree(void*, void* p) { std::free(p); }

// THE DECODER'S FRAME BUFFERS. First from vitaGL's own pools, which are
// already GPU-mapped: vitaGL takes most of CDRAM at `vglInit`, so a fresh
// CDRAM block - the only path until 2026-09-18 - can fail on a console while
// the emulator, with room to spare, grants it. The console played all three
// films as "0 frames shown, sound at 0 Hz", the player starting and stopping
// with nothing decoded, which is what a refused frame buffer does. Every
// allocation is logged, so the next log says which path served it.
std::set<void*> g_blockFrames;   // the ones from sceKernelAllocMemBlock
std::map<void*, void*> g_poolFrames;   // aligned -> what vglAlloc returned
void* frameAlloc(void*, uint32_t alignment, uint32_t size) {
    // GRAPHICS memory, not just any: `vglMemalign` served the 19:43 build from
    // vitaGL's RAM pool and the decoder then produced nothing. Over-allocate
    // from the CDRAM pool (then the physically contiguous one) and align by
    // hand; the original pointer is kept for the free.
    const uint32_t al = alignment < 256 ? 256 : alignment;
    const vglMemType pools[2] = {VGL_MEM_VRAM, VGL_MEM_PHYCONT};
    for (int k = 0; k < 2; ++k) {
        void* raw = vglAlloc(size + al, pools[k]);
        if (!raw) continue;
        const uintptr_t a = (reinterpret_cast<uintptr_t>(raw) + al - 1) & ~static_cast<uintptr_t>(al - 1);
        void* p = reinterpret_cast<void*>(a);
        g_poolFrames[p] = raw;
        std::printf("film: frame buffer %u bytes (align %u) from vitaGL's %s pool\n",
                    static_cast<unsigned>(size), static_cast<unsigned>(alignment),
                    k == 0 ? "CDRAM" : "PHYCONT");
        return p;
    }
    const uint32_t a = 256 * 1024;
    const uint32_t sz = (size + a - 1) & ~(a - 1);
    const SceUID block = sceKernelAllocMemBlock("omk film frame",
                                                SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                                sz, nullptr);
    if (block < 0) {
        std::printf("film: frame buffer %u bytes REFUSED - vitaGL's pool empty, CDRAM block 0x%08X\n",
                    static_cast<unsigned>(size), static_cast<unsigned>(block));
        return nullptr;
    }
    void* base = nullptr;
    sceKernelGetMemBlockBase(block, &base);
    sceGxmMapMemory(base, sz, static_cast<SceGxmMemoryAttribFlags>(
                                  SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE));
    g_blockFrames.insert(base);
    std::printf("film: frame buffer %u bytes from a CDRAM block\n", static_cast<unsigned>(size));
    return base;
}
void frameFree(void*, void* p) {
    if (!p) return;
    if (const auto it = g_poolFrames.find(p); it != g_poolFrames.end()) {
        vglFree(it->second);
        g_poolFrames.erase(it);
        return;
    }
    if (g_blockFrames.erase(p) == 0) return;
    const SceUID block = sceKernelFindMemBlockByAddr(p, 0);
    sceGxmUnmapMemory(p);
    if (block >= 0) sceKernelFreeMemBlock(block);
}

bool moduleLoaded = false;

// every player event, logged: a film that "ends" with no frame says nothing else
void onEvent(void*, int32_t id, int32_t source, void*) {
    std::printf("film: player event 0x%02X (source %d)\n", static_cast<unsigned>(id), static_cast<int>(source));
}

inline int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

}  // namespace

std::string avFind(const std::string& stem, const std::string& dataRoot,
                   std::string& report) {
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string want = lower(stem) + ".mp4";
    const std::string dirs[] = {"ux0:data/omk/movies", dataRoot + "/FLIS", "app0:movies"};
    report.clear();
    for (const auto& dir : dirs) {
        const SceUID d = sceIoDopen(dir.c_str());
        if (d < 0) {
            char e[48];
            std::snprintf(e, sizeof e, " cannot open (0x%08X);", static_cast<unsigned>(d));
            report += "\n    " + dir + ":" + e;
            continue;
        }
        std::string seen;
        std::string found;
        SceIoDirent ent;
        std::memset(&ent, 0, sizeof ent);
        while (sceIoDread(d, &ent) > 0) {
            const std::string n = ent.d_name;
            if (found.empty() && lower(n) == want) found = dir + "/" + n;
            if (seen.size() < 200) seen += " '" + n + "'";
            std::memset(&ent, 0, sizeof ent);
        }
        sceIoDclose(d);
        if (!found.empty()) return found;
        report += "\n    " + dir + ": holds" + (seen.empty() ? std::string(" nothing") : seen);
    }
    return {};
}

struct AvFilm {
    SceAvPlayerHandle handle = 0;
};

AvFilm* avOpen(const std::string& path) {
    SceIoStat st;
    if (sceIoGetstat(path.c_str(), &st) < 0) return nullptr;
    // the size, SAID: a copy damaged in transfer (FileZilla's ASCII mode grew
    // IAM\AREA by 10 bytes) is a film the player stops on at once
    std::printf("film: %s is %lld bytes\n", path.c_str(), static_cast<long long>(st.st_size));
    if (!moduleLoaded) {
        if (sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER) < 0) {
            std::printf("film: SceAvPlayer module did not load\n");
            return nullptr;
        }
        moduleLoaded = true;
    }
    SceAvPlayerInitData init;
    std::memset(&init, 0, sizeof init);
    init.memoryReplacement.allocate = memAlloc;
    init.memoryReplacement.deallocate = memFree;
    init.memoryReplacement.allocateTexture = frameAlloc;
    init.memoryReplacement.deallocateTexture = frameFree;
    init.eventReplacement.eventCallback = onEvent;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart = SCE_TRUE;
    const SceAvPlayerHandle h = sceAvPlayerInit(&init);
    if (!h) {
        std::printf("film: sceAvPlayerInit failed\n");
        return nullptr;
    }
    if (sceAvPlayerAddSource(h, path.c_str()) < 0) {
        std::printf("film: %s did not open in SceAvPlayer\n", path.c_str());
        sceAvPlayerClose(h);
        return nullptr;
    }
    auto* f = new AvFilm;
    f->handle = h;
    return f;
}

bool avActive(AvFilm* f) {
    return f && sceAvPlayerIsActive(f->handle);
}

bool avVideo(AvFilm* f, Surface& out) {
    SceAvPlayerFrameInfo fr;
    std::memset(&fr, 0, sizeof fr);
    if (!f || !sceAvPlayerGetVideoData(f->handle, &fr) || !fr.pData) return false;
    const int w = static_cast<int>(fr.details.video.width);
    const int h = static_cast<int>(fr.details.video.height);
    if (w <= 0 || h <= 0) return false;
    if (out.w != w || out.h != h) out = Surface(w, h, 0);
    // TWO PLANES: luma, then chroma interleaved at half resolution in both
    // axes, U FIRST in each pair (NV12). Settled by LOOKING, 2026-09-18: read
    // V-first, as ports commonly treat it, GAME.MPG's advert came out with its
    // red cap blue, its orange background cyan and its yellow can green - red
    // and blue exchanged, in Vita3K's SceAvPlayer. BT.601, limited range, in
    // integers.
    const std::uint8_t* Y = fr.pData;
    const std::uint8_t* C = fr.pData + static_cast<std::size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* yl = Y + static_cast<std::size_t>(y) * w;
        const std::uint8_t* cl = C + static_cast<std::size_t>(y / 2) * w;
        std::uint16_t* o = out.px.data() + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const int c = 298 * (static_cast<int>(yl[x]) - 16);
            const int u = static_cast<int>(cl[(x & ~1)]) - 128;
            const int v = static_cast<int>(cl[(x & ~1) + 1]) - 128;
            const int r = clamp8((c + 409 * v + 128) >> 8);
            const int g = clamp8((c - 100 * u - 208 * v + 128) >> 8);
            const int b = clamp8((c + 516 * u + 128) >> 8);
            o[x] = rgb565(r, g, b);
        }
    }
    return true;
}

bool avAudio(AvFilm* f, std::vector<float>& pcm, int& rate) {
    bool any = false;
    SceAvPlayerFrameInfo fr;
    for (int guard = 0; guard < 16; ++guard) {
        std::memset(&fr, 0, sizeof fr);
        if (!f || !sceAvPlayerGetAudioData(f->handle, &fr) || !fr.pData) break;
        const int ch = fr.details.audio.channelCount ? fr.details.audio.channelCount : 2;
        rate = static_cast<int>(fr.details.audio.sampleRate);
        const auto* s = reinterpret_cast<const std::int16_t*>(fr.pData);
        const std::size_t n = fr.details.audio.size / sizeof(std::int16_t);
        // interleaved stereo float for the audio queue; a mono stream is
        // doubled rather than played at twice the speed
        if (ch == 1) {
            for (std::size_t i = 0; i < n; ++i) {
                const float v = static_cast<float>(s[i]) / 32768.0f;
                pcm.push_back(v);
                pcm.push_back(v);
            }
        } else {
            for (std::size_t i = 0; i + 1 < n; i += static_cast<std::size_t>(ch)) {
                pcm.push_back(static_cast<float>(s[i]) / 32768.0f);
                pcm.push_back(static_cast<float>(s[i + 1]) / 32768.0f);
            }
        }
        any = true;
    }
    return any;
}

void avClose(AvFilm* f) {
    if (!f) return;
    sceAvPlayerStop(f->handle);
    sceAvPlayerClose(f->handle);
    delete f;
}

}  // namespace omk::vita

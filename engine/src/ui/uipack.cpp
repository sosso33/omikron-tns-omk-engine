// SPDX-License-Identifier: GPL-3.0-or-later
// The interface pack - see uipack.h. Not a port of anything in the original.
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "ui/uipack.h"

#include "platform/datafs.h"
#include "platform/events.h"
#include "platform/hostfs.h"
#include "platform/packkey.h"
#include "platform/pngread.h"
#include "platform/sync.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

namespace omk {

std::uint64_t uiKey(const Surface& s) {
    Fnv1a f;
    for (int v : {s.w, s.h}) f.mixLe(static_cast<std::uint32_t>(v), 4);
    for (const std::uint16_t p : s.px) f.mixLe(p, 2);
    return f.h;
}

namespace {

// eight fixed texels: a buffer the allocator handed to ANOTHER bitmap of the
// same size since `attach` would have to match all eight to be taken for it
struct Noted {
    std::uint64_t key = 0;
    int w = 0, h = 0;
    std::uint16_t sample[8] = {};
};
void sampleOf(const Surface& s, std::uint16_t out[8]) {
    const std::size_t n = s.px.size();
    for (std::size_t k = 0; k < 8; ++k) out[k] = n ? s.px[(n - 1) * k / 7] : 0;
}

}  // namespace

struct UiPack::Impl {
    std::string dir;
    std::set<std::uint64_t> keys;                                   // the pack's files
    std::unordered_map<const std::uint16_t*, Noted> noted;          // by pixel buffer
    std::map<std::uint64_t, std::shared_ptr<const Surface>> hd;     // decoded
    std::list<std::uint64_t> order;                                 // least recent first
    Stats st;
    Mutex m;

    std::shared_ptr<const Surface> decode(std::uint64_t key, int w, int h) {
        const auto bytes = DataFs::readPath(dir + "/" + keyHex(key) + ".png");
        PngImage img;
        if (bytes.empty() || !decodePng(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), img)) {
            ++st.refused;
            return nullptr;
        }
        const int k = img.w / w;
        if (k < 2 || (k & (k - 1)) || img.w != w * k || img.h != h * k) {
            ++st.refused;   // not the original's size times a power of two
            return nullptr;
        }
        auto s = std::make_shared<Surface>(img.w, img.h);
        for (std::size_t i = 0; i < s->px.size(); ++i) {
            const std::uint8_t* p = &img.rgba[4 * i];
            if (p[3] < 128) { s->px[i] = 0; continue; }
            const std::uint16_t v = rgb565(p[0], p[1], p[2]);
            s->px[i] = v ? v : 1;   // never the key unless the original's key
        }
        ++st.decoded;
        return s;
    }
};

bool UiPack::open(const std::string& dir) {
    active_ = false;
    impl_ = std::make_shared<Impl>();
    impl_->dir = dir;
    if (!hostfs::isDirectory(dir)) return false;
    for (const auto& name : hostfs::listNames(dir)) {
        if (name.size() != 20 || name.substr(16) != ".png") continue;
        char* end = nullptr;
        // the hex KEPT while `end` points into it: a temporary's buffer is
        // gone at the semicolon (AddressSanitizer, 2026-11-26)
        const std::string hex = name.substr(0, 16);
        const unsigned long long k = std::strtoull(hex.c_str(), &end, 16);
        if (end && *end == '\0') impl_->keys.insert(k);
    }
    active_ = !impl_->keys.empty();
    return active_;
}

std::size_t UiPack::files() const { return impl_ ? impl_->keys.size() : 0; }

UiPack::Stats UiPack::stats() const {
    if (!impl_) return {};
    ScopedLock l(impl_->m);
    return impl_->st;
}

void UiPack::attach(const Surface& s) {
    if (!active_ || !s.valid()) return;
    const std::uint64_t key = uiKey(s);
    ScopedLock l(impl_->m);
    if (!impl_->keys.count(key)) return;
    Noted n;
    n.key = key; n.w = s.w; n.h = s.h;
    sampleOf(s, n.sample);
    impl_->noted[s.px.data()] = n;
    ++impl_->st.attached;
}

std::shared_ptr<const Surface> UiPack::find(const Surface& src) {
    if (!active_ || !shown_ || !src.valid()) return nullptr;
    ScopedLock l(impl_->m);
    const auto it = impl_->noted.find(src.px.data());
    if (it == impl_->noted.end()) return nullptr;
    const Noted& n = it->second;
    std::uint16_t now[8];
    sampleOf(src, now);
    if (n.w != src.w || n.h != src.h || !std::equal(now, now + 8, n.sample)) {
        impl_->noted.erase(it);   // another bitmap in that buffer now
        return nullptr;
    }
    auto& order = impl_->order;
    auto hit = impl_->hd.find(n.key);
    if (hit == impl_->hd.end()) {
        auto s = impl_->decode(n.key, n.w, n.h);
        if (!s) { impl_->noted.erase(it); return nullptr; }
        hit = impl_->hd.emplace(n.key, std::move(s)).first;
        if (impl_->hd.size() > kCached) {   // the least recently drawn goes
            const std::uint64_t old = order.front();
            order.pop_front();
            impl_->hd.erase(old);
        }
    } else {
        order.remove(n.key);
    }
    order.push_back(n.key);
    ++impl_->st.drawn;
    return hit->second;
}

UiPack& uiPack() {
    static UiPack p;
    return p;
}

}  // namespace omk

#endif  // OMK_REMASTER

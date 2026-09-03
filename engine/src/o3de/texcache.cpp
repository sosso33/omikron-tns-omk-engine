// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/texcache.h"

#include <algorithm>
#include <cstring>

namespace omk {
namespace {
const std::string kNone;
}

BindResult TextureCache::bind(const std::string& model, const Mesh3doHeader& h,
                              std::span<const std::byte> modelFile) {
    BindResult r;
    for (int i = 0; i < h.materials; ++i) {
        const auto m = readMaterial(modelFile, h, i);
        if (!m) break;
        const std::string key(m->texture, std::min(std::strlen(m->texture),
                                                   kCacheKeyChars));
        BindResult::Entry e;
        e.texture = key;
        int found = -1;
        for (int s = 0; s < kTextureSlots; ++s)
            if (slots_[s].used && slots_[s].name == key) { found = s; break; }
        if (found >= 0) {
            // the hit: the file's own pixels are fseek'd past and never read
            e.slot = found;
            e.hit  = true;
            e.residentFrom = slots_[found].firstOwner;
            if (std::find(slots_[found].refs.begin(), slots_[found].refs.end(),
                          model) == slots_[found].refs.end())
                slots_[found].refs.push_back(model);
            ++r.hits;
        } else {
            int free = -1;
            for (int s = 0; s < kTextureSlots; ++s)
                if (!slots_[s].used) { free = s; break; }
            if (free < 0) { ++r.exhausted; r.materials.push_back(e); continue; }
            slots_[free].used = true;
            slots_[free].name = key;
            slots_[free].firstOwner = model;
            slots_[free].refs.assign(1, model);
            e.slot = free;
            ++r.loads;
        }
        r.materials.push_back(e);
    }
    return r;
}

void TextureCache::release(const std::string& model) {
    for (auto& s : slots_) {
        if (!s.used) continue;
        s.refs.erase(std::remove(s.refs.begin(), s.refs.end(), model),
                     s.refs.end());
        if (s.refs.empty()) { s.used = false; s.name.clear(); s.firstOwner.clear(); }
    }
}

int TextureCache::used() const {
    int n = 0;
    for (const auto& s : slots_) n += s.used ? 1 : 0;
    return n;
}

const std::string& TextureCache::owner(int slot) const {
    if (slot < 0 || slot >= kTextureSlots) return kNone;
    return slots_[static_cast<std::size_t>(slot)].firstOwner;
}

}  // namespace omk

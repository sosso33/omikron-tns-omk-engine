// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/camedit.h"

#include <cstring>
#include <set>

namespace omk {
namespace {
std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}
std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(d[o]) | static_cast<std::uint16_t>(d[o + 1]) << 8);
}
float f32(std::span<const std::byte> d, std::size_t o) {
    const auto b = u32(d, o); float f; std::memcpy(&f, &b, sizeof f); return f;
}
std::string str(std::span<const std::byte> d, std::size_t o, std::size_t cap) {
    std::string s;
    for (std::size_t k = 0; k < cap && o + k < d.size(); ++k) {
        const auto c = static_cast<char>(d[o + k]);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}
}  // namespace

CamFile readCamFile(std::span<const std::byte> d) {
    CamFile f;
    f.size = d.size();
    if (d.size() < 36 || u32(d, 0) != 3u) return f;   // the loader's own test

    const auto nCam = u32(d, 4), nKey = u32(d, 8);
    const auto nTrk = u32(d, 12), nEdt = u32(d, 16);
    std::size_t o = 36;

    for (std::uint32_t i = 0; i < nCam; ++i, o += 52) {
        if (o + 52 > d.size()) return f;
        CamCamera c;
        c.id = u32(d, o);
        c.name = str(d, o + 4, 12);
        for (int k = 0; k < 3; ++k) c.pos[k]    = f32(d, o + 16u + 4u * static_cast<std::size_t>(k));
        for (int k = 0; k < 3; ++k) c.target[k] = f32(d, o + 28u + 4u * static_cast<std::size_t>(k));
        c.roll = f32(d, o + 40);
        c.fov  = f32(d, o + 44);
        f.cameras.push_back(std::move(c));
    }
    for (std::uint32_t i = 0; i < nKey; ++i, o += 28) {
        if (o + 28 > d.size()) return f;
        f.keys.push_back({u32(d, o), u32(d, o + 4), f32(d, o + 8), u32(d, o + 20)});
    }
    std::vector<std::uint16_t> trackKeyCounts;
    for (std::uint32_t i = 0; i < nTrk; ++i, o += 24) {
        if (o + 24 > d.size()) return f;
        CamTrack t;
        t.id = u32(d, o);
        t.name = str(d, o + 4, 10);
        trackKeyCounts.push_back(u16(d, o + 14));
        f.tracks.push_back(std::move(t));
    }
    // the key-id lists, concatenated in track order
    for (std::size_t i = 0; i < f.tracks.size(); ++i)
        for (std::uint16_t k = 0; k < trackKeyCounts[i]; ++k, o += 4)
            f.tracks[i].keys.push_back(u32(d, o));

    std::vector<std::uint16_t> edtTrackCounts;
    for (std::uint32_t i = 0; i < nEdt; ++i, o += 32) {
        if (o + 32 > d.size()) return f;
        CamEditing e;
        e.id = static_cast<std::uint8_t>(d[o]);
        e.name = str(d, o + 1, 11);
        edtTrackCounts.push_back(u16(d, o + 12));
        e.duration = u32(d, o + 24);
        e.objectHandle = u16(d, o + 28);
        f.editings.push_back(std::move(e));
    }
    for (std::size_t i = 0; i < f.editings.size(); ++i)
        for (std::uint16_t k = 0; k < edtTrackCounts[i]; ++k, o += 4)
            f.editings[i].tracks.push_back(u32(d, o));

    f.end = o;
    f.exact = (o == d.size());

    // The loader resolves every id to a pointer and returns 0 on a miss, so a
    // dangling reference would stop the scene loading at all - which is what
    // makes counting them a test rather than a statistic.
    std::set<std::uint32_t> camIds, keyIds, trkIds;
    for (const auto& c : f.cameras) camIds.insert(c.id);
    for (const auto& k : f.keys)    keyIds.insert(k.id);
    for (const auto& t : f.tracks)  trkIds.insert(t.id);
    for (const auto& k : f.keys)     if (!camIds.count(k.camera)) ++f.unresolved;
    for (const auto& t : f.tracks)
        for (auto k : t.keys)        if (!keyIds.count(k)) ++f.unresolved;
    for (const auto& e : f.editings)
        for (auto t : e.tracks)      if (!trkIds.count(t)) ++f.unresolved;

    f.valid = true;
    return f;
}

const CamEditing* editingForObject(const CamFile& f, int objectId) {
    if (objectId <= 0) return nullptr;      // 0 is the shipped "unlinked"
    for (const auto& e : f.editings)
        if (static_cast<int>(e.objectHandle) == objectId) return &e;
    return nullptr;
}

const CamEditing* editingById(const CamFile& f, int id) {
    for (const auto& e : f.editings)
        if (static_cast<int>(e.id) == id) return &e;
    return nullptr;
}

bool sampleCamEditing(const CamFile& f, const CamEditing& e, float t,
                      CamSample& out) {
    // `if ((double)duration > a3)` - else the function returns 1 having
    // written nothing.
    if (t < 0.0f || t >= static_cast<float>(e.duration)) return false;

    // The loader resolved every id to a pointer; here they are looked up. An
    // unresolved one would have stopped the scene loading (`unresolved` is
    // 0 across the corpus), so a miss here is a caller's bug, not data.
    const CamTrack* trk = nullptr;
    const CamKey *k0 = nullptr, *k1 = nullptr;
    float base = 0.0f;
    int   trackIdx = -1;
    for (std::size_t ti = 0; ti < e.tracks.size() && !k0; ++ti) {
        const CamTrack* tr = nullptr;
        for (const auto& c : f.tracks) if (c.id == e.tracks[ti]) { tr = &c; break; }
        if (!tr || tr->keys.empty()) continue;
        std::vector<const CamKey*> ks;
        ks.reserve(tr->keys.size());
        for (auto kid : tr->keys) {
            const CamKey* k = nullptr;
            for (const auto& c : f.keys) if (c.id == kid) { k = &c; break; }
            if (!k) return false;
            ks.push_back(k);
        }
        // `v12 = f32(v11[v10 - 1], 8)` - the track's LAST key frame is what
        // the base advances by, whether or not `t` fell inside.
        const float last = ks.back()->frame;
        for (std::size_t i = 0; i + 1 < ks.size(); ++i) {
            if (ks[i]->frame + base <= t && t < ks[i + 1]->frame + base) {
                k0 = ks[i]; k1 = ks[i + 1]; trk = tr;
                trackIdx = static_cast<int>(ti);
                break;
            }
        }
        if (!k0) base += last;
    }
    if (!k0 || !trk) return false;          // "key not found for frame %2.2f"

    const CamCamera *c0 = nullptr, *c1 = nullptr;
    for (const auto& c : f.cameras) {
        if (c.id == k0->camera) c0 = &c;
        if (c.id == k1->camera) c1 = &c;
    }
    if (!c0 || !c1) return false;

    // sub_49EC10: slope = (c1 - c0) / (frame1 - frame0), per field; then
    // value = slope * (t - base - frame0) + c0.
    const float span = k1->frame - k0->frame;
    const float u = t - base - k0->frame;
    const auto lerp = [&](float a, float b) {
        return span != 0.0f ? (b - a) / span * u + a : a;
    };
    for (int k = 0; k < 3; ++k) {
        out.eye[k] = lerp(c0->pos[k], c1->pos[k]);
        out.at[k]  = lerp(c0->target[k], c1->target[k]);
    }
    out.roll   = lerp(c0->roll, c1->roll);
    out.fov    = lerp(c0->fov, c1->fov);
    out.track  = trackIdx;
    out.camera = c0->id;
    return true;
}

}  // namespace omk

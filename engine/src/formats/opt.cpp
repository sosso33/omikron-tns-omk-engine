// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/opt.h"

#include <cstring>

namespace omk {
namespace {

template <typename T>
T at(std::span<const std::byte> d, std::size_t o) {
    T v{};
    if (o + sizeof(T) <= d.size()) std::memcpy(&v, d.data() + o, sizeof(T));
    return v;
}

}  // namespace

void OptTrack::laneEnd(int lane, float out[3]) const {
    const auto& L = lanes[static_cast<std::size_t>(lane)];
    out[0] = L.origin[0]; out[1] = L.origin[1]; out[2] = L.origin[2];
    for (int k = 0; k < L.keyCount; ++k) {
        const auto& K = keys[static_cast<std::size_t>(L.firstKey + k)];
        out[0] += K.delta[0]; out[1] += K.delta[1]; out[2] += K.delta[2];
    }
}

OptTrack loadOpt(std::span<const std::byte> d) {
    OptTrack t;
    auto fail = [&](const char* why) { t.valid = false; t.error = why; return t; };
    if (d.size() < 80) return fail("shorter than the header");
    if (std::memcmp(d.data(), "V1.0", 4) != 0) return fail("magic is not V1.0");
    std::uint32_t h[20];
    for (int i = 0; i < 20; ++i) h[i] = at<std::uint32_t>(d, static_cast<std::size_t>(4 * i));
    t.pedFirst = h[1]; t.pedEnd = h[2]; t.pedSpacing = h[3]; t.vehSpacing = h[4];
    t.laneCount = h[5]; t.stamp = h[19];
    // the seven blocks, each landing on the next and the last on the size
    struct Block { std::uint32_t off, count, size; };
    const Block B[7] = {{h[6], h[5], 24}, {h[8], h[7], 20}, {h[10], h[9], 20}, {h[12], h[11], 12},
                        {h[14], h[13], 16}, {h[16], h[15], 4}, {h[18], h[17], 2}};
    std::uint32_t o = 76;
    for (const auto& b : B) {
        if (b.off != o) return fail("a block does not start where the previous ends");
        o = b.off + b.size * b.count;
        if (o > d.size()) return fail("a block runs past the file");
    }
    if (o != d.size()) return fail("the last block does not end on the file size");
    if (!(t.pedFirst <= t.pedEnd && t.pedEnd <= t.laneCount)) return fail("lane bounds out of order");

    t.lanes.resize(B[0].count);
    for (std::uint32_t i = 0; i < B[0].count; ++i) {
        const std::size_t p = B[0].off + 24 * i;
        auto& L = t.lanes[i];
        for (int k = 0; k < 3; ++k) L.origin[k] = at<float>(d, p + 4 * static_cast<std::size_t>(k));
        if (at<std::int32_t>(d, p + 12) != 0) return fail("a lane's runtime list head is not zero");
        L.firstKey = at<std::int16_t>(d, p + 16);
        L.firstRoute = at<std::int16_t>(d, p + 18);
        L.routeCount = at<std::int8_t>(d, p + 20);
        L.keyCount = at<std::int8_t>(d, p + 21);
    }
    t.keys.resize(B[1].count);
    for (std::uint32_t i = 0; i < B[1].count; ++i) {
        const std::size_t p = B[1].off + 20 * i;
        auto& K = t.keys[i];
        if (at<std::int32_t>(d, p) != 0) return fail("a key's runtime list head is not zero");
        for (int k = 0; k < 3; ++k) K.delta[k] = at<float>(d, p + 4 + 4 * static_cast<std::size_t>(k));
        K.action = at<std::int16_t>(d, p + 16);
    }
    t.actions.resize(B[2].count);
    for (std::uint32_t i = 0; i < B[2].count; ++i) {
        const std::size_t p = B[2].off + 20 * i;
        auto& A = t.actions[i];
        for (int k = 0; k < 3; ++k) A.point[k] = at<float>(d, p + 4 * static_cast<std::size_t>(k));
        A.facing = at<float>(d, p + 12);
        A.clip = at<std::int16_t>(d, p + 16);
        A.count = at<std::int8_t>(d, p + 18);
        A.one = at<std::int8_t>(d, p + 19);
    }
    t.routes.resize(B[3].count);
    for (std::uint32_t i = 0; i < B[3].count; ++i) {
        const std::size_t p = B[3].off + 12 * i;
        auto& R = t.routes[i];
        if (at<std::int32_t>(d, p) != 0) return fail("a route's runtime list head is not zero");
        R.dest = at<std::int16_t>(d, p + 4);
        R.firstStep = at<std::int16_t>(d, p + 6);
        R.group = at<std::int16_t>(d, p + 8);
        R.stepCount = at<std::int8_t>(d, p + 10);
    }
    t.steps.resize(B[4].count);
    for (std::uint32_t i = 0; i < B[4].count; ++i) {
        const std::size_t p = B[4].off + 16 * i;
        auto& S = t.steps[i];
        for (int k = 0; k < 3; ++k) S.delta[k] = at<float>(d, p + 4 * static_cast<std::size_t>(k));
        S.group = at<std::int16_t>(d, p + 12);
    }
    t.groups.resize(B[5].count);
    for (std::uint32_t i = 0; i < B[5].count; ++i) {
        const std::size_t p = B[5].off + 4 * i;
        auto& G = t.groups[i];
        G.first = at<std::int16_t>(d, p);
        G.count = at<std::int8_t>(d, p + 2);
        if (at<std::uint8_t>(d, p + 3) != 0) return fail("a group's runtime busy byte is not zero");
    }
    t.lists.resize(B[6].count);
    for (std::uint32_t i = 0; i < B[6].count; ++i)
        t.lists[i] = at<std::int16_t>(d, B[6].off + 2 * i);

    // the references
    const int nK = static_cast<int>(t.keys.size()), nA = static_cast<int>(t.actions.size());
    const int nR = static_cast<int>(t.routes.size()), nS = static_cast<int>(t.steps.size());
    const int nG = static_cast<int>(t.groups.size()), nI = static_cast<int>(t.lists.size());
    for (std::size_t li = 0; li < t.lanes.size(); ++li) {
        const auto& L = t.lanes[li];
        if (L.firstKey < 0 || L.firstKey + L.keyCount > nK) return fail("a lane's keys are out of range");
        const int nr = L.routeCount > 0 ? L.routeCount : 1;
        if (L.firstRoute < 0 || L.firstRoute + nr > nR) return fail("a lane's routes are out of range");
        const bool ped = t.isPedestrianLane(static_cast<int>(li));
        for (int r = 0; r < nr; ++r) {
            const auto& R = t.routes[static_cast<std::size_t>(L.firstRoute + r)];
            if (t.isPedestrianLane(R.dest) != ped) return fail("a route crosses the pedestrian/vehicle classes");
        }
    }
    for (const auto& K : t.keys)
        if (K.action < -1 || K.action >= nA) return fail("a key's action is out of range");
    for (const auto& R : t.routes) {
        if (R.dest < 0 || R.dest >= static_cast<int>(t.laneCount)) return fail("a route's destination is out of range");
        if (R.firstStep < 0 || R.firstStep + R.stepCount > nS) return fail("a route's steps are out of range");
        if (R.group < -1 || R.group >= nG) return fail("a route's group is out of range");
    }
    for (const auto& S : t.steps)
        if (S.group < -1 || S.group >= nG) return fail("a step's group is out of range");
    for (const auto& G : t.groups)
        if (G.first < 0 || G.first + G.count > nI) return fail("a group's list is out of range");
    for (const auto i : t.lists)
        if (i < 0 || i >= nG) return fail("a list entry is out of range");
    t.valid = true;
    return t;
}

}  // namespace omk

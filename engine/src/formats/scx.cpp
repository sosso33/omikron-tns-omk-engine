// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/scx.h"

#include <algorithm>
#include <cstring>

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}

std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int32_t>(u32(d, o));
}

float f32(std::span<const std::byte> d, std::size_t o) {
    const auto b = u32(d, o);
    float f; std::memcpy(&f, &b, sizeof f); return f;
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

ScxScene readScx(std::span<const std::byte> d) {
    ScxScene s;
    s.size = d.size();
    if (d.size() < 16) return s;
    if (u32(d, 0) != 0x00DEAD00u || u32(d, 4) != 5u) return s;
    s.blockSize = u32(d, 12);
    if (16 + s.blockSize > d.size()) return s;
    const auto b = d.subspan(16, s.blockSize);
    s.streamed = d.size() - 16 - s.blockSize;

    // ---- chunk 2: the script objects, and where it ends
    const auto n = u32(b, 4);
    const std::size_t base = 8;
    const std::size_t after = base + 100u * n;
    const auto pcount = u32(b, after);
    const std::size_t pbase = after + 4;
    std::size_t o = pbase + 4u * pcount;

    for (std::uint32_t i = 0; i < n; ++i) {
        const auto r = base + 100u * static_cast<std::size_t>(i);
        ScxObject ob;
        ob.index = static_cast<int>(i);
        if (o < b.size() && static_cast<std::uint8_t>(b[o])) {
            ob.hasLink = true;
            ob.link = str(b, o + 1, 21);
            o += 22;
        } else {
            o += 1;
        }
        ob.name   = str(b, r + 4, 20);
        ob.handle = u32(b, r + 24);
        ob.nfn    = static_cast<int>(u32(b, r + 32));
        ob.nsync  = static_cast<int>(u32(b, r + 44));
        ob.loop   = i32(b, r + 52);

        const int total = ob.nfn + ob.nsync;
        for (int k = 0; k < total; ++k) {
            const auto fo = o + 24u * static_cast<std::size_t>(k);
            ScxFunction fn;
            fn.id     = u32(b, fo);
            fn.isSync = k >= ob.nfn;
            const auto pn = i32(b, fo + 4);
            const auto pi = i32(b, fo + 8);
            fn.sync   = i32(b, fo + 12);
            fn.repeat = i32(b, fo + 16);
            fn.runs   = i32(b, fo + 20);
            // the params live in the shared pool at pbase, indexed - which is
            // what makes the pool "consumed in order with no gaps" a check
            for (int j = 0; j < pn; ++j)
                fn.params.push_back(i32(b, pbase + 4u * static_cast<std::size_t>(pi + j)));
            ob.functions.push_back(std::move(fn));
        }
        o += 24u * static_cast<std::size_t>(total);

        // two name tables, each [count][8*count bytes][21-byte names]
        for (int t = 0; t < 2; ++t) {
            const auto c = u32(b, o);
            for (std::uint32_t k = 0; k < c; ++k)
                ob.tables[t].push_back(
                    str(b, o + 4u + 8u * c + 21u * static_cast<std::size_t>(k), 21));
            o += 4u + 29u * c;
        }
        s.objects.push_back(std::move(ob));
    }
    s.chunkCounts[2] = s.objects.size();

    // ---- the remaining chunks
    while (o + 4 <= b.size()) {
        const auto t = u32(b, o);
        if (t == 0xDEADFFFFu) { o += 4; break; }
        const int ty = static_cast<int>(t & 0xFFFFu);
        const auto it = kScxStride.find(ty);
        if ((t >> 16) != 0xDEADu || it == kScxStride.end()) {
            o += 4;                    // the loader's own default: skip on
            continue;
        }
        const auto c = u32(b, o + 4);
        s.chunkCounts[ty] = c;
        o += 8u + it->second * c;
    }
    s.blockEnd = o;
    s.complete = o <= b.size();
    s.valid = true;
    return s;
}

ScxStream readScxStream(std::span<const std::byte> d) {
    ScxStream st;
    if (d.size() < 16) return st;
    if (u32(d, 0) != 0x00DEAD00u || u32(d, 4) != 5u) return st;
    const auto blockSize = u32(d, 12);
    if (16 + blockSize > d.size()) return st;
    const auto b = d.subspan(16, blockSize);

    // walk the block for the chunk ORDER and the registries chunk 1/3/4 hold
    const auto scene = readScx(d);
    if (!scene.valid) return st;

    std::vector<int> order{2};
    std::map<int, std::uint32_t> counts;
    std::vector<std::string> animNames, spriteNames;
    std::vector<std::int32_t> animIds, spriteIds;
    {
        // re-walk to recover the order and the in-block registry rows
        const auto n = u32(b, 4);
        const std::size_t afterObjs = [&] {
            // objects() consumed to here; readScx already computed it, but the
            // order matters and it does not keep it, so repeat the cheap part
            const std::size_t after = 8u + 100u * n;
            const auto pc = u32(b, after);
            std::size_t o = after + 4 + 4u * pc;
            for (std::uint32_t i = 0; i < n; ++i) {
                const auto r = 8u + 100u * static_cast<std::size_t>(i);
                o += (o < b.size() && static_cast<std::uint8_t>(b[o])) ? 22u : 1u;
                const int tot = static_cast<int>(u32(b, r + 32) + u32(b, r + 44));
                o += 24u * static_cast<std::size_t>(tot);
                for (int t = 0; t < 2; ++t) {
                    const auto c = u32(b, o);
                    o += 4u + 29u * c;
                }
            }
            return o;
        }();
        std::size_t o = afterObjs;
        while (o + 4 <= b.size()) {
            const auto t = u32(b, o);
            if (t == 0xDEADFFFFu) break;
            const int ty = static_cast<int>(t & 0xFFFFu);
            if ((t >> 16) != 0xDEADu) { o += 4; continue; }
            o += 4;
            order.push_back(ty);
            const auto it = kScxStride.find(ty);
            if (it == kScxStride.end()) continue;   // 8 and 10 hold nothing here
            const auto c = u32(b, o);
            counts[ty] = c;
            for (std::uint32_t i = 0; i < c; ++i) {
                const auto rec = o + 4u + it->second * static_cast<std::size_t>(i);
                if (ty == 1) {
                    animNames.push_back(str(b, rec, 24));
                    animIds.push_back(i32(b, rec + 32));
                } else if (ty == 4) {
                    spriteNames.push_back(str(b, rec, 24));
                    // `+32` is the sprite's ID, the same slot an anim's is in.
                    // `sub_4A5800` walks these 36-byte rows matching it, so an
                    // effect's sprite field is an ID here and not an index
                    // anywhere.
                    spriteIds.push_back(i32(b, rec + 32));
                }
            }
            o += 4u + it->second * c;
        }
    }

    // every record's first word is its own offset - so a mismatch is either a
    // bug or a record whose size did not cover its payload
    const auto resync = [&](std::size_t pos) {
        for (std::size_t q = pos; q + 8 <= d.size(); ++q)
            if (u32(d, q) == q) return q;
        return d.size();
    };

    std::size_t pos = 16u + blockSize;
    std::size_t ai = 0, si = 0;
    for (int ty : order) {
        if (ty == 10) {
            if (u32(d, pos) != pos) pos = resync(pos);
            const auto size = u32(d, pos + 4);
            st.camOffset = pos + 8u;
            st.camSize   = size;
            pos += 8u + size;
            if (pos < d.size() && u32(d, pos) != pos) pos = resync(pos);
            continue;
        }
        if (ty != 0 && ty != 1 && ty != 3 && ty != 4) continue;
        const std::size_t hdr = (ty == 4) ? 12u : 8u;
        for (std::uint32_t i = 0; i < counts[ty]; ++i) {
            if (pos + hdr > d.size()) break;
            if (u32(d, pos) != pos) { ++st.resyncs; pos = resync(pos); }
            if (pos + hdr > d.size()) break;
            const auto size = u32(d, pos + 4);
            const auto payload = pos + hdr;
            if (ty == 4) {
                const auto tsz = u32(d, pos + 8);
                ScxStreamSprite sp;
                sp.name = si < spriteNames.size() ? spriteNames[si] : std::string();
                sp.id = si < spriteIds.size() ? spriteIds[si] : -1;
                sp.offset = payload; sp.model = size; sp.texture = tsz;
                st.sprites.push_back(std::move(sp));
                ++si;
                pos = payload + size + tsz;
                continue;
            }
            if (ty == 1) {
                ScxStreamAnim a;
                a.name = ai < animNames.size() ? animNames[ai] : std::string();
                a.id   = ai < animIds.size() ? animIds[ai] : 0;
                a.offset = payload; a.size = size;
                st.anims.push_back(std::move(a));
                ++ai;
            } else if (ty == 3) {
                ++st.wavs;
            } else if (ty == 0) {
                // a .3dp: a count, then name/duration/keyCount + 32-byte keys
                const auto pd = d.subspan(payload, std::min<std::size_t>(size, d.size() - payload));
                if (pd.size() >= 4) {
                    const auto np = u32(pd, 0);
                    std::size_t q = 4;
                    for (std::uint32_t k = 0; k < np && q + 28 <= pd.size(); ++k) {
                        ScxPath path;
                        path.name = str(pd, q, 20);
                        path.duration = u32(pd, q + 20);
                        const auto nk = u32(pd, q + 24);
                        q += 28;
                        for (std::uint32_t j = 0; j < nk && q + 32 <= pd.size(); ++j) {
                            ScxPathKey key;
                            key.frame = u32(pd, q);
                            for (int c = 0; c < 3; ++c) key.pos[c] = f32(pd, q + 4u + 4u * static_cast<std::size_t>(c));
                            for (int c = 0; c < 4; ++c) key.quat[c] = f32(pd, q + 16u + 4u * static_cast<std::size_t>(c));
                            path.keys.push_back(key);
                            q += 32;
                        }
                        st.paths.push_back(std::move(path));
                    }
                }
            }
            pos = payload + size;
        }
    }
    st.end = pos;
    st.valid = true;
    return st;
}

bool pathSample(const ScxPath& p, float t, float out[3]) {
    if (p.keys.empty()) return false;
    for (std::size_t i = 0; i + 1 < p.keys.size(); ++i) {
        const float a = static_cast<float>(p.keys[i].frame);
        const float b = static_cast<float>(p.keys[i + 1].frame);
        if (t < a || t > b) continue;
        const float u = b > a ? (t - a) / (b - a) : 0.0f;
        for (int k = 0; k < 3; ++k)
            out[k] = p.keys[i].pos[k] + (p.keys[i + 1].pos[k] - p.keys[i].pos[k]) * u;
        return true;
    }
    // Past the last key - the engine's search fails and the caller keeps what
    // it had; clamping to the end is what a sampler wants and is stated here
    // rather than silently done.
    for (int k = 0; k < 3; ++k) out[k] = p.keys.back().pos[k];
    return false;
}

}  // namespace omk

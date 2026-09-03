// SPDX-License-Identifier: GPL-3.0-or-later
// The I2D 2D layer, run: the display list, its pools, its acceptance tests
// and its three flag banks.
//
//     run_i2d <tables/ui_widgets.json> <out.bin>
//
// **What this can and cannot claim.** The DirectDraw back end is not ported -
// there is no surface here and `Blt` is on the far side of the same line as
// the rasterizers - so pixels cannot be diffed and never will be from this
// tree. Everything below is about what the layer DECIDES: which primitives
// are accepted, and in what order they would be drawn. One check touches
// shipped data and is the only one the game could contradict:
//
//   **F. every flag constant in the shipped widget tree resolves to a bank.**
//   139 of them - 87 an item applies through `I2D_SetFlag` and 52 a list
//   broadcasts through `I2D_SetFlagOnAllRows` - and a constant naming no bank
//   is silently DROPPED by the three-way `if`, so this is a property the data
//   could fail and a wrong bank test would break.
//
// The rest are invariants of the transcription. They are worth running - one
// of them is how the ordering below was pinned - but they are not evidence
// about the original, and the check docstring says so.
#include "platform/datafs.h"
#include "platform/json.h"
#include "ui/i2d.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace {

// A reproducible pseudo-random sequence: the sweep has to give the same
// numbers every run, so no rand().
struct Rng {
    std::uint32_t s = 0x1D2C3B4Au;
    std::uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int upto(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_i2d <tables/ui_widgets.json> <out.bin>\n");
        return 2;
    }

    // ---- A. the node cap is the sum of the pools ---------------------------
    long poolSum = 0, poolCount = 0, deadPools = 0;
    for (int i = 0; i < static_cast<int>(omk::I2dPrim::Count); ++i) {
        const auto& p = omk::i2dPool(static_cast<omk::I2dPrim>(i));
        poolSum += p.cap;
        if (p.cap) ++poolCount;
        if (!p.referenced) ++deadPools;
    }
    const long capMatches = (poolSum == omk::kI2dNodeCap) ? 1 : 0;

    // ---- B and C. the ordering --------------------------------------------
    //
    // B: across layers the list must be sorted - that is the property the
    //    whole layer exists for, and it must hold for EVERY submission order.
    // C: within a layer the head cache makes the first node stay first and
    //    the rest come out in reverse. That is measured, not assumed: a real
    //    tail cache would give submission order, and the two differ only
    //    where primitives on one layer overlap.
    long sequences = 0, sorted = 0, drawn = 0;
    long withinPredicted = 0, withinLayers = 0, firstNodeLayers = 0;
    {
        Rng rng;
        for (int seq = 0; seq < 200; ++seq) {
            omk::I2dList L;
            std::vector<std::pair<int, int>> submitted;   // (layer, node)
            const int n = 6 + rng.upto(40);
            for (int k = 0; k < n; ++k) {
                const int layer = rng.upto(omk::kI2dLayers);
                if (L.quad(layer) == omk::I2dRefusal::Accepted)
                    submitted.push_back({layer, L.nodes() - 1});
            }
            ++sequences;
            const auto ord = L.order();
            drawn += static_cast<long>(ord.size());
            bool ok = ord.size() == submitted.size();
            for (std::size_t i = 1; i < ord.size() && ok; ++i)
                if (L.node(ord[i - 1]).layer > L.node(ord[i]).layer) ok = false;
            sorted += ok ? 1 : 0;

            // C, per layer that got more than one node.
            //
            // Two shapes, and the second is a quirk worth stating: the very
            // first node of a FRAME takes `I2D_Enqueue`'s `if (!count)` arm,
            // which sets the head and does NOT set the layer cache. So the
            // second node on that layer goes through the walk instead, is
            // inserted BEFORE the first (their layers are equal, and the walk
            // stops at `>=`), and becomes the cache. Everywhere else the
            // layer's first node is the cache.
            //
            //   ordinary layer   s0, s(n-1), s(n-2), ..., s1
            //   the layer of the frame's first node
            //                    s1, s(n-1), ..., s2, s0
            //
            // A first version predicted only the first shape and scored
            // 1260 of 1407 - the 147 misses were exactly the layers holding
            // node 0, which is how the second shape was found.
            for (int layer = 0; layer < omk::kI2dLayers; ++layer) {
                std::vector<int> subOrder, drawOrder;
                for (const auto& [l, id] : submitted) if (l == layer) subOrder.push_back(id);
                for (int id : ord) if (L.node(id).layer == layer) drawOrder.push_back(id);
                if (subOrder.size() < 2) continue;
                ++withinLayers;
                const bool holdsFirst = subOrder.front() == 0;
                if (holdsFirst) ++firstNodeLayers;
                std::vector<int> want;
                if (holdsFirst) {
                    want.push_back(subOrder[1]);
                    for (std::size_t i = subOrder.size(); i-- > 2;) want.push_back(subOrder[i]);
                    want.push_back(subOrder[0]);
                } else {
                    want.push_back(subOrder.front());
                    for (std::size_t i = subOrder.size(); i-- > 1;) want.push_back(subOrder[i]);
                }
                if (want == drawOrder) ++withinPredicted;
            }
        }
    }

    // ---- D. the acceptance tests ------------------------------------------
    long refusals[6] = {};      // indexed by I2dRefusal
    long dstYAccepted = 0;
    {
        const omk::I2dPoint good0{0, 0, 0}, good1{100, 100, 0};
        const omk::I2dPoint src[2] = {good0, good1};
        const omk::I2dPoint dst[2] = {good0, good1};
        omk::I2dList L;
        ++refusals[static_cast<int>(L.quad(99))];                 // layer
        ++refusals[static_cast<int>(L.quad(3))];                  // accepted
        {   // a SOURCE inverted in x, then in y, then a DESTINATION in x
            const omk::I2dPoint badX[2] = {good1, good0};
            ++refusals[static_cast<int>(L.blitBitmap(0, badX, dst))];
            const omk::I2dPoint badY[2] = {{0, 100, 0}, {100, 0, 0}};
            ++refusals[static_cast<int>(L.blitBitmap(0, badY, dst))];
            ++refusals[static_cast<int>(L.blitBitmap(0, src, badX))];
            // and the asymmetry: a DESTINATION inverted only in Y is accepted,
            // because neither blit tests dwords 7 and 10
            if (L.blitBitmap(0, src, badY) == omk::I2dRefusal::Accepted)
                ++dstYAccepted;
            if (L.blitSurface(0, src, badY) == omk::I2dRefusal::Accepted)
                ++dstYAccepted;
        }
        // the pool: the 3D view's is 16, the smallest live one
        omk::I2dList V;
        long viewAccepted = 0;
        for (int k = 0; k < 40; ++k)
            if (V.view3d(0, true) == omk::I2dRefusal::Accepted) ++viewAccepted;
        refusals[static_cast<int>(omk::I2dRefusal::PoolFull)] += 40 - viewAccepted;
    }

    // ---- E. flush resets everything ---------------------------------------
    long flushClean = 0, zwrite = 0;
    {
        omk::I2dList L;
        for (int k = 0; k < 20; ++k) L.quad(k % omk::kI2dLayers);
        const auto before = L.order();
        L.flush();
        for (int k = 0; k < 20; ++k) L.quad(k % omk::kI2dLayers);
        const auto after = L.order();
        if (before == after && L.nodes() == 20) ++flushClean;
        zwrite = L.zwriteToggles();
    }

    // ---- F. the shipped flag constants ------------------------------------
    long constants = 0, resolved = 0, perBank[3] = {};
    long roundTrips = 0, roundTripsOk = 0;
    long itemWordsUsed[3] = {};
    {
        const auto doc = omk::Json::parseFile(argv[1]);
        const auto& panels = doc["rows"]["panels"];
        for (std::size_t p = 0; p < panels.size(); ++p) {
            const auto& lists = panels[p]["lists"];
            for (std::size_t l = 0; l < lists.size(); ++l) {
                const auto& bc = lists[l]["broadcast"];
                for (std::size_t b = 0; b < bc.size(); ++b) {
                    const auto f = static_cast<std::int32_t>(bc[b][0].i64());
                    ++constants;
                    const int w = omk::i2dFlagWord(f);
                    if (w < 0) continue;
                    ++resolved; ++perBank[w];
                }
                const auto& items = lists[l]["items"];
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const auto& sf = items[i]["setflag"];
                    for (std::size_t k = 0; k < sf.size(); ++k) {
                        const auto f = static_cast<std::int32_t>(sf[k][0].i64());
                        const bool on = sf[k][1].boolean();
                        ++constants;
                        const int w = omk::i2dFlagWord(f);
                        if (w < 0) continue;
                        ++resolved; ++perBank[w];
                        // and the round trip: setting it must land in that
                        // word, be readable back, and clear cleanly
                        omk::I2dFlagWords fw;
                        ++roundTrips;
                        omk::i2dSetFlag(fw, f, true);
                        const bool landed = fw.w[w] != 0 &&
                            fw.w[(w + 1) % 3] == 0 && fw.w[(w + 2) % 3] == 0 &&
                            omk::i2dTestFlag(fw, f);
                        omk::i2dSetFlag(fw, f, false);
                        const bool cleared = fw.w[0] == 0 && fw.w[1] == 0 &&
                                             fw.w[2] == 0 && !omk::i2dTestFlag(fw, f);
                        if (landed && cleared) ++roundTripsOk;
                        (void)on;
                    }
                    const auto& fl = items[i]["flags"];
                    for (std::size_t w = 0; w < fl.size() && w < 3; ++w)
                        if (fl[w].i64() != 0) ++itemWordsUsed[w];
                }
            }
        }
    }

    std::vector<std::uint8_t> o;
    const auto put = [&o](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {poolSum, poolCount, deadPools, capMatches,
                   sequences, sorted, drawn, withinLayers, withinPredicted,
                   firstNodeLayers,
                   refusals[static_cast<int>(omk::I2dRefusal::Accepted)],
                   refusals[static_cast<int>(omk::I2dRefusal::LayerOutOfRange)],
                   refusals[static_cast<int>(omk::I2dRefusal::ListFull)],
                   refusals[static_cast<int>(omk::I2dRefusal::PoolFull)],
                   refusals[static_cast<int>(omk::I2dRefusal::DegenerateSrc)],
                   refusals[static_cast<int>(omk::I2dRefusal::DegenerateDest)],
                   dstYAccepted, flushClean, zwrite,
                   constants, resolved, perBank[0], perBank[1], perBank[2],
                   roundTrips, roundTripsOk,
                   itemWordsUsed[0], itemWordsUsed[1], itemWordsUsed[2]})
        put(v);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("pools: %ld capacity over %ld live pools (%ld unreferenced); "
                "the display list's own cap is %d and %s\n",
                poolSum, poolCount, deadPools, omk::kI2dNodeCap,
                capMatches ? "they are EQUAL" : "they DIFFER");
    std::printf("order: %ld sequences, %ld drawn in layer order, %ld nodes; "
                "%ld layers with more than one node, %ld matching the head-"
                "cache prediction (%ld of them the frame's FIRST node's "
                "layer, which has its own shape)\n",
                sequences, sorted, drawn, withinLayers, withinPredicted,
                firstNodeLayers);
    std::printf("refusals: %ld accepted, %ld layer out of range, %ld list "
                "full, %ld pool full, %ld degenerate src, %ld degenerate "
                "dest; %ld accepted with an inverted DESTINATION Y, which "
                "neither blit tests\n",
                refusals[0], refusals[1], refusals[2], refusals[3], refusals[4],
                refusals[5], dstYAccepted);
    std::printf("flush: %ld clean resets, %ld render-state toggles\n",
                flushClean, zwrite);
    std::printf("flags: %ld shipped constants, %ld resolving to a bank "
                "(%ld/%ld/%ld into words +48/+52/+56); %ld set/test round "
                "trips, %ld correct; item flag words in use: %ld/%ld/%ld\n",
                constants, resolved, perBank[0], perBank[1], perBank[2],
                roundTrips, roundTripsOk, itemWordsUsed[0], itemWordsUsed[1],
                itemWordsUsed[2]);
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/i2d.h"

namespace omk {
namespace {

// The seven pools, from the bounds check each submitter opens with. The two
// marked unreferenced are real functions with real pools and **no callers**:
// `sub_428660` (cap 10) and `sub_4286F0`. They are kept because the node cap
// only adds up with them - 4096+200+100+220+220+16+10 = 4862 - so leaving
// them out would make the list's own limit look arbitrary.
//
// `sub_4286F0` also carries a **latent overflow**: it checks the BITMAP
// counter (`dword_4E97B0`, cap 220) and then writes into the 10-entry pool at
// `dword_4E9784` indexed by `dword_4E97A4`, incrementing that instead. Nothing
// calls it, so the bug never fires; it is recorded rather than fixed, because
// a replica that quietly corrected it would stop being a replica.
const I2dPoolInfo kPools[static_cast<int>(I2dPrim::Count)] = {
    {"line",        4096, 28, 0x00428430, 0x004822F0, true },
    {"triangle",     200, 40, 0x00428560, 0x004806C0, true },
    {"quad",         100, 52, 0x004285E0, 0x00480BD0, true },
    {"blitSurface",  220, 52, 0x00428850, 0x00480F60, true },
    {"blitBitmap",   220, 56, 0x004287A0, 0x004810D0, true },
    {"view3d",        16, 84, 0x00428900, 0x004812E0, true },
    {"rect24",        10, 24, 0x00428660, 0x00481000, false},
    {"fullScreen",     0,  0, 0x00428780, 0x00481170, true },
};

}  // namespace

const I2dPoolInfo& i2dPool(I2dPrim p) {
    return kPools[static_cast<int>(p)];
}

void I2dList::ensure() {
    if (init_) return;
    for (auto& c : cache_) c = -1;
    init_ = true;
}

// `I2D_Enqueue` (0x004284B0), transcribed from the assembly rather than the
// decompiler, because the one thing that matters here is which branch writes
// the cache and the braces were ambiguous. See the header.
I2dRefusal I2dList::enqueue(I2dPrim p, int layer) {
    ensure();
    const int self = static_cast<int>(nodes_.size());
    nodes_.push_back({p, used_[static_cast<int>(p)], layer, -1});
    I2dNode& n = nodes_.back();

    if (self == 0) {                       // the very first node of the frame
        head_ = self;
        n.next = -1;
    } else if (cache_[layer] >= 0) {
        // the cache HIT path (loc_428534): splice in after the cached node -
        // and do NOT update the cache. This is the whole of the reverse
        // ordering within a layer.
        I2dNode& c = nodes_[static_cast<std::size_t>(cache_[layer])];
        n.next = c.next;
        c.next = self;
    } else {
        // the walk (loc_4284FA): find the first node whose layer is >= ours,
        // remembering the pointer that points at it. Both exits set the cache.
        int cur = head_;
        int* prev = &head_;
        while (nodes_[static_cast<std::size_t>(cur)].next != -1) {
            if (nodes_[static_cast<std::size_t>(cur)].layer >= layer) break;
            prev = &nodes_[static_cast<std::size_t>(cur)].next;
            cur = nodes_[static_cast<std::size_t>(cur)].next;
        }
        if (nodes_[static_cast<std::size_t>(cur)].layer >= layer) {
            n.next = *prev;                 // insert before `cur`
            *prev = self;
        } else {
            nodes_[static_cast<std::size_t>(cur)].next = self;   // append
            n.next = -1;
        }
        cache_[layer] = self;
    }
    ++used_[static_cast<int>(p)];
    return I2dRefusal::Accepted;
}

namespace {
// Every submitter opens with the same two: the layer must be in range and the
// list must have room. The pool check is per primitive.
I2dRefusal common(int layer, int nodes) {
    if (layer < 0 || layer >= kI2dLayers) return I2dRefusal::LayerOutOfRange;
    if (nodes >= kI2dNodeCap)             return I2dRefusal::ListFull;
    return I2dRefusal::Accepted;
}
}  // namespace

I2dRefusal I2dList::line(int layer) {
    if (auto r = common(layer, nodes()); r != I2dRefusal::Accepted) return r;
    if (used_[static_cast<int>(I2dPrim::Line)] >= i2dPool(I2dPrim::Line).cap)
        return I2dRefusal::PoolFull;
    return enqueue(I2dPrim::Line, layer);
}

I2dRefusal I2dList::triangle(int layer) {
    if (auto r = common(layer, nodes()); r != I2dRefusal::Accepted) return r;
    if (used_[static_cast<int>(I2dPrim::Triangle)] >= i2dPool(I2dPrim::Triangle).cap)
        return I2dRefusal::PoolFull;
    return enqueue(I2dPrim::Triangle, layer);
}

I2dRefusal I2dList::quad(int layer) {
    if (auto r = common(layer, nodes()); r != I2dRefusal::Accepted) return r;
    if (used_[static_cast<int>(I2dPrim::Quad)] >= i2dPool(I2dPrim::Quad).cap)
        return I2dRefusal::PoolFull;
    return enqueue(I2dPrim::Quad, layer);
}

// `I2D_BlitFullScreen` (0x00428780) is five lines: it enqueues its drawer with
// a NULL payload and no checks at all - not the layer, not the list. The
// replica keeps that, because a node it refuses is a node the original draws.
I2dRefusal I2dList::fullScreen(int layer) {
    return enqueue(I2dPrim::FullScreen, layer);
}

namespace {
// The three rectangle tests both blits apply, in their order and against the
// payload dwords they actually read: 0 vs 3, 1 vs 4, then 6 vs 9. The first
// rectangle is the SOURCE and the second the DESTINATION (see i2d.h), so the
// absent fourth test - `dst.y0 >= dst.y1` - is on the destination.
I2dRefusal rects(const I2dPoint src[2], const I2dPoint dst[2]) {
    if (src[0].x >= src[1].x) return I2dRefusal::DegenerateSrc;   // dwords 0, 3
    if (src[0].y >= src[1].y) return I2dRefusal::DegenerateSrc;   // dwords 1, 4
    if (dst[0].x >= dst[1].x) return I2dRefusal::DegenerateDest;  // dwords 6, 9
    return I2dRefusal::Accepted;
}
}  // namespace

I2dRefusal I2dList::blitBitmap(int layer, const I2dPoint src[2], const I2dPoint dst[2],
                               int bitmap, std::uint32_t flags) {
    if (auto r = common(layer, nodes()); r != I2dRefusal::Accepted) return r;
    if (used_[static_cast<int>(I2dPrim::BlitBitmap)] >= i2dPool(I2dPrim::BlitBitmap).cap)
        return I2dRefusal::PoolFull;
    if (auto r = rects(src, dst); r != I2dRefusal::Accepted) return r;
    blits_.push_back({{src[0], src[1]}, {dst[0], dst[1]}, bitmap, flags});
    return enqueue(I2dPrim::BlitBitmap, layer);
}

I2dRefusal I2dList::blitSurface(int layer, const I2dPoint src[2], const I2dPoint dst[2],
                                int bitmap, std::uint32_t flags) {
    if (auto r = common(layer, nodes()); r != I2dRefusal::Accepted) return r;
    if (used_[static_cast<int>(I2dPrim::BlitSurface)] >= i2dPool(I2dPrim::BlitSurface).cap)
        return I2dRefusal::PoolFull;
    if (auto r = rects(src, dst); r != I2dRefusal::Accepted) return r;
    blits_.push_back({{src[0], src[1]}, {dst[0], dst[1]}, bitmap, flags});
    return enqueue(I2dPrim::BlitSurface, layer);
}

I2dRefusal I2dList::view3d(int layer, bool sceneValid) {
    if (auto r = common(layer, nodes()); r != I2dRefusal::Accepted) return r;
    if (used_[static_cast<int>(I2dPrim::View3d)] >= i2dPool(I2dPrim::View3d).cap)
        return I2dRefusal::PoolFull;
    if (!sceneValid) return I2dRefusal::DegenerateSrc;
    return enqueue(I2dPrim::View3d, layer);
}

std::vector<int> I2dList::order() const {
    std::vector<int> out;
    for (int i = head_; i != -1; i = nodes_[static_cast<std::size_t>(i)].next) {
        out.push_back(i);
        if (out.size() > nodes_.size()) break;   // a cycle would hang the walk
    }
    return out;
}

void I2dList::present(Surface& fb, const std::vector<Surface>& bitmaps,
                      std::uint16_t srcKey, std::uint16_t dstKey) const {
    std::size_t next = 0;
    std::vector<std::size_t> slotOf(nodes_.size(), 0);
    // the blits were pushed in SUBMISSION order and the walk is in DRAW order,
    // so each node needs its own payload index rather than a running counter
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].prim == I2dPrim::BlitBitmap ||
            nodes_[i].prim == I2dPrim::BlitSurface)
            slotOf[i] = next++;
    for (int id : order()) {
        const I2dNode& n = nodes_[static_cast<std::size_t>(id)];
        if (n.prim != I2dPrim::BlitBitmap && n.prim != I2dPrim::BlitSurface)
            continue;      // the other primitives are not the blit back end
        const I2dBlit& b = blits_[slotOf[static_cast<std::size_t>(id)]];
        if (b.bitmap < 0 || b.bitmap >= static_cast<int>(bitmaps.size())) continue;
        const std::uint32_t f = kBltWait
                              | ((b.flags & 1u) ? kBltKeySrc : 0u)
                              | ((b.flags & 2u) ? kBltKeyDest : 0u);
        blt(fb, {b.dst[0].x, b.dst[0].y, b.dst[1].x, b.dst[1].y},
            bitmaps[static_cast<std::size_t>(b.bitmap)],
            {b.src[0].x, b.src[0].y, b.src[1].x, b.src[1].y}, f, srcKey, dstKey);
    }
}

void I2dList::flush() {
    ensure();
    // the D3D state pair around the walk: clear ZWRITEENABLE, draw, set it
    // again. Cached in `dword_8F56D8`, so it is one call each way.
    zwrite_ += 2;
    nodes_.clear();
    blits_.clear();
    head_ = -1;
    for (auto& c : cache_) c = -1;
    for (auto& u : used_) u = 0;
}

// ---------------------------------------------------------------- the flags

std::uint32_t i2dFlagBank(std::int32_t flag) {
    const auto f = static_cast<std::uint32_t>(flag);
    if (f & kI2dBankA) return kI2dBankA;
    if (f & kI2dBankB) return kI2dBankB;
    if (flag < 0)      return kI2dBankC;   // the original's `a1 < 0`
    return 0;                              // -1 in the original: no bank
}

int i2dFlagWord(std::int32_t flag) {
    switch (i2dFlagBank(flag)) {
        case kI2dBankA: return 0;
        case kI2dBankB: return 1;
        case kI2dBankC: return 2;
        default:        return -1;
    }
}

bool i2dTestFlag(const I2dFlagWords& f, std::int32_t flag) {
    const auto v = static_cast<std::uint32_t>(flag);
    // Each arm masks the bank bit back OUT before testing, so the bank is an
    // address and never a bit of the value.
    if (v & kI2dBankA) return (v & f.w[0] & ~kI2dBankA) != 0;
    if (v & kI2dBankB) return (v & f.w[1] & ~kI2dBankB) != 0;
    if (flag < 0)      return (v & f.w[2] & ~kI2dBankC) != 0;
    return false;
}

void i2dSetFlag(I2dFlagWords& f, std::int32_t flag, bool on) {
    const auto v = static_cast<std::uint32_t>(flag);
    const int  w = i2dFlagWord(flag);
    if (w < 0) return;                      // no bank: silently dropped
    const std::uint32_t bank = i2dFlagBank(flag);
    const std::uint32_t bits = v & ~bank;
    if (on) f.w[w] |= bits; else f.w[w] &= ~bits;
}

void i2dSetFlagOnAllRows(std::vector<I2dFlagWords>& rows, std::int32_t flag, bool on) {
    for (auto& r : rows) i2dSetFlag(r, flag, on);
}

}  // namespace omk

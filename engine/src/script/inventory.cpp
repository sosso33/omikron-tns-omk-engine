// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/inventory.h"

#include <algorithm>

namespace omk {

// `ObjectList_SetCapacity(3, 16)` over the id array case 9 points at the
// block's `+8`: count up to the first 0xFFFF, stopping at the capacity.
std::vector<int> shopStock(std::span<const std::byte> block) {
    std::vector<int> out;
    for (std::size_t k = 0; k < 16; ++k) {
        const std::size_t o = 8 + 2 * k;
        if (o + 2 > block.size()) break;
        const auto v = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(block[o]) |
            (static_cast<std::uint16_t>(block[o + 1]) << 8));
        if (v == 0xFFFF) break;
        out.push_back(v);
    }
    return out;
}

std::vector<int> objectList(const GameState& s, ObjectList which) {
    const int k = static_cast<int>(which);
    const int off = GameState::kListOffset[k];
    const int cap = GameState::kListCapacity[k];
    std::vector<int> out;
    const auto raw = s.raw();
    for (int i = 0; i < cap; ++i) {
        const std::size_t o = static_cast<std::size_t>(off + 2 * i);
        if (o + 2 > raw.size()) break;
        const auto v = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(raw[o]) |
            (static_cast<std::uint16_t>(raw[o + 1]) << 8));
        if (v > 0) out.push_back(v);
    }
    return out;
}

const ObjectRecord* Inventory::record(int id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= objects_->size()) return nullptr;
    return &(*objects_)[static_cast<std::size_t>(id)];
}

std::string Inventory::displayName(int id, int playerCount) const {
    const auto* o = record(id);
    if (!o) return {};
    if (!(o->flags & 0x20)) return o->name;
    // kinds 2..6 count from the PLAYER record, 7..11 from the item's own +12
    const int n = (o->kind >= 2 && o->kind <= 6) ? playerCount : o->quantity;
    return o->name + " - " + std::to_string(n);
}

int Inventory::price(int id) const {
    const auto* o = record(id);
    return o ? o->price : 0;
}

int Inventory::sellValue(int id) const {
    const auto* o = record(id);
    if (!o) return 0;
    return std::min(o->price / 2, 0xFFFF);
}

InvResult Inventory::canBuy(int id, int money, int carried, int capacity) const {
    const auto* o = record(id);
    if (!o) return InvResult::Refused;
    if (carried >= capacity) return InvResult::Refused;   // the list is full
    if (o->price > money) return InvResult::Refused;
    return InvResult::Ok;
}

InvResult Inventory::shopAllows(int id, const std::vector<int>& carried) const {
    const auto* o = record(id);
    if (!o) return InvResult::Refused;
    // a gun (kinds 2..6) the player already holds cannot be bought twice
    if (o->kind >= 2 && o->kind <= 6)
        for (int held : carried) {
            const auto* h = record(held);
            if (h && h->kind == o->kind) return InvResult::Refused;
        }
    return InvResult::Ok;
}

int Inventory::combine(int a, int b, int gate) const {
    for (const auto& r : *recipes_) {
        const bool match = (r.a == a && r.b == b) || (r.a == b && r.b == a);
        if (match && r.gate == gate) return r.product;
    }
    return -1;
}

}  // namespace omk

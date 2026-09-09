// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shootmode.h"

namespace omk {

void ShootMode::setWeaponTable(const std::int16_t* slots, int n) {
    table_.assign(slots, slots + n);
}

int ShootMode::slotForObject(int object) const {
    // `Weapon_SlotForObject` (0x0040EA50): -1 in, -1 out; otherwise the walk
    // starts at slot 5 and gives up at 15, so an object in no slot is -1 too.
    if (object == -1) return -1;
    for (std::size_t i = 0; i < table_.size(); ++i)
        if (table_[i] == object) return kWeaponSlotFirst + static_cast<int>(i);
    return -1;
}

int ShootMode::objectForSlot(int slot) const {
    const int i = slot - kWeaponSlotFirst;
    if (i < 0 || static_cast<std::size_t>(i) >= table_.size()) return -1;
    return table_[static_cast<std::size_t>(i)];
}

bool ShootMode::begin(int weaponObject) {
    if (active_) return false;                     // the handler's own guard
    // `if (dword_4C0134 == -1) { v = Weapon_SlotForObject(op); if (v == -1)
    //   dword_4C0134 = 11; else dword_4C0134 = v; }` - a gun already in hand
    // beats the script's operand, and an operand naming no weapon falls to 11.
    if (weapon_ == -1) {
        const int s = slotForObject(weaponObject);
        weapon_ = s == -1 ? kWeaponSlotDefault : s;
    }
    active_ = true;
    hud_ = hudScreenFor(type_);
    log_.push_back({true, weapon_, hud_, "shoot.begin"});
    return true;
}

bool ShootMode::end(int clear) {
    if (!active_) {
        // `Shoot_Leave` traces "Le mode Shoot n'est pas activ<e>!!!" rather
        // than refusing, so the opcode still clears the slot.
        if (clear) weapon_ = -1;
        return false;
    }
    active_ = false;
    hud_ = -1;
    actors_.clear();
    if (clear) weapon_ = -1;
    log_.push_back({false, weapon_, -1, clear ? "shoot.end, weapon cleared"
                                              : "shoot.end, weapon kept"});
    return true;
}

std::vector<int> ShootMode::actorsInMode() const {
    std::vector<int> v;
    v.reserve(actors_.size());
    for (const auto& kv : actors_) v.push_back(kv.first);
    return v;
}

}  // namespace omk

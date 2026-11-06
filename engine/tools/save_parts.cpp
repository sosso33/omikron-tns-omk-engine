// SPDX-License-Identifier: GPL-3.0-or-later
// The save file read in PARTS against the save file read whole - verify.py's
// `engine: save read in parts`.
//
//     save_parts <a whole IAM/GAMES>
//
// Every slot's name, date and DB through `readSaveSlotAt` (its 8232 bytes
// read alone) against `readSaveSlot` over the whole file, and every slot's
// picture through `readSaveThumbAt` (its 24576 bytes) against `readSaveThumb`.
// The parts are what the save screen and the in-game load read since
// 2026-11-06; the whole file, read up to three times at once there, ran the
// iMac G3 out of memory (`todo/ram-vs-original.md`).
#include "platform/datafs.h"
#include "script/savefile.h"

#include <algorithm>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: save_parts <IAM/GAMES>\n");
        return 2;
    }
    const std::string path = argv[1];
    const auto whole = omk::DataFs::readPath(path);
    int slots = 0, slotsSame = 0, named = 0, thumbs = 0, thumbsSame = 0, pictured = 0;
    for (int k = 0; k < static_cast<int>(omk::kSaveSlots); ++k) {
        const auto a = omk::readSaveSlot(whole, k);
        const auto b = omk::readSaveSlotAt(path, k);
        if (a || b) {
            ++slots;
            if (a && b && a->name == b->name && a->day == b->day && a->time == b->time &&
                std::equal(a->state.raw().begin(), a->state.raw().end(),
                           b->state.raw().begin(), b->state.raw().end()))
                ++slotsSame;
            if (a && !a->name.empty()) ++named;
        }
        const auto ta = omk::readSaveThumb(whole, k);
        const auto tb = omk::readSaveThumbAt(path, k);
        if (!ta.empty() || !tb.empty()) {
            ++thumbs;
            if (ta == tb) ++thumbsSame;
            if (std::any_of(ta.begin(), ta.end(), [](std::uint16_t v) { return v != 0; })) ++pictured;
        }
    }
    std::printf("save parts: %zu bytes whole; slots %d, the same read alone %d (%d named); "
                "pictures %d, the same read alone %d (%d not black)\n",
                whole.size(), slots, slotsSame, named, thumbs, thumbsSame, pictured);
    return 0;
}

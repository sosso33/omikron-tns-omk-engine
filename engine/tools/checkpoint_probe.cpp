// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHOOT PHASE'S CHECKPOINT - ops 148 `inventory.save` / 149
// `inventory.restore` (0x405F40 / 0x405FC0, read from the image;
// `todo/drift-audit.md` S4), run as real Session contexts.
//
//     checkpoint_probe <gamedata> <tables>
//
// The carried list (list 0) is set to three ids inserted 10, 20, 30 - so it
// reads 30 20 10, the newest at the front - and the five guns' ammunition
// (player record +260..+268) to 5 6 7 8 9. Op 148 runs; then the list loses
// 20 and gains 40 and the ammunition is spent to 0; then op 149 runs. One
// line per fact:
//
//   before   the list and the ammunition the save saw
//   spent    what the run changed them to
//   after    what the restore left: the SAME ids, REVERSED - the restore
//            re-inserts at the front in saved order - and the ammunition back
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

void emit(std::vector<std::byte>& out, const omk::OpcodeTable& t, int op) {
    if (out.empty()) out.push_back(std::byte{3});
    out.push_back(static_cast<std::byte>(op));
    for (int k = 0; k < t.operandLength(static_cast<std::uint8_t>(op)); ++k)
        out.push_back(std::byte{0});
}

std::string listOf(const omk::GameState& s) {
    std::string out;
    for (int i = 0; i < s.listCount(0); ++i) {
        if (!out.empty()) out += ",";
        out += std::to_string(s.listAt(0, i));
    }
    return out.empty() ? "-" : out;
}

std::string ammoOf(const omk::GameState& s) {
    std::string out;
    for (int k = 0; k < 5; ++k) {
        if (k) out += ",";
        out += std::to_string(s.playerI16(260 + 2 * k));
    }
    return out;
}

void runOne(omk::Session& s, const omk::OpcodeTable& t, int op) {
    std::vector<std::byte> code;
    emit(code, t, op);
    emit(code, t, 3);                                   // end
    const std::int32_t scripts[3] = {1, 0, 0};
    const int idx = s.newContext(0, code, scripts, -1, s.currentArea());
    s.queueAction(idx, 1);
    s.frame();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: checkpoint_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.loadArea(2);
    for (int k = 0; k < 2; ++k) {
        if (s.residentSlot(k).areaCtx >= 0) s.freeContext(s.residentSlot(k).areaCtx);
        if (s.residentSlot(k).sceneCtx >= 0) s.freeContext(s.residentSlot(k).sceneCtx);
    }

    state.listClear(0);
    for (const int id : {10, 20, 30}) state.listAdd(0, id);
    for (int k = 0; k < 5; ++k) state.setPlayerI16(260 + 2 * k, static_cast<std::int16_t>(5 + k));
    std::printf("before list %s ammo %s\n", listOf(state).c_str(), ammoOf(state).c_str());
    runOne(s, table, 148);

    state.listRemove(0, 20);
    state.listAdd(0, 40);
    for (int k = 0; k < 5; ++k) state.setPlayerI16(260 + 2 * k, 0);
    std::printf("spent list %s ammo %s\n", listOf(state).c_str(), ammoOf(state).c_str());
    runOne(s, table, 149);
    std::printf("after list %s ammo %s\n", listOf(state).c_str(), ammoOf(state).c_str());
    return 0;
}

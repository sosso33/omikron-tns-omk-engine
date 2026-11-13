// SPDX-License-Identifier: GPL-3.0-or-later
// THE CONTEXT'S RECORD (`todo/drift-audit.md` M4): `character.show` and
// `character.hide` (0x403CB0 / 0x403DD0) hand `Scene_FindObjectRecord` the
// running context's own AREA SLOT (`mov cl, [edi+1Fh]`), so when both
// resident areas place one id it is the script's area's record that is
// attached or detached, and whose ObjectShown bit is written.
//
//     m4_show <gamedata> <tables>
//
// Anekbah's street (AREA 0) loaded, then AREA 46 into the other slot - the
// two place actors 59 and 62 at two spots, 59 with bits 2 and 163, 62 with
// 3 and 164, all four set in START, so both copies are shown. Each step
// from a context of the named area's slot:
//   * both copies of 59 and 62 hidden, each from its own area's context;
//   * 46 shows 59 -> 46's record attached (6716, 419), bit 163 set, 2 not;
//   * 0 shows 62  -> 0's record attached (3578, -1982), bit 3 set, 164 not;
//   * 46 hides 62 -> 46's record, which was not shown: bit 164 stays clear
//                    and 0's 62 stays shown.
// One line, each answer a field. The port before M4 took the SHOWN slot's
// record for the bit and the first slot's for the body, and hid every
// record of the id.
#include "formats/iam.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/opcodes.h"
#include "script/script.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// one instruction: the opcode and its int16 fields, padded to the operand
// length; offset 0 is the `end` nobody runs (a context arms a script only
// `if (script)`)
void emit(std::vector<std::byte>& out, const omk::OpcodeTable& t, int op, std::initializer_list<int> fields) {
    if (out.empty()) out.push_back(std::byte{3});
    out.push_back(static_cast<std::byte>(op));
    const int n = t.operandLength(static_cast<std::uint8_t>(op));
    int k = 0;
    for (const int f : fields) {
        out.push_back(static_cast<std::byte>(f & 0xFF));
        out.push_back(static_cast<std::byte>((f >> 8) & 0xFF));
        k += 2;
    }
    while (k < n) { out.push_back(std::byte{0}); ++k; }
}

// a context of `slot` running `op actor[, 1]` then `end`, run to its end
void runOn(omk::Session& s, const omk::OpcodeTable& t, int slot, int op, int actor) {
    std::vector<std::byte> code;
    emit(code, t, op, op == omk::op::CharacterShow ? std::initializer_list<int>{actor, 1}
                                                   : std::initializer_list<int>{actor});
    emit(code, t, omk::op::End, {});
    const std::int32_t scripts[3] = {1, 0, 0};
    const int i = s.newContext(slot, code, scripts, -1, -1);
    if (i < 0) return;
    s.queueAction(i, 1);   // ENTER: script 0 armed, run by the next frame's pump
    for (int f = 0; f < 4; ++f) s.frame();
}

// the shown entries for `actor`: which slot's record each is (by its spot)
std::string shownOf(const omk::Session& s, int actor) {
    std::string out;
    for (const auto& sh : s.shown())
        if (sh.actor == actor) {
            char b[64];
            std::snprintf(b, sizeof b, "%s%.0f,%.0f", out.empty() ? "" : "+", sh.pos[0], sh.pos[2]);
            out += b;
        }
    return out.empty() ? "none" : out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: m4_show <gamedata> <tables>\n"); return 2; }
    const std::string fr = argv[1], tb = argv[2], iam = fr + "/IAM";
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.answerUiFromPerson(true);
    s.loadArea(0);
    s.frame();
    s.requestArea(46);
    for (int f = 0; f < 400 && s.otherArea() != 46; ++f) s.frame();
    for (int f = 0; f < 30; ++f) s.frame();
    const int shownSlot = s.shownSlot();
    const int slot46 = s.residentSlot(0).area == 46 ? 0 : 1, slot0 = 1 - slot46;
    using omk::StateArray;
    const auto bit = [&](int i) { return state.bit(StateArray::ObjectShown, i); };
    const int before59 = bit(2) * 10 + bit(163), before62 = bit(3) * 10 + bit(164);
    for (const int a : {59, 62}) {
        runOn(s, table, slot46, omk::op::CharacterHide, a);
        runOn(s, table, slot0, omk::op::CharacterHide, a);
    }
    const std::string hidden = shownOf(s, 59) + "/" + shownOf(s, 62);
    const int cleared = bit(2) + bit(163) + bit(3) + bit(164);
    runOn(s, table, slot46, omk::op::CharacterShow, 59);
    const std::string show59 = shownOf(s, 59);
    const int after59 = bit(2) * 10 + bit(163);
    runOn(s, table, slot0, omk::op::CharacterShow, 62);
    const std::string show62 = shownOf(s, 62);
    const int after62 = bit(3) * 10 + bit(164);
    runOn(s, table, slot46, omk::op::CharacterHide, 62);
    const std::string hide62 = shownOf(s, 62);
    const int hid62 = bit(3) * 10 + bit(164);
    std::printf("m4 shown_slot %d area0_slot %d area46_slot %d | start bits 59 %02d 62 %02d | hidden %s bits %d | "
                "46 shows 59: bits %02d shown %s | 0 shows 62: bits %02d shown %s | 46 hides 62: bits %02d shown %s\n",
                shownSlot, slot0, slot46, before59, before62, hidden.c_str(), cleared, after59, show59.c_str(),
                after62, show62.c_str(), hid62, hide62.c_str());
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// `Actors_SpawnFromTables` IN A LIVE SESSION - the world's characters at an
// area load (todo/iam-script-engine.md 40).
//
//     spawn_probe <gamedata> <tables>
//
// One line per fact, `key name value name value ...`:
//
//   start     the shipped `ObjectShown` bitmap: how many of its bits START
//             sets, which is what decides who is attached at a load
//   boot      `loadArea(118)` - the intro area places two characters (310 and
//             136, bits 468/469) - then two frames of its startup script
//   load      AREA 222 with SCENE 55 over it, loaded straight from the
//             shipped START: 222's four passers-by and 55's three, the four
//             whose bit is set attached, the runtime slots handed out
//   demon     actor 57's placement out of that load - the record the check
//             quotes, and the one `A_2_DemonLook` animates
//   held      actor 57's record +270, which the spawn clears
//   hide      `character.hide 57` through a Session context: the Demon
//             detached and bit 804 cleared; then `character.show 57` back
//   save      bit 806 set BEFORE the load: Kay'l (49) attached at the load,
//             with no script involved - the point of the bit being a save bit
//   impasse   the same place reached by PLAYING: the intro run to SCENE 55's
//             hand-over (`scene.load 237, 57`). The same seven are spawned;
//             who is attached differs from the cold load because the intro's
//             own scripts have run, which is the point of the bit being live
//
// Nothing here is a reference value; the check that quotes these lines pins
// them, and todo/pending/T19.md has what they look like with the spawn
// skipped.
#include "formats/placements.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/props.h"
#include "script/script.h"
#include "ui/widgets.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// One instruction, as livezones_probe lays them down: offset 0 is an `end`
// nobody runs, so a script slot of 0 still means "none".
void emit(std::vector<std::byte>& out, const omk::OpcodeTable& t, int op,
          std::initializer_list<int> fields) {
    if (out.empty()) out.push_back(std::byte{3});
    out.push_back(static_cast<std::byte>(op));
    const int n = t.operandLength(static_cast<std::uint8_t>(op));
    std::size_t k = 0;
    for (const int f : fields) {
        out.push_back(static_cast<std::byte>(f & 0xFF));
        out.push_back(static_cast<std::byte>((f >> 8) & 0xFF));
        k += 2;
    }
    while (static_cast<int>(k) < n) { out.push_back(std::byte{0}); ++k; }
}

int spawnedIn(const omk::Session& s) {
    return static_cast<int>(s.residentSlot(0).characters.size() +
                            s.residentSlot(1).characters.size());
}

int attachedIn(const omk::Session& s) {
    int n = 0;
    for (int k = 0; k < 2; ++k)
        for (const auto& c : s.residentSlot(k).characters) n += c.attached ? 1 : 0;
    return n;
}

std::string idsOf(const omk::Session& s, bool attachedOnly) {
    std::string out;
    for (int k = 0; k < 2; ++k)
        for (const auto& c : s.residentSlot(k).characters) {
            if (attachedOnly && !c.attached) continue;
            if (!out.empty()) out += ",";
            out += std::to_string(c.actor);
        }
    return out.empty() ? "-" : out;
}

std::string slotsOf(const omk::Session& s) {
    std::string out;
    for (int k = 0; k < 2; ++k)
        for (const auto& c : s.residentSlot(k).characters) {
            if (!out.empty()) out += ",";
            out += std::to_string(c.slot);
        }
    return out.empty() ? "-" : out;
}

std::string shownModels(const omk::Session& s) {
    std::string out;
    for (const auto& sh : s.shown()) {
        if (!out.empty()) out += ",";
        out += sh.model.empty() ? "?" : sh.model;
    }
    return out.empty() ? "-" : out;
}

int rounded(float v) { return static_cast<int>(std::lround(v)); }

// Actor 57's record lives in SCENE 55's chunk; scan both slots and both
// tables, the way `Actor_FindById` does.
int heldFieldOf(const omk::Session& s, int actor) {
    for (int k = 0; k < 2; ++k) {
        const auto& sl = s.residentSlot(k);
        if (sl.area < 0) continue;
        if (auto o = omk::findActorRecord(sl.areaChunk, omk::ChunkKind::Area, actor))
            return omk::heldObjectOf(
                std::span<const std::byte>(sl.areaChunk).subspan(*o, omk::kActorRecordSize));
        if (sl.scene != -1)
            if (auto o = omk::findActorRecord(sl.sceneChunk, omk::ChunkKind::Scene, actor))
                return omk::heldObjectOf(
                    std::span<const std::byte>(sl.sceneChunk).subspan(*o, omk::kActorRecordSize));
    }
    return -99;
}

// The intro from a cold start to SCENE 55's hand-over, exactly as
// livezones_probe runs it.
long runToHandover(omk::Session& s, const std::string& tb) {
    s.loadAnnounceMap(tb + "/vm_announce.json");
    static omk::UiWidgets widgets = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
    if (widgets.valid()) s.attachUi(widgets);
    s.loadArea(118);
    s.frame();                                    // ui.open, and the DERIVED answer
    s.frame();
    long handover = -1;
    for (int f = 0; f < 400 && handover < 0; ++f) {
        s.frame();
        if (s.dialogOpen()) s.endDialog();
        for (const auto& an : s.announced())
            if (an.domain == "SCENES" && an.value == 57) { handover = s.frameNo(); break; }
    }
    return handover;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: spawn_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";

    // ---- the shipped bitmap -------------------------------------------------
    {
        const auto shipped = omk::GameState::fromFile(iam + "/START");
        const int total = static_cast<int>(shipped.count(omk::StateArray::ObjectShown));
        int set = 0;
        for (int i = 0; i < total; ++i)
            set += shipped.bit(omk::StateArray::ObjectShown, i) ? 1 : 0;
        std::printf("start bits_set %d of %d b800 %d b801 %d b802 %d b803 %d "
                    "b804 %d b805 %d b806 %d\n",
                    set, total,
                    shipped.bit(omk::StateArray::ObjectShown, 800),
                    shipped.bit(omk::StateArray::ObjectShown, 801),
                    shipped.bit(omk::StateArray::ObjectShown, 802),
                    shipped.bit(omk::StateArray::ObjectShown, 803),
                    shipped.bit(omk::StateArray::ObjectShown, 804),
                    shipped.bit(omk::StateArray::ObjectShown, 805),
                    shipped.bit(omk::StateArray::ObjectShown, 806));
    }

    // ---- boot: AREA 118's own table ----------------------------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        omk::UiWidgets widgets = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
        if (widgets.valid()) s.attachUi(widgets);
        s.loadArea(118);
        const int sp = spawnedIn(s), at = attachedIn(s);
        const std::string ids = idsOf(s, false), slots = slotsOf(s);
        const std::size_t shownAtLoad = s.shown().size();
        s.frame();
        s.frame();
        std::printf("boot spawned %d attached %d ids %s slots %s shown %zu "
                    "after_frames_attached %d after_frames_ids %s "
                    "after_frames_shown %zu models %s\n",
                    sp, at, ids.c_str(), slots.c_str(), shownAtLoad,
                    attachedIn(s), idsOf(s, true).c_str(),
                    s.shown().size(), shownModels(s).c_str());
    }

    // ---- AREA 222 + SCENE 55 straight from START --------------------------
    //
    // `scene.load`'s only lasting effect is `Area_SetLoadedScene` (the DB's
    // SceneOfArea), which `Area_Load` reads straight back and hands to
    // `Scene_Load` - so writing it here is what the intro's `scene.load 222,
    // 55` leaves behind, without the intro. The load is then case 5 with
    // nothing else having happened.
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        state.setSceneOfArea(222, 55);
        omk::Session s(iam, state, table);
        s.loadArea(222);
        std::printf("load area %d scene %d spawned %d attached %d attached_ids %s "
                    "all_ids %s slots %s shown %zu models %s\n",
                    s.residentSlot(0).area, s.residentSlot(0).scene,
                    spawnedIn(s), attachedIn(s), idsOf(s, true).c_str(),
                    idsOf(s, false).c_str(), slotsOf(s).c_str(),
                    s.shown().size(), shownModels(s).c_str());

        const auto* d = s.characterOf(57);
        std::printf("demon id %d x %d y %d z %d facing %d bit %d attached %d "
                    "slot %d model %s bank %s\n",
                    d ? d->actor : -1,
                    d ? rounded(d->pos[0]) : 0, d ? rounded(d->pos[1]) : 0,
                    d ? rounded(d->pos[2]) : 0, d ? rounded(d->facing) : 0,
                    d ? d->bit : -1, d ? d->attached : 0, d ? d->slot : -1,
                    d && !d->model.empty() ? d->model.c_str() : "-",
                    d && !d->bank.empty() ? d->bank.c_str() : "-");

        // ---- held: the +270 the spawn cleared
        // one key per field: the line is parsed as `name value` pairs
        std::printf("held f57 %d f49 %d f212 %d\n",
                    heldFieldOf(s, 57), heldFieldOf(s, 49), heldFieldOf(s, 212));

        // ---- hide: `character.hide 57`, then `character.show 57`
        //
        // The chunks' own startup scripts are not the subject and would run
        // on the first `frame()` - SCENE 55's beats show Kay'l - so they go,
        // the way livezones_probe drops AREA 1's.
        if (s.residentSlot(0).areaCtx >= 0) s.freeContext(s.residentSlot(0).areaCtx);
        if (s.residentSlot(0).sceneCtx >= 0) s.freeContext(s.residentSlot(0).sceneCtx);
        const std::int32_t sa[3] = {1, 0, 0};
        std::vector<std::byte> h;
        emit(h, table, 79, {57});
        emit(h, table, 3, {});
        const int ih = s.newContext(0, h, sa, -1, 222);
        s.queueAction(ih, 1);
        s.frame();
        const int afterHide = attachedIn(s);
        const int bitHide = state.bit(omk::StateArray::ObjectShown, 804);
        const std::size_t shownHide = s.shown().size();
        const std::string modelsHide = shownModels(s);
        std::vector<std::byte> w;
        emit(w, table, 78, {57});
        emit(w, table, 3, {});
        const int iw = s.newContext(0, w, sa, -1, 222);
        s.queueAction(iw, 1);
        s.frame();
        std::printf("hide attached %d bit804 %d shown %zu models %s "
                    "show_attached %d show_bit804 %d show_shown %zu show_models %s\n",
                    afterHide, bitHide, shownHide, modelsHide.c_str(),
                    attachedIn(s), state.bit(omk::StateArray::ObjectShown, 804),
                    s.shown().size(), shownModels(s).c_str());
    }

    // ---- save: bit 806 set BEFORE the load ---------------------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        state.setSceneOfArea(222, 55);
        state.setBit(omk::StateArray::ObjectShown, 806, 1);
        omk::Session s(iam, state, table);
        s.loadArea(222);
        const auto* k = s.characterOf(49);
        std::printf("save bit806 1 spawned %d attached %d kayl_attached %d "
                    "kayl_model %s kayl_bank %s attached_ids %s models %s\n",
                    spawnedIn(s), attachedIn(s), k ? k->attached : -1,
                    k && !k->model.empty() ? k->model.c_str() : "-",
                    k && !k->bank.empty() ? k->bank.c_str() : "-",
                    idsOf(s, true).c_str(), shownModels(s).c_str());
    }

    // ---- the same place reached by PLAYING ---------------------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        const long handover = runToHandover(s, tb);
        // the slot holding 222; 118 stays resident in the other one
        int sl = 0;
        for (int k = 0; k < 2; ++k) if (s.residentSlot(k).area == 222) sl = k;
        int sp = 0, at = 0;
        std::string ids;
        for (const auto& c : s.residentSlot(sl).characters) {
            ++sp;
            at += c.attached ? 1 : 0;
            if (c.attached) { if (!ids.empty()) ids += ","; ids += std::to_string(c.actor); }
        }
        std::printf("impasse handover %ld area %d scene %d other %d slot222_spawned %d "
                    "slot222_attached %d attached_ids %s total_spawned %d "
                    "b804 %d b806 %d shown %zu models %s\n",
                    handover, s.currentArea(), s.residentSlot(sl).scene, s.otherArea(),
                    sp, at, ids.empty() ? "-" : ids.c_str(), spawnedIn(s),
                    state.bit(omk::StateArray::ObjectShown, 804),
                    state.bit(omk::StateArray::ObjectShown, 806),
                    s.shown().size(), shownModels(s).c_str());
    }
    return 0;
}

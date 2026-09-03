// SPDX-License-Identifier: GPL-3.0-or-later
// THE LIVE ZONES, THE WORLD HOOKS AND `end`'S FOUR THINGS, exercised in a
// real Session on the shipped data - the wave-B wiring of T12's hooks, T13's
// registry and E2's position (todo/iam-script-engine.md 7, 10, 38).
//
//     livezones_probe <gamedata> <tables>
//
// One line per fact, `key name value name value ...`:
//
//   hooks         a NON-player actor's stat edited through a Session context
//                 (AREA 1's 397: Vie 100 -> 42 read back), `object.hold.actor`
//                 handing him the prop's live slot - the slot `Scene_LoadProps`
//                 gave it at the load - and `object.release.actor` taking it
//                 back; the record's +270 both ways
//   handover      the intro run to the Impasse's hand-over with the zones
//                 registered from both slots: how many, which
//   walk          the Session's player position walked into AREA 222 zone
//                 3791: touched, armed, a context made and its ENTER script
//                 RUN (the first zone script the live replica ever executed)
//   press         the action button in 3791, which has no activate script:
//                 no activate, and `Script_Pump` step 2 posts message 26
//   leave         out of the quad: leave, free, the context gone
//   activate      a zone WITH an activate script: the press queues action 2,
//                 the script runs, `dword_4E6B20` counts it until its `end`
//   dialogue_scan the scan still runs during a conversation (Actor_Tick
//                 Dialogue -> Actor_ScanZones): a zone entered mid-dialogue
//                 takes a prompt slot at once and gets its context only when
//                 the pump runs again
//   boot          `dword_4E6C7C`, the boot startup context: set at the load,
//                 forgotten when its screen is answered with a non-zero value
//   deferred      `scene.unload` of the caller's own slot: the SCENE block
//                 KEPT (ctx+40 bit 8, the id at -1) until the caller's `end`
//   message0      how many shipped subscription records name message 0 (the
//                 marker's writer), for the record
//
// Nothing here is a reference value: the check that quotes these lines is
// what pins them, and todo/pending/T15.md has what they looked like with
// each rule reverted.
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/props.h"
#include "script/script.h"
#include "script/world.h"
#include "script/zones.h"
#include "ui/widgets.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n));
    return d;
}

// One instruction, as transition_probe lays them down: offset 0 is an `end`
// nobody runs, so a script slot of 0 still means "none".
void emit(std::vector<std::byte>& out, const omk::OpcodeTable& t, int op,
          std::initializer_list<int> fields, int trailingByte = -1) {
    if (out.empty()) out.push_back(std::byte{3});
    out.push_back(static_cast<std::byte>(op));
    const int n = t.operandLength(static_cast<std::uint8_t>(op));
    std::size_t k = 0;
    for (const int f : fields) {
        out.push_back(static_cast<std::byte>(f & 0xFF));
        out.push_back(static_cast<std::byte>((f >> 8) & 0xFF));
        k += 2;
    }
    if (trailingByte >= 0) { out.push_back(static_cast<std::byte>(trailingByte)); ++k; }
    while (static_cast<int>(k) < n) { out.push_back(std::byte{0}); ++k; }
}

// The facing the arc's centre names, in the DEGREES `Actor_ScanZones` works
// in (the loader has multiplied the stored angle by 360/4096).
float arcCentreDegrees(const omk::Zone& z) {
    return static_cast<float>(static_cast<int>(
        static_cast<double>(z.arcMid) * omk::kZoneArcToDegrees));
}

const char* kindName(omk::ZoneEvent::Kind k) {
    switch (k) {
    case omk::ZoneEvent::Kind::Touch:    return "touch";
    case omk::ZoneEvent::Kind::Arm:      return "arm";
    case omk::ZoneEvent::Kind::Activate: return "activate";
    case omk::ZoneEvent::Kind::Leave:    return "leave";
    case omk::ZoneEvent::Kind::Free:     return "free";
    }
    return "?";
}

// The zone-log entries appended by the frames since `from`.
std::string logSince(const omk::Session& s, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < s.zoneLog().size(); ++i) {
        const auto& a = s.zoneLog()[i];
        if (!out.empty()) out += ",";
        out += kindName(a.kind);
        out += ":" + std::to_string(a.zone) + ":ctx" + std::to_string(a.ctx) +
               ":a" + std::to_string(a.action) + "@" + std::to_string(a.script) +
               (a.queued ? "" : ":refused") + (a.reused ? ":reused" : "");
    }
    return out.empty() ? "-" : out;
}

int announcedOf(const omk::Session& s, const char* domain, std::size_t from) {
    for (std::size_t i = from; i < s.announced().size(); ++i)
        if (s.announced()[i].domain == domain) return s.announced()[i].value;
    return -1;
}

void stand(omk::Session& s, const omk::LiveZone* z, bool inside, bool facingIn = true) {
    double c[3];
    z->zone.centre(c);
    float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
    if (!inside) { p[0] += 1e6f; p[2] += 1e6f; }
    const float f = arcCentreDegrees(z->zone) + (facingIn ? 0.0f : 180.0f);
    s.setPlayerPosition(p, f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: livezones_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    const auto areaFile = readFile(iam + "/AREA");
    const auto areas = omk::IamArchive::open(areaFile);

    // ---- hooks: a NON-player actor through a live Session ------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        // AREA 1's prop 360 gets an object slot from `Scene_LoadProps` only
        // when its state bit 0 is set - START ships its state as 3, so the
        // load hands it the first free slot with nothing forced here
        const auto raw = areas.chunk(1);
        const auto prop = omk::findPropById(raw, omk::ChunkKind::Area, 360);
        const int stateIdx = prop ? prop->stateIndex : -1;
        const int before = stateIdx >= 0 ? state.propStateBits(stateIdx) : -1;
        omk::Session s(iam, state, table);
        s.loadArea(1);
        // the area's own startup script is not the subject: drop it
        if (s.residentSlot(0).areaCtx >= 0) s.freeContext(s.residentSlot(0).areaCtx);
        if (s.residentSlot(0).sceneCtx >= 0) s.freeContext(s.residentSlot(0).sceneCtx);
        const int actor = 397;
        const auto recOf = [&]() -> std::span<const std::byte> {
            const auto& ch = s.residentSlot(0).areaChunk;
            const auto o = omk::findActorRecord(ch, omk::ChunkKind::Area, actor);
            if (!o) return {};
            return std::span<const std::byte>(ch).subspan(*o, omk::kActorRecordSize);
        };
        int propSlot = -1;
        {
            const auto& ch = s.residentSlot(0).areaChunk;
            const auto p = omk::findPropById(ch, omk::ChunkKind::Area, 360);
            if (p) propSlot = p->slot;                     // +0, as Scene_LoadProps wrote it
        }
        std::vector<std::byte> a;
        emit(a, table, 86, {actor, 1, 5});                 // Vie -> var 5
        emit(a, table, 14, {6}, 42);                       // set.var.i8 6 = 42
        emit(a, table, 93, {actor, 1, 6});                 // actor.stat.set Vie = var 6
        emit(a, table, 86, {actor, 1, 7});                 // Vie -> var 7
        emit(a, table, 67, {actor, 360});                  // object.hold.actor
        emit(a, table, 3, {});
        const std::int32_t sa[3] = {1, 0, 0};
        const int ia = s.newContext(0, a, sa, -1, 1);
        s.queueAction(ia, 1);
        s.frame();
        const int heldField = recOf().empty() ? -99 : omk::heldObjectOf(recOf());
        const int heldSlot = s.heldSlotOf(actor);
        int holds = 0, drops = 0;
        for (const auto& e : s.propEvents()) { holds += !std::strcmp(e.what, "hold"); drops += !std::strcmp(e.what, "drop"); }
        std::vector<std::byte> b;
        emit(b, table, 69, {actor});                       // object.release.actor
        emit(b, table, 3, {});
        const int ib = s.newContext(0, b, sa, -1, 1);
        s.queueAction(ib, 1);
        s.frame();
        for (const auto& e : s.propEvents()) drops += !std::strcmp(e.what, "drop");
        std::printf("hooks area 1 actor %d prop 360 prop_bit_before %d slot %d slot_id %d "
                    "vie_shipped %d vie_set %d held_field %d held_slot %d hold_events %d "
                    "release_field %d release_slot %d drop_events %d ctx_status %d\n",
                    actor, before, propSlot, s.objectSlotId(propSlot), state.var(5),
                    state.var(7), heldField, heldSlot, holds,
                    recOf().empty() ? -99 : omk::heldObjectOf(recOf()), s.heldSlotOf(actor),
                    drops, s.contextStatus(ia));
    }

    // ---- the intro to the Impasse's hand-over, then a walk ------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        omk::UiWidgets widgets = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
        if (widgets.valid()) s.attachUi(widgets);
        s.loadArea(118);
        const int bootAtLoad = s.bootContext();
        s.frame();                                       // ui.open, and the DERIVED answer 1
        const int bootAnswered = s.bootContext();
        s.frame();
        long handoverFrame = -1;
        int dialogs = 0;
        for (int f = 0; f < 400 && handoverFrame < 0; ++f) {
            s.frame();
            if (s.dialogOpen()) { ++dialogs; s.endDialog(); }
            // SCENE 55's script hands over with `scene.load 237, 57`
            for (const auto& an : s.announced())
                if (an.domain == "SCENES" && an.value == 57) { handoverFrame = s.frameNo(); break; }
        }
        std::string live;
        for (const auto& z : s.zones().registered()) {
            if (!live.empty()) live += ",";
            live += std::to_string(z.zone.id);
        }
        std::printf("handover frame %ld dialogs %d entered %d current %d other %d "
                    "scene %d records %zu registered %zu live %s armed %d walks %d\n",
                    handoverFrame, dialogs, s.areasEntered(), s.currentArea(), s.otherArea(),
                    s.residentSlot(s.shownSlot()).scene, s.zones().all().size(),
                    s.zones().registered().size(), live.c_str(), s.zones().armedCount(),
                    static_cast<int>(s.zoneLog().size()));
        std::printf("boot at_load %d answered %d\n", bootAtLoad, bootAnswered);

        // ---- walk: into 3791, facing in
        const auto* z3791 = s.zones().resolve(3791);
        if (!z3791) { std::printf("walk zone 3791 NOT RESIDENT\n"); return 1; }
        const auto logAt = s.zoneLog().size();
        const auto annAt = s.announced().size();
        stand(s, z3791, true);
        s.frame();                                       // the scan: touch + arm (slot state 1)
        const int armedAfterScan = s.zones().armedCount();
        const int touches1 = s.zones().touches();
        const auto logMid = s.zoneLog().size();
        s.frame();                                       // the pump: context, enter script RUN
        int ctx = -1;
        for (auto i = logAt; i < s.zoneLog().size(); ++i)
            if (s.zoneLog()[i].kind == omk::ZoneEvent::Kind::Arm) ctx = s.zoneLog()[i].ctx;
        std::printf("walk zone 3791 touches %d armed_after_scan %d events_after_scan %zu "
                    "events %s ctx %d zone_of_ctx %d status %d announced %zu first %s:%d "
                    "flags %d\n",
                    touches1, armedAfterScan, logMid - logAt, logSince(s, logAt).c_str(), ctx,
                    s.contextZone(ctx), s.contextStatus(ctx), s.announced().size() - annAt,
                    s.announced().size() > annAt ? s.announced()[annAt].domain.c_str() : "-",
                    s.announced().size() > annAt ? s.announced()[annAt].value : -1,
                    s.contextFlags(ctx));

        // ---- press: 3791 has no activate script -> message 26
        const auto msgAt = s.messagesRun().size();
        const auto nhAt = s.nothingHere().size();
        const bool accepted = s.pressAction();
        s.frame();
        std::printf("press accepted %d activates %d nothing_here %zu message %d table %s "
                    "pending %d ran_frame %ld\n",
                    accepted ? 1 : 0,
                    static_cast<int>(std::count_if(s.zoneLog().begin(), s.zoneLog().end(),
                        [](const omk::Session::ZoneApplied& a) { return a.kind == omk::ZoneEvent::Kind::Activate; })),
                    s.nothingHere().size() - nhAt,
                    s.messagesRun().size() > msgAt ? s.messagesRun()[msgAt].message : -1,
                    s.messagesRun().size() > msgAt ? s.messagesRun()[msgAt].table.c_str() : "-",
                    s.activatesPending(),
                    s.messagesRun().size() > msgAt ? s.messagesRun()[msgAt].ranFrame : -1);
        // a press with NOTHING armed never reaches the pump (event 6's gate)
        // - tested after the leave below

        // ---- leave: out of the quad
        const auto leaveAt = s.zoneLog().size();
        stand(s, z3791, false);
        s.frame();                                       // the scan finds nothing
        s.frame();                                       // the pump: leave + free
        const bool refused = !s.pressAction();
        s.frame();
        std::printf("leave events %s ctx_status %d armed %d press_refused %d nothing_here %zu\n",
                    logSince(s, leaveAt).c_str(), s.contextStatus(ctx), s.zones().armedCount(),
                    refused ? 1 : 0, s.nothingHere().size() - nhAt);

        // ---- (no zone of the Impasse has an activate script: 3790, 3791,
        // 3795, 3799, 3801 and SCENE 55's 3803 are enter-only, so every press
        // in the alley is a "nothing here" - the activate is proved on AREA
        // 146 below)

        // ---- deferred: scene.unload of the caller's own slot
        {
            int slot222 = -1;
            for (int k = 0; k < 2; ++k) if (s.residentSlot(k).area == 222) slot222 = k;
            const int sceneBefore = slot222 >= 0 ? s.residentSlot(slot222).scene : -2;
            const auto& cams = s.residentSlot(slot222 >= 0 ? slot222 : 0).cams.area();
            const int camId = cams.empty() ? -1 : cams.front().id;
            s.setCameraWait(true);
            std::vector<std::byte> u;
            emit(u, table, 72, {222});                   // scene.unload 222
            emit(u, table, 96, {camId, 30});             // camera.set.wait: parks 30 frames
            emit(u, table, 3, {});
            const std::int32_t su[3] = {1, 0, 0};
            const int iu = s.newContext(slot222 >= 0 ? slot222 : 0, u, su, -1, 222);
            s.queueAction(iu, 1);
            s.frame();
            const int sceneDuring = s.residentSlot(slot222).scene;
            const std::size_t keptBytes = s.residentSlot(slot222).sceneChunk.size();
            const int flagsDuring = s.contextFlags(iu);
            const int statusDuring2 = s.contextStatus(iu);
            const int freedDuring = s.sceneBlocksFreedAtEnd();
            int fr2 = 0;
            while (s.contextStatus(iu) > 0 && fr2 < 100) { s.frame(); ++fr2; }
            std::printf("deferred slot %d scene_before %d scene_during %d kept_bytes %zu flags %d "
                        "status %d freed_during %d frames %d kept_after %zu freed_after %d db %d\n",
                        slot222, sceneBefore, sceneDuring, keptBytes, flagsDuring, statusDuring2,
                        freedDuring, fr2, s.residentSlot(slot222).sceneChunk.size(),
                        s.sceneBlocksFreedAtEnd(), state.sceneOfArea(222));
        }
    }

    // ---- activate: AREA 146's one-shot zone 35141 (-30395 as an int16),
    // enter 5167 / activate 5191 / leave 5230 - T13's scenario 3, now on a
    // live Session with real contexts ---------------------------------------
    {
        auto state = omk::GameState::fromFile(iam + "/START");
        omk::Session s(iam, state, table);
        s.loadAnnounceMap(tb + "/vm_announce.json");
        s.loadArea(146);
        if (s.residentSlot(0).areaCtx >= 0) s.freeContext(s.residentSlot(0).areaCtx);
        if (s.residentSlot(0).sceneCtx >= 0) s.freeContext(s.residentSlot(0).sceneCtx);
        const auto* za = s.zones().resolve(static_cast<std::int16_t>(-30395));
        if (!za) { std::printf("activate zone -30395 NOT RESIDENT\n"); return 1; }
        const auto actAt = s.zoneLog().size();
        const auto annA = s.announced().size();
        stand(s, za, true);
        s.frame();                                       // scan: arm
        s.frame();                                       // pump: context + enter
        int ctxA = -1;
        for (auto i = actAt; i < s.zoneLog().size(); ++i)
            if (s.zoneLog()[i].kind == omk::ZoneEvent::Kind::Arm) ctxA = s.zoneLog()[i].ctx;
        const int enterStatus = s.contextStatus(ctxA);
        const auto annEnter = s.announced().size() - annA;
        s.pressAction();
        s.frame();                                       // pump: activate queued, then RUN
        const int pendingDuring = s.activatesPending();
        const bool dlg = s.dialogOpen();
        const int dlgId = announcedOf(s, "DIALOGS", annA);
        const int statusDuring = s.contextStatus(ctxA);
        const int flagsDuring = s.contextFlags(ctxA);
        const int slotState = [&]() {
            for (const auto& ps : s.zones().promptSlots())
                if (ps.zone == za->zone.id) return ps.state;
            return -1;
        }();
        // a second press while the first activate is unfinished: the one-shot
        // slot is latched (4/5) and never re-enters state 2, so NO event
        s.pressAction();
        s.frame();
        int activates = 0, refusedActivates = 0;
        for (auto i = actAt; i < s.zoneLog().size(); ++i)
            if (s.zoneLog()[i].kind == omk::ZoneEvent::Kind::Activate) {
                ++activates;
                if (!s.zoneLog()[i].queued) ++refusedActivates;
            }

        std::string actAnn;
        for (auto i = annA + annEnter; i < s.announced().size() && actAnn.size() < 120; ++i)
            actAnn += (actAnn.empty() ? "" : ",") + s.announced()[i].domain + ":" +
                      std::to_string(s.announced()[i].value);

        // ---- dialogue_scan: while a conversation is up, walk into the
        // neighbouring zone 2374 (enter 5254): the scan runs from
        // Actor_TickDialogue, the pump does not. No zone here opens one, so
        // a synthetic context does (`dialog.start 272`).
        int armedDuring = -1, ctxDuring = -2, ctxAfterClose = -2, logGrew = -1;
        const auto* zb = s.zones().resolve(2374);
        bool dlgUp = dlg;
        if (!dlgUp) {
            std::vector<std::byte> d;
            emit(d, table, 61, {272});
            emit(d, table, 3, {});
            const std::int32_t sd[3] = {1, 0, 0};
            const int id = s.newContext(0, d, sd, -1, 146);
            s.queueAction(id, 1);
            s.frame();
            dlgUp = s.dialogOpen();
        }
        if (dlgUp && zb) {
            const auto logBefore = s.zoneLog().size();
            stand(s, zb, true);
            s.frame();                                   // a dialogue frame
            armedDuring = s.zones().armedCount();
            ctxDuring = -1;
            for (int k = 0; k < 16; ++k)
                if (s.promptContext(k) >= 0 && s.contextZone(s.promptContext(k)) == 2374) ctxDuring = s.promptContext(k);
            logGrew = static_cast<int>(s.zoneLog().size() - logBefore);
            s.endDialog();
            s.frame();                                   // the pump runs again
            ctxAfterClose = -1;
            for (int k = 0; k < 16; ++k)
                if (s.promptContext(k) >= 0 && s.contextZone(s.promptContext(k)) == 2374) ctxAfterClose = s.promptContext(k);
        }
        int frames = 0;
        while (s.contextStatus(ctxA) > 0 && frames < 400) {
            s.frame();
            if (s.dialogOpen()) s.endDialog();
            ++frames;
        }
        std::printf("activate zone %d enter %d script %d leave %d ctx %d enter_status %d "
                    "enter_announced %zu events %s pending_during %d dialog %d dialog_id %d "
                    "status_during %d flags %d slot_state %d activates %d refused %d "
                    "pending_after %d status_after %d frames %d nothing_here %zu announced %s\n",
                    za->zone.id, za->zone.scripts[0], za->zone.scripts[1], za->zone.scripts[2],
                    ctxA, enterStatus, annEnter, logSince(s, actAt).substr(0, 240).c_str(),
                    pendingDuring, dlg ? 1 : 0, dlgId, statusDuring, flagsDuring, slotState,
                    activates, refusedActivates, s.activatesPending(), s.contextStatus(ctxA),
                    frames, s.nothingHere().size(), actAnn.empty() ? "-" : actAnn.c_str());
        std::printf("dialogue_scan dialog %d zone 2374 armed_during %d ctx_during %d "
                    "ctx_after_close %d log_grew %d\n", dlgUp ? 1 : 0, armedDuring, ctxDuring,
                    ctxAfterClose, logGrew);
    }

    // ---- message0: who writes the marker ----------------------------------
    {
        const auto sceneFile = readFile(iam + "/SCENE");
        const auto scenes = omk::IamArchive::open(sceneFile);
        int chunks = 0;
        for (std::size_t i = 0; i < areas.size(); ++i)
            for (const auto& su : omk::chunkSubscriptions(areas.chunk(i), omk::ChunkKind::Area))
                if (su.message == 0) { ++chunks; break; }
        for (std::size_t i = 0; i < scenes.size(); ++i)
            for (const auto& su : omk::chunkSubscriptions(scenes.chunk(i), omk::ChunkKind::Scene))
                if (su.message == 0) { ++chunks; break; }
        int globalScript = -1;
        const auto g = readFile(iam + "/GLOBAL");
        for (const auto& su : omk::globalSubscriptions(g))
            if (su.message == 0) globalScript = static_cast<int>(su.script);
        std::printf("message0 chunks_subscribed %d global_script %d\n", chunks, globalScript);
    }
    return 0;
}

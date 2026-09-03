// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/area.h"

#include "platform/datafs.h"
#include "actor/pose.h"
#include "script/scenehost.h"

#include "script/world.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace omk {
namespace {

std::uint32_t u32at(std::span<const std::byte> b, std::size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) |
           (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::int16_t i16at(std::span<const std::byte> b, std::size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[o]) |
                                     (static_cast<std::uint16_t>(b[o + 1]) << 8));
}

// Write bytes into the game DB through `GameState::rawMutable()` - the
// writable view added for a whole-record copy the field setters do not name.
// The two callers are `becomePlayer`'s bio and record copies.
void dbWrite(GameState& s, std::size_t off, const void* src, std::size_t n) {
    const auto sp = s.rawMutable();
    if (off + n > sp.size()) return;
    std::memcpy(sp.data() + off, src, n);
}

std::vector<std::byte> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n));
    return d;
}

// the game DB's per-area scene table, +12, int16 an entry, -1 for none
std::int16_t sceneOverArea(const GameState& s, int area) {
    const auto raw = s.raw();
    const auto tbl = s.offset(StateArray::SceneOfArea);
    const auto off = static_cast<std::size_t>(tbl) + 2u * static_cast<std::size_t>(area);
    if (area < 0 || off + 2 > raw.size()) return -1;
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(raw[off]) |
        static_cast<std::uint16_t>(raw[off + 1]) << 8);
}

// A NUL-terminated fixed field in a chunk header, as the loaders read them.
std::string headerName(std::span<const std::byte> b, std::size_t off,
                       std::size_t max) {
    if (b.size() < off + max) return {};
    std::string s;
    for (std::size_t i = 0; i < max; ++i) {
        const auto c = static_cast<char>(b[off + i]);
        if (!c) break;
        s.push_back(c);
    }
    return s;
}

std::vector<std::byte> readChunk(const std::string& iam, const char* archive, int id) {
    if (id < 0) return {};
    const auto file = readFile(iam + "/" + archive);
    const auto arch = IamArchive::open(file);
    const auto c = arch.chunk(static_cast<std::size_t>(id));
    return std::vector<std::byte>(c.begin(), c.end());
}

// `Script_ProcessActions`' transition watchdog: 60 s, in frames at 30 fps.
constexpr long kWatchdogFrames = 60 * 30;
constexpr std::size_t kRunaway = 20000;

}  // namespace

std::vector<StartupRun> loadArea(const std::string& iamDir, int areaId,
                                 GameState& state, const OpcodeTable& table) {
    std::vector<StartupRun> out;
    const auto areaFile  = readFile(iamDir + "/AREA");
    const auto sceneFile = readFile(iamDir + "/SCENE");
    const auto areas  = IamArchive::open(areaFile);
    const auto scenes = IamArchive::open(sceneFile);

    const auto ab = areas.chunk(static_cast<std::size_t>(areaId));
    if (ab.empty()) return out;

    // the AREA block first, then the SCENE the game DB says is over it
    struct Pending { bool isScene; int chunk; std::span<const std::byte> code; };
    std::vector<Pending> order;
    order.push_back({false, areaId, ab});

    const auto scene = sceneOverArea(state, areaId);
    if (scene != -1) {
        const auto sb = scenes.chunk(static_cast<std::size_t>(scene));
        if (!sb.empty()) order.push_back({true, scene, sb});
    }

    for (const auto& p : order) {
        const auto at = startupScript(p.code);
        if (at == 0) continue;
        Interpreter vm(state, table);
        // The free `loadArea` is the corpus sweep's entry point, not the
        // game's - it runs each startup script standalone to compare with
        // `tools/sim`, whose VM stubs opcode 70. Parking at `ui.open` here
        // would make the two disagree about something neither models. The
        // Session's own `loadArea` below leaves the park on, because that one
        // really runs the game.
        vm.setUiOpenSuspends(false);
        vm.setRecordCalls(true);
        const auto r = vm.run(p.code, at);
        StartupRun s;
        s.isScene = p.isScene;
        s.chunk   = p.chunk;
        s.offset  = at;
        s.status  = r.status;
        s.calls   = r.calls;
        out.push_back(std::move(s));
    }
    return out;
}

// ------------------------------------------------------------- the session

bool Session::loadAnnounceMap(const std::string& jsonPath) {
    announce_.clear();
    std::ifstream f(jsonPath);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string s = ss.str();
    // the same small scan the opcode table uses: the file's shape is fixed by
    // tools/exetables.py, and one table does not justify a JSON dependency
    std::size_t i = 0;
    while ((i = s.find("\"op\":", i)) != std::string::npos) {
        i += 5;
        const int op = std::atoi(s.c_str() + i);
        const auto dp = s.find("\"domain\":", i);
        const auto fp = s.find("\"field\":", i);
        if (dp == std::string::npos || fp == std::string::npos) break;
        const auto q1 = s.find('"', dp + 9);
        const auto q2 = (q1 == std::string::npos) ? q1 : s.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        const std::string domain = s.substr(q1 + 1, q2 - q1 - 1);
        const int field = std::atoi(s.c_str() + fp + 8);
        if (op >= 0 && op < 256 && !domain.empty())
            announce_[static_cast<std::uint8_t>(op)] = Ann{domain, field};
        i = fp;
    }
    return !announce_.empty();
}

void Session::record(const std::vector<Call>& calls) {
    for (const auto& c : calls) {
        const auto it = announce_.find(c.op);
        // no entry means the handler announces NOTHING - silence, not a
        // default. 104 of the 153 opcodes are silent.
        if (it == announce_.end()) continue;
        const auto f = static_cast<std::size_t>(it->second.field);
        if (f < c.fields.size())
            announced_.push_back({it->second.domain, c.fields[f], c.op});
    }
}

// ------------------------------------------------------- THE RESIDENT SLOTS

void Session::fillSlotTables(ResidentSlot& s) {
    // `Area_LoadSet` builds `MESHES\DECORS\%s.3DO` from the AREA header's
    // `+88`; `+97` is the `.SCX` stem. Nine bytes each, NUL-terminated. `+142`
    // is the area's own music track, which event 9 plays on entering.
    s.set = headerName(s.areaChunk, 88, 9);
    s.scx = headerName(s.areaChunk, 97, 9);
    s.music = i16at(s.areaChunk, 142);
    s.cams.clearChunk();
    if (!s.areaChunk.empty()) {
        s.cams.setArea(s.areaChunk);
        // ...and the chunk's ADDRESSES, which is what `actor.goto_address`
        // resolves against.
        s.addresses = readAddresses(s.areaChunk);
    } else {
        s.addresses.clear();
    }
    // `Camera_FindWorld` walks the SCENE table only `if (v1[3] >= 0)` - the
    // scene ID, not the block: a block kept for a deferred free (ctx+40 bit
    // 8) is invisible to it.
    if (s.scene != -1 && !s.sceneChunk.empty()) s.cams.setScene(s.sceneChunk);
    if (s.cams.global().empty()) {
        // The shared table, loaded once per slot: `Camera_FindWorld` falls
        // back on it when neither resident chunk has the id.
        const auto g = readFile(iam_ + "/GLOBAL");
        if (!g.empty()) s.cams.setGlobal(g);
    }
}

int Session::slotOf(int area) const {
    if (area < 0) return -1;
    for (int s = 0; s < 2; ++s) if (slots_[s].area == area) return s;
    return -1;
}

int Session::loadSlicesFor(int area) const {
    // `o3de_LoadScene` (18_d3d.c): `sub_41F060(path)` opens the set and
    // returns its SIZE, `sub_41EC20(buf, size, Scene_InitAndLoadTextures,
    // scene)` queues one read of the whole file, and `sub_41F320` serves it
    // `ElementSize` bytes a frame - 0x20000 after `Async_SetMode(1)`. A set
    // that does not resolve makes `sub_41F060` return 0 and the loader fail;
    // nothing is queued and `sub_41EFA0` passes at once.
    std::string set;
    const int s = slotOf(area);
    if (s >= 0) set = slots_[s].set;
    else set = headerName(readChunk(iam_, "AREA", area), 88, 9);
    if (set.empty()) return 0;
    // the data root is the parent of the IAM directory
    std::string root = iam_;
    while (!root.empty() && root.back() == '/') root.pop_back();
    const auto cut = root.find_last_of('/');
    if (cut != std::string::npos) root = root.substr(0, cut);
    else root = ".";
    const DataFs fs(root);
    const auto real = fs.resolve("MESHES/DECORS/" + set + ".3DO");
    if (!real) return 0;
    std::error_code ec;
    const auto n = std::filesystem::file_size(*real, ec);
    if (ec || n == 0) return 0;
    return static_cast<int>((n + sliceBytes_ - 1) / sliceBytes_);
}

// `Area_Load` (0x0040CC90): the AREA chunk read and relocated, its
// coordinates converted, `dword_69BC48[slot] = area`, the SCENE chunk the DB
// names loaded over it (`Scene_Load(slot, v35)`), then `dword_4E6D8C = 1` and
// ONE `Area_TickLoad(slot)` - which runs case 1 (the set requested) and stalls
// at case 2 unless the async reader is in mode 0, in which case every case
// runs here and the load is done when this returns.
void Session::areaLoad(int area, int slot) {
    ResidentSlot& s = slots_[slot & 1];
    s = ResidentSlot{};
    s.area = area;
    s.areaChunk = readChunk(iam_, "AREA", area);
    s.scene = sceneOverArea(state_, area);
    if (s.scene != -1) {
        s.sceneChunk = readChunk(iam_, "SCENE", s.scene);
        if (s.sceneChunk.empty()) s.scene = -1;
    }
    fillSlotTables(s);
    load_.active = true;
    load_.slot = slot & 1;
    load_.slicesLeft = asyncMode_ ? loadSlicesFor(area) : 0;
    tickLoad(slot & 1);
}

// `Area_LoadIntoSlot` (0x00402B70).
void Session::loadIntoSlot(int slot, int area) {
    slot &= 1;
    if (slots_[slot].area == area) {
        // `if (dword_69BC48[4 * a1] == a2) return sub_41D380(a2, block + 144)`
        // - the fog block refreshed, and NOTHING else: no `Area_Load`, so
        // `dword_4E6D8C` stays 0, `Area_TickLoad` returns 1 without reaching
        // case 9 and the startup contexts are not created again. Every
        // A -> B -> A return takes this branch (issue 17).
        return;
    }
    if (slots_[slot].area != -1) {
        // `sub_40C090(slot)`: free the slot's SCENE and AREA startup contexts
        // and (`sub_406320`) every context created with that slot number;
        // `sub_40D4A0(slot)`: its actors. Then the ids go to -1.
        evictSlot(slot);
    }
    zonesRegisterAll();
    // `Music_SetFadeMode(1)` - really `Async_SetMode(1)`: the set streams at
    // 0x20000 bytes a frame from here until case 2 sets mode 0 again.
    asyncMode_ = 1;
    areaLoad(area, slot);
}

void Session::evictSlot(int slot) {
    ResidentSlot& s = slots_[slot & 1];
    if (s.sceneCtx >= 0) freeContext(s.sceneCtx);
    if (s.areaCtx >= 0) freeContext(s.areaCtx);
    for (int i = 0; i < kContextSlots; ++i)
        if (ctxs_[static_cast<std::size_t>(i)] &&
            ctxs_[static_cast<std::size_t>(i)]->slot == (slot & 1))
            freeContext(i);
    // `sub_40D4A0(slot)`: its actors - and the props with them, the object
    // slots handed back (`Scene_UnloadProps`).
    freeActorSlots(slot & 1, true, true);
    unloadProps(slot & 1, true, true);
    s = ResidentSlot{};
    rebuildShown();
}

// `Area_TickLoad` (0x0040C7E0). -> 1 when nothing is loading in the slot (a
// block is there), 0 while the set is still streaming or the slot is empty.
bool Session::tickLoad(int slot) {
    slot &= 1;
    if (slots_[slot].areaChunk.empty()) return false;     // `if (!v1) return 0`
    if (!load_.active) return true;                       // dword_4E6D8C == 0
    if (load_.slot != slot) return false;
    // cases 2..8: `if (!sub_41EFA0()) return 0` - the queued read of the set
    // is not served yet. One slice a frame; `frame()` serves them.
    if (load_.slicesLeft > 0) return false;
    completeLoad(slot);
    return true;
}

// Cases 2..9 of `Area_TickLoad`, all in one call once the set has arrived:
// `.SCX`, map, misc model, actors, props, `.ani`, slider track, then the
// startup contexts and the zones. Case 2 also sets the async reader back to
// mode 0.
void Session::completeLoad(int slot) {
    asyncMode_ = 0;
    ResidentSlot& s = slots_[slot & 1];
    // case 9: `Script_NewContext(slot, block[+4], 0, 0)`, `*block = ctx`,
    // `Script_QueueAction(ctx, 1)` for the AREA, then the SCENE the DB names.
    // The active slot and area are set to this slot for the duration and
    // RESTORED after - the switch is event 9's, not the loader's.
    // case 5 before it: `Actors_SpawnFromTables(area, scene, 1)` - the
    // world's characters placed and the set ones attached; then case 5/6,
    // `Scene_LoadProps(area, scene, 1)` - the object slots handed out and
    // the prop records' +0 written. The order is the engine's.
    spawnFromTables(slot & 1, true, true);
    loadProps(slot & 1, true, true);
    s.areaCtx = queueStartup(slot & 1, false);
    if (s.scene != -1 && !s.sceneChunk.empty())
        s.sceneCtx = queueStartup(slot & 1, true);
    s.loaded = true;
    load_.active = false;
    zonesRegisterAll();
}

int Session::loadArea(int areaId) {
    // `Game_NewGame`: both slots reset, slot 0 active, no area. Then
    // `State_Apply`'s `Area_Load(START +1414, 0)` with the async reader in
    // mode 0 - the boot load is synchronous, and its startup contexts are in
    // the table when this returns.
    slots_[0] = ResidentSlot{};
    slots_[1] = ResidentSlot{};
    active_ = 0;
    activeArea_ = -1;
    curSlot_ = 0;
    asyncMode_ = 0;
    playerAnimHeld_ = false;                 // a new animation instance
    resetWorld();
    areaLoad(areaId, 0);
    if (slots_[0].areaChunk.empty()) return 0;
    // case 9 at boot: `dword_69BC64 = v3` and v17 == -1 keeps it
    activeArea_ = areaId;
    showSet(areaId);
    // `Game_NewGame` / `sub_40E170`, right after `State_Apply`:
    //     dword_4E6C7C = u32(dword_69BC40, 0)[4 * dword_69BC60]
    // - the active slot's AREA block +0, the boot startup context.
    bootCtx_ = slots_[0].areaCtx;
    // `State_Apply` ends `dword_69BC60 = 1; Game_RaiseEvent(9, the player's
    // decor)` - which toggles back to slot 0 only when slot 1 is occupied,
    // and it is not at boot; the area's own music (`+142`) plays if it has
    // one. 118's is 0.
    if (slots_[0].music && slots_[0].music != musicTrack_) {
        musicTrack_ = slots_[0].music;
        musicLoop_ = true;
    }
    int n = 0;
    if (slots_[0].areaCtx >= 0) ++n;
    if (slots_[0].sceneCtx >= 0) ++n;
    return n;
}

void Session::requestArea(int area) {
    // A script's transition without objects is `area.goto X -1 -1 ... area.arrive
    // <the area left>` (262 shipped `area.arrive` sites end one), and mode 0
    // at state 8 - loaded, resumed, not yet arrived - accepts a second goto
    // and does NOTHING. So a probe request that follows another stands in for
    // the script's own `area.arrive` first.
    if (tr_.state == 8) areaTransition(1, kNoContext, active_, tr_.outArea, -1, -1);
    areaTransition(0, kNoContext, curSlot_, area, -1, -1);
}

// sub_419AF0: the decor slot holding `area` into state 2 (`o3de_InsertScene`).
// It does NOT hide the other slot: both are drawn until something calls
// `hideSet` (area.h, `slotShown`).
void Session::showSet(int area) {
    const int s = slotOf(area);
    if (s < 0) return;
    slots_[s].shown = true;
    curSlot_ = s;
}

// sub_419A90: back to state 1 (`sub_441200`) - loaded, not drawn. The
// single-set readers (`setName()`) follow the other slot when the shown one
// is hidden under them.
void Session::hideSet(int area) {
    const int s = slotOf(area);
    if (s < 0) return;
    slots_[s].shown = false;
    if (curSlot_ == s && slots_[1 - s].area != -1) curSlot_ = 1 - s;
}

// `Game_HandleEvent` case 9.
void Session::playerOnArea(int area) {
    activeArea_ = area;                                   // dword_69BC64 = a2
    const int other = 1 - active_;
    if (slots_[other].area == -1) return;                 // the other row is empty
    // The raisers (`Walk_ProbeGround`, `sub_459AA0`) fire on a CHANGE of the
    // decor under the player, so the toggle is unconditional there; here a
    // caller may raise it for the set he is already on, and that is not a
    // change.
    if (slots_[active_].area == area) return;
    active_ = other;                                      // dword_69BC60 = 1 - dword_69BC60
    zonesRegisterAll();
    // the area's own track: `if (+142 && +142 != g_MusicTrack) Music_PlayTrack(+142, 1)`
    const ResidentSlot& s = slots_[active_];
    if (s.music && s.music != musicTrack_) {
        musicTrack_ = s.music;
        musicLoop_ = true;
    }
}

void Session::setPlayerPosition(const float pos[3], float yaw) {
    for (int k = 0; k < 3; ++k) playerPos_[k] = pos[k];
    playerYaw_ = yaw;
    playerPlaced_ = true;
    playerWalks_ = true;
}

// `Zones_RegisterAll` (0x00406560): the four-table walk over both rows -
// `if (blocks[2] != -1 && blocks[0])` the AREA's, `if (blocks[3] != -1 &&
// blocks[1])` the SCENE's, both guards the id AND the block - then the tail:
//
//     for (ctx : the 32 slots)
//         if (ctx && u16(ctx,42) != -1 && !Zone_FindScriptsById(u16(ctx,42))) {
//             for (ps : the 16 prompt slots)
//                 if (u32(ps,0) == ctx) { u32(ps,0)=0; ps[5]=-1; ps[4]=0; --dword_4E6B24; }
//             Mem_Free(...); dword_4E61E8[u8(ctx,30)] = 0;
//         }
//
// The registry does the prompt-slot half (a slot whose zone no longer
// resolves is released - `detached()`); the contexts are freed here.
void Session::zonesRegisterAll() {
    // which prompt slot held which zone, before the registry releases any
    std::map<std::int16_t, int> slotOfZone;
    for (int k = 0; k < 16; ++k) {
        const auto& ps = zones_.promptSlots()[static_cast<std::size_t>(k)];
        if (ps.zone != -1) slotOfZone[ps.zone] = k;
    }
    std::vector<omk::ResidentSlot> rows;      // zones.h's row, not Session's
    for (const auto& s : slots_) {
        omk::ResidentSlot r;
        r.areaId  = s.area;
        r.sceneId = s.scene;
        r.area    = {s.areaChunk.data(), s.areaChunk.size()};
        r.scene   = {s.sceneChunk.data(), s.sceneChunk.size()};
        rows.push_back(r);
    }
    zones_.registerAll(rows, state_);
    for (const auto z : zones_.detached()) {
        const auto it = slotOfZone.find(z);
        if (it != slotOfZone.end()) promptCtx_[static_cast<std::size_t>(it->second)] = -1;
    }
    // the prune: a context whose zone id no longer resolves is freed
    for (int i = 0; i < kContextSlots; ++i) {
        const auto& c = ctxs_[static_cast<std::size_t>(i)];
        if (!c || c->zoneId == -1) continue;
        const bool ok = zones_.resolve(static_cast<std::int16_t>(c->zoneId)) != nullptr &&
                        (!zoneResolves_ || zoneResolves_(c->zoneId));
        if (ok) continue;
        if (const char* e = std::getenv("OMK_CAMLOG"))
            if (*e == '1')
                std::fprintf(stderr, "[zone] frame %ld  context %d of zone %d PRUNED\n",
                             frameNo_, i, c->zoneId);
        freeContext(i);
    }
    if (onZonesRegister_) onZonesRegister_();
}

void Session::resetWorld() {
    // `sub_406730`: the 16 prompt slots to -1/0, `dword_4E6B24 = 0`,
    // `dword_4E6B20 = 0`; `memset(word_4E6CA0, 0xFF, 100)`.
    zones_.registerAll({}, state_);
    promptCtx_.fill(-1);
    activatesPending_ = 0;
    actionPressed_ = false;
    objectSlotIds_.fill(-1);
    // `word_69BC80` alongside it: 100 int16 of -1, the actor runtime slots
    // `Actors_SpawnFromTables` hands out.
    actorSlots_.fill(-1);
    heldSlot_.clear();
    shownSlots_.clear();
    message0Ctx_ = -1;
    bootCtx_ = -1;
    playerDriven_ = false;
    playerWalks_ = false;
}

// ------------------------------------------------------- THE CONTEXT TABLE

int Session::newContext(int slot, std::span<const std::byte> chunk,
                        const std::int32_t scripts[3], int zoneId, int area) {
    int k = -1;
    for (int i = 0; i < kContextSlots; ++i)
        if (!ctxs_[static_cast<std::size_t>(i)]) { k = i; break; }
    if (k < 0) {
        // `Script_NewContext`'s loop runs off the table and returns the block
        // UNLISTED: the engine allocated it and will never run it.
        ++unlisted_;
        return -1;
    }
    auto c = std::make_unique<Ctx>(state_, table_);
    c->code.assign(chunk.begin(), chunk.end());
    for (int j = 0; j < 3; ++j) c->scripts[j] = scripts ? scripts[j] : 0;
    c->pc = c->scripts[0] > 0 ? static_cast<std::size_t>(c->scripts[0]) : 0;
    c->slot = slot & 1;
    c->zoneId = zoneId;
    c->chunkArea = area;
    c->vm.setCameraWaitSuspends(camWait_);
    c->vm.setObjectWaitSuspends(objWait_);
    c->vm.setMoveWaitSuspends(static_cast<bool>(moveHook_));
    c->vm.setFightWaitSuspends(static_cast<bool>(fightHook_));
    // the world beyond the DB (hooks.h): the resident blocks, the object
    // slots, the held objects - the same for every context
    c->vm.setHooks(&hooks_);
    ctxs_[static_cast<std::size_t>(k)] = std::move(c);
    ++created_;
    return k;
}

bool Session::queueAction(int idx, int action) {
    if (idx < 0 || idx >= kContextSlots) return false;
    Ctx* c = ctxs_[static_cast<std::size_t>(idx)].get();
    if (!c) return false;
    if (c->actions.size() >= 4) {
        // `printf("%d\n", count)` - the FIFO is full; nothing is queued
    } else {
        // a second activate is refused while one is queued or current
        for (const int a : c->actions)
            if (a == action && action == 2) return false;
        if (action == c->current && action == 2) return false;
        c->actions.push_back(action);
    }
    if (action == 3 && (c->status == 10 || c->status == 9))
        areaTransition(2, idx, c->slot, -1, -1, -1);
    return true;
}

void Session::freeContext(int idx) {
    if (idx < 0 || idx >= kContextSlots) return;
    if (!ctxs_[static_cast<std::size_t>(idx)]) return;
    ctxs_[static_cast<std::size_t>(idx)].reset();
    for (auto& s : slots_) {
        if (s.areaCtx == idx) s.areaCtx = -1;
        if (s.sceneCtx == idx) s.sceneCtx = -1;
    }
    if (pendingUiCtx_ == idx) pendingUiCtx_ = -1;
    // The engine leaves `dword_4C0130` pointing at a freed entry and the
    // pump's tail then reads through a null block. Clearing it is the
    // defined counterpart.
    if (deferred_ == idx) deferred_ = -1;
    // `Script_ProcessActions` action 4 clears every prompt slot whose +0 is
    // this context; `Script_FreeContext` and the prune do not, and leave a
    // dangling pointer that pump case 1's reuse arm then READS (`u16(ctx,42)`
    // on freed memory). The port clears on every free: a dangling index
    // would land on whatever context took the entry next.
    for (auto& p : promptCtx_) if (p == idx) p = -1;
    if (message0Ctx_ == idx) message0Ctx_ = -1;
}

int Session::contextFlags(int idx) const {
    if (idx < 0 || idx >= kContextSlots) return -1;
    const auto& c = ctxs_[static_cast<std::size_t>(idx)];
    return c ? c->flags40 : -1;
}

int Session::contextStatus(int idx) const {
    if (idx < 0 || idx >= kContextSlots) return -1;
    const auto& c = ctxs_[static_cast<std::size_t>(idx)];
    return c ? c->status : -1;
}

int Session::contextZone(int idx) const {
    if (idx < 0 || idx >= kContextSlots) return -1;
    const auto& c = ctxs_[static_cast<std::size_t>(idx)];
    return c ? c->zoneId : -1;
}

std::size_t Session::liveContexts() const {
    std::size_t n = 0;
    for (const auto& c : ctxs_)
        if (c && (c->status != 0 || !c->actions.empty())) ++n;
    return n;
}

int Session::queueStartup(int slot, bool isScene) {
    const ResidentSlot& s = slots_[slot & 1];
    const auto& chunk = isScene ? s.sceneChunk : s.areaChunk;
    const auto at = startupScript(chunk);
    if (at == 0) return -1;
    const std::int32_t scripts[3] = {static_cast<std::int32_t>(at), 0, 0};
    const int idx = newContext(slot & 1, chunk, scripts, -1, s.area);
    if (idx < 0) return -1;
    ctxs_[static_cast<std::size_t>(idx)]->sceneStartup = isScene;
    queueAction(idx, 1);
    return idx;
}

void Session::setStatus(int ctxIdx, int status) {
    if (ctxIdx < 0 || ctxIdx >= kContextSlots) return;
    Ctx* c = ctxs_[static_cast<std::size_t>(ctxIdx)].get();
    if (c) c->status = status;
}

// --------------------------------------------------------- THE TRANSITION

void Session::clearTransition() {
    const int program = tr_.program;      // an object already started still ends
    tr_ = Transition{};
    tr_.startedFrame = frameNo_;
    tr_.program = program;
}

// `ScriptObject_Start(obj, a1[3], a1[1], 1)`: the object of the OUTGOING
// block - resolved against the scene the player is still standing in (416 of
// 448 shipped pairs land there, 84 in the destination's) - started with the
// caller's context index as the waiter, so its end raises event 3 for it.
void Session::startTransitionObject(int obj) {
    Call c;
    c.op = 58;                            // `scx.play.wait`'s shape: object first
    c.fields = {static_cast<std::int16_t>(obj), 0, 0};
    const int idx = scene_.handle({c});
    if (const char* e = std::getenv("OMK_CAMLOG"))
        if (*e == '1')
            std::fprintf(stderr, "[tr] frame %ld  object %d -> program %d (%zu started, %zu missed)\n",
                         frameNo_, obj, idx, scene_.started().size(), scene_.missed().size());
    // Not resident (or no scene loaded): the engine would wait on an object
    // it cannot find until the 60 s watchdog. A replica with no scene must
    // run on where the game would, so the "object" ends next frame - the
    // same policy `ObjectWait` takes.
    tr_.program = idx >= 0 ? idx : -2;
}

// `Game_HandleEvent` case 3, raised by the object runtime when the program
// `ScriptObject_Start` began ends.
// `Game_HandleEvent` case 3, the move half. The object half is
// `transitionObjectEnded`; this is the same event with a different raiser.
void Session::playerMoveEnded(int ctx) {
    if (ctx < 0 || ctx >= kContextSlots) return;
    Ctx* c = ctxs_[static_cast<std::size_t>(ctx)].get();
    if (!c) return;                                  // `if (!v2) return 1;`
    if (c->status != 4) return;                      // case 3's own test
    c->waitingForMove = -1;
    c->status = 1;                                   // runs again this pump
}

// `Game_HandleEvent` case 2. The FIRST context at 3, and the bank reload
// regardless - the status half is modelled; the .CTL reload belongs to the
// player's channel and is named rather than silently skipped.
void Session::fightEnded() {
    for (int i = 0; i < kContextSlots; ++i) {
        Ctx* c = ctxs_[static_cast<std::size_t>(i)].get();
        if (!c || c->status != 3) continue;
        c->fightOpponent = -1;
        c->status = 1;
        break;                                       // the FIRST one only
    }
}

int Session::fightingWith() const {
    for (const auto& c : ctxs_)
        if (c && c->status == 3) return c->fightOpponent;
    return -1;
}

bool Session::startPlayerMove(int addressId, int ctx) {
    return moveHook_ && moveHook_(addressId, ctx);
}

bool Session::beginFight(int opponentId) {
    return fightHook_ && fightHook_(opponentId);
}

void Session::setMoveHook(std::function<bool(int, int)> h) {
    moveHook_ = std::move(h);
    for (auto& c : ctxs_) if (c) c->vm.setMoveWaitSuspends(static_cast<bool>(moveHook_));
}

void Session::setFightHook(std::function<bool(int)> h) {
    fightHook_ = std::move(h);
    for (auto& c : ctxs_) if (c) c->vm.setFightWaitSuspends(static_cast<bool>(fightHook_));
}

void Session::transitionObjectEnded() {
    if (tr_.program == -1) return;
    if (tr_.program >= 0 && scene_.programRunning(tr_.program)) return;
    if (const char* e = std::getenv("OMK_CAMLOG"))
        if (*e == '1')
            std::fprintf(stderr, "[tr] frame %ld  program %d ENDED (state %d)\n",
                         frameNo_, tr_.program, tr_.state);
    tr_.program = -1;
    const int ctxIdx = tr_.ctx;
    if (ctxIdx == kNoContext) {
        areaTransition(4, ctxIdx, active_, -1, -1, -1);
        return;
    }
    Ctx* c = (ctxIdx >= 0 && ctxIdx < kContextSlots)
                 ? ctxs_[static_cast<std::size_t>(ctxIdx)].get() : nullptr;
    if (!c) return;                                       // `if (!v2) return 1`
    if (c->status == 4) { c->status = 1; return; }
    if (c->status == 10 || c->status != 11) {
        areaTransition(4, ctxIdx, active_, -1, -1, -1);
        return;
    }
    // status 11 - the watchdog's last step: link the decor, resume
    hideSet(slots_[1 - active_].area);
    c->status = 1;
}

// `Area_Transition` (0x00408530), the eight-dword block at `dword_6A0600`
// walked by five callers (docs/SCRIPT_VM "The area transition"). Returns what
// it returns: op 47 rewinds its pc by 7 on 0.
int Session::areaTransition(int mode, int ctxIdx, int slot, int area, int f1, int f2) {
    slot &= 1;
    if (const char* e = std::getenv("OMK_CAMLOG"))
        if (*e == '1')
            std::fprintf(stderr, "[tr] frame %ld  mode %d ctx %d slot %d area %d f1 %d f2 %d"
                         "  state %d dest %d deferred %d\n", frameNo_, mode, ctxIdx, slot,
                         area, f1, f2, tr_.state, tr_.dest, deferred_);
    const auto sameCaller = [&]() { return slot == tr_.slot && ctxIdx == tr_.ctx; };
    // `if (dword_69BC60) sub_419A90(dword_69BC48[0]); else sub_419A90(dword_69BC58);`
    // - hide the NON-ACTIVE slot's decor. The engine reaches these arms after
    // event 9 has moved the active row under the player's feet; without a
    // walker nothing raises it, so if the destination is still the other row
    // it is raised here first. **RECONSTRUCTION**: the departure and arrival
    // animations carry the player onto the new set, and `Walk_ProbeGround`
    // would have said so.
    const auto hideOutgoing = [&]() {
        if (tr_.dest >= 0 && slots_[active_].area != tr_.dest &&
            slots_[1 - active_].area == tr_.dest)
            playerOnArea(tr_.dest);
        hideSet(slots_[1 - active_].area);
    };
    const auto finishScene = [&]() {
        reloadScene(slots_[curSlot_].area, slots_[curSlot_].scene);
    };

    switch (mode) {
    case 0:                                               // op 47 `area.goto`
        switch (tr_.state) {
        case 0:
            tr_.ctx = ctxIdx;
            tr_.f1 = f1;
            tr_.slot = slot;
            tr_.f2 = f2;
            tr_.state = f1 != -1 ? 1 : 3;
            tr_.outArea = slots_[slot].area;
            tr_.dest = area;
            loadIntoSlot(1 - slot, area);
            setStatus(ctxIdx, 10);
            tr_.startedFrame = frameNo_;
            deferred_ = ctxIdx;                           // dword_4C0130 = ctx index
            return 1;
        case 1:
            setStatus(ctxIdx, 9);                         // retry next frame
            return 0;
        case 3:
            return 1;                                     // accepted, nothing done
        case 4:
            if (area == tr_.dest) return 0;
            // a second `area.goto` supersedes the first: its caller is left
            // at status 5, which nothing in the image resumes
            setStatus(tr_.ctx, 5);
            tr_.ctx = ctxIdx;
            tr_.dest = area;
            tr_.f1 = -1;
            tr_.outArea = slots_[slot].area;
            tr_.f2 = -1;
            loadIntoSlot(1 - slot, area);
            setStatus(ctxIdx, 10);
            tr_.state = 3;
            tr_.startedFrame = frameNo_;
            deferred_ = ctxIdx;
            return 1;
        case 8:
            if (f1 == -1) return 1;
            setStatus(ctxIdx, 9);
            return 0;
        default:
            if (sameCaller()) return 1;
            setStatus(ctxIdx, 9);
            return 0;
        }
    case 1:                                               // op 48 `area.arrive`
        if (tr_.state == 3) {
            tr_.state = 4;
            tr_.startedFrame = frameNo_;
            return 0;
        }
        if (tr_.state != 8) return 0;
        if (area == -1) hideSet(slots_[1 - active_].area);
        else hideSet(area);
        clearTransition();
        return 1;
    case 2: {                                             // a LEAVE on a parked caller
        const int st = ctxIdx >= 0 && ctxIdx < kContextSlots &&
                       ctxs_[static_cast<std::size_t>(ctxIdx)]
                           ? ctxs_[static_cast<std::size_t>(ctxIdx)]->status : 0;
        if (st == 9) {
            if (sameCaller()) return 0;
            // the retry is cancelled: status 1 and the pc pushed PAST the
            // `area.goto` it was rewound onto
            Ctx* c = ctxs_[static_cast<std::size_t>(ctxIdx)].get();
            c->status = 1;
            c->pc += 7;
            return 1;
        }
        switch (tr_.state) {
        case 1: tr_.state = 2; setStatus(ctxIdx, 10); tr_.startedFrame = frameNo_; return 1;
        case 5: tr_.state = 6; setStatus(ctxIdx, 10); tr_.startedFrame = frameNo_; return 1;
        case 7:
            startTransitionObject(tr_.f2);
            setStatus(ctxIdx, 10);
            tr_.state = 9;
            tr_.startedFrame = frameNo_;
            return 1;
        default:
            return 0;
        }
    }
    case 3:                                               // the pump's tail: loaded
        switch (tr_.state) {
        case 1:
            showSet(tr_.dest);                            // sub_419AF0(a1[4])
            ++entered_;
            startTransitionObject(tr_.f1);
            tr_.state = 5;
            setStatus(tr_.ctx, 10);
            tr_.startedFrame = frameNo_;
            return 0;
        case 2:
            clearTransition();
            finishScene();
            return 0;
        case 3:
            showSet(tr_.dest);
            ++entered_;
            tr_.state = 8;
            setStatus(tr_.ctx, 1);                        // the caller resumes
            tr_.startedFrame = frameNo_;
            finishScene();
            return 0;
        case 4:
            hideOutgoing();
            clearTransition();
            finishScene();
            return 0;
        default:
            return 0;
        }
    case 4:                                               // event 3: an object ended
        switch (tr_.state) {
        case 5:
            tr_.state = 7;
            setStatus(ctxIdx, 10);
            tr_.startedFrame = frameNo_;
            // **RECONSTRUCTION** (area.h `queueLeave`), narrowed: a caller
            // whose zone the scan ARMED gets its leave from the scan when the
            // player's feet leave the quad; one in no prompt slot (a probe's
            // context, a startup script) can never, and gets it the frame the
            // departure object ends.
            {
                bool armed = false;
                for (int k = 0; k < 16; ++k)
                    if (promptCtx_[static_cast<std::size_t>(k)] == ctxIdx) armed = true;
                if (!leaveFromZones_ && !armed)
                    areaTransition(2, ctxIdx, tr_.slot, -1, -1, -1);
            }
            return 0;
        case 6:
            startTransitionObject(tr_.f2);
            tr_.state = 9;
            setStatus(ctxIdx, 10);
            tr_.startedFrame = frameNo_;
            return 0;
        case 9:
            hideOutgoing();
            clearTransition();
            setStatus(ctxIdx, 1);                         // the caller resumes
            finishScene();
            return 0;
        default:
            return 0;
        }
    default:
        return 0;
    }
}

// `Script_Pump`'s tail:
//
//     if (!Area_TickLoad(1 - dword_69BC60) || dword_4C0130 == -1) return 1;
//     ctx = dword_4E61E8[dword_4C0130];
//     if (u16(ctx, 22) == 10) Area_Transition(&dword_6A0600, dword_69BC60, ctx, 3, -1, -1, -1);
//     else                    u16(ctx, 22) = 1;       /* the status-8 preload */
//     dword_4C0130 = -1;
void Session::pumpTail() {
    const int tickSlot = load_.active ? load_.slot : 1 - active_;
    if (!tickLoad(tickSlot) || deferred_ == -1) return;
    if (deferred_ == kNoContext) {
        areaTransition(3, kNoContext, active_, -1, -1, -1);
    } else {
        Ctx* c = ctxs_[static_cast<std::size_t>(deferred_)].get();
        if (c && c->status == 10) areaTransition(3, deferred_, active_, -1, -1, -1);
        else if (c) c->status = 1;
    }
    deferred_ = -1;
}

// --------------------------------------------------------------- RESTART

void Session::restart() {
    // ---- Script_Pump(3): sub_40E260 and the two resets around it
    // `Music_PlayTrack(0, 1)` - a STOP (anything below track 2 is one) - and
    // `g_MusicTrack = -1`
    musicTrack_ = 0;
    musicLoop_ = true;
    // the held object and its scene record, the actor's release, the weapon
    // slot: no held-object or weapon model here (issues 34/26)
    if (dialogState_ == 3) { dialog_.reset(); dialogState_ = 1; }
    speakerModel_.clear();
    // both slots' contexts and scenes freed, the blocks released
    for (int s = 0; s < 2; ++s) if (slots_[s].area != -1) evictSlot(s);
    // the transition block
    tr_ = Transition{};
    tr_.startedFrame = frameNo_;
    deferred_ = -1;
    load_ = Load{};
    // ---- Script_Pump(2): Game_NewGame
    // `sub_406270()` - the table zeroed - and `sub_406730()`, the prompt slots
    for (auto& c : ctxs_) c.reset();
    resetWorld();
    pendingUiCtx_ = -1;
    pendingUiScreen_ = pendingUiParam_ = pendingUiVar_ = -1;
    shown_.clear();
    scriptShown_.clear();
    playerPlaced_ = false;
    playerAddress_ = -1;
    pendingAddress_ = -1;
    haveCam_ = false;
    camTravel_ = camElapsed_ = 0;
    rootMotion_.clear();
    // `IAM\START` over a zeroed DB, `State_Apply`, day 52, 2000000 into it
    state_ = GameState::fromFile(iam_ + "/START");
    state_.setClockDay(52);
    state_.setClock(2000000);
    // `State_Apply`'s `Area_Load(+1414, 0)`: the boot load again
    const int start = state_.currentArea();
    if (start >= 0) {
        loadArea(start);
        if (!scptData_.empty()) reloadScene(start, slots_[0].scene);
    }
    ++entered_;
}

// --------------------------------------------------------------- CAMERAS

const WorldCamera* Session::findCamera(int id) const {
    // `Camera_FindWorld` (0x0040B220): for each of the two rows whose area id
    // is not negative, the AREA table (+64, count +84) then the SCENE table
    // (+32, +52); then GLOBAL (+20, +30). Slot 0 first, whatever is active.
    for (int s = 0; s < 2; ++s) {
        if (slots_[s].area < 0) continue;
        for (const auto& c : slots_[s].cams.area())  if (c.id == id) return &c;
        for (const auto& c : slots_[s].cams.scene()) if (c.id == id) return &c;
    }
    for (int s = 0; s < 2; ++s)
        for (const auto& c : slots_[s].cams.global()) if (c.id == id) return &c;
    return nullptr;
}

void Session::setCameraWait(bool on) {
    camWait_ = on;
    for (auto& c : ctxs_) if (c) c->vm.setCameraWaitSuspends(on);
}

void Session::setObjectWait(bool on) {
    objWait_ = on;
    for (auto& c : ctxs_) if (c) c->vm.setObjectWaitSuspends(on);
}

void Session::applyCamera(int id, int travel) {
    const WorldCamera* c = findCamera(id);
    // `Camera_FindWorld` returns 0 when no resident table has the id, and
    // `Game_HandleEvent` case 8 then does nothing. A script naming a camera
    // that belongs to somewhere else is not a decode failure.
    if (!c) return;
    if (const char* e = std::getenv("OMK_CAMLOG"))
        if (*e == '1')
            std::fprintf(stderr, "[cam] frame %ld  camera %d  travel %d  %s\n",
                         frameNo_, id, travel,
                         c->absolute() ? "absolute" : "RELATIVE");
    if (!haveCam_ || travel <= 0) {      // a cut
        camFrom_ = camTo_ = camNow_ = *c;
        camTravel_ = camElapsed_ = 0;
        haveCam_ = true;
        return;
    }
    camFrom_   = camNow_;
    camTo_     = *c;
    camTravel_ = travel;
    camElapsed_ = 0;
}

void Session::tickCamera() {
    if (const char* e = std::getenv("OMK_CAMLOG"))
        if (*e == '1' && camTravel_ == 1)
            std::fprintf(stderr, "[cam] frame %ld  move to %d ENDS\n",
                         frameNo_, camTo_.id);
    if (camTravel_ <= 0) return;
    ++camElapsed_;
    const float u = camElapsed_ >= camTravel_
                        ? 1.0f
                        : static_cast<float>(camElapsed_) /
                              static_cast<float>(camTravel_);
    for (int k = 0; k < 3; ++k) {
        camNow_.eye[k] = camFrom_.eye[k] + (camTo_.eye[k] - camFrom_.eye[k]) * u;
        camNow_.at[k]  = camFrom_.at[k]  + (camTo_.at[k]  - camFrom_.at[k])  * u;
    }
    camNow_.fov = camFrom_.fov + (camTo_.fov - camFrom_.fov) * u;
    // Roll is an ANGLE: take the SHORT arc. A move between +179 and -179 is
    // two degrees and lerping the numbers spins the camera 358 - which is the
    // error CLAUDE.md 1 files under "invisible at rest", because either
    // reading draws both endpoints identically.
    float d = camTo_.roll - camFrom_.roll;
    while (d > 180.0f) d -= 360.0f;
    while (d <= -180.0f) d += 360.0f;
    camNow_.roll = camFrom_.roll + d * u;
    camNow_.id = camTo_.id;
    camNow_.mode = camTo_.mode;
    if (camElapsed_ >= camTravel_) camTravel_ = 0;
}

// ------------------------------------------------------------ THE PLAYER

void Session::trackPlayer() {
    playerDriven_ = false;
    if (!scene_.loaded()) return;
    const auto& started = scene_.started();
    // The LAST running player program wins: the beats are started in order and
    // each supersedes the one before, which is what `Script_SelectBodyAnimation`
    // does to the node it is handed.
    int which = -1;
    for (std::size_t i = 0; i < started.size(); ++i)
        if (started[i].how == "player" && scene_.programRunning(static_cast<int>(i)))
            which = static_cast<int>(i);
    if (which < 0) return;
    // ACTOR_STATE 4/5: `Actor_TickScxDriven` / `Actor_StartPendingScx`, which
    // do not call `Actor_ScanZones` - the scan is off while a program owns him
    playerDriven_ = true;
    const auto& st = started[static_cast<std::size_t>(which)];
    if (st.clip < 0) return;

    // The PLACEMENT, by whichever of the two body-animation functions the
    // object uses (docs/FILE_FORMATS.md, "How the two body-animation functions
    // PLACE the character"): the relative variant samples an authored `.3DP`
    // and adds its inch offset, the plain one snaps to the clip's root key 0.
    float base[3] = {0, 0, 0};
    bool have = false;
    const auto& paths = scene_.scene().paths();
    if (st.relative && st.path >= 0 &&
        st.path < static_cast<int>(paths.size())) {
        have = pathSample(paths[static_cast<std::size_t>(st.path)], 1.0f, base) ||
               !paths[static_cast<std::size_t>(st.path)].keys.empty();
        for (int k = 0; k < 3; ++k) base[k] += st.offset[k];
    } else {
        have = clipRootStart(scene_.scene().clipData(st.clip), base);
    }
    if (!have) return;

    // ...plus the root motion the clip has accumulated by this frame, which is
    // what `Anim_RootDelta` sums and `Actor_MoveBy` applies.
    auto it = rootMotion_.find(st.clip);
    if (it == rootMotion_.end())
        it = rootMotion_.emplace(st.clip,
                                 clipRootMotion(scene_.scene().clipData(st.clip))).first;
    const auto& rm = it->second;
    if (!rm.empty()) {
        // the frame of the clip `st.clip` now names - see
        // `SceneRunner::programAnimClock`
        const float t = scene_.programAnimClock(which);
        int f = t < 0 ? 0 : static_cast<int>(t);
        if (f >= static_cast<int>(rm.size())) f = static_cast<int>(rm.size()) - 1;
        for (int k = 0; k < 3; ++k)
            base[k] += rm[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)];
    }
    for (int k = 0; k < 3; ++k) playerPos_[k] = base[k];
    playerPlaced_ = true;
}

bool Session::placeActorAt(int addressId) {
    // `Address_Find` searches the two resident chunk slots at `dword_69BC40`,
    // slot 0 first.
    for (int s = 0; s < 2; ++s) {
        for (const auto& a : slots_[s].addresses) {
            if (a.id != addressId) continue;
            for (int k = 0; k < 3; ++k) playerPos_[k] = a.pos[k];
            playerYaw_ = a.yaw;
            playerPlaced_ = true;
            playerAddress_ = addressId;
            pendingAddress_ = -1;
            ++placementSeq_;
            // **RECONSTRUCTION**: a teleport onto the OTHER slot's set puts
            // his feet on that decor, and `Walk_ProbeGround` raises event 9
            // on the next actor tick. No walker runs here, so the Session
            // raises it now. The intro's `actor.goto_address 654` (AREA 222's
            // table, from AREA 118's script) is what this is for.
            if (slots_[s].area != slots_[active_].area) playerOnArea(slots_[s].area);
            return true;
        }
    }
    // Not in either resident chunk. Before 2026-09-02 this was the normal
    // case for a transition's tail - `area.goto` ran through and the address
    // arrived a step early - and the hold-and-retry below covered it. The
    // caller now parks at status 10 the way the handler parks it, so a
    // resident address resolves at once; the hold stays as the fallback for
    // an address no resident chunk owns.
    pendingAddress_ = addressId;
    return false;
}

// ---------------------------------------------------------- THE SCENE

void Session::reloadScene(int area, int scene) {
    // Only for a caller that asked for scenes at all: a Session that only
    // wants the script decisions never pays for a 7 MB load.
    //
    // The programs of the outgoing scene go with it. That is the engine's
    // behaviour and not a simplification: `Scene_LoadSCX` rebuilds the object
    // pool, so no program survives the transition.
    if (scptData_.empty()) return;
    if (area < 0) return;
    // The `.SCX` BELONGS TO THE AREA, and a scene loaded over it must not
    // disturb it. `Area_LoadScx` (0x0041B4E0) is the only thing that ever
    // fills the slot's object container - it clears `slot+8`, calls
    // `Scene_LoadSCX` into it and binds the matching `.sfx` - and it has two
    // callers, `Area_TickLoad` and `Game_Init`. `Scene_LoadSCX` itself has
    // four call sites in the whole binary and the `scene.load` opcode is none
    // of them: handler 0x403950 calls `Scene_Load` (sub_40C120), which brings
    // in the SCENE CHUNK - startup script, zones, props - and never touches
    // the object pool.
    //
    // So EVERY RUNNING PROGRAM SURVIVES A SCENE LOAD. A cutscene does not own
    // the objects it animates; it calls `scx.play` on objects of the area's
    // own `.SCX`, and the ones it never names go on running - which is why
    // the environment keeps animating through a cutscene in the original.
    //
    // This function used to rebuild unconditionally, on the reasoning that
    // "`Scene_LoadSCX` rebuilds the object pool, so no program survives the
    // transition". That is true of the function and false of the transition,
    // because the transition does not call it - the premise was never checked
    // against the call sites. And since `resolveScx` reads the stem from the
    // AREA chunk either way, the rebuild was re-loading THE SAME FILE and
    // resetting every program counter, clock and run count for nothing
    // (`todo/omk-play.md` 52).
    if (area == sceneArea_ && scene_.loaded()) return;
    SceneRunner fresh;
    // Always by AREA: the stem is the area chunk's `+97` whichever kind is
    // asked for, so this is the same file the scene path resolved to - and it
    // does not depend on the scene->area map, which is built from the script
    // slots and is known to miss a scene whose only `scene.load` is in a
    // startup script (scene 55, the Impasse).
    if (fresh.load(scptData_, iam_, table_, ChunkKind::Area, area)) {
        scene_ = std::move(fresh);
        sceneArea_ = area;
        attachSceneSfx();
    }
}

// `Area_LoadScx` loads the `.SCX` and binds the matching `.sfx` in ONE
// function - it rewrites the buffer's extension in place
// (`Buffer[strlen(Buffer) - 2] = 102`, 'f') and calls `Sfx_LoadFile` then
// `Sfx_BindAmbientEffects`. So the two belong together, and every path that
// makes an `.SCX` resident owes the bind: `loadScene` did not, and once
// `reloadScene` stopped rebuilding unconditionally (issue 52) that omission
// became visible as the Impasse losing all 15 of its fired set pieces and
// every particle. Binding is also what SHOWS the standing rows, the ones
// keyed `(1, -1)` that no object start can reach.
void Session::attachSceneSfx() {
    if (scptData_.empty() || !scene_.loaded()) return;
    std::string sfx = scene_.file();
    const auto dot = sfx.rfind('.');
    if (dot == std::string::npos) return;
    sfx = sfx.substr(0, dot) + ".sfx";
    scene_.attachSfx(scptData_, sfx);
}


// Opcode 71 `scene.load(area, scene)`, handler 0x403950. After the two
// fetches and the `SCENES` announcement:
//
//     call sub_40B140              ; Area_GetLoadedScene(area) - the OLD scene
//     mov  ecx, offset dword_69BC48
//   loc_4039F3:                    ; for each of the two resident slots
//     cmp  [ecx], edi              ; slot holds `area`?
//     jnz  loc_403AB3
//     cmp  eax, -1                 ; an old scene over it?
//     jz   loc_403A60
//     call sub_40B160              ; its SCENE block
//     mov  ebx, [eax]              ; block +0: the startup CONTEXT
//     ... dword_4E61E8[slot] = 0; three sub_412060 (Mem_Free)   ; freed
//     call sub_40BEC0 / sub_40A200 ; unload the old scene
//   loc_403A60:
//     call sub_40C120(scene, slot) ; Scene_Load into the slot
//     call sub_40BB90 / sub_409FC0 ; its props and actors
//     call sub_40B160              ; the NEW block
//     mov  eax, [ebx+4]            ; +4: its startup script
//     call sub_406290              ; Script_NewContext(slot, script, 0, 0)
//     mov  [ebx], eax
//     call sub_4063D0(ctx, 1)      ; Script_QueueAction - runs next frame
//   loc_403AB3: next slot
//     call sub_40B120(area, scene) ; Area_SetLoadedScene - the DB field
//     call sub_406560              ; Zones_RegisterAll
//
// So on a RESIDENT area - either slot - the swap is immediate and the new
// scene's script is live on the next frame; on any other area only the DB
// changes. The interpreter already wrote the DB field when it executed the
// opcode; it is written again here so the body stands on its own for a
// caller with no script.
void Session::sceneLoad(int area, int scene) {
    for (int s = 0; s < 2; ++s) {
        if (slots_[s].area != area) continue;
        ResidentSlot& sl = slots_[s];
        if (sl.scene != -1 && sl.sceneCtx >= 0) freeContext(sl.sceneCtx);
        // `sub_40BEC0(area, oldScene, 0)`: the old scene's ACTORS hand their
        // runtime slots back; `sub_40A200` its props.
        if (sl.scene != -1) freeActorSlots(s, false, true);
        if (sl.scene != -1) unloadProps(s, false, true);
        sl.sceneCtx = -1;
        sl.scene = scene;
        sl.sceneChunk = scene != -1 ? readChunk(iam_, "SCENE", scene)
                                    : std::vector<std::byte>{};
        if (sl.sceneChunk.empty()) sl.scene = -1;
        fillSlotTables(sl);
        // `sub_40BB90(area, newScene, 0) / sub_409FC0`: the new scene's
        // actors, then its props - the same order as case 5 then case 6, and
        // `a3 = 0` so the AREA's table is not walked again.
        spawnFromTables(s, false, true);
        if (sl.scene != -1) loadProps(s, false, true);
        if (sl.scene != -1) sl.sceneCtx = queueStartup(s, true);
        if (s == curSlot_) reloadScene(area, sl.scene);
    }
    state_.setSceneOfArea(area, static_cast<std::int16_t>(scene));
    zonesRegisterAll();
}

// Opcode 72 `scene.unload(area)`, handler 0x403AF0 - the same slot walk:
//
//   loc_403B46:                    ; for each resident slot
//     cmp  edi, [ebp-4]            ; slot holds `area`?
//     call sub_40B140              ; Area_GetLoadedScene(area)
//     cmp  esi, -1 ; jz next       ; nothing over it
//     call sub_40B160 ; mov ebx,[eax]    ; the SCENE block's context
//     ... dword_4E61E8[slot] = 0; three Mem_Free      ; freed
//     mov  dword ptr [eax], 0      ; block +0 cleared
//     call sub_40BEC0 / sub_40A200 ; unload the scene
//     mov  dword ptr [ebp+0], -1   ; the slot's scene id
//     cmp  ecx, eax ; jnz          ; the caller's own slot?
//     or   byte ptr [eax+28h], 8   ; ctx+40 bit 8: free the block at `end`
//   next: ...
//     Area_SetLoadedScene(area, -1); Zones_RegisterAll()
//
// Bit 8 (issue 38, 2026-09-02): the caller is RUNNING OUT OF that block -
// its code pointer is inside the SCENE chunk - so the handler cannot free it
// here; `end` (0x401B90) does, `if ((+40 & 8) && dword_69BC44[slot]) {
// Mem_Free; dword_69BC44[slot] = 0; }`. The port keeps the slot's chunk bytes
// with the scene id at -1 until that context's `end` and drops them there.
// Every walk that could see the kept block gates on the ID as the engine's
// do (`Camera_FindWorld` v1[3], `Actor_FindById` v2[3], `Message_RunHandlers`
// dword_69BC4C, `Zones_RegisterAll` id-and-block), so the deferral is
// memory and bookkeeping; `sceneBlocksFreedAtEnd()` counts it. The DB write
// is unconditional and is what a non-resident unload amounts to.
void Session::sceneUnload(int area, int caller) {
    for (int s = 0; s < 2; ++s) {
        if (slots_[s].area != area) continue;
        ResidentSlot& sl = slots_[s];
        if (sl.scene == -1) continue;
        if (sl.sceneCtx >= 0) freeContext(sl.sceneCtx);
        // `sub_40BEC0(area, scene, 0)` here too, before the props
        freeActorSlots(s, false, true);
        for (std::size_t k = sl.characters.size(); k-- > 0;)
            if (sl.characters[k].fromScene)
                sl.characters.erase(sl.characters.begin() + static_cast<long>(k));
        rebuildShown();
        unloadProps(s, false, true);
        sl.sceneCtx = -1;
        sl.scene = -1;
        Ctx* c = (caller >= 0 && caller < kContextSlots)
                     ? ctxs_[static_cast<std::size_t>(caller)].get() : nullptr;
        if (c && c->slot == s) {
            // `mov al, [ecx+1Fh] ; cmp ecx, eax ; jnz` - the caller's own
            // slot: `or byte ptr [ctx+28h], 8`, the block stays until `end`
            c->flags40 |= 8;
        } else {
            sl.sceneChunk.clear();
        }
        fillSlotTables(sl);
        // The area's own `.SCX` (`AREA +97`) stands where the scene's did.
        if (s == curSlot_) reloadScene(area, -1);
    }
    state_.setSceneOfArea(area, -1);
    zonesRegisterAll();
}

bool Session::postMessage(int message, int sender) {
    // `Message_RunHandlers` (0x00409420), transcribed from readable/src/
    // 01_file.c: the SCENE's table, then the AREA's, then GLOBAL's; the first
    // record whose +4 equals the message wins. A record with script 0 (GLOBAL
    // carries one, for message 0) would hand `Script_NewContext` a null
    // script; it is refused here rather than run from offset 0. The slot
    // searched is the ACTIVE one.
    std::vector<std::byte> code;
    std::size_t at = 0;
    std::string table;
    const auto pick = [&](const std::vector<Subscription>& subs,
                          std::span<const std::byte> chunk, const char* name) {
        if (!code.empty()) return;
        for (const auto& s : subs) {
            if (s.message != message) continue;
            if (s.script <= 0) return;
            code.assign(chunk.begin(), chunk.end());
            at = static_cast<std::size_t>(s.script);
            table = name;
            return;
        }
    };
    const ResidentSlot& sl = slots_[active_];
    // `if (dword_69BC4C[4 * areaSlot] != -1)` - the scene ID, not the block
    if (sl.scene != -1 && !sl.sceneChunk.empty())
        pick(chunkSubscriptions(sl.sceneChunk, ChunkKind::Scene), sl.sceneChunk, "scene");
    if (code.empty() && !sl.areaChunk.empty())
        pick(chunkSubscriptions(sl.areaChunk, ChunkKind::Area), sl.areaChunk, "area");
    if (code.empty()) {
        const auto g = readFile(iam_ + "/GLOBAL");
        if (!g.empty()) pick(globalSubscriptions(g), g, "global");
    }
    if (code.empty() || at >= code.size()) return false;

    // `Script_NewContext(slot, script, 0, 0)`, its parameter block
    // `args[0] = msg; args[1] = sender` - what `push.i16 0x4000` reads back
    // as `params[0]` in 22 of the shipped handlers. The sender's slot -> id
    // mapping (`Actor_IdBySlot` for 0..12 but 4, `ObjectSlot_Id` for
    // 4/20/25) is not modelled: there are no slot tables here, so the value
    // is passed through as given.
    const std::int32_t scripts[3] = {static_cast<std::int32_t>(at), 0, 0};
    const int idx = newContext(active_, code, scripts, -1, sl.area);
    if (idx < 0) return true;                 // found, and the table was full
    Ctx* c = ctxs_[static_cast<std::size_t>(idx)].get();
    c->message = static_cast<int>(messages_.size());
    c->vm.setParams({static_cast<std::int16_t>(message),
                     static_cast<std::int16_t>(sender)});
    messages_.push_back({message, sender, table, at, message == 25, frameNo_, -1});
    // `if (!u16(rec, 4)) byte_4C012C = u8i(ctx, 30);` - the message-0
    // handler's table index, which `end` resets to 0xFF
    if (message == 0) message0Ctx_ = idx;

    if (message == 25) {
        // `ctx[3] = script; status = 1; Script_Execute(ctx);
        // Script_FreeContext(ctx)` - inline, this frame, and gone.
        c->status = 1;
        c->current = 1;
        execute(idx);
        freeContext(idx);
    } else {
        // `Script_QueueAction(ctx, 1)` then `(ctx, 4)` - it runs on the next
        // pump and is freed by the action after it.
        queueAction(idx, 1);
        queueAction(idx, 4);
    }
    return true;
}

// Opcode 56 `player.become(id)`, handler 0x402F60 (tables/vm_opcodes.json):
//
//     call sub_419E00 ; Actor_Player()
//     call sub_40AEE0 ; Actor_IdBySlot -> the player's current id
//     cmp  ebx, eax ; jz done            ; already this body: nothing
//     call sub_40D6A0(slot, id)          ; the 20-byte character record
//     movsx esi, word ptr [edi]          ; its actor index
//     call sub_41C270(esi, &pos)         ; position and facing
//     call sub_419E00 ; ebp = the player ; if -1 skip:
//       call sub_40AEE0(ebp) ; mov [edi+2], ax   ; old id into the record
//       call sub_419E10(-1)              ; Player_SetActor(-1)
//     mov  [edi], bp                     ; old index into the record
//     call sub_41BDF0(esi, &pos)         ; place the new body there
//     call sub_41CCA0(esi)               ; show it (character.show's call)
//     call sub_40D590(esi, 0) ; sub_41DDB0(esi)
//     call sub_40B190(id) ; mov ecx,[eax] ; jz     ; Actor_FindById -> +0
//     ... repne scasb / rep movsd -> [dword_69BC6C]     ; strcpy into DB +336
//     call sub_40B190(id) ; mov ecx,[eax+4] ; jz   ; +4
//     ... rep movsd -> [dword_69BC6C+4]                 ; strcpy into DB +592
//     call sub_40B190(id) ; add esi, 8 ; lea edi, [ecx+8]
//     mov  ecx, 43h ; rep movsd          ; record +8..+276 -> DB +68..+336
//
// `dword_69BC6C` is the DB's player record (+60), whose +0/+4 `State_Apply`
// points at the two 256-byte bio slots. So the player's identity IS the DB
// record after this - which is what `playerActor()` reads - and the bio
// travels in the save. The live-actor half (the record swap, `Player_
// SetActor`, the placement) has no actor table to land in here: the position
// and facing the Session tracks stay where they are, which is the transfer's
// own rule, and the new body joins `shown_`.
void Session::becomePlayer(int actor) {
    if (actor == playerActor()) return;
    std::vector<std::byte> chunk;
    std::size_t off = 0;
    if (!actorRecord(actor, chunk, off)) return;   // Actor_FindById -> 0: the
                                                   // engine would fault here
    // `sub_41CCA0(esi)`: shown, like character.show - and no State_SetBit,
    // which this handler does not call. When a placement record spawned the
    // body it is that record's `attached` that turns on; otherwise the body
    // joins the script-shown list, which is what this did for everyone
    // before the spawn landed.
    showCharacter(actor);

    // the two bios: strcpy, so the NUL is copied and the slot's tail is left
    for (int k = 0; k < 2; ++k) {
        const auto p = u32at(chunk, off + 4u * static_cast<std::size_t>(k));
        if (p == 0 || p >= chunk.size()) continue;          // `test ecx, ecx ; jz`
        std::size_t n = 0;
        while (p + n < chunk.size() && chunk[p + n] != std::byte{0}) ++n;
        std::vector<std::byte> s(chunk.begin() + static_cast<long>(p),
                                 chunk.begin() + static_cast<long>(p + n));
        s.push_back(std::byte{0});
        if (s.size() > static_cast<std::size_t>(GameState::kBioSize))
            s.resize(static_cast<std::size_t>(GameState::kBioSize));
        dbWrite(state_, static_cast<std::size_t>(GameState::kBio[k]), s.data(), s.size());
    }
    // `mov ecx, 43h ; rep movsd`: 268 bytes, the record from +8 on
    if (off + 276 <= chunk.size())
        dbWrite(state_, static_cast<std::size_t>(GameState::kPlayerRecord) + 8,
                chunk.data() + off + 8, 268);
}

int Session::playerActor() const {
    return i16at(state_.raw(),
                 static_cast<std::size_t>(GameState::kPlayerRecord) + 272);
}

int Session::shownBitOf(int actor) const {
    // `Scene_FindObjectRecord` (`sub_40D6A0`, 0x0040D6A0): the 20-byte
    // records at AREA +40 (count +72), then at SCENE +8 (count +40) of the
    // scene over the area; the id is at +2. The show/hide handlers read +18
    // (`movsx eax, word ptr [esi+12h]`) and hand it to `State_SetBit`
    // (0x0040AF30), the bitmap at DB +20 - `StateArray::ObjectShown`.
    // `Actors_SpawnFromTables` reads the same +18 back on every area load,
    // which is what makes it a save bit. The shown slot is searched first,
    // then the other resident one.
    // The ad-hoc scan this used to do IS `readPlacements`, which the spawn
    // needs anyway - so it is one reader now (`formats/placements.h`), and
    // the same records answer both questions.
    if (actor == -1) return -1;
    int out = -1;
    const auto scan = [&](std::span<const std::byte> b, ChunkKind kind) {
        if (out >= 0) return;
        for (const auto& r : readPlacements(b, kind))
            if (r.actor == actor) { out = r.bit; return; }
    };
    for (int k = 0; k < 2 && out < 0; ++k) {
        const ResidentSlot& s = slots_[(curSlot_ + k) & 1];
        if (s.area < 0) continue;
        scan(s.areaChunk, ChunkKind::Area);
        if (out < 0 && s.scene != -1 && !s.sceneChunk.empty())
            scan(s.sceneChunk, ChunkKind::Scene);
    }
    return out;
}

// The actor record's `+72`, the `.CTL` bank list `Actor_LoadModel` opens
// (`Actor_LoadBankList`). The sibling of `modelOfActor`'s `+144`.
std::string Session::bankOfActor(int actor) const {
    std::string out;
    std::vector<std::byte> chunk;
    std::size_t o = 0;
    if (!actorRecord(actor, chunk, o)) return out;
    for (int k = 0; k < 20; ++k) {
        const char ch = static_cast<char>(chunk[o + 72u + static_cast<std::size_t>(k)]);
        if (!ch) break;
        out.push_back(ch);
    }
    return out;
}

const Session::Character* Session::characterOf(int actor) const {
    for (int k = 0; k < 2; ++k) {
        const ResidentSlot& s = slots_[(curSlot_ + k) & 1];
        for (const auto& c : s.characters) if (c.actor == actor) return &c;
    }
    return nullptr;
}

// `Actors_SpawnFromTables(area, scene, 1)` (0x0040BB90), `Area_TickLoad`
// case 5 - after the set and the misc model, BEFORE `Scene_LoadProps` at case
// 6 and the startup scripts at case 9. The AREA's table then the SCENE's,
// each record in order:
//
//   * the first free entry of `word_69BC80` takes the ACTOR ID (not a busy
//     marker - the engine writes `word_69BC80[v5] = v4`), and its index goes
//     back to the record's +0; -1 when all 100 are taken;
//   * `Actor_FindById(+2)` -> the 276-byte record; `+144` + ".3DO" ->
//     `Actor_LoadModel(slot, name, area)`. NO MODEL IS LOADED HERE: the
//     Session holds no geometry, so the name is recorded and a frontend
//     opens it (todo/omk-play.md 41). The bank at +72 is recorded with it
//     because `Actor_LoadModel` opens that too.
//   * `Actor_SetPlacement(slot, {x, y, z, facing})`, the coordinates through
//     `rawToWorld` and the facing `* 360/4096` (`formats/placements.h`);
//   * `Actor_Attach(slot)` ONLY IF the DB +20 bit at +18 is set. A clear bit
//     is loaded and placed and not attached.
//   * `Actor_FindById(id) + 270 = -1` - the held-object field cleared, which
//     `var.set.used_object` (75) reads back through `heldObjectOf`.
//
// A record whose id resolves to no actor record faults in the engine
// (`Actor_FindById` returns 0 and it reads +144 off it); here it is skipped
// with the slot still handed out, because the slot is taken before the lookup.
// The two halves are separately callable because the engine calls them
// separately: `Area_TickLoad` case 5 is `Actors_SpawnFromTables(area, scene,
// 1)` - both - while opcode 71 `scene.load`'s handler (0x403950) is
// `push 0 ; push esi ; push edi ; call sub_40BB90` - `a3 = 0`, which is the
// `if (!a3) goto LABEL_16` that skips the AREA table, so a scene swapped over
// a resident area spawns ONLY its own. (`readable/src/01_file.c:4815`'s
// banner says `@callers 1`; that handler is a second caller and is not in the
// decompilation at all, so the count under-reports - CLAUDE.md 1's "a direct
// call site is what makes a proc label" trap, seen from the other side.)
void Session::spawnFromTables(int slot, bool area, bool scene) {
    ResidentSlot& s = slots_[slot & 1];
    for (std::size_t i = s.characters.size(); i-- > 0;)
        if (s.characters[i].fromScene ? scene : area)
            s.characters.erase(s.characters.begin() + static_cast<long>(i));
    const auto walk = [&](std::vector<std::byte>& chunk, ChunkKind kind) {
        if (chunk.empty()) return;
        const auto recs = readPlacements(chunk, kind);
        const std::size_t po = kind == ChunkKind::Area ? 40u : 8u;
        const std::size_t base = recs.empty() ? 0u : u32at(chunk, po);
        for (std::size_t i = 0; i < recs.size(); ++i) {
            const Placement& r = recs[i];
            int k = -1;
            for (int q = 0; q < 100; ++q)
                if (actorSlots_[static_cast<std::size_t>(q)] == -1) { k = q; break; }
            if (k >= 0)
                actorSlots_[static_cast<std::size_t>(k)] =
                    static_cast<std::int16_t>(r.actor);
            // `u16(record, 0) = v5` - the runtime slot written back into the
            // chunk, which is what `Scene_FindObjectRecord`'s callers read.
            const std::size_t o = base + 20u * i;
            chunk[o]     = static_cast<std::byte>(k & 0xFF);
            chunk[o + 1] = static_cast<std::byte>((k >> 8) & 0xFF);

            Session::Character c;
            c.actor = r.actor;
            c.slot  = k;
            c.model = modelOfActor(r.actor);
            c.bank  = bankOfActor(r.actor);
            for (int q = 0; q < 3; ++q) c.pos[q] = r.pos[q];
            c.facing = r.facing;
            c.bit = r.bit;
            c.attached = r.bit >= 0 && state_.bit(StateArray::ObjectShown, r.bit) != 0;
            c.fromScene = kind == ChunkKind::Scene;
            s.characters.push_back(c);

            // `u16(Actor_FindById(v18), 270) = -1`
            for (auto& sl : slots_) {
                if (sl.area < 0) continue;
                if (auto ao = findActorRecord(sl.areaChunk, ChunkKind::Area, r.actor)) {
                    setHeldObjectOf(std::span<std::byte>(sl.areaChunk)
                                        .subspan(*ao, kActorRecordSize), -1);
                    break;
                }
                if (sl.scene != -1)
                    if (auto so = findActorRecord(sl.sceneChunk, ChunkKind::Scene, r.actor)) {
                        setHeldObjectOf(std::span<std::byte>(sl.sceneChunk)
                                            .subspan(*so, kActorRecordSize), -1);
                        break;
                    }
            }
        }
    };
    if (area && s.area != -1) walk(s.areaChunk, ChunkKind::Area);
    if (scene && s.scene != -1) walk(s.sceneChunk, ChunkKind::Scene);
    rebuildShown();
}

// `sub_40BEC0` (0x0040BEC0, the sibling right after the spawn): the same two
// tables walked, and for every record whose `+0` is neither -1 nor the
// PLAYER's slot, `sub_41AD60(slot)` frees the object and
// `word_69BC80[slot] = -1`. `sub_40D4A0(slot)` (the eviction) and opcode 71's
// `sub_40BEC0(area, oldScene, 0)` both reach it.
//
// The player exception is not modelled: the Session holds no actor objects
// and no runtime slot for the player, so there is nothing here that his slot
// would name (labelled in todo/pending/T19.md).
void Session::freeActorSlots(int slot, bool area, bool scene) {
    ResidentSlot& s = slots_[slot & 1];
    for (const auto& c : s.characters) {
        if (c.fromScene ? !scene : !area) continue;
        if (c.slot >= 0 && c.slot < 100 &&
            actorSlots_[static_cast<std::size_t>(c.slot)] == c.actor)
            actorSlots_[static_cast<std::size_t>(c.slot)] = -1;
    }
}

// `shown()` is derived, not appended to: slot 0's attached characters, then
// slot 1's, then whatever a script showed that no table places. Called
// wherever attachment changes - the spawn, 78, 79, `player.become`, an
// eviction.
void Session::rebuildShown() {
    shown_.clear();
    for (int k = 0; k < 2; ++k)
        for (const auto& c : slots_[k].characters) {
            if (!c.attached) continue;
            Shown sh;
            sh.actor = c.actor;
            sh.model = c.model;
            sh.bank  = c.bank;
            for (int q = 0; q < 3; ++q) sh.pos[q] = c.pos[q];
            sh.facing = c.facing;
            sh.slot = c.slot;
            sh.fromTable = true;
            shown_.push_back(sh);
        }
    for (const auto& sh : scriptShown_) {
        bool have = false;
        for (const auto& t : shown_) if (t.actor == sh.actor) have = true;
        if (!have) shown_.push_back(sh);
    }
}

// `character.show` (78, 0x403CB0) and `player.become`'s attach: the handler
// resolves the 20-byte record (`sub_40D6A0`) and calls `Actor_Attach` on the
// slot the spawn wrote into its +0. So an id a table places turns that
// record's `attached` on; an id no table places has no record and no slot,
// and could not be attached in the engine at all - the port keeps it in a
// side list so a frontend still sees the body, and marks it `fromTable`
// false. The bit is written by the caller, because 78 writes one and
// `player.become` does not.
void Session::showCharacter(int actor) {
    if (actor == -1) return;
    for (auto& sl : slots_)
        for (auto& c : sl.characters)
            if (c.actor == actor) { c.attached = true; rebuildShown(); return; }
    bool have = false;
    for (const auto& sh : scriptShown_) if (sh.actor == actor) have = true;
    if (!have) {
        Shown sh;
        sh.actor = actor;
        sh.model = modelOfActor(actor);
        sh.bank  = bankOfActor(actor);
        scriptShown_.push_back(sh);
    }
    rebuildShown();
}

// `character.hide` (79, 0x403DD0): `sub_41CDD0` on the record's slot.
void Session::hideCharacter(int actor) {
    if (actor == -1) return;
    for (auto& sl : slots_)
        for (auto& c : sl.characters)
            if (c.actor == actor) c.attached = false;
    for (std::size_t k = 0; k < scriptShown_.size(); ++k)
        if (scriptShown_[k].actor == actor) {
            scriptShown_.erase(scriptShown_.begin() + static_cast<long>(k));
            break;
        }
    rebuildShown();
}

int Session::answerScreen(int screen, int param) {
    (void)param;                    // -1 means "take the caller's", unused here
    if (!ui_) return -1;
    UiWalk u(*ui_);
    if (!u.open(screen)) return -1;
    // The presses are the player's half and the only thing supplied; the
    // navigation between them is the engine's own. For the start menu that is
    // confirm on "Nouvelle partie", DOWN off the name field onto the buttons,
    // confirm on "Confirmer" - and the name must be typed, because that
    // callback opens by testing the field's cursor and writes nothing when it
    // is empty.
    const std::uint32_t seq[3] = {kUiConfirm, kUiDown, kUiConfirm};
    for (int k = 0; k < 3; ++k) {
        u.press(seq[k]);
        if (k == 0 && !uiName_.empty()) u.typeName(uiName_);
    }
    // An approximate walk went through something unmodelled, so its answer is
    // not evidence about anything. Refusing it is the point: the alternative
    // is inventing one.
    if (u.approximate()) return -1;
    return u.answer();
}

void Session::answerUi(int value) {
    if (pendingUiScreen_ < 0) return;
    // `Game_HandleEvent` case 5: write the variable the `ui.open` named, then
    // release the context. The pc is already past the `ui.open`, so it resumes
    // on the next frame from the instruction after it.
    if (pendingUiVar_ >= 0) state_.setVar(pendingUiVar_, value);
    uiAnswers_.push_back({pendingUiScreen_, pendingUiVar_, value, false});
    uiAnswered(pendingUiCtx_, value);
    pendingUiScreen_ = pendingUiParam_ = pendingUiVar_ = -1;
    pendingUiCtx_ = -1;
}

// `Game_HandleEvent` case 5, after `Var_Set`:
//     v94 = dword_4E61E8[ctx]; v45 = dword_4E6C7C == v94;
//     u16(v94, 22) = 1;
//     if (!v45 || !answer) return 1;
//     dword_4E6C7C = 0;
void Session::uiAnswered(int ctxIdx, int value) {
    if (ctxIdx < 0 || ctxIdx >= kContextSlots) return;
    Ctx* c = ctxs_[static_cast<std::size_t>(ctxIdx)].get();
    if (!c) return;
    if (c->status == 6) c->status = 1;
    if (bootCtx_ == ctxIdx && value != 0) bootCtx_ = -1;
}

void Session::openDialog(int id) {
    // No morph directory attached: the caller is a decision check with no
    // audio and no clock, and it closes the dialog itself. That is what every
    // headless run did before conversations could be played.
    if (morphDir_.empty()) return;
    const auto file = readFile(iam_ + "/DIALOG");
    if (file.empty()) return;
    const auto arch = IamArchive::open(file);
    const auto chunk = arch.chunk(static_cast<std::size_t>(id));
    if (chunk.empty()) return;
    const auto conv = parseConversation(id, chunk);
    dialog_.open(conv, chunk, morphDir_);

    speakerModel_ = modelOfActor(conv.speaker);
}

// `sub_40B190`: the 276-byte actor records at AREA +56 (count +80) and
// SCENE +24 (+48), matched on the id at +272; +144 is the model. Resolved
// against the RESIDENT chunks, because an actor id is reused across areas.
std::string Session::modelOfActor(int actor) const {
    std::string out;
    std::vector<std::byte> chunk;
    std::size_t o = 0;
    if (!actorRecord(actor, chunk, o)) return out;
    for (int k = 0; k < 20; ++k) {
        const char ch = static_cast<char>(chunk[o + 144u + static_cast<std::size_t>(k)]);
        if (!ch) break;
        out.push_back(ch);
    }
    return out;
}

bool Session::actorRecord(int actor, std::vector<std::byte>& chunk,
                          std::size_t& off) const {
    bool found = false;
    const auto scan = [&](std::span<const std::byte> b, std::size_t ao,
                          std::size_t co) {
        if (found || b.size() < co + 2) return;
        const std::size_t p = u32at(b, ao);
        const int n = i16at(b, co);
        if (n <= 0 || p + 276u * static_cast<std::size_t>(n) > b.size()) return;
        for (int i = 0; i < n; ++i) {
            const std::size_t o = p + 276u * static_cast<std::size_t>(i);
            if (i16at(b, o + 272) != actor) continue;
            chunk.assign(b.begin(), b.end());
            off = o;
            found = true;
            return;
        }
    };
    // `v2 = &dword_69BC40; ... v2 += 4`: row 0 then row 1, whatever is shown
    // or active - and each table only when its ID is not negative (`v2[2] <
    // 0` skips the AREA's, `v2[3] >= 0` admits the SCENE's). Until 2026-09-02
    // this walked the shown slot first, which differs only when both rows
    // carry the same actor id.
    for (int k = 0; k < 2 && !found; ++k) {
        const ResidentSlot& s = slots_[k];
        if (s.area < 0) continue;
        scan(s.areaChunk, 56, 80);
        if (!found && s.scene != -1 && !s.sceneChunk.empty()) scan(s.sceneChunk, 24, 48);
    }
    return found;
}

// ------------------------------------------------------------- THE FRAME

void Session::frame() {
    ++frameNo_;
    tickFades();          // both screen fades, on the frame clock
    // `sub_41F320`, the async reader's per-frame slice, sits at the END of the
    // frame function after the render - after the pump. Serving it here, at
    // the start of the next frame, is the same order for everything the
    // scripts can see, and it runs whether or not a conversation is up.
    if (load_.active && load_.slicesLeft > 0) --load_.slicesLeft;

    if (dialogState_ == 3) {
        // `Dialog_TickUI` owns the frame while g_DialogState is 3 - the world
        // pump does not run at all. So this is the whole of a frame during a
        // conversation, and all it does is run the VOICE clock: what ends a
        // line and what picks a reply are the PLAYER's, through `dialogNext`
        // and `dialogChoose`, exactly as `answerUi` is the only thing that
        // releases a script parked on a screen.
        //
        // ...except the SCENE. `Game_Tick` (0x004200F0) has no dialogue gate
        // at all: `Script_PlayAllScripts` per resident slot, then
        // `sub_451600(); Sfx_TickAmbient();` run on every frame, conversation
        // or not - it is `Script_Pump`, the world-script step, that the
        // dialogue displaces. Until 2026-09-02 this returned before the scene
        // ticked and the intro's portal FROZE the moment the conversation
        // opened; a reader watching it said so. The same delta as below.
        scene_.tick(static_cast<float>(frameSeconds_ * 30.0));
        trackPlayer();
        // ...and the ZONE SCAN, which is not the pump's: `Actors_TickAll`
        // dispatches the player's state 16/17 to `Actor_TickDialogue`, which
        // ends `return Actor_ScanZones(a1);`, and cases 7/8 have no dialogue
        // gate - so touches and arms keep landing in the prompt slots during
        // a conversation, and the pump reads them once it closes.
        scanZonesNow();
        // With no morph directory attached there is no conversation to play,
        // and the CALLER owns closing it - which is the contract every headless
        // run has had since before this existed ("closeDialogs is the one thing
        // a caller supplies"). Auto-closing here instead would silently restart
        // the world under `run_scripts`, `walk_zone` and the trace replays.
        if (morphDir_.empty()) return;
        // Attached, but this chunk yielded nothing playable: close it rather
        // than stopping the world for ever.
        if (!dialog_.playing()) { dialogState_ = 1; return; }
        dialog_.tick(frameSeconds_);
        return;
    }

    // `Script_Pump` phase 1 opens with the restart request: phase 3, then
    // phase 2, and the boot contexts it queues run in THIS frame's loop.
    if (restart_) { restart_ = false; restart(); }

    // `Script_Pump(1)` steps 1 and 2: the 16 prompt slots on the states the
    // PREVIOUS frame's scan left, and the unconsumed press. A zone context
    // created here takes the first free table entry and, if that entry is
    // above the loop's cursor, runs in this same frame - which the loop
    // below reproduces because it walks the table AFTER this.
    pumpZoneSlots();

    // `Script_Pump`'s loop runs every table entry in INDEX order, and nothing
    // inside it stops the frame: `dialog.start` returns from `Script_Execute`
    // for ITS context alone (`if (v4 == 61) return;`), the next slot still
    // runs, and `Game_Tick` ticks the camera after. A context created during
    // the walk in a higher slot runs in this same frame; one created in a
    // lower (freed) slot waits for the next.
    for (int i = 0; i < kContextSlots; ++i) runContext(i);
    // ...and the tail: the staged load, and what it releases.
    pumpTail();

    // One frame of the camera move, after the scripts have had their say -
    // a `camera.set` this frame starts moving next frame, which is what
    // `Camera_Request` queueing the request and the camera ticking in
    // `Game_Frame` amounts to.
    tickCamera();

    // `Script_PlayAllScripts`: one frame of every running object program.
    //
    // The delta is `Game_Frame`'s own: `30.0 / fps`, i.e. how many THIRTIETHS
    // OF A SECOND this frame took (docs/BOOT.md 4). Every clock in the engine
    // is in that unit, so this is the one place the real elapsed time enters.
    //
    // It used to be a flat 1.0 - one clip frame per RENDERED frame - which
    // silently ties the animation to the frame rate: a software-rasterised
    // character with 194 particles does not reach 30 fps, so a clip authored
    // at 30 played back at whatever the CPU managed and everything ran SLOW.
    // A reader saw it as "it looks like it has been slowed down". A frame-
    // bounded run leaves `frameSeconds_` at its 1/30 default, so this is
    // exactly 1.0 there and the headless checks stay deterministic.
    scene_.tick(static_cast<float>(frameSeconds_ * 30.0));

    // ...and where that leaves the player. Camera 0 - the one SCENE 55's
    // cutscene asks for - is relative to him, so without this the camera sits
    // wherever he was last teleported while the beats play out somewhere else.
    trackPlayer();

    // ...and the transition's object, if one was running: event 3 is raised
    // by the object runtime when the program ends - `Game_Tick`'s
    // `Game_RaiseEvent(3, ...)` loop, which sits BEFORE `Actors_TickAll`.
    transitionObjectEnded();

    // `Actors_TickAll` (05_sys.c:2178) -> the player's tick -> `Actor_
    // ScanZones`: AFTER the scripts, which is not a detail - the states this
    // leaves are what the NEXT frame's pump reads, and that one-frame offset
    // is the whole of the one-shot latch (case 7 writes 4, pump case 4
    // writes 5).
    scanZonesNow();
}

void Session::runContext(int i) {
    if (!ctxs_[static_cast<std::size_t>(i)]) return;
    processActions(i);
    if (!ctxs_[static_cast<std::size_t>(i)]) return;      // action 4 freed it
    execute(i);
}

// `Script_ProcessActions` (0x00408220): the watchdog, the status-9 retry, and
// ONE action dequeued when the context is idle.
void Session::processActions(int i) {
    Ctx* c = ctxs_[static_cast<std::size_t>(i)].get();
    // the 60-second transition watchdog, on the transition's own caller
    if (tr_.ctx == i && tr_.state != 0 && frameNo_ - tr_.startedFrame > kWatchdogFrames) {
        if (c->status == 10) c->status = 1;
        if (tr_.f2 != -1 && tr_.state != 5 && tr_.state != 6 && tr_.state != 9) {
            startTransitionObject(tr_.f2);
            c->status = 11;
        } else if (tr_.state == 5 || tr_.state == 6 || tr_.state == 9) {
            c->status = 11;
        }
        clearTransition();
        return;
    }
    // `if (g_DialogState == 3) return` is the frame-level gate above
    if (c->status == 9) c->status = 1;                    // the refused area.goto retries
    if (c->status) return;                                // still running something
    if (c->actions.empty()) return;
    const int act = c->actions.front();
    c->current = act;
    switch (act) {
    case 1: case 2: case 3: {
        const std::int32_t at = c->scripts[act - 1];
        if (at > 0 && static_cast<std::size_t>(at) < c->code.size()) {
            c->pc = static_cast<std::size_t>(at);
            c->started = false;
            c->vm.resetStack();
            c->status = 1;
        }
        break;
    }
    case 4:
        // detach from the prompt slots (the zone module's), then free
        freeContext(i);
        return;
    default:
        break;
    }
    c->actions.pop_front();
}

// The stubbed handlers' side effects, per recorded call - what a decision
// trace is made of on the Session's side.
void Session::startColourFade(int mode, std::uint32_t colour, float duration) {
    // `if (dword_536C1C && (a1 != 2 || dword_536C1C != 1)) return 0;`
    if (colourFade_.mode && (mode != 2 || colourFade_.mode != 1)) return;
    colourFade_.mode = mode;
    colourFade_.colour = colour;
    colourFade_.duration = duration;
    colourFade_.clock = 0.0f;
}

void Session::startBlackFade(bool fromBlack) {
    if (fromBlack) {
        blackFade_.mode = 3;             // fade IN - see ScreenFade's note
        blackFade_.colour = 0;
        blackFade_.duration = 60.0f;     // `flt_536C0C = 60.0`
        blackFade_.clock = 0.0f;
    } else if (blackFade_.mode == 3) {   // `else if (dword_536C18 == 3)`
        blackFade_.mode = 4;             // fade OUT, only from 3
        blackFade_.duration = 60.0f;
        blackFade_.clock = 0.0f;
    }
}

void Session::tickFades(float dt) {
    // Each ticker's own end rule, and they are NOT the same shape:
    //
    //   colour (0x00451FE0)  `if (mode == 1) clock = duration;`  a TO holds
    //                        `else mode = 0;`                    a FROM clears
    //   black  (0x00452046)  state 4 past its end -> `mode = 0`, and state 3
    //                        past its end keeps drawing at 0xFF, which under
    //                        the multiply is the scene untouched.
    //
    // So 1 and 3 hold and 2 and 4 clear - which is the two that end ON the
    // scene's own colour holding harmlessly, and the two that end on it
    // stopping. Holding a mode-3 black fade is what a first version got
    // backwards, and it painted every cutscene black.
    for (ScreenFade* f : {&colourFade_, &blackFade_}) {
        if (!f->mode) continue;
        f->clock += dt;
        if (f->clock > f->duration) {
            if (f->mode == 2 || f->mode == 4) f->mode = 0;
            else f->clock = f->duration;
        }
    }
}

void Session::onCall(int i, const Call& call) {
    Ctx* c = ctxs_[static_cast<std::size_t>(i)].get();
    switch (call.op) {
    // ---- THE SCREEN FADES ------------------------------------------------
    //
    // Two independent ones, and the port had neither. `Screen_StartColorFade`
    // (0x00451DC0) owns the COLOUR fade: mode 1 to, 2 from, and it REFUSES a
    // new one while another runs unless the new one is a "from" over a running
    // "to". The ticker (0x00451E60) ramps it LINEARLY -
    // `alpha = clock * 255 / duration` rising for a "to" and
    // `255 - clock * 255 / duration` falling for a "from" - and submits a
    // full-screen quad. `Screen_Fade` (0x0041E1B0) owns the BLACK one: state 3
    // to black over a fixed **60** frames, state 4 back, and only from 3.
    //
    // 118/119 pack the colour as a DWORD out of their first four operand
    // bytes, which is why the disassembler's four int16 view reads the
    // Impasse's opening fade as `-1, 255, 25, 0`: the bytes are
    // `FF FF FF 00 19 00 00 00`, so the colour is 0x00FFFFFF (WHITE) and the
    // duration is 25.
    case 118: case 119: {
        if (call.fields.size() < 3) break;
        const auto lo = static_cast<std::uint32_t>(call.fields[0]) & 0xFFFFu;
        const auto hi = static_cast<std::uint32_t>(call.fields[1]) & 0xFFFFu;
        std::uint32_t colour = lo | (hi << 16);
        // `byte_4C012C` against the context's own +30: a fade issued by the
        // MESSAGE-0 handler is forced to pure red. The bookkeeping was already
        // here with a named reader and nothing to consume it; this consumes it.
        if (message0Ctx_ >= 0 && c && message0Ctx_ == c->slot) colour = 0xFF0000u;
        startColourFade(call.op == 118 ? 1 : 2, colour,
                        static_cast<float>(call.fields[2]));
        break;
    }
    // 132 is the fade IN and 133 the fade OUT, whatever the table calls them
    case 132: startBlackFade(true);  break;
    case 133: startBlackFade(false); break;
    case 103:
        // `music.play`: field 0 is the track, field 1 the loop flag.
        // `Music_PlayTrack` refuses anything below 2, so 0 and 1 are the
        // engine's own "stop", not a file it failed to find.
        if (call.fields.size() >= 2) {
            musicTrack_ = call.fields[0];
            musicLoop_  = call.fields[1] != 0;
        }
        break;
    case 56:
        // `player.become`: the soul transfer - see `becomePlayer`.
        if (!call.fields.empty()) becomePlayer(call.fields[0]);
        break;
    case 71:
        // `scene.load` / `scene.unload`: on a RESIDENT area the swap is
        // immediate - see the two bodies. `sceneLoad` may free THIS context
        // (a scene's own script reloading its area); the run has already
        // happened, which is the order the handler's `Mem_Free` of the
        // running context amounts to. `execute` checks for that.
        if (call.fields.size() >= 2) sceneLoad(call.fields[0], call.fields[1]);
        break;
    case 72:
        if (!call.fields.empty()) sceneUnload(call.fields[0], i);
        break;
    case 78: {
        // `character.show` / `character.hide`: a character is on screen
        // between them, which is well before `dialog.start`. Both handlers
        // (0x403CB0 / 0x403DD0) resolve the 20-byte record (`sub_40D6A0`),
        // give up on an id of -1 or a record whose actor index is -1, show
        // or remove the model (`sub_41CCA0` / `sub_41CDD0`) and write the
        // record's +18 bit with `State_SetBit(idx, 1 / 0)` - the
        // `ObjectShown` map at DB +20, which `Actors_SpawnFromTables` reads
        // back on every area load and which travels in the save (issue 28).
        if (call.fields.empty()) break;
        const int id = call.fields[0];
        showCharacter(id);
        const int bit = shownBitOf(id);
        if (bit >= 0) state_.setBit(StateArray::ObjectShown, bit, 1);
        break;
    }
    case 79: {
        // -1 is the player: `cmp esi, -1 ; jz loc_403E61` ->
        // `sub_41CED0(Actor_Player(), 1)`. AREA 118 does exactly that at pc
        // 1050, three bytes after `player.become 136`, so the body the
        // player just took leaves the screen. No bit is written for him.
        if (call.fields.empty()) break;
        const int id = call.fields[0] == -1 ? playerActor() : call.fields[0];
        hideCharacter(id);
        const int bit = call.fields[0] == -1 ? -1 : shownBitOf(id);
        if (bit >= 0) state_.setBit(StateArray::ObjectShown, bit, 0);
        break;
    }
    case 104:
        // `player.anim.hold`: `Actor_HoldAnimation(Actor_Player(), 1)` - the
        // 0x81 bits (area.h `playerAnimHeld`). The handler tests the dry-run
        // marker first (`mov eax, dword_6A05E0`), which is dead here
        // (SCRIPT_VM: the marker is never 1 on a path that reaches a handler).
        playerAnimHeld_ = true;
        break;
    case 105:
        // `player.anim.release`: the same with 0, and the accumulator cleared -
        // which is why he starts from the rest pose.
        playerAnimHeld_ = false;
        break;
    case 73:
        // `actor.goto_address`: PLACE the actor at the address its operand
        // names. `Address_Find` resolves the id against both resident AREAs'
        // `+60` tables and the result goes to the actor through `sub_41BF50`.
        // Only the player is modelled, and the intro's own `actor.goto_address
        // 654` is what gives him a world position at all.
        if (!call.fields.empty()) placeActorAt(call.fields[0]);
        break;
    case 95:
        // `camera.set`: field 0 is the camera, field 1 the length of the
        // move in frames. 0 cuts.
        if (!call.fields.empty())
            applyCamera(call.fields[0], call.fields.size() >= 2 ? call.fields[1] : 0);
        break;
    case 96:
        // `camera.set.wait` issues the SAME request and, with the hold on,
        // is handled where it parks - so it is applied here only when the
        // interpreter ran through it.
        if (!camWait_ && !call.fields.empty())
            applyCamera(call.fields[0], call.fields.size() >= 2 ? call.fields[1] : 0);
        break;
    case 126:
        // `camera.set.at_address` issues the same mode-12 request; with the
        // hold on it is applied where it parks, so this is only the
        // ran-through path. Fields are [camera][address][travel].
        if (!camWait_ && call.fields.size() >= 3) {
            applyCamera(call.fields[0], call.fields[2]);
            camSubjectAddress_ = call.fields[1];
        }
        break;
    default:
        break;
    }
}

// `Script_Execute` (0x00406460):
//
//     while (status == 1) {
//         op = *pc++;
//         if ((op == 45 || op == 47) && dword_4C0130 != -1) break;   // refused
//         handler(ctx);
//         if (op == 61) return;
//         if (status != 1) goto done;
//     }
//     pc = pc - 1; if (op == 47) status = 9;                          // the refusal
//
// The interpreter runs ONE instruction per call here - the span it is given
// ends at the instruction's last operand byte, so its next fetch is out of
// range and it reports the pc it reached - because the refusal has to look
// at the opcode BEFORE dispatch, and the transition has to write the status
// word AFTER it, and neither is something a whole-script run can be asked
// about afterwards. The cost is one call per instruction; the scripts are
// tens of instructions a frame.
void Session::execute(int i) {
    Ctx* c = ctxs_[static_cast<std::size_t>(i)].get();
    if (c->status == 7) {
        // `camera.set.wait` wrote 7: held until the move it started ends,
        // and the move is counted in frames (`Game_HandleEvent` case 4).
        if (c->waitingForCamera > 0) --c->waitingForCamera;
        if (c->waitingForCamera <= 0) c->status = 1;      // runs next frame
        return;
    }
    if (c->status == 3) return;      // the fight: only `fightEnded` releases it
    if (c->status == 4 && c->waitingForMove >= 0) return;  // ...and the walk
    if (c->status == 4) {
        // parked on a scene object's program until it ends (event 3)
        if (c->waitingForProgram >= 0 && scene_.programRunning(c->waitingForProgram)) return;
        c->waitingForProgram = -1;
        c->status = 1;
    }
    if (c->status != 1) {
        // `Script_Execute`'s else arm: LABEL_8 runs for any status but 1 -
        // and does something only for an activate whose status is 0
        if (c->status == 0) finishRun(c);
        return;
    }
    if (c->message >= 0 && !c->started &&
        static_cast<std::size_t>(c->message) < messages_.size() &&
        messages_[static_cast<std::size_t>(c->message)].ranFrame < 0)
        messages_[static_cast<std::size_t>(c->message)].ranFrame = frameNo_;

    Interpreter& vm = c->vm;
    vm.setRecordAll(true);
    std::size_t steps = 0;
    while (c->status == 1) {
        if (c->pc >= c->code.size()) { c->status = 0; finishRun(c); return; }
        const auto op = static_cast<std::uint8_t>(c->code[c->pc]);
        // the pre-dispatch refusal: one staged load at a time
        if ((op == 45 || op == 47) && deferred_ != -1) {
            if (op == 47) c->status = 9;
            return;
        }
        const int n = table_.operandLength(op);
        if (n < 0 || c->pc + 1 + static_cast<std::size_t>(n) > c->code.size()) {
            c->status = 0;                                // an invalid instruction
            finishRun(c);
            return;
        }
        const std::span<const std::byte> one(c->code.data(),
                                             c->pc + 1 + static_cast<std::size_t>(n));
        const std::size_t before = c->pc;
        const auto r = c->started ? vm.resume(one, c->pc) : vm.run(one, c->pc);
        c->started = true;
        c->pc = r.pc;
        // `zone.enable`/`zone.disable` re-register IN THE HANDLER - its tail is
        // `call sub_406560`, `Zones_RegisterAll` (interp.h `zonesDirty`). The
        // live list is a snapshot filtered by the save bit at registration, so
        // the bit alone is inert until the next area load: AREA 222's tutorial
        // disables ITSELF as its last act and re-fired on every entry without
        // this. Done here, before the calls are dispatched, because the rebuild
        // also prunes contexts whose zone has gone.
        if (r.zonesDirty) zonesRegisterAll();
        if (!ctxs_[static_cast<std::size_t>(i)]) return;   // pruned by that
        record(r.calls);
        // `or byte ptr [ctx+28h], 10h` - the 24 visible handlers set it on
        // entry, before their dry-run test, and nothing ever clears it
        for (const auto& call : r.calls)
            if (visibleOp(call.op)) c->flags40 |= 0x10;
        for (const auto& call : r.calls) onCall(i, call);
        if (!ctxs_[static_cast<std::size_t>(i)]) return;  // freed by its own scene.load
        // every `scx.play*` starts its object on the resident scene; a
        // WAITING variant parks this context on the program it started
        const int waitOn = scene_.handle(r.calls);
        // the three transition opcodes, after their handler's announcement
        for (const auto& call : r.calls) {
            if (call.op == 47 && call.fields.size() >= 3) {
                if (!areaTransition(0, i, c->slot, call.fields[0], call.fields[1], call.fields[2]))
                    c->pc = before;                       // `add [esi+0Ch], -7`
            } else if (call.op == 45 && !call.fields.empty()) {
                // handler 0x402AB0: nothing when the ACTIVE slot holds it;
                // the target is the active slot when that is empty, else
                // the other; if the target already holds it `sub_419AF0`
                // and return; else `Area_LoadIntoSlot`, status 8, and the
                // deferral word.
                const int area = call.fields[0];
                if (slots_[active_].area == area) continue;
                const int target = slots_[active_].area == -1 ? active_ : 1 - active_;
                if (slots_[target].area == area) { showSet(area); continue; }
                loadIntoSlot(target, area);
                c->status = 8;
                deferred_ = i;
            } else if (call.op == 48 && !call.fields.empty()) {
                areaTransition(1, i, active_, call.fields[0], -1, -1);
            }
        }
        switch (r.status) {
        case RunStatus::PcOutOfRange:
            // the single-step's normal outcome: one instruction executed
            // (steps 2: the fetch that ran it and the one that fell off).
            // steps 1 is the chunk ending inside the instruction.
            if (r.steps >= 2) break;
            c->status = 0;
            finishRun(c);
            return;
        case RunStatus::End: {
            // Opcode 3 `end`, handler 0x401B90 (`sub_401B90`, RAW in
            // readable/ - transcribed here, issue 38):
            //
            //     v1 = u8(ctx, 40);
            //     byte_4C012C = 0xFF;               // the message-0 marker
            //     u16(ctx, 22) = 0;                 // status 0
            //     if ((v1 & 8) && dword_69BC44[u8(ctx,31)]) {
            //         Mem_Free(dword_69BC44[slot]); dword_69BC44[slot] = 0;
            //     }                                 // scene.unload's deferred free
            //     if (u32(ctx, 32) == 1) {
            //         if (dword_4E6C7C) dword_4E6C7C = 0;   // the boot context forgotten
            //     } else if (u32(ctx, 32) == 2) {
            //         --dword_4E6B20;
            //         if (dword_4E6C8C) {           // an object was just taken in hand
            //             if (Actor_HeldObjectSlot(Actor_Player()) != -1 &&
            //                 state != 3 && state != 15 && !dword_4E6B20)
            //                 { dword_91068C = -1; sub_41C770(); }
            //             dword_4E6C8C = 0;
            //         }
            //         u32(ctx, 32) = 0;             // the activate dedupe released
            //     }
            //
            // The held-object tail (`dword_4E6C8C`, set by the inventory
            // channel when an object is loaded into the hand; `sub_41C770`,
            // the put-away through `.CTL` group 150 and event 47) is NOT
            // modelled - there is no held object in the Session's 3D -
            // and is labelled here rather than pretended.
            message0Ctx_ = -1;
            c->status = 0;
            if (c->flags40 & 8) {
                ResidentSlot& sl = slots_[c->slot & 1];
                if (!sl.sceneChunk.empty()) {
                    // The engine frees whatever block the slot holds NOW -
                    // a scene loaded over it since would go too (and be
                    // read after). The port frees only the unloaded one.
                    if (sl.scene == -1) { sl.sceneChunk.clear(); ++sceneBlocksFreed_; }
                }
                c->flags40 &= static_cast<std::uint8_t>(~8);
            }
            if (c->current == 1) {
                if (bootCtx_ != -1) bootCtx_ = -1;
            } else if (c->current == 2) {
                --activatesPending_;
                c->current = 0;
            }
            return;
        }
        case RunStatus::Dialog:
            // `if (v4 == 61) return;` - THIS context stops for the frame with
            // its status still 1, and `Script_ProcessActions`' g_DialogState
            // gate is what keeps it (and every other) from running again
            // until the conversation closes. The pump's loop goes on.
            dialogState_ = 3;
            for (const auto& call : r.calls)
                if (call.op == 61 && !call.fields.empty()) openDialog(call.fields[0]);
            return;
        case RunStatus::UiOpen:
            c->status = 6;
            if (personAnswers_) {
                // A PERSON is going to answer this one: park the context and
                // say which screen is waiting. Nothing resumes it until
                // `answerUi`, which is exactly what `Game_HandleEvent` case 5
                // does - so a build with nobody at the keyboard sits here,
                // which is the engine's behaviour and not a hang.
                pendingUiScreen_ = r.uiScreen;
                pendingUiParam_  = r.uiParam;
                pendingUiVar_    = r.uiResultVar;
                pendingUiCtx_    = i;
                return;
            }
            {
                // Walk the screen for its answer, write the named variable,
                // and let the context resume next frame from the pc after the
                // `ui.open`. A walk with NO answer is a screen the person
                // LEAVES, and that resumes the script too: `UI_OpenScreen`
                // seeds `dword_930750` at -1, every close path posts event 5
                // with whatever it holds, and case 5 does `Var_Set(var,
                // answer)` and writes status 1 unconditionally (issue 11).
                const int v = answerScreen(r.uiScreen, r.uiParam);
                uiAnswers_.push_back({r.uiScreen, r.uiResultVar, v, v >= 0});
                if (r.uiResultVar >= 0) state_.setVar(r.uiResultVar, v >= 0 ? v : -1);
                c->status = 6;
                uiAnswered(i, v >= 0 ? v : -1);
            }
            return;
        case RunStatus::ObjectWait:
            if (const char* e = std::getenv("OMK_CAMLOG"))
                if (*e == '1')
                    std::fprintf(stderr, "[obj] frame %ld  op %d  program %d  "
                                 "(%zu started, %zu missed)\n", frameNo_,
                                 r.objectWaitOp, waitOn, scene_.started().size(),
                                 scene_.missed().size());
            // Park on the program - but only if it actually started: with no
            // scene resident the engine has nothing to release the script
            // either, and a replica that parked anyway would stop where the
            // game runs on. That is the difference between modelling the wait
            // and inventing a deadlock. Either way this frame's run ends.
            if (waitOn >= 0) { c->status = 4; c->waitingForProgram = waitOn; }
            return;
        case RunStatus::MoveWait:
            // `Player_GoToMove(address, ctx+30)` and then status 4 - two
            // halves of one instruction, and the port does both or neither.
            // No hook: nothing could raise case 3 either, so the script runs
            // on rather than parking for ever (the `ObjectWait` rule).
            if (!startPlayerMove(r.moveAddress, i)) break;
            c->status = 4;
            c->waitingForMove = r.moveAddress;
            return;
        case RunStatus::FightWait:
            // status 3, then `Camera_Request(14)` with max(field 1, 0)
            // frames. **Camera mode 14 is not modelled** - the travel is
            // recorded and the camera left alone rather than pointed somewhere
            // invented. Field 1 is 0 at all 108 shipped sites.
            if (!beginFight(r.fightOpponent)) break;
            fightCamTravel_ = r.fightCamTravel;
            c->status = 3;
            c->fightOpponent = r.fightOpponent;
            return;
        case RunStatus::CameraWait:
            // The 96 handler calls `Camera_FindWorld` FIRST and on 0 skips
            // everything (`jz loc_404CCB`): no request and no status 7, so
            // the script runs on. Only a camera some resident table has is
            // issued and held for its length - both halves of one
            // instruction, `Camera_Request` mode 12 and then status 7.
            if (!findCamera(r.camId)) break;
            applyCamera(r.camId, r.camTravel);
            // 96's subject is `Actor_Player()` twice; 126's is
            // `Address_Find(field 1)` in both pointers, so a camera whose
            // points are OFFSETS frames that address instead of the player.
            camSubjectAddress_ = (r.camWaitOp == 126) ? r.camAddress : -1;
            c->status = 7;
            c->waitingForCamera = r.camTravel;
            // 126 has NO travel guard: a 0-frame cut still parks, and the
            // countdown at the head of this function releases it next frame -
            // one frame of hold, not none and not for ever. 96 never reaches
            // here at 0.
            return;
        default:
            // Runaway, UnknownOpcode, StackUnderflow: the script is over
            c->status = 0;
            finishRun(c);
            return;
        }
        if (++steps > kRunaway) { c->status = 0; finishRun(c); return; }
    }
}

// `Script_Execute`'s LABEL_8, reached for a context whose status is not 1
// after a handler (or on entry):
//     if (u32(a1,32) != 1 && u32(a1,32) == 2 && !u16(a1,22)) {
//         --dword_4E6B20; ...the held-object tail...; u32(a1,32) = 0;
//     }
// `end` has already done this for itself (it writes +32 = 0 first), so this
// is the arm for a status that reached 0 some OTHER way.
void Session::finishRun(Ctx* c) {
    if (!c || c->status != 0 || c->current != 2) return;
    --activatesPending_;
    c->current = 0;
}

bool Session::visibleOp(std::uint8_t op) {
    switch (op) {
    case 46: case 57: case 58: case 59: case 61: case 62: case 63: case 70:
    case 73: case 89: case 90: case 92: case 94: case 95: case 96: case 118:
    case 119: case 122: case 123: case 126: case 129: case 130: case 138:
    case 139:
        return true;
    default:
        return false;
    }
}

// ------------------------------------------------------------ THE ZONES

bool Session::pressAction() {
    if (dialogState_ == 3 || zones_.armedCount() == 0) return false;
    actionPressed_ = true;
    return true;
}

// `Script_Pump(1)`, steps 1 and 2 (0x00407DC0; readable/src/01_file.c:2072).
//
//     dword_4E61E0 = 0;  dword_4E66B8 = 1;
//     for (16 prompt slots) switch (state) { ... }      // step 1
//     if (dword_4E6C90 && !dword_4E61E0) {              // step 2
//         if (Actor_HeldObjectSlot(Actor_Player()) == -1) {
//             if (dword_4E66B8) { evArgs[0] = 26; Game_HandleEvent(43, evArgs); }
//         } else if (state != 3 && state != 15) { dword_91068C = -1; sub_41C770(); }
//     }
//     dword_4E6C90 = 0;
//
// The registry runs the slot loop and reports what each arm queues; this
// makes the contexts and queues the actions (step 1), then decides the
// "nothing here" message (step 2).
//
// **Issue 7's dry-run marker, read out of the assembly and found to decide
// nothing.** `dword_4E66B8` is cleared only by `Script_Run(ctx)` returning
// 1 - the dry run of the activate script hitting an opcode that ORs 0x10
// into ctx+40 - and that store sits inside the same case-2 arm that does
// `++dword_4E61E0` (asm 11045-11049: the two writes are adjacent, no branch
// between). Its ONLY reader is the `if (dword_4E66B8)` above, which is
// inside `!dword_4E61E0`. So whenever the reader runs the flag is still 1:
// the dry run's result is unobservable, and message 26 posts exactly when
// the press arrived (a slot was taken - event 6's gate) and no slot in
// state 2 had an activate script. The port therefore runs NO dry run, and
// carries bit 0x10 only as the sticky per-context fact it is.
void Session::pumpZoneSlots() {
    // which prompt slot holds which zone - the slots that leave this frame
    // are released by the time the events come back
    std::array<int, 16> ctxOf{};
    std::map<std::int16_t, int> slotOfZone;
    for (int k = 0; k < 16; ++k) {
        const auto& ps = zones_.promptSlots()[static_cast<std::size_t>(k)];
        ctxOf[static_cast<std::size_t>(k)] = promptCtx_[static_cast<std::size_t>(k)];
        if (ps.zone != -1) slotOfZone[ps.zone] = k;
    }
    const bool pressed = actionPressed_;           // dword_4E6C90
    std::vector<ZoneEvent> ev;
    zones_.pumpSlots(pressed, ev);
    int ran = 0;                                   // dword_4E61E0
    const bool log = [] { const char* e = std::getenv("OMK_CAMLOG"); return e && *e == '1'; }();
    for (const auto& e : ev) {
        if (e.kind == ZoneEvent::Kind::Touch) continue;
        const auto it = slotOfZone.find(e.zone);
        const int k = it == slotOfZone.end() ? -1 : it->second;
        int idx = k >= 0 ? ctxOf[static_cast<std::size_t>(k)] : -1;
        Ctx* c = (idx >= 0 && idx < kContextSlots) ? ctxs_[static_cast<std::size_t>(idx)].get() : nullptr;
        ZoneApplied a;
        a.frame = frameNo_; a.kind = e.kind; a.zone = e.zone; a.action = e.action; a.script = e.script;
        switch (e.kind) {
        case ZoneEvent::Kind::Arm:
            // case 1: `ctx = u32(slot,0); if (ctx && u16(ctx,42) == zoneId &&
            // u8(ctx,24) == 4) { u8(ctx,24) = 0; --u16(ctx,28); }` - the zone
            // re-armed while its FREE was still at the head of the FIFO:
            // cancel the free and keep the context, its stack included.
            // Else `Script_NewContext(areaSlot, s[0], s[1], s[2])`, the zone
            // id into +42, the pointer into the slot's +0.
            if (c && c->zoneId == e.zone && !c->actions.empty() && c->actions.front() == 4) {
                c->actions.pop_front();
                a.reused = true;
            } else {
                idx = newContext(e.slot, e.code, e.scripts, e.zone, slots_[e.slot & 1].area);
                if (k >= 0) promptCtx_[static_cast<std::size_t>(k)] = idx;
            }
            // `if (zoneScripts[0]) Script_QueueAction(ctx, 1)`
            if (e.action && idx >= 0) a.queued = queueAction(idx, e.action);
            break;
        case ZoneEvent::Kind::Activate:
            // case 2, `ran`: `++dword_4E61E0`, then `if (Script_QueueAction
            // (ctx, 2)) ++dword_4E6B20`
            ++ran;
            if (idx >= 0) {
                a.queued = queueAction(idx, 2);
                if (a.queued) ++activatesPending_;
            }
            break;
        case ZoneEvent::Kind::Leave:
        case ZoneEvent::Kind::Free:
            // case 3/5: both unconditional; the slot is released by the
            // registry (`+10 = -1; +8 = 0; --dword_4E6B24`) and its +0 is
            // left pointing at the context, as the engine leaves it
            if (idx >= 0) a.queued = queueAction(idx, e.action);
            break;
        default:
            break;
        }
        a.ctx = idx;
        zoneLog_.push_back(a);
        if (log)
            std::fprintf(stderr, "[zone] frame %ld  %s zone %d -> context %d action %d @%zu%s%s\n",
                         frameNo_,
                         e.kind == ZoneEvent::Kind::Arm ? "ARM" :
                         e.kind == ZoneEvent::Kind::Activate ? "ACTIVATE" :
                         e.kind == ZoneEvent::Kind::Leave ? "LEAVE" : "FREE",
                         e.zone, idx, e.action, e.script,
                         a.queued ? "" : " (not queued)", a.reused ? " (reused)" : "");
    }

    // ---- step 2: the unconsumed press
    if (pressed && ran == 0) {
        if (!heldObject_) {
            // `evArgs[0] = 26; if (!byte_910309) Game_HandleEvent(43, evArgs)`
            // - evArgs[1], the sender, is never written (stack garbage);
            // `byte_910309` is the events-off byte, not modelled.
            const bool handled = postMessage(26, -1);
            nothingHere_.push_back({frameNo_, handled});
            if (log)
                std::fprintf(stderr, "[zone] frame %ld  unconsumed press -> message 26 (%s)\n",
                             frameNo_, handled ? "handled" : "no handler");
        } else {
            // **RECONSTRUCTION, labelled**: `sub_41C770()` - the held
            // object put away through `.CTL` group 150 - unless the player
            // is in state 3/15. No held object exists in this Session's 3D.
        }
    }
    actionPressed_ = false;                        // dword_4E6C90 = 0
}

// `Actor_ScanZones(actor)` for the player, from `Actors_TickAll`'s dispatch
// on his ACTOR_STATE - and not at all while a scene program owns him.
void Session::scanZonesNow() {
    // **RECONSTRUCTION, labelled**: the scan is part of the ACTOR TICK, which
    // this Session does not run - so it runs only once something outside is
    // moving the player (`setPlayerPosition`: E2's controller, a probe). A
    // player who has only ever been TELEPORTED is not scanned. The intro
    // shows why the alternative is wrong: `actor.goto_address 654` lands
    // him inside AREA 222's zones 3799 and 3801, and 3801's enter script is
    // `area.goto 142` - in the engine the Impasse's beats carry him away
    // (state 4/5, no scan) before any walker frame, and a headless run with
    // no scene would otherwise leave for area 142 at frame 6.
    if (!playerPlaced_ || !playerWalks_ || playerDriven_) return;
    const double p[3] = {playerPos_[0], playerPos_[1], playerPos_[2]};
    std::vector<ZoneEvent> ev;
    zones_.scanZones(p, playerYaw_, ev);
    for (const auto& e : ev) {
        if (e.kind != ZoneEvent::Kind::Touch) continue;
        // `Game_HandleEvent` case 8: a touched zone whose +66 is not -1 hands
        // it to `Camera_FindWorld`, fills the request block directly and
        // `Camera_Request(12, ...)` - a CUT, no travel length is involved.
        // Each touch overwrites the same block, so the last one wins.
        if (e.camera != -1) applyCamera(e.camera, 0);
    }
}

// ------------------------------------------------------ THE WORLD HOOKS

int Session::objectSlotId(int slot) const {
    return slot >= 0 && slot < 50 ? objectSlotIds_[static_cast<std::size_t>(slot)] : -1;
}

int Session::heldSlotOf(int actor) const {
    const auto it = heldSlot_.find(actor);
    return it == heldSlot_.end() ? -1 : it->second;
}

// `Scene_LoadProps(area, scene, 1)` (0x00409FC0): for every prop record
// whose state has bit 0, the FIRST free entry of word_4E6CA0 (-1 when all 50
// are taken) takes the id and the record's +0 takes the slot; `Object_Load`,
// `Object_SetPlacement`, and bit 1 links it into the scene. AREA's table
// (+44 / +74) first, then the SCENE's (+12 / +42), 24 bytes a record.
void Session::loadProps(int slot, bool area, bool scene) {
    ResidentSlot& s = slots_[slot & 1];
    const auto walk = [&](std::vector<std::byte>& chunk, ChunkKind kind) {
        if (chunk.empty()) return;
        const std::size_t po = kind == ChunkKind::Area ? 44 : 12;
        const std::size_t co = kind == ChunkKind::Area ? 74 : 42;
        const std::span<const std::byte> b(chunk);
        if (b.size() < co + 2) return;
        const std::size_t p = u32at(b, po);
        const int n = i16at(b, co);
        if (n <= 0 || p + 24u * static_cast<std::size_t>(n) > b.size()) return;
        for (int i = 0; i < n; ++i) {
            const std::size_t o = p + 24u * static_cast<std::size_t>(i);
            const int id = i16at(b, o + 2), idx = i16at(b, o + 22);
            if (!(state_.propState(idx) & 1)) continue;
            int k = -1;
            for (int q = 0; q < 50; ++q)
                if (objectSlotIds_[static_cast<std::size_t>(q)] == -1) { k = q; break; }
            if (k >= 0) objectSlotIds_[static_cast<std::size_t>(k)] = id;
            chunk[o]     = static_cast<std::byte>(k & 0xFF);
            chunk[o + 1] = static_cast<std::byte>((k >> 8) & 0xFF);
            if (k >= 0 && (state_.propState(idx) & 2)) shownSlots_.insert(k);
        }
    };
    if (area && s.area != -1) walk(s.areaChunk, ChunkKind::Area);
    if (scene && s.scene != -1) walk(s.sceneChunk, ChunkKind::Scene);
}

// `Scene_UnloadProps` (the function at 01_file.c:3955): for every record
// with state bit 0, `sub_41CB30(+0)` frees the object and
// `word_4E6CA0[+0] = -1`. The record's +0 is left as written.
void Session::unloadProps(int slot, bool area, bool scene) {
    ResidentSlot& s = slots_[slot & 1];
    const auto walk = [&](const std::vector<std::byte>& chunk, ChunkKind kind) {
        if (chunk.empty()) return;
        const std::size_t po = kind == ChunkKind::Area ? 44 : 12;
        const std::size_t co = kind == ChunkKind::Area ? 74 : 42;
        const std::span<const std::byte> b(chunk);
        if (b.size() < co + 2) return;
        const std::size_t p = u32at(b, po);
        const int n = i16at(b, co);
        if (n <= 0 || p + 24u * static_cast<std::size_t>(n) > b.size()) return;
        for (int i = 0; i < n; ++i) {
            const std::size_t o = p + 24u * static_cast<std::size_t>(i);
            const int idx = i16at(b, o + 22);
            if (!(state_.propState(idx) & 1)) continue;
            const int k = i16at(b, o);
            if (k < 0 || k >= 50) continue;
            objectSlotIds_[static_cast<std::size_t>(k)] = -1;
            shownSlots_.erase(k);
            for (auto it = heldSlot_.begin(); it != heldSlot_.end();)
                it = it->second == k ? heldSlot_.erase(it) : std::next(it);
        }
    };
    // `Scene_UnloadProps(area, scene, ...)` is handed the IDS: a block kept
    // under ctx+40 bit 8 has id -1 and is not walked (its slots went when it
    // was unloaded; walking it again would free whatever took them since).
    if (area && s.area != -1) walk(s.areaChunk, ChunkKind::Area);
    if (scene && s.scene != -1) walk(s.sceneChunk, ChunkKind::Scene);
}

std::span<std::byte> Session::Hooks::record(int actor) {
    // `Actor_FindById`: row 0 then row 1, the AREA table when `v2[2] >= 0`
    // and the SCENE's when `v2[3] >= 0`
    for (auto& s : s_->slots_) {
        if (s.area < 0) continue;
        if (auto o = findActorRecord(s.areaChunk, ChunkKind::Area, actor))
            return std::span<std::byte>(s.areaChunk).subspan(*o, kActorRecordSize);
        if (s.scene != -1)
            if (auto o = findActorRecord(s.sceneChunk, ChunkKind::Scene, actor))
                return std::span<std::byte>(s.sceneChunk).subspan(*o, kActorRecordSize);
    }
    return {};
}

bool Session::Hooks::inObjectTable(int actor) const {
    for (const auto& s : s_->slots_) {
        if (s.area < 0) continue;
        if (findCharacterRecord(s.areaChunk, ChunkKind::Area, actor)) return true;
        if (s.scene != -1 && findCharacterRecord(s.sceneChunk, ChunkKind::Scene, actor)) return true;
    }
    return false;
}

bool Session::Hooks::getActorProperty(int actor, int property, std::int32_t& out) {
    if (!inObjectTable(actor)) return false;
    const auto r = record(actor);
    return !r.empty() && readActorProperty(r, property, out);
}

bool Session::Hooks::setActorProperty(int actor, int property, std::int32_t value) {
    if (!inObjectTable(actor)) return false;
    const auto r = record(actor);
    return !r.empty() && writeActorProperty(r, property, value);
}

int Session::Hooks::heldObjectField(int actor) {
    const auto r = record(actor);
    return r.empty() ? -1 : heldObjectOf(r);
}

void Session::Hooks::setHeldObjectField(int actor, int objectId) {
    const auto r = record(actor);
    if (!r.empty()) setHeldObjectOf(r, objectId);
}

int Session::Hooks::heldObjectSlot(int actor) { return s_->heldSlotOf(actor); }

int Session::Hooks::objectIdInSlot(int slot) { return s_->objectSlotId(slot); }

void Session::Hooks::clearObjectSlot(int slot) {
    if (slot >= 0 && slot < 50) s_->objectSlotIds_[static_cast<std::size_t>(slot)] = -1;
}

// `Actor_ReleaseObject(index, remove)`: the slot leaves the hand. Dropped
// (remove false) it stays a scene object at its stored placement; removed it
// is freed - `sub_418DC0(4, slot)` - and leaves the shown set too.
void Session::Hooks::releaseObject(int actor, bool remove) {
    const auto it = s_->heldSlot_.find(actor);
    const int slot = it == s_->heldSlot_.end() ? -1 : it->second;
    if (it != s_->heldSlot_.end()) s_->heldSlot_.erase(it);
    if (remove && slot >= 0) s_->shownSlots_.erase(slot);
    s_->propEvents_.push_back({remove ? "remove" : "drop", actor, slot, -1, s_->frameNo_});
}

void Session::Hooks::holdObject(int actor, int slot) {
    s_->heldSlot_[actor] = slot;
    s_->propEvents_.push_back({"hold", actor, slot, -1, s_->frameNo_});
}

void Session::Hooks::showObject(int slot) {
    s_->shownSlots_.insert(slot);
    s_->propEvents_.push_back({"show", -1, slot, -1, s_->frameNo_});
}

void Session::Hooks::hideObject(int slot) {
    s_->shownSlots_.erase(slot);
    s_->propEvents_.push_back({"hide", -1, slot, -1, s_->frameNo_});
}

bool Session::Hooks::propBySlot(int slot, PropRef& out) {
    // the 67/68/76 walk: `Area_Block(area)`'s table, then the SCENE over it -
    // for the ACTIVE area (`dword_69BC64`), then nothing else
    for (const auto& s : s_->slots_) {
        if (s.area < 0) continue;
        auto p = findPropBySlot(s.areaChunk, ChunkKind::Area, slot);
        if (!p && s.scene != -1) p = findPropBySlot(s.sceneChunk, ChunkKind::Scene, slot);
        if (p) { out = {p->slot, p->id, p->stateIndex}; return true; }
    }
    return false;
}

bool Session::Hooks::propById(int id, PropRef& out) {
    for (const auto& s : s_->slots_) {
        if (s.area < 0) continue;
        auto p = findPropById(s.areaChunk, ChunkKind::Area, id);
        if (!p && s.scene != -1) p = findPropById(s.sceneChunk, ChunkKind::Scene, id);
        if (p) { out = {p->slot, p->id, p->stateIndex}; return true; }
    }
    return false;
}

void Session::Hooks::placeObjectAt(int objectId, int address) {
    s_->propEvents_.push_back({"place", -1, objectId, address, s_->frameNo_});
}

// Every prop of both resident chunks the loader would have loaded, with the
// placement `Area_Load` converted in place. See `Session::PropInstance`.
std::vector<Session::PropInstance> Session::props() const {
    std::vector<PropInstance> out;
    const auto walk = [&](std::span<const std::byte> chunk, ChunkKind kind) {
        if (chunk.empty()) return;
        // The table's own count, the way `findPropById` reaches it.
        for (int id = 0;; ++id) {
            (void)id;
            break;                      // ids are not dense; walk the table
        }
        const std::size_t base = kind == ChunkKind::Area ? 44u : 12u;
        const std::size_t cnt  = kind == ChunkKind::Area ? 74u : 42u;
        if (chunk.size() < cnt + 2) return;
        const int n = static_cast<std::int16_t>(
            static_cast<unsigned char>(chunk[cnt]) |
            (static_cast<unsigned char>(chunk[cnt + 1]) << 8));
        std::uint32_t off = 0;
        for (int k = 0; k < 4; ++k)
            off |= static_cast<std::uint32_t>(static_cast<unsigned char>(chunk[base + static_cast<std::size_t>(k)])) << (8 * k);
        for (int i = 0; i < n; ++i) {
            PropRecord rec;
            rec.offset = static_cast<std::size_t>(off) + 24u * static_cast<std::size_t>(i);
            if (rec.offset + 24 > chunk.size()) break;
            const auto rd16 = [&](std::size_t at) {
                return static_cast<int>(static_cast<std::int16_t>(
                    static_cast<unsigned char>(chunk[rec.offset + at]) |
                    (static_cast<unsigned char>(chunk[rec.offset + at + 1]) << 8)));
            };
            rec.slot = rd16(0);
            rec.id = rd16(2);
            rec.stateIndex = rd16(22);
            if (rec.id < 0 || rec.stateIndex < 0) continue;
            const int st = state_.propState(rec.stateIndex);
            if (!(st & 1)) continue;                 // the loader's own test
            PropInstance pi;
            pi.id = rec.id;
            pi.stateIndex = rec.stateIndex;
            pi.shown = (st & 2) != 0;
            const auto pl = propPlacement(chunk, rec);
            for (int k = 0; k < 3; ++k) { pi.pos[k] = pl.pos[k]; pi.rotDeg[k] = pl.rotDeg[k]; }
            out.push_back(pi);
        }
    };
    for (const auto& sl : slots_) {
        if (sl.area < 0) continue;
        walk(sl.areaChunk, ChunkKind::Area);
        if (sl.scene != -1) walk(sl.sceneChunk, ChunkKind::Scene);
    }
    return out;
}

}  // namespace omk

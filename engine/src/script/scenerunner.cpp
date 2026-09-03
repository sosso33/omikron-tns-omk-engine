// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/scenerunner.h"

#include "platform/datafs.h"
#include "script/scenehost.h"

#include <cstring>

namespace omk {

bool SceneRunner::load(const std::string& scptDataDir, const std::string& iamDir,
                       const OpcodeTable& table, ChunkKind kind, int chunk) {
    const auto areaFile  = DataFs::readPath(iamDir + "/AREA");
    const auto sceneFile = DataFs::readPath(iamDir + "/SCENE");
    const DataFs fs(scptDataDir);
    name_ = resolveScx(fs, areaFile, sceneFile, table, kind, chunk);
    if (name_.empty()) return false;
    auto d = fs.read(name_);
    if (d.empty()) { name_.clear(); return false; }
    scx_ = std::make_unique<ScxRuntime>(d);
    if (!scx_->valid()) { scx_.reset(); name_.clear(); return false; }
    // Chunk 10, the camera EDITINGS - `Scene_LoadSCX` case 0xDEAD000A hands
    // the block to `Cam_LoadCameraFile` and then `Script_LinkCamEditing`
    // writes each editing's id into its target object's slots. Only 29 of
    // the 220 scenes carry one; `cam_.valid` stays false for the rest.
    cam_ = CamFile{};
    editings_.clear();
    ticks_ = 0;
    const auto st = readScxStream(d);
    if (st.valid && st.camSize && d.size() >= st.camOffset + st.camSize)
        cam_ = readCamFile(std::span<const std::byte>(d).subspan(st.camOffset, st.camSize));
    return true;
}

namespace {
// `Script_SelectBodyAnimation` (0x02000004) and its relative variant
// (0x0200002A) both put the clip index in param 1.
int clipOf(const ScxObject& o) {
    for (const auto& f : o.functions) {
        if (f.id != 0x02000004u && f.id != 0x0200002Au) continue;
        if (f.params.size() >= 2) return f.params[1];
    }
    return -1;
}
// `Script_SelectRelativeBodyAnimation` places its character at `Path_Sample`
// of the path param 8 names - GRID's `1KaylArrives` takes 0 (`UBas.p1`) and
// its two successors take 1 (`UBas.p2-3`).
int pathOf(const ScxObject& o) {
    for (const auto& f : o.functions) {
        if (f.id != 0x0200002Au) continue;
        if (f.params.size() >= 9) return f.params[8];
    }
    return -1;
}
// ...and the rest of that call's placement. The params array is DWORDS read
// as either int or float - `Script_GetParamInt`, `...Float`, `...FloatB` and
// `...FloatC` are four names for the same two loads - so the offset and the
// Euler come out of it bit-for-bit.
float asFloat(int v) { float f; std::memcpy(&f, &v, 4); return f; }

bool placementOf(const ScxObject& o, float offset[3], float euler[3]) {
    for (const auto& f : o.functions) {
        if (f.id != 0x0200002Au || f.params.size() < 12) continue;
        // `x -= param * -0.39370078` is `x += param / 2.54` - the inch the
        // engine measures the world in.
        for (int k = 0; k < 3; ++k) {
            offset[k] = asFloat(f.params[static_cast<std::size_t>(9 + k)]) / 2.54f;
            euler[k]  = asFloat(f.params[static_cast<std::size_t>(4 + k)]);
        }
        return true;
    }
    return false;
}
}  // namespace

int SceneRunner::start(int oid, const char* how, bool waiting) {
    if (!scx_) return -1;
    // The operand is the object's HANDLE >> 16, not its index into chunk 2.
    const ScxObject* obj = nullptr;
    int objectIndex = -1;
    for (std::size_t i = 0; i < scx_->scene().objects.size(); ++i) {
        const auto& o = scx_->scene().objects[i];
        if (static_cast<int>(o.handle >> 16) == oid) {
            obj = &o; objectIndex = static_cast<int>(i); break;
        }
    }
    if (!obj) { missed_.push_back(oid); return -1; }
    programs_.push_back(std::make_unique<Program>(*scx_, *obj));
    Started st{oid, obj->name, how, waiting, -1, clipOf(*obj), pathOf(*obj)};
    st.relative = placementOf(*obj, st.offset, st.euler);
    started_.push_back(std::move(st));
    const int idx = static_cast<int>(programs_.size()) - 1;
    // `ScriptObject_HasCamEditing`: does starting this object take the
    // camera? The travel is filled in by `handle`, which has the operand.
    if (const CamEditing* e = editingOf(oid)) {
        ActiveEditing a;
        a.program = idx;
        a.objectIndex = objectIndex;
        a.object = oid;
        a.objectName = obj->name;
        a.editing = e->id;
        a.editingName = e->name;
        a.duration = e->duration;
        a.startedTick = ticks_;
        editings_.push_back(std::move(a));
    }
    return idx;
}

void SceneRunner::attachSfx(const std::string& dir, const std::string& name) {
    sfx_ = SfxFile{};
    fx_.clear();
    fired_ = 0;
    if (name.empty()) return;
    const DataFs fs(dir);
    const auto d = fs.read(name);
    if (!d.empty()) sfx_ = readSfx(d);
    pieces_.attach(&sfx_);
    pieces_.setLinks(links_);

    // BINDING a set's ambient effects also brings up its STANDING ones.
    // `Sfx_BindAmbientEffects` ends by walking section E and showing every row
    // keyed `(1, -1)` - hard-coded, no object involved:
    //
    //     v30 = row + 12;
    //     do {
    //         if (*(v30 - 1) == 1 && *v30 == -1) SetPiece_Show(v30 - 3);
    //         v30 += 19;                          // 76 bytes
    //     } while (--n);
    //
    // So those rows come up with the ENVIRONMENT rather than with any object,
    // which is why nothing starts them and why a search through
    // `Script_StartScript` found nothing. GRID's four sit at (-468, -82, 4)
    // and (-508, -81, 4) - head height, where the intro's portal is.
    firePieces(1, -1);
}

void SceneRunner::setPieceLinks(PieceLinkResolver r) {
    links_ = std::move(r);
    pieces_.setLinks(links_);
}

int SceneRunner::standingPieces() const {
    // The rows `Sfx_BindAmbientEffects` shows itself when a set's effects are
    // bound - keyed (1, -1), so no object start can reach them. Four of GRID's
    // eleven, and they are the intro's portal.
    int n = 0;
    for (const auto& p : sfx_.pieces) if (p.key0 == 1 && p.key1 == -1) ++n;
    return n;
}

void SceneRunner::firePieces(int a1, int objectId) {
    if (!sfx_.valid) return;
    // `sub_451470(a1, a2)`: SHOW every row keyed to the pair. What a shown row
    // then does every frame - ONE emitter at its CURRENT position, the effect
    // `+52` names (1-based) or, for the indirect form, the current waypoint's
    // - is `sub_451600`, which `SetPieceRunner::tick` runs. A first port
    // registered a persistent emitter at the row's shipped `+28` here and so
    // could not move a piece, wait one out, loop it a counted number of times
    // or play the indirect ones at all; the section F records it dismissed as
    // "not emitters" are the WAYPOINTS that do all of that.
    pieces_.showKeyed(a1, objectId);
    fired_ = pieces_.shows();
}

int SceneRunner::handle(const std::vector<Call>& calls) {
    int waitOn = -1;
    for (const auto& c : calls) {
        // 57/58 name a SCENE object, 59/60 an ACTOR - and those two put the
        // actor first and the object second, so reading field 0 for all six
        // starts the wrong thing twice. 46/90 are the player's.
        //
        // 46, 58 and 60 are the WAITING variants: their handler is passed the
        // caller's own slot instead of -1, so finishing the object releases
        // the script. (`ScriptObject_Start` ends `mov [esi+16h], 4`.)
        const char* how = nullptr;
        switch (c.op) {
            case 57: case 58: how = "scene";  break;
            case 59: case 60: how = "actor";  break;
            case 46: case 90: how = "player"; break;
            default: continue;
        }
        const bool actorFirst = c.op == 59 || c.op == 60;
        if (c.fields.size() < (actorFirst ? 2u : 1u)) continue;
        const bool waiting = c.op == 46 || c.op == 58 || c.op == 60;
        const int idx = start(c.fields[actorFirst ? 1 : 0], how, waiting);
        // 59/60 name the CHARACTER first and the object second, so the actor
        // this program drives is field 0 - which is how a frontend knows whose
        // pose it is.
        if (idx >= 0 && actorFirst) started_[static_cast<std::size_t>(idx)].actor = c.fields[0];
        // ...and the set pieces keyed to it. A scene object passes a1 = 0; an
        // actor one passes the caller's own value, which is not modelled, so
        // only the a1 = 0 form fires here.
        if (idx >= 0) firePieces(0, c.fields[actorFirst ? 1 : 0]);
        // ...and the camera. The handler's LAST operand is the travel into
        // the editing's camera, `fild`/`fstp`'d into the request's +24 as
        // `max(field, 0)` frames - see `ActiveEditing` in the header. 46/90
        // carry two fields, 57-60 three, and it is the last in each.
        if (idx >= 0 && !editings_.empty() && editings_.back().program == idx) {
            const int field = c.fields.back();
            editings_.back().travel = static_cast<float>(field < 0 ? 0 : field);
        }
        if (waiting && idx >= 0) waitOn = idx;
    }
    return waitOn;
}

const SceneRunner::ActiveEditing* SceneRunner::activeEditing() const {
    const ActiveEditing* best = nullptr;
    for (const auto& a : editings_) {
        if (!programRunning(a.program)) continue;
        if (programClock(a.program) >= static_cast<float>(a.duration)) continue;
        // `Script_PlayAllScripts` walks the object array, so the LAST setter
        // in array order is the active camera.
        if (!best || a.objectIndex >= best->objectIndex) best = &a;
    }
    return best;
}

float SceneRunner::editingClock() const {
    const ActiveEditing* a = activeEditing();
    if (!a) return 0.0f;
    // `Script_PlayScript` samples at `obj->clock` and THEN advances it, and
    // `Game_Tick` draws after the scripts have played - so the frame on screen
    // is the one sampled BEFORE this tick's delta. `Program::tick` has already
    // added it by the time a frontend asks, hence the subtraction; a
    // just-started object shows frame 0, as the engine's does.
    const float c = programClock(a->program) - lastDt_;
    return c < 0.0f ? 0.0f : c;
}

bool SceneRunner::editingCamera(CamSample& out) const {
    const ActiveEditing* a = activeEditing();
    if (!a) return false;
    const CamEditing* e = editingById(cam_, a->editing);
    if (!e) return false;
    return sampleCamEditing(cam_, *e, editingClock(), out);
}

void SceneRunner::tick(float dt) {
    ++ticks_;
    lastDt_ = dt;
    for (auto& p : programs_) p->tick(dt);
    // The set pieces register this frame's emitters (`sub_451600`), then the
    // particles integrate (`Sfx_TickAmbient`) - the engine's order, on the
    // same clock, in FRAMES.
    pieces_.tick(dt, fx_);
    fx_.tick(dt);
}

int SceneRunner::programsRunning() const {
    int n = 0;
    for (const auto& p : programs_) n += p->running() ? 1 : 0;
    return n;
}

}  // namespace omk

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
    sprites_.clear();        // `Scene_LoadSCX` respawns every row's instance
    nodeScales_.clear();     // the set's nodes come back at 1/1/1
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
// of a path addressed in TWO parts, like `Script_MoveObjectOnPath`'s:
// param 7 is the chunk-0 record (`sub_4A6500(scene, p7)` -> the record's
// loaded path array at +24) and param 8 the path inside it (`u32(v19, 4 *
// p8)`). Read as a flat index into every path of the scene, Anekbah's
// couples, beggars and gym walkers - whose files are records 8 and 7 - all
// landed on the doors and watchtowers of record 0..12, in one heap
// (corrected 2026-09-03 from a reader's frame). GRID has one file, which is
// why the Impasse never showed it. -> the flat index, or -1.
int flatPath(const std::vector<ScxPath>& paths, int file, int inside) {
    for (std::size_t i = 0; i < paths.size(); ++i)
        if (paths[i].file == file && paths[i].index == inside) return static_cast<int>(i);
    return -1;
}
int pathOf(const ScxObject& o, const std::vector<ScxPath>& paths) {
    for (const auto& f : o.functions) {
        if (f.id != 0x0200002Au) continue;
        if (f.params.size() >= 9) return flatPath(paths, f.params[7], f.params[8]);
    }
    return -1;
}
// ...and the rest of that call's placement. The params array is DWORDS read
// as either int or float - `Script_GetParamInt`, `...Float`, `...FloatB` and
// `...FloatC` are four names for the same two loads - so the offset and the
// Euler come out of it bit-for-bit.
float asFloat(int v) { float f; std::memcpy(&f, &v, 4); return f; }

// The same three readings for ONE function - which is what a running program
// needs, because the pc walks the list and each step may name a different clip
// (see `Program::animFn`). The object-wide versions above answer "what does
// this object play", which is only the first step's answer.
int clipOfFn(const ScxFunction& f) {
    if (f.id != 0x02000004u && f.id != 0x0200002Au) return -1;
    return f.params.size() >= 2 ? f.params[1] : -1;
}
int pathOfFn(const ScxFunction& f, const std::vector<ScxPath>& paths) {
    if (f.id != 0x0200002Au) return -1;
    return f.params.size() >= 9 ? flatPath(paths, f.params[7], f.params[8]) : -1;
}
// THE EULER IS WRITTEN BY BOTH body-animation functions, from params 4/5/6,
// and it is NOT sticky - every step writes it, and a step that authors 0
// turns the actor back to 0. `Script_SelectRelativeBodyAnimation`
// (0x004A3AD0) and `Script_SelectBodyAnimation` (0x004A35D0) each end
//
//     Anim_SetFrame(node, clip, prev, cur, delta);
//     Actor_SetEuler(node, p4, p5, p6);
//     Actor_MoveBy (node, delta[0], delta[1], delta[2]);
//
// (the relative one gated on `Anim_TickClipSfx`), and that Euler is what
// `Actors_TickAll` turns into `actor+288`, the matrix `Anim_RootDelta` rotates
// the root motion by. Reading it as sticky - only the relative call writing it
// - rotated the restaurant waiter's whole WALK by the 145 degrees his standing
// step had left behind, which is a route through the walls and a yank back at
// every hand-over ("his whole moving pattern is mirrored", a reader,
// 2026-09-05). His walk clips author 0.
//
// Where the two DO differ is the offset: params 9/10/11 for the relative call,
// against the path sample, and 7/8/9 for the absolute one, against the clip's
// root key 0. Both in inches. The return says which kind this is.
bool placementOfFn(const ScxFunction& f, float offset[3], float euler[3]) {
    const bool rel = f.id == 0x0200002Au;
    const bool abs = f.id == 0x02000004u;
    if (!rel && !abs) return false;
    if (f.params.size() < (rel ? 12u : 10u)) return false;
    const std::size_t oi = rel ? 9u : 7u;
    for (int k = 0; k < 3; ++k) {
        offset[k] = asFloat(f.params[oi + static_cast<std::size_t>(k)]) / 2.54f;
        euler[k]  = asFloat(f.params[static_cast<std::size_t>(4 + k)]);
    }
    return rel;
}

// The object-wide answer, for a `Started` before its program has run a step:
// the FIRST body animation of either kind, since both author the Euler.
bool placementOf(const ScxObject& o, float offset[3], float euler[3]) {
    for (const auto& f : o.functions) {
        if (f.id == 0x02000004u && f.params.size() >= 10) {
            for (int k = 0; k < 3; ++k) {
                offset[k] = asFloat(f.params[static_cast<std::size_t>(7 + k)]) / 2.54f;
                euler[k]  = asFloat(f.params[static_cast<std::size_t>(4 + k)]);
            }
            return false;
        }
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
    Started st{oid, obj->name, how, waiting, -1, clipOf(*obj), pathOf(*obj, scx_->paths())};
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

int SceneRunner::bindSetEmitters(std::span<const std::byte> modelData) {
    if (!sfx_.valid) return 0;
    const auto hd = readHeader(modelData);
    if (!hd) return 0;
    const auto meshes = readMeshes(modelData, *hd);
    int n = 0;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const auto& m = meshes[i];
        if (!(static_cast<std::uint32_t>(m.flags) & 0x40000000u)) continue;
        // FOUR RAW BYTES of the record, not of the parsed name: the mesh
        // record is 140 bytes and its name starts at +16.
        const std::size_t at = static_cast<std::size_t>(hd->meshOff) + 140u * i + 16u;
        if (at + 4 > modelData.size()) continue;
        std::uint32_t want = 0;
        for (int k = 0; k < 4; ++k)
            want |= static_cast<std::uint32_t>(modelData[at + static_cast<std::size_t>(k)]) << (8 * k);
        for (const auto& b : sfx_.bindings) {
            std::uint32_t tag = 0;
            for (int k = 0; k < 4; ++k)
                tag |= static_cast<std::uint32_t>(static_cast<unsigned char>(b.tag[k])) << (8 * k);
            if (tag != want) continue;
            // A BINDING NAMES ITS EFFECT BY ID, NOT BY POSITION. Section C's
            // `+0` id is 1-based and does not track the array index: in
            // `anekbah.sfx` index 3 carries id **5** and is `neon`, so the
            // binding `'neon' -> effect 5` means "the effect whose id is 5".
            // Indexing the array instead handed it `effects[5]`, `agri` - a
            // grey smoke - and because the smoke effects outnumber the rest,
            // nearly every binding in the game resolved to one. That is a
            // street light, a fire and a smoke all coming out as smoke.
            //
            // ASSETS 3b records the same rule for the set-piece path (a row's
            // `+52`); it was never applied here.
            const FxEffect* fx = nullptr;
            for (const auto& e : sfx_.effects)
                if (e.id == b.effect) { fx = &e; break; }
            if (!fx) continue;
            fx_.add(*fx, m.pos);
            ++n;
            break;                       // one emitter a mesh
        }
    }
    return n;
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
        // THE OBJECT WORD IS RAW AND UNSIGNED. All six handlers read it
        // `and ecx, 0FFFFh; mov ebp, ecx` - no 0xFFFF test, no `test ch, 40h`
        // indirect step - and only the actor word (59/60) and the trailing
        // word go through the shared fetch (0x403300, 0x4030E0, asmfn --op).
        // It is the object's `handle >> 16`, a 16-bit id that may carry bit
        // 15 and bit 14: Anekbah's are 0xC2xx, so read as an int16 they came
        // out negative and none of its 26 startup extras matched (and read
        // through the indirect fetch, as the disassembler did, they became
        // params[718]). docs/STREET_LIFE.md 1.
        const int object = static_cast<int>(
            static_cast<std::uint16_t>(c.fields[actorFirst ? 1 : 0]));
        const int idx = start(object, how, waiting);
        // 59/60 name the CHARACTER first and the object second, so the actor
        // this program drives is field 0 - which is how a frontend knows whose
        // pose it is.
        if (idx >= 0 && actorFirst) started_[static_cast<std::size_t>(idx)].actor = c.fields[0];
        // ...and the set pieces keyed to it. A scene object passes a1 = 0; an
        // actor one passes the caller's own value, which is not modelled, so
        // only the a1 = 0 form fires here.
        if (idx >= 0) firePieces(0, object);
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
    sounds_.clear();
    motions_.clear();
    for (auto& p : programs_) p->tick(dt);
    // ...and collect what they started. `Script_PlayScript` runs the sound
    // handlers inside the same chain walk as the animation, so a cue belongs
    // to the frame its program ticked on.
    for (std::size_t i = 0; i < programs_.size() && i < started_.size(); ++i) {
        for (const auto& c : programs_[i]->sounds())
            sounds_.push_back({static_cast<int>(i), started_[i].object,
                               started_[i].actor, c});
        for (const auto& m : programs_[i]->motions()) motions_.push_back(m);
    }
    for (const auto& p : programs_)
        for (const auto& op : p->scaleOps()) {
            auto& ns = nodeScales_[op.name];
            ns.s[op.axis] = op.value;
            ns.mode = op.mode;
        }
    // A sprite is positioned only on a tick a `Display3DSprite` runs: the
    // flag is this tick's, and an instance nobody wrote to keeps its place.
    for (auto& kv : sprites_) kv.second.tracking = false;
    // ...and apply the sprite family to the scene's instances, in the order
    // the chains ran them. Two objects naming one row share the instance,
    // as they do in the engine (last writer wins within a tick).
    for (const auto& p : programs_) {
        for (const auto& op : p->spriteOps()) {
            auto& sp = sprites_[op.row];
            if (sp.row < 0) {
                sp.row = op.row;
                sp.id  = scx_ ? scx_->spriteId(op.row) : -1;
            }
            switch (op.kind) {
            case Program::SpriteOp::Kind::Display:
                if (!sp.linked) { sp.linked = true; sp.linkedAt = ticks_; }
                sp.tracking = op.tracking;
                if (op.tracking && anchorSet_)
                    for (int c = 0; c < 3; ++c) sp.pos[c] = anchor_[c];
                break;
            case Program::SpriteOp::Kind::Frame:  sp.frame = op.ivalue; break;
            case Program::SpriteOp::Kind::Type:   sp.type  = op.ivalue; break;
            case Program::SpriteOp::Kind::ScaleX: sp.sx    = op.fvalue; break;
            case Program::SpriteOp::Kind::ScaleY: sp.sy    = op.fvalue; break;
            case Program::SpriteOp::Kind::Roll:   sp.roll  = op.fvalue; break;
            case Program::SpriteOp::Kind::Palette: break;   // the morph palette is not drawn
            }
        }
    }
    // THE PROGRAM COUNTER MOVES, AND SO DOES THE CLIP. `Started::clip` was
    // filled once at start from the object's FIRST body animation and never
    // touched again, so a program with more than one step posed its whole run
    // with step 0's clip and snapped the body to step 0's root key 0. The
    // Impasse's `A_2_DemonLook` is the case: clip 15 (perched, 91 frames) then
    // clip 17 (the jump off the wall, 41). Frozen on 15 the demon clamps at
    // its last frame - 267 units up the wall - for the whole 132-frame shot,
    // and the descent `sautdemon`'s last 41 frames are filmed to show never
    // happens. Refreshed here, from the function the pc is actually on.
    for (std::size_t i = 0; i < programs_.size() && i < started_.size(); ++i) {
        const ScxFunction* fn = programs_[i]->animFunction();
        if (!fn) continue;                    // this step plays no body animation
        auto& st = started_[i];
        st.animReached = true;                // the engine touches the actor from here on
        st.clip     = clipOfFn(*fn);
        st.path     = pathOfFn(*fn, scx_->paths());
        st.relative = placementOfFn(*fn, st.offset, st.euler);
    }
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

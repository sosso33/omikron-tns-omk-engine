// SPDX-License-Identifier: GPL-3.0-or-later
// `Script_PlayAllScripts` - the scene objects of a resident chunk.
//
// The other half of a frame. The world scripts (`script/world.h`,
// `script/area.h`) decide *what* happens; this animates it: `scx.play*` is
// `Script_StartScript` on one of the resident scene's objects, and every
// frame each started program advances by `Script_PlayScript`.
//
// It is a class of its own rather than part of either caller because BOTH
// drive it - the zone lifecycle (`World`) and the area session (`Session`) -
// and the opcode map below is the sort of thing that goes wrong when it is
// written twice.
#pragma once

#include "script/interp.h"
#include "formats/sfx.h"
#include "o3de/camedit.h"
#include "o3de/particles.h"
#include "o3de/setpiece.h"
#include "script/program.h"
#include "script/script.h"

#include <memory>
#include <string>
#include <vector>

namespace omk {

class DataFs;

class SceneRunner {
public:
    // Make the `.SCX` that `kind`/`chunk` plays its objects from resident.
    // -> false when the chunk names none, which is normal and not an error.
    bool load(const std::string& scptDataDir, const std::string& iamDir,
              const OpcodeTable& table, ChunkKind kind, int chunk);

    bool  loaded() const { return scx_ != nullptr; }

    // ---- THE SCENE'S EFFECTS
    //
    // Starting an object fires the `.SFX` set pieces keyed to it - the engine
    // does `Script_StartScript(instance)` and then
    // `sub_451470(a1, id & 0xFFFF)` in the same handler, and `sub_451470`
    // shows every section E row whose `+8`/`+12` match. Attach the scene's
    // `.SFX` and this places an emitter for each row a start matches.
    //
    // Rows keyed `(1, -1)` are not fired by an object at all - that call
    // masks the id with 0xFFFF and can never produce -1. They are the scene's
    // STANDING effects, and `Sfx_BindAmbientEffects` shows them itself at the
    // end of binding the set; attaching the file is that moment here.
    void attachSfx(const std::string& scptDataDir, const std::string& sfxName);

    // THE SET'S OWN EMITTERS - the environment family, as opposed to the ones
    // an object start fires. `Sfx_BindAmbientEffects` walks the resident
    // `.3DO`'s meshes, and for every one flagged **0x40000000** compares the
    // first FOUR BYTES of its name, as a dword, against each section-D
    // binding's tag; a match registers an emitter for that binding's section-C
    // effect at the mesh's own position. That is the whole chain a set's neon,
    // steam and smoke come out of - three files, authored separately - and
    // **321 of the 579 flagged meshes bind**, `neon` 102, 153 of them in
    // Anekbah (`docs/ASSETS.md` 3b).
    //
    // The port read all three files and never joined them: the effects and
    // bindings were parsed, `ParticleField::add` was there, and nothing walked
    // the set. -> how many emitters were registered.
    //
    // It takes the model's BYTES rather than the parsed meshes because the
    // compare is on four RAW bytes of the record and `readMeshes` stops a name
    // at its first null: a mesh whose name is shorter than four characters
    // compares differently once it has been through a C string. Reading the
    // record at `meshOff + 140 * i + 16` is what the engine does.
    int bindSetEmitters(std::span<const std::byte> modelData);
    const SfxFile& sfx() const { return sfx_; }
    ParticleField& effects() { return fx_; }
    const ParticleField& effects() const { return fx_; }
    // The set pieces the file's section E rows have become - `SetPiece_Show`
    // and `sub_451600` run over them (`o3de/setpiece.h`). A shown row
    // registers ONE emitter a frame at its current position, which the block
    // of waypoints moves; that is what orbits `ttt` round the intro's portal.
    const SetPieceRunner& pieces() const { return pieces_; }
    // How a piece linked to an ACTOR (type 2, a three-letter tag) or the
    // PLAYER (type 3) finds it. The frontend owns those positions; without
    // a resolver such a record stands absolute, and says so in setpiece.h.
    void setPieceLinks(PieceLinkResolver r);
    // How many section E rows have been SHOWN - the standing ones on attach,
    // plus whatever object starts have matched since.
    int  piecesFired() const { return fired_; }
    // The rows keyed `(1, -1)`: the scene's standing effects.
    int  standingPieces() const;
    const std::string& file() const { return name_; }
    const ScxRuntime&  scene() const { return *scx_; }

    // Act on one run's recorded calls: every `scx.play*` starts its object.
    //
    // -> the index of the program a WAITING start (46, 58, 60) just created,
    // or -1. The caller parks its script on that program: the handler is
    // passed the caller's own slot instead of -1, so finishing the object is
    // what releases the script (`ScriptObject_Start` ends `mov [esi+16h], 4`).
    // -1 also covers a waiting start whose object is not in the resident
    // scene, and a script must NOT park on that - the engine has nothing to
    // release it either, but a replica with no scene loaded would otherwise
    // stop dead where the game runs on.
    int handle(const std::vector<Call>& calls);

    // Whether the program at `idx` is still running. Out of range is false,
    // so a caller that parked on a program the runner has since replaced is
    // released rather than stuck.
    // How far a started program has run, in FRAMES - `Program::clock()`, which
    // accumulates the delta `Script_PlayScript` is ticked with. It is the
    // clip's frame index.
    float programClock(int idx) const {
        return (idx >= 0 && idx < static_cast<int>(programs_.size()))
                   ? programs_[static_cast<std::size_t>(idx)]->clock() : 0.0f;
    }
    bool programRunning(int idx) const {
        return idx >= 0 && idx < static_cast<int>(programs_.size()) &&
               programs_[static_cast<std::size_t>(idx)]->running();
    }
    int programPc(int idx) const {
        return (idx >= 0 && idx < static_cast<int>(programs_.size()))
                   ? programs_[static_cast<std::size_t>(idx)]->pc() : -1;
    }
    // THE FRAME OF THE CLIP `Started::clip` NOW NAMES, which is not the
    // program clock once a program has more than one body-animation step:
    // `Script_SelectBodyAnimation` counts from when its own function was
    // entered (`Program::animClock`). Sampling the Impasse demon's 41-frame
    // jump at the program's clock asks for frame 92 of a 41-frame clip.
    float programAnimClock(int idx) const {
        return (idx >= 0 && idx < static_cast<int>(programs_.size()))
                   ? programs_[static_cast<std::size_t>(idx)]->animClock() : 0.0f;
    }

    // One frame of every running program. The delta is in THIRTIETHS OF A
    // SECOND, because that is the unit `Game_Frame` computes (docs/BOOT.md 4)
    // and every authored period in the data is in it.
    void tick(float dt = 1.0f);

    // THE SOUNDS THIS FRAME STARTED - every running object's
    // `Script_PlaySound` / `Script_PlaySyncSound` that fired, with the program
    // that owns it so a caller can position the sound at its actor. Refilled
    // by every `tick`; a frontend reads it after ticking and plays each one.
    // `Program::SoundCue` has the parameter layouts and why the two differ.
    struct FiredSound {
        int program = -1;
        int object = 0;          // the handle >> 16 the script named
        int actor = -1;          // for an `scx.play.actor*` object
        Program::SoundCue cue;
    };
    const std::vector<FiredSound>& sounds() const { return sounds_; }

    // THE NODE MOTIONS this frame - every running program's
    // `Script_MoveObjectOnPath`, with the set mesh it names and the world
    // position to put it at. A frontend that draws the set applies these; the
    // crates of the Impasse are four of them.
    const std::vector<Program::NodeMotion>& motions() const { return motions_; }

    // The motions of ONE program, so a caller can ask where the object a cue
    // came from actually IS. `motions()` flattens every program's together and
    // drops the attribution, which is enough to DRAW them and not enough to
    // place a sound (omk-play 73).
    std::vector<Program::NodeMotion> motionsOf(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(programs_.size())) return {};
        return programs_[static_cast<std::size_t>(idx)]->motions();
    }

    struct Started {
        int         object = 0;
        std::string name;
        std::string how;        // "scene" | "actor" | "player"
        bool        waiting = false;
        int         actor = -1;  // for "actor", the CHARACTERS id it drives
        // Which clip of the scene's animation array the object's body
        // animation selects - `Script_SelectBodyAnimation` param 1, and by
        // POSITION rather than by id, which is what `omkdata.scene_idle`
        // settled (by id it lands on the wrong skeleton). -1 when the object
        // plays no body animation at all, which every `fx*` object is.
        int         clip = -1;
        // And the authored PATH its relative body animation samples -
        // param 8. -1 when it names none.
        int         path = -1;
        // The rest of `Script_SelectRelativeBodyAnimation`'s placement, which
        // it performs ONCE - on the tick where the clip frame is still 0:
        //
        //     Path_Sample(paths[param8], 1.0, &x, &y, &z, quat, 1);  // LINEAR
        //     x -= param9  * -0.39370078;   // i.e. += param/2.54: an INCH
        //     y -= param10 * -0.39370078;   //   offset, the engine's world
        //     z -= param11 * -0.39370078;   //   unit (docs/PORTING A2)
        //     o3de_SetNodePos(node, x, y, z);
        //
        // and then every tick `Actor_SetEuler(node, param4, param5, param6)`.
        // So the FACING is authored in the call, not taken from the path key's
        // quaternion - which is what a reading of the path alone would guess.
        float       offset[3] = {0, 0, 0};   // params 9/10/11, already /2.54
        // NOT APPLIED by the frontend, and recorded rather than claimed:
        // `Matrix3x3_FromEulerAngles`' axis order is untested and GRID's three
        // objects all author 0, so there is no case to test it against.
        // `verify.py: engine clip root` asserts the zeros, so the omission is
        // provably invisible here and stops being so the moment it is not.
        float       euler[3]  = {0, 0, 0};   // params 4/5/6, degrees
        bool        relative = false;        // 0x0200002A rather than 0x02000004
    };
    const std::vector<Started>& started() const { return started_; }
    const std::vector<int>&     missed()  const { return missed_; }
    std::size_t programCount() const { return programs_.size(); }
    int programsRunning() const;

    // ---- THE CAMERA EDITINGS (camera mode 13)
    //
    // Every `scx.play*` handler (46, 57, 58, 59, 60) ends the same way after
    // `ScriptObject_Start` - from the 46 handler at 0x00402C30, the others
    // are the same code:
    //
    //     call    sub_41D9C0          ; ScriptObject_HasCamEditing(obj, slot)
    //     test    eax, eax
    //     jz      short loc_402D08    ; no editing -> no camera request
    //     test    edi, edi            ; edi = the LAST operand field
    //     jge     short loc_402CDB
    //     mov     dword ptr [esp+10h], 0
    // loc_402CDB:
    //     fild    dword ptr [esp+10h]
    //     push    offset dword_930800 ; the request block
    //     push    0Dh                 ; mode 13
    //     mov     dword_93081C, 1
    //     mov     dword_930828, 0FFFFFFFFh
    //     fstp    dword_930818        ; request +24 = (float)max(field, 0)
    //     call    sub_414BF0          ; Camera_Request(13, &request)
    //
    // So the field is a FLOAT travel time in frames (`sub_414A90` reads the
    // request's +24 as `f32` into the move's duration), and mode 13 is "take
    // the camera from the scene's active camera object": the camera tick
    // (04_sys.c 3704) copies eye, target, fov and roll out of `dword_9103D4`
    // every frame, and `dword_9103D4` is `Scene_GetActiveCamera` after
    // `Script_PlayAllScripts` - which `Script_PlayScript` set from the
    // editing, sampled at the OBJECT'S OWN CLOCK (docs/CUTSCENES.md 2). When
    // no object sets one and the mode is still 13, the same frame loop
    // requests mode 0 with travel 0 (05_sys.c 2140): the fall-back is a cut
    // to the player camera.
    //
    // This class does the scene's half: which editing is driving the camera
    // this frame and what it says. The camera itself - the travel from the
    // previous camera and the fall-back - is the frontend's, because that is
    // where the previous camera is.
    struct ActiveEditing {
        int           program = -1;     // index into the programs
        int           objectIndex = -1; // position in the scene's object array
        int           object = 0;       // the handle >> 16 the script named
        std::string   objectName;
        int           editing = 0;      // the editing's id
        std::string   editingName;
        std::uint32_t duration = 0;     // frames
        float         travel = 0;       // the request's +24: max(field, 0)
        long          startedTick = 0;  // `ticks()` when the object started
    };
    // The chunk-10 file the resident scene carries; `valid` false for the 191
    // scenes that ship none.
    const CamFile& camFile() const { return cam_; }
    // The editing linked to scene object `oid` (handle >> 16), or nullptr -
    // `ScriptObject_HasCamEditing`'s answer.
    const CamEditing* editingOf(int oid) const { return editingForObject(cam_, oid); }
    // Every start that carried an editing, in start order.
    const std::vector<ActiveEditing>& editings() const { return editings_; }
    // The editing DRIVING the camera this frame, or nullptr. Each running
    // object's `Script_PlayScript` sets the scene's active camera from its
    // own editing while `clock < duration`, in object-array order, so with
    // two live at once the later object in the array wins - which no shipped
    // scene does (95 linked objects, one editing each, played as beats).
    const ActiveEditing* activeEditing() const;
    // `Cam_PlayEditing` at that object's program clock. -> false with no
    // active editing or when the sampler gives up (past the duration, or
    // outside every track - the engine's "key not found").
    bool editingCamera(CamSample& out) const;
    // The frame the active editing is SHOWING: the driving object's clock as
    // it stood when this tick sampled it - `Script_PlayScript` samples, then
    // advances - so a just-started object reads 0, not the delta.
    float editingClock() const;
    // How many `tick`s the runner has run - the scene frame counter.
    long ticks() const { return ticks_; }

private:
    // -> the new program's index, or -1 when the object is not resident.
    int start(int oid, const char* how, bool waiting);

    std::unique_ptr<ScxRuntime> scx_;
    std::string name_;
    std::vector<std::unique_ptr<Program>> programs_;
    std::vector<Started> started_;
    std::vector<int>     missed_;
    CamFile              cam_;
    std::vector<ActiveEditing> editings_;
    std::vector<FiredSound>    sounds_;
    std::vector<Program::NodeMotion> motions_;
    long                 ticks_ = 0;
    float                lastDt_ = 0.0f;
    SfxFile              sfx_;
    ParticleField        fx_;
    SetPieceRunner       pieces_;
    PieceLinkResolver    links_;
    int                  fired_ = 0;
    // `sub_451470(a1, id)` for a started object - a1 is 0 for a
    // scene object and the caller's own value for an actor one.
    void firePieces(int a1, int objectId);
};

}  // namespace omk

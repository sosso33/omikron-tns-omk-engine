// SPDX-License-Identifier: GPL-3.0-or-later
// Loading an area - `Area_TickLoad` (0x0040C7E0) case 9.
//
// This is the path that reaches the startup scripts, and the reason it needs
// its own module is that **which scene loads is not a parameter**: it comes
// out of the game database's per-area table (`scene_of_area`, +12), indexed by
// area id, with -1 meaning none. So what a load does depends on the save.
//
//     dword_69BC60 = slot; dword_69BC64 = areaId
//     v18 = the AREA block;  Script_NewContext(slot, v18[1], 0, 0)
//     *v18 = ctx;            Script_QueueAction(ctx, 1)
//     v20 = i16(u32(g_GameDB, 12), 2 * areaId)      ; the scene over it
//     if (v20 != -1):
//         v21 = the SCENE block; Script_NewContext(slot, v21[1], 0, 0)
//         *v21 = ctx;            Script_QueueAction(ctx, 1)
//     Zones_RegisterAll()
//
// The AREA script is queued BEFORE the SCENE's, and both are queued rather
// than called, so they interleave with the pump exactly as a zone's do.
//
// ---------------------------------------------------------------------------
// THE SESSION (below) is the world-script half of a frame: `Script_Pump`
// phase 1 (0x00407DC0) over the 32-entry context table at `dword_4E61E8`, the
// two resident area slots at `dword_69BC40` and the area transition
// (`Area_Transition`, 0x00408530) with its staged load. Tier, per
// docs/PORTING.md B2: **6, read and explained**, for the scheduling itself -
// no capture can see a context index or a slot - lifted to **4** exactly where
// the golden traces reach: the intro's announcement stream (42/42 in order)
// and the ORDER of every event in the five play captures. The frame count of
// a staged load and the departure/arrival object sequence are tier 6 with one
// data-constrained piece each (the slice count comes off the set file's size;
// the objects resolve in the source `.SCX` 409/448) and are labelled so in
// the source, in `verify.py: engine: area transition` and in the README row.
#pragma once

#include "formats/iam.h"
#include "formats/addresses.h"
#include "formats/placements.h"
#include "o3de/worldcam.h"
#include "script/dialogue.h"
#include "script/gamestate.h"
#include "script/scenerunner.h"
#include "actor/pedestrians.h"
#include "ui/widgets.h"
#include "script/hooks.h"
#include "script/interp.h"
#include "script/props.h"
#include "script/script.h"
#include "script/zones.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace omk {

struct StartupRun {
    bool         isScene = false;
    int          chunk   = 0;
    std::size_t  offset  = 0;
    RunStatus    status  = RunStatus::End;
    std::vector<Call> calls;
};

// Load `areaId` and run what the load queues, in order.
//
// `iamDir` is the directory holding AREA and SCENE. The scene over the area
// is read from `state`, so seeding a different save changes the result - which
// is the point of it living in the DB rather than in the area.
std::vector<StartupRun> loadArea(const std::string& iamDir, int areaId,
                                 GameState& state, const OpcodeTable& table);

// ------------------------------------------------------------- the session

// One announced decision, in the shape the golden traces record: a .TAG
// domain and the operand the engine would have resolved through it.
struct Announced {
    std::string  domain;
    std::int32_t value = 0;
    // WHICH handler narrated it. `Announced` was domain+value until a viewer
    // needed to tell `media.play` (92) from the nine other opcodes that
    // announce to OBJECTS; the golden traces distinguish them by the string
    // LITERAL's address, which a port has no equivalent of.
    std::uint8_t op = 0;
};

// Booting an area and ticking frames, with the one piece of control flow that
// carries a script past `dialog.start`.
//
// g_DialogState is 1 free, 3 a conversation on screen. Dialog_Load writes 3
// and `Game_HandleEvent` case 63 writes 1 back. While it is 3 the pump does
// not run at all - so a launch script does not "suspend", the world stops
// around it and it resumes at the instruction after the `dialog.start`.
//
// How LONG a conversation lasts is not modelled - there is no dialogue UI -
// so `closeDialogs` is the one thing a caller supplies. Everything either side
// of it is the engine's own control flow.
class Session {
public:
    Session(const std::string& iamDir, GameState& state, const OpcodeTable& table)
        : iam_(iamDir), state_(state), table_(table) { actorSlots_.fill(-1); }

    // Load the announce map - which .TAG domain each handler narrates its
    // operand to, and WHICH operand. From tables/vm_announce.json, derived
    // from the assembly rather than written by hand: a hand-written one was
    // wrong three ways at once (scx.play and music.play announce NOTHING, and
    // scx.play.actor.wait announces the actor, not the object).
    bool loadAnnounceMap(const std::string& jsonPath);

    // The BOOT load: `State_Apply`'s `Area_Load(START +1414, 0)`, which runs
    // with the async reader in mode 0 - synchronous - so every case of
    // `Area_TickLoad` runs inside the one call and the startup contexts are
    // queued before this returns. -> how many were queued. `Game_NewGame`
    // resets both resident slots first, and so does this.
    int loadArea(int areaId);

    // Advance one frame: pump the contexts unless a conversation is up.
    void frame();
    long frameNo() const { return frameNo_; }

    // Whether a conversation is on screen, and closing it (event 63).
    //
    // A conversation does NOT end itself when its voices run out - it ends
    // when the player takes a branch that has no target, or when a node offers
    // nothing. `endDialog` is the outside closing it, which is what skipping
    // one amounts to.
    bool dialogOpen() const { return dialogState_ == 3; }
    void endDialog() { dialogState_ = 1; dialog_.reset(); }

    // ------------------------------------------------- PLAYING a conversation
    //
    // Without this a `dialog.start` opens and the caller closes it on the next
    // frame, which is a labelled stand-in and gets the intro's SHAPE wrong: the
    // grid tunnel AREA 118's script cuts to after `dialog.start 272` then
    // arrives about two seconds after the menu instead of a minute and a half
    // of speech plus however long the player takes over it - conversation 272
    // is three lines of 30.7 + 36.2 + 32.3 seconds, each waiting on a press.
    //
    // Give the Session the `IAM` chunk source and the morph directory and it
    // plays the conversation instead: a node at a time, each held for as long
    // as its own voice lasts, taking the first branch whose condition passes.
    // `dialogOpen()` then stays true for the conversation's real duration, so
    // the world stops for as long as `Dialog_TickUI` owns the frame.
    //
    // It draws nothing and offers no reply menu - see `DialogPlayer`.
    void attachDialogue(const std::string& morphDir) { morphDir_ = morphDir; }
    const DialogPlayer& dialogue() const { return dialog_; }
    // WHICH character speaks the open conversation - the model named by the
    // actor record whose id at +272 is the DIALOG chunk's word 0. Resolved
    // against the RESIDENT chunks, because an actor id is reused across areas
    // and the launching one is the one that means anything.
    const std::string& speakerModel() const { return speakerModel_; }

    // ------------------------------------------------ CHARACTERS ON SCREEN
    //
    // `character.show` (78) resolves its CHARACTERS operand, calls
    // `Actor_Attach` and sets the record's state bit to 1; `character.hide`
    // (79) detaches and clears it. So a character is on screen from the show
    // to the hide, and NOT from `dialog.start` - AREA 118 shows Kay'l at pc
    // 1184 and starts the conversation at 1212, twenty-eight bytes and about
    // six seconds later. A frontend that waited for the conversation drew the
    // arrival as a black screen, which is what this exists to fix.
    //
    // A character reaches this list two ways, and the first is the one the
    // world runs on: `Actors_SpawnFromTables` (0x0040BB90) places everybody
    // the AREA's and the SCENE's 20-byte tables name at the load and attaches
    // those whose `ObjectShown` bit is set, so the alley has its four
    // passers-by and its Demon before any script runs. `fromTable` marks
    // those; a character shown by a script that no table places (or the body
    // `player.become` takes) is pushed with `fromTable` false, which is what
    // this list held on its own until 2026-09-03.
    struct Shown {
        int         actor = -1;
        std::string model;      // the actor record's +144
        std::string bank;       // the actor record's +72, the `.CTL`
        float       pos[3] = {0, 0, 0};   // the placement, world units
        float       facing = 0.0f;        // degrees, 4096 per turn unwrapped
        int         slot = -1;            // the runtime slot record +0 took
        bool        fromTable = false;    // a placement record put it here
    };
    // Slot 0's attached characters, then slot 1's, then anything a script
    // showed that no table places. Rebuilt whenever attachment changes.
    const std::vector<Shown>& shown() const { return shown_; }
    // The model a CHARACTERS id resolves to, through the resident chunks'
    // 276-byte actor records (`sub_40B190`). Empty when it names none.
    std::string modelOfActor(int actor) const;

    // The two things a PERSON does in a conversation, and the only two that
    // move it. `Game_HandleEvent` events 55 and 59 sit behind them.
    void clearDialogLineChanged() { dialog_.clearLineChanged(); }
    void dialogNext()          { dialog_.next(); }
    void dialogChoose(int k)   { dialog_.choose(k); }
    // Seconds per frame for the dialogue clock. Everything else in the engine
    // counts thirtieths (docs/BOOT.md 4); a voice is counted in seconds
    // because it is audio, and 1/30 is what `Game_Frame` gives at 30 fps.
    void setFrameSeconds(double s) { frameSeconds_ = s; }

    // STREET LIFE - the procedural pedestrians (docs/STREET_LIFE.md 2,
    // `actor/pedestrians.h`). `Area_TickLoad` case 8 hands an area's `.OPT`
    // to `Slider_Init` at its load, and `Sliders_Tick` walks the pool every
    // frame. Both need the gamedata tree, which the Session otherwise never
    // opens (the `.SCX` comes the same way, through `loadScene`): without
    // this call no pedestrians exist, with it every later area load spawns
    // its circuit's - and a slot already loaded when it is called spawns now.
    void loadTraffic(const std::string& gamedataRoot);
    const Pedestrians& pedestrians() const { return peds_; }
    // Options row 6, "Niveau d'activite dans les rues", 0..4 - the density
    // `Slider_Init` reads from `dword_90E724+2`. Spawning happens once, at
    // the load, so a change applies to the next circuit loaded. Default:
    // `kDefaultStreetActivity` (the engine's own 3), until the options menu
    // is ported and hands its value here.
    void setStreetActivity(int level) { streetActivity_ = level < 0 ? 0 : level > 4 ? 4 : level; }
    int  streetActivity() const { return streetActivity_; }

    // How many areas have been entered THROUGH A TRANSITION - `sub_419AF0`
    // making a destination's decor the drawn scene at `Area_Transition` mode
    // 3 - so a caller can stop once one has happened rather than after a
    // fixed frame count. The boot load is not one; a restart counts one.
    int areasEntered() const { return entered_; }

    // ------------------------------------------------- THE TWO RESIDENT SLOTS
    //
    // The engine keeps TWO areas loaded: `dword_69BC40` is two 16-byte rows of
    // {AREA block, SCENE block, area id, scene id}, `dword_69BC60` names the
    // ACTIVE row and `dword_69BC64` the active area. Everything that walks the
    // world walks both rows - `Zones_RegisterAll` (0x00406560) registers four
    // tables, `Camera_FindWorld` (0x0040B220) searches slot 0's two tables,
    // then slot 1's, then GLOBAL, `Address_Find` searches both - so the area
    // the player is leaving stays live, its zones armed and its contexts
    // running, for as long as a transition takes. What the viewer DRAWS is a
    // third thing: the decor slot in state 2, which `sub_419AF0(area)` sets
    // (`o3de_InsertScene`) and `sub_419A90(area)` clears back to state 1
    // (`sub_441200` - loaded but unlinked, which is why ASSETS 4b's texture
    // cache sees the outgoing set). `shownSlot()` is that one.
    //
    // The active row is NOT switched by the transition machine. `Game_HandleEvent`
    // case 9 does it - `dword_69BC60 = 1 - dword_69BC60` when the other row is
    // occupied, then `Zones_RegisterAll` and the area's own music (`AREA
    // +142`) - and event 9 is raised by `Walk_ProbeGround` (0x00467030) and
    // `sub_459AA0` when the decor UNDER THE PLAYER changes to a slot in state
    // 2. So the switch follows his feet. `playerOnArea` is that
    // event; the walker (E2) raises it, and until it does `placeActorAt` raises
    // it for a teleport that lands on the other slot's set - labelled where it
    // happens.
    // One character `Actors_SpawnFromTables` spawned into a slot: the
    // placement record's fields, plus what `Actor_FindById(+2)` resolves
    // (`+144` the `.3DO` stem, `+72` the `.CTL` bank list) and whether
    // `Actor_Attach(slot)` was called - which is the record's `+18` bit at
    // the load, and afterwards whatever 78/79 last said.
    //
    // A clear bit is LOADED AND PLACED but not attached: the engine calls
    // `Actor_LoadModel`/`Actor_SetPlacement` unconditionally and only guards
    // the attach, the same "hidden is not unloaded" shape the decors have
    // (`ResidentSlot::shown`).
    struct Character {
        int         actor = -1;
        int         slot  = -1;           // word_69BC80's index, record +0
        std::string model;                // actor record +144
        std::string bank;                 // actor record +72, the `.CTL`
        float       pos[3] = {0, 0, 0};
        float       facing = 0.0f;
        int         bit = -1;             // the ObjectShown index
        bool        attached = false;     // Actor_Attach was called
        bool        fromScene = false;    // the SCENE's table, not the AREA's
    };

    struct ResidentSlot {
        int  area = -1, scene = -1;             // dword_69BC48 / dword_69BC4C
        // `Actors_SpawnFromTables`, case 5, in table order: the AREA's
        // records then the SCENE's. Emptied with the slot (`sub_40D4A0`).
        std::vector<Character> characters;
        std::vector<std::byte> areaChunk;       // the AREA block, dword_69BC40
        std::vector<std::byte> sceneChunk;      // the SCENE block, dword_69BC44
        bool loaded = false;                    // Area_TickLoad reached case 9
        // sub_419AF0 put the decor in state 2 - linked into the render list
        // (`o3de_InsertScene`); sub_419A90 took it back to state 1, loaded
        // and not drawn. NOT the same as `loaded`: the intro's 118 sits
        // behind 222 loaded and hidden.
        bool shown = false;
        int  areaCtx = -1, sceneCtx = -1;       // block +0: the startup contexts
        std::string set, scx;                   // AREA +88 / +97
        // STREET LIFE (docs/STREET_LIFE.md 2): the traffic circuit at +115
        // (`TRAJECTOIRES\<opt>.OPT`), the animation library at +124
        // (`ANIMS\<ani>.ANI` - `PASSANTH` for every city), and the two masks
        // at +164/+168 selecting which men and women models the crowd wears
        std::string opt, ani;
        std::uint32_t menMask = 0, womenMask = 0;
        WorldCameras cams;                      // AREA +64/+84, SCENE +32/+52 (and GLOBAL)
        std::vector<Address> addresses;         // AREA +60
        std::int16_t music = 0;                 // AREA +142, the area's own track
    };
    const ResidentSlot& residentSlot(int i) const { return slots_[i & 1]; }
    int activeSlot() const { return active_; }               // dword_69BC60
    int activeArea() const { return activeArea_; }           // dword_69BC64
    int shownSlot() const { return curSlot_; }               // the decor in state 2
    // Whether slot `s`'s decor is in state 2. TWO can be, and are, during a
    // walk between areas: `Area_Transition`'s completion arm shows the
    // destination (`sub_419AF0(a1[4])`, mode 3 cases 1 and 3) and nothing
    // hides the origin until `area.arrive` (mode 1 at state 8) or the object
    // watchdog's last step (status 11) - so the player walks from one set
    // into the other with both drawn, and the ACTIVE row follows his feet
    // (event 9), not the load (todo/pending/T11.md, finding 2). AIMPASSE ->
    // AIMPASAS is the shipped case: zone 3801's script is `area.goto 142 -1
    // -1; end` and the `area.arrive -1` is AREA 142's record 10, a zone deep
    // in the airlock. `shownSlot()` stays the most recently shown of the
    // two, for the single-set readers.
    bool slotShown(int s) const { return slots_[s & 1].shown; }
    int  shownCount() const { return (slots_[0].shown ? 1 : 0) + (slots_[1].shown ? 1 : 0); }
    // The id in the slot OTHER than the shown one: the area the player came
    // from, or -1 when nothing has been left yet.
    int otherArea() const { return slots_[1 - curSlot_].area; }
    // `Game_HandleEvent` case 9 - the player's feet are on `area`'s decor.
    void playerOnArea(int area);
    // `Game_HandleEvent` case 3 (0x004067D0), the MOVE half: the player has
    // reached where `player.move.wait` sent him. `a2` is a context INDEX -
    // `dword_4E61E8[a2]`, no search - which is why the handler hands
    // `Player_GoToMove` the caller's own `ctx+30`: the resume id travels with
    // the move. Resumes that one context, only while it is still at 4.
    void playerMoveEnded(int ctx);
    // `Game_HandleEvent` case 2: the fight is over. NO index - it walks the
    // table and returns the FIRST context at status 3 to 1, then reloads the
    // player's .CTL bank list whether or not it found one (that half belongs
    // to the player's channel and is named, not done, here).
    void fightEnded();
    int  fightingWith() const;      // the running fight's opponent, or -1
    // The two boundaries to the actor runtime the parks need. `Player_GoToMove`
    // (sub_41B6F0) walks the player to an ADDRESSES record and reports to the
    // context when he arrives; `Fight_Begin` (sub_41A3B0) enters the fight.
    // With no hook installed the opcode RUNS ON instead of parking: nothing
    // could ever release the park, and modelling the wait must never invent
    // a deadlock (the `ObjectWait` rule). Installing a hook arms the park.
    void setMoveHook(std::function<bool(int addressId, int ctx)> h);
    void setFightHook(std::function<bool(int opponentId)> h);
    // 126's SUBJECT: `Address_Find(field 1)` in both subject pointers where 96
    // puts `Actor_Player()` twice. -1 = the player. Read by whoever frames a
    // camera whose points are offsets.
    int  cameraSubjectAddress() const { return camSubjectAddress_; }
    // For the walker: where the player is, from outside. Nothing here moves
    // him after a teleport; E2's `Walker` does, and tells the Session so the
    // subject-relative cameras and the zone scan (T13) see it.
    void setPlayerPosition(const float pos[3], float yaw);

    // `Area_Transition` mode 0's request without a script issuing it - what
    // `area.goto <area> -1 -1` stages, with no caller to park. Public so a
    // probe can drive a transition without replaying whatever script would
    // have asked for it.
    void requestArea(int area);

    // ------------------------------------------------- THE TRANSITION BLOCK
    //
    // `dword_6A0600`, eight dwords: the source slot, the calling context's
    // table index, THE STATE, the outgoing AREA block, the destination, the
    // departure object, the arrival object and the watchdog's start time.
    // `docs/SCRIPT_VM.md` "The area transition" has the walk; `areaTransition`
    // below is its transcription.
    struct Transition {
        int  slot = -1, ctx = -1, state = 0;
        int  outArea = -1;                      // a1[3], identified by area id
        int  dest = -1, f1 = -1, f2 = -1;
        long startedFrame = 0;                  // a1[7], in frames here
        int  program = -1;                      // the object started by ScriptObject_Start:
                                                // -1 none, -2 "not resident, ends next frame"
    };
    const Transition& transition() const { return tr_; }
    // `dword_4C0130`: the context index a staged load will release, -1 when
    // none is in flight. While it is not -1 `Script_Execute` refuses every
    // `area.preload` and `area.goto` before dispatch.
    int deferredContext() const { return deferred_; }
    static constexpr int kNoContext = -2;       // a `requestArea` with no script behind it

    // ---------------------------------------------------- THE STAGED LOAD
    //
    // `Area_Load` (0x0040CC90) reads the AREA and SCENE chunks synchronously,
    // sets `dword_4E6D8C = 1` and calls `Area_TickLoad` once: case 1 asks for
    // the decor set, and cases 2-8 each return 0 until `sub_41EFA0` says the
    // queued read is done. That function is the engine's own async file
    // reader - its error strings name it `Async_LoadDuringFrame` - and
    // `Music_SetFadeMode` (0x0041EFF0) is misnamed: it is `Async_SetMode`,
    // 1 = 0x20000 bytes a frame, 2 = 0x10000, 0 = read it all now.
    // `Area_LoadIntoSlot` sets mode 1 before `Area_Load` and case 2 sets 0
    // after the set, so ONLY the `.3DO` set streams: `o3de_LoadScene` queues
    // the whole file (`sub_41F060` returns its size) and `sub_41F320`, called
    // at the end of every frame after the render, reads one 0x20000 slice.
    // So a transition's load takes ceil(size of MESHES/DECORS/<AREA +88>.3DO /
    // 0x20000) frames - Anekbah's 2099056 bytes are 17, AImpasse's 33392 are
    // 1 - and then cases 2..9 run in one pump tail. That is DATA-CONSTRAINED
    // (the count comes off the shipped file) and tier 6 for the pacing (no
    // capture times a load). The boot load is mode 0 and takes none.
    bool loading() const { return load_.active; }
    int  loadSlicesLeft() const { return load_.slicesLeft; }
    int  loadingSlot() const { return load_.slot; }
    // Async_SetMode(1)'s slice, for a probe that wants to show the count move.
    void setLoadSliceBytes(std::size_t n) { sliceBytes_ = n ? n : 0x20000; }
    std::size_t loadSliceBytes() const { return sliceBytes_; }
    // The slices the set behind `area` would take, 0 when the file is absent
    // (the engine's `sub_41F060` fails and the load completes synchronously).
    int loadSlicesFor(int area) const;

    // ------------------------------------------------- THE CONTEXT TABLE
    //
    // `dword_4E61E8`: 32 entries. `Script_NewContext` (0x00406290) takes the
    // first FREE one (`while (*v6) ++v6`), a context is freed by
    // `Script_FreeContext`, by `Script_ProcessActions` action 4, by
    // `sub_406320(slot)` when its slot is evicted and by `Zones_RegisterAll`'s
    // prune, and the pump runs the table in INDEX order every frame. A 33rd
    // live context is allocated and NOT listed - `Script_NewContext` returns
    // it after the loop falls off the table - so it never runs; here it is
    // not created at all and counted in `contextsUnlisted()`.
    static constexpr int kContextSlots = 32;
    // `Script_NewContext(slot, s0, s1, s2)` on `chunk`'s bytes, with the
    // zone id at +42 (-1 for a startup or message context). -> the index, or
    // -1 when the table is full.
    int  newContext(int slot, std::span<const std::byte> chunk,
                    const std::int32_t scripts[3], int zoneId, int area);
    // `Script_QueueAction` (0x004063D0): 1 enter, 2 activate, 3 leave, 4 free.
    // Returns what it returns - 0 when a second activate is refused - and
    // hands a leave queued on a context at status 9/10 to the transition
    // (mode 2).
    bool queueAction(int idx, int action);
    // `Script_FreeContext` (0x00406390).
    void freeContext(int idx);
    // The status word at +22 (docs/SCRIPT_VM "The context status word"), or
    // -1 for an empty entry.
    int  contextStatus(int idx) const;
    int  contextZone(int idx) const;
    // Contexts not yet done (status non-zero, or an action still queued), and
    // contexts ever LISTED - what a probe counts to see a startup script
    // created, or not created.
    std::size_t liveContexts() const;
    std::size_t contextsCreated() const { return created_; }
    std::size_t contextsUnlisted() const { return unlisted_; }

    // ------------------------------------------------------- THE ZONES
    //
    // `Zones_RegisterAll` (0x00406560) and `Actor_ScanZones` (0x00467770) LIVE
    // in the Session (2026-09-02, wave B; issue 10). `ZoneRegistry`
    // (script/zones.h) DECIDES - the four-table walk over both slots, the 16
    // prompt slots as the engine's own state machine, the per-frame touch and
    // arm - and this turns what it decided into `Script_NewContext` /
    // `Script_QueueAction` on real contexts, which is where the activate
    // dedupe (the FIFO and `+32`) lives. The registry is rebuilt wherever the
    // engine calls `Zones_RegisterAll` - `Area_TickLoad` case 9, the eviction
    // in `Area_LoadIntoSlot`, `scene.load`/`unload`, event 9, the restart -
    // and never per frame.
    //
    // THE ORDER INSIDE A FRAME is the engine's and load-bearing (zones.h):
    // `Script_Pump(1)` reads the prompt slots at the top of the frame
    // (05_sys.c:2107) and `Actors_TickAll` scans at the bottom (:2178), so the
    // pump sees the states the PREVIOUS frame's scan left - that one-frame
    // offset is the one-shot latch. And the scan is reached through the
    // player's ACTOR_STATE dispatch, not the actor tick as a whole:
    // `Actor_TickNpc` (state 1, 11..14), `Actor_TickDialogue` (16/17),
    // `Actor_TickUiHeld` (9) and `sub_466E70` (10) call it; `Actor_
    // TickScxDriven` / `Actor_StartPendingScx` (4/5 - a scene program driving
    // him) and the fight/shoot ticks do NOT. So the scan runs during a
    // CONVERSATION (T13 had that right: there is no dialogue gate) and does
    // not run while a `scx.play.player*` program owns him.
    const ZoneRegistry& zones() const { return zones_; }
    // `Game_HandleEvent` case 6, the action button - `Game_RaiseEvent(6, 4)`
    // from the input handler:
    //     if (g_DialogState == 3 || a2 != 4 || !dword_4E6B24) return 0;
    //     dword_4E6C90 = 1;
    // A press with no prompt slot taken never reaches the pump at all. The
    // flag is cleared by the pump at the end of its slot loop, so a HELD
    // button is one press per frame. -> what case 6 returns.
    bool pressAction();
    // `Actor_HeldObjectSlot(Actor_Player()) != -1`: the player has something
    // in his hands, which sends pump case 2 down `Script_RunToOpcode75` and
    // the unconsumed press to `sub_41C770`. Neither is modelled (labelled in
    // `pumpZoneSlots`); the registry refuses the activate rather than guess.
    void setHeldObject(bool on) { heldObject_ = on; zones_.setHeldObject(on); }
    // `dword_4E6B20`: activates queued and not yet ENDED - `++` when the pump
    // queues one, `--` at that action's `end` (or at `Script_Execute`'s
    // LABEL_8). The held-object tail of `end` gates on it reaching 0.
    int  activatesPending() const { return activatesPending_; }
    // `byte_4C012C`: the table index of the running MESSAGE-0 handler, -1
    // (0xFF) after any `end`. `Message_RunHandlers` writes it for a message-0
    // record and `fade.to_color`/`fade.from_color` (118/119, 0x405201 /
    // 0x4052C1) compare it with their own context's +30: a fade issued by the
    // message-0 handler is forced to 0xFF0000. No port of the fade colour
    // exists to consume it, so it is bookkeeping with a named reader.
    int  message0Context() const { return message0Ctx_; }

    // ---- THE SCREEN FADES, and there are TWO ---------------------------
    //
    // `Screen_StartColorFade` (0x00451DC0) is the colour one - mode 1 to,
    // 2 from - and `Screen_Fade` (0x0041E1B0) the black one, states 3 and 4
    // over a fixed 60 frames. They are independent state machines and the
    // engine runs them side by side, so this carries both.
    //
    // `weight` is what a frontend wants: how much of `colour` to mix into the
    // frame, 0..1. The ticker at 0x00451E60 is linear -
    // `alpha = clock * 255 / duration` for a "to" and `255 - ...` for a
    // "from" - so this is that alpha over 255. **What the engine does with it
    // is a full-screen quad through `I2D_SubmitQuad(&q, flag, 9)` with flag 1
    // for white, 2 for black and 4 for anything else, and those three blends
    // are NOT modelled**: mixing toward the colour matches the ramp's
    // direction for every mode and colour, and is labelled a model here
    // rather than dressed as the blend.
    struct ScreenFade {
        // 1 = to colour, 2 = from colour, 3 = FROM black, 4 = TO black.
        //
        // **3 and 4 are the opposite way round from the opcode names**, and
        // that was a real bug rather than a quibble: the table calls 132
        // `fade.to_black` and 133 `fade.from_black`, and the ticker at
        // 0x00451FFE says otherwise. It draws a grey quad that MULTIPLIES the
        // frame - 0 is black, 255 leaves it alone - and in state 3 that grey
        // is `clock * 255 / duration`, RISING, so state 3 goes black -> normal
        // and is a fade IN. State 4 takes `~al`, falling, and is the fade out.
        //
        // Backwards, every scene went black two seconds in and stayed there:
        // `fade.to_black` is the FIRST thing SCENE 55 does (pc 1228) and its
        // partner is 167 instructions later, past every waiting beat.
        int   mode = 0;
        std::uint32_t colour = 0;   // 0x00RRGGBB
        float duration = 0.0f;      // frames
        float clock = 0.0f;
        bool  running() const { return mode != 0; }
        float weight() const {      // 0..1 of `colour` to mix in
            if (mode == 0 || duration <= 0.0f) return 0.0f;
            float k = clock / duration;
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;
            // rising for the two that END on the colour (to colour, to black),
            // falling for the two that end on the scene
            return (mode == 1 || mode == 4) ? k : 1.0f - k;
        }
        // THE BLACK FADE IS NOT A FULL-SCREEN QUAD, and the colour one is.
        // Transcribed from the ticker (0x00451E60): the colour half submits
        // ONE quad at (0,0) (w,0) (w,h) (0,h); the black half submits TWO, at
        //
        //   (0, h-v3) (w, h-v3) (w, h) (0, h)      the bottom band
        //   (0,   v3) (w,   v3) (w, 0) (0, 0)      the top band
        //
        // with `v3 = (height << 6) / 480` - 64 rows at 480 - and a colour per
        // vertex: `v8`'s grey on the two INNER corners, `v7`'s on the two at
        // the screen edge. So it is a letterbox vignette that darkens from the
        // edges, never the middle of the picture.
        //
        // The two greys, exactly as the ticker computes them from
        // `v4 = clock * 255 / duration`:
        //
        //   state 3   v8 = v4                      0 -> 255
        //             v7 = v4 < 0x80 ? 2*v4 : 255  0 -> 255, saturating early
        //   state 4   v7 = ~v4                     255 -> 0
        //             v8 = (~v4) >> 1              127 -> 0
        //
        // past the end, state 4 clears (`mode = 0`) and state 3 holds both at
        // 255, which under the multiply is the frame untouched.
        int bandGrey(bool outer) const {
            if (mode != 3 && mode != 4) return 255;
            if (duration <= 0.0f) return 255;
            if (clock > duration) return 255;         // state 3's hold
            const int v4 = static_cast<int>(clock * 255.0f / duration) & 0xFF;
            if (mode == 3)
                return outer ? (v4 < 0x80 ? 2 * v4 : 255) : v4;
            const int n = (~v4) & 0xFF;
            return outer ? n : (n >> 1);
        }
    };
    const ScreenFade& colourFade() const { return colourFade_; }
    const ScreenFade& blackFade() const  { return blackFade_; }
    // `Screen_StartColorFade`'s refusal: a running fade blocks a new one
    // unless the new one is a "from" over a running "to".
    void startColourFade(int mode, std::uint32_t colour, float duration);
    // `Screen_Fade` (0x0041E1B0). `fromBlack` is opcode **132** - the one the
    // table calls `fade.to_black` and which actually fades IN - and it sets
    // state 3 over a fixed 60 frames. 133 sets state 4, the fade OUT, and only
    // from state 3: you cannot fade out without having faded in.
    void startBlackFade(bool fromBlack);
    // One frame of both, in FRAMES (the engine's `flt_4C30D8`).
    void tickFades(float dt = 1.0f);
    // `dword_4E6C7C`: the boot AREA's startup context (`Game_NewGame` stores
    // the active slot's block +0 after `State_Apply`), cleared by `end` when
    // ANY context ends action 1 and by event 5 when that context answers a
    // screen with a non-zero value. Its reader is `sub_408410`'s pending-save
    // gate (`if (!dword_4E6C7C && dword_4C09B4 != -1) Game_LoadSave`), which
    // is not modelled. -1 = clear.
    int  bootContext() const { return bootCtx_; }
private:
    ScreenFade colourFade_, blackFade_;
public:
    // The context's flag byte at +40: 0x10 "did something visible" (24
    // handlers OR it in before their dry-run test - see `kVisibleOps`), 8
    // "free my slot's SCENE block at end" (72, issue 25), 2 (69/75).
    int  contextFlags(int idx) const;
    // The blocks `end` released under bit 8 (`sub_412060(dword_69BC44[slot])`).
    int  sceneBlocksFreedAtEnd() const { return sceneBlocksFreed_; }

    // What the pump did with the zones, frame by frame - for a probe. `ctx`
    // is the context the action went to; `queued` is `Script_QueueAction`'s
    // return (a refused second activate is 0).
    struct ZoneApplied {
        long frame = 0;
        ZoneEvent::Kind kind = ZoneEvent::Kind::Touch;
        std::int16_t zone = 0;
        int ctx = -1, action = 0;
        std::size_t script = 0;
        bool queued = false, reused = false;
    };
    const std::vector<ZoneApplied>& zoneLog() const { return zoneLog_; }
    // `Script_Pump` step 2: the frames on which an unconsumed press posted
    // message 26 (`Game_HandleEvent(43, {26, <uninitialised>})`), and whether
    // a handler was found. The sender word is stack garbage in the engine
    // (`evArgs[1]` is never written); the port passes -1, labelled.
    struct NothingHere { long frame; bool handled; };
    const std::vector<NothingHere>& nothingHere() const { return nothingHere_; }
    // The prompt slots' context pointers (+0 of `unk_4E6B30`), by slot index.
    int  promptContext(int slot) const { return promptCtx_[static_cast<std::size_t>(slot & 15)]; }

    // The zone's LEAVE action on a transition caller: `Script_QueueAction(ctx,
    // 3)` from `Script_Pump` slot state 3/5. With objects in flight this is
    // what starts the arrival object (mode 2, state 7 -> 9). The scan queues
    // it when the player's feet leave the quad - which is now what happens
    // for a zone the scan ARMED.
    //
    // **RECONSTRUCTION, narrowed 2026-09-02**: a transition caller whose zone
    // is in NO prompt slot - a probe that queued the enter action itself, or
    // a startup script's own `area.goto` - can never receive a leave from the
    // scan, and the Session issues it the frame the departure object ends,
    // which is the earliest the engine could start f2 in either order.
    // `setLeaveFromZones(true)` turns the stand-in off entirely.
    void queueLeave(int idx) { queueAction(idx, 3); }
    void setLeaveFromZones(bool on) { leaveFromZones_ = on; }
    // Extra hooks beside the built-in registry, kept for a caller that wants
    // to be told when `Zones_RegisterAll` ran or to veto a zone's resolution
    // (T11's API). With none installed the registry alone decides the prune.
    void setZoneHooks(std::function<void()> onRegister,
                      std::function<bool(int)> resolves) {
        onZonesRegister_ = std::move(onRegister);
        zoneResolves_ = std::move(resolves);
    }

    // ------------------------------------------------- THE WORLD HOOKS
    //
    // `script/hooks.h`: what the twenty world opcodes reach beyond the DB.
    // The engine edits its LOADED BLOCKS in place - `Actor_SetProperty`
    // writes the chunk's 276-byte record, 67/68/69 write its +270, `Scene_
    // LoadProps` writes the prop record's +0 - and frees them on unload, so
    // those edits live exactly as long as the block. The resident slots'
    // `areaChunk`/`sceneChunk` ARE those blocks here, and `props.h` walks
    // them. Installed on every context (`newContext`), so a startup, message
    // or zone script all reach the same world.
    //
    // `word_4E6CA0`: the 50 object slots' OBJECTS ids, -1 free. `Scene_
    // LoadProps` (0x00409FC0) fills it from the prop tables at `Area_TickLoad`
    // case 9 and after `scene.load` - FIRST free slot, AREA table then the
    // SCENE's, for every record whose state bit 0 is set, the slot written
    // into the record's +0 and bit 1 linking it into the scene. The unload
    // (0x00409F.. `Scene_UnloadProps`) writes -1 back for every such record.
    int  objectSlotId(int slot) const;
    // `Actor_HeldObjectSlot` by actor id (-1 the player): -1 when none.
    int  heldSlotOf(int actor) const;
    // `Object_ShowInScene` / `HideFromScene`: what a frontend draws props from.
    const std::set<int>& shownSlots() const { return shownSlots_; }
    // What the hooks were asked to do in 3D and did not: for a frontend.
    struct PropEvent { const char* what; int actor; int slot; int address; long frame; };
    const std::vector<PropEvent>& propEvents() const { return propEvents_; }

    // ------------------------------------------------------- RESTART
    //
    // `Script_Pump` phase 1 opens `if (g_RestartRequest) { Script_Pump(3);
    // g_RestartRequest = 0; Script_Pump(2); Screen_FadeFromColor(...); }`.
    // Phase 3 is `sub_40E260` plus the dialogue close and the transition
    // block; phase 2 is `Game_NewGame`. VM opcode 152 sets the request; the
    // menu's "new game" is this.
    void requestRestart() { restart_ = true; }
    void restart();

    // ------------------------------------------- scene.load / scene.unload
    //
    // The two handlers (71 at 0x403950, 72 at 0x403AF0) walk the resident
    // slots for the operand's area and, where it IS resident, swap the scene
    // in or out on the spot: free the old SCENE block's context, unload the
    // old scene, load the new one and (71) create and queue its startup
    // context - `Script_NewContext(slot, block[+4], 0, 0)` then
    // `Script_QueueAction(ctx, 1)` - before writing the DB field
    // (`Area_SetLoadedScene`, 0x0040B120) and re-registering the zones. For an
    // area that is not resident only the DB field changes, and the next load
    // of that area brings the scene with it. These are the handler bodies;
    // the opcode arms in `frame` call them.
    void sceneLoad(int area, int scene);
    // `caller` is the context executing the opcode, or -1 for a caller with
    // no script: when the unloaded scene sits in the CALLER'S OWN slot the
    // handler sets ctx+40 bit 8 and the block is freed at that context's
    // `end` (issue 38's deferred half), not here.
    void sceneUnload(int area, int caller = -1);

    // ------------------------------------------------------ MESSAGES
    //
    // `Message_RunHandlers` (0x00409420), what `Game_HandleEvent` case 43
    // runs: search the resident SCENE's subscription table (+36, count +54),
    // then the AREA's (+68, +86), then IAM\GLOBAL's (+8, +24); first match
    // wins. The handler gets a fresh context whose parameter block is
    // {message, sender}. Message 25 executes inline and is freed on return;
    // every other one is queued with actions 1 then 4 - run next frame, then
    // freed. -> whether a handler was found.
    //
    // Two things are NOT resolved here and are labelled: the sender is passed
    // through as given (the engine maps a slot to an actor id for messages
    // 0..12 except 4 and to an object id for 4/20/25, and this Session has no
    // slot tables), and nothing in the Session POSTS a message yet - the
    // pump's "nothing here" 26 is issue 7.
    struct MessageRun {
        int         message = 0, sender = -1;
        std::string table;              // "scene", "area" or "global" - which won
        std::size_t offset = 0;         // the handler script in that chunk
        bool        inlineRun = false;  // message 25 runs at once
        long        postedFrame = 0, ranFrame = -1;
    };
    bool postMessage(int message, int sender);
    const std::vector<MessageRun>& messagesRun() const { return messages_; }

    // ----------------------------------------------------- player.become
    //
    // Opcode 56 (handler 0x402F60): when the CHARACTERS operand is not the
    // player's current actor id, the player moves into that body - see
    // `becomePlayer`. The id is read back out of the game DB's player record
    // (+60, id at +272), which the handler's own `rep movsd` writes, so a
    // save carries it and `IAM\START`'s -1 is what a new game starts with.
    void becomePlayer(int actor);
    int  playerActor() const;
    // The bit `character.show`/`hide` write: the resident chunk's 20-byte
    // character record for this id (AREA +40 / count +72, then SCENE +8 /
    // +40, id at +2) carries the `ObjectShown` bit index at +18. -1 when no
    // record names the id.
    int  shownBitOf(int actor) const;
    // The bank a CHARACTERS id resolves to (the actor record's +72, the
    // `.CTL` name `Actor_LoadBankList` opens). Empty when nothing names it.
    std::string bankOfActor(int actor) const;
    // The spawned character with this id, or nullptr - the shown slot's
    // tables first, the same order `Scene_FindObjectRecord` searches.
    const Character* characterOf(int actor) const;

    // ---------------------------------------------------- the scene objects
    //
    // `Script_PlayAllScripts` is the other half of a frame: the world scripts
    // decide what happens, the scene objects animate it. Make the chunk's
    // `.SCX` resident and `scx.play*` starts a program on one of its objects.
    //
    // Optional on purpose - a Session that only wants the script decisions
    // (`run_scripts`, `walk_zone`) never loads a 7 MB scene.
    // ------------------------------------------------- answering `ui.open`
    //
    // A script that reaches `ui.open` parks at status 6 and is resumed only
    // when a PERSON answers the screen - `Game_HandleEvent` case 5 writes the
    // named variable and releases it. So the pump cannot release it on its
    // own, and a Session with no screens attached will sit there for ever,
    // which is the engine's behaviour and not a defect.
    //
    // Attach the widget tree and the Session answers by WALKING the screen
    // with the engine's own input words, exactly as a player would. Nothing
    // is supplied: the presses are, the answer is derived.
    void attachUi(const UiWidgets& w, const std::string& typedName = "Kay'l") {
        ui_ = &w;
        uiName_ = typedName;
    }

    // ---------------------------------------------- answered by a PERSON
    //
    // The third mode, and the one a playable build needs. `attachUi` DERIVES
    // an answer by walking the tree, which is right for a headless check and
    // wrong for a game: there the screen belongs to whoever is holding the
    // keyboard. Turn this on and the Session parks instead, names the screen
    // it is parked on, and resumes only when `answerUi` is called - which is
    // `Game_HandleEvent` case 5 and nothing else.
    void answerUiFromPerson(bool on) { personAnswers_ = on; }
    // The track the scripts last asked for - VM opcode 103, `music.play`,
    // whose field 0 names `TRACKS\%d.ADP` and field 1 is the loop flag - or
    // the area's own (`AREA +142`) when event 9 switched to an area with one.
    // The engine's own handler skips the call when the track is already
    // playing, which is why this is a LEVEL and not an event: a caller
    // compares it with what it has going. -1 is "nothing asked yet".
    int  musicTrack() const { return musicTrack_; }
    bool musicLoops() const { return musicLoop_; }

    // ------------------------------------------------------- THE WORLD
    //
    // What a frontend needs to DRAW what the scripts have decided, and every
    // one of these is read out of the shipped chunk rather than supplied:
    //
    //   * which decor is in state 2 - the SHOWN slot's `AREA +88`, the `.3DO`
    //     stem `Area_LoadSet` builds its path from (`+97` is the `.SCX`);
    //   * the camera table that slot's scripts can name (`worldcam.h`);
    //   * and the camera they HAVE named, moved the way `camera.set`'s
    //     second field says to move it.
    //
    // Nothing here decides anything. `camera.set` picks the id, the area
    // header picks the set, and this only resolves them.
    int currentArea() const { return slots_[curSlot_].area; }
    const std::string& setName() const { return slots_[curSlot_].set; }
    const std::string& scxName() const { return slots_[curSlot_].scx; }
    const WorldCameras& cameras() const { return slots_[curSlot_].cams; }
    // `Camera_FindWorld`: slot 0's area then scene table, slot 1's, GLOBAL.
    const WorldCamera* findCamera(int id) const;

    // THE PLAYER'S WORLD POSITION, which `actor.goto_address` (73) and
    // `setPlayerPosition` set here. The actor runtime that would move him
    // afterwards is not driven by this session, so without a walker attached
    // this is where he was last TELEPORTED and nothing else - enough to
    // resolve a subject-relative camera, not enough to follow him.
    bool playerPlaced() const { return playerPlaced_; }
    // Counts every landing of `placeActorAt` - a TELEPORT (`actor.goto_address`).
    // Whoever moves him from outside compares it frame to frame and re-seats
    // him, because `setPlayerPosition` would otherwise write the walker's
    // position straight back over the address the script chose.
    int  placementSeq() const { return placementSeq_; }
    // `player.anim.hold` (104) / `player.anim.release` (105):
    // `Actor_HoldAnimation(Actor_Player(), 1 / 0)` sets or clears bits 0x80 and
    // 0x01 of the CHANNEL's flag word (SCRIPT_VM "104 / 105"). Neither of its
    // two calls touches a transform: `sub_45A870` writes a lone IDLE word into
    // the channel's input queue, `Cef_TickChannel` re-asserts it every tick
    // while `flags & 0x81`, and the queue rule drops it - so the channel keeps
    // TICKING with nothing pressed, which walks a gait to its stand state. The
    // player stops and stands; he does not freeze and does not reset to rest.
    // The scripts bracket a staged camera sequence
    // with them: AREA 222's airlock beat holds at pc 2360 and releases at
    // 2414, after its last `camera.set`.
    //
    // Corrected 2026-09-03: this said the tick "skips its whole input pass" and
    // that the update "pins the transform to rest". It does neither - it re-pins
    // the blend and carries on, and the port's literal reading of "rest" drew a
    // T-pose (todo/omk-play.md 43). A held channel is also what tells the
    // FOLLOW CAMERA to stand down (`sub_415D10`), which is omk-play 42.
    bool playerAnimHeld() const { return playerAnimHeld_; }
    const float* playerPos() const { return playerPos_; }
    float playerYaw() const { return playerYaw_; }
    // The address the last `actor.goto_address` named, or -1. Reported so a
    // check can assert the id the script chose as well as where it landed.
    int  playerAddress() const { return playerAddress_; }

    // Place the player at an ADDRESSES id in either resident area
    // (`Address_Find` walks both slots), or hold it until the area that owns
    // it arrives. -> whether it landed. Public because `actor.goto_address` is
    // the only thing that ever places him and a tool needs to reach it
    // without replaying the whole intro.
    bool placeActorAt(int addressId);

    // Follow the player through the scene program that is PLAYING him.
    // `scx.play.player*` (46/90) hands an object the player, and that object's
    // body animation is what moves him - so while one is running his position
    // is its placement plus the clip's accumulated root motion, not the
    // address he was teleported to. Camera 0 is relative to him, so this is
    // what makes a cutscene visible at all.
    void trackPlayer();

    // The camera as `Camera_Request` mode 12 currently holds it: the target
    // when the move is over, and the interpolation while it is not.
    // -> nullptr before any script has named one. A camera whose points are
    // OFFSETS from an actor (`WorldCamera::absolute()` false) is returned like
    // any other and cannot be pointed anywhere until something knows where
    // that actor is: `IAM\START` leaves the player at the (-1,-1,-1) sentinel
    // and the actor runtime is not driven from here, so a caller has to check.
    const WorldCamera* camera() const { return haveCam_ ? &camNow_ : nullptr; }
    int  cameraId() const { return haveCam_ ? camTo_.id : -1; }
    // Where the move is going, as opposed to where it currently IS. A caller
    // reporting a cut wants this one; a caller pointing a camera wants
    // `camera()`.
    const WorldCamera* cameraTarget() const { return haveCam_ ? &camTo_ : nullptr; }
    int  cameraTravel() const { return camTravel_; }
    bool cameraMoving() const { return camTravel_ > 0; }

    // Whether `camera.set.wait` holds the script for the length of its move.
    // Off by default - see `Interpreter::setCameraWaitSuspends` for why - and
    // on for anything with a frame clock.
    void setCameraWait(bool on);
    // And whether a WAITING `scx.play*` (46, 58, 60) holds the script until
    // the object's program ends. Off by default for the same reason.
    void setObjectWait(bool on);

    int  pendingUiScreen() const { return pendingUiScreen_; }
    int  pendingUiParam() const { return pendingUiParam_; }
    // Write the named variable and release the parked context. Safe to call
    // with nothing parked; it does nothing.
    void answerUi(int value);

    // What the last `ui.open` did, for a caller that wants to see it.
    struct UiAnswer { int screen, variable, value; bool derived; };
    const std::vector<UiAnswer>& uiAnswers() const { return uiAnswers_; }

    bool loadScene(const std::string& scptDataDir, ChunkKind kind, int chunk) {
        scptData_ = scptDataDir;
        // Record which AREA's `.SCX` this is, so `reloadScene` can tell a
        // scene load over the same area (keep every running program) from an
        // area change (rebuild). Asked for by SCENE the area is not known
        // here, and -1 makes the next call rebuild once rather than assume.
        sceneArea_ = kind == ChunkKind::Area ? chunk : -1;
        if (!scene_.load(scptDataDir, iam_, table_, kind, chunk)) return false;
        // `Area_LoadScx` binds the `.sfx` in the same breath as loading the
        // `.SCX`; so must every path that makes one resident.
        attachSceneSfx();
        return true;
    }
    const SceneRunner& scene() const { return scene_; }
    // For a caller that must attach the scene's `.SFX` - the effects belong to
    // the scene and the frontend is the only thing that knows where the files
    // are.
    SceneRunner& sceneMutable() { return scene_; }

    const std::vector<Announced>& announced() const { return announced_; }

private:
    // `Script_NewContext`'s 0x2C-byte block, reduced to what the pump reads:
    // +0/+4/+8 the three script slots, +12 the pc, +16 the stack, +22 the
    // STATUS, +24..27 the action FIFO (+28 its count), +30 the table index,
    // +31 the area slot, +32 the current action, +36 the parameter block,
    // +42 the owning zone id.
    struct Ctx {
        std::vector<std::byte> code;
        std::size_t pc = 0;
        bool        started = false;
        // The status word (docs/SCRIPT_VM "The context status word"): 0 idle,
        // 1 running, 4 waiting on a scene object, 5 a superseded transition
        // caller, 6 a screen, 7 a camera move, 8 `area.preload`'s load, 9
        // retry the transition next frame, 10 the transition, 11 its last
        // step done. `Script_Execute` runs `while (status == 1)`, so anything
        // else is a park with the pc and the stack intact.
        int         status = 0;
        std::int32_t scripts[3] = {0, 0, 0};
        std::deque<int> actions;             // the 4-deep FIFO
        int         current = 0;             // +32
        int         slot = 0;                // +31
        int         zoneId = -1;             // +42
        // status 7's resume: `Game_HandleEvent` case 4 fires when the move
        // ends, and the move is counted in frames, so this is a COUNTDOWN.
        int         waitingForCamera = 0;
        // status 4's resume: case 3 fires when the program ends. This is the
        // program it is parked on. It is the ~5 second beat before a
        // cutscene's dialogue: AREA 118 shows Kay'l, starts his animation
        // with 60, and waits.
        int         waitingForProgram = -1;
        // status 4's OTHER flavour. `Game_HandleEvent` case 3 resumes any
        // context at 4 and does not care what put it there, so the status
        // word alone cannot say what is being waited on: `waitingForProgram`
        // is a scene object's program, and this is the walk
        // `player.move.wait` (89) ordered. -1 = not on a move. Never both.
        int         waitingForMove = -1;     // the ADDRESSES id he walks to
        // status 3: the fight `fight.begin` (62) entered. Only
        // `Game_HandleEvent` case 2 clears it - a context parked here with no
        // fight runtime is parked for ever, which is the engine's shape and
        // the reason the park is armed only when a fight hook is installed.
        int         fightOpponent = -1;
        // Which chunk's STARTUP script this is, if it is one, and the area
        // the block sits over - how `scene.load`/`unload` find "the SCENE
        // block's context".
        bool        sceneStartup = false;
        int         chunkArea = -1;
        // Index into `messages_` when this is a message handler, else -1.
        int         message = -1;
        // The flag byte at +40, zeroed by `Script_NewContext` and only ever
        // OR'd: 0x10 by the 24 "visible" handlers before their dry-run test
        // (sticky - nothing clears it, so `Script_Run`'s abort loop
        // `while (!(+40 & 0x10))` never runs again on a context that has
        // done anything), 8 by `scene.unload` of the caller's own slot, 2
        // by 69/75.
        std::uint8_t flags40 = 0;
        Interpreter vm;
        Ctx(GameState& s, const OpcodeTable& t) : vm(s, t) {}
    };

    std::string        iam_;
    GameState&         state_;
    const OpcodeTable& table_;
    std::array<std::unique_ptr<Ctx>, kContextSlots> ctxs_;   // dword_4E61E8
    std::size_t created_ = 0, unlisted_ = 0;
    int dialogState_ = 1;
    std::vector<Announced> announced_;
    struct Ann { std::string domain; int field; };
    std::map<std::uint8_t, Ann> announce_;   // empty = announce nothing
    SceneRunner scene_;                      // empty unless loadScene ran
    Pedestrians peds_;                       // empty unless loadTraffic ran
    std::string dataRoot_;                   // the gamedata tree, from loadTraffic
    int         trafficSlot_ = -1;           // the slot whose circuit `peds_` holds
    int         streetActivity_ = kDefaultStreetActivity;
    void loadTrafficFor(int slot);
    // The AREA whose `.SCX` `scene_` holds. The file is the area's (`+97`),
    // so a scene loaded over the same area keeps the runner - and every
    // program running - exactly as the engine does; only an area change
    // rebuilds. -1 = nothing loaded yet.
    int sceneArea_ = -1;
    const UiWidgets* ui_ = nullptr;          // null = no screens attached
    std::string uiName_ = "Kay'l";
    std::vector<UiAnswer> uiAnswers_;
    int answerScreen(int screen, int param);
    bool personAnswers_ = false;
    int  musicTrack_ = -1;
    bool musicLoop_ = false;
    int  pendingUiScreen_ = -1, pendingUiParam_ = -1, pendingUiVar_ = -1;
    int  pendingUiCtx_ = -1;
    std::string scptData_;                   // set by loadScene; "" = no scene
    int  entered_ = 0;
    std::vector<MessageRun> messages_;

    // ---- the two resident slots, the transition block and the staged load
    ResidentSlot slots_[2];
    int  active_ = 0;                        // dword_69BC60
    int  activeArea_ = -1;                   // dword_69BC64
    int  curSlot_ = 0;                       // the decor in state 2
    Transition tr_;                          // dword_6A0600
    int  deferred_ = -1;                     // dword_4C0130
    struct Load { bool active = false; int slot = 0; int slicesLeft = 0; } load_;
    std::size_t sliceBytes_ = 0x20000;       // Async_SetMode(1)'s ElementSize
    int  asyncMode_ = 0;                     // dword_4E91C0: 0 read now, 1 a slice a frame
    bool leaveFromZones_ = false;
    bool restart_ = false;                   // g_RestartRequest
    std::function<void()>    onZonesRegister_;
    std::function<bool(int)> zoneResolves_;

    // ---- the live zones (Zones_RegisterAll / Script_Pump's slot loop /
    //      Actor_ScanZones)
    ZoneRegistry zones_;
    bool actionPressed_ = false;             // dword_4E6C90
    bool heldObject_ = false;                // Actor_HeldObjectSlot(player) != -1
    int  activatesPending_ = 0;              // dword_4E6B20
    int  message0Ctx_ = -1;                  // byte_4C012C
    int  bootCtx_ = -1;                      // dword_4E6C7C
    int  sceneBlocksFreed_ = 0;
    std::array<int, 16> promptCtx_{};        // unk_4E6B30[k] + 0: the context
    std::vector<ZoneApplied> zoneLog_;
    std::vector<NothingHere> nothingHere_;
    bool playerDriven_ = false;              // a scx.play.player* program owns him
    bool playerWalks_ = false;               // something outside feeds his position
    int  placementSeq_ = 0;                  // landings of placeActorAt
    bool playerAnimHeld_ = false;            // Actor_HoldAnimation's 0x81 on the player
    // Script_Pump(1) step 1 (the slot loop) and step 2 (the unconsumed press).
    void pumpZoneSlots();
    // Actors_TickAll -> Actor_ScanZones, at the bottom of the frame.
    void scanZonesNow();
    // `Script_Execute`'s LABEL_8 for a context whose status just reached 0
    // without `end`: `if (+32 == 2) { --dword_4E6B20; +32 = 0; }`.
    void finishRun(Ctx* c);
    // `Game_HandleEvent` case 5's tail: the answer written, the context
    // released, and the boot context forgotten on a non-zero answer.
    void uiAnswered(int ctxIdx, int value);
    // The 24 opcodes whose handler ORs 0x10 into ctx+40 (from the assembly:
    // 46, 57, 58, 59, 61, 62, 63, 70, 73, 89, 90, 92, 94, 95, 96, 118, 119,
    // 122, 123, 126, 129, 130, 138, 139 - NOT 91 or 93, which issue 7's
    // "89-96" implied). Applied to every recorded call, which is the OR.
    static bool visibleOp(std::uint8_t op);

    // ---- the world hooks (script/hooks.h) - see the public section
    std::array<int, 50> objectSlotIds_{};    // word_4E6CA0
    std::map<int, int> heldSlot_;            // Actor_HeldObjectSlot, by id; -1 = player
    std::set<int> shownSlots_;               // Object_ShowInScene / HideFromScene
    std::vector<PropEvent> propEvents_;
    class Hooks final : public WorldHooks {
    public:
        explicit Hooks(Session* s) : s_(s) {}
        bool getActorProperty(int actor, int property, std::int32_t& out) override;
        bool setActorProperty(int actor, int property, std::int32_t value) override;
        int  heldObjectField(int actor) override;
        void setHeldObjectField(int actor, int objectId) override;
        int  heldObjectSlot(int actor) override;
        int  objectIdInSlot(int slot) override;
        void clearObjectSlot(int slot) override;
        void releaseObject(int actor, bool remove) override;
        void holdObject(int actor, int slot) override;
        void showObject(int slot) override;
        void hideObject(int slot) override;
        bool propBySlot(int slot, PropRef& out) override;
        bool propById(int id, PropRef& out) override;
        void placeObjectAt(int objectId, int address) override;
    private:
        Session* s_;
        // `Actor_FindById` over the two slots' blocks (the DB's own record is
        // the interpreter's): a writable span on the record, empty when none.
        std::span<std::byte> record(int actor);
        // `Scene_FindObjectRecord`: the 20-byte table, AREA then SCENE. 86/93
        // read garbage in the engine when this misses; the hook refuses.
        bool inObjectTable(int actor) const;
    };
    Hooks hooks_{this};
    // `Scene_LoadProps(area, scene, 1)` over one slot's tables, and the
    // unload's `word_4E6CA0[rec+0] = -1`.
    void loadProps(int slot, bool area, bool scene);
    void unloadProps(int slot, bool area, bool scene);
    // Everything `Game_NewGame` / `sub_40E170` zero: the prompt slots, the
    // object slots, the counters.
    void resetWorld();

    // `Area_Transition(&dword_6A0600, slot, ctx, mode, area, f1, f2)`.
    int  areaTransition(int mode, int ctxIdx, int slot, int area, int f1, int f2);
    // `Area_LoadIntoSlot` / `Area_Load` / `Area_TickLoad` / the pump's tail.
    void loadIntoSlot(int slot, int area);
    // The `.sfx` beside the resident `.SCX` - `Area_LoadScx`'s second half.
    void attachSceneSfx();
    void areaLoad(int area, int slot);
    bool tickLoad(int slot);
    void completeLoad(int slot);
    void pumpTail();
    // sub_40C090: free the slot's contexts (its blocks' and `sub_406320`'s).
    void evictSlot(int slot);
    // sub_419AF0 / sub_419A90: the decor into state 2 / back to state 1.
    void showSet(int area);
    void hideSet(int area);
    // `ScriptObject_Start(obj, outgoing block, ctx, 1)` for the transition.
    void startTransitionObject(int obj);
    // `Game_HandleEvent` case 3 for the transition's object.
    void transitionObjectEnded();
    void clearTransition();
    void zonesRegisterAll();
    int  slotOf(int area) const;
    void setStatus(int ctxIdx, int status);
    // One context's slice of the frame - `Script_ProcessActions` then
    // `Script_Execute`.
    void runContext(int i);
    void processActions(int i);
    void execute(int i);
    // The stubbed handlers' side effects, one recorded call at a time.
    void onCall(int i, const Call& call);
    // `Script_NewContext(slot, block[+4], 0, 0)` + `Script_QueueAction(ctx,
    // 1)`: queue a chunk's startup script. -> the index, or -1.
    int  queueStartup(int slot, bool isScene);
    // Make the resident `.SCX` follow the shown slot - the scene over `area`
    // when the DB names one, else the area's own.
    void reloadScene(int area, int scene);
    // The 276-byte actor record with `actor` at +272, out of the shown
    // slot's AREA (+56 / +80) then SCENE (+24 / +48), then the other slot's
    // - `Actor_FindById` (0x0040B190). -> false when no record.
    bool actorRecord(int actor, std::vector<std::byte>& chunk,
                     std::size_t& off) const;
    void fillSlotTables(ResidentSlot& s);

    // ---- the world, as the shown slot describes it
    float playerPos_[3] = {0, 0, 0};
    float playerYaw_ = 0.0f;
    bool  playerPlaced_ = false;
    int   playerAddress_ = -1;
    int   pendingAddress_ = -1;
    // per-clip root motion, kept because a clip is sampled every frame
    std::map<int, std::vector<std::array<float, 3>>> rootMotion_;
    bool         camWait_ = false;
    std::function<bool(int, int)> moveHook_;
    std::function<bool(int)>      fightHook_;
    int          fightCamTravel_ = 0;      // `dword_930818` for mode 14, recorded
    int          camSubjectAddress_ = -1;
    bool startPlayerMove(int addressId, int ctx);
    bool beginFight(int opponentId);
    bool         objWait_ = false;
    bool         haveCam_ = false;
    WorldCamera  camFrom_, camTo_, camNow_;
    int          camTravel_ = 0, camElapsed_ = 0;
    long         frameNo_ = 0;
    mutable DialogPlayer dialog_{state_, table_};
    std::string  morphDir_;              // "" = conversations end at once
    std::string  speakerModel_;
    std::vector<Shown> shown_;
    // The characters 78/79/`player.become` showed that NO placement record
    // names - what `shown_` was made of before the spawn landed. `shown_`
    // itself is derived: `rebuildShown` puts the slots' attached characters
    // in front of these.
    std::vector<Shown> scriptShown_;
    // `word_69BC80`: 100 int16, -1 free, holding the ACTOR ID of whoever took
    // the entry. `Actors_SpawnFromTables` takes the first free one per record
    // and writes its index back to record +0; -1 when the table is full.
    // Reset by `resetWorld` (so by `loadArea` and `restart`), and a slot's
    // entries handed back by `evictSlot`.
    std::array<std::int16_t, 100> actorSlots_{};
    // `Actors_SpawnFromTables(area, scene, 1)` - `Area_TickLoad` case 5.
    void spawnFromTables(int slot, bool area, bool scene);
    void freeActorSlots(int slot, bool area, bool scene);
    void rebuildShown();
    void showCharacter(int actor);
    void hideCharacter(int actor);
    double       frameSeconds_ = 1.0 / 30.0;
    void openDialog(int id);
    // `camera.set` / `camera.set.wait`: cut when the move is 0 frames long,
    // travel otherwise. A camera that is not in any resident table is left
    // alone, which is what `Camera_FindWorld` returning 0 does.
    void applyCamera(int id, int travel);
    void tickCamera();

    void record(const std::vector<Call>& calls);
};

}  // namespace omk

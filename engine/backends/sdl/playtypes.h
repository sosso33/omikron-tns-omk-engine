// SPDX-License-Identifier: GPL-3.0-or-later
// THE TYPES `main` used to define for itself - the model caches, the staged
// bodies, the world slots, the sky, the set load and the rest - moved to
// namespace scope by `todo/play-split.md` (2026-10-02) without a change, so
// that a second translation unit (the frame, `playframe.cpp`) can name them.
// In a NAMED namespace, not an anonymous one: every unit must see the SAME
// types. No SDL.
#pragma once

#include "formats/anim.h"
#include "formats/ctl.h"
#include "formats/map2d.h"
#include "formats/mesh3do.h"
#include "formats/scx.h"
#include "formats/tex3dt.h"
#include "input/bindings.h"
#include "actor/fight.h"
#include "actor/shoot.h"
#include "actor/shootfire.h"
#include "actor/shoothit.h"
#include "actor/shootaim.h"
#include "actor/shootmove.h"
#include "actor/projectile.h"
#include "actor/moves.h"
#include "actor/slider.h"
#include <cctype>
#include <unordered_map>
#include <limits>
#include <optional>
#include "actor/pose.h"
#include "actor/speaker.h"
#include "actor/player.h"
#include "actor/moves.h"
#include "actor/walk.h"
#include "o3de/collision.h"
#include "o3de/geom3do.h"
#include "o3de/particles.h"
#include "o3de/pointplace.h"
#include "app/playhelpers.h"
#include "app/playoptions.h"
#include "app/game.h"
#include "o3de/shadow.h"
#include "platform/threads.h"
#include "o3de/shimmer.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/voiceover.h"
#include "formats/adpcm.h"
#include "script/area.h"
#include "script/savefile.h"
#include "formats/light3do.h"
#include "o3de/vertexlight.h"
#include "platform/options.h"
#include "platform/settings.h"
#include "script/gamestate.h"
#include "script/inventory.h"
#include "script/props.h"
#include "o3de/raster.h"
#include "o3de/render.h"
#include "o3de/renderer.h"
#include "platform/boot.h"
#include "platform/movie.h"
#include "platform/datafs.h"
#include "platform/frontend.h"
#include "ui/overlay.h"
#include "ui/hudbar.h"
#include "ui/iamtext.h"
#include "ui/options.h"
#include "ui/radar.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <set>
#include <string>

namespace omk::play {

// omk-play 69: THE TAKE CAMERA. `MDGETOBJ` (0x0046B380, no proc label -
// read from the raw listing) fills the request block with the player as
// both subjects, a 30-frame travel (`dword_930818 = 30.0`) and calls
// `Camera_Request(1)`; mode 1 loads preset 1 of the table at 0x004C20C8
// (tables/camera_presets.json, `verify.py: camera presets`). `MDPUTSNK`
// (0x0046B4B0) and `MDLETOBJ` (0x0046B460) call mode 16 - the swap back
// with nothing loaded, 30 frames too - gated on `C+12 == 1`, the live
// block being the mode-1 camera. `MDNOTAKE` swaps nothing: a cancel goes
// through the put-back to MDLETOBJ. So the side view holds from the
// hand-over until the object is banked or set down, on either path.
// THE `.CTL` EFFECT SPRITES (omk-play 69, point 3). `Cef_UpdateStateEffects`
// (0x0045B260) spawns every record of a state on ENTRY (or when the clock
// wraps) unless the record's flag bit 2 marks it a per-frame emitter;
// `Cef_SpawnEffect` (0x0045B3B0) takes one sprite instance from the scene
// registry by the record's +20 id, places it at `Actor_AttachPoint(code)`,
// scale +28, mode 4 (ADDITIVE); `Cef_TickEffects` (0x0045ADF0) keeps it
// alive while `from <= clock <= from + duration` (and `<= to` when +8 is
// set), sets its frame to `(clock - from) / duration * frames`, and with
// flag 1 moves it to the bone every tick. The confirm's H_GETOBJ carries
// two - sprites 127 and 130 on the LEFT HAND (attach 10 -> actor+44,
// "Maing"), which is the "particle effect on the arm" a reader saw.
// Attach codes map through Actor_AttachPoint's switch onto the loader's
// bone table (04_sys.c 5497..5513), by NAME below.
struct CtlSpriteInst { int sprite = 0; float duration = 0, from = 0, to = 0, scale = 1;
                       std::uint8_t flags = 0, attach = 0; int state = -1; };

// the `--hold` stream, parsed into (keys, frames) runs
struct HoldRun { std::vector<int> keys; int frames = 0; };

// ---- EVERY BODY THE SESSION SAYS IS ON SCREEN (issue 41) -----------
//
// `Actor_Attach` (0x0041CCA0) puts an actor in the o3de tree and
// `Render_Scene` draws them ALL. This file used to keep exactly one
// `speaker*` set - the model of `session.shown().front()`, posed by the
// LAST running `"actor"` program whichever actor that program drives - so
// the Impasse drew one passer-by performing Kay'l's arrival. What follows
// is one `Staged` per shown actor, its pose resolved per actor, in the
// engine's own precedence:
//
//   (a) a RUNNING scene program whose `Started.actor` is this actor -
//       `ScriptObject_StartOnActor` (0x0041BA80) drives ITS actor - and
//       then its clip on its authored `.3DP` path (`Path_Sample`);
//   (b) else, this actor being the conversation's speaker, the line's own
//       `.3DM` and its face, exactly as before;
//   (c) else the IDLE. `Actor_LoadModel` -> `Actor_LoadBankList` leaves
//       the channel on the default group's default entry
//       (`Cef_DefaultGroup` = the group whose flags bit 0 is set,
//       `Cef_DefaultEntry` = its 0x20 entry), and that entry's clip is
//       what he stands in. This takes FRAME 0 of it and does not tick a
//       channel per actor - a still idle, labelled, not a claim.
//
// The model and the bank are shared by NAME - three passers-by are one
// `PA1_FN` - because the pool is only 64 slots wide (a bucket key's low
// six bits, ASSETS 4b) and because the engine's own texture cache matches
// on the name alone.
struct CharModel {
    omk::Geometry rest;
    std::vector<omk::Mesh> meshes;
    std::vector<omk::Texture> tex;
    omk::FaceMesh face;
    // Built once per model name, and the model is loaded once and shared
    // (`o3de/shadow.h`; todo/pending/vita-meshidx-playcpp.md, applied
    // 2026-09-22). It stores mesh INDICES, so it must be rebuilt if
    // `meshes` is ever replaced; here it never is.
    omk::MeshNameIndex boneIdx;
    // The HIERARCHY ROOT - the pelvis in all 181 character models, the
    // mesh whose parent id resolves to nothing. `composePose` leaves a
    // root at its AUTHORED position, and the models are not authored
    // about the origin: `UBassin` is at (2.9, -2.4, 17.9) but `D1Bassin`
    // is at (507.1, -168.1, 39.2), so a body placed by its model origin
    // is 507 units up the alley. The placement names the PELVIS, so the
    // pelvis is what is moved onto it.
    int root = -1;
    std::size_t texBase = 0;      // its first slot in the pool
    bool ready = false;
    // LOOKUPS OF THE MODEL ALONE, filled on first use and kept: they were
    // walked per body per frame - the head's O(n^2) root search and name
    // strings twice, the 76x76 roots count, the skeleton walk - where
    // `Actor_LoadModel` (0x0041A730) binds its bones ONCE into the actor's
    // slots (`optimization.md` step 28, row l).
    mutable int headMesh = -2;                                  // -2: not yet
    mutable int severalSkeletons = -1;                          // -1: not yet
    mutable std::unordered_map<std::int32_t, int> skelRootByFirstId;
    int headOf() const {
        if (headMesh == -2) headMesh = omk::headMeshOf(meshes);
        return headMesh;
    }
    // THE LOD CHAIN (`optimization.md` step 28 k). A crowd model holds
    // four skeletons; `sub_453A70` sorts them by size, largest first, and
    // `sub_453910` chains them at 10/20/30/40 m. `sub_48D7F0` binds the
    // chosen one's tracks at `dword_6A50A4 = level * count` - a node takes
    // the track keyed by its own index LESS that - so level k's meshes must
    // sit exactly `k * count` after level 0's; a model that does not is
    // left at one skeleton (`lodLevels == 0`), as before.
    mutable int lodLevels = -1;                  // -1: not yet
    mutable int lodCount = 0;                    // meshes in one skeleton
    mutable int lodRootAt[4] = {-1, -1, -1, -1};
    mutable std::vector<std::uint8_t> lodMask[4];
};

struct CharBank {
    omk::CtlFile ctl;
    std::vector<std::byte> data;
    int  idleClip = -1;           // the default group's default entry's
    bool ready = false;
};

// THE WORLD'S PROPS. One model per OBJECTS stem, from
// `MESHES/OBJETS/<stem>.3DO` - `Object_ModelPath` (0x0040BAF0) copies the
// record's `+14` and appends ".3DO". A prop is static: no pose, just the
// placement `Object_SetPlacement` gives its node, so the rest geometry is
// transformed into world space per frame and submitted like any batch.
struct PropModel {
    omk::Geometry rest;
    std::vector<omk::Texture> tex;
    std::size_t texBase = 0;
    // THE MODEL'S OWN ORIGIN. `buildGeometry` bakes each mesh's authored
    // `pos` into its corners, exactly as it does for a decor set, so a
    // model is NOT centred on nothing: `ANNEAU` is a 4.6-unit ring whose
    // corners run x 503.4..508.0, y -177.4..-173.1, z 13.2..15.0 about a
    // mesh position of (505.7, -175.3, 14.1). Adding the placement on top
    // of that put the rings ~500 units up the alley. The placement names
    // where the object's ROOT goes, so the root is what is moved onto it -
    // the same correction the scripted crates needed.
    float origin[3] = {0, 0, 0};
    float localOff[3] = {0, 0, 0};   // the root mesh's +128: where it sits under a parent
    bool ready = false;
};

struct Staged {
    int actor = -1;
    std::string model, bank;
    CharModel* mo = nullptr;
    CharBank*  bk = nullptr;
    omk::Geometry posed;
    float at[3] = {0, 0, 0};
    float facing = 0.0f;
    bool  placed = false;   // something authored says where he stands
    bool  pelvis = false;   // ...and it names his PELVIS, not his feet
    bool  seen = false;
    bool  drawn = false;
    int   sceneClipWas = -2;      // the program's clip, cached
    omk::NodeTracks sceneTracks;
    omk::NodeTracks idle;         // the bank's default clip, frame 0
    bool  idleBuilt = false;
    // ...and its POSE, composed once: frame 0 of a fixed clip on a fixed
    // model is the same every frame (todo/cpu-vs-original.md tier A)
    std::vector<omk::MeshPose> idlePose;
    const CharModel* idlePoseFor = nullptr;   // ...for this model (an evicted one's
    std::string idlePoseModel;                // address can be reused: the name too)
    float drawAt[3] = {0, 0, 0};   // where he was actually put, for a set piece
    bool  drawAtKnown = false;     // ...and whether a frame has put him yet
    std::vector<float> poseWas;    // OMK_BODYLOG: last frame's posed corners
    long  lastSkinned = -1;        // the frame `posed` was last written; -1 released
    // The yaw the body was last DRAWN with, kept so a held pose is held
    // whole: a scene clip's pose already carries the clip's root rotation,
    // so the world heading must not be applied over it a second time.
    bool  lastYawKnown = false;
    float lastBodyYaw = 0.0f, lastDrawnYaw = 0.0f;
    bool  lastAboutPelvis = true;
    // The placement a running scene PROGRAM gives him (a path sample or
    // the clip's root key 0), cached so it can be re-asserted every
    // frame - a `fromTable` body's per-frame reset to its 20-byte record
    // would otherwise win on every frame but the clip-change one, and he
    // would stand at the placement plus the whole root motion.
    bool  progPlaced = false;      // a program placed him this clip
    // ...and whether one EVER did. The two are not the same question:
    // "the program ended, so he stays where it left him" is only true of
    // a body a program actually moved. An actor no program ever named -
    // Gandhar's cave stages twelve of them, `shoot.actor.enter` bodies
    // driven by the shoot AI - has only his placement record, and
    // carrying `drawAt` over on his FIRST frame reads it before anything
    // wrote it and teleports him to the world origin.
    bool  progRan = false;         // a program placed him at some point
    bool  fightPlaced = false;     // a melee moved him: the fight's place STANDS after it
    float progYaw = 0.0f;          // the call's Euler y (`Actor_SetEuler(node, p4, p5, p6)` every tick)
    bool  progYawKnown = false;
    float progBase[3] = {0, 0, 0};
    bool  progPelvis = false;
    const omk::SceneRunner* poolWas = nullptr;   // which pool last drove him
    const char* src = "none";
    omk::HeadLook look;            // `character.look_at_player`'s head aim, eased
    bool  shootTold = false;
    bool  brainTold = false;
    // KILLED BY A BOLT (`sub_4240E0`): the death clip TYPE its band chose,
    // and the frame it began - the clip plays once and holds its last
    // frame. -1 while alive.
    int   deathType = -1;
    double deathStart = 0;             // on `gameClock`, not the presented `n`
    // ...and the death CLIP itself, whose root motion lays the body down
    const omk::PedClip* deathClip = nullptr;
    bool  deathFallTold = false;       // his fall has been logged
    // ...the fall as it has been APPLIED, one clip frame a tick through
    // the wall test (`sub_421770`), the last frame taken, and how many
    // ticks a wall held the slide back
    float deathMove[3] = {0, 0, 0};
    long  deathEl = 0;
    int   deathWalls = 0;
    // his CURRENT clip's root motion, `sub_421370`'s walk (a gunman's
    // brain clip; zero for everyone else), and the instruments over it
    float walkMove[3] = {0, 0, 0};
    int   walkWalls = 0, walkSlides = 0;
    // how far his walk actually carried him LAST tick: state 2 counts the
    // edge down by it (`ShootFrameIn::movedThisFrame`), and the walk runs
    // after the brain, so the brain reads the previous tick's
    float walkDist = 0.0f;
    bool  walkTold = false, walkWallTold = false;
    bool  deathFloorTold = false;      // ...and where it left his pelvis
    // message 3 posted for this death (`sub_424DE0`'s dead arm, once the
    // death clip has played out) - see the post below
    bool  deathPosted = false;
    // SEATED ONCE, the way the engine seats an actor once and then
    // `Actor_MoveBy`s him: the feet go on the floor when the pose SOURCE
    // or the clip changes, and the clip's root motion moves him from
    // there. Re-seating every frame would cancel every vertical the clip
    // has - a jump would slide along the ground instead of leaving it.
    float seatFeet = 0.0f;
    int   seatClip = -3;
    const char* seatSrc = "";
    bool  seatKnown = false;
    bool  placeTold = false, groundTold = false;
    // THE YAW THE BODY WAS DRAWN WITH - the node's world heading, which is
    // what subject kind 2 (`sub_4151E0`) reads back for a camera that
    // hangs off him: `atan2` of the node matrix's forward, +90.
    float drawnYaw = 0.0f;
    bool  drawnYawKnown = false;
    // WHERE THE HEAD WAS DRAWN. Subject kinds 1 and 3 anchor a camera on
    // the actor's `Tete` node (`actor+16`, cached by `Actor_LoadModel`),
    // not on the body - 176 of the 253 relative cameras in `IAM\DIALOG`
    // ask for one of the two. Recorded as the body is posed and read a
    // frame later by the camera block, which runs earlier in the frame.
    float headAt[3] = {0, 0, 0};
    // WHICH SKELETON was posed - a crowd model has four, side by side,
    // and a bone name matches in all of them (see `o3de/shadow.h`).
    int   shadowRoot = -1;
    // EVERY MESH'S WORLD POSITION, filled by the same transform that
    // places `headAt` - what the shadow needs, because
    // `Shadow_EmitBoneBlob` probes from the BONE NODE'S OWN ORIGIN
    // (`node+44/48/52`) rather than from anything in the drawn corners.
    // Three floats a mesh, empty when the body was not placed this frame.
    std::vector<float> meshAt;
    // ...and each mesh's world ROTATION beside it, nine floats a mesh,
    // column-major (the mesh's local X, Y, Z in the world) - what the
    // projectile sweep turns a mesh's sphere centre and box by
    // (`actor/shoothit.h`). The same frame `meshAt` is in.
    std::vector<float> meshRot;
    bool  headKnown = false;
    float pelvisDrawnAt[3] = {0, 0, 0};   // `pelvis + off`, last frame: the head look's inverse
    bool  pelvisDrawnKnown = false;
    // `Morph_Play` reads the node's world heading ONCE, when the line
    // starts, and `sub_42BE00` hands the morph that yaw for its whole
    // length - so the line's yaw is a LATCH, not a per-frame read.
    float lineYaw = 0.0f;
    bool  lineYawLatched = false;
    // ...and the heading a scene clip left the node with, kept after the
    // program ends: nothing resets the Euler at +416 or the last frame
    // `Anim_ApplyNodeFrame` wrote, so a body a program turned stays
    // turned - the facing half of the rule the position already obeys.
    float restYaw = 0.0f;
    bool  restYawKnown = false;
    // The line's .3DM with its root already turned by the clip's heading
    // - `Morph_Play`'s `heading`, composed the way `08_wave.c` does it -
    // so the fade blends two poses in ONE frame. Blending the raw morph
    // against a clip whose root carries the yaw, then adding the yaw on
    // top, double-turned the clip end and faded to single: she started
    // every line turned away and slowly came round to the lens.
    omk::NodeTracks lineTracks;
    float lineRootYaw = 0.0f;
    std::string lineVoice;         // whose line `lineTracks` was taken from
    // A body nothing drives keeps the pose it was last given. The engine
    // never resets a node - the last frame `Anim_ApplyNodeFrame` wrote
    // stays - so when a program and a line are both over and the model
    // has no bank, this is what stands there; the REST pose was drawn
    // instead, a T-pose in software and nothing at all in the Vulkan
    // window (a reader: *Telis appears normally at the beginning before
    // disappearing*).
    std::vector<omk::MeshPose> lastPose;
    // A MELEE LOSER the teardown left in ACTOR_STATE 0 (`sub_445AC0` writes
    // the opponent's +404 to 0, and state 0 is `nullsub_6` - no tick at
    // all), so his node keeps the last pose the fight gave it: on the
    // floor in his knock-out loop, not back in the bank's idle.
    bool inertAfterFight = false;
    // POSED BY THE RENDERER (todo/gpu-skinning.md step 4), as a walker is:
    // `restGeo` and one affine a mesh, `posed` untouched
    bool gpu = false;
    const omk::Geometry* restGeo = nullptr;
    std::vector<float> affine;
};

// THE PROCEDURAL PEDESTRIANS (docs/STREET_LIFE.md 2): one body per
// walker of `session.sliders()`, its model shared through
// `charModels`, posed from the crowd library's clip at the walker's own
// clock, stood with its feet on the walker's body point and turned to its
// heading. The engine draws a pedestrian through four LOD objects out to
// `kLodDistances[3]` (40 m) and nothing beyond; this draws the full model
// inside that distance and nothing beyond, so a street at density 3 in
// Anekbah is 200 walkers of which a camera sees a few dozen.
// ONE WALKER'S SHARE OF THE BODY PASS (todo/vita-port.md P4). The serial
// half resolves the caches and fills these; the body pass reads them and
// writes only that walker's own `PedStaged`; the merge afterwards takes
// the counts and times in index order, so a threaded frame is a serial
// one's frame exactly.
struct PedJob {
    std::size_t i = 0;
    const omk::Geometry* rest = nullptr;
    const omk::NodeTracks* tracks = nullptr;   // this level's
    const std::uint8_t* only = nullptr;        // its skeleton's meshes, or all
    int level = 0;
    int foot[2] = {-1, -1};
    int lodRoot = 0;
    float frame = 0.0f;   // its fraction is enhancement 12's; floored otherwise
    int lit = 0;
    float footOff = 0.0f;
    double tCompose = 0, tApply = 0, tPlace = 0, tLight = 0;
};

struct PedStaged {
    CharModel* mo = nullptr;
    // A crowd model carries FOUR skeletons - `PhBassin`, `PiBassin`, ...,
    // the LOD sub-objects `sub_453A70` splits it into (76 meshes in
    // PSH_FN, 19 a skeleton) - and the library's tracks name the first.
    // Drawing the whole model posed the first and left the other three at
    // rest, a T-pose inside every walker; so the rest geometry is cut to
    // the meshes under the root the tracks name, once per model.
    std::map<int, omk::Geometry>* lodRest = nullptr;
    omk::Geometry posed;
    const omk::NodeTracks* tracks = nullptr;
    const omk::PedClip* clipWas = nullptr;
    float feet = 0.0f;               // this frame's level's
    float feetLevel0 = 0.0f;         // level 0's, once `feetKnown`
    bool  feetKnown = false;
    bool  drawn = false;
    // `Piedg` and `Piedd` in world space - `Slider_PlaceShadow`'s two
    // nodes, bound into the pedestrian record at spawn by
    // `sub_41E210(aPiedg, model, &ped[16])`.
    float footAt[2][3] = {};
    bool  footKnown = false;
    // POSED BY THE RENDERER (todo/gpu-skinning.md step 3): the body is
    // drawn from `restGeo` - the model's rest, cut to its skeleton and
    // shared by every walker of it - with one affine a mesh and the
    // lights that reach it, and `posed` is not touched
    bool  gpu = false;
    const omk::Geometry* restGeo = nullptr;
    std::vector<float> affine;
    std::vector<float> lights;
    int   lightCount = 0;
    bool  lightsBlack = false;
    // THE WALKER'S OWN POSE BUFFER, kept across frames: `composePose`
    // fills it in place (todo/optimization.md step 18's leftovers) where
    // it built a fresh vector for every body every frame.
    std::vector<omk::MeshPose> pose;
    long lastDrawn = -1;         // the last frame this slot was drawn (its buffers' release)
    // WHAT A WALKER'S MODEL AND CLIP DECIDE, cached (step 19): the
    // skeleton root its tracks name, the rest geometry cut to it, and the
    // two foot bones' mesh indices under that root - each recomputed every
    // frame before, by a mesh walk, two map lookups (one by a string) and
    // two name searches. Valid while the model, its tracks, the model's
    // NAME and `pedCacheGen` are what they were when it was filled.
    long cacheGen = -1;
    const CharModel* cacheMo = nullptr;
    const omk::NodeTracks* cacheTracks = nullptr;
    std::string cacheModel;
    int cacheRoot = -1;
    const omk::Geometry* cacheRest = nullptr;
    int cacheFoot[2] = {-1, -1};
    // ...per LOD level, filled as a level is first used (level 0 is the
    // three above); `lodFilled` is cleared with the cache
    std::uint8_t lodFilled = 0;
    const omk::Geometry* lodRestL[4] = {nullptr, nullptr, nullptr, nullptr};
    int lodFoot[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
    // the rest pose's feet height, per level: each skeleton is its own
    bool  lodFeetKnown[4] = {false, false, false, false};
    float lodFeet[4] = {0, 0, 0, 0};
};

// THE ROAD TRAFFIC (docs/STREET_LIFE.md 2b, actor/vehicles.cpp). A vehicle
// is far simpler to stage than a walker: it has no clip and no skeleton -
// `sub_456C70` moves a POINT and `sub_437F80` puts the instance on it - so
// the geometry is composed once at rest and only transformed per frame.
struct VehStaged {
    CharModel* mo = nullptr;
    // the chosen sub-object, composed, in model space - SHARED by every
    // vehicle of this model and sub-object (`PlayState::vehAtRestFor`), as
    // the original holds a model's LOD sub-objects once (`sub_453A70`)
    const omk::Geometry* atRest = nullptr;
    omk::Geometry posed;       // ...that, placed in the world this frame
    int   lodRoot = -1;
    float origin[3] = {0, 0, 0};
    float radius = 0.0f;       // about `origin`, over `atRest`'s corners - the view cull's
    bool  built = false;
    bool  drawn = false;
    // PLACED BY THE RENDERER where it poses bodies: `atRest` as it is, one
    // affine a mesh (all the same - a vehicle is rigid) - the original's
    // node matrix (`o3de_SetNodePos` and the facing) instead of every
    // corner rewritten and re-sent each frame (2026-09-30)
    bool  gpu = false;
    long  lastDrawn = -1;        // the last frame this slot was drawn (its buffers' release)
    std::vector<float> affine;
};

// ...and the clip his brain PICKED and is playing (the record's `+8` and
// `+184`, the actor's frame `+188`): its type, length, the frame, and the
// turn it makes each frame. Type -1: none.
struct GunClip { int type = -1; int frames = 0; float frame = 0.0f, turn = 0.0f; };

// ...and his CURRENT clip, the one his action asked for (the record's `+8`,
// `Shoot_ActorAction` -> `sub_421A20`), with the actor's frame `+188` that
// `sub_421370` advances each tick no picked clip plays. Until 2026-09-11
// this port held frame 0, and a reader saw every robber standing frozen
// between his entrance and his death.
struct GunAnim { const omk::PedClip* clip = nullptr; float frame = 1.0f; };

// ...and his AIM: `dword_6A4720` / `dword_6A4724`, the angles `sub_47C2A0`'s
// target arm hands `sub_434C30` - and so `sub_471950` - on every tick it
// runs. `live` is "the gate ran him this tick": a tick it does not run (a
// turn clip holding the brain, a state that fires nothing) bends nothing.
struct GunAim { bool live = false; float yaw = 0.0f, pitch = 0.0f; };

// A SHOT EFFECT'S SOUND (`todo/shoot-mode.md` 8.2). `Sfx_RegisterEmitter`
// arms the effect's `+36` countdown when its caller's last argument is 0 -
// the entry's creation and the impact; not the wind-up's per-frame muzzle
// calls, not the hit effect 93 - and `Sfx_TickAmbient` plays the effect's
// sound the tick that countdown goes negative, which with `+36` = 0.0 in
// all 42 of shoot2.sfx's rows is the same frame: `Scene_FindSoundIndex`
// in the resident library, then `Sound_Play3D` at the emitter with the
// distances 78 and 584 (inches). What DirectSound does between them is the
// device's and has no reachable tier (PORTING B5): this takes its default
// inverse-distance rolloff - full inside 78, 78/d out to 584, held there -
// measured from the player. The sprite half of the effects is not drawn.
// THE CONVERTED SOUNDS, KEPT (2026-09-27). Every play of an effect or
// scene sound decoded its WAV and resampled it to the device's rate again
// - a footstep every step, and a console's city frame showed `sounds`
// sections of 66 ms. Kept by the bytes' address and size: the global,
// fight and shoot libraries stay resident for the run, and the scene's -
// the one library that changes - clears the cache when the resident scene
// does (`sfxSceneWas`, checked each frame in the sounds pass), so an
// address reused by another scene's sound never answers from the cache.
//
// ...AND SHARED WITH THE MIXER (2026-09-30): a play hands over the cache's
// own sample (`Frontend::playSound`'s shared form), as `Sound_Play3D`
// duplicates the bank's buffer rather than its memory. A sample still
// sounding when the scene's cache is cleared lives until its voice ends.
struct SfxSample {
    // kept as the file holds it, read at the device rate as it plays
    // (`omk::DeviceSound`; todo/ram-vs-original.md tier B) - never null
    std::shared_ptr<const omk::DeviceSound> pcm;
    float peak = 0.0f;
};

struct GunFacts {
    bool ok = false;
    std::string root;             // `o3de_FindNodeByName(model, 0)`'s name
    int   tirMesh = -1;           // its index, for the geometry's cornerMesh
    float tirLocal[3] = {0, 0, 0};   // +128, relative to what it hangs from
    float tirPos[3] = {0, 0, 0};     // +?? absolute - the origin of its corners
};

// ---- MELEE, `todo/fight-mode.md` step 2 -----------------------------
//
// `fight.begin` (op 62) parks its script at status 3 and hands the
// opponent and the AI LEVEL here; `Game_HandleEvent` case 2 releases it
// when the fight ends. What this does is `Fight_Engage`'s (0x0041A3B0)
// own list: both bodies onto `.CTL` slot 2 - the `*CMBT` bank, through
// `Actor_LoadBankList` rather than a rebuilt controller - ACTOR_STATE 2
// on both, the two combat contexts `Fight_Begin` builds, and control
// scheme 3. The runtime itself is `actor/fight.cpp`.
//
// It is installed HERE rather than beside the move hook because it needs
// `charBankFor`, which is defined just above.
struct FightRun {
    bool  active = false;
    bool  harnessFired = false;    // `--fight` stands in for op 62, once
    int   opponent = -1;
    Staged* body = nullptr;            // the opponent's staged body
    omk::FightBody player, foe;
    std::unique_ptr<omk::CefChannel> foeChannel;
    std::unique_ptr<omk::Fight> fight;
    // The opponent's MOTION PASS. He has no `PlayerController`, so the
    // half of `Actor_ApplyMotion` that carries a clip's root motion into
    // the world is applied from here: the clip's own accumulated root
    // track (`omk::clipRootMotion`), sampled between last tick's frame and
    // this one's and turned by his facing.
    CharBank* foeBank = nullptr;
    int   foeClip = -1;                              // whose root is cached
    std::vector<std::array<float, 3>> foeRoot;       // per frame, accumulated
    float foeFrame = 1.0f;                           // last frame sampled
    // ...and his POSE: the same clip's full tracks, so the staged body is
    // drawn from the fight channel rather than from the one-frame idle
    // `idleTracksFor` builds. Cached per clip like the root motion,
    // because `clipTracks` decodes every rotation in the clip.
    omk::NodeTracks foePose;
    // ...and the pelvis height his OPENING stance's pose puts it at - the
    // reference the clip's pelvis track is read against for the DRAW
    // (see the body placement in the fight step). -1e30 until known.
    float foeRootRef = -1e30f;
    // ...and his COLLISION (`todo/fight-mode.md` 15.8a). The engine's
    // `Actor_TickPlayerAndOpponent` ends BOTH fighters with
    // `Actor_ApplyMotion` (0x004672D0): the frame's velocity is applied,
    // remembered, undone, and handed to `Actor_Move` - a horizontal
    // collide-and-slide, then the ground probe. This is the player's own
    // walker over the same soups, seated on the opponent's FEET: his
    // `y` is the pelvis, `foeLift` above them (Y down: feet = y + lift).
    std::unique_ptr<omk::Walker> foeWalker;
    float foeLift = 0.0f;
    long  foeBlocked = 0, foeSlid = 0, foeSteps = 0;
    bool  camRaySet = false;           // the camera ray, installed on the first tick
    // `Hud_Refresh`, which `Fight_Begin` calls: served at the next HUD
    // draw, where the framebuffer's width is known. The player's six
    // properties it snapshots, in the card's row order.
    bool  hudRefresh = false;
    int   koSeen = 0;                  // the KO counter as the fade last saw it
    int   cardProps[6] = {0, 0, 0, 0, 0, 0};
    double ms = 0.0;                   // the AI's `Sys_GetTimeMs` clock
    long  startedAt = 0;
};

// ---- THE EFFECT SPRITES -------------------------------------------
//
// A section C effect names its sprite by an index into the GLOBAL library
// `aventure.scx` registers - the 20 the boot already reports. GRID's
// effects use 9..12, which land on EFFECTS2_SMOKE1, EFFECTS1_IMPACT1,
// EFFECTS1_IMPACT2 and EFFECTS1_M16D; the scene's own chunk 4 re-registers
// copies of the same files, so the global one is what the index means.
//
// Each sprite is a whole `.3DO` in the stream immediately followed by its
// `.3DT`, so the pair is read straight out of the same buffer.
// Keyed by the sprite's ID, because that is what an effect names -
// `sub_4A5800(scene + 8, id)` walks the scene's 36-byte registry rows
// matching `+32`. Indexing a library instead lands on the wrong sprite:
// for GRID it swaps IMPACT1 and IMPACT2 and turns `burn`'s smoke into a
// muzzle flash, which is why the portal came out fire-orange.
//
// The GLOBAL library loads first and the SCENE's over it, since the tick
// resolves through the scene when there is one.
// THE SPRITE TABLES, SPARSE (todo/optimization.md step 6). Indexed by sprite
// ID, which is scene-local and runs to 49591 in Anekbah, so the two vectors
// this replaces were ~50000 slots with a few dozen in use - a snapshot found
// them as one 6.4 MB and one 5.5 MB allocation of empty placeholders. An id
// with nothing registered answers exactly as an empty slot did: `texOf` /
// `framesOf` give null, and `idCount` is the old vectors' size - the highest
// id seen plus one, grown even for a record whose data was bad, because the
// resize ran before that test - so `empty()` and the log's "0..N" read the
// same as before.
struct SpriteTable {
    std::unordered_map<int, omk::Texture> tex;
    std::unordered_map<int, omk::SpriteFrames> frames;
    std::size_t idCount = 0;
    const omk::Texture* texOf(int id) const {
        const auto it = tex.find(id);
        return it == tex.end() ? nullptr : &it->second;
    }
    // null for an id with no frames too - the old `>= size || frames.empty()`
    const omk::SpriteFrames* framesOf(int id) const {
        const auto it = frames.find(id);
        return it == frames.end() || it->second.frames.empty() ? nullptr : &it->second;
    }
    bool empty() const { return idCount == 0; }
    void clear() { tex.clear(); frames.clear(); idCount = 0; }
};

// THE SHOWN DECORS - one per resident slot, because TWO are drawn while
// the player walks between areas. `Area_Transition`'s completion arm puts
// the destination in state 2 and nothing hides the origin until
// `area.arrive` (area.h, `Session::slotShown`); until 2026-09-03 this
// file drew `session.setName()` alone, so the frame after the airlock
// transition was black: the airlock set at x 7415..8149 through a follow
// camera on a player still standing in the alley at x 6705. Keyed by
// SLOT at stable addresses, since the Vulkan backend caches a vertex
// buffer by pointer and revision.
struct WorldSlot {
    std::string stem;
    int area = -1;
    // DRAWN, which is not the same as LOADED. `sub_419AF0` / `sub_419A90`
    // (show / hide) only link the set into and out of the RENDER list
    // (`sub_441170` / `sub_441200`); the COLLISION array is joined when the
    // set finishes loading (`sub_443300` in `sub_4195C0`) and left only on
    // unload or eviction (`sub_443320`). So a hidden set is still SOLID.
    bool shown = false;
    // the `worldGen` its depth tie was last baked for (`bakeDepthTie`)
    unsigned tieBakedGen = ~0u;
    omk::Geometry geo;
    std::vector<omk::Texture> tex;
    omk::MirrorPlane mirror;
    omk::TriangleSoup soup;      // its WALKABLE soup: the feet's decor probe
    // and the faces PAST the slope limit: `Walk_GroundResponse` stands the
    // actor on a steep face and slides him off it, so dropping them made a
    // slope a hole with no floor in any direction (omk-play 67)
    omk::TriangleSoup steep;
    // The set's own meshes, kept so a scripted motion can name one, and
    // the corners as BUILT, so a motion patches the original rather than
    // accumulating on the last frame's patch.
    std::vector<omk::Mesh>   meshes;
    // THE SET'S LIGHTS (`todo/mesh-lights.md`). A decor `.3DO` supplies
    // them and the street's moving population receives them - the static
    // set is shaded by a colour baked into every vertex and needs none.
    std::vector<omk::Light3do> lights;
    // its `0x40000000` meshes, the candidates `Sfx_BindAmbientEffects`
    // matches against the resident `.sfx` - kept, because that file can
    // arrive after the set does (the frame loop binds them)
    std::vector<omk::SceneRunner::SetEmitterMesh> emitters;
    // A MOVING MESH'S REST, ITS OWN (todo/ram-vs-original.md, tier A): the
    // x y z of its corners and the 9 floats of its walkable and steep
    // triangles AS BUILT, copied the first time it moves (until then its
    // corners are untouched - only its own patch writes them), in
    // `cornersOfMesh` / `soupTrisOfMesh` / `steepTrisOfMesh` order. A patch
    // writes the rest back and places it IN PLACE (`placePoints` allows the
    // same array), so the bits are those of placing from a whole copy. These
    // were whole-set copies - 6.7 MB of corners and 1.6 of soups in Anekbah
    // to re-place some 30 meshes; the original copies nothing (the probe and
    // the sweep read the node's matrix, `Walk_ProbeGround`, `Sweep_MeshTest`).
    std::unordered_map<int, std::vector<float>> restXyzOfMesh, restSoupOfMesh, restSteepOfMesh;
    // A MOVING MESH DRAWN AS THE ENGINE DRAWS IT (todo/optimization.md step
    // 37). `o3de_SetNodePos` (0x004370A0) writes a node's three floats and
    // `sub_494650` builds its matrix; the vertices go through it on the way
    // to the card, and nothing is rewritten. On a renderer that poses
    // bodies, a set mesh the scene moves is taken out of the set's draw the
    // first time it moves and drawn from a copy of its own corners - relative
    // to its origin, so a pure translation lands bit for bit where the CPU
    // patch put it - with one affine. Its collision triangles are still
    // patched (`soup` / `steep`), and the set's buffer keeps it at rest.
    struct MovingMesh {
        omk::Geometry rest;          // the mesh's corners minus its origin, cornerMesh 0
        float affine[12] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0};
        float at[3] = {0, 0, 0};     // where its origin is now
        float reach = 0.0f;          // its radius times its largest scale
    };
    std::map<int, MovingMesh> moving;
    // every mesh whose collision triangles a motion has re-placed, in the
    // order they first moved - the moving layer's parts (`SplitSoupGrid::parts`)
    std::vector<int> soupMovers;
    // THE VISIBLE-SET WALK's unit of work. `sub_48D3B0` walks the scene
    // MESH BY MESH and submits each one that passes; this port flattens a
    // set into batches by material, so a mesh's corners are runs inside
    // them. One run is a maximal stretch of consecutive corners in one
    // batch that all belong to one mesh - computed once at the load, so a
    // frame tests each mesh once (a few thousand) instead of each corner
    // (1.7 million).
    struct MeshRun { std::uint32_t start, count; std::int32_t mesh; std::size_t batch; };
    std::vector<MeshRun> runs;
    // ...and the collision soups the same way: the mesh each triangle
    // came from, and the soups as BUILT, so a moved crate's collision
    // follows the crate (a reader, 2026-09-04: "some crates fall at a
    // moment. It looks like their colliders stay at their initial
    // position" - the sweep made rest-baked collision visible).
    std::vector<int>  soupMesh, steepMesh;
    // THE PATCH'S INDEX (todo/optimization.md step 7). A set's moving meshes
    // are re-placed every frame - on the Anekbah street 30 of them move on
    // every one - and each re-placement scanned ALL of the set's render
    // corners (139245 in Anekbah) and both whole soups for its own. Built on
    // the first patch after a load (`w = WorldSlot{}` drops it): mesh name,
    // lower-cased, -> its FIRST index - a scene function names a set mesh in
    // whatever case, and the scan this replaced took the first mesh whose
    // name matched letter for letter ignoring case; and each
    // mesh's own corners and soup triangles in ascending order under the
    // bounds the scans used.
    bool patchIndexReady = false;
    // WHAT THE PATCH LAST WROTE, per mesh: s[3], pos[3], q[4], and the two
    // flags packed into the eleventh. A node's placement PERSISTS now
    // (`SceneRunner::placements()`), so the table is the same every frame
    // once a door has finished opening and re-placing it would rewrite the
    // same corners to the same values, bump `geo.revision`, and make the
    // backend re-upload - every frame, for ever. This says what is already
    // in the buffer, so only a placement that actually changed costs
    // anything. Dropped with the slot (`w = WorldSlot{}`), which is right:
    // a reloaded set is back at its authored corners.
    std::unordered_map<int, std::array<float, 11>> appliedPatch;
    std::unordered_map<std::string, int> meshByLowerName;
    std::vector<std::vector<std::uint32_t>> cornersOfMesh, soupTrisOfMesh, steepTrisOfMesh;

    // THE TWO WORLD RAYS (`actor/projectile.h`). Both walk the linked
    // set's meshes through `sub_444460`, which skips CollisionOnly
    // (0x800000) and gives a mesh with either bit of 0x41 no triangle
    // test: the bolts' `sub_444810` tests the rest (`shotSoup`), and the
    // engage's sight `sub_4449E0` also skips the 0x800 cutouts (its
    // context +444 set to 1) - the shot soup through `shotCutout`, a byte a
    // triangle, rather than a second soup (todo/ram-vs-original.md tier B).
    // Baked at rest: unlike the walker's soups above, a mesh a scene program
    // moves is not followed, which is this port's and labelled.
    omk::TriangleSoup shotSoup;
    std::vector<std::uint8_t> shotCutout;
};

// ------------------------------------------------------------- THE SKY
//
// `Area_TickLoad` case 4 hands the AREA chunk's `+133` to
// `Area_LoadMiscModel` (0x0041D2C0), which loads `MESHES\MISC\<name>.3DO`,
// takes its node 0, scales it 12.5x on all three axes and lifts it 2250
// units (Y is DOWN, so `-2250.0` is UP). Every frame, while options row 4
// is on, `sub_41CF10` sets the node to the CAMERA's x and z and its OWN y
// and relinks it under the scene root - so it follows you horizontally and
// never gets closer.
//
// It is not a dome. All 864 corners sit at Y = 401.09: a flat 12x12 quad
// grid, 5310 x 5164 units, carrying one 256x256 texture whose name in the
// file is `ciel2` - French for sky - over a mesh called `face`, or
// `TOITCIEL` in the roof set. So Omikron's sky is a painted CEILING, which
// is what a domed city has.
struct Sky {
    std::string stem;
    omk::Geometry geo;                 // the plane, rewritten when the camera moves
    std::vector<omk::Corner> base;     // ...from these, as loaded
    // ...or, on a renderer that poses bodies, MOVED AS THE ENGINE MOVES
    // IT: `sub_41CF10` writes the node's three floats and nothing else, so
    // the plane is a static geometry - scaled about node 0's origin once,
    // at load - and one translation a frame.
    omk::Geometry rest;
    std::vector<float> affine;         // 12 floats a mesh, all the same
    float placedAt[2] = {0, 0};        // the camera x/z `geo` was written for
    bool  placed = false;
    std::vector<omk::Texture> tex;
    std::size_t texBase = 0;
    float origin[3] = {0, 0, 0};       // node 0's own position
};

// One slot's decor in or out. The Geometry object stays where it is and
// its revision climbs, so the backend refills rather than mistakes it.
//
// A SET IS PREPARED OFF THE FRAME, AS THE ORIGINAL READS IT
// (todo/optimization.md step 31). `o3de_LoadScene` queues ONE read of the
// whole `.3DO` (`sub_41EC20`), `sub_41F320` serves it `ElementSize` bytes
// a frame from `Game_Tick` - 0x20000 after `Async_SetMode(1)` - and
// `Area_TickLoad` waits on `sub_41EFA0` while the game draws on. The
// Session has counted those frames since 2026-09-02 (`loadSlicesLeft`,
// Anekbah's 17), but this file read, built, decoded and sorted the whole
// set in the FIRST of them, so the port had the original's wait and the
// stall beside it. Now the work - everything below that is a function of
// the two files' bytes - runs on a thread of its own while the slices
// count down, and the set comes into the world on the frame they reach
// zero, which is one frame before `Area_TickLoad`'s cases 2..9 run.
//
// WHEN it comes in is the Session's count and never the thread's clock -
// the frame waits for the job if it must - so a run is the same run
// however fast the machine reads. A load the engine makes in mode 0 (the
// boot, a save) has no slices and is done where it is asked for, as before.
// `OMK_SYNC_SETS=1` does every load that way: the comparison.
struct SetLoad {
    int slot = 0;
    std::string stem;
    int area = -1;
    std::string path3do, path3dt;    // resolved on the frame's thread
    WorldSlot out;                   // what the job builds; nobody else's until it is done
    bool found = false;
    std::size_t bytes = 0;
    double ms[5] = {0, 0, 0, 0, 0};  // read, geometry, textures, soups, the rest
    long askedFrame = 0;
    int delayMs = 0;                 // `OMK_LOAD_DELAY_MS`
    int heldFrames = 0;              // frames the Session's load waited on the job
    std::unique_ptr<omk::BackgroundJob> job;
};

}  // namespace omk::play

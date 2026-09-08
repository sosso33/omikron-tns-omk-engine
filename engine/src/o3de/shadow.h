// SPDX-License-Identifier: GPL-3.0-or-later
// THE CHARACTER SHADOW - `Actor_DrawShadow` (0x00467E20) and
// `Shadow_EmitBoneBlob` (0x00467A00), option row 5 *Affichage des ombres*.
//
// The engine has no shadow pass and no projector. What it has is ONE shipped
// quad, `MESHES\MISC\shadows.3DO` - 804 bytes, four vertices, one quad, mesh
// flags 0x5000 (`0x1000|0x4000`, the MULTIPLY blend, which is `dst x (1-src)`)
// over a 31x30 patch of `SHOOT.BMP` holding a soft white disc on black.
// `sub_419060` loads it once at game start and scales its root by 0.07;
// `Shadow_CloneNode` (0x0041D120) clones it per actor and `Actor_LoadModel`
// stores the clone at `actor+80`.
//
// Every frame `Actors_TickAll` calls `Actor_DrawShadow(detail, actor)`, which
// emits blobs under a fixed set of BONES straight into the frame's vertex and
// triangle pools - the shadow is geometry built per frame, not a drawn node.
// How many bones is option row 7, the level of detail, and the player and the
// fight opponent get it whole while every other actor gets it MINUS ONE:
//
//   level 2   Brasd Brasg Avantd Avantg   divisor 14   reach 70.87 (1.8 m)
//   level 1+  Tete                        divisor 12   reach 70.87
//             Cuisseg Cuissed Jambeg Jambed            reach 55.12 (1.4 m)
//   level 0+  Buste                       divisor 10   reach 141.73 (3.6 m)
//
// and a level of 3 or more draws NOTHING - the switch has no default arm.
// The three reaches are round metres against the inch world unit.
//
// The blob itself, per bone: probe straight down, drop out if the floor is
// further than the reach, scale the quad by `0.07 * min(radius/divisor, 1.5)`
// where the radius is the bone mesh's own bounding sphere (`.3DO` mesh +88),
// lay it flat one unit above the floor, and shade every corner the SAME grey
// `255 - dist*255/reach`. White in contact, black at the limit - and under
// `dst x (1-src)` that is a solid shadow fading to nothing as the bone rises.
// So the fade is the vertex colour and nothing else; there is no alpha here
// (`Raster_DrawTriangles` never reads one for this bucket, ASSETS 4).
#pragma once

#include "formats/mesh3do.h"
#include "formats/tex3dt.h"
#include "o3de/collision.h"
#include "o3de/geom3do.h"
#include "platform/datafs.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

// `flt_4CB054` - the lift off the floor, and Y grows DOWN so it subtracts.
inline constexpr float kShadowLift = 1.0f;
// `SetObjectScaleX/Y/Z(node, 0.07, 8)` in sub_419060, applied to the whole
// subtree (flag 8), so every clone carries it.
inline constexpr float kShadowScale = 0.07f;
// `g_ShadowReach18` / `g_ShadowReach14`, and the chest's is twice the first.
inline constexpr float kShadowReach18 = 70.866142f;   // 1.8 m
inline constexpr float kShadowReach14 = 55.118111f;   // 1.4 m

// One bone `Actor_DrawShadow` emits under, in its own emission order.
struct ShadowBone {
    const char* bone;      // the mesh name `Actor_LoadModel` caches
    int   minLevel;        // the lowest detail level that reaches this call
    float divisor;         // `g_ShadowBoneDivisor` while the group runs
    float reach;           // the third argument of the call
};

// The ten calls, in the order the handler makes them at level 2. `minLevel`
// is which arm reaches them: the arms only at 2, the head and legs from 1,
// the chest always (the -1 and 0 cases jump straight to it).
inline constexpr ShadowBone kShadowBones[] = {
    {"Brasd",   2, 14.0f, kShadowReach18},
    {"Brasg",   2, 14.0f, kShadowReach18},
    {"Avantd",  2, 14.0f, kShadowReach18},
    {"Avantg",  2, 14.0f, kShadowReach18},
    {"Tete",    1, 12.0f, kShadowReach18},
    {"Cuisseg", 1, 12.0f, kShadowReach14},
    {"Cuissed", 1, 12.0f, kShadowReach14},
    {"Jambeg",  1, 12.0f, kShadowReach14},
    {"Jambed",  1, 12.0f, kShadowReach14},
    {"Buste",   0, 10.0f, 2.0f * kShadowReach18},
};
inline constexpr int kShadowBoneCount =
    static_cast<int>(sizeof kShadowBones / sizeof kShadowBones[0]);

// The shipped blob, read out of `MESHES\MISC\shadows.3DO`.
struct ShadowModel {
    bool         loaded = false;
    float        corner[4][3] = {};   // the quad's four vertices, in the order
                                      // the quad record indexes them
    std::uint8_t uv[4][2] = {};       // ...and the UV pair that goes with each
    float        r = 1, g = 1, b = 1; // the vertex colour the file carries
    std::vector<Texture> tex;         // SHOOT, its one material
    std::size_t  texBase = 0;         // where it lands in the frame's pool
};

// -> the model, `loaded` false when the file is missing. Never throws: a tree
// without the file must still run, it just casts nothing.
ShadowModel loadShadowModel(const DataFs& fs);

// One bone's blob, appended to `g.corners` as twelve corners - four triangles
// fanned to a centre vertex, which is what the engine emits and not the two
// triangles a quad would normally become.
//
// `bone` is the bone's WORLD position, `radius` its `.3DO` mesh +88, and
// `floorY` the surface under it (game Y grows down, so it is >= bone[1]).
// -> false when the floor is further than `reach`, which is the engine's own
// `if (dist <= reach)` and the reason a jumping character loses his shadow.
//
// The caller pushes ONE `Batch` over everything it appended: every blob a
// body casts goes through the same material and the same mesh flags, so one
// batch is the whole body's shadow.
bool shadowBlob(Geometry& g, const ShadowModel& m, const float bone[3],
                float radius, float divisor, float reach, float floorY);

// Which bones a detail level draws, as indices into `kShadowBones`. Empty for
// a level past 2, because `Actor_DrawShadow`'s switch has no default arm.
std::vector<int> shadowBonesFor(int detail);

// `o3de_FindMeshByName` (0x00436D90), and it is a SUBSTRING match on the
// LAST hit, not an equality: `sub_436D60` is `strstr(mesh + 16, wanted)` and
// the traverse overwrites its answer at every match. That is not a detail -
// every bone of every character model carries a `U` prefix (`UBuste`,
// `UTete`, `UPiedg`), so an equality test finds NOTHING and the whole
// mechanism silently draws nothing at all.
//
// **`underRoot` SCOPES it to one skeleton, and without that the shadows are
// orphans.** A crowd model carries FOUR LOD skeletons - `Ph…` `Pi…` `Pm…`
// `Pw…`, 19 meshes each - and they are authored SIDE BY SIDE: PSH_FN's four
// `Buste` sit at x -12.6, -91.2, -170.3 and -248.7, FSH_FN's spread 247. So
// every bone name matches four times and the engine's last-match rule lands
// on the LOWEST-detail skeleton, six metres from the body being drawn. The
// engine cannot meet this because `sub_453A70` splits the model into
// sub-objects and only one is live; this port holds the whole file's meshes
// in one array and poses the subtree the tracks name, so the search has to be
// scoped the same way. A reader saw the result first: blobs sliding across
// the street with nothing above them.
//
// `underRoot` is a mesh INDEX; -1 searches the whole model, which is right
// for a model with one skeleton (every `PERSOS` hero, HO1_FNM included).
// -> the mesh index, or -1.
int findMeshContaining(const std::vector<Mesh>& meshes, const char* wanted,
                       int underRoot = -1);

// THE STREET CROWD's shadow, and it is a different mechanism -
// `Slider_PlaceShadow` (0x00467F50). `Sliders_Tick`'s walkers get ONE whole
// node, placed at the MIDPOINT of the walker's two FEET (`Piedg` and `Piedd`,
// bound into the pedestrian record's +64/+68 by `sub_41E210` at spawn), at
// the probed floor minus 2, and turned to lie along the floor's NORMAL. It is
// then drawn like any other mesh, so unlike a bone blob it is full strength,
// carries no radius factor and does NOT fade as the walker's feet lift.
//
// Appends six corners - the quad as two triangles, which is what the ordinary
// mesh path makes of it. -> false when the model has no feet.
//
// **The normal alignment is this port's, and shaped rather than transcribed.**
// The engine solves two angles out of the probe's normal and builds a 3x3;
// what is reproduced here is the intent - the quad lies in the floor's plane -
// by the minimal rotation from straight down onto that normal. On the flat
// street a city crowd walks the two are the same thing.
bool shadowFootBlob(Geometry& g, const ShadowModel& m, const float left[3],
                    const float right[3], float floorY, const float normal[3]);

// `Slider_PlaceShadow`'s own lift, and it is not the bone blob's.
inline constexpr float kShadowFootLift = 2.0f;

// ------------------------------------------------- FITTED (an ENHANCEMENT)
//
// `todo/enhancements.md` row 5, and OFF unless `shadowquality` says otherwise.
// The engine lays each blob as a FLAT quad at the height probed under the
// bone's own centre, so on a slope, a kerb or a stair it clips through the
// ground or floats over it. Fitted subdivides the quad and lays every vertex
// on the surface under IT.
//
// **This one is not backend-gated, and that is a departure from the file's
// "only the Vulkan backend draws them" line.** The change is to the geometry
// the port generates, not to how a backend rasterises it, and making the two
// backends build different geometry would destroy the property the whole
// renderer boundary rests on - that they draw the same picture from the same
// decisions, which `mirror pass` and `engine silhouette` measure at 0.995 and
// 0.998 agreement. Off by default, so the software reference still draws what
// the original drew, which is what the rule is protecting.
//
// The grid is 4x4 cells. A cell is then a quarter of the blob, 5-8 units,
// which resolves the 30 cm (11.8 unit) step the walker's own limit allows.
inline constexpr int kShadowFitCells = 4;

// A vertex whose probed surface is further than this fraction of the blob's
// half-width from the CENTRE's takes the centre's height instead. LABELLED as
// this port's: it keeps a blob overhanging a ledge from stretching down the
// drop, and 1.0 means slopes up to 45 degrees are followed exactly.
inline constexpr float kShadowFitDrop = 1.0f;

// One bone's blob, laid on the surface. `local` is the triangles under the
// body, gathered once by `soupInBox` - probing the whole set per vertex would
// rescan the city thousands of times a frame.
//
// -> false for the same reasons `shadowBlob` does. On perfectly flat ground
// this draws the same shape as `shadowBlob` in more triangles, which is what
// `verify.py: engine: fitted shadows` asserts: identical on the flat, and
// different only where the ground is not.
bool shadowBlobFitted(Geometry& g, const ShadowModel& m, const float bone[3],
                      float radius, float divisor, float reach, float floorY,
                      const TriangleSoup& local);

}  // namespace omk

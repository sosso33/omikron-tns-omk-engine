// SPDX-License-Identifier: GPL-3.0-or-later
// THE SOFTWARE 3D RASTERIZER - and it is NOT a port.
//
// `docs/PORTING.md` B6 says so in the row this belongs to, and the distinction
// is load-bearing. Every other output slice in this tree transcribes something
// the engine does: `Blt`, the mode-2 Bresenham, the box fill, `Text_DrawRun`.
// **The engine has no software 3D rasterizer.** Exactly three drawing
// functions test the software driver mode and all three are I2D's - the 3D
// path is Direct3D in every mode, so there is nothing here to transcribe.
//
// This is a REFERENCE IMPLEMENTATION standing where D3D stood, the same status
// as the mixer's `render()` and `movie.cpp`. What that licenses, and what it
// does not:
//
//   * the DECISIONS it consumes are ported and checked - the drawable mask,
//     the 14-bit bucket key, the batch order (opaque, additive, multiply, by
//     material within each), the two blend modes and the cutout, the 58-slot
//     texture cache, `cullMesh`'s frustum;
//   * the GEOMETRY it computes - where a vertex lands - is checkable, and is
//     differenced against `tools/camshot.py`, which projects the same records
//     independently and has been laid over a real screenshot;
//   * the PIXELS are nobody's to claim. `PORTING` B5: a captured 3D frame is
//     exact about geometry and ordering and NOT about a pixel's low bits,
//     because filtering, dithering and fog are the driver's and Wine's are not
//     a Voodoo's. So per-pixel equality is not the criterion and must not be
//     claimed here or anywhere downstream.
//
// TIER (`PORTING` B2, declared here, in `verify.py: engine silhouette` and in
// B6's row): **4, for ONE camera, and geometry and ordering only.** The set
// drawn through dialog 402's camera 4555 is measured against the engine's own
// framebuffer by SILHOUETTE - directed edge alignment, density-normalised,
// quoted against its own chance floor - and by COVERAGE, the holes a set-only
// render leaves falling on pixels the capture draws black. 0.73/0.83 against
// the two parked frames on a floor of 0.27/0.30; 92%/99% of the holes dark
// against 33% frame-wide.
//
// What that does NOT license, and each of these has been measured rather than
// guessed:
//
//   * **one camera in one set is not a claim about the renderer.** A second
//     camera is a second claim and needs a second capture.
//   * this draws no characters and no props. The metric is directed so that
//     it can be right about the set while a third of the picture is missing -
//     which means it CANNOT see that they are missing. A render that dropped
//     every character would score identically.
//   * the drawable mask is not under test through this camera: swapping the
//     engine's rule for the viewer's changes 0 pixels here, because both
//     select the identical 10257 corners of Aapkayl.
//   * no pixel VALUE is checked - not the filter, not the dither, not the fog,
//     not the blend arithmetic. Those have no reachable tier from this rig,
//     the same way the audio attenuation law has none.
#pragma once

#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "ui/surface.h"

#include <cstdint>
#include <span>
#include <vector>

namespace omk {

// The camera, in the game's own space. `tools/camshot.py`'s `projector` is the
// reference for every line of this: forward = normalize(at - eye), world up is
// **(0, -1, 0)** because the game's Y points DOWN, right = f x up, up = r x f.
//
// `mirror` negates the right vector - the reading that was wrong for months,
// kept because a convention that is wrong everywhere at once looks right from
// inside, and the only way to pin it is to show the wrong one differs.
struct RCamera {
    float eye[3]{}, at[3]{};
    float hfovDeg = 60.0f;      // angle[1] is the HORIZONTAL fov, not vertical
    // ROLL, in degrees - the rotation about the forward axis. A camera record
    // carries one (`Cam_PlayEditing` keyframes position/target/roll/fov; a
    // world camera's is `Global_Load`'s 4096ths converted by 360/4096) and
    // dropping it is invisible in any single still frame, which is how it
    // survived here until 2026-09-03 while the web viewer had been fixed.
    //
    // The SENSE is the engine's own, derived rather than chosen: `sub_442400`
    // turns a direction and a roll into `sub_441FF0(pitch, yaw, roll)`, which
    // `o3de/setpiece.cpp`'s `headingMatrix` already transcribes verbatim, and
    // that matrix's COLUMNS are exactly this basis - column 0 is `s`, column 1
    // is `-u` (the sign is the game's Y-down), column 2 is `f`, agreeing to
    // 1e-3 at roll 0 for every direction tried. Rolling it 30 degrees carries
    // `s` from +X toward +Y about a +Z forward, which is
    // `v' = v cos(roll) + (f x v) sin(roll)`.
    float rollDeg = 0.0f;
    int   w = 640, h = 480;
    bool  mirror = false;
    // `noClip` restores the pre-2026-09-01 rule: DROP a triangle when any
    // vertex is behind the near cut, instead of clipping it. Kept for the same
    // reason `mirror` is - a wrong rule that is wrong everywhere at once looks
    // right from inside, and the only way to pin the right one is to render
    // both and show they differ. It was a real bug, reported by a player
    // flying the viewer: a floor or a wall with one corner behind the camera
    // vanished whole while plainly still in shot.
    bool  noClip = false;
    // A TRUE screen-X flip, and it is NOT `mirror`. `mirror` negates the right
    // vector BEFORE `u = s x f` is taken, so `u` flips with it and the result
    // is a 180-degree ROTATION of the image - which is the correct model of the
    // reflected-reading bug it exists for, and the wrong operation for a
    // mirror. `Raster_DrawTriangles` computes `v143 - x` while the mirror pass
    // flag is set and leaves y alone, so the mirror pass wants this one: negate
    // the right vector AFTER `u` has been taken from the unflipped basis.
    bool  flipX = false;
};

struct Projected {
    float x = 0, y = 0, z = 0;   // screen pixels, and depth along the view axis
    bool  ahead = false;         // z > 1, the near cut camshot.py uses
};

Projected project(const RCamera& c, const float p[3]);

// The camera's own axes and its two tangents, exposed so that a SECOND
// renderer uses these conventions rather than a second copy of them.
//
// That is not tidiness. The two errors that laying `camshot.py`'s wireframe
// over a real screenshot corrected both live in here - world up is **(0, -1,
// 0)** because the game's Y points DOWN, and `hfovDeg` is the **horizontal**
// fov with the vertical following from the frame's shape - and a Vulkan
// backend that re-derived them would be a fresh opportunity to get either
// wrong, in a place where the only symptom is a picture that looks plausible.
// `mirror` is honoured here too, so the reflected reading stays renderable
// through every backend.
//
// `s`, `u`, `f` are the right, up and forward axes; view space is
// `(dot(d,s), dot(d,u), dot(d,f))` for `d = world - eye`.
void cameraBasis(const RCamera& c, float s[3], float u[3], float f[3],
                 float& tanHalfH, float& tanHalfV);

// The near cut, shared for the same reason: `camshot.py` cuts at z <= 1 and a
// GPU projection matrix has to put its near plane in the same place or the two
// renderers disagree about what is in shot.
inline constexpr float kNearCut = 1.0f;

struct RasterStats {
    long triangles = 0;      // corners/3 offered
    long drawn = 0;          // rasterised (all three vertices ahead)
    long behind = 0;         // rejected: a vertex at or behind the near cut
    long offscreen = 0;      // rejected: bounding box outside the frame
    long pixels = 0;         // texels written
    long depthRejects = 0;   // failed the z test
    std::uint32_t hash = 0;  // FNV of the framebuffer
};

// Draw one `.3DO`'s geometry. `textures` is indexed by the batch's material,
// which is how the shipped data links the two; a material with no texture
// draws its vertex colour alone.
//
// The z buffer is the caller's so a frame can be composed from several models.
RasterStats drawGeometry(Surface& fb, std::vector<float>& depth,
                         const RCamera& cam, const Geometry& g,
                         std::span<const Texture> textures);

// Clear a depth buffer to "nothing here yet".
void clearDepth(std::vector<float>& depth, int w, int h);

}  // namespace omk

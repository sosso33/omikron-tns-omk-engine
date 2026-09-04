// SPDX-License-Identifier: GPL-3.0-or-later
// THE SNEAK'S 3D PREVIEWS - the interface's own 3D path.
//
// The device shows three turning models down the left of its inventory
// window, and they are NOT per-object: the sneak's open callback loads three
// LITERAL models and holds each in a global of its own -
//
//     meshes\objets\setek.3do    -> dword_670BF8
//     meshes\objets\anneau.3do   -> dword_670C58
//     meshes\objets\imager.3do   -> dword_670CC0
//
// through `sub_41E200(path)` and `sub_41E230(0, model, &state)`, and the
// close callback's parameter-0 arm frees exactly those three.
//
// The drawer is `I2D_Submit3DView` (0x00428900), an I2D primitive of its own -
// 84 bytes of rect + scene + camera. `Ui_DrawItem` never reaches it, so no
// item flag names it; the function after `sub_477990` submits it.
//
// THE CAMERA is `sub_478DE0(node, distance, camOut)` and is four numbers:
//
//     target      the model's own centre
//     position    that point with Z + the distance
//     cam+0x30    the FOV, 0x42480000 = 50
//     cam+0x2C    0
//
// and the distance the sneak passes is `0x42EC3871` = 118.110, which is
// 3.0 / 0.0254 - THREE METRES in the engine's inch unit, the same 0.0254
// `Sound_Init` tells the listener. The sign of that argument picks the arm and
// reads backwards easily: `fcomp` against 0 then `test ah, 41h` / `jz` takes
// the jump when NEITHER C0 nor C3 is set, i.e. when it is POSITIVE - so a
// positive distance uses the literal offset and SKIPS the bounding-box fit.
//
// THE SPIN is `sub_441EB0(0, angle, 0, node+0x38)` - three Euler angles with
// only the middle one set, so it turns about Y, a turntable - and the angle is
// OSCILLATOR 4 (`sub_42B5E0(4)`, period 5000) through pi/180. Once every five
// seconds, which is what a capture of the original shows.
//
// TWO THINGS THIS DOES DIFFERENTLY, both stated rather than hidden:
//
//   * the target is computed from the GEOMETRY's own centre rather than read
//     from the model record's `+0x24..0x2C`. The engine reads a field of a
//     loaded structure; the port has the corners, and their centre is the
//     same point for a model authored about its origin - which is what these
//     three are, and the render shows it.
//   * the CAMERA turns about the model rather than the model turning under
//     the camera. For a rigid body with per-vertex baked colour the two give
//     the same picture, and it avoids re-transforming the geometry every
//     frame. Nothing in the engine's own data can tell them apart.
//
// TIER 5: the models, the distance, the fov and the period are read out of the
// image and asserted by `verify.py: sneak previews`; the picture is not
// compared against a capture.
#pragma once

#include "o3de/geom3do.h"
#include "o3de/raster.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"
#include "ui/surface.h"

#include <string>
#include <vector>

namespace omk {

class UiModels {
public:
    // `sub_41E200` on each of the three literal paths. Returns how many
    // loaded; a model that will not load is skipped and its slot draws
    // nothing, the way the engine's own `cmp eax, ebx` arm skips one.
    int load(const DataFs& fs);
    int count() const { return static_cast<int>(m_.size()); }
    const std::string& name(int k) const { return m_[static_cast<std::size_t>(k)].name; }

    // Draw model `k` into `dst`'s rect, turned `angleDeg` about Y.
    bool draw(Surface& dst, int k, int x, int y, int w, int h, float angleDeg);

    // ---- THE EXAMINE PAGE'S CONTENT, and there are TWO kinds ----------
    //
    // `sub_49B950` asks `sub_42B330` for the object's kind and sends kind 5
    // to `sub_478EF0`. Both of those raise the inventory channel's EVENT 40,
    // which fills one block from the record's own `+2`:
    //
    //     kind 15  ->  result 4, `+0` = the loaded preview MODEL
    //     kind 16  ->  result 5, `+0` = `sub_40BB40(rec + 0x0E)`, and
    //                  `sub_478EF0` loads `Images\<that>` as a BITMAP
    //     else     ->  result 2, no examine content at all
    //
    // `rec + 0x0E` is exactly the `stem` this port already reads, and the
    // shipped data checks both arms two ways: **83 of 83** kind-15 records
    // have a `MESHES\OBJETS\<stem>.3do` and **17 of 17** kind-16 records have
    // an `IMAGES\<stem>.bmp`. Neither had to come out whole.
    //
    // The documents are what a player described: newspapers, notes, maps and
    // books - "Omikron News - 11 Nadim 7216", "Plan des egouts de la Zone 9".
    enum class Examine { None, Model, Document };
    // Set the page's content from an object record's kind and stem. Returns
    // what it became, so a caller can say so rather than guess.
    Examine examine(const DataFs& fs, int kind, const std::string& stem);
    Examine examineKind() const { return exKind_; }
    // Draw whatever `examine` loaded into the rect. A document is blitted
    // whole and scaled to fit; a model goes through the same camera the
    // previews use.
    bool drawExamine(Surface& dst, int x, int y, int w, int h, float angleDeg);

    // `sub_42B5E0(4)` - oscillator 4, period 5000, and this is its angle in
    // degrees. The oscillator's own ramp is `sub_42B700`'s triangle over
    // lo..hi; row 4 ships lo/hi of -1, so what it carries is the raw phase.
    static float spinDegrees(long clockMs) {
        return static_cast<float>(clockMs % 5000) * 360.0f / 5000.0f;
    }
    // `0x42EC3871` = 3.0 / 0.0254. NOT the previews' distance: they pass 0
    // and take the bounding-box fit. This is the CHARACTER view's, which
    // `sub_4778E0` builds from the player's own model - three metres for a
    // standing man - and is kept here because the two call sites share
    // `sub_478DE0` and telling them apart is the whole point.
    static constexpr float kCharacterDistance = 118.11024f;
    static constexpr float kFovDeg   = 50.0f;   // `cam+0x30` = 0x42480000

private:
    struct M {
        std::string name;
        Geometry    geo;
        std::vector<Texture> tex;
        float centre[3] = {0, 0, 0};
        float extent = 0.0f;   // max(dx, dy, dz), for the fit arm
    };
    std::vector<M>   m_;
    SoftwareRenderer sw_;
    int rw_ = 0, rh_ = 0;
    // The examine page's content: one model loaded on demand, or one bitmap.
    Examine     exKind_ = Examine::None;
    std::string exStem_;
    M           exModel_;
    Surface     exImage_;
};

}  // namespace omk

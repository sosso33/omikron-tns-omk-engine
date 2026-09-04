// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/models.h"

#include "formats/tex3dt.h"

#include <cmath>

namespace omk {
namespace {

// The three literal paths the sneak's open callback pushes, in the order it
// pushes them. Two ship upper-case and one mixed, so the case-insensitive
// resolve `DataFs` does because Win95 did is load-bearing here.
constexpr const char* kPaths[] = {"MESHES/OBJETS/setek.3do",
                                  "MESHES/OBJETS/anneau.3do",
                                  "MESHES/OBJETS/imager.3do"};

}  // namespace

int UiModels::load(const DataFs& fs) {
    m_.clear();
    for (const char* p : kPaths) {
        const auto d = fs.read(p);
        if (d.empty()) continue;          // the engine's own `cmp eax, ebx` arm
        M m;
        m.name = p;
        m.geo = buildGeometry(d, DrawFilter::Engine);
        if (m.geo.corners.empty()) continue;
        std::string tp = p;
        tp.replace(tp.size() - 4, 4, ".3dt");
        const auto t = fs.read(tp);
        if (!t.empty()) m.tex = textures(d, t);
        // The camera's target. The engine reads the loaded model's own
        // `+0x24..0x2C`; this takes the centre of the corners, which is the
        // same point for a model authored about its origin.
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& c : m.geo.corners) {
            const float v[3] = {c.x, c.y, c.z};
            for (int k = 0; k < 3; ++k) {
                if (v[k] < lo[k]) lo[k] = v[k];
                if (v[k] > hi[k]) hi[k] = v[k];
            }
        }
        for (int k = 0; k < 3; ++k) m.centre[k] = (lo[k] + hi[k]) * 0.5f;
        // `sub_437D60`'s box, reduced the way the fit arm reduces it: the
        // largest of the three extents.
        m.extent = 0.0f;
        for (int k = 0; k < 3; ++k)
            if (hi[k] - lo[k] > m.extent) m.extent = hi[k] - lo[k];
        m_.push_back(std::move(m));
    }
    return count();
}

bool UiModels::draw(Surface& dst, int k, int x, int y, int w, int h,
                    float angleDeg) {
    if (k < 0 || k >= count() || w <= 0 || h <= 0 || !dst.valid()) return false;
    M& m = m_[static_cast<std::size_t>(k)];

    if (w != rw_ || h != rh_) { sw_.init(w, h); rw_ = w; rh_ = h; }
    sw_.setTextures(m.tex);

    // `sub_478DE0`: look at the model's centre from that point with Z offset
    // by the distance, fov 50. The SPIN is the camera turning about the
    // model - see the header for why that is the same picture.
    RCamera cam;
    const float pi = 3.14159265358979323846f;
    const float a = angleDeg * pi / 180.0f;
    // THE FIT ARM of `sub_478DE0`, and which arm runs is decided by the sign
    // of the distance argument the caller passes. The item previews pass
    // **0** - `push 0` right before the call - and 0 is not positive, so
    // `fcomp` sets C3, `test ah, 41h` is nonzero, the `jz` is NOT taken and
    // the bounding-box block runs:
    //
    //     E    = max(dx, dy, dz) of the model's box   (`sub_437D60`)
    //     dist = E + E / tan(fov degrees)
    //
    // The 118.110 literal in the other arm belongs to the CHARACTER view -
    // `sub_4778E0` builds that node from the player's own model name and an
    // `ANIMS\%s` clip, and three metres is right for a standing man. Reading
    // that camera as the previews' put these 3-to-8-unit objects a hundred
    // units away: two lit pixels of a fifty-pixel slot, where a capture of
    // the original shows them filling it. The picture is what separated the
    // two call sites.
    const float dist = m.extent + m.extent / std::tan(kFovDeg * pi / 180.0f);
    cam.at[0] = m.centre[0]; cam.at[1] = m.centre[1]; cam.at[2] = m.centre[2];
    cam.eye[0] = m.centre[0] + std::sin(a) * dist;
    cam.eye[1] = m.centre[1];
    cam.eye[2] = m.centre[2] + std::cos(a) * dist;
    cam.hfovDeg = kFovDeg;
    cam.w = w; cam.h = h;

    View v; v.cam = cam;
    sw_.begin(v);
    for (const auto& b : m.geo.batches) {
        Draw dr;
        // The key's low six bits ARE the texture slot (ASSETS 4b), which is
        // how every other caller of this boundary builds one.
        dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
        dr.geo = &m.geo; dr.start = b.start; dr.count = b.count;
        dr.blend = b.blend; dr.cutout = b.cutout;
        sw_.submit(dr);
    }
    sw_.end();

    // ...and place it. `I2D_Submit3DView` renders into a RECTANGLE of the 2D
    // frame, so the picture goes where the item is. Black is the 3D view's
    // own background and is left transparent, which is what makes the models
    // sit on the panel rather than in a black box.
    const Surface& pic = sw_.readback();
    if (!pic.valid()) return false;
    int drawn = 0;
    for (int j = 0; j < h && j < pic.h; ++j) {
        const int dy = y + j;
        if (dy < 0 || dy >= dst.h) continue;
        for (int i = 0; i < w && i < pic.w; ++i) {
            const int dx = x + i;
            if (dx < 0 || dx >= dst.w) continue;
            const std::uint16_t px = pic.px[static_cast<std::size_t>(j) * pic.w + i];
            if (!px) continue;
            dst.px[static_cast<std::size_t>(dy) * dst.w + dx] = px;
            ++drawn;
        }
    }
    return drawn > 0;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/models.h"

#include "formats/mesh3do.h"
#include "formats/tex3dt.h"

#include <cctype>
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

// `Game_HandleEvent` case 40, the arm this port needs: the record's `+2`
// decides, and `+0x0E` (the `stem`) names the content.
UiModels::Examine UiModels::examine(const DataFs& fs, int kind,
                                    const std::string& stem) {
    if (stem == exStem_ && exKind_ != Examine::None) return exKind_;
    exKind_ = Examine::None;
    exStem_ = stem;
    exImage_ = Surface();
    exModel_ = M();
    if (stem.empty()) return exKind_;
    if (kind == 15) {                       // result 4 - the 3D preview
        const auto d = fs.read("MESHES/OBJETS/" + stem + ".3do");
        if (d.empty()) return exKind_;
        exModel_.name = stem;
        exModel_.geo = buildGeometry(d, DrawFilter::Engine);
        if (exModel_.geo.corners.empty()) return exKind_;
        const auto t = fs.read("MESHES/OBJETS/" + stem + ".3dt");
        if (!t.empty()) exModel_.tex = textures(d, t);
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& c : exModel_.geo.corners) {
            const float v[3] = {c.x, c.y, c.z};
            for (int j = 0; j < 3; ++j) {
                if (v[j] < lo[j]) lo[j] = v[j];
                if (v[j] > hi[j]) hi[j] = v[j];
            }
        }
        for (int j = 0; j < 3; ++j) {
            exModel_.centre[j] = (lo[j] + hi[j]) * 0.5f;
            if (hi[j] - lo[j] > exModel_.extent) exModel_.extent = hi[j] - lo[j];
        }
        exKind_ = Examine::Model;
    } else if (kind == 16) {                // result 5 - `Images\<stem>`
        auto d = fs.read("IMAGES/" + stem + ".bmp");
        if (d.empty()) d = fs.read("IMAGES/" + stem);
        if (d.empty()) return exKind_;
        exImage_ = surfaceFromBmp(d);
        if (exImage_.valid()) exKind_ = Examine::Document;
    }
    return exKind_;
}

bool UiModels::loadWeapon(const DataFs& fs, const std::string& stem) {
    if (stem == weaponStem_) return weaponOk_;
    weaponStem_ = stem;
    weaponOk_ = false;
    weapon_ = M();
    if (stem.empty()) return false;
    const auto d = fs.read("MESHES/OBJETS/" + stem + ".3do");
    if (d.empty()) return false;
    Geometry g = buildGeometry(d, DrawFilter::Engine);
    if (g.corners.empty()) return false;
    // `tir`, by name as `sub_41E230(0x4C487C "tir", ...)` finds it, and
    // every corner it poses dropped - the same test the gun in the hand uses
    // (`cornerMesh` against its index).
    int tir = -1;
    if (const auto h = readHeader(d)) {
        const auto ms = readMeshes(d, *h);
        for (std::size_t i = 0; i < ms.size(); ++i) {
            std::string nm = ms[i].name;
            for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (nm == "tir") tir = static_cast<int>(i);
        }
    }
    if (tir >= 0 && g.cornerMesh.size() == g.corners.size()) {
        Geometry f = g;
        f.corners.clear(); f.batches.clear(); f.cornerMirror.clear();
        f.cornerMesh.clear(); f.cornerVertex.clear(); f.cornerDeclared.clear();
        for (const Batch& b : g.batches) {
            Batch nb = b;
            nb.start = f.corners.size();
            // whole triangles: a triangle whose corners all belong to `tir`
            // goes, any other stays
            for (std::size_t c = b.start; c + 2 < b.start + b.count; c += 3) {
                if (g.cornerMesh[c] == tir && g.cornerMesh[c + 1] == tir &&
                    g.cornerMesh[c + 2] == tir) continue;
                for (std::size_t j = c; j < c + 3; ++j) {
                    f.corners.push_back(g.corners[j]);
                    if (j < g.cornerMirror.size())   f.cornerMirror.push_back(g.cornerMirror[j]);
                    f.cornerMesh.push_back(g.cornerMesh[j]);
                    if (j < g.cornerVertex.size())   f.cornerVertex.push_back(g.cornerVertex[j]);
                    if (j < g.cornerDeclared.size()) f.cornerDeclared.push_back(g.cornerDeclared[j]);
                }
            }
            nb.count = f.corners.size() - nb.start;
            if (nb.count) f.batches.push_back(nb);
        }
        g = std::move(f);
    }
    weapon_.name = stem;
    weapon_.geo = std::move(g);
    const auto t = fs.read("MESHES/OBJETS/" + stem + ".3dt");
    if (!t.empty()) weapon_.tex = textures(d, t);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& c : weapon_.geo.corners) {
        const float v[3] = {c.x, c.y, c.z};
        for (int j = 0; j < 3; ++j) {
            if (v[j] < lo[j]) lo[j] = v[j];
            if (v[j] > hi[j]) hi[j] = v[j];
        }
    }
    for (int j = 0; j < 3; ++j) {
        weapon_.centre[j] = (lo[j] + hi[j]) * 0.5f;
        if (hi[j] - lo[j] > weapon_.extent) weapon_.extent = hi[j] - lo[j];
    }
    weaponOk_ = !weapon_.geo.corners.empty();
    return weaponOk_;
}

bool UiModels::drawWeapon(Surface& dst, int x, int y, int w, int h, float angleDeg,
                          float distance) {
    if (!weaponOk_) return false;
    m_.push_back(weapon_);
    const bool ok = draw(dst, static_cast<int>(m_.size()) - 1, x, y, w, h, angleDeg,
                         distance);
    m_.pop_back();
    return ok;
}

bool UiModels::drawExamine(Surface& dst, int x, int y, int w, int h,
                           float angleDeg) {
    if (exKind_ == Examine::Model) {
        m_.push_back(exModel_);
        const bool ok = draw(dst, static_cast<int>(m_.size()) - 1, x, y, w, h,
                             angleDeg);
        m_.pop_back();
        return ok;
    }
    if (exKind_ != Examine::Document || !exImage_.valid() || !dst.valid())
        return false;
    // A document is a BITMAP, blitted into the page's rect. `sub_478EF0`
    // hands it to the I2D bitmap cache and the page's own item draws it;
    // scaling to fit is this port's, because the item's rect and the file's
    // size are both fixed and the engine's blit stretches.
    for (int j = 0; j < h; ++j) {
        const int dy = y + j, sy = j * exImage_.h / h;
        if (dy < 0 || dy >= dst.h || sy < 0 || sy >= exImage_.h) continue;
        for (int i = 0; i < w; ++i) {
            const int dx = x + i, sx = i * exImage_.w / w;
            if (dx < 0 || dx >= dst.w || sx < 0 || sx >= exImage_.w) continue;
            dst.px[static_cast<std::size_t>(dy) * dst.w + dx] =
                exImage_.px[static_cast<std::size_t>(sy) * exImage_.w + sx];
        }
    }
    return true;
}

bool UiModels::draw(Surface& dst, int k, int x, int y, int w, int h,
                    float angleDeg, float distance) {
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
    // ...and a POSITIVE distance is the other arm: the literal offset, no fit
    // - what the shoot HUD passes (10.0 for the ring, 25.0 for the weapon).
    const float dist = distance > 0.0f
                           ? distance
                           : m.extent + m.extent / std::tan(kFovDeg * pi / 180.0f);
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

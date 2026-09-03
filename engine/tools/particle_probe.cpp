// SPDX-License-Identifier: GPL-3.0-or-later
// THE PARTICLE PATH, PROBED - six rules of `Sfx_TickAmbient`,
// `Render_SubmitSprites` and `Raster_DrawTriangles`, each printed as a number
// the data cannot fake.
//
//     particle_probe <gamedata> <out.txt>
//
// Written 2026-09-02 against the code as it stood, and its first run FAILED
// six of six - which is the point. `traces/frames/intro-75.png` is a
// saturated cyan disc with a white core and a DARK STARBURST inside it; the
// port drew a dim uniform haze. Every difference came from a rule that had
// been read and then transcribed one step wrong:
//
//   alive     a particle is integrated and drawn while `age <= life` (the
//             tick's own test, `f32(v41,4) <= f32(v41,0)`), so an emitter
//             with life L keeps L+1 alive, not L. GRID's four standing rows
//             hold 37*5 + 10 + 4 + 11 = 210, not 194.
//   frame     `(frames-1) * age / life` is computed from the age BEFORE the
//             tick increments it, so a newborn is drawn on frame 0 and the
//             last draw lands on frame `frames-1`. Computing it after put the
//             port one frame ahead: IMPACT2's bright starburst (frame 0) was
//             never drawn for `burnv`, whose life is 3.
//   colour    the additive blend is SRCBLEND=ONE / DESTBLEND=ONE and the
//             multiply ZERO / INVSRCCOLOR - NEITHER reads the alpha. The
//             instance's 0.5 goes into the diffuse dword's high byte
//             (`u32(v6,108) << 24` in the bucket walk) and only the plain
//             transparent modes (SRCALPHA / INVSRCALPHA) consume it, which no
//             shipped effect uses. Scaling the colour by it halved every
//             particle in the game.
//   order     `Render_FlushBuckets` walks keys ASCENDING, and the sprite's
//             mode ORs 0x2100 (additive) or 0x2200 (multiply) into the key -
//             so every additive particle draws before every multiply one,
//             whatever their textures. Grouped by sprite first, `ttt`'s
//             multiply starburst (sprite 10) drew before `burn`'s puffs
//             (sprite 12) and darkened only black.
//   mul       DESTBLEND=INVSRCCOLOR (D3DBLEND 4) is `dst * (1 - src)`: the
//             pass darkens where the SPRITE IS BRIGHT and leaves the frame
//             alone where it is black. `dst * src` is the opposite quad.
//   drift     `+28` enters the Y velocity NEGATED unless bits 0x600 read
//             exactly 0x200 (`fld [ebx+1Ch]; fchs` at 0x46DD9D). Y points
//             down, so a positive drift climbs. 104 shipped rows carry a
//             non-zero drift with the bit clear; the port sank all of them.
//   revision  `particleGeometry` REBUILDS its output every frame, and a
//             backend that caches a vertex buffer by pointer and revision
//             must see the revision move. A fresh Geometry's is always 1.
//
// And the SET PIECES (`setpiece.h`), which the capture's dark ring turned out
// to need: `ttt`'s row is linked to row 0 and walks a 27-waypoint circle of
// radius 39.4 in ten frames, so the probe prints the orbit's radius and its
// period; the arrival piece (`1KaylArrives`, the indirect form linked to
// actor 'HO1') waits 6 frames, plays `kaylarr` then `kay arr`, ping-pongs,
// and hides after its second loop - frame 44.
#include "platform/datafs.h"
#include "formats/sfx.h"
#include "o3de/particles.h"
#include "o3de/raster.h"
#include "o3de/setpiece.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* blendName(omk::Blend b) {
    return b == omk::Blend::Add ? "add" : b == omk::Blend::Mul ? "mul" : "opaque";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: particle_probe <gamedata> <out.txt>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[2])) return 2;
    const omk::DataFs fs(argv[1]);
    const auto d = fs.read("SCPTDATA/Grid.sfx");
    const omk::SfxFile sfx = omk::readSfx(d);
    if (!sfx.valid) { std::fprintf(stderr, "Grid.sfx unreadable\n"); return 1; }
    std::ofstream out(argv[2]);

    // ---- GRID's four standing rows, the way `Sfx_BindAmbientEffects` shows
    // them: section E keyed (1, -1), effect by section C's 1-based id.
    omk::ParticleField field;
    field.seed(7);
    int emitters = 0;
    for (const auto& p : sfx.pieces) {
        if (p.key0 != 1 || p.key1 != -1) continue;
        for (const auto& e : sfx.effects)
            if (e.id == p.effectId) { field.add(e, p.pos); ++emitters; }
    }
    out << "emitters " << emitters << "\n";

    // Synthetic sprites for ids 9..12: eight frames laid out along U at 32
    // texels a frame, so a corner's U names the frame it was drawn from.
    std::vector<omk::SpriteFrames> sprites(13);
    for (int id = 9; id <= 12; ++id) {
        auto& sf = sprites[static_cast<std::size_t>(id)];
        for (int k = 0; k < 8; ++k) {
            const float u0 = static_cast<float>(32 * k);
            sf.frames.push_back({u0, 0, u0 + 31, 0, u0 + 31, 31, u0, 31});
            sf.extent.push_back(10.0f);
        }
        sf.material = id;
    }
    const float eye[3] = {-468.0f, -82.0f, -300.0f};
    const float at[3]  = {-468.0f, -82.0f, 4.0f};

    omk::Geometry g;
    for (int t = 0; t < 90; ++t) {
        field.tick(1.0f);
        omk::particleGeometry(g, field, eye, at, sprites);
        if (t == 0) {
            // The FIRST draw of every newborn: the frame index each batch's
            // first corner was drawn from, by its U.
            for (const auto& b : g.batches) {
                const auto& c = g.corners[b.start];
                out << "first sprite " << b.material << " " << blendName(b.blend)
                    << " frame " << static_cast<int>(c.u / 32.0f) << "\n";
            }
        }
    }
    out << "alive " << field.count() << "\n";
    out << "order";
    for (const auto& b : g.batches) out << " " << blendName(b.blend) << ":" << b.material;
    out << "\n";
    // `ttt` is the one multiply row, colour 0x859588 for its whole life, so
    // its corners must carry exactly that: 0x85/255 in red.
    for (const auto& b : g.batches)
        if (b.blend == omk::Blend::Mul) {
            const auto& c = g.corners[b.start];
            out << "mulcolour " << c.r << " " << c.g << " " << c.b << "\n";
            break;
        }
    // and `vd`'s 0x2125AF -> 0x2121EF ramp: a particle's red never leaves
    // [0x21/255, 0x21/255], so the additive batch of sprite 11 must read it
    // at full value rather than half.
    for (const auto& b : g.batches)
        if (b.blend == omk::Blend::Add && b.material == 11) {
            const auto& c = g.corners[b.start];
            out << "addcolour " << c.r << " " << c.g << " " << c.b << "\n";
            break;
        }
    // The last draw: how far the frame index reaches. `burnv` (sprite 10,
    // additive, life 3) is drawn at ages 0,1,2,3 -> frames 0,2,4,7.
    {
        std::vector<int> seen;
        for (const auto& b : g.batches)
            if (b.blend == omk::Blend::Add && b.material == 10)
                for (std::size_t i = b.start; i < b.start + b.count; i += 6)
                    seen.push_back(static_cast<int>(g.corners[i].u / 32.0f));
        out << "burnv-frames";
        for (int v : seen) out << " " << v;
        out << "\n";
    }

    // ---- the set pieces: the four standing rows and the arrival, run by
    // the state machine with a stub actor under 'HO1'.
    {
        omk::SetPieceRunner sp;
        sp.attach(&sfx);
        sp.setLinks([](int type, std::uint32_t id, omk::PieceLink& L) {
            if (type != 2 || id != 0x484F31u) return false;   // 'HO1'
            L.pos[0] = -487.0f; L.pos[1] = -43.0f; L.pos[2] = -78.0f;
            L.hasMatrix = true;
            return true;
        });
        sp.showKeyed(1, -1);
        sp.showKeyed(0, 1);
        omk::ParticleField f3;
        f3.seed(7);
        int row0 = -1, row3 = -1, row4 = -1;
        for (std::size_t i = 0; i < sfx.pieces.size(); ++i) {
            const auto& p = sfx.pieces[i];
            if (p.key0 == 1 && p.key1 == -1 && p.effectId == 2) row0 = static_cast<int>(i);
            if (p.key0 == 1 && p.key1 == -1 && p.effectId == 4) row3 = static_cast<int>(i);
            if (p.key0 == 0 && p.key1 == 1) row4 = static_cast<int>(i);
        }
        out << "pieces shown " << sp.shownCount() << " rows " << row0 << ' ' << row3 << ' ' << row4 << "\n";
        float rmin = 1e9f, rmax = 0.0f, firstAngle = 0.0f;
        int period = -1, hiddenAt = -1, waitFrames = 0;
        std::vector<int> arrivalEffects;
        for (int t = 0; t < 90; ++t) {
            sp.tick(1.0f, f3);
            f3.tick(1.0f);
            const auto& st = sp.states();
            if (row0 >= 0 && row3 >= 0) {
                const auto& a = st[static_cast<std::size_t>(row0)];
                const auto& b = st[static_cast<std::size_t>(row3)];
                const float dx = b.pos[0] - a.pos[0], dy = b.pos[1] - a.pos[1],
                            dz = b.pos[2] - a.pos[2];
                const float r = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (r < rmin) rmin = r;
                if (r > rmax) rmax = r;
                const float ang = std::atan2(dy, dx);
                if (t == 0) firstAngle = ang;
                else if (period < 0 && t >= 5 && std::fabs(ang - firstAngle) < 0.05f) period = t;
            }
            if (row4 >= 0) {
                const auto& s = st[static_cast<std::size_t>(row4)];
                if (s.shown && s.waiting) ++waitFrames;
                if (s.shown && s.effectLast &&
                    (arrivalEffects.empty() || arrivalEffects.back() != s.effectLast))
                    arrivalEffects.push_back(s.effectLast);
                if (!s.shown && hiddenAt < 0) hiddenAt = t;
            }
        }
        out << "orbit radius " << rmin << ' ' << rmax << " period " << period << "\n";
        out << "arrival wait " << waitFrames << " hidden " << hiddenAt << " effects";
        for (int e : arrivalEffects) out << ' ' << e;
        out << "\n";
        out << "pieces alive " << f3.count() << " shown " << sp.shownCount()
            << " registered " << sp.registered() << "\n";
    }

    // ---- the revision a caching backend keys on
    {
        omk::Geometry gg;
        omk::particleGeometry(gg, field, eye, at, sprites);
        const auto r1 = gg.revision;
        omk::particleGeometry(gg, field, eye, at, sprites);
        out << "revision " << r1 << " " << gg.revision << "\n";
    }

    // ---- the drift sign, both arms
    for (std::uint32_t flags : {0x0u, 0x200u}) {
        omk::ParticleField f2;
        omk::FxEffect e;
        e.id = 1; e.drift = 1.0f; e.life = 10; e.count = 1; e.flags = flags;
        const float o[3] = {0, 0, 0};
        f2.add(e, o);
        f2.tick(1.0f);
        out << "drift " << flags << " vy " << f2.particles()[0].vel[1] << "\n";
    }

    // ---- the multiply arithmetic: a white quad over a white frame
    {
        omk::Surface fb(64, 64, 0xFFFF);
        std::vector<float> depth;
        omk::clearDepth(depth, 64, 64);
        omk::RCamera cam;
        cam.eye[0] = 0; cam.eye[1] = 0; cam.eye[2] = -100;
        cam.at[0] = 0; cam.at[1] = 0; cam.at[2] = 0;
        cam.w = 64; cam.h = 64;
        omk::Geometry q;
        const float xs[4] = {-20, 20, 20, -20}, ys[4] = {-20, -20, 20, 20};
        omk::Corner c[4];
        for (int k = 0; k < 4; ++k) {
            c[k].x = xs[k]; c[k].y = ys[k]; c[k].z = 0;
            c[k].u = 0; c[k].v = 0; c[k].r = c[k].g = c[k].b = 1.0f; c[k].phase = -1;
        }
        q.corners = {c[0], c[1], c[2], c[0], c[2], c[3]};
        omk::Batch b; b.material = 0; b.blend = omk::Blend::Mul; b.start = 0; b.count = 6;
        q.batches.push_back(b);
        q.cornerMirror.assign(6, 0); q.cornerMesh.assign(6, -1);
        q.cornerVertex.assign(6, -1); q.cornerDeclared.assign(6, -1);
        omk::drawGeometry(fb, depth, cam, q, std::span<const omk::Texture>{});
        out << "mul white-on-white " << fb.at(32, 32) << "\n";
    }
    return 0;
}

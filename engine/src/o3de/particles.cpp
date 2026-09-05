// SPDX-License-Identifier: GPL-3.0-or-later
// The particle system. See `particles.h` for where each rule comes from.
#include "o3de/particles.h"

#include "formats/mesh3do.h"

#include <algorithm>
#include <map>
#include <cmath>

namespace omk {
namespace {


void norm(float v[3]) {
    const float m = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (m > 1e-6f) { v[0] /= m; v[1] /= m; v[2] /= m; }
}

}  // namespace

float ParticleField::rnd() {
    // xorshift32 - deterministic, so a check can assert a frame. Which numbers
    // come out is not the engine's; WHERE randomness enters is.
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / 16777216.0f;
}

Emitter ParticleField::place(const FxEffect& fx, const float pos[3]) {
    Emitter e;
    e.fx = fx;
    for (int k = 0; k < 3; ++k) e.pos[k] = pos[k];
    e.axis[0] = fx.vx; e.axis[1] = fx.vy; e.axis[2] = fx.vz;
    // `Sfx_RegisterEmitter` jitters the axis ONCE per registration, under
    // flag 0x40, over the +x..+z quadrant - always positive, so the lean is
    // consistent rather than shimmering per particle.
    if (fx.flags & kFxJitDir)
        for (int k = 0; k < 3; ++k) e.axis[k] += rnd() * 2.0f;
    norm(e.axis);
    // A non-zero period starts on a random phase; 0 emits every frame.
    e.phase = 0.0f;
    return e;
}

void ParticleField::add(const FxEffect& fx, const float pos[3]) {
    emitters_.push_back(place(fx, pos));
}

void ParticleField::spawn(const FxEffect& fx, const float pos[3]) {
    Emitter e = place(fx, pos);
    emit(e);
}

void ParticleField::emit(Emitter& e) {
    int n = e.fx.count > 0 ? e.fx.count : 1;
    if (e.fx.flags & kFxNCount)
        n += static_cast<int>(n * rnd() * 0.1f);
    for (int i = 0; i < n; ++i) {
        Particle p;
        for (int k = 0; k < 3; ++k) p.pos[k] = e.pos[k];
        // THE CONE, as `Sfx_TickAmbient` builds it - spherical polar about the
        // emitter's axis, not a jitter of its components:
        //
        //     theta = fx[+60] degrees   (+ rand*theta under flag 0x1000)
        //     phi   = rand * 360
        //     local = (cos phi sin theta, sin phi sin theta, -cos theta) * speed
        //     dir   = basis * local
        //
        // and the SPEED is `|vx, vy, vz|`, which the engine takes the same way
        // (`v60 = sqrt(vx*vx + vy*vy + vz*vz)`). A speed of 0 emits a
        // STATIONARY particle, which is what the engine's `if (v60 == 0.0)`
        // arm does and what a standing glow wants.
        const float speed = std::sqrt(e.fx.vx * e.fx.vx + e.fx.vy * e.fx.vy +
                                      e.fx.vz * e.fx.vz);
        if (speed == 0.0f) {
            p.vel[0] = p.vel[1] = p.vel[2] = 0.0f;
        } else {
            float theta = e.fx.cone;
            if (e.fx.flags & kFxRndCone) theta += rnd() * e.fx.cone;
            const float th = theta * 0.0174532925199433f;
            const float phi = rnd() * 6.2831853071795864f;
            const float st = std::sin(th);
            const float local[3] = {std::cos(phi) * st * speed,
                                    std::sin(phi) * st * speed,
                                    -std::cos(th) * speed};
            // The basis maps -Z onto the emitter's axis, which is what
            // `sub_442690`'s two Euler angles amount to; building it from the
            // axis directly avoids transcribing that path and cannot disagree
            // with it about which way -Z goes.
            float u[3] = {0, 0, 0}, v[3] = {0, 0, 0};
            const float a[3] = {e.axis[0], e.axis[1], e.axis[2]};
            float pick[3] = {0.0f, 1.0f, 0.0f};
            if (std::fabs(a[1]) >= 0.9f) { pick[0] = 1.0f; pick[1] = 0.0f; }
            u[0] = pick[1] * a[2] - pick[2] * a[1];
            u[1] = pick[2] * a[0] - pick[0] * a[2];
            u[2] = pick[0] * a[1] - pick[1] * a[0];
            norm(u);
            v[0] = a[1] * u[2] - a[2] * u[1];
            v[1] = a[2] * u[0] - a[0] * u[2];
            v[2] = a[0] * u[1] - a[1] * u[0];
            for (int k = 0; k < 3; ++k)
                p.vel[k] = local[0] * u[k] + local[1] * v[k] - local[2] * a[k];
        }
        p.life = e.fx.life > 0 ? e.fx.life : 1.0f;
        if (e.fx.flags & kFxNLife) p.life += p.life * rnd() * 0.1f;
        // The INSTANCE scale, which multiplies the sprite quad's own size.
        // `Sfx_TickAmbient` writes the effect's `+56` into the instance's
        // +24 AND +28 (`v28 = u32(v6, 56); u32i(v17, 6) = v28; u32i(v17, 7) =
        // v29;`), overwriting the 1.0 `Sprite_SpawnInstance` initialises - so
        // the effect's `scale` is the STARTING size, not a ramp magnitude.
        // The ramp is separate and OPTIONAL:
        //
        //     if      (flags & 4)      rate = +scale/life
        //     else if (flags & 0x2000) rate = -scale/life
        //     else                     rate = 0
        //
        // Nine of GRID's ten effects would grow or shrink under the old
        // unconditional reading; only two actually do.
        p.scale = e.fx.scale;
        p.dScale = 0.0f;
        if (e.fx.flags & kFxGrow)        p.dScale =  e.fx.scale / p.life;
        else if (e.fx.flags & kFxShrink) p.dScale = -e.fx.scale / p.life;
        p.angle = (e.fx.flags & kFxAngle) ? rnd() * 6.2831853f : 0.0f;
        // `+64` is DEGREES a frame - the engine multiplies it by pi/180 on
        // its way into the instance.
        p.spin = e.fx.spin * 0.0174532925199433f;
        // The colour ramp: `+48` at birth, `+52` at death, each 0x00RRGGBB
        // taken high byte to low. The engine only builds the rate when the
        // two dwords differ, which is the same arithmetic as a zero rate.
        for (int k = 0; k < 3; ++k) {
            const int sh = 16 - 8 * k;
            const float c0 = float((e.fx.colour0 >> sh) & 0xFFu) / 255.0f;
            const float c1 = float((e.fx.colour1 >> sh) & 0xFFu) / 255.0f;
            p.col[k]  = c0;
            p.dCol[k] = e.fx.colour0 == e.fx.colour1 ? 0.0f : (c1 - c0) / p.life;
        }
        // ...and the alpha, which the tick sets to 0.5 outright unless the
        // fade flag ramps it up from 0 by `1/life` (capped at 0.99). No
        // shipped row sets that flag, so this is always 0.5.
        if (e.fx.flags & kFxFade) {
            p.alpha = 0.0f;
            p.dAlpha = p.life > 0 ? std::min(1.0f / p.life, 0.99f) : 1.0f;
        } else {
            p.alpha = 0.5f;
            p.dAlpha = 0.0f;
        }
        // `+28` enters the Y velocity NEGATED unless bits 0x600 read exactly
        // 0x200 - `fld [ebx+1Ch]; fchs` at 0x46DD9D against a plain `mov` at
        // 0x46DD95. Y points down, so a positive drift CLIMBS, which is what
        // ASSETS 3b measured on the brazier. 0x400 and 0x600 leave the slot
        // untouched in the assembly and no shipped row sets them; they take
        // the negated arm here.
        p.drift = ((e.fx.flags & 0x600u) == 0x200u) ? e.fx.drift : -e.fx.drift;
        p.sprite = e.fx.sprite;
        p.mode = e.fx.mode;
        particles_.push_back(p);
    }
}

void ParticleField::tick(float dt) {
    for (auto& e : emitters_) {
        if (!e.live) continue;
        // The period is in FRAMES. 0 means every frame, which is what 148 of
        // Anekbah's 153 emitters ask for.
        const float period = 1.0f;
        e.phase -= dt;
        if (e.phase <= 0.0f) { emit(e); e.phase += period; }
    }
    // The engine's per-particle loop, in its order. Its test is
    // `f32(v41,4) <= f32(v41,0)` - AGE <= LIFE: a particle past its life is
    // released, every other one, the newborns included, is integrated and
    // drawn. So a particle of life L is drawn L+1 times, and one that has just
    // crossed stays in the list until the NEXT tick releases it - which is
    // why the release comes first here and the integration second.
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return !p.alive(); }),
                     particles_.end());
    for (auto& p : particles_) {
        // The frame index `(frames-1) * age / life` is taken from the age as
        // it stands BEFORE this tick's increment (`u16(v43,22) = ...` reads
        // `f32(v41,4)` and the `+= dt` follows), so a newborn draws frame 0.
        p.frameAge = p.age;
        // The integrator, in order: position by velocity, then the drift
        // added to the Y VELOCITY - an acceleration, so a flame climbs.
        for (int k = 0; k < 3; ++k) p.pos[k] += p.vel[k] * dt;
        p.vel[1] += p.drift * dt;
        p.scale += p.dScale * dt;
        p.angle += p.spin * dt;
        for (int k = 0; k < 3; ++k) p.col[k] += p.dCol[k] * dt;
        p.alpha += p.dAlpha * dt;
        p.age += dt;
    }
}

// One sprite's frames - its `.3DO`'s quads, each with its own four UV pairs.
SpriteFrames spriteFrames(std::span<const std::byte> model) {
    SpriteFrames sf;
    const auto h = readHeader(model);
    if (!h) return sf;
    const auto qs = readQuads(model, *h);
    const auto vs = readVertices(model, *h);
    sf.frames.reserve(qs.size());
    for (const auto& q : qs) {
        std::array<float, 8> uv{};
        for (int k = 0; k < 8; ++k) uv[static_cast<std::size_t>(k)] = q.uv[k];
        sf.frames.push_back(uv);
        // The quad's own half-extent - half the span of ITS four corners, not
        // the largest coordinate in the model. A sprite with eight quads has
        // them laid out across the file, so `max|p|` measures the whole sheet
        // rather than the frame and makes every particle sprite-sized.
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (int k = 0; k < 4; ++k) {
            const auto vi = static_cast<std::size_t>(q.idx[k]);
            if (vi >= vs.size()) continue;
            for (int c = 0; c < 3; ++c) {
                if (vs[vi].p[c] < lo[c]) lo[c] = vs[vi].p[c];
                if (vs[vi].p[c] > hi[c]) hi[c] = vs[vi].p[c];
            }
        }
        float e = 0;
        for (int c = 0; c < 3; ++c)
            if (hi[c] > lo[c] && (hi[c] - lo[c]) * 0.5f > e)
                e = (hi[c] - lo[c]) * 0.5f;
        sf.extent.push_back(e);
        sf.material = q.material;
    }
    return sf;
}

namespace {

// The renderer's mode table, from ASSETS 3b: the bits it ORs into the bucket
// key per instance mode. 4 is what `Cef_SpawnEffect` sets, so every character
// effect is additive.
Blend blendOfMode(std::uint8_t m) {
    if (m == 4 || m == 5) return Blend::Add;
    if (m == 6 || m == 7) return Blend::Mul;
    return Blend::Opaque;
}
bool cutoutOfMode(std::uint8_t m) {
    return m == 1 || m == 3 || m == 5 || m == 7 || m == 8;
}

}  // namespace

int spriteModeBits(std::uint8_t m) {
    // `Render_SubmitSprites`' switch on the instance's +20, verbatim.
    switch (m) {
    case 1: case 8: return 0x400;
    case 2: return 0x2000;
    case 3: return 0x2400;
    case 4: return 0x2100;
    case 5: return 0x2500;
    case 6: return 0x2200;
    case 7: return 0x2600;
    default: return 0;
    }
}

void particleGeometry(Geometry& g, const ParticleField& f, const float eye[3],
                      const float at[3],
                      const std::vector<SpriteFrames>& sprites) {
    g.corners.clear();
    g.batches.clear();
    g.cornerMirror.clear();
    g.cornerMesh.clear();
    g.cornerVertex.clear();
    g.cornerDeclared.clear();
    float fwd[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    norm(fwd);
    // The same basis the rasterizer takes: world up is (0,-1,0) because the
    // game's Y points DOWN, and the right vector is f x up.
    const float up[3] = {0.0f, -1.0f, 0.0f};
    float right[3] = {fwd[1] * up[2] - fwd[2] * up[1],
                      fwd[2] * up[0] - fwd[0] * up[2],
                      fwd[0] * up[1] - fwd[1] * up[0]};
    norm(right);
    float vup[3] = {right[1] * fwd[2] - right[2] * fwd[1],
                    right[2] * fwd[0] - right[0] * fwd[2],
                    right[0] * fwd[1] - right[1] * fwd[0]};
    norm(vup);

    // Grouped by (mode bits, sprite), because a batch carries one texture and
    // one blend - and in THAT order, because it is the bucket key's: the mode
    // bits sit above the texture slot and `Render_FlushBuckets` walks keys
    // ascending, so every additive batch precedes every multiply one whatever
    // their sprites. The batch's `material` is the SPRITE INDEX; the caller
    // maps it to a slot, which is the key's low six bits.
    std::map<std::pair<int, int>, std::vector<Corner>> groups;
    for (const auto& p : f.particles()) {
        const auto si = static_cast<std::size_t>(p.sprite);
        const SpriteFrames* sf =
            si < sprites.size() && !sprites[si].frames.empty() ? &sprites[si] : nullptr;
        // `(frames - 1) * age / life` - the sprite's quads played across the
        // particle's own lifetime, not at a fixed rate - from the age BEFORE
        // this tick's increment, which is the value the engine reads.
        std::array<float, 8> uv{0, 0, 255, 0, 255, 255, 0, 255};
        if (sf) {
            const int n = static_cast<int>(sf->frames.size());
            int fi = p.life > 0 ? static_cast<int>((n - 1) * (p.frameAge / p.life)) : 0;
            if (p.frame >= 0) fi = p.frame;      // a scripted sprite's own `+22`
            if (fi < 0) fi = 0;
            if (fi > n - 1) fi = n - 1;
            uv = sf->frames[static_cast<std::size_t>(fi)];
        }
        float base = 1.0f;
        if (sf) {
            const int n = static_cast<int>(sf->extent.size());
            int ei = p.life > 0 ? static_cast<int>((n - 1) * (p.frameAge / p.life)) : 0;
            if (p.frame >= 0) ei = p.frame;
            if (ei < 0) ei = 0;
            if (ei > n - 1) ei = n - 1;
            if (n) base = sf->extent[static_cast<std::size_t>(ei)];
        }
        const float s = base * p.scale;
        // `+24` scales the X corners and `+28` the Y ones - two scales, which
        // a scripted sprite may set apart; an effect's particle has one
        const float sy = p.scaleY >= 0.0f ? base * p.scaleY : s;
        const float ca = std::cos(p.angle), sa = std::sin(p.angle);
        const float ox[4] = {-s, s, s, -s}, oy[4] = {-sy, -sy, sy, sy};
        Corner c[4];
        for (int k = 0; k < 4; ++k) {
            const float rx = ox[k] * ca - oy[k] * sa;
            const float ry = ox[k] * sa + oy[k] * ca;
            c[k].x = p.pos[0] + right[0] * rx + vup[0] * ry;
            c[k].y = p.pos[1] + right[1] * rx + vup[1] * ry;
            c[k].z = p.pos[2] + right[2] * rx + vup[2] * ry;
            c[k].u = uv[static_cast<std::size_t>(2 * k)];
            c[k].v = uv[static_cast<std::size_t>(2 * k + 1)];
            // The particle's COLOUR, and only that. `Render_SubmitSprites`
            // puts `+36 * 255` in the diffuse's HIGH byte, and neither the
            // additive blend (ONE / ONE) nor the multiply (ZERO / INVSRCCOLOR)
            // reads it; only modes 2/3 (SRCALPHA / INVSRCALPHA) would, and no
            // shipped effect uses them. Folding it into the colour halved
            // every particle in the game.
            c[k].r = p.col[0];
            c[k].g = p.col[1];
            c[k].b = p.col[2];
            c[k].phase = -1.0f;
        }
        auto& v = groups[{spriteModeBits(p.mode) * 16 + p.mode, p.sprite}];
        v.insert(v.end(), {c[0], c[1], c[2], c[0], c[2], c[3]});
    }

    for (auto& [key, corners] : groups) {
        const auto mode = static_cast<std::uint8_t>(key.first & 0xF);
        Batch b;
        b.material = key.second;            // the SPRITE index
        b.blend    = blendOfMode(mode);
        b.cutout   = cutoutOfMode(mode);
        b.start    = g.corners.size();
        b.count    = corners.size();
        g.batches.push_back(b);
        g.corners.insert(g.corners.end(), corners.begin(), corners.end());
        g.cornerMirror.insert(g.cornerMirror.end(), corners.size(), 0);
        g.cornerMesh.insert(g.cornerMesh.end(), corners.size(), -1);
        g.cornerVertex.insert(g.cornerVertex.end(), corners.size(), -1);
        g.cornerDeclared.insert(g.cornerDeclared.end(), corners.size(), -1);
    }
    ++g.revision;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORLD DRAWN: the particles, the visible set, the lights, the shadows, the mirror and the present pass.
// Parts of `PlayState::phaseWorld`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// -> true when the sphere is wholly outside one side plane
bool PlayState::outsideView(const float c[3], float r, bool bodies) {
        if (!(bodies ? sideCullBodies : sideCullSet)) return false;
        for (const auto& pl : sideFr.side)
            if (pl.n[0] * c[0] + pl.n[1] * c[1] + pl.n[2] * c[2] + pl.d > r) return true;
        return false;
    
}

// The particles, the visible set, the lights and the shadows into the frame's draw lists
void PlayState::worldDrawLists() {
    OMK_ZONE("world: draw lists");   // the profiler (todo/debug-tools.md 6)
    auto& session = *session_;
    omk::Renderer& world = *world_;
    // THE PARTICLES. A section C effect names its sprite by an
    // index into the GLOBAL library `aventure.scx` registers, and its
    // texture goes into the pool after the set's and the speaker's -
    // the batch then carries that slot in the bucket key's low six
    // bits, which is the engine's own indexing and not a second
    // mechanism.
    // Each batch carries the SPRITE index as its material; the pool
    // holds every sprite after the set's and the speaker's textures,
    // so a batch's slot is `spriteBase + sprite`.
    int spriteBase = -1;
    for (int k = 0; k < 3; ++k) spriteAnchor[k] = view.cam.at[k];
    spriteAnchorSet = true;
    const bool scriptSprites = !noScriptSprites && session.scene().loaded() && !session.scene().sprites().empty();
    // THE PARTICLES' DEPTH GATE, the original's (`Render_SubmitSprites`,
    // `o3de/particles.h`): near the renderer's own cut, far the clip
    // distance. OFF while a mirror reflects (last frame's word): the engine
    // submits the sprites once a PASS with that pass's camera, and these are
    // built once a frame with the eye's - a particle behind the eye can be in
    // the reflection. `OMK_NO_FX_GATE=1` draws every particle, as before.
    static const bool noFxGate = omk::envSet("OMK_NO_FX_GATE");
    const bool fxGated = !noFxGate && !mirrorLive;
    const float fxNear = fxGated ? omk::kNearCut : -std::numeric_limits<float>::infinity();
    const float fxFar = fxGated && std::isfinite(clipInches) ? static_cast<float>(clipInches)
                                                             : std::numeric_limits<float>::infinity();
    if ((session.scene().effects().count() || !ctlSprites.empty() || !foeSprites.empty() ||
         scriptSprites) && !spriteTab.empty()) {
        omk::particleGeometry(fxGeo, session.scene().effects(),
                              view.cam.eye, view.cam.at, spriteLookup, fxNear, fxFar);
        // The `.CTL` sprites, each on its bone THIS frame (flag 1,
        // "follow the bone every frame" - all shipped records here
        // carry it; the others are placed once and this moves them
        // too, labelled). The frame is `(clock - from) / duration`,
        // as Cef_TickEffects writes it; flag 8's doubled sprite
        // clock is not modelled.
        if (!ctlSprites.empty() && player && drawPlayer) {
            const omk::NodeTracks* pt = player->poseTracks();
            const std::vector<omk::MeshPose> pose = pt
                ? omk::composePose(playerMeshes, *pt, player->poseFrameF(), false)
                : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
            const float* pp = player->pos();
            const float spriteEuler[3] = {player->euler()[0], player->facing(),
                                          player->euler()[2]};
            const float fr = player->channelFrame();
            ctlField.clear();
            for (const auto& c : ctlSprites) {
                const char* want = c.attach < 18 ? kAttachName[c.attach] : "Buste";
                int node = -1;
                for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                    if (std::strstr(playerMeshes[i].name, want)) node = static_cast<int>(i);
                if (node < 0 || static_cast<std::size_t>(node) >= pose.size()) continue;
                const omk::MeshPose& hp = pose[static_cast<std::size_t>(node)];
                const float in[3] = {hp.pos[0] - playerRootXZ[0], hp.pos[1],
                                     hp.pos[2] - playerRootXZ[1]};
                float o[3];
                omk::rotateEuler(spriteEuler, in, o);
                omk::Particle p;
                p.pos[0] = o[0] + pp[0];
                p.pos[1] = o[1] + pp[1] - playerFeet + lastRootDrop;
                p.pos[2] = o[2] + pp[2];
                p.life = c.duration > 0.0f ? c.duration : 1.0f;
                p.frameAge = fr - c.from;
                if (p.frameAge < 0.0f) p.frameAge = 0.0f;
                if (p.frameAge > p.life) p.frameAge = p.life;
                p.age = p.frameAge;
                p.scale = c.scale > 0.0f ? c.scale : 1.0f;
                p.sprite = c.sprite;
                p.mode = 4;                        // Cef_SpawnEffect: +10 = 4, additive
                if (fr < c.from) continue;         // not in its window yet
                ctlField.addParticle(p);
            }
            omk::particleGeometry(ctlGeo, ctlField, view.cam.eye, view.cam.at, spriteLookup, fxNear, fxFar);
            const std::size_t base = fxGeo.corners.size();
            for (omk::Batch b : ctlGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
            fxGeo.corners.insert(fxGeo.corners.end(), ctlGeo.corners.begin(), ctlGeo.corners.end());
            fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), ctlGeo.cornerMirror.begin(), ctlGeo.cornerMirror.end());
            fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), ctlGeo.cornerMesh.begin(), ctlGeo.cornerMesh.end());
            fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), ctlGeo.cornerVertex.begin(), ctlGeo.cornerVertex.end());
            fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), ctlGeo.cornerDeclared.begin(), ctlGeo.cornerDeclared.end());
            ++fxGeo.revision;
        }
        // THE OPPONENT'S, on his bones as DRAWN last frame (`meshAt`,
        // the same frame the bolts' hit test reads), found by the
        // attach table's name - the last match, as the player's.
        if (!foeSprites.empty() && fightRun.active && fightRun.body &&
            fightRun.body->mo && fightRun.foeChannel) {
            const Staged& fb2 = *fightRun.body;
            const auto& ms = fb2.mo->meshes;
            const float fr = fightRun.foeChannel->frame();
            ctlField.clear();
            long placed = 0;
            for (const auto& c : foeSprites) {
                const char* want = c.attach < 18 ? kAttachName[c.attach] : "Buste";
                int node = -1;
                for (std::size_t i = 0; i < ms.size(); ++i)
                    if (std::strstr(ms[i].name, want)) node = static_cast<int>(i);
                if (node < 0 || fb2.meshAt.size() < (static_cast<std::size_t>(node) + 1) * 3) continue;
                if (fr < c.from) continue;
                omk::Particle p;
                for (int k = 0; k < 3; ++k)
                    p.pos[k] = fb2.meshAt[static_cast<std::size_t>(node) * 3 + static_cast<std::size_t>(k)];
                p.life = c.duration > 0.0f ? c.duration : 1.0f;
                p.frameAge = std::clamp(fr - c.from, 0.0f, p.life);
                p.age = p.frameAge;
                p.scale = c.scale > 0.0f ? c.scale : 1.0f;
                p.sprite = c.sprite;
                p.mode = 4;
                ctlField.addParticle(p);
                ++placed;
                // what can actually DRAW: a batch whose sprite has no
                // pool slot is skipped at submission, which is how 482
                // placements once drew nothing (15.16)
                if (spriteSlot.count(c.sprite)) ++foeSpritesPooled;
            }
            if (placed) {
                foeSpritesDrawn += placed;
                omk::particleGeometry(ctlGeo, ctlField, view.cam.eye, view.cam.at, spriteLookup, fxNear, fxFar);
                const std::size_t base = fxGeo.corners.size();
                for (omk::Batch b : ctlGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
                fxGeo.corners.insert(fxGeo.corners.end(), ctlGeo.corners.begin(), ctlGeo.corners.end());
                fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), ctlGeo.cornerMirror.begin(), ctlGeo.cornerMirror.end());
                fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), ctlGeo.cornerMesh.begin(), ctlGeo.cornerMesh.end());
                fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), ctlGeo.cornerVertex.begin(), ctlGeo.cornerVertex.end());
                fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), ctlGeo.cornerDeclared.begin(), ctlGeo.cornerDeclared.end());
                ++fxGeo.revision;
            }
        }
        // THE SCRIPTED SPRITES - `Script_Display3DSprite` and its
        // family (program.h). An instance the scene has linked is
        // drawn every frame from its own fields: frame `+22`, type
        // `+20` (the blend mode), the two scales, the roll. The
        // engine walks the scene's `+36` list after the effects, and
        // `Sprite_SetFrame` hides an instance whose frame is past the
        // sprite's count by writing 0xFFFF, which the clamp here
        // stands in for.
        if (scriptSprites) {
            omk::ParticleField scField;
            for (const auto& kv : session.scene().sprites()) {
                const auto& sp = kv.second;
                const omk::SpriteFrames* spf = sp.id < 0 ? nullptr : spriteTab.framesOf(sp.id);
                if (!sp.linked || !spf) continue;
                const int n = static_cast<int>(spf->frames.size());
                if (sp.frame < 0 || sp.frame >= n) continue;   // 0xFFFF: not drawn
                if (spriteLogged[sp.row] != sp.linkedAt + 1) {
                    spriteLogged[sp.row] = sp.linkedAt + 1;
                    std::printf("sprite: LINKED row %d id %d type %d frame %d/%d scale %.2f/%.2f at %.0f,%.0f,%.0f (scene tick %ld)\n",
                                sp.row, sp.id, sp.type, sp.frame, n, sp.sx, sp.sy,
                                sp.pos[0], sp.pos[1], sp.pos[2], sp.linkedAt);
                }
                omk::Particle p;
                for (int k = 0; k < 3; ++k) p.pos[k] = sp.pos[k];
                p.life = 1.0f;
                p.frame = sp.frame;
                p.scale = sp.sx;
                p.scaleY = sp.sy;
                p.angle = sp.roll;
                p.sprite = sp.id;
                p.mode = static_cast<std::uint8_t>(sp.type & 0xF);
                scField.addParticle(p);
            }
            omk::Geometry scGeo;
            omk::particleGeometry(scGeo, scField, view.cam.eye, view.cam.at, spriteLookup, fxNear, fxFar);
            const std::size_t base = fxGeo.corners.size();
            for (omk::Batch b : scGeo.batches) { b.start += base; fxGeo.batches.push_back(b); }
            fxGeo.corners.insert(fxGeo.corners.end(), scGeo.corners.begin(), scGeo.corners.end());
            fxGeo.cornerMirror.insert(fxGeo.cornerMirror.end(), scGeo.cornerMirror.begin(), scGeo.cornerMirror.end());
            fxGeo.cornerMesh.insert(fxGeo.cornerMesh.end(), scGeo.cornerMesh.begin(), scGeo.cornerMesh.end());
            fxGeo.cornerVertex.insert(fxGeo.cornerVertex.end(), scGeo.cornerVertex.begin(), scGeo.cornerVertex.end());
            fxGeo.cornerDeclared.insert(fxGeo.cornerDeclared.end(), scGeo.cornerDeclared.begin(), scGeo.cornerDeclared.end());
            ++fxGeo.revision;
        }
        // The pool already carries them: it is built above, in one
        // place, with a section per model.
        spriteBase = static_cast<int>(spriteTexBase);
    }
    // ONE bucket order for the set, the speaker and the particles.
    // `Render_FlushBuckets` walks a single 14-bit key ascending and
    // meshes and sprites share it (`Render_SubmitSprites` ORs its
    // mode bits into the same array), so an additive particle draws
    // after every opaque mesh and before every multiply one, whatever
    // was submitted first. Submitting the three geometries in turn
    // put `ttt`'s multiply starburst before `burn`'s additive puffs
    // and the intro's dark ring darkened only black. The state bits
    // are the mesh path's own (render.h: 0x2100 additive, 0x2200
    // multiply, 0x400 cutout); the per-face depth bits 0x80/0x1000
    // are not modelled here, as they are not for the set.
    draws.clear();   // its capacity kept from frame to frame (tier A)
    const auto keyOf = [](omk::Blend bl, bool cutout, std::uint32_t slot) {
        std::uint32_t state = 0;
        if (bl == omk::Blend::Add)      state = 0x2100;
        else if (bl == omk::Blend::Mul) state = 0x2200;
        else if (cutout)                state = 0x400;
        return state | (slot & 0x3Fu);
    };
    // THE SKY, placed and submitted before anything else.
    //
    // `sub_41CF10` sets the node to the camera's x and z and its own
    // y, so the plane slides with you and never approaches; the
    // corners are rebuilt from the loaded ones about node 0's origin,
    // scaled 12.5x. Options row 4 turns it off by UNLINKING the node,
    // which is simply not submitting it.
    //
    // Its bucket state is 0x800 and not computed from the blend flags:
    // `Area_LoadMiscModel` sets mesh flag 0x10000, and 0x10000's line
    // in `Render_SubmitMesh` is an ASSIGNMENT - `state = 0x800` - which
    // wipes everything else. That is also why the sky never picks up
    // the far-bucket bit (`!(key & 0x800)` guards it) and why its fog
    // range is DOUBLED. It clears 0x3000 too, so the sky is opaque
    // whatever the material says.
    if (drawSky && !sky.base.empty() && !sky.geo.batches.empty()) {
        const float sx = view.cam.eye[0], sz = view.cam.eye[2];
        const float sy = sky.origin[1] - kSkyLift;
        static const bool cpuSky = omk::envSet("OMK_CPU_SKY");   // A/B only
        const bool skyGpu = !cpuSky && !cpuBodiesFlag && world.posesBodies() &&
                            sky.rest.cornerMesh.size() == sky.rest.corners.size() &&
                            !sky.rest.corners.empty();
        static int skyPathWas = -1;
        if (skyPathWas != (skyGpu ? 1 : 0)) {
            skyPathWas = skyGpu ? 1 : 0;
            std::printf("frame %ld: the sky %s - %zu corners\n", n,
                        skyGpu ? "MOVED BY THE RENDERER (static, one translation a frame)"
                               : "rewritten on the CPU when the camera moves",
                        sky.base.size());
        }
        if (skyGpu) {
            std::int32_t meshes = 0;
            for (const std::int32_t m : sky.rest.cornerMesh) meshes = std::max(meshes, m + 1);
            sky.affine.assign(12u * static_cast<std::size_t>(meshes), 0.0f);
            for (std::int32_t m = 0; m < meshes; ++m) {
                float* a = sky.affine.data() + 12 * m;
                a[0] = a[5] = a[10] = 1.0f;
                a[3] = sx; a[7] = sy; a[11] = sz;
            }
            for (const auto& b : sky.rest.batches) {
                draws.push_back({0x800u | ((static_cast<std::uint32_t>(b.material) +
                                            static_cast<std::uint32_t>(sky.texBase)) & 0x3Fu),
                                 &sky.rest, b.start, b.count, omk::Blend::Opaque, false});
                draws.back().meshPose = sky.affine.data();
                draws.back().meshPoses = static_cast<std::size_t>(meshes);
            }
        } else {
            // ...and on the CPU only when the camera's x or z CHANGED:
            // the same corners are the same bytes, and a revision left
            // alone is a buffer not sent again
            if (!sky.placed || sky.placedAt[0] != sx || sky.placedAt[1] != sz) {
                for (std::size_t k = 0; k < sky.base.size(); ++k) {
                    const omk::Corner& b0 = sky.base[k];
                    omk::Corner& c = sky.geo.corners[k];
                    c = b0;
                    c.x = sx + (b0.x - sky.origin[0]) * kSkyScale;
                    c.y = sy + (b0.y - sky.origin[1]) * kSkyScale;
                    c.z = sz + (b0.z - sky.origin[2]) * kSkyScale;
                }
                sky.geo.revision = ++worldGeoRev;
                sky.placed = true;
                sky.placedAt[0] = sx;
                sky.placedAt[1] = sz;
            }
            for (const auto& b : sky.geo.batches)
                draws.push_back({0x800u | ((static_cast<std::uint32_t>(b.material) +
                                            static_cast<std::uint32_t>(sky.texBase)) & 0x3Fu),
                                 &sky.geo, b.start, b.count, omk::Blend::Opaque, false});
        }
    }

    // THE VISIBLE SET, and it is the CLIP DISTANCE that sizes it.
    //
    // `sub_48D3B0` walks the scene's meshes and keeps one when
    // `radius + dword_6A2B9C` exceeds its distance from the camera -
    // and `dword_6A2B9C` is the scene's `+340`, which is options row
    // 3 in metres times 39.37 (`platform/settings.h`). So the option
    // is exactly this radius, and at 25 m ("Tres proche") a street
    // ends a few buildings away.
    //
    // ...and then the four SIDE planes, built from the same global at
    // `25_sys.c` 11598 (`outsideView`, above the staged bodies). They
    // were left out until 2026-09-30 because a wrong plane sign
    // deletes the world silently; what guards that now is the test
    // that a correct cull leaves the frame byte-identical
    // (`OMK_NO_SIDECULL=1` for the other side of it).
    const float clipReach = static_cast<float>(clipInches);
    std::size_t runsDrawn = 0, runsCulled = 0, movingDrawn = 0, movingCulled = 0;
    std::size_t hiddenMeshes = 0, hiddenDrawn = 0;
    for (int slot = 0; slot < 2; ++slot) {
        const WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
        // loaded and solid, but not in the RENDER list (state 1)
        if (!w.shown) continue;
        const std::uint32_t texBase = static_cast<std::uint32_t>(worldTexBase[slot]);
        // THE DEPTH TIE, decided once per set and texture base: the
        // set's whole draw order as this loop builds it - batches in
        // order, keyed alike, stable by key like the sort below - with
        // nothing culled (todo/optimization.md step 29)
        if (w.tieBakedGen != worldGen) {
            worldSlots[static_cast<std::size_t>(slot)].tieBakedGen = worldGen;
            std::vector<omk::Draw> order;
            order.reserve(w.geo.batches.size());
            for (const auto& b : w.geo.batches)
                order.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material) + texBase),
                                 &w.geo, b.start, b.count, b.blend, b.cutout});
            std::stable_sort(order.begin(), order.end(),
                             [](const omk::Draw& a, const omk::Draw& b) {
                                 return (a.bucketKey & 0x3FFFu) < (b.bucketKey & 0x3FFFu);
                             });
            world.bakeDepthTie(&w.geo, order);
        }
        if (w.runs.empty()) {          // no per-corner mesh: whole batches
            for (const auto& b : w.geo.batches)
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material) + texBase),
                                 &w.geo, b.start, b.count, b.blend, b.cutout});
            continue;
        }
        // One test per mesh, cached across its runs.
        std::vector<std::uint8_t> vis(w.meshes.size(), 2);   // 2 = untested
        // a mesh drawn from its own geometry (`MovingMesh`) is out of
        // the set's draw - never side-culled, so never bridged over
        for (const auto& kv : w.moving)
            if (kv.first >= 0 && static_cast<std::size_t>(kv.first) < vis.size())
                vis[static_cast<std::size_t>(kv.first)] = 0;
        // ...and a mesh whose flag 2 was set at run time (`sub_436F20` -
        // Astaroth's souls): the drawable mask `flags & 0x800043` skips it
        for (std::size_t mi = 0; mi < w.meshHidden.size() && mi < vis.size(); ++mi)
            if (w.meshHidden[mi]) vis[mi] = 0;
        const auto visible = [&](std::int32_t mi) -> bool {
            if (mi < 0 || static_cast<std::size_t>(mi) >= w.meshes.size()) return true;
            std::uint8_t& v = vis[static_cast<std::size_t>(mi)];
            if (v != 2) return v == 1;
            const omk::Mesh& m = w.meshes[static_cast<std::size_t>(mi)];
            const float dx = m.pos[0] - view.cam.eye[0];
            const float dy = m.pos[1] - view.cam.eye[1];
            const float dz = m.pos[2] - view.cam.eye[2];
            const float reach = m.radius + clipReach;
            // 1 drawn, 0 beyond the distance, 3 outside the side planes
            v = !(reach * reach > dx * dx + dy * dy + dz * dz) ? 0
              : outsideView(m.pos, m.radius, false) ? 3 : 1;
            return v == 1;
        };
        // A run culled by the SIDE PLANES lies wholly outside the view
        // and cannot put a pixel on the screen, so drawing it costs
        // only vertices - and dropping it SPLITS the batch around it,
        // one draw becoming two (Bowie: 175 draws -> 221, on a backend
        // where each draw is CPU in the driver). So a short run of them
        // between two drawn runs of one batch is drawn through. Never
        // one culled by DISTANCE: that one would show what the engine
        // does not.
        const auto sideCulledOnly = [&](std::int32_t mi) {
            if (mi < 0 || static_cast<std::size_t>(mi) >= w.meshes.size()) return false;
            (void)visible(mi);
            return vis[static_cast<std::size_t>(mi)] == 3;
        };
        constexpr std::uint32_t kBridgeCorners = 300;
        // Emit, merging adjacent surviving runs so a fully visible
        // batch still costs one draw.
        std::size_t i = 0;
        while (i < w.runs.size()) {
            if (!visible(w.runs[i].mesh)) { ++runsCulled; ++i; continue; }
            const auto& first = w.runs[i];
            std::uint32_t start = first.start, count = first.count;
            std::size_t j = i + 1;
            std::size_t bridged = 0;
            for (;;) {
                while (j < w.runs.size() && w.runs[j].batch == first.batch &&
                       w.runs[j].start == start + count && visible(w.runs[j].mesh)) {
                    count += w.runs[j].count; ++j;
                }
                // a gap of side-culled runs, short, and a drawn run after it
                std::size_t k = j;
                std::uint32_t gap = 0;
                while (k < w.runs.size() && w.runs[k].batch == first.batch &&
                       w.runs[k].start == start + count + gap && sideCulledOnly(w.runs[k].mesh) &&
                       gap + w.runs[k].count <= kBridgeCorners) {
                    gap += w.runs[k].count; ++k;
                }
                if (k == j || k >= w.runs.size() || w.runs[k].batch != first.batch ||
                    w.runs[k].start != start + count + gap || !visible(w.runs[k].mesh))
                    break;
                count += gap; bridged += k - j; j = k;
            }
            runsDrawn += j - i - bridged;
            runsCulled += bridged;
            const auto& b = w.geo.batches[first.batch];
            draws.push_back({keyOf(b.blend, b.cutout,
                                   static_cast<std::uint32_t>(b.material) + texBase),
                             &w.geo, start, count, b.blend, b.cutout});
            i = j;
        }
        // FLAG 2, as the walk just treated it: how many meshes are hidden
        // and how many of THOSE it drew anyway (`vis` 1) - the consumer's
        // own figure, which a check reads (`engine: astaroth souls`)
        for (std::size_t mi = 0; mi < w.meshHidden.size() && mi < vis.size(); ++mi)
            if (w.meshHidden[mi]) { ++hiddenMeshes; if (vis[mi] == 1) ++hiddenDrawn; }
        // ...and the moving meshes, where they ARE: the clip distance
        // and the side planes about their placed origin
        for (const auto& kv : w.moving) {
            if (kv.first >= 0 && static_cast<std::size_t>(kv.first) < w.meshHidden.size() &&
                w.meshHidden[static_cast<std::size_t>(kv.first)]) continue;   // flag 2
            const WorldSlot::MovingMesh& mm = kv.second;
            const float dx = mm.at[0] - view.cam.eye[0], dy = mm.at[1] - view.cam.eye[1],
                        dz = mm.at[2] - view.cam.eye[2];
            const float reach = mm.reach + clipReach;
            if (!(reach * reach > dx * dx + dy * dy + dz * dz) ||
                outsideView(mm.at, mm.reach, false)) { ++movingCulled; continue; }
            ++movingDrawn;
            for (const auto& b : mm.rest.batches) {
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material) + texBase),
                                 &mm.rest, b.start, b.count, b.blend, b.cutout});
                draws.back().meshPose = mm.affine;
                draws.back().meshPoses = 1;
            }
        }
    }
    {
        static std::size_t hiddenTold = 0, hiddenDrawnTold = 0;
        if (hiddenMeshes != hiddenTold || hiddenDrawn != hiddenDrawnTold) {
            hiddenTold = hiddenMeshes;
            hiddenDrawnTold = hiddenDrawn;
            std::printf("frame %ld: world draw - flag 2: %zu set mesh%s hidden, %zu of them "
                        "drawn\n", n, hiddenMeshes, hiddenMeshes == 1 ? "" : "es", hiddenDrawn);
        }
    }
    if (clipReport) {
        if (unlimitedClip)
            std::printf("clip: unlimited - %zu mesh runs drawn, %zu culled\n",
                        runsDrawn, runsCulled);
        else
            std::printf("clip: %.0f in - %zu mesh runs drawn, %zu culled\n",
                        clipInches, runsDrawn, runsCulled);
        if (movingDrawn + movingCulled)
            std::printf("clip: and %zu moving meshes drawn by the renderer from their "
                        "own corners, %zu culled\n", movingDrawn, movingCulled);
        clipReport = false;
    }
    // A FRAME WHERE THE SET IS CULLED AND THE BODIES ARE NOT draws a
    // character on black, which is what a reader sees as a flicker:
    // the visible-set walk above tests every mesh against the clip
    // distance FROM THE CAMERA EYE, and a staged body is added below
    // with no such test. One line a frame, so a run can be read for
    // the frames where the two disagree.
    std::size_t litBodies = 0;
    for (const auto& up : staged) if (up->drawn && up->mo) ++litBodies;
    if (omk::envSet("OMK_CLIPLOG")) {
        std::printf("  [clip] frame %ld  runs %zu drawn %zu culled  bodies %zu"
                    "  eye %.0f %.0f %.0f\n", n, runsDrawn, runsCulled, litBodies,
                    view.cam.eye[0], view.cam.eye[1], view.cam.eye[2]);
    }
    harnessFlickerNote(runsDrawn, runsCulled, litBodies);
    phSpan["player"] += phaseNow() - player0;
    mark("pedestrians, traffic");
    // ---- THE LIGHTS, per pixel (`todo/enhancements.md` 7) -------
    //
    // The same `.3DO` records the crowd is lit by, handed to the
    // backend instead of applied on the CPU. Nearest first and capped,
    // because a uniform block is finite and a body is reached by a
    // handful: Anekbah ships 155 and its lamps reach 700-900 units.
    // THE SHIMMER's clock, advanced the way `Game_Tick` does it:
    // `clock += 2 * frameDelta`, wrapping at 256, where one frame's
    // delta is 1.0 at 30 Hz (`docs/BOOT.md` 4). 233 set meshes ride
    // it - the far skyline of every city - and it is the GAME's, not
    // an enhancement, so both backends draw it.
    // ...by the DELTA, not the presented frame: `frameSec * 30` is 1.0
    // at 30 and in every `--frames` run, 0.5 at 60 (todo/sixty-fps.md 2)
    shimmerClock += 2.0f * static_cast<float>(frameSec * 30.0);
    while (shimmerClock >= omk::kShimmerWrap) shimmerClock -= omk::kShimmerWrap;
    view.shimmerClock = shimmerClock;
    view.dither = dither;
    const bool litPerPixel = lighting > 0;
    // TWO BASES, and the difference is a property of the models.
    //
    // `1` starts from BLACK, which is the engine's own rule for the
    // bodies it lights: `sub_494E80` writes `instance[+416]` into
    // every runtime vertex colour and every site that sets +416 sets
    // it to 0, and the crowd models ship pure white (all 446 of
    // PSH_FN's vertices are 255,255,255), so there is no baked light
    // in them to lose.
    //
    // `2` ADDS to the baked colour, and that is this port's decision
    // for the bodies the engine never lights. HO1_FN is not a white
    // model - it carries real baked shading - so black-plus-lamps
    // throws away everything the artist put in and leaves him a
    // silhouette wherever no lamp reaches. Stated here because it is
    // the one place row 7 departs from transcription.
    const int litCrowd  = litPerPixel ? 1 : 0;
    const int litStaged = litPerPixel ? 2 : 0;
    view.lights.clear();
    if (litPerPixel) {
        float at[3] = {view.cam.eye[0], view.cam.eye[1], view.cam.eye[2]};
        if (drawPlayer && !playerPosed.corners.empty()) {
            at[0] = playerPosed.corners.front().x;
            at[1] = playerPosed.corners.front().y;
            at[2] = playerPosed.corners.front().z;
        }
        std::vector<std::pair<float, const omk::Light3do*>> near;
        for (const WorldSlot& ws2 : worldSlots)
            for (const auto& l : ws2.lights) {
                const float dx = at[0] - l.pos[0], dy = at[1] - l.pos[1],
                            dz = at[2] - l.pos[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > l.radiusA * l.radiusA) continue;   // out of reach
                if (!(l.radiusA > l.radiusB)) continue;
                near.emplace_back(d2, &l);
            }
        std::sort(near.begin(), near.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        if (near.size() > static_cast<std::size_t>(omk::View::kMaxGpuLights))
            near.resize(static_cast<std::size_t>(omk::View::kMaxGpuLights));
        for (const auto& [d2, l] : near) {
            omk::View::GpuLight g{};
            for (int k = 0; k < 3; ++k) { g.pos[k] = l->pos[k]; g.dir[k] = l->dir[k]; }
            g.radiusA = l->radiusA; g.radiusB = l->radiusB;
            g.colour[0] = static_cast<float>((l->colour >> 16) & 0xFF) / 255.0f;
            g.colour[1] = static_cast<float>((l->colour >> 8) & 0xFF) / 255.0f;
            g.colour[2] = static_cast<float>(l->colour & 0xFF) / 255.0f;
            g.intensity = l->f32;
            view.lights.push_back(g);
        }
        if (!lightsTold) {
            lightsTold = true;
            std::printf("frame %ld: per-pixel lighting - %zu of the set's lights "
                        "reach the player, nearest first\n", n, view.lights.size());
        }
    }
    // ---- THE MAPPED SHADOW's LIGHT (`todo/enhancements.md` 6) --
    //
    // An ENHANCEMENT: nothing in the engine casts a real shadow. What
    // keeps it from being an invented sun is where the direction comes
    // from - the strongest of the SET's own `.3DO` light records
    // reaching the casters, the same table and the same reach and
    // falloff rules `applyLights` uses to light the crowd. With no
    // light in reach nothing is drawn, which is honest: the port does
    // not know where the light is, so it does not guess.
    //
    // Only characters cast. A set is shaded by a colour baked into
    // every vertex and that colour ALREADY contains the artists'
    // shadows (ASSETS 4c), so a map that darkened the set from the set
    // would paint a second shadow over the first.
    const bool castShadows = drawShadows && shadowQuality >= 2;
    view.shadow = omk::View::ShadowLight{};
    if (castShadows) {
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        bool any = false;
        int bodies = 0;
        // ...and only the bodies NEAR THE CAMERA. The slab is one
        // 1024-texel map, so its width is its resolution: bounding
        // every staged body in the city made it 7484 units across -
        // 15 units a texel, which is a shadow the size of a torso.
        // 1200 units is 30 m, past which a character's shadow is
        // sub-pixel anyway. LABELLED as this port's number.
        constexpr float kShadowRange = 1200.0f;
        const auto bound = [&](const omk::Geometry& g) {
            if (g.corners.empty()) return;
            const auto& c0 = g.corners.front();
            const float dx = c0.x - view.cam.eye[0], dy = c0.y - view.cam.eye[1],
                        dz = c0.z - view.cam.eye[2];
            if (dx * dx + dy * dy + dz * dz > kShadowRange * kShadowRange) return;
            for (const auto& c : g.corners) {
                const float p3[3] = {c.x, c.y, c.z};
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], p3[k]);
                    hi[k] = std::max(hi[k], p3[k]);
                }
            }
            ++bodies;
            any = true;
        };
        // A BODY THE RENDERER POSES (GLES) has no world corners here, so it
        // is bounded by its meshes' ORIGINS - each affine's translation, the
        // skeleton's joints - which is what places the slab and nothing
        // finer is needed for that
        const auto boundAffine = [&](const std::vector<float>& a) {
            if (a.size() < 12) return;
            const float dx = a[3] - view.cam.eye[0], dy = a[7] - view.cam.eye[1],
                        dz = a[11] - view.cam.eye[2];
            if (dx * dx + dy * dy + dz * dz > kShadowRange * kShadowRange) return;
            for (std::size_t m = 0; m + 12 <= a.size(); m += 12) {
                const float p3[3] = {a[m + 3], a[m + 7], a[m + 11]};
                for (int k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], p3[k]);
                    hi[k] = std::max(hi[k], p3[k]);
                }
            }
            ++bodies;
            any = true;
        };
        if (drawPlayer && !playerPosed.corners.empty()) bound(playerPosed);
        for (const auto& up : staged)
            if (up->drawn && up->mo) { if (up->gpu) boundAffine(up->affine); else bound(up->posed); }
        for (const auto& up : pedStaged)
            if (up->drawn && up->mo) { if (up->gpu) boundAffine(up->affine); else bound(up->posed); }
        if (any) {
            // ...CENTRED ON THE PLAYER, not on the bodies' midpoint.
            // A street's lamps reach about 700 units and eleven
            // scattered bodies put their midpoint out in the road
            // beyond all of them, so the light picked there was the
            // one distant fill that reaches everywhere - and its
            // shadow lands off the frame. One map, centred on the
            // character you are looking at; a body outside the slab
            // casts nothing, which is stated rather than hidden.
            float centre[3];
            if (drawPlayer && !playerPosed.corners.empty()) {
                float plo[3] = {1e30f, 1e30f, 1e30f}, phi[3] = {-1e30f, -1e30f, -1e30f};
                for (const auto& c : playerPosed.corners) {
                    const float q[3] = {c.x, c.y, c.z};
                    for (int k = 0; k < 3; ++k) {
                        plo[k] = std::min(plo[k], q[k]);
                        phi[k] = std::max(phi[k], q[k]);
                    }
                }
                for (int k = 0; k < 3; ++k) centre[k] = (plo[k] + phi[k]) * 0.5f;
            } else {
                for (int k = 0; k < 3; ++k) centre[k] = (lo[k] + hi[k]) * 0.5f;
            }
            // Enough for a body and the throw of its shadow under a
            // steep light; this port's number, and the resolution is
            // its width (1024 texels over the slab).
            float rad = 220.0f;
            const omk::Light3do* best = nullptr;
            float bestK = 0.0f;
            for (const WorldSlot& ws2 : worldSlots)
                if (!ws2.lights.empty()) {
                    float k = 0.0f;
                    const omk::Light3do* l =
                        omk::strongestLightAt(centre, ws2.lights, &k);
                    if (l && k > bestK) { bestK = k; best = l; }
                }
            if (best) {
                view.shadow.on = true;
                for (int k = 0; k < 3; ++k) {
                    view.shadow.dir[k] = best->dir[k];
                    view.shadow.centre[k] = centre[k];
                }
                view.shadow.radius = rad;
                view.shadow.strength = 0.6f;
                static const bool shLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                if (!shadowLightTold || (shLog && (n % 30) == 0)) {
                    shadowLightTold = true;
                    std::printf("frame %ld: mapped shadows - light '%s' at "
                                "%.0f %.0f %.0f, direction %.2f %.2f %.2f, "
                                "slab radius %.0f over %d bodies, asked at "
                                "%.0f %.0f %.0f\n", n, best->name.c_str(),
                                static_cast<double>(best->pos[0]),
                                static_cast<double>(best->pos[1]),
                                static_cast<double>(best->pos[2]),
                                static_cast<double>(best->dir[0]),
                                static_cast<double>(best->dir[1]),
                                static_cast<double>(best->dir[2]),
                                static_cast<double>(rad), bodies,
                                static_cast<double>(centre[0]),
                                static_cast<double>(centre[1]),
                                static_cast<double>(centre[2]));
                }
            } else if (!shadowLightTold) {
                shadowLightTold = true;
                std::printf("frame %ld: mapped shadows asked for, but no set light "
                            "reaches the cast - nothing drawn (the port does not "
                            "invent a sun)\n", n);
            }
        }
    }
    mark("lights");
    // ---- THE SHADOWS ------------------------------------------
    //
    // `Actors_TickAll` calls `Actor_DrawShadow(detail, actor)` for
    // every live actor and `Sliders_Tick` places the crowd's, both
    // behind option row 5. Built here, after every body has been
    // placed and before the draw list is sorted, because the blobs
    // are FRAME GEOMETRY - the engine writes them straight into the
    // scene's vertex and triangle pools each frame, not into a node.
    //
    // The probe is the walkable soup, which is what the bodies are
    // seated on; the engine's `World_ProbePoint(-1, ...)` runs on the
    // same collision geometry.
    shadowGeo.corners.clear();
    shadowGeo.batches.clear();
    shadowGeo.cornerMirror.clear();
    shadowGeo.cornerMesh.clear();
    shadowGeo.cornerVertex.clear();
    shadowGeo.cornerDeclared.clear();
    // ...and only up to `fitted`: a MAPPED frame replaces the blobs
    // with a real shadow rather than drawing both.
    if (drawShadows && shadowQuality < 2 && shadowModel.loaded && !playerSoup.empty()) {
        // Option row 7. The player and the fight opponent get it
        // whole; every other actor gets it MINUS ONE, so an npc always
        // casts one tier coarser.
        const int detail = shadowDetail;
        // FITTED gathers the triangles under a body ONCE and probes
        // its grids against those - see `o3de/shadow.h`. Gathered per
        // body inside `castBones`.
        const bool fitted = shadowQuality >= 1;
        shadowSpreadMax = 0.0f; shadowSpreadPlayer = 0.0f;
        long blobs = 0, nPlayer = 0, nActor = 0, nCrowd = 0;
        long nPedDrawn = 0, nPedFeet = 0;
        // ...and the same detector over the BONE path: a bone taken
        // off the wrong LOD skeleton is ~240 units from the body it
        // belongs to, so measure every blob against where the body was
        // actually drawn (its corners' horizontal centre) and report
        // the worst. `ref` is null for the player, whose model has one
        // skeleton and cannot meet this.
        const auto castBones = [&](const std::vector<omk::Mesh>& meshes,
                                   const std::vector<float>& at, int lvl,
                                   int root, const omk::Geometry* ref,
                                   const omk::MeshNameIndex* idx) {
            // the index when it is built; the scan otherwise, since an
            // unbuilt index answers -1 for everything and would draw
            // NO shadows silently
            const auto boneMesh = [&](const char* bn) {
                return idx && idx->built() ? idx->find(bn, root)
                                           : omk::findMeshContaining(meshes, bn, root);
            };
            if (at.empty()) return;
            // the body's centre feeds ONE number, `shadowFootOffMax`, which
            // only `OMK_SHADOWLOG` prints - so the pass over every posed
            // corner runs only then (todo/cpu-vs-original.md tier A)
            static const bool footLog = std::getenv("OMK_SHADOWLOG") != nullptr;
            float rx = 0.0f, rz = 0.0f;
            if (footLog && ref && !ref->corners.empty()) {
                float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                for (const auto& c : ref->corners) {
                    lo[0] = std::min(lo[0], c.x); hi[0] = std::max(hi[0], c.x);
                    lo[1] = std::min(lo[1], c.z); hi[1] = std::max(hi[1], c.z);
                }
                rx = (lo[0] + hi[0]) * 0.5f; rz = (lo[1] + hi[1]) * 0.5f;
            }
            const auto& bones = omk::shadowBonesFor(lvl);
            // THE BODY'S OWN PATCH OF GROUND, gathered once. Fitted
            // probes 25 vertices a blob; rescanning the whole set for
            // each would be thousands of city-wide scans a frame.
            omk::TriangleSoup local;
            if (fitted) {
                float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                bool any = false;
                for (int bi : bones) {
                    const int mi = boneMesh(omk::kShadowBones[static_cast<std::size_t>(bi)].bone);
                    if (mi < 0 || static_cast<std::size_t>(mi) * 3 + 2 >= at.size()) continue;
                    const float* q = &at[static_cast<std::size_t>(mi) * 3];
                    lo[0] = std::min(lo[0], q[0]); hi[0] = std::max(hi[0], q[0]);
                    lo[1] = std::min(lo[1], q[2]); hi[1] = std::max(hi[1], q[2]);
                    any = true;
                }
                if (!any) return;
                // inflated by more than the widest blob's half-width
                // (275.59 x 0.07 x 1.5 = 28.9)
                local = omk::soupInBox(playerSoup, playerGrid, lo[0] - 40.0, hi[0] + 40.0,
                                       lo[1] - 40.0, hi[1] + 40.0);
            }
            const omk::TriangleSoup& soup = fitted ? local : playerSoup;
            for (int bi : bones) {
                const auto& sb = omk::kShadowBones[static_cast<std::size_t>(bi)];
                const int mi = boneMesh(sb.bone);
                if (mi < 0 || static_cast<std::size_t>(mi) * 3 + 2 >= at.size()) continue;
                const float* p3 = &at[static_cast<std::size_t>(mi) * 3];
                if (footLog && ref && !ref->corners.empty()) {
                    const float dx = p3[0] - rx, dz = p3[2] - rz;
                    const float d = std::sqrt(dx * dx + dz * dz);
                    if (d > shadowFootOffMax) shadowFootOffMax = d;
                }
                const auto f = fitted
                    ? omk::floorUnder(soup, p3[0], p3[1], p3[2])
                    : omk::floorUnder(playerSoup, playerGrid, p3[0], p3[1], p3[2]);
                if (!f) continue;
                const std::size_t was = shadowGeo.corners.size();
                const float rad = meshes[static_cast<std::size_t>(mi)].radius;
                const bool drew = fitted
                    ? omk::shadowBlobFitted(shadowGeo, shadowModel, p3, rad,
                                            sb.divisor, sb.reach,
                                            static_cast<float>(*f), soup)
                    : omk::shadowBlob(shadowGeo, shadowModel, p3, rad,
                                      sb.divisor, sb.reach, static_cast<float>(*f));
                if (drew) {
                    ++blobs;
                    // THE PROPERTY `fitted` EXISTS FOR: how far a
                    // single blob's own vertices spread vertically.
                    // A classic blob is a flat quad, so this is 0 for
                    // every one of them at every camera and on every
                    // surface; a fitted blob on a slope or a stair
                    // follows the ground and it is not. That is the
                    // measurement, and a pixel count is not - the fan
                    // and the grid interpolate the disc's UVs
                    // differently, so they differ a little even on
                    // perfectly flat ground.
                    float lo = 1e30f, hi = -1e30f;
                    for (std::size_t ci = was; ci < shadowGeo.corners.size(); ++ci) {
                        lo = std::min(lo, shadowGeo.corners[ci].y);
                        hi = std::max(hi, shadowGeo.corners[ci].y);
                    }
                    if (hi - lo > shadowSpreadMax) shadowSpreadMax = hi - lo;
                }
            }
        };
        // `Actors_TickAll` skips the shadow in ACTOR_STATE 7 and
        // 11..14 - the slider mount, the ladder, the two scripted
        // states and the water. Read off the same `if` the mark's
        // enable/disable sits under.
        const auto castsIn = [](omk::ActorState st) {
            const int v = static_cast<int>(st);
            return !(v == 7 || (v > 10 && v <= 14));
        };
        // The player's model has ONE skeleton, so -1 is the whole of it.
        shadowFootOffMax = pedFootOffMax;
        if (drawPlayer && playerMeshAtKnown && player && castsIn(player->state()))
            castBones(playerMeshes, playerMeshAt, detail, -1, nullptr, &playerBoneIdx);
        nPlayer = blobs;
        shadowSpreadPlayer = shadowSpreadMax;   // his alone, before the rest
        for (const auto& up : staged)
            if (up->drawn && up->mo)
                castBones(up->mo->meshes, up->meshAt, detail - 1, up->shadowRoot,
                          up->gpu ? nullptr : &up->posed, &up->mo->boneIdx);
        nActor = blobs - nPlayer;
        // The crowd's, which is the other mechanism entirely.
        for (const auto& up : pedStaged) {
            if (up->drawn) ++nPedDrawn;
            if (up->drawn && up->footKnown) ++nPedFeet;
            if (!up->drawn || !up->footKnown) continue;
            const float mid[3] = {(up->footAt[0][0] + up->footAt[1][0]) * 0.5f,
                                  (up->footAt[0][1] + up->footAt[1][1]) * 0.5f,
                                  (up->footAt[0][2] + up->footAt[1][2]) * 0.5f};
            const auto h = omk::surfaceUnder(playerSoup, playerGrid, mid[0], mid[1], mid[2]);
            if (!h) continue;
            const float nrm[3] = {static_cast<float>(h->n[0]),
                                  static_cast<float>(h->n[1]),
                                  static_cast<float>(h->n[2])};
            const std::size_t before = shadowGeo.corners.size();
            if (omk::shadowFootBlob(shadowGeo, shadowModel, up->footAt[0],
                                    up->footAt[1], static_cast<float>(h->y), nrm)) {
                ++blobs; ++nCrowd;
                static const bool pedLog = std::getenv("OMK_SHADOWLOG") != nullptr;
                if (pedLog && nCrowd == 1 && (n % 60) == 0)
                    std::printf("  [shadow] crowd blob: feet %.0f %.0f %.0f / "
                                "%.0f %.0f %.0f  floor %.0f  corner0 %.0f %.0f %.0f "
                                "extent %.1f\n",
                                static_cast<double>(up->footAt[0][0]),
                                static_cast<double>(up->footAt[0][1]),
                                static_cast<double>(up->footAt[0][2]),
                                static_cast<double>(up->footAt[1][0]),
                                static_cast<double>(up->footAt[1][1]),
                                static_cast<double>(up->footAt[1][2]), h->y,
                                static_cast<double>(shadowGeo.corners[before].x),
                                static_cast<double>(shadowGeo.corners[before].y),
                                static_cast<double>(shadowGeo.corners[before].z),
                                static_cast<double>(
                                    shadowGeo.corners[before].x - up->footAt[0][0]));
            }
        }
        if (!shadowGeo.corners.empty()) {
            // ONE batch: every blob goes through the same material and
            // the same mesh flags 0x5000, so the whole frame's shadow
            // is one submission.
            shadowGeo.batches.push_back({0, false, omk::Blend::Mul, 0,
                                         shadowGeo.corners.size()});
            shadowGeo.revision = ++worldGeoRev;
            draws.push_back({keyOf(omk::Blend::Mul, false,
                                   static_cast<std::uint32_t>(shadowTexBase)),
                             &shadowGeo, 0, shadowGeo.corners.size(),
                             omk::Blend::Mul, false});
        }
        shadowBlobsDrawn = blobs;
        if (!shadowTold && blobs > 0) {
            shadowTold = true;
            std::printf("frame %ld: shadows ON (row 7 detail %d, quality %s) - "
                        "%ld blobs, %zu corners, texture slot %zu\n",
                        n, detail, omk::shadowQualityName(shadowQuality),
                        blobs, shadowGeo.corners.size(), shadowTexBase);
        }
        static const bool shadowLog = std::getenv("OMK_SHADOWLOG") != nullptr;
        if (shadowLog && (n % 30) == 0)
            std::printf("  [shadow] frame %ld: %ld player + %ld actor + %ld crowd"
                        " = %ld blobs (%zu peds staged, %ld drawn, %ld with feet)\n",
                        n, nPlayer, nActor, nCrowd, blobs, pedStaged.size(),
                        nPedDrawn, nPedFeet);
        if (shadowLog && (n % 30) == 0)
            std::printf("  [shadow] worst blob vertical spread %.2f units, "
                        "the player's %.2f (a flat quad is 0 by construction)\n",
                        static_cast<double>(shadowSpreadMax),
                        static_cast<double>(shadowSpreadPlayer));
        if (shadowLog && (n % 30) == 0)
            std::printf("  [shadow] worst foot-to-body offset %.1f units "
                        "(a bone off the wrong LOD skeleton is ~240)\n",
                        static_cast<double>(shadowFootOffMax));
    }
    // EVERY staged body, each with its own model's base - the change
    // issue 41 asks for. One geometry per actor, so two bodies wearing
    // the same model still draw at their own two places.
    for (const auto& up : staged) {
        if (!up->drawn || !up->mo) continue;
        const int base = static_cast<int>(up->mo->texBase);
        if (up->gpu && up->restGeo) {
            for (const auto& b : up->restGeo->batches) {
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material + base)),
                                 up->restGeo, b.start, b.count, b.blend, b.cutout,
                                 litStaged, castShadows});
                draws.back().meshPose = up->affine.data();
                draws.back().meshPoses = up->mo->meshes.size();
                draws.back().vertexLights = up->lightCount ? up->lights.data() : nullptr;
                draws.back().vertexLightCount = up->lightCount;
                draws.back().lightsFromBlack = up->lightsBlack;
                draws.back().lightBase = up->lightBase;
            }
            continue;
        }
        for (const auto& b : up->posed.batches)
            draws.push_back({keyOf(b.blend, b.cutout,
                                   static_cast<std::uint32_t>(b.material + base)),
                             &up->posed, b.start, b.count, b.blend, b.cutout,
                             litStaged, castShadows});
    }
    for (const auto& up : pedStaged) {
        if (!up->drawn || !up->mo) continue;
        const int base = static_cast<int>(up->mo->texBase);
        if (up->gpu && up->restGeo) {
            for (const auto& b : up->restGeo->batches) {
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material + base)),
                                 up->restGeo, b.start, b.count, b.blend, b.cutout,
                                 litCrowd, castShadows});
                omk::Draw& dr = draws.back();
                dr.meshPose = up->affine.data();
                dr.meshPoses = up->mo->meshes.size();
                dr.vertexLights = up->lightCount ? up->lights.data() : nullptr;
                dr.vertexLightCount = up->lightCount;
                dr.lightsFromBlack = up->lightsBlack;
                dr.lightBase = up->lightBase;
            }
            continue;
        }
        for (const auto& b : up->posed.batches)
            draws.push_back({keyOf(b.blend, b.cutout,
                                   static_cast<std::uint32_t>(b.material + base)),
                             &up->posed, b.start, b.count, b.blend, b.cutout,
                             litCrowd, castShadows});
    }
    for (const auto& up : vehStaged) {
        if (!up->drawn || !up->mo) continue;
        const int base = static_cast<int>(up->mo->texBase);
        if (up->gpu) {
            for (const auto& b : up->atRest->batches) {
                draws.push_back({keyOf(b.blend, b.cutout,
                                       static_cast<std::uint32_t>(b.material + base)),
                                 up->atRest, b.start, b.count, b.blend, b.cutout,
                                 litCrowd, castShadows});
                draws.back().meshPose = up->affine.data();
                draws.back().meshPoses = up->mo->meshes.size();
            }
            continue;
        }
        for (const auto& b : up->posed.batches)
            draws.push_back({keyOf(b.blend, b.cutout,
                                   static_cast<std::uint32_t>(b.material + base)),
                             &up->posed, b.start, b.count, b.blend, b.cutout,
                             litCrowd, castShadows});
    }
    // which path drew him, said when it changes - and counted, for the
    // runs that compare the two
    {
        static int pathWas = -1;
        static long gpuFrames = 0, cpuFrames = 0;
        const int path = !drawPlayer ? 0 : playerGpu ? 1 : playerOffView ? 2 : 3;
        gpuFrames += path == 1;
        cpuFrames += path == 3;
        if (path != pathWas) {
            pathWas = path;
            static const char* const names[4] = {
                "not drawn", "posed by the RENDERER (his rest, one affine a mesh)",
                "outside the view, not drawn", "posed on the CPU"};
            std::printf("frame %ld: the player %s - %zu meshes, %zu corners; so far %ld "
                        "frames by the renderer, %ld on the CPU\n", n, names[path],
                        playerMeshes.size(), playerRest.corners.size(), gpuFrames, cpuFrames);
        }
    }
    if (drawPlayer && playerGpu) {
        for (const auto& b : playerRest.batches) {
            draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                       b.material + static_cast<int>(playerTexBase))),
                             &playerRest, b.start, b.count, b.blend, b.cutout,
                             litStaged, castShadows});
            draws.back().meshPose = playerAffine.data();
            draws.back().meshPoses = playerMeshes.size();
            draws.back().vertexLights = playerLightCount ? playerLights.data() : nullptr;
            draws.back().vertexLightCount = playerLightCount;
            draws.back().lightsFromBlack = playerLightsBlack;
            draws.back().lightBase = playerLightBase;
        }
    } else if (drawPlayer && !playerOffView)
        for (const auto& b : playerPosed.batches)
            draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                       b.material + static_cast<int>(playerTexBase))),
                             &playerPosed, b.start, b.count, b.blend, b.cutout,
                             litStaged, castShadows});
    // FIRST PERSON: only the meshes the hide spared - flag 0x200000,
    // the left arm - cut out of the posed body batch by batch.
    static omk::Geometry playerArm;
    if (drawArm) {
        playerArm.corners.clear();
        playerArm.batches.clear();
        const std::vector<std::int32_t>& cm =
            playerPosed.cornerMesh.size() == playerPosed.corners.size()
                ? playerPosed.cornerMesh : playerRest.cornerMesh;
        if (cm.size() == playerPosed.corners.size()) {
            for (const auto& b : playerPosed.batches) {
                const std::size_t base = playerArm.corners.size();
                for (std::size_t c = b.start; c < b.start + b.count; ++c) {
                    const std::int32_t mi = cm[c];
                    if (mi < 0 || static_cast<std::size_t>(mi) >= playerMeshes.size()) continue;
                    if (!(static_cast<std::uint32_t>(playerMeshes[static_cast<std::size_t>(mi)].flags)
                          & 0x200000u)) continue;
                    playerArm.corners.push_back(playerPosed.corners[c]);
                }
                const std::size_t cnt = playerArm.corners.size() - base;
                if (!cnt) continue;
                omk::Batch nb = b;
                nb.start = base;
                nb.count = cnt;
                playerArm.batches.push_back(nb);
            }
        }
        playerArm.revision = ++worldGeoRev;
        for (const auto& b : playerArm.batches)
            draws.push_back({keyOf(b.blend, b.cutout, static_cast<std::uint32_t>(
                                       b.material + static_cast<int>(playerTexBase))),
                             &playerArm, b.start, b.count, b.blend, b.cutout,
                             litStaged, false});
        static long armTold = -1;
        if (armTold < 0) {
            armTold = n;
            std::printf("frame %ld: first person - the arm the hide spares: %zu corners "
                        "of %zu, %zu batches (meshes flagged 0x200000)\n", n,
                        playerArm.corners.size(), playerPosed.corners.size(),
                        playerArm.batches.size());
        }
    }
    // The props, each batch through its own model's pool section.
    for (std::size_t bi = 0; bi < propGeo.batches.size(); ++bi) {
        const auto& b = propGeo.batches[bi];
        const PropModel* owner = bi < propBatchOwner.size() ? propBatchOwner[bi] : nullptr;
        if (!owner) continue;
        draws.push_back({keyOf(b.blend, b.cutout,
                               static_cast<std::uint32_t>(b.material +
                                   static_cast<int>(owner->texBase))),
                         &propGeo, b.start, b.count, b.blend, b.cutout});
    }
    if (spriteBase >= 0)
        for (const auto& b : fxGeo.batches) {
            // `b.material` is the sprite's ID; the pool is packed, so
            // it has to be looked up rather than added to the base
            const auto slIt = spriteSlot.find(b.material);
            const int sl = slIt == spriteSlot.end() ? -1 : slIt->second;
            if (sl < 0) continue;      // a sprite with no texture: not drawn
            draws.push_back({keyOf(b.blend, b.cutout,
                                   static_cast<std::uint32_t>(spriteBase + sl)),
                             &fxGeo, b.start, b.count, b.blend, b.cutout});
        }
    std::stable_sort(draws.begin(), draws.end(),
                     [](const omk::Draw& a, const omk::Draw& b) {
                         return (a.bucketKey & 0x3FFFu) < (b.bucketKey & 0x3FFFu);
                     });
    mark("shadows");
}

// The mirror and the present pass
void PlayState::worldMirror() {
    OMK_ZONE("world: mirror");   // the profiler (todo/debug-tools.md 6)
    auto& comp = *comp_;
    auto& session = *session_;
    omk::Renderer& world = *world_;
    // ---- AND THE MIRROR, which until now only `--scene` ever got.
    //
    // `drawWithMirror` has been on the renderer boundary since
    // 2026-09-01 and the free-fly viewer was its only caller, so a
    // mirror reflected there and was a flat blended pane in adventure
    // mode and in every cutscene - which is where a player meets one.
    // A reader: *the mirror in the chamber is displayed with
    // transparency instead of reflecting.* The plane was already
    // being read per set (`w.mirror`) and used for nothing but a word
    // in a log line.
    //
    // The engine keeps a SINGLE global (`dword_534F48`), so at most
    // one mirror is live at a time; the shown slot's is the one.
    static const bool noMirror = std::getenv("OMK_NO_MIRROR") != nullptr;
    const omk::MirrorPlane& wmp =
        worldSlots[static_cast<std::size_t>(session.shownSlot() & 1)].mirror;
    {
        // `OMK_CAMLOG=1`: the frame's camera as DRAWN, every frame,
        // after every writer above. A creep of a hundredth of a unit
        // is invisible in any still and is what makes a point-sampled
        // texture twinkle (todo/omk-play 88).
        static const bool camLog = [] { const char* e = std::getenv("OMK_CAMLOG"); return e && *e == '1'; }();
        if (camLog)
            std::fprintf(stderr, "[cam] frame %ld eye %.4f %.4f %.4f at %.4f %.4f %.4f fov %.3f\n",
                         n, view.cam.eye[0], view.cam.eye[1], view.cam.eye[2],
                         view.cam.at[0], view.cam.at[1], view.cam.at[2], view.cam.hfovDeg);
    }
    mark("shadows, hud models");
    const auto mst = omk::drawWithMirror(world, draws, view,
                                         noMirror ? omk::MirrorPlane{} : wmp);
    mark("world begin..end (submit, GL)");
    if (mst.active != mirrorLive || (mst.maskPixels > 0 && !mirrorSeen)) {
        mirrorLive = mst.active;
        if (mst.maskPixels > 0) mirrorSeen = true;
        std::printf("frame %ld: the set's mirror is %s%s (%ld px, camera "
                    "%.0f in front)\n", n,
                    mst.active ? "REFLECTING" : "out of view - the camera is behind it",
                    mst.native ? " [native: the two passes]" : "",
                    mst.maskPixels, static_cast<double>(mst.distance));
    }
    // The backend drew the picture into the top-left `vw x vh`; place
    // it, leaving the bands as the black `fb` was cleared to.
    // THE PRESENT PASS (todo/optimization.md step 4b). On a frame that
    // draws NOTHING over the 3D, the readback below, its dither and the
    // upload in `present` only reproduce the attachment's own pixels,
    // so the frame is dithered and presented on the GPU instead
    // (`present.frag`). Everything that draws over the picture or reads
    // `fb` afterwards is excluded here by the test that gates it further
    // down: a screen, the shoot HUD, a media bitmap or line, a
    // conversation, either screen fade, the flicker and clip logs, the
    // snapshots and the final `--dump`. `OMK_NO_GPU_PRESENT=1` turns it
    // off; `OMK_VERIFY_GPU_PRESENT=1` composes the CPU frame as well and
    // compares the two - which is also how a gate missing from this list
    // would show.
    // only a build with a GPU window decides it (`playgpu_<backend>.cpp`)
    if (gpuWindowBuild()) {
        static const bool noGpuPresent = std::getenv("OMK_NO_GPU_PRESENT") != nullptr;
        const bool lastDumped = !dump.empty() && frames && n + 1 >= frames;
        // THE GLES WINDOW TAKES THE SAME PATH (`todo/vita-port.md` G6
        // step 1, 2026-09-18). A console log put 90 of a cutscene
        // frame's 106 ms in exactly the round trip this skips -
        // glReadPixels 36, the 888 -> 565 dither 35, the upload 16 -
        // against 9 ms for the game itself. `GlesRenderer::presentWorld`
        // dithers on the GPU as `present.frag` does on Vulkan.
        const bool onVulkan = gpuWorldOnWindow();   // the world is the window's renderer
        // the gates in order; the first that holds names why the frame
        // stays on the CPU path (`OMK_GPU_PRESENT_STATS` counts them)
        const char* keep = nullptr;
        if (noGpuPresent) keep = "off";
        else if (!onVulkan) keep = "not vulkan";
        // an open screen is a HARD gate only where something reads the
        // picture by a law the overlay cannot carry (G6 step 4): the
        // viewport item (the world goes INTO the screen), SAVE GAME
        // (the slot's thumbnail is taken from `fb` itself) and a panel
        // with the monitors' INTERFERENCE (row shifts and an OR mask).
        else if (vpItem || (walk && screenReadsPicture)) keep = "screen";
        else if (mst.active && !mst.native) keep = "cpu mirror";
        else if (!flickerDir.empty() || !snapsDir.empty() || omk::envSet("OMK_CLIPLOG")) keep = "instrument";
        else if (lastDumped) keep = "dump";
        // THE SOFT GATES, last: what these three draw goes OVER the
        // picture without reading it (a full-screen bitmap, the text's
        // opaque ramp) or reads it by one of two fixed laws (the
        // dialogue boxes). On the GLES window such a frame is composed
        // over a KEY and blended on the GPU (`presentOverlay`, G6 step
        // 2) - no readback. `OMK_NO_OVERLAY=1` turns that off.
        // a fade is a gate only while it would CHANGE a pixel: the black
        // one is `running()` for whole scenes with both bands at 255,
        // which held every frame of play off the direct path
        else if (session.colourFade().running() && session.colourFade().weight() > 0.0f) { keep = "colour fade"; softGate = true; }
        else if (session.blackFade().running() &&
                 (session.blackFade().bandGrey(false) < 255 || session.blackFade().bandGrey(true) < 255)) { keep = "black fade"; softGate = true; }
        else if (mediaBmp.w > 0 && mediaBmp.h > 0) { keep = "media bitmap"; softGate = true; }
        else if (mediaTextFrames > 0) { keep = "media line"; softGate = true; }
        else if (session.dialogOpen()) { keep = "conversation"; softGate = true; }
        // G6 step 3: both gauges are `Hud_DrawBar`, whose only reads of
        // the picture are `fillQuadD3d`'s three blends - affine, so they
        // run on the overlay's planes. SOFT, and therefore after every
        // hard gate: a soft gate that answered first would hide a dump.
        // ...and the FIGHT HUD, under the same test that draws it: the
        // gauges go into `fb` every melee frame outside a KO replay. It
        // was missing from this list, so on the Vulkan window the gauges
        // showed only while another gate (the fight's opening fade) held
        // the frame on the CPU - a reader: *"The health disappear after
        // some time (only the stats should disappear)"*.
        else if (fightRun.active && fightRun.fight && fightRun.fight->koCounter() == 0 &&
                 !omk::envSet("OMK_NOUI")) { keep = "fight hud"; softGate = true; }
        // ...and the BREATH gauge, under the test that draws it - the
        // same GPU-present gap the fight's gauges fell into
        else if (player && player->breathLeftMs() >= 0.0 &&
                 !omk::envSet("OMK_NOUI")) { keep = "breath gauge"; softGate = true; }
        // G6 step 4: the SHOOT HUD. Its readers are the composer's alpha
        // fill and 50% quad, the gauge's and the radar's `fillQuadD3d` -
        // all on the planes; the turning models, the crosshair and the
        // text write opaque pixels.
        else if (shootMode && hudWalk) { keep = "shoot hud"; softGate = true; }
        // ...and every other OPEN SCREEN: the composer's readers are the
        // alpha fill (the panel dim among them) and the 50% quad
        else if (walk) { keep = "open screen"; softGate = true; }
        gpuOverlayDecision(keep);   // the GLES window's overlay, for a soft gate
        gpuFrame = keep == nullptr;
        gpuKeep = keep ? keep : "";
        gpuVy = view.vy;
        gpuVh = view.vh;
    }
    if (overlayFrame) {
        // the world's rows are the KEY: "the GPU's picture shows here"
        g_ov.begin(fb.w, fb.h);
        for (int y = 0; y < view.vh; ++y) {
            const int dy = view.vy + y;
            if (dy < 0 || dy >= fb.h) continue;
            std::fill(fb.px.begin() + static_cast<long>(dy) * fb.w,
                      fb.px.begin() + static_cast<long>(dy + 1) * fb.w,
                      std::uint16_t(0xF81F));
        }
    } else if (!gpuFrame || verifyGpuPresent) {
    phRb0 = phaseNow();
    mark("world end, submit");
    const omk::Surface& pic = world.readback();
    phRb1 = phaseNow();
    if (vpItem && pic.w == fb.w) {
        // The picture is the viewport item's; the composer places it
        // at the item's layer, not the frame.
        view3dPic = omk::Surface(view.vw, view.vh, 0);
        for (int y = 0; y < view.vh && y < pic.h; ++y)
            std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                      pic.px.begin() + static_cast<long>(y) * pic.w + view.vw,
                      view3dPic.px.begin() + static_cast<long>(y) * view.vw);
        comp.attachView3D(&view3dPic);
    } else if (pic.w == fb.w) {
        for (int y = 0; y < view.vh && y < pic.h; ++y) {
            const int dy = view.vy + y;
            if (dy < 0 || dy >= fb.h) continue;
            std::copy(pic.px.begin() + static_cast<long>(y) * pic.w,
                      pic.px.begin() + static_cast<long>(y + 1) * pic.w,
                      fb.px.begin() + static_cast<long>(dy) * fb.w);
        }
    }
    }   // the CPU composite
    ++worldFrames;
}

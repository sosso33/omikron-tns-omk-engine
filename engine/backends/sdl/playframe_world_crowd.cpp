// SPDX-License-Identifier: GPL-3.0-or-later
// THE CROWD: the pedestrians, the vehicles, the player.
// Parts of `PlayState::phaseWorld`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// The pedestrians, the vehicles, the player
void PlayState::worldCrowd() {
    OMK_ZONE("world: crowd");   // the profiler (todo/debug-tools.md 6)
    const auto& fs = *fs_;
    auto& session = *session_;
    omk::Renderer& world = *world_;
    // ---- THE PEDESTRIANS ---------------------------------------
    pedDrawn = pedLive = pedInAction = pedIdle = pedOffView = 0;
    pedLit = 0;
    vehDrawn = vehLive = vehStopped = 0;
    {
        const auto& pd = session.sliders();
        const auto& rs = session.residentSlot(session.activeSlot());
        // `Area_TickLoad` case 7 loads `ANIMS\<+124>.ANI` for the area
        // whether or not it has a slider circuit - the crowd is only
        // one of its consumers, and a shoot-mode character with no
        // `.CTL` is another. So this no longer waits on `pd.loaded()`.
        if (!rs.ani.empty() && rs.ani != pedAniName) {
            pedAniName = rs.ani;
            pedAni = fs.read("ANIMS/" + rs.ani + ".ANI");
            pedTracks.clear();
            pedLodTracks.clear();
            // ...and the shoot groups' clip lists, which are DESCRIPTORS into
            // the library just replaced (`shootClipBySlot`)
            shootClips.clear();
            ++pedCacheGen;
            std::printf("frame %ld: crowd library - ANIMS/%s.ANI (%zu bytes), "
                        "the track caches cleared (generation %ld)\n", n, rs.ani.c_str(),
                        pedAni.size(), pedCacheGen);
        }
        const auto& ws = pd.movers();
        if (pedStaged.size() != ws.size()) {
            pedStaged.clear();
            for (std::size_t i = 0; i < ws.size(); ++i) pedStaged.push_back(std::make_unique<PedStaged>());
            pedTracks.clear();
            pedLodTracks.clear();
            ++pedCacheGen;
        }
        // THE REACH is the CLIP DISTANCE, as for every other instance:
        // `sub_48D7F0` rejects one only when it lies at or past
        // `dword_6A2B9C` plus its model root's `+88` radius, or outside
        // the four side planes, and the LOD chain's LAST level holds past
        // its distance (40 m) - nothing in `Sliders_Tick` hides a far
        // walker (read 2026-10-07). Until then this drew no walker past
        // `kLodDistances[3]`, and a reader saw the crowd end at 40 m while
        // the street went on behind it.
        const float reach = static_cast<float>(clipInches);
        int pedPastLod = 0;
        // the view axis, for the LOD's depth (`sub_48D7F0`'s is the
        // camera matrix's third row: the depth along it)
        float viewFwd[3] = {view.cam.at[0] - view.cam.eye[0], view.cam.at[1] - view.cam.eye[1],
                            view.cam.at[2] - view.cam.eye[2]};
        {
            const float l = std::sqrt(viewFwd[0] * viewFwd[0] + viewFwd[1] * viewFwd[1] +
                                      viewFwd[2] * viewFwd[2]);
            if (l > 0.0f) for (float& c : viewFwd) c /= l;
        }
        // `OMK_NO_PED_LOD=1`: every walker on its largest skeleton, as
        // before - for laying the two side by side
        static const bool noPedLod = std::getenv("OMK_NO_PED_LOD") != nullptr;
        // `OMK_PED_LOD_MAX=n`: no level past n - at 0 the skeleton mask
        // is on and every walker on level 0, which must draw exactly
        // what `OMK_NO_PED_LOD` draws
        static const int pedLodMax = std::getenv("OMK_PED_LOD_MAX") ? std::atoi(std::getenv("OMK_PED_LOD_MAX")) : 3;
        pedFootOffMax = 0.0f;
        pedJobs.clear();
        const double pedSerial0 = phaseNow();
        // THE CROWD IS DRAWN WITH ITS STREET (docs/STREET_LIFE.md, read
        // 2026-10-07). Every instance of the circuit is linked into the
        // street decor's own scene (`Slider_Init`'s `dword_8F5E34`, the slot's
        // +4), `sub_48D7F0` draws the list of the scene it is handed, and
        // `sub_479C20` hands it only the scenes in the render chain - which a
        // hidden decor (state 1, `sub_419A90`) is not. `Sliders_Tick` has no
        // such test: the crowd goes on WALKING indoors, and only its drawing
        // stops. The port drew it whatever the slot (the Quest: 44 walkers
        // inside Qalisar's temple, seen through a door's gap).
        const bool circuitShown = session.trafficSlot() >= 0 &&
                                  session.slotShown(session.trafficSlot());
        {
            static int shownWas = -1;
            if (session.sliders().loaded() && shownWas != (circuitShown ? 1 : 0)) {
                shownWas = circuitShown ? 1 : 0;
                std::printf("frame %ld: crowd library - the circuit's slot %d is %s: its walkers and "
                            "traffic %s\n", n, session.trafficSlot(), circuitShown ? "SHOWN" : "HIDDEN",
                            circuitShown ? "drawn" : "not drawn (they keep moving)");
            }
        }
        int pedRebound = 0;   // walkers whose clip pointer survived a library change
        int pedModelSwapped = 0;   // walkers whose slot was drawn last as ANOTHER model
        for (std::size_t i = 0; i < ws.size(); ++i) {
            const auto& w = ws[i];
            PedStaged& p = *pedStaged[i];
            p.drawn = false;
            if (!w.live || !w.clip || pedAni.empty()) continue;
            ++pedLive;
            if (w.flags & 0x80u) ++pedInAction;
            if (w.flags & 0x100u) ++pedIdle;
            if (!circuitShown) continue;   // its street is out of the render chain
            const float dx = w.body[0] - view.cam.eye[0], dy = w.body[1] - view.cam.eye[1],
                        dz = w.body[2] - view.cam.eye[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > reach * reach * 4.0f) continue;   // far past any root radius: skip the model
            // THE WALKER'S OWN MODEL, looked up every frame - a map lookup when
            // it is resident. It was taken ONCE (`if (!p.mo)`), and this list
            // is rebuilt only when the number of movers changes: a city
            // reloads its circuit on every entry and refills to the same cap
            // (Anekbah's 200), so after any interior each slot kept its LAST
            // occupant's model - the wrong body on a new walker - and, once
            // the per-frame eviction let that model go because no live
            // walker wore it any more, a pointer to a freed one (the Quest,
            // 2026-10-07: T-posed npcs beside animated ones after an
            // interior). The caches below follow `p.mo` (`cacheMo`).
            {
                CharModel* const was = p.mo;
                p.mo = charModelFor(w.model);
                if (was && p.mo && was != p.mo && p.cacheMo == was) ++pedModelSwapped;
            }
            if (!p.mo || !p.mo->ready) continue;
            {
                const float r = p.mo->root >= 0 && static_cast<std::size_t>(p.mo->root) < p.mo->meshes.size()
                                    ? p.mo->meshes[static_cast<std::size_t>(p.mo->root)].radius : 0.0f;
                if ((r + reach) * (r + reach) <= d2) continue;   // `sub_48D7F0`'s reach test
            }
            // outside the view: `sub_48D7F0` skips the instance whole
            // on its model root's radius at its position
            if (p.mo->root >= 0 && static_cast<std::size_t>(p.mo->root) < p.mo->meshes.size() &&
                outsideView(w.body, p.mo->meshes[static_cast<std::size_t>(p.mo->root)].radius, true)) {
                ++pedOffView;
                continue;
            }
            // REBOUND WHEN THE LIBRARY CHANGES, not only when the clip
            // pointer does. `p.tracks` points INTO `pedTracks`, which a new
            // library clears; the walkers' clips are rebuilt at the same
            // time, and a new clip at the freed one's address left the
            // pointer test equal - the walker drawn from freed tracks. The
            // Quest, 2026-10-07: back in Anekbah from an interior, "all npc
            // in the streets are T-posed".
            if (w.clip != p.clipWas || p.cacheGen != pedCacheGen || p.cacheMo != p.mo) {
                if (w.clip == p.clipWas && p.cacheGen != pedCacheGen) ++pedRebound;
                p.clipWas = w.clip;
                p.tracks = pedTracksFor(w.sex, *w.clip, p.mo->meshes, w.model);
            }
            if (p.cacheGen != pedCacheGen || p.cacheMo != p.mo || p.cacheTracks != p.tracks ||
                p.cacheModel != w.model) {
                p.cacheGen = pedCacheGen;
                p.cacheMo = p.mo;
                p.cacheTracks = p.tracks;
                p.cacheModel = w.model;
                p.cacheRoot = p.tracks ? skeletonRootOf(*p.mo, *p.tracks) : p.mo->root;
                p.cacheRest = &lodRestFor(w.model, *p.mo, p.cacheRoot);
                p.lodFilled = 0;
                for (int f = 0; f < 2; ++f) {
                    const char* bone = f == 0 ? "Piedg" : "Piedd";
                    p.cacheFoot[f] = p.mo->boneIdx.built() ? p.mo->boneIdx.find(bone, p.cacheRoot)
                                                           : omk::findMeshContaining(p.mo->meshes, bone, p.cacheRoot);
                }
            }
            // THE LEVEL, `sub_48D7F0`'s walk of the chain: start one
            // level down when row 7's detail is 0, then step on while
            // the VIEW DEPTH is at or past the level's distance; the
            // last level holds past 40 m. Only for a model whose
            // chain the index rule holds for, and whose tracks were
            // bound on the chain's first (largest) skeleton.
            PedJob job;
            job.i = i;
            job.tracks = p.tracks;
            job.rest = p.cacheRest;
            job.lodRoot = p.cacheRoot;
            job.foot[0] = p.cacheFoot[0]; job.foot[1] = p.cacheFoot[1];
            const int nL = noPedLod ? 0 : lodChainOf(*p.mo);
            if (nL > 1 && p.tracks && p.cacheRoot == p.mo->lodRootAt[0]) {
                const float depth = (w.body[0] - view.cam.eye[0]) * viewFwd[0] +
                                    (w.body[1] - view.cam.eye[1]) * viewFwd[1] +
                                    (w.body[2] - view.cam.eye[2]) * viewFwd[2];
                int level = std::min(shadowDetail <= 0 ? 1 : 0, nL - 1);
                while (level + 1 < nL && depth >= omk::kLodDistances[level]) ++level;
                level = std::max(0, std::min(level, pedLodMax));
                job.level = level;
                job.only = p.mo->lodMask[level].data();
                if (level > 0) {
                    if (!(p.lodFilled & (1u << level))) {
                        p.lodFilled |= static_cast<std::uint8_t>(1u << level);
                        const int rk = p.mo->lodRootAt[level];
                        p.lodRestL[level] = &lodRestFor(w.model, *p.mo, rk);
                        for (int f = 0; f < 2; ++f) {
                            const char* bone = f == 0 ? "Piedg" : "Piedd";
                            p.lodFoot[level][f] = p.mo->boneIdx.built() ? p.mo->boneIdx.find(bone, rk)
                                                : omk::findMeshContaining(p.mo->meshes, bone, rk);
                        }
                    }
                    job.tracks = lodTracksFor(p.tracks, level, p.mo->lodCount);
                    job.rest = p.lodRestL[level];
                    job.lodRoot = p.mo->lodRootAt[level];
                    job.foot[0] = p.lodFoot[level][0]; job.foot[1] = p.lodFoot[level][1];
                }
            }
            // `floor(clock) - 1` as before, with the clock's FRACTION
            // kept for enhancement 12: `composePose` floors it when
            // smoothing is off, so the pose is the same key
            float frame = static_cast<float>(w.clock) - 1.0f;
            if (frame < 0.0f) frame = 0.0f;
            if (p.tracks && frame >= static_cast<float>(p.tracks->frames - 1))
                frame = static_cast<float>(p.tracks->frames - 1);
            job.frame = frame;
            pedJobs.push_back(job);
        }
        phSpan["ped serial"] += phaseNow() - pedSerial0;
        if (pedModelSwapped)
            std::printf("frame %ld: crowd library - %d walker(s) whose slot was last drawn as another "
                        "model, re-resolved to their own\n", n, pedModelSwapped);
        if (pedRebound)
            std::printf("frame %ld: crowd library - %d walker(s) rebound after the library change "
                        "though their clip pointer was unchanged (their tracks were the cleared "
                        "cache's)\n", n, pedRebound);
        // ---- THE BODIES, which may run on several cores -----------
        //
        // The pass above is the SERIAL half and it is serial for a
        // reason: it resolves the shared caches (`charModelFor` loads
        // a model, `pedTracksFor` binds a clip's tracks,
        // `lodRestFor` cuts and keeps a rest geometry) and counts the
        // crowd. Everything below writes only its OWN walker's
        // `PedStaged` and reads the rest, so the chunks are disjoint
        // and `omk::Threads`' contract holds: no ordering, no
        // reduction, bit-identical whatever the split
        // (`platform/threads.h`; todo/vita-port.md P4).
        //
        // What is deliberately NOT in here: the geometry REVISION and
        // the counters, which are assigned in index order after the
        // pass so that a threaded frame numbers its buffers exactly as
        // a serial one does.
        const bool gpuPoseOn = !cpuBodiesFlag && world.posesBodies();
        const int gpuMaxLights = world.maxVertexLights();
        // THE GREY A LIT WALKER STARTS FROM: the scene's `+416`, which
        // `Read3DO_Init` sets from the set's ambient (desc+184 x 255) - not
        // 0, which is what this file read until 2026-10-06 (todo/drift-audit.md
        // L1). The active slot's scene, the one the walkers are linked to.
        const float crowdBase = static_cast<float>(std::clamp(
            activeAmbientGrey, 0, 255)) / 255.0f;
        {
            static bool told = false;
            if (!told) {
                told = true;
                std::printf("bodies: the walkers are posed and lit %s\n",
                            gpuPoseOn ? "by the RENDERER (one affine a mesh; "
                                        "todo/gpu-skinning.md)"
                                      : "on the CPU");
            }
        }
        const auto pedBodies = [&](std::size_t from, std::size_t to) {
            for (std::size_t k = from; k < to; ++k) {
                PedJob& j = pedJobs[k];
                const auto& w = ws[j.i];
                PedStaged& p = *pedStaged[j.i];
                const omk::Geometry& rest = *j.rest;
                const float frame = j.frame;
                // The per-body times are the JOB's own, summed in index
                // order after the pass: `spanned` writes a shared map and
                // this body may be running on another thread.
                const double tc0 = phaseNow();
                std::vector<omk::MeshPose>& pose = p.pose;
                if (j.tracks) omk::composePose(p.mo->meshes, *j.tracks, frame, false, pose, j.only);
                else omk::composePose(p.mo->meshes, omk::NodeTracks{}, 0, false, pose, j.only);
                const double tc1 = phaseNow();
                // THE GPU PATH (todo/gpu-skinning.md step 3): where the
                // renderer poses bodies and the lights that reach this
                // one fit its program, nothing below touches a corner -
                // the body is its rest, one affine a mesh and a list of
                // lights. The per-pixel lighting enhancement stays on
                // the CPU path, whose corners it needs.
                p.gpu = false;
                p.lightCount = 0;
                p.restGeo = &rest;
                if (gpuPoseOn) {
                    p.lights.clear();
                    int reachN = 0;
                    const bool lit = lightCrowd && lighting == 0;
                    if (lit) {
                        const float at[3] = {w.body[0], w.body[1], w.body[2]};
                        for (const WorldSlot& ws2 : worldSlots)
                            if (!ws2.lights.empty())
                                reachN += omk::lightReach(at, ws2.lights, p.lights);
                    }
                    if (lighting == 0 && reachN <= gpuMaxLights) {
                        p.gpu = true;
                        p.lightCount = reachN;
                        p.lightsBlack = lit;
                        p.lightBase = crowdBase;
                    }
                }
                if (!p.gpu) { OMK_MEM_TAG("crowd: posed slots"); omk::applyPose(p.posed, rest, p.mo->meshes, pose); }
                j.tCompose += tc1 - tc0;
                j.tApply += phaseNow() - tc1;
                // THE HEIGHT is the engine's rule, `sub_437F80(inst, x, body.y
                // + footY - radius, z)`: the model origin stands one root
                // radius (41.9 for PSH_FN - the pelvis-to-feet height) above
                // the body point and the root track's summed y moves it.
                // Written here as the REST pose's feet on the body point plus
                // that summed y, which is the same constant for a model
                // whose radius is its height - and NOT the feet of the clip's
                // first frame, which for the seated clip are the folded legs
                // at pelvis level and sank every sitter into the street (a
                // reader's frame, 2026-09-03). The sit's root drops 20.7 over
                // its enter clip; that is what puts him on the ground.
                // ...per LOD level: each skeleton is its own rest
                if (j.level == 0 ? !p.feetKnown : !p.lodFeetKnown[j.level]) {
                    const auto restPose = omk::composePose(p.mo->meshes, omk::NodeTracks{}, 0, false);
                    omk::Geometry restPosed;
                    omk::applyPose(restPosed, rest, p.mo->meshes, restPose);
                    float feet = -1e9f;
                    for (const auto& c : restPosed.corners) if (c.y > feet) feet = c.y;
                    if (j.level == 0) { p.feetLevel0 = feet; p.feetKnown = true; }
                    else { p.lodFeet[j.level] = feet; p.lodFeetKnown[j.level] = true; }
                }
                p.feet = j.level == 0 ? p.feetLevel0 : p.lodFeet[j.level];
                // the pelvis of the skeleton DRAWN: the four are
                // authored side by side (PSH_FN's at x -12.6, -91.2,
                // -170.2, -248.9), so level 0's would stand a lower
                // level ~79 units off per step
                float rootXZ[2] = {0.0f, 0.0f};
                const int drawRoot = j.level == 0 ? p.mo->root : j.lodRoot;
                if (drawRoot >= 0 && static_cast<std::size_t>(drawRoot) < p.mo->meshes.size()) {
                    rootXZ[0] = p.mo->meshes[static_cast<std::size_t>(drawRoot)].pos[0];
                    rootXZ[1] = p.mo->meshes[static_cast<std::size_t>(drawRoot)].pos[2];
                }
                // one `cos`/`sin` for the whole body instead of two a
                // corner - the same bits, since the angle does not change
                float wcs, wsn;
                omk::yawSinCos(w.facing, wcs, wsn);
                const double pedPlace0 = phaseNow();   // the job's own
                if (p.gpu) {
                    // the placement below, as a 3x4 folded into each
                    // mesh's affine: x' = cs (x - rx) - sn (z - rz) + bx,
                    // z' = sn (x - rx) + cs (z - rz) + bz, y' = y + lift
                    const float place[12] = {
                        wcs, 0.0f, -wsn, w.body[0] - (wcs * rootXZ[0] - wsn * rootXZ[1]),
                        0.0f, 1.0f, 0.0f, w.body[1] + w.footY - p.feet,
                        wsn, 0.0f, wcs, w.body[2] - (wsn * rootXZ[0] + wcs * rootXZ[1])};
                    omk::meshAffines(p.mo->meshes, pose, place, p.affine);
                }
                if (!p.gpu) for (auto& c : p.posed.corners) {
                    const float in[3] = {c.x - rootXZ[0], c.y, c.z - rootXZ[1]};
                    float r[3];
                    omk::rotateYawCS(wcs, wsn, in, r);
                    c.x = r[0] + w.body[0];
                    c.y = r[1] + w.body[1] + w.footY - p.feet;
                    c.z = r[2] + w.body[2];
                    // the normal turns with the walker and does not move
                    const float n[3] = {c.nx, c.ny, c.nz};
                    float rn[3];
                    omk::rotateYawCS(wcs, wsn, n, rn);
                    c.nx = rn[0]; c.ny = rn[1]; c.nz = rn[2];
                }
                j.tPlace += phaseNow() - pedPlace0;
                // `Slider_PlaceShadow`'s two nodes, on the same transform.
                p.footKnown = false;
                {
                    // the serial pass's cache, for this model and root
                    const int fi[2] = {j.foot[0], j.foot[1]};
                    if (fi[0] >= 0 && fi[1] >= 0 &&
                        static_cast<std::size_t>(fi[0]) < pose.size() &&
                        static_cast<std::size_t>(fi[1]) < pose.size()) {
                        for (int f = 0; f < 2; ++f) {
                            const auto& mp = pose[static_cast<std::size_t>(fi[f])].pos;
                            const float in[3] = {mp[0] - rootXZ[0], mp[1], mp[2] - rootXZ[1]};
                            float r[3];
                            omk::rotateYawCS(wcs, wsn, in, r);
                            p.footAt[f][0] = r[0] + w.body[0];
                            p.footAt[f][1] = r[1] + w.body[1] + w.footY - p.feet;
                            p.footAt[f][2] = r[2] + w.body[2];
                        }
                        p.footKnown = true;
                        // THE INVARIANT THAT WOULD HAVE CAUGHT THE ORPHANS.
                        // A walker's feet are within a stride of his own
                        // body point; a bone taken off the wrong LOD
                        // skeleton is 240 units away. Measured every frame
                        // and reported, because "a shadow with no origin"
                        // is only visible to a person and this is not.
                        const float mx = (p.footAt[0][0] + p.footAt[1][0]) * 0.5f - w.body[0];
                        const float mz = (p.footAt[0][2] + p.footAt[1][2]) * 0.5f - w.body[2];
                        const float off = std::sqrt(mx * mx + mz * mz);
                        if (off > j.footOff) j.footOff = off;
                    }
                }
                // THE DYNAMIC LIGHTS (`o3de/vertexlight.h`). `sub_4380B0`
                // registers a walker in the same structure the set's
                // lights went into and `sub_48D7F0` applies every one that
                // reaches it, per frame, before submitting. Both resident
                // slots contribute, because during a transition two sets
                // are drawn and the engine's structure holds both.
                // ...unless the GPU is going to do it per pixel, in which
                // case the corners keep their black base and `Draw::lit`
                // carries the decision to the shader.
                const double pedLight0 = phaseNow();
                if (p.gpu) j.lit += p.lightCount;
                if (!p.gpu && lightCrowd && lighting == 0) {
                    // THE BASE IS THE SET'S AMBIENT GREY, and that is the
                    // part that had to be read rather than assumed - twice.
                    // A lit instance does not start from the model's baked
                    // vertex colour: the lit path `sub_494E80` writes its
                    // FIRST ARGUMENT's `+416` into every runtime vertex's
                    // colour. That argument is the SCENE (`sub_48D3B0`'s,
                    // and `sub_440CA0`'s for a character), and
                    // `Read3DO_Init` sets the scene's `+416` to the set's
                    // ambient grey (`.3DO` desc+184 x 255). This read
                    // "every site that sets +416 sets it to 0" until
                    // 2026-10-06 - those are actor records, a different
                    // `+416` (todo/drift-audit.md L1). The crowd models ship pure white
                    // (all 446 of PSH_FN's vertices are 255,255,255), so
                    // there is no baked light in them to keep - the .3DO
                    // lights ARE their lighting, and adding to white is
                    // what made this port's first attempt change exactly
                    // zero pixels.
                    for (auto& c : p.posed.corners) c.r = c.g = c.b = crowdBase;
                    const float at[3] = {w.body[0], w.body[1], w.body[2]};
                    for (const WorldSlot& ws2 : worldSlots)
                        if (!ws2.lights.empty())
                            j.lit += omk::applyLights(p.posed, 0, p.posed.corners.size(),
                                                          at, ws2.lights);
                }
                j.tLight += phaseNow() - pedLight0;
            }
        };
        {
            const double pedAll0 = phaseNow();
            if (threadBodies && pedJobs.size() > 1)
                omk::Threads::shared().parallelFor(0, pedJobs.size(), 1, pedBodies);
            else
                pedBodies(0, pedJobs.size());
            phSpan["ped bodies (wall)"] += phaseNow() - pedAll0;
        }
        for (PedJob& j : pedJobs) {
            PedStaged& p = *pedStaged[j.i];
            p.posed.revision = ++worldGeoRev;
            p.drawn = true;
            ++pedDrawn;
            {
                const float* b = ws[j.i].body;
                const float ex = b[0] - view.cam.eye[0], ey = b[1] - view.cam.eye[1], ez = b[2] - view.cam.eye[2];
                if (ex * ex + ey * ey + ez * ez > omk::kLodDistances[3] * omk::kLodDistances[3]) ++pedPastLod;
            }
            pedLit += j.lit;
            if (j.footOff > pedFootOffMax) pedFootOffMax = j.footOff;
            phSpan["ped compose"] += j.tCompose;
            phSpan["ped apply"] += j.tApply;
            phSpan["ped place"] += j.tPlace;
            phSpan["ped light"] += j.tLight;
        }

        // ...and on a headless run's LAST frame, which is the one its
        // `--dump` shows: since the side planes (optimization step 28 j)
        // a frame-0 count can be all `outside the view` while the frame
        // a check compares has walkers on it
        if (pedLive && (pedTold < 0 || n - pedTold >= 300 ||
                        (frames > 0 && n + 1 == static_cast<long>(frames)))) {
            pedTold = n;
            int pedGpu = 0;
            for (const auto& up : pedStaged) pedGpu += up->drawn && up->gpu;
            std::printf("frame %ld: pedestrians - %d live, %d drawn within %.0f of the eye "
                        "(%d outside the view, %d past the last LOD distance), %d at an action "
                        "point, %d idling, %d light hits, %d posed by the renderer\n",
                        n, pedLive, pedDrawn, reach, pedOffView, pedPastLod, pedInAction, pedIdle,
                        pedLit, pedGpu);
        }
        // ...and the ROAD TRAFFIC on the same circuit's vehicle lanes.
        const auto& vs = pd.vehicles();
        if (vehStaged.size() != vs.size()) {
            vehStaged.clear();
            for (std::size_t i = 0; i < vs.size(); ++i) vehStaged.push_back(std::make_unique<VehStaged>());
        }
        // `dword_4C8860`, the VEHICLE LOD distances - 20/30/40/50 m
        // where the crowd's are 10/20/30/40, so a slider is still
        // drawn a good way past the last walker.
        // ...and the vehicles' reach is the CLIP DISTANCE too, by the same
        // walk (`sub_48D7F0`); `dword_4C8860[3]`, 50 m, only ends their
        // LOD chain. Until 2026-10-07 this drew no vehicle past it.
        const float vreach = static_cast<float>(clipInches);
        int vehPastLod = 0;
        const double veh0 = phaseNow();
        for (std::size_t i = 0; i < vs.size(); ++i) {
            const auto& v = vs[i];
            VehStaged& sv = *vehStaged[i];
            sv.drawn = false;
            // ...and SAY why the player's own slider is not staged, once
            // per reason: after a load into another city it vanished
            // from the picture with the log silent about it.
            const bool mine = static_cast<int>(i) == pd.calledVehicle();
            static int calledSkipTold = -1;
            auto skipMine = [&](int why, const char* what) {
                if (mine && calledSkipTold != why) {
                    calledSkipTold = why;
                    std::printf("frame %ld: the called vehicle (slot %zu, model '%s') is NOT staged: %s\n",
                                n, i, v.model.c_str(), what);
                }
            };
            if (!v.live || v.mover < 0) { skipMine(1, "not live / no mover"); continue; }
            const auto& m = pd.movers()[static_cast<std::size_t>(v.mover)];
            ++vehLive;
            if (m.flags & 0x100u) ++vehStopped;
            // ...and the traffic the same way - but NOT the player's own slider
            // while he rides it: `Slider_TickRide` relinks it into the head
            // scene `dword_93076C` every tick, so it draws wherever he goes
            if (!circuitShown && !(mine && (ride || boarded))) {
                skipMine(4, "its street's slot is hidden (out of the render chain)");
                continue;
            }
            const float vx = m.body[0] - view.cam.eye[0], vy = m.body[1] - view.cam.eye[1],
                        vz = m.body[2] - view.cam.eye[2];
            const float vd2 = vx * vx + vy * vy + vz * vz;
            if (vd2 > vreach * vreach * 4.0f) { skipMine(2, "beyond the clip distance"); continue; }
            // A SLOT IS REUSED, and with another model: a call spawns into
            // the first dead slot, in a full
            // pool takes an ambient vehicle and rebinds its model to the
            // slider row (`sub_452CC0`'s take-over) - so a slot that drew a
            // MOTO can hold the player's slider next. `sv.mo` was taken once
            // and never compared, so the called slider was drawn as the moto
            // the slot had held: a reader's call after a journey brought "a
            // cutscene without any slider on it"
            if (sv.mo && sv.model != v.model) {
                std::printf("frame %ld: vehicle slot %zu restaged - '%s' -> '%s'\n",
                            n, i, sv.model.c_str(), v.model.c_str());
                sv.mo = nullptr; sv.atRest = nullptr; sv.built = false; sv.lodRoot = -1;
            }
            if (!sv.mo) { sv.mo = charModelFor(v.model); sv.model = v.model; }
            if (!sv.mo || !sv.mo->ready) { skipMine(3, "its model did not load"); continue; }
            if (mine && calledSkipTold != 0) { calledSkipTold = 0; std::printf("frame %ld: the called vehicle (slot %zu, model '%s') is staged at %.0f %.0f %.0f\n", n, i, v.model.c_str(), m.body[0], m.body[1], m.body[2]); }
            // ---- `sub_4521E0`, THE MODEL SWAP ----------------------
            // The slider model TABLE (`dword_538E28`, 88-byte rows)
            // holds its four root sub-objects at +4..+16 sorted
            // heaviest first by `sub_453A70` (vertices + faces):
            // SlBassin 1527 corners, slider_fl 750, SlBasA 366, SlBasB
            // 144. The reserved slider is created on +8 (slider_fl -
            // `CharModel::root` here, a shell with no interior and NO
            // DOOR: both door meshes hang off SlBassin) and
            // `sub_4521E0` toggles the sub-node to +4, SlBassin, the
            // COCKPIT, at `MDACTION`'s snap - which is what puts a
            // door under the clip - and `sub_4570F0` toggles it back
            // before the exit clip. A reader saw the shell: *"the
            // current model when Kay'l enters the slider has no
            // modelised interior"*.
            const bool swapped = (boarding || boarded) && static_cast<int>(i) == pd.calledVehicle();
            const int wantRoot = swapped ? heaviestRootOf(*sv.mo)
                                         : (v.lodBase == 0 ? heaviestRootOf(*sv.mo) : sv.mo->root);
            if (sv.built && sv.lodRoot != wantRoot) sv.built = false;
            if (!sv.built) {
                sv.lodRoot = wantRoot;
                if (static_cast<int>(i) == pd.calledVehicle() &&
                    wantRoot >= 0 && static_cast<std::size_t>(wantRoot) < sv.mo->meshes.size())
                    std::printf("slider: the called vehicle is staged on sub-object "
                                "'%s'%s\n", sv.mo->meshes[static_cast<std::size_t>(wantRoot)].name,
                                swapped ? " - the COCKPIT, sub_4521E0's swap" : "");
                sv.atRest = &vehAtRestFor(v.model, *sv.mo, sv.lodRoot);
                // The sub-objects of one model are laid out APART in
                // model space - SLI_FN's four roots sit at x 351..550 -
                // so each is re-centred on its own root, which is what
                // makes the four LOD variants land in one place. The
                // walkers do the same for x and z; a vehicle takes y
                // too, because it has no feet rule to stand on.
                if (sv.lodRoot >= 0 && static_cast<std::size_t>(sv.lodRoot) < sv.mo->meshes.size())
                    for (int k = 0; k < 3; ++k)
                        sv.origin[k] = sv.mo->meshes[static_cast<std::size_t>(sv.lodRoot)].pos[k];
                sv.radius = 0.0f;
                for (const auto& c : sv.atRest->corners) {
                    const float dx = c.x - sv.origin[0], dy = c.y - sv.origin[1], dz = c.z - sv.origin[2];
                    sv.radius = std::max(sv.radius, std::sqrt(dx * dx + dy * dy + dz * dz));
                }
                sv.built = true;
            }
            // PAST THE CLIP DISTANCE: `sub_48D7F0`'s reach test, the
            // distance against `dword_6A2B9C` plus the radius
            if ((sv.radius + vreach) * (sv.radius + vreach) <= vd2) {
                skipMine(2, "beyond the clip distance");
                continue;
            }
            // OUTSIDE THE VIEW: `sub_48D7F0` rejects the instance whole
            // before any matrix or vertex; the radius here is the
            // composed model's own, never smaller than the node's
            {
                const float at[3] = {m.body[0], m.body[1] - omk::kVehNodeLift, m.body[2]};
                if (!mine && outsideView(at, sv.radius, true)) continue;
            }
            // ...unless this is the slider he is CLIMBING INTO, in
            // which case its door is posed from the clip at the
            // character's own frame - the engine's shared clock.
            bool doorPosed = false;
            if ((boarding || leaving) && static_cast<int>(i) == pd.calledVehicle() && player) {
                if (!doorClipsRead) {
                    doorClipsRead = true;
                    const auto a = fs.read("ANIMS/SLF_112.3DA");
                    const auto b = fs.read("ANIMS/SLF_113.3DA");
                    if (!a.empty()) doorIn  = omk::clipTracks(a);
                    if (!b.empty()) doorOut = omk::clipTracks(b);
                }
                const omk::NodeTracks& dt0 = boarding ? doorIn : doorOut;
                // THE TRACKS ARE KEYED BY NODE ID, NOT MESH INDEX. The
                // clip names its five tracks with the bytes 0,3,1,4,2
                // and `SLI_FN.3DO`'s ids run SlBassin 0, SlPorteZG 1,
                // SlPorteG 2, SlPorteZD 3, SlPorteD 4 - the cockpit and
                // its four door parts, exactly a door clip's set. Read
                // as indices they land on two SHELLS, and the one
                // moving track fell on the wrong panel. `clipTracks`
                // fills `ids` as indices (right for the scene clips it
                // was written for); remapped through the model's ids.
                omk::NodeTracks dt = dt0;
                // BY NAME (+4 of the track header), not by id or index:
                // the mover is named `SlPorteZG`, the parent copy on the
                // G side; its child `SlPorteG` follows it, so the two
                // coincident copies swing together and nothing stays
                // over the hole. Read by index the swing landed on
                // `SlPorteG` alone; by id on `SlPorteD`, the far door.
                for (std::size_t ti = 0; ti < dt.ids.size(); ++ti) {
                    int idx = -1;
                    const std::string& nm = ti < dt.names.size() ? dt.names[ti] : std::string();
                    for (std::size_t k = 0; k < sv.mo->meshes.size(); ++k)
                        if (nm == sv.mo->meshes[k].name) { idx = static_cast<int>(k); break; }
                    dt.ids[ti] = idx;
                }
                static bool doorTold = false;
                if (!doorTold && dt.valid()) {
                    doorTold = true;
                    std::printf("slider: the door clip's tracks by name ->");
                    for (auto id : dt.ids)
                        std::printf(" %s", id >= 0 ? sv.mo->meshes[static_cast<std::size_t>(id)].name : "?");
                    std::printf("\n");
                }
                if (dt.valid()) {
                    int f = player->poseFrame();
                    if (f < 0) f = 0;
                    if (f >= dt.frames) f = dt.frames - 1;
                    const omk::Geometry& rest = lodRestFor(v.model, *sv.mo, sv.lodRoot);
                    const auto dp = omk::composePose(sv.mo->meshes, dt, f, false);
                    omk::applyPose(sv.posed, rest, sv.mo->meshes, dp);
                    doorPosed = true;
                }
            }
            // `sub_437F80(inst, x, y - 30.75, z)`: the instance sits
            // 30.75 units ABOVE the body point (y is down), turned to
            // the heading `sub_453330` built from the direction to its
            // mover - which for a vehicle is where it is going.
            float vcs, vsn;
            omk::yawSinCos(m.facing, vcs, vsn);   // once a vehicle, not a corner
            // THE BANK (drift audit M6): Manuelle's node matrix is
            // `sub_442160(0, -yaw, -(360 - roll))` (`sub_457F50`), which is a
            // turn about the model's own LONG axis (local Z) by the roll, then
            // the yaw: rows (cos r, -sin r, 0), (sin r, cos r, 0), (0, 0, 1)
            // at yaw 0, applied row-vector. Only the manual ride's tick writes
            // it - a called or ambient vehicle is yaw alone - so the slider
            // leans into a turn, up to `kBankLimit` (11), only while he flies it.
            float bcr = 1.0f, bsr = 0.0f;
            if (mine && ride && ride->roll != 0.0) {
                const double rr = ride->roll * 0.017453292519943295;
                bcr = static_cast<float>(std::cos(rr));
                bsr = static_cast<float>(std::sin(rr));
            }
            sv.gpu = !doorPosed && !cpuBodiesFlag && world.posesBodies() &&
                     sv.atRest->cornerMesh.size() == sv.atRest->corners.size();
            if (sv.gpu) {
                // x' = R (x - origin) + body - lift, R the yaw as
                // `rotateYawCS` applies it: x' = c x - s z, z' = s x + c z
                // M = Yaw * Roll(about local z), column form; t = body - lift - M origin
                const float m00 = vcs * bcr, m01 = vcs * bsr, m02 = -vsn;
                const float m10 = -bsr,      m11 = bcr,       m12 = 0.0f;
                const float m20 = vsn * bcr, m21 = vsn * bsr, m22 = vcs;
                const float* o = sv.origin;
                const float tx = m.body[0] - (m00 * o[0] + m01 * o[1] + m02 * o[2]);
                const float ty = m.body[1] - omk::kVehNodeLift - (m10 * o[0] + m11 * o[1] + m12 * o[2]);
                const float tz = m.body[2] - (m20 * o[0] + m21 * o[1] + m22 * o[2]);
                const float a[12] = {m00, m01, m02, tx,
                                     m10, m11, m12, ty,
                                     m20, m21, m22, tz};
                sv.affine.resize(12 * sv.mo->meshes.size());
                for (std::size_t k = 0; k < sv.mo->meshes.size(); ++k)
                    std::memcpy(&sv.affine[12 * k], a, sizeof a);
                sv.drawn = true;
                ++vehDrawn;
                if (vd2 > omk::kVehLodDistances[3] * omk::kVehLodDistances[3]) ++vehPastLod;
                continue;
            }
            if (!doorPosed) { OMK_MEM_TAG("crowd: vehicle slots"); sv.posed = *sv.atRest; }
            // the DRAWN tilt, read back from the posed corners: the two lateral
            // extremes (model x), their height difference over their spread
            float latLo = 1e30f, latHi = -1e30f, loW[3] = {0, 0, 0}, hiW[3] = {0, 0, 0};
            const bool measureBank = mine && ride;
            for (auto& c : sv.posed.corners) {
                const float l[3] = {c.x - sv.origin[0], c.y - sv.origin[1], c.z - sv.origin[2]};
                // the bank first, about the long axis (identity off a manual ride)
                const float in[3] = {l[0] * bcr + l[1] * bsr, -l[0] * bsr + l[1] * bcr, l[2]};
                float r[3];
                omk::rotateYawCS(vcs, vsn, in, r);
                c.x = r[0] + m.body[0];
                c.y = r[1] + m.body[1] - omk::kVehNodeLift;
                c.z = r[2] + m.body[2];
                if (measureBank) {
                    if (l[0] < latLo) { latLo = l[0]; loW[0] = c.x; loW[1] = c.y; loW[2] = c.z; }
                    if (l[0] > latHi) { latHi = l[0]; hiW[0] = c.x; hiW[1] = c.y; hiW[2] = c.z; }
                }
            }
            if (measureBank && latHi > latLo) {
                const double dx = hiW[0] - loW[0], dz = hiW[2] - loW[2];
                const double tilt = std::atan2(-(hiW[1] - loW[1]), std::sqrt(dx * dx + dz * dz)) * 57.29577951308232;
                static double tiltMax = 0.0;
                if (std::fabs(tilt) > tiltMax + 2.0) {
                    tiltMax = std::fabs(tilt);
                    std::printf("frame %ld: slider: Manuelle drawn BANKED - its +X side %.1f degrees "
                                "up, the ride's roll %.1f (sub_457F50's node matrix)\n", n, tilt,
                                ride->roll > 180.0 ? ride->roll - 360.0 : ride->roll);
                }
            }
            sv.posed.revision = ++worldGeoRev;
            sv.drawn = true;
            ++vehDrawn;
            if (vd2 > omk::kVehLodDistances[3] * omk::kVehLodDistances[3]) ++vehPastLod;
        }
        phSpan["vehicles"] += phaseNow() - veh0;
        releaseIdleCrowd();
        if (vehLive && (vehTold < 0 || n - vehTold >= 300)) {
            vehTold = n;
            int brakes = 0, bumps = 0, touches = 0;
            float closest = 1e9f;
            const float* ppos = player ? player->pos() : session.playerPos();
            for (const auto& vv : session.sliders().vehicles()) {
                brakes += vv.brakes; bumps += vv.bumps; touches += vv.touches;
                if (!vv.live || vv.mover < 0) continue;
                const auto& mm = session.sliders().movers()[static_cast<std::size_t>(vv.mover)];
                const float dx = mm.body[0] - ppos[0], dz = mm.body[2] - ppos[2];
                closest = std::min(closest, std::sqrt(dx * dx + dz * dz));
            }
            std::printf("frame %ld: traffic - %d live, %d drawn within %.0f of the eye "
                        "(%d past the last LOD distance), "
                        "%d stopped; braked for the player %d frames, touched him %d frames, "
                        "ran him over %d times, nearest now %.0f\n", n, vehLive, vehDrawn, vreach,
                        vehPastLod, vehStopped, brakes, touches, bumps, double(closest));
        }
    }
    player0 = phaseNow();
    playerOffView = false;    // his corners not built, his body not drawn
    playerGpu = false;        // his corners not built, his rest drawn by the renderer
    if (drawPlayer || drawArm) {
        // THE PLAYER, posed by his channel's clip - the quaternions
        // alone, since the root motion is the position the walker
        // integrated - turned by his facing (the row-vector rotation
        // `Matrix3x3_FromEulerAngles(0, yaw, 0)` gives, the same one
        // his root delta and the camera use) and stood with his FEET
        // on `pos()`, which the walker keeps on the floor. The feet
        // offset is the rest pose's, taken once, so a clip's bob does
        // not move the ground under him.
        // `player.anim.hold` neither resets to rest nor freezes: the
        // channel keeps ticking with no input (see the tick above), so
        // the pose comes from it exactly as it does unheld. This code
        // has now been wrong twice in the other two directions - first
        // `composePose(meshes, {}, 0)`, the REST SENTINEL, which drew a
        // T-pose; then a latched pose, which froze him mid-stride.
        const omk::NodeTracks* pt = player->poseTracks();
        std::vector<omk::MeshPose> pose = pt
            ? playerPoseNow(&*player, session.shootMode().active() &&
                                          player->state() == omk::ActorState::Shoot,
                            *pt, player->poseFrameF())
            : omk::composePose(playerMeshes, omk::NodeTracks{}, 0, false);
        // OUTSIDE THE VIEW, as any actor: `sub_48D3B0` skips a node
        // outside the four side planes before its matrices or its
        // vertices, and the Bowie sequence flies the camera round the
        // city while he stands still at his arrival point. Only his
        // CORNERS and his draw are skipped - the pose, the head, the
        // bones and the shadow bones below come from `pose` - and only
        // while nothing this frame reads the corners: the feet latch,
        // a clip with variants, melee's fist, shoot mode's arm, and
        // the two enhancements that bound or light him by them. The
        // sphere is generous: 150 units about a point 35 up his body.
        {
            const float* p0 = player->pos();
            const float centre[3] = {p0[0], p0[1] - 35.0f, p0[2]};
            const bool standingNow = player->clipName() == "H_STAND";
            const bool cornersUnread =
                playerFeetKnown && !(standingNow && !playerStandLatched) &&
                player->variantCount() <= 1 && !omk::envSet("OMK_PLY") &&
                !fightRun.active && !session.shootMode().active() && !drawArm &&
                lighting == 0 && !(drawShadows && shadowQuality >= 2);
            playerOffView = cornersUnread && outsideView(centre, 150.0f, true);
            // IN THE VIEW, AS THE ORIGINAL DRAWS ANY ACTOR: `sub_48D3B0`
            // builds one 3x3 a mesh (`sub_494650`: the node's rotation
            // times its parent's, the position beside it) and
            // `sub_4947F0` runs each vertex through it once, on the way
            // to the card - nothing keeps a posed copy of the body. On
            // a renderer that poses bodies that is his rest, uploaded
            // once, and one affine a mesh with his placement folded in
            // (`todo/gpu-skinning.md` step 5); the frames that READ his
            // corners - the list above - keep the CPU path.
            static const bool cpuPlayer = omk::envSet("OMK_CPU_PLAYER");   // A/B only
            playerGpu = cornersUnread && !playerOffView && drawPlayer && !cpuBodiesFlag &&
                        !cpuPlayer && world.posesBodies() &&
                        playerRest.cornerMesh.size() == playerRest.corners.size();
        }
        if (!playerOffView && !playerGpu) {
            omk::applyPose(playerPosed, playerRest, playerMeshes, pose);
            playerPosedFrame = n;
        }
        // THE ANCHOR IS THE FLOOR, NOT THE HIPS (omk-play 69).
        //
        // `playerFeet` used to be latched from the FIRST pose - the
        // standing one - and subtracted for ever after. Every later
        // pose was then placed by where the STANDING feet were, so a
        // pose that lowers the body relative to its feet is drawn with
        // the hips pinned and the legs coming up instead: a reader,
        // watching a take, "the character anchor is their hips and not
        // the floor... their legs go up, like the character is
        // floating".
        //
        // The crouch itself is a root TRANSLATION, and this port has no
        // path for one: `poseTracks` assigns `t.trans` all zeroes and
        // `composePose` never reads it. Re-measuring the lowest corner
        // each frame supplies the same result from the other side - the
        // body's lowest point sits on the walker's floor point, so the
        // planted foot stays down and the pelvis drops.
        //
        // **LABELLED, a port decision rather than a transcription**:
        // the engine seats the actor by his own origin and lets root
        // motion move him, which is a different mechanism. This is
        // equivalent while some part of him is on the ground and would
        // be WRONG for a pose where both feet leave it - a jump. The
        // port has no jump or fall state yet (omk-play 68), so nothing
        // today can tell them apart; when one arrives, this is the
        // line that has to become the real root-motion path.
        // THE ANCHOR AND THE DROP MUST SHARE ONE ORIGIN
        // (`todo/player-vertical.md` step 1, 2026-09-09).
        //
        // The reader: *"walking or running seems to rise its y
        // position and stopping (and so returning to the idle
        // position) reset the y position to a normal one"*. It is not
        // the clips: measured with `tools/vertical_probe`, `H_WALK`
        // f0 lifts the body's lowest corner 2.06 above the standing
        // one while its pelvis track drops 2.09, so the authored pair
        // CANCELS to 0.03 - the planted foot stays planted, which is
        // the "authored motion netting out" the engine relies on.
        //
        // What floated was this port's bookkeeping. `playerFeet` is a
        // POSE's lowest corner, latched from `H_STAND` - whose pelvis
        // trans is +0.68, not zero - while `rootAccum` below re-bases
        // to the ENTERED clip's first frame: +2.09 for `H_WALK`,
        // +3.68 for `H_RUN`. The two halves therefore measured from
        // different origins, and the body was drawn a CONSTANT
        // 2.09-0.68 = 1.41 too high walking and 3.68-0.68 = 3.00 too
        // high running, snapping back the moment `H_STAND` released
        // the sum. That is the report exactly, running rising more.
        //
        // So the anchor records the pelvis trans it was taken at
        // (`playerRootRef`) and the drop is measured from it.
        //
        // AND IT IS LATCHED FROM `H_STAND` SPECIFICALLY, not from
        // whatever pose happened to be up on the first frame: the
        // anchor is meant to be a model constant, and one taken off an
        // arbitrary pose carries that pose's own vertical into every
        // frame afterwards. A provisional latch draws until the idle
        // first comes round, and is then replaced once.
        //
        // Cross-checked against the engine's own constant, which is a
        // different quantity and does not substitute for this one:
        // `Walk_ProbeGround` (0x00467030) seats by the model's
        // COLLISION SPHERES - `Collision_BodySphere`'s largest radius
        // (10.91) plus `sub_4443B0`'s second-lowest centre.y (14.98) -
        // and HO1_FN's lowest sphere reaches 41.81 below the node
        // against a standing visual foot at 38.76 + 0.68 = 39.44. The
        // sphere hangs 2.37 BELOW the feet, and where the engine
        // absorbs that is `actor+276`, the reference its clearance
        // `actor[264] - actor[276]` is taken against - which is
        // UNTRACED. So the sphere constant is not used as the seat
        // here; it is quoted because it corroborates the scale.
        const bool standing = player->clipName() == "H_STAND";
        if (!playerFeetKnown || (standing && !playerStandLatched)) {
            playerFeet = -1e9f;
            for (const auto& c : playerPosed.corners)
                if (c.y > playerFeet) playerFeet = c.y;
            playerRootRef = (pt && !pt->trans.empty())
                                ? pt->trans[static_cast<std::size_t>(
                                      player->poseFrame() > 0 ? player->poseFrame() : 0)][1]
                                : 0.0f;
            // the hierarchy root: the one mesh with no parent. A model
            // constant, so this half stays latched.
            playerRootXZ[0] = playerRootXZ[1] = 0.0f;
            for (const auto& m : playerMeshes)
                if (m.parent < 0) {
                    playerRootXZ[0] = m.pos[0];
                    playerRootXZ[1] = m.pos[2];
                    break;
                }
            playerFeetKnown = true;
            if (standing) playerStandLatched = true;
        }
        // THE ROOT TRANSLATION'S VERTICAL, which is the crouch.
        //
        // `Walk_ProbeGround` (0x00467030) anchors the actor by a
        // CONSTANT: `actor[264] = actor[236] - actor[248] + groundY +
        // sphere[12]`, where `sphere[12]` comes from
        // `Collision_BodySphere` and does not change with the pose. So
        // the engine's anchor never moves, and the crouch is carried
        // entirely by the animation's ROOT MOTION - the pelvis track's
        // summed position keys, 24.3 units (62 cm) inside one cell of
        // H_TAKL12.
        //
        // Re-measuring the lowest corner each frame, which is what
        // this did for one build, gets a similar picture and STUTTERS,
        // because it re-derives the anchor from a pose that changes
        // every frame. The anchor is constant again and the drop comes
        // from `trans` instead.
        //
        // Only the VERTICAL is taken: X and Z would double-count
        // against the walker, which already moves him.
        // AND IT ACCUMULATES ACROSS STATES. `Anim_RootDelta(prev,
        // cur)` adds the movement between the previous frame and the
        // current one every tick and "the accumulated offset stands"
        // (CLAUDE.md 6) - there is no reset per clip. Each clip's own
        // sum starts at zero, so taking it as an absolute made
        // H_TAKL22 run 0 -> -24.6: from STANDING to 62 cm above it,
        // instead of from crouched back down to standing. A reader:
        // "animation from up to down => ok / animation from down to up
        // => character suddenly way higher than they should be."
        //
        // Carried, the pair nets out: +24.3 then -24.6 is -0.3.
        static float rootAccum = 0.0f, rootLast = 0.0f;
        static int   rootState = -12345;
        float rootDrop = 0.0f;
        if (pt && !pt->trans.empty()) {
            const int rf = player->poseFrame();
            const std::size_t ri = static_cast<std::size_t>(
                rf < 0 ? 0 : (rf < static_cast<int>(pt->trans.size())
                                  ? rf : static_cast<int>(pt->trans.size()) - 1));
            const float cur = pt->trans[ri][1];
            // a new state re-bases the delta, it does not reset the sum
            if (player->ctlState() != rootState) {
                rootState = player->ctlState();
                rootLast  = cur;
            }
            // A WINDOW'S LAST FRAME IS A DISCONTINUITY, NOT MOTION.
            // Measured across a take: the blend runs 0 -> +21.90 over
            // H_TAKL12's 21 frames (the crouch, against 19.08 of feet
            // lifted by the rotations - they very nearly cancel) and
            // then snaps to -0.22 at f 20. That one bogus delta
            // poisons the sum, so H_TAKL22 starts from a corrupted
            // base and ends 22 units up - "it stays higher until I
            // release the object".
            //
            // **A PORT GUARD, labelled**: real root motion is a few
            // units a frame at most (H_WALK's whole cycle spans 2).
            // Anything larger is a seam between variants, not the
            // pelvis moving, so it is not accumulated. The engine has
            // no such guard because `Anim_RootDelta` never crosses a
            // seam: it indexes the position keys by the RAW frame and
            // so reads cell 0 only. Reading the blended cells is this
            // port's choice, and this is its cost.
            rootAccum += cur - rootLast;
            rootLast   = cur;
            // **A PORT GUARD, labelled**: the engine relies on the
            // authored motion netting out and has a ground probe under
            // it every frame; this has neither, so any residue would
            // live for ever. Back in the idle, the body is standing by
            // definition, so the sum is released there.
            if (player->clipName() == "H_STAND") rootAccum = 0.0f;
            // ...and NOT while the slider clips carry him: in ACTOR_STATE
            // 6 and 8 the clip's root y is the NODE's (`sub_45C680` cases
            // 6/8), carried by `PlayerController::nodeDrop()` below - adding
            // this sum as well put him one clip's descent (12.5) too low in
            // the seat (the reader: *"he is just a bit too low"*).
            if (boarding || leaving) rootAccum = 0.0f;
            rootDrop = rootAccum;
            // ...EXCEPT WHERE THE BODY IS ON THE GROUND BY DEFINITION,
            // which is every LOOPING clip - the idle, the walk, the
            // run (`todo/player-vertical.md` step 1).
            //
            // The accumulator above exists for the TAKE, whose clips
            // CHAIN: H_TAKL12 crouches +24.3 and H_TAKL22 carries that
            // crouch back up, so each reads as a delta on the last and
            // an absolute reading of either is meaningless (it is the
            // regression recorded in the note above - H_TAKL22 run as
            // an absolute goes 0 -> -24.6, from standing to 62 cm
            // ABOVE it). A locomotion clip is the opposite: it is one
            // self-contained cycle that begins and ends on the floor,
            // so its pelvis trans is an OFFSET FROM THE STANDING POSE
            // and must be read against the origin the anchor was
            // latched at, never against whatever the last state left
            // in the sum.
            //
            // The discriminator is structural, not a list of names:
            // `variantCount() > 1` is what marks the grid/take states
            // (it is what selects `gridTracks`, and what the crouch
            // trace below is gated on). Everything else loops.
            //
            // H_STAND lands on cur 0.68 - ref 0.68 = 0 by
            // construction, which is why the explicit release below it
            // was able to look correct while the walk was 1.41 out.
            if (player->variantCount() <= 1) rootDrop = cur - playerRootRef;
            // ...AND NOT IN THE WATER, for the slider's reason above. In
            // ACTOR_STATEs 11..14 the clip's root y reaches his POSITION
            // (`sub_4A9470` moves him by the whole delta, vertical
            // included), and for a SWIM clip that y is not a bob at all:
            // the clips are authored upright and stroke along the body's
            // own axis, which his pitch then lays along the water. Drawn
            // as a drop as well, the stroke's travel went onto the body a
            // second time, in WORLD y and unturned - so on screen he
            // climbed through every loop whatever way he pointed and
            // snapped back at the wrap. A reader, 2026-09-17: *"the
            // character always goes up, whatever is his actual direction
            // ... each time the animation loop restarts, the character's
            // position is reset"*.
            {
                const int ws = static_cast<int>(player->state());
                if (ws >= 11 && ws <= 14) { rootDrop = 0.0f; rootAccum = 0.0f; }
            }
            // ...AND NOT WHILE THE SLIDER CLIPS CARRY HIM, AFTER the looping
            // rule as well as before it. The guard above zeroed the
            // accumulator, but `H_SLDIN` and `H_SLDOUT` are single-variant
            // clips, so the looping rule then put `cur - ref` straight back:
            // +11.5..11.9 of descent drawn as a drop on top of the same
            // descent moving his position (ACTOR_STATE 6/8, `Actor_MoveBy`).
            // A reader, again (2026-10-06): *"Kay'l position is a bit low
            // when he sits in the slider"*.
            // ...and what IS drawn there is the NODE's own drop: in ACTOR_STATE
            // 6/8 the clip's root y moves the node and not his position
            // (`sub_45C680` cases 6/8, `PlayerController::nodeDrop`), so the
            // body steps into the seat and out of it on top of a position
            // that stays where the arm put him (2026-10-06, the exit's fall).
            if (boarding || leaving) rootDrop = player->nodeDrop();
            // MEASURING, not fixing: how far does the model's own
            // lowest point travel across a take? If the rotations
            // lower the body, a CONSTANT anchor is right and the
            // float came from somewhere else; if it barely moves
            // while the legs bend, the body never crouches at all.
            // OMK_PLY=N prints every Nth frame (1 for every frame, which is what a
            // jump needs - its whole arc is 14 frames and a stride of 4
            // steps straight over the apex).
            static const long plyEvery = [] {
                const char* e = std::getenv("OMK_PLY");
                const long v = e ? std::atol(e) : 0;
                return v > 1 ? v : 1;
            }();
            if (omk::envSet("OMK_PLY") && (n % plyEvery) == 0) {
                float lo = -1e9f, hi = 1e9f;
                for (const auto& c : playerPosed.corners) {
                    if (c.y > lo) lo = c.y;
                    if (c.y < hi) hi = c.y;
                }
                // `foot` is the one that matters and the one this
                // print did NOT carry: `lo` is a MODEL-space corner
                // (this runs before the offset loop below), so its
                // `gap` against the ground is not a distance on
                // screen. The drawn foot sits at
                // `lo + pos.y - playerFeet + rootDrop`, so its height
                // above the floor is `lo - playerFeet + rootDrop` -
                // NEGATIVE is above, y growing down. That is the
                // number `verify.py: engine player vertical` asserts
                // and the one the walk float showed up in.
                std::printf("DBG ply f%ld %-9s pf %2d  ground %+8.2f  lowest %+8.2f"
                            "  gap %+7.2f  head %+8.2f  rootDrop %+6.2f  foot %+7.2f"
                            "  at %+9.2f %+9.2f  air %d\n",
                            n, player->clipName().c_str(), player->poseFrame(),
                            player->pos()[1], lo, lo - player->pos()[1], hi,
                            rootDrop, lo - playerFeet + rootDrop,
                            player->pos()[0], player->pos()[2],
                            player->walker().airborne() ? 1 : 0);
            }
            if (player->variantCount() > 1) {
                float lo = -1e9f;
                for (const auto& c : playerPosed.corners)
                    if (c.y > lo) lo = c.y;
                const auto& lf = player->last();
                std::printf("  crouch: %-9s f %2d  lowest %+8.2f  "
                            "(latched %+8.2f, delta %+7.2f)  root %+6.2f"
                            "  at %.1f %.1f  dxz %+.2f %+.2f  step %d%s\n",
                            player->clipName().c_str(), player->poseFrame(),
                            lo, playerFeet, lo - playerFeet, rootAccum,
                            player->pos()[0], player->pos()[2],
                            lf.rootDelta[0], lf.rootDelta[2],
                            static_cast<int>(lf.step),
                            lf.stepped ? "" : " (no step asked)");
            }
        }
        const float* pp = player->pos();
        float yaw = player->facing();
        // ---- ...AND THE SLIDER OWNS HIM WHILE HE BOARDS ----------
        //
        // `MDACTION` and `sub_468FA0` both end with
        // `sub_437140(node, M_slider)`, which writes the SLIDER's
        // matrix into the actor node's `+156` - and `+156` is read as
        // an ORIENTATION (21_d3d.c 2854 takes
        // `Matrix3x3_RotateVector(0, 0, -1, node+156)` as the node's
        // heading). So for the whole of ACTOR_STATE 6 and 8 the body
        // is drawn in the vehicle's frame, which is what squares him
        // to the door however he walked up to it.
        //
        // The engine's forward is `-row2`: `sub_456530` case 7 tests
        // `(sin y, 0, -cos y)` against the player's own +420, and the
        // vehicle matrix's row 2 is the negated travel direction. So
        // the drawn yaw is `atan2(-row2.x, row2.z)`.
        //
        // THIS is the line that had to change. Writing the same yaw
        // into `Session::setPlayerPosition` instead - which is what
        // the first attempt did - updates the logical player record
        // and NOTHING on screen, because the model is posed from
        // `player->facing()` right here. A reader saw exactly that:
        // *"I don't see any change"*.
        if (boarding || leaving) {
            float bat[3], bx[3], bz[3];
            if (session.sliders().calledFrame(bat, bx, bz))
                yaw = static_cast<float>(
                    std::atan2(-bz[0], bz[2]) * 57.29577951308232);
        }
        // THE WHOLE EULER, not the yaw: the node's `+156` is actor+288,
        // `Matrix3x3_FromEulerAngles(+416, +420, +424)` rebuilt each
        // frame by `Actors_TickAll`, so the pitch `sub_4A8F30` turns a
        // swimmer by - and the shove's lean - reach the drawn body as
        // they reach his root motion (`todo/swimming.md` step 3b).
        const float drawEuler[3] = {player->euler()[0], yaw, player->euler()[2]};
        // ---- THE SET'S LIGHTS ON HIM (todo/drift-audit.md L1): he is an
        // actor `Actor_LoadModel` loaded, so `LightObject` registered him and
        // `sub_440CA0` lights him as it does every character - from the
        // scene's ambient grey (`+416`), by every set light reaching his
        // root. His root's world point on the transform his corners get
        // below; a list longer than the renderer's program takes sends him
        // the CPU way, before his corners are placed.
        const bool playerLit = lightActors && lighting == 0 && drawPlayer && !playerOffView;
        float playerLitAt[3] = {pp[0], pp[1], pp[2]};
        playerLightCount = 0;
        playerLightsBlack = false;
        playerLightBase = static_cast<float>(std::clamp(
            activeAmbientGrey, 0, 255)) / 255.0f;
        if (playerLit) {
            std::size_t ri = 0;
            for (std::size_t i = 0; i < playerMeshes.size(); ++i)
                if (playerMeshes[i].parent < 0) { ri = i; break; }
            if (ri < pose.size()) {
                const float in[3] = {pose[ri].pos[0] - playerRootXZ[0], pose[ri].pos[1],
                                     pose[ri].pos[2] - playerRootXZ[1]};
                float r[3];
                omk::rotateEuler(drawEuler, in, r);
                playerLitAt[0] = r[0] + pp[0];
                playerLitAt[1] = r[1] + pp[1] - playerFeet + rootDrop;
                playerLitAt[2] = r[2] + pp[2];
            }
            playerLights.clear();
            int reachN = 0;
            for (const WorldSlot& ws2 : worldSlots)
                if (!ws2.lights.empty()) reachN += omk::lightReach(playerLitAt, ws2.lights, playerLights);
            if (playerGpu && reachN > world.maxVertexLights()) {
                playerGpu = false;
                omk::applyPose(playerPosed, playerRest, playerMeshes, pose);
                playerPosedFrame = n;
            }
            if (playerGpu) { playerLightCount = reachN; playerLightsBlack = true; }
        }
        // ROTATE ABOUT THE PELVIS, not the model's origin. A `.3DO`'s
        // meshes carry ABSOLUTE positions and the body is not built
        // around (0,0,0): `HO1_FN`'s root `UBassin` sits at
        // x 2.87, z **17.94**, and its whole bounding box spans
        // z 10.5..21.3. Spinning the raw corners about the origin
        // therefore swings the character around a point about 18
        // inches - half a metre - away from himself, which is what a
        // reader described as "the pivot is placed about a metre
        // ahead of the character". The actor's own origin is the
        // pelvis (`player.h`, settled with the camera lift), so that
        // is what must stay put.
        if (playerGpu) {
            // the loop below as a 3x4: turned about the pelvis's x/z
            // (`t = T - R root`), then stood at `pp` on his feet
            float place[12];
            for (int j = 0; j < 3; ++j) {
                const float e[3] = {j == 0 ? 1.0f : 0.0f, j == 1 ? 1.0f : 0.0f,
                                    j == 2 ? 1.0f : 0.0f};
                float col[3];
                omk::rotateEuler(drawEuler, e, col);
                for (int r = 0; r < 3; ++r) place[4 * r + j] = col[r];
            }
            const float T[3] = {pp[0], pp[1] - playerFeet + rootDrop, pp[2]};
            for (int r = 0; r < 3; ++r)
                place[4 * r + 3] = T[r] - (place[4 * r] * playerRootXZ[0] +
                                           place[4 * r + 2] * playerRootXZ[1]);
            omk::meshAffines(playerMeshes, pose, place, playerAffine);
        }
        if (!playerOffView && !playerGpu) for (auto& c : playerPosed.corners) {
            const float in[3] = {c.x - playerRootXZ[0], c.y,
                                 c.z - playerRootXZ[1]};
            float r[3];
            omk::rotateEuler(drawEuler, in, r);
            c.x = r[0] + pp[0];
            c.y = r[1] + pp[1] - playerFeet + rootDrop;
            c.z = r[2] + pp[2];
        }
        // the head mesh, looked up again only when the player's model
        // changes (`player.become`) - it was an O(n^2) walk and a string
        // a mesh, every frame
        static const void* headFor = nullptr;
        static std::size_t headForN = 0;
        static int headCached = -1;
        if (headFor != playerMeshes.data() || headForN != playerMeshes.size()) {
            headFor = playerMeshes.data();
            headForN = playerMeshes.size();
            headCached = omk::headMeshOf(playerMeshes);
        }
        if (const int hd = headCached;
            hd >= 0 && static_cast<std::size_t>(hd) < pose.size()) {
            const float in[3] = {pose[static_cast<std::size_t>(hd)].pos[0] - playerRootXZ[0],
                                 pose[static_cast<std::size_t>(hd)].pos[1],
                                 pose[static_cast<std::size_t>(hd)].pos[2] - playerRootXZ[1]};
            float r[3];
            omk::rotateEuler(drawEuler, in, r);
            playerHeadRise = -r[1];
            playerHeadAt[0] = r[0] + pp[0];
            playerHeadAt[1] = r[1] + pp[1] - playerFeet + rootDrop;
            playerHeadAt[2] = r[2] + pp[2];
            // ...and the same point as an OFFSET from where he stands, which
            // is what the camera's head subject takes: the engine re-reads the
            // head from the body it has just moved (`sub_415050` updates the
            // hierarchy first), so a teleport must carry the head with it
            for (int k = 0; k < 3; ++k) playerHeadRel[k] = playerHeadAt[k] - pp[k];
            playerHeadKnown = true;
        }
        playerMeshAt.assign(pose.size() * 3, 0.0f);
        playerMeshRot.assign(pose.size() * 9, 0.0f);
        for (std::size_t mi = 0; mi < pose.size(); ++mi) {
            const float in[3] = {pose[mi].pos[0] - playerRootXZ[0],
                                 pose[mi].pos[1],
                                 pose[mi].pos[2] - playerRootXZ[1]};
            float r[3];
            omk::rotateEuler(drawEuler, in, r);
            playerMeshAt[mi * 3 + 0] = r[0] + pp[0];
            playerMeshAt[mi * 3 + 1] = r[1] + pp[1] - playerFeet + rootDrop;
            playerMeshAt[mi * 3 + 2] = r[2] + pp[2];
            // its world rotation: the pose's own, then his yaw - the
            // same composition the corners get
            for (int ax = 0; ax < 3; ++ax) {
                const float e[3] = {ax == 0 ? 1.0f : 0.0f, ax == 1 ? 1.0f : 0.0f,
                                    ax == 2 ? 1.0f : 0.0f};
                float qv[3], wv[3];
                omk::qrot(pose[mi].q, e, qv);
                omk::rotateEuler(drawEuler, qv, wv);
                for (int k = 0; k < 3; ++k)
                    playerMeshRot[mi * 9 + static_cast<std::size_t>(ax * 3 + k)] = wv[k];
            }
        }
        playerMeshAtKnown = true;
        lastRootDrop = rootDrop;
        if (playerLit && !playerGpu) {
            for (auto& c : playerPosed.corners) c.r = c.g = c.b = playerLightBase;
            for (const WorldSlot& ws2 : worldSlots)
                if (!ws2.lights.empty())
                    omk::applyLights(playerPosed, 0, playerPosed.corners.size(), playerLitAt, ws2.lights);
        }
        if (!playerOffView && !playerGpu) playerPosed.revision = ++worldGeoRev;
        for (int k = 0; k < 3; ++k) actorAt[k] = pp[k];
        actorAt[1] -= playerFeet;
        actorKnown = true;
    }
}

// THE CROWD'S MEMORY, AS THE ORIGINAL KEEPS IT (2026-10-04). The engine poses
// every body each frame into ONE frame scratch pool and submits from it
// (`sub_4947F0`, `classic-mac-port-1999.md` 3b-i) - nothing posed is kept per
// body - and builds a model's LOD sub-objects ONCE, at area load
// (`Slider_Init`, `sub_453A70`). Here each of the 200 walker and 40 vehicle
// slots poses into its own geometry (the posing runs on several threads, a
// slot each, and the renderers cache a geometry's buffers by its address), so
// the footprint is matched rather than the layout: a slot whose body has not
// been drawn for two seconds GIVES BACK its posed geometry and its pose,
// light and affine buffers - memory follows the bodies recently on screen, as
// the original's pool does - and a vehicle's composed sub-object is one per
// (model, sub-object), shared, as the original's are. The profiler's first
// street capture had the slots' copies at ~9 MB after three minutes and
// climbing (`todo/debug-tools.md`). Two seconds, not one frame: a walker on
// the edge of the draw distance would otherwise re-allocate every frame.
const omk::Geometry & PlayState::vehAtRestFor(const std::string& model, const CharModel& mo, int rootMesh) {
    OMK_MEM_TAG("crowd: vehicle sub-objects");   // the profiler's category (todo/debug-tools.md)
    auto it = vehAtRest.find({model, rootMesh});
    if (it != vehAtRest.end()) return it->second;
    const omk::Geometry& rest = lodRestFor(model, mo, rootMesh);
    const auto pose = omk::composePose(mo.meshes, omk::NodeTracks{}, 0, false);
    omk::Geometry g;
    omk::applyPose(g, rest, mo.meshes, pose);
    return vehAtRest.emplace(std::make_pair(model, rootMesh), std::move(g)).first->second;
}

namespace {
// Destroyed in place and made anew at the same address: the renderers learn
// of a geometry's end by its destruction (`addGeometryListener`), keyed by its
// address, so this - not clearing its vectors - is what frees its GPU buffers.
void releaseGeometry(omk::Geometry& g) {
    g.~Geometry();
    new (&g) omk::Geometry();
}
template <class T>
void releaseVector(std::vector<T>& v) { std::vector<T>().swap(v); }
}  // namespace

void PlayState::releaseIdleCrowd() {
    constexpr long kIdleFrames = 60;      // two seconds at 30
    for (auto& up : pedStaged) {
        PedStaged& p = *up;
        if (p.drawn) { p.lastDrawn = n; continue; }
        if (p.lastDrawn < 0 || n - p.lastDrawn <= kIdleFrames) continue;
        releaseGeometry(p.posed);
        releaseVector(p.pose);
        releaseVector(p.lights);
        releaseVector(p.affine);
        p.lastDrawn = -1;                 // nothing held until it is drawn again
    }
    for (auto& up : vehStaged) {
        VehStaged& v = *up;
        if (v.drawn) { v.lastDrawn = n; continue; }
        if (v.lastDrawn < 0 || n - v.lastDrawn <= kIdleFrames) continue;
        releaseGeometry(v.posed);
        releaseVector(v.affine);
        v.lastDrawn = -1;
    }
}

// The staged bodies the same way (todo/ram-vs-original.md tier B): a body
// beyond the clip distance or outside the view is not skinned, and one the
// renderer poses reads its rest - either way its CPU `posed` copy waits,
// unread, for the next frame that skins it, which rebuilds it whole. The
// original poses every body into its one per-frame pool (`sub_4947F0`).
// Two seconds idle, as the crowd's slots.
void PlayState::releaseIdleStaged() {
    constexpr long kIdleFrames = 60;
    for (auto& up : staged) {
        Staged& s = *up;
        if (s.lastSkinned < 0 || n - s.lastSkinned <= kIdleFrames) continue;
        releaseGeometry(s.posed);
        s.lastSkinned = -1;
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE REMASTER'S GLES BACKEND (`todo/remaster.md`) - the post pass, the water
// reflection's pass, FSR's two passes, the scene programs' variants and the
// pack's uploads (ASTC), the preload's pins. Included at the end of `glesrender.cpp`, inside its
// namespace, under OMK_REMASTER only - its stubs are there otherwise. Not
// compiled on its own.

// THE REMASTER'S POST-PASS (`todo/remaster.md` step 2): `colour_` copied to
// `postSrc_`; the glow down / blur / blur at a quarter; the final pass back
// into `colour_` (`fbo_`) with the glow screen-blended and the grade applied.
void GlesRenderer::runPost() {
    if (!postProg_) {
        postProg_ = link(kPostVert, kPostFrag, {{0, "aPos"}});
        if (!postProg_) {
            std::printf("gles: the post-pass program did not build - bloom and grade refused\n");
            bloom_ = 0.0f; lutN_ = 0;
            return;
        }
        std::printf("gles: the remaster's post-pass - bloom %.2f, grade %s\n",
                    static_cast<double>(bloom_), lutN_ > 1 ? "on" : "off");
        omk::event("remaster.post", {{"backend", "gles"}, {"bloom", static_cast<double>(bloom_)},
                                     {"grade", lutN_}});
        // its uniforms, looked up once
        const char* names[10] = {"uSrc", "uGlow", "uLut", "uSrcInv", "uDir", "uStrength", "uMode", "uLutN", "uHalo",
                                 "uHaloTex"};
        for (int k = 0; k < 10; ++k) postLoc_[k] = glGetUniformLocation(postProg_, names[k]);
    }
    const auto texture = [](GLuint& id, int w, int h) {
        if (!id) glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        texParams(GL_LINEAR, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gpuTexture("render targets", id, 4LL * w * h);
    };
    const int gw = std::max(1, rw_ / 4), gh = std::max(1, rh_ / 4);
    if (postW_ != rw_ || postH_ != rh_) {
        texture(postSrc_, rw_, rh_);
        for (int k = 0; k < 2; ++k) {
            texture(glowTex_[k], gw, gh);
            if (!glowFbo_[k]) glGenFramebuffers(1, &glowFbo_[k]);
            glBindFramebuffer(GL_FRAMEBUFFER, glowFbo_[k]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glowTex_[k], 0);
        }
        postW_ = rw_; postH_ = rh_;
    }
    if (lutDirty_ && lutN_ > 1) {
        if (!lutTex_) glGenTextures(1, &lutTex_);
        glBindTexture(GL_TEXTURE_2D, lutTex_);
        texParams(GL_LINEAR, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, lutN_ * lutN_, lutN_, 0, GL_RGBA, GL_UNSIGNED_BYTE, lutData_.data());
        gpuTexture("textures", lutTex_, 4LL * lutN_ * lutN_ * lutN_);
        lutDirty_ = false;
    }
    // the frame, copied out of the world target
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glBindTexture(GL_TEXTURE_2D, postSrc_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, rw_, rh_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(postProg_);
    curProg_ = postProg_;
    const GLint lSrc = postLoc_[0], lGlow = postLoc_[1], lLut = postLoc_[2], lInv = postLoc_[3],
                lDir = postLoc_[4], lStr = postLoc_[5], lMode = postLoc_[6], lN = postLoc_[7];
    glUniform1i(lSrc, 0); glUniform1i(lGlow, 1); glUniform1i(lLut, 2); glUniform1i(postLoc_[9], 3);
    // (the grade alone when no draw bound a light map: the glow would be black)
    const bool glow = bloom_ > 0.0f && lightMapNow_;
    glUniform1f(lStr, glow ? bloom_ : 0.0f);
    glUniform1f(postLoc_[8], glow ? halo_ : 0.0f);   // ...and its halos in the rain (`View::halo`)
    glUniform1f(lN, lutN_ > 1 ? static_cast<float>(lutN_) : 0.0f);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    for (GLuint a = 1; a <= kAttrPhase; ++a) glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    const auto pass = [&](GLuint fb, int w, int h, GLuint src, int srcW, int srcH, float dx, float dy, int mode) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glViewport(0, 0, w, h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform2f(lInv, 1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH));
        glUniform2f(lDir, dx, dy);
        glUniform1i(lMode, mode);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    if (glow) {
        pass(glowFbo_[0], gw, gh, postSrc_, rw_, rh_, 0, 0, 0);
        pass(glowFbo_[1], gw, gh, glowTex_[0], gw, gh, 1, 0, 1);
        pass(glowFbo_[0], gw, gh, glowTex_[1], gw, gh, 0, 1, 1);
        // THE HALOS at the glow's size into glow B (mode 3), the frame's
        // `srcInv` as the final pass used: its 25 taps a quarter-size texel
        // once, not a full-size pixel's
        if (halo_ > 0.0f) pass(glowFbo_[1], gw, gh, glowTex_[0], rw_, rh_, 0, 0, 3);
    }
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, glowTex_[1]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glowTex_[0]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, lutTex_ ? lutTex_ : glowTex_[0]);
    glActiveTexture(GL_TEXTURE0);
    pass(fbo_, rw_, rh_, postSrc_, rw_, rh_, 0, 0, 2);
    glEnable(GL_DEPTH_TEST);
    forgetState();
    lastEmis_ = 0;
}

// THE MIRROR, THE ORIGINAL'S WAY (`sub_440D90`, 0x00440D90): the scene is
// drawn FIRST through the camera reflected in the mirror's plane - with the
// screen-X flip `dword_53ADE0` asks of `Raster_DrawTriangles`, here `flipX` -
// and then AGAIN through the real camera, over it, with no clear between: only
// `Render_Frame(reflected, 1)` then `Render_Frame(camera, 0)`. The reflected
// pass leaves exactly the depths of a virtual room behind the plane, so the
// real room's walls, nearer, cover it everywhere but where the wall is open for
// the mirror, and the mirror's own faces blend over what shows through. No
// stencil, no read-back: the CPU fallback reads the frame back and converts
// it, 190 ms a frame in Kay'l's apartment on the console. The
// reflected pass takes the draws in FRONT of the plane, as the fallback does -
// the port's own guard against what lies behind the mirror's wall.
// `OMK_CPU_MIRROR=1` keeps the fallback, for laying the two side by side.
// THE WATER REFLECTION (`todo/remaster.md` A3): the scene through the
// reflected camera into `reflFbo_` (single-sampled, the target's size), then
// the frame as usual with the water draws adding it from unit 4.
bool GlesRenderer::drawWaterScene(const View& v, const View& refl, std::span<const Draw> scene,
                                  std::span<const Draw> above, std::span<const Draw> water, float strength,
                                  float planeY) {
    // (no water draws: the REFLECTING FLOORS' frame - its floors are in
    // `scene`, `Draw::reflect` set)
    if (waterWant_ <= 0.0f || strength <= 0.0f || mainLoc_.water < 0) return false;
    reflHalf_ = water.empty();
    if (reflW_ != rw_ || reflH_ != rh_) {
        if (!reflFbo_) glGenFramebuffers(1, &reflFbo_);
        if (!reflTex_) glGenTextures(1, &reflTex_);
        if (!reflDepth_) glGenRenderbuffers(1, &reflDepth_);
        glBindTexture(GL_TEXTURE_2D, reflTex_);
        texParams(GL_LINEAR, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rw_, rh_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gpuTexture("render targets", reflTex_, 4LL * rw_ * rh_);
        glBindRenderbuffer(GL_RENDERBUFFER, reflDepth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, rw_, rh_);
        gpuRenderbuffer("render targets", reflDepth_, 2LL * rw_ * rh_);
        glBindFramebuffer(GL_FRAMEBUFFER, reflFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflTex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, reflDepth_);
        const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (st != GL_FRAMEBUFFER_COMPLETE) { std::fprintf(stderr, "gles: water reflection target incomplete\n"); return false; }
        reflW_ = rw_; reflH_ = rh_;
        std::printf("gles: the water reflection's target - %dx%d (a REMASTER feature)\n", rw_, rh_);
    }
    passFbo_ = reflFbo_;
    begin(refl);
    reflectingNow_ = true;
    waterY_ = planeY;
    for (const auto& d : above) submit(d);
    reflectingNow_ = false;
    // ...and THE SECOND LEVEL's (`drawTwoReflections`), in the right half's
    // quarter - a pass of the same frame
    if (secondView_) {
        reflSecond_ = true;
        beginPass(*secondView_);
        reflectingNow_ = true;
        waterY_ = secondY_;
        for (const auto& d : secondAbove_) submit(d);
        reflectingNow_ = false;
        reflSecond_ = false;
    }
    passFbo_ = 0;
    recording_ = false;
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, reflTex_);
    glActiveTexture(GL_TEXTURE0);
    beginPass(v);   // the same frame as the reflection's pass: one `frameNo_`
    waterStrength_ = strength;
    for (int k = 0; k < 3; ++k) waterEye_[k] = v.cam.eye[k];
    rain_ = v.rain;                 // the wet floors' ripples (`View::rain`)
    rippleClock_ = v.shimmerClock;
    for (const auto& d : scene) submit(d);
    // ...then the SECOND level's floors again, each in an occlusion query
    // (`secondLevelSeen`), Vulkan's probe: after the whole scene, depth
    // LEQUAL, neither depth nor colour written
    levelIssued_ = 0;
    levelOver_ = false;
#if OMK_GLES_QUERY
    bool marked = false;
    for (const auto& d : scene) marked |= d.reflectPlane == 1;
    if (water.empty() && marked) {
        if (!levelQ_[0]) glGenQueries(kLevelQueries, levelQ_);
        forgetState();
        probeNow_ = true;
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        for (const auto& d : scene) {
            if (d.reflectPlane != 1) continue;
            if (levelIssued_ >= kLevelQueries) { levelOver_ = true; break; }
            glBeginQuery(OMK_GLES_QUERY_TARGET, levelQ_[levelIssued_++]);
            submit(d);
            glEndQuery(OMK_GLES_QUERY_TARGET);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_LESS);
        probeNow_ = false;
        forgetState();
    }
#endif
    waterNow_ = true;
    for (const auto& d : water) submit(d);
    waterNow_ = false;
    end();
    return true;
}

// THE SECOND LEVEL'S OCCLUSION TEST: the last floors' frame's queries - seen
// if any sample passed; -1 with none issued or a result not in yet (the
// level then counts as seen)
int GlesRenderer::secondLevelSeen() {
#if OMK_GLES_QUERY
    if (levelIssued_ <= 0) return -1;
    GLuint ready = 0;
    glGetQueryObjectuiv(levelQ_[levelIssued_ - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) return -1;
    int seen = levelOver_ ? 1 : 0;
    for (int i = 0; i < levelIssued_; ++i) {
        GLuint n = 0;
        glGetQueryObjectuiv(levelQ_[i], GL_QUERY_RESULT, &n);
        if (n > 0) seen = 1;
    }
    levelIssued_ = 0;
    return seen;
#else
    return -1;
#endif
}

// THE SECOND REFLECTING LEVEL (`Renderer::drawTwoReflections`, Vulkan's): the
// floors' frame of `drawWaterScene` with a second reflection after the first,
// and each floor sampling its own quarter (`uReflOff`, `uReflBox`)
bool GlesRenderer::drawTwoReflections(const View& v, const View& refl, std::span<const Draw> scene,
                                      std::span<const Draw> above, float strength, float planeY,
                                      const View& refl2, std::span<const Draw> above2, float planeY2) {
    if (!drawsTwoReflections()) return false;
    twoRefl_ = true;
    secondView_ = &refl2;
    secondAbove_ = above2;
    secondY_ = planeY2;
    const bool ok = drawWaterScene(v, refl, scene, above, {}, strength, planeY);
    secondView_ = nullptr;
    secondAbove_ = {};
    twoRefl_ = false;
    return ok;
}

// THE UPSCALE's two passes (`kEasuFrag`, `kRcasFrag`): `colour_`'s drawn
// picture, `rs(w_) x rs(vh)`, up to `w_ x vh` in `easuTex_`'s top rows, then
// sharpened into `rcasTex_`'s - which the present pass and `readback` then
// read exactly as they read `colour_` unscaled. The rows below the picture
// are cleared black, as the world target's are.
void GlesRenderer::fsrPass(int vh) {
    vh = std::clamp(vh, 1, h_);
    if (fsrVh_ == vh && fsrW_ == w_ && fsrH_ == h_) return;   // this frame's, already
    if (fsrLoc_[0] < 0) {
        const char* easu[6] = {"uDst", "uSrc", "uSrcTex", "uIn", "uOut", "uOutTexH"};
        const char* rcas[5] = {"uDst", "uSrc", "uSrcTex", "uPic", "uSharp"};
        for (int k = 0; k < 6; ++k) fsrLoc_[k] = glGetUniformLocation(easuProg_, easu[k]);
        for (int k = 0; k < 5; ++k) fsrLoc_[6 + k] = glGetUniformLocation(rcasProg_, rcas[k]);
    }
    if (fsrW_ != w_ || fsrH_ != h_) {
        for (const auto& [tex, fbo] : {std::pair<GLuint*, GLuint*>{&easuTex_, &easuFbo_}, {&rcasTex_, &rcasFbo_}}) {
            if (!*tex) glGenTextures(1, tex);
            glBindTexture(GL_TEXTURE_2D, *tex);
            texParams(GL_NEAREST, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gpuTexture("render targets", *tex, 4LL * w_ * h_);
            if (!*fbo) glGenFramebuffers(1, fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
        }
        fsrW_ = w_; fsrH_ = h_;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    for (GLuint a = 1; a <= kAttrPhase; ++a) glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    const float whole[4] = {-1.0f, -1.0f, 2.0f, 2.0f};
    const auto run = [&](GLuint fbo, GLuint prog, GLuint src, const GLint* loc) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w_, h_);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, h_ - vh, w_, vh);   // the picture's rows, the target's top
        glUseProgram(prog);
        curProg_ = prog;
        glUniform4fv(loc[0], 1, whole);
        glUniform1i(loc[1], 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
    };
    const GLint* e = fsrLoc_;
    const GLint* r = fsrLoc_ + 6;
    run(easuFbo_, easuProg_, colour_, e);
    glUniform2f(e[2], static_cast<float>(rw_), static_cast<float>(rh_));
    glUniform2f(e[3], static_cast<float>(rs(w_)), static_cast<float>(rs(vh)));
    glUniform2f(e[4], static_cast<float>(w_), static_cast<float>(vh));
    glUniform1f(e[5], static_cast<float>(h_));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    run(rcasFbo_, rcasProg_, easuTex_, r);
    glUniform2f(r[2], static_cast<float>(w_), static_cast<float>(h_));
    glUniform2f(r[3], static_cast<float>(w_), static_cast<float>(vh));
    glUniform1f(r[4], std::exp2(-omk::kFsrRcasStops));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_DEPTH_TEST);
    forgetState();
    fsrVh_ = vh;
}

// A pack texture in ASTC (`o3de/texpack.h` PackBlocks) into the BOUND
// texture: every level its file carries when `mips`, else level 0 alone. The
// format's GL name from its block size (`GL_KHR_texture_compression_astc_ldr`).
// -> false when `mips` asked a chain the file does not hold WHOLE (to 1x1):
// GLES2 has no `GL_TEXTURE_MAX_LEVEL`, and an incomplete chain samples black
// under a mipmap filter - the caller then drops the filter's mip (the audit's
// item 9; every ASTC of the pack holds its whole chain, 2026-10-26)
bool GlesRenderer::uploadAstc(const omk::PackBlocks& z, bool mips) {
    const omk::BlockFormat f = omk::blockFormat(z.vkFormat);
    const GLenum gl = f.bw == 4 ? 0x93B0 : f.bw == 5 ? 0x93B2 : f.bw == 6 ? 0x93B4 : 0x93B7;   // 4x4 5x5 6x6 8x8
    std::size_t whole = 1;
    for (int s = std::max(z.w, z.h); s > 1; s >>= 1) ++whole;
    const bool chain = mips && z.levels.size() == whole;
    const std::size_t n = chain ? z.levels.size() : 1;
    for (std::size_t l = 0; l < n; ++l)
        glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), gl, std::max(1, z.w >> l), std::max(1, z.h >> l), 0,
                               static_cast<GLsizei>(z.levels[l].second), z.bytes.data() + z.levels[l].first);
    return chain || !mips;
}

// the city grade, any time: n^3 RGB 0..1, red fastest
bool GlesRenderer::setGrade(const float* rgb, int n) {
    if (!rgb || n < 2) { lutN_ = 0; return true; }
    lutStrip(rgb, n, lutData_);
    lutN_ = n; lutDirty_ = true;
    return true;
}

bool GlesRenderer::packActive() const { return texturePack().shown(); }   // hidden: the original mode (part B)

std::vector<PackImage> GlesRenderer::packDecode(std::span<const Texture* const> ts) const {
    return texturePack().replacements(ts, true, false);
}

bool GlesRenderer::astcBlocks(const PackBlocks& z) { return !z.empty() && blockFormat(z.vkFormat).astc; }

// THE PRELOAD RADIUS (`Renderer::setPreloadTextures`, `todo/remaster.md`): a
// prepared set's textures, PINNED - `setTextures`' eviction spares them while
// they are listed, so an airlock's round trip does not drop and reload the
// city's pool, and a set arriving finds its textures on the GPU. Those not
// there yet wait in `preQueue_` for `preloadStep`. A new list replaces the
// old; what drops off it is evicted by the next `setTextures`.
void GlesRenderer::setPreloadTextures(std::span<const Texture> t) {
    for (auto& [key, u] : uploaded_) u.pinned = false;
    preQueue_.clear();
    for (const auto& x : t) {
        if (!x.hasPixels()) continue;
        const auto key = std::make_tuple(x.idx.data(), x.pal.data(), x.width, x.height);
        if (auto hit = uploaded_.find(key); hit != uploaded_.end()) hit->second.pinned = true;
        else preQueue_.push_back(x);   // shares its pixel buffers: the key stays
    }
}

// up to `budget` of the waiting textures uploaded (the pack's replacements
// decoded first, most of them parked by the set's load); -> how many still wait
int GlesRenderer::preloadStep(int budget) {
    if (preQueue_.empty() || budget <= 0) return static_cast<int>(preQueue_.size());
    const double t0 = glesClockMs();
    const std::size_t n = std::min(preQueue_.size(), static_cast<std::size_t>(budget));
    std::vector<Texture> now(preQueue_.end() - static_cast<std::ptrdiff_t>(n), preQueue_.end());
    preQueue_.resize(preQueue_.size() - n);
    const bool packOn = packActive();
    std::vector<PackImage> reps;
    long prepared = 0;
    if (packOn) {
        std::vector<const Texture*> ts;
        for (const auto& x : now) ts.push_back(&x);
        const std::size_t taken0 = texturePack().parkedTaken();
        reps = packDecode(ts);
        prepared = static_cast<long>(texturePack().parkedTaken() - taken0);   // decoded ahead, on a load's thread
    }
    const double t1 = glesClockMs();
    std::vector<unsigned char> px;
    int fresh = 0, lit = 0;
    for (std::size_t i = 0; i < now.size(); ++i) {
        const auto key = std::make_tuple(now[i].idx.data(), now[i].pal.data(), now[i].width, now[i].height);
        if (auto hit = uploaded_.find(key); hit != uploaded_.end()) { hit->second.pinned = true; continue; }
        Uploaded u = uploadOne(now[i], packOn ? &reps[i] : nullptr, px, lit);
        u.used = false;
        u.pinned = true;
        uploaded_[key] = u;
        ++fresh;
    }
    const double t2 = glesClockMs();
    if (fresh > 0)
        std::printf("gles: preload - %d textures uploaded ahead, read and decode %.1f ms, uploads %.1f ms; "
                    "%zu still to come\n", fresh, t1 - t0, t2 - t1, preQueue_.size());
    omk::event("texpack.preload", {{"backend", "gles"}, {"fresh", fresh}, {"prepared", prepared},
                                   {"decodeMs", t1 - t0}, {"uploadMs", t2 - t1},
                                   {"waiting", static_cast<long>(preQueue_.size())}});
    return static_cast<int>(preQueue_.size());
}

// the scene and posing programs' sources through the variants asked for
void GlesRenderer::remasterSources(bool pointLit, const char*& sceneFragX, const char*& sceneFrag,
                                   const char*& vSceneX, const char*& vScene, const char*& vPosedX,
                                   const char*& vPosed) {
    // the remaster's variants over BOTH program sets, each from its own
    // sources: the plain set is the fallback when the enhanced one does not
    // link, and an enhanced fragment beside a plain vertex stage cannot link
    // (it reads `vNrm` / `vWorld`) - so it must not inherit the enhanced one's
    const auto fragOf = [&](const char* base, bool points) {
        // a relink keeps the bloom variant once built (`bloomBuilt_`), at 0 too
        std::string f = bloom_ > 0.0f || bloomBuilt_ ? bloomVariant(base) : std::string(base);
        if (points) f = pointLightVariant(f);
        if (waterWant_ > 0.0f) f = waterVariant(f);
        return f;
    };
    bloomFragX_ = fragOf(kSceneFragX, pointLit);
    bloomFrag_ = fragOf(kSceneFrag, false);
    sceneFragX = bloomFragX_.c_str();
    sceneFrag = bloomFrag_.c_str();
    // the water variant's fragment reads `vWy`: its vertex stages hand it down
    if (waterWant_ > 0.0f) {
        waterVertX_ = waterVertVariant(kSceneVertX);
        waterVert_ = waterVertVariant(kSceneVert);
        waterPosedVertX_ = waterVertVariant(kPosedVertX);
        waterPosedVert_ = waterVertVariant(kPosedVert);
        vSceneX = waterVertX_.c_str();
        vScene = waterVert_.c_str();
        vPosedX = waterPosedVertX_.c_str();
        vPosed = waterPosedVert_.c_str();
    }
    // the BODY LIGHT over either (only the posing program is a variant: it is
    // the one that draws the bodies)
    if (tintWant_) {
        tintPosedVertX_ = tintVariant(vPosedX);
        tintPosedVert_ = tintVariant(vPosed);
        vPosedX = tintPosedVertX_.c_str();
        vPosed = tintPosedVert_.c_str();
    }
}

// ...and their starting uniforms, once linked
void GlesRenderer::remasterProgramUniforms(bool pointLit) {
    if (waterWant_ > 0.0f) {
        // the reflection on unit 4
        for (const GLuint p : {prog_, posed_}) {
            if (!p) continue;
            glUseProgram(p);
            glUniform1i(glGetUniformLocation(p, "uRefl"), 4);
            glUniform1f(glGetUniformLocation(p, "uWater"), 0.0f);
            glUniform1f(glGetUniformLocation(p, "uWet"), 0.0f);
            glUniform1f(glGetUniformLocation(p, "uRain"), 0.0f);
            glUniform1f(glGetUniformLocation(p, "uSnow"), 0.0f);
            glUniform1f(glGetUniformLocation(p, "uWetBody"), 0.0f);
        }
        glUseProgram(0);
        std::printf("gles: the water reflection - the scene programs' water variant (unit 4)\n");
    }
    if (pointLit) {
        // the night at 1 until a frame says otherwise: a uniform starts at 0
        for (const GLuint p : {prog_, posed_}) {
            if (!p) continue;
            glUseProgram(p);
            glUniform1f(glGetUniformLocation(p, "uSetDim"), 1.0f);
        }
        glUseProgram(0);
    }
    if (tintWant_) {
        // 1, 1, 1 until a draw says otherwise: a uniform starts at 0, and the
        // pose self-test's bodies drawn BLACK read as never posed (all of
        // them went the CPU way, 2026-10-20)
        if (posed_) {
            glUseProgram(posed_);
            glUniform3f(glGetUniformLocation(posed_, "uLightTint"), 1.0f, 1.0f, 1.0f);
            glUseProgram(0);
        }
        std::printf("gles: the body light - the posing program's variant (%s)\n",
                    posed_ ? "built" : "not built - the bodies tinted on the CPU");
    }
    bloomBuilt_ = bloomBuilt_ || bloom_ > 0.0f;   // the variant `remasterSources` linked
    if (bloomBuilt_) {
        // the light maps on unit 3 (0 the texture, 1 the present's matrix, 2 the shadow map)
        for (const GLuint p : {prog_, posed_}) {
            if (!p) continue;
            glUseProgram(p);
            glUniform1i(glGetUniformLocation(p, "uEmis"), 3);
        }
        glUseProgram(0);
        std::printf("gles: bloom %.2f - the scene programs' bloom variant (light maps on unit 3)\n",
                    static_cast<double>(bloom_));
    }
}

// FSR's programs (`kEasuFrag`, `kRcasFrag`) when the upscale is asked for;
// a context that cannot build them refuses it - false only when the target,
// re-made at the output size, fails
bool GlesRenderer::remasterInitUpscale() {
    if (upscale_ > 1.0f) {
        // FSR WANTS HIGHP in the fragment stage: EASU's constants run down to
        // 1e-20 and its coordinates past 2048 texels, and where the device has
        // no highp float the prelude falls back to mediump - fp16 on a
        // Mali-400 class GPU, which flushes the one and loses the other's
        // texel (the audit's item 9). Refused there, not drawn wrong. A desktop
        // GL's 1.20 floats are fp32. `OMK_GLES_NO_HIGHP=1`, an instrument: the
        // refusal anywhere.
        bool highp = true;
#if !defined(__APPLE__)
        GLint range[2] = {0, 0}, bits = 0;
        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &bits);
        highp = bits > 0;
#endif
        if (omk::envFlag("OMK_GLES_NO_HIGHP")) highp = false;
        if (highp && !easuProg_) easuProg_ = link(kPresentVert, kEasuFrag, {{0, "aPos"}});   // once
        if (highp && !rcasProg_) rcasProg_ = link(kPresentVert, kRcasFrag, {{0, "aPos"}});
        if (!highp || !easuProg_ || !rcasProg_) {
            // a context that cannot build them, or run them right: refused,
            // not the renderer
            std::printf("gles: %s - upscale refused\n", highp ? "the upscale's programs did not build"
                                                             : "no highp float in the fragment stage, which FSR needs");
            omk::event("gles.upscale.refused", {{"highp", highp}});
            upscale_ = 1.0f;
            rw_ = w_; rh_ = h_;
            if (!allocTarget()) return false;
        } else {
            std::printf("gles: upscale x%.2f - the world drawn at %dx%d for %dx%d, brought up by AMD FSR 1 "
                        "(EASU, RCAS %.1f stops), a REMASTER feature\n", static_cast<double>(upscale_), rw_, rh_, w_, h_,
                        static_cast<double>(omk::kFsrRcasStops));
            omk::event("gles.upscale", {{"factor", static_cast<double>(upscale_)}, {"rw", rw_}, {"rh", rh_},
                                        {"w", w_}, {"h", h_}});
        }
    }
    return true;
}

// the pack's block formats the context samples
void GlesRenderer::remasterInitCodecs() {
    // THE PACK'S BLOCK FORMATS (`o3de/texpack.h`): ASTC, where the context
    // names `GL_KHR_texture_compression_astc_ldr` - Android's GPUs and the
    // Quest's; a desktop GL has no ASTC and keeps the PNGs (and no BC7: the
    // Mac's GL stops at 4.1, before BPTC). `OMK_NO_BLOCK_FORMATS=1` keeps
    // the PNGs anywhere.
    {
        const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        astcOk_ = ext && std::strstr(ext, "GL_KHR_texture_compression_astc_ldr") &&
                  !omk::envFlag("OMK_NO_BLOCK_FORMATS");
        omk::texturePack().setCodecs(astcOk_ ? omk::kCodecAstc : 0u);
        if (astcOk_ && omk::texturePack().active())
            std::printf("gles: the texture pack's ASTC files - %zu colours read as blocks\n",
                        omk::texturePack().blockFiles(omk::kCodecAstc));
    }
}

// ---- A DRAW'S REMASTER STATE (`submit`, moved here from the shared file -
// the audit's cleanliness, 2026-10-26) --------------------------------------

// A WET BODY posed on the CPU (`Draw::wetBody` with no pose - the player
// under the mapped shadows) reads its posed normals from their own buffer, as
// a lit draw does, for its sheen (the water variant's `vBodyN`)
bool GlesRenderer::remasterWantsNormals(const Draw& d) const {
    return d.wetBody && waterWant_ > 0.0f;
}

// THE WATER's uniforms, with the water variant built: its strength and the
// clip through the cache (every draw of every frame sent them, water or none),
// the water draws' and the reflection's own each time
void GlesRenderer::remasterWaterUniforms(const Draw& d, const SceneLoc& L, UniCache& U, bool uv) {
    const auto set = [&](bool changed) { return stateSet(changed); };
    // ...a REFLECTING FLOOR's run (`Draw::reflect`) at its own strength
    const bool floorNow = !reflectingNow_ && d.reflect > 0.0f;
    const float w = waterNow_ ? waterStrength_ : floorNow ? waterStrength_ * d.reflect : 0.0f;
    if (set(!uv || U.water != w)) {
        glUniform1f(L.water, w);
        U.water = w;
    }
    // ...and a WET floor's film (`Draw::wet`), in the frame's pass alone
    const float film = reflectingNow_ ? 0.0f : std::min(d.wet, 1.0f);
    if (set(!uv || U.wet != film)) {
        glUniform1f(L.wet, film);
        U.wet = film;
    }
    // THE MIST (`View::mist`): its frame's strength, ground and height - not
    // in the reflection's pass, as Vulkan's
    {
        const float m0 = reflectingNow_ ? 0.0f : mist_[0];
        if (set(!uv || U.mist[0] != m0 || U.mist[1] != mist_[1] || U.mist[2] != mist_[2])) {
            glUniform3f(L.mist, m0, mist_[1], mist_[2]);
            U.mist[0] = m0; U.mist[1] = mist_[1]; U.mist[2] = mist_[2];
        }
        // ...and THE SAND'S DUST (`View::dust`), not in the reflection's pass
        const float d0 = reflectingNow_ ? 0.0f : dust_[0];
        if (set(!uv || U.dust[0] != d0 || U.dust[1] != dust_[1] || U.dust[2] != dust_[2] || U.dust[3] != dust_[3])) {
            glUniform4f(L.dust, d0, dust_[1], dust_[2], dust_[3]);
            U.dust[0] = d0; U.dust[1] = dust_[1]; U.dust[2] = dust_[2]; U.dust[3] = dust_[3];
        }
        if (d0 > 0.0f && set(!uv || std::memcmp(U.dustColour, dustColour_, sizeof U.dustColour) != 0)) {
            glUniform3f(L.dustColour, dustColour_[0], dustColour_[1], dustColour_[2]);
            std::memcpy(U.dustColour, dustColour_, sizeof U.dustColour);
        }
        // ...and THE LIGHTS' STREAKS (`View::streakLights`), once a frame on a
        // program that draws a wet floor
        if (film > 0.0f && L.streakN >= 0 && (!uv || U.streakFrame != frameNo_)) {
            stateSet(true);
            if (streakN_[0] > 0.0f && L.streak >= 0)
                glUniform4fv(L.streak, 2 * static_cast<GLsizei>(streakN_[0]), streak_);
            glUniform2f(L.streakN, streakN_[0], streakN_[1]);
            U.streakFrame = frameNo_;
        }
        // ...and THE RAIN'S VEILS (`View::veil`), not in the reflection's pass
        const float v0 = reflectingNow_ ? 0.0f : veil_[0];
        if (set(!uv || U.veil[0] != v0 || U.veil[1] != veil_[1] || U.veil[2] != veil_[2] || U.veil[3] != veil_[3])) {
            glUniform4f(L.veil, v0, veil_[1], veil_[2], veil_[3]);   // w the PUDDLES' strength
            U.veil[0] = v0; U.veil[1] = veil_[1]; U.veil[2] = veil_[2]; U.veil[3] = veil_[3];
        }
        if (v0 > 0.0f && set(!uv || std::memcmp(U.veilColour, veilColour_, sizeof U.veilColour) != 0)) {
            glUniform4f(L.veilColour, veilColour_[0], veilColour_[1], veilColour_[2], veilColour_[3]);
            std::memcpy(U.veilColour, veilColour_, sizeof U.veilColour);
        }
        // ...whose rays start at the frame's eye: the vertex variant's `uEye`
        // (a wet body's sheen faces it too)
        if ((m0 > 0.0f || d0 > 0.0f || v0 > 0.0f || (film > 0.0f && d.wetBody)) && !(waterNow_ || floorNow) &&
            set(!uv || std::memcmp(U.eye, waterEye_, sizeof U.eye) != 0)) {
            glUniform3f(L.eye, waterEye_[0], waterEye_[1], waterEye_[2]);
            std::memcpy(U.eye, waterEye_, sizeof U.eye);
        }
    }
    // ...or SNOW lying (`Draw::snow`): `uWet` is then its cover
    const float snowNow = film > 0.0f && d.snow ? 1.0f : 0.0f;
    if (set(!uv || U.snow != snowNow)) {
        glUniform1f(L.snow, snowNow);
        U.snow = snowNow;
    }
    // ...or a BODY in the rain (`Draw::wetBody`): `uWet` is then how wet
    const float bodyNow = film > 0.0f && d.wetBody ? 1.0f : 0.0f;
    if (set(!uv || U.wetBody != bodyNow)) {
        glUniform1f(L.wetBody, bodyNow);
        U.wetBody = bodyNow;
    }
    // ...and its RIPPLES: the rain's strength and the shimmer's clock
    const float rain = film > 0.0f ? rain_ : 0.0f;
    if (set(!uv || U.rain != rain)) {
        glUniform1f(L.rain, rain);
        U.rain = rain;
    }
    if (rain > 0.0f && set(!uv || U.rippleClock != rippleClock_)) {
        glUniform1f(L.rippleClock, rippleClock_);
        U.rippleClock = rippleClock_;
    }
    if (waterNow_ || floorNow) {
        // (a floor samples the half-size reflection: half the scale)
        const float k = floorNow ? 0.5f : 1.0f;
        if (set(!uv || U.reflK != k)) {
            glUniform2f(L.reflInv, k / static_cast<float>(rw_), k / static_cast<float>(rh_));
            U.reflK = k;
        }
        // ...in a two-level frame, the floor's own quarter: the second's from
        // the right half, each clamped inside its own (the filter would read
        // the other's edge); else as before, no offset and no clamp
        const int plane = floorNow && twoRefl_ ? 1 + (d.reflectPlane == 1 ? 1 : 0) : 0;
        if (L.reflOff >= 0 && set(!uv || U.reflPlane != plane)) {
            const float iw = 1.0f / static_cast<float>(rw_), ih = 1.0f / static_cast<float>(rh_);
            const float x0 = plane == 2 ? static_cast<float>((rw_ + 1) / 2) * iw : 0.0f;
            const float qw = static_cast<float>((rw_ + 1) / 2) * iw, qh = static_cast<float>((rh_ + 1) / 2) * ih;
            glUniform2f(L.reflOff, x0, 0.0f);
            if (plane == 0) glUniform4f(L.reflBox, -1e4f, -1e4f, 1e4f, 1e4f);
            else glUniform4f(L.reflBox, x0 + 0.5f * iw, 0.5f * ih, std::min(x0 + qw, 1.0f) - 0.5f * iw, qh - 0.5f * ih);
            U.reflPlane = plane;
        }
        if (set(!uv || std::memcmp(U.eye, waterEye_, sizeof U.eye) != 0)) {
            glUniform3f(L.eye, waterEye_[0], waterEye_[1], waterEye_[2]);
            std::memcpy(U.eye, waterEye_, sizeof U.eye);
        }
    }
    const int clip = reflectingNow_ ? 1 : 0;
    if (set(!uv || U.clip != clip)) {
        glUniform1i(L.clip, clip);
        U.clip = clip;
    }
    if (reflectingNow_ && set(!uv || U.clipY != waterY_)) {
        glUniform1f(L.clipY, waterY_);
        U.clipY = waterY_;
    }
}

// THE BLOOM's light map on unit 3, with the bloom on: the texture's own, or
// the black 1x1 of one with none
void GlesRenderer::remasterLightMap(const Tex& t) {
    if (!lightBlack_) {
        const unsigned char z[4] = {0, 0, 0, 255};
        glGenTextures(1, &lightBlack_);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, lightBlack_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, z);
        gpuTexture("texture stand-ins", lightBlack_, 4);
        glActiveTexture(GL_TEXTURE0);
        lastEmis_ = 0;
    }
    const GLuint em = t.em ? t.em : lightBlack_;
    lightMapNow_ |= t.em != 0;
    if (em != lastEmis_) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, em);
        glActiveTexture(GL_TEXTURE0);
        lastEmis_ = em;
    }
}

// its GL objects, at the renderer's end
void GlesRenderer::remasterRelease() {
    if (postProg_) glDeleteProgram(postProg_);
    if (postSrc_) deleteTextures(1, &postSrc_);
    for (int k = 0; k < 2; ++k) {
        if (glowTex_[k]) deleteTextures(1, &glowTex_[k]);
        if (glowFbo_[k]) glDeleteFramebuffers(1, &glowFbo_[k]);
    }
    if (lutTex_) deleteTextures(1, &lutTex_);
    if (lightBlack_) deleteTextures(1, &lightBlack_);
    if (reflTex_) deleteTextures(1, &reflTex_);
    if (reflDepth_) deleteRenderbuffers(1, &reflDepth_);
    if (reflFbo_) glDeleteFramebuffers(1, &reflFbo_);
#if OMK_GLES_QUERY
    if (levelQ_[0]) glDeleteQueries(kLevelQueries, levelQ_);
#endif
}

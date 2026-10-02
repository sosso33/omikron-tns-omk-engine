// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S PRESENT PHASE - the fps counter, the fades, the flicker catcher and the present.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
#include "playframe.h"

int PlayFrame::phasePresent() {
    bool done = false;
    do {
        {
            const double pr0 = phaseNow();
            mark("fades, flicker");
            // a frame nothing drew over goes straight from the world target
            // to the window; anything else, or a backend that refuses, is the
            // composed `fb` as before
            bool presentedWorld = false;
#if defined(OMK_GLES)
            // `OMK_VERIFY_GPU_PRESENT` on the GLES window: present the world
            // directly, read the window back and compare it byte for byte with
            // the CPU frame composed beside it - the Vulkan verify's GLES twin.
            // Only where the window is the frame's own size (1:1).
            if (gpuFrame && verifyGpuPresent && glRen) {
                int ww = 0, wh = 0;
#if defined(OMK_SDL3)
                SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
                SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
                if (ww == fb.w && wh == fb.h &&
                    omk::glesPresentWorld(glRen, gpuVy, gpuVh, fb.w, fb.h, ww, wh)) {
                    static std::vector<unsigned char> winPic;
                    static long compared = 0, differing = 0;
                    omk::glesWindowPicture(glRen, ww, wh, winPic);
                    // compared in 565: the driver's 565 -> 888 expansion is
                    // its own (rounding on a Mac), so an 888 compare reports
                    // the driver (todo/vita-port.md, the 113303 differences)
                    long diff = 0;
                    for (std::size_t i = 0; i < fb.px.size(); ++i) {
                        const unsigned char* q = &winPic[4 * i];
                        const int r5 = (q[0] * 31 + 127) / 255, g6 = (q[1] * 63 + 127) / 255,
                                  b5 = (q[2] * 31 + 127) / 255;
                        if (static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5) != fb.px[i]) ++diff;
                    }
                    ++compared;
                    if (diff) ++differing;
                    if (diff || compared % 30 == 0)
                        std::printf("gles present verify: frame %ld, %ld pixels differ; %ld of %ld frames differed\n",
                                    n, diff, differing, compared);
                    SDL_GL_SwapWindow(glWin);
                    presentedWorld = true;
                }
            }
#endif
#if defined(OMK_GLES)
            if (overlayFrame && glRen) {
                // the key resolves into the two planes: C in `fb`, M beside it
                // - only on the rows a pass touched; an untouched key pixel stays
                // the key, which the shader reads as "the world" by itself, so
                // M is zero everywhere else and mostly never changes
                static std::vector<std::uint8_t> ovMask, ovMaskRow;
                if (ovMask.size() != fb.px.size()) {
                    ovMask.assign(fb.px.size(), std::uint8_t(0));
                    ovMaskRow.assign(static_cast<std::size_t>(fb.h), 0);
                }
                for (int y = 0; y < fb.h; ++y) {
                    const std::size_t o = static_cast<std::size_t>(y) * fb.w;
                    if (!g_ov.rowInit[static_cast<std::size_t>(y)]) {
                        if (ovMaskRow[static_cast<std::size_t>(y)]) {
                            std::fill(ovMask.begin() + o, ovMask.begin() + o + fb.w, std::uint8_t(0));
                            ovMaskRow[static_cast<std::size_t>(y)] = 0;
                        }
                        continue;
                    }
                    ovMaskRow[static_cast<std::size_t>(y)] = 1;
                    for (std::size_t i = o; i < o + static_cast<std::size_t>(fb.w); ++i) {
                        if (fb.px[i] == kOverlayKey) { fb.px[i] = g_ov.c[i]; ovMask[i] = g_ov.m[i]; }
                        else ovMask[i] = 0;
                    }
                }
                int ww = 0, wh = 0;
#if defined(OMK_SDL3)
                SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
                SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
                presentedWorld = omk::glesPresentOverlay(glRen, fb, ovMask.data(), ovMaskRow.data(), ovFade, gpuVy, gpuVh, ww, wh);
                if (presentedWorld) {
                    static const char* winDump = std::getenv("OMK_GLES_WINDUMP");
                    if (winDump && frames && n + 1 >= frames) {
                        std::vector<unsigned char> pic;
                        omk::glesWindowPicture(glRen, ww, wh, pic);
                        if (omk::safeOutputPath(winDump)) {
                            std::ofstream o(winDump, std::ios::binary);
                            o.write(reinterpret_cast<const char*>(pic.data()), static_cast<std::streamsize>(pic.size()));
                        }
                    }
                    SDL_GL_SwapWindow(glWin);
                }
            }
#endif
            if (gpuFrame && !verifyGpuPresent) {
#if defined(OMK_VULKAN)
                if (vkRen) presentedWorld = omk::vulkanPresentWorld(vkRen, gpuVy, gpuVh);
#endif
#if defined(OMK_GLES)
                if (!presentedWorld && glRen) {
                    int ww = 0, wh = 0;
#if defined(OMK_SDL3)
                    SDL_GetWindowSizeInPixels(glWin, &ww, &wh);
#else
                    SDL_GL_GetDrawableSize(glWin, &ww, &wh);
#endif
                    presentedWorld = omk::glesPresentWorld(glRen, gpuVy, gpuVh, fb.w, fb.h, ww, wh);
                    if (presentedWorld) {
                        const auto sw0 = SDL_GetPerformanceCounter();
                        SDL_GL_SwapWindow(glWin);
                        glSwapMs += static_cast<double>(SDL_GetPerformanceCounter() - sw0) * 1000.0 /
                                    static_cast<double>(SDL_GetPerformanceFrequency());
                    }
                }
#endif
            }
            if (!presentedWorld) present(fb);
            const double pr1 = phaseNow();
            mark("present, swap");
            for (std::size_t k = 1; k < phMarks.size(); ++k)
                phSection[phMarks[k].first] += phMarks[k].second - phMarks[k - 1].second;
            if (phRb0 < 0.0) phRb0 = phRb1 = pr0;   // a frame with no readback
            phSum[0] += phRb0 - phTop;
            phSum[1] += phRb1 - phRb0;
            phSum[2] += pr0 - phRb1;
            phSum[3] += pr1 - pr0;
            if (++phN == 60) {
                std::printf("frame %ld phases (ms, mean of 60): sim+draw %.1f, readback %.1f, "
                            "compose %.1f, present %.1f\n", n, phSum[0] * 1000.0 / 60.0,
                            phSum[1] * 1000.0 / 60.0, phSum[2] * 1000.0 / 60.0,
                            phSum[3] * 1000.0 / 60.0);
#if defined(OMK_GLES)
                if (glRen) {
                    double g[4];
                    omk::glesTakeTimings(g);
                    std::printf("frame %ld gles (ms, mean of 60): glReadPixels %.1f, to-565 %.1f, "
                                "texture upload %.1f, present draw %.1f, swap %.1f; "
                                "%.0f buffer patches a frame (%.0f the depth tie's)\n", n,
                                g[0] / 60.0, g[1] / 60.0, g[2] / 60.0, g[3] / 60.0, glSwapMs / 60.0,
                                omk::glesTakePatches() / 60.0, omk::glesTakeTiePatches() / 60.0);
                    {
                        // the draw-state cache's work (todo/optimization.md step 17)
                        long sc[3];
                        omk::glesTakeStateCalls(sc);
                        std::printf("frame %ld gles state (a frame, mean of 60): %.0f draws, "
                                    "%.0f state calls made, %.0f skipped\n", n,
                                    sc[0] / 60.0, sc[1] / 60.0, sc[2] / 60.0);
                    }
                    {
                        double gw[7];
                        omk::glesTakeWindow(gw);
                        std::printf("frame %ld gles world (a frame, mean of 60): %.1f vertex uploads, "
                                    "%.1f of them whole, %.1f streamed, %.0f KB sent, %.1f ms; draws %.1f ms, "
                                    "ties %.1f ms\n", n,
                                    gw[0] / 60.0, gw[5] / 60.0, gw[6] / 60.0, gw[1] / 60.0, gw[2] / 60.0,
                                    gw[3] / 60.0, gw[4] / 60.0);
                    }
                    std::printf("frame %ld overlay: %ld plane rows re-sent in 60 frames\n", n,
                                omk::glesTakeOverlayRows(glRen));
                    glSwapMs = 0.0;
                }
#endif
                std::printf("frame %ld present: %ld of 60 straight from the GPU; on the CPU:", n, phGpu);
                for (const auto& [why, count] : phKept) std::printf(" %s %ld,", why.c_str(), count);
                std::printf("\n");
                phGpu = 0;
                phKept.clear();
                std::printf("frame %ld spans (ms, mean of 60):", n);
                for (auto& [name, sec] : phSpan) {
                    std::printf(" %s %.1f,", name.c_str(), sec * 1000.0 / 60.0);
                    sec = 0.0;
                }
                std::printf("\n");
                {
                    // the sections, largest first; each is the gap ENDING at its mark
                    std::vector<std::pair<double, std::string>> secs;
                    for (auto& [name, sec] : phSection) {
                        if (sec * 1000.0 / 60.0 >= 0.1) secs.emplace_back(sec * 1000.0 / 60.0, name);
                        sec = 0.0;
                    }
                    std::sort(secs.begin(), secs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                    std::printf("frame %ld sections (ms, mean of 60):", n);
                    for (const auto& [ms, name] : secs) std::printf(" %s %.1f,", name.c_str(), ms);
                    std::printf("\n");
                }
                phSum[0] = phSum[1] = phSum[2] = phSum[3] = 0.0;
                phN = 0;
            }
        }
        if (!snapsDir.empty() && handoverFrame >= 0 && ((n - handoverFrame) % snapEvery) == 0) {
            const std::string path = snapsDir + "/snap-" + std::to_string(n) + ".bin";
            if (omk::safeOutputPath(path)) {
                std::ofstream o(path, std::ios::binary);
                for (auto v : fb.px) {
                    const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
                    o.write(b2, 2);
                }
            }
        }
        ++n;
        if (frames && n >= frames) break;
        // 30 Hz, PORTING A7 - and it is a CAP, not an addition. A flat
        // `SDL_Delay(33)` sleeps a whole frame budget ON TOP of however long
        // the frame took, so a 30 ms software frame ran the loop at ~16 fps
        // and halved the apparent speed of everything. Sleep only what is left
        // of the budget, and nothing at all when the frame overran it.
        //
        // ...and it is a DEADLINE on a 1/30 s grid, not a sleep of whole
        // milliseconds (todo/optimization.md, "Capped at 30"). The first cap
        // slept `33 - spent` ms: a 33 ms budget in integer ms, plus whatever
        // `SDL_Delay` oversleeps, so a frame with time to spare still averaged
        // ~34.7 ms and the street held a flat 28.8 fps with every 1 s window's
        // worst frame at 36-40 ms. Now each frame ends at the next multiple of
        // 1/30 s on the performance counter: sleep in whole milliseconds to
        // 1.5 ms short of it, then yield the rest. A frame that ran a little
        // late shortens the next budget, so the AVERAGE stays 30; one that
        // fell more than a whole frame behind resynchronises instead of
        // bursting to catch up. The simulation is untouched - it steps on the
        // measured delta above - and `--frames` runs never reach here.
        if (!frames) {
            static const double perfHz = static_cast<double>(SDL_GetPerformanceFrequency());
            const double kPeriod = 1.0 / static_cast<double>(frameRate);   // row 11
            const auto nowSec = [] {
                return static_cast<double>(SDL_GetPerformanceCounter()) / perfHz;
            };
            double now = nowSec();
            // A SLOW FRAME, SAID - an instrument. A reader reported "small freezes
            // on the streets" (2026-09-17) and the log held nothing to attribute
            // them with: the work of a frame is the time from the last pacer exit
            // to this entry, and one over two periods is written down with where
            // he was, so a hitch can be laid beside what else the log says then.
            static double paceLeft = 0.0;
            if (paceLeft > 0.0 && now - paceLeft > 2.0 * kPeriod)
                std::printf("frame %ld: SLOW FRAME - %.0f ms of work (the budget is 33)%s\n", n,
                            (now - paceLeft) * 1000.0,
                            player ? (" at " + std::to_string(int(player->pos()[0])) + " " +
                                      std::to_string(int(player->pos()[2]))).c_str() : "");
            // ...and one over OMK_MARKS_MS (150) says which SECTIONS: the
            // gaps between the marks, largest first, the top eight
            static const double marksMs = std::getenv("OMK_MARKS_MS") ? std::atof(std::getenv("OMK_MARKS_MS")) : 150.0;
            if (paceLeft > 0.0 && (now - paceLeft) * 1000.0 > marksMs && phMarks.size() > 1) {
                std::vector<std::pair<double, const char*>> gaps;
                for (std::size_t k = 1; k < phMarks.size(); ++k)
                    gaps.emplace_back((phMarks[k].second - phMarks[k - 1].second) * 1000.0, phMarks[k].first);
                std::sort(gaps.begin(), gaps.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                std::printf("frame %ld: sections -", n);
                for (std::size_t k = 0; k < gaps.size() && k < 8; ++k)
                    std::printf(" %s %.1f ms,", gaps[k].second, gaps[k].first);
                std::printf(" (%zu marks, %.1f ms top to last)\n", phMarks.size(),
                            (phMarks.back().second - phMarks.front().second) * 1000.0);
            }
            // ...and a VERY slow one says where it went: this frame's own
            // simulation-and-submission span, and the GL backend's counts
            if (paceLeft > 0.0 && now - paceLeft > 0.5) {
                std::printf("frame %ld: of which sim+draw %.0f ms", n, (phRb0 - phTop) * 1000.0);
#if defined(OMK_GLES)
                if (glRen) std::printf("; %s", omk::glesFrameReport().c_str());
#endif
                std::printf("\n");
            }
            if (paceNext <= 0.0 || now > paceNext + kPeriod) paceNext = now;
            while (now < paceNext) {
                const double left = paceNext - now;
                SDL_Delay(left > 0.002 ? static_cast<Uint32>((left - 0.0015) * 1000.0) : 0);
                now = nowSec();
            }
            paceNext += kPeriod;
            paceLeft = nowSec();
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}

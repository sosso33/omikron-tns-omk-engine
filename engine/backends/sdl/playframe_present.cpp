// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S PRESENT PHASE - the fps counter, the fades, the flicker catcher and the present.
// Part of `main`'s loop body, moved byte for byte by `todo/play-split.md`
// (2026-10-02); see `playframe.h` for what the names below refer to.
#include "playframe.h"

bool omkInstrumentsBuilt();   // playharness.cpp / playharness_off.cpp

int PlayState::phasePresent() {
    bool done = false;
    do {
        {
            const double pr0 = phaseNow();
            mark("fades, flicker");
            // a frame nothing drew over goes straight from the world target
            // to the window; anything else, or a backend that refuses, is the
            // composed `fb` as before
            bool presentedWorld = false;
            gpuPresentVerify(presentedWorld);    // the GLES window's verify
            gpuPresentOverlay(presentedWorld);   // the GLES window's overlay pass
            if (gpuFrame && !verifyGpuPresent) gpuPresentWorld(presentedWorld);
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
                gpuReportTimings();   // the GLES backend's counters
                // counted by the instruments (`playharness.cpp`): a build
                // without them would print "0 of 60" for every frame
                if (omkInstrumentsBuilt()) {
                    std::printf("frame %ld present: %ld of 60 straight from the GPU; on the CPU:", n, phGpu);
                    for (const auto& [why, count] : phKept) std::printf(" %s %ld,", why.c_str(), count);
                    std::printf("\n");
                }
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
        harnessSnaps();
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
            static const double perfHz = static_cast<double>(front.perfFrequency());
            const double kPeriod = 1.0 / static_cast<double>(frameRate);   // row 11
            const auto nowSec = [this] {
                return static_cast<double>(front.perfCounter()) / perfHz;
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
                gpuSlowFrameReport();
                std::printf("\n");
            }
            if (paceNext <= 0.0 || now > paceNext + kPeriod) paceNext = now;
            while (now < paceNext) {
                const double left = paceNext - now;
                front.delayMs(left > 0.002 ? static_cast<std::uint32_t>((left - 0.0015) * 1000.0) : 0);
                now = nowSec();
            }
            paceNext += kPeriod;
            paceLeft = nowSec();
        }
        done = true;
    } while (false);
    return done ? -1 : -2;
}

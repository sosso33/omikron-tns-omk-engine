// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME'S PRESENT PHASE - the fps counter, the fades, the flicker catcher and the present.
// Part of `main`'s loop body, split out by `todo/play-split.md`; see `playframe.h` for what the names below refer to.
#include "playframe.h"
#include "platform/env.h"

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
            if (!presentedWorld) {
                // a straight frame whose present was refused: its `fb` was never
                // cleared (`phaseWorld`'s deferred clear) - cleared now, as it was
                if (fbStale) clearFrame();
                present(fb);
            }
            const double pr1 = phaseNow();
            mark("present, swap");
            for (std::size_t k = 1; k < phases.marks.size(); ++k)
                phases.section[phases.marks[k].first] += phases.marks[k].second - phases.marks[k - 1].second;
            if (phases.rb0 < 0.0) phases.rb0 = phases.rb1 = pr0;   // a frame with no readback
            phases.sum[0] += phases.rb0 - phases.top;
            phases.sum[1] += phases.rb1 - phases.rb0;
            phases.sum[2] += pr0 - phases.rb1;
            phases.sum[3] += pr1 - pr0;
            if (++phases.n == 60) {
                std::printf("frame %ld phases (ms, mean of 60): sim+draw %.1f, readback %.1f, "
                            "compose %.1f, present %.1f\n", frameNo, phases.sum[0] * 1000.0 / 60.0,
                            phases.sum[1] * 1000.0 / 60.0, phases.sum[2] * 1000.0 / 60.0,
                            phases.sum[3] * 1000.0 / 60.0);
                gpuReportTimings();   // the GLES backend's counters
                // counted by the instruments (`playharness.cpp`): a build
                // without them would print "0 of 60" for every frame
                if (omkInstrumentsBuilt()) {
                    std::printf("frame %ld present: %ld of 60 straight from the GPU; on the CPU:", frameNo, phases.gpu);
                    for (const auto& [why, count] : phases.kept) std::printf(" %s %ld,", why.c_str(), count);
                    std::printf("\n");
                }
                phases.gpu = 0;
                phases.kept.clear();
                std::printf("frame %ld spans (ms, mean of 60):", frameNo);
                for (const auto& [name, sec] : phases.span.byName())
                    std::printf(" %s %.1f,", name, sec * 1000.0 / 60.0);
                phases.span.zero();
                std::printf("\n");
                {
                    // the sections, largest first; each is the gap ENDING at its mark
                    std::vector<std::pair<double, std::string>> secs;
                    for (const auto& [name, sec] : phases.section.byName())
                        if (sec * 1000.0 / 60.0 >= 0.1) secs.emplace_back(sec * 1000.0 / 60.0, name);
                    phases.section.zero();
                    std::sort(secs.begin(), secs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                    std::printf("frame %ld sections (ms, mean of 60):", frameNo);
                    for (const auto& [ms, name] : secs) std::printf(" %s %.1f,", name.c_str(), ms);
                    std::printf("\n");
                }
                phases.sum[0] = phases.sum[1] = phases.sum[2] = phases.sum[3] = 0.0;
                phases.n = 0;
            }
        }
        harnessSnaps();
        ++frameNo;
        if (frames && frameNo >= frames) break;
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
            // A SLOW FRAME, SAID - an instrument, so that a hitch on the streets
            // can be attributed from the log: the work of a frame is the time
            // from the last pacer exit to this entry, and one over two periods is
            // written down with where he was, so a hitch can be laid beside what
            // else the log says then.
            static double paceLeft = 0.0;
            if (paceLeft > 0.0 && now - paceLeft > 2.0 * kPeriod)
                std::printf("frame %ld: SLOW FRAME - %.0f ms of work (the budget is 33)%s\n", frameNo,
                            (now - paceLeft) * 1000.0,
                            player ? (" at " + std::to_string(int(player->pos()[0])) + " " +
                                      std::to_string(int(player->pos()[2]))).c_str() : "");
            // ...and one over OMK_MARKS_MS (150) says which SECTIONS: the
            // gaps between the marks, largest first, the top eight
            static const double marksMs = omk::envNum("OMK_MARKS_MS", 150.0);
            if (paceLeft > 0.0 && (now - paceLeft) * 1000.0 > marksMs && phases.marks.size() > 1) {
                std::vector<std::pair<double, const char*>> gaps;
                for (std::size_t k = 1; k < phases.marks.size(); ++k)
                    gaps.emplace_back((phases.marks[k].second - phases.marks[k - 1].second) * 1000.0, phases.marks[k].first);
                std::sort(gaps.begin(), gaps.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                std::printf("frame %ld: sections -", frameNo);
                for (std::size_t k = 0; k < gaps.size() && k < 8; ++k)
                    std::printf(" %s %.1f ms,", gaps[k].second, gaps[k].first);
                std::printf(" (%zu marks, %.1f ms top to last)\n", phases.marks.size(),
                            (phases.marks.back().second - phases.marks.front().second) * 1000.0);
            }
            // ...and a VERY slow one says where it went: this frame's own
            // simulation-and-submission span, and the GL backend's counts
            if (paceLeft > 0.0 && now - paceLeft > 0.5) {
                std::printf("frame %ld: of which sim+draw %.0f ms", frameNo, (phases.rb0 - phases.top) * 1000.0);
                gpuSlowFrameReport();
                std::printf("\n");
            }
            if (paceNext <= 0.0 || now > paceNext + kPeriod) paceNext = now;
            // ...unless the HOST paces (a headset's `xrWaitFrame`, step 5b)
            {
                // the WAIT for the frame's slot, apart from the work (`present`
                // holds both; the iBook's profiles read the two as one)
                OMK_ZONE("frame: pace");
                while (!front.paces() && now < paceNext) {
                    const double left = paceNext - now;
                    front.delayMs(left > 0.002 ? static_cast<std::uint32_t>((left - 0.0015) * 1000.0) : 0);
                    now = nowSec();
                }
            }
            paceNext += kPeriod;
            paceLeft = nowSec();
        }
        done = true;
    } while (false);
    return done ? phase::Continue : phase::EndLoop;
}

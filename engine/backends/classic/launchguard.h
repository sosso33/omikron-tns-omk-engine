// SPDX-License-Identifier: GPL-3.0-or-later
// THE LAUNCH GUARD - what keeps Mac OS 9 from freezing at the same place twice
// (`todo/handoff-classic-mac.md` 3i). On OS 9 a call that hangs in a driver
// (a real 3D card's AGL, the Sound Manager, QuickTime) takes the whole
// computer down, and the reader's iBook G3 froze at the start. The emulated
// OS 9 cannot show which call: it has no 3D card, so the accelerated path
// never runs there.
//
//   * each `checkpoint` (carbonfront.cpp) on OS 9 also writes the step it is
//     about to take into `omk.guard` beside the application, flushed;
//   * the guard is REMOVED once the run has gone 15 s past its last
//     checkpoint, and at a clean exit;
//   * a guard found at launch means the last run stopped inside that step.
//     Its log is kept as `omk-freeze.txt` (the new run would overwrite
//     `omk-out.txt`), and the step is written into `omk.safe`, which the
//     launch reads every time: `opengl` runs the software renderer
//     (`--software`), `quicktime` the software film decoder, `sound` no audio
//     device. Deleting `omk.safe` tries everything again.
//
// Mac OS X never writes a guard: a hung application there is force-quit, not
// the machine.
#pragma once

#include <string>
#include <vector>

// At launch, BEFORE the log is opened: a guard left behind is turned into an
// `omk.safe` line, and `omk-out.txt` kept as `omk-freeze.txt`.
void classic_guard_launch();
// After the log is opened: says what the last run's stop and `omk.safe`
// mean, and adds `--software` to the arguments when OpenGL is skipped.
void classic_guard_apply(std::vector<std::string>& args);
// Whether `omk.safe` skips a step: "quicktime", "sound" (and "opengl").
bool classic_guard_skips(const char* what);
// A checkpoint on OS 9: `stage` written into `omk.guard` and flushed.
void classic_guard_arm(const char* stage);
// Called every frame: the guard removed 15 s after the last checkpoint.
void classic_guard_tick();
// A clean exit: the guard removed.
void classic_guard_clear();

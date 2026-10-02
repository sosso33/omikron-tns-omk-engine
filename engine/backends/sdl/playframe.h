// SPDX-License-Identifier: GPL-3.0-or-later
// THE FRAME - one turn of the viewer's loop: `PlayState::step` and its six
// phases, `playframe_<phase>.cpp` (`todo/play-split.md`, 2026-10-02). They
// are methods of `PlayState`, so the names in them are its members.
#pragma once

#include "playstate.h"

namespace {
// THE OVERLAY'S SIDE PLANES live in `ui/overlay.h` (G6 steps 2 and 3), shared
// with the HUD's blended quads.
omk::OverlayPlanes& g_ov = omk::overlayPlanes();
}  // namespace

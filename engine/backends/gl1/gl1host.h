// SPDX-License-Identifier: GPL-3.0-or-later
// THE AGL HOST - the one call between the fixed-function backend and the
// CLASSIC Mac frontend (`backends/classic/carbonfront.cpp`), for the build
// that compiles `gl1render.cpp` with `OMK_GL1_AGL`.
//
// On classic Mac OS the OpenGL context belongs to the WINDOW: AGL attaches it
// to the frontend's window port, and the frame is presented through it
// (`glDrawPixels` and `aglSwapBuffers`) rather than by CopyBits, which an AGL
// surface over the window would hide on Mac OS X. So the frontend makes and
// owns the context, on first ask - a run that never asks (`OMK_GL1=0`, or no
// OpenGL installed) keeps the window plain and presents by CopyBits as before.
#pragma once

namespace omk {
// -> the frontend's AGLContext, made on the first call and current, or
// nullptr when there is no window yet or no OpenGL renderer to be had.
void* gl1AglContext();
}  // namespace omk

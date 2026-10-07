// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// THE OPENXR HOST - `todo/quest-port.md` §5 step 5. The Android build's
// alone (`OMK_OPENXR`, defined by `backends/android/CMakeLists.txt`); every
// other build compiles none of it and its seams are `#if OMK_OPENXR` lines.
//
// It sits BESIDE the SDL frontend rather than replacing it: SDL still owns the
// activity, the EGL context, the audio and the events; this adds the headset
// session on that same context. Its seams:
//
//   start()           once the GLES context is current (`playgpu_gles.cpp`)
//   frameTarget()     where a present pass draws: a swapchain image's FBO and
//                     its size, the headset's frame begun - or false, and the
//                     window is the target as before (`playgpu_gles.cpp`)
//   submit()          the frame ended in place of `SDL_GL_SwapWindow`
//   readControllers() the Touch controllers into the pad (`sdlfront.cpp`'s
//                     pump) - the only way they reach the game: a 2D panel
//                     receives the laser alone (step 4, measured)
//
// STEP 5a: the composed frame - the world and the interface as the flat game
// presents them - on ONE quad layer, a screen 2 m ahead in the LOCAL space.
// Step 5b adds the eyes (a projection layer) and keeps the quad for the
// interface.
namespace omk { struct HostInput; }

namespace omk::xr {

// Bring the session up on the CURRENT EGL context; false (and said) when there
// is no OpenXR runtime, and the game stays flat.
bool start(int screenW, int screenH);
bool running();                 // a session is running (frames go to the headset)

// The headset's frame for this game frame: waits (xrWaitFrame paces), begins,
// acquires the quad's image. `fbo` is the image's framebuffer, `w`/`h` its
// size. Idempotent until `submit`. False when no frame is to be drawn - the
// session is not running yet, or stopped.
bool frameTarget(unsigned& fbo, int& w, int& h);
// End the frame begun by `frameTarget` with the quad layer (or none when the
// runtime said not to render). A no-op when none was begun.
void submit();

// The controllers into `out.pad` (OR'ed over whatever SDL put there), START
// as `kEscape`, and the session's EXITING as `out.quit`.
void readControllers(HostInput& out);

}  // namespace omk::xr

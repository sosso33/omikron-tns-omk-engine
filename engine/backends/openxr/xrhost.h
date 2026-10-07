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
//   headPose()        the frame's head, eyes and hands (`SdlFrontend::headPose`,
//                     which `playvr_camera.cpp` reads) - the headset's frame
//                     BEGINS here, so the pose is predicted for its display
//   eyeTarget() /     an eye's swapchain image to present that eye's picture
//   eyeDone()         into (`playvr_draw.cpp`); a frame with both eyes
//                     submits a PROJECTION layer
//   frameTarget()     where a flat present pass draws: the quad's image - or
//                     false, and the window is the target as before
//                     (`playgpu_gles.cpp`); false too in a frame with eyes
//   submit()          the frame ended in place of `SDL_GL_SwapWindow`
//   readControllers() the Touch controllers into the pad (`sdlfront.cpp`'s
//                     pump) - the only way they reach the game: a 2D panel
//                     receives the laser alone (step 4, measured)
//
// STEP 5a: the composed frame on ONE quad layer, a screen 2 m ahead in the
// LOCAL space - still what a frame without eyes shows (films, menus).
// STEP 5b: a frame whose world was drawn per eye submits the eyes instead.
// The interface over the eyes is step 6.
#include <string>
#include <vector>

namespace omk { struct HostInput; }
namespace omk::vr { struct HeadPose; }

namespace omk::xr {

// Bring the session up on the CURRENT EGL context; false (and said) when there
// is no OpenXR runtime, and the game stays flat. `screenW x screenH` is the
// composed frame's size, the quad's; an eye is the runtime's size.
// `scale` multiplies the runtime's recommended eye size (`--vr-scale`).
bool start(int screenW, int screenH, float scale = 1.0f);
// The eyes' picture size: the runtime's recommended, which the renderer's
// target must hold (`playgpu_gles.cpp`) and the eyes are composed at
// (`HeadPose::eyeW/eyeH`, read by `playvr_camera.cpp`).
void eyeSize(int& w, int& h);
// OPTIONS ROW 2 IN A HEADSET (the reader, 2026-10-07: "use the resolution set
// in the sneak options"): the row lists EYE sizes - the runtime's recommended
// times 0.8 .. 1.5, capped at its maximum - and choosing one remakes the eyes'
// swapchains between frames; the interface frame keeps its size. -> the index
// of the current size in `names` ("W x H", the row's own parse).
int eyeModes(std::vector<std::string>& names);
// The eyes at `w x h` from the next frame; false when the size is refused or
// a frame is open (then it is not changed).
bool setEyeSize(int w, int h);
bool running();                 // a session is running (frames go to the headset)

// The head, the eyes (pose and field of view) and the controllers' aim poses
// in the LOCAL space, for this frame's predicted display time. Begins the
// headset's frame. False when there is no frame or no tracking.
bool headPose(vr::HeadPose& out);

// Eye `e`'s image for this frame: `fbo`, its size. False when the frame is not
// to be drawn. `eyeDone(e)` releases it; both done -> a projection layer.
bool eyeTarget(int e, unsigned& fbo, int& w, int& h);
void eyeDone(int e);
// A SCREEN IS OPEN (the sneak, a shop...): the composed frame goes on the quad
// OVER the eyes this frame, opaque - until step 6 puts the interface there
// with the world showing through. Set by `playvr_draw.cpp` each world frame.
void setQuadOverEyes(bool on);

// The headset's frame for a FLAT present: begins it if `headPose` did not,
// acquires the quad's image. Idempotent until `submit`. False when no frame is
// to be drawn, or this frame's eyes were drawn (no quad over them yet).
bool frameTarget(unsigned& fbo, int& w, int& h);
// End the frame with what was drawn: the eyes' projection layer, or the quad,
// or nothing. A no-op when none was begun.
void submit();

// The controllers into `out.pad` (OR'ed over whatever SDL put there), START
// as `kEscape`, and the session's EXITING as `out.quit`.
void readControllers(HostInput& out);

}  // namespace omk::xr

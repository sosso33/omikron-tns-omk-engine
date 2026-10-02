// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT EVERY UNIT OF THE VIEWER SEES - `play.cpp` and the frame
// (`playframe.cpp`): the includes, the backends' entry points, the Vita's
// heap checkpoint, and the names the code uses unqualified. Moved out of the
// top of `play.cpp` by `todo/play-split.md` (2026-10-02) without a change.
#pragma once

#if defined(OMK_SDL3)
#  include <SDL3/SDL.h>
#else
#  include <SDL.h>
#endif

#include "formats/anim.h"
#include "formats/ctl.h"
#include "formats/map2d.h"
#include "formats/mesh3do.h"
#include "formats/scx.h"
#include "formats/tex3dt.h"
#include "input/bindings.h"
#include "actor/fight.h"
#include "actor/shoot.h"
#include "actor/shootfire.h"
#include "actor/shoothit.h"
#include "actor/shootaim.h"
#include "actor/shootmove.h"
#include "actor/projectile.h"
#include "actor/moves.h"
#include "actor/slider.h"

#include <cctype>
#include <unordered_map>
#include <limits>
#include <optional>
#include "actor/pose.h"
#include "actor/speaker.h"
#include "actor/player.h"
#include "actor/moves.h"
#include "actor/walk.h"
#include "o3de/collision.h"
#include "o3de/geom3do.h"
#include "o3de/particles.h"
#include "o3de/pointplace.h"
#include "app/playhelpers.h"
#include "app/playoptions.h"
#include "app/game.h"
#include "sdlfront.h"
#include "playtypes.h"
#include "o3de/shadow.h"
#include "platform/threads.h"
#include "o3de/shimmer.h"
#include "audio/mixer.h"
#include "audio/music.h"
#include "audio/voiceover.h"
#include "formats/adpcm.h"
#include "script/area.h"
#include "script/savefile.h"
#include "formats/light3do.h"
#include "o3de/vertexlight.h"
#include "platform/options.h"
#include "platform/settings.h"
#include "script/gamestate.h"
#include "script/inventory.h"
#include "script/props.h"
#include "o3de/raster.h"
#include "o3de/render.h"
#include "o3de/renderer.h"
#include "platform/boot.h"
#include "platform/movie.h"
#include "platform/datafs.h"
#include "platform/frontend.h"
#include "ui/overlay.h"
#include "ui/hudbar.h"
#include "ui/iamtext.h"
#include "ui/options.h"
#include "ui/radar.h"
#include "ui/screendraw.h"
#include "ui/text.h"
#include "ui/widgets.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <set>
#include <string>

// A HEAP CHECKPOINT on the Vita (`backends/vita/printf_c99.cpp`): newlib's
// `mallinfo` walks every free list, so a checkpoint AFTER a heap overwrite
// crashes there - the last label logged brackets the overwrite. A no-op
// everywhere else. (todo/vita-port.md, the start-up heap corruption.)
#if defined(__vita__)
extern "C" void omk_vita_heap_check(const char* where);
#  define OMK_HEAPCHECK(w) omk_vita_heap_check(w)
#else
#  define OMK_HEAPCHECK(w) ((void)0)
#endif
#if defined(__vita__)
// The Vita's hardware film player (`backends/vita/avmovie.h`).
namespace omk::vita {
struct AvFilm;
AvFilm* avOpen(const std::string& path);
std::string avFind(const std::string& stem, const std::string& dataRoot, std::string& report);
bool avActive(AvFilm* f);
bool avVideo(AvFilm* f, Surface& out);
bool avAudio(AvFilm* f, std::vector<float>& pcm, int& rate);
void avClose(AvFilm* f);
}
#endif
#if defined(__vita__)
// The Vita's on-screen keyboard (`backends/vita/ime.h`), for the name field.
namespace omk::vita {
bool imeEdit(const char* title, const std::string& initial, int maxLen, std::string& out);
}
#endif


using omk::kTypeMarker;
using omk::kCharMarker;
using omk::kOverlayKey;
// ...and the four file-level helpers that used to sit here are in
// `src/app/playhelpers.h` since `todo/play-split.md` S1: `shortArc`,
// `wavToDevice`, `SubBox` + `drawSubtitleBox` and `cp1252ToUtf8`. They are
// named unqualified below exactly as before.
using omk::cp1252ToUtf8;
using omk::drawSubtitleBox;
using omk::shortArc;
using omk::SubBox;
using omk::wavToDevice;
using omk::optionDisplayModes;
using omk::SdlFrontend;
using omk::play::CtlSpriteInst;
using omk::play::HoldRun;
using omk::play::CharModel;
using omk::play::CharBank;
using omk::play::PropModel;
using omk::play::Staged;
using omk::play::PedJob;
using omk::play::PedStaged;
using omk::play::VehStaged;
using omk::play::GunClip;
using omk::play::GunAnim;
using omk::play::GunAim;
using omk::play::SfxSample;
using omk::play::GunFacts;
using omk::play::FightRun;
using omk::play::SpriteTable;
using omk::play::WorldSlot;
using omk::play::Sky;
using omk::play::SetLoad;
using omk::kDeviceRate;
// ...and `ViewCam`, `Light` and `relight` joined them (S1b): the free-look
// camera and the vertex-light toggle, neither of which touches SDL.
using omk::Light;
using omk::relight;
using omk::ViewCam;
using omk::resampleToDevice;

// THE CONSTANTS both `main` and the frame name - moved out of `main` with
// the frame (todo/play-split.md); a reference cannot stand in for a constant
// expression.
constexpr int kFlickPre = 3, kFlickPost = 3, kFlickWindow = 31;

constexpr int kScreenPause = 31;   // PAUSE GAME - the only screen that
                                   // sets dword_4E9728, the pause flag

// ---- THE SNEAK CALL (screen 0, `VIDEOPHONE`) ----------------------
//
// The device the game shows a caller on, and the ONE screen in the game
// that answers its own question. `Ui_OpenSneakFamily`'s param-2 arm
// (0x0049B400) hides the tab column, installs panel 0x004DF128 and ends
//
//     mov  dword_4DF140, edi        ; 1
//     mov  dword_670BF0, edi        ; 1 - "a call is up"
//     call sub_42B560               ; UI_SendAnswer
//
// and `UI_SendAnswer` fires `Game_RaiseEvent(5, {ctx, dword_930750})`,
// which is what resumes a script parked at `ui.open`. `UI_OpenScreen`
// seeded that answer at **-1** and nothing on this screen ever writes it
// - which is exactly why `docs/UI.md` 3d-bis lists VIDEOPHONE as one of
// the three screens that "keep the answer with no writer". The screen
// does not ask a question: it opens, hands -1 straight back, and STAYS
// UP while the script runs on.
//
// That is the whole call idiom, and the corpus is unambiguous: **10
// `ui.open 0` sites**, 8 of them followed immediately by `dialog.start`
// and 2 by `media.play 534` (`ZVO P315 DATA MEMORIZED`, the caption a
// capture shows top-right). The conversation's SPEAKER is a real actor
// parked off-stage in the chunk - 386 `Policier Sneak/Supermarche` is
// actor 95, the guard `ARESTO14` parks 745 units above the restaurant
// (docs/UI.md 3d-bis) - and the script hides him afterwards with
// `character.hide`.
constexpr int kScreenVideophone = 0;

static constexpr const char* kAttachName[18] = {
    "Buste", "Tete", "Buste", "Buste", "Buste", "Bassin", "Brasg", "Brasd",
    "Avantg", "Avantd", "Maing", "Maind", "Cuisseg", "Cuissed", "Jambeg",
    "Jambed", "Piedg", "Piedd"};   // 0/2/3/4 fall to the default arm, a1[5] = Buste

// `Area_LoadMiscModel`'s two constants.
constexpr float kSkyScale = 12.5f;

constexpr float kSkyLift  = 2250.0f;
// the take camera's preset (camera mode 1), named by `run` and `takeCamRequest`
constexpr float kTakeCamEye[3] = {24.4094f, 29.5276f, -4.7244f};
constexpr float kTakeCamAt[3]  = {0.0f, 4.7244f, 19.685f};
constexpr float kTakeCamFov    = 75.0f;
constexpr float kTakeCamTravel = 30.0f;

// THE SET VIEWER (`--scene`, `playscene.cpp`); run takes it when a set is named
int sceneViewer(const std::string& fr, const std::string& setName,
                int camIndex, const float* eyeArg, const float* atArg,
                float fovArg, bool letterbox, int frameBudget,
                const std::string& dump, bool startVulkan, bool noDelay,
                int aaSamples, int texFilter, int texAniso, int ssaa,
                bool sceneDither);

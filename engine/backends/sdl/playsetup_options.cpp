// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the command line, the game's state, the slider's door clips.
// A stretch of `main`'s body, split out by `todo/play-split.md`;
// `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"
#include "platform/json.h"

#include <algorithm>
#include <cmath>
#include <memory>

int PlayState::setupOptions(int argc, char** argv) {
    // THE FLAGS (`todo/play-split.md` S2): `omk::PlayOptions`, parsed in
    // `src/app/playoptions.cpp`; each flag is a reference member of
    // `PlayState` into it under its old name (`playstate.h`).
    if (const int rc = opt.parse(argc, argv); rc >= 0) return rc;
    // THE PROFILER (`todo/debug-tools.md`): `--profile <file>` writes a
    // capture for `tools/omkprof.py`, the clock being the frontend's
#if OMK_PROFILE
    if (!opt.profilePath.empty()) {
        static omk::Frontend* profFront = nullptr;
        profFront = &front;
        omk::prof::setClock([] { return profFront->perfCounter(); }, front.perfFrequency());
        if (omk::prof::open(opt.profilePath)) {
            // the windows and the A/B (step 8): a few KB every S seconds, for
            // a slow disk - the iMac G3's took 26 MB a run a chunk a frame
            omk::prof::setWindow(opt.profileWindow);
            if (!opt.profileAb.empty()) omk::prof::setAb(opt.profileAb, opt.profileAbSec);
            if (opt.profileWindow > 0 || !opt.profileAb.empty())
                std::printf("profile: writing %s - the frames summed in windows of %g s%s%s, "
                            "for tools/omkprof.py\n", opt.profilePath.c_str(),
                            opt.profileWindow > 0 ? opt.profileWindow
                                                  : opt.profileAbSec / std::ceil(opt.profileAbSec / 10.0),
                            opt.profileAb.empty() ? "" : ", A/B on ", opt.profileAb.c_str());
            else
                std::printf("profile: writing %s - one chunk a frame, for tools/omkprof.py\n",
                            opt.profilePath.c_str());
        } else
            std::printf("profile: cannot write %s\n", opt.profilePath.c_str());
    }
#else
    if (!opt.profilePath.empty())
        std::printf("profile: this is a RELEASE build - no profiler (OMK_PROFILE=0)\n");
#endif
    // THE GAME'S STATE (`todo/play-split.md` S3), one group at a time, each
    // member initialised below.
    boardPress = false;
    mountSpent = false;    // the action button is edged, not held
    calledOpenTold = false;
    boarded = false;           // aboard, `Slider_TickRide` not yet driving
    // ...and the BOARDING that comes first: `MDACTION` has put him at the
    // door, group 60 (`H_SLDIN`, 72 frames) is playing the door and the step
    // in, and the channel's own `MDSLIDIN` entry at the end of it is what
    // takes him aboard. Between the two he is ACTOR_STATE 6.
    boarding = false;
    doorOffState = 0;         // 0 not read yet, 1 read, -1 unavailable
    boardCam = 0;            // frames (at 30 Hz) left of `Camera_Request(9, ..)`
    // ...and the EXIT, which is the same shape mirrored: `sub_468FA0` places
    // him from group 61's clip against a DIFFERENT reference (slf_113.3da,
    // `dword_9103D8`) and plays `H_SLDOUT`.
    leaving = false;
    exitOffState = 0;
    // ---- THE SLIDER'S OWN DOOR CLIPS -------------------------------------
    //
    // `Cef_TickChannel`'s ACTOR_STATE switch (19_dsound.c, cases 6 and 8)
    // plays a clip ON THE SLIDER while the character plays `H_SLDIN` /
    // `H_SLDOUT`, driven by the SAME clock `a2`:
    //
    //     sub_437FC0(sub, dword_90EF28);              // bind
    //     sub_437FE0(sub, dword_90EF28, 0.0, a2, &d); // sample
    //     sub_438310(slider, &sp);
    //     sub_437F80(sub, sp + d, sp.y - 33.149605 + d.y, sp.z + d.z);
    //
    // `dword_90EF28` is `ANIMS\slf_112.3da` and `dword_9103D8` is
    // `slf_113.3da` (05_sys.c 1842-1844) - and the clips are **72 and 51
    // frames**, exactly the lengths of `H_SLDIN` and `H_SLDOUT`. So the door
    // is an ANIMATION, not the two-state model swap `sub_4521E0` does, and
    // this port had read the clips only for their root key and never played
    // them. `build/slider_doorclip` measures what each drives: of the five
    // tracks, one moves - **`SlPorteG` turns 71.4 degrees**, the gull-wing
    // swing - and the other four and the root hold still.
    doorClipsRead = false;
    journeyTo = -1;            // the address the journey ends at
    // `dword_6A17CC` - which destination row the call was made for.
    calledDestination = -1;
    // THE LIVE RIDE, when there is one. `todo/slider.md` step 3's harness.
    scxPlayed = false;
    // ...and the save's OWN placement, which is `State_Apply`'s and not a
    // harness flag: a loaded game stands where it was saved unless something
    // explicit says otherwise.
    savedYaw = 0.0f;
    haveSavedPlacement = false;

    // The viewer takes the whole program: it wants no boot chain, no widget
    // tree and no movies, and mixing it into the interface loop would make
    // both harder to read than either is worth.
    if (!scene.empty()) {
        // ...and so the TEXTURE PACK too comes from its flag alone here
        remasterSceneViewer();
        return sceneViewer(dataRoot, scene, camIndex, haveEye ? eyeA : nullptr,
                           haveAt ? atA : nullptr, fovA, letterbox, frames, dump,
                           startVulkan, noDelay, aaFlag < 0 ? 0 : aaFlag,
                           filterFlag < 0 ? 0 : filterFlag, anisoFlag < 0 ? 1 : anisoFlag,
                           // the scene viewer runs BEFORE settings resolve, so
                           // it takes the flag alone; `--config` is the game's
                           ssaaFlag < 0 ? 1 : ssaaFlag, dither);
    }

    const auto& fs = fs_.emplace(dataRoot);
    // ui_widgets.json parsed ONCE for the widgets and the options tree below
    // (`UiWidgets::fromJson`); released when the setup is done with it
    auto widgetsDoc = std::make_unique<omk::Json>(omk::Json::parseFile(tb + "/ui_widgets.json"));
    uiWidgets = omk::UiWidgets::fromJson(*widgetsDoc);
    // `Ui_BuildLoadPanel`'s layout, applied for screen 29.  The four buttons
    // of the load panel all ship at (460, 210) and the builder moves three of
    // them apart; without this they draw on top of one another and the panel
    // shows one line of text where the original shows three.  The engine does
    // this in the OPEN callback and would redo it for screen 30's save
    // layout - which this port does not open yet, so it is applied once.
    omk::applyLoadPanelLayout(uiWidgets, 29);
    if (!uiWidgets.valid())
        std::printf("tables: no widget tree (ui_widgets.json) - the interface "
                    "screens cannot be drawn or walked, so the Session answers "
                    "them itself and the start menu is skipped\n");
    uiWidgets.loadScreens(tb + "/ui.json");
    fonts = omk::FontTable::loadJson(tb + "/ui.json");
    auto& lay = lay_.emplace(fonts, dataRoot + "/FONTS");   // not const: the text-scaling enhancement
    auto& comp = comp_.emplace(fs, uiWidgets, lay);
    // SCREEN 35, the options (`todo/options-menu.md`): the page tree out of
    // the widget lift, the 74 rows out of `ui.json`, and the screen's own
    // strings - the labels by `+24`, the save prompt's and Accel 3D's.
    optTree = omk::OptionTree::fromJson(*widgetsDoc, omk::Json::parseFile(tb + "/ui.json"));
    widgetsDoc.reset();
    auto& optMenu = optMenu_.emplace(optTree);
    optMenu.setText(omk::iamStrings(fs, "IAM/Options"));
    // ...and OMK's own pages on it (`ui/omkoptions.h`, `playomkoptions.cpp`)
    omkOptionsSetup();
    // The world rendered at a panel's 3D VIEWPORT item's size, for the
    // composer to place (`ScreenComposer::attachView3D`).
    // The menu's animated background - `IMAGES/cloud.bmp` embossed by a
    // rotating light and warped by two cosine tables (`ui/cloud.h`). The
    // screen's own sheet is colour-keyed over it.
    if (cloud.load(fs)) comp.attachCloud(&cloud);
    else std::printf("no IMAGES/cloud.bmp - the menu draws on black\n");

    // The real input path: scancodes in, the live binding tables and
    // `Game_Frame`'s edge filter in the middle, one 14-bit word out. Nothing
    // here hands the walk a word directly, which is the whole point of
    // `verify.py: engine input`.
    schemes = omk::ControlSchemes::loadJson(tb + "/key_bindings.json");
    auto& in = in_.emplace(schemes);
    in.installScheme(0);
    in.setRepeatMask(0x203F);          // `Ui_BeginScreen`
    // The keybinding rows' defaults and value column (`ui/keynames.h`)
    keyNames = omk::KeyNames::loadJson(tb + "/key_names.json");
    keyNames.setKeyCap([this](int dik) { return front.keyCap(dik); });
    optMenu.setBindingSources(&schemes, &keyNames);

    OMK_HEAPCHECK("before boot chain");
    return phase::Continue;
}

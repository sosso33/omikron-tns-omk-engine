// SPDX-License-Identifier: GPL-3.0-or-later
// THE VIEWER'S COMMAND LINE - every flag `omk-play` takes, and the parse.
//
// Moved out of `play.cpp`'s `main` by `todo/play-split.md` S2 (2026-10-02)
// without a change: the declarations, their defaults and their comments are
// the struct's fields, and `parse` is the usage text, the `--help` scan, the
// tables lookup and the argument loop, verbatim. Nothing here includes SDL, so
// a platform with no command line (the Vita) can fill one directly.
#pragma once

#include "actor/sliders.h"   // kDefaultStreetActivity, `--density`'s default

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace omk {

// A scripted-key entry meaning "type the --type string here", not a scan code.
constexpr int kTypeMarker = -1;
// `cN` in a `--keys` list arrives as `kCharMarker - N`, so one negative range
// carries any character the field's switch understands. -2 is character 0,
// which nothing sends, so the first real one is -10 for backspace.
constexpr int kCharMarker = -2;

struct PlayOptions {
    // the two positional arguments: the data tree, and the tables directory
    // (optional - see `parse`)
    std::string fr;
    std::string tb;
    int screenId = 29, frames = 0;
    bool playMovies = true;
    std::string dump, typeText;
    // THE DISPLAY. The interface is authored at 640x480 and its coordinates
    // are scaled by `I2D_ScaleX/Y` (`v * w / 640`, `v * h / 480`), so a bigger
    // display spreads the same layout without enlarging the glyphs. 800x600 is
    // the mode a reader's own screenshot of the original is in.
    int dispW = 800, dispH = 600;
    bool resFlag = false;      // --res given: it beats the ini's screen_x/screen_y
    int fullscreenFlag = -1;   // --fullscreen 1 / --window 0; -1 = the settings'
    std::vector<int> scripted;
    bool bankReject = false;          // --bank-reject, a DEBUG switch
    int keyEvery = 2;
    // ADVENTURE MODE, headless: `--hold k200*120,k203*30` is a replayable
    // input stream fed AFTER the hand-over - DIK scancodes HELD for that many
    // frames (`+` joins several), `0*n` holds nothing - the same syntax
    // `tools/player_probe.cpp` takes, so a walk can be reproduced without a
    // person at the keys. `--snaps DIR` writes the framebuffer as raw LE
    // RGB565 every 30 frames from the hand-over on (`snap-<frame>.bin`,
    // 640x480 after the display size), which is how the walk was LOOKED at.
    std::string holdStream, snapsDir, flickerDir;
    std::string profilePath;   // --profile: the profiler's capture (todo/debug-tools.md)
    bool waterCamPreset = false;   // `--water-cam preset`: the old fixed-offset reading
    int snapEvery = 30;            // `--snap-every N`: 1 catches a flicker
    // A STREET START (docs/STREET_LIFE.md, step 4): `--save FILE` takes the
    // game DB from a save's slot 0 - the player record lives there, and
    // Kay'l's actor record is in no city chunk - `--area N` loads that area
    // instead of the save's own, `--address A` puts him down on one of its
    // ADDRESSES (listed at start), and adventure mode begins at once, with
    // no intro to replay. `--density` is options row 6 for the crowd
    // (default the engine's 3), `--no-crowd` leaves the pedestrians out.
    std::string saveFile;
    // WHERE THIS PORT KEEPS ITS SAVES, and it is deliberately not where the
    // engine keeps them.  `Game_WriteSave` writes `IAM\GAMES` inside the game
    // directory; `omk::safeOutputPath` refuses that, because `gamedata/` is
    // input and a shipped file was destroyed once already (CLAUDE.md 1).  So
    // the file lives outside the tree and READING falls back to the shipped
    // one, which means a checkout that has never been saved into still sees
    // the (empty) directory the game would see.  `--saves FILE` moves it.
    std::string savesPath = "omk-saves/GAMES";
    // Which slot of it to load.  -1 is "no slot asked for", which is not the
    // same as slot 0: asking for a slot is asking to RESUME, and that starts
    // adventure mode in the save's own area at its own placement, the way
    // `Game_LoadSave` -> `State_Apply` does.
    int slotArg = -1;
    // ...and the slot a run WRITES when it ends.  A harness path, not the
    // game's: in the game a save is `ui.open 30` out of a save point's
    // activate script and the ring is spent when the panel confirms
    // (GAME_STATE 8c).  This writes the file without either, so a save can be
    // made and re-loaded before the panel exists.
    int saveSlotArg = -1;
    std::string saveNameArg;
    int areaArg = -1, addressArg = -1, density = omk::kDefaultStreetActivity;
    // `--ride`: MOUNT the player on a slider where he stands, without the
    // pool, the reservation or the arrival - `todo/slider.md` step 3's
    // harness. The flight model is the engine's (`actor/slider.h`); what this
    // skips is how a slider gets to you, which is `sub_452570`'s other arm.
    // `--fight N` / `--fight-level N`: the MELEE harness. The engine's way in
    // is a script running op 62, and this stands in for one so a fight can be
    // reached without walking the story to it - the same shape as `--ride`
    // for the slider, and it says so in its own log line.
    int  fightArg = -1;
    bool noFoeCollision = false;   // --no-foe-collision: the opponent as before 15.8a, for comparison
    bool noFightCamRay = false;    // --no-fight-camera-collision: the fight camera as before, for comparison
    bool foeAtSet = false;         // --fight-foe-at x,z: a HARNESS - start him there instead
    float foeAtXZ[2] = {0.0f, 0.0f};
    int  fightLevelArg = 1;
    bool rideArg = false;
    // `--board`: HARNESS. When the called slider goes OPEN, put him at its
    // door point and press the action button once, so a check can board
    // without a scripted walk that has to find the door side of a vehicle
    // whose park point moves with every call. The gate - `MDACTION`'s side
    // and reach - still runs for real on where he is put.
    bool boardArg = false;
    // --config: the game's own ini (`[Preferences]`, 65 keys) plus this
    // port's `[Options]` for the two rows with no key. The SAVE's 3496-byte
    // header carries the same settings and is LATER, so it wins - see
    // `platform/settings.h`. An explicit --density or --clip beats both,
    // because a flag typed on the command line is the most recent word of
    // all; `densityFlag`/`clipFlag` record whether one was.
    std::string configFile;
    bool densityFlag = false, clipFlag = false;
    int clipArg = 0;
    int skyFlag = -1;      // --sky 0|1, options row 4; -1 = take it from the settings
    int shadowFlag = -1;   // --shadows 0|1, options row 5; -1 = the settings'
    int threadFlag = 1;    // --no-thread-bodies: pose the crowd on one core
    bool noTieFlag = false;   // --no-tie: the GLES window draws without the depth tie
    bool cpuBodiesFlag = false;   // --cpu-bodies: pose every body on the CPU even where the renderer can
    int detailFlag = -1;   // --detail 0..2, options row 7; -1 = the settings'
    int aaFlag = -1;       // --aa N, [Enhancements] antialiasing; -1 = the settings'
    int filterFlag = -1;   // --filter nearest|bilinear|trilinear, [Enhancements] texturefiltering
    int anisoFlag = -1;    // --anisotropy N, [Enhancements] anisotropy
    int shadowQFlag = -1;  // --shadow-quality classic|fitted|mapped, [Enhancements] shadowquality
    int uiScaleFlag = -1;  // --ui-scaling nearest|linear, [Enhancements] uiscaling
    int textScaleFlag = -1;  // --text-scaling game|fit, [Enhancements] textscaling
    int lightingFlag = -1; // --lighting pervertex|perpixel, [Enhancements] lighting
    int ssaaFlag = -1;     // --ssaa N, [Enhancements] supersampling
    int radarFlag = -1;    // --radar game|always, [Enhancements] radar
    int frameRateFlag = -1; // --framerate N, [Enhancements] framerate
    int smoothAnimFlag = -1; // --smooth-anim, [Enhancements] animation = smooth
    // `--dither 0|1`. NOT an enhancement: `sub_4638C0` sets D3DRENDERSTATE 26
    // (DITHERENABLE) to 1 on both device arms, so on is what the engine does.
    // The flag exists to lay a dithered frame beside an undithered one.
    bool dither = true;
    // WHICH WAY THE MOUSE TURNS HIM. A reader played the shoot phase and said
    // both axes were inverted, so the defaults are THEIR sense (`todo/
    // omk-play.md` 97b). `--invert-x` / `--invert-y` put each one back.
    //
    // This used to say nothing in the data fixes the sign because "the engine
    // reads mouse motion nowhere in the binding path". The binding path, no -
    // but it READS it: `sub_47D370` (one caller) turns `+420` by
    // `-word_90E1AC * 0.01 * dx` and the pitch by `word_90E1AE * 0.01 * dy`,
    // its sign from the invert byte at 0x90E1B0, clamped at +-45 degrees -
    // all three in the options header. So the senses, the sensitivities
    // below and the +-70 clamp are this port's and the original's are
    // readable (`todo/shoot-mode.md` 7h). Found 2026-09-10 and PORTED the same
    // day (`todo/shoot-mode.md` 8.5): the sensitivities, row 25's invert and
    // the +-45 clamp are the engine's now; the pitch's absolute sign and
    // `--invert-x` stay this port's.
    bool mouseInvertX = false, mouseInvertY = false;
    // The shoot camera's EYE LIFT, in inches. THE DEFAULT IS THE ENGINE'S OWN
    // RULE, found where a reader told me to look: `Camera_Request` stores the
    // subject and calls `sub_414520`, whose case 4 - the mode `Shoot_Enter`
    // asks for - sets the camera's `+180`/`+128` to `0.7 * actor[+276]`, and
    // `+276` is the model's own lowest sphere extent below its origin
    // (`Actor_LoadModel`). `--shoot-eye N` overrides it; `--shoot-eye 0` is
    // the preset's literal (0,0,0).
    float shootEyeLift = 0.0f;
    bool  shootEyeSet = false;   // did --shoot-eye override the model's own?
    // --enhance-all: every enhancement as high as it goes, in one word. The
    // two the DEVICE caps are asked for at their largest defined value and the
    // backend reduces what it cannot meet, which is what "max available" means
    // here. A specific flag still wins, whatever order they are typed.
    bool enhanceAll = false;
    // The fog is not an option row - it is always on in the engine - so this
    // is a diagnostic switch, not a setting. Default ON, because that is what
    // the game does.
    bool drawFog = true;
    // The crowd's dynamic lighting from the set's own light table. On by
    // default because that is what the engine does; `--no-crowd-light` is the
    // before/after.
    bool lightCrowd = true;
    std::uint8_t fogRGB[3] = {0, 0, 0};   // the scene's +336, which ships as 0
    // --give: object ids for the carried list, comma-separated. A LIST
    // rather than one id because the flows worth driving need a bagful - row
    // scrolling wants more than the nine row widgets, and `Utiliser sur`
    // wants a recipe PAIR, and a new game ships exactly two objects.
    std::string giveList;
    int moneyArg = -1;              // --money: a harness write of record +172
    int ringsArg = -1;              // --rings: the same over record +174
    std::string varList;
    bool newWorld = false; // --newgame-world: START's world, the save's player
    // --scene-chunk N: run a SCENE chunk's startup script over the area, the
    // way `scene.load` does. A street start jumps straight to an area, so the
    // chunk that would have been loaded on the way in never is - and with it
    // go the beats that SHOW the area's props. For AREA 222 that is SCENE 55,
    // whose script ends in `object.show 162`, the Impasse's rings: without it
    // there is nothing in the world to take (`tools/prop_probe.cpp` does the
    // same call, and is where this shape comes from).
    int sceneChunk = -1;
    std::vector<int> zoneEnable;   // `--zone-enable`, the harness below
    std::vector<int> zoneDisable;  // `--zone-disable`, its mirror
    std::vector<std::pair<int, int>> sceneLoads;   // `--scene-load A,S`, the opcode
    bool noCrowd = false;
    bool noScriptSprites = false;   // DEBUG: leave the scripted sprites undrawn, for a before/after
    std::vector<int> scxPlay;       // --scx-play: objects to start by handle, once
    // --hide-show A,H,S,F: opcode 79 on actor A at frame H, then opcode 78
    // with second field F at frame S - a hidden body's return, on demand
    int hideShow[4] = {-1, -1, -1, 0};
    // --game-restart N: op 152 run as a context at frame N
    long gameRestartAt = -1;
    // --op-at F:OP[,F:OP...]: a NO-OPERAND opcode run as a context at frame
    // F - one of 106, 107, 116, 117, 152, whose handlers take no bytes
    std::vector<std::pair<long, int>> opAt;
    // `--sneak` opens the device as soon as the player is on his feet,
    // through the SAME path TAB takes - `MDSNEAK0`'s handler, event 25 and
    // screen 9 - rather than a second way in. A testing convenience for a
    // screen that otherwise costs a walk to reach; it sets the same
    // `playerScreen` the special move sets and nothing else.
    bool openSneak = false;
    bool startShoot = false;   // --shoot: enter shoot mode at the hand-over
    // --shoot-health N: a TEST HARNESS, not the game - property 1 written as N
    // at shoot entry, so a check of the player's own weapon, movement and
    // bolts can outlive the gallery's gunmen (they kill him in ~16 frames, and
    // since the death is ported he then fights no more). -1: the save's value
    int shootHealth = -1;
    int fightHealth = -1;   // --fight-health N: a HARNESS, the player's Vie at Fight_Begin
    long shootEndAt = -1;      // --shoot-end N: `shoot.end 1` at frame N
    float standAt[4] = {0, 0, 0, 0};
    bool haveStand = false;      // `--stand x,y,z,yaw`: put the player down there after the hand-over
    // the scene viewer's
    std::string scene;
    int camIndex = -1;
    float eyeA[3], atA[3], fovA = 0;
    // NOT letterboxed by default, and that was a mistake worth naming. The
    // 1.818:1 letterbox is measured off DIALOGUE captures (ASSETS: dialog 402,
    // an 800x600 frame carrying an 800x440 strip), so it is established for
    // CAMERA MODE - conversations and cutscenes. Nothing establishes it for
    // free roaming, and this is a free-look tool; imposing it here was
    // generalising a camera-mode property to all rendering. `--letterbox`
    // brings it back for comparing against those captures, which is the one
    // job it is evidence for.
    // `--call N`: the SNEAK CALL idiom - `ui.open 0` then `dialog.start N` -
    // fired on the first adventure frame. A HARNESS: in the game it is a zone
    // script that does this, and reaching one means playing the beat.
    int  callDialog = -1;
    // `--anim-hold`: hold the player on the first adventure frame, the way a
    // staged beat's `player.anim.hold` does. A HARNESS, for reaching camera
    // mode without playing a beat.
    bool animHoldHarness = false;
    bool haveEye = false, haveAt = false, letterbox = false, startVulkan = false, noDelay = false;
    double speed = 1.0;                 // --speed: the frame delta's multiplier
    bool forceSoftware = false, showFps = false;
    // the classic Mac's GL1: the composited present, not the straight one
    // (todo/cpu-vs-original.md tier B) - a flag, as classic Mac OS has no
    // environment to set `OMK_GL1_COMPOSITE` in
    bool gl1Composite = false;
    // A HARNESS flag, not a mode: draw the 3D world through a SURFACELESS
    // Vulkan renderer while the frame is still presented (or dumped) the
    // ordinary way. `--vulkan` needs a real window and therefore a real
    // display, so nothing that only the GPU backend does - the mapped shadow,
    // the MSAA, the filters - can otherwise be measured headlessly.
    bool worldVulkan = false;

    // The whole command line, in `main`'s order. Returns -1 to go on, or the
    // exit code `main` returns: 0 after `--help`, 2 for a bad argument.
    int parse(int argc, char** argv);
};

}  // namespace omk

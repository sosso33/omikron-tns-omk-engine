// SPDX-License-Identifier: GPL-3.0-or-later
// `PlayOptions::parse` - see `playoptions.h`. Moved out of `play.cpp`'s
// `main` by `todo/play-split.md` S2 (2026-10-02) without a change.
#include "app/playoptions.h"
#include "platform/datafs.h"

#include "platform/settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace omk {

int PlayOptions::parse(int argc, char** argv) {
    // `--help` anywhere on the line, and the same text the argument check
    // prints. Kept in one place so a flag cannot be added without a line here.
    const auto usage = [](std::FILE* to) {
        std::fprintf(to,
"omk-play - the OMK engine's viewer\n"
"\n"
"  omk-play <gamedata> [tables dir] [screen] [options]      play\n"
"  omk-play <gamedata> [tables dir] --save F --area N       start in a street\n"
"  omk-play <gamedata> [tables dir] --scene <set> [options] look at one set\n"
"\n"
"<gamedata> is the shipped tree. <tables dir> is OPTIONAL - the lifted\n"
"tables (tables/); without them the run still works but tells you less, and\n"
"the interface screens cannot be drawn. `tables/vm_opcodes.json` is the one\n"
"exception: the world scripts cannot be decoded without it.\n"
"[screen] is the interface screen to open, default 29, the start menu.\n"
"\n"
"RUNNING\n"
"  --speed X        scale the frame delta - the engine's own trick. Its\n"
"                   `dword_4E972C` has 1.0, 0.5, 0.1 and 2.0, so 2 is the\n"
"                   game's double speed and 0.5 its slow motion. Capped at 3.\n"
"  --nofmv          skip the three FLIS movies (~143 s of them)\n"
"  --frames N       run N frames and exit - headless, no pacing\n"
"  --dump out.bin   write the last framebuffer as RGB565. NOTE: this still\n"
"                   OPENS A WINDOW, and a window takes the keyboard - set\n"
"                   SDL_VIDEODRIVER=dummy for a headless render, which is\n"
"                   what verify.py does. Two comparison renders once came\n"
"                   out of different places because a reader saw the window\n"
"                   appear and walked the player, quite reasonably\n"
"  --snaps <dir>    write a framebuffer every 30 frames after the hand-over\n"
"  --snap-every N   ...every N frames instead; 1 is how a FLICKER is caught,\n"
"                   a fault no one-second snapshot can show\n"
"  --flicker <dir>  CATCH A ONE-TO-FIVE-FRAME FAULT. Watches its own\n"
"                   output and, when a frame is far darker than the median\n"
"                   of the last 31 - a body drawn on a set that is not\n"
"                   there - writes that frame with the three before and\n"
"                   three after it, plus a line of context each (which\n"
"                   camera, the set runs drawn and culled, the bodies).\n"
"                   For a fault you can see and cannot screenshot\n""  --fps            report the rate and the worst frame once a second\n"
"\n"
"SAVED GAMES\n"
"  --saves FILE     the save file this port reads and WRITES, default\n"
"                   omk-saves/GAMES. The engine keeps it as IAM\\GAMES\n"
"                   inside the game tree; this port must not, because\n"
"                   gamedata/ is input and safeOutputPath refuses it. When\n"
"                   the file does not exist yet, reading falls back to the\n"
"                   shipped IAM/GAMES so the directory still reads\n"
"  --slot N         load slot N and RESUME: adventure mode in the save's\n"
"                   own area, standing where it was saved. --area, --stand\n"
"                   or --address override the placement; --save names a\n"
"                   file to take the slot from instead of --saves\n"
"  --save-slot N    when the run ENDS, write a save into slot N: the live\n"
"                   area and the scene over it, the player's position and\n"
"                   facing, the clock, and a 128x96 picture of the last\n"
"                   frame - which is what the load panel draws beside the\n"
"                   row. A HARNESS path: in the game a save is `ui.open 30`\n"
"                   out of a save point's activate script and costs one\n"
"                   anneau, and neither of those is here yet\n"
"  --save-name S    the profile name to write with (default: the name of\n"
"                   the slot this run loaded)\n"
"\n"
"STARTING IN A STREET (street life)\n"
"  --save FILE      load a save instead of IAM/START, so there is no intro.\n"
"                   Slot 0 unless --slot says otherwise. Paired with --area\n"
"                   this drops the save's PLAYER RECORD into another area,\n"
"                   which is what the street starts want; on its own with\n"
"                   --slot it resumes the game the save holds\n"
"  --area N         the area to stand in\n"
"  --address A      the ADDRESSES record to stand on\n"
"  --fight-supermarket  the SCRIPTED fight after the supermarket shoot phase,\n"
"                   in one command: AREA 245 with the player standing in the\n"
"                   zone whose record 0 stages CHARACTERS 48 'Gun Waver 3',\n"
"                   plays the approach and runs `fight.begin`. Nothing is\n"
"                   harnessed - the chunk's own script does all of it. Short\n"
"                   for --area 245 --stand 14855,-32,1914,0\n"
"  --fight N        HARNESS: begin a MELEE against CHARACTERS id N where the\n"
"                   player stands, the way `fight.begin` (op 62) would - both\n"
"                   bodies onto .CTL slot 2, ACTOR_STATE 2, control scheme 3.\n"
"                   The opponent must be staged in the resident scene. Without\n"
"                   it a fight still starts wherever a script runs op 62\n"
"  --fight-health N HARNESS: the player's Vie at `Fight_Begin`, so a fight can\n"
"                   be watched through without losing it\n"
"  --fight-level N  the AI difficulty a --fight harness asks for, 0..2 (the\n"
"                   scripts' 'Niveau Combat'; default 1). A scripted fight\n"
"                   carries its own level in the opcode's third field\n"
"  --ride           mount a slider where he stands and FLY it (step 3's\n"
"                   harness: no pool, no reservation, no arrival)\n"
"  --stand x,y,z[,facing]   an explicit spot instead of an address\n"
"  --density 0..4   how much crowd - the options menu\'s own row 6\n"
"  --shadows 0|1    the character shadows - options row 5 (--no-shadows too)\n"
"  --no-thread-bodies  pose the crowd on ONE core. The default poses it over\n"
"                   several (`omk::Threads`), bit-identical to one core and\n"
"                   proven on a console (--thread-bodies asks for the default)\n"
"  --cpu-bodies     pose and light every body on the CPU, even on a renderer that\n"
"                   poses them itself (GLES; todo/gpu-skinning.md) - for comparing\n"
"  --no-tie         the GLES window draws WITHOUT the depth tie (o3de/depthtie.h),\n"
"                   for judging on a console whether its 16-bit depth needs it\n"
"  --detail 0..2    how many bones cast one - options row 7\n"
"  --world-vulkan   draw the WORLD through an offscreen Vulkan renderer (a\n"
"                   harness: it needs no window, so the GPU-only enhancements\n"
"                   can be measured headlessly)\n"
"  --shadow-quality classic|fitted|mapped  ENHANCEMENT: fitted lays each blob\n"
"                   on the surface under it; mapped is a real shadow map, on\n"
"                   the GPU backends (Vulkan, GLES) (default classic)\n"
"  --dither 0|1     the engine's own DITHERENABLE, on by default - `--no-dither`\n"
"                   is for comparing two frames, not for play\n"
"  --ssaa N         ENHANCEMENT: render N times larger each way and average it\n"
"                   down - 1 off, 2 or 4. Reaches the CUTOUT edges (grilles,\n"
"                   railings, signs) that MSAA never looks at; GPU backends.\n"
"                   NOT part of --enhance-all: it costs the most by far\n"
"  --enhance-all    every ENHANCEMENT as high as it goes, BUT supersampling -\n"
"                   none of them is what the original drew; a specific flag\n"
"                   still wins\n"
"  --lighting pervertex|perpixel  ENHANCEMENT: the engine's own light law per\n"
"                   fragment, and every character receives it rather than the\n"
"                   crowd alone; GPU backends (default pervertex)\n"
"\n"
"  the HARNESS flags - ways in that the game reaches by being played:\n"
"  --call N         the SNEAK CALL idiom (`ui.open 0` then `dialog.start N`) on\n"
"                   the first adventure frame; in the game a zone script does it\n"
"  --anim-hold      hold the player as a staged beat's `player.anim.hold` does,\n"
"                   for reaching camera mode without playing the beat\n"
"  --board          walk onto the called slider and press action once, so the\n"
"                   engine\'s own boarding gate runs (todo/slider.md)\n"
"  --board-after N  the same, N frames after the slider opens\n"
"  --newgame-world  keep the save\'s PLAYER but take the world from IAM\\START,\n"
"                   to try a flow against a new game\'s state without the intro\n"
"  --var N=V,...    set world VARIABLES before the first frame, for a gate that\n"
"                   a save predates\n"
"  --config <ini>   the game's own config file - [Preferences], and this\n"
"                   port's [Options] for density and level of detail\n"
"  --clip <metres>  options row 3, the clip distance (25/50/100/150/200);\n"
"                   a --save's own header supplies it otherwise\n"
"  --sky 0|1        options row 4, Affichage du ciel\n"
"  --fog 0|1        the linear fog (default on - the engine always fogs)\n"
"  --no-crowd-light  do not light the crowd from the set's .3DO lights\n"
"  --no-actor-light  do not light the characters and props from them\n"
"  --fog-colour r,g,b   override the scene's +336 (else the area's day/night colour)\n"
"  --no-crowd       no pedestrians at all\n"
"  --invert-x       invert the mouse's X axis in shoot mode; --invert-y\n"
"                   the same for Y. The engine reads mouse motion NOWHERE\n"
"                   in the binding path, so no shipped value fixes either\n"
"                   sense and the defaults are a reader's own.\n"
"  --shoot          HARNESS: enter shoot mode at the hand-over, as a script's\n"
"                   `shoot.begin -1` would (the Gun Waver) - to stand in an\n"
"                   arena and look at the mode, the way --ride does a slider\n"
"  --aim-at X,Y,Z   HARNESS: every shot the player fires in shoot mode is aimed\n"
"                   from its muzzle at that world point (todo/astaroth.md)\n"
"  --player-at F:X,Y,Z,YAW HARNESS: the player put down at that point at frame\n"
"                   F (todo/astaroth.md 2 - no shipped address is behind him);\n"
"                   given twice, the second is a later placement\n"
"  --astaroth-health N HARNESS: Astaroth's health (+92) written as N at his\n"
"                   setup, so a check can kill him in a few hits\n"
"  --gandhar-health N HARNESS: Gandhar's health (+92) written as N at his\n"
"                   entry (todo/gandhar.md): his bands, his death\n"
"  --astaroth-souls F HARNESS: at frame F each of Astaroth's six souls is struck\n"
"                   three times through sub_47FCF0, as a bolt's world hit would\n"
"  --shoot-health N HARNESS: the player's health (actor property 1) written as\n"
"                   N when shoot mode starts, so the record, the gauge and the\n"
"                   property agree as a save carrying N would make them\n"
"  --hide-show A,H,S,F HARNESS: character.hide A at frame H and character.show\n"
"                   A, F at frame S, through the Session's own ops 78/79\n"
"                   (todo/drift-audit.md M1)\n"
"  --game-restart N HARNESS: op 152 (game.restart) run as a context at frame N\n"
"                   (todo/drift-audit.md S3)\n"
"  --op-at F:OP     HARNESS: one opcode (106, 107, 116, 117, 150, 151 or 152) run as a\n"
"                   context at frame F; repeatable as a comma list\n"
"  --shoot-end N    shoot.end 1 at frame N - the harness's way OUT of the mode\n"
"  --water-cam preset  the swim camera's OLD fixed-offset reading, to lay\n"
"                   beside the chase camera the port now uses\n""  --shoot-eye N    raise the shoot camera N inches. A DEPARTURE: camera\n"
"                   preset row 4 puts the eye at (0,0,0) on the subject -\n"
"                   the actor's origin, which is the pelvis - and nothing\n"
"                   found in the engine lifts it. Default 0, the faithful\n"
"                   value.\n"

"  --no-script-sprites  DEBUG: do not draw Script_Display3DSprite's sprites (a before/after)\n"
"  --scx-play h,h   HARNESS: start scene objects by handle on the first adventure frame\n"
"  --zone-enable N  HARNESS: `zone.enable N`, the opcode and nothing else, on a\n"
"                   zone the story would have enabled. Everything after is the\n"
"                   game's path - walk in and the zone runs its own script.\n"
"                   AREA 141's 2295 'Start Shoot' opens the catacombs' shoot\n"
"                   phase, whose ten spectres PATROL (todo/shoot-patrol.md)\n"
"  --zone-disable N HARNESS: `zone.disable N`, the mirror - for a start that\n"
"                   skipped the script that would have disabled it. The\n"
"                   supermarket harness needs 3949: AREA 231's record 1,\n"
"                   the airlock cutscene in, disables it on the real path.\n"
"  --scene-chunk N  run SCENE chunk N's startup script over the area, the\n"
"                   way `scene.load` does. A street start jumps straight to\n"
"                   an area, so the chunk that would have been loaded on the\n"
"                   way in never is - and with it go the beats that SHOW the\n"
"                   area's props. AREA 222 wants SCENE 55, whose script ends\n"
"                   in `object.show 162`, the Impasse's rings: without it\n"
"                   there is nothing in the world to take\n"
"  --scene-load A,S HARNESS: `scene.load A S`, the opcode and nothing else - over an\n"
"                   area not yet resident it is recorded and arrives with the area.\n"
"  --bank-reject    DEBUG: every bank is REFUSED as a full list refuses it,\n"
"                   the object staying in his hand. Without it, the original's\n"
"                   Inventory_Insert: a row, a merge, or for money and rings\n"
"                   Object_ApplyEffect - the count goes up, no row\n"
"  --money N        the player record's +172, the seteks - a HARNESS write, so\n"
"                   a shop purchase can be driven from a save that has none\n""  --clock T        the game clock at start, 0..3599999 a day (a harness write) - the\n"
"                   time of day the day/night cycle (fog colour, ambient) follows\n"
"  --rings N        the same over the record's +174, the anneaux - what a SAVE\n"
"                   costs, one apiece\n"
"  --give a,b,c     object ids into the carried list - a HARNESS write, not\n"
"                   `inventory.add`. A list, because a new game ships two\n"
"                   objects and the flows worth driving want a bagful: row\n"
"                   scrolling needs more than the nine row widgets, and\n"
"                   `Utiliser sur` needs a recipe pair (18,7 -> 33)\n"
"  --sneak          open the SNEAK as soon as he is on his feet, through\n"
"                   the same path TAB takes - for testing that screen\n"
"                   without walking to it\n"
"  TAB              once he is on his feet, opens the SNEAK - his handheld\n"
"                   device. It is the adventure scheme\'s own \"Ouvrir\n"
"                   sneak\" binding, and the .CTL has to play H_SNKON first,\n"
"                   so it is not instant. LEFT/RIGHT move between the\n"
"                   device\'s columns, UP/DOWN within one, ENTER opens a tab\n"
"\n"
"DISPLAY\n"
"  --res WxH        default: options row 2 as last saved, else 800x600; the\n"
"                   interface is authored at 640x480\n"
"                   and scaled, so a bigger display spreads the same layout\n"
"  --fullscreen     the desktop's mode, the frame scaled in at its aspect;\n"
"                   F11 toggles (--window, or [Preferences] window = 0|1)\n"
"  --vulkan         force the Vulkan backend (V toggles it live)\n"
"  --aa N           ENHANCEMENT, off by default: N-sample MSAA (2/4/8) on\n"
"                   Vulkan and desktop GL (not the Vita) - the original has none; also\n"
"                   [Enhancements] antialiasing=N in --config\n"
"  --filter M       texture filtering on a GPU backend, M = bilinear (the\n"
"                   DEFAULT: what the original drew on a 3D card), nearest\n"
"                   (its software devices; the software reference always\n"
"                   samples so) or trilinear (ENHANCEMENT: a mip chain\n"
"                   generated at load, GPU backends); also\n"
"                   [Enhancements] texturefiltering=M in --config\n"
"  --anisotropy N   ENHANCEMENT: N-tap anisotropic filtering (2..16), with\n"
"                   trilinear only; [Enhancements] anisotropy=N\n"
"  --clip 0         ENHANCEMENT: UNLIMITED draw distance - the option's own\n"
"                   values stop at 200 m. The fog goes with it, its range\n"
"                   being the clip distance; [Enhancements] clipdistance=0\n"
"  --text-scaling M ENHANCEMENT, off by default: M = game (the original draws\n"
"                   glyphs at their native size, so text shrinks as the\n"
"                   display grows) or fit (glyphs scaled with the layout);\n"
"                   [Enhancements] textscaling=M\n"
"  --ui-scaling M   ENHANCEMENT, off by default: how the 640x480 interface is\n"
"                   stretched to the display, M = nearest (the original's\n"
"                   Blt) or linear; BOTH backends, since the interface is\n"
"                   composed on the CPU; [Enhancements] uiscaling=M\n"
"  --radar game|always  shoot mode's minimap: the game's own switch (default -\n"
"                   a script turns it on, mostly only for an item nothing gives)\n"
"                   or in every shoot phase with a radar file; [Enhancements] radar=M\n"
"  --framerate N    ENHANCEMENT: present up to N frames a second (30..240,\n"
"                   default 30). The original had no cap - its delta is 30/fps\n"
"                   - so the simulation is unchanged; [Enhancements] framerate=N\n"
"  --smooth-anim    ENHANCEMENT: bodies slerped between two keys by the clock's\n"
"                   fraction; the original truncates to one. Seen above 30 fps;\n"
"                   [Enhancements] animation=smooth\n"
"  --software       force the software rasteriser\n"
"  --profile F      write a profiler capture to F (tools/omkprof.py reads it;\n"
"                   todo/debug-tools.md - absent from `make release`)\n"
"  --gl1-composite  the classic Mac's GL1: compose the frame on the CPU, not\n"
"                   the straight present (OMK_GL1_COMPOSITE=1)\n"
"  --letterbox      the 1.818:1 camera-mode bars, for laying a shot beside\n"
"                   a capture; --full is the old spelling of the opposite\n"
"\n"
"INPUT, scripted\n"
"  --keys D,D,...   DIK scancodes fed in order; `T` types --type there\n"
"  --type <text>    what `T` types - the start menu refuses an empty name\n"
"  --keydelay N     frames between scripted keys, default 2\n"
"  --no-foe-collision  the melee opponent collides with nothing (comparison)\n"
"  --no-fight-camera-collision  the fight camera is never pulled in (comparison)\n"
"  --fight-foe-at X,Z  HARNESS: start the melee opponent there\n"
"  --hold <stream>  after the hand-over, DIK codes HELD: `k200*120,k203*30`,\n"
"                   `+` joins several, `0*n` holds nothing - player_probe's\n"
"                   own syntax, so a walk can be replayed\n"
"\n"
"THE SET VIEWER (--scene)\n"
"  --cam N          step the set's own cameras\n"
"  --eye x,y,z      place the eye        --at x,y,z   aim it\n"
"  --fov F          horizontal field of view, degrees\n"
"  --nodelay        drop the 16 ms sleep. THE SET VIEWER ONLY - it does\n"
"                   nothing to a game run; use --frames for that\n"
"\n"
"  --help           this\n");
    };
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--help") { usage(stdout); return 0; }
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    fr = argv[1];
    // THE TABLES ARE OPTIONAL, and the argument for them is too.
    //
    // `tables/` holds what a replica cannot read out of the shipped tree - the
    // VM opcode table, the widget tree, the fonts and key bindings, the ADPCM
    // coefficients. Most of it only makes the run RICHER, so its absence is a
    // warning and not a refusal; each loader below says what is lost.
    //
    // The second positional argument is taken as the directory when it is not
    // a flag; otherwise a few obvious places are tried, so `omk-play <tree>`
    // works from the repo root or from `engine/`.
    if (argc >= 3 && argv[2][0] != '-') tb = argv[2];
    else {
        for (const char* cand : {"tables", "../tables", "../../tables"}) {
            if (omk::fileSize(std::string(cand) + "/vm_opcodes.json") >= 0) { tb = cand; break; }
        }
        if (!tb.empty())
            std::printf("tables: none given, using %s\n", tb.c_str());
    }
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scene" && i + 1 < argc) scene = argv[++i];
        else if (a == "--cam" && i + 1 < argc) camIndex = std::atoi(argv[++i]);
        else if (a == "--fov" && i + 1 < argc) fovA = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--call" && i + 1 < argc) callDialog = std::atoi(argv[++i]);
        else if (a == "--anim-hold") animHoldHarness = true;
        else if (a == "--letterbox") letterbox = true;   // camera mode, for comparing with captures
        else if (a == "--full") letterbox = false;       // kept: it was the old spelling
        else if (a == "--vulkan") startVulkan = true;
        // The game uses Vulkan when the machine has it; this forces the
        // software reference, which is what every check is written against.
        else if (a == "--software") forceSoftware = true;
        else if (a == "--gl1-composite") gl1Composite = true;
        else if (a == "--world-vulkan") worldVulkan = true;
        // The frame rate, reported once a second on stdout. Off by default:
        // it is a diagnostic, and a game that prints every second when nobody
        // asked is a game with a line of noise under it.
        else if (a == "--fps") showFps = true;
        else if (a == "--nodelay") noDelay = true;
        // THE SPEED MULTIPLIER, and the engine has one of its own.
        // `Game_Frame` switches on `dword_4E972C` to set the frame delta:
        //
        //     case 0   flt_4C30D8 = 30.0 / fps, capped at 3.0   the normal rate
        //     case 1   1.0        case 2   0.5
        //     case 3   0.1        case 4   2.0
        //
        // so the game ships with a double-speed and two slow-motions, driven
        // by scaling the DELTA rather than by running the loop faster. This
        // does the same to `frameSec`, so 2 is the engine's case 4 and 0.5 its
        // case 2. Clamped at the engine's own ceiling of 3.0 - case 0 caps
        // there, and past it a single frame steps further than any of the
        // runtime's own clamps expect.
        else if (a == "--speed" && i + 1 < argc) {
            speed = std::atof(argv[++i]);
            if (!(speed > 0.0)) speed = 1.0;
            if (speed > 3.0) speed = 3.0;
        }
        // How many frames apart the scripted keys are fed. The default 2 is
        // one press and one release, which is the minimum that produces an
        // EDGE; a walk that has to wait for a line to play needs more.
        else if (a == "--keydelay" && i + 1 < argc) keyEvery = std::atoi(argv[++i]);
        else if (a == "--hold" && i + 1 < argc) holdStream = argv[++i];
        // `--water-cam preset`: the FIRST reading of the swim camera - preset 21's
        // fixed eye turned by all three of the swimmer's angles - kept only to
        // lay beside the chase camera that replaced it (see the water entry).
        else if (a == "--water-cam" && i + 1 < argc) waterCamPreset = std::string(argv[++i]) == "preset";
        else if (a == "--snaps" && i + 1 < argc) snapsDir = argv[++i];
        else if (a == "--snap-every" && i + 1 < argc) snapEvery = std::max(1, std::atoi(argv[++i]));
        else if (a == "--flicker" && i + 1 < argc) flickerDir = argv[++i];
        else if (a == "--profile" && i + 1 < argc) profilePath = argv[++i];
        else if (a == "--res" && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &dispW, &dispH);
            resFlag = true;
        }
        else if (a == "--fullscreen") fullscreenFlag = 1;
        else if (a == "--window") fullscreenFlag = 0;
        else if (a == "--eye" && i + 1 < argc)
            haveEye = std::sscanf(argv[++i], "%f,%f,%f", &eyeA[0], &eyeA[1], &eyeA[2]) == 3;
        else if (a == "--at" && i + 1 < argc)
            haveAt = std::sscanf(argv[++i], "%f,%f,%f", &atA[0], &atA[1], &atA[2]) == 3;
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--dump" && i + 1 < argc) dump = argv[++i];
        else if (a == "--nofmv") playMovies = false;   // the engine's own switch
        else if (a == "--save" && i + 1 < argc) saveFile = argv[++i];
        else if (a == "--saves" && i + 1 < argc) savesPath = argv[++i];
        else if (a == "--slot" && i + 1 < argc) slotArg = std::atoi(argv[++i]);
        else if (a == "--save-slot" && i + 1 < argc) saveSlotArg = std::atoi(argv[++i]);
        else if (a == "--save-name" && i + 1 < argc) saveNameArg = argv[++i];
        else if (a == "--area" && i + 1 < argc) areaArg = std::atoi(argv[++i]);
        else if (a == "--address" && i + 1 < argc) addressArg = std::atoi(argv[++i]);
        // The SCRIPTED supermarket fight in one command. Not a harness: it
        // only stands the player in the zone whose record 0 runs the whole
        // sequence - `scene.unload 230`, `character.show 48`, the approach,
        // then `fight.begin 48`. The position is where the player crossed
        // into AREA 245 on a played run (`event 9 - his feet are on AREA
        // 245's decor`), so the zone scan raises it on the first frames.
        else if (a == "--fight-supermarket") {
            areaArg = 245;
            standAt[0] = 14855.0f; standAt[1] = -32.0f;
            standAt[2] = 1914.0f;  standAt[3] = 0.0f;   // the yaw is [3]
            haveStand = true;
        }
        else if (a == "--fight" && i + 1 < argc) fightArg = std::atoi(argv[++i]);
        else if (a == "--no-foe-collision") noFoeCollision = true;
        else if (a == "--no-fight-camera-collision") noFightCamRay = true;
        else if (a == "--fight-foe-at" && i + 1 < argc)
            foeAtSet = std::sscanf(argv[++i], "%f,%f", &foeAtXZ[0], &foeAtXZ[1]) == 2;
        else if (a == "--fight-level" && i + 1 < argc) fightLevelArg = std::atoi(argv[++i]);
        else if (a == "--ride") rideArg = true;
        else if (a == "--board") boardArg = true;
        else if (a == "--board-after" && i + 1 < argc) { boardArg = true; boardAfter = std::atoi(argv[++i]); }
        // A HARNESS FLAG, not a port: put an object into the carried list so
        // a flow can be exercised from a save that does not carry it. VM
        // opcode 50 `inventory.add` is what the game uses; this writes the
        // slot and runs none of its bookkeeping.
        else if (a == "--give" && i + 1 < argc) giveList = argv[++i];
        else if (a == "--money" && i + 1 < argc) moneyArg = std::atoi(argv[++i]);
        else if (a == "--rings" && i + 1 < argc) ringsArg = std::atoi(argv[++i]);
        else if (a == "--clock" && i + 1 < argc) clockArg = std::atol(argv[++i]);
        // A HARNESS FLAG, not a port: `--var 652=1,657=1` writes the game DB
        // directly. A flow can sit behind state no flag can otherwise reach -
        // the flat's lift gate tests `Porte Asc Fermee` and `Rencontre Telis`
        // before it even looks at what you carry, so the Telis cutscene
        // behind it cannot be entered from a save that predates them.
        else if (a == "--var" && i + 1 < argc) varList = argv[++i];
        else if (a == "--bank-reject") bankReject = true;
        // ...and its companion: keep the save's PLAYER but take the world
        // from `IAM\START`, so a flow can be tried against a new game's
        // state without the intro. Also a harness flag, not a port.
        else if (a == "--newgame-world") newWorld = true;
        else if (a == "--scene-chunk" && i + 1 < argc) sceneChunk = std::atoi(argv[++i]);
        else if (a == "--zone-enable" && i + 1 < argc) zoneEnable.push_back(std::atoi(argv[++i]));
        else if (a == "--zone-disable" && i + 1 < argc) zoneDisable.push_back(std::atoi(argv[++i]));
        else if (a == "--scene-load" && i + 1 < argc) {
            int sa = -1, ss = -1;
            if (std::sscanf(argv[++i], "%d,%d", &sa, &ss) == 2) sceneLoads.push_back({sa, ss});
        }
        else if (a == "--density" && i + 1 < argc) { density = std::atoi(argv[++i]); densityFlag = true; }
        else if (a == "--shadows" && i + 1 < argc) shadowFlag = std::atoi(argv[++i]);
        else if (a == "--thread-bodies") threadFlag = 1;
        else if (a == "--no-thread-bodies") threadFlag = 0;
        else if (a == "--no-tie") noTieFlag = true;
        else if (a == "--cpu-bodies") cpuBodiesFlag = true;
        else if (a == "--no-shadows") shadowFlag = 0;
        else if (a == "--detail" && i + 1 < argc) detailFlag = std::atoi(argv[++i]);
        else if (a == "--config" && i + 1 < argc) configFile = argv[++i];
        // `--clip 0` is the ENHANCEMENT, not a zero distance: the option's
        // five values stop at 200 m and 0 is outside them, so it says "no cap".
        else if (a == "--clip" && i + 1 < argc) { clipArg = std::atoi(argv[++i]); clipFlag = true; }
        else if (a == "--sky" && i + 1 < argc) skyFlag = std::atoi(argv[++i]);
        else if (a == "--aa" && i + 1 < argc) aaFlag = omk::msaaSamples(std::atoi(argv[++i]));
        else if (a == "--filter" && i + 1 < argc) {
            filterFlag = omk::textureFilterMode(argv[++i]);
            if (filterFlag < 0) {
                std::fprintf(stderr, "--filter %s: not a mode (nearest|bilinear|trilinear)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--anisotropy" && i + 1 < argc)
            anisoFlag = std::max(1, std::min(16, std::atoi(argv[++i])));
        else if (a == "--enhance-all") enhanceAll = true;
        else if (a == "--ssaa" && i + 1 < argc) ssaaFlag = std::atoi(argv[++i]);
        else if (a == "--smooth-anim") smoothAnimFlag = 1;
        else if (a == "--framerate" && i + 1 < argc) {
            frameRateFlag = omk::frameRateValue(argv[++i]);
            if (frameRateFlag < 0) {
                std::fprintf(stderr, "--framerate %s: not 30..240 (or game)\n", argv[i]);
                return 2;
            }
        }
        else if (a == "--radar" && i + 1 < argc) {
            radarFlag = omk::radarMode(argv[++i]);
            if (radarFlag < 0) {
                std::fprintf(stderr, "--radar %s: not a mode (game|always)\n", argv[i]);
                return 2;
            }
        }
        else if (a == "--dither" && i + 1 < argc) dither = std::atoi(argv[++i]) != 0;
        else if (a == "--invert-x") mouseInvertX = true;
        else if (a == "--invert-y") mouseInvertY = true;
        else if (a == "--shoot-eye" && i + 1 < argc) { shootEyeLift = float(std::atof(argv[++i])); shootEyeSet = true; }
        else if (a == "--no-dither") dither = false;
        else if (a == "--lighting" && i + 1 < argc) {
            lightingFlag = omk::lightingMode(argv[++i]);
            if (lightingFlag < 0) {
                std::fprintf(stderr, "--lighting %s: not a mode (pervertex|perpixel)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--text-scaling" && i + 1 < argc) {
            textScaleFlag = omk::textScalingMode(argv[++i]);
            if (textScaleFlag < 0) {
                std::fprintf(stderr, "--text-scaling %s: not a mode (game|fit)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--ui-scaling" && i + 1 < argc) {
            uiScaleFlag = omk::uiScalingMode(argv[++i]);
            if (uiScaleFlag < 0) {
                std::fprintf(stderr, "--ui-scaling %s: not a mode (nearest|linear)\n",
                             argv[i]);
                return 2;
            }
        }
        else if (a == "--shadow-quality" && i + 1 < argc) {
            shadowQFlag = omk::shadowQualityMode(argv[++i]);
            if (shadowQFlag < 0) {
                std::fprintf(stderr, "--shadow-quality %s: not a mode "
                                     "(classic|fitted|mapped)\n", argv[i]);
                return 2;
            }
        }
        else if (a == "--fog" && i + 1 < argc) drawFog = std::atoi(argv[++i]) != 0;
        else if (a == "--no-crowd-light") lightCrowd = false;
        else if (a == "--no-actor-light") lightActors = false;
        else if (a == "--fog-colour" && i + 1 < argc) {
            int rr = 0, gg = 0, bb = 0;
            if (std::sscanf(argv[++i], "%d,%d,%d", &rr, &gg, &bb) == 3) {
                fogRGB[0] = static_cast<std::uint8_t>(std::clamp(rr, 0, 255));
                fogRGB[1] = static_cast<std::uint8_t>(std::clamp(gg, 0, 255));
                fogRGB[2] = static_cast<std::uint8_t>(std::clamp(bb, 0, 255));
                fogRGBSet = true;
            }
        }
        else if (a == "--no-crowd") noCrowd = true;
        else if (a == "--no-script-sprites") noScriptSprites = true;
        // A HARNESS FLAG: start scene objects by their `scx.play` operand (the
        // handle >> 16) on the first adventure frame the scene is resident,
        // so a scene function can be looked at without the script that
        // would reach it. Not a port of anything.
        else if (a == "--scx-play" && i + 1 < argc) {
            std::string t = argv[++i], cur;
            for (char c : t + ",") {
                if (c == ',') { if (!cur.empty()) scxPlay.push_back(std::atoi(cur.c_str())); cur.clear(); }
                else cur.push_back(c);
            }
        }
        // A HARNESS FLAG: `--hide-show A,H,S,F` - `character.hide A` at
        // frame H and `character.show A, F` at frame S, through the
        // Session's own 78/79, so a body's return can be watched without the
        // script that would hide it (todo/drift-audit.md M1).
        // A HARNESS FLAG: `--game-restart N` - op 152 run as a context at
        // frame N, its arm and the pump's answer (todo/drift-audit.md S3).
        else if (a == "--game-restart" && i + 1 < argc) gameRestartAt = std::atol(argv[++i]);
        // A HARNESS FLAG: `--op-at F:OP,...` - an opcode that takes NO
        // operand bytes (106/107 the freeze, 116/117 the player's suspend,
        // 150/151 the greyscale bank, 152 the restart), run as a context at
        // frame F through the arm a script reaches (todo/drift-audit.md S7,
        // S11). Anything else is refused.
        else if (a == "--op-at" && i + 1 < argc) {
            std::string t = argv[++i], cur;
            for (char c : t + ",") {
                if (c != ',') { cur.push_back(c); continue; }
                long f = -1; int op = -1;
                if (std::sscanf(cur.c_str(), "%ld:%d", &f, &op) == 2 &&
                    (op == 106 || op == 107 || op == 116 || op == 117 || op == 150 ||
                     op == 151 || op == 152))
                    opAt.emplace_back(f, op);
                else if (!cur.empty())
                    std::fprintf(stderr, "--op-at: '%s' refused (F:OP, OP one of "
                                 "106 107 116 117 150 151 152)\n", cur.c_str());
                cur.clear();
            }
        }
        else if (a == "--hide-show" && i + 1 < argc)
            std::sscanf(argv[++i], "%d,%d,%d,%d", &hideShow[0], &hideShow[1],
                        &hideShow[2], &hideShow[3]);
        else if (a == "--shoot") startShoot = true;
        else if (a == "--shoot-end" && i + 1 < argc) shootEndAt = std::atol(argv[++i]);
        else if (a == "--shoot-health" && i + 1 < argc) shootHealth = std::atoi(argv[++i]);
        else if (a == "--player-at" && i + 1 < argc) {
            // a second `--player-at` is a second, later placement
            if (playerAtFrame >= 0) {
                if (std::sscanf(argv[++i], "%ld:%f,%f,%f,%f", &playerAt2Frame, &playerAt2[0],
                                &playerAt2[1], &playerAt2[2], &playerAt2[3]) != 5)
                    playerAt2Frame = -1;
            } else if (std::sscanf(argv[++i], "%ld:%f,%f,%f,%f", &playerAtFrame, &playerAt[0],
                                   &playerAt[1], &playerAt[2], &playerAt[3]) != 5)
                playerAtFrame = -1;
        }
        else if (a == "--astaroth-souls" && i + 1 < argc) astarothSoulsAt = std::atol(argv[++i]);
        else if (a == "--astaroth-health" && i + 1 < argc) astarothHealth = std::atoi(argv[++i]);
        else if (a == "--gandhar-health" && i + 1 < argc) gandharHealth = std::atoi(argv[++i]);
        else if (a == "--aim-at" && i + 1 < argc)
            aimAtSet = std::sscanf(argv[++i], "%f,%f,%f", &aimAt[0], &aimAt[1], &aimAt[2]) == 3;
        else if (a == "--fight-health" && i + 1 < argc) fightHealth = std::atoi(argv[++i]);
        else if (a == "--sneak") openSneak = true;
        else if (a == "--stand" && i + 1 < argc)
            haveStand = std::sscanf(argv[++i], "%f,%f,%f,%f", &standAt[0], &standAt[1], &standAt[2], &standAt[3]) >= 3;
        else if (a == "--type" && i + 1 < argc) typeText = argv[++i];
        else if (a == "--keys" && i + 1 < argc) {
            // A `T` in the list means "type `--type` here" - the start menu's
            // confirm is gated on a non-empty name field, so a scripted walk
            // has to type between two presses, not before them.
            //
            // ...and `cN` sends CHARACTER N down the same channel, which is
            // the only way to drive the field's control codes headlessly:
            // BACKSPACE and RETURN are `WM_CHAR` 8 and 13 to the engine and
            // reach no binding table at all, so a scan code in this list
            // cannot express them. `c8` is a backspace.
            std::string t = argv[++i], cur;
            for (char c : t + ",") {
                if (c == ',') {
                    if (cur == "T") scripted.push_back(kTypeMarker);
                    else if (cur.size() > 1 && cur[0] == 'c')
                        scripted.push_back(kCharMarker -
                                           std::stoi(cur.substr(1), nullptr, 0));
                    else if (!cur.empty()) scripted.push_back(std::stoi(cur, nullptr, 0));
                    cur.clear();
                }
                else cur.push_back(c);
            }
        } else if (a[0] != '-') screenId = std::atoi(a.c_str());
    }
    return -1;
}

}  // namespace omk

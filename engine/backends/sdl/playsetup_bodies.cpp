// SPDX-License-Identifier: GPL-3.0-or-later
// THE SETUP: the speaker, every staged body, melee, the effect sprites.
// A stretch of what was `main`'s body, moved byte for byte by
// `todo/play-split.md` (2026-10-02); `PlayState::run` calls the sections in
// order. Returns -1 to go on, or the exit code `main` returns.
#include "playstate.h"

int PlayState::setupBodies() {
    const auto& fs = *fs_;
    auto& session = *session_;
    // ---- THE SPEAKER --------------------------------------------------
    //
    // A conversation's cameras all aim at the character speaking it, so
    // without him they aim at nothing and the frame is black - which is what
    // the replica drew. Three things resolve him, and all three are the
    // engine's own:
    //
    //   * WHICH model: the DIALOG chunk's word 0 is the speaker's actor id,
    //     and the 276-byte actor record carrying that id at +272 names the
    //     model at +144 (`sub_40B190` is the scan);
    //   * WHERE he stands: the least-squares convergence of the line cameras'
    //     rays, dropped onto the walkable floor (`actor/speaker.h`);
    //   * HOW he is posed: the line's own `.3DM`, whose per-frame node
    //     quaternions compose down the mesh hierarchy (`actor/pose.h`).
    //
    // The pose advances with the VOICE - frame = elapsed * 30 - because that
    // is the clock the line runs on.
    // What is left here belongs to the LINE, not to a body: the model name,
    // the `.3DM` and its face vertices, the voice, and the camera solve that
    // says where a speaker no scene object drives is standing. The GEOMETRY
    // moved into `Staged`/`CharModel` above, one per actor (issue 41).
    // The scene clip's frame the last time it was drawn, and the frame it was
    // on when the current line began - `Morph_Play` hands the morph player
    // the actor's clip AND its frame (rec[47]), and the blend-in eases from
    // that frame into the line (pose.h, BLENDING TWO POSES).
    sceneFrameLast = 0.0f, lineIdleFrame = 0.0f;
    // the line's .3DM, for the FACE - the conversation's own bytes, shared
    voiceShot = -1;   // the line's voice in the mixer, for the press that cuts it
    speakerSolved = false;
    speakerReady = false;
    speakerConv = -1;
    // Whether the player's actor is in DIALOGUE MODE, so the two sides of
    // `Actor_EnterDialogueMode` / `Actor_LeaveDialogueMode` are called once
    // each per conversation. See the transition below.
    dialogMode = false;
    shootMode = false;      // ops 80/81, `actor/shootmode.h`

    OMK_HEAPCHECK("before every body");
    // (struct CharModel: `backends/sdl/playtypes.h`, todo/play-split.md)
    // (struct CharBank: `backends/sdl/playtypes.h`, todo/play-split.md)
    // `std::map` is node-based, so a `Staged`'s pointer into these survives
    // every later insert.
    // (struct PropModel: `backends/sdl/playtypes.h`, todo/play-split.md)
    // Which model each of `propGeo`'s batches came from: the geometry is
    // built before the pool assigns the sections their bases, so a batch's
    // slot is resolved at submission through its owner.
    // (struct Staged: `backends/sdl/playtypes.h`, todo/play-split.md)
    // OWNING POINTERS, not a vector of values: the Vulkan backend caches a
    // vertex buffer by (pointer, revision), so a `Staged` may never be moved
    // by a reallocation. Dropping one frees its address, which a later one
    // could reuse - `posed.revision` is taken from the global `worldGeoRev`
    // every frame it is drawn, so a reused address can never carry a
    // revision the backend has already seen.
    // (struct PedJob: `backends/sdl/playtypes.h`, todo/play-split.md)

    // (struct VehStaged: `backends/sdl/playtypes.h`, todo/play-split.md)
    vehDrawn = 0, vehLive = 0, vehStopped = 0;
    vehTold = -1;
    // bumped wherever `pedTracks` or `pedStaged` is cleared: a walker's cached
    // root / rest / feet (`PedStaged::cacheGen`) are then taken afresh
    pedCacheGen = 0;
    pedDrawn = 0, pedLive = 0, pedInAction = 0, pedIdle = 0, pedOffView = 0;
    // how many (walker, light) pairs actually reached this frame
    pedLit = 0;
    pedTold = -1;
    // one shoot record per gunman, built from his own properties the first
    // frame he is staged and kept for the run (`todo/shoot-mode.md` 7d)
    // Is the FIRST-PERSON shoot camera actually the one in force? Hiding the
    // player is right only while it is: a reader found Kay'l missing at the
    // end of the supermarket cutscene because the port hid him for "shoot
    // mode" while a script's own camera had taken the view back to third
    // person, leaving an empty room (`todo/omk-play.md` 97).
    // (the mouse's sensitivities are the engine's own now - `mouseSensX` below)

    shootCameraLive = false;
    // The first-person AIM. Yaw is the player's own facing (the mouse turns
    // the body, which is what the shoot scheme's `Tourner` keys do too);
    // pitch is the camera's alone, since nothing in the 14-slot word carries
    // it and the body has no bone for it here. Clamped to +/-70 degrees, a
    // choice this port is making - the engine's own limit is untraced.
    shootPitch = 0.0f;
    // A GUNMAN'S SHOTS (`sub_424DE0`'s fire epilogue, below): which of them
    // has said how his weapon resolved, and how many bolts each has fired.
    // The jitter's `rand()` is the CRT's own generator from its default seed
    // - the engine's is one stream shared by every caller, so this is the
    // formula and not the engine's place in the sequence.
    // (struct GunClip: `backends/sdl/playtypes.h`, todo/play-split.md)
    // (struct GunAnim: `backends/sdl/playtypes.h`, todo/play-split.md)
    // ...or the clip SLOT the brain started (`sub_4272B0` case 9, the attack by
    // +108) - when set it wins over the type, and an action's type clears it
    // THE PLAYER'S DEATH (`sub_423FC0`): the countdown `dword_4E975C` his death
    // clip runs for, and the gunmen told to stand down on their next tick
    playerDeathCountdown = 0.0f;
    // (struct GunAim: `backends/sdl/playtypes.h`, todo/play-split.md)
    // THE GAUGE'S OWN VALUE, `dword_90E100` - what `Hud_DrawBar` draws. It is
    // NOT his record's +92: `Shoot_Enter` seeds it, `sub_423A40` (a property 1
    // write, the medikits) and a hit he survives copy +92 into it, and the
    // killing hit returns before it is touched, so the bar keeps its last value.
    hudHealth = 0;
    // the CRT's `rand()` (MSVC: `seed * 214013 + 2531011`, bits 16..30) from
    // its default seed, drawn by the gunmen AND the melee AI - the engine's is
    // one stream too. Never the host's `std::rand()`, which is another
    // generator and is shared with whatever system library calls it.
    gunRandSeed = 1;
    hudAmmo = -1;
    shootMoveFrames = 0;
    shootMoveDist = 0.0f;
    // `sub_47D370`'s sensitivities and its invert, options rows 23, 24 and 25
    // (`word_90E1AC` / `word_90E1AE` / `byte_90E1B0`, the header's +44/+46/
    // +48) out of the save header or the ini's MouseSensX / MouseSensY - the
    // defaults 20, 15 and off. The mouse and the turn keys share row 23.
    mouseSensX = settings.v.mouseSensitivityX;
    mouseSensY = settings.v.mouseSensitivityY;
    // `--invert-y` flips the row, as choosing it in the menu would
    mouseInverted = settings.v.mouseInverted != mouseInvertY;
    shootPitchDirty = false;   // MDLUP / MDLDO moved the pitch this frame
    shotsFired = 0;
    stagedEver = 0;                 // for the summary line
    // The pool is rebuilt on a COMPOSITION change, not on a size change: two
    // models with the same texture count swapping is exactly what a size test
    // cannot see.
    poolComposition = 1, poolBuiltFor = 0, poolTold = 0;
    poolHasSprites = false, poolHasPlayer = false;
    playerTexBase = 0, spriteTexBase = 0;
    // sprite id -> its slot within the pool's sprite section, or -1
    // The sprite ids the resident scene can actually name - what goes in the
    // pool, as opposed to everything that decoded.
    poolOverflowTold = false;
    stagedProbe = std::getenv("OMK_STAGE_PROBE") != nullptr;
    // `engine: camera obstruction` - one line per frame from `sub_417070`.
    obstructProbe = std::getenv("OMK_CAM_OBSTRUCT_PROBE") != nullptr;
    // A DIAGNOSTIC for the sign of the scene call's Euler: CLAUDE.md 5's
    // rule is that leaving the game's space reflects one axis and so
    // reverses the sense of every rotation about it.
    progYawSign = std::getenv("OMK_PROGYAW_NEG") ? -1.0f : 1.0f;
    // ...and that clip as a pose, at FRAME 0. The recipe is
    OMK_HEAPCHECK("before melee");
    // (struct FightRun: `backends/sdl/playtypes.h`, todo/play-split.md)
    fightCamTold = false;        // one line when mode 14 takes the view
    session.setFightHook([&](int opponentId, int level) {
        return beginMelee(opponentId, level);
    });

    poolSize = 0;

    // The 3D renderer, behind `PORTING` A2's boundary: the GPU one when the
    // machine has it, the software reference otherwise. Everything below
    // submits DECISIONS and never touches an API, which is what lets the two
    // be swapped by assigning a pointer.
    OMK_HEAPCHECK("before effect sprites");
    // (struct SpriteTable: `backends/sdl/playtypes.h`, todo/play-split.md)
    spriteLookup = [this](int id) { return spriteTab.framesOf(id); };
    // THE TWO LIBRARIES' SPRITES, decoded ONCE (2026-09-23). They never change,
    // and every scene change read `aventure.SCX` and `fight.SCX` again - 4 MB,
    // which a console's memory card takes most of a second over, on the frame
    // of the hand-over. The table a scene change starts from is this one; the
    // order global, fight, scene - and so every collision - is unchanged.
    spriteBaseGlobal = 0, spriteBaseFight = 0;
    // WHICH scene's sprites `spriteTex` currently holds. An effect names its
    // sprite by ID and `Sfx_TickAmbient` resolves that id through the SCENE
    // (`sub_4A5800`), and the ids are scene-local: `Grid.sfx` wants 9..12 and
    // `Grid.SCX` registers exactly those, `anekbah.sfx` wants 49589..49591 and
    // `anekbah.SCX` registers exactly those. Loading one scene's sprites ONCE
    // at boot and then changing scene leaves every later effect resolving
    // against the wrong table - Anekbah's three ids fall outside it entirely
    // and draw nothing, which is fire and smoke not working, while the
    // Impasse's 13/14/114... collide with the GLOBAL library's and draw the
    // wrong picture, which is `todo/omk-play.md` 48.
    // THE GLOBAL LIBRARY IS THE EFFECTS SCENE. `Game_Start` loads
    // `SCPTDATA\aventure.scx` into `stru_930780` (04_sys.c 4425), and
    // `Effects_SetScene(.., &stru_930780)` (05_sys.c 2177/2208) makes THAT the
    // scene `Cef_TickEffects` resolves a `.CTL` record's sound id in through
    // `Scene_FindSoundIndex`, and `Cef_SpawnEffect` its sprite id through
    // `sub_4A5800`. Not the resident scene: the Impasse's 20 sounds have no
    // 194 (the grab, POBJ01.WAV), 199/203 (the footsteps STPR/STPL) or 180,
    // and its 187 is a DEMON footstep where the library's 187 is SNEAKIN.WAV.
    // Searching the resident scene - what this did until 2026-09-05 - made
    // the grab silent and played a demon's step on the confirm.
    if (const auto gp = fs.resolve("SCPTDATA/aventure.SCX")) {
        globalRt = std::make_unique<omk::ScxRuntime>(omk::DataFs::readPath(*gp));
        if (!globalRt->valid()) globalRt.reset();
    }
    // ...AND THE FIGHT'S OWN LIBRARY. `Fight_Begin` (0x004455B0) ends with
    // `Game_Start("fight.scx")` - a second `Game_Start`, exactly like the one
    // that installs `aventure.scx` at boot - and `gamedata/SCPTDATA/fight.SCX`
    // holds **21 sounds and 16 sprites** that ship nowhere else: the punches
    // (`CPOING02`, `PUNCHD`, `PUNCHG`), the kicks (`CPIED03`), the head hit
    // (`COUPTETE03`), the block (`ARRETCOUP01`), the fall (`CHUTEF04`), the
    // cries and `STEPSH1`, at ids 402..423.
    //
    // Without it every `.CTL` effect record of a fight fired with its timing,
    // attach point and scale all computed correctly and then failed its
    // lookup: 68 `ctl-effect` lines in a played fight, every one of them
    // `sound id 408 / 417 / 419 is not in the global library` (those three are
    // `ELECMB02`, `MVT02` and `ELECMB03`). A reader: *"no sound fx, no visual
    // effect"* (`todo/fight-mode.md` 15.2).
    //
    // Loaded once and kept, rather than at `fight.begin`: the engine's
    // `Game_Start` is a load, and doing it here costs one read instead of one
    // per fight.
    if (const auto fp = fs.resolve("SCPTDATA/fight.SCX")) {
        fightRt = std::make_unique<omk::ScxRuntime>(omk::DataFs::readPath(*fp));
        if (!fightRt->valid()) fightRt.reset();
        std::printf("fight library: SCPTDATA/fight.SCX %s\n",
                    fightRt ? "loaded (Fight_Begin's Game_Start)" : "INVALID");
    }
    {
        const int glob = loadSpritesInto(spriteBase, "aventure.SCX");
        // ...then the FIGHT's, which `Fight_Begin`'s own `Game_Start` installs
        // (see `fightRt` above). Its 16 sprites are the blow effects the
        // `.CTL` combat states spawn - ids 8..14, 32..35, 40, 43, 44, 192,
        // 193 - and without them every one reported `sprite N is not
        // registered by the library or the scene`.
        //
        // Loaded BEFORE the scene rather than after, although the engine's
        // `Game_Start` comes last, so that no existing scene's ids change
        // meaning: measured, the fight's sixteen collide with NONE of
        // `aventure.SCX`'s twenty, and the fight's own set `ASm49res.SCX`
        // registers no sprites at all, so the order is unobservable here and
        // the safe one is chosen deliberately. A scene that did register one
        // of those ids would keep its own, which is the behaviour this tree
        // already has everywhere else.
        const int fightSp = loadSpritesInto(spriteBase, "fight.SCX");
        spriteBaseGlobal = glob;
        spriteBaseFight = fightSp;
        spriteTab = spriteBase;
        // ...then the SCENE's own, which is what `Sfx_TickAmbient` resolves
        // against and which wins where the ids collide.
        const int local = session.scene().file().empty()
                            ? 0 : loadSprites(session.scene().file());
        int okTex = 0; std::size_t frames = 0;
        for (const auto& kv : spriteTab.tex) if (kv.second.width) ++okTex;
        for (const auto& kv : spriteTab.frames) frames += kv.second.frames.size();
        std::printf("sprites: %d global + %d fight + %d from %s, %d decoded over ids "
                    "0..%zu, %zu frames in all\n", glob, fightSp, local,
                    session.scene().file().c_str(), okTex,
                    spriteTab.idCount == 0 ? 0 : spriteTab.idCount - 1, frames);
        spriteScx = session.scene().file();
    }

    omk::Renderer& world = *(world_ = &(vkRen ? *vkRen
                         : glRen ? *glRen
                         : worldVk ? *worldVk
                                   : static_cast<omk::Renderer&>(worldSw)));
    // THE ENHANCEMENTS THIS RENDERER CANNOT DRAW, refused here rather than
    // prepared every frame for nothing: each one also turns off what it
    // replaces, so an undrawn per-pixel light left the crowd UNLIT and an
    // undrawn shadow map left every body SHADOWLESS - the Vita, 2026-09-29,
    // whose shared config asked for both. Mapped falls back to fitted, the
    // best of the shadows every backend draws (they are geometry).
    if (lighting > 0 && !world.drawsPixelLights()) {
        std::printf("lighting: per pixel REFUSED - the %s renderer does not draw it; "
                    "per vertex, as the engine lights\n", world.name());
        lighting = 0;
    }
    if (shadowQuality >= 2 && !world.drawsShadowMap()) {
        std::printf("shadows: mapped REFUSED - the %s renderer has no shadow map; fitted\n",
                    world.name());
        shadowQuality = 1;
    }
    worldReady = false;
    // (struct WorldSlot: `backends/sdl/playtypes.h`, todo/play-split.md)
    worldGen = 0;                  // bumped by every `rebuildWorld`: re-bake the tie

    // (struct Sky: `backends/sdl/playtypes.h`, todo/play-split.md)
    // ---------------------------------------------------- THE SHADOWS
    //
    // Option row 5 *Affichage des ombres*. `sub_419060` loads
    // `MESHES\MISC\shadows.3DO` once at game start, `Actor_LoadModel` clones
    // it under every character, and `Actors_TickAll` emits blobs under a
    // fixed set of BONES each frame - see `o3de/shadow.h` for the whole
    // mechanism. Loaded once here for the same reason the engine loads it
    // once: it is not a set's asset and no area change touches it.
    shadowModel = omk::loadShadowModel(fs);
    // Rebuilt every frame into this one object, outside the loop so the
    // revision accumulates - the same rule `fxGeo` above states, and for the
    // same backend-side reason.
    shadowTexBase = 0;
    // The worst distance from a walker's foot-pair midpoint to his own body
    // point this frame - the orphan-shadow detector (see the ped loop).
    shadowFootOffMax = 0.0f, pedFootOffMax = 0.0f;
    // The worst vertical spread inside ONE blob - 0 for every classic blob by
    // construction, nonzero for a fitted one wherever the ground is not flat.
    shadowSpreadMax = 0.0f, shadowSpreadPlayer = 0.0f;
    shadowBlobsDrawn = 0;
    shadowTold = false, shadowLightTold = false, lightsTold = false;
    shimmerClock = 0.0f;   // `dword_907310`, wrapped at 256
    // A set's geometry is REBUILT on every area change, and a fresh
    // Geometry's revision is 0 - the same value the previous set was cached
    // under by the Vulkan backend, which keys its vertex buffer on the
    // POINTER and the revision. So the Impasse drew GRID's tunnel through the
    // Impasse's batch ranges: black with a few stray triangles where the
    // software renderer, which reads the Geometry directly, drew the alley.
    // The particle geometry hit the identical fault on 2026-09-02; the cure
    // is the same - a revision that only ever climbs.
    worldGeoRev = 0;
    worldFrames = 0;
    // The particles' quads, REBUILT every frame into this one object. It lives
    // outside the frame loop so `Geometry::revision` accumulates: the Vulkan
    // backend caches a vertex buffer by pointer and revision, and a
    // block-local Geometry had the same address and a revision of 1 on every
    // frame, so the GPU drew the first frame's particles for ever while the
    // software path animated - the posed-character bug over again, one level
    // out, because this geometry is rebuilt rather than mutated.
    // Where the scene's character is this frame - his model origin, the
    // pelvis - for set pieces linked to him. `sub_450FC0` case 2 finds an
    // actor by the first THREE letters of its name, uppercased ('HO1' for
    // HO1_FNM, Kay'l); the intro's arrival piece is linked that way and
    // plays `kaylarr` and `kay arr` at his position as he lands. His frame
    // is the identity here because the facing is not applied (see
    // `SceneRunner::Started::euler`), and the position is LAST frame's -
    // the runner ticks before the pose is composed. Type 3 (the PLAYER,
    // `unk_8F5EA0`) has no counterpart in this viewer and is left to the
    // runner's absolute fallback, which setpiece.h labels.
    actorKnown = false;
    session.sceneMutable().setPieceLinks(
        [&](int type, std::uint32_t id, omk::PieceLink& L) -> bool {
            if (type != 2) return false;
            const char tag[4] = {static_cast<char>((id >> 16) & 0xFF),
                                 static_cast<char>((id >> 8) & 0xFF),
                                 static_cast<char>(id & 0xFF), 0};
            const auto tagged = [&](const std::string& m) {
                if (m.size() < 3) return false;
                for (int k = 0; k < 3; ++k) {
                    char c = m[static_cast<std::size_t>(k)];
                    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    if (c != tag[k]) return false;
                }
                return true;
            };
            // EVERY body on screen is a candidate now, not just the one this
            // file used to draw - the piece is linked to the actor whose model
            // carries the tag.
            for (const auto& up : staged)
                if (up->drawn && tagged(up->model)) {
                    for (int k = 0; k < 3; ++k) L.pos[k] = up->drawAt[k];
                    L.hasMatrix = true;   // identity: the facing is unported
                    return true;
                }
            if (!actorKnown || !tagged(playerModel)) return false;
            for (int k = 0; k < 3; ++k) L.pos[k] = actorAt[k];
            L.hasMatrix = true;   // identity: the facing is unported
            return true;
        });
    lastCamera = -2;
    // (struct SetLoad: `backends/sdl/playtypes.h`, todo/play-split.md)
    // what each slot was last ASKED for - not `WorldSlot::stem`, which is what
    // is IN it: a set that does not resolve is asked for once, not every frame
    syncSets = omk::envSet("OMK_SYNC_SETS");   // A/B only
    // THE LOAD HELD FOR THE SET (`Session::setLoadGate`). A console's card and
    // A9 took 1.3 s over Anekbah where the slices give 0.57, and the frame
    // that brought the set in waited 727 ms for the rest (2026-09-30). With
    // the gate the Session's load waits instead, as `Area_TickLoad` waits on
    // its reader, and the game draws on. The frame the set arrives on is then
    // the machine's, so it is ON only where that is wanted: a Vita, or
    // `OMK_LOAD_GATE=1`; every check runs by the count alone.
#if defined(__vita__)
    loadGate = !syncSets && !omk::envSet("OMK_NO_LOAD_GATE");
#else
    loadGate = !syncSets && omk::envSet("OMK_LOAD_GATE");
#endif
    // (a test's slow card: the job sleeps this long first - desktop only)
    loadDelayMs = std::getenv("OMK_LOAD_DELAY_MS") ? std::atoi(std::getenv("OMK_LOAD_DELAY_MS")) : 0;
    session.setVoiceToDevice([](const std::vector<std::int16_t>& pcm, int channels) {
        return resampleToDevice(pcm, channels, 22050, kDeviceRate);
    });
    if (loadGate)
        session.setLoadGate([this](int slot) {
            const auto& L = setLoads[slot & 1];
            return !L || !L->job || L->job->ready();
        });

    return -1;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE VIEWER'S STATE - every local of `main`'s body, as a member under the
// same name and in the same order (so they are destroyed in that order),
// gathered by `todo/play-split.md` S3. `run` is `main`'s body: each
// declaration there is an ASSIGNMENT at the same point, so everything is set
// in the original order. The list and the types
// came from clang's AST.
//
// Three kinds stay locals of `run`: the lambdas, the function-local statics
// and constants, and `world` (a reference chosen at run time). Seven objects
// that are built from others and can be neither default-built nor assigned
// (the data tree, the text layout, the composer, the options menu, the input,
// the inventory, the Session) are `std::optional` storage here, built in
// place by `emplace` where they were declared, under their old name.
// The command-line flags and the audio group are REFERENCES into `opt` and
// `game`, bound when the object is built (the parse fills them later).
#pragma once

#include "playshared.h"
#include "platform/cacheline.h"   // clearFrame
#include "platform/env.h"
#if OMK_VR
#include "../vr/playvr.h"   // VrState: the VR build's only state (todo/quest-port.md)
#endif

#include <optional>

// WHAT A SETUP SECTION OR A FRAME PHASE ASKS OF THE LOOP AROUND IT
// (`PlayState::run`, `PlayState::step`). Any value >= 0 ends the program at
// once with that exit code - a `--help`, a failed load, a finished render.
namespace phase {
constexpr int Continue = -1;   // go on: the next section, phase or frame
constexpr int EndLoop = -2;    // leave the frame loop; `finish()` writes the dumps and returns
}

struct PlayState {
    omk::PlayOptions opt{};
    const std::string & dataRoot = opt.fr;
    std::string & tb = opt.tb;
    int & screenId = opt.screenId;
    int & frames = opt.frames;
    bool & playMovies = opt.playMovies;
    std::string & dump = opt.dump;
    std::string & typeText = opt.typeText;
    int & dispW = opt.dispW;
    int & dispH = opt.dispH;
    bool & resFlag = opt.resFlag;
    int & fullscreenFlag = opt.fullscreenFlag;
    std::vector<int> & scripted = opt.scripted;
    bool & bankReject = opt.bankReject;
    int & keyEvery = opt.keyEvery;
    std::string & holdStream = opt.holdStream;
    std::string & snapsDir = opt.snapsDir;
    std::string & flickerDir = opt.flickerDir;
    bool & waterCamPreset = opt.waterCamPreset;
    int & snapEvery = opt.snapEvery;
    std::string & saveFile = opt.saveFile;
    std::string & savesPath = opt.savesPath;
    int & slotArg = opt.slotArg;
    int & saveSlotArg = opt.saveSlotArg;
    std::string & saveNameArg = opt.saveNameArg;
    int & areaArg = opt.areaArg;
    int & addressArg = opt.addressArg;
    int & density = opt.density;
    int & fightArg = opt.fightArg;
    bool & noFoeCollision = opt.noFoeCollision;
    bool & noFightCamRay = opt.noFightCamRay;
    bool & foeAtSet = opt.foeAtSet;
    float (&foeAtXZ)[2] = opt.foeAtXZ;
    int & fightLevelArg = opt.fightLevelArg;
    bool & rideArg = opt.rideArg;
    bool & boardArg = opt.boardArg;
    int & boardAfter = opt.boardAfter;
    std::string & configFile = opt.configFile;
    bool & densityFlag = opt.densityFlag;
    bool & clipFlag = opt.clipFlag;
    int & clipArg = opt.clipArg;
    int & skyFlag = opt.skyFlag;
    int & shadowFlag = opt.shadowFlag;
    int & threadFlag = opt.threadFlag;
    bool & noTieFlag = opt.noTieFlag;
    bool & cpuBodiesFlag = opt.cpuBodiesFlag;
    bool & fullTexturesFlag = opt.fullTexturesFlag;
    int & detailFlag = opt.detailFlag;
    int & aaFlag = opt.aaFlag;
    int & filterFlag = opt.filterFlag;
    int & anisoFlag = opt.anisoFlag;
    int & shadowQFlag = opt.shadowQFlag;
    int & uiScaleFlag = opt.uiScaleFlag;
    int & textScaleFlag = opt.textScaleFlag;
    int & lightingFlag = opt.lightingFlag;
    int & ssaaFlag = opt.ssaaFlag;
    int & radarFlag = opt.radarFlag;
    int & frameRateFlag = opt.frameRateFlag;
    int & smoothAnimFlag = opt.smoothAnimFlag;
    std::string & texturePackFlag = opt.texturePackFlag;
    std::string & uiPackFlag = opt.uiPackFlag;
    std::string & fontPackFlag = opt.fontPackFlag;
    std::string & filmPackFlag = opt.filmPackFlag;
    float & bloomFlag = opt.bloomFlag;
    std::string & gradeFlag = opt.gradeFlag;
    std::string & remasterFlag = opt.remasterFlag;
    float & waterFlag = opt.waterFlag;
    float & floorsFlag = opt.floorsFlag;
    float & reflectionsFlag = opt.reflectionsFlag;
    float & wetFlag = opt.wetFlag;
    int & restoreWeatherFlag = opt.restoreWeatherFlag;
    float & rain3dFlag = opt.rain3dFlag;
    float & mistFlag = opt.mistFlag;
    float & upscaleFlag = opt.upscaleFlag;
    float & preloadFlag = opt.preloadFlag;
    float & bodyLightFlag = opt.bodyLightFlag;
    float & fxLightFlag = opt.fxLightFlag;
    float & reliefFlag = opt.reliefFlag;
    float & smoothFlag = opt.smoothFlag;
    std::string & smoothSkipFlag = opt.smoothSkipFlag;
    float & aoFlag = opt.aoFlag;
    bool & dither = opt.dither;
    bool & mouseInvertX = opt.mouseInvertX;
    bool & mouseInvertY = opt.mouseInvertY;
    float & shootEyeLift = opt.shootEyeLift;
    bool & shootEyeSet = opt.shootEyeSet;
    bool & enhanceAll = opt.enhanceAll;
    bool & drawFog = opt.drawFog;
    bool & lightCrowd = opt.lightCrowd;
    bool & lightActors = opt.lightActors;
    std::uint8_t (&fogRGB)[3] = opt.fogRGB;
    bool & fogRGBSet = opt.fogRGBSet;
    omk::DayNight dayNight{};          // this frame's `sub_41E7A0` for the active slot
    int dayNightToldArea = -2, dayNightToldPhase = -1;
    // THE UNDERWATER MODE, `dword_93082C` (todo/drift-audit.md L1 step 5):
    // the water line `sub_413CD0` probes when the swim camera is set up
    // (`flt_4E7D0C`), whether the camera's eye is under it, and the clock the
    // sway reads (`dword_4E9750`, + dt x 0.0004 a frame, wrapped at 1)
    // `flt_4E7D0C` is in the BSS (`dd ?`): 0.0 until a probe finds a 0x20000000
    // surface, and kept for the session after - so the mode works from 0.0 too
    float waterLine = 0.0f;
    bool  waterLineKnown = true;
    bool  underwater = false;
    bool  swimCamNow = false;          // this frame's camera is the swim variant (flags 0x4800)
    float swayClock = 0.0f;
    int activeAmbientGrey = 0;         // the active scene's +416 this frame (static, or by the clock)
    std::string & giveList = opt.giveList;
    int & moneyArg = opt.moneyArg;
    int & ringsArg = opt.ringsArg;
    long & clockArg = opt.clockArg;
    std::string & varList = opt.varList;
    std::string & addressEnableList = opt.addressEnableList;
    bool & newWorld = opt.newWorld;
    int & sceneChunk = opt.sceneChunk;
    std::vector<int> & zoneEnable = opt.zoneEnable;
    std::vector<int> & zoneDisable = opt.zoneDisable;
    std::vector<int> & characterShow = opt.characterShow;
    std::vector<int> & zoneRun = opt.zoneRun;
    std::vector<std::pair<int, int>> & characterProg = opt.characterProg;
    std::vector<std::pair<int, int>> & sceneLoads = opt.sceneLoads;
    bool & noCrowd = opt.noCrowd;
    bool & noScriptSprites = opt.noScriptSprites;
    std::vector<int> & scxPlay = opt.scxPlay;
    int (&hideShow)[4] = opt.hideShow;
    std::vector<omk::PlayOptions::OmkSet> & omkSets = opt.omkSets;
    long & gameRestartAt = opt.gameRestartAt;
    std::vector<std::pair<long, int>> & opAt = opt.opAt;
    bool & openSneak = opt.openSneak;
    bool & startShoot = opt.startShoot;
    int & shootHealth = opt.shootHealth;
    float (&aimAt)[3] = opt.aimAt;
    bool & aimAtSet = opt.aimAtSet;
    long & playerAtFrame = opt.playerAtFrame;
    long & playerAt2Frame = opt.playerAt2Frame;
    float (&playerAt)[4] = opt.playerAt;
    long & astarothSoulsAt = opt.astarothSoulsAt;
    int & astarothHealth = opt.astarothHealth;
    int & gandharHealth = opt.gandharHealth;
    int & fightHealth = opt.fightHealth;
    long & shootEndAt = opt.shootEndAt;
    std::vector<omk::PlayOptions::PadRun> & padRuns = opt.padRuns;
    float (&standAt)[4] = opt.standAt;
    bool & haveStand = opt.haveStand;
    std::string & scene = opt.scene;
    int & camIndex = opt.camIndex;
    float (&eyeA)[3] = opt.eyeA;
    float (&atA)[3] = opt.atA;
    float & fovA = opt.fovA;
    int & callDialog = opt.callDialog;
    bool & animHoldHarness = opt.animHoldHarness;
    bool & haveEye = opt.haveEye;
    bool & haveAt = opt.haveAt;
    bool & letterbox = opt.letterbox;
    bool & startVulkan = opt.startVulkan;
    bool & noDelay = opt.noDelay;
    double & speed = opt.speed;
    bool & forceSoftware = opt.forceSoftware;
    bool & gl1Composite = opt.gl1Composite;
    bool & gl1FogUnit = opt.gl1FogUnit;
    bool & gl1NoLock = opt.gl1NoLock;
    bool & gl1CpuFog = opt.gl1CpuFog;
    bool & filmsDirect = opt.filmsDirect;
    bool & showFps = opt.showFps;
    bool & worldVulkan = opt.worldVulkan;
    omk::Game game{};
    bool boardPress{};
    bool mountSpent{};   // the action button is edged, not held
    bool calledOpenTold{};
    bool boarded{};   // aboard, `Slider_TickRide` not yet driving
    bool boarding{};
    float doorOff[3] = {0, 0, 0};   // the placement, in the SLIDER's frame
    int doorOffState{};   // 0 not read yet, 1 read, -1 unavailable
    double boardCam{};   // frames (at 30 Hz) left of `Camera_Request(9, ..)`
    bool leaving{};
    float exitOff[3] = {0, 0, 0};
    int exitOffState{};
    omk::NodeTracks doorIn{};
    omk::NodeTracks doorOut{};
    bool doorClipsRead{};
    int journeyTo{};   // the address the journey ends at
    int calledDestination{};
    std::optional<omk::SliderRide> ride{};
    bool scxPlayed{};
    float savedAt[3] = {0, 0, 0};
    float savedYaw{};
    bool haveSavedPlacement{};
    std::optional<omk::DataFs> fs_;
    omk::UiWidgets uiWidgets{};
    omk::FontTable fonts{};
    std::optional<omk::TextLayout> lay_;
    std::optional<omk::ScreenComposer> comp_;
    omk::OptionTree optTree{};
    std::optional<omk::OptionsMenu> optMenu_;
    // OMK's own pages on it, and their texts (`playomkoptions.cpp`)
    std::optional<omk::OmkOptions> omkOpt_;
    omk::OmkText omkText_{};
    omk::Surface view3dPic{};
    omk::MenuCloud cloud{};
    omk::ControlSchemes schemes{};
    omk::KeyNames keyNames{};
    std::optional<omk::Input> in_;
    omk::OpcodeTable opcodes{};
    omk::GameState state{};
    std::vector<std::byte> saveBytes{};
    std::string saveFrom{};
    std::string loadedName{};
    std::optional<omk::SettingsBlock> saveSettings{};
    omk::OptionsFile ini{};
    omk::Settings settings{};
    bool unlimitedClip{};
    double clipInches{};
    bool clipReport{};
    struct FlickerCatcher {
        std::vector<std::vector<std::uint16_t>> ring{};   // the last kFlickPre frames
        std::vector<std::string> note{};   // ...and their context
        std::vector<long> lit{};   // the lit-count window
        int after{};   // frames still to write
        long event{};
        long quietUntil{};
    } flicker{};
    std::string frameNote{};   // this frame's context
    bool drawSky{};
    bool drawShadows{};
    bool threadBodies{};
    int shadowQuality{};
    int shadowDetail{};
    int lighting{};
    int aaSamples{};
    int texFilter{};
    int texAniso{};
    int ssaa{};
    bool radarAlways{};
    int frameRate{};
    bool smoothAnim{};
    int uiScaling{};
    int textScaling{};
    char clipText[128]{};
    bool forceAdventure{};
    // the shared `IAM\OBJECT` table (`omk::sharedObjects`), not a copy of it
    const std::vector<omk::ObjectRecord>* objectRecords = &omk::noObjects();
    std::vector<std::byte> globalFile{};
    std::vector<omk::Recipe> recipes{};
    std::optional<omk::Inventory> inv_;
    std::vector<omk::Destination> destinations{};
    bool sliderTold{};
    std::string examineTold{};
    std::string examineText{};
    std::string memoBodyPending{};
    std::optional<omk::Session> session_;
    int startArea{};
    std::unique_ptr<omk::UiWalk> walk{};
    int openScreen{};
    int conversations{};
    int lastArea{};
    long restartsSeen{};   // `Session::restarts()` last answered
    omk::LoadPanel loadPanelState{};   // rebuilt each time a screen opens
    int pendingLoadSlot{};   // `dword_4C09B4`
    bool quitRequested{};   // `dword_4E6C9C`, the pause screen's Oui
    bool exitProgram{};   // the start menu's Oui - WM_QUIT
    bool videophoneCall{};   // `dword_670BF0`
    double callCloseLeftMs{};   // oscillator 5 on the call, 0 = not running
    bool callWorldBack{};   // `sub_466B60`'s `byte_90E155 = 1` under the closing call
    bool dialogFromUi{};   // ACTOR_STATE 17: the conversation began under a world-hiding screen
    bool callHarness{};   // `--call` opened this one
    int callPending{};   // ...and the conversation it owes
    omk::UiCursor uiCursor{};   // Ui_DrawItemCursor's one pool (dword_6A4D20)
    omk::UiListState uiLists{};   // every list's `+2`, for as long as we run
    omk::UiModels uiModels{};
    bool screenFromScript{};
    std::uint32_t screenOpenBits{};
    int playerScreen{};
    bool actionSpent{};
    std::map<std::uint32_t, std::string> sneakRows{};
    std::vector<std::uint8_t> addrSeen{};
    std::set<std::uint32_t> sneakHidden{};
    int sneakTold{};   // one line per run, not one per frame
    int replySel{};   // which reply the player is on
    int actionTold{};   // one line for a press that reaches nothing
    std::uint32_t prevBits{};
    omk::SpecialMoves specialMoves{};
    omk::CityMaps cityMaps{};
    omk::ShootWeaponTable shootWeapons{};
    std::map<std::pair<int, int>, int> sceneVoices{};
    // THE VEHICLES' ENGINE SOUND (`sub_456B40`): vehicle slot -> its looping
    // voice, and `SOUNDS\sliderm01.wav` read once (drift audit A9)
    struct VehSounds {
        std::map<int, int> voices{};
        std::shared_ptr<const omk::DeviceSound> sound{};
        bool soundRead = false;
        int  soundStarts = 0, soundStops = 0;
    } vehSounds{};
    int takeCandidate{};   // `dword_53AF6C`, MDACTION's pick
    bool takeWasLow{};
    int heldInHand{};   // the object drawn on the left hand: from MDGETOBJ to the release
    int lineScroll{};
    int lineOverflow{};
    bool menuShown{};
    int lastDlgCam{};
    std::vector<omk::CameraRay> worldRays{};
    int lastRayCam{};
    int editingShown{};   // the announced editing's program
    bool haveLastDrawn{};   // a 3D camera has been drawn
    float lastEye[3] = {0, 0, 0};
    float lastAt[3] = {0, 0, 0};
    float lastFov{};
    std::vector<CtlSpriteInst> ctlSprites{};
    int ctlFxState{};
    float ctlFxFrame{};
    struct FoeFx {
        std::vector<CtlSpriteInst> sprites{};
        int fxState{};
        float fxFrame{};
        long spritesDrawn{};   // particle-frames placed on his bones
        long spritesPooled{};   // ...of which the sprite had a texture slot
    } foeFx{};
    omk::ParticleField ctlField{};
    omk::Geometry ctlGeo{};
    struct TakeCamera {
        bool live{};   // `C+12 == 1`: the mode-1 camera is live
        int phase{};   // 1 travelling in, 2 holding, 3 travelling back
        float clock{};   // frames since the request
        float fromFov{};
        float eye[3]{};
        float at[3]{};
        float fov{};
        float travel{};
    } takeCamera{};
    float takeCamFromEye[3] = {0, 0, 0};
    float takeCamFromAt[3] = {0, 0, 0};
    int fallCamMode{};
    bool fallBanded{};   // `+1304` set this fall (1, 3 or 4) - once per fall
    float lastRoll{};   // the camera ROLL, blended like the fov
    bool musicPaused{};   // the pause screen suspends the sound
    bool holdEditCam{};   // mode 13 with no active camera: hold
    unsigned long heldUnderRequests{};   // the Session's request count when the hold began
    bool editFromKnown{};   // ...and it was captured for the travel
    float editFromEye[3] = {0, 0, 0};
    float editFromAt[3] = {0, 0, 0};
    float editFromFov{};
    float editFromRoll{};
    bool rollTold{};
    int fxSpriteWas{};
    std::unique_ptr<omk::PlayerController> player{};
    omk::CtlFile playerCtl{};
    int moveWaitCtx{};
    int moveWaitGroup{};
    struct PlayerBody {
        std::vector<std::byte> ctlData{};
        std::vector<omk::Mesh> meshes{};
        omk::MeshNameIndex boneIdx{};   // rebuilt wherever `meshes` is
        int headMesh{-1};               // `headMeshOf(meshes)`, set with them
        std::vector<omk::Texture> tex{};
        omk::Geometry rest{};
        omk::Geometry posed{};
        std::vector<float> affine{};
        // the set's lights on the PLAYER (todo/drift-audit.md L1): `Actor_LoadModel`
        // lights his node as it does every character's - the GPU path's list, and
        // the grey his corners start from
        std::vector<float> lights{};
        int   lightCount{};
        bool  lightsBlack{};
        float lightBase{};
        float lightTint[3]{1.0f, 1.0f, 1.0f};   // the body light's (`o3de/lightprobe.h`)
        long posedFrame{};
        std::vector<omk::CollisionSphere> spheres{};   // the crowd push tests these
        float reach{};   // his model's +88
    } playerBody{};
    struct WalkSoup {
        omk::TriangleSoup soup{};
        std::vector<std::uint32_t> soupFlags{};
        omk::SplitSoupGrid grid{};
        std::vector<std::uint8_t> movingTri{};
        std::vector<std::uint8_t> fixedTri{};
        std::vector<std::uint32_t> movingIds{};   // the same set, ascending: the moving layer's build list
    } walkSoup{};
    const float * soupFlagsFor{};
    std::size_t soupFlagsSize{};
    bool mergedValid{};
    // THE MOVING COLLISION PLACED LAZILY (todo/cpu-vs-original.md tier C): a
    // moving mesh's walkable and steep triangles are re-placed only when a
    // query reaches them, once a frame - `lazyPlace`, through the grids'
    // `place` hook and `omk::ensurePlaced` for the linear readers. Keyed by
    // slot and mesh (`sl * 1000000 + mi`). `OMK_EAGER_SOUPS=1` places every
    // moving mesh every frame, as before.
    struct LazyMesh {
        int sl = 0, mi = 0;
        long gen = -1;           // the `worldGen` it was recorded in
        omk::PointPlace pp;
        bool pending = false;
        bool boxKnown = false;
        float restBox[2][6]{};   // walkable, steep: the rest triangles' min xyz, max xyz
    };
    struct LazyMeshes {
        std::map<int, LazyMesh> meshes{};
        int pending = 0;
        long placedFrame = 0, placedTotal = 0, recordedTotal = 0;   // counts for the log
    } lazy{};
    std::function<void()> lazyPlaceAllFn{};
    std::function<void(int)> lazyPlaceOneFn{};
    omk::TriangleSoup playerSteep{};
    omk::SplitSoupGrid playerSteepGrid{};
    std::vector<std::uint8_t> steepMovingTri{};
    std::vector<std::uint8_t> steepFixedTri{};
    std::vector<std::uint32_t> steepMovingIds{};
    std::string playerModel{};
    std::string playerCtlName{};
    bool playerReady{};
    bool adventure{};
    bool followCam{};
    bool playerProgram{};
    bool playerProgramWas{};   // ...and did last frame, for the hand-back
    bool mirrorLive{};   // the set's mirror is reflecting, said once
    bool mirrorSeen{};   // ...and has actually covered a pixel
    float playerHeadAt[3] = {0, 0, 0};   // the player's `Tete`, for subject kinds 0/1
    float playerHeadRise{};   // the DRAWN head over the pelvis (Y up), the swim log
    bool playerHeadKnown{};
    float playerHeadRel[3] = {0, 0, 0};   // `playerHeadAt` less where he stood when posed
    const char* cameraHeadFrom = "";   // where `feedCameraHead` took the head from, for the log
    std::vector<float> playerMeshAt{};   // ...and every mesh, for the shadow
    std::vector<float> playerMeshRot{};   // ...and each one's world rotation, nine
    bool playerMeshAtKnown{};
    int placementSeen{};   // Session::placementSeq() as last consumed
    // PER BODY, reset where the controller is built (`controlBinding`):
    // where his step was last accepted, for the hidden-set undo
    // (`Walk_ProbeGround`'s state-1 arm) - and forgotten on a placement too,
    // which is not a step - and the root's vertical sum across states
    float lastGood[3]{};
    bool haveLastGood{};
    float rootAccum{}, rootLast{};
    int rootState{-12345};
    long heldFrames{};   // frames under player.anim.hold
    std::string mediaText{};
    double mediaTextFrames{};
    omk::Surface mediaBmp{};
    float playerFeet{};
    bool playerFeetKnown{};
    float playerRootRef{};
    bool playerStandLatched{};   // the anchor came from H_STAND, not from whatever was up
    float playerRootXZ[2] = {0.0f, 0.0f};
    float lastRootDrop{};   // the crouch's root drop as drawn last frame (the held prop rides it)
    int playerCamId{};
    float handoverFacing{};
    bool handoverFacingKnown{};
    bool playerDrivenSeen{};
    int playerDrivenArea{};
    double frameSec{};
    double gameClock{};
    std::vector<HoldRun> holds{};
    long handoverFrame{};
    std::vector<float> & sndMove = game.audio.sndMove;
    std::vector<float> & sndConfirm = game.audio.sndConfirm;
    std::vector<float> & sndBack = game.audio.sndBack;
    std::vector<float> & optSndMove = game.audio.optSndMove;
    std::vector<float> & optSndConfirm = game.audio.optSndConfirm;
    std::vector<float> & optSndBack = game.audio.optSndBack;
    const omk::UiWalk * optWalkSeen{};   // the walk of 29 the options were opened for
    bool optEntered{};   // focused for this visit to panel 0x004CF420
    std::pair<int, int> pendingDisplay{};   // options row 2, served between frames
    // THE HOST, through the gateway (`platform/frontend.h`): the frontend
    // backend's own object behind the interface. The GPU windows are the
    // GPU glue's (`playgpu_<backend>.cpp`), not members.
    std::unique_ptr<omk::Frontend> frontHost = omk::makeHostFrontend();
    omk::Frontend & front = *frontHost;
    omk::Renderer * vkRen{};
    omk::Renderer * worldVk{};   // --world-vulkan, the offscreen harness
    omk::Renderer * glRen{};
    double glSwapMs{};   // the GL swap's share, for the phase line
    const omk::AdpcmTables & adpcmTables = game.audio.adpcmTables;
    omk::MusicPlayer & music = game.audio.music;
    int & playingTrack = game.audio.playingTrack;
    omk::VoiceOverLibrary & voiceLib = game.audio.voiceLib;
    omk::VoiceOverPlayer & voices = game.audio.voices;
    int & voiceOverShot = game.audio.voiceOverShot;
    struct SpeakerStage {
        std::vector<omk::Mesh> meshes{};
        omk::NodeTracks tracks{};
        std::shared_ptr<const std::vector<std::byte>> morph{};
        std::string model{};
        std::string voice{};
        float at[3] = {0, 0, 0};   // the camera solve, a GROUND point
        bool solved{};
        bool ready{};
        int conv{};
    } speaker{};
    float sceneFrameLast{};
    float lineIdleFrame{};
    int voiceShot{};   // the line's voice in the mixer, for the press that cuts it
    std::size_t voiceFed{};   // ...and how many of its samples it has been fed
    omk::LineSync lineSync{};
    std::uint32_t smoothMs{33};   // `dword_4E9700`, the smoothed frame time (T6)   // `Game_Frame`'s line sync (T2, `script/linesync.h`)
    bool dialogMode{};
    bool shootMode{};   // ops 80/81, `actor/shootmode.h`
    bool shootFrozenApplied{};   // the records' 0x8000, as last made equal to dword_4E9760
    long shootSuspendsSeen{}, shootResumesSeen{};   // ShootMode::suspends()/resumes() answered
    std::map<std::string, CharModel> charModels{};
    std::map<std::string, PropModel> propModels{};
    omk::Geometry propGeo{};   // the shown props, in world space
    std::vector<const PropModel *> propBatchOwner{};
    std::set<int> propsTold{};   // one line per prop, not per frame
    std::map<std::string, CharBank> charBanks{};
    std::vector<std::unique_ptr<Staged>> staged{};
    // HIDDEN, NOT GONE (todo/drift-audit.md M1): a body `character.hide`
    // detached while its actor still holds a slot (`Session::actorHeld`),
    // kept whole - position, facing, the pose it was left in - and moved
    // back into `staged` when a show re-links it, as `Actor_Detach` /
    // `Actor_Attach` keep the node. Dropped once the slot is freed.
    std::vector<std::unique_ptr<Staged>> parked{};
    // THE STREET'S PEDESTRIANS (`actor/pedestrians.h`, STREET_LIFE): their
    // posing jobs and staged bodies, the LOD caches, the library their clips
    // come from, and this frame's counts
    struct Crowd {
        std::vector<PedJob> jobs{};
        std::vector<std::unique_ptr<PedStaged>> staged{};
        std::map<std::pair<const omk::NodeTracks *, int>, omk::NodeTracks> lodTracks{};
        std::map<std::string, std::map<int, omk::Geometry>> lodRest{};   // model -> root mesh -> its subtree's rest
        // (sex or shoot group, clip slot, the model's SKELETON LAYOUT) -> its tracks:
        // a track's mesh index is the model's own, so two layouts cannot share one
        std::map<std::tuple<int, int, std::uint64_t>, omk::NodeTracks> tracks{};
        long cacheGen{};
        std::vector<std::byte> ani{};
        std::string aniName{};
        int drawn{};
        int live{};
        int inAction{};
        int idle{};
        int offView{};
        int lit{};
        long told{};
    } crowd{};
    // THE ROAD TRAFFIC on the same circuit (the sliders and the motos): the
    // staged vehicles and this frame's counts
    struct Traffic {
        std::vector<std::unique_ptr<VehStaged>> staged{};
        int drawn{};
        int live{};
        int stopped{};
        long told{};
    } traffic{};
    std::map<int, std::vector<omk::PedClip>> shootClips{};   // character type -> its group
    bool shootCameraLive{};
    // THE FREE LOOK's camera (preset 3, `PlayerController::lookTick`): on
    // while the arm runs, its lagged target, and the mouse flag 0x80
    // `image.show`'s bitmap (`dword_93072C`) while ACTOR_STATE 10 holds him,
    // and the edge word's two inputs (`dword_4E9720` the mask - the keys held
    // when it opened - and `dword_4E9724` the last poll)
    struct ImageShow {
        omk::Surface shown{};
        bool live{};
        std::uint32_t mask{}, last{};
    } image{};
    // THE HIGH-SCORE BOARD (op 142, screen 36). The page `UI_LoadScreen(36,
    // table, -1)` asked for until the screen can open (-1 none); then what
    // its open callback 0x004AD9A0 starts on a level page (1..3): the posted
    // message (oscillator 0, 5000 ms - the board's last line), the close
    // (oscillator 5, 10000 ms on the screen) and the player's release
    // (`sub_466B60`, run AFTER the state-9 hold, so he walks while it shows).
    struct HighScoreBoard {
        int openPage = -1;
        std::string message;
        double messageLeftMs = 0.0;   // oscillator 0; its close is the walk's oscillator 5
        bool released = false;
    } highScore{};
    long posSyncsSeen{};   // op 127's count, applied once each
    bool lookCam{};
    bool lookMouse{};
    bool lookAtKnown{};
    float lookAt[3] = {0, 0, 0};
    float shootPitch{};
    std::map<int, omk::ShootRecord> shootBrains{};
    // THE GUNMEN of a shoot phase, per actor id: their shots, clips and
    // animations, aims, the requests applied, who has said what, and the CRT
    // `rand()` state their brains and the bolts' jitter draw from
    struct Gunmen {
        std::map<int, long> shots{};
        std::set<int> told{};
        std::map<int, GunClip> clips{};
        std::map<int, GunAnim> anims{};
        std::map<int, int> curType{};   // the clip TYPE his last action started
        std::map<int, int> curSlot{};
        std::map<int, int> actSerialSeen{};   // the shoot requests applied, per actor (S13)
        std::set<int> standDown{};
        std::set<int> looped{};   // who has said his clip looped
        std::map<int, GunAim> aims{};
        std::set<int> aimTold{};   // who has said his first aim
        std::set<int> drawnTold{};   // whose gun has been said drawn
        std::set<int> barrelTold{};   // whose barrel direction has been said
        std::uint32_t randSeed{};
    } gun{};
    float playerDeathCountdown{};
    int hudHealth{};
    // THE ADVENTURE GAUGE's state, `Hud_ShowValue`'s globals: the mode
    // (`dword_4E90A8`: 0 for `Vie`, 2 for any other stat - the gauge plus the
    // stat card), the target (`dword_910688`), the value drawn (`dword_930748`,
    // eased toward the target) and the stamp (`dword_4E90A4`, 0 = off) on
    // `clock`, the `Sys_GetTimeMs` stand-in.
    struct HudFlash {
        int mode = 0, value = 0, shown = 0;
        double stamp = 0.0, clock = 0.0;
        bool armed = false;
    } hudFlash{};
    omk::ShootRecord playerShootRec{};
    omk::ShotLatch shotLatch{};
    omk::ShootAim shootAim{};
    // ASTAROTH's fight - the engine's globals around shoot type 13
    // (`actor/astaroth.h`, `todo/astaroth.md`)
    omk::AstarothFight astaroth{};
    int astarothSoulSlot[omk::kAstarothSouls] = {-1, -1, -1, -1, -1, -1};   // the worldSlots each soul is in
    std::map<int, omk::AstarothActor> astarothActors{};   // his actor-side fields, by actor id
    std::map<int, std::array<float, 4>> actorSlotTimer{};   // actor+148+4*slot, any actor that fires (`sub_44CDF0`)
    omk::AcquireOut astarothAcquire{};    // what `sub_420C70` left for `sub_420EB0`
    float astarothSlot1Wait = -1.0f;      // slot 1's sprite-row WAIT, rewritten each tick
    // `Hud_DrawBar(+92, 200, 1, 0)` this frame, -1 none: the BOSS bar, which
    // both Astaroth's brain (`sub_4800C0`) and Gandhar's (`sub_47F6F0`) draw
    int   astarothBar = -1;
    // GANDHAR (`todo/gandhar.md`): his actor-side fields by actor id, what his
    // cone test left for his turn, and his behaviour scripts
    std::map<int, omk::GandharActor> gandharActors{};
    omk::AcquireOut gandharAcquire{};
    omk::ShootAi::Tables shootTables{};
    bool shootTablesLoaded = false;
    long  astarothShakes = 0;             // `Camera_SetShake` calls (step 4 ports the shake)
    omk::ShootMover shootMover{};
    std::unique_ptr<omk::UiWalk> hudWalk{};
    omk::HudBar hudBar{};   // `Hud_DrawBar` mode 0, the health gauge
    omk::Radar radar{};   // 0x42F000, screen 34's minimap
    omk::Map2d shootMap{};   // MAP2D\<+106>.MPT - the noise's floors
    omk::ShootField shootField{};   // `sub_436260`'s distance field toward the player
    std::set<int> gunCellSeeded{};
    std::set<int> gunEntryPending{};   // his entry action, waiting for a floor         // gunmen whose +136/+140 came off the grid
    std::map<std::uint32_t, std::string> hudRows{};
    int hudAmmo{};
    std::string hudTold{};
    long shootMoveFrames{};
    float shootMoveDist{};
    float shootMoveFrom[3] = {0.0f, 0.0f, 0.0f};
    int mouseSensX{};
    int mouseSensY{};
    bool mouseInverted{};
    bool shootPitchDirty{};   // MDLUP / MDLDO moved the pitch this frame
    omk::ProjectilePool projectiles{};
    long shotsFired{};
    omk::SfxFile shootSfx{};
    std::unique_ptr<omk::ScxRuntime> shootRt{};
    std::map<std::pair<const std::byte *, std::size_t>, SfxSample> sfxCache{};
    std::string sfxSceneWas{};
    std::map<std::string, GunFacts> gunFacts{};
    std::string shotGunStem{};   // the stem the player's bolts are cloned from
    long stagedEver{};   // for the summary line
    std::vector<int> stagedIds{};
    struct TexturePool {
        std::uint64_t composition{};
        std::uint64_t builtFor{};
        std::uint64_t told{};
        bool hasSprites{};
        bool hasPlayer{};
    } texPool{};
    std::size_t propEventsSeen{};
    std::map<int, std::array<float, 3>> propsDrawnAt{};   // by prop id, for the moved line   // the Session's prop events this frontend has answered
    // the pool was handed over GREYED (ops 150/151): a change of bank is a
    // re-hand, as `sub_42FE80` / `sub_42FC10` re-grey or re-upload every slot
    bool poolGrey{};
    // source palette -> {the source, held so its address cannot be reused
    // while the key names it; its grey}
    std::map<const std::uint8_t*, std::pair<omk::PixelBuffer, omk::PixelBuffer>> greyPalettes{};
    std::size_t playerTexBase{};
    struct Sprites {
        std::size_t texBase{};
        std::unordered_map<int, int> slot{};   // absent = -1, as an unassigned slot was
        std::set<int> wanted{};
        std::set<int> pooled{};
        SpriteTable tab{};
        omk::SpriteLookup lookup{};
        SpriteTable base{};
        int baseGlobal{};
        int baseFight{};
    } sprites{};
    bool poolOverflowTold{};
    bool stagedProbe{};
    bool obstructProbe{};
    float progYawSign{};
    FightRun fightRun{};
    bool fightCamTold{};   // one line when mode 14 takes the view
    std::vector<omk::Texture> pool{};
    std::size_t poolSize{};
    std::unique_ptr<omk::ScxRuntime> globalRt{};
    std::unique_ptr<omk::ScxRuntime> fightRt{};
    std::string spriteScx{};
    omk::SoftwareRenderer worldSw{};
    bool worldReady{};
    std::array<WorldSlot, 2> worldSlots{};
    std::string worldSet{};   // the ACTIVE slot's stem - the set under his feet
    // THE REMASTER's post-pass (`todo/remaster.md` step 2): the bloom's
    // strength (0 off) and the set the city grade was last chosen for
    float bloomStrength{};
    float waterStrength{};   // the water reflection's, 0 off
    float floorStrength{};   // the reflecting floors', 0 off (`o3de/floorreflect.h`)
    float reflectLevels = 1.0f;   // ...how many levels mirror a frame, 0..2 (`[Remaster] reflections`)
    float wetStrength{};     // the wet floors', 0 off (`o3de/wetfloor.h`)
    omk::Wetness wetness{};  // ...how wet they are now, after the shower
    std::unordered_map<const void*, float> bodyWet{};   // ...and each body, by its pose (`stepBodyWet`)
    bool wetTold{};          // ...whether the log last said they reflect
    float rain3dStrength{};  // the rain drawn in the scene, 0 off (`playremaster_rain.cpp`)
    std::vector<omk::WeatherDrop> rainDrops{};   // ...the weather's drops it takes, last stepped
    omk::Geometry rainGeo{};                     // ...and their strips, rebuilt each frame
    int rainTexSlot = -1;                        // ...their streak texture's pool slot, -1 none
    int snowTexSlot = -1;                        // ...and the snow's flake (`Images\snow.bmp`)
    int sandTexSlot = -1;                        // ...and the restored sand's field (`Images\sand.bmp`)
    std::vector<omk::GrainRect> sandGrains3d{};  // ...its grains, texel rectangles
    unsigned rainBits = 0;                       // ...the view's weather bits they were taken under
    struct RainSplash { float p[3]; double born; float size = 1.0f; float glow = 1.0f; };   // a drop's crown on the floor, born on the weather's clock
    // ...and each body's last place, its stride and the weather's clock then,
    // by its pose: the splashes at its feet and on its head (`remasterWetBodies`)
    struct BodyStride { float at[3]; float owed; int side; double clock; };
    std::unordered_map<const void*, BodyStride> bodyStride{};
    std::uint32_t bodySplashSeed = 0x6C8E9CF5u;  // ...their own sequence: the floor's splashes land as without them
    std::vector<RainSplash> rainSplashes{};      // ...the live ones (`playremaster_rain.cpp`)
    double splashClock = -1.0;                   // ...the weather's clock they were last spawned at
    double splashOwed = 0.0;                     // ...the fraction of a splash carried to the next frame
    double dripClock = -1.0;                     // ...and the drips' last frame on the weather's clock, ms
    std::uint32_t splashSeed = 0x2F6B1D35u;      // ...where they land: a fixed sequence, the same on every backend
    int splashTexSlot = -1;                      // ...their crown texture's pool slot, -1 none
    float mistStrength{};    // the mist's (`View::mist`), 0 off
    float upscale{};         // the upscale's factor (`Renderer::setUpscale`), <= 1 off
    float bodyLight{};       // the body light's strength (`o3de/lightprobe.h`), 0 off
    float fxLight{};         // the emitters' light's (`o3de/emitterlight.h`), 0 off
    float relief{};          // the relief's (`View::relief`), 0 off
    float smoothBodies{};    // the rounded bodies' pull (`o3de/smoothbody.h`), 0 off
    std::set<std::string> smoothSkip;   // ...and the models left as they are (upper case)
    // whether `model` is rounded: the option on and the name not skipped
    bool smoothes(const std::string& model) const {
        if (smoothBodies <= 0.0f) return false;
        std::string up = model;
        for (auto& ch : up) ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
        return !smoothSkip.count(up);
    }
    float aoStrength{};      // the ambient occlusion's (`Renderer::setAmbientOcclusion`), 0 off
    bool waterLive{};        // the frame drew through the water pass (for the log)
    bool floorLive{};        // ...the floors' (for the log), at this plane
    float floorLiveY{};
    bool floor2Live{};       // ...and a SECOND level's (`[Remaster] reflections = 2`), at this one
    float floor2LiveY{};
    float level2Fade{};      // ...its reflection fading in while it is seen (the occlusion test)
    float level2Y = 1e30f;   // ...the level that fade is of
    static constexpr float kLevel2FadeSec = 0.25f;
    std::string gradeFor{"\x01"};
    int gradeOvercast = 0;   // ...the shower's step it was built at, 0..16 (`remasterGrade`)
    std::size_t worldTexBase[2] = {0, 0};   // each slot's first index in `worldTex`
    unsigned int worldGen{};   // bumped by every `rebuildWorld`: re-bake the tie
    // A DOOR'S TWIN (`o3de/settwin.h`): the faces each slot keeps of a mesh the
    // other slot also carries, settled once per `worldGen`. `start` is the run's
    // first corner in the set's buffer, `ordinal` the same corner in the mesh's
    // own `MovingMesh::rest`, which is laid out batch by batch in the same order.
    struct TwinRun {
        std::uint32_t start = 0, ordinal = 0, count = 0, batch = 0;
    };
    struct TwinMesh {
        int mesh = -1, other = -1;
        long lost = 0;              // faces of this mesh that are not submitted
        std::vector<TwinRun> keep;
    };
    std::vector<TwinMesh> setTwins[2];
    unsigned int setTwinsGen = ~0u;
    long twinFacesHeld = 0;         // this frame's, for the check
    void settleSetTwins();
    bool twinLive(int slot, const TwinMesh& t) const;
    Sky sky{};
    struct Shadows {
        omk::ShadowModel model{};
        omk::Geometry geo{};
        std::size_t texBase{};
        float footOffMax{};
        float spreadMax{};
        float spreadPlayer{};
        long blobsDrawn{};
        bool told{};
        bool lightTold{};
    } shadow{};
    float pedFootOffMax{};
    bool lightsTold{};
    bool fxLightsTold{};   // the first frame an emitter's light reached the player said
    float nightWas = 1.0f; // the night (`View::setDim`) last said
    float shimmerClock{};   // `dword_907310`, wrapped at 256
    std::vector<omk::DecorSoup> worldDecors{};   // every LOADED slot's soup, for decorUnder
    std::uint64_t worldGeoRev{};
    std::vector<omk::Texture> worldTex{};   // the shown slots' textures, slot 0 first
    long worldFrames{};
    omk::Geometry fxGeo{};
    float actorAt[3] = {0, 0, 0};
    bool actorKnown{};
    int lastCamera{};
    std::shared_ptr<SetLoad> setLoads[2]{};
    // a load replaced while its job still ran (`askSet`): kept until it
    // returns, so the session's end can wait for it - the job captures `this`
    std::vector<std::shared_ptr<SetLoad>> setLoadsLeft{};
    // THE PRELOAD RADIUS (`playpreload.cpp`): metres (0 off), the sets
    // prepared ahead by stem, and the doors of the resident pair
    float preloadMetres{};
    struct Preloaded {
        int area = -1;
        std::shared_ptr<SetLoad> L;
        bool sent = false;    // its textures handed to the renderer's preload
        double dist = 0;      // to its nearest door, game units
        std::function<void()> cancel;   // stops its job at its next stage
    };
    std::map<std::string, Preloaded> preloads{};
    // released while still preparing: cancelled, and counted in flight
    // until their jobs return
    std::vector<std::shared_ptr<SetLoad>> preloadDropped{};
    std::vector<omk::Session::DoorTarget> preloadDoors{};
    int preloadDoorsKey[2] = {-2, -2};
    long preloadTaken{}, preloadMade{};
    void phasePreload();
    // ---- THE REMASTER (`backends/remaster/`; empty stubs in
    // `backends/sdl/playremaster_off.cpp` for a build without OMK_REMASTER)
    void remasterSetup();                          // the packs, strengths, preset
    bool remasterBuilt() const;                    // false in `playremaster_off.cpp`
    // a [Remaster] strength moved by OMK's options menu, on the running game -
    // false when it waits for the next start (`todo/graphics-menu.md` 3a)
    bool remasterApplyLive(const std::string& key, float v);
    std::map<std::string, float> remasterAtStart_;   // each strength as the start set it
    // THE ORIGINAL/REMASTER SWITCH (`backends/remaster/playremaster_mode.cpp`,
    // `todo/graphics-menu.md` B): F10, the Mode row, `[Remaster] mode`
    bool remasterShown_ = true;                    // false: the original's look, the remaster armed
    bool remasterStartHidden_ = false;             // `mode = original`: switched at the first frame
    bool remasterKeyDown_ = false;                 // F10's last state, for its edge
    bool remasterNothingTold_ = false;
    std::map<std::string, float> remasterHeld_;    // the strengths while the original shows
    // the ROUNDED BODIES both ways (B2): each rounded model's rest NOT shown -
    // the flat one while the remaster shows, the rounded one while the
    // original does - swapped into `CharModel::rest` by the switch
    struct RemasterBody { omk::Geometry other; bool roundShown = true; bool active = false; };
    std::map<std::string, RemasterBody> remasterBodies_;
    RemasterBody remasterPlayerBody_;              // ...and the player's (`playerBody.rest`)
    void remasterSwapBodies(bool round);
    // the player's feet anchor (`playerFeet`) measured on the KEPT body in the
    // pose the latch read - what a start showing that body would latch
    void remasterFeetLatched(const std::vector<omk::MeshPose>& pose);
    float remasterOtherFeet_ = 0.0f;
    bool remasterShow(bool on, const char* why);   // false when nothing was armed
    void remasterModeFrame(bool f10);              // each frame, beside ESC's read: F10 down
    void remasterSlotsFrame(bool all);             // the sets' lights brought to the mode
    void remasterSlotLights(WorldSlot& ws, bool emitters);
    bool remasterSwitchable() const;
    float remasterArmed(const char* key) const;    // the start's strength, its resources loaded when > 0
    float remasterRunning(const char* key, float v) const;   // ...what the remaster mode runs with
    float* remasterStrength(const std::string& key);
    void preloadReleaseAll();                      // the preload radius turned to 0, live
    void omkOptionsSetup();                        // OMK's pages, beside screen 35
    void omkOptionsOpen();                         // ...given this run's values, at each open
    void omkOptionsServe();                        // ...their changes written and applied
    bool omkApplyLive(const std::string& key, double v);   // ...one of them, on the running game
    double omkReduce(const std::string& key, double v) const;   // ...as the start reduces it
    bool omkOpened_ = false;                       // omkOptionsOpen has run
    void remasterPrepareSet(SetLoad& L);           // a set's water, floors, probes, glows
    void remasterSmoothModel(CharModel& m, const std::string& name);
    void remasterSmoothPlayer();
    bool remasterTakePreload(int slot, const std::string& stem, int area, long frame);
    void remasterFinish();                         // the preload's jobs stopped and waited for
    void remasterSceneViewer();                    // the scene viewer's texture pack
    void remasterEmitterLights(WorldSlot& ws);     // a set's table: its lights, glows, flames
    void remasterGrade();                          // the city grade for the set underfoot
    void remasterAliasGrey(const omk::Texture& colour, const omk::Texture& grey);
    void remasterConfigure(omk::Renderer& r);      // a GPU renderer's remaster requests
    // the draw (`backends/remaster/playremaster_draw.cpp`)
    // the intro film's upscaled copy, opened into `mov` (smoothed) - false: none
    bool remasterOpenFilm(const std::string& real, omk::Movie& mov, const char* name);
    void remasterViewMist(omk::View& v);
    int  remasterViewLights(omk::View& v, bool& fxPix, float& kFxView);
    void remasterPointLights(omk::View& v, std::vector<std::pair<float, const omk::Light3do*>>& fx,
                             float kFxView, int kFxSlots, float& fxCut);
    void remasterLightsTold(const omk::View& v, const std::vector<std::pair<float, const omk::Light3do*>>& near,
                            const float at[3]);
    void remasterLitSet(std::vector<omk::Draw>& list);
    // THE RAIN IN 3D (`playremaster_rain.cpp`): the weather hands its stepped
    // drops over (-> true: the remaster draws them, the 2D streaks are not), and
    // the next frame's draw list takes them as strips in the scene
    bool remasterRainTake(const std::vector<omk::WeatherDrop>& drops, unsigned viewBits);
    void remasterRainDraws(std::vector<omk::Draw>& list, const omk::View& v);
    void remasterPoolRain(std::vector<omk::Texture>& slots);   // the streak's texture, after the sprites
    bool remasterWeatherRoofless() const;   // the 3D weather showing: a roof does not end the shower
    float remasterGust(double ms, float along, float shower) const;   // the gusts' lean (`playremaster_rain.cpp`)
    void remasterSnowCover(std::vector<omk::Draw>& list);      // the snow lying (`o3de/wetfloor.h` coverRuns)
    void remasterWetBodies(std::vector<omk::Draw>& list);      // the bodies wet in the rain (`o3de/wetfloor.h`)
    bool remasterDrawReflected(omk::Renderer& world, std::span<const omk::Draw> list, const omk::View& drawn);
    bool fxTiled{};   // this frame's emitters' lights go to `View::points` (`remasterViewLights`)
    // the pinned list: every prepared preload's textures
    void preloadPin();
    std::string slotAsked[2]{};
    int slotAskedArea[2] = {-1, -1};
    bool syncSets{};   // A/B only
    bool loadGate{};
    int loadDelayMs{};
    omk::HostInput host{};
    omk::Surface fb{};
    long frameNo{};
    long slidOutAt{-1};        // the frame MDSLIDOU last fired, for the release line
    // THE SLIDER'S CAMERA - `Camera_Request` as the slider's code makes it
    // (todo/slider-drift-audit.md A3/A7/B11): the mode it asked for (8 the
    // slider from behind, 9 the boarding side, 10 the arrival, 17 the fixed
    // eye on him; -1 when the slider owns no camera, the mode-0 follow), the
    // blend from the camera on screen at the request over `dword_930818`
    // frames (0 a cut), mode 10's address and eye side, mode 17's fixed eye
    // and its lagging target
    struct SliderCam {
        int mode{-1};
        float clock{}, dur{};
        float fromEye[3]{}, fromAt[3]{}, fromFov{75.0f};
        float addr[3]{}, eyeX10{-314.9606f};
        float eye17[3]{}, at17[3]{};
        // the LAG (`sub_415D10`/`sub_415E60`, drift audit B12): the slider's yaw
        // chased at dt/f46, the eye at dt/f44 and the target at dt/f42 - seeded
        // on the request's first frame
        bool  fresh{true};
        float lagYaw{}, lagEye[3]{}, lagAt[3]{};
        // MODE 8's obstruction pass (`sub_4141F0` arms `sub_417070`; drift
        // audit B12) - its state, reset by every request as `Camera_Request`'s
        // memset of +208.. does
        omk::CamObstruct obs{};
    } sliderCam{};
    // `sub_415D10`/`sub_415E60`'s chase for a slider camera: the subject at
    // `at` heading (fx, fz), the preset's two offsets, the lag f42/f44/f46
    void sliderCamLag(const float at[3], float fx, float fz, const float eyeOff[3],
                      const float atOff[3], int f42, int f44, int f46,
                      float eye[3], float tat[3]);
    // ...and mode 8's wall pass over it: `fresh` is flag 1, `steadyEye` /
    // `steadyAt` the camera with no lag (the second ray), `subjY` the
    // slider's y (+156)
    void sliderCamObstruct(float eye[3], float tat[3], const float steadyEye[3],
                           const float steadyAt[3], float subjY, bool fresh);
    int   sliderPrevState{0};      // the called slider's mode last frame
    long  boardAt{-1};             // `--board-after`: the frame to board on
    float boardAtSlider[3]{}, boardAtDoor[3]{};
    // the sneak's echo-bar message, oscillator 0's 5000 ms (`sub_42B820(0,
    // -1, text)` into `byte_6A4CA0`): string 42 when a slider call is refused
    std::string sneakEcho{};
    long sneakEchoMs{-1000000};
    std::uint32_t lastMs{};
    struct FpsMeter {
        std::uint32_t since{};
        std::uint32_t lastMs{};
        std::uint32_t worst{};
        int frames{};
    } fpsMeter{};
    float spriteAnchor[3] = {0.0f, 0.0f, 0.0f};
    bool spriteAnchorSet{};
    std::map<int, long> spriteLogged{};   // row -> the link tick already reported
    std::set<std::string> motionLogged{};   // mesh/pool pairs already reported
    static constexpr int kStagedSubs = 7;
    double stagedSub_[kStagedSubs] = {};   // `worldStagedBody`'s steps before the skin gate, this frame
    double paceNext{};
    double phaseHz{};
    struct PhaseTimes {
        double top{};
        double rb0{};
        double rb1{};
        std::vector<std::pair<const char *, double>> marks{};
        std::map<std::string, double> span{};
        std::map<std::string, double> section{};
        double sum[4] = {0, 0, 0, 0};
        long n{};
        long gpu{};   // frames presented from the GPU
        std::map<std::string, long> kept{};   // ...and why the others were not
    } phases{};

    // `world`'s object, chosen in `run` (the GPU renderer when there is one)
    omk::Renderer* world_ = nullptr;
    // THE SHOOTING RANGE'S HIGH SCORES - a `static` in `main` until S3e
    std::array<std::pair<std::string, int>, 20> highScores;

    // ---- THE FRAME'S OWN STATE that one phase sets and a later one reads
    // (todo/play-split.md): each is a local of the loop body, assigned at
    // its original point of declaration every turn.
    omk::DeviceState devices;                    // the devices, as the bindings read them
    std::uint32_t bits = 0, heldBits = 0, edgeBits = 0;   // the input word, held, edged
    bool uiPause = false;                   // the pause screen is the open one
    bool gpuFrame = false;                  // the present pass: the world went to the GPU
    bool overlayFrame = false, softGate = false;   // G6 step 2, the GLES window
    // `fb` NOT YET CLEARED this frame (`phaseWorld`'s deferred clear)
    bool fbStale = false;
    // (`omk::zeroBytes`: `dcbz` on a PowerPC, so the 614 KB are not read first)
    void clearFrame() { omk::zeroBytes(fb.px.data(), fb.px.size() * sizeof(std::uint16_t)); fbStale = false; }
    // ...and NOT cleared at all on a world-less frame the menu's CLOUD will
    // cover (step 3m): it writes every pixel of `fb` at any size,
    // so the clear was 614 KB written for nothing. `screensHuds` settles the
    // debt before the composer: if the cloud is not drawn after all, it
    // clears there and says so.
    bool fbCloudOwed = false;
    bool cloudCoversFrame() const;   // `playframe_world.cpp` - NOT inline: its local static
                                     // would be a weak object the classic linker leaves
                                     // without storage (PORTING A10)
    float ovFade[4] = {0, 0, 0, 0};         // the colour fade, for its shader
    int gpuVy = 0, gpuVh = 0;               // where the GPU world is placed
    const char* gpuKeep = nullptr;          // the first gate that kept the frame on the CPU path
    bool drawWorld = false;                 // the world is drawn this turn
    // a function-local `static const` in the body, read once: the same here,
    // read when the frame is built
    const bool verifyGpuPresent = omk::envFlag("OMK_VERIFY_GPU_PRESENT");

    // ---- THE PHASES, in the body's order (`playframe_<name>.cpp`). Each
    // returns what `step()` does: -1 to go on, -2 for the body's `break`, or
    // `main`'s exit code.
    int phaseInput();   // input, the pause screen, the game tick, scripted object motion, the sound effects
    int phaseControl();   // the hand-over and the controller's frame
    int phaseModes();   // the pause's sound, the voices, dialogue mode, shoot mode, the quit and the pending load
    int phaseWorld();   // whoever is on screen, and the world when no screen is over it
    void phaseWorldShown(int playerId);   // from `phaseWorld`
    void phaseWorldPlayerProgram(int progPid);   // from `phaseWorld`
    void phaseWorldPlayerBecome(int playerId, int& lastPlayerActor);   // from `phaseWorld`
    void phaseWorldSets(bool& changed, bool& skyChanged);   // from `phaseWorld`
    void phaseWorldDialogCamera(const omk::DialogCamera*& ca, const omk::DialogCamera*& cb,
                                const omk::DialogPlayer& dlg);   // from `phaseWorld`
    int phaseScreens();   // the screens' rows and the HUDs
    int phasePresent();   // the fps counter, the fades, the flicker catcher and the present

    int step();

    // ---- `phaseInput`'s PARTS (todo/play-split.md) and the state they share: each
    // was a local of the phase, and is assigned where it was declared, every turn
    std::vector<std::pair<std::string, std::array<float, 3>>> motionAt{};   // was a function-local static
    const std::array<float, 3> * motionAtFind(const std::string& name);
    int inputPump();   // the pump, a new display size, the mouse's first move
    void inputPause();   // ESC opens the pause screen; the last screen's close flushes the input
    void inputTick();   // the pause flag, one frame of the game, the Session under a screen
    void inputMotion();   // scripted object motion - the crates, the doors, the lifts
    void inputCollectMotions(std::vector<omk::Program::NodeMotion>& allMotions,
                             std::map<std::string, omk::SceneRunner::NodeScale>& allScales);   // from `inputMotion`
    void inputMeshAt();   // from `inputMotion`
    void inputSoupPatchInPlace(bool inPlace, std::vector<std::uint32_t> (&movedSoup)[2],
                               bool& newlyMoving, std::vector<std::uint32_t> (&movedSteep)[2],
                               bool& newlySteep);   // from `inputMotion`
    void inputVerifySplit(bool verifySplit, std::vector<std::uint32_t> (&movedSoup)[2]);   // from `inputMotion`
    void inputSounds();
    void vehicleSounds();          // sub_456B40's voices (playframe_input_parts.cpp)   // adventure mode's and the scene's own sound effects

    // ---- `phaseModes`'s PARTS (todo/play-split.md) and the state they share: each
    // was a local of the phase, and is assigned where it was declared, every turn

    void modesSound();   // the pause screen stops the sound; the voices
    void modesDialogue();   // dialogue mode
    int modesShoot();   // shoot mode
    void modesShootEnterLeave(omk::Input& in);   // from `modesShoot`
    void modesShootResume(omk::Input& in);   // from `modesShoot`
    void modesDialogEnterLeave();   // from `modesShoot`
    void modesScreenWalk(omk::Input& in, omk::OptionsMenu& optMenu, omk::Inventory& inv);   // from `modesShoot`
    void modesOptionsInput(std::uint32_t& uiBits, bool optActive, bool ate,
                           omk::OptionsMenu& optMenu);   // from `modesScreenWalk`
    void modesOptionsHost(bool optHost, bool optOnSneakTab, omk::OptionsMenu& optMenu,
                          omk::Input& in);   // from `modesScreenWalk`
    void modesSliderTravel();   // from `modesScreenWalk`
    void modesSaveRequest();   // from `modesScreenWalk`
    void modesScreenClosing(bool screenLeaving, omk::Inventory& inv);   // from `modesScreenWalk`
    int modesQuitLoad();   // the quit asked for, and the pending load, served between pumps

    // ---- `phaseScreens`'s PARTS (todo/play-split.md) and the state they share: each
    // was a local of the phase, and is assigned where it was declared, every turn
    std::map<std::uint32_t, std::pair<int, int>> itemMoved{};   // was a function-local static
    std::set<std::uint32_t> hintReport{};   // was a function-local static
    std::map<std::uint32_t, int> itemSection{};   // was a function-local static
    std::string hintTitleTold{};   // was a function-local static
    std::uint32_t hintPanel{};
    std::map<std::uint32_t, std::pair<int, int>> itemSource{};   // was a function-local static
    std::map<std::uint32_t, std::pair<int, int>> itemLitSource{};   // was a function-local static
    std::vector<std::uint32_t> denWheelItems{};   // was a function-local static
    std::vector<std::uint32_t> xachenItems{};   // was a function-local static
    bool liftHandled{};
    void screensSneakRows();   // the sneak's inventory rows
    void screensSneakVerbs(bool& useClosedSneak);   // from `screensSneakRows`
    void screensSneakMap(const omk::UiPanel*& pn, omk::ScreenComposer::CityMapView& cityView,
                         omk::ScreenComposer& comp);   // from `screensSneakRows`
    void screensSneakIdentity(const omk::UiPanel*& pn,
                              omk::ScreenComposer::PlayerSheet& playerSheet,
                              omk::ScreenComposer& comp);   // from `screensSneakRows`
    void screensSneakExamine(const omk::UiPanel*& pn, omk::ScreenComposer& comp);   // from `screensSneakRows`
    void screensSneakInventoryRows(int rowKind, omk::Inventory& inv, const omk::UiPanel*& pn,
                                   omk::ScreenComposer& comp);   // from `screensSneakRows`
    void screensShopRows();   // the shop's stock rows
    void screensMultiplanHints();   // Multiplan's rows and header, the Gandhar door's cursor, the hint shop
    void screensPuzzles();   // Den's locker, Gandhar's door, Xachen's cartridges
    void screensTerminals();   // the terminal family's display
    void screensLift();   // the lift's description box
    void screensPropertyTail();   // the tail of Actor_SetProperty
    void screensHuds();   // the fight HUD, the breath gauge, the shoot HUD
    void screensFightHud(omk::TextLayout& lay);   // from `screensHuds`
    void screensShootHud(omk::ScreenComposer& comp);   // from `screensHuds`
    void screensShootRadar(bool radarShown);   // from `screensHuds`
    void screensInterfaceLayer(omk::ScreenComposer& comp, omk::OptionsMenu& optMenu);   // from `screensHuds`
    void screensMediaInset();   // from `screensHuds`
    void screensSubtitle(omk::TextLayout& lay);   // from `screensHuds`
    // THE CITY WEATHER (`o3de/weather.h`, `playframe_world_weather.cpp`):
    // the cycle ticked and the drops projected before the present's gates,
    // which then know whether `fb` carries any; drawn first by `screensHuds`
    void weatherFrame(const omk::View& v);
    void weatherDraw();
    void screensFps();   // the fps counter
    void screensFades();   // the screen fades, over everything
    void screensFlicker();   // the flicker catcher

    // ---- `controlAdventure`'s PARTS (todo/play-split.md) and the state they share: each
    // was a local of the phase, and is assigned where it was declared, every turn
    bool playerTicked{};
    std::vector<std::string> kNoMoves{};   // was a function-local static
    bool actionFromMove{};
    bool actionTookObject{};
    void sliderRefused();  // `sub_452570` said no: string 42 on the echo bar
    void sliderHold();     // `sub_452570` said yes: Screen_Fade(1) and the hold
    void sliderForget(const char* why);   // a load or restart: no ride survives it
    bool tryBoardSlider();   // MDACTION's slider arm, after the take found nothing
    void sliderCamRequest(int mode, float frames);   // `Camera_Request` for the slider's modes
    bool beginSliderExit();   // `sub_468FA0`: out at the door, H_SLDOUT, ACTOR_STATE 8, mode 5
    void adventureAim();   // the follow camera's offsets, first-person aim
    void adventurePathField();   // the path field
    void adventureDeath();   // the death's countdown
    void adventureCrowdPush();
    const char* soupMeshName(std::uint32_t tri) const;   // a playerSoup triangle's set mesh name   // the crowd push
    void adventureScreenInput();   // a screen has the input, and the world still runs
    void adventureFightFoeMotion();   // from `adventureScreenInput`
    void adventureFightReport();   // from `adventureScreenInput`
    void adventureFightEnd(bool on, omk::Input& in);   // from `adventureScreenInput`
    void adventureShootMoverTick();   // from `adventureScreenInput`
    void adventureWaterEntry(std::uint32_t& standFl, bool& enteredWater, omk::Input& in);   // from `adventureScreenInput`
    void adventureFallAndLanding(bool wasAir, bool wasSlide, bool& enteredWater, bool inWaterState);   // from `adventureScreenInput`
    void adventureSeated();   // seated, not driving; where the slider is after he gets out
    void adventureRide();   // the ride
    void adventureWalls();   // stuck between walls; where the floor ends
    void adventureTake();   // the world take; where the action raise comes from
    void adventureShot();   // the shot
    void adventureSpecialMove(const std::string& mv, omk::Input& in, omk::Inventory& inv);   // from `adventureShot`
    bool adventureActionMove(const std::string& mv, omk::Inventory& inv);   // from `adventureSpecialMove`
    void adventureTakeObject();   // from `adventureActionMove`
    void adventureShotRequest(const omk::ShootWeaponRow*& row);   // from `adventureShot`
    void adventureAction();   // the action button, MDACTION, one activation per press

    // ---- `phaseControl`'s PARTS (todo/play-split.md) and the state they share: each
    // was a local of the phase, and is assigned where it was declared, every turn
    const omk::WorldCamera * hc{};
    void controlBinding();   // who is bound to him and who poses him, the shoot camera, the player made, the screens that stop the world, a teleport
    void controlFlight();   // the bolts' flight, before the actors tick
    void controlAdventure();   // the player ticked under a conversation, and adventure mode's controller frame

    // ---- `phaseWorld`'s PARTS (todo/play-split.md) and the state they share: each
    // was a local of the phase, and is assigned where it was declared, every turn
    const omk::WorldCamera * wc{};
    omk::View dlgView{};
    bool haveDlgCam{};
    const omk::SceneRunner::ActiveEditing * edit{};
    omk::CamSample editCam{};
    bool haveEdit{};
    const omk::UiItem * vpItem{};
    bool screenReadsPicture{};
    // THE CLOUD ON THE GPU (`ScreenComposer::setCloudTarget`, `under.frag`):
    // the window's backend can draw it (Vulkan's, unless OMK_NO_GPU_CLOUD),
    // this frame does, and the 640x480 picture it draws from
    bool cloudUnderGpu{};
    bool cloudGpuFrame{};
    omk::Surface cloudPic{};
    omk::View view{};
    // `sub_4815D0`'s state and this frame's projected drops: the rain's
    // streaks (head colour, tail grey) and the snow's flake squares
    omk::Weather weather{};
    double weatherClockMs = 0.0;                    // the frame's milliseconds, summed
    struct WeatherLine { int x0, y0, x1, y1; std::uint32_t c0, c1; };
    struct WeatherFlake { int x0, y0, x1, y1; };
    std::vector<WeatherLine> weatherLines{};
    std::vector<WeatherFlake> weatherFlakes{};
    omk::Surface weatherSnow{};
    bool weatherSnowTried = false;
    // THE RESTORED SAND (`[Restored] weather`): each drop one grain of
    // `Images\sand.bmp` around its centre, drawn `half` across a 4-texel grain
    struct WeatherGrain { int cx, cy; float half; int grain; };
    std::vector<WeatherGrain> weatherGrains{};
    omk::Surface weatherSand{};
    std::vector<omk::GrainRect> weatherSandGrains{};
    bool weatherSandTried = false;
    unsigned weatherBitsTold = 0xFFFFu;
    int weatherStateTold = -1;
    float weatherRain = 0.0f;   // the rain's drops / 200 this frame, 0 with none (the wet floors' input)
    float weatherSandLevel = 0.0f;   // ...the restored sand's, likewise (its dusty sky's input)
    bool weatherSnowing = false;   // the SNOW's cycle runs here this frame (the snow lying's input)
    int weatherAreaTold = -2;      // the active area the weather last saw (coming back into a shower)
    bool restoredWeather = false;  // [Restored] weather: Qalisar's and Lahoreh's showers, the sand
    bool drawPlayer{};
    bool drawArm{};
    bool sideCullSet{};
    bool sideCullBodies{};
    omk::Frustum sideFr{};
    double player0{};
    bool playerOffView{};
    bool playerGpu{};
    std::vector<omk::Draw> draws{};
    bool outsideView(const float c[3], float r, bool bodies);
    void worldCamera();   // the letterbox, the camera, the instrument override
    void worldCameraFollow();   // from `worldCamera`
    void worldCameraEditing();   // from `worldCamera`
    void worldCameraSlider();   // from `worldCamera`
    void worldCameraRide();   // from `worldCamera`
    void worldCameraTake();   // from `worldCamera`
    void worldCameraScript();   // from `worldCamera`
    void feedCameraHead();   // camera subject kinds 1/3: the player's head, to the Session
    void worldTexturePool();   // the texture pool
    void worldProps();   // the world's props
    void worldGuns();   // the gun in his hand, each gunman's gun
    void worldBolts();   // the bolts
    void worldStaged();   // every staged body, posed by whatever drives it
    void worldStagedBody(std::unique_ptr<Staged>& up, const omk::SceneRunner& sc, int playerId,
                         int& loneProgs, int& loneProg, int convSpeaker, int& stagedFar,
                         int& stagedOff, bool& firstBody);   // from `worldStaged`
    void worldStagedBodyReport(Staged& s, float& bodyYaw, const char*& src,
                                std::vector<omk::MeshPose>& pose, bool& firstBody,
                                const float (&off)[3]);   // from `worldStagedBody`
    void worldStagedFindProgram(const omk::SceneRunner*& run, int& prog,
                                const omk::SceneRunner& sc, bool activeOnly, int& playerId,
                                Staged& s, const omk::SceneRunner::Started::Drive*& nameDrive);   // from `worldStagedBody`
    bool worldStagedSkinGate(Staged& s, const char*& src, int& stagedFar, int& stagedOff);   // from `worldStagedBody`
    void worldStagedGround(Staged& s, float& ground);   // from `worldStagedBody`
    int nodeOwner(const std::string& name) const;   // the staged body a mesh name belongs to
    void worldStagedProgramDrive(const omk::SceneRunner*& run, int& prog,
                                 const omk::SceneRunner::Started*& stt,
                                 const omk::SceneRunner::Started::Drive*& drv,
                                 omk::SceneRunner::Started& driveView, int& sceneClip,
                                 int& scenePath, float& sceneFrame, Staged& s);   // from `worldStaged`
    void worldStagedDeathRoot(Staged& s, float (&rootMove)[3]);   // from `worldStaged`
    void worldStagedTurnPose(std::vector<omk::MeshPose>& pose, bool spins, bool& aboutPelvis,
                             float& bcs, float& bsn, const float (&off)[3], float (&pelvis)[3],
                             float scs, float ssn, Staged& s);   // from `worldStaged`
    void worldStagedSceneClipChange(const omk::SceneRunner*& run, int& sceneClip, Staged& s,
                                    int& scenePath, const omk::SceneRunner::Started*& stt,
                                    bool& byLone);   // from `worldStaged`
    void worldStagedLinePose(const omk::SceneRunner*& run, bool useLine, float lineT, Staged& s,
                             int& sceneClip, float& sceneFrame, std::vector<omk::MeshPose>& pose,
                             float& rootW, int& rootFrame, std::vector<float>& fv,
                             const char*& src, const omk::NodeTracks*& shootTracks,
                             const omk::AstarothActor*& astGrid, int& shootFrame, bool& poseHeld,
                             int playerId);   // from `worldStaged`
    void worldStagedHeadLook(bool lookAll, Staged& s, std::vector<omk::MeshPose>& pose,
                             bool useLine);   // from `worldStaged`
    void worldStagedRootMotion(float& rootW, Staged& s, int& rootFrame, float (&rootMove)[3]);   // from `worldStaged`
    void worldStagedShoot(Staged& s, const omk::NodeTracks*& shootTracks, int& shootFrame,
                          const omk::AstarothActor*& astGrid);   // its shoot-mode tick
    void worldStagedShootEnter(std::map<int, omk::ShootRecord>::iterator& it, Staged& s, int act);   // from `worldStagedShoot`
    void worldStagedShootEnterProps(bool astThinks, Staged& s, omk::ShootRecord& rec, int act);   // from `worldStagedShoot`
    void worldStagedShootBrain(omk::ShootRecord& rec, Staged& s, float astDt,
                                  omk::AstarothBand& astBand, int act);   // from `worldStagedShoot`
    void worldStagedShootAstaroth(Staged& s, omk::ShootRecord& rec, float& astDt,
                                  omk::AstarothBand& astBand);   // from `worldStagedShootBrain`
    void worldStagedShootGandhar(Staged& s, omk::ShootRecord& rec, float& astDt);   // from `worldStagedShootBrain`
    void worldStagedShootGeneric(omk::ShootRecord& rec, Staged& s, int& act);   // from `worldStagedShootBrain`
    void worldStagedShootMoveDecision(omk::ShootRecord& rec, int gunFloor, omk::ShootFrameIn& fin,
                                      float& yaw);   // from `worldStagedShoot`
    void worldStagedShootPatrol(omk::ShootRecord& rec, int patrolFloor, omk::ShootFrameIn& fin,
                                float& yaw, Staged& s);   // from `worldStagedShoot`
    void worldStagedShootEdgeWalk(omk::ShootRecord& rec, int gunFloor, omk::ShootFrameIn& fin);   // from `worldStagedShoot`
    void worldStagedShootStateClip(bool entryPending, omk::ShootRecord& rec, int before, Staged& s);   // from `worldStagedShoot`
    void worldStagedShootArrive(const omk::ShootStep& st, omk::ShootFrameIn& fin, int gunFloor,
                                Staged& s, omk::ShootRecord& rec);   // from `worldStagedShoot`
    void worldStagedShootClipAdvance(omk::ShootRecord& rec, Staged& s, int act,
                                     omk::ShootFrameIn& fin);   // from `worldStagedShoot`
    void worldStagedShootStrike(const omk::ShootStep& st, omk::ShootRecord& rec, Staged& s,
                                bool entryPending);   // from `worldStagedShoot`
    void worldStagedShootFireGate(bool fullArm, omk::ShootRecord& rec, const omk::ShootStep& st,
                                  bool entryPending, Staged& s, omk::ShootFrameIn& fin);   // from `worldStagedShoot`
    void worldStagedShootTurnClip(const omk::ShootStep& st, Staged& s, omk::ShootRecord& rec,
                                  omk::ShootFrameIn& fin);   // from `worldStagedShoot`
    void worldStagedShootClipTick(bool inShoot, bool shotDead, bool brainsOff,
                                  const std::map<int, omk::ShootRecord>::iterator& deadIt,
                                  omk::play::Staged& s, bool astarothRec,
                                  omk::AstarothBand& astBand, bool& clipHolds, float astDt,
                                  bool gandharRec);   // from `worldStagedShoot`
    void shootApplyAction(Staged& s, omk::ShootRecord& ar, int action, int a3,
                          const char* why);   // `Shoot_ActorAction` on a staged gunman
    void worldCrowd();   // the pedestrians, the vehicles, the player
    void worldCrowdPedestrians();   // from `worldCrowd`
    void worldCrowdPlayer();   // from `worldCrowd`
    void worldCrowdWalkers(const std::vector<omk::Pedestrian>& ws, bool circuitShown, float reach,
                           int& pedModelSwapped, int& pedRebound, bool noPedLod,
                           float (&viewFwd)[3], int pedLodMax);   // from `worldCrowd`
    void worldCrowdVehicles(const std::vector<omk::Vehicle>& vs, const omk::Sliders& pd,
                            bool circuitShown, float vreach, int& vehPastLod);   // from `worldCrowd`
    void worldCrowdPlayerRootDrop(const omk::NodeTracks*& pt, float& rootDrop);   // from `worldCrowd`
    void worldDrawLists();   // the particles, the visible set, the lights and the shadows into the frame's draw lists
    void worldDrawSetRuns(float clipReach, std::size_t& runsCulled, std::size_t& runsDrawn,
                          std::size_t& hiddenMeshes, std::size_t& hiddenDrawn,
                          std::size_t& movingCulled, std::size_t& movingDrawn);   // from `worldDrawLists`
    void worldDrawClassicShadows();   // from `worldDrawLists`
    // THE BODY LIGHT's tint for a body at `p` (`o3de/lightprobe.h`): the
    // shown slot's painted light, 1, 1, 1 when off or not drawn
    void bodyTint(const float p[3], float out[3]) const;
    // ...and under PER-PIXEL lighting (`--lighting`, `--enhance-all`), where
    // the shader lights a body from the scene's grey and ignores its corner
    // colours (`Draw::lit` 1): a tinted body is drawn in mode 2 instead - the
    // lights added to its corner colour - with that colour the grey x tint
    bool bodyTintPerPixel() const {
        return lighting > 0 && bodyLight > 0.0f && world_ && world_->drawsBodyTint();
    }
    void worldDrawParticles(bool scriptSprites, float fxNear, float fxFar, int& spriteSlotBase);   // from `worldDrawLists`
    void worldDrawSky();   // from `worldDrawLists`
    void worldDrawShadowLight(bool castShadows);   // from `worldDrawLists`
    void worldMirror();   // the mirror and the present pass

    // ---- WHAT `main` DEFINED AS LAMBDAS, methods now (`playstate.cpp`)
    float attGain(int a);
    float fxGain();
    float dialogueGain();
    int enh(int flag, int fromSettings, int top);
    void applyTextScale(int w, int h);
    void sfxLog(const char* what, std::size_t samples, int a, int b,
                            float gain = 1.0f, const float* peakOf = nullptr);
    void takeCamRequest(int phase);
    void playerCamRequest(const float eye[3], const float at[3], float fov, float frames);
    void fallCamRequest(int mode, bool flag, const char* who, int actorState);
    void rebuildFixedGrid();
    void rebuildMovingGrid();
    void rebuildSteepFixedGrid();
    void rebuildSteepMovingGrid();
    std::vector<float> loadSlot(int screen, int slot);
    void present(const omk::Surface& pic);
    void blip(const std::vector<float>& v);
    const std::vector<float>& uiSoundPcm(int id);   // an interface sound, decoded once
    int skeletonRootWalk(const CharModel& mo, const omk::NodeTracks& t);
    int skeletonRootOf(const CharModel& mo, const omk::NodeTracks& t);
    bool hasSeveralSkeletons(const CharModel& mo);
    int lodChainOf(const CharModel& mo);
    const omk::NodeTracks * lodTracksFor(const omk::NodeTracks* base, int level, int count);
    const omk::Geometry & lodRestFor(const std::string& model, const CharModel& mo, int rootMesh);
    int heaviestRootOf(const CharModel& mo);
    const omk::PedClip * shootClipFor(int group, int action);
    const omk::PedClip * shootClipOfType(int group, int type);
    const omk::PedClip * shootClipExact(int group, int type);
    const omk::PedClip * shootClipBySlot(int group, int slot);
    const omk::NodeTracks * pedTracksFor(int sex, const omk::PedClip& c, const std::vector<omk::Mesh>& meshes,
                                         const std::string& model = {});
    void shootNoise(long frame, int from, const float at[3], const char* what);
    // the freeze (`dword_4E9760`): its bit on every record made equal to the
    // Session's flag, and the WAKE the noise, a hit and a strike begin with
    void shootFreezeSync(long frame);
    void shootWake(long frame, const char* what);
    void astarothWorldHit(long frame, int mesh, int slot, const float at[3]);   // sub_47FCF0
    // his tick's world (`actor/astaroth.h` AstarothWorld) and his FIRE (`sub_44CDF0`)
    omk::AstarothWorld astarothWorld(Staged& s, omk::ShootRecord& rec, float dt);
    omk::GandharWorld gandharWorld(Staged& s, omk::ShootRecord& rec, float dt);
    bool hitBodyOf(const Staged& s, omk::HitBody& hb, bool drawnNow = true) const;   // a body as the bolt sweep sees it
    bool playerHitBody(omk::HitBody& hb) const;                // the player's, actor -1
    void strikePlayer(Staged& s, int dmg, const float dir[3], const char* who,
                      const char* kind);   // sub_423B10
    void astarothFire(Staged& s, omk::ShootRecord& rec, int slot, const float target[3]);
    bool recordFire(Staged& s, omk::ShootRecord& rec, int slot, const float target[3],
                    const char* who, float slot1Wait);
    void astarothStamp(omk::ShootRecord& rec);
    // `sub_4725B0`'s pose: four keys of the grid clip, two k/256 slerps
    std::vector<omk::MeshPose> astarothPoseNow(const CharModel* mo, const omk::NodeTracks& pt,
                                               const omk::AstarothActor& a);   // `sub_420B80` / `sub_421770`'s tail
    // `Shoot_InitWeapon` (0x00421FB0) for the PLAYER: the row of the object
    // in his hand and the magazine count - `Shoot_Enter` step 9 and
    // `shoot.player.resume` both call it; out: the object, its kind, the type
    void shootInitWeapon(int& obj, int& kind, int& type);
    // EVENT 48 and what its raisers do with a slot: the gun, `Shoot_InitWeapon`
    // and the HUD weapon (`Session::changeWeapon`). -> whether it changed.
    bool shootChangeWeapon(long frame, const char* why);
    std::vector<omk::MeshPose> playerPoseNow(omk::PlayerController* pl, bool aimLayer,
                                   const omk::NodeTracks& pt, float frame);
    std::vector<omk::MeshPose> gunmanPoseNow(int actor, int deathType, const CharModel* mo,
                                   const omk::NodeTracks& pt, float frame);
    const SfxSample & sfxPcm(std::span<const std::byte> wav);
    void loadLibrary(std::unique_ptr<omk::ScxRuntime>& rt, const char* path);
    void dropLibrary(std::unique_ptr<omk::ScxRuntime>& rt);
    void shotSound(long frame, int effectId, const float at[3],
                               const float* listener, const char* what);
    const GunFacts & gunFactsFor(const std::string& stem);
    CharModel * charModelFor(const std::string& name);
    PropModel * propModelFor(const std::string& stem);
    CharBank * charBankFor(const std::string& name);
    std::span<const std::byte> playerRecordSpan();
    bool beginMelee(int opponentId, int level);
    omk::NodeTracks idleTracksFor(const CharBank& b,
                                   const std::vector<omk::Mesh>& meshes);
    int loadSpritesInto(SpriteTable& table, const std::string& scx);
    int loadSpritesFrom(SpriteTable& table, std::span<const std::byte> scxFile);
    int loadSprites(const std::string& scx);
    void refreshSprites();
    void prepareSet(SetLoad& L);
    bool askSet(int slot, const std::string& stem, int area, long frame);
    bool integrateSet(SetLoad& L, long frame);
    void rebuildWorld();
    long uiClockMs();
    void applyPlayerDamage(long n, int owner, const char* what, int dmgIn,
                                       int shield, const omk::HitOut& ho);
    double phaseNow();
    void mark(const char* name);

// ...and every SECTION between two marks, summed over the 60-frame window
// and printed with the spans: the per-frame breakdown prints only past
// `OMK_MARKS_MS` and only when paced, so a console frame of 65 ms - over
// budget and under 150 - left ~50 ms of it attributed to nothing
// (measured on the Bowie sequence).
    template <class F>
    void spanned(const char* name, F&& fn) {
        OMK_ZONE(name);                     // and the profiler's (a literal: kept by pointer)
        const double a = phaseNow();
        fn();
        phases.span[name] += phaseNow() - a;
    }
    // THE CROWD'S MEMORY, as the original keeps it: a vehicle's
    // composed sub-object once per (model, sub-object), and a slot's posed
    // buffers given back when its body has not been drawn for a while
    // (`playframe_world_crowd.cpp`, the end of `worldCrowd`).
    std::map<std::pair<std::string, int>, omk::Geometry> vehAtRest;
    const omk::Geometry& vehAtRestFor(const std::string& model, const CharModel& mo, int rootMesh);
    void releaseIdleCrowd();
    void releaseIdleStaged();
    void lazyPlace(int key);       // the moving collision, placed on demand
    void lazyPlaceAll();
    void lazyRegister();           // the soups' `ensurePlaced` registration
    void lazyForget();             // a world rebuild: nothing is pending
    std::size_t slotSoupOffset(int sl, bool steep) const;
    // the profiler's view of the marks: each a SECTION from the mark before
    // it (`--profile`, todo/debug-tools.md), outside the call tree
    std::uint64_t profMarkT = 0;
#if OMK_PROFILE
    // one turn of the profiler's PAUSE: the window's events, the last frame
    // presented again, a short sleep. -> false when the window was closed
    bool profPauseTick();
    long profLastFrame = -1;    // the last frame stepped: what a pause shows
#endif

    // ---- THE SETUP'S SECTIONS (`playsetup_<name>.cpp`), in `run`'s order
    int setupOptions(int argc, char** argv);   // the command line, the game's state, the slider's door clips
    int setupBoot();   // the boot chain
    int setupSession();   // the live Session, the settings, the save, the sneak call
    bool setupSessionLoadSave(int& retVal);   // from `setupSession`
    void setupSessionPlacement();   // from `setupSession`
    int setupAdventure();   // adventure mode
    int setupDevices();   // the interface sounds, the renderer, the GLES window, the music, the world
    int setupBodies();   // the speaker, every staged body, melee, the effect sprites
    int setupPlay();   // the scripted objects start to play
    int setupSplash();   // the splash screen, the player's damage, the frame's instruments
    int finish();   // after the loop: the run's report, writing a save, the window closed

    // ---- THE INSTRUMENTS (`playharness.cpp`; empty stubs in
    // `playharness_off.cpp` for a build made with INSTRUMENTS=0)
    void harnessNewWorld();   // --newgame-world: a new game's world under the save's player
    void harnessStateWrites();   // --money, --rings, --give, --var: harness writes into the DB
    void harnessBankReject();   // --bank-reject: every bank refused (DEBUG)
    void harnessScriptForcing();   // --scene-load, --zone-disable, --zone-enable: the opcodes by hand
    void harnessRide();   // --ride: mounted where he stands, without MDSLIDIN
    void harnessSaveSlot();   // --save-slot: a save written when the run ends, without the panel
    void harnessHoldAndCall();   // --anim-hold and --call: a held player, the sneak call, fired once
    void harnessPad();   // --pad

    void harnessShootEnd();   // --shoot-end: shoot.end 1 at a frame
    void closeAllScreens(const char* who);   // UI_CloseAllScreens 0x0042A340
    void releaseFromScreen(int screen, const char* who);   // sub_466B60 / Actor_TickUiHeld 3: the held-object press
    void harnessBoard(float (&at)[3], float (&door)[3]);   // --board: put at the called slider's door and the action pressed
    void harnessFight();   // --fight: op 62's entry by hand
    void harnessShootHealth(std::int32_t& hp);
    void harnessAimAt(omk::RecordShot& rs);
    void harnessAstaroth();                      // --player-at, --astaroth-souls      // --aim-at: the player's shot aimed at a point   // --shoot-health: property 1 written at shoot entry
    void harnessFoeAt(const float *& foeAt);   // --fight-foe-at: the opponent started elsewhere
    void harnessFightHealth(omk::FightStats& ps);   // --fight-health: the player's Vie at Fight_Begin
    void harnessScxPlay();   // --scx-play: scene objects started by handle, once
    void harnessHideShow();  // --hide-show: opcodes 79 then 78 on one actor
    void harnessOmkSet();    // --omk-set: one of OMK's options changed at a frame
    void harnessGameRestart();   // --game-restart: op 152's write at a frame
    void harnessFlickerNote(std::size_t& runsDrawn, std::size_t& runsCulled, std::size_t& litBodies);   // the frame's facts for the flicker catcher
    void harnessSnaps();   // --snaps: the framebuffer every N frames from the hand-over

    // ---- VR (`todo/quest-port.md` §5): `backends/vr/playvr_<part>.cpp` in a
    // build with OMK_VR, empty stubs in `playvr_off.cpp` everywhere else
#if OMK_VR
    VrState vr{};
    void vrShootAim(std::uint32_t& word);   // shoot mode: the controller aims (backends/vr only)
#endif
    void vrSetup(int argc, char** argv);   // the --vr-* flags
    void vrAfterWorldCamera();             // the head on the authored camera
    bool vrWorldDraw(const omk::View& drawn, const omk::MirrorPlane& plane);   // the eyes; true = drawn
    bool vrHidesPlayer() const;            // first person: his body not drawn
    bool vrDrawsBehindScreen(bool screenKeepsWorld);   // a headset: the world behind a screen that hides it
    bool vrNoSideCull() const;             // a headset frame with a monitor: no side-plane cull
    void vrAdventureInput(std::uint32_t& word);   // first person: the walk turned to the look

    // ---- THE GPU WINDOW, per backend (`playgpu_vulkan.cpp`, `playgpu_gles.cpp`
    // or `playgpu_none.cpp` - each build links exactly one; todo/play-split.md S5)
    bool gpuWindowBuild() const;                 // this build has a GPU window at all
    void gpuOpenWindow();                         // the window and its renderer, if the device has one
    void gpuOpenWorldHarness();                   // --world-vulkan: the world offscreen
    bool gpuPresentSurface(const omk::Surface& pic);   // the composed frame; false = not taken
    void gpuPresentVerify(bool& presentedWorld);  // OMK_VERIFY_GPU_PRESENT on the GLES window
    void gpuPresentOverlay(bool& presentedWorld); // the GLES overlay pass
    void gpuPresentWorld(bool& presentedWorld);   // the world straight from the GPU
    bool gpuWorldOnWindow();                      // the world's renderer is the window's
    void gpuOverlayDecision(const char*& keep);   // a soft gate presented as an overlay
    void gpuResize(int nw, int nh, bool& ok);     // options row 2 on the GPU target
    // per-pixel lighting / mapped shadows asked again on the running GPU
    // renderer - GLES relinks its programs; false where nothing is relinked
    bool gpuRelinkLighting(bool perPixel, bool shadowMap);
    // MSAA, the supersample, the upscale and the AO asked again on the running
    // GPU renderer - its target remade; false where it cannot be
    bool gpuRetarget(int samples, int ss, float up, float ao);
    bool gpuDriverRow(std::vector<std::string>& drivers);   // options row 8's device name
    void gpuReportTimings();                      // the backend's own counters, every 60 frames
    void gpuSlowFrameReport();                    // ...and on a very slow frame
    void gpuFinishReport();                       // ...and at the end of the run
    void gpuVerifyWorldPicture();                 // --verify on the GPU world's picture

    int run(int argc, char** argv);
};

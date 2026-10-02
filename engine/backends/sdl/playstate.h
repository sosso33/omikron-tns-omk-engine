// SPDX-License-Identifier: GPL-3.0-or-later
// THE VIEWER'S STATE - every local `main` used to hold, as a member under the
// same name and in the same order (so they are destroyed in the order they
// were), gathered by `todo/play-split.md` S3 (2026-10-02). `run` is what was
// `main`'s body: each declaration there is now an ASSIGNMENT at the same
// point, so everything is set in the order it was. The list and the types
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

#include <optional>

struct PlayState {
    omk::PlayOptions opt{};
    const std::string & fr = opt.fr;
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
    std::string & configFile = opt.configFile;
    bool & densityFlag = opt.densityFlag;
    bool & clipFlag = opt.clipFlag;
    int & clipArg = opt.clipArg;
    int & skyFlag = opt.skyFlag;
    int & shadowFlag = opt.shadowFlag;
    int & threadFlag = opt.threadFlag;
    bool & noTieFlag = opt.noTieFlag;
    bool & cpuBodiesFlag = opt.cpuBodiesFlag;
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
    bool & dither = opt.dither;
    bool & mouseInvertX = opt.mouseInvertX;
    bool & mouseInvertY = opt.mouseInvertY;
    float & shootEyeLift = opt.shootEyeLift;
    bool & shootEyeSet = opt.shootEyeSet;
    bool & enhanceAll = opt.enhanceAll;
    bool & drawFog = opt.drawFog;
    bool & lightCrowd = opt.lightCrowd;
    std::uint8_t (&fogRGB)[3] = opt.fogRGB;
    std::string & giveList = opt.giveList;
    int & moneyArg = opt.moneyArg;
    int & ringsArg = opt.ringsArg;
    std::string & varList = opt.varList;
    bool & newWorld = opt.newWorld;
    int & sceneChunk = opt.sceneChunk;
    std::vector<int> & zoneEnable = opt.zoneEnable;
    std::vector<int> & zoneDisable = opt.zoneDisable;
    std::vector<std::pair<int, int>> & sceneLoads = opt.sceneLoads;
    bool & noCrowd = opt.noCrowd;
    bool & noScriptSprites = opt.noScriptSprites;
    std::vector<int> & scxPlay = opt.scxPlay;
    bool & openSneak = opt.openSneak;
    bool & startShoot = opt.startShoot;
    int & shootHealth = opt.shootHealth;
    int & fightHealth = opt.fightHealth;
    long & shootEndAt = opt.shootEndAt;
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
    omk::UiWidgets w{};
    omk::FontTable fonts{};
    std::optional<omk::TextLayout> lay_;
    std::optional<omk::ScreenComposer> comp_;
    omk::OptionTree optTree{};
    std::optional<omk::OptionsMenu> optMenu_;
    omk::Surface view3dPic{};
    omk::MenuCloud cloud{};
    omk::ControlSchemes schemes{};
    std::optional<omk::Input> in_;
    omk::BootOptions bo{};
    omk::BootReport br{};
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
    std::vector<std::vector<std::uint16_t>> flickRing{};   // the last kFlickPre frames
    std::vector<std::string> flickNote{};   // ...and their context
    std::vector<long> flickLit{};   // the lit-count window
    std::string frameNote{};   // this frame's context
    int flickAfter{};   // frames still to write
    long flickEvent{};
    long flickQuietUntil{};
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
    std::vector<omk::ObjectRecord> objectRecords{};
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
    omk::LoadPanel loadPanelState{};   // rebuilt each time a screen opens
    int pendingLoadSlot{};   // `dword_4C09B4`
    bool quitRequested{};   // `dword_4E6C9C`, the pause screen's Oui
    bool exitProgram{};   // the start menu's Oui - WM_QUIT
    bool videophoneCall{};
    bool videophoneSpoke{};   // a line or a voice-over has played
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
    std::vector<CtlSpriteInst> foeSprites{};
    int foeFxState{};
    float foeFxFrame{};
    long foeSpritesDrawn{};   // particle-frames placed on his bones
    long foeSpritesPooled{};   // ...of which the sprite had a texture slot
    omk::ParticleField ctlField{};
    omk::Geometry ctlGeo{};
    bool takeCam{};   // `C+12 == 1`: the mode-1 camera is live
    int takeCamPhase{};   // 1 travelling in, 2 holding, 3 travelling back
    float takeCamClock{};   // frames since the request
    float takeCamFromEye[3] = {0, 0, 0};
    float takeCamFromAt[3] = {0, 0, 0};
    float takeCamFromFov{};
    float takeCamEye[3]{};
    float takeCamAt[3]{};
    float takeCamFov{};
    float takeCamTravel{};
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
    std::vector<std::byte> playerCtlData{};
    std::vector<omk::Mesh> playerMeshes{};
    omk::MeshNameIndex playerBoneIdx{};   // rebuilt wherever `playerMeshes` is
    std::vector<omk::Texture> playerTex{};
    omk::Geometry playerRest{};
    omk::Geometry playerPosed{};
    std::vector<float> playerAffine{};
    long playerPosedFrame{};
    std::vector<omk::CollisionSphere> playerSpheres{};   // the crowd push tests these
    float playerReach{};   // his model's +88
    omk::TriangleSoup playerSoup{};
    std::vector<std::uint32_t> playerSoupFlags{};
    const float * soupFlagsFor{};
    std::size_t soupFlagsSize{};
    omk::SplitSoupGrid playerGrid{};
    std::vector<std::uint8_t> playerMovingTri{};
    std::vector<std::uint8_t> playerFixedTri{};
    std::vector<std::uint32_t> playerMovingIds{};   // the same set, ascending: the moving layer's build list
    bool mergedValid{};
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
    std::vector<float> playerMeshAt{};   // ...and every mesh, for the shadow
    std::vector<float> playerMeshRot{};   // ...and each one's world rotation, nine
    bool playerMeshAtKnown{};
    int placementSeen{};   // Session::placementSeq() as last consumed
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
    SdlFrontend front{};
    SDL_Window * vkWin{};
    omk::Renderer * vkRen{};
    omk::Renderer * worldVk{};   // --world-vulkan, the offscreen harness
    SDL_Window * glWin{};
    omk::Renderer * glRen{};
    double glSwapMs{};   // SDL_GL_SwapWindow's share, for the phase line
    const omk::AdpcmTables & adpcmTables = game.audio.adpcmTables;
    omk::MusicPlayer & music = game.audio.music;
    int & playingTrack = game.audio.playingTrack;
    omk::VoiceOverLibrary & voiceLib = game.audio.voiceLib;
    omk::VoiceOverPlayer & voices = game.audio.voices;
    int & voiceOverShot = game.audio.voiceOverShot;
    std::vector<omk::Mesh> speakerMeshes{};
    omk::NodeTracks speakerTracks{};
    float sceneFrameLast{};
    float lineIdleFrame{};
    std::shared_ptr<const std::vector<std::byte>> speakerMorph{};
    std::string speakerModel{};
    std::string speakerVoice{};
    int voiceShot{};   // the line's voice in the mixer, for the press that cuts it
    float speakerAt[3] = {0, 0, 0};   // the camera solve, a GROUND point
    bool speakerSolved{};
    bool speakerReady{};
    int speakerConv{};
    bool dialogMode{};
    bool shootMode{};   // ops 80/81, `actor/shootmode.h`
    std::map<std::string, CharModel> charModels{};
    std::map<std::string, PropModel> propModels{};
    omk::Geometry propGeo{};   // the shown props, in world space
    std::vector<const PropModel *> propBatchOwner{};
    std::set<int> propsTold{};   // one line per prop, not per frame
    std::map<std::string, CharBank> charBanks{};
    std::vector<std::unique_ptr<Staged>> staged{};
    std::vector<PedJob> pedJobs{};
    std::vector<std::unique_ptr<PedStaged>> pedStaged{};
    std::map<std::pair<const omk::NodeTracks *, int>, omk::NodeTracks> pedLodTracks{};
    std::map<std::string, std::map<int, omk::Geometry>> pedLodRest{};   // model -> root mesh -> its subtree's rest
    std::vector<std::unique_ptr<VehStaged>> vehStaged{};
    int vehDrawn{};
    int vehLive{};
    int vehStopped{};
    long vehTold{};
    std::map<std::pair<int, int>, omk::NodeTracks> pedTracks{};   // (sex, clip slot) -> its tracks
    long pedCacheGen{};
    std::vector<std::byte> pedAni{};
    std::string pedAniName{};
    int pedDrawn{};
    int pedLive{};
    int pedInAction{};
    int pedIdle{};
    int pedOffView{};
    int pedLit{};
    long pedTold{};
    std::map<int, std::vector<omk::PedClip>> shootClips{};   // character type -> its group
    bool shootCameraLive{};
    float shootPitch{};
    std::map<int, omk::ShootRecord> shootBrains{};
    std::map<int, long> gunShots{};
    std::set<int> gunTold{};
    std::map<int, GunClip> gunClips{};
    std::map<int, GunAnim> gunAnims{};
    std::map<int, int> gunCurType{};   // the clip TYPE his last action started
    std::map<int, int> gunCurSlot{};
    float playerDeathCountdown{};
    std::set<int> gunStandDown{};
    std::set<int> gunLooped{};   // who has said his clip looped
    std::map<int, GunAim> gunAims{};
    std::set<int> gunAimTold{};   // who has said his first aim
    std::set<int> gunDrawnTold{};   // whose gun has been said drawn
    std::set<int> gunBarrelTold{};   // whose barrel direction has been said
    int hudHealth{};
    std::uint32_t gunRandSeed{};
    omk::ShootRecord playerShootRec{};
    omk::ShotLatch shotLatch{};
    omk::ShootAim shootAim{};
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
    std::uint64_t poolComposition{};
    std::uint64_t poolBuiltFor{};
    std::uint64_t poolTold{};
    bool poolHasSprites{};
    bool poolHasPlayer{};
    std::size_t playerTexBase{};
    std::size_t spriteTexBase{};
    std::unordered_map<int, int> spriteSlot{};   // absent = -1, as an unassigned slot was
    std::set<int> spriteWanted{};
    std::set<int> spritePooled{};
    bool poolOverflowTold{};
    bool stagedProbe{};
    bool obstructProbe{};
    float progYawSign{};
    FightRun fightRun{};
    bool fightCamTold{};   // one line when mode 14 takes the view
    std::vector<omk::Texture> pool{};
    std::size_t poolSize{};
    SpriteTable spriteTab{};
    omk::SpriteLookup spriteLookup{};
    SpriteTable spriteBase{};
    int spriteBaseGlobal{};
    int spriteBaseFight{};
    std::unique_ptr<omk::ScxRuntime> globalRt{};
    std::unique_ptr<omk::ScxRuntime> fightRt{};
    std::string spriteScx{};
    omk::SoftwareRenderer worldSw{};
    bool worldReady{};
    std::array<WorldSlot, 2> worldSlots{};
    std::string worldSet{};   // the ACTIVE slot's stem - the set under his feet
    std::size_t worldTexBase[2] = {0, 0};   // each slot's first index in `worldTex`
    unsigned int worldGen{};   // bumped by every `rebuildWorld`: re-bake the tie
    Sky sky{};
    omk::ShadowModel shadowModel{};
    omk::Geometry shadowGeo{};
    std::size_t shadowTexBase{};
    float shadowFootOffMax{};
    float pedFootOffMax{};
    float shadowSpreadMax{};
    float shadowSpreadPlayer{};
    long shadowBlobsDrawn{};
    bool shadowTold{};
    bool shadowLightTold{};
    bool lightsTold{};
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
    std::string slotAsked[2]{};
    int slotAskedArea[2] = {-1, -1};
    bool syncSets{};   // A/B only
    bool loadGate{};
    int loadDelayMs{};
    omk::HostInput host{};
    omk::Surface fb{};
    long n{};
    Uint32 lastMs{};
    Uint32 fpsSince{};
    Uint32 fpsLastMs{};
    Uint32 fpsWorst{};
    int fpsFrames{};
    float spriteAnchor[3] = {0.0f, 0.0f, 0.0f};
    bool spriteAnchorSet{};
    std::map<int, long> spriteLogged{};   // row -> the link tick already reported
    std::set<std::string> motionLogged{};   // mesh/pool pairs already reported
    double paceNext{};
    double phaseHz{};
    double phTop{};
    double phRb0{};
    double phRb1{};
    std::vector<std::pair<const char *, double>> phMarks{};
    std::map<std::string, double> phSpan{};
    std::map<std::string, double> phSection{};
    double phSum[4] = {0, 0, 0, 0};
    long phN{};
    long phGpu{};   // frames presented from the GPU
    std::map<std::string, long> phKept{};   // ...and why the others were not

    // `world`'s object, chosen in `run` (the GPU renderer when there is one)
    omk::Renderer* world_ = nullptr;
    // THE SHOOTING RANGE'S HIGH SCORES - a `static` in `main` until S3e
    std::array<std::pair<std::string, int>, 20> highScores;

    // ---- THE FRAME'S OWN STATE that one phase sets and a later one reads
    // (todo/play-split.md): each was a local of the loop body, and is now
    // assigned where it used to be declared, every turn.
    omk::DeviceState st;                    // the devices, as the bindings read them
    std::uint32_t bits = 0, heldBits = 0, edgeBits = 0;   // the input word, held, edged
    bool uiPause = false;                   // the pause screen is the open one
    bool gpuFrame = false;                  // the present pass: the world went to the GPU
    bool overlayFrame = false, softGate = false;   // G6 step 2, the GLES window
    float ovFade[4] = {0, 0, 0, 0};         // the colour fade, for its shader
    int gpuVy = 0, gpuVh = 0;               // where the GPU world is placed
    const char* gpuKeep = nullptr;          // the first gate that kept the frame on the CPU path
    bool drawWorld = false;                 // the world is drawn this turn
    // a function-local `static const` in the body, read once: the same here,
    // read when the frame is built
    const bool verifyGpuPresent = std::getenv("OMK_VERIFY_GPU_PRESENT") != nullptr;

    // ---- THE PHASES, in the body's order (`playframe_<name>.cpp`). Each
    // returns what `step()` does: -1 to go on, -2 for the body's `break`, or
    // `main`'s exit code.
    int phaseInput();   // input, the pause screen, the game tick, scripted object motion, the sound effects
    int phaseControl();   // the hand-over and the controller's frame
    int phaseModes();   // the pause's sound, the voices, dialogue mode, shoot mode, the quit and the pending load
    int phaseWorld();   // whoever is on screen, and the world when no screen is over it
    int phaseScreens();   // the screens' rows and the HUDs
    int phasePresent();   // the fps counter, the fades, the flicker catcher and the present

    int step();

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
    const omk::NodeTracks * pedTracksFor(int sex, const omk::PedClip& c, const std::vector<omk::Mesh>& meshes);
    void shootNoise(long frame, int from, const float at[3], const char* what);
    std::vector<omk::MeshPose> playerPoseNow(omk::PlayerController* pl, bool aimLayer,
                                   const omk::NodeTracks& pt, float frame);
    std::vector<omk::MeshPose> gunmanPoseNow(int actor, int deathType, const CharModel* mo,
                                   const omk::NodeTracks& pt, float frame);
    const SfxSample & sfxPcm(std::span<const std::byte> wav);
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
    int loadSpritesInto(SpriteTable& spriteTab, const std::string& scx);
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
// (2026-09-30, the Bowie sequence).
    template <class F>
    void spanned(const char* name, F&& fn) {
        const double a = phaseNow();
        fn();
        phSpan[name] += phaseNow() - a;
    }

    // ---- THE SETUP'S SECTIONS (`playsetup_<name>.cpp`), in `run`'s order
    int setupOptions(int argc, char** argv);   // the command line, the game's state, the slider's door clips
    int setupBoot();   // the boot chain
    int setupSession();   // the live Session, the settings, the save, the sneak call
    int setupAdventure();   // adventure mode
    int setupDevices();   // the interface sounds, the renderer, the GLES window, the music, the world
    int setupBodies();   // the speaker, every staged body, melee, the effect sprites
    int setupPlay();   // the scripted objects start to play
    int setupSplash();   // the splash screen, the player's damage, the frame's instruments
    int finish();   // after the loop: the run's report, writing a save, the window closed

    int run(int argc, char** argv);
};

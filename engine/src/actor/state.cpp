// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/state.h"

#include <cstring>

namespace omk {
namespace {

// Read straight out of `Actors_TickAll`'s switch (0x004681C0) and the three
// filters that follow it in the same loop:
//
//   * the effect records run only for the player and g_FightOpponentRec, and
//     only when the state is not 4, 5, 9, 10, 16 or 17;
//   * the marker object at slot [20] is drawn unless the state is 7 or in
//     11..14;
//   * `Actor_TickNpc` returns before the walk when the state is 7 or 8.
//
// Note what is NOT in the switch: state 7. It falls through `default:` and
// gets no tick from this loop at all - `Sliders_Tick` drives a riding actor,
// with the camera on mode 8 and the delta halved. An absent case is content.
const ActorStateInfo kStates[kActorStateCount] = {
 // `nullsub_6` has no address here on purpose: the listing collapses it to a
 // one-byte `retn` stub and never prints one. A wrong address is worse than
 // none, and 0x0045B6F0 - the call at the TOP of Actors_TickAll - is not it.
 { 0,"Inert",         "nullsub_6",                   0,          false,false,true ,false,true ,
   "no tick; Actor_LoadModel's initial value and where Shoot_Leave and an "
   "empty Actor_StartPendingScx land"},
 { 1,"Normal",        "Actor_TickNpc",               0x00466580,true ,true ,true ,true ,true ,
   "the ordinary actor: channel, separation push, walk, zone scan"},
 { 2,"Melee",         "Actor_TickPlayerAndOpponent", 0x00466710,true ,true ,true ,false,true ,
   "only the player and g_FightOpponentRec ever hold it; the opponent's "
   "Fight_TickAI runs inside this tick"},
 { 3,"Shoot",         "Actor_TickShoot",             0x00466840,true ,true ,true ,true ,true ,
   "the player also installs .CTL group 200 and camera mode 4"},
 { 4,"ScxDriven",     "Actor_TickScxDriven",         0x00466990,false,false,false,false,true ,
   "an SCX scene object owns the body (slot [43]); no channel, and the "
   "effect records are suppressed"},
 { 5,"ScxPending",    "Actor_StartPendingScx",       0x00466A60,false,false,false,false,true ,
   "the resume half of Morph_Play's suspend; the ONLY state 5 write in the "
   "binary is Morph_Play's"},
 { 6,"ChannelOnly6",  "Actor_TickChannelOnly",       0x00466B00,true ,false,true ,false,true ,
   "the channel and nothing else"},
 { 7,"SliderMount",   "",                            0,          false,false,true ,false,false,
   "NO case in Actors_TickAll: Sliders_Tick drives it, camera mode 8, dt "
   "halved, and Actor_TickNpc returns early on it. MDSLIDIN writes it on "
   "the mount and sub_457040 parks the prior state in a GLOBAL"},
 { 8,"SliderRide",    "Actor_TickChannelOnly",       0x00466B00,true ,false,true ,false,true ,
   "shares 6's tick, and it is the RIDING half: sub_468FA0 puts the actor "
   "on .CTL group 61 and sets [101] and [102] both to 8, and MDSLIDOU "
   "refuses to dismount from anything else"},
 { 9,"UiHeld",        "Actor_TickUiHeld",            0x00466CC0,false,false,false,true ,true ,
   "an interface screen holds the body; releasing installs group 45 when it "
   "lands in state 1"},
 {10,"ImageScreen",   "sub_466E70",                  0x00466E70,false,false,false,true ,true ,
   "a full-screen bitmap; leaves to [102]"},
 {11,"Ladder11",      "Actor_TickNpc",               0x00466580,true ,true ,true ,true ,false,
   "Actor_ApplyMotion writes it when the ground mesh is a ladder, with .CTL "
   "group 300 and camera mode 21"},
 {12,"Scripted12",    "Actor_TickNpc",               0x00466580,true ,true ,true ,true ,false,
   "written by sub_4A9580"},
 {13,"Scripted13",    "Actor_TickNpc",               0x00466580,true ,true ,true ,true ,false,
   "written by sub_4A8F30; free-look is allowed in 1 and 13 only"},
 {14,"Swim",           "Actor_TickNpc",               0x00466580,true ,true ,true ,true ,false,
   "the WATER state: the tab_special_move[] handlers RSTNAGE (0x0046C150, "
   "`nage`) and MDDIVEND write it, and RSTAVNT / MDSW2SD write 1 back"},
 {15,"Shoot15",       "Actor_TickShoot",             0x00466840,true ,true ,true ,true ,true ,
   "shares 3's tick; written by sub_423FC0"},
 {16,"Dialogue",      "Actor_TickDialogue",          0x00466950,true ,false,false,true ,true ,
   "channel on .CTL group 400, input cut, Dialog_TickUI driven from the "
   "phase global"},
 {17,"DialogueFromUi","Actor_TickUiHeld+Dialogue",   0x00466CC0,true ,false,false,true ,true ,
   "case 17 FALLS THROUGH to 16; leaving lands in 1 and closes the screens, "
   "not in [102]"},
};

// Every ACTOR_STATE write in the binary. Enumerated two ways, because one way
// is not enough: a scan of `Runtime.exe.asm` for stores to `[reg+194h]` finds
// most of them, and misses `Fight_Engage`'s - which writes through the
// `dword_910834[328*index]` alias and is the only writer of state 2 in the
// game. A byte-offset search that came back without a writer for a state in
// the dispatch was the tell.
const std::vector<ActorTransition> kTransitions = {
 {kAny, 0, "Actor_LoadModel",          0x0041A730, "the initial value"},
 {kAny, 1, "Actor_LoadBankList",       0x00419CB0, "a bank loaded and linked"},
 {kAny, 0, "Actor_LoadBankList",       0x00419CB0, "the old bank released first"},
 {kAny, 1, "Player_SetActor",          0x00419E10, "this actor becomes the player"},
 {kAny, 0, "Player_SetActor",          0x00419E10, "the old player is released"},
 {kAny, 1, "State_Apply",              0x0040DB00, "a save is restored"},
 {kAny, 7, "sub_40E630",               0x0040E630, "read but not named; sets [101] and [102] alike"},
 {kAny, 2, "Fight_Engage",             0x0041A3B0, "BOTH fighters, through the dword_910834 alias"},
 {   2, 1, "sub_445AC0",               0x00445AC0, "the fight ends: the winner"},
 {   2, 0, "sub_445AC0",               0x00445AC0, "the fight ends: the loser"},
 {kAny, 3, "Shoot_Enter",              0x004222D0, "shoot.begin"},
 {kAny, 3, "Shoot_ActorEnter",         0x00422C10, "an NPC enters shoot mode"},
 {kAny, 3, "Shoot_TickPlayer",         0x00427AC0, "re-asserted per frame"},
 {   3, 1, "Shoot_Leave",              0x00422730, "shoot.end"},
 {   3, 0, "Shoot_Leave",              0x00422730, "shoot.end, the other arm"},
 {kAny,15, "sub_423FC0",               0x00423FC0, "the shoot variant"},
 {kAny, 0, "sub_422FE0",               0x00422FE0, "a shoot actor is dropped"},
 {kAny, 0, "sub_424DE0",               0x00424DE0, "the generic shooter gives up"},
 {kAny, 4, "ScriptObject_StartOnActor",0x0041BA80, "an SCX object takes the body"},
 {   4, 5, "Morph_Play",               0x0041AFC0, "a spoken line parks the object"},
 {   5, 4, "Actor_StartPendingScx",    0x00466A60, "the parked object resumes"},
 {   5, 0, "Actor_StartPendingScx",    0x00466A60, "nothing was pending"},
 {   4, 1, "Actor_TickScxDriven",      0x00466990, "the player's program finished"},
 {   4, kParked,"Actor_TickScxDriven", 0x00466990, "an NPC's program finished"},
 {kAny, 9, "UI_LoadScreen",            0x00429BB0, "a screen opens over the body"},
 {kAny, 9, "UI_BindScreenSounds",      0x00429950, "the same, from the sound path"},
 {   9, 1, "Actor_TickUiHeld",         0x00466CC0, "the screen closed"},
 {   9, 8, "Actor_TickUiHeld",         0x00466CC0, "the screen closed and [102] was 8"},
 {   9, 1, "sub_466B60",               0x00466B60, "the same release, other caller"},
 {   9, 8, "sub_466B60",               0x00466B60, "the same release, other caller"},
 {kAny,10, "sub_468F20",               0x00468F20, "a full-screen image opens; [101] parks in [102]"},
 {  10, kParked,"sub_466E70",          0x00466E70, "the image closes"},
 {kAny,11, "Actor_ApplyMotion",        0x004672D0, "the ground mesh is a ladder"},
 {kAny,11, "sub_465390",               0x00465390, "the scripted variant"},
 {kAny,12, "sub_4A9580",               0x004A9580, ""},
 {kAny,13, "sub_4A8F30",               0x004A8F30, ""},
 {kAny,14, "MDDIVEND",                0x0046BEF0, "a tab_special_move[] handler: the dive ends"},
 {kAny, 1, "MDSW2SD",                 0x0046BF20, "swim -> standard"},
 {kAny, 1, "RSTAVNT",                 0x0046C120, "reset to the adventure state"},
 {kAny,14, "RSTNAGE",                 0x0046C150, "reset to the SWIM state - `nage`"},
 {kAny, 6, "MDACTION",                0x0046AEC0, "a tab_special_move[] handler"},
 {kAny, 7, "MDSLIDIN",                0x0046B7F0, "mount the slider, and open screen 7"},
 {   8, 1, "MDSLIDOU",                0x0046B890, "dismount - and it REFUSES unless the "
                                                  "state is 8: \"bad mode getting out of the slider !\""},
 {kAny, 7, "sub_457040",              0x00457040, "the slider system's own hold; the prior "
                                                  "state parks in a GLOBAL, not in [102]"},
 {   7, kAny, "sub_4570F0",           0x004570F0, "and is restored from it"},
 {kAny, 8, "sub_468FA0",               0x00468FA0, "the riding pose: .CTL group 61, and "
                                                  "[101] and [102] both set to 8"},
 {kAny, 1, "sub_452570",               0x00452570, ""},
 {kAny, 0, "sub_472C30",               0x00472C30, ""},
 {kAny,16, "Actor_EnterDialogueMode",  0x00468DE0, "[101] parks in [102]"},
 {   9,17, "Actor_EnterDialogueMode",  0x00468DE0, "a UI screen already held him"},
 {  16, kParked,"Actor_LeaveDialogueMode",0x00468E80,"the parked state comes back"},
 {  17, 1, "Actor_LeaveDialogueMode",  0x00468E80, "17 lands in 1 and closes the screens"},
 {kAny, kAny, "Actor_SetState",        0x00419C30, "the generic setter; also drops slot [43]"},
};

}  // namespace

const ActorStateInfo& actorStateInfo(ActorState s) {
    const int i = static_cast<int>(s);
    return kStates[(i >= 0 && i < kActorStateCount) ? i : 0];
}

const std::vector<ActorTransition>& actorTransitions() { return kTransitions; }

bool ActorRuntime::setState(ActorState to, const char* writer) {
    const int f = static_cast<int>(state_), t = static_cast<int>(to);
    bool allowed = false;
    for (const auto& e : kTransitions) {
        if (std::strcmp(e.writer, writer) != 0) continue;
        if (e.from != kAny && e.from != f) continue;
        if (e.to != kAny && e.to != kParked && e.to != t) continue;
        if (e.to == kParked && t != static_cast<int>(parked_)) continue;
        allowed = true;
        break;
    }
    if (!allowed) { ++refused_; return false; }
    log_.push_back({state_, to, writer});
    state_ = to;
    return true;
}

bool ActorRuntime::installGroup(std::int32_t groupId) {
    const int g = channel_.findGroupById(groupId);
    // A bank that does not carry the group simply does not answer. The three
    // combat files keep stale authoring ids in every group, so this misses far
    // more often than it hits, and the engine tolerates it the same way.
    if (g < 0) return false;
    return channel_.setBankGroup(g);
}

void ActorRuntime::loadModel() {
    state_ = ActorState::Inert;
    log_.push_back({ActorState::Inert, ActorState::Inert, "Actor_LoadModel"});
    // Actor_LoadBankList follows and puts him in 1 with the bank's own default
    // group - Cef_DefaultGroup, the one group whose flags carry bit 0.
    const int g = channel_.defaultGroup();
    if (g >= 0) channel_.setBankGroup(g);
    setState(ActorState::Normal, "Actor_LoadBankList");
}

void ActorRuntime::playerSetActor() {
    setState(ActorState::Normal, "Player_SetActor");
}

bool ActorRuntime::fightEngage() {
    // Fight_Engage writes 2 to the player AND the opponent, then Fight_Begin
    // caches the six role entries and Input_InstallScheme(3) swaps the
    // bindings to the combat scheme.
    return setState(ActorState::Melee, "Fight_Engage");
}

bool ActorRuntime::fightEnd(bool winner) {
    const bool ok = setState(winner ? ActorState::Normal : ActorState::Inert,
                             "sub_445AC0");
    if (ok && winner) installGroup(kGroupLocomotion);
    return ok;
}

bool ActorRuntime::shootEnter() {
    const bool ok = setState(ActorState::Shoot, "Shoot_Enter");
    if (ok) installGroup(kGroupShoot);   // Actor_TickShoot, camera mode 4
    return ok;
}

bool ActorRuntime::shootLeave(bool toNormal) {
    return setState(toNormal ? ActorState::Normal : ActorState::Inert,
                    "Shoot_Leave");
}

bool ActorRuntime::scxStart() {
    return setState(ActorState::ScxDriven, "ScriptObject_StartOnActor");
}

bool ActorRuntime::morphPlay() {
    // Only reachable out of 4, and Morph_Play is the binary's only write of 5.
    if (state_ != ActorState::ScxDriven) { ++refused_; return false; }
    return setState(ActorState::ScxPending, "Morph_Play");
}

bool ActorRuntime::startPendingScx(bool pending) {
    return setState(pending ? ActorState::ScxDriven : ActorState::Inert,
                    "Actor_StartPendingScx");
}

bool ActorRuntime::scxDrivenDone() {
    if (isPlayer_) return setState(ActorState::Normal, "Actor_TickScxDriven");
    return setState(parked_, "Actor_TickScxDriven");
}

bool ActorRuntime::uiLoadScreen() {
    return setState(ActorState::UiHeld, "UI_LoadScreen");
}

bool ActorRuntime::uiHeldRelease() {
    const auto to = (parked_ == ActorState::SliderRide) ? ActorState::SliderRide
                                                          : ActorState::Normal;
    const bool ok = setState(to, "Actor_TickUiHeld");
    // "if (a1[101] == 1) Cef_FindGroupById(a1[45], 45)" - group 45 is the get
    // up / recover set, and it is installed only on the arm that lands in 1.
    if (ok && to == ActorState::Normal) installGroup(kGroupGetUp);
    return ok;
}

bool ActorRuntime::imageScreenOpen() {
    parked_ = state_;
    return setState(ActorState::ImageScreen, "sub_468F20");
}

bool ActorRuntime::imageScreenClose() {
    return setState(parked_, "sub_466E70");
}

bool ActorRuntime::enterDialogue() {
    // The park is the whole point: state 9 becomes 17 and does NOT overwrite
    // [102], because the interface still owns what is parked there.
    channel_.setInputEnabled(false);
    installGroup(kGroupDialogue);
    if (state_ == ActorState::UiHeld)
        return setState(ActorState::DialogueFromUi, "Actor_EnterDialogueMode");
    parked_ = state_;
    const bool ok = setState(ActorState::Dialogue, "Actor_EnterDialogueMode");
    // A prior state 4 sets the channel's no-playback flag: clips must not
    // fight the morph for the body. This is the belt to `player.anim.hold`'s
    // braces, and it is why a seated scene pose survives a conversation.
    if (ok && parked_ == ActorState::ScxDriven) channel_.setNoPlayback(true);
    return ok;
}

bool ActorRuntime::leaveDialogue() {
    installGroup(kGroupLocomotion);
    channel_.setInputEnabled(true);
    channel_.setNoPlayback(false);
    if (state_ == ActorState::DialogueFromUi) {
        parked_ = ActorState::Normal;
        return setState(ActorState::Normal, "Actor_LeaveDialogueMode");
    }
    return setState(parked_, "Actor_LeaveDialogueMode");
}

bool ActorRuntime::ladderEnter() {
    const bool ok = setState(ActorState::Ladder11, "Actor_ApplyMotion");
    if (ok) installGroup(kGroupLadder);
    return ok;
}

bool ActorRuntime::falling() {
    // Walk_GroundResponse does not change ACTOR_STATE - it swaps the channel
    // to group 2 and asks for camera mode 18. Kept here because "falling" is
    // otherwise looked for among the states, and it is not one of them.
    return installGroup(kGroupFalling);
}

const char* ActorRuntime::tick(float dt, std::uint32_t input) {
    const auto& info = actorStateInfo(state_);
    if (info.channelTicks) channel_.tick(dt, input);
    return info.tick;
}

}  // namespace omk

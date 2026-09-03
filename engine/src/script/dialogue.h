// SPDX-License-Identifier: GPL-3.0-or-later
// The conversation runtime - the last stubbed subsystem in the decision path.
//
// `dialog.start` loads a conversation; this runs it. Node by node, evaluating
// each branch's CONDITION and executing the chosen branch's ACTION against the
// same GameState everything else reads, so a conversation becomes an execution
// rather than an event.
//
// One IAM\DIALOG chunk is one conversation:
//
//     +0   int16  speakerObjectId      scene-local
//     +2   int16  nodeCount
//     +4   int16  cameraCount
//     +6   int16  unread by the loader
//     +8   DialogNode nodes[]          64 bytes each
//
// The node's nine pointers split in two, and the split was **proven by
// tracing** rather than guessed (FILE_FORMATS):
//
//     ptr[0..3]   the condition script for branch k. `Game_HandleEvent` event
//                 55, fired while Dialog_TickUI builds the reply menu,
//                 EVALUATES it and takes the value.
//     ptr[4..7]   the action script for branch k. Event 59, fired when a reply
//                 is chosen, EXECUTES it in a throwaway context.
//     param[0..3] the node to go to if branch k is taken; -1 unused.
//
// So conditions gate and actions run, and a branch with no condition script is
// simply available.
#pragma once

#include "script/gamestate.h"
#include "script/interp.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct DialogNode {
    std::uint32_t ptr[9] = {};
    std::int16_t  param[4] = {};
    std::int16_t  id = 0;
    // The node's VOICE, ten bytes at +46 - a bare stem naming `MORPH/<name>.3DM`,
    // whose audio block is this line spoken. It is the line's CLOCK: a
    // conversation is timed by its audio, not by a fixed dwell (CLAUDE.md 5),
    // and conversation 272 - the one a new game opens with - is three nodes of
    // 30.7 + 36.2 + 32.3 seconds.
    //
    // Those were first written down as 61.5 / 72.5 / 64.6, twice as long,
    // because `tools/morph3dm.read` returns BYTES and they were divided by the
    // channel count instead of by two. The port had it right and the
    // measurement checking it was wrong - the direction that is easy to miss,
    // because the disagreement looks like a port bug.
    char          name[11] = {};
    // The node's four camera ids, and they are NOT in field order: the REPLY
    // pair comes first. `sub_4013B0` issues camera command 12 twice per phase -
    // the first with duration -1.0, which snaps, and the second with 160.0,
    // which travels over 160 frames (5.3 s at 30). So a line SNAPS to `lineCam`
    // and moves to `lineCam2`; the menu snaps to `replyCam` and moves to
    // `replyCam2`. -1 is "none".
    std::int16_t  replyCam = -1, replyCam2 = -1;   // +56, +58
    std::int16_t  lineCam  = -1, lineCam2  = -1;   // +60, +62
};

// A conversation's own cameras - the 44-byte records after the nodes, the same
// shape as a world camera's (`o3de/worldcam.h`) down to the two subject fields.
//
// **The subject means something different here.** A world camera's `-1` is an
// absolute point and anything else is an actor id; a dialogue camera's `+32`
// is `0xFFFF` for an absolute SET coordinate and otherwise names one of the
// two speakers, with an axis map for the one facing back. All five of
// conversation 272's are absolute.
struct DialogCamera {
    std::int16_t id = -1;
    float eye[3] = {0, 0, 0};
    float at[3]  = {0, 0, 0};
    float roll = 0.0f;          // angle[0], 4096ths of a turn -> degrees
    float fov  = 74.0f;         // angle[1], the HORIZONTAL fov
    std::uint16_t subject[2] = {0xFFFF, 0xFFFF};
    bool absolute() const { return subject[0] == 0xFFFF; }
};

struct Conversation {
    int id = 0;
    std::int16_t speaker = 0;
    int cameras = 0;
    std::vector<DialogNode> nodes;
    std::vector<DialogCamera> cams;
    bool valid = false;
};

Conversation parseConversation(int id, std::span<const std::byte> chunk);

enum class DialogEnd { Leaf, Cycle, Limit, OutOfRange };

struct DialogStep {
    int node = 0;
    std::int16_t nodeId = 0;
    std::vector<int> available;   // the branches whose condition passed
};

struct DialogRun {
    std::vector<DialogStep> path;
    DialogEnd end = DialogEnd::Leaf;
    int steps = 0;
    int conditionsRun = 0;
    int actionsRun = 0;
};

// Walk from node 0, taking the first available branch at each step.
//
// Which reply a person picks is player input and is not in the data; what this
// tests is that the conditions evaluate, the actions run, and the graph
// terminates. The visited guard is deliberate rather than incidental - a
// conversation is a graph and nothing in the format forbids a cycle, so
// "it terminates" has to be something the run can observe rather than assume.
DialogRun runConversation(const Conversation& c, std::span<const std::byte> chunk,
                          GameState& state, const OpcodeTable& table,
                          int limit = 200);

// --------------------------------------------------------- PLAYING one
//
// `runConversation` walks the graph as fast as it can, which is what a
// decision check wants and is not a conversation. This plays one - and the
// whole of what makes it a conversation is that **it never advances itself**,
// with **one shipped exception the engine names by asset** (`LineEnd` below).
//
// **The flow, and it is the one `tools/omkweb.html` already implements** (its
// own comment, from reading the game): *the game never shows the NPC line and
// the menu together: the player presses NEXT, and THAT click both reveals the
// menu and cuts to its camera. NEXT with nothing but an unnamed continue
// branch advances straight to the next line.* So:
//
//     Speaking   the node's voice plays. NEXT is available throughout -
//                a player may cut a line short, and the viewer's button is
//                live from the moment the line starts.
//     next()     stop the voice, evaluate the branch CONDITIONS (which is
//                `Game_HandleEvent` event 55, fired while `Dialog_TickUI`
//                builds the reply menu) -> Menu.
//     Menu       waiting. Nothing here chooses.
//     choose(k)  execute branch k's ACTION (event 59, fired when a reply is
//                chosen) and enter `param[k]`. -> Speaking, or Finished.
//
// **A timer here was wrong and was corrected.** The first version ended each
// line when its audio ran out and walked on by itself, which made the intro
// take about the right total time and was still not a conversation: *this is a
// dialog, you are not supposed to do anything until the user does something.*
// That is the same correction `ui.open` needed - a screen belongs to whoever
// is holding the keyboard, and `Session::answerUi` is the only thing that
// releases it. `choose` is that for a conversation.
//
// **And the correction has an exception, which is `LineEnd`.** Removing the
// timer went one step too far: it made EVERY line wait, and the engine has
// one that does not. The rule is per-ASSET and is transcribed below; the
// general statement stands for 1172 of the 1174 shipped lines.
//
// **What the duration is for.** The node's `+46` stem names `MORPH/<stem>.3DM`
// and that file's audio block is the line, so `lineSeconds` is the decoded
// sample count over 22050 - shipped data, not a chosen number. It times the
// VOICE and nothing else: it does not advance the conversation.
//
// **What this is NOT.** It draws nothing. The reply TEXT comes out of the
// node's string pool at `ptr[8]` (index 0 the spoken line, 1+k reply k, the
// walk `0x004011D0` performs) and is offered to a caller, but presenting it,
// and the dialogue cameras and the speaker's pose the web viewer also carries,
// are the frontend's and are not here.
enum class DialogPhase { Speaking, Menu, Finished };

// How the line on screen ENDS. `Dialog_TickUI` (0x0046A200) case 4 stamps the
// phase global with one of three values, chosen from the line's asset name:
//
//     dword_9103DC = 2;
//     if (!strcmp(v48, a125338))    dword_9103DC = 7;   // "125338"
//     if (!memcmp(v4,  a02e19a, 7)) dword_9103DC = 8;   // "02E19A" and its NUL
//
// - so the second test is exact too, the seventh byte being the terminator.
// The two are mutually exclusive, which is why the engine can let the later
// assignment win without ordering them.
//
// Case 2/7/8 is the whole of what those three states then do, and it is one
// condition:
//
//     if ((a2 & 0x10) != 0
//      || dword_9103DC == 7 && (a2 & 0xFFFFFFF3) != 0
//      || dword_9103DC == 8 && Morph_IsDone())
//     { Morph_Stop(); dword_9103DC = 3; }
//
// state 3 being "the line is over", which walks on to the reply menu. So:
//
//   Confirm (2)     the ordinary line, and 1172 of the 1174 shipped ones.
//                   Only CONFIRM cuts it; the voice running out leaves it on
//                   screen, which is the rule `tick` has always modelled.
//   AnyKey (7)      ANY input bit except 4 and 8 cuts it. Those two are up
//                   and down - `Dialog_TickUI(2, dword_90E0E0 | dword_4E9718
//                   & 0xC)` is why they reach the test at all - and they are
//                   the pair the reply menu moves on. ONE shipped line:
//                   conversation 272 node 0, "Kay'l / Intro", the line a new
//                   game opens with.
//   SelfEnding (8)  ends ITSELF the moment `Morph_IsDone`, with no press.
//                   ONE shipped line: conversation 186 node 0, "Telis
//                   Demon/Toit (Scene)" - a one-node conversation whose four
//                   `param` are all -1, so it opens, speaks and closes
//                   without the player touching anything. That is what a
//                   line inside a cutscene has to do, and it is why the port
//                   treating every line alike hung on it.
enum class LineEnd { Confirm = 2, AnyKey = 7, SelfEnding = 8 };

struct DialogReply {
    int         branch = 0;      // 0..3, the node's own index
    int         target = -1;     // param[branch]
    std::string text;            // "" = an unnamed CONTINUE branch
    bool        available = true;
};

class DialogPlayer {
public:
    DialogPlayer(GameState& state, const OpcodeTable& table)
        : state_(&state), table_(&table) {}

    // `morphDir` is where the `.3DM` voices live (`gamedata/MORPH`).
    // -> false when the conversation will not parse.
    bool open(const Conversation& c, std::span<const std::byte> chunk,
              const std::string& morphDir);

    DialogPhase phase() const { return phase_; }
    bool playing() const { return phase_ != DialogPhase::Finished; }
    void reset() {
        phase_ = DialogPhase::Finished; pcm_.clear(); replies_.clear();
        lineState_ = LineEnd::Confirm;
    }

    // One frame of the VOICE. `dt` is in seconds - the only clock here not in
    // thirtieths, because what it counts is audio.
    //
    // It changes phase for a `SelfEnding` line and for nothing else: case
    // 2/7/8's third clause is `dword_9103DC == 8 && Morph_IsDone()`, which
    // takes the line to state 3 with no input word at all, so the tick that
    // sees the voice run out does exactly what a press would have done -
    // `next()`, unchanged, including its rule that a lone unnamed continue
    // branch advances straight through.
    void tick(double dt);

    // ---- THE LINE'S END RULE (`LineEnd` above)
    //
    // The classifier, as a free function of the asset name so a caller can
    // ask about a name it has not opened - and so the two literals can be
    // tested against names the corpus does not contain, which one example of
    // each cannot do.
    static LineEnd lineEndFor(const std::string& asset);

    // The state of the line ON SCREEN, stamped when it starts.
    LineEnd lineState() const { return lineState_; }
    // What a frontend needs: state 7 is cut by any key but up and down, so a
    // viewer that only watches CONFIRM ignores presses the game accepts.
    bool anyKeyCuts() const { return lineState_ == LineEnd::AnyKey; }
    // ...and state 8 is not waiting for the viewer at all.
    bool selfEnding() const { return lineState_ == LineEnd::SelfEnding; }

    // The input bits case 2/7/8 tests, named rather than re-derived.
    static constexpr std::uint32_t kConfirmBit = 0x10u;        // a2 & 0x10
    static constexpr std::uint32_t kMenuBits   = 0x0Cu;        // 4 up, 8 down
    static constexpr std::uint32_t kAnyKeyMask = 0xFFFFFFF3u;  // ~kMenuBits

    // The whole of case 2/7/8's condition, for an input word: does THIS frame
    // cut the line? `cutBy(0)` is the no-input case, and is true only for a
    // `SelfEnding` line whose voice has run out - which is what `tick` uses.
    bool cutBy(std::uint32_t input) const;

    // The player pressed NEXT. Evaluates the conditions and opens the menu -
    // or, when the only thing available is an unnamed continue branch, takes
    // it, which is the viewer's rule and the game's.
    void next();
    // The player picked reply `branch`. Runs its action and enters its target.
    void choose(int branch);

    // ---- what a frontend needs to show
    int  node() const { return node_; }
    const std::string& voice() const { return voice_; }
    const std::string& lineText() const { return line_; }
    const std::vector<DialogReply>& replies() const { return replies_; }
    double lineSeconds() const { return lineLen_; }
    double elapsed() const { return lineAt_; }
    bool   lineOver() const { return lineAt_ >= lineLen_; }
    const std::vector<std::int16_t>& pcm() const { return pcm_; }
    int    channels() const { return channels_; }
    bool   lineChanged() const { return lineChanged_; }
    void   clearLineChanged() { lineChanged_ = false; }

    int  linesPlayed() const { return lines_; }
    int  voiced() const { return voiced_; }
    // The conversation itself, for a caller that needs its cameras or its
    // speaker id.
    const Conversation& conversation() const { return conv_; }

    // ---- THE CAMERAS
    //
    // The pair in force, and how far the move between them has run. The clock
    // belongs to the LINE, not to any animation: 160 frames from the moment
    // the line starts or the menu is revealed, then it holds. Driving it from a
    // body animation instead is what made a looping clip drag the camera back
    // to the start of its move every loop, which is written up in CLAUDE.md 5.
    static constexpr int kCameraMoveFrames = 160;
    // Whether the menu's own pair is in force - it is not when the node names
    // none, and the line's framing then holds.
    bool menuPair() const;
    const DialogCamera* cameraA() const;
    const DialogCamera* cameraB() const;
    float cameraProgress() const {
        return camFrames_ >= kCameraMoveFrames ? 1.0f
             : static_cast<float>(camFrames_) / kCameraMoveFrames;
    }

private:
    bool enter(int node);
    std::vector<std::string> pool(std::uint32_t at) const;

    GameState*         state_;
    const OpcodeTable* table_;
    Conversation       conv_;
    std::vector<std::byte> chunk_;
    std::string        morphDir_;
    std::vector<int>   visited_;
    DialogPhase phase_ = DialogPhase::Finished;
    LineEnd     lineState_ = LineEnd::Confirm;
    bool   lineChanged_ = false;
    int    node_ = 0, lines_ = 0, voiced_ = 0, channels_ = 1;
    double lineLen_ = 0.0, lineAt_ = 0.0;
    double camFrames_ = 0.0;      // frames since the phase began
    std::string voice_, line_;
    std::vector<DialogReply>  replies_;
    std::vector<std::int16_t> pcm_;
};

}  // namespace omk

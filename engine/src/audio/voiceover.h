// SPDX-License-Identifier: GPL-3.0-or-later
// THE VOICE-OVER PLAYER - VM opcode 92 `media.play`, handler 0x00404590.
//
// A cutscene's spoken lines are not in its scene data: a world script fires
// `media.play <OBJECTS id>` and the handler turns that object's `IAM\OBJECT`
// record into a file name.  This is that resolution and the decode behind it.
//
// ---------------------------------------------------------------- the handler
//
// Read from the assembly (`python3 tools/asmfn.py --op 92`), in order.  The
// operand is one int16 with the usual `0x4000` indirect bit (`[ctx+0x24]`, the
// parameter block).  Then:
//
//   1. `if (dword_6A05E0) return;`  - g_ScriptDryRun.  A dry-run pass does
//      NOTHING here, not even announce.  `ctx+0x28 |= 0x10` is set either way.
//   2. `Dbg_LogTagged(id, "OBJECTS")`.  The literal is `aObjects` at
//      **0x004C0844**, and the image contains exactly ONE `push offset
//      aObjects` (Runtime.exe.asm:5528) - so a golden trace line naming
//      `004c0844 "OBJECTS"` is `media.play` and nothing else.  See below.
//   3. `if (id == -1) return;`
//   4. `rec = ObjectRecord_Read(id)` (sub_409AE0) - the 1304-byte record.
//   5. THE SUBTITLE.  `sprintf(subtitle, "{C}%s", rec+280)` when the PLAYER's
//      `ACTOR_STATE` (`g_PlayerActorRec+0x194`, int slot 101) is 3 or 15,
//      otherwise a plain `strcpy` of the same field.  `rec+280` is the
//      record's description string (`script/objects.h`), and `{C}` is the
//      CENTRE alignment (docs/UI.md, docs/FILE_FORMATS.md 5b4) - so the line
//      is centred in shoot mode and left-aligned everywhere else.
//   6. `sprintf(name, "%s.ADP", rec+14)` - **the stem at +14**, which is the
//      one field this module needs.
//   7. If a media BITMAP is up (`dword_93072C` set and `dword_4E6C88` set),
//      `I2D_FreeBitmap` it, clear both, and put the player in `ACTOR_STATE` 1.
//   8. `if (rec[+2] == 16)` - kind 16, a DOCUMENT - build `IMAGES\<stem>.BMP`
//      (sub_40BB40 appends the constant ".BMP" at 0x004C0D24),
//      `I2D_LoadBitmap` it, and set the player's `ACTOR_STATE` to **10**
//      (`ImageScreen`, "a full-screen bitmap holds it").  This arm plays NO
//      audio.
//   9. Otherwise: `if (strlen(name) <= 4) skip` - an empty stem gives ".ADP"
//      and the handler refuses it.  Then, if the first four bytes of the name
//      are `ZVOT` or `ZVOP` (a raw dword compare, so CASE-SENSITIVE), the
//      first 13 bytes are overwritten with the constant at 0x004C0868 -
//      `4A 49 4E 47 / 4F 46 46 33 / 2E 41 44 50 / 00` = **"JINGOFF3.ADP"**.
//      Then `sub_41B200(name)`.
//  10. `Mem_Free(rec)`; `Subtitle_Show(subtitle)`.
//
// `sub_41B200` is the play, and it is **not the sound bank**:
//
//      if (byte_91030C) {              // the audio system is up
//          Morph_Stop();               // <- ONE media voice at a time
//          Morph_ResetTracks(0,0,0,0,0);
//          sub_42BC10(0); sub_42CC30(1);
//          Morph_SetAudioFormat(0x5640, 1, 30);     // 22080 Hz, MONO, 30 fps
//          sprintf(buf, "VOICEOFF\\%s", name);
//          Morph_Start(buf, 0);
//      }
//
// So a voice-over goes through the MORPH player - the same streamer a `.3DM`
// dialogue line uses - with no morph tracks, mono, at the ADPCM rate.  The
// leading `Morph_Stop` is why this module keeps ONE handle: a second
// `media.play` cuts the first.  The other caller of `sub_41B200` is
// `Game_HandleEvent` case 47 - reading a document - which shows the
// description with an `{I255000000}` colour prefix and plays `JINGOFF2.ADP`.
//
// ------------------------------------------------------- what the data says
//
// The `.ADP` stem is the record's `+14`, and the identity of a voice object is
// its `IAM\OBJECTS.TAG` name beginning `ZVO`.  Over the shipped tree:
//
//   * **561** ZVO-tagged objects (`verify.py: cutscene music` asserts this);
//   * **10** of them name a file that ships in `gamedata/VOICEOFF`;
//   * **520** have a `ZVOT`/`ZVOP` stem and therefore play **JINGOFF3.ADP**,
//     which ships - so they are AUDIBLE, contra "the voices cannot be played
//     from this tree".  JINGOFF3 is a 2.08 s sting, not silence (peak 24889,
//     rms 3463 over its 45898 samples), and it is the placeholder the shipped
//     build substitutes for an unrecorded line;
//   * **31** are genuinely silent - no shipped file, no substitution;
//   * `gamedata/VOICEOFF` holds **17** files: 10 named by an object stem, JINGOFF2
//     and JINGOFF3 named by the executable, and **5 orphans** (JINGOFF1,
//     133205, ZVOPG001, ZVOU001, ZVOp201) that no path the engine can build
//     reaches - the `pluie.wav` shape (docs/ASSETS.md 3b).
//   * across the whole world-script corpus there are **2605** `media.play`
//     sites and **all 2605** name a ZVO-tagged object; 550 distinct.
//
// -------------------------------------------------- TIER (PORTING B1/B2): 4
//
// **Tier 4 for the resolution, over the golden traces.**  `media.play` is one
// of the 49 announcing handlers, and its `"OBJECTS"` literal is unique in the
// image, so the captures name the engine's own media ids: 102 announcements
// across the eight non-empty traces, 56 distinct, and **56 of 56 resolve to a
// ZVO-tagged object** through this module's rule.  `traces/impasse-walk.log`
// opens the Impasse cutscene with 142, 141, 404, 410 in that order - the four
// this port has to play.
//
// **Tier 3 for the decode**, differenced against `tools/adp.py`; the codec
// itself is tier 1 elsewhere (777/777 `.3DM` sample-identical).
//
// **Tier 6 for the two arms this module does NOT do**: the `{C}` subtitle and
// the kind-16 `IMAGES\<stem>.BMP` document screen are read above and reported
// by `resolve`, but nothing here draws them - there is no subtitle layer and
// no `ACTOR_STATE` 10 in the viewer.
//
// **No tier at all** for how loud it is or where it sits: `Morph_Start` hands
// the buffer to DirectSound and the attenuation law is the device's
// (PORTING B5, the same argument as the interface sounds).
#pragma once

#include "formats/adpcm.h"
#include "platform/datafs.h"
#include "script/objects.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace omk {

// What `media.play <id>` resolves to.  Every field is a decision the handler
// makes, in the order it makes them.
struct VoiceOver {
    int         object = -1;        // the OBJECTS id, as announced
    std::string objectName;         // its `IAM\OBJECTS.TAG` name ("" if none)
    std::string stem;               // the record's +14
    // `IMAGES\<stem>.BMP` when this is kind 16; then there is no audio at all.
    bool        image = false;
    // `<stem>.ADP`, or `JINGOFF3.ADP` after the ZVOT/ZVOP substitution.
    // Empty means the handler refused it (`strlen <= 4`) or this is `image`.
    std::string file;
    bool        substituted = false;    // the JINGOFF3 rule fired
    // `VOICEOFF/<file>`, and whether it resolves in the shipped tree.  A name
    // that does not resolve is not an error here: the engine builds the path
    // regardless and 31 ZVO objects have no file at all.
    std::string path;
    bool        shipped = false;

    bool playable() const { return shipped && !file.empty(); }
};

// `IAM\OBJECT` plus `IAM\OBJECTS.TAG`, held once.
class VoiceOverLibrary {
public:
    // -> false when `IAM/OBJECT` did not read.  The `.TAG` is OPTIONAL: it is
    // a debug-name file and the handler never opens it, so a tree without one
    // still resolves - only `isVoiceObject` degrades (see its comment).
    bool load(const DataFs& fs);

    bool loaded() const { return !objects_.empty(); }

    // The handler's rule, steps 4-9 above.
    VoiceOver resolve(const DataFs& fs, int objectId) const;

    // The `.ADP` behind a resolution, as MONO int16 at `kAdpcmRate`.  Empty
    // when there is nothing to play.  `Morph_SetAudioFormat(0x5640, 1, 30)` is
    // what makes it mono; the rate the engine asks for is 22080 and the codec
    // runs at 22050, which is the same 736-samples-a-frame arithmetic the
    // `.3DM` lines use (`formats/adpcm.h`).
    std::vector<std::int16_t> decode(const DataFs& fs, const AdpcmTables& t,
                                     const VoiceOver& v) const;
    std::vector<std::int16_t> decodeById(const DataFs& fs, const AdpcmTables& t,
                                         int objectId) const {
        return decode(fs, t, resolve(fs, objectId));
    }

    // Whether the object's `.TAG` name begins "ZVO".
    //
    // **The ENGINE does not test this** - `media.play` plays whatever `+14`
    // says for whatever id it is handed.  It exists only for the announcement
    // filter below, which cannot see the opcode; see `VoiceOverPlayer`.  With
    // no `.TAG` loaded it answers true for every id, which makes the filter
    // permissive rather than silently empty.
    bool isVoiceObject(int objectId) const;

    // The counts `verify.py: engine voice over` asserts against the Python
    // side.  All four are over the whole file, not a sample, and all four
    // partition the ZVO-tagged objects: 10 + 520 + 31 = 561.  Each answers -1
    // when no `.TAG` was loaded (see the note in the .cpp).
    int zvoObjects(const DataFs& fs)    const { return count(fs, 0); }  // 561
    int shippedVoices(const DataFs& fs) const { return count(fs, 1); }  // 10
    int jingleVoices(const DataFs& fs)  const { return count(fs, 2); }  // 520
    int silentVoices(const DataFs& fs)  const { return count(fs, 3); }  // 31
    static int voiceFiles(const DataFs& fs);     // 17, `gamedata/VOICEOFF/*.ADP`

    // Whether the object's `.TAG` name begins "ZVO", with NO fallback - false
    // when there is no tag file.  `isVoiceObject` is the permissive one.
    bool taggedVoice(int objectId) const;

    const std::vector<ObjectRecord>& objects() const { return objects_; }

private:
    int count(const DataFs& fs, int which) const;

    std::vector<ObjectRecord> objects_;
    std::vector<std::string>  tagNames_;   // by id; empty vector = no .TAG
};

// Polled with the Session's announcement vector once a frame; -> the media ids
// announced SINCE the last poll, in order.
//
// `Session::announced()` is append-only, so the cursor is the whole mechanism.
//
// **Which entries are `media.play` is the one thing this cannot read.**
// `omk::Announced` carries a `.TAG` domain and a value and no opcode, and TEN
// opcodes announce to `OBJECTS` (49, 50, 51, 52, 66, 67, 76, 77, 92, 143).
// Two behaviours, chosen at compile time:
//
//   * if the announcement type has an `.op` member, only op 92 is taken -
//     exact.  `todo/pending/E1.md` proposes that one-field addition to
//     `script/area.h`, which E1 was not allowed to make;
//   * otherwise `domain == "OBJECTS"` plus `isVoiceObject`.  That is an
//     APPROXIMATION, and it is measured rather than assumed: over the whole
//     world-script corpus the other nine opcodes have 1584 sites between them
//     and **exactly one** names a ZVO object (`inventory.remove_all 111`,
//     "ZVO P006 Rien Partic", AREA 65 pc 3564).  So the filter is wrong at
//     most once in the shipped game, and that site is named here so nobody has
//     to rediscover it.
class VoiceOverPlayer {
public:
    explicit VoiceOverPlayer(const VoiceOverLibrary& lib) : lib_(&lib) {}

    template <class Ann>
    std::vector<int> poll(const std::vector<Ann>& announced) {
        std::vector<int> out;
        if (seen_ > announced.size()) seen_ = 0;      // a Session was reset
        for (; seen_ < announced.size(); ++seen_) {
            const auto& a = announced[seen_];
            if constexpr (requires { a.op; }) {
                if (a.op != 92) continue;
            } else {
                if (a.domain != "OBJECTS") continue;
                if (!lib_->isVoiceObject(static_cast<int>(a.value))) continue;
            }
            out.push_back(static_cast<int>(a.value));
        }
        return out;
    }

    // Consume everything already announced without playing it - for a caller
    // that has just loaded an area and does not want its backlog.
    template <class Ann>
    void skip(const std::vector<Ann>& announced) { seen_ = announced.size(); }

    void reset() { seen_ = 0; }
    std::size_t cursor() const { return seen_; }

private:
    const VoiceOverLibrary* lib_;
    std::size_t seen_ = 0;
};

}  // namespace omk

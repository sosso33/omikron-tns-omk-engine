// SPDX-License-Identifier: GPL-3.0-or-later
// `SetMaterialsMemory(58, 0)` - the texture slot pool, and why it matters.
//
// `Tex3DT_BindMaterials` (0x004A6CF0) walks the `.3dt` beside each `.3DO` and
// for every material either claims a free slot or REUSES one already loaded:
//
//     for (i = 0; i < 58; i++) {
//         if (slot[i].subslots) continue;              // a packed page
//         if (!strcmp(slot[i].name, material + 20)) break;
//     }
//     if (i != 58) {                                   // CACHE HIT
//         fseek(stream, material->imageDataSize, SEEK_CUR);   // skip the pixels
//         *(uint16_t *)(material + 64) = i;            // point at what is there
//     } else { ... claim a free slot, read and upload ... }
//
// The key is the **texture file name alone**, compared and copied at nineteen
// characters. Nothing about the pixels, the model, or the scene enters it.
//
// And the pool is GLOBAL while two decor sets are resident at once:
// `Area_LoadSet` keeps a two-entry table with a state byte - 2 linked into the
// render list, 1 loaded but unlinked, 0 empty - and prefers an empty slot to
// freeing a state-1 one. **Hidden is not unloaded.** So an incoming set's
// materials cache-hit against the OUTGOING location's atlases, and where the
// two ship different pixels under the same name the incoming set silently
// draws the wrong image.
//
// That is not a corner case in this data: 182 texture names ship with
// different pixels in different models, and all twenty of ANEKBAH's are among
// them. It is the mechanism behind the wrong sign panel in Anekbah, and it
// means a replica that wants to look like the game has to REPRODUCE this, not
// correct it - the viewers are right and the game is the odd one out.
//
// Observing it in the shipped build is shut off: the `cached texture :%s` call
// goes to `Dbg_Printf`, which is `nullsub_1`, a one-byte `retn`. Only the
// string literal survives. So this is the way to watch it happen.
#pragma once

#include "formats/mesh3do.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

inline constexpr int kTextureSlots = 58;   // SetMaterialsMemory(58, 0)
inline constexpr std::size_t kCacheKeyChars = 19;

// What binding one model's materials did.
struct BindResult {
    struct Entry {
        std::string texture;     // the material's +20, the key
        int  slot = -1;          // what +64 was stamped with
        bool hit  = false;       // reused a resident slot: its pixels were SKIPPED
        std::string residentFrom;  // which model loaded the slot it hit
    };
    std::vector<Entry> materials;
    int hits = 0, loads = 0, exhausted = 0;
};

class TextureCache {
public:
    // Bind one model's materials, in material order - which is the order
    // `Tex3DT_BindMaterials` walks and therefore the order slots are handed
    // out in. That order is why "material-id order looks right" for the
    // coincident Anekbah faces: it is the SLOT order seen through the one case
    // where the two coincide.
    BindResult bind(const std::string& model, const Mesh3doHeader& h,
                    std::span<const std::byte> modelFile);

    // `Materials_ReleaseSlots` (0x00441840): a slot is freed only when no
    // other resident model points at it. The reference counting is sound -
    // there is no stale-slot bug, which is worth stating because the symptom
    // looks exactly like one.
    void release(const std::string& model);

    int used() const;
    const std::string& owner(int slot) const;

private:
    struct Slot {
        bool used = false;
        std::string name;            // truncated to 19 chars, as strncpy does
        std::string firstOwner;      // which model actually uploaded the pixels
        std::vector<std::string> refs;
    };
    Slot slots_[kTextureSlots];
};

}  // namespace omk

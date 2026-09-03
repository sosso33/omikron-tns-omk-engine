// SPDX-License-Identifier: GPL-3.0-or-later
// The WORLD the script opcodes reach that is NOT in the game DB.
//
// The interpreter runs on `GameState` alone - the 8192-byte block, the clock
// and the timer - and a dozen opcodes need more than that: an actor's
// 276-byte record when it is not the player's (the player's IS the DB, at
// +60), the object an actor is HOLDING (a 96-byte scene-object slot linked
// from the live actor record at `+41*4`, not a DB field), the prop table of
// the resident chunks, and the 3D scene that shows, hides, attaches and
// places an object. Those live in the engine's actor table, its 50 object
// slots and its two loaded blocks - which the Session owns - so the
// interpreter asks through this interface, ONE VIRTUAL PER ENGINE FUNCTION
// the handler would have called, named beside it.
//
// A NULL hook means STUB, AS BEFORE: the opcode is recorded and changes
// nothing, which is what every one of them did until 2026-09-02 and what
// `tools/sim` - the corpus sweep's reference - still does. The defaults below
// are the same answer for a hook that is installed but does not model a part
// (a probe overrides what it tests; a Session without a scene answers "no
// record" and the handler's own not-found path runs).
//
// What is deliberately NOT here, because the DB already holds it and the
// interpreter reads it there: the player's identity (`Actor_IdBySlot(Actor_
// Player())` is the DB player record's +272 - `player.become` writes it and a
// save restores it), the player's stat block (+60..+336) and the player's
// held-object field (+330). So `var.set.player_id`, and `var.set.actor_stat`
// / `actor.stat.set` on the player, need no hook at all.
#pragma once

#include <cstdint>

namespace omk {

// One 24-byte prop record, as the two opcodes that walk the table read it:
// `+0` the runtime object slot (`Scene_LoadProps` fills it; -1 on disk), `+2`
// the OBJECTS id, `+22` the index into the DB's 2-bit `PropState` array.
struct PropRef {
    int slot = -1;
    int id = -1;
    int stateIndex = -1;
};

class WorldHooks {
public:
    virtual ~WorldHooks() = default;

    // ---- actor records that are NOT the player's ----------------------
    // `Scene_FindObjectRecord(slot, id)` -> index -> `Actor_FindById` ->
    // `Actor_GetProperty` / `Actor_SetProperty` over the 276-byte record in
    // the resident chunk (`props.h` carries the field map and the clamps).
    // `actor` is a CHARACTERS id; the interpreter has already taken -1 and
    // the DB's own id to the player record, so these see other actors only.
    // false = the engine would have read garbage (no record; a pointer-slot
    // property): the port leaves the variable alone.
    virtual bool getActorProperty(int /*actor*/, int /*property*/, std::int32_t& /*out*/) { return false; }
    virtual bool setActorProperty(int /*actor*/, int /*property*/, std::int32_t /*value*/) { return false; }
    // The record's `+270` - the OBJECTS id it holds, -1 for none - which
    // `object.hold.actor` (67) compares and writes and 68/69 clear.
    virtual int  heldObjectField(int /*actor*/) { return -1; }
    virtual void setHeldObjectField(int /*actor*/, int /*objectId*/) {}

    // ---- the live actor and its object slot ----------------------------
    // `Actor_HeldObjectSlot(index)` (0x0041A350): the 96-byte object slot the
    // actor's live record links at `+41*4`, as an index 0..49, or -1 when the
    // actor is not active or holds nothing. -1 is the player.
    virtual int  heldObjectSlot(int /*actor*/) { return -1; }
    // `word_4E6CA0[slot]`: the OBJECTS id an object slot carries, which is
    // what `var.set.used_object` (75) writes into its variable.
    virtual int  objectIdInSlot(int /*slot*/) { return -1; }
    // `word_4E6CA0[slot] = -1`, the 68 handler's not-found arm.
    virtual void clearObjectSlot(int /*slot*/) {}
    // `Actor_ReleaseObject(index, remove)` (0x0041A140): unlink the held
    // object; `remove` false re-parents it to the world at its stored
    // placement (the prop is DROPPED where it was), true frees it.
    virtual void releaseObject(int /*actor*/, bool /*remove*/) {}
    // `Actor_HoldObject(index, slot)` (0x0041A0A0): parent the slot's node to
    // the actor's hand.
    virtual void holdObject(int /*actor*/, int /*slot*/) {}
    // `Object_ShowInScene(slot)` (0x0041CBE0) / `Object_HideFromScene(slot)`
    // (0x0041CC20): link the slot's node into, or out of, the scene.
    virtual void showObject(int /*slot*/) {}
    virtual void hideObject(int /*slot*/) {}

    // ---- the prop table of the resident chunks -------------------------
    // The walk the 67/68/76 handlers share: `Area_Block(area)`'s table at
    // +44 (count +74), then the SCENE over it (`Area_GetLoadedScene` ->
    // `Scene_Block`) at +12 (count +42), 24 bytes a record. 68 matches the
    // held SLOT against `+0`; 67 and 76 match the id against `+2`.
    virtual bool propBySlot(int /*slot*/, PropRef& /*out*/) { return false; }
    virtual bool propById(int /*id*/, PropRef& /*out*/) { return false; }

    // ---- `object.place_at` (98) ----------------------------------------
    // `sub_40AF00(id)` -> the object's slot in the id table; `Address_Find`
    // (`sub_40A2C0`) the ADDRESSES record; `sub_41CF50(slot, pos)` moves
    // the node. World positioning, no DB write: output-class.
    virtual void placeObjectAt(int /*objectId*/, int /*address*/) {}
};

}  // namespace omk

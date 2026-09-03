#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Disassemble the dialogue scripts in gamedata/IAM/DIALOG.

Opcodes are one byte; opcode 3 ends a script (see the dispatch loop at
0x004060B0). Each handler consumes its own operands, and the table at
0x004C0140 records how many bytes - mostly. Where it is wrong the disassembler
desynchronises and hits an invalid opcode, so decoding the whole corpus is the
check that the lengths are right.

  dialog_disasm.py            decode every script, report success rate
  dialog_disasm.py <chunk>    disassemble one conversation
"""
import omkpaths
import json, struct, sys, os, collections

def _vm_table():
    r"""The 153-row VM dispatch table: {op: {"operands": n}}.

    TWO SOURCES, AND THE ORDER MATTERS.

    `clean/_vmtable.json` is authoritative - it is extracted from the listing
    at 0x004C0140 by `vm_table.py`. But the listing is a derivative work of
    `Runtime 2.exe` and is not distributed with this repository, so a
    legitimate checkout may not have it, and this module is imported by
    `verify.py` at start-up: a bare `open()` here made the ENTIRE suite
    unrunnable without the disassembly, not merely the 14 checks that read it.

    `tables/vm_opcodes.json` IS committed, and reproduces this table exactly -
    153/153 operand counts and 49/49 `.TAG` domains, asserted by
    `verify.py: vm table sources`. So it serves as the fallback.

    `clean/` is tried FIRST rather than second, and that is not a preference.
    `exetables.py` GENERATES `tables/vm_opcodes.json` out of this module's
    `SECTION`; if this read `tables/` whenever it existed, that generator
    would derive the file from itself and `exetables.py --check` would report
    a stale table as fresh. Preferring `clean/` keeps the freshness check
    honest wherever the listing is present, and the fallback only ever engages
    where there is nothing to be stale against.
    """
    p = omkpaths.clean("_vmtable.json")
    if os.path.exists(p):
        return {e["op"]: {"operands": e["operands"]} for e in json.load(open(p))}
    rows = json.load(open(omkpaths.tables("vm_opcodes.json")))["rows"]
    return {r["op"]: {"operands": r["table_says"]} for r in rows}


TAB = _vm_table()

# hand-decoded from the handler assembly
NAME = {
  0:"dbg.dump_ctx", 1:"dbg.dump_code", 2:"nop", 3:"end",
  4:"jmp", 5:"jmp_if_true", 6:"jmp_if_false",
  7:"push.i8", 8:"push.i16", 9:"push.i32", 10:"push.var", 11:"drop",
  12:"set.var", 13:"set.var2", 14:"set.var.i8", 15:"set.var.i16",
  16:"set.var.i32", 17:"set.var.var", 18:"set.var.pop",
  19:"var.add", 20:"var.sub", 21:"var.mul", 22:"var.div",
  23:"var.and", 24:"var.or",
  25:"cmp.eq", 26:"cmp.lt", 27:"cmp.gt", 28:"cmp.le", 29:"cmp.ge", 30:"cmp.ne",
  31:"add", 32:"sub", 33:"mul", 34:"div", 35:"bitand", 36:"bitor",
  37:"and", 38:"or", 39:"neg", 40:"not", 41:"bitnot",
  # 42/43/44 are one family, read off 0x00402940 / 0x004029A0 / 0x00402A30:
  # a 16-bit jump TARGET through the shared fetch (the 0x4000 bit selects a
  # variable), then a LABEL of 1, 2 or 4 bytes, compared against the stack top
  # with a PEEK - `mov edi, [eax+10h]` / `mov dx, [eax+14h]` /
  # `mov <reg>, [<base>+edx*4-4]`, and nothing pops. Equal falls through to the
  # next instruction; different sets pc to the target. So the label is the
  # `case` value and the target is where the chain continues, which is a switch
  # compiled as a comparison chain over one pushed selector. 43's label goes
  # through the same 0x4000 indirect as the target; 44's is a plain int32.
  42:"case", 43:"case.i16", 44:"case.i32",
  61:"dialog.start", 64:"zone.enable", 65:"zone.disable",

  # --- phase 1, each traced through its handler ---------------------------
  # 73: operand logged as ADDRESSES; resolved by Address_Find (0x0040E5E0),
  #     which scans the AREA +60 table, then the result is handed to
  #     sub_41BF50 together with sub_419E00() - the actor being moved.
  73:"actor.goto_address",
  # 92: builds "%s.ADP" and "IMAGES\\%s" and "{C}%s" - a voice/still/subtitle
  #     clip played on the OBJECTS index it logs.
  92:"media.play",
  # 95: field 0 is the CAMERAS index - traced: the first operand lands in edi
  #     and edi is what the handler passes to the logger with "CAMERAS". It
  #     then issues Camera_Request mode 12, the same path the dialogue uses,
  #     with the duration read from the camera record rather than the operand.
  95:"camera.set",
  # 78/79: an opposite-polarity pair on the same CHARACTERS operand. Both
  #     resolve it through Scene_FindObjectRecord to a 20-byte object record,
  #     then 78 calls Actor_Attach and sets that record's state bit to 1 while
  #     79 calls Actor_Detach and clears it. 78 additionally places the actor
  #     at the record's own x/y/z and angle when its second field is non-zero.
  78:"character.show", 79:"character.hide",
  # 50/51: field 0 selects one of four object lists, field 1 is the OBJECTS id.
  #     50 reads the record from IAM\\OBJECT and calls Inventory_Insert, skipping
  #     the add when lists 2 or 3 already hold the id. 51 finds the id and
  #     shifts the array down over it, writing 0xFFFF into the vacated slot and
  #     doing the same to the parallel array - an unambiguous remove.
  50:"inventory.add", 51:"inventory.remove",
  # 132/133: sub_41E1B0(1) / (0), which drives a 0..255 alpha ramp over 60
  #     frames - state 3 ramps the overlay up, state 4 back down. A screen fade.
  #     They bracket the cutscene idiom: goto_address, fade, dialog.start, unfade.
  132:"fade.to_black", 133:"fade.from_black",
  # 96: the same mode-12 Camera_Request as 95, but it also writes the script
  #     status word at ctx+22 and stores a resume id. Script_Execute runs only
  #     while that word is 1, so writing it suspends the script until the move
  #     finishes. 95 leaves it alone and the script carries straight on.
  96:"camera.set.wait",
  # 47: hands its AREAS operand to Area_Transition, which loads the area into
  #     the *other* of the two scene slots and sets the script status to 10.
  #     If the transition is refused the handler rewinds the pc by 7 - its own
  #     size - so the instruction retries on the next tick.
  47:"area.goto",
  # 93: the mirror of 86, and it was misnamed `hud.show_var` until 2026-09-02.
  #     Read off 0x00404790: three int16 fields through the shared fetch -
  #     field 0 the ACTOR (-1 = the player), field 1 the PROPERTY id, field 2
  #     the VARIABLES index it announces. `Var_Get(field 2)` reads the value
  #     (logged as VALUES) and BOTH arms then fall into `Actor_SetProperty`
  #     (0x0040B8D0) with {property, _, value, actorId}: the opcode WRITES a
  #     variable into an actor's property. The player arm calls
  #     `Hud_ShowValue` (0x0041CE50) first, which is where the old name came
  #     from - but that is a flash of the changed stat on the HUD, not the
  #     effect: it returns 0 for anyone but the player and for properties 4/5,
  #     and the property write happens either way. Op 86 `var.set.actor_stat`
  #     is the same three fields in the same order the other way round
  #     (`Actor_GetProperty` then `Var_Set(field 2)`). Every variable it is
  #     used with is a stat: Vie (262), Carac Attack (133), Carac Dodge (131),
  #     Anneaux, Mana, Argent.
  93:"actor.stat.set",
  # 48: Area_Transition in mode 1, which advances the state machine past the
  #     pending state (3 -> 4, or 8 -> reset). Where area.goto *requests* a
  #     change and retries until accepted, this completes one: in the shipped
  #     scripts it is the last instruction before `end`, after area.goto and
  #     actor.goto_address have run. 119 scripts use both, 10 use only this.
  48:"area.arrive",
  # 138/139: another opposite-polarity pair. Both resolve their CHARACTERS
  #     operand and call Actor_SetLookAt, 138 with 1 and 139 with 0, which sets
  #     the actor's look-at field (+400) to the current player or to -1. The
  #     renderer reads that field: when it is set it takes the vector between
  #     the two actors' node transforms and turns the head; when it is -1 it
  #     resets the angles to zero.
  138:"character.look_at_player", 139:"character.look_away",

  # 57/58/59/60: the four ways a world script runs a .SCX script object.
  #     sub_44CD10 scans the loaded scene's object array - pointer at +12,
  #     count at +8, stride **100** - matching an int16 at +26, which is
  #     exactly the 100-byte script-object record of FILE_FORMATS 5c. So the
  #     operand is a scene-local script-object id, not a .TAG index, and that
  #     is why no handler in this group logs a domain.
  #
  #     57/58 start it on the scene (ScriptObject_Start); 59/60 start it on a
  #     character, taking a CHARACTERS index in field 0 and the object id in
  #     field 1 (ScriptObject_StartOnActor). All four then ask
  #     ScriptObject_IsRunning and, if it is, request camera mode 13 with the
  #     last field as the travel time.
  #
  #     The .wait halves are the ones that write **4** into the script's status
  #     at ctx+22, which is how an opcode suspends (see SCRIPT_VM.md); 58 also
  #     registers the running object against the script's own slot at ctx+30
  #     so it can be resumed. 57 and 59 write no status and run on.
  57:"scx.play",  58:"scx.play.wait",
  59:"scx.play.actor", 60:"scx.play.actor.wait",

  # 63/89: the same pair shape for the player's own animation. Both call
  #     Player_GoToMove, which looks the operand up in the bank at the player
  #     record's [45] and hands it to SetPersoBank - the engine's own name for
  #     it, from "SetPersoBank, error on GoToMove". 89 passes the script's slot
  #     and writes status 4; 63 passes -1 and runs on.
  63:"player.move", 89:"player.move.wait",

  # 104/105: a complementary pair, both Actor_HoldAnimation on Actor_Player(),
  #     104 with 1 and 105 with 0. It sets or clears bits 0x80 and 0x01 of the
  #     animation instance's flag word, and the animation update tests
  #     `flags & 0x81` and pins the transform back to rest every frame while
  #     either is set. In the scripts they bracket a cutscene: 104 before the
  #     fade to black, 105 after the last camera and before the fade back.
  104:"player.anim.hold", 105:"player.anim.release",

  # 120: Var_Set(field2, Random_NoRepeat(field0, field1)) - pick one of N and
  #     remember it. field0 is 1 at all 235 sites and field1 runs 2..22.
  #     **field2 is the variable the very next instruction pushes, at 235 of
  #     235 sites**, which is what proves both the field and the 6-byte length:
  #     the idiom is always `var.set.random 1, N, v` / `push.var v` / `case`.
  #     VARIABLES[119], the commonest, is called 'n° VO passant'.
  120:"var.set.random",

  # 103: sub_41E110 builds "TRACKS\\%d.ADP" from field 0 and streams it, with
  #     field 1 as the loop flag; field 2 selects the stream buffer size. The
  #     handler skips the whole thing when field 0 already equals g_MusicTrack,
  #     which it sets afterwards - a "don't restart the track that is already
  #     playing" guard. 511 of its 514 sites name a file that exists in
  #     gamedata/TRACKS; the other 3 are track 0, and Music_PlayTrack returns without
  #     playing anything for a track below 2. The area loader does the same
  #     thing with the same guard, from the area header's own +142 field.
  103:"music.play",

  # 118/119: Screen_FadeToColor / Screen_FadeFromColor, which are
  #     Screen_StartColorFade modes 1 and 2 - the same full-screen quad, one
  #     ramping alpha 0 -> 255 and holding, the other 255 -> 0 and switching
  #     itself off. Fields 0 and 1 are the low and high halves of a **24-bit
  #     colour**: the top byte is zero at all 439 sites, and the values are
  #     black (314), white (123) and two others. Field 2 is the duration and
  #     field 3 a delay before it starts. 132/133 are the same idea with the
  #     colour and timing fixed.
  118:"fade.to_color", 119:"fade.from_color",

  # 131: Music_SetVolumeRamp(field0, field1) - set the target music volume and
  #     the per-frame step to reach it. The mixer walks the current value
  #     towards the target and hands it to Music_SetVolume, which clamps to
  #     0..100 and converts to the DirectSound hundredths-of-a-dB attenuation.
  #     All 52 sites are inside 0..100, and 36 of them are 0 - scripts fading
  #     the music out.
  131:"music.volume",

  # 71/72: the engine keeps two areas resident and each may have a scene loaded
  #     over it; gameDB+12 is an int16 per area holding which, or -1.
  #     71 takes AREAS in field 0 and SCENES in field 1: for whichever resident
  #     slot holds that area it tears down the scene there and calls Scene_Load
  #     + Actors_SpawnFromTables for the new one, then records area -> scene.
  #     72 takes the area alone, tears its scene down and writes -1; if the
  #     slot is the calling script's own (ctx+31) it also sets bit 8 of the
  #     script flags. Field 0 is a valid AREAS id at 78/78 and 82/82 sites and
  #     a SCENES id at only 55%/50%, which is what separates the two fields.
  71:"scene.load", 72:"scene.unload",

  # 66/68/76/77: the prop rig. A second table alongside the 20-byte object
  #     table holds the world's **props** - 24-byte records at AREA +44 /
  #     SCENE +12 (FILE_FORMATS). Each carries the OBJECTS id at +2 and, at
  #     +22, an index into a **2-bit** array in the game DB: bit 0 "this prop
  #     exists", bit 1 "it is in the scene". All four handlers resolve their
  #     operand by scanning that table in the script's own area and then in
  #     the scene loaded over it, and all four act on the record they find.
  #
  #     76 re-parents the prop's node under the **scene root** and sets bit 1;
  #     77 puts it back under its own file root and clears bit 1. The scene
  #     root is what gets drawn, so this is show/hide - and Scene_LoadProps
  #     replays exactly the same call for every record whose bit 1 is set,
  #     which is what makes the state persistent. 78/79 are the same pair for
  #     characters, and the four opcodes are adjacent.
  #
  #     66 hands the prop's instance to Actor_HoldObject, which parents it to
  #     a bone of the player and records it at the actor's +164; 68 takes no
  #     operand because it reads that field back - it finds the record whose
  #     runtime slot matches what the player is holding, releases it, hides it
  #     and clears the field. All 443 operands of 66/76/77 name a real prop
  #     record.
  66:"object.hold", 68:"object.release",
  76:"object.show", 77:"object.hide",
  # 67 is 66 for a named character rather than the player, and it is what
  #     arms the enemies: field 0 is the CHARACTERS id, field 1 the prop, and
  #     **157 of its 159 objects are called `Gun ...`** - 'Gun Braqueur 4',
  #     'Gun Double-Waver 2', 'Gun Tetra 30'. It also settles the character
  #     record's **+270**: 67 writes the held object's id there, and 68 and 69
  #     both write -1. Field 1 names a prop record at 159/159 sites and field
  #     0 an object-table record at 159/159.
  67:"object.hold.actor",

  # 75/91: both end in Var_Set, and the .TAG name of the variable they write
  #     is what identifies them - a test the data could fail outright.
  #     75 stores the OBJECTS id of the prop the player is holding
  #     (word_4E6CA0[Actor_HeldObjectSlot(player)], or -1). It writes
  #     VARIABLES[13] 'ObjetUtilisé' at **235 of 235** sites, and the scripts
  #     read it straight back: `var.set.used_object 13` / `push.i8 61` /
  #     `push.var 13` / `cmp.eq` - "is the thing in your hand the Sneak Den?"
  #     91 stores word_69BC80[player], the CHARACTERS id of the body the
  #     player currently occupies. Every one of its 62 sites writes a variable
  #     whose name begins 'Joueur'.
  75:"var.set.used_object", 91:"var.set.player_id",

  # 80/81/82/84/116/117: **Shoot mode**, and the binary names it itself -
  #     sub_423170 refuses with "Le mode Shoot n'est pas activ\xe9!!!" and
  #     "error : perso is not in shoot mode !". So do the areas that use these
  #     six: 'Anekbah Shooting gallery', '1-10 Supermarch\xe9 Shoot', the four
  #     'Jaunpur Tetra' gunfights, 'Jaunpur Docks', 'Ix Astaroth 2'. All six
  #     occur in the same 11-18 chunks of 328.
  #
  #     80 Shoot_Enter: allocate the 100 x 192 combat records, set g_ShootMode
  #        and put the player in actor state 3. Its operand is the weapon to
  #        start with, resolved by Weapon_SlotForObject - -1 at 27 of 30 sites
  #        and OBJECTS[40] 'B\xe2ton de pouvoir' at the other 3.
  #     81 Shoot_Leave: release whatever the player holds, clear g_ShootMode,
  #        and when the operand is non-zero also forget the weapon slot.
  #     82 Shoot_ActorEnter: put CHARACTERS[field0] into state 3 and install
  #        its behaviour function, chosen by the actor's own type property.
  #     84 Shoot_ActorAction: issue action field1 (parameter field2) to that
  #        character; the action picks an animation type out of its .ani list.
  #        **309 of its 319 sites are preceded, in the same script, by an 82
  #        on the same character** - which is what the handler's own error
  #        string demands.
  #     116/117 are an exact opposite pair on two globals: 116 sets
  #        g_PlayerBehaviourOff to 1 and the input profile to 0, 117 clears it
  #        to 0 and sets profile 2 (and the player's state back to 3). The
  #        per-frame dispatch calls the player's behaviour only while the flag
  #        is 0. In the scripts 116 opens a beat and 117 closes it: 116 is the
  #        first instruction 46 times, 117 the last 42 times, and 98 of the
  #        105 scripts using one use both.
  80:"shoot.begin", 81:"shoot.end",
  82:"shoot.actor.enter", 84:"shoot.actor.action",
  116:"shoot.player.suspend", 117:"shoot.player.resume",

  # 69: release the object held by CHARACTERS[field0] - the same tail as
  #     object.release but for a named character rather than the player, and
  #     without the prop-table half. It clears the character record's +270,
  #     the field 67 sets. All 499 operands name a record in the 20-byte
  #     object table.
  69:"object.release.actor",

  # 86: Var_Set(field2, Actor_GetProperty(field1, field0)). field0 is the
  #     actor, -1 meaning the player at 458 of 459 sites; field1 selects one
  #     of the getter's cases, which read a contiguous stat block in the
  #     276-byte actor record; field2 is the VARIABLES index.
  #
  #     What identifies it is that the .TAG names agree with the offsets:
  #     18 distinct variables are written, and **every one is written by
  #     exactly one property value** - property 1 (+170) only ever writes a
  #     'Vie' variable, 2 (+156) only 'Mana', 4 (+172) only 'Argent', 5 (+174)
  #     only 'Anneaux', and 16/17/18/19 (+160/+162/+164/+166) only
  #     'Carac Attack' / 'Carac Body Shield' / 'Carac Dodge' /
  #     'Carac Fight Experience'.
  86:"var.set.actor_stat",

  # 70: open one of the game's 2D interface screens and **suspend the script**
  #     - it writes 6 into the status word at ctx+22, so Script_Execute stops
  #     until the screen closes. sub_429BB0 loads the screen's artwork with
  #     sprintf("I2d\\bitmaps\\%s"), and gamedata/I2D/bitmaps holds exactly the 11
  #     files the binary names, no more and no fewer.
  #
  #     Field 0 indexes a 92-byte table of 37 screens whose names sit
  #     contiguously in the binary (SCREEN below). Three things confirm the
  #     mapping, none of them a range check:
  #       * the shop screens open in the area their name says - BANK in the
  #         three 'Banque' areas, LIBRAIRIE in the four 'Librairie' ones,
  #         LIB. LAHOREY 40 times and every one of them in 'Lahoreh
  #         Bibliothèque'. 57 of 57.
  #       * five entries are called "(ELIMINE)" - cut - and the shipped
  #         scripts use **none** of those five indices.
  #       * the handler singles out id 29 and stashes the script context for
  #         it; 29 is OMK START MENU. And shoot.player.resume opens 33 or 34
  #         depending on the player's type field, which are SHOOT MECA and
  #         SHOOT HUMAN.
  #     Fields 1 and 2 are parameters the screen reads out of two globals.
  70:"ui.open",

  # 49: Var_Set(field2, list field0 contains OBJECTS[field1] ? 1 : 0). The
  #     third field was recorded as "a second object" until the handler was
  #     read: it goes to Script_StoreVar. The corpus is unambiguous - 213 of
  #     222 sites write a variable named 'Inventaire', 'Inventaire 2' or
  #     'CDs bowie in inventory', and 197 of 222 are read back by a push.var
  #     within five instructions.
  49:"var.set.has_object",

  # 52: remove **every** copy of OBJECTS[field1] from list field0 - the same
  #     shift-down as inventory.remove but looping until none is left, moving
  #     both the id array and the 56-byte records at dword_69BD68. field1 of
  #     -1 instead sweeps the list, removing each object whose IAM\OBJECT
  #     record has **bit 1 of the flags at +4** set (245 of 1002: weapons,
  #     ammunition, consumables, keys - but not money, whose flag value 32
  #     lacks the bit). The offset was first recorded as "+36": the handler
  #     reads the 56-byte *list* record at +0x24, and the list record is
  #     [display name 32][header 24], so +0x24 lands on header byte 4 = the
  #     original record's +4. 29 of the 37 -1 sites sit next to
  #     player.become - lost on reincarnation.
  52:"inventory.remove_all",

  # 56: **reincarnation** - the game's signature mechanic. Returns at once if
  #     word_69BC80[player] already equals the operand; otherwise tears down
  #     the old player's attachments (UI, held object, sound), moves the new
  #     body in, gives the old body the vacated object-table record, and
  #     copies the 276-byte character record into the game DB - 268 bytes to
  #     +68, the bio strings into the two buffers its header points at. That
  #     copy is why the player's stats persist in the save.
  56:"player.become",

  # 62: **melee combat**. Switches BOTH the player and CHARACTERS[field0] to
  #     .CTL slot 2 - the slots name themselves: +72 H1AVNT (aventure),
  #     +81 H1SHOT (shoot), +90 H1CMBT (combat) - then Fight_Engage installs
  #     the opponent (g_FightOpponent), the script suspends with status 3,
  #     and camera mode 14 is requested with field2 as the travel time.
  #     field0 names a character record at 108/108 sites; field1 is 0 at all.
  62:"fight.begin",

  # 46/90: the scx.play pair **on the player** - ScriptObject_StartOnActor
  #     with Actor_Player(), then the same camera mode 13 request as the
  #     other four, field1 being the travel time. 46 registers the script's
  #     slot and writes status 4 (waits); 90 passes -1 and runs on.
  46:"scx.play.player.wait", 90:"scx.play.player",

  # 87/88: an opposite pair on the game DB's third bitmap (+24, one bit per
  #     ADDRESSES entry, set by 0x0040B090). Every operand is a city
  #     destination with a postal-style name - 'Anekbah - Bar Zone 52',
  #     "Lahoreh - Place d'Yrmali" - never an internal address like
  #     'Dialogue Telis Cuisine', which reads as the slider's unlocked
  #     destination list; but no reader of the bit has been traced, so the
  #     names stay neutral.
  87:"address.enable", 88:"address.disable",

  # 126: camera.set.wait from the **world camera table** - 44-byte records,
  #     id at +24, at AREA +64 (count +84), SCENE +32 (+52) and GLOBAL +20
  #     (+30) - aimed at an ADDRESSES entry resolved by Address_Find, mode
  #     12, status 7. field0 is in that table at 84/84 sites, field1 in
  #     ADDRESSES.TAG at 84/84, field2 is 20 at all 84.
  126:"camera.set.at_address",

  # --- the tail: everything at 32 uses or fewer -------------------------
  # 111..115: the mission timer, and 111/112 were named BACKWARDS here until
  #     2026-09-02. dword_930768 is the flag word, dword_930764 the set
  #     value, dword_930760 the START STAMP and dword_4C2BD0 the running
  #     clock. Every handler is a `jmp` behind the usual dword_6A05E0 gate:
  #     111 -> 0x0041E2B0, 112 -> 0x0041E2D0 (read from the image's own
  #     bytes; the listing's `align` runs make this easy to transpose).
  #
  #     What settles the pair is the READER, sub_41E300(&h,&m,&s), which
  #     nothing had cited: bit 4 set reports dword_930764 flat (frozen);
  #     the word EXACTLY 1 reports 0/0/0; otherwise it reports
  #     `dword_4C2BD0 - dword_930760` - now minus the start stamp - turned
  #     round into `dword_930764 - elapsed` when bit 2 is set (countdown).
  #     So bit 0 is not "running", it is "being CONFIGURED", and the timer
  #     only runs once bit 0 is CLEAR and a stamp has been written.
  #
  #     That makes the sequence: 111 claims the timer (`or bit 0`, refusing
  #     if it is already set) and it reads 0/0/0 - the STOP; 114 and 113
  #     configure it and both refuse unless bit 0 is set (sub_41E290,
  #     sub_41E270), i.e. they are only legal between a stop and a start;
  #     112 writes `dword_930760 = dword_4C2BD0` - stamping NOW as the
  #     start - and clears bits 0 and 4 while keeping the mode bit, which is
  #     the START. The corpus is exactly this shape: `114 -> 113 -> 112`
  #     twelve times (the Tetra bomb missions, 900 s = 15 minutes at all 12,
  #     mode 12) and `111` alone or `111 -> 115`, i.e. stop then read the
  #     zero. 113 stores field0 * 1000, so its operand is SECONDS; 115 is
  #     Var_Set(field0, the reader's result).
  #
  #     110 (sub_41E260, `dword_930768 = 1` unconditionally) is the same end
  #     state as 111 without the refusal - a hard reset. 1 site, unnamed.
  111:"timer.stop", 112:"timer.start", 113:"timer.set", 114:"timer.mode",
  115:"var.set.timer",
  # 45: load AREAS[field0] into the *other* resident slot without
  #     transitioning to it - the teardown-and-load half of Area_Transition,
  #     status 8 until the load lands. Sits before lift/door screens: the
  #     area behind the door is being brought in.
  45:"area.preload",
  # 54: Camera_Request mode 0 - the default follow camera - with both
  #     subjects set to Actor_Player().
  54:"camera.follow_player",
  # 94: sprintf("%06lx.BMP", field0) under IMAGES\ and display it. All four
  #     shipped operands name a file that exists (000012/000014/000020/
  #     000021.BMP).
  94:"image.show",
  # 98: find the runtime object field1, take its position plus its ground
  #     height, find prop field0's record, Object_SetPlacement - move a prop
  #     to where something else is.
  98:"object.place_at",
  # 106/107: walk all 100 shoot records setting/clearing bit 15 of the flag
  #     word at +160 and toggling dword_4E9760 - freeze and release every
  #     combatant at once.
  106:"shoot.freeze_all", 107:"shoot.unfreeze_all",
  # 123: SetPiece_Find scans the loaded set's 76-byte piece records (id at
  #     +0) and clears bit 1 of the piece's +72 flags - remove one piece of
  #     the decor. The show path of the same helper is never called from
  #     here.
  123:"set.hide_piece",
  # 127: copy the player node's current world position into the walker's
  #     position pair at +0xE8/+0xF4 and invalidate the ground cache
  #     (+0x104 = FLT_MAX) - re-sync collision after a scripted move.
  127:"player.pos.sync",
  # 128: move up to Var_Get(field) objects from the head of one list to
  #     another, clamped by both lists' room; each moves with its 56-byte
  #     record.
  128:"inventory.transfer",
  # 129/130: byte_910327. The walker in sub_465460 refuses a step down of
  #     more than 11.81 units (30 raw, knee height) unless that byte is set -
  #     scripts set it around staged moves so a character may walk off a
  #     ledge, then restore it.
  129:"walk.ledges.ignore", 130:"walk.ledges.obey",
  # 136: Camera_SetShake(C, field0, field1 / 2.54) - a vertical sine offset
  #     on the active camera's eye and aim that decays over field0, amplitude
  #     field1 in raw units. Explosions and rumbles: Grotte Gandhar, the
  #     Tetra raids.
  136:"camera.shake",
  # 142: insert Var_Get(field1) under the character's name into the
  #     high-score table and open UI screen 36, HIGH-SCORE. The shooting
  #     gallery's exit.
  142:"ui.highscore",
  # 144: assemble a 32-bit number from fields 1-2, sprintf "%06x.3dm", play
  #     it on CHARACTERS[field0] via Morph_Play. The mechanism is certain;
  #     the three shipped sites name morphs that do not exist (ff00ffff..d),
  #     so the opcode is live but its content was cut.
  144:"morph.play",
  # 146/147: dword_4EB8C8 on/off - the flag the .WRE ambience updater runs
  #     under (Ambience_Load parses SOUKT.WRE, SMARKET1.WRE...).
  146:"ambience.on", 147:"ambience.off",
  # 148/149: stash the player's object list 0 (up to 18 ids into
  #     dword_4E66C0, -1 padded) plus five int16s of the character sheet at
  #     gameDB+60+0x104; 149 empties the list and re-adds the stash from
  #     IAM\OBJECT. Bracket the sub-games.
  148:"inventory.save", 149:"inventory.restore",
  # 152: sets dword_4E6C9C. The game loop answers by resetting the session
  #     (sub_407DC0 mode 3), calling Game_NewGame (mode 2) and fading from
  #     WHITE - restart the game from the beginning. Three sites: Lahoreh,
  #     Tetra 1, and Ix Astaroth 2, where Astaroth captures the player's
  #     soul.
  # 150/151: swap a bank of FIVE renderer function pointers - six two-entry
  #     arrays at 0x004C4910, `sub_42FA00(bank)`. `Game_Init` installs bank 0;
  #     150 is `sub_42FA00(1)` and 151 `sub_42FA00(0)`. The two banks' scene
  #     renderers are the SAME 0x4000-bucket walk of near-identical length
  #     (Render_FlushBuckets 659 lines against sub_42FF80's 660), differing in
  #     that bank 1 converts every vertex colour to luma grey - 13 sites of
  #     `(299R + 587G + 114B)/1000` against none in bank 0. So bank 1 is a
  #     GREYSCALE scene renderer, and the corpus says exactly what it is for:
  #     all **14** sites of 150 are closed by a later 151, and **74 of the 82**
  #     instructions between them (90%) are `camera.set`, `camera.set.wait`,
  #     `fade.to_color` and `fade.from_color`. The bank brackets a CUTSCENE, in
  #     eight chunks - Mahaleel, Anekbah Grotte Gandhar Light, Jaunpur Zone 24,
  #     Lahoreh Konshu, 1-13 Morgue, 1-12 Anissa Aka's Bar, 1-02 Appart Kayl
  #     Rencontre and 1-20 Concert Bowie Bar 02.
  #     The other four swapped pointers are NOT read; the name is for the one
  #     difference that is established. docs/ASSETS.md 4c corrected: this bank
  #     was recorded as never installed, from a caller count taken out of the
  #     decompilation, which cannot see a VM table entry.
  150:"render.grey.on", 151:"render.grey.off",
  152:"game.restart",
}

# Value labels for operands that index a table inside the binary rather than a
# .TAG file. tools/script_dump.py renders them the same way as a tag name.
SCREEN = [
    "VIDEOPHONE", "TRANSCAN (ELIMINE)", "MULTIPLAN", "APPARTEMENT (ELIMINE)",
    "LIFT", "TERMINAL", "JOURNAL (ELIMINE)", "SLIDER", "PUZZLE (ELIMINE)",
    "SNEAK", "FIGHT_NOT_SIM (ELIMINE)", "FIGHT SIM", "GANDHAR DOOR",
    "APPART DEN", "XACHEN", "SURV ERROR", "SURV NO KIT", "SURV KIT",
    "ARCHIVES", "MORGUE", "BANK", "PHARMACIE", "ARMURERIE", "RESTAURANT",
    "BAR", "SORCELLERIE", "LIBRAIRIE", "SEX-SHOP", "DIVERS",
    "OMK START MENU", "SAVE GAME", "PAUSE GAME", "LIB. LAHOREY",
    "SHOOT MECA", "SHOOT HUMAN", "OPTIONS", "HIGH-SCORE",
]
# 13 and 24 carry no name string of their own; they are named here for what
# the scripts open them in ('1-16 Appart Den', and the bars and Zone 24
# alongside the other boutiq.bmp shops).
FIELD_ENUM = {70: {0: SCREEN}}
# lengths the table gets wrong, read off the handler assembly
# Operand counts the table at 0x004C0140 gets wrong.
#
# Each was recovered from the handler's own assembly: the interpreter keeps the
# instruction pointer at [ctx+0Ch], and a handler walks it forward over its
# operands and stores it back, so summing those advances is the ground truth.
# Two things have to be excluded when reading them - a handler may reuse the
# register afterwards, and op 47 *rewinds* the pointer by 7 to loop, which is
# control flow rather than operands.
#
# 42 was already known. The rest showed up once the world scripts in IAM\AREA
# and IAM\SCENE were decodable: they exercise 124 opcodes against the
# conversations' 25, so most of the table had never been tested. With these,
# all 5647 world script slots decode cleanly, against 53 failures before.
#
# 147, 148, 149 and 151 take *no* operands - their handlers never touch the
# instruction pointer at all - though the table claims 2 each.
#
# 57, 58 and 78 were found a different way, and the way matters. A decode-
# integrity test cannot see an operand count that is *short by an even number
# of zero bytes*: the surplus decodes as opcode 0, which is a legal
# zero-operand instruction, so nothing desynchronises. Those three were
# invisible until a listing showed opcode 0 running at 11% of all instructions
# - absurd for a handler that hex-dumps 64 bytes to printf. Correcting them
# drops opcode 0 from 11.2% to 2.9% with no new failures, and the handler
# assembly independently gives the same three lengths.
#
# 103 has the same shape and was found the same way. Its handler reads three
# int16 operands in a straight line - no branch, nothing conditional - and the
# only reason it stood at the table's 2 was that a corpus test with 57/58/78
# still wrong showed a break at 6. With those corrected it decodes at 6 with no
# failures, the same 514 sites, and 2056 fewer instructions - exactly the 4
# surplus bytes per site, which at length 2 were showing up as 1531 phantom
# `dbg.dump_ctx` and 525 phantom `dbg.dump_code`. Opcode 0 drops 2.83% -> 0.32%.
#
# 146 and 150 joined 147/148/149/151 late: their handlers also never touch the
# instruction pointer, and at the table's 2 the swallowed bytes showed as
# phantom dbg ops (op0 187 -> 149, op1 55 -> 37 across the corpus, no new
# failures, site counts unchanged).
#
# 43 and 44 came last (2026-09-02) and by a different route again: nothing in
# the corpus uses them, so no decode could ever have found them. They are 42's
# two siblings - the same 16-bit target, then a label of 2 bytes (43,
# `add ecx, 2` then `mov dl, [ecx-2]` / `mov dh, [ecx-1]`) or 4 (44,
# `add edi, 4` then four `mov bl, [eax…]` assembled with two shifts) - so 4 and
# 6 against the table's 0. They are recorded because a decoder that met one
# would desynchronise from the label byte on, silently: the table says 0.
# 16 is the same shape as 43/44 and was found the same way (T1, 2026-09-02):
# 0 shipped sites, so no decode could reach it. `set.var.i32` fetches the
# VARIABLE as a 16-bit operand (`lea esi, [ecx+2]`, with the 0x4000 indirect)
# and then reads a 32-bit literal - `add esi, 4`, four bytes assembled from
# [eax], [eax+1] and two `inc eax` - before `Var_Set` (sub_40E510). That is
# 2 + 4 = 6, against the table's 5. Its own family corroborates: 14
# `set.var.i8` is 2+1 = 3 and 15 `set.var.i16` is 2+2 = 4, both of which the
# table gets right, so 16 is the odd one out and 5 is not a length any of the
# three shapes can produce.
LEN_FIX = {16: 6, 42: 3, 43: 4, 44: 6, 62: 6, 17: 4, 57: 6, 58: 6, 78: 4, 80: 2,
           84: 6, 103: 6, 118: 8, 119: 8, 144: 6, 146: 0, 147: 0, 148: 0,
           149: 0, 150: 0, 151: 0}

# Which IAM\*.TAG table an opcode's operand indexes. Taken from the section
# name each handler passes to the debug logger sub_40EC70 - "VALUES" means a
# literal number, anything else names a .TAG file.
def _sections():
    """{op: TAG domain}. Same two sources, same order, as `_vm_table()`."""
    p = omkpaths.clean("_vmsummary.json")
    if os.path.exists(p):
        out = {}
        for _r in json.load(open(p)):
            _s = [x for x in _r["strings"] if x.isupper() and x != "VALUES"]
            if _s: out[_r["op"]] = _s[0]
        return out
    rows = json.load(open(omkpaths.tables("vm_opcodes.json")))["rows"]
    return {r["op"]: r["tag"] for r in rows if r["tag"]}


SECTION = _sections()
SECTION.update({10: "VARIABLES", 12: "VARIABLES", 13: "VARIABLES"})

# Where a handler was traced but logs nothing - or logs one table while another
# of its fields indexes a second one. SECTION is per opcode; this is per field,
# and tools/script_dump.py prefers it. Only fields actually traced belong here.
FIELD_SECTION = {
    71: {0: "AREAS", 1: "SCENES"},   # scene.load: into the slot holding AREAS[0]
    72: {0: "AREAS"},                # scene.unload
    75: {0: "VARIABLES"},            # var.set.used_object - VARIABLES[13] 235/235
    91: {0: "VARIABLES"},            # var.set.player_id - always a 'Joueur' one
    80: {0: "OBJECTS"},              # shoot.begin - the weapon to start with
    67: {1: "OBJECTS"},              # object.hold.actor - field 0 is the actor
    49: {1: "OBJECTS", 2: "VARIABLES"},
    50: {1: "OBJECTS"},              # inventory.add - the handler pushes ebx,
                                     # its SECOND operand (0x0040A4D0); field 0
                                     # is the list selector, which the same
                                     # code then compares against 2 and 3
    51: {1: "OBJECTS"},              # inventory.remove - the same shape as 50
                                     # and 52; the handler (0x0040A5A0) pushes
                                     # edi, its second operand, and uses the
                                     # first as `[esi+esi*2]` into the list
                                     # table at word_69BD62
    52: {1: "OBJECTS"},              # field 0 is the list selector
    126: {1: "ADDRESSES"},           # field 0 is a world-camera id
    115: {0: "VARIABLES"},           # var.set.timer
    86: {2: "VARIABLES"},            # var.set.actor_stat - the third field
    93: {2: "VARIABLES"},            # actor.stat.set - the handler pushes esi,
                                     # the THIRD operand; field 0 is the ACTOR
                                     # (-1 = the player, `cmp edi, 0FFFFFFFFh`)
                                     # and field 1 the PROPERTY id. Read from
                                     # 0x00404790 and confirmed by the capture,
                                     # which announces VARIABLES twice where a
                                     # `set.var N` is followed by an
                                     # `actor.stat.set(-1, _, N)`.
}

def load_tags():
    import os, re
    out = {}
    for fn in os.listdir(omkpaths.data("IAM")):
        if not fn.endswith(".TAG"): continue
        sec = fn[:-4]
        cur = {}
        for line in open(omkpaths.data("IAM/") + fn, "rb").read().decode("cp1252", "replace").splitlines():
            m = re.match(r"^(\d+)=(.*)$", line)
            if m: cur[int(m.group(1))] = m.group(2).strip()
        out[sec] = cur
    return out
TAGS = load_tags()

def tag_in(sec, value):
    if not sec: return ""
    name = TAGS.get(sec, {}).get(value)
    return f"   ; {sec}[{value}] = {name!r}" if name else f"   ; {sec}[{value}]"

def tag(op, value):
    return tag_in(SECTION.get(op), value)


def word_operand(b):
    """Render one 16-bit operand the way the shared fetch (0x00401AA0) reads it.

    0xFFFF is the literal -1 and is never indirected; otherwise bit 0x4000
    means "index the context's variable table at [ctx+24h]" rather than a
    literal, which the handlers implement as
    `test <reg>, 4000h` / `movsx <reg>, word ptr [<vars>+<reg>*2+2]`.
    """
    v = b[0] | (b[1] << 8)
    if v != 0xFFFF and v & 0x4000: return f"{v & 0x3FFF}(indirect)"
    return f"{struct.unpack('<h', b[:2])[0]}"


# The `case` family, keyed on the OPCODE and not on len(operands): 43's four
# bytes are a target plus an int16 label, NOT an int32, and 44's six are a
# target plus an int32 label, not "no operand at all".  See NAME above.
CASE_LABEL = {
    42: lambda b: f"{b[0]}",                          # int8, no indirect
    43: word_operand,                                 # int16, 0x4000 indirect
    44: lambda b: f"{struct.unpack('<i', b[:4])[0]}",  # int32, no indirect
}


def oplen(op):
    if op in LEN_FIX: return LEN_FIX[op]
    e = TAB.get(op)
    return e["operands"] if e else None

def disasm(b, start, limit):
    """decode from `start`; returns (list of (pc,op,operandbytes), status)"""
    out, pc = [], start
    while True:
        if pc >= limit: return out, "ran off the end"
        op = b[pc]
        n = oplen(op)
        if n is None: return out, f"invalid opcode {op} at 0x{pc:x}"
        if pc + 1 + n > limit: return out, "operands run off the end"
        out.append((pc, op, b[pc+1:pc+1+n]))
        pc += 1 + n
        if op == 3: return out, "ok"
        if len(out) > 20000: return out, "runaway"

def chunks(d):
    n = len(d); first = None
    for i in range(n // 8):
        off, size = struct.unpack_from("<II", d, 8*i)
        if off and size and off + size <= n:
            if first is None or off < first: first = off
        if first is not None and 8*(i+1) > first: break
    for i in range(first // 8):
        off, size = struct.unpack_from("<II", d, 8*i)
        if off and size and off + size <= n and size >= 8:
            yield i, d[off:off+size]

def parse(b):
    sp, nn, nc, _ = struct.unpack_from("<4h", b, 0)
    if nn <= 0 or nc <= 0 or 8 + 64*nn + 44*nc > len(b): return None
    return sp, nn, nc

d = open(omkpaths.data("IAM/DIALOG"), "rb").read()

def scripts_of(b, nn):
    for j in range(nn):
        o = 8 + 64*j
        ptr = struct.unpack_from("<9I", b, o)
        nid = struct.unpack_from("<h", b, o+44)[0]
        for k in range(8):                       # ptr[8] is the string pool
            if ptr[k]: yield j, nid, k, ptr[k]

if __name__ == "__main__":
    if len(sys.argv) > 1:
        want = int(sys.argv[1])
        for i, b in chunks(d):
            if i != want: continue
            p = parse(b)
            if not p: sys.exit("not a conversation")
            sp, nn, nc = p
            print(f"chunk {want}: {nn} nodes, {nc} cameras\n")
            for j, nid, k, off in scripts_of(b, nn):
                role = "cond" if k < 4 else "act "
                code, st = disasm(b, off, len(b))
                print(f"node {j} (id {nid})  {role} branch {k%4}   @0x{off:x}  [{st}]")
                for pc, op, ops in code:
                    nm = NAME.get(op, f"op_{op}")
                    arg = ""
                    if op in CASE_LABEL and len(ops) == oplen(op):
                        arg = (f"target {word_operand(ops[:2])}, "
                               f"case {CASE_LABEL[op](ops[2:])}")
                    elif len(ops) == 1: arg = f"{ops[0]}"
                    elif len(ops) == 2:
                        arg = word_operand(ops)
                    elif len(ops) == 4: arg = f"{struct.unpack('<i', ops)[0]}"
                    note = ""
                    if op not in CASE_LABEL and len(ops) in (1, 2, 4):
                        try: note = tag(op, int(arg.split("(")[0]))
                        except ValueError: note = ""
                    print(f"    {pc:5}  {nm:16} {arg}{note}")
                print()
            sys.exit()

    tot = ok = 0
    bad = collections.Counter()
    opuse = collections.Counter()
    for i, b in chunks(d):
        p = parse(b)
        if not p: continue
        for j, nid, k, off in scripts_of(b, p[1]):
            tot += 1
            code, st = disasm(b, off, len(b))
            if st == "ok":
                ok += 1
                for _, op, _ in code: opuse[op] += 1
            else:
                bad[st.split(" at ")[0]] += 1
    print(f"{tot} scripts, {ok} decode cleanly to `end` ({ok*100//max(tot,1)}%)")
    if bad: print("failures:", dict(bad))
    print(f"\ndistinct opcodes actually used: {len(opuse)}")
    for op, c in opuse.most_common():
        print(f"  op {op:3} {NAME.get(op,'op_%d'%op):16} {c:6}")

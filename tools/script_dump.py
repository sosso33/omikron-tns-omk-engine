#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Print a world script as an annotated listing.

The world scripts in IAM\AREA, IAM\SCENE and IAM\GLOBAL are where the game's
logic lives - triggers, conditions, what happens when you walk somewhere or use
something. This renders one as text, resolving every operand through the .TAG
name tables so the listing reads as intent rather than numbers.

That makes it the main *human*-checkable test for naming VM opcodes: an opcode
name is only right if the scripts using it read as sensible game logic. No
invariant catches a plausible-but-wrong name; a person reading a shop's script
does.

    python3 tools/script_dump.py AREA 22            # every script in a chunk
    python3 tools/script_dump.py AREA 22 --rec 3    # just one record
    python3 tools/script_dump.py SCENE 0
    python3 tools/script_dump.py GLOBAL
"""
import sys, os, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dialog_disasm as D, omkdata
from dialog_triggers import (archive, area_records, scene_records,
                             _second_table, _scripts_from_records, global_file)

BRANCH = {4, 5, 6}          # jmp / jmp_if_true / jmp_if_false take a target

# Which operand field actually carries the .TAG index, where the handler has
# been traced. The domain the handler logs says *which* table; it does not say
# which field, and guessing wrong makes the listing lie: ops 49/50/51 take a
# list selector in field 0, so annotating it as OBJECTS[3] invents an object.
#
# Fields not listed here are annotated with a `?` to mark them unverified.
# A value may be a single field index or a tuple of them.
DOMAIN_FIELD = {
    # the variable and comparison group - one variable, in field 0
    10: 0, 12: 0, 13: 0, 14: 0, 15: 0, 16: 0, 18: 0,
    19: 0, 20: 0, 21: 0, 22: 0, 23: 0, 24: 0,
    17: (0, 1),                  # set.var.var - both fields are variables
    49: 1, 50: 1, 51: 1,         # field 0 selects one of four object lists
    61: 0,                       # dialog.start
    64: 0, 65: 0,                # zone.enable / zone.disable
    73: 0,                       # actor.goto_address
    78: 0, 79: 0,                # character.show / hide
    92: 0,                       # media.play
    93: 2,                       # the VARIABLES index is the *third* field -
                                 # traced: the handler logs esi, which is what
                                 # its third operand fetch lands in
    95: 0,                       # camera.set - traced into edi
    47: 0, 48: 0,                # area.goto / area.arrive
    138: 0, 139: 0,              # character.look_at_player / look_away
    59: 0, 60: 0,                # scx.play.actor - traced: the handler logs
                                 # edi, which is where its first operand lands
    120: 2,                      # var.set.random - the variable is field 2,
                                 # and the next instruction pushes exactly it
                                 # at 235 of 235 sites
    103: 0,                      # music.play - field 0 is the TRACKS\\N.ADP
                                 # number, not a .TAG index
    131: 0,                      # music.volume - field 0 is a 0..100 volume
    71: (0, 1), 72: 0,           # scene.load / scene.unload - see FIELD_SECTION
    66: 0, 76: 0, 77: 0,         # the prop rig - field 0 is the OBJECTS id and
                                 # names a real record in the prop table at all
                                 # 443 sites
    75: 0, 91: 0,                # the VARIABLES index they write
    80: 0,                       # shoot.begin - the weapon OBJECTS id
    86: 2,                       # var.set.actor_stat - field 0 is the actor and
                                 # field 1 the property; the variable is field 2
    67: 1,                       # object.hold.actor - field 1 is the object,
                                 # field 0 the character being armed
    70: 0,                       # ui.open - the screen table index
    49: (1, 2),                  # var.set.has_object - list, object, variable
    52: 1,                       # inventory.remove_all - field 0 is the list
    56: 0, 62: 0,                # player.become / fight.begin - a character id
    87: 0, 88: 0,                # address.enable / .disable
    126: 1,                      # camera.set.at_address - field 0 is a camera
                                 # id in the world table, not CAMERAS.TAG
    113: (), 114: (), 136: (),   # timer.set / .mode / camera.shake - numbers,
    94: (), 128: (),             # not .TAG indices; annotate nothing
    115: 0,                      # var.set.timer
    123: (), 144: 0,             # set piece id; morph.play's character
    69: 0, 82: 0, 84: 0,         # a CHARACTERS id - 1135/1135 name a record in
                                 # the 20-byte object table (no CHARACTERS.TAG
                                 # ships, so nothing is annotated)
}


def operand_text(op, raw):
    """-> (rendered value, trailing comment).

    Operands are **int16 fields**, not one wide integer. A 4-byte operand is two
    of them and a 6-byte operand three, and the opcode's .TAG domain applies to
    whichever field is an index rather than to the whole. Reading op 50's four
    bytes as one int gives `OBJECTS[25493507]`; as two int16 it is
    `OBJECTS[3], 389`, and that reading is valid on 100% of its 472 uses where
    the whole-int reading is valid on 0%.
    """
    if not raw: return "", ""
    if len(raw) == 1: return str(raw[0]), ""
    if len(raw) == 3:                        # target + case byte
        return "%d, case %d" % (struct.unpack("<h", raw[:2])[0], raw[2]), ""
    if len(raw) % 2:                         # odd and not 1 or 3: show the bytes
        return raw.hex(" "), ""

    fields = [struct.unpack_from("<h", raw, i)[0] for i in range(0, len(raw), 2)]
    parts, notes = [], []
    for i, v in enumerate(fields):
        u = v & 0xFFFF
        if u == 0xFFFF:
            parts.append("-1"); continue
        if u & 0x4000:                       # the handler's indirect mode
            parts.append("param[%d]" % (u & 0x3FFF))
            notes.append("field %d fetched at run time" % i); continue
        parts.append(str(v))
        known = DOMAIN_FIELD.get(op)
        if known is not None:
            ok = (i in known) if isinstance(known, tuple) else (i == known)
            if not ok: continue              # this field is not the index
        enum = D.FIELD_ENUM.get(op, {}).get(i)
        if enum is not None and 0 <= v < len(enum):
            notes.append("%s[%d] = %r" % ("SCREEN", v, enum[v])); continue
        sec = D.FIELD_SECTION.get(op, {}).get(i)
        t = D.tag_in(sec, v) if sec else D.tag(op, v)
        if t:
            note = t.strip().lstrip("; ")
            notes.append(note if known is not None else note + " ?")
    return ", ".join(parts), ("   ; " + " | ".join(notes) if notes else "")


def listing(b, start, label=""):
    ops, st = D.disasm(b, start, len(b))
    out = ["", "  %s  @0x%x  [%s]" % (label, start, st)]
    targets = {struct.unpack("<h", o[2][:2])[0] for o in ops
               if o[1] in BRANCH and len(o[2]) >= 2}
    for pc, op, raw in ops:
        name = D.NAME.get(op) or "op_%d" % op
        val, note = operand_text(op, raw)
        mark = ">" if pc in targets else " "
        out.append("  %s %5d  %-16s %-14s%s" % (mark, pc, name, val, note))
    return "\n".join(out)


def scripts_of(arch, chunk):
    """-> (block, [(label, offset)])"""
    if arch == "GLOBAL":
        b, slots = global_file()
        return b, [("script %d" % i, p) for i, _f, p in slots]
    b = archive(os.path.join(omkdata.TAGDIR, arch))[chunk]
    fn = area_records if arch == "AREA" else scene_records
    r = fn(b)
    out = []
    if r:
        for rec, field, p in _scripts_from_records(b, r[0], r[1]):
            out.append(("record %d slot +%d" % (rec, field), p))
    for rec, field, p in _second_table(arch, b):
        out.append(("table entry %d" % rec, p))
    return b, out


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    arch = (args[0] if args else "GLOBAL").upper()
    chunk = int(args[1]) if len(args) > 1 else 0
    only = None
    if "--rec" in sys.argv: only = int(sys.argv[sys.argv.index("--rec") + 1])

    b, scripts = scripts_of(arch, chunk)
    print("%s%s — %d script%s" % (arch, "" if arch == "GLOBAL" else " chunk %d" % chunk,
                                  len(scripts), "" if len(scripts) == 1 else "s"))
    for i, (label, off) in enumerate(scripts):
        if only is not None and i != only: continue
        print(listing(b, off, label))

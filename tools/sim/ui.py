# SPDX-License-Identifier: GPL-3.0-or-later
"""The interface widget tree, walked with the engine's own input bits.

`ui.open` suspends a script and the screen answers it. Until now the simulator
supplied that answer as a literal, which tested the suspend/resume mechanism
and nothing else: the whole widget layer sat outside the harness. This models
it, so the answer is *derived* from button presses instead of asserted.

What is data and what is code, kept apart on purpose:

  * the **navigation** is data - the 92-byte screen record names an open
    callback, that callback installs a panel, and a panel's lists and items
    are static records in the image. All of it is read here (docs/UI.md 3b).
  * the **item callbacks** are native code and are NOT run. Like every other
    subsystem the simulator stubs, an unmodelled callback is logged rather
    than guessed. `ANSWER` holds the ones whose effect has been read, each
    with its evidence.

The dispatch follows `Ui_DispatchInput` (docs/UI.md 3c): the back and close
bits here, then the current list's own `+4` hook, then `Ui_MoveSelection` as
the default - which steps over every UIF_UNSELECTABLE item and, when no
direction was pressed, falls through to `Ui_ConfirmSelection`.
"""
import os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
import ui_tables as U
import omkpaths

# The input bits, out of the engine's 14 binding slots (docs/UI.md 3c).
LEFT, RIGHT, UP, DOWN = 1, 2, 4, 8
CONFIRM, BACK, CLOSE = 0x10, 0x20, 0x2000

UNSELECTABLE = 4          # item flags A (+48) bit 2 - UIF_UNSELECTABLE
NOWRAP = 0x80000          # list flags A (+16) - pin the selection at the ends

#: The list input hook that is NOT the default walk. One reference in the
#: whole image, and it is the LIFT screen's 7-item list (docs/UI.md 3c).
GRID_HOOK = 0x004B00D0

#: The other one: the start menu's name field. It reads the character channel
#: rather than the input bits and returns 0 for every bit, so a list carrying
#: it does not move on a direction - and since the hook exists, the default
#: walk is not reached either.
NAME_HOOK = 0x0047A390

#: Item callbacks whose effect on the answer has been read. Everything else
#: is logged as unmodelled.
ANSWER = {
    # `Confirmer` under the start menu's "Nouvelle partie". Its body carries
    # `mov dword_930750, 1` at 0x0047A354, and 1 is what the shipped save
    # records for the intro's `Interface` variable (verify.py: save file).
    0x0047A2B0: 1,
}

#: ...but that callback is GATED, and the gate is its FIRST instruction:
#:
#:      0047A2B0  mov  eax, [dword_657994]   ; the name field's cursor
#:      0047A2B6  test eax, eax
#:      0047A2B9  je   0x0047A35E            ; empty name -> straight to the ret
#:
#: On the empty-name path it writes nothing: not the answer, and not the
#: screen's state word at `+8` (`mov [esi+8], 3` at 0x0047A34D, which is what
#: closes the screen and lets `UI_SendAnswer` fire). So an empty field leaves
#: the screen OPEN and the calling script suspended at `ui.open` for ever.
#:
#: `NameField` already modelled the same emptiness rule for RETURN inside the
#: field; the engine enforces it a second time at the button, and THIS walker
#: answered regardless until a golden capture went looking (docs/UI.md 3f).
ANSWER_NEEDS_NAME = {0x0047A2B0}

#: The same callback has a second refusal one branch later: `sub_408CE0` looks
#: the typed name up in the save directory and, when it is already there,
#: 0x0047A328 shows a message and 0x0047A336 forces eax to -1, which the
#: `cmp eax, -1` at 0x0047A348 turns into the same silent no-answer. Not
#: modelled - it needs the directory - but it is the same shape: a duplicate
#: name cannot start a new game either.
ANSWER_NEEDS_UNIQUE_NAME = {0x0047A2B0}


def _hook_start_confirm(ui, bits):
    """`sub_0047A230` - the start menu's confirm dialog moves between its two
    lists with UP and DOWN, not left/right.

        current list is the name field (0x004CE890) and DOWN  -> panel+24 = 1
        current list is the buttons  (0x004CE948), the name field is not
          flagged unselectable, the selection is on "Confirmer", and UP
                                                              -> panel+24 = 0
    """
    ls = ui.lists(ui.panel)
    cur = ls[ui.cur] if 0 <= ui.cur < len(ls) else None
    if cur == 0x004CE890:
        if bits & DOWN:
            ui.cur = 1
            ui.log.append(("focus list", 1))
            return True
    elif cur == 0x004CE948:
        if (not (ui._u32(0x004CE890 + 16) & UNSELECTABLE)
                and ui.sel.get(cur) == 0 and bits & UP):
            ui.cur = 0
            ui.log.append(("focus list", 0))
            return True
    return False


#: `Ui_MoveBetweenLists` BOUND TO LEFT AND RIGHT, and the reason `_move_lists`
#: below stopped being dead code.
#:
#:     sub_42A710(screen, panel) { return sub_42A5C0(screen, panel, 1, 2); }
#:
#: `sub_42A5C0` is the mover itself and it takes the two bits as parameters -
#: `a3` steps back, `a4` steps on - so 1 and 2 are LEFT and RIGHT. It walks
#: `panel+24` (the current list) over the `panel+28` count and the `panel+32`
#: pointers, skipping a list whose `+16 & 4` is set and one with nothing
#: selectable in it, wrapping unless `panel+72 & 0x80000`. That is
#: `_move_lists` line for line - which was transcribed and then never called,
#: because until the sneak family entered the tree no panel in it named this
#: hook. The sneak device's inventory page does, and without it the tab column
#: down the left is unreachable: the player opens on the inventory rows and
#: can never leave them.
MOVE_LISTS_HOOK = 0x0042A710

#: ...and its LIST-level counterpart. `sub_42A930(screen, list)` is
#: `sub_42A7E0(screen, list, 1, 2)` - the same `Ui_MoveSelection` the default
#: dispatch reaches with `(4, 8)`, bound to LEFT and RIGHT instead of UP and
#: DOWN. The sneak's verb bar ("Utiliser" / "Utiliser sur" / "Examiner") and
#: the slider page's buttons name it, and both are rows that run across the
#: screen. Not a screen-specific hook at all, which is why it belongs beside
#: the other two rather than in a per-screen table.
MOVE_SELECTION_LR = 0x0042A930


def _hook_move_lists(ui, bits):
    """`sub_42A710` - LEFT and RIGHT move the panel's focus between lists."""
    if bits & LEFT:
        return ui._move_lists(-1)
    if bits & RIGHT:
        return ui._move_lists(1)
    return False


#: `panel 0x004DEDE8 + 16` - the SLIDER page's own list mover, where the
#: inventory page's is the generic `sub_42A710`. Hand-written, and it walks
#: `panel+24` between three of the page's four lists on a state machine.
SLIDER_LISTS_HOOK = 0x0049D4D0
SLIDER_TABS, SLIDER_HEAD, SLIDER_ROWS = 0x004DE210, 0x004DEA08, 0x004DE6F0


def _hook_slider_lists(ui, bits):
    """`sub_49D4D0` - the slider page cannot use the generic mover.

    Read out of the image; the transitions, with the panel's lists being
    [0] tabs, [1] header, [2] rows, [3] echo:

        from tabs    the 0x3 pair                 -> header, if selectable
        from header  0x1 at its first item, or
                     0x2 at its last              -> tabs, if selectable
        from header  the 0xC pair                 -> rows, if selectable,
                     0x4 selecting the LAST row and 0x8 the FIRST
        from rows    0x4 at the first row, or
                     0x8 at the last              -> header, if selectable
        from rows    the 0x3 pair                 -> tabs, if selectable

    Which pair is which on a keyboard follows this file's existing binding
    for `sub_42A710` rather than being re-derived: that hook is
    `sub_42A5C0(screen, panel, 1, 2)` and is driven here from LEFT/RIGHT.
    """
    ls = ui.lists(ui.panel)
    idx = {a: i for i, a in enumerate(ls)}
    tabs = idx.get(SLIDER_TABS)
    head = idx.get(SLIDER_HEAD)
    rows = idx.get(SLIDER_ROWS)
    if tabs is None or head is None or rows is None:
        return False

    def ok(k):
        return k is not None and ui._usable(ls[k])

    def go(k):
        ui.cur = k
        ui.log.append(("focus list", k))
        return True

    cur = ui.cur
    sel = ui.sel.get(ls[cur], 0)
    last = len(ui.items(ls[cur])) - 1
    list_axis, cross_axis = bits & (LEFT | RIGHT), bits & (UP | DOWN)
    if cur == tabs:
        return go(head) if (list_axis and ok(head)) else False
    if cur == head:
        if ((bits & LEFT) and sel == 0) or ((bits & RIGHT) and sel == last):
            if ok(tabs):
                return go(tabs)
        if cross_axis and ok(rows):
            ui.sel[ls[rows]] = (len(ui.items(ls[rows])) - 1) if (bits & UP) else 0
            return go(rows)
        return False
    if cur == rows:
        if ((bits & UP) and sel == 0) or ((bits & DOWN) and sel == last):
            if ok(head):
                return go(head)
        return go(tabs) if (list_axis and ok(tabs)) else False
    return False


#: Panel-level input hooks (`panel+16`) that have been read. Anything else is
#: logged and the walk is marked approximate.
PANEL_HOOKS = {0x0047A230: _hook_start_confirm,
               MOVE_LISTS_HOOK: _hook_move_lists,
               SLIDER_LISTS_HOOK: _hook_slider_lists}



def _s16(v):
    """A pushed immediate as the int16 the layout helpers actually store.

    `sub_429680` and friends take `__int16` parameters and the callers push a
    dword, so a negative offset arrives as 0xFFFF.... Reading it unsigned puts
    a widget 65 thousand pixels down the screen.
    """
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Ui:
    """One open screen, driven by input words."""

    def __init__(self, exe=None):
        self.e = exe or U.Exe()
        self.screens = {s["id"]: s for s in U.screens(self.e)}
        self.reset()

    # ---------------------------------------------------------------- data
    def _u32(self, va):
        return struct.unpack_from("<I", self.e.read(va, 4), 0)[0]

    def _i16(self, va):
        return struct.unpack_from("<h", self.e.read(va, 2), 0)[0]

    def _u8(self, va):
        return self.e.read(va, 1)[0]

    def _i8(self, va):
        v = self.e.read(va, 1)[0]
        return v - 256 if v > 127 else v

    #: `Ui_OpenSneakFamily` (0x0049B400) is the second open callback whose
    #: body BRANCHES on the screen's `+4` parameter, and the only one whose
    #: PANEL does. Decoded from the image's own bytes at 0x0049B400:
    #:
    #:      49B409  8B 46 04           mov  eax, [esi+4]   ; the parameter
    #:      49B40C  2B C3              sub  eax, ebx       ; ebx = 0
    #:      49B40E  0F 84 B0 00 00 00  jz   0x0049B4C4     ; 0 SNEAK
    #:      49B414  48                 dec  eax
    #:      49B415  74 62              jz   0x0049B479     ; 1 SLIDER
    #:      49B417  48                 dec  eax
    #:      49B418  0F 85 B8 01 00 00  jnz  0x0049B5D6     ; the shared tail
    #:      49B41E                                         ; 2 VIDEOPHONE
    #:
    #: so the three arms are [0x49B41E, 0x49B479) = VIDEOPHONE,
    #: [0x49B479, 0x49B4C4) = SLIDER and [0x49B4C4, 0x49B5D6) = SNEAK, and
    #: everything from 0x0049B5D6 is shared. Each arm writes its own
    #: `mov [esi+0x1C], imm32`, so a linear scan finds all three and cannot
    #: say which arm each belongs to - which is why this raised on the family
    #: and why screens 0, 7 and 9 were absent from the tree. Same shape as
    #: `SHOP_TITLE`, and the same fix: record the branch, then CHECK the scan
    #: against it rather than trusting either alone.
    SNEAK_OPEN = 0x0049B400
    SNEAK_ARM = {0: (0x0049B4C4, 0x0049B5D6),    # SNEAK      - the inventory
                 1: (0x0049B479, 0x0049B4C4),    # SLIDER     - call a slider
                 2: (0x0049B41E, 0x0049B479)}    # VIDEOPHONE
    SNEAK_BODY = 0x0049B41E          # where the branch ends and arm 2 begins
    SNEAK_TAIL, SNEAK_END = 0x0049B5D6, 0x0049B607
    SNEAK_PANEL = {0: 0x004DEE50, 1: 0x004DEDE8, 2: 0x004DF128}

    def panel_of(self, screen_id):
        """The panel a screen's open callback installs.

        Every open callback writes it with one `mov [reg+0x1C], imm32` -
        screen+0x1C is UISCR_PANEL - so the address is recoverable without
        running anything. 28 of the 32 live screens give exactly one; the
        sneak family gives three, one per arm of its `+4` branch, and those
        are resolved through `SNEAK_PANEL` above. SHOOT MECA names both shoot
        panels and is still not modelled here.
        """
        s = self.screens[screen_id]
        d = self.e.read(s["cb"][0], 320)
        hits = []
        for i in range(len(d) - 7):
            if d[i] == 0xC7 and 0x40 <= d[i + 1] <= 0x47 and d[i + 2] == 0x1C:
                v = struct.unpack_from("<I", d, i + 3)[0]
                if 0x4C0000 <= v < 0x700000 and v not in hits:
                    hits.append(v)
        if s["cb"][0] == self.SNEAK_OPEN and s["param"] in self.SNEAK_PANEL:
            # The scan is the check on the transcription, not the source of
            # it: if the image ever held a different set of three, the branch
            # map is wrong and this must say so rather than pick from it.
            if set(hits) != set(self.SNEAK_PANEL.values()):
                raise ValueError(
                    "the sneak family installs %s, not the three recorded %s"
                    % ([hex(h) for h in hits],
                       [hex(h) for h in sorted(self.SNEAK_PANEL.values())]))
            return self.SNEAK_PANEL[s["param"]]
        if len(hits) != 1:
            raise ValueError("screen %d installs %d candidate panels: %s"
                             % (screen_id, len(hits), [hex(h) for h in hits]))
        return hits[0]

    #: `I2D_SetFlagOnAllRows(list, flag, on)` and `I2D_SetFlag(item, flag, on)`.
    #: A screen's open callback sets flags the item records do NOT carry - the
    #: start menu centres its four buttons with one broadcast of 0x80000010
    #: rather than storing it - so reading the static record alone gets the
    #: alignment wrong. These are recovered from the callback's bytes the same
    #: way `Opt_BindRow`'s bindings are.
    #: ...and a THIRD helper the first version of this scan did not know,
    #: which silently dropped **41** edits across eleven screens. All three
    #: have the same (target, flag, on) shape and differ only in the record
    #: they write, which is why one can be missed without anything looking
    #: wrong: `I2D_SetFlag` writes an ITEM's +48/+52/+56, `I2D_SetListFlag`
    #: writes a LIST's +16/+20, and `I2D_SetFlagOnAllRows` walks a list and
    #: applies the flag to every row. The shops make four list-level edits
    #: each and MULTIPLAN one; none of them was in the table until 2026-09-01.
    BROADCAST, SETFLAG, LISTFLAG = 0x00429140, 0x00428FF0, 0x004290D0
    # The LAYOUT helpers, and the static records are not the final geometry
    # without them. Each walks a list's items and writes one field:
    #
    #   sub_4295C0(list, x)             every item's +0  - the X
    #   sub_429650(list, h)             every item's +6  - the HEIGHT
    #   sub_429680(list, firstY, step)  every item's +2  - the Y, stepping
    #
    # Screen 29's open callback lays out five lists this way. Its start menu
    # goes to y 120 step 80 while the records say 150 step 60 - and the
    # engine's own capture has its four labels at 127/208/290/370, which is
    # the callback's spacing and not the record's. Its confirm dialog's
    # `Confirmer`/`Annuler` both ship at y=330 and are laid out to 260 and
    # 320; drawn from the records alone they land on top of each other, which
    # is what a player saw.
    SETX, SETH, LAYOUTY = 0x004295C0, 0x00429650, 0x00429680

    #: `Ui_OpenShop` is the one open callback whose flag edits BRANCH, and the
    #: branch is the same `+4` parameter its title switch uses: `test eax, eax`
    #: at 0x004AE5DA jumps to 0x004AE66A when the parameter is 0, which is
    #: BANK. The two arms are exact mirrors - the "Acheter" and "Vendre" rows
    #: swap - so a linear scan sees every flag set BOTH ways and records a
    #: contradiction: 20 items (2 rows x 10 screens) carried `[f, False]` and
    #: `[f, True]` together, and they were the only contradictions in the whole
    #: tree. Same shape as the shop TITLES, and the same fix: follow the
    #: branch instead of walking through it.
    SHOP_OPEN = 0x004AE540
    SHOP_TEST, SHOP_BANK, SHOP_JOIN = 0x004AE5DA, 0x004AE66A, 0x004AE6ED

    def open_flags(self, screen_id):
        """-> ({list broadcast}, {item}, {list}, {list layout}) from the callback.

        Each maps a target address to `[(flag, on), ...]`.

        `Ui_OpenShop` and `Ui_OpenSneakFamily` are handled by following their
        parameter branch rather than walking through every arm - see
        `SHOP_TEST` and `SNEAK_ARM`. Every other open callback is a straight
        line to its first `ret`, which is why the twenty contradictions the
        shops resolve were all on that one function.

        The sneak family contradicts itself the same way and on the SAME
        list: all three arms call `I2D_SetListFlag(0x004DE210, 0x20000004, x)`
        on the tab column, with `x` = 0 for SNEAK and 1 for the other two -
        so a linear scan records the column both hidden and shown, and a
        reader taking either would draw SNEAK without its tabs or SLIDER with
        them.
        """
        s = self.screens[screen_id]
        fn = s["cb"][0]
        if not fn:
            return {}, {}, {}, {}
        # The BYTE WINDOWS to walk, and the arms not taken inside them.
        #
        # Two mechanisms because the two branching callbacks need different
        # ones. The shops are one straight run with a hole in the middle, so
        # a skip range over the whole body is enough. The sneak family is
        # not: `sub eax, ebx` at 0x0049B40C assembles as `2B C3`, and the
        # `ret`-stop below sees that 0xC3 as the end of the function and
        # returns an EMPTY table - which is what it did, silently, for all
        # three screens. Walking the taken arm and the shared tail as
        # explicit windows removes both the stray `ret` and the need to skip.
        # (base, size, stop at the first `ret`, follow register constants)
        windows, skips = [(fn, 1400, True, False)], []
        # BANK is parameter 0; the other nine take the fall-through arm.
        if fn == self.SHOP_OPEN:
            if s["param"] == 0:
                skips.append((self.SHOP_TEST, self.SHOP_BANK))    # the nine
            else:
                skips.append((self.SHOP_BANK, self.SHOP_JOIN))    # BANK's
        if fn == self.SNEAK_OPEN and s["param"] in self.SNEAK_ARM:
            lo, hi = self.SNEAK_ARM[s["param"]]
            # The PROLOGUE comes first and carries no call: it is walked only
            # so `xor ebx, ebx` is seen before the arm pushes ebx. It has to
            # stop at `SNEAK_BODY`, not at the arm - taking it up to arm 0's
            # 0x0049B4C4 swallows the two arms in between and hides the tab
            # column on every screen of the family.
            windows = [(fn, self.SNEAK_BODY - fn, False, True),
                       (lo, hi - lo, False, True),
                       (self.SNEAK_TAIL, self.SNEAK_END - self.SNEAK_TAIL,
                        False, True)]

        def blocked(addr):
            return any(lo <= addr < hi for lo, hi in skips)

        per_list, per_item, per_page = {}, {}, {}
        layout = {}
        regs = {}                       # eax..edi, when a constant is known
        for base, size, stop_at_ret, track in windows:
            d = self.e.read(base, size)
            pushes, i = [], 0
            while i < len(d) - 4:
                op = d[i]
                if op == 0x68:
                    pushes.append(struct.unpack_from("<I", d, i + 1)[0])
                    i += 5; continue
                if op == 0x6A:
                    pushes.append(
                        struct.unpack_from("<b", d, i + 1)[0] & 0xFFFFFFFF)
                    i += 2; continue
                # `push ebx` / `push edi` - and the sneak family passes EVERY
                # `on` flag that way, which is why its table came back empty
                # of flags even once the windows were right. Only the two
                # forms that actually appear are decoded (`xor r, r` and
                # `mov r32, imm32`); a register with no known constant pushes
                # None, and a call taking one is dropped rather than guessed.
                if track and 0x50 <= op <= 0x57:
                    pushes.append(regs.get(op - 0x50)); i += 1; continue
                if track and op == 0x33 and (d[i + 1] & 0xC0) == 0xC0 \
                        and ((d[i + 1] >> 3) & 7) == (d[i + 1] & 7):
                    regs[d[i + 1] & 7] = 0; i += 2; continue
                if track and 0xB8 <= op <= 0xBF:
                    regs[op - 0xB8] = struct.unpack_from("<I", d, i + 1)[0]
                    i += 5; continue
                if op == 0xE8:
                    t = base + i + 5 + struct.unpack_from("<i", d, i + 1)[0]
                    here = base + i
                    known = None not in pushes[-3:]
                    if (t in (self.BROADCAST, self.SETFLAG, self.LISTFLAG)
                            and len(pushes) >= 3 and known
                            and not blocked(here)):
                        target, flag, on = pushes[-1], pushes[-2], pushes[-3]
                        bucket = (per_list if t == self.BROADCAST else
                                  per_item if t == self.SETFLAG else per_page)
                        bucket.setdefault(target, []).append((flag, bool(on)))
                    elif (t in (self.SETX, self.SETH) and len(pushes) >= 2
                            and None not in pushes[-2:] and not blocked(here)):
                        lst, v = pushes[-1], pushes[-2]
                        layout.setdefault(lst, {})[
                            "x" if t == self.SETX else "h"] = _s16(v)
                    elif (t == self.LAYOUTY and len(pushes) >= 3 and known
                            and not blocked(here)):
                        lst, first, step = pushes[-1], pushes[-2], pushes[-3]
                        layout.setdefault(lst, {})["firstY"] = _s16(first)
                        layout.setdefault(lst, {})["stepY"] = _s16(step)
                    pushes = []; i += 5; continue
                if op == 0xC3 and stop_at_ret:
                    break
                i += 1
        return per_list, per_item, per_page, layout

    def open_binds(self, screen_id, items):
        """-> {item: {"string": id, "tag": n}} the open callback WRITES.

        The flags are not all a callback does. It also binds each item's text
        and its tag - `TERMINAL` gives its five rows string ids 5..9 and tags
        1..4 - and those are not in the item record: they ship as -1 and the
        callback writes them, so a reader that trusts the record alone finds a
        screen with no labels at all.

        `items` is the sorted list of known item addresses. Resolving against
        them is what keeps the scan honest: a linear walk over a callback's
        bytes finds stores to plenty of globals, and only those landing inside
        an item's own 72 bytes are bindings - 119 of 171 stores do, and the
        rest are panels, screen records and other state.

        **Two encodings, and getting the first wrong yields plausible
        rubbish.** `+28` is an int16, so it is written with the operand-size
        prefix - `66 C7 05 <abs32> <imm16>` - while `+60` is a dword and uses
        `C7 05 <abs32> <imm32>`. Scanning for the dword form alone matches
        INSIDE the 16-bit one: the target still resolves, but the value reads
        four bytes where there are two, and TERMINAL's string 5 comes back as
        3345350661.

        Same linear scan as `open_flags`, with the same limits: it stops at the
        first `ret` and does not follow branches.
        """
        s = self.screens[screen_id]
        fn = s["cb"][0]
        if not fn or not items:
            return {}
        def at(addr):
            for a in items:
                if a <= addr < a + 0x48:
                    return a, addr - a
            return None, None
        d = self.e.read(fn, 1400)
        out, i = {}, 0
        while i < len(d) - 12:
            if d[i] == 0x66 and d[i + 1] == 0xC7 and d[i + 2] == 0x05:
                tgt = struct.unpack_from("<I", d, i + 3)[0]
                val = struct.unpack_from("<H", d, i + 7)[0]
                a, off = at(tgt)
                if a is not None and off == 28:
                    out.setdefault(a, {})["string"] = val
                i += 9
                continue
            if d[i] == 0xC7 and d[i + 1] == 0x05:
                tgt, val = struct.unpack_from("<II", d, i + 2)
                a, off = at(tgt)
                if a is not None and off == 60:
                    out.setdefault(a, {})["tag"] = val
                i += 10
                continue
            if d[i] == 0xC3:
                break
            i += 1
        return out

    def open_state(self, screen_id, panels, lists):
        """-> ({panel: current list}, {list: selected row}) the callback SETS.

        Two fields this lift recorded as unknowable, because until the sneak
        family every open callback left them alone:

        * **`panel+24`** is the panel's CURRENT LIST. `_settle` says of it -
          correctly, for the twenty-eight screens it was written for - that it
          is "runtime state the panel's builder sets, and every builder is
          native code the simulator does not run", so the walk falls back on
          `Ui_MoveBetweenLists`'s own rule: the first list that is not hidden
          and has something selectable in it.
        * **`list+2`** is that list's SELECTED ROW, the int16 after the item
          count at `+0`.

        `Ui_OpenSneakFamily` writes both, in the open callback itself, and
        they are what decides which page of the device comes up:

            SNEAK       panel 0x004DEE50 +24 = 3   the nine inventory rows
                        list  0x004DE6F0 +2  = 0   its first row
                        list  0x004DE210 +2  = 2   the tab column on
                                                   "Inventaire"
            SLIDER      panel 0x004DEDE8 +24 = 1
                        list  0x004DEA08 +2  = 1
                        list  0x004DE210 +2  = 1   ..."Appel du slider"
            VIDEOPHONE  panel 0x004DF128 +24 = 1

        Without them the walk opens the sneak on the TAB COLUMN rather than on
        the page, and the column's own highlight sits on "Identite" - the
        first selectable row - instead of on the page the player asked for.

        The same four encodings as `open_binds`, plus the two REGISTER-source
        forms, because this callback writes 0 and 1 out of `ebx` and `edi`:

            C7 05 <abs32> <imm32>       mov [abs], imm32
            66 C7 05 <abs32> <imm16>    mov [abs], imm16
            89 <reg|05> <abs32>         mov [abs], r32
            66 89 <reg|05> <abs32>      mov [abs], r16

        Resolved against the KNOWN panel and list addresses, the way
        `open_binds` resolves against the known items: a linear walk over a
        callback finds stores to plenty of globals, and only the ones landing
        on a real record at the right offset are these.
        """
        s = self.screens[screen_id]
        fn = s["cb"][0]
        if not fn:
            return {}, {}
        panels, lists = set(panels), set(lists)
        windows = [(fn, 1400, True, False)]
        if fn == self.SNEAK_OPEN and s["param"] in self.SNEAK_ARM:
            lo, hi = self.SNEAK_ARM[s["param"]]
            windows = [(fn, self.SNEAK_BODY - fn, False, True),
                       (lo, hi - lo, False, True),
                       (self.SNEAK_TAIL, self.SNEAK_END - self.SNEAK_TAIL,
                        False, True)]
        cur, sel, regs = {}, {}, {}

        def store(tgt, val):
            if tgt - 24 in panels:
                cur[tgt - 24] = val
            elif tgt - 2 in lists:
                sel[tgt - 2] = val

        for base, size, stop_at_ret, track in windows:
            d = self.e.read(base, size)
            i = 0
            while i < len(d) - 10:
                if d[i] == 0x66 and d[i + 1] == 0xC7 and d[i + 2] == 0x05:
                    store(struct.unpack_from("<I", d, i + 3)[0],
                          struct.unpack_from("<H", d, i + 7)[0])
                    i += 9; continue
                if d[i] == 0xC7 and d[i + 1] == 0x05:
                    tgt, val = struct.unpack_from("<II", d, i + 2)
                    store(tgt, val)
                    i += 10; continue
                if track and d[i] == 0x66 and d[i + 1] == 0x89 \
                        and (d[i + 2] & 0xC7) == 0x05:
                    r = regs.get((d[i + 2] >> 3) & 7)
                    if r is not None:
                        store(struct.unpack_from("<I", d, i + 3)[0], r & 0xFFFF)
                    i += 7; continue
                if track and d[i] == 0x89 and (d[i + 1] & 0xC7) == 0x05:
                    r = regs.get((d[i + 1] >> 3) & 7)
                    if r is not None:
                        store(struct.unpack_from("<I", d, i + 2)[0], r)
                    i += 6; continue
                if track and d[i] == 0x33 and (d[i + 1] & 0xC0) == 0xC0 \
                        and ((d[i + 1] >> 3) & 7) == (d[i + 1] & 7):
                    regs[d[i + 1] & 7] = 0; i += 2; continue
                if track and 0xB8 <= d[i] <= 0xBF:
                    regs[d[i] - 0xB8] = struct.unpack_from("<I", d, i + 1)[0]
                    i += 5; continue
                if d[i] == 0xC3 and stop_at_ret:
                    break
                i += 1
        return cur, sel

    # `Ui_OpenShop` serves ten screens and switches on the screen's fixed
    # `+8` parameter through this jump table; each of the ten targets is one
    # `mov word ptr [0x004E37CC], imm16` - the title string in `IAM\Buy`, on
    # the item the ten screens share.
    SHOP_JUMP = 0x004AE7AC
    SHOP_TITLE_FIELD = 0x004E37CC

    def shop_titles(self):
        """-> {param: string id} for the ten shops.

        **A linear scan cannot get this right, and gets it wrong quietly.** It
        walks straight through the jump table's ten arms and keeps whichever
        `mov` it saw last, so all ten shops come back bound to string 19 -
        "Bibliotheque de Lahoreh" - and nine screens would show the tenth's
        title. Following the table is the only way to tell them apart.
        """
        out = {}
        for p in range(10):
            tgt = struct.unpack_from("<I", self.e.read(self.SHOP_JUMP + 4 * p, 4))[0]
            d = self.e.read(tgt, 16)
            if d[0] == 0x66 and d[1] == 0xC7 and d[2] == 0x05:
                addr = struct.unpack_from("<I", d, 3)[0]
                if addr == self.SHOP_TITLE_FIELD:
                    out[p] = struct.unpack_from("<H", d, 7)[0]
        return out

    def item_flags(self, item, lst):
        """The item's three flag words with the open callback's changes applied.

        -> (bankA, bankB, bankC). A flag constant carries its bank in the top
        bits, so which word a broadcast lands in is decided by the constant.
        """
        w = [self._u32(item + 48), self._u32(item + 52), self._u32(item + 56)]
        for flag, on in (self._oflags[0].get(lst, []) + self._oflags[1].get(item, [])):
            bank = (0 if flag & 0x20000000 else
                    1 if flag & 0x40000000 else
                    2 if flag & 0x80000000 else None)
            if bank is None:
                continue
            bit = flag & 0x1FFFFFFF
            w[bank] = (w[bank] | bit) if on else (w[bank] & ~bit)
        return tuple(w)

    def lists(self, panel):
        n = self._i16(panel + 28)
        return [self._u32(panel + 32 + 4 * k) for k in range(max(0, n))]

    def items(self, lst):
        n = self._i16(lst)
        arr = self._u32(lst + 12)
        return [self._u32(arr + 4 * j) for j in range(max(0, n))]

    def selectable(self, item, lst=None):
        """`UIF_UNSELECTABLE` on the item's EFFECTIVE flags.

        This read the static `+48` raw until 2026-09-01, and so did the port -
        both deliberately, because a linear scan of a branching open callback
        recorded the shops' two rows as set BOTH ways and applying that in
        address order would grey whichever arm came last. Now that
        `open_flags` follows `Ui_OpenShop`'s parameter branch there are no
        contradictory edits left in the tree, so the edits can simply be
        applied - which is what makes the ten shops show the right one of
        "Acheter" and "Vendre" instead of both.
        """
        w = (self.item_flags(item, lst) if lst is not None
             else (self._u32(item + 48), 0, 0))
        return not (w[0] & UNSELECTABLE)

    # -------------------------------------------------------------- driving
    def reset(self):
        self.panel = None
        self.cur = 0
        self.approx = False
        self._oflags = ({}, {}, {})
        # what the open callback wrote into `panel+24` and `list+2`
        self._ocur, self._osel = {}, {}
        self.answer = None
        #: The name field's contents, as `byte_69BDA0` would hold them. The
        #: engine's builder for the "Nouvelle partie" panel (0x0047A050)
        #: clears that buffer and zeroes the cursor on entry, so EMPTY is the
        #: right starting state - and `ANSWER_NEEDS_NAME` reads it.
        self.name = ""
        self.log = []

    def type_name(self, text):
        """Type into the name field, capped where the buffer is."""
        for ch in text:
            if len(self.name) >= NAME_MAX:
                break
            self.name += ch
        return self.name

    def open(self, screen_id):
        self.reset()
        self._oflags = self.open_flags(screen_id)
        self.panel = self.panel_of(screen_id)
        # ...and what the open callback WRITES into `panel+24` and `list+2`.
        # Collected over the panels reachable from this one, because a `+44`
        # descent can land on a page the callback also set up - the sneak's
        # open sets three panels' state, not just the one it installs.
        reach, stack = {self.panel}, [self.panel]
        while stack:
            pn = stack.pop(0)
            for l in self.lists(pn):
                for it in self.items(l):
                    kid = self._u32(it + 44)
                    if kid and kid not in reach:
                        reach.add(kid); stack.append(kid)
        self._ocur, self._osel = self.open_state(
            screen_id, reach, {l for pn in reach for l in self.lists(pn)})
        self.log.append(("open", screen_id, self.panel))
        self._settle()
        return self.panel

    def _cur_list(self):
        ls = self.lists(self.panel)
        return ls[self.cur] if 0 <= self.cur < len(ls) else None

    def _usable(self, lst):
        """`Ui_MoveBetweenLists`'s own predicate: not hidden, and something in
        it can be selected."""
        if self._u32(lst + 16) & 4:
            return False
        return any(self.selectable(it, lst) for it in self.items(lst))

    def _settle(self):
        """Entering a panel: pick the current list and each list's selection.

        `panel+24` on disk is not the answer - it is runtime state, and for
        most screens the builder that writes it is native code the simulator
        does not run. What IS in the data is the rule the engine itself uses
        to move between lists, so that is the FALLBACK: the first list that is
        not hidden and has something selectable in it. For the start menu's
        confirm dialog that is list 1 ("Confirmer" / "Annuler"), not the list
        0 the disk image names.

        **Where the OPEN CALLBACK writes them, they are read instead** -
        `open_state` above. Fifteen panels set `+24` and eight lists set
        `+2`, and for the sneak it is the difference between opening on the
        inventory page with "Inventaire" lit and opening on the tab column
        with "Identite" lit.
        """
        ls = self.lists(self.panel)
        cur = self._ocur.get(self.panel, -1)
        self.cur = (cur if 0 <= cur < len(ls) else
                    next((k for k, l in enumerate(ls) if self._usable(l)), 0))
        self.sel = {}
        for lst in ls:
            its = self.items(lst)
            j = self._osel.get(lst, -1)
            self.sel[lst] = (j if 0 <= j < len(its) else
                             next((k for k, it in enumerate(its)
                                   if self.selectable(it, lst)), 0))

    def selected(self):
        lst = self._cur_list()
        if lst is None:
            return None
        its = self.items(lst)
        j = self.sel.get(lst, 0)
        return its[j] if 0 <= j < len(its) else None

    def label(self, item, textfile):
        """The item's string out of the screen's own `IAM\\<file>`."""
        s = self._i16(item + 28)
        if s < 0:
            return None
        S = U.iam_strings(textfile)
        return S[s] if s < len(S) else None

    def press(self, bits):
        """One frame of `Ui_DispatchInput`. -> True if the frame was consumed."""
        if self.panel is None:
            return False
        if bits & BACK:
            parent = self._u32(self.panel)
            if parent:
                self.panel = parent
                self._settle()
                self.log.append(("back", parent))
            else:
                self.panel = None
                self.log.append(("close",))
            return True
        hook = self._u32(self.panel + 16)
        if hook:
            fn = PANEL_HOOKS.get(hook)
            if fn is None:
                # The engine falls through only when the hook returns 0, and
                # this cannot know. Fall through, but say the run is no longer
                # exact so a caller can refuse to trust the answer.
                self.approx = True
                self.log.append(("unmodelled panel hook", hex(hook)))
            elif fn(self, bits):
                return True
        lst = self._cur_list()
        if lst is None:
            return False
        hook = self._u32(lst + 4)
        if hook == GRID_HOOK:
            return self._grid(lst, bits)
        if hook == NAME_HOOK:
            # The name field answers the CHARACTER channel (sub_4397B0), not
            # the input bits, and returns 0 for everything here - so the list
            # simply does not respond to a direction, and because the hook
            # exists `Ui_MoveSelection` is not reached either. Type into it
            # with `NameField`; DOWN off it is the panel hook's job.
            self.log.append(("name field: no bit response",))
            return False
        if hook == MOVE_SELECTION_LR:
            return self._move(lst, bits, LEFT, RIGHT)
        if hook:
            self.approx = True
            self.log.append(("unmodelled list hook", hex(hook)))
            return False
        return self._move(lst, bits)

    def _grid(self, lst, bits):
        """`UI_GridMenuInput` (0x004B00D0) transcribed - the LIFT's floor panel.

        Six slots in a 3-wide, 2-deep grid plus one standing apart at 6, which
        the ITEM COORDINATES show independently: x 278/321/370 on two rows at
        y 194 and 241, then slot 6 alone at (325, 288), centred under the
        middle column. The `% 3` is the three columns.

        Confirm writes the answer itself rather than going through an item
        callback - `dword_930750 = slot - 1`, and slot 0 gives 6 - so the
        answer is the slot rotated by one: slot 1 ("Niveau 0", the entrance)
        answers 0, and slot 0 ("Niveau 1", the only floor above ground)
        answers 6. All 18 `ui.open 4` sites store it in variable 496, `Etage`.
        """
        n = self.sel[lst]
        before = n
        if bits & UP:
            n = 6 if n < 3 else (4 if n == 6 else n - 3)
        elif bits & DOWN:
            n = n + 3 if n < 3 else (1 if n == 6 else 6)
        elif bits & LEFT:
            if n != 6:
                n = n - 1 if n % 3 else n + 2
        elif bits & RIGHT:
            if n != 6:
                n = n - 2 if n % 3 == 2 else n + 1
        self.sel[lst] = n
        if n != before:
            self.log.append(("move", n))
        if bits & CONFIRM:
            self.answer = n - 1 if n else 6
            self.log.append(("answer", self.answer, "grid slot %d" % n))
        return n != before or bool(bits & CONFIRM)

    def _move_lists(self, step):
        """`Ui_MoveBetweenLists`: left/right move the panel's focus between
        lists, skipping a hidden one or one with nothing selectable in it."""
        ls = self.lists(self.panel)
        if not ls:
            return False
        nowrap = self._u32(self.panel + 72) & NOWRAP
        k = self.cur
        for _ in range(len(ls)):
            k += step
            if k < 0:
                k = 0 if nowrap else len(ls) - 1
            elif k >= len(ls):
                k = len(ls) - 1 if nowrap else 0
            if self._usable(ls[k]):
                break
        moved = k != self.cur
        self.cur = k
        self.log.append(("focus list", k))
        return moved

    def _move(self, lst, bits, back=UP, on=DOWN):
        """`sub_42A7E0(screen, list, a3, a4)` - `Ui_MoveSelection`, whose two
        direction bits are PARAMETERS.

        The default dispatch passes `(4, 8)` - UP and DOWN - and `sub_42A930`,
        the hook the sneak's verb bar and the slider page name, passes
        `(1, 2)`: LEFT and RIGHT, because those rows of buttons run across the
        screen rather than down it. One function, two bindings.
        """
        its = self.items(lst)
        if not its:
            return False
        n = len(its)
        step = -1 if bits & back else (1 if bits & on else 0)
        if step:
            nowrap = self._u32(lst + 16) & NOWRAP
            j = self.sel[lst]
            for _ in range(n):                    # skip unselectable items
                j += step
                if j < 0:
                    j = 0 if nowrap else n - 1
                elif j >= n:
                    j = n - 1 if nowrap else 0
                if self.selectable(its[j], lst):
                    break
            self.sel[lst] = j
            self.log.append(("move", j))
            return True
        if bits & CONFIRM:
            return self._confirm(its[self.sel[lst]])
        return False

    def _confirm(self, item):
        """`Ui_ConfirmSelection`: the +40 callback, else the +44 panel."""
        cb = self._u32(item + 40)
        if cb:
            if cb in ANSWER:
                if cb in ANSWER_NEEDS_NAME and not self.name:
                    # The engine's own first instruction. Nothing is written
                    # and the screen stays open, so this is NOT an answer and
                    # not a refusal to model either - it is the modelled
                    # behaviour of confirming with an empty field.
                    self.log.append(("no answer: name field empty", hex(cb)))
                    return True
                self.answer = ANSWER[cb]
                self.log.append(("answer", self.answer, hex(cb)))
            else:
                self.log.append(("unmodelled item callback", hex(cb)))
            return True
        nxt = self._u32(item + 44)
        if nxt:
            self.panel = nxt
            self._settle()
            self.log.append(("enter", hex(nxt)))
            return True
        return False

# --------------------------------------------------------------- the options
#: The options screen is not like the others. Its "panel" is one of thirteen
#: PAGE records and every page fills the SAME sixteen row widgets - so what a
#: page shows is not in the row, it is in the calls its builder makes to
#: `Opt_BindRow(row, item, page)`. Those calls are recovered from the builder's
#: bytes: `push page; push item; push row; call 0x00490F90`.
OPT_PAGES = (0x004DD3D0, 104, 13)
OPT_ROWS = (0x004DCDE8, 72, 16)
OPT_TABLE = (0x004DA574, 140)
OPT_BINDROW = 0x00490F90

#: Rows a builder binds inside a branch, so the linear scan sees only one of
#: the two item ids. Read from the assembly instead; keyed (page, row).
OPT_BRANCH = {
    # `Opt_PageRoot` binds row 4 to item 72 ("Retour") only when the screen's
    # parameter is 1 - `sub eax,0 / jz` in 0x00491200 - and to -1 otherwise.
    # The linear scan sees the -1 arm.
    (1, 4): "Retour when the screen parameter is 1",
}


class OptionsUi:
    """Screen 35, walked page by page.

    The value-changing rules are `sub_492DA0`, the live page's own input hook:

        type 0 / 4 (choice, device)   LEFT steps the choice back, RIGHT and
                                      CONFIRM step it forward, both wrapping
        type 1     (slider)           LEFT -10 (floor 0), RIGHT +10
        type 2     (header)           nothing here - the generic
                                      `Ui_ConfirmSelection` enters its +44 page
        type 5     (defaults)         CONFIRM restores from a compiled table
        type 6     (back)             CONFIRM at the root returns to screen 29

    A choice's index lives in the option record's +8 and its values in +92, so
    changing one is observable in the same table `tools/ui_tables.py options`
    prints. The model keeps its own copy rather than writing the image.
    """

    def __init__(self, exe=None):
        self.e = exe or U.Exe()
        self.opt = {o["index"]: o for o in U.options(self.e)}
        self.bind, self.branch = {}, {}
        base, stride, n = OPT_PAGES
        for k in range(n):
            fn = self._u32(base + stride * k + 4)
            self.bind[k], self.branch[k] = self._scan(fn)
        self.page = None
        self.cur = {}                # option index -> chosen index, when moved

    def _u32(self, va):
        return struct.unpack_from("<I", self.e.read(va, 4), 0)[0]

    def _scan(self, fn, cap=1500):
        """The builder's `Opt_BindRow` calls, in order. -> {row: (item, page)}"""
        rbase, rstride, nrows = OPT_ROWS
        rows = {rbase + rstride * k: k for k in range(nrows)}
        d = self.e.read(fn, cap)
        out, pushes, i = {}, [], 0
        multi = set()
        while i < len(d) - 4:
            op = d[i]
            if op == 0x68:
                pushes.append(struct.unpack_from("<i", d, i + 1)[0]); i += 5; continue
            if op == 0x6A:
                pushes.append(struct.unpack_from("<b", d, i + 1)[0]); i += 2; continue
            if op == 0xE8:
                t = fn + i + 5 + struct.unpack_from("<i", d, i + 1)[0]
                if t == OPT_BINDROW and len(pushes) >= 3:
                    page, item, row = pushes[-3], pushes[-2], pushes[-1]
                    row &= 0xFFFFFFFF
                    if row in rows:
                        k = rows[row]
                        if k in out and out[k][0] != item:
                            multi.add(k)
                        out[k] = (item, page & 0xFFFFFFFF)
                pushes = []; i += 5; continue
            if op == 0xC3:
                break
            i += 1
        return out, multi

    # ------------------------------------------------------------- walking
    def open(self, page=1):
        self.page = page
        self.sel = 0
        self.approx = False
        self.log = [("open options", page)]
        self._first()
        return page

    def rows(self):
        """[(row, item index or None)] for the current page, in row order."""
        b = self.bind.get(self.page, {})
        return [(k, (b[k][0] if b[k][0] >= 0 else None))
                for k in range(OPT_ROWS[2]) if k in b]

    def selectable(self, row):
        """`Opt_BindRow` clears 0x20000004 for every real row and sets it for
        an empty one and for a header with no page behind it - so a caption is
        skipped and a submenu entry is not."""
        b = self.bind[self.page]
        if row not in b:
            return False
        item, page = b[row]
        if item < 0:
            return False
        return bool(page) if self.opt[item]["kind"] == "header" else True

    def _first(self):
        for k, _ in self.rows():
            if self.selectable(k):
                self.sel = k
                return

    def selected(self):
        b = self.bind.get(self.page, {})
        it = b.get(self.sel, (-1, 0))[0]
        return None if it < 0 else it

    def label(self):
        i = self.selected()
        return self.opt[i]["label"] if i is not None else None

    def value(self):
        """The chosen caption and value of the selected row, if it has any."""
        i = self.selected()
        if i is None:
            return None
        o = self.opt[i]
        if not o["choices"]:
            return None
        return o["choices"][self.cur.get(i, 0)]

    def press(self, bits):
        if self.page is None:
            return False
        if self.sel in self.branch.get(self.page, set()):
            self.approx = True
        rows = [k for k, _ in self.rows()]
        if bits & (UP | DOWN):
            step = -1 if bits & UP else 1
            j = rows.index(self.sel)
            for _ in range(len(rows)):
                j = (j + step) % len(rows)
                if self.selectable(rows[j]):
                    break
            self.sel = rows[j]
            self.log.append(("move", self.sel, self.label()))
            return True
        i = self.selected()
        if i is None:
            return False
        o = self.opt[i]
        if o["kind"] in ("choice", "device") and o["choices"]:
            n = len(o["choices"])
            c = self.cur.get(i, 0)
            if bits & LEFT:
                c = c - 1 if c else n - 1
            elif bits & (RIGHT | CONFIRM):
                c = 0 if c == n - 1 else c + 1
            else:
                return False
            self.cur[i] = c
            self.log.append(("set", o["label"], o["choices"][c]))
            return True
        if bits & CONFIRM:
            if o["kind"] == "header":
                pg = self.bind[self.page][self.sel][1]
                if pg:
                    self._goto(self._page_index(pg))
                    self.log.append(("enter page", self.page, self.label()))
                    return True
            if o["kind"] == "back":
                pg = self.bind[self.page][self.sel][1]
                self._goto(self._page_index(pg) if pg else None)
                self.log.append(("back to page", self.page))
                return True
            self.log.append(("unmodelled row", o["kind"], o["label"]))
        return False

    def _page_index(self, va):
        base, stride, n = OPT_PAGES
        return (va - base) // stride if base <= va < base + stride * n else None

    def _goto(self, k):
        """Enter page `k`, following page 0's trampoline.

        Page 0 has no rows: its builder (0x00492A70) is
        `if (dword_9103C8) word_4DD3B2 = 1; else Ui_GoToPanel(screen, page 1)`,
        so it bounces straight to the root - unless a setting has been changed,
        `dword_9103C8` being the dirty latch, in which case it raises a prompt
        the simulator does not model. Every sub-page's "Retour" binds page 0,
        not page 1, so without this the walk lands on an empty page.
        """
        seen = set()
        while k == 0 and k not in seen:
            seen.add(k)
            if self.cur:                       # a value was changed this run
                self.approx = True
                self.log.append(("page 0: dirty latch, prompt not modelled",))
                break
            k = 1
            self.log.append(("page 0 trampoline -> 1",))
        self.page = k
        if k is not None:
            self._first()
        return k

# ------------------------------------------------------------ the load panel
#: `IAM\GAMES`: a 3496-byte profile/settings header then 256 slots of 32808
#: (docs/GAME_STATE.md 8). `sub_408A10` builds the in-memory directory the
#: SaveDir_* helpers walk - 256 entries of 72 bytes, memset 0x4800 = 256*72 -
#: by lifting four fields out of each slot.
GAMES_HEADER, GAMES_SLOT, GAMES_SLOTS = 3496, 32808, 256

#: The load panel's lists and the two items its empty branch disables.
LOAD_PANEL = 0x004CF2E8
LOAD_SLOTLIST = 0x004CEC08
LOAD_CHARGER = 0x004CE968          # "Charger une partie"
LOAD_NOUVELLE = 0x004CE9B0         # "Nouvelle partie"
LOAD_DETRUIRE = 0x004CE9F8         # "Détruire"


def save_directory(path=None):
    r"""`sub_408A10` - the 72-byte directory entries, read out of `IAM\GAMES`.

        +0   char[32]   <- slot +0    the profile name
        +32  u32        <- slot +32   the day counter
        +36  u32        <- slot +36   the time within the day
        +40  32 bytes   <- slot +76   lifted from the slot's DB at +36; not a
                                      string, and what it is for is not read
    """
    path = path or omkpaths.data("IAM", "GAMES")
    if not os.path.exists(path):
        return []
    d = open(path, "rb").read()
    out = []
    for k in range(GAMES_SLOTS):
        s = GAMES_HEADER + GAMES_SLOT * k
        if s + 108 > len(d):
            break
        out.append({"slot": k,
                    "name": d[s:s + 32].split(b"\0")[0].decode("cp1252"),
                    "day": struct.unpack_from("<I", d, s + 32)[0],
                    "time": struct.unpack_from("<I", d, s + 36)[0],
                    "db36": d[s + 76:s + 108]})
    return out


def profiles(directory=None):
    """`sub_408B50` - how many DISTINCT non-empty profile names the directory
    holds. The load panel branches on this and on nothing else."""
    d = save_directory() if directory is None else directory
    seen = []
    for e in d:
        if e["name"] and e["name"] not in seen:
            seen.append(e["name"])
    return len(seen)


class LoadPanel:
    """The start menu's "Charger une partie" panel - `sub_47A6D0` for screen 29.

    The builder reads the save directory and takes one of two branches, and
    that is the whole of what the panel's shape depends on:

      profiles > 0   focus the slot list (panel+24 = 0), show it, and leave
                     "Charger une partie" and "Détruire" selectable
      profiles == 0  focus the BUTTONS (panel+24 = 1), hide the slot list, and
                     make both of those unselectable - `word_4CEA9A = 3`

    Either way "Nouvelle partie" is hidden on this screen: it belongs to
    screen 30, the save panel, which shares the panel and is told apart by
    `word_4CEA9A` (0 here, 1 there).
    """

    def __init__(self, exe=None, directory=None):
        self.e = exe or U.Exe()
        self.n = profiles(directory)
        self.empty = self.n == 0
        self.focus = 1 if self.empty else 0
        self.mode = 3 if self.empty else 0          # word_4CEA9A
        self.hidden = {LOAD_SLOTLIST} if self.empty else set()
        self.disabled = ({LOAD_CHARGER, LOAD_DETRUIRE} if self.empty else set())
        self.disabled.add(LOAD_NOUVELLE)            # always, on screen 29

    def buttons(self):
        """[(label, selectable)] for the button list, in order."""
        S = U.iam_strings("Menu")
        lst = struct.unpack_from("<I", self.e.read(LOAD_PANEL + 36, 4), 0)[0]
        n = struct.unpack_from("<h", self.e.read(lst, 2), 0)[0]
        arr = struct.unpack_from("<I", self.e.read(lst + 12, 4), 0)[0]
        out = []
        for j in range(n):
            it = struct.unpack_from("<I", self.e.read(arr + 4 * j, 4), 0)[0]
            si = struct.unpack_from("<h", self.e.read(it + 28, 2), 0)[0]
            out.append((S[si] if 0 <= si < len(S) else None,
                        it not in self.disabled))
        return out

    def selectable(self):
        return [l for l, ok in self.buttons() if ok]

# ---------------------------------------------------------- the key bindings
#: Four CONTEXT schemes of fourteen actions, and three device tables. The
#: engine installs a group as the player changes context: `Game_Init` and the
#: end of a fight or a shoot install 0, `Fight_Begin` 3, `Shoot_Enter` 2, the
#: swim transitions 1 (`Input_InstallScheme`, 11 sites).
BIND_GROUPS = {0: "Aventure", 1: "Nager", 2: "Tirer", 3: "Combat"}
BIND_SLOTS = 14

#: The compiled defaults `Opt_RowInput` case 5 restores, and the live tables
#: it restores them into. `Input_InstallScheme` reads the live ones at
#: `0x0090E294 + group*56` with -0xE0 / +0xE0 for the other two devices.
BIND_DEFAULTS = {"keyboard": 0x004C8F90, "mouse": 0x004C9070,
                 "joystick": 0x004C9150}
BIND_LIVE = {"keyboard": 0x0090E1B4, "mouse": 0x0090E294,
             "joystick": 0x0090E374}

#: The option record's fields for a keybind row, from its apply hook
#: (0x004902C0): the destination index is `group*14 + action`.
OPT_GROUP, OPT_ACTION = 92, 112
OPT_SLOT = {"keyboard": 96, "joystick": 100, "mouse": 104}


def bindings(exe=None):
    """The shipped default control schemes.

    -> {group: {action: {"label": .., "keyboard": .., "mouse": .., "joystick": ..}}}

    Codes are `Input_ReadOneControl`'s: a keyboard scan code 1..255, a joystick
    button 48..57 or an axis, a mouse button 12..14, and 0 for unbound. The
    labels come from the option rows, matched by their own +92 group and +112
    action index - which is how the two halves are known to line up.
    """
    e = exe or U.Exe()
    lab = {}
    for o in U.options(e):
        if o["type"] != 3:
            continue
        r = e.read(OPT_TABLE[0] + OPT_TABLE[1] * o["index"], 140)
        g = struct.unpack_from("<i", r, OPT_GROUP)[0]
        a = struct.unpack_from("<i", r, OPT_ACTION)[0]
        lab[(g, a)] = o["label"]
    tabs = {}
    for dev, base in BIND_DEFAULTS.items():
        d = e.read(base, 4 * len(BIND_GROUPS) * BIND_SLOTS)
        tabs[dev] = [struct.unpack_from("<i", d, 4 * i)[0]
                     for i in range(len(BIND_GROUPS) * BIND_SLOTS)]
    out = {}
    for g in BIND_GROUPS:
        out[g] = {}
        for a in range(BIND_SLOTS):
            i = g * BIND_SLOTS + a
            out[g][a] = {"label": lab.get((g, a)), "bit": 1 << a,
                         **{dev: tabs[dev][i] for dev in tabs}}
    return out


def rebind(rows, item, slot, code):
    """`Opt_RebindKey`'s conflict rule, over a {item: {slot: code}} map.

    The scan is over the whole 74-record option table but it only clears a row
    in the SAME GROUP - `*(v7-23) == 3 && *v7 == rec[item].+92` - which is why
    "Avancer" can be the same key in Aventure and in Combat without either
    losing it. Codes 0, 1 and 4 are refused before the scan (0 and 4 are the
    joystick axes, 1 is ESC).

    `rows` is {item: {"group": g, "slot": {n: code}}}; returns the items whose
    binding was cleared.
    """
    if code in (0, 1, 4):
        return None
    me = rows.get(item)
    if me is None:
        return None
    cleared = []
    for other, r in rows.items():
        if other == item or r["group"] != me["group"]:
            continue
        if r["slot"].get(slot) == code:
            r["slot"][slot] = 0
            cleared.append(other)
    me["slot"][slot] = code
    return cleared

# ------------------------------------------------------------ the name field
#: The start menu's new-game panel keeps a text field in its list 0, whose
#: `+4` hook (0x0047A390) is the only list-level handler in the game besides
#: the LIFT's grid. It is a plain 20-character buffer with a cursor.
NAME_BUFFER, NAME_CURSOR, NAME_MAX = 0x0069BDA0, 0x00657994, 20

#: The hook's switch, read from its own jump table: a compact switch over the
#: characters 8..27, everything else falling to the insert case.
NAME_JUMPTABLE, NAME_INDEXMAP = 0x0047A4DC, 0x0047A4F0


def name_switch(exe=None):
    """{character: 'backspace' | 'ignore' | 'return' | 'insert'} for 8..27.

    Read from the compiled jump table rather than transcribed, so a wrong
    reading of the switch shows up as a wrong label.
    """
    e = exe or U.Exe()
    tgt = [struct.unpack_from("<I", e.read(NAME_JUMPTABLE + 4 * i, 4), 0)[0]
           for i in range(5)]
    names = {0x0047A3CF: "backspace", 0x0047A4CE: "ignore",
             0x0047A40C: "return", 0x0047A448: "insert"}
    idx = e.read(NAME_INDEXMAP, NAME_MAX)
    return {8 + k: names.get(tgt[c], hex(tgt[c])) for k, c in enumerate(idx)}


class NameField:
    """`sub_47A390` - the new-game panel's name entry.

    BACKSPACE deletes before the cursor and shifts the tail left; TAB and ESC
    are ignored; RETURN moves the focus to the button list, but ONLY when the
    buffer is not empty; anything else inserts at the cursor, refused once the
    buffer holds 20 characters. The cap is the buffer itself - 0x0069BDA0 to
    0x0069BDB4 is 0x14 bytes - and the save slot that receives the name has
    room for 32, so the field is the tighter of the two.
    """

    def __init__(self, exe=None):
        self.sw = name_switch(exe)
        self.buf = ""
        self.cursor = 0
        self.done = False        # RETURN accepted: focus moves to the buttons

    def type(self, ch):
        """One character, as `sub_4397B0` would hand it over. -> consumed?"""
        if not ch:
            return False
        c = ord(ch) if isinstance(ch, str) else ch
        kind = self.sw.get(c, "insert")
        if kind == "ignore":
            return False
        if kind == "backspace":
            if not self.cursor:
                return False
            self.buf = self.buf[:self.cursor - 1] + self.buf[self.cursor:]
            self.cursor -= 1
            return True
        if kind == "return":
            if not self.buf:
                return False          # an empty name is refused
            self.done = True
            return True
        if len(self.buf) >= NAME_MAX:
            return False              # the buffer is full
        self.buf = self.buf[:self.cursor] + chr(c) + self.buf[self.cursor:]
        self.cursor += 1
        return True

    def enter(self, text):
        for ch in text:
            self.type(ch)
        return self.buf

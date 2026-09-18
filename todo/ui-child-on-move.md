# A tab strip switches pages on the MOVE, not on the confirm

**Filed 2026-09-18, not yet done.** It is nine lines of code and it changes a
rule every UI check leans on, so it wants a settled tree and the whole UI check
family, not a corner of a session with four branches in flight.

## What the engine does

`Ui_MoveSelection` (0x0042A7E0) has a TAIL that nobody here had read. Once the
move has settled, at `loc_42A897`:

```
    if (list->sel == before)            return          ; nothing moved
    item = list->items[list->sel]
    child = item->+2C ;  if (!child)    return
    if (!(item->+30 & 0x40))            return          ; bank A 0x40
    if (screen->+1C == child)           return          ; already there
    screen->+24 = child
    if (screen->+1C && screen->+1C->+8) screen->+1C->+8(screen, screen->+1C)
    ... then the child's own `+4` BUILDER runs
```

So **an item whose bank A carries `0x40` installs its child panel as the
selection lands on it** — the page changes under the cursor, with no confirm.

And `Ui_ConfirmSelection` (0x0042A750) carries the exact complement, `and edx,
40h` on the same field with the opposite sense: it **refuses** a child that
carries `0x40`, precisely because the move has already entered it. The two
halves only make sense together.

## What it covers, measured

All **six** of the sneak device's tab icons (list `0x004DE210`) ship bank A
`0x20000040` with a child panel:

| item | child |
|---|---|
| 0x004DDFB0 | 0x004DED80 |
| 0x004DDFF8 | 0x004DEDE8 |
| 0x004DE040 | 0x004DEE50 — Inventaire |
| 0x004DE088 | 0x004DEF88 |
| 0x004DE0D0 | 0x004DF058 |
| 0x004DE118 | 0x004DF0C0 — the QUIT confirm |

Read out of the image, not out of the lift.

## What the port does instead

`UiWalk::move` (`engine/src/ui/widgets.cpp`) has **no child descent at all** —
`it->child` appears only inside `confirm()`. So every `0x20000040` tab strip in
the tree switches on CONFIRM in the replica and on MOVE in the engine.

## And it explains a claim in the port that is WRONG

`engine/src/ui/widgets.h` (~208) and `widgets.cpp` (~1966) say panel
`0x004DF0C0` is "**BUILT AND UNREACHABLE**", because "`Ui_ConfirmSelection`
prefers a callback over a `+44` child, and nothing else installs it", so "what
the callback shows is drawn by nothing".

That is exactly backwards: the panel is unreachable **because the port made it
so**. `Ui_MoveSelection` installs it, and `0x004DF0C0`'s own `+4` builder
(`0x0049D980`) is the precise inverse of the callback `0x0049DBF0` — it hides
*Oui*/*Non*, clears `dword_4DF0D8` and `word_4DEBA2`, and tints the echo bar
from the Quit icon's rgb. A builder that is the inverse of a callback is a
function that only makes sense if it runs; and `word_4DEBA2` has exactly two
writers in the whole listing, that builder and that callback.

## Doing it

* ~9 lines in `UiWalk::move`, guarded on bank A `0x40` and on the child not
  already being current, with the old panel's leave hook run first.
* The complement in `confirm()`: refuse a child carrying `0x40`.
* Then the WHOLE UI family, because this is the shared walk:
  `--only "ui page" "sim: ui coverage" "engine: UI" "engine: screen"
  "engine: sneak memos" "engine: shop open" "engine: multiplan open"
  "engine: row window" "engine: pause" "engine: name field"`.
  Expect `tools/sim/ui.py` to need the same rule — the invariant that check
  exists for is that the two implementations do not disagree, and this changes
  what "the current panel" is after an arrow press.
* `engine: UI` and `ui geometry` are **already red** for unrelated census drift
  (`todo/sweep-log.md`); read their rows rather than re-baselining them.

## Where the reading came from

`todo/pending/videophone-read.md` §"the quit confirm" (the agent that read it),
re-verified independently in the main session on 2026-09-18: the
`loc_42A897` tail, `Ui_ConfirmSelection`'s `and edx, 40h`, and all six tab
icons' bank A read straight out of `Runtime 2.exe`.

# `Text_LayOutBlock` — porting the engine's own text layout

`Text_LayOutBlock` (0x0043F3E0, 577 lines, one caller) is the last piece of the
interface's text path that this port had a **guess** in place of. Everything
around it is ported and checked — the 13 fonts, the 2899 glyphs, the coverage
ramp, the markup parse, the advance (`Text_GlyphAdvance` = face kern + the
glyph's width, or the face's default advance when the file has no glyph) — but
the part that turns a string and a box into POSITIONED LINES was this repo's:
a greedy break at spaces, a line advance of `height + 2`, a blank line of 12,
no vertical placement and no alignment beyond the item's own flag.

`docs/UI.md` has labelled it a reconstruction since it was written, and the
examine page's scroll (`todo/omk-play.md` 86) made it matter: the scroll bound
is `laidOutHeight − boxHeight`, so a wrong layout is a wrong bound.

Asked for by the reader on 2026-09-07, straight after 85/86.

## What the engine does

`Text_DrawBlock` (0x0043F180, 20 callers) is the entry point. It writes the
box and a dozen style globals, then calls `Text_LayOutBlock(text, measureOnly,
literal)`, which returns **`maxY − top`** — the laid-out height, which is why
one function serves both the draw and the measure.

The globals, all set by `Text_DrawBlock` from its `params` block (the
`TEXTP_*` mask):

| global | what |
|---|---|
| `907A14` / `907A18` / `907A08` / `907A1C` | left, top, right, bottom |
| `907A10` | the current font (the id LETTER); 74 `'J'`, or 76 `'L'` below 640×480 |
| `907A0C` | the ALTERNATE font, for a bracketed span |
| `9079F4` / `9079F8` | the ORIGIN, subtracted from every drawn position — and `9079F8` is where the examine page's scroll offset arrives |
| `907A00` | the style word: `0x1E` the four horizontal alignments, `0x1C00` the three verticals, `0x4000` blink |
| `907A04` | the ALTERNATE style |
| `907A24` | WHICH bracketed span swaps to the alternate (`-1`: none) |
| `9079FC/FD/FE` | the colour; `907940/41/42` the alternate |
| `907948` | passed to `Text_DrawRun` unchanged (11 by default) |
| `907A28` / `907A2C` | the `{E}` value and its alternate |

and the algorithm:

* **Vertical placement, before anything is read.** `0x800` → bottom
  (`y = bottom − lineHeight`), `0x1000` → middle
  (`y = top + (bottom − lineHeight − top) / 2`), neither → top. The height is
  the CURRENT FONT's `+12`, and the markup letters `H` / `L` / `M` redo the
  same three at any point in the string.
* **The wrap.** Per character, `w += Text_GlyphAdvance(ch, font)`; while
  `w <= right − left` the character is kept, and a SPACE records both the run
  position and the input position. Past the width: if a space was seen the line
  is cut there and the input rewinds to just after it, otherwise the character
  is dropped and the line breaks where it is.
* **The line advance is 120% of the current font's `+12`** — `v6 += 120 * h /
  100` — and a blank line advances by the same, not by a constant.
* **Alignment at flush**, from `style & 0x1E`: 4 → `x = right − runWidth`,
  8 → `x = left + (right − runWidth − left) / 2`, anything else → the pen. The
  run width is re-measured with each character's OWN font.
* **A line is flushed** when a newline is pending OR when
  `(style ^ workingStyle) & 0x1C1E` — the alignment or vertical bits changed.
  All four alignment directives therefore break the line: `{F}` at its own
  letter, which falls into the test, and `{C}` / `{D}` / `{G}` one character
  later at the **closing brace**, because `{`, `}`, `[` and `]` are the
  else-halves of the four nested `if`s that end at that test and fall through
  to it as well.
* **`{P}` does NOTHING**, and this was read wrong first time round. `P` is not
  a case in the switch, so it reaches the test — but it changes no style bit,
  and neither does its brace, so nothing flushes. Every one of the five
  shipped `{P}` occurrences (three in one object description, two in `Fsim`)
  sits beside a `\r\n\r\n` that does the paragraph break for real, which is
  exactly why an author could write it and nobody could see it was inert.
  `verify.py: engine: text block` asserts the no-op directly.
* **Brackets are COUNTED.** `[` increments a counter and, when it reaches
  `907A24`, saves the colour/font/style/`E` and installs the alternates; `]`
  restores them at the same index. That is how one string carries a label and a
  value in two colours.
* **`{B}`** toggles `0x4000`, and at emit time a blinking character whose
  oscillator 1 is high is written **(255, 0, 0)** — red, which this port had
  already measured off two captures and can now read in the code.
* **The run buffer is 6 bytes a character** — char, styleChanged, r, g, b,
  font — and a `Text_DrawRun` is issued per span of constant style.

`measureOnly` does all of it but the drawing and still returns the height,
which is what `Ui_ItemTextStyle` sizes a scrolling box with. `literal` turns
the markup off entirely, so `{`, `}`, `[` and `]` are ordinary characters.

## The steps

Each ends in a commit and a report.

1. **`layOutBlock` in `engine/src/ui/text.*`** — the algorithm above behind one
   entry point, with the box, the style word, the origin, the alternates and
   the measure-only flag, returning the height. Its own check, over the
   shipped strings.  — **DONE**
2. **The examine page uses it**, replacing the greedy wrap, and
   `engine: text scroll` and `engine: sneak` are re-baselined against the real
   height.  — **DONE**
3. **The other callers**: an item's own text — `Ui_DrawItem` scales the item's
   box and hands it to `Text_DrawBlock`, so the engine wraps and aligns inside
   it, where this port drew one unwrapped line — and the subtitle path in
   `omk-play`, whose `wrapInto`/`wrapRun` greedy break and `height + 2` pitch
   are gone. **`engine: screen`'s framebuffer hashes did not move**, which is
   the result worth having: the new layout reproduces the old single-line
   output pixel for pixel wherever nothing wraps.  — **DONE**

## What stays out

`Text_DrawRun` itself is already ported as `TextLayout::drawRun` and is not
touched here. `907A28` (`{E}`) is carried through the save/restore but nothing
in this port reads it — the engine reads it outside this function.

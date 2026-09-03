# readable/ — hand-cleaning working copy

Start at [`../CLAUDE.md`](../CLAUDE.md) for the project as a whole.

A copy of `../clean/` where every function carries a status banner. Rewrite
functions here; the mechanical original always stays untouched in
`../clean/src/` for comparison.

```
/* @func 0x00437E00  o3de_GetObjectByIndex  @status RAW  @lines 10  @callers 1 */
```

| field | meaning |
|---|---|
| `@func` | load address — the key, and the cross-reference into IDA |
| `@status` | see below |
| `@lines` | size of the RAW body, as a rough cost estimate |
| `@callers` | how many call sites — how much a good name buys you |

`@status` has four values, and the middle two exist so that work already done
cannot be lost:

| status | meaning |
|---|---|
| `CLEAN` | body rewritten by hand |
| `NAMED` | read and named from evidence, body left as generated |
| `READ` | read, then deliberately left alone — the banner comment says why |
| `RAW` | untouched. May still carry a real name: the automatic recovery pass
  lifted ~50 out of the binary's own debug strings and nobody has read those. |

`tools/rename.py` promotes `RAW` → `NAMED` for anything in `renames.json`
automatically, so the trace stays in step with the renames.

`READ` is the one that is easy to lose. A function can be read closely, supply
exactly the fact that was wanted, and still be the wrong thing to rename or
rewrite — usually because a name would be a guess. Without the record, the next
pass re-reads it and risks inventing the name that was rejected the first time.

`types.h` accumulates struct/offset facts discovered while cleaning, each
annotated with how many independent sites corroborate it.

## The index

`INDEX.md` lists every processed function - address, name, module, call count and
a one-line description taken from its own leading comment, so it cannot drift
from the code. Regenerate with `python3 tools/index.py`.

## Renaming

`tools/renames.json` is the symbol map and `tools/rename.py` applies it across
the whole tree - every module, `decls.h`, `globals.h`, `types.h` and the
`@func` banners. Renames are real, not `#define` aliases: a function renamed
where it is defined reads the same at every call site.

    python3 tools/rename.py --check    # what would change
    python3 tools/rename.py            # apply

## Picking what to do next

```
python3 tools/status.py            # progress, per file
python3 tools/status.py top 30     # most-called RAW functions
python3 tools/status.py big 30     # longest RAW functions
python3 tools/status.py named      # RAW functions with a recovered real name
python3 tools/status.py list o3de  # everything in the o3de modules
python3 tools/status.py find Script_Reinit
python3 tools/status.py show 437E00
```

The banners are the source of truth; `status.json` is regenerated from them on
every `status.py` run.

## Batch 1 — `Dialog_Load` (0x00401800) and its dependencies

16 functions, `@status CLEAN`. `sub_401800`'s full transitive closure is 626
functions / 33.5k lines (`sub_41B280` reaches most of the game), so this batch
is the root plus everything one and two calls deep — a self-contained unit.

| address | now called | file |
|---|---|---|
| 0x00401800 | `Dialog_Load` | 01_file.c |
| 0x0040D760 | `Scene_FindObjectIndexById` | 01_file.c |
| 0x0040FF90 | `Archive_ReadChunk` | 02_file.c |
| 0x004128F0 | `Res_OpenFile` | 04_sys.c |
| 0x0041E040 | `Subtitle_Show` | 05_sys.c |
| 0x0041B280 | `Camera_ResetForActor` | 04_sys.c |
| 0x0043F180 | `Text_DrawBlock` | 15_dinput.c |
| 0x00441EB0 | `Matrix3x3_FromEulerAngles` | 16_o3de.c |
| 0x00411EF0 | `Mem_Alloc` | 03_win32.c |
| 0x00412060 | `Mem_Free` | 03_win32.c |
| 0x00412100 | `Sys_GetTimeMs` | 04_sys.c |
| 0x004372D0 | *(unchanged)* | 10_dsound.c |
| 0x00440C80 | *(unchanged)* | 16_o3de.c |
| 0x00457030 | *(unchanged)* | 19_dsound.c |
| 0x00468DE0 | *(unchanged)* | 21_d3d.c |
| 0x00437E00 | `o3de_GetObjectByIndex` | 11_o3de.c |

Five kept their address names: nothing in this closure shows what they do, and
a wrong name is worse than none. Their bodies were still cleaned and commented.

Renames are `#define` aliases in `types.h`, not edits, so the other 32 modules
keep working and the two spellings cannot drift apart.

### Verified against the shipped data

`tools/dialog_dump.py` parses `gamedata/IAM/DIALOG` using the format `Archive_ReadChunk`
and `Dialog_Load` implement. It reproduces 321 conversations, 1174 nodes and
1923 cameras, which pins down the layout in `types.h`:

* `8 + 64*nodeCount + 44*cameraCount` lands exactly on the string data, so both
  strides are right.
* `DialogNode::ptr[8]` is non-null in all 1174 nodes and points at the dialogue
  text; the other eight slots are used by 3–212 nodes each, so all nine are real.
* `param[]` holds branch targets — all 1452 used values are valid node indices.
* The unit conversions check out on real records: position raw 252 → 37
  (8.8-fixed centimetres → hundredths of an inch, less a bias of 1) and angle
  raw 853 → 74° (a 4096-step turn → degrees).

One hypothesis was **rejected** this way: `field56`/`field60` looked like camera
ids in the first chunk examined, but across the corpus they match only
246/234 times in 1174, i.e. chance. They are left unnamed.

    python3 tools/dialog_dump.py       # summary and layout self-check
    python3 tools/dialog_dump.py 2     # dump one conversation

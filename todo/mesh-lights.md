# The `.3DO` light table

The 1999 spec sheet's "Multilights" line (`todo/engine-spec-1999.md`) pointed
at a table this repo has parsed the header of since the format was decoded and
never read the records of. This is the work.

## Steps

| # | step | state |
|---|---|---|
| 1 | the count and the stride, from the LOADER | **DONE** 2026-09-05 |
| 2 | decode the 304-byte record | open |
| 3 | what CONSUMES a light - the question that makes it worth doing | open |
| 4 | port, if step 3 warrants it | open |

## Step 1, done - and it corrected a number this repo had just published

`Read3DO_Init` (0x0044DF10) relocates the light table like every other, but
the line before it matters more:

```c
u32(*v1, 232) = u32(*v1, 240);       /* the count comes from +240 */
if (u32(*v1, 232))
    v1[8] = v1[11] + v3[10];         /* lights = base + header[+40] */
else
    v1[8] = 0;
```

**The engine copies `desc+240` over `desc+232` and then uses it.** The port
read the on-disk `+232`, and so did the `mesh lights` check written hours
earlier - which is how "6244 lights in 375 files" got published. The two fields
disagree in **256 of 635** files, `+240` is never larger, and the real figures
are **4179 lights in 216 files**.

The data settles it independently, which is the point of CLAUDE.md 1's
self-checking parse. The light table is the LAST thing in a `.3DO`, so the walk
has a file size to land on:

| count used | `lightOff + n * 304 == filesize` |
|---|---|
| **`desc+240`** | **216 of 216** |
| `desc+232` | 119 of 216 |

So the record is **304 bytes** and the count is **`desc+240`**, agreeing from
the loader and from the corpus. `desc+232` is an authored or allocated figure
that the engine ignores; nothing yet says what it means.

The lesson is the one CLAUDE.md 1 opens with, and it cost a published number:
the port's parser is DATA reading, and a field's meaning is not established
until a loader says so. `readHeader` had carried `+232` as `lights` since the
format work, plausibly and wrongly, and a check built on it merely made the
error harder to notice.

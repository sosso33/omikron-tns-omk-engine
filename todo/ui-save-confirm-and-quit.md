# Two small UI gaps, read and not yet done

Both found by the 2026-09-18 triage (`todo/pending/ui-remainder-survey.md` §3)
and **read out of the image here** so that doing them is transcription rather
than research. Held only because three branches were in flight over the same
files.

---

## 1. `0x0047BC30` — both confirm dialogs lose their SUBJECT LINE

Panels `0x004CF350` (title string 2, *Détruire*) and `0x004CF3B8` (title string
10, *Ecraser ce fichier ?*) **share** item `0x004CEE08` at (95, 120) 450x40,
whose only content is this `textFn`. The port models both panels and both `Oui`
callbacks (`kCbDestroyYes`, `kCbConfirmYes`) and draws no subject — so a player
is asked *"Ecraser ce fichier ?"* without being told **which file**.

The function, whole:

```
    if (screen->+0 == 0x1D && screen->+1C == off_4CF3B8)
        sprintf(out, "\" %s \"", byte_69BDA0)        ; the TYPED NAME
    else {
        sub_47BCB0(screen, dword_4CEBAC, buf)        ; the selected row
        sprintf(out, "\" %s \"", buf)
    }
```

`0x1D` is 29, the START MENU; `dword_4CEBAC` is the selected row, which the
port's load panel already carries. The quotes are literal and part of the
format — `" %s "` with a space inside each quote.

`sub_47BCB0(screen, row, buf)` is the same composer the load panel's own rows
and heading use:

| row | what it composes |
|---|---|
| `-1` | `Ui_ScreenString(screen, s, 12, -1)` on screen **29**, or string **14** on screen **30**, then `sprintf(buf, "%s : %s", s, byte_657970)` — the label and the PROFILE name |
| `== dword_657968` | `Ui_ScreenString(screen, buf, 15, -1)` — one fixed string, the empty-slot label |
| `< dword_657968` | the directory record at `sub_408D20(byte_69BDC0, byte_657970, row)`: its name at `+0x28`, its day at `+0x20` through `sub_41E690`, its time at `+0x24` through `sub_41E6E0`, sprintf'd together |

**The port already produces both of those strings.** A run prints

    save: the load panel's row for it is "KAY'L 669 - 12 Nadim 7216 - 14:14:17"
          under "Joueur : hereIsTheProfileName"

which are exactly the `row < count` and the `row == -1` arms. So the whole
port is: hand item `0x004CEE08` the text `"\" " + thatString + " \""` through
`setRowText`, with the typed-name case for screen 29. `verify.py: engine: save
load` already asserts those strings against a photograph of the ORIGINAL
drawing the same save file, so the input is tier-4 already.

---

## 2. `0x0047BC10` — the start menu's `Quitter` → `Oui`, the only real exit

```
    mov  eax, [esp+4]              ; the screen
    mov  dword ptr [eax+8], 3      ; screen->+8 = 3
    push 0
    call PostQuitMessage
    xor  eax, eax
    retn
```

Two effects and nothing else: the screen's `+8` goes to **3**, and the process
is asked to quit. This is **distinct from the pause menu's and the sneak's
quit**, which the port does have — those set a request the game loop reads;
this one ends the application.

Worth one careful thought before porting: the replica's frontend owns the
window, so "PostQuitMessage" means "leave `Game_RunLoop`", and a viewer run
that is measuring something must not be ended by a stray confirm. Port the
DECISION (the screen's `+8 = 3` and a quit request the frontend honours), not
a literal exit call buried in the UI walk.

---

## Not to be done, and that is the finding

* **`0x0049BC30`** on the sneak's ANNEAUX tile is `mov eax, 1; retn`. It exists
  only so `Ui_ConfirmSelection` sees a callback and does **not** descend into
  the item's `+44`. Do not "implement" it.
* **`0x0047BE50`**, the start menu's footer, is
  `mov cl, byte_65799C; mov [out], cl` — one byte of BSS with exactly two
  references in the whole listing, this read and its `db ?`. Nothing writes it.
  No feature to reproduce; leave it unported and say why.
* **`0x0047B470`**, the load panel's connector, is three `sub_4285E0` quads in
  an elbow where the port draws one reconstructed bar, with a colour byte
  chosen by `sub_428F90(item, 3.0f)` -> `sub_42B5E0(2) + 0x18`. A cosmetic
  difference on a screen that is otherwise tier-4 against a real capture —
  worth transcribing if anyone touches the load panel, not a task of its own.

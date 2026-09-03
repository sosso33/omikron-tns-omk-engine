#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""omkweb - a browser front end for the Omikron dialogue data.

Serves the conversations, the character models and the per-frame morph
animation straight out of gamedata/, so what you see is the shipped data being
interpreted by the format notes in docs/, not an export.

    python3 tools/omkweb.py            then open http://localhost:8752
    python3 tools/omkweb.py --port N

/world serves the annotated IAM\AREA / SCENE / GLOBAL script listings - the
phase-1 opcode work rendered as readable game logic, with every dialog.start
linking into the conversation player.
"""
import json, os, sys, glob, struct, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("OMKWEB_PLAIN", "1")
import omkdata, omkdialog, tex3dt, mesh3do
# script_dump / dialog_disasm read with repo-relative paths (omkdialog already
# imports them under a cwd guard); reuse the loaded modules for /world.
import dialog_disasm as _disasm, script_dump as _sdump
import ui_tables
import omkpaths
from sim import ui as simui
import cutscene as _cut
from dialog_triggers import archive as _archive

PAGE = os.path.join(HERE, "omkweb.html")
CUTPAGE = os.path.join(HERE, "omkcut.html")
UIPAGE = os.path.join(HERE, "omkui.html")

def build_stamp():
    """A token that changes whenever the decoders or the page change.

    Textures and morph streams are expensive to decode, so they are served with
    a long cache lifetime - which means a browser will happily keep serving the
    output of a decoder bug long after it is fixed. Every cacheable URL carries
    this stamp, so correcting a decoder invalidates the cache by itself."""
    import hashlib
    h = hashlib.md5()
    for fn in sorted(os.listdir(HERE)):
        if fn.endswith((".py", ".html")):
            st = os.stat(os.path.join(HERE, fn))
            h.update(f"{fn}:{st.st_mtime_ns}:{st.st_size}".encode())
    return h.hexdigest()[:10]

BUILD = build_stamp()

# --------------------------------------------------------------- the UI page
#: The input words the page can send, by the name it sends them under. These
#: are the engine's own 14 binding slots (docs/UI.md 3c).
UI_KEYS = {"left": simui.LEFT, "right": simui.RIGHT, "up": simui.UP,
           "down": simui.DOWN, "confirm": simui.CONFIRM, "back": simui.BACK,
           "close": simui.CLOSE}


def _ui_screens():
    """The 37 screens, with what the walker can do with each."""
    out = []
    for s in ui_tables.screens():
        panel = None
        if any(s["cb"]):
            try:
                panel = simui.Ui().panel_of(s["id"])
            except Exception:
                panel = None
        out.append({"id": s["id"], "name": s["name"], "param": s["param"],
                    "bitmap": s["bitmap"], "text": s["text"],
                    "live": bool(any(s["cb"])), "panel": panel,
                    "options": s["id"] == 35})
    return out


def _ui_state(u, screen):
    """The widget tree as the page draws it: every list, every item, its real
    rectangle, its label out of the screen's own IAM file, and what the walk
    has made of it."""
    scr = {x["id"]: x for x in ui_tables.screens()}[screen]
    txt = scr["text"]
    if u.panel is None:
        # the walk closed the screen - `back` from a panel with no parent
        return {"screen": screen, "name": scr["name"], "bitmap": scr["bitmap"],
                "text": txt, "panel": None, "lists": [], "closed": True,
                "answer": u.answer, "approx": u.approx,
                "log": [[str(x) for x in e] for e in u.log]}
    lists = []
    for k, lst in enumerate(u.lists(u.panel)):
        items = []
        for j, it in enumerate(u.items(lst)):
            items.append({
                "at": it,
                "x": u._i16(it), "y": u._i16(it + 2),
                "w": u._i16(it + 4), "h": u._i16(it + 6),
                "label": u.label(it, txt) if txt else None,
                "selectable": u.selectable(it),
                "selected": u.sel.get(lst) == j,
                "focused": u.sel.get(lst) == j and k == u.cur,
                "cb": u._u32(it + 40), "page": u._u32(it + 44),
                # A sprite item carries TWO source rectangles into the screen's
                # own sheet - Ui_DrawItemSprite picks +12/+14 when the item is
                # lit and +16/+18 when it is not. For 146 of the 164 sprite
                # items the unlit source IS the destination, so "unlit" blits
                # the background back over itself; the lit copy is a second
                # rendering of the button elsewhere on the sheet.
                "sprite": bool(u._u32(it + 52) & 0x100),
                "lit_src": [u._i16(it + 12), u._i16(it + 14)],
                "off_src": [u._i16(it + 16), u._i16(it + 18)]})
        lists.append({"at": lst, "current": k == u.cur,
                      "hook": u._u32(lst + 4), "items": items})
    flags = u._u32(u.panel + 72)
    back = ("none" if flags & 0x2000 else
            "sheet" if flags & 0x4000 else
            "tiles" if u._u32(u.panel + 20) else "none")
    return {"screen": screen, "name": scr["name"], "bitmap": scr["bitmap"],
            "text": txt, "panel": u.panel, "lists": lists, "back": back,
            "answer": u.answer, "approx": u.approx,
            "log": [[str(x) for x in e] for e in u.log]}


_TEXT_CACHE = {}


def _ui_text_png(screen, item, lit):
    """One item's label rendered with the shipped fonts. -> PNG bytes or None.

    The alignment bits live in the item's bank-C word and are the same 2/4/8/16
    `Text_DrawBlock` takes: 4 right, 8 centred, else left.
    """
    import uitext
    scr = {x["id"]: x for x in ui_tables.screens()}.get(screen)
    if not scr or not scr["text"]:
        return None
    u = simui.Ui()
    si = u._i16(item + 28)
    lit_txt = None
    if 0 <= si:
        S = ui_tables.iam_strings(scr["text"])
        lit_txt = S[si] if si < len(S) else None
    if not lit_txt:
        return None
    fid = u.e.read(item + 36, 1)[0]
    letter = chr(fid) if 32 < fid < 127 else "J"
    r, g, b = u.e.read(item + 8, 3)
    w = max(1, u._i16(item + 4))
    # The EFFECTIVE flags, not the static ones: a screen's open callback sets
    # flags the item record does not carry, and the start menu centres its
    # four buttons with exactly one such broadcast.
    u.open(screen)
    fc = u._u32(item + 56)
    for lst in u.lists(u.panel):
        if item in u.items(lst):
            fc = u.item_flags(item, lst)[2]
            break
    align = (4 if fc & 8 else 8 if fc & 0x10 else 16 if fc & 0x20 else None)
    if fc & 1:                       # UIF_TEXT_WHITE
        r = g = b = 255
    key = (lit_txt, letter, (r, g, b), w, align, lit)
    if key not in _TEXT_CACHE:
        try:
            _TEXT_CACHE[key] = uitext.png(lit_txt, width=w, font_letter=letter,
                                          rgb=(r, g, b), align=align, lit=lit)
        except Exception:
            _TEXT_CACHE[key] = None
    return _TEXT_CACHE[key]


def _ui_walk(screen, keys, text=""):
    """Replay a key sequence from a fresh screen - the model is deterministic,
    so the page stays stateless and every request is the whole history."""
    if screen == 35:
        o = simui.OptionsUi()
        o.open(1)
        for k in keys:
            o.press(UI_KEYS.get(k, 0))
        rows = []
        for r, item in o.rows():
            oo = o.opt[item] if item is not None else None
            rows.append({"row": r, "item": item,
                         "label": oo["label"] if oo else None,
                         "kind": oo["kind"] if oo else None,
                         "value": (o.value() if r == o.sel else None),
                         "selectable": o.selectable(r),
                         "selected": r == o.sel})
            if oo and oo["choices"]:
                rows[-1]["choices"] = [list(c) for c in oo["choices"]]
                rows[-1]["chosen"] = o.cur.get(item, 0)
        return {"screen": 35, "name": "OPTIONS", "options": True,
                "page": o.page, "rows": rows, "approx": o.approx,
                "log": [[str(x) for x in e] for e in o.log]}
    scr = {x["id"]: x for x in ui_tables.screens()}.get(screen)
    if scr is None or not any(scr["cb"]):
        return {"screen": screen,
                "error": "screen %d is one of the five (ELIMINE) entries - it "
                         "has no callbacks at all, so there is nothing to open"
                         % screen}
    u = simui.Ui()
    try:
        u.open(screen)
    except (ValueError, KeyError) as e:
        return {"screen": screen, "error": str(e)}
    field = simui.NameField()
    for k in keys:
        if k.startswith("ch:"):
            field.type(k[3:] and k[3:][0] or "")
            continue
        # `NameField` is the faithful model of the field itself; `Ui.name` is
        # what the CONFIRMER's gate reads (0x0047A2B0's first instruction
        # tests the cursor). They are two views of one buffer, so mirror it
        # before every frame - otherwise the page types a name, the walker
        # still sees an empty field, and Confirmer silently refuses.
        u.name = field.buf
        u.press(UI_KEYS.get(k, 0))
    for ch in text:
        field.type(ch)
    u.name = field.buf
    st = _ui_state(u, screen)
    st["name_field"] = {"text": field.buf, "cursor": field.cursor,
                        "done": field.done, "max": simui.NAME_MAX,
                        "active": any(l["hook"] == simui.NAME_HOOK
                                      and l["current"] for l in st["lists"])}
    return st


_DECOR_CAMS = None

def _decor_cams():
    """Eye positions of every DECORS/*.3DO scene camera, by set name.

    Dialogue cameras whose subject is -1 carry absolute world coordinates.  The
    same coordinates appear in the set's own camera list, so the nearest set
    identifies where a conversation is staged (verified: dialog 402
    "Telis/Appart" lands on Aapkayl at 71 units against 1176 for the runner-up)."""
    global _DECOR_CAMS
    if _DECOR_CAMS is None:
        _DECOR_CAMS = {}
        for path in sorted(glob.glob(omkpaths.data("MESHES/DECORS/*.3DO"))):
            try:
                h = mesh3do.header(path)
                if not h["cameras"]: continue
                raw = open(path, "rb").read()
                _DECOR_CAMS[os.path.basename(path)[:-4]] = [
                    struct.unpack_from("<3f", raw, h["camOff"] + 52 * i + 20)
                    for i in range(h["cameras"])]
            except Exception:
                pass
    return _DECOR_CAMS

def match_decor(conv):
    """Best-matching set for a conversation, or None when it has no world cameras."""
    pts = [tuple(cm["pos"][:3]) for cm in conv["cameras"]
           if tuple(cm["subject"]) == (65535, 65535)]
    if not pts: return None
    best = None
    for name, cams in _decor_cams().items():
        tot = sum(min((p[0]-q[0])**2 + (p[1]-q[1])**2 + (p[2]-q[2])**2
                      for q in cams) ** 0.5 for p in pts) / len(pts)
        if best is None or tot < best[1]: best = (name, tot)
    return best

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def _send(self, code, ctype, body, cache=False):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        if cache: self.send_header("Cache-Control", "max-age=3600")
        self.end_headers()
        try: self.wfile.write(body)
        except BrokenPipeError: pass

    def _binpack(self, meta, blob):
        """4-byte length, JSON header, then the float payload.

        The header is padded with spaces to a multiple of 4 so the payload
        starts 4-aligned - `new Float32Array(buffer, offset)` throws in the
        browser otherwise, and the failure is easy to swallow in a catch."""
        head = json.dumps(meta).encode("utf-8")
        head += b" " * (-len(head) % 4)
        return struct.pack("<I", len(head)) + head + blob

    def _json(self, obj):
        self._send(200, "application/json",
                   json.dumps(obj).encode("utf-8"))

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        try:
            self.route(path)
        except Exception as e:
            self._send(500, "text/plain", f"{type(e).__name__}: {e}".encode())

    def _page(self, filename):
        page = open(filename, "rb").read().replace(b"__BUILD__", BUILD.encode())
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(page)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        return self.wfile.write(page)

    def route(self, path):
        if path in ("/", "/index.html"):
            return self._home()
        if path in ("/dialog", "/dialog/"):
            return self._page(PAGE)
        if path in ("/cutscene", "/cutscene/"):
            return self._page(CUTPAGE)
        if path in ("/ui", "/ui/"):
            return self._page(UIPAGE)

        if path == "/api/ui/screens":
            return self._json(_ui_screens())

        if path.startswith("/api/ui/walk"):
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            try:
                scr = int(q.get("screen", ["29"])[0])
            except ValueError:
                return self._send(400, "text/plain", b"bad screen")
            keys = [k for k in q.get("keys", [""])[0].split(",") if k]
            text = q.get("text", [""])[0]
            return self._json(_ui_walk(scr, keys, text))

        if path.startswith("/api/ui/text"):
            # One item's label, drawn with the game's own fonts and its own
            # markup. The item names its font at +36, its colour at +8..10 and
            # its alignment in flag bank C; `lit` applies Ui_ItemTextStyle's
            # halving of every channel for a row that is not selected.
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            try:
                scr = int(q.get("screen", ["29"])[0])
                item = int(q.get("item", ["0"])[0])
            except ValueError:
                return self._send(400, "text/plain", b"bad item")
            p2 = _ui_text_png(scr, item, q.get("lit", ["0"])[0] == "1")
            if not p2:
                return self._send(404, "text/plain", b"no text")
            return self._send(200, "image/png", p2, cache=True)

        if path.startswith("/api/ui/background"):
            # What `Ui_DrawPanelBack` actually composes - usually 80 tiles out
            # of the sheet, NOT the sheet itself. The parts of a sheet that are
            # source art (a button's lit copy) never reach the screen.
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            try:
                scr = int(q.get("screen", ["29"])[0])
                pan = simui.Ui().panel_of(scr)
            except Exception:
                return self._send(404, "text/plain", b"no panel")
            bg = ui_tables.panel_background(scr, pan)
            if not bg:
                return self._send(404, "text/plain", b"no background")
            return self._send(200, "image/png",
                              tex3dt.png(bg[0], bg[1], bg[2]), cache=True)

        if path.startswith("/api/ui/bitmap/"):
            name = urllib.parse.unquote(path[len("/api/ui/bitmap/"):])
            name = name.split("?")[0]
            b = ui_tables.bitmap(name.replace(".png", ""))
            if not b:
                return self._send(404, "text/plain", b"no such bitmap")
            return self._send(200, "image/png",
                              tex3dt.png(b[0], b[1], b[2]), cache=True)

        if path == "/api/cutscenes":
            return self._json(_cut.index())

        if path.startswith("/api/cutscene/"):
            rest = path[len("/api/cutscene/"):]
            if "/" not in rest:
                return self._send(404, "text/plain", b"want /api/cutscene/<file>/<shot>")
            fn, edt = rest.split("/", 1)
            s = _cut.shot(urllib.parse.unquote(fn), urllib.parse.unquote(edt))
            if not s: return self._send(404, "text/plain", b"no such shot")
            return self._json(s)

        if path == "/api/camerascripts":
            return self._json(_cut.camera_scripts())

        if path.startswith("/api/camerashot/"):
            # /api/camerashot/<AREA|SCENE>/<chunk>/<record>
            try:
                arch, chunk, rec = path[len("/api/camerashot/"):].split("/")
                sh = _cut.camera_shot(arch.upper(), int(chunk), int(rec))
            except Exception:
                sh = None
            if not sh: return self._send(404, "text/plain", b"no such camera script")
            return self._json(sh)

        if path.startswith("/api/scxwav/"):
            # /api/scxwav/<scene>.SCX/<index>.wav - a sound out of the scene's
            # own streamed section (16-bit PCM mono, so the browser plays it
            # as it stands)
            rest = path[len("/api/scxwav/"):]
            if "/" not in rest:
                return self._send(404, "text/plain", b"want <file>/<index>.wav")
            fn, idx = rest.split("/", 1)
            try: i = int(idx.replace(".wav", ""))
            except ValueError: return self._send(404, "text/plain", b"bad index")
            w = _cut.wav(urllib.parse.unquote(fn), i)
            if not w: return self._send(404, "text/plain", b"no such sound")
            return self._send(200, "audio/wav", w, cache=True)

        if path.startswith("/api/track/"):
            # /api/track/<n>.wav - TRACKS\<n>.ADP, the game's music, decoded
            # by the same ADPCM reader the dialogue voices use
            try: n = int(path.rsplit("/", 1)[1].replace(".wav", ""))
            except ValueError: return self._send(404, "text/plain", b"bad track")
            p2 = omkpaths.data("TRACKS", "%d.ADP" % n)
            if not os.path.exists(p2):
                return self._send(404, "text/plain", b"no such track")
            import adp
            pcm, ch = adp.read(p2)
            return self._send(200, "audio/wav", adp.wav(pcm, ch), cache=True)

        if path == "/api/dialogs":
            return self._json(omkdata.conversations())

        if path.startswith("/api/dialog/"):
            c = omkdata.conversation(int(path.rsplit("/", 1)[1]))
            if not c: return self._send(404, "text/plain", b"no such conversation")
            d = match_decor(c)
            if d: c = dict(c, decor=d[0], decorDist=round(d[1], 1))
            try:
                a = omkdata.dialog_actor(int(path.rsplit("/", 1)[1]))
            except Exception:
                a = None
            if a:
                try:
                    si = omkdata.scene_idle(int(path.rsplit("/", 1)[1]))
                except Exception:
                    si = None
                if si: a = dict(a, sceneIdle=si)
                try:
                    pi = omkdata.scene_idle(int(path.rsplit("/", 1)[1]),
                                            player=True)
                except Exception:
                    pi = None
                if pi: a = dict(a, playerSceneIdle=pi)
                try:
                    if omkdata.dialog_lookat(int(path.rsplit("/", 1)[1])):
                        a = dict(a, lookAtPlayer=True)
                except Exception:
                    pass
                c = dict(c, actor=a)
            return self._json(c)

        if path.startswith("/api/floor/"):
            # /api/floor/<set>?x=&y=&z=  -> the surface under that point
            setname = urllib.parse.unquote(path[len("/api/floor/"):])
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            pt = [float(q.get(k, [0])[0]) for k in ("x", "y", "z")]
            geo = omkdata.decor_geometry_cached(setname)
            if not geo: return self._send(404, "text/plain", b"no such set")
            return self._json({"y": omkdata.floor_under(geo, pt)})

        if path.startswith("/api/place/"):
            did, setname = path[len("/api/place/"):].split("/")
            c = omkdata.conversation(int(did))
            if not c: return self._send(404, "text/plain", b"no such conversation")
            sp = omkdata.speaker_positions(c, urllib.parse.unquote(setname))
            return self._json(sp or {})

        if path == "/api/ctl":
            return self._json(omkdata.list_ctl())

        if path.startswith("/api/ctl/"):
            return self._json(omkdata.ctl_graph(
                urllib.parse.unquote(path[len("/api/ctl/"):])))

        if path == "/api/decors":
            return self._json(omkdata.list_decors())

        if path.startswith("/api/decorgeo/"):
            g = dict(omkdata.decor_geometry_cached(
                urllib.parse.unquote(path[len("/api/decorgeo/"):])) or {}) or None
            if not g: return self._send(404, "text/plain", b"no such set")
            verts = g.pop("verts")
            # 8 floats per vertex: x y z u v r g b. The light is a COLOUR -
            # see omkdata.decor_geometry's `shade`.
            g["stride"] = len(verts[0]) if verts else 8
            blob = struct.pack("<%df" % (len(verts) * g["stride"]),
                               *[c for v in verts for c in v])
            g["count"] = len(verts)
            return self._send(200, "application/octet-stream",
                              self._binpack(g, blob), cache=True)

        if path.startswith("/api/ambientfx/"):
            # a set's neon / smoke / steam emitters, resolved across the .3DO,
            # the .SFX and the .SCX - see tools/ambientfx.py
            import ambientfx
            g = ambientfx.emitters(
                urllib.parse.unquote(path[len("/api/ambientfx/"):]))
            if g is None: return self._send(404, "text/plain", b"no such set")
            return self._json(g)

        if path.startswith("/api/fxtex/"):
            import ambientfx
            name, sprite = path[len("/api/fxtex/"):].split("/", 1)
            png = ambientfx.sprite_png(urllib.parse.unquote(name),
                                       urllib.parse.unquote(sprite))
            if not png: return self._send(404, "text/plain", b"no such sprite")
            return self._send(200, "image/png", png, cache=True)

        if path.startswith("/api/dtex/"):
            name, idx = path[len("/api/dtex/"):].split("/")
            txs = tex3dt.textures(omkdata.decor_path(name))
            idx = int(idx.replace(".png", ""))
            if not (0 <= idx < len(txs)):
                return self._send(404, "text/plain", b"no such material")
            t = txs[idx]
            return self._send(200, "image/png",
                              tex3dt.png(t["w"], t["h"], t["rgb"]), cache=True)

        if path == "/api/anims":
            return self._json(omkdata.list_anims())

        if path.startswith("/api/scxanims/"):
            fn = urllib.parse.unquote(path[len("/api/scxanims/"):])
            import anim_3da
            try:
                st = anim_3da.scx_stream(omkpaths.data("SCPTDATA", fn))
            except Exception:
                return self._send(404, "text/plain", b"no such scene")
            out = []
            for i, a in enumerate(st["anims"]):
                r = anim_3da.descriptor(st["data"], a["offset"], a["declared"])
                out.append({"index": i, "name": a["name"],
                            "frames": r["frames"] if r else 0,
                            "tracks": len(r["tracks"]) if r else 0})
            return self._json(out)

        if path.startswith("/api/anipose/"):
            model, fn, clip = path[len("/api/anipose/"):].split("/")
            meta, blob = omkdata.ani_pose_stream(model, fn, int(clip))
            if not meta: return self._send(404, "text/plain", b"no clip")
            return self._send(200, "application/octet-stream",
                              self._binpack(meta, blob), cache=True)

        if path == "/api/models":
            return self._json(omkdata.models())

        if path.startswith("/api/fullmodel/"):
            g = omkdata.model_geometry(path.rsplit("/", 1)[1])
            if not g: return self._send(404, "text/plain", b"no model")
            return self._json(g)

        if path.startswith("/api/model/"):
            g = omkdata.face_geometry(path.rsplit("/", 1)[1])
            if not g: return self._send(404, "text/plain", b"no face mesh")
            return self._json(g)

        if path.startswith("/api/tex/"):
            name, idx = path[len("/api/tex/"):].split("/")
            idx = int(idx.replace(".png", ""))
            p = os.path.join(omkdata.PERSOS, name + ".3DO")
            if not os.path.exists(p):
                for fn in os.listdir(omkdata.PERSOS):
                    if fn.lower() == (name + ".3do").lower():
                        p = os.path.join(omkdata.PERSOS, fn); break
            txs = tex3dt.textures(p)
            if not (0 <= idx < len(txs)):
                return self._send(404, "text/plain", b"no such material")
            t = txs[idx]
            return self._send(200, "image/png",
                              tex3dt.png(t["w"], t["h"], t["rgb"]), cache=True)

        if path.startswith("/api/pose/"):
            model, asset = path[len("/api/pose/"):].split("/")
            meta, blob = omkdata.pose_stream(model, asset)
            if not meta: return self._send(404, "text/plain", b"no pose")
            return self._send(200, "application/octet-stream",
                              self._binpack(meta, blob), cache=True)

        if path.startswith("/api/morph/"):
            asset = path.rsplit("/", 1)[1]
            meta, blob = omkdata.morph_frames(asset)
            if not meta: return self._send(404, "text/plain", b"no morph")
            return self._send(200, "application/octet-stream",
                              self._binpack(meta, blob), cache=True)

        if path.startswith("/api/audio/"):
            asset = path.rsplit("/", 1)[1].replace(".wav", "")
            wav = omkdata.morph_audio(asset)
            if not wav: return self._send(404, "text/plain", b"no audio")
            return self._send(200, "audio/wav", wav, cache=True)

        if path == "/world" or path == "/world/":
            return self._world_index()
        if path.startswith("/world/"):
            parts = path.split("/")
            if len(parts) == 4:
                return self._world_chunk(parts[2].upper(), int(parts[3]))
            return self._send(404, "text/plain", b"want /world/AREA/22")

        if path.startswith("/api/script/"):
            # /api/script/<dialog>/<offset>?vars={...}
            # Runs one branch script on the bytecode VM and reports the trace
            # and the resulting variables, so the page never has to interpret
            # anything itself.
            _, _, _, did, off = path.split("/")
            b = omkdata.chunk_bytes(int(did))
            if b is None: return self._send(404, "text/plain", b"no chunk")
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            try: state = json.loads(q.get("vars", ["{}"])[0])
            except ValueError: state = {}
            log = []
            vm = omkdialog.VM({int(k): int(v) for k, v in state.items()}, log.append)
            res = vm.run(b, int(off))
            return self._json({"result": res, "log": log, "vars": vm.vars})

        self._send(404, "text/plain", b"not found")

    # ------------------------------------------------------------ the home page
    # Server-rendered on purpose, like /world: it is an index, it needs no
    # state, and counting the shipped data at request time means the numbers
    # cannot drift from what the tools actually read.

    _HOME_CSS = (
        ":root{--bg:#12141a;--panel:#1a1d26;--line:#2a2f3d;--fg:#d8dce6;"
        "--dim:#8a91a3;--acc:#6fb3ff}"
        "*{box-sizing:border-box}"
        "body{margin:0;background:var(--bg);color:var(--fg);"
        "font:14px/1.6 ui-monospace,SFMono-Regular,Menlo,monospace}"
        ".wrap{max-width:900px;margin:0 auto;padding:48px 20px 64px}"
        "h1{font-size:22px;margin:0 0 6px;color:#fff;letter-spacing:.02em}"
        "p.sub{color:var(--dim);margin:0 0 34px}"
        "a{color:var(--acc);text-decoration:none}a:hover{text-decoration:underline}"
        ".cards{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(270px,1fr))}"
        ".card{display:block;background:var(--panel);border:1px solid var(--line);"
        "border-radius:10px;padding:16px 18px;color:inherit;transition:.12s}"
        ".card:hover{border-color:var(--acc);text-decoration:none;transform:translateY(-1px)}"
        ".card h2{margin:0 0 6px;font-size:15px;color:#fff}"
        ".card .d{color:var(--dim);font-size:12.5px;margin:0 0 10px}"
        ".card .n{color:var(--acc);font-size:12px}"
        ".foot{margin-top:38px;color:var(--dim);font-size:12.5px;"
        "border-top:1px solid var(--line);padding-top:16px}"
        ".foot code{color:#c8ccd4}"
        ".g{display:grid;grid-template-columns:auto 1fr;gap:2px 14px;margin-top:8px}"
        ".g b{color:#c8ccd4;font-weight:400}")

    def _home(self):
        import html
        try:    nshots = sum(len(s["shots"]) for s in _cut.index())
        except Exception: nshots = 0
        try:    nscenes = len(_cut.files())
        except Exception: nscenes = 0
        nareas = len(_archive(os.path.join(omkdata.TAGDIR, "AREA")))
        nscn = len(_archive(os.path.join(omkdata.TAGDIR, "SCENE")))
        cards = [
            ("/dialog", "Conversations",
             "Play a conversation the way the engine stages it: the text and "
             "its branches, the ADPCM voice, the speaker posed from the "
             "line&rsquo;s own <code>.3DM</code>, the set, the dialogue "
             "cameras and the <code>.CTL</code> state machine.",
             "%d conversations &middot; %d character models"
             % (len(omkdata.conversations()), len(omkdata.models()))),
            ("/cutscene", "Cutscenes",
             "The in-engine cutscenes: a scene object&rsquo;s program driving "
             "the characters while its chunk-10 <i>camera editing</i> flies "
             "the shot. The camera path is <code>Cam_PlayEditing</code>&rsquo;s "
             "own interpolation, frame for frame.",
             "%d shots in %d scenes" % (nshots, nscenes)),
            ("/ui", "Interface",
             "Drive the game&rsquo;s own menus: the widget tree walked with "
             "the engine&rsquo;s fourteen input bits, every rectangle an item "
             "record at its real coordinates over the shipped artwork. The "
             "start menu&rsquo;s answer is <i>derived</i>, not supplied.",
             "%d screens &middot; 5 panels modelled" % 37),
            ("/world", "World scripts",
             "The bytecode in <code>IAM\\AREA</code>, <code>IAM\\SCENE</code> "
             "and <code>IAM\\GLOBAL</code> as annotated listings &mdash; every "
             "operand resolved through the <code>.TAG</code> tables, every "
             "<code>dialog.start</code> linking into the player.",
             "%d areas &middot; %d scenes &middot; GLOBAL" % (nareas, nscn)),
        ]
        out = ["<div class='wrap'><h1>Omikron &mdash; The Nomad Soul</h1>",
               "<p class='sub'>Reading the 1999 game&rsquo;s own data with the "
               "formats recovered from its executable. Everything here is "
               "decoded out of <code>gamedata/</code> at request time &mdash; nothing "
               "is exported or converted in advance.</p>",
               "<div class='cards'>"]
        for href, title, desc, n in cards:
            out.append("<a class='card' href='%s'><h2>%s</h2>"
                       "<p class='d'>%s</p><div class='n'>%s</div></a>"
                       % (href, title, desc, n))
        out.append("</div>")
        out.append("<div class='foot'>The findings these pages rest on are in "
                   "<code>docs/</code>: <b>FILE_FORMATS</b>, <b>ASSETS</b>, "
                   "<b>SCRIPT_VM</b>, <b>GAME_STATE</b> and <b>CUTSCENES</b>. "
                   "<code>python3 tools/verify.py</code> asserts every number "
                   "they quote.<div class='g'>"
                   "<b>build</b><span>%s</span></div></div></div>" % BUILD)
        page = ("<!doctype html><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Omikron data viewers</title><style>%s</style><body>%s"
                % (self._HOME_CSS, "".join(out)))
        return self._send(200, "text/html; charset=utf-8", page.encode())

    # ------------------------------------------------ the world-script pages
    # Phase 1 named 99.9% of the executed instructions, which makes the world
    # scripts in IAM\AREA / SCENE / GLOBAL readable as the game's logic. These
    # pages serve tools/script_dump.py's annotated listings so they can be
    # read next to the conversations they launch - every `dialog.start` links
    # into the player. Server-rendered on purpose: the client stays untouched.

    _WORLD_CSS = ("body{background:#14161a;color:#c8ccd4;font:13px/1.45 ui-monospace,Menlo,monospace;"
                  "margin:24px auto;max-width:1000px;padding:0 16px}"
                  "a{color:#7ab3e0;text-decoration:none}a:hover{text-decoration:underline}"
                  "h1{font-size:16px;color:#e8eaee}h2{font-size:13px;color:#9aa3b0;margin:18px 0 4px}"
                  ".c{color:#6f7a68}.op{color:#d8b56a}.lbl{color:#5a80a8}"
                  "pre{margin:0 0 14px}span.tgt{color:#c07070}"
                  ".cols{columns:3;-webkit-columns:3}"
                  ".cols div{break-inside:avoid}")

    def _html(self, title, body):
        page = ("<!doctype html><meta charset='utf-8'><title>%s</title>"
                "<style>%s</style><body>%s" % (title, self._WORLD_CSS, body))
        return self._send(200, "text/html; charset=utf-8", page.encode())

    def _world_index(self):
        import html
        out = ["<h1>world scripts</h1>",
               "<p class='c'>the bytecode in IAM\\AREA, IAM\\SCENE and IAM\\GLOBAL, "
               "rendered by tools/script_dump.py &mdash; "
               "<a href='/'>home</a> &mdash; "
               "<a href='/dialog'>the conversation player</a></p>"]
        out.append("<h2><a href='/world/GLOBAL/0'>GLOBAL</a> &mdash; the ambient scripts</h2>")
        for arch, tag in (("AREA", "AREAS"), ("SCENE", "SCENES")):
            names = omkdata.TAGS.get(tag, {})
            keys = sorted(_archive(os.path.join(omkdata.TAGDIR, arch)).keys())
            out.append("<h2>%s &mdash; %d chunks</h2><div class='cols'>" % (arch, len(keys)))
            for k in keys:
                nm = html.escape(names.get(k, ""))
                out.append("<div><a href='/world/%s/%d'>%d</a> %s</div>" % (arch, k, k, nm))
            out.append("</div>")
        return self._html("world scripts", "".join(out))

    def _world_chunk(self, arch, chunk):
        import html, re
        try:
            b, scripts = _sdump.scripts_of(arch, chunk)
        except KeyError:
            return self._send(404, "text/plain", b"no such chunk")
        tag = {"AREA": "AREAS", "SCENE": "SCENES"}.get(arch)
        name = omkdata.TAGS.get(tag, {}).get(chunk, "") if tag else "the ambient scripts"
        out = ["<h1>%s %s &mdash; %s</h1>" % (arch, "" if arch == "GLOBAL" else chunk,
                                              html.escape(name) or "(unnamed)"),
               "<p class='c'><a href='/world'>all chunks</a> &mdash; "
               "<a href='/'>home</a> &mdash; "
               "<a href='/dialog'>conversation player</a> &mdash; "
               "%d scripts</p>" % len(scripts)]
        for label, off in scripts:
            text = html.escape(_sdump.listing(b, off, label))
            # dialogue launches link into the player
            text = re.sub(r"(dialog\.start   )(\s*)(\d+)",
                          lambda m: "%s%s<a href='/dialog#dialog=%s'>%s</a>"
                                    % (m.group(1), m.group(2), m.group(3), m.group(3)),
                          text)
            text = re.sub(r"(;.*)$", r"<span class='c'>\1</span>", text, flags=re.M)
            text = re.sub(r"^(  [ >] +\d+  )(\S+)", r"\1<span class='op'>\2</span>",
                          text, flags=re.M)
            out.append("<pre>%s</pre>" % text)
        return self._html("%s %s" % (arch, chunk), "".join(out))

def main():
    port = 8752
    if "--port" in sys.argv: port = int(sys.argv[sys.argv.index("--port") + 1])
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    print(f"omkweb: http://localhost:{port}   (ctrl-c to stop)")
    print(f"  build {BUILD}")
    print(f"  {len(omkdata.conversations())} conversations, "
          f"{len(omkdata.models())} character models")
    try: srv.serve_forever()
    except KeyboardInterrupt: print()

if __name__ == "__main__":
    main()

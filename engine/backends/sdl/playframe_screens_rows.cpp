// SPDX-License-Identifier: GPL-3.0-or-later
// THE SCREENS' ROWS: the sneak, the shops, Multiplan, the hints, the puzzles, the terminals, the lift.
// Parts of `PlayState::phaseScreens`, moved byte for byte by `todo/play-split.md`
// (2026-10-02); the phase calls them in this order.
#include "playframe.h"

// The sneak's inventory rows
void PlayState::screensSneakRows() {
    const auto& fs = *fs_;
    auto& comp = *comp_;
    auto& inv = *inv_;
    auto& session = *session_;
    // ---- THE SNEAK'S INVENTORY ROWS ------------------------------
    //
    // The nine row widgets of list 0x004DE6F0 belong to the DEVICE, not
    // to one page: several pages carry the same list, and what a row
    // shows is whatever that page's own code wrote into it -
    // `sub_42AA00` reads the row's `+60` tag and asks the channel for a
    // name with `Game_RaiseEvent(33, ...)`. So the text has to follow the
    // PANEL the walk is on and not the screen. Filled once at the open,
    // it showed the carried items on the "Memoire" tab as well, which is
    // a different list - caught by looking at the tab, not by a number.
    //
    // Only the inventory page is filled. The other pages' rows are the
    // player's bio, his statistics and the memos, and which list each one
    // asks for has not been read: an empty row says so, where the carried
    // list would be a plausible-looking wrong answer.
    //
    // The channel, in the order the interface asks it: case 29 for the
    // count and case 33 for each name, both refusing (result 3) while no
    // list is open - which is why the open raised event 25 first.
    if (walk && openScreen == omk::kScreenSneak) {
        // `sub_49BEA0` (Utiliser) writes the screen slot's state word to
        // **3** and returns 1 when `sub_42B470` returned 1 - and
        // docs/UI.md's state machine says 3 is CLOSING. So a successful
        // use CLOSES THE SNEAK. Acted on at the end of this block, where
        // nothing else still holds `walk` or its panel.
        bool useClosedSneak = false;
        sneakRows.clear();
        sneakHidden.clear();
        const omk::UiPanel* pn = walk->panel();
        // ---- THE SLIDER PAGE, and WHICH SOURCE fills the rows -----
        //
        // One global picks it: `dword_670CB8`, written by each page's
        // `panel+4` builder - 0 inventory, 2 memory, **4 slider** - and
        // `sub_42ADD0` branches on it. For 0 and 2 it raises the
        // inventory channel's event 25 with that number as the list id;
        // for 4 it raises NOTHING and instead sets flag `0x1000` on every
        // row widget, which is the flag `sub_42AA00` tests to take its
        // text from `sub_40E540(tag)` rather than from case 33.
        //
        // `sub_40E540`, and `sub_40E8E0` for the count, walk
        // `GLOBAL +16` (36-byte records, count `+28`) and keep only the
        // entries whose bit is set in the DB's `+24` array - which is
        // `StateArray::AddressEnabled`, what VM ops 87/88 write. So the
        // page lists the places the game has given the player, and a
        // capture of the original shows exactly that: four rows.
        // WHICH SOURCE, not which panel. `dword_670CB8` is what the
        // engine dispatches on, and the row bindings are a static record
        // that survives a descent - so the names stay put when an object
        // is chosen and the walk moves to the verb panel.
        const int rowKind = walk->rowKind();
        if (rowKind == 4) {
            std::vector<std::string> known;
            std::vector<const omk::Destination*> knownRecs;   // the same rows, for the listing
            for (const auto& d : destinations)
                if (state.bit(omk::StateArray::AddressEnabled, d.bit)) {
                    known.push_back(d.name); knownRecs.push_back(&d);
                }
            const omk::UiPanel* rp = w.at(omk::kPanelSneakSlider);
            for (const auto& l : (rp ? rp->lists : pn->lists)) {
                if (l.addr != omk::kListSneakRows) continue;
                for (std::size_t k = 0; k < l.items.size(); ++k) {
                    if (k >= known.size()) {
                        sneakHidden.insert(l.items[k].addr);   // sub_42AAE0
                        continue;
                    }
                    sneakRows[l.items[k].addr] = known[k];
                }
            }
            // `sub_42AAE0`'s OTHER half: the rows past the end are not
            // selectable either, so the walk must not put the highlight
            // on one. Without this the selection walks off the end of
            // the live destinations and the cursor goes with it.
            // CARRYING the window, not resetting it. This runs every
            // frame, so passing 0 would scroll the list back to the top
            // between the keypress and the next draw.
            walk->bindRows(omk::kListSneakRows,
                           static_cast<int>(known.size()),
                           walk->rowWindow(omk::kListSneakRows));
            if (!sliderTold) {
                sliderTold = true;
                std::printf("sneak: slider page - %zu of %zu destinations "
                            "enabled (GLOBAL +16, DB +24)\n",
                            known.size(), destinations.size());
                for (std::size_t r = 0; r < known.size(); ++r)
                    std::printf("   row %zu: '%s' (area %d, address bit %d)\n", r,
                                knownRecs[r]->name.c_str(), knownRecs[r]->area, knownRecs[r]->bit);
            }
        }
        // ---- THE EXAMINE PAGE'S CONTENT -------------------------
        //
        // Which object is being examined is the ROW the walk was on when
        // "Examiner" was confirmed, and the row list keeps its selection
        // (it is a static record), so it is still there. `Game_HandleEvent`
        // case 40 then dispatches on that object's own kind.
        // ---- A VERB WAS CONFIRMED -------------------------------
        //
        // `sub_42B470`'s decision is the record's own `+4 & 1`, which is
        // `usable()`: yes and the engine runs
        // `Object_ApplyEffect(rec, Actor_IdBySlot(Actor_Player()))`, no
        // and it plays interface sound 13 and does nothing else. The
        // REFUSAL is ported whole; the apply is announced and not run,
        // because `Object_ApplyEffect`'s body is `named` and not read and
        // `sub_409780`'s context gate - whether the object may be used
        // HERE - has not been read at all.
        // ---- THE COMBINE, once both slots are full ----------------
        //
        // `Game_HandleEvent` case 37's SECOND arm:
        //
        //     recipe = sub_409650(second, first)
        //     if (!recipe || dword_4E6C70 != recipe+6) { result 2; }
        //     else { ObjectList_RemoveById(list, first);
        //            ObjectList_RemoveById(list, second);
        //            ObjectList_InsertFront(list, recipe+4, 0, 0);
        //            dword_4E6C70 = -1; }
        //
        // and `sub_49BC60`'s tail plays interface sound 12 on the success
        // and shows text 35 on the failure, then reinstalls the inventory
        // page either way. The gate is the one `beginCombine` set.
        // THE INTERFACE SOUNDS BY THEIR OWN ID. `sub_482D90(n)` takes an
        // index into the 45-row table (`tables/ui.json` sounds), not a
        // screen's slot - so 12 is `SNK009` and 13 `SNK012`, where
        // `sndConfirm` / `sndBack` are the screen's slots 1 and 2 (`SNK003`
        // and `SNK001` on the sneak). A reader: *"there should be a sound
        // effect when a new object is created"*.
        const auto uiSound = [&](int id) -> const std::vector<float>& {
            static std::map<int, std::vector<float>> cache;
            auto it = cache.find(id);
            if (it == cache.end()) {
                std::vector<float> v;
                const std::string& nm = w.soundNameById(id);
                if (!nm.empty())
                    if (const auto path = fs.resolve("I2D/sounds/" + nm + ".wav"))
                        v = wavToDevice(omk::DataFs::readPath(*path), kDeviceRate);
                it = cache.emplace(id, std::move(v)).first;
            }
            return it->second;
        };
        if (int ra = -1, rb = -1; walk->takeCombine(ra, rb)) {
            const auto bag = omk::objectList(state, omk::ObjectList::Carried);
            const auto at = [&](int r) {
                return r >= 0 && static_cast<std::size_t>(r) < bag.size()
                     ? bag[static_cast<std::size_t>(r)] : -1;
            };
            const int a = at(ra), b = at(rb);
            const int gate = (a == omk::globalSpellItem(globalFile) ||
                              b == omk::globalSpellItem(globalFile)) ? 1 : 0;
            const int made = (a >= 0 && b >= 0) ? inv.combine(a, b, gate) : -1;
            if (made > 0) {
                state.listRemove(0, a);
                state.listRemove(0, b);
                state.listAdd(0, made);          // InsertFront
                blip(uiSound(12));               // `push 0Ch; call sub_482D90`
                std::printf("sneak: combine %d '%s' + %d '%s' (gate %d) -> "
                            "%d '%s'\n", a, session.objectName(a).c_str(),
                            b, session.objectName(b).c_str(), gate,
                            made, session.objectName(made).c_str());
            } else {
                blip(sndBack);
                std::printf("sneak: combine %d '%s' + %d '%s' (gate %d) -> "
                            "nothing - no recipe, or its gate is not %d "
                            "(interface text 35)\n",
                            a, session.objectName(a).c_str(),
                            b, session.objectName(b).c_str(), gate, gate);
            }
            walk->endCombine();
            // read back from the walk, after the combine closed: is the
            // verb still flashing, and which list holds the focus
            std::printf("sneak: combine closed - `Utiliser sur` %s, focus on list %d\n",
                        (walk->itemFlagsOn(omk::kItemSneakUseOn) & 0x2u) ? "STILL FLASHING"
                                                                         : "no longer lit",
                        walk->currentList());
        }
        if (const int verb = walk->takeVerb(); verb >= 0) {
            const auto carried =
                omk::objectList(state, omk::ObjectList::Carried);
            // THE ROW TAG, not the widget index. All three verb
            // callbacks read `selected_widget[+0x3C]`, which is
            // `widget + window`; the two agree only while the rows are
            // unscrolled, and reading the selection applied the verb to
            // whatever had been under the cursor before the scroll.
            const int row = walk->selectedRow(omk::kListSneakRows);
            const omk::ObjectRecord* rec = nullptr;
            if (row >= 0 && static_cast<std::size_t>(row) < carried.size()) {
                const int idx = carried[static_cast<std::size_t>(row)];
                if (idx >= 0 &&
                    static_cast<std::size_t>(idx) < objectRecords.size())
                    rec = &objectRecords[static_cast<std::size_t>(idx)];
            }
            if (!rec) {
                std::printf("sneak: %s with no object selected\n",
                            verb ? "Utiliser sur" : "Utiliser");
            } else {
              // the OBJECTS id of the selected row - both halves need it
              const int objIdx = carried.empty() ? -1
                  : carried[static_cast<std::size_t>(row)];
              if (verb == 1) {
                // ---- `Utiliser sur` IS A MODE, not a use --------------
                //
                // `sub_49BF30` does not touch case 35 at all: it opens a
                // COMBINE, puts the object in one of two slots, disables
                // the verb list and sends the player back to the rows for
                // a second object. Running `Utiliser`'s arm here - which
                // this did until 2026-09-04 - took the object IN HAND
                // under the other verb's name.
                //
                // Which slot is `sub_42B520`'s answer: event 37's first
                // arm compares the object with `u16(GLOBAL, 64)`, the
                // spell item, and sets the recipe gate to 1 for it and 0
                // for anything else.
                const int spellItem = omk::globalSpellItem(globalFile);
                const bool isSpell = (objIdx == spellItem);
                // The slot takes the ROW, not the object id: `sub_49BF30`
                // stores the selected widget's row tag (`[+3Ch]`), the
                // second pick stores its row the same way, and the
                // resolver below maps BOTH through the carried list.
                // Handing it `objIdx` made the first slot "row 18" - a
                // row that does not exist - so every combine came back
                // `-1 ''` and nothing could ever be made. The spell test
                // above still reads the object id, which is what event
                // 37 compares.
                walk->beginCombine(row, isSpell);
                std::printf("sneak: Utiliser sur '%s' -> combine opened, "
                            "gate %d%s. Pick a second object\n",
                            rec->name.c_str(), isSpell ? 1 : 0,
                            isSpell ? " (the spell item - and NO shipped "
                                      "recipe carries gate 1, so this arm "
                                      "cannot produce anything)" : "");
              } else if (verb == 0) {
                // ---- WHAT REACHES THE WORLD -------------------------
                //
                // `sub_42B420(tag, 20)` announces, and its second event
                // is 43 - whose block starts at the ACTION, so case 43
                // runs `Message_RunHandlers(20, ..., object, ...)`. That
                // walks the resident SCENE's subscription table, then the
                // AREA's, then GLOBAL's, first match wins; and this
                // header already recorded that a message's sender is an
                // OBJECT id for 4, 20 and 25.
                //
                // So `Utiliser` posts message 20 with the object, and
                // whichever resident chunk subscribes to it decides -
                // which is why a player says the key "is automatically
                // used when you are near the location where you should
                // use it": proximity is which SCENE is resident, and the
                // handler is its own. Nothing in the Session posted a
                // message before this.
                const bool ran = session.postMessage(20, objIdx);
                const auto& m = session.messagesRun();
                std::printf("sneak: Utiliser '%s' -> message 20, sender "
                            "object %d - %s\n", rec->name.c_str(), objIdx,
                            ran && !m.empty()
                              ? (m.back().table + " table handles it").c_str()
                              : "no resident chunk subscribes to it");
              }
              // ...and THEN the decision. `sub_49BEA0` calls
              // `sub_42B420` (the announce, above) and `sub_42B470` (this)
              // in that order, so both happen on one confirm.
              //
              // `Utiliser`'s ONLY. `Utiliser sur` is `sub_49BF30`, a
              // different callback that opens the combine and returns -
              // it never calls `sub_42B470`. This block used to run for
              // BOTH verbs, so `Utiliser sur` opened the combine and then,
              // on the same press, took the object IN HAND and closed the
              // device. A reader: *"Utiliser sur does not work correctly
              // (it means using on another object in the sneak, not
              // interacting with the environment)"*.
              if (verb != 0) {
                // the combine is open; the rows are waiting for object two
              } else if (!rec->usable()) {
                // THE ARM THAT WORKS, and it is the one WITHOUT the
                // usable bit. Case 35's `loc_407314` loads the object's
                // own model from its stem and returns result **1**, and
                // `sub_42B470` then runs
                // `sub_41C490(dword_930724, tag)`, which writes
                // `player[+0xA4] = &unk_4E7EA0[tag * 96]` and attaches
                // the model to him. So "Utiliser" on a key TAKES IT IN
                // HAND - which is what a player then carries to a door.
                // ...and it IS the hand now. `Session::useObject` is
                // case 35's arm: allocate a `word_4E6CA0` slot for the
                // id, drop the item from list 0, and hold that slot -
                // which is exactly what `var.set.used_object` (75) reads
                // back. The MODEL attach (`sub_437400`/`sub_4374E0`
                // inside `sub_41C490`) is the renderer's half and is
                // still not done, so nothing appears in his hand.
                const int slot = session.useObject(objIdx);
                std::printf("sneak: '%s' -> IN HAND, slot %d (case 35 "
                            "result 1, sub_41C490 sets player+0xA4). A "
                            "zone whose activate script reaches opcode 75 "
                            "will now see object %d; the model attach is "
                            "not ported, so it is invisible\n",
                            rec->name.c_str(), slot, objIdx);
                // `sub_42B470` returns 1 on this arm alone; `sub_49BEA0`
                // turns that into `[slot+8] = 3`.
                useClosedSneak = true;
            } else {
                // ---- THE CONSUMABLE ARM, and it now CONSUMES ----------
                //
                // `Game_HandleEvent` case 35, the `rec+4 & 1` branch:
                //
                //     Object_ApplyEffect(rec, Actor_IdBySlot(Actor_Player()));
                //     ObjectList_RemoveAt(0, row);        // result 2
                //
                // `Object_ApplyEffect` (0x00409780), the consumable half
                // (`rec+4 & 0x20` clear): `rec+6` picks the property
                // (`omk::effectProperty` - 6 is *Vie*), `rec+8` is added
                // to the current value through `Actor_GetProperty` /
                // `Actor_SetProperty`, and a sum past 0xFFFF becomes
                // 0xFFFF (`v7 = -1`). The setter applies its own per-
                // property clamp, which `writeActorProperty` carries.
                //
                // This used to ANNOUNCE the apply and run nothing - "the
                // apply is announced and not run" - so a medkit did
                // nothing and stayed in the bag. A reader: *"impossible to
                // use health items"*.
                //
                // NOT ported, labelled: the `rec+4 & 0x20` arm (weapons,
                // ammunition, seteks, rings - the valuables), which writes
                // property 35's ammunition slots and 4/5; nothing usable
                // AND valuable reaches it through this verb in practice,
                // and it is left announcing rather than guessed.
                const int prop = omk::effectProperty(rec->effect);
                std::int32_t before = -1, after = -1;
                if (prop > 0 && !rec->valuable()) {
                    const auto pr = state.rawMutable().subspan(
                        static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                        static_cast<std::size_t>(omk::GameState::kPlayerRecordSize));
                    omk::readActorProperty(pr, prop, before);
                    std::int32_t next = before + rec->amount;
                    if (next > 0xFFFF) next = 0xFFFF;             // `v7 = -1`
                    omk::writeActorProperty(pr, prop, next);
                    omk::readActorProperty(pr, prop, after);      // the setter's clamp
                    state.listRemove(0, objIdx);                  // ObjectList_RemoveAt(0, row)
                }
                blip(uiSound(13));               // `sub_42B470`: interface sound 13
                if (after >= 0)
                    std::printf("sneak: '%s' USED - property %d %d -> %d "
                                "(+%d, effect %d), and it leaves the bag "
                                "(case 35 result 2)\n",
                                rec->name.c_str(), prop, before, after,
                                rec->amount, rec->effect);
                else
                    std::printf("sneak: '%s' is a consumable on the VALUABLES "
                                "arm of Object_ApplyEffect (effect %d), which "
                                "is not ported - announced, not run\n",
                                rec->name.c_str(), rec->effect);
                // `sub_42B470` returned 0, so `sub_49BEA0` takes
                // `loc_49BEF8`: reset the row list and
                // `sub_42A370(screen, unk_4DEE50)` - back to the
                // INVENTORY page, screen still open.
                walk->installPanel(omk::kPanelSneakInventory);
              }
            }
        }
        comp.setExamineText(nullptr);
        // ---- THE CITY MAP (`ui/citymap.h`) ---------------------------
        //
        // `Lire plan` installs panel 0x004DF190, whose open hook
        // `sub_49D9E0` loads `Images\\<resident set>.bmp` and matches the
        // uppercased stem against the compiled four-row table. Everything
        // the two draw hooks then need is resolved HERE, because none of
        // it is a property of the widget tree: the bitmap, the city row,
        // where the player stands and which way he faces, and the markers.
        //
        // THE MARKERS are the ENABLED slider destinations whose names
        // begin with the city's - the same `GLOBAL +16` list the slider
        // page shows, filtered by the DB's AddressEnabled bits. The
        // engine positions each through the 15-row OVERRIDE table first
        // and falls back on `sub_40E630`, which is the TRANSPORT and
        // would `Area_Load` from a draw hook; the port instead resolves
        // only against the RESIDENT chunk's own address table and counts
        // what it had to drop (`ui/citymap.h` says why that costs nothing
        // in the shipped data: every destination of a city carries that
        // city's area id, so the engine takes its same-area fast path).
        static omk::ScreenComposer::CityMapView cityView;
        comp.setCityMap(nullptr);
        if (pn && pn->addr == omk::kPanelSneakMap) {
            static std::string cityBmpStem;
            static omk::Surface cityBmp;
            const std::string stem = session.setName();
            if (stem != cityBmpStem) {
                cityBmpStem = stem;
                // `sprintf("Images\\%s.bmp")` then `fopen`. DataFs
                // resolves case-insensitively, which is what the shipped
                // lower-case `anekbah.bmp` against an upper-case set name
                // needs.
                cityBmp = omk::surfaceFromBmp(fs.read("IMAGES/" + stem + ".bmp"));
            }
            cityView = omk::ScreenComposer::CityMapView{};
            cityView.sheet = cityBmp.valid() ? &cityBmp : nullptr;
            cityView.row = cityMaps.findCity(stem);
            cityView.playerX = session.playerPos()[0];
            cityView.playerZ = session.playerPos()[2];
            cityView.playerFacing = session.playerYaw();
            {
                const auto raw = state.raw();
                const std::size_t rec =
                    static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                for (std::size_t i = rec + 8;
                     i < raw.size() && raw[i] != std::byte{0}; ++i)
                    cityView.playerName.push_back(static_cast<char>(raw[i]));
            }
            int unplaced = 0;
            if (cityView.row) {
                const auto& rs = session.residentSlot(session.activeSlot());
                for (const auto& d : destinations) {
                    if (!state.bit(omk::StateArray::AddressEnabled, d.bit)) continue;
                    const std::string place =
                        omk::cityMapPlaceName(d.name, cityView.row->name);
                    if (place.empty()) continue;
                    omk::ScreenComposer::CityMapView::Marker mk;
                    mk.name = place;
                    if (const omk::CityPlaceRow* pr = cityMaps.findPlace(place)) {
                        mk.x = pr->pos[0];
                        mk.z = pr->pos[2];
                    } else {
                        const omk::Address* ad = nullptr;
                        for (const auto& x : rs.addresses)
                            if (x.id == d.bit) ad = &x;
                        if (!ad) { ++unplaced; continue; }
                        mk.x = ad->pos[0];
                        mk.z = ad->pos[2];
                    }
                    cityView.markers.push_back(mk);
                }
            }
            comp.setCityMap(&cityView);
            static std::string mapTold;
            const std::string said =
                "set '" + stem + "' -> " +
                (cityBmp.valid() ? "Images/" + stem + ".bmp " +
                     std::to_string(cityBmp.w) + "x" + std::to_string(cityBmp.h)
                                 : std::string("no bitmap")) +
                ", city " + (cityView.row ? cityView.row->name + " id " +
                             std::to_string(cityView.row->id)
                                          : std::string("none (tag -1)")) +
                ", " + std::to_string(cityView.markers.size()) +
                " markers, " + std::to_string(unplaced) + " unplaced";
            if (said != mapTold) {
                mapTold = said;
                std::printf("sneak map: %s\n", said.c_str());
            }
        }
        // ---- THE IDENTITY PAGE'S SHEET (todo/sneak.md §5e step 2) -------
        //
        // What `Actor_GetProperty` (event 44) hands the identity hooks for
        // the player, read where it reads it - the character record, which
        // for the player is the DB's at `+60`. Cases 0/6/9..13 return a
        // POINTER into the record (`+108`, `+8`, `+128`, `+136`, `+40`,
        // `+116`, `+124`), 14 and 15 the two pointers the record itself
        // holds at `+4` and `+0` - the bio strings `State_Apply` plants.
        // All are C strings: read up to their NUL.
        static omk::ScreenComposer::PlayerSheet playerSheet;
        comp.setPlayerSheet(nullptr);
        if (pn && pn->addr == omk::kPanelSneakIdentity) {
            static const auto sneakText = omk::iamStrings(fs, "IAM/Sneak");
            playerSheet.text = sneakText;
            const auto raw = state.raw();
            const auto cstr = [&](std::size_t at) {
                std::string s;
                for (std::size_t i = at; i < raw.size() && raw[i] != std::byte{0}; ++i)
                    s.push_back(static_cast<char>(raw[i]));
                return s;
            };
            const std::size_t rec = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
            const auto u32at = [&](std::size_t at) {
                std::uint32_t v = 0;
                for (int k = 0; k < 4; ++k)
                    v |= static_cast<std::uint32_t>(raw[at + k]) << (8 * k);
                return v;
            };
            playerSheet.str = {
                {0, cstr(rec + 108)}, {6, cstr(rec + 8)},   {9, cstr(rec + 128)},
                {10, cstr(rec + 136)}, {11, cstr(rec + 40)}, {12, cstr(rec + 116)},
                {13, cstr(rec + 124)},
                // 14 and 15 dereference the record's `+4` and `+0`, which
                // `State_Apply` plants as the two bio blocks at image 592
                // and 336 (`GameState::relocate`). The viewer's copy is not
                // relocated - its `+0`/`+4` still hold what the save
                // stored - so the blocks those pointers always designate
                // are read directly.
                {14, cstr(static_cast<std::size_t>(omk::GameState::kBio[1]))},
                {15, cstr(static_cast<std::size_t>(omk::GameState::kBio[0]))},
            };
            static bool bioTold = false;
            if (!bioTold) {
                bioTold = true;
                std::printf("sneak: identity bio pointers as stored +0 %#x +4 %#x "
                            "(State_Apply plants %d / %d)\n", u32at(rec + 0), u32at(rec + 4),
                            omk::GameState::kBio[0], omk::GameState::kBio[1]);
            }
            const auto i16at = [&](std::size_t at) {
                return static_cast<int>(static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(raw[at]) |
                    static_cast<std::uint16_t>(static_cast<std::uint16_t>(raw[at + 1]) << 8)));
            };
            // The integers event 44 answers from the same record, for both
            // contents: 8 the age, and the Caracteristiques - 1 Energie
            // `+170`, 16 Attaque `+160`, 19 Maitrise `+166`, 17 Resistance
            // `+162`, 3 Vitesse `+158`, 18 Esquive `+164`, 2 Mana `+156`.
            playerSheet.num = {{8, i16at(rec + 154)}, {1, i16at(rec + 170)},
                               {16, i16at(rec + 160)}, {19, i16at(rec + 166)},
                               {17, i16at(rec + 162)}, {3, i16at(rec + 158)},
                               {18, i16at(rec + 164)}, {2, i16at(rec + 156)}};
            static std::string characteristicsTold;
            std::string cs;
            for (int p : {1, 16, 19, 17, 3, 18, 2})
                cs += " | " + std::to_string(p) + "=" + std::to_string(playerSheet.num[p]);
            if (cs != characteristicsTold) {
                characteristicsTold = cs;
                std::printf("sneak: characteristics sheet%s (rank string %d)\n", cs.c_str(),
                            playerSheet.num[19] / 41 <= 4 ? 36 + playerSheet.num[19] / 41 : -1);
            }
            if (const omk::UiList* tabs = w.listAt(omk::kListSneakTabs))
                if (!tabs->items.empty())
                    for (int c = 0; c < 3; ++c)
                        playerSheet.icon[c] = static_cast<std::uint8_t>(tabs->items.front().rgb[c]);
            comp.setPlayerSheet(&playerSheet);
            // ---- THE CHARACTER VIEW (`sub_4778E0`, step 4) ---------------
            //
            // Built once per player model - the engine builds it in the
            // sneak's open; only this page shows it. The bank is the
            // LITERAL "F1AVNT.CTL" the open pushes (0x004DF5EC), whatever
            // the player's own; `Cef_DefaultGroup` is the first group with
            // flag 1 and `Cef_DefaultClip` that group's flag-0x20 entry's
            // clip, NOT followed through a goto; `Anim_SetFrame(node, clip,
            // 0.0, 1.0)` applies frame 1 - key 2, key 0 being the rest
            // sentinel - to every node, the root's rotation included.
            // `Anim_RootDelta`'s 0 -> 1 step moves the node, which the
            // camera does not follow (it targets the model's centre), so
            // it is not applied here.
            static std::string characterBuiltFor;
            if (characterBuiltFor != playerModel) {
                characterBuiltFor = playerModel;
                CharModel* cm = charModelFor(playerModel);
                const std::string characterBank = "F1AVNT";   // 0x004DF5EC, literal
                CharBank* cb = charBankFor(characterBank);
                int entry = -1, clip = -1;
                int clipFrames = 0;
                std::string firstTrack;
                if (cb && cb->ready)
                    for (const auto& g : cb->ctl.groupList)
                        if (g.flags & 1u) { entry = g.defaultEntry; break; }
                if (cb && entry >= 0 && entry < static_cast<int>(cb->ctl.states.size()))
                    clip = cb->ctl.states[static_cast<std::size_t>(entry)].clip;
                int bound = 0;
                if (cm && cm->ready && clip >= 0 &&
                    clip < static_cast<int>(cb->ctl.clips.size())) {
                    omk::NodeTracks t;
                    const auto d = omk::animDescriptor(
                        cb->data, cb->ctl.clips[static_cast<std::size_t>(clip)].offset);
                    if (d && d->frames > 0 && !d->tracks.empty()) {
                        clipFrames = d->frames;
                        firstTrack = d->tracks.front().name;
                        const auto lower = [](std::string v) {
                            for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                            return v;
                        };
                        t.count = static_cast<int>(d->tracks.size());
                        t.frames = 1;
                        t.rootTrack = -1;
                        // THE BINDING IS BY BONE, NOT BY NAME. F1AVNT's
                        // tracks are `Sh`-prefixed (`ShBassin`) and Kay'l's
                        // meshes `U`-prefixed (`UBassin`), so an equality
                        // test binds 0 of 19 and he stands in his T-pose -
                        // which is what this drew first. The engine finds a
                        // bone by `strstr` on the node names
                        // (`docs/ASSETS.md`, "the bones are found by
                        // strstr"), and the port's rule for the same
                        // pairing is the crowd's and the gunmen's: the bone
                        // begins at the SECOND UPPERCASE LETTER
                        // (`verify.py: bone names`). Exact first, as there.
                        const auto boneOf = [&](const std::string& n) {
                            for (std::size_t i = 1; i < n.size(); ++i)
                                if (n[i] >= 'A' && n[i] <= 'Z') return lower(n.substr(i));
                            return n.size() > 2 ? lower(n.substr(2)) : lower(n);
                        };
                        for (const auto& tr : d->tracks) {
                            std::int32_t mi = -1;
                            const std::string want = lower(tr.name);
                            for (const auto& m : cm->meshes)
                                if (lower(m.name) == want) { mi = m.index; break; }
                            if (mi < 0) {
                                const std::string bone = boneOf(tr.name);
                                for (const auto& m : cm->meshes)
                                    if (boneOf(m.name) == bone) { mi = m.index; break; }
                            }
                            if (mi >= 0) ++bound;
                            t.ids.push_back(mi);
                        }
                        t.quats.assign(1, {});
                        t.trans.assign(1, {0.0f, 0.0f, 0.0f});
                        t.quats[0].resize(d->tracks.size());
                        for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                            const omk::AnimTrack& tr = d->tracks[i];
                            if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                            const int key = tr.rotKeys > 2 ? 2 : tr.rotKeys - 1;   // frame 1
                            const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
                            if (o + 16 > cb->data.size()) continue;
                            float qv[4];
                            std::memcpy(qv, cb->data.data() + o, 16);
                            t.quats[0][i] = {qv[0], qv[1], qv[2], qv[3]};
                        }
                        const auto pose = omk::composePose(cm->meshes, t, 0, false);
                        omk::Geometry posed = cm->rest;
                        omk::applyPose(posed, cm->rest, cm->meshes, pose);
                        uiModels.setCharacter(std::move(posed), cm->tex, playerModel);
                    }
                }
                // The bank from the variable it was loaded under, and the
                // clip's own length and first track: H1AVNT also opens on
                // entry 0 'H_STAND', clip 0, and binds 19 by exact name, so
                // only these tell the literal F1AVNT from the player's bank.
                std::printf("sneak: identity character '%s' - bank %s, default entry %d "
                            "'%s', clip %d (%d frames, first track '%s'), %d tracks bound, "
                            "frame 1\n", playerModel.c_str(), characterBank.c_str(),
                            entry,
                            cb && entry >= 0 && entry < static_cast<int>(cb->ctl.states.size())
                                ? cb->ctl.states[static_cast<std::size_t>(entry)].name.c_str() : "",
                            clip, clipFrames, firstTrack.c_str(), bound);
            }
            static std::string identityTold;
            std::string said;
            for (int p : {6, 8, 0, 13, 9, 10, 12, 11, 15, 14})
                said += " | " + std::to_string(p) + "=" +
                        (p == 8 ? std::to_string(playerSheet.num[8]) : playerSheet.str[p]);
            if (said != identityTold) {
                identityTold = said;
                std::printf("sneak: identity sheet%s\n", said.c_str());
            }
        }
        if (pn && pn->addr == omk::kPanelSneakExamine) {
            const auto carried =
                omk::objectList(state, omk::ObjectList::Carried);
            // BY ADDRESS: the examine page carries no row list of its
            // own, and the selections are a static record keyed by list,
            // so the row chosen two panels ago is still there.
            // ...and the TAG here too: `sub_49BFF0` latches
            // `dword_4DE74C = selected_widget[+0x3C]` when Examiner is
            // confirmed, and the page draws whatever that names.
            const int row = walk->selectedRow(omk::kListSneakRows);
            // `sub_49BFF0`'s last call, `sub_42B420(tag, 4)`: event 30
            // answers the object's slot and event 43 runs
            // `Message_RunHandlers(4, player, slot)`, whose sender is
            // `ObjectSlot_Id(slot)` - the object. The port never posted
            // it, so the GLOBAL table's handler never ran from here.
            if (walk->takeExamineMessage()) {
                const int eid = row >= 0 && static_cast<std::size_t>(row) < carried.size()
                    ? carried[static_cast<std::size_t>(row)] : -1;
                const bool ran = eid >= 0 && session.postMessage(4, eid);
                const auto& runs = session.messagesRun();
                std::printf("sneak: Examiner row %d id %d -> message 4 %s\n", row, eid,
                            ran && !runs.empty()
                                ? ("handled by the " + runs.back().table + " table").c_str()
                                : "(no resident chunk subscribes)");
            }
            if (row >= 0 && static_cast<std::size_t>(row) < carried.size()) {
                const int idx = carried[static_cast<std::size_t>(row)];
                if (idx >= 0 &&
                    static_cast<std::size_t>(idx) < objectRecords.size()) {
                    const auto& rec = objectRecords[static_cast<std::size_t>(idx)];
                    // Case 30 loads the model for whatever is SELECTED,
                    // whatever its kind, and case 40 hands the
                    // description back on every arm - so the page gets
                    // both: the object's own prop and its text.
                    // WHICH CONTENT. Two captures of the original
                    // settle it between them: Kay'l's apartment key is
                    // kind 0 and shows a 3D model with a one-line label,
                    // and the MK400 notice is kind 15 and shows TEXT
                    // ONLY - no prop behind it, though its record does
                    // name one (PAPIER). So the document kinds 15 and 16
                    // suppress the model, and everything else shows it.
                    const auto k = uiModels.examine(
                        fs, (rec.kind == 15 || rec.kind == 16) ? rec.kind : 15,
                        (rec.kind == 15) ? std::string() : rec.stem);
                    examineText = rec.description;
                    comp.setExamineText(&examineText);
                    // ...and where it is scrolled to. The composer
                    // CLAMPS this against the laid-out height, the way
                    // `Ui_ItemTextStyle` clamps `dword_6A5090`, so it
                    // takes the walk's own field rather than a copy.
                    comp.setTextScroll(&walk->textScroll());
                    if (rec.stem != examineTold) {
                        examineTold = rec.stem;
                        std::printf("sneak: examine '%s' kind %d -> %s\n",
                                    rec.name.c_str(), rec.kind,
                                    k == omk::UiModels::Examine::Model ? "3D model"
                                    : k == omk::UiModels::Examine::Document
                                      ? "document bitmap" : "nothing (case 40 result 2)");
                    }
                }
            }
        }
        // ---- THE MEMORY PAGE'S ROWS ARE THE MEMO JOURNAL ---------------
        //
        // Kind 2 is the memory page, and it binds OBJECT LIST 2:
        // `sub_42ADD0(rows, 0, 2)` raises the channel's event 25 on that
        // list and takes the count from event 29. This port drew that page
        // empty and `todo/sneak.md` §2c called it correct, on the grounds
        // that the builder's count `dword_4DE708` has no absolute store
        // anywhere in the image - but `0x004DE708` IS the row list's own
        // `+0x18` (`0x004DE6F0 + 24`), which `sub_42ADD0` writes through
        // the list pointer at `0x0042AF70`, exactly as it writes the shops'
        // and MULTIPLAN's counts. A reader playing it settled which was
        // right: *"info page contains important info you may have heard in
        // dialog"*. 76 `inventory.add` sites fill list 2 with `Memo NNN ...`
        // objects, whose record NAME is the memo's heading ("Moi :") and
        // whose description is its body.
        if (rowKind == 2 && inv.openedList() != 2) inv.openList(2);
        if ((rowKind == 0 || rowKind == 2) && inv.openedList() >= 0) {
            const omk::ObjectList rowSource = rowKind == 2
                ? omk::ObjectList::Memos : omk::ObjectList::Carried;
            const auto carried = omk::objectList(state, rowSource);
            // ---- `sub_42AAE0`, THE ROW BINDER --------------------
            //
            // The nine widgets are a WINDOW onto the list, and which of
            // them are live is decided per row rather than by drawing
            // whatever has text:
            //
            //     for each widget k of the list:
            //       if (k + window >= list+24)     // past the end
            //           item+60 = -1;              // tag: empty
            //           set item 0x40000001;       // and NOT DRAWN
            //           set item 0x20000004;       // and unselectable
            //       else
            //           item+60 = k + window;      // the row it shows
            //           clear those two;
            //
            // so the engine draws only the rows that HOLD something -
            // two of nine in the user's capture - and that is the gate
            // `Ui_DrawItemFill`'s bars are behind. `list+24` is the
            // count the channel reports (case 29).
            //
            // THE WINDOW is `sub_42AFF0`'s, kept in widget 0's `+0x3C`
            // and moved by the mover - so the text follows the scroll
            // rather than always starting at row 0. Hardcoded 0 until
            // 2026-09-04, which truncated any list longer than the nine
            // widgets: a tenth carried object could not be reached.
            const omk::UiPanel* rp = w.at(omk::kPanelSneakInventory);
            for (const auto& l : (rp ? rp->lists : pn->lists)) {
                if (l.addr != omk::kListSneakRows) continue;
                const std::size_t window = static_cast<std::size_t>(
                    std::max(0, walk->rowWindow(omk::kListSneakRows)));
                for (std::size_t k = 0; k < l.items.size(); ++k) {
                    const std::size_t row = k + window;
                    if (row >= carried.size()) {
                        // `sub_42AAE0`: past the end, so tag -1 and
                        // `0x40000001` - not drawn and not selectable.
                        sneakHidden.insert(l.items[k].addr);
                        continue;
                    }
                    // `playerCount` is case 33's other half and is NOT
                    // read: for kinds 2..6 the quantity lives in the
                    // player record and which field it is has not been
                    // established, so those rows show the name without
                    // its " - N". Kinds 7..11 take the item's own `+12`
                    // and are complete.
                    sneakRows[l.items[k].addr] =
                        inv.displayName(carried[row], 0);
                }
            }
            walk->bindRows(omk::kListSneakRows,
                           static_cast<int>(carried.size()),
                           walk->rowWindow(omk::kListSneakRows));
            // One line per change, so a headless run can be held to the
            // rows the page bound - and to WHICH list they came from.
            // ---- THE MEMO'S BODY, in the page's own box ----------------
            //
            // Item 0x004DEA98's draw hook `0x00477F60` raises event 40 on
            // the item's `+0x3C` and lays out what it answers - and that
            // tag field IS `dword_4DEAD4`, which the row hook writes with
            // the SELECTED row's tag whenever the row kind is 2. Case 40
            // hands back the object record's description on every arm, so
            // the box shows the selected memo's text. The composer draws it
            // through the same path as the examine page's.
            // ...and only while the BOX IS SHOWN. `0x0049D750` hides it
            // when the page is built and `0x0049D8B0` lights it once the
            // rows are the current list, so a memo body on an opened page
            // nobody has moved into is this port's invention, not the
            // game's.
            if (rowKind == 2 && walk->memoBodyShown()) {
                const int sel = walk->selectedRow(omk::kListSneakRows);
                const int memo = sel >= 0 && sel < static_cast<int>(carried.size())
                    ? carried[static_cast<std::size_t>(sel)] : -1;
                if (const omk::ObjectRecord* mr = memo > 0 ? inv.record(memo) : nullptr) {
                    examineText = mr->description;
                    comp.setExamineText(&examineText);
                    comp.setTextScroll(&walk->textScroll());
                    // The body, named from the memo the rows selected and
                    // measured rather than described, so a check can hold
                    // it: the id, its heading, and how long the text is.
                    memoBodyPending = "row " + std::to_string(sel) + " id " +
                                      std::to_string(memo) + " '" + mr->name + "'";
                }
            }
            if (rowKind == 2) {
                // The list is named from the SOURCE the rows were read
                // from, not from a literal: a mutation that made this page
                // read list 0 still printed "object list 2" and only the
                // ids gave it away.
                static std::string memosTold;
                const int from = static_cast<int>(rowSource);
                std::string said = "object list " + std::to_string(from) + ", " +
                                   std::to_string(carried.size()) + " rows:";
                for (int id : carried) said += " " + std::to_string(id);
                if (said != memosTold) {
                    memosTold = said;
                    std::printf("sneak: memory page - %s\n", said.c_str());
                }
            }
            // ---- THE CLOCK, and WHAT THE ECHO BAR NEEDS ---------
            //
            // Two of the device's rows are filled by callbacks of its
            // own, and both are readable - `sub_0049DC20` and
            // `sub_0049E090` carry no `proc` label (nothing calls them;
            // they are dwords in the widget table, CLAUDE.md 1's trap),
            // so `asmfn.py` returns a neighbour and the range has to be
            // dumped by hand.
            //
            // THE ECHO BAR IS NO LONGER COMPOSED HERE. It was: this
            // block used to put the selected item's own label into
            // `sneakRows` for it, which is right for three of the
            // function's seven arms and silent for the other four - and
            // the four include the one the page spends its time in, the
            // TAB LABEL with `  (n / 18)` after it, so the bar was blank
            // whenever the selection sat in the row list, which is
            // whenever the player is looking at his inventory.
            // `ScreenComposer::echoBarText` runs the whole function now,
            // and reports what it drew in `ScreenFrame::echoBar` - a
            // line composed where the drawing happens can be asserted;
            // one composed here could only report the intention.
            //
            // What the bar cannot get for itself is the two counts:
            // `sub_42B1C0(4)` and `(5)` raise `Game_HandleEvent(44)` on
            // the player, whose cases 4 and 5 are the player record's
            // `+172` and `+174` - the seteks and the anneaux.
            comp.setPlayerCounts(state.money(), state.rings());
            {
                for (const auto& l : pn->lists) {
                    for (const auto& e : l.items) {
                        if (e.textFn == 0x0049E090u) {
                            // The clock. Both halves are the engine's own
                            // formatters, already ported and checked
                            // (`sub_0041E690`'s integer division); the
                            // " - " joining them is read off the user's
                            // screenshot - "12 Nadim 7216 - 13:01:15" -
                            // and is the one part of this line that is
                            // not from the code.
                            sneakRows[e.addr] =
                                omk::formatDate(state.clockDay()) + " - " +
                                omk::formatTime(state.clock());
                        }
                    }
                }
            }
            if (!sneakTold++) {
                // Count the ROWS, not the map: since the echo bar and
                // the clock share `sneakRows` this reported "2 rows
                // shown" for a list holding one object.
                std::size_t rows = 0;
                for (const auto& l : pn->lists)
                    if (l.addr == omk::kListSneakRows)
                        for (const auto& e : l.items)
                            rows += sneakRows.count(e.addr);
                std::printf("sneak: object list %d holds %zu, %zu rows "
                            "shown, window at %d\n", inv.openedList(),
                            carried.size(), rows,
                            walk->rowWindow(omk::kListSneakRows));
            }
        }
        if (useClosedSneak) {
            std::printf("screen %d closed by the use - `sub_49BEA0` wrote "
                        "state 3 because `sub_42B470` returned 1 (event "
                        "%d, object list %d)\n", openScreen,
                        omk::kEventSneakClose, inv.openedList());
            inv.closeList();          // Game_RaiseEvent(26, 0)
            walk.reset();
            openScreen = -1;
            screenFromScript = true;
        }
    }
}

// The shop's stock rows
void PlayState::screensShopRows() {
    const auto& fs = *fs_;
    auto& inv = *inv_;
    auto& session = *session_;
    // ---- THE SHOP'S STOCK ROWS (todo/shops.md step 2) ----------------
    //
    // `Ui_OpenShop` binds the nine rows with `sub_42ADD0(rows, 0, arm)`,
    // where the arm is 0 at the bank and -1 in the other nine. A list
    // argument that is not -1 raises event 25 on it, so the BANK opens
    // list 0 - the player's own inventory, which is what a bank buys -
    // and the nine keep the list `ui.open` named: 3, the stock, which is
    // the active area block's `+8` (`omk::shopStock`). Then case 29 for
    // the count and case 33 for each name, through the same window and
    // past-the-end rule as the sneak's rows (`sub_42AAE0`).
    // All THREE of the shop's panels share the header, so all three are
    // served here: the shop, the Vente confirm and the Examiner page.
    const std::uint32_t shopPanel =
        walk && walk->panel() ? walk->panel()->addr : 0u;
    if (shopPanel == omk::kPanelShop || shopPanel == omk::kPanelShopSellConfirm ||
        shopPanel == omk::kPanelShopExamine) {
        sneakRows.clear();
        sneakHidden.clear();
        if (w.screenParam(openScreen) == 0 && inv.openedList() != 0)
            inv.openList(0);                        // Game_RaiseEvent(25, 0)
        std::vector<int> ids;
        const auto readIds = [&]() {
            ids.clear();
            if (inv.openedList() == 3)
                ids = omk::shopStock(session.residentSlot(session.activeSlot()).areaChunk);
            else if (inv.openedList() == 0)
                ids = omk::objectList(state, omk::ObjectList::Carried);
        };
        readIds();
        // ---- STEP 4: THE PURCHASE AND THE SALE ------------------------
        //
        // The walk recorded which (`takeShop`); the channel carries it
        // out, as `Game_HandleEvent` does in the engine. A message goes
        // through `sub_42B820(0, -1, text)` into `byte_6A4CA0` and runs for
        // oscillator 0's 5000 ms, shown in place of line two (below).
        static std::string shopMessage;
        static long shopMessageMs = -1000000;
        const long shopNowMs = static_cast<long>(front.ticksMs());
        const auto buyText = omk::iamStrings(fs, "IAM/Buy");
        const auto str = [&](int id) {
            return id >= 0 && id < static_cast<int>(buyText.size())
                ? buyText[static_cast<std::size_t>(id)] : std::string();
        };
        static std::map<int, std::vector<float>> shopSounds;
        const auto playId = [&](int id) {       // `Ui_PlaySound` (0x00482D90)
            auto it = shopSounds.find(id);
            if (it == shopSounds.end()) {
                std::vector<float> pcm;
                const std::string& nm = w.soundNameById(id);
                if (!nm.empty())
                    if (const auto path = fs.resolve("I2D/sounds/" + nm + ".wav"))
                        pcm = wavToDevice(omk::DataFs::readPath(*path), kDeviceRate);
                it = shopSounds.emplace(id, std::move(pcm)).first;
            }
            if (!it->second.empty()) blip(it->second);
        };
        const auto post = [&](int stringId) {
            shopMessage = str(stringId);
            shopMessageMs = shopNowMs;
        };
        if (int kind = -1, row = -1; walk->takeShop(kind, row)) {
            const int id = row >= 0 && row < static_cast<int>(ids.size())
                ? ids[static_cast<std::size_t>(row)] : -1;
            const auto carried = omk::objectList(state, omk::ObjectList::Carried);
            const int money = state.money();
            const int price = id > 0 ? inv.price(id) : 0;
            if (kind == 0 && id > 0) {
                // the row's Acheter arm: `sub_42B3E0(tag)` - event 41 -
                // refused is sound 18 and nothing else
                if (inv.shopAllows(id, carried) != omk::InvResult::Ok) {
                    playId(18);
                    std::printf("shop: buy %d refused by event 41 (a gun already held)\n", id);
                } else if (static_cast<int>(carried.size()) >=
                               omk::GameState::kListCapacity[0] || price > money) {
                    // case 38's result 2: `sub_42B300(tag) > sub_42B1C0(4)`
                    // picks 9 "Pas assez de seteks ! !", else 8 "Sneak plein !"
                    post(price > money ? 9 : 8);
                    playId(18);
                    std::printf("shop: buy %d refused by event 38 (price %d, money %d, "
                                "carried %zu) - message %d\n", id, price, money,
                                carried.size(), price > money ? 9 : 8);
                } else {
                    // case 38: `Inventory_Insert` + `ObjectList_InsertFront(0, ...)`,
                    // then `u16(+172) -= price`. Inventory_Insert's merge of
                    // ammunition and money into an existing slot is not
                    // modelled (`GameState::listAdd` says so).
                    state.listAdd(0, id);
                    state.setMoney(money - price);
                    post(21);                        // "Objet achete !"
                    playId(16);
                    std::printf("shop: bought %d for %d, money %d -> %d\n",
                                id, price, money, state.money());
                }
            } else if (kind == 1 && id > 0) {
                // `Oui` (0x004AEC00): event 36 request 10 - the record's flag
                // 0x2 - and `sub_42B300(tag) > 0`, then event 39
                const auto* rec = inv.record(id);
                const bool sellable = rec && (rec->flags & 0x2) != 0 && price > 0;
                if (!sellable) {
                    post(7);                         // "Vente non autorisee !"
                    std::printf("shop: sale of %d refused (flags %#x, price %d) - message 7\n",
                                id, rec ? rec->flags : 0, price);
                } else {
                    state.setMoney(std::min(money + price / 2, 0xFFFF));
                    state.listRemove(0, id);         // `ObjectList_RemoveAt(0, tag)`
                    playId(17);
                    std::printf("shop: sold %d for %d, money %d -> %d\n",
                                id, price / 2, money, state.money());
                }
            }
            readIds();
            // `if (!dword_4E3658) dword_4E3988 = 0` - an emptied list hands
            // the focus back to the buttons
            if (ids.empty()) walk->focusList(omk::kListShopButtons);
        }
        // ---- THE ANALYSER PAGE'S MODEL: event 30 on the chosen row -----
        //
        // The Examiner arm stores the row's tag and installs 0x004E39D8,
        // whose box draws `sub_42B2C0(tag)` - case 30, the preview load,
        // which loads the SELECTED object's model whatever its kind and
        // refuses list 2 (no models). The rows' selection is a static
        // record, so the row chosen on the shop panel is still selected.
        if (shopPanel == omk::kPanelShopExamine && inv.openedList() != 2) {
            const int row = walk->selectedRow(omk::kListShopRows);
            const int id = row >= 0 && row < static_cast<int>(ids.size())
                ? ids[static_cast<std::size_t>(row)] : -1;
            static int shopExamined = -1;
            if (id > 0 && id != shopExamined &&
                static_cast<std::size_t>(id) < objectRecords.size()) {
                shopExamined = id;
                const auto& rec = objectRecords[static_cast<std::size_t>(id)];
                const auto k = uiModels.examine(fs, 15, rec.stem);
                std::printf("shop: Analyser - object %d '%s', model %s\n", id,
                            rec.stem.c_str(),
                            k == omk::UiModels::Examine::Model ? "loaded" : "none");
            }
        }
        const int window = walk->rowWindow(omk::kListShopRows);
        for (const auto& l : walk->panel()->lists) {
            if (l.addr != omk::kListShopRows) continue;
            for (std::size_t k = 0; k < l.items.size(); ++k) {
                const std::size_t row = k + static_cast<std::size_t>(std::max(0, window));
                if (row >= ids.size()) {
                    sneakHidden.insert(l.items[k].addr);   // not drawn, not selectable
                    continue;
                }
                sneakRows[l.items[k].addr] = inv.displayName(ids[row], 0);
            }
        }
        walk->bindRows(omk::kListShopRows, static_cast<int>(ids.size()), window);
        // ---- THE HEADER's three text hooks (step 3) ------------------
        //
        //   0x004AEE30  the SELECTED BUTTON's label - `sub_476860` on
        //               `off_4E337C[word_4E3372]`, printed "%s". It also
        //               fetches the selected row's name into a buffer
        //               nothing reads (dead in the original), and while
        //               oscillator 0's flag runs it shows the flashing
        //               message `byte_6A4CA0` instead - step 4's, not
        //               modelled here.
        //   0x004AEF10  string 4 and `sub_42B1C0(4)`, the player's money,
        //               "%s %d".
        //   0x004AEF60  string 5 and `sub_42B300(tag)` - case 34, the
        //               price of the selected row's object - HALVED when
        //               the screen's parameter is 0, the bank quoting what
        //               it pays; "%s %d". A row with no object is tag -1
        //               and `sub_42B300` returns 0 for it.
        {
            // The hooks name the lists by ADDRESS (`0x004E3640`,
            // `off_4E337C`), not through the installed panel - which on the
            // Vente confirm and the Examiner page carries no stock rows.
            const omk::UiList* buttons = w.listAt(omk::kListShopButtons);
            const omk::UiList* rows = w.listAt(omk::kListShopRows);
            int price = 0;
            int rowId = -1;                 // the selected row's object
            if (rows) {
                const int sel = walk->selectionOf(*rows);
                const int row = sel >= 0 ? sel + std::max(0, window) : -1;
                if (row >= 0 && row < static_cast<int>(ids.size())) {
                    rowId = ids[static_cast<std::size_t>(row)];
                    price = inv.price(rowId);
                    if (w.screenParam(openScreen) == 0) price /= 2;
                }
            }
            // oscillator 0: `sub_42B6D0` adds the frame's ms to `+4` and at
            // `+8` = 5000 `sub_42B7B0` clears the flag line two tests
            const bool messageUp = shopNowMs - shopMessageMs < 5000;
            for (const auto& l : walk->panel()->lists) {
                for (const auto& e : l.items) {
                    if (e.textFn == 0x004AEE30u && messageUp) {
                        sneakRows[e.addr] = shopMessage;     // `sub_478D60`
                    } else if (e.textFn == 0x004AEE30u && buttons) {
                        const int b = walk->selectionOf(*buttons);
                        if (b >= 0 && b < static_cast<int>(buttons->items.size()))
                            sneakRows[e.addr] =
                                str(buttons->items[static_cast<std::size_t>(b)].label());
                    } else if (e.textFn == 0x004AEFD0u) {
                        // the Vente confirm's second line: `sub_42AA00` on
                        // the selected stock row - case 33, its name
                        if (rowId > 0) sneakRows[e.addr] = inv.displayName(rowId, 0);
                    } else if (e.textFn == 0x004AEF10u) {
                        sneakRows[e.addr] = str(e.label()) + " " +
                                            std::to_string(state.money());
                    } else if (e.textFn == 0x004AEF60u) {
                        sneakRows[e.addr] = str(e.label()) + " " +
                                            std::to_string(price);
                    }
                }
            }
            // One line whenever the header changes, so a headless run can
            // be held to what the three hooks composed.
            std::string said;
            for (const auto& l : walk->panel()->lists) {
                if (l.addr != omk::kListShopHeader) continue;
                for (const auto& e : l.items) {
                    const auto t = sneakRows.find(e.addr);
                    if (t != sneakRows.end()) said += " | '" + t->second + "'";
                }
            }
            static std::string headerTold;
            if (said != headerTold) {
                headerTold = said;
                std::printf("shop header: screen %d%s\n", openScreen, said.c_str());
            }
        }
        static int shopTold = -1;
        if (shopTold != openScreen * 100 + static_cast<int>(ids.size())) {
            shopTold = openScreen * 100 + static_cast<int>(ids.size());
            std::printf("shop: screen %d, object list %d, %zu rows:", openScreen,
                        inv.openedList(), ids.size());
            for (int id : ids) std::printf(" %d", id);
            std::printf("\n");
        }
    }
}

// Multiplan's rows and header, the Gandhar door's cursor, the hint shop
void PlayState::screensMultiplanHints() {
    const auto& fs = *fs_;
    auto& comp = *comp_;
    auto& inv = *inv_;
    // ---- MULTIPLAN's ROWS and HEADER (todo/multiplan.md step 2) --------
    //
    // The rows list the SOURCE (`dword_68A610`, the walk's
    // `multiplanSource`): 0 the sneak, 1 the storage every terminal
    // shares. `sub_42ADD0(rows, 0, src)` raises event 25 on it, then case
    // 29 for the count and case 33 for each name, through the window.
    // The header's hook `0x004B0B60` is the shops' `0x004AEE30` again:
    // the posted message while oscillator 0 runs, else the SELECTED
    // BUTTON's label (`IAM\Multip` 0..3).
    // ---- THE GANDHAR DOOR's cursor, where its hook put it ------------
    //
    // `sub_4AFE90` rewrites the cursor item's x/y on every step - `col * 63
    // + 135`, `row * 63 + 61` - and the tree only carries the authored
    // place, so the composer is told where the walk moved it.
    itemMoved.clear();
    // ---- `Indices`, THE HINT SHOP (screen 30's second page) -----------
    //
    // Five ROW widgets bound to OBJECT LIST 2 - the memo journal - a
    // 440x120 body box showing one section of the selected memo's
    // description, a footer counting the player's anneaux, and, on the
    // confirm, `Cet indice te coutera : 3` over `Acheter` / `Annuler`.
    //
    // Everything the widget tree cannot carry comes from here: the row
    // NAMES (the channel's case 33), the body's TEXT (case 40, the
    // object record's description), the two native `textFn`s, and the
    // Y each builder writes over the two items the pages SHARE.
    hintReport.clear();
    itemSection.clear();
    hintPanel = walk && walk->panel() ? walk->panel()->addr : 0u;
    if (hintPanel == omk::kPanelHints || hintPanel == omk::kPanelHintBuy) {
        sneakRows.clear();
        sneakHidden.clear();
        comp.setExamineText(nullptr);
        const auto saveText = omk::iamStrings(fs, "IAM/Save");
        const auto str = [&](int id) {
            return id >= 0 && id < static_cast<int>(saveText.size())
                ? saveText[static_cast<std::size_t>(id)] : std::string();
        };
        const auto memos = omk::objectList(state, omk::ObjectList::Memos);
        // `sub_42ADD0` raises event 25 on list 2 before it asks for the
        // count, so the channel is opened here too - case 33 refuses
        // (result 3) while no list is open.
        if (inv.openedList() != 2) inv.openList(2);
        // The five widgets, through `sub_42AAE0`'s window rule. The walk
        // already bound them in `buildPage`; this supplies the NAMES,
        // which are `sub_42AA00` -> event 33 on the widget's own tag.
        if (const omk::UiList* rl = w.listAt(omk::kListHintRows))
            for (std::size_t k = 0; k < rl->items.size(); ++k)
                if (k < memos.size())
                    sneakRows[rl->items[k].addr] =
                        inv.displayName(memos[k], 0);
        // THE BODY BOX. `sub_477F60` raises event 40 on the item's own
        // `+0x3C` and lays out what it answers; `dword_4E2B4C` is that
        // field, and the builders write it from the row list's selection
        // - which they have just reset to 0. So it is the FIRST memo
        // whatever row is highlighted, and `word_4E2B2E` picks which
        // bracketed section of it: 0 the memo, 1 the clue `Acheter` buys.
        const int bodyRow = walk->hintBodyRow();
        const int bodyId = bodyRow >= 0 && bodyRow < static_cast<int>(memos.size())
            ? memos[static_cast<std::size_t>(bodyRow)] : -1;
        if (const omk::ObjectRecord* br = bodyId > 0 ? inv.record(bodyId) : nullptr) {
            examineText = br->description;
            comp.setExamineText(&examineText);
            comp.setTextScroll(&walk->textScroll());
        }
        itemSection[omk::kItemHintBody] = walk->hintBodySection();
        // THE FOOTER, `sub_4AE290`. Three arms, and the item's `+28` -
        // which the row callback rewrites to 8 on a refusal - picks
        // between the last two.
        if (walk->hintRows() <= 0)
            sneakRows[omk::kItemHintFoot] = str(5);
        else if (walk->hintFooterString() == 6)
            sneakRows[omk::kItemHintFoot] =
                "{C}" + str(6) + " " + std::to_string(walk->rings());
        else
            sneakRows[omk::kItemHintFoot] = str(walk->hintFooterString());
        // THE PRICE LINE, `sub_4AE340`, and it is transcribed WITH ITS
        // ODDITY. The hook calls `Ui_ItemStringDefault` (0x00476860) on
        // an item whose bank C carries no `0x200` and whose `+30` is 0,
        // so the string goes through `sub_43FEA0(0, ...)` - the SECTION
        // extractor - and `Cet indice te coutera :` has no brackets at
        // all. The engine's own answer is therefore `{TEXT ERROR!}`
        // followed by the whole string, and the layout swallows the
        // brace as an unknown directive, so the line reads correctly on
        // screen. Reproduced rather than tidied: the `{TEXT ERROR!}` is
        // what the function returns.
        if (walk->hintRows() > 0 && saveText.size() > 2)
            sneakRows[omk::kItemHintPrice] =
                "{C}" + omk::extractTextSection(&saveText[2], 0) + " " +
                std::to_string(walk->hintPrice());
        // `word_4E2B12` and `word_4E2CF2`: the two shared items move
        // between the pages, 250/400 on the shop and 180/290 on the
        // confirm.
        for (const auto& l : walk->panel()->lists)
            for (const auto& e : l.items) {
                if (e.addr == omk::kItemHintBody)
                    itemMoved[e.addr] = {e.x, walk->hintBodyY()};
                if (e.addr == omk::kItemHintFoot)
                    itemMoved[e.addr] = {e.x, walk->hintFooterY()};
            }
        hintReport.insert(omk::kItemHintBody);
        hintReport.insert(omk::kItemHintFoot);
        hintReport.insert(omk::kItemHintPrice);
        hintReport.insert(omk::kItemHintDone);
    }
}

// Den's locker, Gandhar's door, Xachen's cartridges
void PlayState::screensPuzzles() {
    // ---- DEN'S LOCKER: the wheels show their digits -----------------
    //
    // `sub_4AFBE0` spins a wheel by writing `digit * 46` into its UNLIT
    // SOURCE's y, which cuts that figure out of the artwork's strip at
    // x = 0. The tree carries (0, 0) - digit zero - so the mover says which
    // row of the strip each wheel is on.
    itemSource.clear();
    itemLitSource.clear();
    denWheelItems.clear();
    xachenItems.clear();
    // ---- GANDHAR'S DOOR: the cursor and the markers where the hooks put
    // them - the position AND the unlit source, which the hook and the
    // press write to the same cell (`UiListState::itemPlace`). Moving only
    // the position left the cursor's unlit sprite sampling the artwork at
    // (135, 61), pasted over whichever cell it stood on.
    if (walk && openScreen == 12 && walk->panel()) {
        for (const auto& l : walk->panel()->lists) {
            if (l.hook != omk::kHookGandharGrid || l.items.empty()) continue;
            for (const auto& it : l.items)
                if (const auto* pl = walk->itemPlace(it.addr)) {
                    itemMoved[it.addr]  = {(*pl)[0], (*pl)[1]};
                    itemSource[it.addr] = {(*pl)[2], (*pl)[3]};
                }
            static int gandTold = -1;
            const int key = walk->gandharRow() * 10 + walk->gandharCol();
            if (key != gandTold) {
                gandTold = key;
                std::printf("gandhar door: the cursor is on row %d col %d; %d of the four "
                            "symbols in (%d presses)\n", walk->gandharRow(),
                            walk->gandharCol(), __builtin_popcount(walk->gandharMask()),
                            walk->gandharPresses());
            }
        }
    }
    // ---- XACHEN'S CARTRIDGES: the four symbols above the buttons ------
    //
    // `sub_4AF9D0` writes the symbol's 51x23 cell into the widget's LIT
    // source, and the widget carries bank B `0x8` so that is what it
    // draws. The symbols are the list the mover cannot reach (flags
    // `0x20000004`); the buttons are the one it can, and on the code all
    // four of THOSE take lit source (0, 23) with `0x40000008` set, which
    // is the lamp coming on.
    if (walk && openScreen == 14 && walk->panel()) {
        const omk::UiList* syms = nullptr;
        const omk::UiList* btns = nullptr;
        for (const auto& l : walk->panel()->lists) {
            if (l.hook == 0x0042A930u) btns = &l;
            else if (l.items.size() == 4) syms = &l;
        }
        if (syms)
            for (std::size_t k = 0; k < 4 && k < syms->items.size(); ++k) {
                int xy[2];
                omk::UiWalk::xachenSprite(walk->xachen(static_cast<int>(k)), xy);
                itemLitSource[syms->items[k].addr] = {xy[0], xy[1]};
                xachenItems.push_back(syms->items[k].addr);
            }
        if (btns && walk->xachenSolved())
            for (const auto& e : btns->items) itemLitSource[e.addr] = {0, 23};
    }
    if (walk && openScreen == 13 && walk->panel()) {
        for (const auto& l : walk->panel()->lists) {
            if (l.hook != omk::kHookDenDial) continue;
            for (std::size_t k = 0; k < 4 && k < l.items.size(); ++k) {
                itemSource[l.items[k].addr] = {0, walk->denDigit(static_cast<int>(k)) * 46};
                denWheelItems.push_back(l.items[k].addr);
            }
        }
    }
    liftHandled = walk && openScreen == 4;
}

// The terminal family's display
void PlayState::screensTerminals() {
    const auto& fs = *fs_;
    // ---- THE TERMINAL FAMILY'S DISPLAY (`todo/missing-ui.md` 3) ------
    //
    // The same gap as the lift's box, over seven screens: panel 0x004E4108's
    // body is a 430x320 item at (40, 80) in font 74 whose text comes from a
    // native `textFn`, 0x004AF5D0, so the composer drew nothing and a reader
    // found Kay'l's terminal blank - the artwork's keypad and screen with no
    // dossiers on it.
    //
    // `sub_4AF5D0` read from the image: two state queries first
    // (`sub_42B5E0(6)` then `sub_42B5F0`, and the same pair on 5), and
    // failing both a SEVEN-CASE jump table on the screen's fixed parameter,
    // each arm calling `sub_4767E0(panel, out, <string>, -1)`:
    //
    //     case 0 TERMINAL   string 12     case 4 SURV ERROR  string 1
    //     case 1 FIGHT SIM  string 5      case 5 SURV NO KIT string 3
    //     case 2 MORGUE     string 6      case 6 SURV KIT    string 2
    //     case 3 ARCHIVES   string 5
    //
    // The two query branches - string 11 for the TERMINAL and 4 for the
    // FIGHT SIM when the first is true, 6 for the FIGHT SIM when the second
    // is - are NOT ported: `sub_42B5E0`/`sub_42B5F0` are an object lookup
    // this has not read, so the default arm is what draws. Labelled, and it
    // is the arm the shipped state takes on a first visit.
    if (walk && walk->panel() && !liftHandled) {
        struct Fam { int screen; const char* file; int str; };
        static constexpr Fam kFam[] = {
            {5,  "IAM/Term", 12}, {11, "IAM/Fsim", 5}, {19, "IAM/Morg", 6},
            {18, "IAM/Arch", 5},  {15, "IAM/Surv", 1}, {16, "IAM/Surv", 3},
            {17, "IAM/Surv", 2},
        };
        for (const auto& f : kFam) {
            if (openScreen != f.screen) continue;
            sneakRows.clear();
            const auto txt = omk::iamStrings(fs, f.file);
            if (f.str >= static_cast<int>(txt.size())) break;
            for (const auto& l : walk->panel()->lists)
                for (const auto& e : l.items)
                    if (e.textFn == 0x004AF5D0u)
                        sneakRows[e.addr] = txt[static_cast<std::size_t>(f.str)];
            // ---- AND THE HEADER BAR, `textFn` 0x004AF5A0 -------------
            //
            // Eleven bytes of code, and the whole of what was left of this
            // family. `unk_4E3FE0` is the KEYPAD'S OWN LIST - `db 0Bh` at
            // +0 is its eleven items, `word_4E3FE2` at +2 its selection,
            // `sub_4AF300` at +4 its hook and `off_4E3FEC` at +0x0C its
            // item array - so
            //
            //     movsx ecx, word_4E3FE2 ; mov edx, off_4E3FEC
            //     ... sub_476860(screen, [edx+ecx*4], out)
            //
            // hands the GENERIC string callback the keypad item the cursor
            // is on, in place of the header's own. The bar at (40, 28) is
            // therefore the LABEL OF THE HIGHLIGHTED CELL, and it is the
            // same shape as the lift's description box: a widget whose
            // text belongs to another widget.
            //
            // The labels are the ones each screen's OPEN callback binds
            // (`item+28`, lifted as `bind.string`): TERMINAL 5..9 on the
            // first five cells and 10 on the big button, FIGHT SIM 0..2,
            // ARCHIVES 0..3 with tags 6..9, MORGUE 0..4, and the three
            // SURV screens bind none at all - so on those the bar is
            // rightly empty, which is what the record says and not a gap.
            {
                const omk::UiList* pad = nullptr;
                for (const auto& l : walk->panel()->lists)
                    if (l.hook == omk::kHookTerminalPad) pad = &l;
                const int cell = pad ? walk->selectionOf(*pad) : -1;
                const int id = pad && cell >= 0 &&
                               cell < static_cast<int>(pad->items.size())
                             ? pad->items[static_cast<std::size_t>(cell)].label() : -1;
                if (id >= 0 && id < static_cast<int>(txt.size()))
                    for (const auto& l : walk->panel()->lists)
                        for (const auto& e : l.items)
                            if (e.textFn == 0x004AF5A0u)
                                sneakRows[e.addr] = txt[static_cast<std::size_t>(id)];
                // printed from what the BAR will draw, and from the cell
                // the WALK is on - not from the id, which is the value
                // handed over rather than the one used
                static std::string padTold;
                std::string bar;
                for (const auto& l : walk->panel()->lists)
                    for (const auto& e : l.items)
                        if (e.textFn == 0x004AF5A0u) {
                            const auto r = sneakRows.find(e.addr);
                            if (r != sneakRows.end()) bar = r->second;
                        }
                std::string one;
                for (char c : bar.substr(0, 60)) one += (c == '\r' || c == '\n') ? ' ' : c;
                const std::string said = std::to_string(openScreen) + "/" +
                                         std::to_string(cell) + ": " + one;
                if (said != padTold) {
                    padTold = said;
                    std::printf("terminal family: screen %d cell %d - the bar says "
                                "'%s'\n", openScreen, cell, one.c_str());
                }
            }
            static int famTold = -1;
            if (famTold != openScreen) {
                famTold = openScreen;
                // the BODY item's own row, named rather than "the last
                // thing in the map" - the map gained the header bar above
                std::string shown;
                for (const auto& l : walk->panel()->lists)
                    for (const auto& e : l.items)
                        if (e.textFn == 0x004AF5D0u) {
                            const auto r = sneakRows.find(e.addr);
                            if (r != sneakRows.end()) shown = r->second;
                        }
                std::string one;
                for (char c : shown.substr(0, 80)) one += (c == '\r' || c == '\n') ? ' ' : c;
                std::printf("terminal family: screen %d - the display says '%s'\n",
                            openScreen, one.c_str());
            }
            break;
        }
    }
}

// The lift's description box
void PlayState::screensLift() {
    const auto& fs = *fs_;
    auto& comp = *comp_;
    auto& inv = *inv_;
    auto& session = *session_;
    // ---- THE LIFT'S DESCRIPTION BOX (`todo/missing-ui.md` 2b) --------
    //
    // Screen 4's list 1 is one 475x105 text item at (15, 360) in font 67
    // whose text comes from a NATIVE callback, 0x004B01C0 - so the composer,
    // which draws an item's own string or nothing, drew nothing and the box
    // sat empty. A reader: *"when a level is hovered, the names of the
    // people's office and other sections of the level should be displayed"*.
    //
    // What it says is `IAM\Lift`, whose seven strings are the seven slots
    // IN ORDER - the grid's items carry string ids 0..6 and the file reads
    // "Niveau 1 : Bureau du commandant Gandhar", "Niveau 0 : Entree
    // principale", then -1 to -5 with the agents' names in their own
    // colours (`{I045175045}Tarek 511` and the rest; Kay'l 669 is on -2).
    // So the box is the SELECTED slot's string, and the port already has
    // both halves: the walk knows the selection and `iamStrings` reads the
    // file the composer itself would have used.
    //
    // LABELLED: 0x004B01C0's own body is not transcribed. It builds its
    // string through the inventory channel (`sub_4083F0` with event 0x24
    // and a {slot, 7} block, then `sub_4767E0` into a buffer) and this
    // takes the file's string directly, which is what that produces for
    // every one of the seven shipped slots.
    if (walk && openScreen == 4 && walk->panel()) {
        sneakRows.clear();
        const auto liftText = omk::iamStrings(fs, "IAM/Lift");
        const omk::UiPanel* pn = walk->panel();
        int slot = -1;
        for (const auto& l : pn->lists)
            if (l.items.size() == 7) { slot = walk->selectionOf(l); break; }
        for (const auto& l : pn->lists)
            for (const auto& e : l.items)
                if (e.textFn == 0x004B01C0u && slot >= 0 &&
                    slot < static_cast<int>(liftText.size()))
                    sneakRows[e.addr] = liftText[static_cast<std::size_t>(slot)];
        static int liftTold = -2;
        if (slot != liftTold) {
            liftTold = slot;
            // printed from what the BOX will draw, not from the selection
            std::string shown;
            for (const auto& r : sneakRows) shown = r.second;
            const auto nl = shown.find('\r');
            std::printf("lift: slot %d - the panel says '%s'\n", slot,
                        nl == std::string::npos ? shown.c_str()
                                                : shown.substr(0, nl).c_str());
        }
    }
    if (walk && walk->panel() &&
        (walk->panel()->addr == omk::kPanelMultiplan ||
         walk->panel()->addr == omk::kPanelMultiplanExamine ||
         walk->panel()->addr == omk::kPanelMultiplanDestroy)) {
        sneakRows.clear();
        sneakHidden.clear();
        const int src = walk->multiplanSource();
        if (inv.openedList() != src) inv.openList(src);   // Game_RaiseEvent(25, src)
        const auto mpText = omk::iamStrings(fs, "IAM/Multip");
        // ---- THE TRANSFERS (step 3): `Game_HandleEvent` case 36 ---------
        //
        // The walk recorded the row callback's request (`takeMultiplan`).
        // Case 36 reads the OPEN list's slot at the tag, then:
        //   7: kind 1 (a hand weapon) -> 2; `ObjectList_IsFull(1)` -> 2;
        //      else `ObjectList_InsertFront(1, ...)`, `RemoveAt(0, tag)`, 1
        //   8: `ObjectList_IsFull(0)` -> 2; else `Inventory_Insert(rec, 0,
        //      player)` and, when it answers 1, `InsertFront(0, ...)`;
        //      then `RemoveAt(1, tag)` whatever it answered, 1
        // So a kind 12/13 object (seteks, rings) leaves the storage as a
        // COUNT on the player and takes no sneak slot - the session's
        // `insertArm` / `applyObjectEffect` are that ladder. The callback
        // then posts message 8 on 1 and 7 / 6 otherwise through
        // `sub_42B820(0, -1, text)`, oscillator 0, 5000 ms, and rebinds
        // the rows (`sub_42ADD0(rows, -1, -1)`), handing the focus back
        // to the buttons when they are left empty.
        static std::string mpMessage;
        static long mpMessageMs = -1000000;
        const long mpNowMs = static_cast<long>(front.ticksMs());
        if (int request = -1, row = -1; walk->takeMultiplan(request, row)) {
            const auto from = omk::objectList(state, src == 0 ? omk::ObjectList::Carried
                                                               : omk::ObjectList::Second);
            const int id = row >= 0 && row < static_cast<int>(from.size())
                ? from[static_cast<std::size_t>(row)] : -1;
            int result = request;          // case 36 leaves +4 alone past the count
            std::string arm;
            const auto postText = [&](int m) {
                mpMessage = m < static_cast<int>(mpText.size())
                    ? mpText[static_cast<std::size_t>(m)] : std::string();
                mpMessageMs = mpNowMs;
            };
            if (request == 4) {
                // `sub_42B420(tag, 4)`: event 30 loads the preview and
                // answers the object's SLOT, event 43 runs
                // `Message_RunHandlers(4, player, slot)`, whose block
                // carries `ObjectSlot_Id(slot)` - the object id.
                const bool ran = id >= 0 && session.postMessage(4, id);
                const auto& runs = session.messagesRun();
                std::printf("multiplan: examine row %d id %d -> message 4 %s\n", row, id,
                            ran && !runs.empty()
                                ? ("handled by the " + runs.back().table + " table").c_str()
                                : "(no resident chunk subscribes)");
            } else if (request == 6) {
                // Oui: the SOURCE test comes before the tag's; case 36
                // request 6 is `(flags & 2) ? RemoveAt(1, tag), 1 : 2`,
                // and success posts nothing.
                int msg = -1;
                if (src != 1) { msg = 4; arm = "not the kiosk's list"; }
                else if (id >= 0) {
                    const auto* rec = inv.record(id);
                    if (rec && (rec->flags & 0x2)) {
                        state.listRemove(1, id); result = 1; arm = "destroyed";
                    } else { result = 2; msg = 4; arm = "flag 0x2 clear"; }
                } else { arm = "no row"; }
                if (msg >= 0) postText(msg);
                std::printf("multiplan: destroy row %d id %d -> %d (%s)%s\n", row, id,
                            result, arm.c_str(), msg >= 0 ? ", message 4" : "");
                const auto after = omk::objectList(state, omk::ObjectList::Second);
                if (result == 1 && after.empty()) walk->focusList(omk::kListMultiplanButtons);
            } else if (id >= 0 && request == 7) {
                const auto* rec = inv.record(id);
                if (rec && rec->kind == 1) { result = 2; arm = "kind 1"; }
                else if (!state.listAdd(1, id)) { result = 2; arm = "storage full"; }
                else { state.listRemove(0, id); result = 1; arm = "row"; }
            } else if (id >= 0 && request == 8) {
                const auto carried = omk::objectList(state, omk::ObjectList::Carried);
                if (static_cast<int>(carried.size()) >= omk::GameState::kListCapacity[0]) {
                    result = 2; arm = "sneak full";
                } else {
                    const auto a = session.insertArm(id);
                    if (a == omk::Session::Banked::Row) { state.listAdd(0, id); arm = "row"; }
                    else { session.applyObjectEffect(id);
                           arm = a == omk::Session::Banked::Consumed ? "consumed" : "merged"; }
                    state.listRemove(1, id);
                    result = 1;
                }
            }
            if (request == 7 || request == 8) {
                const int msg = result == 1 ? 8 : request == 7 ? 7 : 6;
                postText(msg);
                std::printf("multiplan: request %d row %d id %d -> %d (%s), message %d, "
                            "rings %d\n", request, row, id, result, arm.c_str(), msg,
                            state.rings());
                const auto after = omk::objectList(state, src == 0 ? omk::ObjectList::Carried
                                                                    : omk::ObjectList::Second);
                if (result == 1 && after.empty()) walk->focusList(omk::kListMultiplanButtons);
            }
        }
        const auto ids = omk::objectList(state, src == 0 ? omk::ObjectList::Carried
                                                         : omk::ObjectList::Second);
        const int window = walk->rowWindow(omk::kListMultiplanRows);
        if (const omk::UiList* rows = w.listAt(omk::kListMultiplanRows)) {
            for (std::size_t k = 0; k < rows->items.size(); ++k) {
                const std::size_t row = k + static_cast<std::size_t>(std::max(0, window));
                if (row >= ids.size()) {
                    sneakHidden.insert(rows->items[k].addr);   // sub_42AAE0
                    continue;
                }
                sneakRows[rows->items[k].addr] = inv.displayName(ids[row], 0);
            }
        }
        walk->bindRows(omk::kListMultiplanRows, static_cast<int>(ids.size()), window);
        // ---- THE EXAMINER PAGE and THE DESTROY CONFIRM (step 4) ---------
        //
        // Both name the object through the rows' SELECTION, a static
        // record, so the row chosen on the kiosk is still there. The page's
        // box is draw hook `0x004780A0`, the sneak's examine box again:
        // case 40 answers 4 for kind 15 (text only), 5 for kind 16 (the
        // `IMAGES` bitmap the builder `0x004B0510` loads, then the text),
        // 2 otherwise (case 30's model on oscillator 4, then the text) -
        // `uiModels.examine` as the sneak page calls it. The confirm's
        // second line `0x004B0C30` is `sub_42AA00` on the selected row.
        {
            const int sel = walk->selectedRow(omk::kListMultiplanRows);
            const int selId = sel >= 0 && sel < static_cast<int>(ids.size())
                ? ids[static_cast<std::size_t>(sel)] : -1;
            const std::uint32_t pa = walk->panel()->addr;
            if (pa == omk::kPanelMultiplanExamine && selId >= 0 &&
                static_cast<std::size_t>(selId) < objectRecords.size()) {
                const auto& rec = objectRecords[static_cast<std::size_t>(selId)];
                static int mpExamined = -1;
                const auto k = uiModels.examine(
                    fs, (rec.kind == 15 || rec.kind == 16) ? rec.kind : 15,
                    (rec.kind == 15) ? std::string() : rec.stem);
                examineText = rec.description;
                comp.setExamineText(&examineText);
                comp.setTextScroll(&walk->textScroll());
                if (selId != mpExamined) {
                    mpExamined = selId;
                    std::printf("multiplan: Examiner '%s' kind %d -> %s\n", rec.name.c_str(),
                                rec.kind,
                                k == omk::UiModels::Examine::Model ? "3D model"
                                : k == omk::UiModels::Examine::Document ? "document bitmap"
                                : "text only");
                }
            }
            if (pa == omk::kPanelMultiplanDestroy && selId >= 0)
                if (const omk::UiList* c = w.listAt(omk::kListMultiplanDestroy))
                    for (const auto& e : c->items)
                        if (e.textFn == omk::kTextMultiplanRowName)
                            sneakRows[e.addr] = inv.displayName(selId, 0);
        }
        // `0x004B0B60`: the posted message while oscillator 0 runs
        // (`sub_478D60`); otherwise `sprintf(out, "%s", label)` of the
        // selected button (`sub_476860`). On the Examiner page, or with
        // the rows focused, it ALSO fetches the selected row's name into a
        // second buffer (`sub_42AA00`) - and never prints it: the format
        // at 0x004E5AF8 is "%s" with the label as its only argument.
        const bool mpMessageUp = mpNowMs - mpMessageMs < 5000;
        if (const omk::UiList* buttons = w.listAt(omk::kListMultiplanButtons)) {
            const int b = walk->selectionOf(*buttons);
            if (b >= 0 && b < static_cast<int>(buttons->items.size())) {
                const int id = buttons->items[static_cast<std::size_t>(b)].label();
                if (const omk::UiList* head = w.listAt(omk::kListMultiplanHeader))
                    for (const auto& e : head->items) {
                        if (e.textFn != 0x004B0B60u) continue;
                        if (mpMessageUp)
                            sneakRows[e.addr] = mpMessage;
                        else if (id >= 0 && id < static_cast<int>(mpText.size()))
                            sneakRows[e.addr] = mpText[static_cast<std::size_t>(id)];
                    }
            }
        }
        static std::string multiplanTold;
        std::string said = "source " + std::to_string(src) + ", " +
                           std::to_string(ids.size()) + " rows:";
        for (int id : ids) said += " " + std::to_string(id);
        if (said != multiplanTold) {
            multiplanTold = said;
            std::printf("multiplan: %s\n", said.c_str());
        }
    }
}

// The tail of Actor_SetProperty
void PlayState::screensPropertyTail() {
    auto& session = *session_;
    // ---- `sub_423A40`, the tail of `Actor_SetProperty` (`script/hooks.h`)
    //
    // A script's property write in a shoot phase reaches the shoot
    // records here: property 1 is the record's `+92` (for the player, the
    // gauge's value too), property 35 the HUD's ammo when the held
    // weapon's row matches - its TYPE at least 6 (the `>= 2 && >= 6`
    // pair, transcribed as read) and equal to the magazine slot + 2. A
    // SHOOT medikit is a zone script that does exactly this:
    // `var.set.actor_stat(player, 1)`, `var.add`, `actor.stat.set` - so
    // the kit is used the moment it is taken, not stored.
    for (const auto& w : session.takeShootStatWrites()) {
        if (!shootMode) continue;
        if (w.property == 1) {
            if (w.actor == -1) {
                playerShootRec.health = static_cast<int>(w.value);
                hudHealth = playerShootRec.health;   // `dword_90E100 = value`
                std::printf("frame %ld: SHOOT STAT (sub_423A40) - the player's health "
                            "-> %d, and the gauge's\n", n, int(w.value));
            } else if (const auto it = shootBrains.find(w.actor); it != shootBrains.end()) {
                it->second.health = static_cast<int>(w.value);
                std::printf("frame %ld: SHOOT STAT (sub_423A40) - actor %d's health -> %d\n",
                            n, w.actor, int(w.value));
            }
        } else if (w.property == 35 && w.actor == -1) {
            if (const omk::ShootWeaponRow* row = playerShootRec.weapon)
                if (row->key >= 2 && row->key >= 6 && row->key == (w.value >> 16) + 2)
                    hudAmmo = static_cast<int>(w.value & 0xFFFF);
        }
    }
}

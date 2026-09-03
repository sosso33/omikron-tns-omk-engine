// SPDX-License-Identifier: GPL-3.0-or-later
// THE AIRLOCK WALK - walking from one area into the next without the frame
// going black (todo/pending, "OPEN 1"; the fix of 2026-09-03).
//
//     airlock_probe <gamedata> <tables>
//
// The Impasse hands over to adventure mode, and the player walks SOUTH-EAST
// out of AIMPASSE (AREA 222) through zone 3801, whose enter script is
// `area.goto 142 -1 -1` - the airlock "Anekbah Impasse Sas", AIMPASAS. The
// engine keeps BOTH decors in state 2 across the transition and moves the
// ACTIVE row only when the player's feet cross onto the new one (event 9,
// `Walk_ProbeGround`); the port used to draw one set and rebuild the player
// on the area change, so the frame was black afterwards. This drives the
// Session-side of that headlessly and asserts each step.
//
// One line per fact, `key name value ...`:
//
//   handover    the intro run to SCENE 55 over AREA 222, shown and behind
//   transition  the walk into 3801: the goto, both decors in state 2, the
//               destination announced, the origin still resident
//   feet        `decorUnder` telling the two soups apart, and `playerOnArea`
//               moving the active row from 222 to 142 on the feet
//   beat        zone 3795's script: the teleport (placementSeq up), the two
//               tutorial voices and their cameras
//   stream      the airlock announcements in order, for the trace to match
//               (traces/impasse-walk.log lines ~105-135)
#include "actor/walk.h"
#include "formats/iam.h"
#include "o3de/collision.h"
#include "platform/datafs.h"
#include "script/area.h"
#include "script/gamestate.h"
#include "script/script.h"
#include "script/world.h"
#include "script/zones.h"
#include "ui/widgets.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

namespace {

float arcCentre(const omk::Zone& z) {
    return static_cast<float>(static_cast<int>(
        static_cast<double>(z.arcMid) * omk::kZoneArcToDegrees));
}

// Stand the player at a zone's centre, facing into its arc.
void stand(omk::Session& s, const omk::LiveZone* z) {
    double c[3];
    z->zone.centre(c);
    const float p[3] = {static_cast<float>(c[0]), static_cast<float>(c[1]),
                        static_cast<float>(c[2])};
    s.setPlayerPosition(p, arcCentre(z->zone));
}

// A point standing on a soup: the centroid of its first triangle, lifted a
// little so `decorUnder`'s downward probe finds it.
bool pointOn(const omk::TriangleSoup& soup, double out[3]) {
    if (soup.size() < 9) return false;
    for (int k = 0; k < 3; ++k)
        out[k] = (soup[k] + soup[3 + k] + soup[6 + k]) / 3.0;
    out[1] -= 8.0;                       // a step above the floor (Y grows down)
    return true;
}

// The last announced value in a domain, or INT_MIN.
long lastOf(const omk::Session& s, const char* dom, std::size_t from) {
    long v = -999999;
    const auto& a = s.announced();
    for (std::size_t i = from; i < a.size(); ++i)
        if (a[i].domain == dom) v = a[i].value;
    return v;
}
bool sawInOrder(const omk::Session& s, std::size_t from,
                const std::vector<std::pair<std::string, long>>& want) {
    const auto& a = s.announced();
    std::size_t w = 0;
    for (std::size_t i = from; i < a.size() && w < want.size(); ++i)
        if (a[i].domain == want[w].first && a[i].value == want[w].second) ++w;
    return w == want.size();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: airlock_probe <gamedata> <tables>\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) return 1;
    const std::string iam = fr + "/IAM";
    const omk::DataFs fs(fr);

    auto state = omk::GameState::fromFile(iam + "/START");
    omk::Session s(iam, state, table);
    s.loadAnnounceMap(tb + "/vm_announce.json");
    omk::UiWidgets widgets = omk::UiWidgets::loadJson(tb + "/ui_widgets.json");
    if (widgets.valid()) s.attachUi(widgets);

    // ---- the intro to the Impasse's hand-over (livezones_probe's chain) ----
    s.loadArea(118);
    s.frame();                                       // ui.open, the derived answer
    s.frame();
    long handoverFrame = -1;
    for (int f = 0; f < 400 && handoverFrame < 0; ++f) {
        s.frame();
        if (s.dialogOpen()) s.endDialog();
        for (const auto& an : s.announced())
            if (an.domain == "SCENES" && an.value == 57) { handoverFrame = s.frameNo(); break; }
    }
    // Let the SCENE 55 startup beats finish so the airlock zones are live.
    for (int f = 0; f < 60; ++f) s.frame();

    const int shownArea = s.residentSlot(s.shownSlot()).area;
    std::printf("handover frame %ld current %d shown %d other %d scene %d shown_count %d\n",
                handoverFrame, s.currentArea(), shownArea, s.otherArea(),
                s.residentSlot(s.shownSlot()).scene, s.shownCount());

    const std::size_t airlockFrom = s.announced().size();

    // ---- SCENE 55 zone 3803's enter script: `media.play 410` --------------
    long line410 = -999999;
    if (const auto* z3803 = s.zones().resolve(3803)) {
        const std::size_t at = s.announced().size();
        stand(s, z3803);
        s.frame();                                   // arm
        s.frame();                                   // pump: the enter script runs
        line410 = lastOf(s, "OBJECTS", at);
    }
    std::printf("scene_line media %ld\n", line410);

    // The two decors' walkable soups, resolved from the resident slots' stems.
    const std::string impasseStem = s.residentSlot(s.activeSlot()).set;   // AIMPASSE
    omk::TriangleSoup impasseSoup;
    if (const auto o = fs.resolve("MESHES/DECORS/" + impasseStem + ".3DO"))
        impasseSoup = omk::collisionSoup(omk::DataFs::readPath(*o), omk::SoupKind::Walkable);

    // ---- the walk into 3801: `area.goto 142` ------------------------------
    const auto* z3801 = s.zones().resolve(3801);
    long gotoAnn = -999999;
    int shownDuring = 0, destArea = -1;
    std::string destStem;
    omk::TriangleSoup airlockSoup;
    if (z3801) {
        const std::size_t annAt = s.announced().size();
        stand(s, z3801);
        s.frame();                                   // scan: arm
        s.frame();                                   // pump: the enter script runs area.goto
        gotoAnn = lastOf(s, "AREAS", annAt);
        // the load streams a slice a frame; AIMPASAS is small (1 slice), but
        // pump generously and let the transition's tail show the destination
        for (int f = 0; f < 40; ++f) s.frame();
        shownDuring = s.shownCount();
        // the destination is the slot that is NOT the origin's
        for (int slot = 0; slot < 2; ++slot)
            if (s.residentSlot(slot).area == 142) {
                destArea = 142; destStem = s.residentSlot(slot).set;
            }
        if (!destStem.empty())
            if (const auto o = fs.resolve("MESHES/DECORS/" + destStem + ".3DO"))
                airlockSoup = omk::collisionSoup(omk::DataFs::readPath(*o), omk::SoupKind::Walkable);
    }
    std::printf("transition goto %ld shown_during %d dest %d dest_set %s "
                "origin_resident %d\n",
                gotoAnn, shownDuring, destArea, destStem.empty() ? "-" : destStem.c_str(),
                s.residentSlot(0).area == 222 || s.residentSlot(1).area == 222 ? 1 : 0);

    // ---- the feet: decorUnder tells the two soups apart, event 9 follows ---
    std::vector<omk::DecorSoup> decors;
    if (!impasseSoup.empty()) decors.push_back({222, &impasseSoup});
    if (!airlockSoup.empty()) decors.push_back({142, &airlockSoup});
    double pi[3], pa[3];
    const int underImpasse = pointOn(impasseSoup, pi)
        ? omk::decorUnder(decors, pi[0], pi[1], pi[2]) : -2;
    const int underAirlock = pointOn(airlockSoup, pa)
        ? omk::decorUnder(decors, pa[0], pa[1], pa[2]) : -2;
    const int activeBefore = s.activeArea();
    s.playerOnArea(142);                             // his feet cross onto AIMPASAS
    const int activeAfter = s.activeArea();
    std::printf("feet under_impasse %d under_airlock %d active_before %d active_after %d "
                "shown_after %d\n",
                underImpasse, underAirlock, activeBefore, activeAfter, s.shownCount());

    // ---- the airlock beat: zone 3795's script -----------------------------
    const auto* z3795 = s.zones().resolve(3795);
    long beatAddr = -999999, media405 = -999999;
    int placedBefore = s.placementSeq(), placedAfter = -1;
    std::size_t beatFrom = s.announced().size();
    if (z3795) {
        stand(s, z3795);
        s.frame();                                   // arm
        for (int f = 0; f < 30 && s.placementSeq() == placedBefore; ++f) s.frame();
        placedAfter = s.placementSeq();
        beatAddr = lastOf(s, "ADDRESSES", beatFrom);
        media405 = lastOf(s, "OBJECTS", beatFrom);
    }
    std::printf("beat placed_before %d placed_after %d address %ld media %ld player_at %.0f\n",
                placedBefore, placedAfter, beatAddr, media405, s.playerPos()[0]);

    // ---- the stream, for the trace to match -------------------------------
    // OBJECTS 410 (the line) and ZONES 3803 open SCENE 55's enter script;
    // AREAS 142 is 3801's goto; ADDRESSES 653 the teleport; OBJECTS 405/406
    // the two tutorial voices (traces/impasse-walk.log lines ~105-135).
    const bool order = sawInOrder(s, airlockFrom, {
        {"OBJECTS", 410}, {"ZONES", 3803}, {"AREAS", 142},
        {"ADDRESSES", 653}, {"OBJECTS", 405}, {"OBJECTS", 406},
    });
    std::printf("stream airlock_in_order %d\n", order ? 1 : 0);
    return 0;
}

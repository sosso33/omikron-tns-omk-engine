// SPDX-License-Identifier: GPL-3.0-or-later
// AFTER THE LOOP: the run's report, a save written when one was asked for,
// the window closed. Moved byte for byte out of `main`'s body by
// `todo/play-split.md` (2026-10-02); returns `main`'s exit code.
#include "playstate.h"

int PlayState::finish() {
    auto& session = *session_;
    std::printf("%ld frames presented\n", n);
    gpuFinishReport();
    if (!dump.empty()) {
        // The framebuffer the WINDOW was shown, as raw LE RGB565 - so the
        // live half of PORTING A1's pair can be diffed against the reference
        // half, which is the property rule 3 is about: the frontend uploads
        // the pixels and must not have touched them.
        // not a stream (PORTING A10): little-endian bytes, written whole
        std::vector<unsigned char> le(fb.px.size() * 2);
        for (std::size_t i = 0; i < fb.px.size(); ++i) {
            le[2 * i] = static_cast<unsigned char>(fb.px[i] & 0xFF);
            le[2 * i + 1] = static_cast<unsigned char>(fb.px[i] >> 8);
        }
        omk::writeWholeFile(dump, le.data(), le.size());
        // SAY THE SIZE. This wrote the bytes and no dimensions, and a reader
        // of the file has nothing to go on but its length - 960000 bytes is
        // 800x600, not the 640x480 that a `.bin` from this viewer is assumed
        // to be, and one shot was decoded at the wrong stride and read as a
        // broken renderer.
        std::printf("wrote %s (%dx%d RGB565, %zu bytes)\n", dump.c_str(),
                    fb.w, fb.h, fb.px.size() * 2);
    }
    harnessSaveSlot();
    {
        const auto& ps = session.scene().effects().particles();
        float lo[3] = {1e9f,1e9f,1e9f}, hi[3] = {-1e9f,-1e9f,-1e9f}, sc = 0;
        for (const auto& p : ps) {
            for (int k = 0; k < 3; ++k) {
                if (p.pos[k] < lo[k]) lo[k] = p.pos[k];
                if (p.pos[k] > hi[k]) hi[k] = p.pos[k];
            }
            if (p.scale > sc) sc = p.scale;
        }
        if (!ps.empty())
            std::printf("particles: sprite %d, scale up to %.2f, box "
                        "%.0f %.0f %.0f .. %.0f %.0f %.0f\n",
                        ps.front().sprite, sc, lo[0], lo[1], lo[2],
                        hi[0], hi[1], hi[2]);
    }
    if (session.dialogOpen()) {
        const auto& dlg = session.dialogue();
        std::printf("dialogue: line '%s' at %.2f of %.2f s - frame %d of %d, blend %g\n",
                    dlg.voice().c_str(), dlg.elapsed(), dlg.lineSeconds(),
                    static_cast<int>(dlg.elapsed() * 30.0), speakerTracks.frames,
                    omk::morphBlendFrames(speakerTracks.frames));
    }
    std::printf("effects: %d set pieces shown so far, %d shown now, "
                "%d emitters registered on the last frame, %zu particles alive\n",
                session.scene().piecesFired(), session.scene().pieces().shownCount(),
                session.scene().pieces().registered(),
                session.scene().effects().count());
    std::printf("effects: the melee opponent's .CTL sprites placed on his bones %ld times, "
                "%ld with a texture slot to draw with\n", foeSpritesDrawn, foeSpritesPooled);
    std::printf("world: %ld frames drawn, last set %s (%d shown), last camera %d, "
                "%ld frames under player.anim.hold\n",
                worldFrames, worldSet.empty() ? "(none)" : worldSet.c_str(),
                session.shownCount(), session.cameraId(), heldFrames);
    if (player)
        std::printf("player: ends at %.1f %.1f %.1f facing %.0f, channel entry %d group %d\n",
                    player->pos()[0], player->pos()[1], player->pos()[2],
                    player->facing(), player->ctlState(), player->ctlGroup());
    {
        std::string ids;
        for (std::size_t k = 0; k < stagedIds.size(); ++k)
            ids += (k ? ", " : "") + std::to_string(stagedIds[k]);
        std::printf("staged %ld characters (ids %s), %zu on screen at the end, "
                    "%zu models and %zu banks resident\n",
                    stagedEver, ids.empty() ? "none" : ids.c_str(),
                    staged.size(), charModels.size(), charBanks.size());
        for (const auto& up : staged)
            std::printf("  actor %d %s (bank %s) at %.0f %.0f %.0f facing %.0f - %s%s\n",
                        up->actor, up->model.c_str(),
                        up->bank.empty() ? "none" : up->bank.c_str(),
                        up->drawAt[0], up->drawAt[1], up->drawAt[2], up->facing,
                        up->src, up->drawn ? "" : "  [not drawn]");
    }
    if (player)
        std::printf("player: %s/%s at %.1f %.1f %.1f facing %.1f, ACTOR_STATE %d, "
                    ".CTL state %d '%s' clip %s frame %.1f, walked %.1f over %ld ticks, "
                    "pose tracks %s\n",
                    playerModel.c_str(), playerCtlName.c_str(), player->pos()[0],
                    player->pos()[1], player->pos()[2], player->facing(),
                    static_cast<int>(player->state()), player->ctlState(),
                    player->ctlStateName().c_str(), player->clipName().c_str(),
                    player->clipFrame(), player->distanceWalked(), player->ticks(),
                    player->poseTracks() ? "valid" : "NONE (drawn at rest - a T-pose)");
    {
        std::int32_t vie = -1;
        omk::readActorProperty(playerRecordSpan(), 1, vie);
        std::printf("player: Vie %d (the DB player record's property 1); %ld run-overs posted\n",
                    vie, session.runOvers());
    }
    std::printf("session: %d areas entered, %d ui answers, %d zones turned away by "
                "the height band\n",
                session.areasEntered(),
                static_cast<int>(session.uiAnswers().size()),
                session.zones().heightSkips());
    front.close();
    return 0;
}

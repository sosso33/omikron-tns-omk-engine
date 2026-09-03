// SPDX-License-Identifier: GPL-3.0-or-later
// THE CAMERA EDITING an object start hands the camera to - camera mode 13.
//
//     camedit_probe <gamedata> <tables> [--scene N | --area N] [--object ID]
//                   [--travel F] [--sweep out.txt]
//
// Loads the .SCX a resident chunk plays (SCENE 55, the Impasse, by default),
// starts one object through `SceneRunner::handle` exactly as a world script's
// `scx.play.wait` would - `Call{58, {object, 0, travel}}` - and prints which
// editing took the camera and the camera it yields at frames 0, 15 and 30,
// ticking the runner one frame at a time between them. Beside those numbers
// `tools/cutscene.py`'s `sample()` of the same editing is the reference: it
// is a transcription of `Cam_PlayEditing` too, and `verify.py: cutscene
// camera` already pins it to 24112/24112 frames.
//
// `--sweep` writes EVERY frame of EVERY linked editing in every scene that
// carries a chunk 10 - the whole 24112 - as text, one row a frame, for the
// Python side to difference. That is the corpus test; the three frames above
// are what a person reads.
//
// Before 2026-09-02 there was nothing to print: `SceneRunner` did not read
// chunk 10 and `o3de/camedit.h` had a parser and no sampler.
#include "formats/scx.h"
#include "o3de/camedit.h"
#include "platform/datafs.h"
#include "script/scenerunner.h"
#include "script/script.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: camedit_probe <gamedata> <tables> [--scene N | --area N] "
                             "[--object ID] [--travel F] [--sweep out.txt]\n");
        return 2;
    }
    const std::string fr = argv[1], tb = argv[2];
    omk::ChunkKind kind = omk::ChunkKind::Scene;
    int chunk = 55, object = 221, travel = 0;
    std::string sweep;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scene" && i + 1 < argc) { kind = omk::ChunkKind::Scene; chunk = std::atoi(argv[++i]); }
        else if (a == "--area" && i + 1 < argc) { kind = omk::ChunkKind::Area; chunk = std::atoi(argv[++i]); }
        else if (a == "--object" && i + 1 < argc) object = std::atoi(argv[++i]);
        else if (a == "--travel" && i + 1 < argc) travel = std::atoi(argv[++i]);
        else if (a == "--sweep" && i + 1 < argc) sweep = argv[++i];
    }
    const auto table = omk::OpcodeTable::loadJson(tb + "/vm_opcodes.json");
    if (!table.valid()) { std::fprintf(stderr, "no VM opcode table\n"); return 1; }

    // ---- the sweep: every frame of every linked editing ------------------
    if (!sweep.empty()) {
        if (!omk::safeOutputPath(sweep)) return 2;
        std::ofstream o(sweep);
        omk::DataFs dir(fr + "/SCPTDATA");
        long rows = 0, editings = 0, misses = 0;
        for (const auto& p : dir.list(".", "scx")) {
            const auto d = omk::DataFs::readPath(p);
            const auto st = omk::readScxStream(d);
            if (!st.valid || !st.camSize) continue;
            const auto cf = omk::readCamFile(
                std::span<const std::byte>(d).subspan(st.camOffset, st.camSize));
            if (!cf.valid) continue;
            const auto slash = p.find_last_of('/');
            const std::string stem = slash == std::string::npos ? p : p.substr(slash + 1);
            for (const auto& e : cf.editings) {
                ++editings;
                o << "editing " << stem << ' ' << static_cast<int>(e.id) << ' '
                  << e.name << ' ' << e.objectHandle << ' ' << e.duration << '\n';
                for (std::uint32_t f = 0; f < e.duration; ++f) {
                    omk::CamSample s;
                    if (!omk::sampleCamEditing(cf, e, static_cast<float>(f), s)) {
                        o << f << " none\n"; ++misses; continue;
                    }
                    char line[256];
                    std::snprintf(line, sizeof line,
                                  "%u %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n", f,
                                  s.eye[0], s.eye[1], s.eye[2], s.at[0], s.at[1], s.at[2],
                                  s.roll, s.fov);
                    o << line;
                    ++rows;
                }
            }
        }
        std::printf("sweep: %ld editings, %ld frames sampled, %ld the sampler gave up on -> %s\n",
                    editings, rows, misses, sweep.c_str());
    }

    // ---- one object, started the way a world script starts it -----------
    omk::SceneRunner runner;
    if (!runner.load(fr + "/SCPTDATA", fr + "/IAM", table, kind, chunk)) {
        std::printf("%s %d names no .SCX\n",
                    kind == omk::ChunkKind::Scene ? "SCENE" : "AREA", chunk);
        return 1;
    }
    const auto& cf = runner.camFile();
    std::printf("%s %d -> %s: %zu objects, chunk 10 %s (%zu editings, %zu of them linked)\n",
                kind == omk::ChunkKind::Scene ? "SCENE" : "AREA", chunk,
                runner.file().c_str(), runner.scene().scene().objects.size(),
                cf.valid ? "present" : "absent", cf.editings.size(),
                [&] { std::size_t n = 0; for (const auto& e : cf.editings) n += e.objectHandle != 0; return n; }());
    for (const auto& e : cf.editings) {
        std::string oname = "(unlinked)";
        for (const auto& ob : runner.scene().scene().objects)
            if (static_cast<int>(ob.handle >> 16) == static_cast<int>(e.objectHandle)) oname = ob.name;
        std::printf("  editing %2d %-11s %4u frames %zu tracks -> object %3u %s\n",
                    e.id, e.name.c_str(), e.duration, e.tracks.size(), e.objectHandle,
                    oname.c_str());
    }

    // `scx.play.wait object, 0, travel` - op 58, three fields, the last one
    // the travel into the editing's camera.
    omk::Call c;
    c.op = 58;
    c.fields = {static_cast<std::int16_t>(object), 0, static_cast<std::int16_t>(travel)};
    const int idx = runner.handle({c});
    std::printf("scx.play.wait %d, 0, %d -> program %d", object, travel, idx);
    if (idx < 0) { std::printf(" (object not resident)\n"); return 1; }
    std::printf(" '%s'\n", runner.started().back().name.c_str());

    const auto* ae = runner.activeEditing();
    if (!ae) {
        std::printf("no editing took the camera (ScriptObject_HasCamEditing says no)\n");
        return 0;
    }
    std::printf("editing %d '%s' takes the camera: object %d '%s', %u frames, travel %.0f, "
                "started on tick %ld\n", ae->editing, ae->editingName.c_str(), ae->object,
                ae->objectName.c_str(), ae->duration, ae->travel, ae->startedTick);
    // Frame f is the f-th `Game_Tick` after the start: the handler starts the
    // object in `Script_Pump`, `Script_PlayAllScripts` samples the editing at
    // clock 0 and advances, the frame draws. So one tick per frame, frame 0
    // included, and `editingClock()` reads back the frame just shown.
    for (int f = 0; f <= 30; ++f) {
        runner.tick(1.0f);
        if (f != 0 && f != 15 && f != 30) continue;
        omk::CamSample s;
        const auto* now = runner.activeEditing();
        if (!now || !runner.editingCamera(s)) {
            std::printf("  frame %2d: no editing camera (%s)\n", f,
                        now ? "sampler gave up" : "editing not active");
            continue;
        }
        std::printf("  frame %2d (clock %.1f): eye %.2f %.2f %.2f  at %.2f %.2f %.2f  "
                    "roll %.3f  fov %.3f  track %d camera %u\n", f, runner.editingClock(),
                    s.eye[0], s.eye[1], s.eye[2], s.at[0], s.at[1], s.at[2], s.roll,
                    s.fov, s.track, s.camera);
    }
    return 0;
}

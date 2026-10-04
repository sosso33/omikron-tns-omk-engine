// SPDX-License-Identifier: GPL-3.0-or-later
// THE PROFILER'S PROBES - `todo/debug-tools.md`. This project's own instrument
// (PORTING B6): nothing here is a reading of the original, and its one
// obligation is to change NOTHING the game does.
//
// What the game does with them is only RECORD and WRITE: zones (a scope's
// begin and end, nested) into a fixed buffer for the frame, and at the frame's
// end one chunk appended to a capture file. Everything else - the frame graph,
// the call tree with each zone's own and total time - is the EXTERNAL tool's
// (`tools/omkprof.py`), so the cost in the game is a clock read per zone edge
// and a copy per frame. A file is the transport because it is the one every
// target has, Mac OS 9 included.
//
//     OMK_ZONE("name")           this scope, from here to its closing brace
//     OMK_SECTION("name", t0, t1)  a SECTION already timed (in the clock's
//                                  ticks): a flat partition of the frame
//                                  (the viewer's marks), which may cross
//                                  zones and so is kept OUT of the call tree
//
// Off unless a capture was asked for (`prof::open`): a zone then costs one
// test of a global. `OMK_PROFILE=0` compiles every probe to nothing - the
// release build (`make RELEASE=1`) - and this header to a few empty inlines.
//
// THE MAIN THREAD ONLY: a zone opened on a worker thread (`parallelFor`'s
// runners) is not recorded, so the per-frame buffer needs no lock.
//
// THE CAPTURE FILE, little-endian on every host:
//     "OMKPROF1"  u32 version (1)
//     chunks: u8 type, u32 payload length, payload
//       1 NAME   u32 id, the name's bytes                   (once per name)
//       2 FRAME  i32 frame, u64 start (us since the capture opened),
//                u32 duration (us), u32 zones, u32 dropped,
//                then per zone: u32 name id, u16 depth, u16 kind (0 a zone,
//                1 a section), u32 start (us from the frame's), u32 duration
//     A zone's PARENT is the nearest zone before it of one less depth; zones
//     are in the order they OPENED. Sections are not in that order (each is
//     written when it ENDS) and belong to no tree.
//
// THE CONTROL (step 3): the tool steers the game through three files beside
// the capture - files, again, because every target has them:
//     <capture>.ctl    the TOOL writes one line, "<seq> <command> [n]":
//                      pause | resume | step <n> | snapshot. The game reads
//                      it once a frame (and while paused) and acts on a seq
//                      it has not seen.
//     <capture>.state  the GAME writes "paused <frame> <snap>" or
//                      "running <frame> <snap>" whenever that changes
//     <capture>.snap   the GAME writes the frame it is paused on (or was
//                      asked for): "OMKSNAP1", u32 width, height, frame,
//                      snap number, then width*height RGB565 words, LE
// While paused the game does not step: it keeps its window alive and
// presents the last frame. `step n` runs n frames and pauses again.
#pragma once

#include <cstdint>
#include <string>

#if !defined(OMK_PROFILE)
#  define OMK_PROFILE 1
#endif

namespace omk::prof {

#if OMK_PROFILE

// The clock, from the frontend (the gateway: `src/` reads no host clock).
using Clock = std::uint64_t (*)();
void setClock(Clock now, std::uint64_t ticksPerSecond);

// Start writing a capture; -> false when the path cannot be written (and it
// must pass `safeOutputPath`). The calling thread becomes the recorded one.
bool open(const std::string& path);
void close();
bool on();                         // a capture is being written

void beginFrame(long frame);       // the frame's root zone opens
void endFrame();                   // ...closes, and the frame is written

std::uint64_t now();               // the clock, in its ticks
void enter(const char* name);      // a zone opens (a string literal: kept by pointer)
void leave();                      // the innermost open zone closes
void section(const char* name, std::uint64_t t0, std::uint64_t t1);

// The control: -> HOLD when the game must not step this time round (paused,
// no step owed). Called by the frame loop before every step, and by the
// paused loop as it waits.
enum class Run { Go, Hold };
Run control(long frame);
// After a step: -> true when a snapshot is owed (asked for, or the frame a
// pause or a step came to rest on); `snapshot` writes it.
bool snapshotOwed();
void snapshot(long frame, int w, int h, const std::uint16_t* px);

struct Zone {
    explicit Zone(const char* name) : on_(on()) { if (on_) enter(name); }
    ~Zone() { if (on_) leave(); }
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
private:
    bool on_;
};

#  define OMK_PROF_CAT2(a, b) a##b
#  define OMK_PROF_CAT(a, b) OMK_PROF_CAT2(a, b)
#  define OMK_ZONE(name) ::omk::prof::Zone OMK_PROF_CAT(omkZone_, __LINE__)(name)
#  define OMK_SECTION(name, t0, t1) \
       do { if (::omk::prof::on()) ::omk::prof::section((name), (t0), (t1)); } while (0)

#else   // OMK_PROFILE == 0: the release build - nothing at all

using Clock = std::uint64_t (*)();
inline void setClock(Clock, std::uint64_t) {}
inline bool open(const std::string&) { return false; }
inline void close() {}
inline bool on() { return false; }
inline void beginFrame(long) {}
inline void endFrame() {}
inline std::uint64_t now() { return 0; }
enum class Run { Go, Hold };
inline Run control(long) { return Run::Go; }
inline bool snapshotOwed() { return false; }
inline void snapshot(long, int, int, const std::uint16_t*) {}

#  define OMK_ZONE(name) do {} while (0)
#  define OMK_SECTION(name, t0, t1) do {} while (0)

#endif

}  // namespace omk::prof

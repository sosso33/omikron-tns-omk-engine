// SPDX-License-Identifier: GPL-3.0-or-later
// `%zu` for the VitaSDK's newlib - see `c99format.h` for why - and WHERE
// stdout goes.
//
// THE LOG: `freopen` does not redirect stdout on the Vita - newlib keeps
// writing the standard streams to the TTY whatever file is opened over them
// (seen in Vita3K, 2026-09-18: the log file opened, every line went to
// `tty0:`), so a console run would leave `omk-play.log` empty. Every printf
// already comes through here, so `omk_vita_redirect` points stdout and stderr
// at real files instead; `puts` and `putchar` are wrapped too, because GCC
// turns `printf("text\n")` into them.
//
// Linked into every Vita target with `-Wl,--wrap=<fn>` for each function
// below (`CMakeLists.txt`), so every call in the engine reaches the wrapper,
// which strips the `z` / `t` length modifiers from the format and hands the
// rewritten format to the real function. Vita-only by construction: nothing
// on the host links it. A format that needs no change goes through untouched.
#include "c99format.h"

#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <vector>

extern "C" {
int __real_vfprintf(FILE*, const char*, va_list);
int __real_puts(const char*);
int __real_putchar(int);

namespace {
FILE* g_out = nullptr;   // where stdout goes, when redirected
FILE* g_err = nullptr;
FILE* route(FILE* f) {
    if (f == stdout && g_out) return g_out;
    if (f == stderr && g_err) return g_err;
    return f;
}
}  // namespace

// ---- THE LOG IS WRITTEN OFF THE FRAME (2026-09-23) --------------------------
//
// It used to be UNBUFFERED, so every line was its own synchronous write to the
// memory card, on the frame that printed it. A console log of the city showed
// what that costs: frames on the 60-frame report boundary, which print six
// lines, averaged 201 ms against 130 for the other slow frames, and single
// report frames stalled for 1.1-1.2 s with 8-27 ms of marked work - the card
// taking its time over a write. And in the city every frame is slow, so every
// frame wrote its own SLOW FRAME and sections lines: the instrument fed the
// slowness it was reporting.
//
// So stdout and stderr are MEMORY streams now (`funopen`): a write copies the
// bytes into a buffer under a lightweight mutex and returns - no syscall on the
// frame - and a low-priority thread drains them to the card every 250 ms.
// `omk_vita_log_flush` drains synchronously, and every exit path calls it.
// What this gives up is the last ~250 ms on a HARD crash (a fault, not a
// bad_alloc), so an empty `ux0:data/omk/log-sync` file brings back the old
// unbuffered writes for hunting one.
namespace {
struct AsyncLog {
    FILE* real = nullptr;
    std::vector<char> pending, spare;
};
AsyncLog g_logs[2];
SceKernelLwMutexWork g_queue;      // guards `pending`
SceKernelLwMutexWork g_io;         // guards the real files
bool g_async = false;

int logWrite(void* cookie, const char* data, int n) {
    AsyncLog* l = static_cast<AsyncLog*>(cookie);
    sceKernelLockLwMutex(&g_queue, 1, nullptr);
    l->pending.insert(l->pending.end(), data, data + n);
    sceKernelUnlockLwMutex(&g_queue, 1);
    return n;
}

void drainAll() {
    sceKernelLockLwMutex(&g_io, 1, nullptr);
    for (AsyncLog& l : g_logs) {
        if (!l.real) continue;
        sceKernelLockLwMutex(&g_queue, 1, nullptr);
        l.pending.swap(l.spare);
        sceKernelUnlockLwMutex(&g_queue, 1);
        if (!l.spare.empty()) {
            std::fwrite(l.spare.data(), 1, l.spare.size(), l.real);
            std::fflush(l.real);
            l.spare.clear();
        }
    }
    sceKernelUnlockLwMutex(&g_io, 1);
}

int logWriterMain(SceSize, void*) {
    for (;;) {
        sceKernelDelayThread(250 * 1000);
        drainAll();
    }
    return 0;
}

FILE* asyncStream(AsyncLog& l, FILE* real) {
    l.real = real;
    l.pending.reserve(64 * 1024);
    l.spare.reserve(64 * 1024);
    FILE* f = funopen(&l, nullptr, logWrite, nullptr, nullptr);
    if (f) setvbuf(f, nullptr, _IONBF, 0);   // hand every write to `logWrite` at once
    return f;
}

void unbuffered(FILE* out, FILE* err) {        // the old way: every line to the card now
    g_out = out;
    g_err = err;
    if (g_out) setvbuf(g_out, nullptr, _IONBF, 0);
    if (g_err) setvbuf(g_err, nullptr, _IONBF, 0);
}
}  // namespace

// Point stdout / stderr at these files. Null leaves that stream on the TTY.
void omk_vita_redirect(FILE* out, FILE* err) {
    SceIoStat st;
    if (sceIoGetstat("ux0:data/omk/log-sync", &st) >= 0) { unbuffered(out, err); return; }
    sceKernelCreateLwMutex(&g_queue, "omk_logq", 0, 0, nullptr);
    sceKernelCreateLwMutex(&g_io, "omk_logio", 0, 0, nullptr);
    FILE* o = out ? asyncStream(g_logs[0], out) : nullptr;
    FILE* e = err ? asyncStream(g_logs[1], err) : nullptr;
    if ((out && !o) || (err && !e)) { unbuffered(out, err); return; }   // no funopen
    // below the game's own threads (0x10000100 is the default), so the card
    // write never takes a core from the frame
    const SceUID t = sceKernelCreateThread("omk_log", logWriterMain, 0x10000110,
                                           16 * 1024, 0, 0, nullptr);
    if (t < 0 || sceKernelStartThread(t, 0, nullptr) < 0) { unbuffered(out, err); return; }
    g_out = o;
    g_err = e;
    g_async = true;
    std::atexit(drainAll);   // an `exit()` that skips main's own drain loses nothing either
}

// Everything logged so far, onto the card, now. Every exit calls this.
void omk_vita_log_flush() {
    if (g_async) { drainAll(); return; }
    if (g_out) std::fflush(g_out);
    if (g_err) std::fflush(g_err);
}
// A heap checkpoint: `mallinfo` walks newlib's free lists, so it faults on a
// corrupted one - the checkpoint's label, logged first, says where.
void omk_vita_heap_check(const char* where) {
    if (g_out) { std::fputs("heapcheck: ", g_out); std::fputs(where, g_out); std::fputc('\n', g_out); }
    const struct mallinfo mi = mallinfo();
    if (g_out) std::fprintf(g_out, "heapcheck: ok, %d bytes in use\n", mi.uordblks);
}
int __real_vsnprintf(char*, size_t, const char*, va_list);
int __real_vsprintf(char*, const char*, va_list);

namespace {
// Formats are short; one that does not fit is passed through as it was.
constexpr std::size_t kCap = 1024;
}  // namespace

int __wrap_vfprintf(FILE* f, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vfprintf(route(f), omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vsnprintf(char* s, size_t n, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vsnprintf(s, n, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vsprintf(char* s, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vsprintf(s, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_puts(const char* s) {
    if (!g_out) return __real_puts(s);
    return (std::fputs(s, g_out) < 0 || std::fputc('\n', g_out) < 0) ? EOF : 1;
}
int __wrap_putchar(int c) {
    return g_out ? std::fputc(c, g_out) : __real_putchar(c);
}
int __wrap_vprintf(const char* fmt, va_list ap) {
    return __wrap_vfprintf(stdout, fmt, ap);
}
int __wrap_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_snprintf(char* s, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_sprintf(char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}
}  // extern "C"

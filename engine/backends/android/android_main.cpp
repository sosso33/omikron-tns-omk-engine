// SPDX-License-Identifier: GPL-3.0-or-later
// THE GAME'S ENTRY POINT ON ANDROID / THE META QUEST - `todo/quest-port.md`
// §5 step 4.
//
// The Vita's arrangement (`backends/vita/vita_main.cpp`): `play.cpp` is
// compiled with `-Dmain=omk_play_main`, and this file is what SDLActivity
// calls - `SDL_main` - supplying the arguments a desktop user would type.
// Everything lives in the app's own external folder, the one `adb push` can
// reach without any storage permission:
//
//   /sdcard/Android/data/org.omk.play/files/
//     gamedata/        the game's data tree (MESHES/, IAM/, ...), pushed
//     tables/          the tables lifted out of the exe, pushed
//     saves/GAMES      where saves go (never into the data tree - CLAUDE.md §1)
//     omk.ini          the game's own config file, when present
//     args.txt         EXTRA arguments, one per line, AFTER the defaults (the
//                      viewer keeps the last `--res`), '#' starts a comment
//     omk-play-YYYYMMDD-HHMMSS.log / .err   each run's output, dated
//
// stdout and stderr go nowhere on Android, so both are piped: every line
// lands in the dated file AND in `adb logcat -s OMK`.
#include <SDL.h>
#include <SDL_main.h>
#include <SDL_system.h>

#include <android/log.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

int omk_play_main(int argc, char** argv);

namespace {

struct Pipe {
    int fd;              // the read end
    std::FILE* file;     // the dated log
    int prio;            // logcat priority
};

void* drain(void* arg) {
    auto* p = static_cast<Pipe*>(arg);
    std::string line;
    char buf[4096];
    for (;;) {
        const ssize_t n = read(p->fd, buf, sizeof buf);
        if (n <= 0) break;
        if (p->file) { std::fwrite(buf, 1, static_cast<size_t>(n), p->file); std::fflush(p->file); }
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                __android_log_write(p->prio, "OMK", line.c_str());
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    return nullptr;
}

// Point `stream`'s descriptor `fd` into a pipe drained to `path` and logcat.
void redirect(int fd, std::FILE* stream, const std::string& path, int prio) {
    int ends[2];
    if (pipe(ends) != 0) return;
    auto* p = new Pipe{ends[0], std::fopen(path.c_str(), "w"), prio};
    std::setvbuf(stream, nullptr, _IOLBF, 0);   // a line at a time, so a crash keeps its tail
    dup2(ends[1], fd);
    close(ends[1]);
    pthread_t t;
    pthread_create(&t, nullptr, drain, p);
    pthread_detach(t);
}

struct Run {
    std::vector<std::string> args;
    int rc = 1;
};

void* play(void* arg) {
    auto* r = static_cast<Run*>(arg);
    std::vector<char*> argv;
    for (auto& a : r->args) argv.push_back(a.data());
    argv.push_back(nullptr);
    r->rc = omk_play_main(static_cast<int>(r->args.size()), argv.data());
    return nullptr;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int SDL_main(int, char**) {
    const char* ext = SDL_AndroidGetExternalStoragePath();   // creates the folder
    const std::string home = ext ? ext : "/sdcard/Android/data/org.omk.play/files";
    mkdir((home + "/saves").c_str(), 0777);

    char stamp[32] = "undated";
    const std::time_t now = std::time(nullptr);
    if (const std::tm* t = std::localtime(&now)) std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", t);
    redirect(STDOUT_FILENO, stdout, home + "/omk-play-" + stamp + ".log", ANDROID_LOG_INFO);
    redirect(STDERR_FILENO, stderr, home + "/omk-play-" + stamp + ".err", ANDROID_LOG_WARN);

    Run run;
    run.args = {"omk-play", home + "/gamedata", home + "/tables",
                "--saves", home + "/saves/GAMES", "--res", "1280x720"};
    const std::string ini = home + "/omk.ini";
    if (std::ifstream(ini)) { run.args.push_back("--config"); run.args.push_back(ini); }
    if (std::ifstream extra{home + "/args.txt"}) {
        std::string line;
        while (std::getline(extra, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') run.args.push_back(line);
        }
    }
    std::printf("run %s\n", stamp);
    for (const auto& a : run.args) std::printf("%s ", a.c_str());
    std::printf("\n");

    // The game on a thread with an 8 MiB stack, as the Vita gives it: the
    // SDLThread SDLActivity starts has Java's default, ~1 MiB, and the setup
    // recurses. SDL attaches the new thread to the JVM on its first call.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    pthread_t t;
    if (pthread_create(&t, &attr, play, &run) == 0) pthread_join(t, nullptr);
    else play(&run);
    pthread_attr_destroy(&attr);

    std::printf("exit %d\n", run.rc);
    std::fflush(stdout);
    std::fflush(stderr);
    return run.rc;
}

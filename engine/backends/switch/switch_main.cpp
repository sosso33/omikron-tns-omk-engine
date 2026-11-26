// SPDX-License-Identifier: GPL-3.0-or-later
// THE GAME'S ENTRY POINT ON THE NINTENDO SWITCH - `todo/switch-port.md` S1.
//
// The Vita's arrangement (`backends/vita/vita_main.cpp`): `play.cpp` is
// compiled with `-Dmain=omk_play_main` and this file is the real `main`,
// supplying the arguments a desktop user would type. hbmenu launches the NRO
// from `sdmc:/switch/omk/`, and everything else lives beside it:
//
//   sdmc:/switch/omk/omk.nro         the program
//   sdmc:/switch/omk/gamedata/       the game's data tree, copied from the
//                                    disc in BINARY (MESHES/, IAM/, ... inside)
//   romfs:/tables/                   the tables lifted out of the exe, inside
//                                    the NRO (CMakeLists.txt stages them)
//   sdmc:/switch/omk/saves/GAMES     where saves go (never into the data tree -
//                                    CLAUDE.md §1)
//   sdmc:/switch/omk/omk.ini         the config, read when present; OMK's menu writes it
//   sdmc:/switch/omk/args.txt        EXTRA arguments, one per line, AFTER the
//                                    defaults ('#' starts a comment) - `--area`,
//                                    `--res`, `--fps` without a rebuild
//   sdmc:/switch/omk/omk-play-YYYYMMDD-HHMMSS.log / .err   each run's output
//   (link arguments, too: `--env NAME=VALUE` sets an instrument's variable;
//   `--rmtree DIR` deletes a folder and `--recv` takes files from the Mac -
//   both under sdmc:/switch/omk/ only - `tools/switch_link.py --push`)
//   sdmc:/switch/omk/bench/plan.txt  THE PERFORMANCE SWEEP's PLAN, when there:
//                                    one RUN a line (its extra arguments, a
//                                    place: `--area 0 --stand X,Y,Z,YAW`); each
//                                    launch runs the next line under `--bench`
//                                    (`backends/sdl/playbench.cpp`), its report
//                                    `bench/NN-<stamp>.txt`, and loads itself
//                                    again for the line after (hbloader's next
//                                    load) - one launch takes every reading. The
//                                    last renames the plan `plan-done-<stamp>`,
//                                    so the launch after it is the game again.
//
// Launched with `nxlink -s omk.nro` (the dev loop over Wi-Fi) the output goes
// to the Mac's terminal instead of the files - `tools/switch_link.py` is the
// Mac's side, nxlink's protocol in Python. The ARGUMENTS it sends: `--ls DIR`,
// `--cat FILE` and `--b64 FILE` (a binary, base64) print and end - a FILE
// ending in `*` is every name in its folder with that prefix, sorted
// (read-only, no game: the card's logs and
// reports read without taking the card out), `--no-plan` leaves the
// performance sweep's plan alone, and any other is the game's, after
// args.txt's.
//
// The frame is the mode's own: 1280x720 handheld, 1920x1080 docked, taken at
// launch (a dock change mid-run is step S4). The 3D keeps its horizontal FOV;
// the 640x480 interface is stretched by the engine's own I2D scaling, as on
// the Vita; `--res 960x720` in args.txt gives the 4:3 frame back.
#include <switch.h>

#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <ctime>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#include "platform/hostthread.h"

int omk_play_main(int argc, char** argv);

namespace {
constexpr const char* kHome   = "sdmc:/switch/omk";
constexpr const char* kRoot   = "sdmc:/switch/omk/gamedata";
constexpr const char* kTables = "romfs:/tables";
constexpr const char* kSaves  = "sdmc:/switch/omk/saves/GAMES";
constexpr const char* kIni    = "sdmc:/switch/omk/omk.ini";
constexpr const char* kExtra  = "sdmc:/switch/omk/args.txt";
constexpr const char* kBench  = "sdmc:/switch/omk/bench";
constexpr int kPushPort = 28772;   // `switch_link.py --push` listens here (nxlink's stdout is 28771)
constexpr const char* kNro    = "sdmc:/switch/omk/omk.nro";   // when argv[0] does not say

// the plan's runs: its lines, blanks and `#` comments out
std::vector<std::string> benchPlan() {
    std::vector<std::string> runs;
    std::ifstream in{std::string(kBench) + "/plan.txt"};
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') runs.push_back(line);
    }
    return runs;
}
int benchNext() {
    std::ifstream in{std::string(kBench) + "/next.txt"};
    int n = 0;
    return in >> n ? n : 0;
}

// `play.cpp`'s `main` is one function with ~470 locals in a single frame; the
// Vita needed 8 MiB for it and the recursion below it. An NRO's main stack is
// whatever the loader gave it, so the game runs on a thread whose stack this
// file sets.
constexpr std::size_t kGameStackBytes = 8u * 1024 * 1024;

struct GameArgs {
    std::vector<std::string> args;
    int rc = 1;
};

void* runGame(void* p) {
    auto* g = static_cast<GameArgs*>(p);
    std::vector<char*> argv;
    for (auto& a : g->args) argv.push_back(a.data());
    argv.push_back(nullptr);
    try {
        g->rc = omk_play_main(static_cast<int>(g->args.size()), argv.data());
    } catch (const std::bad_alloc&) {
        // out of memory is SAID, not a silent abort: the log is the only
        // report a console run leaves
        std::printf("FATAL: out of memory (std::bad_alloc)\n");
        std::fprintf(stderr, "FATAL: out of memory\n");
    }
    return nullptr;
}

// the process's memory as the kernel accounts it - applet mode (launched from
// the album) gets a fraction of what title takeover (R held over a game) gets
void logMemory(const char* when) {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::printf("memory %s: %llu MB used of %llu MB\n", when,
                static_cast<unsigned long long>(used >> 20),
                static_cast<unsigned long long>(total >> 20));
}

const char* appletTypeName(AppletType t) {
    switch (t) {
    case AppletType_Application: return "application (title takeover: full memory)";
    case AppletType_SystemApplication: return "system application";
    case AppletType_LibraryApplet: return "library applet (launched from the album: LITTLE memory)";
    case AppletType_OverlayApplet: return "overlay applet";
    case AppletType_SystemApplet: return "system applet";
    default: return "unknown";
    }
}
}  // namespace

int main(int argc, char** argv) {
    romfsInit();
    mkdir(kHome, 0777);
    mkdir("sdmc:/switch/omk/saves", 0777);

    mkdir(kBench, 0777);
    // ONE PAIR A RUN, DATED, so the runs sort by name and a report never
    // overwrites the one before it. The console's local clock.
    char stamp[32] = "undated";
    const std::time_t now = std::time(nullptr);
    if (const std::tm* t = std::localtime(&now))
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", t);

    const bool nxlink = __nxlink_host.s_addr != 0;
    if (nxlink) {
        socketInitializeDefault();
        nxlinkStdio();
    } else {
        const std::string base = std::string(kHome) + "/omk-play-" + stamp;
        std::freopen((base + ".log").c_str(), "w", stdout);
        std::freopen((base + ".err").c_str(), "w", stderr);
        std::printf("run %s\n", stamp);
    }
    // a line at a time, so a crash keeps its tail
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // THE LINK'S ARGUMENTS (nxlink's, after argv[0]): the read-only requests
    // first - answered and the program ended, no game
    std::vector<std::string> linkArgs;
    for (int i = 1; i < argc; ++i)
        if (argv[i] && argv[i][0]) linkArgs.push_back(argv[i]);
    bool noPlan = false, served = false;
    // a FILE ending in `*`: its folder's names with that prefix, sorted
    const auto expand = [](const std::string& path) {
        std::vector<std::string> out;
        if (path.empty() || path.back() != '*') { out.push_back(path); return out; }
        const auto slash = path.rfind('/');
        const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
        const std::string pre = path.substr(slash == std::string::npos ? 0 : slash + 1, path.size() - 1 - (slash + 1));
        if (DIR* d = opendir(dir.c_str())) {
            while (const dirent* e = readdir(d))
                if (std::string(e->d_name).rfind(pre, 0) == 0) out.push_back(dir + "/" + e->d_name);
            closedir(d);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    const auto b64 = [](std::FILE* f) {
        static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned char in[3];
        int col = 0;
        std::size_t n;
        while ((n = std::fread(in, 1, 3, f)) > 0) {
            const unsigned v = in[0] << 16 | (n > 1 ? in[1] << 8 : 0) | (n > 2 ? in[2] : 0);
            char o[4] = {k[v >> 18 & 63], k[v >> 12 & 63], n > 1 ? k[v >> 6 & 63] : '=', n > 2 ? k[v & 63] : '='};
            std::fwrite(o, 1, 4, stdout);
            if (++col == 19) { std::fputc('\n', stdout); col = 0; }
        }
        std::fputc('\n', stdout);
    };
    for (std::size_t i = 0; i < linkArgs.size(); ++i) {
        const std::string& a = linkArgs[i];
        if (a == "--no-plan") { noPlan = true; continue; }
        // `--env NAME=VALUE`: an instrument's variable (`OMK_UPLOAD_PROBE`...),
        // set before the game reads it
        if (a == "--env" && i + 1 < linkArgs.size()) {
            const std::string kv = linkArgs[++i];
            const auto eq = kv.find('=');
            if (eq != std::string::npos) {
                setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
                std::printf("switch: %s set\n", kv.c_str());
            }
            continue;
        }
        if ((a == "--ls" || a == "--cat" || a == "--b64") && i + 1 < linkArgs.size()) {
          for (const std::string& path : a == "--ls" ? std::vector<std::string>{linkArgs[i + 1]} : expand(linkArgs[i + 1])) {
            served = true;
            if (a == "--ls") {
                std::printf("ls %s\n", path.c_str());
                if (DIR* d = opendir(path.c_str())) {
                    while (const dirent* e = readdir(d)) {
                        struct stat st{};
                        const std::string full = path + "/" + e->d_name;
                        const bool ok = stat(full.c_str(), &st) == 0;
                        std::printf("%10lld %s%s\n", ok ? static_cast<long long>(st.st_size) : -1LL, e->d_name,
                                    ok && S_ISDIR(st.st_mode) ? "/" : "");
                    }
                    closedir(d);
                } else {
                    std::printf("(not a folder)\n");
                }
            } else if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
                std::printf("%s %s\n", a == "--b64" ? "b64" : "cat", path.c_str());
                if (a == "--b64") {
                    b64(f);
                } else {
                    char buf[4096];
                    std::size_t n;
                    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) std::fwrite(buf, 1, n, stdout);
                }
                std::fclose(f);
                std::printf("\n-- end of %s\n", path.c_str());
            } else {
                std::printf("cat %s: cannot open\n", path.c_str());
            }
          }
          ++i;
        }
    }
    // ...and the WRITES, under kHome alone: `--rmtree DIR`, then `--recv` -
    // the Mac's `switch_link.py --push` listening on kPushPort: per file its
    // path's length (u32 LE, 0 ends), the path under kHome, its size (u64
    // LE), its bytes; a line said per file, the stream's end acknowledged
    const auto underHome = [](const std::string& p) {
        const std::string home = std::string(kHome) + "/";
        return p.rfind(home, 0) == 0 && p.find("..") == std::string::npos && p.size() > home.size();
    };
    for (std::size_t i = 0; i < linkArgs.size(); ++i) {
        if (linkArgs[i] != "--rmtree" || i + 1 >= linkArgs.size()) continue;
        const std::string dir = linkArgs[++i];
        served = true;
        if (!underHome(dir)) { std::printf("rmtree %s: refused - not under %s\n", dir.c_str(), kHome); continue; }
        int files = 0, dirs = 0;
        // depth first, without recursion's stack: the folders found, deleted last
        std::vector<std::string> todo{dir}, found;
        while (!todo.empty()) {
            const std::string d = todo.back();
            todo.pop_back();
            found.push_back(d);
            if (DIR* h = opendir(d.c_str())) {
                while (const dirent* e = readdir(h)) {
                    const std::string n = e->d_name;
                    if (n == "." || n == "..") continue;
                    const std::string full = d + "/" + n;
                    struct stat st{};
                    if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) todo.push_back(full);
                    else if (std::remove(full.c_str()) == 0) ++files;
                }
                closedir(h);
            }
        }
        for (auto it = found.rbegin(); it != found.rend(); ++it) dirs += rmdir(it->c_str()) == 0 ? 1 : 0;
        std::printf("rmtree %s: %d files and %d folders deleted\n", dir.c_str(), files, dirs);
    }
    if (std::find(linkArgs.begin(), linkArgs.end(), "--recv") != linkArgs.end()) {
        served = true;
        const int s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(kPushPort);
        a.sin_addr = __nxlink_host;
        if (s < 0 || connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            std::printf("recv: no connection to the Mac's port %d\n", kPushPort);
        } else {
            const auto readAll = [s](void* dst, std::size_t n) {
                auto* p = static_cast<char*>(dst);
                while (n > 0) {
                    const ssize_t r = recv(s, p, n, 0);
                    if (r <= 0) return false;
                    p += r;
                    n -= static_cast<std::size_t>(r);
                }
                return true;
            };
            std::vector<char> buf(1u << 20);
            int files = 0;
            unsigned long long total = 0;
            const u64 t0 = armGetSystemTick();
            for (;;) {
                std::uint32_t len = 0;
                if (!readAll(&len, 4) || len == 0 || len > 1024) break;
                std::string rel(len, '\0');
                std::uint64_t size = 0;
                if (!readAll(rel.data(), len) || !readAll(&size, 8)) break;
                const std::string path = std::string(kHome) + "/" + rel;
                if (!underHome(path)) { std::printf("recv %s: refused\n", rel.c_str()); break; }
                // its folders, then the file
                for (std::size_t k = std::string(kHome).size() + 1; (k = path.find('/', k)) != std::string::npos; ++k)
                    mkdir(path.substr(0, k).c_str(), 0777);
                std::FILE* f = std::fopen(path.c_str(), "wb");
                std::uint64_t left = size;
                bool ok = f != nullptr;
                while (left > 0) {
                    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
                    if (!readAll(buf.data(), n)) { ok = false; left = 0; break; }
                    if (f && std::fwrite(buf.data(), 1, n, f) != n) ok = false;
                    left -= n;
                }
                if (f) std::fclose(f);
                if (!ok) { std::printf("recv %s: FAILED\n", rel.c_str()); break; }
                ++files;
                total += size;
                if (files % 100 == 0) std::printf("recv: %d files, %llu MB\n", files, total >> 20);
            }
            const double sec = static_cast<double>(armTicksToNs(armGetSystemTick() - t0)) / 1e9;
            std::printf("recv: %d files, %.1f MB in %.0f s (%.1f MB/s)\n", files, static_cast<double>(total) / 1e6,
                        sec, sec > 0 ? static_cast<double>(total) / 1e6 / sec : 0.0);
            const std::uint32_t ack = static_cast<std::uint32_t>(files);
            send(s, &ack, 4, 0);
            close(s);
        }
    }
    if (served) {
        std::fflush(stdout);
        if (nxlink) socketExit();
        romfsExit();
        return 0;
    }

    const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    GameArgs game;
    game.args = {"omk-play", kRoot, kTables, "--saves", kSaves,
                 "--res", docked ? "1920x1080" : "1280x720",
                 // always named: read when it is there, and where OMK's options menu writes
                 "--config", kIni};
    if (std::ifstream extra{kExtra}) {
        std::string line;
        while (std::getline(extra, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') game.args.push_back(line);
        }
    }
    for (std::size_t i = 0; i < linkArgs.size(); ++i) {
        if (linkArgs[i] == "--no-plan") continue;
        if (linkArgs[i] == "--env") { ++i; continue; }
        game.args.push_back(linkArgs[i]);
    }
    // THE PERFORMANCE SWEEP's plan: this launch's run, its place's arguments
    // split at the spaces, and where its report goes
    const std::vector<std::string> plan = noPlan ? std::vector<std::string>() : benchPlan();
    const int run = plan.empty() ? -1 : benchNext();
    const bool benching = run >= 0 && run < static_cast<int>(plan.size());
    if (benching) {
        // a line starting `!` is the RUN'S OWN: its arguments as they stand,
        // no report added (a series of loads, its own `--bench` or `--frames`);
        // `--env NAME=VALUE` in a line sets that variable for the run
        std::string line = plan[static_cast<std::size_t>(run)];
        const bool raw = !line.empty() && line[0] == '!';
        if (raw) line.erase(0, 1);
        std::vector<std::string> words;
        std::string word;
        for (const char c : line + " ") {
            if (c != ' ') { word += c; continue; }
            if (!word.empty()) words.push_back(word);
            word.clear();
        }
        for (std::size_t w = 0; w < words.size(); ++w) {
            if (words[w] == "--env" && w + 1 < words.size()) {
                const std::string kv = words[++w];
                const auto eq = kv.find('=');
                if (eq != std::string::npos) setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
                continue;
            }
            game.args.push_back(words[w]);
        }
        char report[160];
        // 90 frames settled and 240 measured a step: ~5 minutes a place
        std::snprintf(report, sizeof report, "%s/%02d-%s.txt:90:240", kBench, run + 1, stamp);
        if (!raw) {
            game.args.push_back("--bench");
            game.args.push_back(report);
        }
        if (std::FILE* f = std::fopen((std::string(kBench) + "/next.txt").c_str(), "w")) {
            std::fprintf(f, "%d\n", run + 1);
            std::fclose(f);
        }
        std::printf("switch: the performance sweep, run %d of %zu: %s\n", run + 1, plan.size(),
                    plan[static_cast<std::size_t>(run)].c_str());
    }
    std::printf("switch: %s, %s, output to %s\n", appletTypeName(appletGetAppletType()),
                docked ? "docked" : "handheld", nxlink ? "nxlink" : "the SD card");
    logMemory("at main");
    for (const auto& a : game.args) std::printf("%s ", a.c_str());
    std::printf("\n");

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kGameStackBytes);
    pthread_t thread;
    if (pthread_create(&thread, &attr, runGame, &game) == 0) {
        pthread_join(thread, nullptr);
    } else {
        std::printf("FATAL: no thread with a %zu MB stack could be made\n", kGameStackBytes >> 20);
    }
    pthread_attr_destroy(&attr);

    // ...and the next run of the plan loaded after this one, or the plan put
    // away when this was its last
    if (benching) {
        if (run + 1 < static_cast<int>(plan.size())) {
            const std::string self = argc > 0 && argv[0] && argv[0][0] ? argv[0] : kNro;
            const Result rc = envSetNextLoad(self.c_str(), self.c_str());
            std::printf("switch: the performance sweep's next run loaded after this one (%s): %s\n",
                        self.c_str(), R_SUCCEEDED(rc) ? "set" : "REFUSED - launch the game again");
        } else {
            std::rename((std::string(kBench) + "/plan.txt").c_str(),
                        (std::string(kBench) + "/plan-done-" + stamp + ".txt").c_str());
            std::remove((std::string(kBench) + "/next.txt").c_str());
            std::printf("switch: the performance sweep's plan done - put away as plan-done-%s.txt\n", stamp);
        }
    }
    // ...and every background thread the game left running WAITED FOR before
    // the process goes to the next program (hbloader loads it into this one:
    // a preload job running on wrote into the next run's heap)
    const int drained = omk::hostthread::drainDetached();
    if (drained > 0) std::printf("switch: %d background thread(s) waited for before the next program\n", drained);
    logMemory("at exit");
    std::fflush(stdout);
    std::fflush(stderr);
    if (nxlink) socketExit();
    romfsExit();
    return game.rc;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT `main` DEFINED AS LAMBDAS - `PlayState`'s methods since
// `todo/play-split.md` S3e (2026-10-02), each body moved byte for byte,
// each with the comment that stood above it, in their old order. A method
// that reads one of the objects built in place (`fs`, `session`, ...) names
// it first under its old name.
#include "playstate.h"

// THE THREE VOLUME ROWS (10..12) - ATTENUATIONS, and one law for all
// three: `-10000 * a / 100` hundredths of a dB, so `a` dB. Read where the
// engine reads them (`05_sys.c` ~1037..1062, every frame): the music's is
// summed into `Music_SetVolume` (the Session's `musicOption_`), the
// dialogue's goes to the speech buffer `Morph_Start` plays a line through
// (`sub_42BBB0`), the effects' is ADDED to every world voice's own volume
// (`sub_46C1C0`, `dword_53B31C`). The interface's blips take none of them.
// A sound already playing keeps the gain it started with - the engine
// re-sets its sixteen voices at once, which this frontend cannot.
float PlayState::attGain(int a) {
    return static_cast<float>(std::pow(10.0, -std::clamp(a, 0, 100) / 20.0));
}

float PlayState::fxGain() {
 return attGain(settings.v.volumeEffects);
}

float PlayState::dialogueGain() {
 return attGain(settings.v.volumeDialogue);
}

// The ENHANCEMENT, and it is subordinate to the option above: with row 5
// off nothing draws whatever this says. Default 0 = what the engine draws.
// `--enhance-all` is a BASE: an explicit flag beats it, and so does a
// specific `[Enhancements]` key, because `resolveSettings` decided that
// half already.
int PlayState::enh(int flag, int fromSettings, int top) {
    return flag >= 0 ? flag : enhanceAll ? top : fromSettings;
}

void PlayState::applyTextScale(int w, int h) {
    auto& lay = *lay_;
    if (textScaling <= 0) { lay.setGlyphScale(1, 1, 0); return; }
    if (w * 480 <= h * 640) lay.setGlyphScale(w, 640, uiScaling);
    else                    lay.setGlyphScale(h, 480, uiScaling);
}

// the LOOPING scene voices, keyed the way `Script_StopSound` matches them:
// by (wav, node). Only loops are kept - a one-shot ends by itself.
// omk-play 72: WHICH audio source is the one that will not stop. Every
// start is labelled with its length, and `flushAudio` says how many
// one-shots it does NOT clear - the streamed music is flushed on a switch
// and `shots_` are not, so anything long started as a one-shot outlives
// an area change, a music switch and a cutscene.
void PlayState::sfxLog(const char* what, std::size_t samples, int a, int b,
                            float gain, const float* peakOf) {
    const double secs = samples / static_cast<double>(kDeviceRate) / 2.0;
    // the PEAK of what is fed, because a reader heard the effects "very
    // low" against the music: the number says whether the source or the
    // mix is quiet. Measured ONCE, where the sample is converted
    // (`SfxSample::peak`) - it was a pass over every sample on every play.
    const float peak = peakOf ? *peakOf : 0.0f;
    const bool pcm = peakOf != nullptr;
    if (secs >= 0.75 || pcm)
        std::printf("audio: %-14s %6.2f s  (%d, %d)  gain %.2f  peak %.3f\n",
                    what, secs, a, b, gain, static_cast<double>(peak));
}

void PlayState::takeCamRequest(int phase) {
    takeCamPhase = phase;
    takeCamClock = 0.0f;
    for (int k = 0; k < 3; ++k) { takeCamFromEye[k] = lastEye[k]; takeCamFromAt[k] = lastAt[k]; }
    takeCamFromFov = lastFov;
    if (phase == 1) {                       // the take: preset 1, 30 frames
        for (int k = 0; k < 3; ++k) { takeCamEye[k] = kTakeCamEye[k]; takeCamAt[k] = kTakeCamAt[k]; }
        takeCamFov = kTakeCamFov; takeCamTravel = kTakeCamTravel;
    }
}

void PlayState::playerCamRequest(const float eye[3], const float at[3], float fov, float frames) {
    takeCamRequest(1);
    for (int k = 0; k < 3; ++k) { takeCamEye[k] = eye[k]; takeCamAt[k] = at[k]; }
    takeCamFov = fov; takeCamTravel = frames;
    takeCam = true;
}

void PlayState::fallCamRequest(int mode, bool flag, const char* who, int actorState) {
    auto& session = *session_;
    static constexpr float kOverEye[3] = {0.0f, 118.1102f, -3.937f};
    static constexpr float kOverAt[3]  = {0.0f, 0.0f, 0.0f};
    // `if (a1 && u32(a1, 404) != 3)` - nothing at all in ACTOR_STATE 3
    if (actorState == 3) return;
    float travel = -1.0f;
    if (mode == 18) {
        travel = flag ? 60.0f : 30.0f;
        playerCamRequest(kOverEye, kOverAt, 75.0f, travel);
    } else if (mode == 19 && fallCamMode == 18) {
        travel = 60.0f;
        playerCamRequest(kOverEye, kOverAt, 75.0f, travel);
    } else if ((mode == 16 && fallCamMode == 18) || (mode == 0 && fallCamMode == 19)) {
        travel = mode == 16 ? 30.0f : 90.0f;
        takeCamRequest(3);
        takeCamTravel = travel;
        takeCam = true;
    }
    if (travel < 0.0f) return;
    fallCamMode = (mode == 18 || mode == 19) ? mode : 0;
    std::printf("frame %ld: %s - sub_414DE0 camera %d over %.0f frames\n",
                session.frameNo(), who, mode, double(travel));
}

// ...and its probe GRID, rebuilt wherever `playerSoup` is refilled: the
// shadows and the crowd's feet probe it every frame, and a linear scan of
// the city per bone was most of the frame (todo/optimization.md step 2)
// Which of `playerSoup`'s triangles belong to a mesh that has moved since the
// sets last changed: those go in `playerGrid.moving`, rebuilt every moving
// frame, the rest in `playerGrid.fixed`, rebuilt only when this changes
// (todo/optimization.md step 7c).
void PlayState::rebuildFixedGrid() {
    playerFixedTri.assign(playerMovingTri.size(), 0);
    for (std::size_t t = 0; t < playerMovingTri.size(); ++t) playerFixedTri[t] = !playerMovingTri[t];
    playerGrid.fixed = omk::buildSoupGrid(playerSoup, 256.0, &playerFixedTri);
}

void PlayState::rebuildMovingGrid() {
    // from the id list, NOT the mask: the mask build walks all of the soup
    // (15137 triangles in Anekbah) to find the ~750 that move
    playerGrid.moving = omk::buildSoupGrid(playerSoup, 256.0,
                                           std::span<const std::uint32_t>(playerMovingIds));
    playerGrid.useParts = false;    // the grid stands for the moving layer again
}

// the same merge for the STEEP faces, so the controller can stand on a
// slope and slide off it instead of finding no floor (omk-play 67)
// ...and ITS two-layer grid, kept exactly as `playerGrid` is kept over
// `playerSoup` - rebuilt wherever `playerSteep` is refilled, the moving
// layer from the steep triangles the motion patch re-places. The player's
// body sweep and the camera's sweep walk it (todo/optimization.md step 11).
void PlayState::rebuildSteepFixedGrid() {
    steepFixedTri.assign(steepMovingTri.size(), 0);
    for (std::size_t t = 0; t < steepMovingTri.size(); ++t) steepFixedTri[t] = !steepMovingTri[t];
    playerSteepGrid.fixed = omk::buildSoupGrid(playerSteep, 256.0, &steepFixedTri);
}

void PlayState::rebuildSteepMovingGrid() {
    playerSteepGrid.moving = omk::buildSoupGrid(playerSteep, 256.0,
                                                std::span<const std::uint32_t>(steepMovingIds));
    playerSteepGrid.useParts = false;
}

std::vector<float> PlayState::loadSlot(int screen, int slot) {
    const auto& fs = *fs_;
    const std::string& nm = w.soundName(screen, slot);
    if (nm.empty()) return std::vector<float>{};
    const auto path = fs.resolve("I2D/sounds/" + nm + ".wav");
    if (!path) return std::vector<float>{};
    return wavToDevice(omk::DataFs::readPath(*path), kDeviceRate);
}

void PlayState::present(const omk::Surface& pic) {
    if (gpuPresentSurface(pic)) return;   // the GPU window's own (playgpu_<backend>.cpp)
    front.present(pic);
}

// Queued, not mixed: a menu plays one blip at a time and the device is a
// FIFO. Flushing first keeps them prompt - a blip that waits behind the
// previous one arrives after the selection has already moved on.
// Mixed OVER whatever is streaming, not queued behind it and not flushing
// it - which is what lets a blip and the music coexist.
void PlayState::blip(const std::vector<float>& v) {
 front.playSound(v);
}

// (struct PedStaged: `backends/sdl/playtypes.h`, todo/play-split.md)
// level-k tracks: the level-0 ones with every mesh index moved k skeletons on
// The skeleton a set of tracks poses: the first track's mesh followed up
// to its root. A model with one skeleton answers its only root.
int PlayState::skeletonRootWalk(const CharModel& mo, const omk::NodeTracks& t) {
    int m = -1;
    if (!t.ids.empty() && t.ids[0] >= 0)
        for (std::size_t j = 0; j < mo.meshes.size(); ++j)
            if (mo.meshes[j].index == t.ids[0]) { m = static_cast<int>(j); break; }
    if (m < 0) return mo.root;
    for (int guard = 0; guard < 64; ++guard) {
        const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
        int next = -1;
        for (std::size_t j = 0; j < mo.meshes.size(); ++j)
            if (mo.meshes[j].id == pid) { next = static_cast<int>(j); break; }
        if (next < 0) return m;
        m = next;
    }
    return m;
}

// ...cached on the model by the first track's mesh id, the only thing
// of the tracks the walk reads
int PlayState::skeletonRootOf(const CharModel& mo, const omk::NodeTracks& t) {
    const std::int32_t key = t.ids.empty() ? std::numeric_limits<std::int32_t>::min() : t.ids[0];
    const auto it = mo.skelRootByFirstId.find(key);
    if (it != mo.skelRootByFirstId.end()) return it->second;
    const int r = skeletonRootWalk(mo, t);
    mo.skelRootByFirstId.emplace(key, r);
    return r;
}

bool PlayState::hasSeveralSkeletons(const CharModel& mo) {
    if (mo.severalSkeletons >= 0) return mo.severalSkeletons == 1;
    int roots = 0;
    for (const auto& m : mo.meshes) {
        bool hasParent = false;
        for (const auto& p : mo.meshes) if (p.id == m.parent) { hasParent = true; break; }
        if (!hasParent) ++roots;
    }
    mo.severalSkeletons = roots > 1 ? 1 : 0;
    return roots > 1;
}

int PlayState::lodChainOf(const CharModel& mo) {
    if (mo.lodLevels >= 0) return mo.lodLevels;
    mo.lodLevels = 0;
    const std::size_t nm = mo.meshes.size();
    for (std::size_t j = 0; j < nm; ++j)
        if (mo.meshes[j].index != static_cast<std::int32_t>(j)) return 0;   // the rule is by index
    std::unordered_map<std::int32_t, int> byId;
    for (std::size_t j = 0; j < nm; ++j) byId.emplace(mo.meshes[j].id, static_cast<int>(j));
    std::vector<int> treeOf(nm, -1);
    std::vector<int> roots;
    for (std::size_t j = 0; j < nm; ++j) {
        int m = static_cast<int>(j);
        for (int guard = 0; guard < 64; ++guard) {
            const auto it = byId.find(mo.meshes[static_cast<std::size_t>(m)].parent);
            if (it == byId.end() || it->second == m) break;
            m = it->second;
        }
        treeOf[j] = m;
        if (m == static_cast<int>(j)) roots.push_back(m);
    }
    if (roots.size() < 2 || roots.size() > 4) return 0;
    // largest first, by the corners each draws (`sub_453A70`: vertex +
    // face count - the order is what matters, and corners are faces x 3)
    std::map<int, std::size_t> size;
    for (std::size_t c = 0; c < mo.rest.cornerMesh.size(); ++c) {
        const auto mi = mo.rest.cornerMesh[c];
        if (mi >= 0 && static_cast<std::size_t>(mi) < nm) ++size[treeOf[static_cast<std::size_t>(mi)]];
    }
    std::stable_sort(roots.begin(), roots.end(), [&](int a, int b) { return size[a] > size[b]; });
    int count = 0;
    for (std::size_t j = 0; j < nm; ++j) count += treeOf[j] == roots[0];
    for (std::size_t k = 1; k < roots.size(); ++k) {
        int cnt = 0;
        for (std::size_t j = 0; j < nm; ++j) {
            cnt += treeOf[j] == roots[k];
            if (treeOf[j] != roots[0]) continue;
            const std::size_t jj = j + k * static_cast<std::size_t>(count);
            if (jj >= nm || treeOf[jj] != roots[k]) return 0;
        }
        if (cnt != count) return 0;
    }
    for (std::size_t k = 0; k < roots.size(); ++k) {
        mo.lodRootAt[k] = roots[k];
        mo.lodMask[k].assign(nm, 0);
        for (std::size_t j = 0; j < nm; ++j) mo.lodMask[k][j] = treeOf[j] == roots[k] ? 1 : 0;
    }
    mo.lodCount = count;
    mo.lodLevels = static_cast<int>(roots.size());
    return mo.lodLevels;
}

const omk::NodeTracks * PlayState::lodTracksFor(const omk::NodeTracks* base, int level, int count) {
    if (!base || level == 0) return base;
    OMK_MEM_TAG("crowd: LOD tracks");   // the profiler's category (todo/debug-tools.md)
    const auto key = std::make_pair(base, level);
    auto it = pedLodTracks.find(key);
    if (it == pedLodTracks.end()) {
        omk::NodeTracks t = *base;
        for (auto& id : t.ids) if (id >= 0) id += level * count;
        it = pedLodTracks.emplace(key, std::move(t)).first;
    }
    return &it->second;
}

const omk::Geometry & PlayState::lodRestFor(const std::string& model, const CharModel& mo, int rootMesh) {
    OMK_MEM_TAG("crowd: LOD rest geometry");   // the profiler's category (todo/debug-tools.md)
    auto& per = pedLodRest[model];
    auto it = per.find(rootMesh);
    if (it != per.end()) return it->second;
    // the meshes whose ancestor chain ends at `rootMesh`
    std::vector<bool> keep(mo.meshes.size(), false);
    for (std::size_t i = 0; i < mo.meshes.size(); ++i) {
        int m = static_cast<int>(i);
        for (int guard = 0; guard < 64 && m >= 0; ++guard) {
            if (m == rootMesh) { keep[i] = true; break; }
            const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
            int next = -1;
            for (std::size_t j = 0; j < mo.meshes.size(); ++j)
                if (mo.meshes[j].id == pid) { next = static_cast<int>(j); break; }
            m = next;
        }
    }
    omk::Geometry g;
    g.batches.clear();
    for (const auto& b : mo.rest.batches) {
        omk::Batch nb = b;
        nb.start = g.corners.size(); nb.count = 0;
        for (std::size_t c = b.start; c + 3 <= b.start + b.count; c += 3) {
            const auto mi = mo.rest.cornerMesh[c];
            if (mi < 0 || static_cast<std::size_t>(mi) >= keep.size() || !keep[static_cast<std::size_t>(mi)]) continue;
            for (int k = 0; k < 3; ++k) {
                g.corners.push_back(mo.rest.corners[c + static_cast<std::size_t>(k)]);
                g.cornerMesh.push_back(mo.rest.cornerMesh[c + static_cast<std::size_t>(k)]);
                if (!mo.rest.cornerVertex.empty()) g.cornerVertex.push_back(mo.rest.cornerVertex[c + static_cast<std::size_t>(k)]);
                if (!mo.rest.cornerDeclared.empty()) g.cornerDeclared.push_back(mo.rest.cornerDeclared[c + static_cast<std::size_t>(k)]);
            }
            nb.count += 3;
        }
        if (nb.count) g.batches.push_back(nb);
    }
    return per.emplace(rootMesh, std::move(g)).first->second;
}

// `sub_453A70`: the model's root sub-objects sorted by vertex+face count
// DESCENDING - the LOD ladder. Sub-object 0 is what `sub_4544B0` hands
// ambient traffic (`v16[1]`); the reserved slider takes sub-object 1
// (`v16[2]`), which is read from the call sites, NOT judged by eye, and
// not drawn here because the player's ride is not ported.
int PlayState::heaviestRootOf(const CharModel& mo) {
    int best = mo.root;
    std::size_t bestWeight = 0;
    for (std::size_t i = 0; i < mo.meshes.size(); ++i) {
        bool hasParent = false;
        for (const auto& q : mo.meshes) if (q.id == mo.meshes[i].parent) { hasParent = true; break; }
        if (hasParent) continue;
        // the subtree's weight, the counts `sub_453A70` adds (+44 and +48)
        std::size_t w = 0;
        for (std::size_t j = 0; j < mo.meshes.size(); ++j) {
            int m = static_cast<int>(j);
            for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                if (static_cast<std::size_t>(m) == i) {
                    w += static_cast<std::size_t>(mo.meshes[j].vertices) +
                         static_cast<std::size_t>(mo.meshes[j].triangles) +
                         static_cast<std::size_t>(mo.meshes[j].quads);
                    break;
                }
                const std::int32_t pid = mo.meshes[static_cast<std::size_t>(m)].parent;
                int next = -1;
                for (std::size_t k = 0; k < mo.meshes.size(); ++k)
                    if (mo.meshes[k].id == pid) { next = static_cast<int>(k); break; }
                m = next;
            }
        }
        if (w > bestWeight) { bestWeight = w; best = static_cast<int>(i); }
    }
    return best;
}

// THE SHOOT-MODE POSE. These characters carry no `.CTL` in any of the
// three 9-byte slots, so nothing the actor runtime does can pose them:
// `Shoot_ActorEnter` resolves their CHARACTER TYPE's group in the area's
// `.ani` (`sub_434530`) and `Shoot_ActorAction` asks it for a clip by
// BEHAVIOUR type through `List_PickRandomByType`:
//
//     action 0, 5        type 11, else 25
//     action 1           type 9
//     action 2,3,4,6,7   type 10, else 9
//
// WHAT RUNS HERE IS THE SCRIPT'S LAST ACTION, NOT THE AI, and that is a
// decision rather than an omission. `Shoot_TickNpc` calls one of four
// brains every frame; `actor/shoot.h` models them and is deliberately not
// wired to this, because the generic arm - 302 of the 306 shipped sites -
// "takes the first edge and RECORDS the choice rather than pretending to
// compute it": the real branch needs the navigation node, the line of
// sight and the weapon's range, none of which this tree has. Running it
// here would draw a deterministic first-edge walk as if it were the
// game's behaviour, which is putting a guess where a fact belongs.
//
// THE PICK, though, is a fact and is now faithful. `List_PickRandomByType`
// returns a RANDOM one of the matches, and 40 of the 195 (library, group,
// behaviour type) buckets in the shipped `.ani` hold more than one clip -
// up to six - so the choice is real in a fifth of them. The roll is seeded
// per (actor, action) rather than re-rolled every frame: the engine rolls
// once per `Shoot_ActorAction` and this has no AI asking again, so a
// per-request seed is the same shape and keeps a still frame
// reproducible. LABELLED as that.
const omk::PedClip * PlayState::shootClipFor(int group, int action) {
    if (pedAni.empty()) return nullptr;
    auto it = shootClips.find(group);
    if (it == shootClips.end())
        it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
    if (it->second.empty()) return nullptr;
    int want[2] = {9, -1};
    if (action == 0 || action == 5)      { want[0] = 11; want[1] = 25; }
    else if (action == 1)                { want[0] = 9;  want[1] = -1; }
    else if (action >= 2 && action <= 7) { want[0] = 10; want[1] = 9;  }
    for (int w : want) {
        if (w < 0) continue;
        std::vector<const omk::PedClip*> m;
        for (const auto& c : it->second) if (c.type == w) m.push_back(&c);
        if (m.empty()) continue;
        // the roll: a cheap LCG on (group, action), so two characters of
        // one type asking for one action can still draw different clips
        std::uint32_t r = static_cast<std::uint32_t>(group * 2654435761u
                                                     + action * 40503u + w);
        r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
        return m[r % m.size()];
    }
    return &it->second.front();          // "anim non existante dans le .ANI"
}

// `List_PickRandomByType(list, type)` for a DEATH clip (`sub_4240E0`): a
// clip of that TYPE, and type 5 when the group has none. The engine's pick
// is `rand()`; this one is a fixed function of (group, type), labelled.
const omk::PedClip * PlayState::shootClipOfType(int group, int type) {
    if (pedAni.empty()) return nullptr;
    auto it = shootClips.find(group);
    if (it == shootClips.end())
        it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
    for (int t : {type, 5}) {
        std::vector<const omk::PedClip*> m;
        for (const auto& c : it->second) if (c.type == t) m.push_back(&c);
        if (!m.empty())
            return m[static_cast<std::size_t>(group * 7 + t) % m.size()];
    }
    return nullptr;
}

// ...and `List_PickRandomByType` as the brain's LABEL_177 calls it: a clip
// of EXACTLY that type or none - no type-5 fallback, since the null return
// is what sends the arm to its fallback turn rate. The same fixed pick.
const omk::PedClip * PlayState::shootClipExact(int group, int type) {
    if (pedAni.empty()) return nullptr;
    auto it = shootClips.find(group);
    if (it == shootClips.end())
        it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
    std::vector<const omk::PedClip*> m;
    for (const auto& c : it->second) if (c.type == type) m.push_back(&c);
    return m.empty() ? nullptr : m[static_cast<std::size_t>(group * 7 + type) % m.size()];
}

// ...and `sub_434630(list, slot)`: the clip whose +4 SLOT is that, or none
// - how `sub_4272B0` case 9 finds the attack at +108
const omk::PedClip * PlayState::shootClipBySlot(int group, int slot) {
    if (pedAni.empty() || !slot) return nullptr;
    auto it = shootClips.find(group);
    if (it == shootClips.end())
        it = shootClips.emplace(group, omk::animGroupClips(pedAni, group)).first;
    for (const auto& c : it->second) if (c.slot == slot) return &c;
    return nullptr;
}

const omk::NodeTracks * PlayState::pedTracksFor(int sex, const omk::PedClip& c, const std::vector<omk::Mesh>& meshes) {
    // `PlayerController::poseTracks`'s recipe over the crowd library: the
    // descriptor's tracks resolve to meshes by name, key 0 is the rest
    // sentinel so frame f reads key f + 1
    const auto key = std::make_pair(sex, c.slot);
    auto it = pedTracks.find(key);
    if (it != pedTracks.end()) return it->second.valid() ? &it->second : nullptr;
    omk::NodeTracks t;
    const auto d = omk::animDescriptor(pedAni, c.descriptor);
    if (d && d->frames > 0 && !meshes.empty()) {
        const auto lower = [](std::string v) {
            for (auto& ch : v) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            return v;
        };
        t.count = static_cast<int>(d->tracks.size());
        t.frames = d->frames;
        t.rootTrack = -1;
        // THE BONE NAMES CARRY A TWO-LETTER SKELETON PREFIX and the library
        // does not share it with every model: the men's clips say
        // `PhBassin`, the women's idle `ShBassin`, Jaunpur's men are
        // `KhBassin` and their women `FhBassin`. Matched by the whole
        // name, a woman idled and every Jaunpur man walked in a T-pose
        // (a reader's frame, 2026-09-03). The bone is the name after the
        // prefix, resolved inside the FIRST skeleton - the exact name is
        // tried first, for the one model whose prefix does agree.
        // THE BONE IS THE NAME AFTER ITS PREFIX, AND THE PREFIX IS NOT
        // TWO LETTERS - it is whatever the library and the model each
        // chose. `braqueur.ani`'s tracks are `UBassin`, `UCuissed`,
        // `UPiedd`; VIR_FN's meshes are `ViBassin`, `ViCuissed`,
        // `ViPiedd`. Stripping a fixed 2 from both gives "assin" against
        // "bassin" and NOT ONE of the nineteen tracks bound, which is why
        // the Shooting gallery's gunmen stood in their rest pose
        // (`todo/omk-play.md` 96). The crowd's four libraries all happen
        // to use two-letter prefixes (`Ph`, `Sh`, `Kh`, `Fh`), so the
        // fixed strip was right everywhere it had been looked at.
        //
        // The ENGINE does not strip at all: `o3de_FindMeshByName`
        // (0x00436D90) is `o3de_Traverse` running a `strstr` over the
        // node names and keeping the LAST match, and its callers pass the
        // BARE bone - `Bassin`, `Tete`, `Buste`, `Cuisseg`, `Piedd`
        // (04_sys.c 5497-5513, seventeen of them in a row). So the bone
        // name is a substring and the prefix's length never enters into
        // it. The same `strstr`-on-the-last-match is what finds the
        // shadow bones (`docs/ASSETS.md`).
        //
        // Reproduced here without hard-coding the seventeen: every prefix
        // in the corpus is a capitalised letter followed by lower case,
        // and every bone starts with a capital, so THE BONE BEGINS AT THE
        // SECOND UPPERCASE LETTER. `verify.py: bone names` measures that
        // over every shipped library and character model rather than
        // taking it on trust.
        const auto suffix = [&](const std::string& n) {
            for (std::size_t i = 1; i < n.size(); ++i)
                if (n[i] >= 'A' && n[i] <= 'Z') return lower(n.substr(i));
            return n.size() > 2 ? lower(n.substr(2)) : lower(n);
        };
        int firstRoot = -1;
        for (std::size_t j = 0; j < meshes.size() && firstRoot < 0; ++j) {
            bool hasParent = false;
            for (const auto& p : meshes) if (p.id == meshes[j].parent) { hasParent = true; break; }
            if (!hasParent) firstRoot = static_cast<int>(j);
        }
        const auto underFirst = [&](std::size_t j) {
            int m = static_cast<int>(j);
            for (int guard = 0; guard < 64 && m >= 0; ++guard) {
                if (m == firstRoot) return true;
                const std::int32_t pid = meshes[static_cast<std::size_t>(m)].parent;
                int next = -1;
                for (std::size_t q = 0; q < meshes.size(); ++q) if (meshes[q].id == pid) { next = static_cast<int>(q); break; }
                m = next;
            }
            return false;
        };
        for (const auto& tr : d->tracks) {
            std::int32_t mi = -1;
            const std::string want = lower(tr.name);
            for (const auto& m : meshes) if (lower(m.name) == want) { mi = m.index; break; }
            if (mi < 0) {
                const std::string bone = suffix(tr.name);
                for (std::size_t j = 0; j < meshes.size(); ++j)
                    if (suffix(meshes[j].name) == bone && underFirst(j)) { mi = meshes[j].index; break; }
            }
            t.ids.push_back(mi);
        }
        t.quats.assign(static_cast<std::size_t>(d->frames), {});
        t.trans.assign(static_cast<std::size_t>(d->frames), {0.0f, 0.0f, 0.0f});
        for (int f = 0; f < d->frames; ++f) {
            auto& row = t.quats[static_cast<std::size_t>(f)];
            row.resize(d->tracks.size());
            for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                const omk::AnimTrack& tr = d->tracks[i];
                if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                int k = f + 1;
                if (k >= tr.rotKeys) k = tr.rotKeys - 1;
                const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(k);
                if (o + 16 > pedAni.size()) continue;
                // // little-endian floats, read as such (PORTING A9): a memcpy here reversed
                // every quaternion on PowerPC - the crowd drew as shards (2026-10-03)
                const std::byte* src = pedAni.data() + o;
                row[i] = {omk::loadLE<float>(src), omk::loadLE<float>(src + 4),
                          omk::loadLE<float>(src + 8), omk::loadLE<float>(src + 12)};
            }
        }
    }
    it = pedTracks.emplace(key, std::move(t)).first;
    return it->second.valid() ? &it->second : nullptr;
}

// THE PLAYER'S SHOT (`actor/shootfire.h`, todo/shoot-mode.md 7h): his own
// shoot record - the engine keeps one for him among the 100, and the
// gate's three numbers live on it - the two one-shot globals, and the
// projectile pool the request fills.
// the arm's aim angles, `dword_6579A0/A4` (`actor/shootaim.h`)
// the first-person MOVER's block at `dword_6579B0` (`actor/shootmove.h`),
// and the run it is on, for the log: frames and distance since it started
// THE SHOOT HUD (`todo/shoot-mode.md` 8.3): screen 34's panel composed over
// the frame while the mode runs - a walk of its own, so it takes no input -
// the runtime texts its items' native callbacks produce, and
// `dword_90E11C`, the ammo counter `Shoot_InitWeapon` and the shot write.
// THE NOISE (`sub_4246E0`, `actor/shoot.h`): at a shot's muzzle, and where
// a bolt stops on the world or on a body. Every gunman with a brain is a
// record, tested in actor order (the engine's is slot order). His FLOOR
// is his record's `+188`, as the engine reads it - 0 for a gunman, the
// zeroed record's, since `Shoot_Think` (its writer) is not wired; this read
// `sub_435020` of where he stood at first, which found -1 for the two
// supermarket gunmen standing off the grid and called 22 of a reader's
// noises "another floor" on a one-floor map. Two things stand in,
// labelled: a record the port marks dead (`+160 & 8`) is
// passed over, for the death arm's `& ~0x40` the port does not run; and
// `Shoot_ActorAction` is RECORDED on the Session, as the hit's is - its own
// arms are not ported, and an action of -1 (whose arm is case 0) is not
// recorded at all.
// THE FREEZE (todo/drift-audit.md S7). Every writer of bit 0x8000 touches
// all 100 records at once - `shoot.freeze_all` / `.unfreeze_all` and the four
// wakes - and `sub_422540` gives a record made under the flag the bit too, so
// a live record's bit always equals `dword_4E9760`. The flag is the
// Session's (the ops write it); the records are here.
void PlayState::shootFreezeSync(long frame) {
    auto& session = *session_;
    const bool fz = session.shootMode().frozen();
    if (fz == shootFrozenApplied) return;
    shootFrozenApplied = fz;
    for (auto& [id, r] : shootBrains) {
        (void)id;
        if (fz) r.flags |= 0x8000u; else r.flags &= ~0x8000u;
    }
    if (fz) playerShootRec.flags |= 0x8000u; else playerShootRec.flags &= ~0x8000u;
    std::printf("frame %ld: shoot.%s - bit 0x8000 %s on %zu gunmen's records\n", frame,
                fz ? "freeze_all" : "unfreeze_all", fz ? "set" : "cleared", shootBrains.size());
}

void PlayState::shootInitWeapon(int& obj, int& kind, int& type) {
    auto& session = *session_;
    obj = session.shootMode().weaponObject();
    const auto& objs = voiceLib.objects();
    const bool known = obj >= 0 && static_cast<std::size_t>(obj) < objs.size();
    kind = known ? objs[static_cast<std::size_t>(obj)].kind : -1;
    // The -2 exception tests the MODEL NAME on the held node, and
    // that is `Scene_Load3DO`'s own copy of its PATH at descriptor
    // +48: `Object_Load` builds "MESHES\OBJETS\%s" around
    // `Object_ModelPath(stem)`, which appends ".3DO" (the five
    // bytes at 0x4C0D1C). So the character 11 from the end is the
    // first of a SEVEN-letter stem - and `BATPOUV`, the Baton de
    // pouvoir, is the one weapon it catches: type -2, its own row.
    type = omk::shootWeaponType(
        kind, known ? "MESHES\\OBJETS\\" + objs[static_cast<std::size_t>(obj)].stem + ".3DO"
                    : std::string());
    playerShootRec.weapon = shootWeapons.find(type, true);
    // `dword_90E11C`, as `Shoot_InitWeapon` leaves it (0x004220A8 /
    // 0x004220F6 / 0x00422107): the magazine's count - property
    // 35, slot `index - 1` - when the row has one, else -1
    hudAmmo = -1;
    if (const omk::ShootWeaponRow* row = playerShootRec.weapon) {
        std::int32_t cnt = 0;
        if (row->ammoIndex &&
            omk::readAmmoSlot(state.raw().subspan(
                                  static_cast<std::size_t>(omk::GameState::kPlayerRecord),
                                  static_cast<std::size_t>(omk::GameState::kPlayerRecordSize)),
                              row->ammoIndex - 1, cnt))
            hudAmmo = static_cast<int>(cnt);
    }
}

// `if (dword_4E9760 && g_ShootRecords) { every +160 &= ~0x8000; dword_4E9760 = 0; }`
// - the head of `sub_4246E0`, `sub_4240E0`, `sub_423B10` and `sub_424470`.
// `sub_47FCF0`, Astaroth's world-hit callback (`actor/astaroth.h`), as
// `Projectiles_Tick` calls it at 0x44DDDF: `mesh` is the set mesh the bolt's
// world ray met in world slot `slot`. The souls all lie in one set, so a mesh
// of another set is none of them - but the call is still made, since its
// `dword_657AFC == 6` test runs on every call.
void PlayState::astarothWorldHit(long frame, int mesh, int slot, const float at[3]) {
    auto& session = *session_;
    int soulSlot = -1;
    for (const int k : astarothSoulSlot)
        if (k >= 0) { soulSlot = k; break; }
    const int m = slot >= 0 && slot == soulSlot ? mesh : -1;
    const omk::SoulHit h = omk::astarothSoulHit(astaroth, m);
    if (h.soul >= 0)
        std::printf("frame %ld: ASTAROTH SOUL %d (%s, mesh %d) struck at %.0f %.0f %.0f - "
                    "%d hit%s left (sub_47FCF0)\n", frame, h.soul,
                    omk::kAstarothSoulNames[h.soul], mesh, double(at[0]), double(at[1]),
                    double(at[2]), h.hitsLeft, h.hitsLeft == 1 ? "" : "s");
    if (h.destroyed && slot >= 0 && static_cast<std::size_t>(slot) < worldSlots.size()) {
        // `sub_436F20`: mesh flag 2 over the node's SUBTREE - the drawable
        // mask then skips it, and nothing else reads it
        WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
        if (w.meshHidden.size() != w.meshes.size()) w.meshHidden.assign(w.meshes.size(), 0);
        int hidden = 0;
        if (mesh >= 0 && static_cast<std::size_t>(mesh) < w.meshes.size()) {
            w.meshHidden[static_cast<std::size_t>(mesh)] = 1;
            ++hidden;
            for (bool grew = true; grew;) {
                grew = false;
                for (std::size_t i = 0; i < w.meshes.size(); ++i) {
                    const int p = w.meshes[i].parent;
                    if (!w.meshHidden[i] && p >= 0 &&
                        static_cast<std::size_t>(p) < w.meshes.size() &&
                        w.meshHidden[static_cast<std::size_t>(p)]) {
                        w.meshHidden[i] = 1;
                        ++hidden;
                        grew = true;
                    }
                }
            }
        }
        // `Game_RaiseEvent(43, {27 + i, i})` - the sender the raw index
        const bool ran = session.postMessage(h.message, h.soul);
        std::printf("frame %ld: ASTAROTH SOUL %d DOWN - %d mesh%s hidden (sub_436F20), "
                    "%d of %d down, message %d from %d: %s\n", frame, h.soul, hidden,
                    hidden == 1 ? "" : "es", astaroth.destroyed, omk::kAstarothSouls,
                    h.message, h.soul, ran ? "handled" : "unsubscribed");
    }
    if (h.disarmed)
        std::printf("frame %ld: ASTAROTH - all six souls down: the world-hit callback "
                    "cleared (sub_44CD90(0))\n", frame);
}

// ---- ASTAROTH's TICK, its world (`actor/astaroth.h`, `todo/astaroth.md` 3)
omk::AstarothWorld PlayState::astarothWorld(Staged& s, omk::ShootRecord& rec, float dt) {
    omk::AstarothWorld w;
    const int grp = static_cast<int>(rec.type);
    w.clipFrames = [this, grp](int id) {
        const omk::PedClip* c = shootClipBySlot(grp, id);
        return c ? c->frames : -1;
    };
    w.rootDelta = [this, grp](int id, float t0, float t1, float out[3]) {
        out[0] = out[1] = out[2] = 0.0f;
        if (const omk::PedClip* c = shootClipBySlot(grp, id))
            if (c->root.size() >= 3) omk::pedRootDelta(*c, t0, t1, nullptr, out);
    };
    // `Actor_GetPosAndFacing(rec+96)` - the player's node
    w.player = [this](float out[3]) {
        for (int k = 0; k < 3; ++k) out[k] = player ? float(player->pos()[k]) : 0.0f;
    };
    // `**(actor+24)` - `Epauleg`, the LAST strstr match as `o3de_FindMeshByName`
    // keeps it - where he was drawn last frame (the engine's node matrices
    // are last pose's too until `sub_440C80` composes them)
    w.shoulder = [&s](float out[3]) {
        for (int k = 0; k < 3; ++k) out[k] = s.drawAt[k];
        if (!s.mo) return;
        int sh = -1;
        for (std::size_t i = 0; i < s.mo->meshes.size(); ++i)
            if (std::strstr(s.mo->meshes[i].name, "Epauleg")) sh = static_cast<int>(i);
        if (sh >= 0 && s.meshAt.size() >= static_cast<std::size_t>(sh) * 3 + 3)
            for (int k = 0; k < 3; ++k)
                out[k] = s.meshAt[static_cast<std::size_t>(sh) * 3 + static_cast<std::size_t>(k)];
    };
    w.fire = [this, &s, &rec](int slot, const float target[3]) {
        astarothFire(s, rec, slot, target);
    };
    w.slot1Wait = [this](float wait) { astarothSlot1Wait = wait; };
    // `sub_421CD0(him, rec, 1)` - the path field's heading, then `sub_421C00`
    w.fieldTurn = [this, &s, &rec, dt](float& facing, int& clipType, float& perFrame) {
        if (!shootMap.valid()) return false;
        auto crt = [this]() {
            gunRandSeed = gunRandSeed * 214013u + 2531011u;
            return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
        };
        const int h = shootField.heading(shootMap, static_cast<signed char>(rec.node & 0xFF),
                                         rec.destX, rec.destZ, crt);
        omk::ShootStep st;
        if (!omk::shootGridTurn(rec, h, facing, dt, crt, st)) return false;
        if (st.turnClip < 0) return false;
        const omk::PedClip* tc = shootClipExact(static_cast<int>(rec.type), st.turnClip);
        if (tc && tc->frames > 0) {
            clipType = st.turnClip;
            perFrame = st.turnTotal / static_cast<float>(tc->frames);
            return true;
        }
        facing += st.turnRate * dt;              // `sub_421C00`: no clip
        if (facing < 0.0f) facing += 360.0f;
        if (facing >= 360.0f) facing -= 360.0f;
        (void)s;
        return false;
    };
    w.aimed = [this, &s, &rec](const float pos[3], const float target[3]) {
        const float self[4] = {pos[0], pos[1], pos[2], s.facing};
        const bool in = omk::shootAcquires(rec, self, target, astarothAcquire, false);
        return in && !(double(astarothAcquire.dotFlat) <
                       std::sqrt(double(astarothAcquire.dist2d2)) * double(0.80000001f));
    };
    w.turnToTarget = [this, dt](float& facing) {
        omk::shootTurnToward(facing, astarothAcquire, false, dt);
    };
    // the SLAM: `sub_423B10(target, dmg, dir)`, the player's arm
    w.strike = [this, &s](int dmg, const float dir[3]) {
        strikePlayer(s, dmg, dir, "ASTAROTH's SLAM", "slam");
    };
    // state 18: `sub_435020` / `sub_435770` / `sub_4353E0` on the player,
    // and `sub_4359A0(+188, his cell, the player's, 1)`
    w.leapCheck = [this, &rec](bool& sight, bool& refused) {
        sight = false;
        refused = false;
        if (!shootMap.valid() || !player) return;
        const float* p = player->pos();
        const int fl = shootMap.floorAt(float(p[0]), float(p[1]), float(p[2]), -1);
        int cx = 0, cz = 0;
        // (LABELLED: with no floor the engine passes its own actor pointer
        // as the cell - garbage; this asks for cell 0,0 on floor -1)
        if (fl >= 0) shootMap.cellAt(fl, float(p[0]), float(p[2]), cx, cz);
        refused = shootMap.blocked(fl, cx, cz);
        const int own = static_cast<signed char>(rec.node & 0xFF);
        // (every door open, as the generic sight - LABELLED there)
        sight = own >= 0 && shootMap.lineOfSight(own, rec.destX, rec.destZ,
                                                 playerShootRec.destX, playerShootRec.destZ,
                                                 0xFFFF, nullptr, nullptr);
    };
    w.cell = [this, &rec](const float pos[3]) {
        const int fl = static_cast<signed char>(rec.node & 0xFF);
        int cx = 0, cz = 0;
        if (fl >= 0 && shootMap.valid() && shootMap.cellAt(fl, pos[0], pos[2], cx, cz)) {
            rec.destX = cx;
            rec.destZ = cz;
        }
    };
    w.stamp = [this, &rec]() { astarothStamp(rec); };
    // `Camera_SetShake(dword_9307E4, 30.0, 10)` - his footstep
    w.shake = [this](float duration, int amp) {
        ++astarothShakes;
        session_->cameraShake(duration, amp);
    };
    return w;
}

// A staged body as `sub_45E9C0` and `sub_45BC50` see it (`actor/shoothit.h`):
// each mesh at its `meshAt` in its `meshRot` as DRAWN last frame, bounded by
// its own record's +76 centre, +88 radius and +92/+104 box. Every drawn actor
// - the engine's list is every ATTACHED one (`Actor_Attach` -> `sub_45DFF0`).
// `drawnNow` false: the meshes as posed last frame whatever this frame's
// `drawn` says - for a caller inside the body's own turn, where `drawn` has
// been cleared and not yet set again (Gandhar's brain asking for a touch)
bool PlayState::hitBodyOf(const Staged& s, omk::HitBody& hb, bool drawnNow) const {
    if (s.actor < 0 || !s.mo || (drawnNow && !s.drawn)) return false;
    const std::size_t nm = s.mo->meshes.size();
    if (s.meshAt.size() != nm * 3 || s.meshRot.size() != nm * 9) return false;
    hb = omk::HitBody{};
    hb.actor = s.actor;
    hb.root = s.mo->root;
    hb.meshes.resize(nm);
    for (std::size_t mi = 0; mi < nm; ++mi) {
        const omk::Mesh& me = s.mo->meshes[mi];
        omk::HitMesh& hm = hb.meshes[mi];
        for (int k = 0; k < 3; ++k) {
            hm.pos[k] = s.meshAt[mi * 3 + static_cast<std::size_t>(k)];
            hm.centre[k] = me.centre[k];
            hm.boxMin[k] = me.boxMin[k];
            hm.boxMax[k] = me.boxMax[k];
        }
        for (int k = 0; k < 9; ++k)
            hm.m[k] = s.meshRot[mi * 9 + static_cast<std::size_t>(k)];
        hm.radius = me.radius;
    }
    return true;
}

// THE PLAYER'S BODY, actor -1: his meshes as drawn last frame - the
// first-person frame draws only a few of them, and all of them are posed
bool PlayState::playerHitBody(omk::HitBody& hb) const {
    if (!player || !playerMeshAtKnown ||
        playerMeshAt.size() != playerMeshes.size() * 3 ||
        playerMeshRot.size() != playerMeshes.size() * 9)
        return false;
    hb = omk::HitBody{};
    hb.actor = -1;
    for (std::size_t i = 0; i < playerMeshes.size(); ++i)
        if (playerMeshes[i].parent < 0) { hb.root = static_cast<int>(i); break; }
    hb.meshes.resize(playerMeshes.size());
    for (std::size_t mi = 0; mi < playerMeshes.size(); ++mi) {
        const omk::Mesh& me = playerMeshes[mi];
        omk::HitMesh& hm = hb.meshes[mi];
        for (int k = 0; k < 3; ++k) {
            hm.pos[k] = playerMeshAt[mi * 3 + static_cast<std::size_t>(k)];
            hm.centre[k] = me.centre[k];
            hm.boxMin[k] = me.boxMin[k];
            hm.boxMax[k] = me.boxMax[k];
        }
        for (int k = 0; k < 9; ++k)
            hm.m[k] = playerMeshRot[mi * 9 + static_cast<std::size_t>(k)];
        hm.radius = me.radius;
    }
    return true;
}

// `sub_423B10(player, dmg, dir)` - the DIRECT DAMAGE a boss deals the player
// (Astaroth's slam, Gandhar's strike): the shield, the difficulty, the hurt
void PlayState::strikePlayer(Staged& s, int dmg, const float dir[3], const char* who,
                             const char* kind) {
    if (!player) return;
    const std::size_t recAt = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
    const std::size_t recLen = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
    omk::StrikeIn sin;
    sin.damage = dmg;
    sin.victimIsPlayer = true;
    sin.victimInShoot = player->state() == omk::ActorState::Shoot;
    std::int32_t shield = 0;
    omk::readActorProperty(state.raw().subspan(recAt, recLen), 17, shield);
    sin.bodyShield = shield;
    sin.difficulty = settings.v.shootDifficulty;
    sin.victimYaw = player->facing();
    sin.dir[0] = dir[0];
    sin.dir[2] = dir[2];
    shootWake(n, "a boss's direct damage (sub_423B10)");
    const omk::HitOut sho = omk::shootApplyStrike(playerShootRec, sin);
    std::printf("frame %ld: actor %d %s - %s (sub_423B10): damage %d at %.0f away\n", n,
                s.actor, s.model.c_str(), who, dmg,
                std::sqrt(double(dir[0]) * dir[0] + double(dir[2]) * dir[2]));
    if (sho.refused)
        std::printf("  PLAYER strike REFUSED (`sub_423B10` returns -1)\n");
    else if (sho.healthWas <= 0)
        std::printf("  the player is down already (health %d): nothing\n", sho.healthWas);
    else
        applyPlayerDamage(n, s.actor, kind, dmg, int(shield), sho);
}

// GANDHAR's world (`actor/gandhar.h`, `todo/gandhar.md`): the clips, the
// sight, the turn, the attack pick, the posts, his step's wall test, his fire,
// and the TOUCH his grab and strike ask.
omk::GandharWorld PlayState::gandharWorld(Staged& s, omk::ShootRecord& rec, float dt) {
    omk::GandharWorld w;
    const int grp = static_cast<int>(rec.type);
    auto crt = [this]() {
        gunRandSeed = gunRandSeed * 214013u + 2531011u;
        return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
    };
    w.rnd = crt;
    // `List_PickRandomByType(+20, type)`: `rand() % count` over the clips of
    // that type, in list order
    w.pickType = [this, grp, crt](int type) {
        if (pedAni.empty() || grp < 0 || grp >= 64) return -1;
        std::vector<int> slots;
        for (const auto& c : omk::animGroupClips(pedAni, grp))
            if (c.type == type) slots.push_back(c.slot);
        if (slots.empty()) return -1;
        return slots[static_cast<std::size_t>(crt() % static_cast<int>(slots.size()))];
    };
    // `sub_434630(+20, id)`: the clip whose id is `id`
    w.pickId = [this, grp](int id) {
        return shootClipBySlot(grp, id) ? id : -1;
    };
    w.clipFrames = [this, grp](int slot) {
        const omk::PedClip* c = shootClipBySlot(grp, slot);
        return c ? c->frames : 0;
    };
    // `Anim_SetFrame`'s root, turned into the world by his facing
    w.rootDelta = [this, grp, &s](int slot, float t0, float t1, float out[3]) {
        out[0] = out[1] = out[2] = 0.0f;
        const omk::PedClip* c = shootClipBySlot(grp, slot);
        if (!c || c->root.size() < 3) return;
        float d[3] = {0, 0, 0};
        omk::pedRootDelta(*c, t0, t1, nullptr, d);
        omk::rotateYaw(s.facing, d, out);
    };
    // `sub_420C70(rec, his node, the player)` - and what it leaves behind
    w.sight = [this, &s, &rec]() {
        const auto it = gandharActors.find(s.actor);
        if (it == gandharActors.end() || !player) return false;
        const float self[4] = {it->second.node[0], it->second.node[1], it->second.node[2],
                               s.facing};
        const float target[3] = {float(player->pos()[0]), float(player->pos()[1]),
                                 float(player->pos()[2])};
        return omk::shootAcquires(rec, self, target, gandharAcquire, false);
    };
    w.turn = [this, dt](float& facing) {
        omk::shootTurnToward(facing, gandharAcquire, false, dt);
    };
    // his STEP's three questions (`sub_47E5F0`): the wall test of one step from
    // his record point on his floor (`sub_421140`), the nearest standable
    // point on floor 1 (`sub_4368E0`), the forward axis the cone test left
    w.wall = [this, &s, &rec](float dx, float dz) {
        const auto it = gandharActors.find(s.actor);
        if (it == gandharActors.end() || !shootMap.valid()) return 0;
        float snap[2];
        return omk::shootWallTest(rec, shootMap, it->second.pos[0], it->second.pos[2],
                                  dx, dz, 1, snap);
    };
    w.standable = [this](float& x, float& z) {
        if (!shootMap.valid()) return false;
        return shootMap.snapToStandable(1, x, 0.0f, z);
    };
    // `sub_44CDF0(him, slot, the player)`: his `Tire` marker's weapon slot -
    // slot 1 from action 23, slot 0 from 24
    w.fire = [this, &s, &rec](int slot) {
        if (!player) return false;
        const float target[3] = {float(player->pos()[0]), float(player->pos()[1]),
                                 float(player->pos()[2])};
        return recordFire(s, rec, slot, target, "GANDHAR", -1.0f);
    };
    w.forward = [this](float& fx, float& fz) {
        fx = gandharAcquire.fwdX;
        fz = gandharAcquire.fwdZ;
    };
    // `sub_421020`: the attack whose property-21 range still reaches
    w.pickAttack = [this, &s, &rec, crt]() {
        auto& session = *session_;
        return omk::shootPickAttack(
            rec, gandharAcquire.dist2d2,
            [&](int slot) {
                std::int32_t rm = 2, dm = 1;
                session.actorAttack(s.actor, slot, rm, dm);
                return static_cast<int>(rm);
            },
            crt);
    };
    // event 43 {message, sender}: Gandhar for his death, the player for a grab
    w.post = [this, &s](int message, bool fromPlayer) {
        auto& session = *session_;
        const int sender = fromPlayer ? session.playerActor() : s.actor;
        const bool ran = session.postMessage(message, sender);
        std::printf("frame %ld: actor %d %s - GANDHAR posts message %d from %d (%s)\n", n,
                    s.actor, s.model.c_str(), message, sender, ran ? "handled" : "unsubscribed");
    };
    w.say = [this, &s](const std::string& line) {
        std::printf("frame %ld: actor %d %s - GANDHAR %s\n", n, s.actor, s.model.c_str(),
                    line.c_str());
    };
    // `sub_440C80(player) ; sub_45BC50(his node, the player's)`: his root
    // mesh's sphere against the player's node boxes, both as DRAWN last frame
    w.touch = [this, &s]() {
        omk::HitBody him, them;
        const bool hb1 = hitBodyOf(s, him, false), hb2 = playerHitBody(them);
        if (!hb1 || !hb2) {
            std::printf("frame %ld: actor %d %s - GANDHAR's TOUCH (sub_45BC50) not tested: %s%s\n",
                        n, s.actor, s.model.c_str(), hb1 ? "" : "his body is not drawn ",
                        hb2 ? "" : "the player's body is not posed");
            return false;
        }
        const int m = omk::shootBodyTouch(him, them);
        // ...and the geometry either way, which is what a miss needs
        const omk::HitMesh& r = him.meshes[static_cast<std::size_t>(him.root)];
        const float c[3] = {r.pos[0] + r.m[0] * r.centre[0] + r.m[3] * r.centre[1] + r.m[6] * r.centre[2],
                            r.pos[1] + r.m[1] * r.centre[0] + r.m[4] * r.centre[1] + r.m[7] * r.centre[2],
                            r.pos[2] + r.m[2] * r.centre[0] + r.m[5] * r.centre[1] + r.m[8] * r.centre[2]};
        double best = 1e30;
        for (const auto& hm : them.meshes) {
            const double dx = hm.pos[0] - c[0], dy = hm.pos[1] - c[1], dz = hm.pos[2] - c[2];
            best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        std::printf("frame %ld: actor %d %s - GANDHAR's TOUCH (sub_45BC50) %s: his root sphere at "
                    "%.0f %.0f %.0f radius %.0f, the player's nearest node %.0f away%s%s\n", n,
                    s.actor, s.model.c_str(), m >= 0 ? "MEETS the player" : "misses",
                    double(c[0]), double(c[1]), double(c[2]), double(r.radius), best,
                    m >= 0 ? " - his " : "", m >= 0 ? playerMeshes[static_cast<std::size_t>(m)].name : "");
        return m >= 0;
    };
    // the grab's message: his node against his floor's box
    w.side = [this, &s, &rec]() {
        const auto it = gandharActors.find(s.actor);
        const int fl = static_cast<signed char>(rec.node & 0xFF);
        if (it == gandharActors.end() || !shootMap.valid() || fl < 0 ||
            fl >= static_cast<int>(shootMap.floors().size()))
            return -1;
        return omk::gandharGrabSide(it->second.node[0], it->second.node[2],
                                    shootMap.floors()[static_cast<std::size_t>(fl)].bound);
    };
    // event 44, property 22 of HIM - the struct's value starts at 11, which
    // stands when he has no such property
    w.strikeDamage = [this, &s]() {
        std::int32_t v = 11;
        if (!session_->actorProperty(s.actor, 22, v)) v = 11;
        return static_cast<int>(v);
    };
    // `sub_423B10(player, damage, (player - his node) in x / z)`
    w.strike = [this, &s](int dmg) {
        const auto it = gandharActors.find(s.actor);
        if (!player || it == gandharActors.end()) return;
        const float dir[3] = {float(player->pos()[0]) - it->second.node[0], 0.0f,
                              float(player->pos()[2]) - it->second.node[2]};
        strikePlayer(s, dmg, dir, "GANDHAR's STRIKE", "strike");
    };
    return w;
}

// `sub_4725B0` (0x004725B0): for every node, the key `(int)frame` at four
// offsets -
//     A = slerp(key + off8,  key + off10, wPitch)
//     B = slerp(key + off14, key + off12, wPitch)
//     q = slerp(A, B, wYaw)
// with `sub_4721F0`'s integer k/256 weight (`qslerpK`). This tree's tracks
// hold key k at index k - 1 (key 0 is the rest sentinel).
std::vector<omk::MeshPose> PlayState::astarothPoseNow(const CharModel* mo,
                                                      const omk::NodeTracks& pt,
                                                      const omk::AstarothActor& a) {
    if (!mo || !pt.valid() || pt.frames <= 0) return {};
    const int key = static_cast<int>(static_cast<std::int64_t>(a.frame));
    const auto at = [&](int off) {
        return std::clamp(off + key - 1, 0, pt.frames - 1);
    };
    const auto& g = a.blend;
    const auto& q8 = pt.quats[static_cast<std::size_t>(at(g.off8))];
    const auto& q10 = pt.quats[static_cast<std::size_t>(at(g.off10))];
    const auto& q12 = pt.quats[static_cast<std::size_t>(at(g.off12))];
    const auto& q14 = pt.quats[static_cast<std::size_t>(at(g.off14))];
    omk::NodeTracks one;
    one.count = pt.count;
    one.frames = 1;
    one.rootTrack = pt.rootTrack;
    one.ids = pt.ids;
    one.names = pt.names;
    one.quats.emplace_back(q8.size());
    auto& row = one.quats[0];
    const unsigned wp = static_cast<unsigned>(g.wPitch), wy = static_cast<unsigned>(g.wYaw);
    for (std::size_t i = 0; i < row.size() && i < q10.size() && i < q12.size() && i < q14.size();
         ++i) {
        const omk::Quatf A = omk::qslerpK(q8[i], q10[i], wp);
        const omk::Quatf B = omk::qslerpK(q14[i], q12[i], wp);
        row[i] = omk::qslerpK(A, B, wy);
    }
    if (!pt.trans.empty())
        one.trans.push_back(pt.trans[static_cast<std::size_t>(
            std::min<int>(at(g.off8), static_cast<int>(pt.trans.size()) - 1))]);
    return omk::composePose(mo->meshes, one, 0, false);
}

void PlayState::astarothStamp(omk::ShootRecord& rec) {
    // the generic stamp (playframe_world_staged.cpp, after the brain), on the
    // cell the tick left at +136/+140
    if (!shootMap.valid() || rec.state == 2 || rec.cellStamped) return;
    const int fl = static_cast<signed char>(rec.node & 0xFF);
    if (fl < 0 || fl >= static_cast<int>(shootMap.floors().size())) return;
    rec.cellSaved = shootMap.floors()[static_cast<std::size_t>(fl)].cell(rec.destX, rec.destZ);
    shootMap.setCell(fl, rec.destX, rec.destZ, omk::Map2d::kOccupied);
    rec.cellStamped = true;
}

// `sub_44CDF0(actor, slot, target)` (17_script.c 941, read from the assembly):
// his weapon slot is his own `Tire` marker (`astarothTireSlots`), the timer
// `actor+148+4*slot` is property 34's RELOAD (+202 - 0 for him), the ammo
// property 35 spent through `Actor_SetProperty` - whose slot is the value's
// HIGH word, so every shot spends SLOT 0's - and the bolt flies from the
// marker's drawn position straight at the target, no jitter, at
// `(int16)hi * 39 / 10` with damage `(int16)lo`.
// `sub_44CDF0` (0x0044CDF0), an actor's weapon slot fired from his `Tire`
// marker - Astaroth's and Gandhar's alike. `slot1Wait` >= 0 is Astaroth's
// `dword_657AF0` override of slot 1's muzzle wait. -> whether a shot left.
bool PlayState::recordFire(Staged& s, omk::ShootRecord& rec, int slot, const float target[3],
                           const char* who, float slot1Wait) {
    auto& session = *session_;
    (void)rec;
    if (!s.mo || slot < 0 || slot > 3) return false;
    int slots[4];
    omk::astarothTireSlots(s.mo->meshes, slots);
    const int w = slots[slot];
    if (w < 0) return false;                             // `if (!w) return 0`
    auto& timer = actorSlotTimer[s.actor][static_cast<std::size_t>(slot)];
    if (timer > 0.0f) return false;                      // the slot timer
    int reload = 0, speedHi = 0, damage = 0, ammo = 0;
    if (!session.actorWeaponSlot(s.actor, slot, reload, speedHi, damage, ammo)) return false;
    if (ammo < 0) return false;                          // -1 refuses; 0 still fires
    session.setActorProperty(s.actor, 35, ammo - 1);     // the HIGH word 0: slot 0's
    timer = static_cast<float>(static_cast<std::int16_t>(reload));
    omk::ShootWeaponRow row;
    row.speed = static_cast<float>(static_cast<std::int16_t>(speedHi) * 39 / 10);
    row.damage = static_cast<std::int16_t>(damage);
    row.ammoIndex = 0;                                   // spent above, as the engine does
    omk::RecordShot rs;
    if (s.meshAt.size() >= static_cast<std::size_t>(w) * 3 + 3)
        for (int k = 0; k < 3; ++k)
            rs.muzzle[k] = s.meshAt[static_cast<std::size_t>(w) * 3 + static_cast<std::size_t>(k)];
    else
        for (int k = 0; k < 3; ++k) rs.muzzle[k] = s.drawAt[k];
    const int k0[3] = {0, 0, 0};
    const omk::GunmanAim aim = omk::shootGunmanAim(target, rs.muzzle, 0.0f, k0);
    rs.yawDeg = aim.yawDeg;
    rs.pitchDeg = aim.pitchDeg;
    // the sprite row by the marker's PARENT bone (`sub_44EEB0(w->parent->desc
    // + 16)`, four bytes): AstMaing -> AstMain, AstBuste -> AstBust
    const omk::Mesh& wm = s.mo->meshes[static_cast<std::size_t>(w)];
    std::string parentName;
    for (const auto& m : s.mo->meshes)
        if (m.id == wm.parent) { parentName = m.name; break; }
    int muzzleFx = 0;
    std::string rowName = "none";
    if (const omk::FxShotSprite* sp = shootSfx.shotSprite(parentName)) {
        rowName = sp->name;
        muzzleFx = sp->muzzleEffect;
        rs.impactEffect = sp->impactEffect;
        rs.sprite = true;
        // slot 1's WAIT is the one his tick rewrites (`dword_657AF0`)
        rs.windUp = (slot == 1 && slot1Wait >= 0.0f) ? slot1Wait : sp->windUp;
        rs.grow = sp->grow;
        for (int k = 0; k < 3; ++k) rs.growStep[k] = sp->growStep[k];
    }
    const omk::RecordShotOut out = projectiles.fireFromRecord(s.actor, row, rs);
    std::printf("frame %ld: actor %d %s - %s FIRES slot %d (sub_44CDF0): %s from %.0f %.0f "
                "%.0f, speed %.1f, damage %d, wait %.1f, row '%s' -> '%s', ammo %d -> %d, entry %d\n",
                n, s.actor, s.model.c_str(), who, slot, wm.name, double(rs.muzzle[0]),
                double(rs.muzzle[1]), double(rs.muzzle[2]), double(row.speed), row.damage,
                double(rs.windUp), parentName.c_str(), rowName.c_str(), ammo, ammo - 1, out.entry);
    if (out.entry >= 0) {
        shotSound(n, muzzleFx, rs.muzzle, player ? player->pos() : nullptr, who);
        shootNoise(n, s.actor, rs.muzzle, who);
    }
    return out.entry >= 0;
}

void PlayState::astarothFire(Staged& s, omk::ShootRecord& rec, int slot, const float target[3]) {
    recordFire(s, rec, slot, target, "ASTAROTH", astarothSlot1Wait);
}

void PlayState::shootWake(long frame, const char* what) {
    auto& session = *session_;
    if (!session.shootMode().frozen() || !session.shootMode().active()) return;
    session.shootModeMutable().setFrozen(false);
    shootFrozenApplied = false;
    for (auto& [id, r] : shootBrains) { (void)id; r.flags &= ~0x8000u; }
    playerShootRec.flags &= ~0x8000u;
    std::printf("frame %ld: the freeze is WOKEN by %s - bit 0x8000 cleared on %zu gunmen's "
                "records\n", frame, what, shootBrains.size());
}

void PlayState::shootNoise(long frame, int from, const float at[3], const char* what) {
    auto& session = *session_;
    shootWake(frame, "a noise (sub_4246E0)");      // before anything, map or none
    if (!shootMap.valid()) return;
    const int nf = shootMap.floorAt(at[0], at[1], at[2], -1);
    // one line per noise, so a silence says WHY: how many records were
    // passed over for each of the tests, and the first one's hearing
    int tested = 0, dead = 0, alertedAlready = 0, unentered = 0, far = 0, heard = 0;
    int firstCells = -1;
    for (auto& [actor, rec] : shootBrains) {
        if (nf == -1) break;
        if (actor == from) continue;
        ++tested;
        // dead: flag 8 with no health - flag 8 alone is a clip playing
        if ((rec.flags & 8u) && rec.health <= 0) { ++dead; continue; }
        if (!(rec.flags & 0x40u)) { ++unentered; continue; }
        if (rec.flags & 0x20u) { ++alertedAlready; continue; }
        const Staged* sp = nullptr;
        for (const auto& up : staged)
            if (up && up->actor == actor) { sp = up.get(); break; }
        if (!sp) continue;
        std::int32_t cells = 0;
        session.actorProperty(actor, 28, cells);
        if (firstCells < 0) firstCells = static_cast<int>(cells);
        // his FLOOR is his record's `+188`, a signed byte - `movsx edx,
        // byte [esi+1Ch]`
        const int sf = static_cast<int>(static_cast<std::int8_t>(rec.node & 0xFF));
        const omk::NoiseHearing h = omk::shootHearNoise(
            rec, sp->drawAt, at, static_cast<int>(cells),
            static_cast<float>(shootMap.scale()), nf, sf);
        if (!h.heard) { ++far; continue; }
        ++heard;
        if (h.act && h.action >= 0)
            session.shootModeMutable().actorAction(actor, h.action);
        std::printf("frame %ld: NOISE (sub_4246E0) - %s at %.0f %.0f %.0f, floor %d: "
                    "actor %d %s (hearing %d cells of %u), his floor %d, action %d\n",
                    frame, what, double(at[0]), double(at[1]), double(at[2]), nf, actor,
                    h.alerted ? "ALERTED" : "heard it from another floor",
                    int(cells), shootMap.scale(), sf, h.act ? h.action : -99);
    }
    std::printf("frame %ld: NOISE (sub_4246E0) - %s at %.0f %.0f %.0f, floor %d: %d "
                "records - %d dead, %d not entered, %d already alerted, %d out of "
                "hearing (the first hears %d cells), %d heard\n", frame, what,
                double(at[0]), double(at[1]), double(at[2]), nf, tested, dead,
                unentered, alertedAlready, far, firstCells, heard);
}

// THE RAISE (`actor/shootaim.h`): the player's pose for this frame with the
// shoot aim layer over its upper body - every bone the table at 0x4C3798
// marks takes `S_AUTOLK`'s grid (group 202's default) at the aim angles,
// lowered toward the stance's key 1 (group 200's default) by the weapon's
// +176. Outside shoot mode it is the pose as it was. The arm, the gun and
// the muzzle all read it, so what fires is what is drawn.
std::vector<omk::MeshPose> PlayState::playerPoseNow(omk::PlayerController* pl, bool aimLayer,
                                   const omk::NodeTracks& pt, float frame) {
    if (!pl || !aimLayer || !pt.valid())
        return omk::composePose(playerMeshes, pt, frame, false);
    const omk::NodeTracks* aim = pl->clipTracks(pl->groupDefaultClip(202));
    const omk::NodeTracks* stance = pl->clipTracks(pl->groupDefaultClip(200));
    if (!aim || aim->frames < 15) return omk::composePose(playerMeshes, pt, frame, false);
    // the aim layer bends ONE key - the truncated one (enhancement 12
    // smooths the plain pose above, not this)
    const int fi = static_cast<int>(std::floor(frame));
    const int f = fi < 0 ? 0 : (fi >= pt.frames ? pt.frames - 1 : fi);
    omk::NodeTracks one;
    one.count = pt.count;
    one.frames = 1;
    one.rootTrack = pt.rootTrack;
    one.ids = pt.ids;
    one.names = pt.names;
    one.quats.push_back(pt.quats[static_cast<std::size_t>(f)]);
    if (!pt.trans.empty())
        one.trans.push_back(pt.trans[static_cast<std::size_t>(
            f < static_cast<int>(pt.trans.size()) ? f : static_cast<int>(pt.trans.size()) - 1)]);
    auto& row = one.quats[0];
    for (std::size_t i = 0; i < one.ids.size() && i < row.size(); ++i) {
        const std::int32_t mi = one.ids[i];
        if (mi < 0 || static_cast<std::size_t>(mi) >= playerMeshes.size()) continue;
        if (!omk::shootAimMarked(playerMeshes[static_cast<std::size_t>(mi)].slot)) continue;
        // the bone's `S_AUTOLK` keys 1..15: the tracks' frame k is key k + 1
        std::vector<omk::Quatf> keys;
        for (std::size_t j = 0; j < aim->ids.size(); ++j) {
            if (aim->ids[j] != mi) continue;
            for (int k = 0; k < 15; ++k)
                keys.push_back(aim->quats[static_cast<std::size_t>(k)][j]);
            break;
        }
        if (keys.size() < 15) continue;
        omk::Quatf q = omk::shootAimBone(keys, shootAim.yaw, shootAim.pitch);
        // the stance's key 1 for this bone - identity when the stance has
        // no track for it, as `sub_471950`'s `v72 = 1.0` is
        if (stance && !stance->quats.empty()) {
            omk::Quatf key1{};
            for (std::size_t j = 0; j < stance->ids.size(); ++j)
                if (stance->ids[j] == mi) { key1 = stance->quats[0][j]; break; }
            q = omk::shootAimLower(q, key1, playerShootRec.weaponLowered);
        }
        row[i] = q;
    }
    return omk::composePose(playerMeshes, one, 0, false);
}

// A GUNMAN'S AIM LAYER - the same `sub_471950` bend, on HIS pieces. The
// fire gate's target arm binds his group's type-18 clip (`sub_434590(+20,
// 18)`, the S_AUTOLK of his library: 15 frames in braqueur.ani) over the
// bones his `+84` table marks (0x4C3798 for every type but 13, the
// player's own table), with the type-17 clip as the stance the lowering
// blends to (`dword_6A472C`), and bends them by his aim angles and his
// `+176`. Only while the gate ran him this tick (`gunAims`).
std::vector<omk::MeshPose> PlayState::gunmanPoseNow(int actor, int deathType, const CharModel* mo,
                                   const omk::NodeTracks& pt, float frame) {
    auto& session = *session_;
    const auto aimIt = gunAims.find(actor);
    const auto recIt = shootBrains.find(actor);
    if (!mo || !pt.valid() || deathType >= 0 || aimIt == gunAims.end() ||
        !aimIt->second.live || recIt == shootBrains.end() ||
        session.typeOfActor(actor) == 13u)      // 0x4C37E8, not lifted
        return omk::composePose(mo->meshes, pt, frame, false);
    const int grp = static_cast<int>(session.typeOfActor(actor));
    const omk::PedClip* c18 = (grp >= 0 && grp < 64) ? shootClipExact(grp, 18) : nullptr;
    const omk::PedClip* c17 = (grp >= 0 && grp < 64) ? shootClipExact(grp, 17) : nullptr;
    const omk::NodeTracks* aim = c18 ? pedTracksFor(grp, *c18, mo->meshes) : nullptr;
    const omk::NodeTracks* stance = c17 ? pedTracksFor(grp, *c17, mo->meshes) : nullptr;
    if (!aim || aim->frames < 15) return omk::composePose(mo->meshes, pt, frame, false);
    // the aim layer bends ONE key - the truncated one (enhancement 12
    // smooths the plain pose above, not this)
    const int fi = static_cast<int>(std::floor(frame));
    const int f = fi < 0 ? 0 : (fi >= pt.frames ? pt.frames - 1 : fi);
    omk::NodeTracks one;
    one.count = pt.count;
    one.frames = 1;
    one.rootTrack = pt.rootTrack;
    one.ids = pt.ids;
    one.names = pt.names;
    one.quats.push_back(pt.quats[static_cast<std::size_t>(f)]);
    if (!pt.trans.empty())
        one.trans.push_back(pt.trans[static_cast<std::size_t>(
            f < static_cast<int>(pt.trans.size()) ? f : static_cast<int>(pt.trans.size()) - 1)]);
    auto& row = one.quats[0];
    for (std::size_t i = 0; i < one.ids.size() && i < row.size(); ++i) {
        const std::int32_t mi = one.ids[i];
        if (mi < 0 || static_cast<std::size_t>(mi) >= mo->meshes.size()) continue;
        if (!omk::shootAimMarked(mo->meshes[static_cast<std::size_t>(mi)].slot)) continue;
        std::vector<omk::Quatf> keys;
        for (std::size_t j = 0; j < aim->ids.size(); ++j) {
            if (aim->ids[j] != mi) continue;
            for (int k = 0; k < 15; ++k)
                keys.push_back(aim->quats[static_cast<std::size_t>(k)][j]);
            break;
        }
        if (keys.size() < 15) continue;
        omk::Quatf q = omk::shootAimBone(keys, aimIt->second.yaw, aimIt->second.pitch);
        if (stance && !stance->quats.empty()) {
            omk::Quatf key1{};
            for (std::size_t j = 0; j < stance->ids.size(); ++j)
                if (stance->ids[j] == mi) { key1 = stance->quats[0][j]; break; }
            q = omk::shootAimLower(q, key1, recIt->second.weaponLowered);
        }
        row[i] = q;
    }
    return omk::composePose(mo->meshes, one, 0, false);
}

// `scptdata\shoot2.sfx`, which `Shoot_Enter` loads for its section A -
// the SHOT SPRITES, looked up by the held gun's root mesh name - and the
// gun's own model facts a shot needs: that name, and where its `tir` node
// sits. `Object_Load` unlinks `tir` from the gun at load (the name is the
// string at 0x4C2E4C) and `Actor_TickProjectiles` clones it for each shot,
// linked under the HAND with its own +128 local - so the muzzle is the
// hand's pose applied to `tir`'s local, and the bolt IS that mesh.
// ...and `Game_Start("shoot2.scx")`: the mode's LIBRARY, loaded into
// `stru_930780` over `aventure.scx`, which is the scene `Sfx_TickAmbient`
// resolves a shot effect's SOUND id in (`Scene_FindSoundIndex`) when the
// emitter names no scene of its own - and the shot's never does.
// (struct SfxSample: `backends/sdl/playtypes.h`, todo/play-split.md)
// A mode's LIBRARY (`fight.scx`, `shoot2.scx`), loaded when the mode is
// entered and released when it ends - the engine's `Game_Start` swaps it into
// `stru_930780` and puts `aventure.scx` back (todo/ram-vs-original.md tier B).
// Its converted samples go with it: `sfxCache` is keyed by the file's own
// bytes, and a later load at the same address would find another file's
// sounds there. A voice still playing keeps its own samples (shared).
void PlayState::loadLibrary(std::unique_ptr<omk::ScxRuntime>& rt, const char* path) {
    const omk::DataFs fs(fr);
    if (const auto p = fs.resolve(path)) {
        rt = std::make_unique<omk::ScxRuntime>(omk::DataFs::readPath(*p));
        if (!rt->valid()) rt.reset();
    }
}

void PlayState::dropLibrary(std::unique_ptr<omk::ScxRuntime>& rt) {
    if (!rt) return;
    for (int i = 0; i < rt->wavCount(); ++i) {
        const auto w = rt->wavData(i);
        sfxCache.erase(std::make_pair(w.data(), w.size()));
    }
    rt.reset();
}

const SfxSample & PlayState::sfxPcm(std::span<const std::byte> wav) {
    const auto key = std::make_pair(wav.data(), wav.size());
    auto it = sfxCache.find(key);
    if (it == sfxCache.end()) {
        SfxSample sm;
        auto v = wavToDeviceSound(wav, kDeviceRate);
        if (!v) v = std::make_shared<const omk::DeviceSound>();   // empty, as before
        for (std::size_t i = 0; i < v->size; ++i) sm.peak = std::max(sm.peak, std::fabs(v->at(i)));
        sm.pcm = std::move(v);
        it = sfxCache.emplace(key, std::move(sm)).first;
    }
    return it->second;
}

void PlayState::shotSound(long frame, int effectId, const float at[3],
                               const float* listener, const char* what) {
    const omk::FxEffect* e = effectId ? shootSfx.byId(effectId) : nullptr;
    if (!e || e->sound == 0xFFFF || e->sound == -1 || !shootRt) return;
    const int w = shootRt->wavBydId(e->sound);
    if (w < 0) {
        std::printf("frame %ld: SHOT SOUND %s - effect %d's sound %d is not in shoot2.scx\n",
                    frame, what, effectId, e->sound);
        return;
    }
    float d = 0.0f;
    if (listener) {
        const float dx = at[0] - listener[0], dy = at[1] - listener[1],
                    dz = at[2] - listener[2];
        d = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const float gain = d <= 78.0f ? 1.0f : 78.0f / std::min(d, 584.0f);
    const SfxSample& sm = sfxPcm(shootRt->wavData(w));
    std::printf("frame %ld: SHOT SOUND %s - effect %d sound %d '%s', %.0f from him, "
                "gain %.2f\n", frame, what, effectId, e->sound,
                shootRt->wavName(w).c_str(), double(d), double(gain));
    if (sm.pcm->size) front.playSound(sm.pcm, false, gain * fxGain());
}

// (struct GunFacts: `backends/sdl/playtypes.h`, todo/play-split.md)
const GunFacts & PlayState::gunFactsFor(const std::string& stem) {
    const auto& fs = *fs_;
    auto it = gunFacts.find(stem);
    if (it != gunFacts.end()) return it->second;
    GunFacts g;
    if (const auto mo = fs.resolve("MESHES/OBJETS/" + stem + ".3DO")) {
        const auto md = omk::DataFs::readPath(*mo);
        if (const auto mh = omk::readHeader(md)) {
            const auto ms = omk::readMeshes(md, *mh);
            // the model's FIRST node is what `FindNodeByName(v8, 0)` gives
            // `Object_Load` as the slot's node, and `sub_44EEB0` reads the
            // shot sprite's name off it
            if (!ms.empty()) g.root = ms.front().name;
            for (std::size_t i = 0; i < ms.size(); ++i) {
                std::string nm = ms[i].name;
                for (auto& ch : nm) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (nm != "tir") continue;
                g.tirMesh = static_cast<int>(i);
                for (int k = 0; k < 3; ++k) {
                    g.tirLocal[k] = ms[i].local[k];
                    g.tirPos[k] = ms[i].pos[k];
                }
            }
            g.ok = g.tirMesh >= 0;
        }
    }
    return gunFacts.emplace(stem, g).first->second;
}

// One `.3DO`/`.3DT` per MODEL NAME, loaded once and shared by every actor
// wearing it.
CharModel * PlayState::charModelFor(const std::string& name) {
    const auto& fs = *fs_;
    if (name.empty()) return nullptr;
    auto it = charModels.find(name);
    if (it != charModels.end()) return &it->second;
    // EVERY LOAD SAYS WHAT IT COST, and whether it is a RELOAD - a model
    // this run already had and the per-frame eviction below let go of. A
    // transition's stall is a load inside a frame (todo/vita-port.md
    // 2026-09-23), and a reload is the one that need not happen at all.
    static std::map<std::string, int> loadsOf;
    const int nth = ++loadsOf[name];
    const auto loadT0 = std::chrono::steady_clock::now();
    struct Said {
        const std::string& n; int k; std::chrono::steady_clock::time_point t0;
        ~Said() {
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0).count();
            std::printf("model load: %s in %.1f ms%s\n", n.c_str(), ms,
                        k > 1 ? (" - a RELOAD, load " + std::to_string(k)).c_str() : "");
        }
    } said{name, nth, loadT0};
    CharModel m;
    if (const auto mo = fs.resolve("MESHES/PERSOS/" + name + ".3DO")) {
        const auto md = omk::DataFs::readPath(*mo);
        m.rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
        if (const auto mh = omk::readHeader(md)) m.meshes = omk::readMeshes(md, *mh);
        if (const auto mt = fs.resolve("MESHES/PERSOS/" + name + ".3DT"))
            m.tex = omk::textures(md, omk::DataFs::readPath(*mt));
        m.face = omk::faceMeshOf(m.meshes);
        m.boneIdx.build(m.meshes, omk::MeshNameIndex::shadowNames());
        for (std::size_t i = 0; i < m.meshes.size() && m.root < 0; ++i) {
            bool hasParent = false;
            for (const auto& p : m.meshes)
                if (p.id == m.meshes[i].parent) { hasParent = true; break; }
            if (!hasParent) m.root = static_cast<int>(i);
        }
        m.ready = !m.rest.corners.empty() && !m.meshes.empty();
    }
    ++poolComposition;      // the pool gains this model's textures
    return &charModels.emplace(name, std::move(m)).first->second;
}

PropModel * PlayState::propModelFor(const std::string& stem) {
    const auto& fs = *fs_;
    if (stem.empty()) return nullptr;
    auto it = propModels.find(stem);
    if (it != propModels.end()) return &it->second;
    PropModel m;
    if (const auto mo = fs.resolve("MESHES/OBJETS/" + stem + ".3DO")) {
        const auto md = omk::DataFs::readPath(*mo);
        m.rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
        if (const auto mt = fs.resolve("MESHES/OBJETS/" + stem + ".3DT"))
            m.tex = omk::textures(md, omk::DataFs::readPath(*mt));
        // The HIERARCHY ROOT's position, the way a character model's
        // pelvis is found: the mesh whose parent resolves to nothing.
        if (const auto mh = omk::readHeader(md)) {
            const auto ms = omk::readMeshes(md, *mh);
            int root = -1;
            for (std::size_t i = 0; i < ms.size() && root < 0; ++i) {
                bool hasParent = false;
                for (const auto& q : ms)
                    if (q.id == ms[i].parent) { hasParent = true; break; }
                if (!hasParent) root = static_cast<int>(i);
            }
            if (root >= 0)
                for (int k = 0; k < 3; ++k)
                    m.origin[k] = ms[static_cast<std::size_t>(root)].pos[k];
                for (int k = 0; k < 3; ++k)
                    m.localOff[k] = ms[static_cast<std::size_t>(root)].local[k];
        }
        m.ready = !m.rest.corners.empty();
        std::printf("prop model %s: %zu corners, %zu batches, %zu textures\n",
                    stem.c_str(), m.rest.corners.size(), m.rest.batches.size(),
                    m.tex.size());
    }
    ++poolComposition;
    return &propModels.emplace(stem, std::move(m)).first->second;
}

// One `.CTL` per BANK NAME, and the entry `Actor_LoadBankList` leaves the
// channel on. `PlayerController`'s constructor is the rule quoted:
// `rt_.loadModel()` then `SetPersoBankGroup(channel, Cef_DefaultGroup)`,
// and `clipOwner()`'s chain - an entry whose flags carry 0x8002 is an
// alias and hands the clip on through its GoTo.
CharBank * PlayState::charBankFor(const std::string& name) {
    const auto& fs = *fs_;
    if (name.empty()) return nullptr;
    auto it = charBanks.find(name);
    if (it != charBanks.end()) return &it->second;
    CharBank b;
    if (const auto cp = fs.resolve("ANIMS/" + name + ".CTL")) {
        b.data = omk::DataFs::readPath(*cp);
        b.ctl = omk::readCtl(b.data);
        b.ready = b.ctl.valid;
        int g = -1;
        for (std::size_t k = 0; k < b.ctl.groupList.size(); ++k)
            if (b.ctl.groupList[k].flags & 1u) { g = static_cast<int>(k); break; }
        int s = g >= 0 ? b.ctl.groupList[static_cast<std::size_t>(g)].defaultEntry : -1;
        for (int guard = 0; guard < 64; ++guard) {
            if (s < 0 || s >= static_cast<int>(b.ctl.states.size())) { s = -1; break; }
            if (!(b.ctl.states[static_cast<std::size_t>(s)].flags & 0x8002u)) break;
            s = b.ctl.states[static_cast<std::size_t>(s)].gotoIdx;
        }
        if (s >= 0 && s < static_cast<int>(b.ctl.states.size())) {
            const int c = b.ctl.states[static_cast<std::size_t>(s)].clip;
            if (c >= 0 && c < static_cast<int>(b.ctl.clips.size())) b.idleClip = c;
        }
    }
    return &charBanks.emplace(name, std::move(b)).first->second;
}

std::span<const std::byte> PlayState::playerRecordSpan() {
    return state.raw().subspan(
        static_cast<std::size_t>(omk::GameState::kPlayerRecord),
        static_cast<std::size_t>(omk::GameState::kPlayerRecordSize));
}

bool PlayState::beginMelee(int opponentId, int level) {
    const auto& fs = *fs_;
    auto& in = *in_;
    auto& session = *session_;
    if (!player || fightRun.active) return false;
    Staged* s = nullptr;
    for (auto& up : staged) if (up->actor == opponentId) { s = up.get(); break; }
    if (!s || !s->mo || s->mo->meshes.empty()) {
        std::printf("fight.begin %d: no staged body for that character, "
                    "the script runs on\n", opponentId);
        return false;
    }
    // Both fighters' `.CTL` slot 2. The opponent's comes off his record;
    // the player's off the DB player record's own slot 2 (+90), with the
    // same LABELLED fallback shape his adventure bank has.
    const std::string foeBankName = session.ctlSlotOfActor(opponentId, 2);
    std::string myBankName;
    {
        const auto raw = state.raw();
        const std::size_t off =
            static_cast<std::size_t>(omk::GameState::kPlayerRecord) + 90u;
        for (std::size_t k = 0; k < 9 && off + k < raw.size(); ++k) {
            const char c = static_cast<char>(raw[off + k]);
            if (!c) break;
            myBankName.push_back(c);
        }
        if (myBankName.empty())
            myBankName = (!playerModel.empty() && playerModel[0] == 'F')
                             ? "F1CMBT" : "H1CMBT";   // LABELLED fallback
    }
    CharBank* fb = charBankFor(foeBankName);
    CharBank* pb = charBankFor(myBankName);
    if (!fb || !fb->ready || !pb || !pb->ready) {
        std::printf("fight.begin %d: combat bank missing (player '%s' %s, "
                    "opponent '%s' %s), the script runs on\n", opponentId,
                    myBankName.c_str(), (pb && pb->ready) ? "ok" : "MISSING",
                    foeBankName.c_str(), (fb && fb->ready) ? "ok" : "MISSING");
        return false;
    }
    // The stats, `Fight_Begin`'s six event-44 reads. The player's live in
    // the DB record - `Hooks::getActorProperty` refuses an actor with no
    // chunk record, and Kay'l has none in a city chunk.
    omk::FightStats ps{}, os{};
    {
        const auto rec = playerRecordSpan();
        std::int32_t v = 0;
        // `Hud_Refresh`'s six reads, in `dword_530CB0`'s order
        {
            static const int kCardProps[6] = {16, 19, 17, 3, 18, 2};
            for (int k = 0; k < 6; ++k) {
                std::int32_t c = 0;
                fightRun.cardProps[k] = omk::readActorProperty(rec, kCardProps[k], c) ? c : 0;
            }
        }
        if (omk::readActorProperty(rec, 1, v))  ps.vie = v;
        harnessFightHealth(ps);
        if (omk::readActorProperty(rec, 16, v)) ps.attack = v;
        if (omk::readActorProperty(rec, 18, v)) ps.dodge = v;
        if (omk::readActorProperty(rec, 19, v)) ps.experience = v;
        std::int32_t w = 0;
        if (session.actorProperty(opponentId, 1, w))  os.vie = w;
        if (session.actorProperty(opponentId, 16, w)) os.attack = w;
        if (session.actorProperty(opponentId, 18, w)) os.dodge = w;
        if (session.actorProperty(opponentId, 19, w)) os.experience = w;
    }
    fightRun.foeChannel = std::make_unique<omk::CefChannel>(fb->ctl);
    {
        const int g = fightRun.foeChannel->defaultGroup();
        if (g >= 0) fightRun.foeChannel->setBankGroup(g);
    }
    player->setBank(pb->ctl, pb->data);
    player->setActorState(omk::ActorState::Melee, "Fight_Engage");

    // THE SEPARATION RADIUS. `Fight_Begin` takes `f32(node, 88)` for each
    // fighter and keeps the larger - the NODE's own bounding radius, and
    // this tree has no node, so the model's LARGEST MESH bound stands in
    // and is labelled as a stand-in. It is not `meshes.front()`: that is
    // one mesh (7.1 for HO1_FN, 18 cm) and the first version of this used
    // it, which would have let two fighters stand inside each other. The
    // whole-body mesh gives 42.5, a little over a metre.
    const auto bodyRadius = [](const std::vector<omk::Mesh>& ms) {
        float r = 0.0f;
        for (const auto& m : ms) r = std::max(r, m.radius);
        return r;
    };
    fightRun.player = omk::FightBody{};
    // ONE ORIGIN FOR BOTH FIGHTERS. The engine's two actors each store a
    // single position at `+244/+248/+252`, and `Fight_KeepSeparation` and
    // the camera both read it; this tree had the player on his WALKER's
    // origin (the feet) and the opponent on his staged placement (the
    // PELVIS), about 41 units apart. `measureSeparation` is a 3D distance,
    // so that constant gap satisfied the 43.4 separation radius on its own
    // and the two stood inside each other - a reader: *"some camera issues
    // are when Kay'l and the opponent are at the same place at the same
    // time"*. The camera then took its heading from a two-unit
    // `atan2(b - a)`, which is noise (`todo/fight-mode.md` 15.8d).
    //
    // The opponent's placement is the convention to meet, because it is
    // the one the engine's actor record uses, so the player is lifted to
    // his pelvis on the way in and dropped back to his feet on the way out.
    fightRun.player.x = player->pos()[0];
    fightRun.player.y = player->pos()[1] - player->cameraLift();
    fightRun.player.z = player->pos()[2];
    fightRun.player.yaw = player->facing();
    fightRun.player.radius = bodyRadius(playerMeshes);
    fightRun.player.channel = &player->channel();
    fightRun.player.isPlayer = true;
    // His channel is ticked by the CONTROLLER, because melee's row runs
    // `Actor_ApplyMotion` after `Cef_TickChannel` and the controller is
    // both halves. Without this the fight ticks the channel alone and he
    // stands still while his clips play - which is what the first run of
    // this harness did, 79 units apart for 245 frames.
    fightRun.player.externallyTicked = true;
    fightRun.foe = omk::FightBody{};
    // WHERE HIS PROGRAM LEFT HIM, not where it began. `fight.begin` runs
    // inside the script pump, BEFORE this frame's staged pass notices the
    // program has ended and carries `drawAt` over into `at` - so `at` is
    // still the program's placement (the path START) and `drawAt` is the
    // body the engine's actor record holds: `Anim_RootDelta` has summed
    // the clip into +244 all along. Reading `at` put the supermarket's
    // robber at 'D1BassinP2''s start, 11.9 m away with `SMbox45` between
    // them, where the approach had left him 1.5 m from the player
    // (`todo/fight-mode.md` 15.8e). The same rule `npcBody` uses.
    const float* foeAt = s->progRan ? s->drawAt : s->at;
    fightRun.foe.x = foeAt[0]; fightRun.foe.y = foeAt[1]; fightRun.foe.z = foeAt[2];
    harnessFoeAt(foeAt);
    fightRun.foe.yaw = s->facing;
    fightRun.foe.radius = bodyRadius(s->mo->meshes);
    fightRun.foe.channel = fightRun.foeChannel.get();
    // HIS WALKER. The lift is the player's recipe (`PlayerController`'s
    // `camLift_`) with the opponent's model: the hierarchy root above the
    // model's lowest extent. The swept body is his own sphere list
    // (descriptor +244/+248), centres made relative to the feet.
    fightRun.foeWalker.reset();
    fightRun.foeBlocked = fightRun.foeSlid = fightRun.foeSteps = 0;
    if (!noFoeCollision && !playerSoup.empty() && s->mo->root >= 0) {
        float feet = -1e30f;
        for (const auto& m : s->mo->meshes) feet = std::max(feet, m.pos[1] + m.boxMax[1]);
        const float lift = feet - s->mo->meshes[static_cast<std::size_t>(s->mo->root)].pos[1];
        fightRun.foeLift = (lift > 0.0f && lift < 200.0f) ? lift : 0.0f;
        double fy = double(fightRun.foe.y) + fightRun.foeLift;
        auto w = std::make_unique<omk::Walker>(playerSoup, fightRun.foe.x, fy, fightRun.foe.z);
        if (const auto g = w->ground(fightRun.foe.x, fy, fightRun.foe.z)) {
            w->moveTo(fightRun.foe.x, *g, fightRun.foe.z);
            fy = *g;
        }
        w->setGrid(&playerGrid);
        w->setSteep(&playerSteep);
        std::vector<omk::CollisionSphere> sph;
        if (const auto mp = fs.resolve("MESHES/PERSOS/" + s->model + ".3DO")) {
            const auto md = omk::DataFs::readPath(*mp);
            sph = omk::modelSweepSpheres(md);
        }
        float r = 0.0f;
        std::vector<std::array<double, 3>> centres;
        for (const auto& c : sph) {
            r = std::max(r, c.radius);
            centres.push_back({double(c.pos[0]),
                               double(c.pos[1]) - double(fightRun.foeLift),
                               double(c.pos[2])});
        }
        w->setBlockers(&playerSteep, r > 0.0f ? r : 12.0f, std::move(centres));
        w->setBlockerGrid(&playerSteepGrid);
        std::printf("fight: the opponent's walker - feet %.0f (pelvis %.0f, lift %.1f), "
                    "%zu sweep spheres of radius %.1f against %zu wall faces\n",
                    fy, double(fightRun.foe.y), double(fightRun.foeLift), sph.size(),
                    double(r > 0.0f ? r : 12.0f), playerSteep.size() / 9);
        fightRun.foeWalker = std::move(w);
    }
    fightRun.foeBank = fb;
    fightRun.foeClip = -1;
    fightRun.foeRootRef = -1e30f;
    s->inertAfterFight = false;
    fightRun.foeRoot.clear();
    fightRun.foeFrame = 1.0f;

    fightRun.ms = 0.0;
    fightRun.fight = std::make_unique<omk::Fight>(
        // THE CRT's GENERATOR, NOT THE HOST'S (2026-09-23). This was
        // `std::rand()`: on macOS a different generator from the engine's
        // MSVC one, and one the SYSTEM LIBRARIES share - an audio or
        // input thread drawing once before the fight shifted every roll
        // by one, and a headless fight came out two ways, about one run
        // in four. Same stream as the gunmen's (the engine has one).
        [this] {
            gunRandSeed = gunRandSeed * 214013u + 2531011u;
            return static_cast<int>((gunRandSeed >> 16) & 0x7FFFu);
        },
        [this] { return static_cast<long>(fightRun.ms); });
    fightRun.fight->setBodyTick([&](float dt, std::uint32_t word) {
        if (player) player->tick(dt, word);
    });
    fightRun.fight->begin(fightRun.player, ps, fightRun.foe, os, level,
                          settings.v.fightDifficulty,
                          settings.v.combatCamera);
    in.installScheme(3);          // `Input_InstallScheme(3)`, group Combat
    // `Fight_Begin`'s own `Game_Start("fight.scx")` (playsetup_bodies.cpp)
    if (!fightRt) {
        loadLibrary(fightRt, "SCPTDATA/fight.SCX");
        std::printf("fight library: SCPTDATA/fight.SCX %s\n",
                    fightRt ? "loaded (Fight_Begin's Game_Start)" : "INVALID");
    }
    fightRun.active = true;
    fightRun.camRaySet = false;
    fightRun.hudRefresh = true;
    fightRun.koSeen = 0;
    fightRun.opponent = opponentId;
    fightRun.body = s;
    fightRun.startedAt = session.frameNo();
    // read back from the CHANNEL, not from what `Fight` meant to write
    std::printf("frame %ld: FIGHT GATE - the player's channel honours priority <= %d "
                "(experience %d; flag 0x400 %s), the opponent's %s\n",
                session.frameNo(), player->channel().priorityThreshold(), ps.experience,
                player->channel().priorityGated() ? "set" : "CLEAR",
                (fightRun.foeChannel && fightRun.foeChannel->priorityGated()) ? "SET" : "none");
    std::printf("frame %ld: FIGHT BEGINS against CHARACTERS %d - banks '%s' "
                "vs '%s', AI level %d (profile %d), difficulty %d, "
                "radius %.1f, scheme 3\n"
                "  the player: Vie %d, attack %d, dodge %d, experience %d "
                "(the DB player record)\n"
                "  the opponent: Vie %d, attack %d, dodge %d, experience %d "
                "(his chunk record)\n"
                "  they stand %.0f apart, %.2f m\n",
                session.frameNo(), opponentId, myBankName.c_str(),
                foeBankName.c_str(), level, level + 1,
                settings.v.fightDifficulty,
                double(fightRun.fight->radius()),
                ps.vie, ps.attack, ps.dodge, ps.experience,
                os.vie, os.attack, os.dodge, os.experience,
                double(fightRun.fight->separation()),
                double(fightRun.fight->separation()) * 0.0254);
    return true;
}

// `PlayerController::poseTracks`'s, transcribed rather than reinvented: a
// `.CTL` clip is an `.ani` DESCRIPTOR with no "3.0V" wrapper, its tracks
// resolve to meshes by name, and KEY 0 IS THE REST SENTINEL so frame `f`
// reads key `f + 1`. Only frame 0 is built, because nothing here ticks a
// channel per actor.
omk::NodeTracks PlayState::idleTracksFor(const CharBank& b,
                                   const std::vector<omk::Mesh>& meshes) {
    omk::NodeTracks t;
    if (b.idleClip < 0 || meshes.empty()) return t;
    const auto d = omk::animDescriptor(
        b.data, b.ctl.clips[static_cast<std::size_t>(b.idleClip)].offset);
    if (!d || d->frames <= 0 || d->tracks.empty()) return t;
    const auto lower = [](std::string v) {
        for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return v;
    };
    t.count = static_cast<int>(d->tracks.size());
    t.frames = 1;
    t.rootTrack = -1;
    for (const auto& tr : d->tracks) {
        std::int32_t mi = -1;
        const std::string want = lower(tr.name);
        for (const auto& m : meshes)
            if (lower(m.name) == want) { mi = m.index; break; }
        t.ids.push_back(mi);
    }
    t.quats.assign(1, {});
    t.trans.assign(1, {0.0f, 0.0f, 0.0f});
    t.quats[0].resize(d->tracks.size());
    for (std::size_t i = 0; i < d->tracks.size(); ++i) {
        const omk::AnimTrack& tr = d->tracks[i];
        if (!tr.rotOffset || tr.rotKeys <= 0) continue;
        const int key = tr.rotKeys > 1 ? 1 : 0;      // frame 0 reads key 1
        const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
        if (o + 16 > b.data.size()) continue;
        // // little-endian floats, read as such (PORTING A9): a memcpy here reversed
        // every quaternion on PowerPC - the crowd drew as shards (2026-10-03)
        const std::byte* src = b.data.data() + o;
        t.quats[0][i] = {omk::loadLE<float>(src), omk::loadLE<float>(src + 4),
                         omk::loadLE<float>(src + 8), omk::loadLE<float>(src + 12)};
    }
    return t;
}

int PlayState::loadSpritesInto(SpriteTable& spriteTab, const std::string& scx) {
    const auto& fs = *fs_;
    const auto ap = fs.resolve("SCPTDATA/" + scx);
    if (!ap) return 0;
    const auto ad = omk::DataFs::readPath(*ap);
    const auto st = omk::readScxStream(ad);
    int n = 0;
    for (const auto& sp : st.sprites) {
        if (sp.id < 0) continue;
        const auto slot = static_cast<std::size_t>(sp.id);
        if (spriteTab.idCount <= slot) spriteTab.idCount = slot + 1;
        if (!sp.model || !sp.texture ||
            sp.offset + sp.model + sp.texture > ad.size()) continue;
        const std::span<const std::byte> mo(ad.data() + sp.offset, sp.model);
        const std::span<const std::byte> te(ad.data() + sp.offset + sp.model,
                                            sp.texture);
        auto t = omk::textures(mo, te);
        if (!t.empty()) spriteTab.tex[sp.id] = t.front();
        spriteTab.frames[sp.id] = omk::spriteFrames(mo);
        ++n;
    }
    return n;
}

int PlayState::loadSprites(const std::string& scx) {
 return loadSpritesInto(spriteTab, scx);
}

// Re-run that whenever the resident scene changes: the two libraries first
// (`spriteBase`, decoded once), then the scene's, because the scene's ids
// WIN where they collide, which is the order `Sfx_TickAmbient` resolves in.
void PlayState::refreshSprites() {
    auto& session = *session_;
    if (session.scene().file() == spriteScx) return;
    spriteScx = session.scene().file();
    const auto sp0 = std::chrono::steady_clock::now();
    spriteTab = spriteBase;                          // the two libraries, as above
    const int glob = spriteBaseGlobal;
    const int fightSp = spriteBaseFight;
    const int local = spriteScx.empty() ? 0 : loadSprites(spriteScx);
    int okTex = 0;
    for (const auto& kv : spriteTab.tex) if (kv.second.width) ++okTex;
    std::printf("sprites: reloaded for %s - %d global + %d fight + %d local, "
                "%d decoded, %.1f ms\n",
                spriteScx.empty() ? "<none>" : spriteScx.c_str(), glob,
                fightSp, local, okTex,
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - sp0).count());
    ++poolComposition;          // the sprite section of the pool changed
}

void PlayState::prepareSet(SetLoad& L) {
 OMK_ZONE("set: prepare");   // the profiler (todo/debug-tools.md 6)
    using clk = std::chrono::steady_clock;
    const auto since = [](clk::time_point a) {
        return std::chrono::duration<double, std::milli>(clk::now() - a).count();
    };
    WorldSlot& w = L.out;
#if !defined(__vita__) && !defined(macintosh)   // no std::this_thread on either
    if (L.delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(L.delayMs));
#endif
    auto t = clk::now();
    const auto d = omk::DataFs::readPath(L.path3do);
    std::vector<std::byte> td;
    if (!L.path3dt.empty()) td = omk::DataFs::readPath(L.path3dt);
    L.bytes = d.size() + td.size();
    L.ms[0] = since(t);
    if (d.empty()) return;
    w.stem = L.stem;
    w.area = L.area;
    t = clk::now();
    w.geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    omk::dropCharacterArrays(w.geo);   // a set has no face morph and no seams
    // The runs, in submission order: batch by batch, and inside a batch
    // split wherever `cornerMesh` changes. A set whose corners carry no
    // mesh index leaves this empty, and the draw path then submits whole
    // batches exactly as it did before.
    w.runs.clear();
    if (w.geo.cornerMesh.size() == w.geo.corners.size()) {
        for (std::size_t bi = 0; bi < w.geo.batches.size(); ++bi) {
            const auto& b = w.geo.batches[bi];
            std::uint32_t c = 0;
            while (c < b.count) {
                const std::size_t at = static_cast<std::size_t>(b.start) + c;
                const std::int32_t mi = w.geo.cornerMesh[at];
                std::uint32_t n = 0;
                while (c + n < b.count &&
                       w.geo.cornerMesh[static_cast<std::size_t>(b.start) + c + n] == mi) ++n;
                w.runs.push_back({static_cast<std::uint32_t>(b.start + c), n, mi, bi});
                c += n;
            }
        }
    }
    L.ms[1] = since(t);
    t = clk::now();
    if (!L.path3dt.empty()) w.tex = omk::textures(d, td);
    L.ms[2] = since(t);
    t = clk::now();
    w.soup = omk::collisionSoup(d, omk::SoupKind::Walkable, &w.soupMesh);
    w.steep = omk::collisionSoup(d, omk::SoupKind::Steep, &w.steepMesh);
    {
        // the sight's soup is the shot's minus the cutouts: one soup and a
        // byte a triangle (`omk::cutoutMask`), not two soups
        std::vector<int> shotMesh;
        w.shotSoup = omk::collisionSoup(d, omk::SoupKind::Shot, &shotMesh);
        w.shotCutout = omk::cutoutMask(d, shotMesh);
        // ...and which MESH each triangle is, as runs (a set emits its
        // meshes whole, so a few hundred runs where a triangle each would
        // be ~1 MB): the bolts' world ray names the mesh it met
        // (`off_4C8444`, Astaroth's souls - `todo/astaroth.md`)
        w.shotMeshRuns.clear();
        for (std::size_t t = 0; t < shotMesh.size(); ++t)
            if (w.shotMeshRuns.empty() || w.shotMeshRuns.back().second != shotMesh[t])
                w.shotMeshRuns.push_back({static_cast<std::uint32_t>(t), shotMesh[t]});
        w.shotMeshRuns.shrink_to_fit();
    }
    w.restXyzOfMesh.clear(); w.restSoupOfMesh.clear(); w.restSteepOfMesh.clear();
    L.ms[3] = since(t);
    t = clk::now();
    w.mirror = omk::mirrorPlane(d);
    if (const auto mh = omk::readHeader(d)) {
        w.lights = omk::readLights(d, *mh);
        w.ambientGrey = static_cast<int>(static_cast<std::int64_t>(
            static_cast<double>(mh->ambient) * 255.0));
        w.meshes = omk::readMeshes(d, *mh);
        w.meshHidden.clear();        // a fresh set: flag 2 as shipped
    }
    // THE SET'S OWN EMITTERS - `Sfx_BindAmbientEffects`, the environment
    // family. Every mesh flagged 0x40000000 whose first four name bytes
    // match a section-D tag registers that binding's effect at the mesh's
    // position: the neon, the steam, the smoke. They come up with the SET,
    // not with any object, which is why nothing started them and why they
    // had never appeared here. 319 across the 12 sets that have any.
    // BOUND IN THE FRAME LOOP, not here: walking in, the set loads while
    // the OUTGOING scene is resident, and binding here put ANEKBAH's
    // meshes against the Impasse's `.sfx` (102, the neon alone) in a pool
    // that was about to be dropped, and the city's own `.SCX` then came up
    // with none - the Bowie sequence's fire among them (2026-09-29).
    w.emitters = omk::SceneRunner::setEmitterMeshes(d);
    L.ms[4] = since(t);
    L.found = true;
}

// The slot emptied and the new set's preparation started. -> whether the
// world lost a set by it (the rebuild is then owed at once).
bool PlayState::askSet(int slot, const std::string& stem, int area, long frame) {
 OMK_ZONE("set: ask");   // the profiler (todo/debug-tools.md 6)
    const auto& fs = *fs_;
    auto& session = *session_;
    slot &= 1;
    WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
    const bool had = !w.stem.empty();
    w = WorldSlot{};
    mergedValid = false;
    w.geo.revision = ++worldGeoRev;
    setLoads[slot].reset();          // a job still running finishes into its own state
    slotAsked[slot] = stem;
    slotAskedArea[slot] = area;
    if (stem.empty()) return had;
    const auto o = fs.resolve("MESHES/DECORS/" + stem + ".3DO");
    if (!o) { std::printf("world: no set %s.3DO\n", stem.c_str()); return had; }
    auto L = std::make_shared<SetLoad>();
    L->slot = slot;
    L->stem = stem;
    L->area = area;
    L->askedFrame = frame;
    L->path3do = *o;
    if (const auto t = fs.resolve("MESHES/DECORS/" + stem + ".3DT")) L->path3dt = *t;
    // on a thread only when there are frames to spend on it: a load that
    // comes in on this same frame is simply done here
    const bool streams = !syncSets && session.loading() && session.loadingSlot() == slot &&
                         session.loadSlicesLeft() > 1;
    if (!streams) prepareSet(*L);
    else {
        L->delayMs = loadDelayMs;
        L->job = std::make_unique<omk::BackgroundJob>([this, L] { prepareSet(*L); });
    }
    setLoads[slot] = L;
    return had;
}

// The prepared set into its slot, on the frame's thread. -> whether it came.
bool PlayState::integrateSet(SetLoad& L, long frame) {
 OMK_ZONE("set: integrate");   // the profiler (todo/debug-tools.md 6)
    double waited = 0.0;
    if (L.job) {
        const double w0 = static_cast<double>(front.perfCounter());
        L.job->wait();
        waited = (static_cast<double>(front.perfCounter()) - w0) * 1000.0 /
                 static_cast<double>(front.perfFrequency());
    }
    if (!L.found) return false;
    WorldSlot& w = worldSlots[static_cast<std::size_t>(L.slot & 1)];
    w = std::move(L.out);
    mergedValid = false;
    w.geo.revision = ++worldGeoRev;
    // The Vulkan one is already initialised - its swapchain had to exist
    // before the window could be presented to at all.
    if (!worldReady) {
        if (!vkRen && !glRen && !worldVk) worldSw.init(dispW, dispH);
        worldReady = true;
    }
    std::printf("world: slot %d set %s (AREA %d) - %zu corners, %zu batches, "
                "%zu textures, %zu walkable triangles%s\n",
                L.slot, L.stem.c_str(), L.area, w.geo.corners.size(), w.geo.batches.size(),
                w.tex.size(), w.soup.size() / 9, w.mirror.found ? ", mirror" : "");
    std::printf("set load: %s - %zu KB read in %.1f ms, geometry %.1f, textures %.1f, "
                "soups %.1f, the rest %.1f; asked at frame %ld, in at frame %ld - %s, "
                "the frame waited %.1f ms for it, the Session's load %d frame(s)\n",
                L.stem.c_str(), L.bytes / 1024, L.ms[0], L.ms[1], L.ms[2], L.ms[3], L.ms[4],
                L.askedFrame, frame,
                !L.job ? "prepared on the frame" :
                L.job->threaded() ? "prepared on its own thread" : "no thread, prepared on the frame",
                waited, L.heldFrames);
    return true;
}

// After any slot changed: the texture pool (slot 0's first, then slot
// 1's - a batch's material is offset by its slot's base), the decor list
// the feet are probed against, and the WALKER'S soup. `playerSoup` is
// refilled IN PLACE because the controller's walker holds a reference to
// it: the player walks off one set onto the other without being rebuilt,
// and his `.CTL` state, position and facing survive the transition the
// way the engine's actor does (it is one record; only the decor changes).
void PlayState::rebuildWorld() {
 OMK_ZONE("set: rebuild world");   // the profiler (todo/debug-tools.md 6)
    omk::Renderer& world = *world_;
    const double rebuild0 = static_cast<double>(front.perfCounter());
    // nothing recorded survives new soups (`lazyPlace`); main thread, here -
    // not in `prepareSet`, which may run on a background job
    lazyForget();
    ++worldGen;
    worldTex.clear();
    worldDecors.clear();
    playerSoup.clear();
    playerSteep.clear();   // rebuilt with the soup, or it accumulates
    for (int slot = 0; slot < 2; ++slot) {
        WorldSlot& w = worldSlots[static_cast<std::size_t>(slot)];
        worldTexBase[slot] = worldTex.size();
        if (w.stem.empty()) continue;
        worldTex.insert(worldTex.end(), w.tex.begin(), w.tex.end());
        worldDecors.push_back({w.area, &w.soup});
        playerSoup.insert(playerSoup.end(), w.soup.begin(), w.soup.end());
        playerSteep.insert(playerSteep.end(), w.steep.begin(), w.steep.end());
    }
    const double rebuildA = static_cast<double>(front.perfCounter());
    // exactly their size (todo/ram-vs-original.md, tier A): `clear` keeps a
    // larger set's capacity, and `insert` grows by doubling
    playerSoup.shrink_to_fit();
    playerSteep.shrink_to_fit();
    playerMovingTri.assign(playerSoup.size() / 9, 0);   // new sets: nothing has moved yet
    playerMovingIds.clear();
    rebuildFixedGrid();
    rebuildMovingGrid();
    const double rebuildB = static_cast<double>(front.perfCounter());
    steepMovingTri.assign(playerSteep.size() / 9, 0);
    steepMovingIds.clear();
    rebuildSteepFixedGrid();
    rebuildSteepMovingGrid();
    mergedValid = true;
    const double rebuild1 = static_cast<double>(front.perfCounter());
    // NOT handed to the renderer here. The frame's pool - the sets', then
    // the characters', the player's and the sprites' - is composed and set
    // before anything is drawn, and this bumps `poolComposition`, so it
    // will be. Setting the sets' textures ALONE first made a backend that
    // keeps what it has uploaded drop every character and sprite texture
    // and send them again a moment later: on a console's walk into
    // Anekbah, `27 dropped` then `18 uploaded, 57 ms`, and again `18
    // dropped` / `18 uploaded, 52 ms` on the arrival (2026-09-30).
    // `OMK_POOL_TWICE=1` is the old order, for the comparison.
    static const bool poolTwice = omk::envSet("OMK_POOL_TWICE");
    if (poolTwice) world.setTextures(worldTex);
    poolSize = worldTex.size();
    ++poolComposition;   // the character and sprite sections re-append over this
    const double hz = static_cast<double>(front.perfFrequency());
    std::printf("world: rebuild - soups and grids %.1f ms (the merge %.1f, the walkable grid "
                "%.1f over %zu triangles, the steep one %.1f over %zu), the texture pool %.1f ms\n",
                (rebuild1 - rebuild0) * 1000.0 / hz, (rebuildA - rebuild0) * 1000.0 / hz,
                (rebuildB - rebuildA) * 1000.0 / hz, playerSoup.size() / 9,
                (rebuild1 - rebuildB) * 1000.0 / hz, playerSteep.size() / 9,
                (static_cast<double>(front.perfCounter()) - rebuild1) * 1000.0 / hz);
}

// the interface's clock: the wall's, or a headless run's frames at 30 a second
long PlayState::uiClockMs() {
    if (frames > 0) return n * 1000 / 30;
    return static_cast<long>(front.ticksMs());
}

// ---- THE PLAYER'S DAMAGE, from the HIT onward ----
// `sub_4240E0`'s (a bolt) and `sub_423B10`'s (a strike - the dogs' bite,
// `todo/released-spectres.md` step 5) player arms are the SAME from the
// damage on: the death `sub_423FC0` before the gauge, or the hurt shove,
// the gauge, property 1 and message 0. `what` names the source in the log.
void PlayState::applyPlayerDamage(long n, int owner, const char* what, int dmgIn,
                                       int shield, const omk::HitOut& ho) {
    auto& session = *session_;
                    const std::size_t recAt = static_cast<std::size_t>(omk::GameState::kPlayerRecord);
                    const std::size_t recLen = static_cast<std::size_t>(omk::GameState::kPlayerRecordSize);
                    if (ho.killed) {
                        // `if (+92 <= 0) { sub_423FC0(him); return v30; }` - BEFORE
                        // the gauge, the property and the message, so the killing
                        // hit leaves the gauge at its last value and tells no one.
                        // ---- THE DEATH, `sub_423FC0` (05_sys.c 4606, ported
                        // 2026-09-11 - a reader: "continue with the player's death") -
                        //  1. every live gunman (+160 0x40 up, 0x4002 clear, +92 > 0)
                        //     STANDS DOWN: action 0, or on script step 8 his 0x20
                        //     cleared - applied on his own next tick here, where the
                        //     engine does it inside the hit;
                        //  2. ACTOR_STATE 15; `sub_436D20` shows his body (this port
                        //     draws it throughout - NOT PORTED as a switch); then
                        //     `sub_47CE70` (a global actor's pitch and roll zeroed,
                        //     its writer not traced - NOT PORTED); MESSAGE 9;
                        //  3. .CTL group 201 on his channel, and the countdown
                        //     `dword_4E975C` = its default entry's clip length
                        int stood = 0;
                        for (const auto& [ga, gr] : shootBrains)
                            if ((gr.flags & 0x40u) && !(gr.flags & 0x4002u) && gr.health > 0) {
                                gunStandDown.insert(ga);
                                ++stood;
                            }
                        playerDeathCountdown = 0.0f;
                        if (player) player->setActorState(omk::ActorState::Shoot15, "sub_423FC0");
                        const bool ran9 = session.postMessage(9, session.playerActor());
                        const bool g201 = player && player->enterGroupById(201);
                        if (g201) playerDeathCountdown = static_cast<float>(player->clipFrames());
                        std::printf("frame %ld: PLAYER HIT by actor %d's %s - damage %d, Body "
                                    "Shield %d -> %d; health %d -> %d - KILLED (sub_423FC0): "
                                    "ACTOR_STATE 15, message 9 %s, .CTL group 201 %s, %.0f "
                                    "frames to count down, %d gunmen stand down; the gauge "
                                    "stays at %d\n", n, owner, what, dmgIn, shield,
                                    ho.damage, ho.healthWas, ho.health,
                                    ran9 ? "to its handler" : "- NO handler subscribes",
                                    g201 ? "on" : "NOT FOUND", double(playerDeathCountdown),
                                    stood, hudHealth);
                        return;
                    }
                    // ---- THE HURT REACTION, `sub_47D1F0` (ported 2026-09-12)
                    // The SOUND first - `word_657A14` resolved in the
                    // resident library, which in shoot mode is shoot2.scx,
                    // and played FLAT: `Sound_Play3D`'s `a3` is 0, so it
                    // takes the `v6[12] = 4` arm and carries no position.
                    // Then the shove itself, pointed by the hit's own band
                    // and spent over four frames by the mover above.
                    if (shootRt) {
                        const int w = shootRt->wavBydId(shootMover.hurtSound);
                        if (w < 0)
                            std::printf("  the hurt sound %d is not in shoot2.scx\n",
                                        shootMover.hurtSound);
                        else {
                            const SfxSample& sm = sfxPcm(shootRt->wavData(w));
                            if (sm.pcm->size) front.playSound(sm.pcm, false, fxGain());
                        }
                    }
                    const bool shoved = player &&
                        omk::shootHurt(shootMover, ho.band, player->eulerPitch(),
                                       player->eulerRoll());
                    // then the gauge, property 1 and message 0
                    hudHealth = playerShootRec.health;                // `dword_90E100`
                    omk::writeActorProperty(state.rawMutable().subspan(recAt, recLen), 1,
                                            playerShootRec.health);
                    std::int32_t stored = 0;
                    omk::readActorProperty(state.raw().subspan(recAt, recLen), 1, stored);
                    const bool ran = session.postMessage(0, session.playerActor());
                    std::printf("frame %ld: PLAYER HIT by actor %d's %s - damage %d, Body "
                                "Shield %d -> %d; health %d -> %d, gauge %d (property 1 stored "
                                "%d); message 0 %s; the shove (sub_47D1F0) band %d %s\n",
                                n, owner, what, dmgIn, shield,
                                ho.damage, ho.healthWas, ho.health, hudHealth, int(stored),
                                ran ? "to the hurt handler" : "- NO handler subscribes",
                                ho.band,
                                !shoved ? "- NO shooter installed"
                                        : ho.band < 2
                                            ? "rolls him - and the first-person preset "
                                              "CANNOT SHOW a roll"
                                            : "TIPS the view 2 degrees");
}

double PlayState::phaseNow() {
 return static_cast<double>(front.perfCounter()) / phaseHz;
}

// ...and a few NAMED spans inside them, summed the same way, so a slow
// phase says which call it is (the Vita's start menu: ~600 ms of
// "sim+draw" with no world drawn).
// ...and MARKS down the frame, so a slow one names its SECTION: each
// mark is the end of the section before it, the gaps are printed on a
// frame over `OMK_MARKS_MS` (default 150) from the largest down. The named
// spans covered 6 of a console's 200 ms city frame (2026-09-22).
#if OMK_PROFILE
bool PlayState::profPauseTick() {
 omk::HostInput in;
 if (!front.pump(in) || in.quit) return false;
 present(fb);
 front.delayMs(30);
 return true;
}
#endif

void PlayState::mark(const char* name) {
 phMarks.emplace_back(name, phaseNow());
 if (omk::prof::on()) {
  const std::uint64_t t = omk::prof::now();
  OMK_SECTION(name, profMarkT, t);
  profMarkT = t;
 }
}

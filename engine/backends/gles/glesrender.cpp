// SPDX-License-Identifier: GPL-3.0-or-later
// THE GLES2 BACKEND - `PORTING` A2's boundary turned into OpenGL ES 2.0 calls,
// for the PS VITA through vitaGL (`todo/vita-port.md`), and on the way for any
// host whose GPU speaks nothing newer (WebGL 1, an old Mesa, a Raspberry Pi).
//
// **A backend receives decisions and turns them into API calls; it never
// makes one** (`o3de/renderer.h`). Everything this file does is already
// decided on the far side of the boundary, and every one of those decisions is
// taken here exactly as `backends/vulkan/vkrender.cpp` takes it, because two
// live backends that disagree about a rule draw two different games:
//
//   * the texture is the bucket key's LOW SIX BITS and nothing else (ASSETS 4b);
//   * the three blend states - opaque writes depth, ADD is `one + one`, MUL is
//     `dst * (1 - src)` - and neither transparent state writes depth;
//   * the cutout (flag 0x800) is a colour key on BLACK carried in alpha,
//     never the texture's alpha;
//   * the fog's two exclusions (key bits 0x2080 unfogged, 0x800 doubles both
//     ends), applied per draw from the key;
//   * the depth compare is LESS, culling is off, the clear is black;
//   * the 888 -> 565 quantisation happens in ONE place, `quantise888DitherRow`,
//     on readback - the same function the Vulkan readback calls.
//
// ONE DELIBERATE DIFFERENCE FROM THE VULKAN SHADER, and it is the reference's
// side: the SHIMMER is added PER VERTEX, in the vertex shader. `raster.cpp`
// adds `shimmerOffset(phase, clock)` to each corner's colour before
// interpolation ("the engine adds it into the three bytes in its own
// per-vertex loop"), while `scene.frag` interpolates the PHASE and looks the
// wave up per fragment - which differs wherever a triangle's corners carry
// different phases, and the phase is derived from the vertex address, so they
// usually do. GLSL ES 1.00 would have forced the per-vertex form anyway (no
// integer ops, no guaranteed dynamic indexing in a fragment shader); here the
// constraint and the reference agree. `todo/vita-port.md` records the Vulkan
// side as a question for whoever owns that file.
//
// THE ENHANCEMENTS (`todo/enhancements.md`), all off by default:
//   * the mapped shadow pass and the per-pixel lights (`View::lights`,
//     `Draw::lit`) - the ENHANCED PROGRAMS (`kSceneFragX`), linked in place
//     of the default two only when either is asked for
//     (`glesSetEnhancedLighting`), so a default run builds what it always did;
//   * trilinear filtering, anisotropy (where the context has the extension),
//     supersampling and MSAA (where the GL can multisample a render target -
//     desktop GL; not GLES2, not vitaGL) are the enhancements here; bilinear
//     is the default;
//   * the native mirror (`drawMirrorScene`) - returns false, so
//     `drawWithMirror` takes its CPU path through `readback()`. A stencil
//     version is `todo/vita-port.md` step G5; GLES2 has the stencil for it.
//
// THREE BUILD TARGETS, one source:
//   * `__vita__`         vitaGL. Shaders are GLSL ES 1.00, which vitaGL
//                        translates at run time - that needs
//                        `ur0:data/libshacccg.suprx` on the device (the
//                        standard requirement of vitaGL ports; see the plan).
//   * `__APPLE__`        the legacy OpenGL 2.1 framework, so this file can be
//                        CHECKED on the machine the rest of the port is
//                        developed on (`gles_probe.cpp`, against the software
//                        reference - the same differential `run_vulkan` is).
//   * anything else      <GLES2/gl2.h>: Linux GLES, Emscripten/WebGL 1.
//
// **A current GL context is the caller's.** The Vulkan backend creates its
// own device because Vulkan lets it; a GL context belongs to the window
// system, so the frontend (SDL, vitaGL's `vglInit`, CGL in the probe) makes it
// current before `init()` and keeps it current.
//
// NOTHING includes this file and the Makefile does not glob `backends/`, so it
// is in nobody's build until a target names it - `backends/vita/CMakeLists.txt`
// and the probe's own build line do.
#if defined(__vita__)
#  include <vitaGL.h>
#elif defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION 1
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#else
#  include <GLES2/gl2.h>
#endif

// MULTISAMPLED RENDER TARGETS (`todo/enhancements.md` 0) need
// `glRenderbufferStorageMultisample` and `glBlitFramebuffer`: desktop GL has
// them (ARB_framebuffer_object), GLES2 does not, and vitaGL multisamples only
// the DISPLAY - a mode fixed at `vglInit` for every surface, not a render
// target this backend can resolve. So MSAA is built where the calls exist and
// refused, and said, everywhere else; supersampling is the anti-aliasing a
// Vita has.
#if defined(__APPLE__)
#  define OMK_GLES_MSAA 1
#else
#  define OMK_GLES_MSAA 0
#endif

// The one call whose NAME differs: GLES has only the float form, desktop GL
// 2.1 only the double one.
#if defined(__APPLE__)
#  define OMK_GL_CLEAR_DEPTH(d) glClearDepth(d)
#else
#  define OMK_GL_CLEAR_DEPTH(d) glClearDepthf(d)
#endif

#include "o3de/depthtie.h"
#include "platform/profile.h"
#include "o3de/renderer.h"
#include "o3de/shimmer.h"
#include "ui/surface.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <chrono>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------- shaders
//
// The prelude differs by dialect and nothing else does: GLSL ES 1.00 needs a
// default float precision in the fragment stage, desktop GLSL 1.20 rejects the
// qualifier-less `precision` statement on some drivers and has no use for it.
#if defined(__APPLE__)
constexpr const char* kVertPrelude = "#version 120\n";
constexpr const char* kFragPrelude = "#version 120\n";
#else
constexpr const char* kVertPrelude = "#version 100\n";
// highp where the device has it: the fog compares a VIEW DEPTH in world units
// (hundreds to tens of thousands) and the present pass does exact integer
// arithmetic in floats; fp16 would get both wrong. The Vita's USSE has fp32.
constexpr const char* kFragPrelude =
    "#version 100\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n";
#endif

// THE SCENE, vertex stage. `uMvp` is built on the CPU from `cameraBasis`, the
// software rasterizer's own basis, so the conventions cannot drift apart.
// The UVs arrive in TEXEL units, as the shipped data stores them, and are
// normalised here by the bound texture's size - GLSL ES 1.00 has no
// `textureSize`, and doing it per vertex is cheaper anyway.
constexpr const char* kSceneVert = R"(
uniform mat4  uMvp;
uniform vec2  uTexSize;
uniform float uShimmerClock;
// the 32-step wave, `kShimmerWave / 255`, as EIGHT vec4s and not one float
// array: vitaGL (at the SDK's commit) sizes a uniform float ARRAY's storage
// short and `glUniform1fv(loc, 32, ...)` writes the rest over newlib's heap -
// the Vita's start-up crash of 2026-09-18, found by heap checkpoints and by
// the corrupt free-list pointer being -10/255, this table's own value.
uniform vec4 uWave0; uniform vec4 uWave1; uniform vec4 uWave2; uniform vec4 uWave3;
uniform vec4 uWave4; uniform vec4 uWave5; uniform vec4 uWave6; uniform vec4 uWave7;
float waveAt(float i) {
    float b = floor(i / 4.0);
    vec4 v = b < 1.0 ? uWave0 : b < 2.0 ? uWave1 : b < 3.0 ? uWave2 : b < 4.0 ? uWave3 :
             b < 5.0 ? uWave4 : b < 6.0 ? uWave5 : b < 7.0 ? uWave6 : uWave7;
    float c = i - b * 4.0;
    return c < 1.0 ? v.x : c < 2.0 ? v.y : c < 3.0 ? v.z : v.w;
}
attribute vec3  aPos;
attribute vec2  aUV;
attribute vec3  aCol;
attribute float aPhase;
varying vec2  vUV;
varying vec3  vCol;
varying float vDepth;
void main() {
    vUV = aUV / uTexSize;
    // A TIE LOSER carries its phase moved down by 8192 (`kTieLoserPhase`): -1
    // (no shimmer) becomes -8193, a phase p becomes p - 8192. Both decode exactly.
    float ph = aPhase;
    float tie = 0.0;
    if (ph < -4096.0) { ph += 8192.0; tie = 1.0; }
    float wave = 0.0;
    if (ph >= 0.0) {
        // ((int(clock) >> 2) + int(phase)) & 31, with no integer operators:
        // both are non-negative, so floor(x / 4) is the shift and mod the mask
        float i = mod(floor(floor(uShimmerClock) / 4.0) + floor(ph), 32.0);
        wave = waveAt(i);
    }
    vCol = aCol + vec3(wave);
    gl_Position = uMvp * vec4(aPos, 1.0);
    // ...and is drawn TWO 16-bit depth steps back (window depth is NDC / 2), so
    // the earlier coincident face keeps every pixel under GL_LESS whatever the
    // interpolation noise between the two triangulations - the engine's strict
    // GREATER on its quantised buffer, which a later face beats only by being
    // a whole step nearer (ASSETS 4b; `raster.cpp`'s kDepthTie is the same band)
    gl_Position.z += tie * (4.0 / 65535.0) * gl_Position.w;
    // row 3 of uMvp is f . (world - eye): w is the view depth raster.cpp fogs on
    vDepth = gl_Position.w;
}
)";

// THE POSING PROGRAM's vertex stage (todo/gpu-skinning.md) - `kSceneVert` with
// the corner moved and lit by its mesh. A SEPARATE STRING on purpose, not an
// `#ifdef` inside the scene's: the Vita's shader cache is keyed by a hash of the
// source, and editing `kSceneVert` would orphan its cached `.gxp` - a console
// without `libshacccg.suprx` would lose the SCENE, not just this program.
// Keep the two in step by hand; `engine: gles pose` draws through both.
constexpr const char* kPosedVert = R"(
uniform mat4  uMvp;
uniform vec2  uTexSize;
uniform float uShimmerClock;
// the 32-step wave, `kShimmerWave / 255`, as EIGHT vec4s and not one float
// array: vitaGL (at the SDK's commit) sizes a uniform float ARRAY's storage
// short and `glUniform1fv(loc, 32, ...)` writes the rest over newlib's heap -
// the Vita's start-up crash of 2026-09-18, found by heap checkpoints and by
// the corrupt free-list pointer being -10/255, this table's own value.
uniform vec4 uWave0; uniform vec4 uWave1; uniform vec4 uWave2; uniform vec4 uWave3;
uniform vec4 uWave4; uniform vec4 uWave5; uniform vec4 uWave6; uniform vec4 uWave7;
float waveAt(float i) {
    float b = floor(i / 4.0);
    vec4 v = b < 1.0 ? uWave0 : b < 2.0 ? uWave1 : b < 3.0 ? uWave2 : b < 4.0 ? uWave3 :
             b < 5.0 ? uWave4 : b < 6.0 ? uWave5 : b < 7.0 ? uWave6 : uWave7;
    float c = i - b * 4.0;
    return c < 1.0 ? v.x : c < 2.0 ? v.y : c < 3.0 ? v.z : v.w;
}
attribute vec3  aPos;
attribute vec2  aUV;
attribute vec3  aCol;
attribute float aPhase;
// THE BODY POSED HERE (todo/gpu-skinning.md): `aPos` is the REST corner and
// `aSlot` its mesh's slot; each slot's affine is the first three of its four rows of `uPose`. A vec4
// ARRAY on purpose: vitaGL copies one straight (16 bytes an element), where a
// float array is laid out 8 bytes an element and overran the heap (above).
attribute float aSlot;
// FOUR rows a slot, the fourth unused. Chosen 2026-09-27 on a WRONG reading
// (that a computed index must be even): the device still dropped every odd
// slot at 4 rows. The cause is the SLOT'S CONVERSION - see `s` below.
uniform vec4  uPose[128];
// ...and LIT here (step 2): `vertexlight.cpp`'s law on the posed normal. Two
// vec4 a light: the direction scaled by its strength, then its colour bytes.
attribute vec3  aNormal;
uniform vec4  uLight[16];
uniform float uLightCount;
uniform float uLightBlack;
varying vec2  vUV;
varying vec3  vCol;
varying float vDepth;
void main() {
    vUV = aUV / uTexSize;
    float wave = 0.0;
    if (aPhase >= 0.0) {
        // ((int(clock) >> 2) + int(phase)) & 31, with no integer operators:
        // both are non-negative, so floor(x / 4) is the shift and mod the mask
        float i = mod(floor(floor(uShimmerClock) / 4.0) + floor(aPhase), 32.0);
        wave = waveAt(i);
    }
    vCol = aCol + vec3(wave);
    // floor FIRST: the Vita's compiler ROUNDS in int() - half to even - so
    // int(k + 0.5) is k + 1 for every odd k, which put each odd slot on its
    // even neighbour's affine (the self-test's 16 of 32, and the cutscene
    // bodies' flung hands, feet and head). int() of an exact integer agrees
    // under any rounding.
    int s = int(floor(aSlot + 0.5)) * 4;
    // THE LIGHT, corner by corner as `applyLights` walks it: t = -(N.L)
    // truncated toward zero and clamped to 0..255, the ramp `(t * c) >> 8`
    // (exact in float: both are integers under 256), each light added and
    // clamped in turn
    vec3 nrm = vec3(dot(uPose[s].xyz, aNormal), dot(uPose[s + 1].xyz, aNormal),
                    dot(uPose[s + 2].xyz, aNormal));
    vec3 lit = uLightBlack > 0.5 ? vec3(0.0) : aCol;
    for (int i = 0; i < 8; ++i) {
        if (float(i) >= uLightCount) break;
        float t = -dot(nrm, uLight[2 * i].xyz);
        float ti = clamp(t < 0.0 ? ceil(t) : floor(t), 0.0, 255.0);
        vec3 a = floor(ti * uLight[2 * i + 1].rgb / 256.0) / 255.0;
        lit = min(lit + a, vec3(1.0));
    }
    if (uLightCount > 0.5 || uLightBlack > 0.5) vCol = lit + vec3(wave);
    vec3 wp = vec3(dot(uPose[s].xyz, aPos) + uPose[s].w,
                   dot(uPose[s + 1].xyz, aPos) + uPose[s + 1].w,
                   dot(uPose[s + 2].xyz, aPos) + uPose[s + 2].w);
    gl_Position = uMvp * vec4(wp, 1.0);
    // row 3 of uMvp is f . (world - eye): w is the view depth raster.cpp fogs on
    vDepth = gl_Position.w;
}
)";

// THE SCENE, fragment stage - `scene.frag` with the enhancements taken out.
constexpr const char* kSceneFrag = R"(
uniform sampler2D uTex;
uniform int   uCutout;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3  uFogColour;
varying vec2  vUV;
varying vec3  vCol;
varying float vDepth;
void main() {
    vec4 t = texture2D(uTex, vUV);
    if (uCutout != 0) {
        if (t.a < 0.5) discard;
        t.rgb /= t.a;
    }
    vec3 c = clamp(t.rgb * vCol, 0.0, 1.0);
    if (uFogEnd > uFogStart && vDepth > uFogStart) {
        float f = clamp((uFogEnd - vDepth) / (uFogEnd - uFogStart), 0.0, 1.0);
        c = mix(uFogColour, c, f);
    }
    gl_FragColor = vec4(c, 1.0);
}
)";

// THE PRESENT PASS: a picture onto the window, NEAREST, scaled into `uDst`.
// Used for both the world target (`uDither` 1: the 888 -> 565 rule applied
// here, `present.frag`'s arithmetic in float) and an RGB565 surface the CPU
// composed (`uDither` 0 and `uQuantise` 0: already 565, shown as it is).
constexpr const char* kPresentVert = R"(
attribute vec2 aPos;       // 0..1 over the destination rectangle
uniform vec4 uDst;         // x, y, w, h in NDC
varying vec2 vPic;         // 0..1 over the picture, top-left origin
void main() {
    vPic = vec2(aPos.x, 1.0 - aPos.y);
    gl_Position = vec4(uDst.x + aPos.x * uDst.z, uDst.y + aPos.y * uDst.w, 0.0, 1.0);
}
)";

constexpr const char* kPresentFrag = R"(
uniform sampler2D uPic;
uniform sampler2D uBayer;  // 4x4, the matrix / 255, NEAREST + REPEAT
uniform vec2  uPicSize;    // the picture's size in pixels
uniform vec2  uTexSize;    // the texture's - larger than the picture when a
                           // letterboxed world fills only the target's top rows
uniform int   uFlipY;      // 1 for a GL render target, whose row 0 is the bottom
uniform int   uDither;     // the View's DITHERENABLE
uniform int   uQuantise;   // 0 = the picture is already 565
varying vec2  vPic;
float trunc0(float v) { return v < 0.0 ? -floor(-v) : floor(v); }
float q(float c8, float t, float levels, float step) {
    float d = trunc0(t * step / 16.0);
    float v = clamp(c8 + d, 0.0, 255.0);
    float n = floor((v * levels + 127.0) / 255.0);
    return n;
}
void main() {
    vec2 px = floor(vPic * uPicSize);                 // picture pixel, top-left origin
    vec2 src = px;
    // a render target's picture is its TOP rows, and GL's row 0 is the bottom
    if (uFlipY != 0) src.y = uTexSize.y - 1.0 - px.y;
    vec4 s = texture2D(uPic, (src + 0.5) / uTexSize);
    if (uQuantise == 0) { gl_FragColor = vec4(s.rgb, 1.0); return; }
    float r = floor(s.r * 255.0 + 0.5);
    float g = floor(s.g * 255.0 + 0.5);
    float b = floor(s.b * 255.0 + 0.5);
    float r5, g6, b5;
    if (uDither != 0) {
        float t = floor(texture2D(uBayer, (mod(px, 4.0) + 0.5) / 4.0).r * 255.0 + 0.5) - 8.0;
        r5 = q(r, t, 31.0, 8.0);
        g6 = q(g, t, 63.0, 4.0);
        b5 = q(b, t, 31.0, 8.0);
    } else {
        r5 = floor(r / 8.0);
        g6 = floor(g / 4.0);
        b5 = floor(b / 8.0);
    }
    vec3 o = vec3(r5 * 8.0 + floor(r5 / 4.0),
                  g6 * 4.0 + floor(g6 / 16.0),
                  b5 * 8.0 + floor(b5 / 4.0));
    gl_FragColor = vec4(o / 255.0, 1.0);
}
)";

// ---- THE ENHANCED PROGRAMS (`todo/enhancements.md` 6 and 7) ----------------
//
// The scene and posing programs again, carrying what PER-PIXEL LIGHTING and
// the MAPPED SHADOW need - the world position and the normal - and a fragment
// stage with `scene.frag`'s two laws transcribed. Linked IN PLACE OF the two
// default programs, and only when either enhancement is asked for: separate
// strings, so the Vita's cached default programs are not orphaned by them
// (`kPosedVert`'s note), and a context that cannot build them falls back to
// the defaults with both enhancements refused. Keep them in step with
// `kSceneVert` / `kPosedVert` by hand.
constexpr const char* kSceneVertX = R"(
uniform mat4  uMvp;
uniform vec2  uTexSize;
uniform float uShimmerClock;
uniform vec4 uWave0; uniform vec4 uWave1; uniform vec4 uWave2; uniform vec4 uWave3;
uniform vec4 uWave4; uniform vec4 uWave5; uniform vec4 uWave6; uniform vec4 uWave7;
float waveAt(float i) {
    float b = floor(i / 4.0);
    vec4 v = b < 1.0 ? uWave0 : b < 2.0 ? uWave1 : b < 3.0 ? uWave2 : b < 4.0 ? uWave3 :
             b < 5.0 ? uWave4 : b < 6.0 ? uWave5 : b < 7.0 ? uWave6 : uWave7;
    float c = i - b * 4.0;
    return c < 1.0 ? v.x : c < 2.0 ? v.y : c < 3.0 ? v.z : v.w;
}
attribute vec3  aPos;
attribute vec2  aUV;
attribute vec3  aCol;
attribute float aPhase;
// a LIT draw's normals, from a buffer of their own (`uploadNormals`); every
// other draw leaves the array off and reads a constant it never uses
attribute vec3  aNormal;
varying vec2  vUV;
varying vec3  vCol;
varying float vDepth;
varying vec3  vWorld;
varying vec3  vNrm;
void main() {
    vUV = aUV / uTexSize;
    float ph = aPhase;
    float tie = 0.0;
    if (ph < -4096.0) { ph += 8192.0; tie = 1.0; }
    float wave = 0.0;
    if (ph >= 0.0) {
        float i = mod(floor(floor(uShimmerClock) / 4.0) + floor(ph), 32.0);
        wave = waveAt(i);
    }
    vCol = aCol + vec3(wave);
    vWorld = aPos;
    vNrm = aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_Position.z += tie * (4.0 / 65535.0) * gl_Position.w;
    vDepth = gl_Position.w;
}
)";

constexpr const char* kPosedVertX = R"(
uniform mat4  uMvp;
uniform vec2  uTexSize;
uniform float uShimmerClock;
uniform vec4 uWave0; uniform vec4 uWave1; uniform vec4 uWave2; uniform vec4 uWave3;
uniform vec4 uWave4; uniform vec4 uWave5; uniform vec4 uWave6; uniform vec4 uWave7;
float waveAt(float i) {
    float b = floor(i / 4.0);
    vec4 v = b < 1.0 ? uWave0 : b < 2.0 ? uWave1 : b < 3.0 ? uWave2 : b < 4.0 ? uWave3 :
             b < 5.0 ? uWave4 : b < 6.0 ? uWave5 : b < 7.0 ? uWave6 : uWave7;
    float c = i - b * 4.0;
    return c < 1.0 ? v.x : c < 2.0 ? v.y : c < 3.0 ? v.z : v.w;
}
attribute vec3  aPos;
attribute vec2  aUV;
attribute vec3  aCol;
attribute float aPhase;
attribute float aSlot;
uniform vec4  uPose[128];
attribute vec3  aNormal;
uniform vec4  uLight[16];
uniform float uLightCount;
uniform float uLightBlack;
varying vec2  vUV;
varying vec3  vCol;
varying float vDepth;
varying vec3  vWorld;
varying vec3  vNrm;
void main() {
    vUV = aUV / uTexSize;
    float wave = 0.0;
    if (aPhase >= 0.0) {
        float i = mod(floor(floor(uShimmerClock) / 4.0) + floor(aPhase), 32.0);
        wave = waveAt(i);
    }
    vCol = aCol + vec3(wave);
    int s = int(floor(aSlot + 0.5)) * 4;
    vec3 nrm = vec3(dot(uPose[s].xyz, aNormal), dot(uPose[s + 1].xyz, aNormal),
                    dot(uPose[s + 2].xyz, aNormal));
    vec3 lit = uLightBlack > 0.5 ? vec3(0.0) : aCol;
    for (int i = 0; i < 8; ++i) {
        if (float(i) >= uLightCount) break;
        float t = -dot(nrm, uLight[2 * i].xyz);
        float ti = clamp(t < 0.0 ? ceil(t) : floor(t), 0.0, 255.0);
        vec3 a = floor(ti * uLight[2 * i + 1].rgb / 256.0) / 255.0;
        lit = min(lit + a, vec3(1.0));
    }
    if (uLightCount > 0.5 || uLightBlack > 0.5) vCol = lit + vec3(wave);
    vec3 wp = vec3(dot(uPose[s].xyz, aPos) + uPose[s].w,
                   dot(uPose[s + 1].xyz, aPos) + uPose[s + 1].w,
                   dot(uPose[s + 2].xyz, aPos) + uPose[s + 2].w);
    vWorld = wp;
    vNrm = nrm;
    gl_Position = uMvp * vec4(wp, 1.0);
    vDepth = gl_Position.w;
}
)";

// `scene.frag`'s per-pixel light (`sub_493E40` per fragment: the reach test,
// the LINEAR falloff between the two radii, `k = intensity * 256 * fall`,
// `-(N.L)` and the `(t * c) >> 8` ramp) and its mapped-shadow lookup (an
// orthographic slab fitted to the casters, 3x3 PCF, a caster never receives),
// over `kSceneFrag`. The map's depth arrives PACKED into RGBA8 (`kShadowFrag`):
// GLES2 guarantees no depth texture.
constexpr const char* kSceneFragX = R"(
uniform sampler2D uTex;
uniform int   uCutout;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3  uFogColour;
uniform float uLit;        // 0 the baked colour, 1 lit from BLACK, 2 lit ADDED to it
uniform float uCaster;     // a caster does not receive
uniform vec4  uPL[24];     // 8 lights: (pos, outer radius), (dir, inner), (colour, intensity)
uniform float uPLCount;
uniform sampler2D uShadow;
uniform mat4  uLightMvp;
uniform vec4  uShadowP;    // strength (0: no shadow this frame), texel, bias
varying vec2  vUV;
varying vec3  vCol;
varying float vDepth;
varying vec3  vWorld;
varying vec3  vNrm;
vec3 litColour(vec3 n, vec3 w) {
    vec3 c = vec3(0.0);
    for (int i = 0; i < 8; ++i) {
        if (float(i) >= uPLCount) break;
        vec4 pa = uPL[3 * i];
        vec4 db = uPL[3 * i + 1];
        vec4 ci = uPL[3 * i + 2];
        vec3 d = w - pa.xyz;
        float d2 = dot(d, d);
        if (d2 > pa.w * pa.w) continue;      // the engine's own reach test
        if (pa.w <= db.w) continue;          // a degenerate pair lights nothing
        float fall = min(1.0 - (sqrt(d2) - db.w) / (pa.w - db.w), 1.0);
        float k = ci.a * 256.0 * fall;
        if (k <= 0.0) continue;
        float t = -dot(n, db.xyz * k);
        if (t <= 0.0) continue;
        c += min(t * ci.rgb / 256.0, vec3(1.0));
    }
    return min(c, vec3(1.0));
}
float unpackDepth(vec4 e) {
    return dot(e, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0));
}
float litness() {
    if (uShadowP.x <= 0.0 || uCaster > 0.5) return 1.0;
    vec4 lp = uLightMvp * vec4(vWorld, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 p = lp.xyz / lp.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    if (p.z < 0.0 || p.z > 1.0) return 1.0;
    float lit = 0.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            float d = unpackDepth(texture2D(uShadow, uv + vec2(float(i), float(j)) * uShadowP.y));
            lit += (p.z - uShadowP.z <= d) ? 1.0 : 0.0;
        }
    return mix(1.0 - uShadowP.x, 1.0, lit / 9.0);
}
void main() {
    vec4 t = texture2D(uTex, vUV);
    if (uCutout != 0) {
        if (t.a < 0.5) discard;
        t.rgb /= t.a;
    }
    vec3 shade = vCol;
    if (uLit > 1.5) shade = min(vCol + litColour(normalize(vNrm), vWorld), vec3(1.0));
    else if (uLit > 0.5) shade = litColour(normalize(vNrm), vWorld);
    vec3 c = clamp(t.rgb * shade * litness(), 0.0, 1.0);
    if (uFogEnd > uFogStart && vDepth > uFogStart) {
        float f = clamp((uFogEnd - vDepth) / (uFogEnd - uFogStart), 0.0, 1.0);
        c = mix(uFogColour, c, f);
    }
    gl_FragColor = vec4(c, 1.0);
}
)";

// THE SHADOW MAP's DEPTH PASS: the casters from the light, their slab depth
// (0..1, `uLightMvp`'s z) PACKED into the RGBA8 target - the 8-bit fractions of
// one float, read back by `unpackDepth`. GL's depth buffer orders the casters;
// its clip z is the slab's remapped to -1..1.
constexpr const char* kShadowVert = R"(
uniform mat4 uLightMvp;
attribute vec3 aPos;
varying float vZ;
void main() {
    vec4 p = uLightMvp * vec4(aPos, 1.0);
    vZ = p.z;
    gl_Position = vec4(p.xy, p.z * 2.0 - 1.0, p.w);
}
)";

constexpr const char* kShadowPosedVert = R"(
uniform mat4 uLightMvp;
uniform vec4 uPose[128];
attribute vec3 aPos;
attribute float aSlot;
varying float vZ;
void main() {
    int s = int(floor(aSlot + 0.5)) * 4;
    vec3 wp = vec3(dot(uPose[s].xyz, aPos) + uPose[s].w,
                   dot(uPose[s + 1].xyz, aPos) + uPose[s + 1].w,
                   dot(uPose[s + 2].xyz, aPos) + uPose[s + 2].w);
    vec4 p = uLightMvp * vec4(wp, 1.0);
    vZ = p.z;
    gl_Position = vec4(p.xy, p.z * 2.0 - 1.0, p.w);
}
)";

constexpr const char* kShadowFrag = R"(
varying float vZ;
void main() {
    float d = clamp(vZ, 0.0, 0.99999);
    vec4 e = fract(d * vec4(1.0, 255.0, 65025.0, 16581375.0));
    e -= e.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
    gl_FragColor = e;
}
)";

// THE PRESENT PASS OF A SUPERSAMPLED WORLD (`todo/enhancements.md` 9): the
// target is `uSS` times the picture each way, and each picture pixel is first
// the ROUNDED integer mean of its `uSS x uSS` block, `(sum + n/2) / n` on the
// 8-bit values - `readback`'s resolve, and `present.frag`'s on Vulkan - and
// only then quantised ONCE, with `kPresentFrag`'s arithmetic. A SEPARATE
// STRING, not a branch in `kPresentFrag`: the Vita's shader cache is keyed by
// the source, and an edit there would orphan the cached program every frame
// presents through. `n` is 4 or 16, so every division here is exact.
constexpr const char* kPresentSSFrag = R"(
uniform sampler2D uPic;
uniform sampler2D uBayer;  // 4x4, the matrix / 255, NEAREST + REPEAT
uniform vec2  uPicSize;    // the picture's size in OUTPUT pixels
uniform vec2  uTexSize;    // the render target's - `uSS` times the frame
uniform float uSS;         // 2 or 4
uniform int   uDither;
varying vec2  vPic;
float trunc0(float v) { return v < 0.0 ? -floor(-v) : floor(v); }
float q(float c8, float t, float levels, float step) {
    float d = trunc0(t * step / 16.0);
    float v = clamp(c8 + d, 0.0, 255.0);
    return floor((v * levels + 127.0) / 255.0);
}
void main() {
    vec2 px = floor(vPic * uPicSize);
    vec3 acc = vec3(0.0);
    for (int sy = 0; sy < 4; ++sy) {
        if (float(sy) >= uSS) break;
        for (int sx = 0; sx < 4; ++sx) {
            if (float(sx) >= uSS) break;
            // the picture is the target's TOP rows, and GL's row 0 is the bottom
            vec2 src = vec2(px.x * uSS + float(sx), uTexSize.y - 1.0 - (px.y * uSS + float(sy)));
            acc += floor(texture2D(uPic, (src + 0.5) / uTexSize).rgb * 255.0 + 0.5);
        }
    }
    float n = uSS * uSS;
    vec3 m = floor((acc + floor(n / 2.0)) / n);
    float r5, g6, b5;
    if (uDither != 0) {
        float t = floor(texture2D(uBayer, (mod(px, 4.0) + 0.5) / 4.0).r * 255.0 + 0.5) - 8.0;
        r5 = q(m.r, t, 31.0, 8.0);
        g6 = q(m.g, t, 63.0, 4.0);
        b5 = q(m.b, t, 31.0, 8.0);
    } else {
        r5 = floor(m.r / 8.0);
        g6 = floor(m.g / 4.0);
        b5 = floor(m.b / 8.0);
    }
    vec3 o = vec3(r5 * 8.0 + floor(r5 / 4.0),
                  g6 * 4.0 + floor(g6 / 16.0),
                  b5 * 8.0 + floor(b5 / 4.0));
    gl_FragColor = vec4(o / 255.0, 1.0);
}
)";

GLuint compile(GLenum kind, const char* prelude, const char* body) {
    const GLuint s = glCreateShader(kind);
    const char* src[2] = {prelude, body};
    glShaderSource(s, 2, src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(s, sizeof log - 1, nullptr, log);
        std::fprintf(stderr, "gles: shader compile failed:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs,
            std::initializer_list<std::pair<GLuint, const char*>> attribs) {
    const GLuint v = compile(GL_VERTEX_SHADER, kVertPrelude, vs);
    const GLuint f = compile(GL_FRAGMENT_SHADER, kFragPrelude, fs);
    if (!v || !f) return 0;
    const GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    for (const auto& [loc, name] : attribs) glBindAttribLocation(p, loc, name);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(p, sizeof log - 1, nullptr, log);
        std::fprintf(stderr, "gles: program link failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// The vertex as the GPU sees it: 9 floats, 36 bytes. The normal stays on the
// CPU - it only feeds the per-pixel light, which this backend does not have -
// and dropping it is 40% of the upload on a device where the upload is the
// cost (`todo/vita-port.md`, the posed bodies).
struct GpuVert {
    float x, y, z, u, v, r, g, b, phase;
};
static_assert(sizeof(GpuVert) == 36, "the GLES vertex is 36 bytes");

// A REST corner of a body the GPU poses: the vertex, and its mesh's SLOT - the
// meshes a geometry uses, numbered densely, so a 76-mesh crowd model whose
// drawn skeleton has 19 needs 19 affines and not 76.
struct GpuPoseVert {
    GpuVert v;
    float slot;
    float nx, ny, nz;          // the REST normal, turned by the slot's affine
};
static_assert(sizeof(GpuPoseVert) == 52, "the posed GLES vertex is 52 bytes");

GpuVert gpuVert(const omk::Corner& c) {
    return {c.x, c.y, c.z, c.u, c.v, c.r, c.g, c.b, c.phase};
}
// how a baked tie loser's corner is marked (the scene shader decodes it)
constexpr float kTieLoserPhase = 8192.0f;

constexpr GLuint kAttrPos = 0, kAttrUV = 1, kAttrCol = 2, kAttrPhase = 3, kAttrSlot = 4,
                 kAttrNormal = 5;
// the lights a posed body may carry (`uLight`); a body reached by more is lit
// on the CPU by the frontend
constexpr int kVertexLights = 8;
// a posed body's meshes, one affine (four vec4 rows of `uPose`, three used) each: Kay'l
// has 20, a crowd skeleton 19; a body with more is posed here on the CPU
constexpr int kPoseSlots = 32;

// ---- THE INTERFACE AS AN OVERLAY (`todo/vita-port.md` G6 step 2) ----------
//
// The frame the CPU composed, with the world's rows left as a KEY, blended
// over the world the GPU already presented - so a frame with a subtitle or a
// conversation on it needs no readback. Three 565 values mean something
// `play.cpp` resolves its key into two planes - C, the 565 frame, and M, an
// 8-bit "how much of the world shows through" - and this draws
// `C + world * M` in one blend (GL_ONE, GL_SRC_ALPHA).
constexpr const char* kOverlayFrag = R"(
uniform sampler2D uPic;
uniform sampler2D uMask;
uniform vec4  uFade;       // rgb + weight
uniform vec2  uPicSize;
varying vec2  vPic;
void main() {
    vec2 uv = (floor(vPic * uPicSize) + 0.5) / uPicSize;
    vec3 c = texture2D(uPic, uv).rgb;
    // the KEY (0xF81F) is "the world shows here", untouched by any pass
    vec3 k = floor(c * vec3(31.0, 63.0, 31.0) + 0.5);
    vec4 o = vec4(c, texture2D(uMask, uv).r);
    if (k.r > 30.5 && k.g < 0.5 && k.b > 30.5) o = vec4(0.0, 0.0, 0.0, 1.0);
    // the colour fade, over everything: new = old * (1 - f) + colour * f
    gl_FragColor = vec4(o.rgb * (1.0 - uFade.a) + uFade.rgb * uFade.a, o.a * (1.0 - uFade.a));
}
)";

}  // namespace


// THE PROFILER'S GPU MEMORY (todo/debug-tools.md 5): each texture, buffer and
// renderbuffer this backend specifies - by the object it just specified and
// the size its own arguments give - and each delete. A release build keeps
// only the plain GL calls.
namespace {
using omk::prof::gpuKey;
inline void gpuTexture(const char* tag, GLuint id, long long bytes) {
    OMK_GPU_ALLOC(tag, gpuKey(omk::prof::kGlTexture, id), bytes);
    (void)tag; (void)id; (void)bytes;
}
inline void gpuBuffer(const char* tag, GLuint id, long long bytes) {
    OMK_GPU_ALLOC(tag, gpuKey(omk::prof::kGlBuffer, id), bytes);
    (void)tag; (void)id; (void)bytes;
}
inline void gpuRenderbuffer(const char* tag, GLuint id, long long bytes) {
    OMK_GPU_ALLOC(tag, gpuKey(omk::prof::kGlRenderbuffer, id), bytes);
    (void)tag; (void)id; (void)bytes;
}
inline void deleteTextures(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) OMK_GPU_FREE(gpuKey(omk::prof::kGlTexture, ids[i]));
    glDeleteTextures(n, ids);
}
inline void deleteBuffers(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) OMK_GPU_FREE(gpuKey(omk::prof::kGlBuffer, ids[i]));
    glDeleteBuffers(n, ids);
}
inline void deleteRenderbuffers(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) OMK_GPU_FREE(gpuKey(omk::prof::kGlRenderbuffer, ids[i]));
    glDeleteRenderbuffers(n, ids);
}
}  // namespace

namespace omk {

// Which GL face is the software rasterizer's BACK (a positive area in
// `raster.cpp`) under this backend's projection, glFrontFace left at GL_CCW.
// MEASURED, as Vulkan's `kCullSign` is: the wrong value culls every front
// face and the room turns inside out.
constexpr bool kGlesCullBack = true;

// EXT_texture_filter_anisotropic's two enums, spelled out: GLES2's own header
// does not carry the extension's names
constexpr GLenum kGlTextureMaxAnisotropy = 0x84FE, kGlMaxAnisotropy = 0x84FF;

// THE GL CALLS' OWN TIME, for the frame-phase line (`play.cpp`, 2026-09-18:
// a Vita menu frame cost ~760 ms and nothing said where). Milliseconds summed
// since the last `glesTakeTimings`: glReadPixels, the 888 -> 565 conversion,
// the texture upload of a CPU frame, and the present draw.
namespace {
double g_glesMs[4] = {0, 0, 0, 0};
// Buffer patches since the last report, and how many of them the DEPTH TIE
// made. The split is what named the city's cost: 219 patches a frame on
// Anekbah's street and 150 of them the tie's (`todo/vita-port.md`).
long g_glesPatchesWindow = 0;
long g_glesTiePatchWindow = 0;
bool g_glesInTie = false;
// ONE FRAME'S OWN COUNTS, reset at `begin` - what a SLOW FRAME line quotes
// (`glesFrameReport`). A console's 8.4 s city frame (2026-09-21) had nothing
// in the log to say which GL call it was.
struct GlesFrameCounts {
    double uploadMs = 0, tieMs = 0, drawMs = 0, texMs = 0;
    long uploads = 0, uploadBytes = 0, patches = 0, patchedBuffers = 0, draws = 0, wholeUploads = 0,
         streamed = 0;
} g_glesFrame;
// ...and the same counts summed over the 60-frame window, so a console log at
// 65 ms a frame says what the world submit spent without waiting for a frame
// over half a second (2026-09-30)
GlesFrameCounts g_glesWindow;
// THE PER-DRAW STATE, set and skipped, since the last `glesTakeStateCalls`
// (todo/optimization.md step 17): what the draw-state cache saves.
long g_glesStateSet = 0, g_glesStateSkipped = 0, g_glesDrawsWindow = 0;
// `OMK_UPLOAD_LOG=1`: every WHOLE vertex upload, named by geometry and size -
// which geometries a frame re-sends in full, the cost the console's `gles world`
// line measures (2026-09-30)
bool glesUploadLog() {
    static const bool on = std::getenv("OMK_UPLOAD_LOG") != nullptr;
    return on;
}
// The renderer's own timings (the `gles` line, a slow frame's report): six
// reads a DRAW, ~1500 a street frame. A development build's diagnostics, so
// a release build (`OMK_PROFILE=0`) reads no clock at all and the inlined 0
// folds the arithmetic away (todo/cpu-vs-original.md tier A).
double glesClockMs() {
#if OMK_PROFILE
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    return 0.0;
#endif
}
}  // namespace
long glesTakeOverlayRows(Renderer* r);
void glesTakeTimings(double out[4]) {
    for (int i = 0; i < 4; ++i) { out[i] = g_glesMs[i]; g_glesMs[i] = 0.0; }
}

// Buffer patches since the last call, and the tie's share - on the `gles`
// line every 60 frames, so a console log need not wait for a half-second
// frame to show what the depth tie costs.
long glesTakePatches() {
    const long n = g_glesPatchesWindow;
    g_glesPatchesWindow = 0;
    return n;
}

long glesTakeTiePatches() {
    const long n = g_glesTiePatchWindow;
    g_glesTiePatchWindow = 0;
    return n;
}

// draws, per-draw state calls made and state calls the cache skipped, since
// the last call
void glesTakeStateCalls(long out[3]) {
    out[0] = g_glesDrawsWindow; out[1] = g_glesStateSet; out[2] = g_glesStateSkipped;
    g_glesDrawsWindow = g_glesStateSet = g_glesStateSkipped = 0;
}

// the world frames' counts summed since the last call: uploads, KB SENT,
// upload ms, draw ms, tie ms, and how many of the uploads were WHOLE buffers
void glesTakeWindow(double out[7]) {
    out[5] = static_cast<double>(g_glesWindow.wholeUploads);
    out[6] = static_cast<double>(g_glesWindow.streamed);
    out[0] = static_cast<double>(g_glesWindow.uploads);
    out[1] = static_cast<double>(g_glesWindow.uploadBytes) / 1024.0;
    out[2] = g_glesWindow.uploadMs; out[3] = g_glesWindow.drawMs; out[4] = g_glesWindow.tieMs;
    g_glesWindow = GlesFrameCounts{};
}

// the last world frame's own counts, for a SLOW FRAME line
std::string glesFrameReport() {
    char b[256];
    std::snprintf(b, sizeof b,
                  "gles frame: %ld draws %.0f ms, %ld uploads (%ld KB) %.0f ms, ties %.0f ms, "
                  "%ld buffer patches",
                  g_glesFrame.draws, g_glesFrame.drawMs, g_glesFrame.uploads,
                  g_glesFrame.uploadBytes / 1024, g_glesFrame.uploadMs, g_glesFrame.tieMs,
                  g_glesFrame.patches);
    return b;
}

class GlesRenderer : public Renderer {
public:
    // A geometry the renderer holds state for tells it when it is destroyed
    // (`omk::GpuResidency`, todo/optimization.md step 26).
    GlesRenderer() {
        releaseOn_ = std::getenv("OMK_NO_GEOMETRY_RELEASE") == nullptr;
        addGeometryListener(&GlesRenderer::geometryGone, this);
    }
    ~GlesRenderer() override;
    bool init(int w, int h) override;
    void setTextures(std::span<const Texture> t) override;
    void begin(const View& v) override;
    void submit(const Draw& d) override;
    void end() override;
    const Surface& readback() override;
    RasterStats stats() const override { return st_; }
    const char* name() const override { return "gles2"; }
    // TEXTURE FILTERING (renderer.h): 0 nearest, 1 bilinear - what the
    // original's HARDWARE device drew, MAG/MIN LINEAR with MIP NONE
    // (`docs/ASSETS.md` 4) - and 2 TRILINEAR, the enhancement
    // (`todo/enhancements.md` 2): a mip chain `glGenerateMipmap` builds at
    // upload, sampled LINEAR_MIPMAP_LINEAR. Before `setTextures`: an upload
    // keeps the filter it was made with. The colour key survives all three as
    // on Vulkan - the key is in alpha, the chain averages the (0,0,0,0) key
    // texels so every level stays premultiplied, and the fragment shader
    // un-premultiplies a filtered sample.
    bool setTextureFilter(int mode) override {
        filter_ = mode < 1 ? 0 : mode < 2 ? 1 : 2;
        return true;
    }
    // ANISOTROPY (`todo/enhancements.md` 2): EXT_texture_filter_anisotropic,
    // with trilinear only, as on Vulkan. Recorded here and RESOLVED in `init`,
    // where the context can say whether it has the extension - vitaGL answers
    // a maximum of 1, so on the Vita it is refused there, and said.
    bool setAnisotropy(int n) override {
        aniso_ = n < 1 ? 1 : n > 16 ? 16 : n;
        return true;
    }
    int anisotropy() const { return anisoOn_; }
    // SUPERSAMPLING (`todo/enhancements.md` 9): the world drawn `n` times
    // larger each way and averaged down - in `readback` on the CPU and in the
    // present pass on the GPU, by one rule. Before `init`: the target is made
    // at that size, and a context that cannot hold it gets a smaller factor,
    // said.
    bool setSupersample(int n) override {
        if (ready_) return false;   // too late: the target exists
        ss_ = n < 2 ? 1 : n < 4 ? 2 : 4;
        return true;
    }
    int supersample() const { return ss_; }
    // MULTISAMPLING (`todo/enhancements.md` 0): the world rasterised into a
    // multisampled target and resolved into `colour_` by a blit at `end()`, so
    // every consumer - the readback, the present, the supersample resolve -
    // reads a single-sample picture as before. Before `init`; the context's
    // `GL_MAX_SAMPLES` gets the last word, as the device's limits do on
    // Vulkan.
    bool setMultisample(int samples) override {
        if (ready_) return false;   // too late: the target exists
        wantSamples_ = samples < 2 ? 1 : samples < 4 ? 2 : samples < 8 ? 4 : 8;
        return true;
    }
    int samples() const { return samples_; }
    int maxSamples() const { return maxSamples_; }
    // PER-PIXEL LIGHTING and the MAPPED SHADOW (`todo/enhancements.md` 7 and
    // 6): asked before `init`, which links the enhanced programs only then
    // (`kSceneFragX`) - so a run that asks for neither builds, compiles and
    // caches exactly what it did before. What `init` could build is what the
    // two questions below answer, and the frontend refuses what they deny.
    void setEnhancedLighting(bool perPixel, bool shadowMap) {
        if (ready_) return;
        wantPixLights_ = perPixel;
        wantShadowMap_ = shadowMap;
    }
    bool drawsPixelLights() const override { return pixLights_; }
    bool drawsShadowMap() const override { return shadowMap_; }
    void shadowPass(const View& v, std::span<const Draw> casters) override;

    // ---- presentation, the window side. `frameW x frameH` is the ENGINE's
    // frame (640x480); the window is whatever the device has (960x544 on a
    // Vita). The picture is scaled NEAREST into the largest rectangle of the
    // frame's aspect, or stretched, and the rest is black.
    bool presentWorld(int vy, int vh, int frameW, int frameH, int winW, int winH);
    bool presentSurface(const Surface& s, int winW, int winH);
    bool presentOverlay(const Surface& s, const unsigned char* mask, const unsigned char* maskRows,
                        const float fade[4], int vy, int vh, int winW, int winH);
    void setStretch(bool on) { stretch_ = on; }
    void setDepthTie(bool on) { tieOn_ = on; }
    // Where "the window" is: 0, the default framebuffer, everywhere but the
    // probe, which has no window and points the present pass at an FBO of its
    // own so it can read back what presenting produced.
    void setWindowTarget(GLuint fbo) { windowFbo_ = fbo; }
    // the window's back buffer, top row first - `OMK_VERIFY_GPU_PRESENT`
    void windowPicture(int w, int h, std::vector<unsigned char>& out) {
        std::vector<unsigned char> raw(static_cast<std::size_t>(w) * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, windowFbo_);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
        out.resize(raw.size());
        for (int y = 0; y < h; ++y)
            std::memcpy(out.data() + static_cast<std::size_t>(y) * w * 4,
                        raw.data() + static_cast<std::size_t>(h - 1 - y) * w * 4,
                        static_cast<std::size_t>(w) * 4);
    }

private:
    struct Tex { GLuint id = 0; float w = 1, h = 1; };
    // THE UPLOADED TEXTURES, kept across pool changes and keyed by the pixel
    // STORAGE (2026-09-23). A pool change - one character arriving or leaving,
    // which every cutscene hand-over does - used to delete and re-upload EVERY
    // texture, the city's unchanged ones included, converting each to RGBA on
    // the CPU first. `PixelBuffer` is shared between copies of a `Texture` and
    // detaches before any write, so the same storage IS the same bytes; and
    // `keep` holds that storage alive, so its address cannot be reused for
    // other pixels while the GL texture exists. A NAME would not do: 182
    // names ship with different pixels in different files (CLAUDE.md 6).
    struct Uploaded { GLuint id = 0; int w = 0, h = 0; PixelBuffer keep; bool used = false; };
    // keyed by the storage AND the size: two slots may share one storage at
    // different sizes, and a key on the storage alone let the second upload
    // delete the texture the first slot still drew with
    std::map<std::tuple<const std::uint8_t*, int, int>, Uploaded> uploaded_;
    struct Vbo {
        GLuint id = 0; std::size_t n = 0; std::uint64_t rev = 0;
        // STREAMED (2026-09-30): this frame's corners live in the ring at
        // corner `base`, not in `id`; valid for the frame `streamFrame` only
        bool streamed = false;
        std::size_t base = 0;
        unsigned long streamFrame = 0;
        unsigned long lastWhole = 0;   // the last frame it was sent WHOLE (+1; 0 never)
    };
    // THE STREAMING RING (todo/optimization.md step 28, 2026-09-30). A geometry
    // re-sent WHOLE frame after frame - the sky that follows the camera, the
    // particles, a body posed on the CPU, the shadow quads - cost the console
    // about 1 ms an upload whatever its size: vitaGL's `glBufferSubData` on a
    // buffer drawn in the last frames allocates a new buffer and copies, and a
    // size change is a new `glBufferData`. So such a geometry is written into
    // one ring allocated once, a third of it per presented frame, and drawn
    // from there at an offset; a third is reused only two presented frames
    // later, after the GPU is done with it. The frame is counted on PRESENT,
    // not on `begin` - the mirror pass begins twice a frame. Off while the
    // depth tie is on (it keeps per-buffer state). On by default on the Vita,
    // `OMK_STREAM=1` elsewhere and `OMK_NO_STREAM=1` off.
    static constexpr std::size_t kRingCorners = 49152;   // a third: 1.7 MB of GpuVert
    GLuint ring_ = 0;
    int ringRegion_ = 0;
    std::size_t ringUsed_ = 0;
    // ONE COUNT A FRAME: the first `begin` after a present. Counting presents
    // themselves (the first version) took two a frame wherever a fade or an
    // overlay presents twice - no geometry then looked "sent last frame", so
    // nothing streamed, and the ring's thirds rotated twice a frame, reused
    // after one frame instead of two (the console, 2026-09-30).
    unsigned long presentSeq_ = 1, frameSeq_ = 1, frameFromPresent_ = 0, rotatedAt_ = 0;
    bool streamOn_ = false;
    bool streamToRing(const Geometry* g, Vbo& vb);
public:
    void notePresent() { ++presentSeq_; }
private:

    // `allowStream` false: never into the ring - a LIT draw reads its normals
    // from a buffer of their own at the same corner index, which the ring's
    // offset would break
    bool uploadGeometry(const Geometry* g, bool allowStream = true);
    void setView(const View& view);
public:
    bool drawMirrorScene(const View& v, const View& refl, std::span<const Draw> scene,
                         std::span<const Draw> sceneClipped, std::span<const Draw> mirror) override;
private:
    void resolveTies(const Draw& d, Vbo& vb);
    // Write the tie's applied losers degenerate into `v`, which holds corners
    // [lo, hi] of `g` about to be uploaded - so the buffer keeps holding every
    // loser degenerate and `resolveTies` only writes what is new.
    void foldTies(const Geometry& g, std::vector<GpuVert>& v, std::uint32_t lo, std::uint32_t hi);
    // THE BAKED TIE (`bakeDepthTie`): each geometry's loser triangles, sorted,
    // decided once from its full draw order. A baked geometry skips
    // `resolveTies` - so it may stream - and its losers are MARKED in every
    // upload (`foldBias`) rather than degenerated.
    std::unordered_map<const Geometry*, std::vector<std::uint32_t>> baked_;
    void foldBias(const Geometry& g, GpuVert* v, std::uint32_t lo, std::uint32_t hi) const;
public:
    bool bakeDepthTie(const Geometry* g, std::span<const Draw> order) override;
    long bakedLosers() const {
        long n = 0;
        for (const auto& [g, l] : baked_) n += static_cast<long>(l.size());
        return n;
    }
private:
    void destRect(int picW, int picH, int winW, int winH, float out[4]) const;
    // One picture onto the window: `picW x picH` pixels of a `texW x texH`
    // texture, into the NDC rectangle `dst`.
    void drawPresent(GLuint tex, int picW, int picH, int texW, int texH, bool flipY,
                     bool quantise, const float dst[4], int winW, int winH);
    void drawPresentSS(int vh, const float dst[4], int winW, int winH);

    int w_ = 0, h_ = 0;      // the OUTPUT size - what `readback()` answers with
    int ss_ = 1;             // the supersample factor, 1 off
    int rw_ = 0, rh_ = 0;    // ...and the RENDER size, `w_ * ss_` by `h_ * ss_`
    bool ready_ = false;
    bool allocTarget();      // `colour_` and `depth_` at the render size
    bool allocMultisample(); // ...and the multisampled pair, when asked
    int wantSamples_ = 1, samples_ = 1, maxSamples_ = 1;
    GLuint msFbo_ = 0, msColour_ = 0, msDepth_ = 0;
    // where the scene is RASTERISED: the multisampled target, else the target
    GLuint drawFbo() const { return msFbo_ ? msFbo_ : fbo_; }
    GLuint prog_ = 0, present_ = 0, presentSS_ = 0;
    GLint sDst_ = -1, sPic_ = -1, sBayer_ = -1, sPicSize_ = -1, sTexSize_ = -1, sSS_ = -1,
          sDither_ = -1;
    GLuint overlay_ = 0;
    GLuint maskTex_ = 0;
    // SCRATCH, kept between calls. `uploadGeometry` built a fresh vector every
    // call - a posed body rewrites its whole buffer every frame, so that was an
    // allocation and a free of ~16 KB per body per frame (2026-09-22).
    std::vector<GpuVert> up_;
    std::vector<std::size_t> losers_, restore_;
    GLint oDst_ = -1, oPic_ = -1, oPicSize_ = -1, oMask_ = -1, oFade_ = -1;
    std::vector<std::uint32_t> lastMask_;
    std::vector<std::uint32_t> lastOverlay_;   // a hash a row of what `surfTex_` holds
    std::vector<unsigned char> maskFlagged_, surfFlagged_;
    long overlayRows_ = 0;                     // rows re-sent, for the timing line
    // `presentSurface`'s own sync (todo/optimization.md step 34): how many
    // more frames are copied whole without asking which rows changed, and -
    // under `OMK_PRESENT_CHECK` - what the texture must hold by the rows sent
    int surfSkip_ = 0;
    std::vector<std::uint16_t> surfShadow_;
    long surfCheckFrames_ = 0, surfCheckBad_ = 0, surfRowsSent_ = 0, surfRowsKept_ = 0;
public:
    long takeOverlayRows() { const long r = overlayRows_; overlayRows_ = 0; return r; }
private:
    // THE POSING PROGRAM (todo/gpu-skinning.md) - `kPosedVert` with the
    // scene's fragment stage: the corner moved and lit by its mesh's affine
    GLuint posed_ = 0;
    struct SceneLoc {
        GLint mvp = -1, texSize = -1, clock = -1, tex = -1, cutout = -1,
              fogStart = -1, fogEnd = -1, fogColour = -1, pose = -1,
              light = -1, lightCount = -1, lightBlack = -1;
        GLint wave[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        // the enhanced programs' (`kSceneFragX`); -1 in the default ones
        GLint lit = -1, caster = -1, pl = -1, plCount = -1, shadow = -1, lightMvp = -1,
              shadowP = -1;
    };
    SceneLoc mainLoc_, posedLoc_;
    GLuint curProg_ = 0;
    const float* lastPose_ = nullptr;          // the pose `uPose` holds, per draw
    const Geometry* lastPoseGeo_ = nullptr;    // (reset at `begin`: same pointer, new values)
    std::uint64_t cpuPoseRev_ = 0;
    float mvp_[16] = {};
    // a rest geometry's buffer: uploaded once per revision, and which mesh
    // each slot is
    struct PoseVbo { GLuint id = 0; std::size_t n = 0; std::uint64_t rev = 0; std::vector<int> meshOfSlot;
                     std::vector<float> slotOfCorner; };
    // THE DEPTH TIE OF A POSED BODY. With `tieClass` the corner's MESH, the
    // tie pairs faces by position AND mesh, so its answer is the same in every
    // pose (`Geometry::tieClass`): it is resolved on a private copy of the REST
    // geometry, a new revision each frame that names the last as rigid - the
    // replay the CPU-posed bodies take - and what it degenerates is written
    // into the STATIC buffer once.
    struct PoseTie { Geometry g; std::uint64_t restRev = ~0ull; std::uint64_t frame = 0; DepthTie tie;
                     // the draws resolved THIS frame: many bodies share one rest
                     // geometry (every walker of a model), and the tie's answer is
                     // the same for all of them - so the first resolves it and the
                     // rest are the same call again, which the tie would otherwise
                     // take as the same faces drawn a second time
                     std::vector<std::tuple<std::size_t, std::size_t, bool>> seen; };
    std::unordered_map<const Geometry*, PoseTie> poseTie_;
    std::uint64_t frameNo_ = 0, poseTieRev_ = 0;
    void resolvePosedTies(const Draw& d, PoseVbo& pv);
    std::unordered_map<const Geometry*, PoseVbo> poseVbo_;
    std::vector<GpuPoseVert> poseUp_;
    std::vector<float> poseUni_;
    // a body with more meshes than `kPoseSlots`, posed here - per geometry
    std::unordered_map<const Geometry*, Geometry> cpuPosed_;
    bool uploadPosedGeometry(const Geometry* g, PoseVbo*& out);
    // `uPose`'s values for a posed draw - one affine a slot - into `poseUni_`
    void buildPoseUniforms(const Draw& d, const PoseVbo& pv);
    // A BODY WITH MORE MESHES THAN THE PROGRAM HOLDS, posed here on the CPU
    // into a geometry of its own - and its normals turned with it when the
    // per-pixel light reads them. -> the draw to make instead.
    Draw cpuPose(const Draw& d);
    // ---- the enhancements' state (`setEnhancedLighting`)
    bool wantPixLights_ = false, wantShadowMap_ = false;
    bool pixLights_ = false, shadowMap_ = false;
    // a LIT plain draw's normals, a buffer of their own (GpuVert carries none)
    struct NrmVbo { GLuint id = 0; std::size_t n = 0; std::uint64_t rev = ~std::uint64_t{0}; };
    std::unordered_map<const Geometry*, NrmVbo> nrmVbo_;
    std::vector<float> nrmUp_;
    bool uploadNormals(const Geometry* g);
    // the shadow map: an RGBA8 target holding packed slab depth, its depth
    // buffer, and the two programs that fill it
    static constexpr int kShadowSide = 1024;
    GLuint shFbo_ = 0, shTex_ = 0, shDepth_ = 0, shProg_ = 0, shPosed_ = 0;
    GLint shMvp_ = -1, shPosedMvp_ = -1, shPosedPose_ = -1;
    bool shadowLive_ = false;              // a depth pass was made this frame
    float lightMvp_[16] = {};
    float shadowStrength_ = 0.0f;
    // THE POSING PROGRAM CHECKED ON THE DEVICE (2026-09-26): see the body.
    bool poseSelfTest();
    bool usePosed(const Draw& d) const { return d.meshPose && d.meshPoses && posed_; }
    void useProgram(GLuint p) { if (curProg_ != p) { glUseProgram(p); curProg_ = p; } }

    // THE DRAW-STATE CACHE (todo/optimization.md step 17). `submit` used to set
    // every piece of state a draw needs on every draw - blend, depth mask,
    // texture, seven uniforms, four to six attributes - and on vitaGL each is
    // real work. What the last draw left is remembered here and a call that
    // would set the same value is skipped. It is FORGOTTEN (`forgetState`) at
    // `begin` and wherever GL is touched outside the submit path, so the first
    // draw after either sets everything. Uniforms are per PROGRAM, so each of
    // the two scene programs has its own. `OMK_GLES_NO_STATE_CACHE=1` turns it
    // off, which is how `engine: gles state cache` compares the two.
    struct UniCache {
        bool valid = false;
        float texW = 0, texH = 0, fogStart = 0, fogEnd = 0, fog[3] = {0, 0, 0};
        int cutout = 0;
        float lit = -1, caster = -1;       // the enhanced programs'
        // the posed program's lights
        int lights = -1; float lightBlack = -1; std::vector<float> lightVals;
    };
    struct DrawState {
        int blend = -1;                    // a `Blend`, -1 unknown
        GLuint tex = 0; bool texValid = false;
        GLuint attrBuf = 0; int attrLayout = -1;   // 0 plain, 1 posed; -1 unknown
        int cull = -1;                     // 0 off, 1 GL_BACK, 2 GL_FRONT; -1 unknown
        UniCache uni[2];                   // [0] prog_, [1] posed_
    } ds_;
    bool stateCache_ = true;
    void forgetState() { ds_ = DrawState{}; }

    // THE BUFFERS OF GEOMETRIES THAT ARE GONE (todo/optimization.md step 26).
    // The maps are cleared at once - a new geometry at the same address then
    // starts afresh instead of meeting the old one's buffer and tie - and the
    // GL buffers are deleted at the next `begin`, outside any draw.
    // `OMK_NO_GEOMETRY_RELEASE=1` keeps the old behaviour, to compare.
    bool releaseOn_ = true;
    std::vector<GLuint> deadBufs_;
    long released_ = 0;
    static void geometryGone(void* self, const Geometry* g) {
        auto* r = static_cast<GlesRenderer*>(self);
        if (!r->releaseOn_) return;
        bool had = false;
        if (auto it = r->vbo_.find(g); it != r->vbo_.end()) {
            r->deadBufs_.push_back(it->second.id);
            r->vbo_.erase(it);
            had = true;
        }
        if (auto it = r->poseVbo_.find(g); it != r->poseVbo_.end()) {
            r->deadBufs_.push_back(it->second.id);
            r->poseVbo_.erase(it);
            had = true;
        }
        if (auto it = r->nrmVbo_.find(g); it != r->nrmVbo_.end()) {
            r->deadBufs_.push_back(it->second.id);
            r->nrmVbo_.erase(it);
            had = true;
        }
        had |= r->tie_.erase(g) > 0;
        had |= r->baked_.erase(g) > 0;
        had |= r->poseTie_.erase(g) > 0;
        // a CPU-posed copy is itself uploaded: erasing it notifies again, for
        // its own address, which the lines above then release
        had |= r->cpuPosed_.erase(g) > 0;
        if (had) ++r->released_;
    }
public:
    // for `gles_probe`: the same draws with and without the cache, in one
    // context (`engine: gles state cache`)
    void setStateCache(bool on) { stateCache_ = on; forgetState(); }
    void geometryStats(long out[3]) const {
        out[0] = released_;
        out[1] = static_cast<long>(vbo_.size());
        out[2] = static_cast<long>(poseVbo_.size());
    }
    bool posesBodies() const override { return posed_ != 0; }
    int maxVertexLights() const override { return posed_ ? kVertexLights : 0; }
private:
    // uniform locations, looked up once
    GLint uMvp_ = -1, uTexSize_ = -1, uClock_ = -1, uWave_[8] = {-1, -1, -1, -1, -1, -1, -1, -1},
          uTex_ = -1,
          uCutout_ = -1, uFogStart_ = -1, uFogEnd_ = -1, uFogColour_ = -1;
    GLint pDst_ = -1, pPic_ = -1, pBayer_ = -1, pPicSize_ = -1, pTexSize_ = -1, pFlip_ = -1,
          pDither_ = -1, pQuant_ = -1;
    GLuint fbo_ = 0, colour_ = 0, depth_ = 0;
    GLuint bayer_ = 0, quad_ = 0, surfTex_ = 0;
    int surfW_ = 0, surfH_ = 0;

    std::vector<Tex> tex_;
    Tex white_;
    std::unordered_map<const Geometry*, Vbo> vbo_;
    std::unordered_map<const Geometry*, DepthTie> tie_;
    bool tieOn_ = true;
    int  filter_ = 0;        // 0 nearest (the software devices), 1 bilinear (a 3D card), 2 trilinear
    int  aniso_ = 1;         // asked; 1 is off
    int  anisoOn_ = 1;       // what `init` found the context grants (1: none)
    GLuint windowFbo_ = 0;

    View view_;
    bool flipX_ = false;   // the CURRENT view's screen-X flip - the mirror pass sets it
    bool fog_ = false, dither_ = true, stretch_ = false;
    float fogStart_ = 0, fogEnd_ = 0, fogColour_[3] = {0, 0, 0};
    bool recording_ = false, dirty_ = false;
    Surface fb_{1, 1, 0};
    std::vector<unsigned char> rgba_;
    RasterStats st_;
};

GlesRenderer::~GlesRenderer() {
    // first: the members destroyed below include geometries (`cpuPosed_`)
    removeGeometryListener(&GlesRenderer::geometryGone, this);
    if (!deadBufs_.empty()) deleteBuffers(static_cast<GLsizei>(deadBufs_.size()), deadBufs_.data());
    for (auto& [g, vb] : vbo_) deleteBuffers(1, &vb.id);
    // the pool's ids are owned by `uploaded_` (two slots may share one)
    for (auto& [key, u] : uploaded_) if (u.id) deleteTextures(1, &u.id);
    if (white_.id) deleteTextures(1, &white_.id);
    if (bayer_) deleteTextures(1, &bayer_);
    if (surfTex_) deleteTextures(1, &surfTex_);
    if (quad_) deleteBuffers(1, &quad_);
    if (colour_) deleteTextures(1, &colour_);
    if (depth_) deleteRenderbuffers(1, &depth_);
    if (msColour_) deleteRenderbuffers(1, &msColour_);
    if (msDepth_) deleteRenderbuffers(1, &msDepth_);
    if (msFbo_) glDeleteFramebuffers(1, &msFbo_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (prog_) glDeleteProgram(prog_);
    if (present_) glDeleteProgram(present_);
    if (presentSS_) glDeleteProgram(presentSS_);
    for (auto& [g, pv] : poseVbo_) deleteBuffers(1, &pv.id);
    if (posed_) glDeleteProgram(posed_);
    for (auto& [g, nb] : nrmVbo_) deleteBuffers(1, &nb.id);
    if (shTex_) deleteTextures(1, &shTex_);
    if (shDepth_) deleteRenderbuffers(1, &shDepth_);
    if (shFbo_) glDeleteFramebuffers(1, &shFbo_);
    if (shProg_) glDeleteProgram(shProg_);
    if (shPosed_) glDeleteProgram(shPosed_);
}

bool GlesRenderer::init(int w, int h) {
    w_ = w; h_ = h;
    // the environment can only turn it OFF - a probe's `setStateCache` holds
    if (std::getenv("OMK_GLES_NO_STATE_CACHE")) stateCache_ = false;
    fb_ = Surface(w, h, 0);
    if (ready_) {
        // a resize: only the target changes
        rw_ = w_ * ss_; rh_ = h_ * ss_;
        rgba_.assign(static_cast<std::size_t>(rw_) * rh_ * 4, 0);
        return allocTarget() && (!msFbo_ || allocMultisample());
    }
    // the supersample factor the context can hold: a target past its largest
    // texture or renderbuffer is refused by the driver, so it is halved here
    // until it fits, and said
    if (ss_ > 1) {
        GLint maxTex = 0, maxRb = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRb);
        const int lim = std::min(maxTex, maxRb);
        const int asked = ss_;
        while (ss_ > 1 && (w * ss_ > lim || h * ss_ > lim)) ss_ /= 2;
        if (ss_ != asked)
            std::printf("gles: supersampling %dx does not fit (%dx%d past the context's %d) - %dx\n",
                        asked, w * asked, h * asked, lim, ss_);
    }
    rw_ = w_ * ss_; rh_ = h_ * ss_;
    rgba_.assign(static_cast<std::size_t>(rw_) * rh_ * 4, 0);
    // THE ENHANCED PROGRAMS, in place of the default two when either
    // enhancement is asked for; a context that cannot build them keeps the
    // defaults and refuses both (`drawsPixelLights` / `drawsShadowMap`)
    bool enhanced = false;
    if (wantPixLights_ || wantShadowMap_) {
        prog_ = link(kSceneVertX, kSceneFragX, {{kAttrPos, "aPos"}, {kAttrUV, "aUV"},
                                                {kAttrCol, "aCol"}, {kAttrPhase, "aPhase"},
                                                {kAttrNormal, "aNormal"}});
        if (prog_) {
            posed_ = link(kPosedVertX, kSceneFragX,
                          {{kAttrPos, "aPos"}, {kAttrUV, "aUV"}, {kAttrCol, "aCol"},
                           {kAttrPhase, "aPhase"}, {kAttrSlot, "aSlot"}, {kAttrNormal, "aNormal"}});
            if (!posed_) std::fprintf(stderr, "gles: no enhanced posing program - bodies are posed on the CPU\n");
            enhanced = true;
        } else {
            std::printf("gles: the enhanced scene program did not build - per-pixel lighting and "
                        "mapped shadows refused\n");
        }
    }
    if (!prog_)
        prog_ = link(kSceneVert, kSceneFrag, {{kAttrPos, "aPos"}, {kAttrUV, "aUV"},
                                              {kAttrCol, "aCol"}, {kAttrPhase, "aPhase"}});
    present_ = link(kPresentVert, kPresentFrag, {{0, "aPos"}});
    if (!prog_ || !present_) return false;
    // ALL THREE PROGRAMS AT START: a shader first linked mid-game would be
    // missing from a shader cache made by one short run (`todo/vita-port.md`,
    // the precompiled shaders). Its uniforms are still looked up on first use.
    overlay_ = link(kPresentVert, kOverlayFrag, {{0, "aPos"}});
    if (!enhanced) {
        // at start with the others, for the shader cache (above); a context
        // that cannot build it simply poses on the CPU
        posed_ = link(kPosedVert, kSceneFrag,
                      {{kAttrPos, "aPos"}, {kAttrUV, "aUV"}, {kAttrCol, "aCol"},
                       {kAttrPhase, "aPhase"}, {kAttrSlot, "aSlot"}, {kAttrNormal, "aNormal"}});
        if (!posed_) std::fprintf(stderr, "gles: no posing program - bodies are posed on the CPU\n");
    }
    uMvp_       = glGetUniformLocation(prog_, "uMvp");
    uTexSize_   = glGetUniformLocation(prog_, "uTexSize");
    uClock_     = glGetUniformLocation(prog_, "uShimmerClock");
    for (int k = 0; k < 8; ++k) {
        const std::string n = "uWave" + std::to_string(k);
        uWave_[k] = glGetUniformLocation(prog_, n.c_str());
    }
    uTex_       = glGetUniformLocation(prog_, "uTex");
    uCutout_    = glGetUniformLocation(prog_, "uCutout");
    uFogStart_  = glGetUniformLocation(prog_, "uFogStart");
    uFogEnd_    = glGetUniformLocation(prog_, "uFogEnd");
    uFogColour_ = glGetUniformLocation(prog_, "uFogColour");
    const auto locsOf = [](GLuint p, SceneLoc& L) {
        L.mvp = glGetUniformLocation(p, "uMvp");
        L.texSize = glGetUniformLocation(p, "uTexSize");
        L.clock = glGetUniformLocation(p, "uShimmerClock");
        for (int k = 0; k < 8; ++k)
            L.wave[k] = glGetUniformLocation(p, ("uWave" + std::to_string(k)).c_str());
        L.tex = glGetUniformLocation(p, "uTex");
        L.cutout = glGetUniformLocation(p, "uCutout");
        L.fogStart = glGetUniformLocation(p, "uFogStart");
        L.fogEnd = glGetUniformLocation(p, "uFogEnd");
        L.fogColour = glGetUniformLocation(p, "uFogColour");
        L.pose = glGetUniformLocation(p, "uPose");
        L.light = glGetUniformLocation(p, "uLight");
        L.lightCount = glGetUniformLocation(p, "uLightCount");
        L.lightBlack = glGetUniformLocation(p, "uLightBlack");
        L.lit = glGetUniformLocation(p, "uLit");
        L.caster = glGetUniformLocation(p, "uCaster");
        L.pl = glGetUniformLocation(p, "uPL");
        L.plCount = glGetUniformLocation(p, "uPLCount");
        L.shadow = glGetUniformLocation(p, "uShadow");
        L.lightMvp = glGetUniformLocation(p, "uLightMvp");
        L.shadowP = glGetUniformLocation(p, "uShadowP");
    };
    locsOf(prog_, mainLoc_);
    if (posed_) locsOf(posed_, posedLoc_);
    pDst_     = glGetUniformLocation(present_, "uDst");
    pPic_     = glGetUniformLocation(present_, "uPic");
    pBayer_   = glGetUniformLocation(present_, "uBayer");
    pPicSize_ = glGetUniformLocation(present_, "uPicSize");
    pTexSize_ = glGetUniformLocation(present_, "uTexSize");
    pFlip_    = glGetUniformLocation(present_, "uFlipY");
    pDither_  = glGetUniformLocation(present_, "uDither");
    pQuant_   = glGetUniformLocation(present_, "uQuantise");

    // The shimmer table, once: `kShimmerWave / 255`, the same values
    // `shimmerOffset` returns.
    glUseProgram(prog_);
    float wave[32];
    for (int i = 0; i < 32; ++i) wave[i] = static_cast<float>(kShimmerWave[i]) / 255.0f;
    for (int k = 0; k < 8; ++k) glUniform4fv(uWave_[k], 1, wave + 4 * k);
    glUniform1i(uTex_, 0);
    if (mainLoc_.shadow >= 0) glUniform1i(mainLoc_.shadow, 2);   // the shadow map's unit
    if (posed_) {
        glUseProgram(posed_);
        for (int k = 0; k < 8; ++k) glUniform4fv(posedLoc_.wave[k], 1, wave + 4 * k);
        glUniform1i(posedLoc_.tex, 0);
        if (posedLoc_.shadow >= 0) glUniform1i(posedLoc_.shadow, 2);
        glUseProgram(prog_);
    }
    if (enhanced) {
        pixLights_ = wantPixLights_;
        if (wantShadowMap_) {
            // THE SHADOW MAP: 1024 x 1024 as Vulkan's, RGBA8 holding packed
            // depth, NEAREST (the PCF is the shader's own nine taps)
            shProg_ = link(kShadowVert, kShadowFrag, {{kAttrPos, "aPos"}});
            if (posed_) shPosed_ = link(kShadowPosedVert, kShadowFrag, {{kAttrPos, "aPos"}, {kAttrSlot, "aSlot"}});
            if (shProg_) {
                shMvp_ = glGetUniformLocation(shProg_, "uLightMvp");
                if (shPosed_) {
                    shPosedMvp_ = glGetUniformLocation(shPosed_, "uLightMvp");
                    shPosedPose_ = glGetUniformLocation(shPosed_, "uPose");
                }
                glGenTextures(1, &shTex_);
                glBindTexture(GL_TEXTURE_2D, shTex_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kShadowSide, kShadowSide, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, nullptr);
gpuTexture("render targets", shTex_, 4LL * kShadowSide * kShadowSide);
                glGenRenderbuffers(1, &shDepth_);
                glBindRenderbuffer(GL_RENDERBUFFER, shDepth_);
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, kShadowSide, kShadowSide);
gpuRenderbuffer("render targets", shDepth_, 2LL * kShadowSide * kShadowSide);
                glGenFramebuffers(1, &shFbo_);
                glBindFramebuffer(GL_FRAMEBUFFER, shFbo_);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shTex_, 0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, shDepth_);
                shadowMap_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            if (!shadowMap_) std::printf("gles: the shadow map did not build - mapped shadows refused\n");
        }
        std::printf("gles: enhanced programs - per-pixel lighting %s, mapped shadows %s\n",
                    pixLights_ ? "on" : "off", shadowMap_ ? "on" : "off");
    }

    // THE RENDER TARGET. Offscreen, at the ENGINE's size, because three
    // things need the picture before the window does: `readback()`, the
    // present pass's 565 quantisation, and the scale onto a window that is not
    // 640x480. Depth is 16-bit, the one depth format GLES2 guarantees for a
    // renderbuffer; the engine's own z-buffer was 16-bit too, and the near
    // plane at 1 keeps it usable. (If the depth tie turns out to be needed
    // BECAUSE of 16 bits, `OES_depth24` is the lever - `todo/vita-port.md`.)
    glGenTextures(1, &colour_);
    glBindTexture(GL_TEXTURE_2D, colour_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &depth_);
    glGenFramebuffers(1, &fbo_);
    if (!allocTarget()) return false;
    if (wantSamples_ > 1) {
#if OMK_GLES_MSAA
        GLint most = 1;
        glGetIntegerv(GL_MAX_SAMPLES, &most);
        maxSamples_ = most;
        int n = wantSamples_;
        while (n > 1 && n > most) n >>= 1;
        if (n > 1) {
            samples_ = n;
            glGenFramebuffers(1, &msFbo_);
            glGenRenderbuffers(1, &msColour_);
            glGenRenderbuffers(1, &msDepth_);
            if (!allocMultisample()) {
                deleteRenderbuffers(1, &msColour_);
                deleteRenderbuffers(1, &msDepth_);
                glDeleteFramebuffers(1, &msFbo_);
                msFbo_ = msColour_ = msDepth_ = 0;
                samples_ = 1;
            }
        }
        if (samples_ != wantSamples_)
            std::printf("gles: %dx MSAA is not available here (the context allows %d) - using %dx\n",
                        wantSamples_, most, samples_);
#else
        std::printf("gles: %dx MSAA refused - this GL has no multisampled render target "
                    "(vitaGL multisamples only the display); supersampling is the anti-aliasing here\n",
                    wantSamples_);
#endif
        if (samples_ > 1) std::printf("gles: %dx MSAA\n", samples_);
    }
    if (ss_ > 1) presentSS_ = link(kPresentVert, kPresentSSFrag, {{0, "aPos"}});
    if (ss_ > 1 && !presentSS_) {
        // a console with neither the runtime shader compiler nor this program
        // in its cache: the enhancement is refused, not the renderer
        std::printf("gles: the supersample present did not build - supersampling refused\n");
        ss_ = 1;
        rw_ = w_; rh_ = h_;
        rgba_.assign(static_cast<std::size_t>(rw_) * rh_ * 4, 0);
        if (!allocTarget()) return false;
    }
    if (ss_ > 1) {
        sDst_     = glGetUniformLocation(presentSS_, "uDst");
        sPic_     = glGetUniformLocation(presentSS_, "uPic");
        sBayer_   = glGetUniformLocation(presentSS_, "uBayer");
        sPicSize_ = glGetUniformLocation(presentSS_, "uPicSize");
        sTexSize_ = glGetUniformLocation(presentSS_, "uTexSize");
        sSS_      = glGetUniformLocation(presentSS_, "uSS");
        sDither_  = glGetUniformLocation(presentSS_, "uDither");
        std::printf("gles: supersampling %dx - the world drawn at %dx%d\n", ss_, rw_, rh_);
    }

    // the white 1x1 a material with no texture binds - raster.cpp leaves the
    // texel at 255 and lets the vertex colour stand alone
    const unsigned char wpx[4] = {255, 255, 255, 255};
    glGenTextures(1, &white_.id);
    glBindTexture(GL_TEXTURE_2D, white_.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wpx);
gpuTexture("textures", white_.id, 4);
    // the shadow map's unit holds the white texel until a depth pass fills
    // it, so no draw - the posing self-test's included - samples nothing
    if (mainLoc_.shadow >= 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, white_.id);
        glActiveTexture(GL_TEXTURE0);
    }

    // the dither matrix - `omk::kBayer4`, the one the software dither and the
    // readback use - as a texture, so the fragment stage needs no array
    unsigned char bl[16 * 4];
    for (int i = 0; i < 16; ++i)
        bl[4 * i] = bl[4 * i + 1] = bl[4 * i + 2] = bl[4 * i + 3] =
            static_cast<unsigned char>(kBayer4[i]);
    glGenTextures(1, &bayer_);
    glBindTexture(GL_TEXTURE_2D, bayer_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, bl);
gpuTexture("textures", bayer_, 64);

    const float quad[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &quad_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
gpuBuffer("vertex buffers", quad_, sizeof quad);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // THE ANISOTROPY the context grants: the extension named, and its own
    // maximum. Only behind trilinear, as Vulkan asks for the device feature
    // only then; a refusal is SAID, the way `vulkan: anisotropy ignored` is.
    anisoOn_ = 1;
    if (aniso_ > 1) {
        const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        GLfloat most = 1.0f;
        const bool have = ext && std::strstr(ext, "GL_EXT_texture_filter_anisotropic");
        if (have) glGetFloatv(kGlMaxAnisotropy, &most);
        if (filter_ < 2)
            std::printf("gles: anisotropy %d ignored - it needs trilinear filtering (a mip chain)\n", aniso_);
        else if (!have || most < 2.0f)
            std::printf("gles: anisotropy %d ignored - the context has no anisotropic filtering%s\n",
                        aniso_, have ? " beyond 1x" : "");
        else
            anisoOn_ = std::min(aniso_, static_cast<int>(most));
        if (anisoOn_ > 1) std::printf("gles: anisotropy %dx\n", anisoOn_);
    }
    static const bool noTie = std::getenv("OMK_NO_TIE") != nullptr;
    if (noTie) tieOn_ = false;
#if defined(__vita__)
    streamOn_ = std::getenv("OMK_NO_STREAM") == nullptr;
#else
    streamOn_ = std::getenv("OMK_STREAM") != nullptr;
#endif
    if (posed_ && !poseSelfTest()) {
        glDeleteProgram(posed_);
        posed_ = 0;
    }
    ready_ = true;
    return true;
}

// THE RENDER TARGET's storage, at the RENDER size - the output size times the
// supersample factor. A context that cannot allocate a supersampled target
// (the Vita's memory, a driver's limit) gets the factor halved, said, until it
// can; at 1x a failure is the caller's.
bool GlesRenderer::allocTarget() {
    for (;;) {
        while (glGetError() != GL_NO_ERROR) {}
        glBindTexture(GL_TEXTURE_2D, colour_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rw_, rh_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
gpuTexture("render targets", colour_, 4LL * rw_ * rh_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, rw_, rh_);
gpuRenderbuffer("render targets", depth_, 2LL * rw_ * rh_);
        const bool oom = glGetError() == GL_OUT_OF_MEMORY;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
        const GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!oom && fbs == GL_FRAMEBUFFER_COMPLETE) return true;
        if (ss_ <= 1) {
            std::fprintf(stderr, "gles: render target incomplete (0x%x)%s\n", fbs,
                         oom ? ", out of memory" : "");
            return false;
        }
        std::printf("gles: supersampling %dx refused (%s at %dx%d) - %dx\n", ss_,
                    oom ? "out of memory" : "target incomplete", rw_, rh_, ss_ / 2);
        ss_ /= 2;
        rw_ = w_ * ss_; rh_ = h_ * ss_;
        rgba_.assign(static_cast<std::size_t>(rw_) * rh_ * 4, 0);
    }
}

// THE MULTISAMPLED PAIR, at the render size: colour and depth renderbuffers
// the scene rasterises into, resolved into `colour_` at `end()`. Depth 16-bit
// as the single-sample target's, 24 where a driver will not multisample 16.
bool GlesRenderer::allocMultisample() {
#if OMK_GLES_MSAA
    while (glGetError() != GL_NO_ERROR) {}
    glBindRenderbuffer(GL_RENDERBUFFER, msColour_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, rw_, rh_);
gpuRenderbuffer("render targets", msColour_, 4LL * samples_ * rw_ * rh_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColour_);
    GLenum fbs = GL_FRAMEBUFFER_UNSUPPORTED;
    for (const GLenum df : {GLenum(GL_DEPTH_COMPONENT16), GLenum(GL_DEPTH_COMPONENT24)}) {
        glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, df, rw_, rh_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
        fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbs == GL_FRAMEBUFFER_COMPLETE) {
gpuRenderbuffer("render targets", msDepth_, (df == GL_DEPTH_COMPONENT16 ? 2LL : 4LL) * samples_ * rw_ * rh_);
            break;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (fbs == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR) return true;
    std::printf("gles: %dx MSAA target refused (0x%x)\n", samples_, fbs);
#endif
    return false;
}

// THE POSING PROGRAM, CHECKED WHERE IT RUNS (2026-09-26). On the Mac a posed
// body matches the CPU's to a few pixels (`engine: gles pose`); on the reader's
// Vita the cutscene characters came out with "hands, feet and head placed way
// too far from the body" - a GPU and a shader compiler this repo cannot run.
// So the program proves itself at start-up, on the device: 32 thin bars, one
// per SLOT, each coloured by its slot and each slot's affine moving its bar to
// its own cell of an 8 x 4 grid - once as a pure translation, once turned 90
// degrees. The read-back says, cell by cell, WHICH slot arrived there and
// whether its bar was turned, and a program that gets any wrong is dropped:
// the bodies are then posed on the CPU, as before any of this, and the log
// says what the GPU did - the evidence the fix needs.
bool GlesRenderer::poseSelfTest() {
    constexpr int kCols = 8, kRows = 4, kN = kCols * kRows;
    static_assert(kN == kPoseSlots, "one bar a slot");
    const float q = 0.05f;               // the bar's half-length, NDC
    std::vector<GpuPoseVert> v;
    v.reserve(kN * 6);
    for (int k = 0; k < kN; ++k) {
        const float r = static_cast<float>(k + 1) / 40.0f;
        const float xs[6] = {-q, q, q, -q, q, -q};
        const float ys[6] = {-q * 0.25f, -q * 0.25f, q * 0.25f, -q * 0.25f, q * 0.25f, q * 0.25f};
        for (int c = 0; c < 6; ++c) {
            GpuPoseVert p{};
            p.v = GpuVert{xs[c], ys[c], 0.0f, 0.0f, 0.0f, r, 1.0f, 0.0f, -1.0f};
            p.slot = static_cast<float>(k);
            p.nx = 0.0f; p.ny = 0.0f; p.nz = 1.0f;
            v.push_back(p);
        }
    }
    GLuint vb = 0;
    glGenBuffers(1, &vb);
    glBindBuffer(GL_ARRAY_BUFFER, vb);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(GpuPoseVert)),
                 v.data(), GL_STATIC_DRAW);
gpuBuffer("posed bodies", vb, static_cast<long long>(v.size() * sizeof(GpuPoseVert)));
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(posed_);
    curProg_ = posed_;
    const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(posedLoc_.mvp, 1, GL_FALSE, ident);
    glUniform1f(posedLoc_.clock, 0.0f);
    glUniform2f(posedLoc_.texSize, 1.0f, 1.0f);
    glUniform1i(posedLoc_.cutout, 0);
    glUniform1f(posedLoc_.fogStart, 0.0f);
    glUniform1f(posedLoc_.fogEnd, 0.0f);
    glUniform3f(posedLoc_.fogColour, 0.0f, 0.0f, 0.0f);
    glUniform1f(posedLoc_.lightCount, 0.0f);
    glUniform1f(posedLoc_.lightBlack, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, white_.id);
    const GLsizei st = sizeof(GpuPoseVert);
    for (const GLuint a : {kAttrPos, kAttrUV, kAttrCol, kAttrPhase, kAttrSlot, kAttrNormal})
        glEnableVertexAttribArray(a);
    glVertexAttribPointer(kAttrPos, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(0));
    glVertexAttribPointer(kAttrUV, 2, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(12));
    glVertexAttribPointer(kAttrCol, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(20));
    glVertexAttribPointer(kAttrPhase, 1, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(32));
    glVertexAttribPointer(kAttrSlot, 1, GL_FLOAT, GL_FALSE, st,
                          reinterpret_cast<const void*>(offsetof(GpuPoseVert, slot)));
    glVertexAttribPointer(kAttrNormal, 3, GL_FLOAT, GL_FALSE, st,
                          reinterpret_cast<const void*>(offsetof(GpuPoseVert, nx)));
    std::vector<unsigned char> px(static_cast<std::size_t>(w_) * h_ * 4);
    const auto at = [&](float nx, float ny) -> const unsigned char* {
        int x = static_cast<int>((nx + 1.0f) * 0.5f * static_cast<float>(w_));
        int y = static_cast<int>((ny + 1.0f) * 0.5f * static_cast<float>(h_));
        x = std::clamp(x, 0, w_ - 1);
        y = std::clamp(y, 0, h_ - 1);
        return px.data() + (static_cast<std::size_t>(y) * w_ + x) * 4;
    };
    int placed[2] = {0, 0};
    std::string report;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> aff(static_cast<std::size_t>(kN) * 16, 0.0f);   // four rows a slot
        for (int k = 0; k < kN; ++k) {
            const float cx = -1.0f + 2.0f * (static_cast<float>(k % kCols) + 0.5f) / kCols;
            const float cy = -1.0f + 2.0f * (static_cast<float>(k / kCols) + 0.5f) / kRows;
            float* o = aff.data() + 16 * k;
            if (pass == 0) { o[0] = 1; o[5] = 1; }             // as it is
            else { o[1] = -1; o[4] = 1; }                      // turned 90 degrees
            o[10] = 1;
            o[3] = cx; o[7] = cy;
        }
        glUniform4fv(posedLoc_.pose, kN * 4, aff.data());
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, kN * 6);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int k = 0; k < kN; ++k) {
            const float cx = -1.0f + 2.0f * (static_cast<float>(k % kCols) + 0.5f) / kCols;
            const float cy = -1.0f + 2.0f * (static_cast<float>(k / kCols) + 0.5f) / kRows;
            const unsigned char* c = at(cx, cy);
            // the bar's far end along its own length, and across it: a
            // horizontal bar lights the first and not the second
            const unsigned char* along = pass == 0 ? at(cx + 0.7f * q, cy) : at(cx, cy + 0.7f * q);
            const unsigned char* across = pass == 0 ? at(cx, cy + 0.7f * q) : at(cx + 0.7f * q, cy);
            const int got = c[1] > 128 ? static_cast<int>(std::lround(c[0] * 40.0 / 255.0)) - 1 : -1;
            const bool turned = along[1] > 128 && across[1] <= 128;
            if (got == k && turned) { ++placed[pass]; continue; }
            if (report.size() < 300) {
                char b[96];
                std::snprintf(b, sizeof b, " %s slot %d: %s%s;", pass ? "turned" : "moved", k,
                              got < 0 ? "nothing in its cell" : ("slot " + std::to_string(got) +
                                                                 " in its cell").c_str(),
                              turned ? "" : ", bar the wrong way");
                report += b;
            }
        }
    }
    for (const GLuint a : {kAttrSlot, kAttrNormal}) glDisableVertexAttribArray(a);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    deleteBuffers(1, &vb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    curProg_ = 0;
    const bool ok = placed[0] == kN && placed[1] == kN;
    std::printf("gles: pose self-test - %d of %d slots moved, %d of %d turned%s%s\n",
                placed[0], kN, placed[1], kN,
                ok ? " - the renderer poses bodies" :
                     " - FAILED, bodies are posed on the CPU:", ok ? "" : report.c_str());
    return ok;
}

void GlesRenderer::setTextures(std::span<const Texture> t) {
    const double t0 = glesClockMs();
    forgetState();
    for (auto& [key, u] : uploaded_) u.used = false;
    tex_.assign(t.size(), Tex{});
    std::vector<unsigned char> px;
    int reused = 0, fresh = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const auto& s = t[i];
        if (!s.hasPixels()) continue;
        auto& out = tex_[i];
        out.w = static_cast<float>(s.width);
        out.h = static_cast<float>(s.height);
        // already on the GPU from an earlier pool: the same storage, the same bytes
        const auto key = std::make_tuple(s.idx.data(), s.width, s.height);
        const auto hit = uploaded_.find(key);
        if (hit != uploaded_.end()) {
            hit->second.used = true;
            out.id = hit->second.id;
            ++reused;
            continue;
        }
        const int n = s.width * s.height;
        px.resize(static_cast<std::size_t>(n) * 4);
        // alpha carries the COLOUR KEY, exactly as the Vulkan upload does:
        // 0 where the texel is black, 255 elsewhere (`formats/tex3dt.h`, NEON
        // where the platform has it)
        s.toRgbaKeyed(px.data());
        // REPEAT is the engine's addressing (the sampler Vulkan builds), and
        // GLES2 allows it only on power-of-two textures. All 2534 shipped under
        // MESHES are (max 256x256, measured 2026-09-18); a texture that is not
        // - a set-piece model out of an SCX stream, say - is clamped and SAID.
        const auto pot = [](int v) { return v > 0 && !(v & (v - 1)); };
        const bool repeat = pot(s.width) && pot(s.height);
        if (!repeat)
            std::fprintf(stderr, "gles: texture %s is %dx%d, not a power of two - "
                                 "clamped, where the engine repeats\n",
                         s.name.c_str(), s.width, s.height);
        glGenTextures(1, &out.id);
        glBindTexture(GL_TEXTURE_2D, out.id);
        // THE MIP CHAIN (trilinear): GLES2 builds one only for a power-of-two
        // texture, which is every one the engine repeats - a clamped one keeps
        // bilinear, as said above for its addressing
        const bool mips = filter_ >= 2 && repeat;
        const GLint f = filter_ ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        if (mips && anisoOn_ > 1)
            glTexParameterf(GL_TEXTURE_2D, kGlTextureMaxAnisotropy, static_cast<GLfloat>(anisoOn_));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.width, s.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, px.data());
        if (mips) glGenerateMipmap(GL_TEXTURE_2D);
gpuTexture("textures", out.id, (mips ? 16LL : 12LL) * s.width * s.height / 3);
        uploaded_[key] = Uploaded{out.id, s.width, s.height, s.idx, true};
        ++fresh;
    }
    // what no slot of this pool uses any more leaves the GPU
    int dropped = 0;
    for (auto it = uploaded_.begin(); it != uploaded_.end(); ) {
        if (it->second.used) { ++it; continue; }
        deleteTextures(1, &it->second.id);
        it = uploaded_.erase(it);
        ++dropped;
    }
    std::printf("gles: texture pool of %zu - %d kept on the GPU, %d uploaded, %d dropped, %.1f ms\n",
                t.size(), reused, fresh, dropped, glesClockMs() - t0);
}

// A SMALL WRITE INTO THE BOUND ARRAY BUFFER. On a host it is glBufferSubData.
// On vitaGL that call, for any buffer drawn in the last frames, ALLOCATES A
// NEW BUFFER OF THE WHOLE SIZE AND COPIES THE OLD ONE INTO IT
// (`glNamedBufferSubData`, buffers.c) - so the depth tie's three-vertex patches
// into Anekbah's 5 MB set buffer, made before each of its 21 batches, cost up
// to 21 whole copies and 21 allocations a frame, freed only frames later.
// `glMapBuffer` hands out the buffer's own memory, so the patch is written in
// place. The GPU may still be reading that buffer: a tie patch touches only
// its own batch's range, which this frame has not submitted yet, and last
// frame's scene has been kicked.
static void patchArrayBuffer(std::size_t offset, std::size_t size, const void* data) {
    ++g_glesFrame.patches;
    ++g_glesPatchesWindow;
    if (g_glesInTie) ++g_glesTiePatchWindow;
#if defined(__vita__)
    if (void* base = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) {
        std::memcpy(static_cast<unsigned char*>(base) + offset, data, size);
        glUnmapBuffer(GL_ARRAY_BUFFER);
        return;
    }
#endif
    glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(size), data);
}

bool GlesRenderer::streamToRing(const Geometry* g, Vbo& vb) {
    const std::size_t n = g->corners.size();
    if (rotatedAt_ != frameSeq_) {               // a new presented frame: the next third
        rotatedAt_ = frameSeq_;
        ringRegion_ = (ringRegion_ + 1) % 3;
        ringUsed_ = 0;
    }
    if (ringUsed_ + n > kRingCorners) return false;   // this frame's third is full
    if (!ring_) {
        glGenBuffers(1, &ring_);
        glBindBuffer(GL_ARRAY_BUFFER, ring_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(3 * kRingCorners * sizeof(GpuVert)),
                     nullptr, GL_DYNAMIC_DRAW);
gpuBuffer("vertex buffers", ring_, 3LL * kRingCorners * sizeof(GpuVert));
    }
    std::vector<GpuVert>& v = up_;
    v.resize(n);
    for (std::size_t k = 0; k < n; ++k) v[k] = gpuVert(g->corners[k]);
    foldBias(*g, v.data(), 0, static_cast<std::uint32_t>(n - 1));
    const std::size_t base = static_cast<std::size_t>(ringRegion_) * kRingCorners + ringUsed_;
    glBindBuffer(GL_ARRAY_BUFFER, ring_);
    ds_.attrBuf = 0;                                // the draws re-point their attributes
#if defined(__vita__)
    // the ring's own memory: a third the GPU finished with two frames ago
    if (void* mem = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY)) {
        std::memcpy(static_cast<unsigned char*>(mem) + base * sizeof(GpuVert), v.data(), n * sizeof(GpuVert));
        glUnmapBuffer(GL_ARRAY_BUFFER);
    } else
#endif
    glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(base * sizeof(GpuVert)),
                    static_cast<GLsizeiptr>(n * sizeof(GpuVert)), v.data());
    ringUsed_ += n;
    vb.streamed = true;
    vb.base = base;
    vb.streamFrame = frameSeq_;
    vb.lastWhole = frameSeq_;
    vb.rev = g->revision;
    ++g_glesFrame.uploads;
    ++g_glesFrame.streamed;
    g_glesFrame.uploadBytes += static_cast<long>(n * sizeof(GpuVert));
    if (glesUploadLog()) std::printf("  [upload] streamed %p %zu corners at %zu\n",
                                     static_cast<const void*>(g), n, base);
    return true;
}

bool GlesRenderer::uploadGeometry(const Geometry* g, bool allowStream) {
    // The Vulkan backend's contract, verbatim in intent: cached by POINTER
    // while the revision holds; the same object with new vertices refills in
    // place, and when the geometry names the corners that moved since the
    // revision this buffer holds, only those are written.
    auto it = vbo_.find(g);
    if (it != vbo_.end() && it->second.rev == g->revision) {
        if (!it->second.streamed || (allowStream && it->second.streamFrame == frameSeq_)) return true;
        // streamed corners are one frame's: unchanged since, it goes back to
        // its own buffer (a whole refill below)
        it->second.streamed = false;
        it->second.rev = 0;
    }
    if (g->corners.empty()) return false;
    if (streamOn_ && allowStream && (!tieOn_ || baked_.count(g)) && it != vbo_.end() && it->second.lastWhole + 1 >= frameSeq_ &&
        !(g->dirtyTo != 0 && g->dirtyTo == g->revision && it->second.rev == g->dirtyFrom &&
          it->second.n == g->corners.size()) &&
        streamToRing(g, it->second))
        return true;
    if (it != vbo_.end()) it->second.streamed = false;
    std::vector<GpuVert>& v = up_;
    v.clear();
    ++g_glesFrame.uploads;
    // the bytes SENT, counted where they are sent: a partial upload used to be
    // booked here at the whole geometry's size, so the set's 69-run patch read
    // as a 5 MB upload every frame (2026-09-30)
    if (it != vbo_.end() && it->second.n == g->corners.size()) {
        Vbo& vb = it->second;
        static const bool noDirty = std::getenv("OMK_NO_DIRTY") != nullptr;
        const bool partial = !noDirty && g->dirtyTo != 0 && g->dirtyTo == g->revision &&
                             vb.rev == g->dirtyFrom;
        glBindBuffer(GL_ARRAY_BUFFER, vb.id);
        // `OMK_DIRTY_AUDIT` (todo/optimization.md step 25): is the dirty list
        // COMPLETE? A copy of the corners last uploaded, per buffer, and on a
        // partial upload every corner the list does not name must be what it
        // was. An instrument - off, it costs one static bool.
        static const bool audit = std::getenv("OMK_DIRTY_AUDIT") != nullptr;
        if (audit) {
            static std::unordered_map<const Geometry*, std::vector<Corner>> shadow;
            auto& sh = shadow[g];
            if (partial && sh.size() == g->corners.size()) {
                std::vector<char> listed(g->corners.size(), 0);
                for (const std::uint32_t c : g->dirtyCorners) if (c < listed.size()) listed[c] = 1;
                long missed = 0, pos = 0, uv = 0, col = 0, other = 0;
                std::size_t first = ~std::size_t{0};
                for (std::size_t k = 0; k < g->corners.size(); ++k) {
                    if (listed[k] || std::memcmp(&sh[k], &g->corners[k], sizeof(Corner)) == 0) continue;
                    ++missed;
                    if (first == ~std::size_t{0}) first = k;
                    const Corner& a = sh[k]; const Corner& b = g->corners[k];
                    if (a.x != b.x || a.y != b.y || a.z != b.z) ++pos;
                    else if (a.u != b.u || a.v != b.v) ++uv;
                    else if (a.r != b.r || a.g != b.g || a.b != b.b) ++col;
                    else ++other;
                }
                {
                    long descents = 0;
                    for (std::size_t k = 1; k < g->dirtyCorners.size(); ++k)
                        descents += g->dirtyCorners[k] < g->dirtyCorners[k - 1];
                    if (descents)
                        std::printf("dirty audit: geometry %p rev %llu: the list is NOT sorted - "
                                    "%ld descents in %zu corners\n", static_cast<const void*>(g),
                                    static_cast<unsigned long long>(g->revision), descents,
                                    g->dirtyCorners.size());
                }
                if (missed)
                    std::printf("dirty audit: geometry %p rev %llu: %ld corners changed and NOT listed "
                                "(%zu listed) - position %ld, uv %ld, colour %ld, other %ld; first %zu "
                                "(mesh %d)\n", static_cast<const void*>(g),
                                static_cast<unsigned long long>(g->revision), missed,
                                g->dirtyCorners.size(), pos, uv, col, other, first,
                                first < g->cornerMesh.size() ? g->cornerMesh[first] : -1);
            }
            sh = g->corners;
        }
        if (partial) {
            // one call a corner would be thousands of driver calls for a
            // walker, so coalesce the list into runs of consecutive corners
            // and send each run once. The list is NOT sorted - `play.cpp`
            // builds it mesh by mesh, ~20 descents a street frame
            // (`OMK_DIRTY_AUDIT`) - which is fine here, since each run is
            // written wherever it lies; a merge that assumed it was sorted
            // skipped corners (todo/optimization.md step 25)
            const auto& dc = g->dirtyCorners;
            std::size_t i = 0;
            while (i < dc.size()) {
                std::size_t j = i;
                while (j + 1 < dc.size() && dc[j + 1] == dc[j] + 1) ++j;
                // WHOLE TRIANGLES. A run that cut through a triangle the tie
                // holds degenerate would leave its corners outside the run at
                // the OLD first-corner position - still zero area, but not the
                // buffer the tie describes. Widened, `foldTies` rewrites all
                // three, and a non-degenerate triangle's extra corners are
                // rewritten with the values they already hold.
                const std::uint32_t lo = dc[i] / 3 * 3;
                const std::uint32_t hi = std::min<std::uint32_t>(
                    dc[j] / 3 * 3 + 2, static_cast<std::uint32_t>(g->corners.size() - 1));
                if (hi < g->corners.size()) {
                    v.resize(hi - lo + 1);
                    for (std::uint32_t k = lo; k <= hi; ++k) v[k - lo] = gpuVert(g->corners[k]);
                    // a run that crosses a triangle the tie holds degenerate
                    // must write it degenerate again, or the buffer stops
                    // matching `applied_` - see `foldTies`
                    foldTies(*g, v, lo, hi);
                    foldBias(*g, v.data(), lo, hi);
                    patchArrayBuffer(lo * sizeof(GpuVert), v.size() * sizeof(GpuVert), v.data());
                    g_glesFrame.uploadBytes += static_cast<long>(v.size() * sizeof(GpuVert));
                }
                i = j + 1;
            }
        } else {
            v.resize(g->corners.size());
            for (std::size_t k = 0; k < v.size(); ++k) v[k] = gpuVert(g->corners[k]);
            foldTies(*g, v, 0, static_cast<std::uint32_t>(v.size() - 1));
            foldBias(*g, v.data(), 0, static_cast<std::uint32_t>(v.size() - 1));
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(v.size() * sizeof(GpuVert)), v.data());
            g_glesFrame.uploadBytes += static_cast<long>(v.size() * sizeof(GpuVert));
            ++g_glesFrame.wholeUploads;
            vb.lastWhole = frameSeq_;
            if (glesUploadLog()) std::printf("  [upload] whole refill %p %zu corners rev %llu\n",
                                             static_cast<const void*>(g), v.size(),
                                             static_cast<unsigned long long>(g->revision));
        }
#if defined(__APPLE__)
        // ...and on the desktop, the BUFFER itself against a whole upload's
        // bytes (the tie off, so there is no fold to account for)
        if (audit && !tieOn_) {
            std::vector<GpuVert> got(g->corners.size());
            glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(got.size() * sizeof(GpuVert)), got.data());
            long bad = 0; std::size_t first = ~std::size_t{0};
            for (std::size_t k = 0; k < got.size(); ++k) {
                GpuVert w = gpuVert(g->corners[k]);
                foldBias(*g, &w, static_cast<std::uint32_t>(k), static_cast<std::uint32_t>(k));
                if (std::memcmp(&w, &got[k], sizeof w) != 0) { ++bad; if (first == ~std::size_t{0}) first = k; }
            }
            if (bad)
                std::printf("dirty audit: buffer of %p rev %llu (%s): %ld corners differ from a whole "
                            "upload; first %zu\n", static_cast<const void*>(g),
                            static_cast<unsigned long long>(g->revision), partial ? "partial" : "whole",
                            bad, first);
        }
#endif
        vb.rev = g->revision;
        return true;
    }
    Vbo vb;
    if (it != vbo_.end()) { vb.id = it->second.id; }
    else glGenBuffers(1, &vb.id);
    vb.lastWhole = frameSeq_;
    v.resize(g->corners.size());
    for (std::size_t k = 0; k < v.size(); ++k) v[k] = gpuVert(g->corners[k]);
    foldBias(*g, v.data(), 0, static_cast<std::uint32_t>(v.size() - 1));
    glBindBuffer(GL_ARRAY_BUFFER, vb.id);
    // DYNAMIC: a posed body rewrites its buffer every frame. What vitaGL does
    // with a buffer the GPU may still be reading is the open question the
    // device must answer (`todo/vita-port.md` G3).
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(GpuVert)),
                 v.data(), GL_DYNAMIC_DRAW);
gpuBuffer("vertex buffers", vb.id, static_cast<long long>(v.size() * sizeof(GpuVert)));
    g_glesFrame.uploadBytes += static_cast<long>(v.size() * sizeof(GpuVert));
    ++g_glesFrame.wholeUploads;
    if (glesUploadLog()) std::printf("  [upload] new buffer %p %zu corners rev %llu%s\n",
                                     static_cast<const void*>(g), v.size(),
                                     static_cast<unsigned long long>(g->revision),
                                     it != vbo_.end() ? " (resized)" : "");
    vb.n = v.size();
    vb.rev = g->revision;
    vbo_[g] = vb;
    g->resident.mark();
    tie_[g].vboReplaced();
    return true;
}

void GlesRenderer::foldTies(const Geometry& g, std::vector<GpuVert>& v,
                            std::uint32_t lo, std::uint32_t hi) {
    if (!tieOn_) return;
    const auto it = tie_.find(&g);
    if (it == tie_.end()) return;
    const DepthTie& t = it->second;
    // the triangles overlapping [lo, hi]; a degenerate triangle has all three
    // positions set to its FIRST corner's, which `resolveTies` also writes
    for (std::size_t tri = lo / 3; tri <= hi / 3; ++tri) {
        if (!t.isApplied(tri)) continue;
        const Corner& a = g.corners[3 * tri];
        for (std::size_t j = 0; j < 3; ++j) {
            const std::size_t c = 3 * tri + j;
            if (c < lo || c > hi) continue;
            v[c - lo].x = a.x; v[c - lo].y = a.y; v[c - lo].z = a.z;
        }
    }
}

// `v` holds corners [lo, hi] of `g`; the baked losers among them are marked.
void GlesRenderer::foldBias(const Geometry& g, GpuVert* v, std::uint32_t lo, std::uint32_t hi) const {
    const auto it = baked_.find(&g);
    if (it == baked_.end() || it->second.empty()) return;
    const std::vector<std::uint32_t>& L = it->second;
    for (auto t = std::lower_bound(L.begin(), L.end(), lo / 3); t != L.end() && 3 * *t <= hi; ++t)
        for (std::uint32_t j = 0; j < 3; ++j) {
            const std::uint32_t c = 3 * *t + j;
            if (c >= lo && c <= hi) v[c - lo].phase -= kTieLoserPhase;
        }
}

// THE TIE DECIDED ONCE. The same `DepthTie` the per-frame path runs, walked
// once over the geometry's whole draw order: its losers are the faces an
// earlier depth-writing face already claimed, which is a property of the
// order and the positions, not of the frame. Two things differ from settling
// it per frame, and both are the engine's behaviour rather than a departure:
// a loser is drawn one step back instead of removed, so where its winner is
// CULLED it still shows - as the engine shows it, having nothing in front -
// and a face that moves off its winner (a door leaf) still draws, the step
// back changing nothing where nothing is coincident.
bool GlesRenderer::bakeDepthTie(const Geometry* g, std::span<const Draw> order) {
    if (!tieOn_ || !g) return false;
    static const bool noBake = std::getenv("OMK_NO_TIE_BAKE") != nullptr;
    if (noBake) return false;
    DepthTie t;
    std::vector<std::size_t> losers, restore;
    for (const Draw& d : order)
        if (d.geo == g) t.resolve(*g, d.start, d.count, d.blend == Blend::Opaque, losers, restore);
    std::vector<std::uint32_t> L(losers.begin(), losers.end());
    std::sort(L.begin(), L.end());
    L.erase(std::unique(L.begin(), L.end()), L.end());
    baked_[g] = std::move(L);
    // what the buffer holds is the old decision: degenerated faces, or none
    // marked. Forget both, so the next upload is whole and marked.
    tie_.erase(g);
    if (auto v = vbo_.find(g); v != vbo_.end()) v->second.rev = ~std::uint64_t{0};
    static const bool tieLog = std::getenv("OMK_TIE_LOG") != nullptr;
    if (tieLog)
        std::printf("[tie] gles: baked %zu losers over %zu draws on geometry %p\n",
                    baked_[g].size(), order.size(), static_cast<const void*>(g));
    return true;
}

void GlesRenderer::resolveTies(const Draw& d, Vbo& vb) {
    // THE DEPTH TIE, the Vulkan backend's rule (`o3de/depthtie.h`): the engine
    // shows the FIRST-drawn of two coincident faces, a GPU's float compare
    // picks per pixel, so the losers are degenerated in the buffer. Whether a
    // GL target with its own depth format needs it at all is `vita-port.md`
    // G4's question, and it costs ~1 ms of CPU a frame on an M1 - so it is a
    // switch (`setDepthTie`, `OMK_NO_TIE=1`), on until that is answered.
    if (!tieOn_) return;
    const Geometry* g = d.geo;
    if (baked_.count(g)) return;             // settled once, drawn back (`bakeDepthTie`)
    auto& t = tie_[g];
    std::vector<std::size_t>& losers = losers_;
    std::vector<std::size_t>& restore = restore_;
    losers.clear();
    restore.clear();
    t.resolve(*g, d.start, d.count, d.blend == Blend::Opaque, losers, restore);
    if (t.newlyApplied().empty() && restore.empty()) return;
    g_glesInTie = true;
    glBindBuffer(GL_ARRAY_BUFFER, vb.id);
    GpuVert tri[3];
    for (const std::size_t k : restore) {
        const std::size_t c = 3 * k;
        if (c + 2 >= vb.n || c + 2 >= g->corners.size()) continue;
        for (int j = 0; j < 3; ++j) tri[j] = gpuVert(g->corners[c + j]);
        patchArrayBuffer(c * sizeof(GpuVert), sizeof tri, tri);
    }
    // ONLY THE DELTA. Every upload keeps the buffer equal to the tie's applied
    // set (`foldTies`), so a loser the tie had already marked is ALREADY
    // degenerate here and writing it again is a map/unmap for nothing - which
    // is what the SET did every frame: its moving cargo sends it down the
    // replay path, the replay reports the call's whole loser set, and all ~150
    // were written back although the dirty upload never touched them.
    for (const std::size_t k : t.newlyApplied()) {
        const std::size_t c = 3 * k;
        if (c + 2 >= vb.n || c + 2 >= g->corners.size()) continue;
        for (int j = 0; j < 3; ++j) {
            tri[j] = gpuVert(g->corners[c + j]);
            tri[j].x = g->corners[c].x;
            tri[j].y = g->corners[c].y;
            tri[j].z = g->corners[c].z;
        }
        patchArrayBuffer(c * sizeof(GpuVert), sizeof tri, tri);
    }
    g_glesInTie = false;
    t.dropped += static_cast<long>(losers.size());
    t.touched = true;
}

// THE VIEW, apart from `begin`'s clear: the mirror pass switches it mid-frame
// (`drawMirrorScene`) - the viewport, the MVP and both programs' copies of it.
void GlesRenderer::setView(const View& view) {
    // The letterbox is a VIEWPORT in the TOP-LEFT `vw x vh` of the target, as
    // the Vulkan backend places it. GL's window origin is bottom-left, so the
    // top-left rectangle starts at row `h - vh`.
    RCamera cam = view.cam;
    flipX_ = cam.flipX;
    const int vw = view.letterboxed() ? view.vw : w_;
    const int vh = view.letterboxed() ? view.vh : h_;
    cam.w = vw; cam.h = vh;
    // ...at the RENDER size: a supersampled target is the frame `ss_` times
    // over, and the projection is the same (the aspect does not change)
    glViewport(0, rh_ - vh * ss_, vw * ss_, vh * ss_);

    // THE VIEW-PROJECTION from the software rasterizer's own basis. GL's clip
    // space differs from Vulkan's in the two ways that matter: Y points UP
    // (so row 1 is +u/tanV where Vulkan has -u/tanV) and Z runs -1..1 (so
    // the depth rows are the textbook (f+n)/(f-n), -2fn/(f-n)). Row 3 is the
    // same f . (world - eye), so w is still the view depth the fog reads.
    float s[3], u[3], f[3], tanH = 0, tanV = 0;
    cameraBasis(cam, s, u, f, tanH, tanV);
    const float n = kNearCut, fa = 200000.0f;
    const float A = (fa + n) / (fa - n), B = -2.0f * fa * n / (fa - n);
    const float* e = cam.eye;
    const float se = s[0] * e[0] + s[1] * e[1] + s[2] * e[2];
    const float ue = u[0] * e[0] + u[1] * e[1] + u[2] * e[2];
    const float fe = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
    const float r0[4] = {s[0] / tanH, s[1] / tanH, s[2] / tanH, -se / tanH};
    const float r1[4] = {u[0] / tanV, u[1] / tanV, u[2] / tanV, -ue / tanV};
    const float r2[4] = {A * f[0], A * f[1], A * f[2], -A * fe + B};
    const float r3[4] = {f[0], f[1], f[2], -fe};
    const float* rows[4] = {r0, r1, r2, r3};
    float mvp[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) mvp[c * 4 + r] = rows[r][c];   // column-major

    std::memcpy(mvp_, mvp, sizeof mvp_);
    if (posed_) {
        glUseProgram(posed_);
        glUniformMatrix4fv(posedLoc_.mvp, 1, GL_FALSE, mvp);
    }
    glUseProgram(prog_);
    curProg_ = prog_;
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp);
}

// THE MIRROR, THE ORIGINAL'S WAY (`sub_440D90`, 0x00440D90): the scene is
// drawn FIRST through the camera reflected in the mirror's plane - with the
// screen-X flip `dword_53ADE0` asks of `Raster_DrawTriangles`, here `flipX` -
// and then AGAIN through the real camera, over it, with no clear between: only
// `Render_Frame(reflected, 1)` then `Render_Frame(camera, 0)`. The reflected
// pass leaves exactly the depths of a virtual room behind the plane, so the
// real room's walls, nearer, cover it everywhere but where the wall is open for
// the mirror, and the mirror's own faces blend over what shows through. No
// stencil, no read-back: the CPU fallback read the frame back and converted
// it, 190 ms a frame in Kay'l's apartment on the console (2026-09-30). The
// reflected pass takes the draws in FRONT of the plane, as the fallback does -
// the port's own guard against what lies behind the mirror's wall.
// `OMK_CPU_MIRROR=1` keeps the fallback, for laying the two side by side.
bool GlesRenderer::drawMirrorScene(const View& v, const View& refl, std::span<const Draw> scene,
                                   std::span<const Draw> sceneClipped, std::span<const Draw> mirror) {
    static const bool cpuMirror = std::getenv("OMK_CPU_MIRROR") != nullptr;
    if (cpuMirror || mirror.empty()) return false;
    begin(v);
    setView(refl);
    for (const auto& d : sceneClipped) submit(d);
    setView(v);
    for (const auto& d : scene) submit(d);
    for (const auto& d : mirror) submit(d);
    end();
    return true;
}

void GlesRenderer::begin(const View& view) {
    g_glesWindow.uploads += g_glesFrame.uploads;
    g_glesWindow.uploadBytes += g_glesFrame.uploadBytes;
    g_glesWindow.uploadMs += g_glesFrame.uploadMs;
    g_glesWindow.drawMs += g_glesFrame.drawMs;
    g_glesWindow.tieMs += g_glesFrame.tieMs;
    g_glesWindow.wholeUploads += g_glesFrame.wholeUploads;
    g_glesWindow.streamed += g_glesFrame.streamed;
    g_glesFrame = GlesFrameCounts{};
    if (!deadBufs_.empty()) {
        deleteBuffers(static_cast<GLsizei>(deadBufs_.size()), deadBufs_.data());
        deadBufs_.clear();
    }
    ++frameNo_;
    if (frameFromPresent_ != presentSeq_) { frameFromPresent_ = presentSeq_; ++frameSeq_; }
    st_ = RasterStats{};
    view_ = view;
    fog_ = view.fog;
    fogStart_ = view.fogStart;
    fogEnd_ = view.fogEnd;
    for (int i = 0; i < 3; ++i) fogColour_[i] = static_cast<float>(view.fogColour[i]) / 255.0f;
    dither_ = view.dither;

    glBindFramebuffer(GL_FRAMEBUFFER, drawFbo());
    glViewport(0, 0, rw_, rh_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // black, as the engine clears
    OMK_GL_CLEAR_DEPTH(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    setView(view);
    lastPose_ = nullptr;
    lastPoseGeo_ = nullptr;
    forgetState();
    if (posed_) {
        glUseProgram(posed_);
        glUniform1f(posedLoc_.clock, view.shimmerClock);
    }
    glUseProgram(prog_);
    curProg_ = prog_;
    glUniform1f(uClock_, view.shimmerClock);
    // THE ENHANCEMENTS' per-frame state, on both programs: the set's lights
    // (`View::lights`, nearest first, at most eight) and the shadow map the
    // depth pass just filled - or a strength of 0, which turns the lookup off
    if (pixLights_ || shadowMap_) {
        float pl[24 * 4] = {};
        const int nl = pixLights_ ? static_cast<int>(std::min<std::size_t>(
                                        view.lights.size(), static_cast<std::size_t>(View::kMaxGpuLights)))
                                  : 0;
        for (int i = 0; i < nl; ++i) {
            const auto& l = view.lights[static_cast<std::size_t>(i)];
            float* o = pl + 12 * i;
            for (int k = 0; k < 3; ++k) { o[k] = l.pos[k]; o[4 + k] = l.dir[k]; o[8 + k] = l.colour[k]; }
            o[3] = l.radiusA; o[7] = l.radiusB; o[11] = l.intensity;
        }
        const float sp[4] = {shadowLive_ ? shadowStrength_ : 0.0f, 1.0f / kShadowSide, 0.0015f, 0.0f};
        for (const GLuint p : {prog_, posed_}) {
            if (!p) continue;
            const SceneLoc& L = p == prog_ ? mainLoc_ : posedLoc_;
            glUseProgram(p);
            if (L.pl >= 0 && nl > 0) glUniform4fv(L.pl, 3 * nl, pl);
            if (L.plCount >= 0) glUniform1f(L.plCount, static_cast<float>(nl));
            if (L.shadowP >= 0) glUniform4fv(L.shadowP, 1, sp);
            if (L.lightMvp >= 0) glUniformMatrix4fv(L.lightMvp, 1, GL_FALSE, lightMvp_);
        }
        glUseProgram(prog_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, shadowLive_ ? shTex_ : white_.id);
        glActiveTexture(GL_TEXTURE0);
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    recording_ = true;
}

bool GlesRenderer::uploadPosedGeometry(const Geometry* g, PoseVbo*& out) {
    // A REST geometry: its corners change only when the model does, so this
    // runs once per revision and the buffer is STATIC - the whole point
    // (todo/gpu-skinning.md). Each corner carries its mesh's SLOT.
    PoseVbo& pv = poseVbo_[g];
    g->resident.mark();
    out = &pv;
    if (pv.id && pv.rev == g->revision && pv.n == g->corners.size()) return true;
    if (g->corners.empty() || g->cornerMesh.size() != g->corners.size()) return false;
    pv.meshOfSlot.clear();
    std::unordered_map<std::int32_t, int> slotOf;
    std::vector<GpuPoseVert>& v = poseUp_;
    v.resize(g->corners.size());
    for (std::size_t k = 0; k < v.size(); ++k) {
        const std::int32_t m = g->cornerMesh[k];
        auto it = slotOf.find(m);
        if (it == slotOf.end()) {
            it = slotOf.emplace(m, static_cast<int>(pv.meshOfSlot.size())).first;
            pv.meshOfSlot.push_back(m);
        }
        v[k].v = gpuVert(g->corners[k]);
        v[k].slot = static_cast<float>(it->second);
        v[k].nx = g->corners[k].nx;
        v[k].ny = g->corners[k].ny;
        v[k].nz = g->corners[k].nz;
    }
    ++g_glesFrame.uploads;
    ++g_glesFrame.wholeUploads;
    g_glesFrame.uploadBytes += static_cast<long>(v.size() * sizeof(GpuPoseVert));
    if (!pv.id) glGenBuffers(1, &pv.id);
    glBindBuffer(GL_ARRAY_BUFFER, pv.id);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(GpuPoseVert)),
                 v.data(), GL_STATIC_DRAW);
gpuBuffer("posed bodies", pv.id, static_cast<long long>(v.size() * sizeof(GpuPoseVert)));
    pv.n = v.size();
    pv.rev = g->revision;
    pv.slotOfCorner.resize(v.size());
    for (std::size_t k = 0; k < v.size(); ++k) pv.slotOfCorner[k] = v[k].slot;
    // a fresh buffer holds nothing degenerate
    if (const auto it = poseTie_.find(g); it != poseTie_.end()) it->second.tie.vboReplaced();
    return true;
}

void GlesRenderer::resolvePosedTies(const Draw& d, PoseVbo& pv) {
    if (!tieOn_) return;
    const Geometry* g = d.geo;
    PoseTie& pt = poseTie_[g];
    if (pt.restRev != g->revision || pt.g.corners.size() != g->corners.size()) {
        pt.g = *g;
        pt.g.tieClass.assign(g->cornerMesh.begin(), g->cornerMesh.end());
        pt.g.tieRigidFrom = 0;
        pt.g.revision = ++poseTieRev_;
        pt.restRev = g->revision;
        pt.frame = frameNo_;
        pt.tie = DepthTie{};
        pt.seen.clear();
    } else if (pt.frame != frameNo_) {
        pt.g.tieRigidFrom = pt.g.revision;
        pt.g.revision = ++poseTieRev_;
        pt.frame = frameNo_;
        pt.seen.clear();
    }
    const auto call = std::make_tuple(d.start, d.count, d.blend == Blend::Opaque);
    for (const auto& c : pt.seen) if (c == call) return;
    pt.seen.push_back(call);
    losers_.clear();
    restore_.clear();
    pt.tie.resolve(pt.g, d.start, d.count, d.blend == Blend::Opaque, losers_, restore_);
    if (pt.tie.newlyApplied().empty() && restore_.empty()) return;
    g_glesInTie = true;
    glBindBuffer(GL_ARRAY_BUFFER, pv.id);
    GpuPoseVert tri[3];
    const auto write = [&](std::size_t k, bool degenerate) {
        const std::size_t c = 3 * k;
        if (c + 2 >= pv.n || c + 2 >= g->corners.size()) return;
        for (int j = 0; j < 3; ++j) {
            tri[j].v = gpuVert(g->corners[c + j]);
            tri[j].slot = pv.slotOfCorner[c + j];
            tri[j].nx = g->corners[c + j].nx;
            tri[j].ny = g->corners[c + j].ny;
            tri[j].nz = g->corners[c + j].nz;
            if (degenerate) {
                tri[j].v.x = g->corners[c].x;
                tri[j].v.y = g->corners[c].y;
                tri[j].v.z = g->corners[c].z;
                tri[j].slot = pv.slotOfCorner[c];
            }
        }
        patchArrayBuffer(c * sizeof(GpuPoseVert), sizeof tri, tri);
    };
    for (const std::size_t k : restore_) write(k, false);
    for (const std::size_t k : pt.tie.newlyApplied()) write(k, true);
    g_glesInTie = false;
}

void GlesRenderer::buildPoseUniforms(const Draw& d, const PoseVbo& pv) {
    // one affine a slot: the mesh's, or the identity for a corner no mesh
    // owns (`applyPose` leaves those at rest)
    const std::size_t slots = pv.meshOfSlot.size();
    poseUni_.assign(slots * 16u, 0.0f);            // four rows a slot (`kPosedVert`)
    for (std::size_t sl = 0; sl < slots; ++sl) {
        const std::int32_t m = pv.meshOfSlot[sl];
        float* o = poseUni_.data() + 16 * sl;
        if (m >= 0 && static_cast<std::size_t>(m) < d.meshPoses) {
            std::memcpy(o, d.meshPose + 12 * static_cast<std::size_t>(m), 12 * sizeof(float));
        } else {
            o[0] = 1.0f; o[5] = 1.0f; o[10] = 1.0f;
        }
    }
}

Draw GlesRenderer::cpuPose(const Draw& d) {
    Geometry& cp = cpuPosed_[d.geo];
    d.geo->resident.mark();
    if (cp.corners.size() != d.geo->corners.size()) cp = *d.geo;
    for (std::size_t i = 0; i < cp.corners.size(); ++i) {
        const Corner& rc = d.geo->corners[i];
        const std::int32_t m = d.geo->cornerMesh[i];
        Corner& c = cp.corners[i];
        if (m < 0 || static_cast<std::size_t>(m) >= d.meshPoses) {
            c.x = rc.x; c.y = rc.y; c.z = rc.z;
            if (pixLights_) { c.nx = rc.nx; c.ny = rc.ny; c.nz = rc.nz; }
            continue;
        }
        const float* a = d.meshPose + 12 * static_cast<std::size_t>(m);
        c.x = a[0] * rc.x + a[1] * rc.y + a[2] * rc.z + a[3];
        c.y = a[4] * rc.x + a[5] * rc.y + a[6] * rc.z + a[7];
        c.z = a[8] * rc.x + a[9] * rc.y + a[10] * rc.z + a[11];
        if (pixLights_) {
            c.nx = a[0] * rc.nx + a[1] * rc.ny + a[2] * rc.nz;
            c.ny = a[4] * rc.nx + a[5] * rc.ny + a[6] * rc.nz;
            c.nz = a[8] * rc.nx + a[9] * rc.ny + a[10] * rc.nz;
        }
    }
    cp.revision = d.geo->revision + (++cpuPoseRev_ << 32);
    Draw c = d;
    c.geo = &cp;
    c.meshPose = nullptr;
    c.meshPoses = 0;
    return c;
}

bool GlesRenderer::uploadNormals(const Geometry* g) {
    NrmVbo& nb = nrmVbo_[g];
    const std::size_t n = g->corners.size();
    if (nb.id && nb.rev == g->revision && nb.n == n) return true;
    if (!n) return false;
    nrmUp_.resize(3 * n);
    for (std::size_t k = 0; k < n; ++k) {
        nrmUp_[3 * k] = g->corners[k].nx;
        nrmUp_[3 * k + 1] = g->corners[k].ny;
        nrmUp_[3 * k + 2] = g->corners[k].nz;
    }
    if (!nb.id) glGenBuffers(1, &nb.id);
    glBindBuffer(GL_ARRAY_BUFFER, nb.id);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(nrmUp_.size() * sizeof(float)),
                 nrmUp_.data(), GL_DYNAMIC_DRAW);
gpuBuffer("vertex buffers", nb.id, static_cast<long long>(nrmUp_.size() * sizeof(float)));
    ds_.attrBuf = 0;                      // the next draw re-points its attributes
    ++g_glesFrame.uploads;
    g_glesFrame.uploadBytes += static_cast<long>(nrmUp_.size() * sizeof(float));
    nb.n = n;
    nb.rev = g->revision;
    g->resident.mark();
    return true;
}

// THE DEPTH PASS FROM THE LIGHT - `todo/enhancements.md` row 6, Vulkan's
// `shadowPass` on this API. The same ORTHOGRAPHIC slab fitted to the casters
// (a fragment outside it is lit by definition), the same basis and the same
// 0..1 slab depth - which `kShadowFrag` packs into RGBA8 and `kSceneFragX`
// unpacks, GLES2 guaranteeing no depth texture. A body the renderer poses is
// posed here too (`kShadowPosedVert`), one with more meshes than the program
// holds on the CPU, as the scene pass does.
void GlesRenderer::shadowPass(const View& v, std::span<const Draw> casters) {
    if (!shadowMap_ || !v.shadow.on || casters.empty()) return;
    const float R = v.shadow.radius > 1.0f ? v.shadow.radius : 1.0f;
    float f[3] = {v.shadow.dir[0], v.shadow.dir[1], v.shadow.dir[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) return;
    for (float& c : f) c /= fl;
    float up[3] = {0.0f, -1.0f, 0.0f};
    if (std::fabs(f[0] * up[0] + f[1] * up[1] + f[2] * up[2]) > 0.99f) {
        up[0] = 1.0f; up[1] = 0.0f; up[2] = 0.0f;
    }
    float r[3] = {up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2],
                  up[0] * f[1] - up[1] * f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& c : r) c /= rl;
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2],
                  f[0] * r[1] - f[1] * r[0]};
    const float back = R * 2.0f;
    float eye[3];
    for (int k = 0; k < 3; ++k) eye[k] = v.shadow.centre[k] - f[k] * back;
    const float far = back + R * 2.0f;
    float m[16] = {};
    const float* ax[3] = {r, u, f};
    const float sc[3] = {1.0f / R, 1.0f / R, 1.0f / far};
    for (int row = 0; row < 3; ++row) {
        float dd = 0.0f;
        for (int c = 0; c < 3; ++c) {
            m[c * 4 + row] = ax[row][c] * sc[row];
            dd += ax[row][c] * eye[c];
        }
        m[12 + row] = -dd * sc[row];
    }
    m[15] = 1.0f;

    // a new presented frame starts HERE when the depth pass precedes `begin`,
    // so a geometry streamed now lands in this frame's third of the ring
    if (frameFromPresent_ != presentSeq_) { frameFromPresent_ = presentSeq_; ++frameSeq_; }
    glBindFramebuffer(GL_FRAMEBUFFER, shFbo_);
    glViewport(0, 0, kShadowSide, kShadowSide);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);    // packed "far": past every slab depth
    OMK_GL_CLEAR_DEPTH(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (GLuint a = 0; a <= kAttrNormal; ++a) glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(kAttrPos);
    bool mvpPlain = false, mvpPosed = false;
    for (const Draw& d0 : casters) {
        if (!d0.geo || !d0.count) continue;
        Draw d = d0;
        PoseVbo* pv = nullptr;
        bool posed = usePosed(d) && shPosed_;
        if (posed) {
            if (!uploadPosedGeometry(d.geo, pv)) continue;
            if (pv->meshOfSlot.size() > static_cast<std::size_t>(kPoseSlots)) { d = cpuPose(d0); posed = false; }
        } else if (usePosed(d)) {
            d = cpuPose(d0);
        }
        if (posed) {
            useProgram(shPosed_);
            if (!mvpPosed) { glUniformMatrix4fv(shPosedMvp_, 1, GL_FALSE, m); mvpPosed = true; }
            buildPoseUniforms(d, *pv);
            glUniform4fv(shPosedPose_, static_cast<GLsizei>(pv->meshOfSlot.size() * 4), poseUni_.data());
            glBindBuffer(GL_ARRAY_BUFFER, pv->id);
            glEnableVertexAttribArray(kAttrSlot);
            glVertexAttribPointer(kAttrPos, 3, GL_FLOAT, GL_FALSE, sizeof(GpuPoseVert), nullptr);
            glVertexAttribPointer(kAttrSlot, 1, GL_FLOAT, GL_FALSE, sizeof(GpuPoseVert),
                                  reinterpret_cast<const void*>(offsetof(GpuPoseVert, slot)));
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(d.start), static_cast<GLsizei>(d.count));
            glDisableVertexAttribArray(kAttrSlot);
        } else {
            if (!uploadGeometry(d.geo, !(pixLights_ && d.lit))) continue;
            const Vbo& vb = vbo_[d.geo];
            useProgram(shProg_);
            if (!mvpPlain) { glUniformMatrix4fv(shMvp_, 1, GL_FALSE, m); mvpPlain = true; }
            glBindBuffer(GL_ARRAY_BUFFER, vb.streamed ? ring_ : vb.id);
            glVertexAttribPointer(kAttrPos, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVert), nullptr);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>((vb.streamed ? vb.base : 0) + d.start),
                         static_cast<GLsizei>(d.count));
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    forgetState();                            // `begin` sets every draw state afresh
    std::memcpy(lightMvp_, m, sizeof m);
    shadowStrength_ = v.shadow.strength;
    shadowLive_ = true;
}

void GlesRenderer::submit(const Draw& d) {
    if (!recording_ || !d.geo || !d.count) return;
    PoseVbo* pvb = nullptr;
    bool posed = usePosed(d);
    // a LIT plain draw reads its normals from a buffer of their own
    const bool litNrm = !posed && pixLights_ && d.lit != 0;
    const double fu0 = glesClockMs();
    bool uploaded = posed ? uploadPosedGeometry(d.geo, pvb) : uploadGeometry(d.geo, !litNrm);
    if (uploaded && litNrm) uploaded = uploadNormals(d.geo);
    if (posed && uploaded && pvb->meshOfSlot.size() > static_cast<std::size_t>(kPoseSlots)) {
        // MORE MESHES THAN THE PROGRAM HOLDS: pose it here, on the CPU, into a
        // geometry of its own, and draw that the ordinary way
        const Draw c = cpuPose(d);
        g_glesFrame.uploadMs += glesClockMs() - fu0;
        submit(c);
        return;
    }
    g_glesFrame.uploadMs += glesClockMs() - fu0;
    if (!uploaded) return;
    {
        const double ft0 = glesClockMs();
        if (posed) resolvePosedTies(d, *pvb);
        else resolveTies(d, vbo_[d.geo]);
        g_glesFrame.tieMs += glesClockMs() - ft0;
    }
    st_.triangles += static_cast<long>(d.count / 3);
    const SceneLoc& L = posed ? posedLoc_ : mainLoc_;
    useProgram(posed ? posed_ : prog_);
    if (posed && (lastPose_ != d.meshPose || lastPoseGeo_ != d.geo)) {
        buildPoseUniforms(d, *pvb);
        glUniform4fv(L.pose, static_cast<GLsizei>(pvb->meshOfSlot.size() * 4), poseUni_.data());
        lastPose_ = d.meshPose;
        lastPoseGeo_ = d.geo;
    }
    // Every call below goes through the draw-state cache (`DrawState`): `set`
    // says whether it must be made, and counts it either way.
    const auto set = [&](bool changed) {
        if (changed || !stateCache_) { ++g_glesStateSet; return true; }
        ++g_glesStateSkipped;
        return false;
    };
    UniCache& U = ds_.uni[posed ? 1 : 0];
    const bool uv = U.valid;
    if (posed) {
        // the body's lights; a draw handed more than the program holds is
        // the frontend's error, and gets the first `kVertexLights`
        const int n = d.vertexLights ? std::min(d.vertexLightCount, kVertexLights) : 0;
        const std::size_t nv = static_cast<std::size_t>(8 * n);
        // compared by VALUE: a body's lights are rebuilt every frame and two
        // bodies may hand the same array
        const bool same = uv && U.lights == n && U.lightVals.size() == nv &&
                          (nv == 0 || std::memcmp(U.lightVals.data(), d.vertexLights, nv * sizeof(float)) == 0);
        if (set(!same)) {
            if (n > 0) glUniform4fv(L.light, 2 * n, d.vertexLights);
            glUniform1f(L.lightCount, static_cast<float>(n));
            U.lights = n;
            U.lightVals.assign(d.vertexLights, d.vertexLights + nv);
        }
        const float black = d.lightsFromBlack ? 1.0f : 0.0f;
        if (set(!uv || U.lightBlack != black)) {
            glUniform1f(L.lightBlack, black);
            U.lightBlack = black;
        }
    }

    if (set(ds_.blend != static_cast<int>(d.blend))) {
        switch (d.blend) {
        case Blend::Opaque:
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            break;
        case Blend::Add:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            break;
        case Blend::Mul:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);   // dst * (1 - src)
            glDepthMask(GL_FALSE);
            break;
        }
        ds_.blend = static_cast<int>(d.blend);
    }

    // The texture is the key's LOW SIX BITS and nothing else (ASSETS 4b).
    const std::size_t slot = d.bucketKey & 0x3Fu;
    const Tex& t = (slot < tex_.size() && tex_[slot].id) ? tex_[slot] : white_;
    if (set(!ds_.texValid || ds_.tex != t.id)) {
        glBindTexture(GL_TEXTURE_2D, t.id);
        ds_.tex = t.id;
        ds_.texValid = true;
    }
    if (set(!uv || U.texW != t.w || U.texH != t.h)) {
        glUniform2f(L.texSize, t.w, t.h);
        U.texW = t.w; U.texH = t.h;
    }
    const int cut = d.cutout ? 1 : 0;
    if (set(!uv || U.cutout != cut)) {
        glUniform1i(L.cutout, cut);
        U.cutout = cut;
    }
    // the enhanced programs' two per-draw words: how this batch is LIT, and
    // whether it CASTS (a caster does not receive - `scene.frag`)
    if (L.lit >= 0) {
        const float lit = pixLights_ ? static_cast<float>(d.lit) : 0.0f;
        if (set(!uv || U.lit != lit)) { glUniform1f(L.lit, lit); U.lit = lit; }
        const float cs = d.castsShadow ? 1.0f : 0.0f;
        if (set(!uv || U.caster != cs)) { glUniform1f(L.caster, cs); U.caster = cs; }
    }

    // THE FOG's two exclusions - the rule `renderer.cpp` applies for the
    // software loop and `vkrender.cpp` for Vulkan.
    float fs = 0.0f, fe = 0.0f;
    if (fog_ && !(d.bucketKey & 0x2080u)) {
        const float k = (d.bucketKey & 0x800u) ? 2.0f : 1.0f;
        fs = fogStart_ * k;
        fe = fogEnd_ * k;
    }
    if (set(!uv || U.fogStart != fs || U.fogEnd != fe)) {
        glUniform1f(L.fogStart, fs);
        glUniform1f(L.fogEnd, fe);
        U.fogStart = fs; U.fogEnd = fe;
    }
    if (set(!uv || U.fog[0] != fogColour_[0] || U.fog[1] != fogColour_[1] || U.fog[2] != fogColour_[2])) {
        glUniform3f(L.fogColour, fogColour_[0], fogColour_[1], fogColour_[2]);
        for (int k = 0; k < 3; ++k) U.fog[k] = fogColour_[k];
    }
    U.valid = true;

    // THE ATTRIBUTES. A pointer captures the buffer bound WHEN IT IS SET, so
    // the uploads' and tie patches' own binds in between leave it alone: the
    // same buffer in the same layout needs nothing.
    const Vbo* sv = posed ? nullptr : &vbo_[d.geo];
    const bool fromRing = sv && sv->streamed;
    const GLuint buf = posed ? pvb->id : fromRing ? ring_ : sv->id;
    const int layout = posed ? 1 : litNrm ? 2 : 0;
    if (set(ds_.attrBuf != buf || ds_.attrLayout != layout)) {
        if (litNrm) {
            // the normals' own buffer, at the same corner index (a lit draw is
            // never streamed, so its first corner is 0 in both)
            glBindBuffer(GL_ARRAY_BUFFER, nrmVbo_[d.geo].id);
            glEnableVertexAttribArray(kAttrNormal);
            glVertexAttribPointer(kAttrNormal, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
        }
        glBindBuffer(GL_ARRAY_BUFFER, buf);
        const GLsizei st = posed ? sizeof(GpuPoseVert) : sizeof(GpuVert);
        glEnableVertexAttribArray(kAttrPos);
        glEnableVertexAttribArray(kAttrUV);
        glEnableVertexAttribArray(kAttrCol);
        glEnableVertexAttribArray(kAttrPhase);
        if (posed) {
            glEnableVertexAttribArray(kAttrSlot);
            glVertexAttribPointer(kAttrSlot, 1, GL_FLOAT, GL_FALSE, st,
                                  reinterpret_cast<const void*>(offsetof(GpuPoseVert, slot)));
            glEnableVertexAttribArray(kAttrNormal);
            glVertexAttribPointer(kAttrNormal, 3, GL_FLOAT, GL_FALSE, st,
                                  reinterpret_cast<const void*>(offsetof(GpuPoseVert, nx)));
        } else {
            glDisableVertexAttribArray(kAttrSlot);
            if (!litNrm) glDisableVertexAttribArray(kAttrNormal);
        }
        glVertexAttribPointer(kAttrPos, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(0));
        glVertexAttribPointer(kAttrUV, 2, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(12));
        glVertexAttribPointer(kAttrCol, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(20));
        glVertexAttribPointer(kAttrPhase, 1, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(32));
        ds_.attrBuf = buf;
        ds_.attrLayout = layout;
    }
    ++g_glesDrawsWindow;
    const double fd0 = glesClockMs();
    // THE BACK-FACE CULL, one draw per run of `cornerCull` (`geom3do.h`): a
    // batch groups by material, so single- and two-sided faces can share one.
    // The engine culls in software (`Render_SubmitMesh`); here it is GL's own
    // cull with the face chosen from `kGlesCullBack` - which GL face is the
    // software rasterizer's BACK under this projection - and the mirror pass's
    // screen-X flip swapping it, as it swaps the area `raster.cpp` tests.
    const GLint base = static_cast<GLint>(fromRing ? sv->base : 0);
    const bool haveCull = d.geo->cornerCull.size() == d.geo->corners.size();
    std::size_t i = d.start;
    const std::size_t e = d.start + d.count;
    while (i < e) {
        const std::uint8_t c = haveCull ? d.geo->cornerCull[i] : 0u;
        std::size_t j = i;
        while (j < e && (haveCull ? d.geo->cornerCull[j] : 0u) == c) ++j;
        const int mode = !c ? 0 : (kGlesCullBack != flipX_) ? 1 : 2;
        if (set(ds_.cull != mode)) {
            if (mode == 0) glDisable(GL_CULL_FACE);
            else { glEnable(GL_CULL_FACE); glCullFace(mode == 1 ? GL_BACK : GL_FRONT); }
            ds_.cull = mode;
        }
        glDrawArrays(GL_TRIANGLES, base + static_cast<GLint>(i), static_cast<GLsizei>(j - i));
        i = j;
    }
    g_glesFrame.drawMs += glesClockMs() - fd0;
    ++g_glesFrame.draws;
    st_.drawn += static_cast<long>(d.count / 3);
}

void GlesRenderer::end() {
    if (!recording_) return;
    recording_ = false;
    shadowLive_ = false;   // the next frame's depth pass, if any, fills it again
    // The cull is the SCENE's: nothing drawn after it (the mirror composite,
    // the interface, the present) has an authored winding.
    glDisable(GL_CULL_FACE);
    ds_.cull = 0;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
#if OMK_GLES_MSAA
    // THE MSAA RESOLVE: the samples averaged into `colour_`, which every
    // consumer reads
    if (msFbo_) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
        glBlitFramebuffer(0, 0, rw_, rh_, 0, 0, rw_, rh_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    dirty_ = true;
    // Per geometry, once, as the Vulkan backend reports it.
    static const bool tieLog = std::getenv("OMK_TIE_LOG") != nullptr;
    if (tieLog)
        for (auto& [g, t] : tie_)
            if (t.touched && !t.logged) {
                std::printf("[tie] gles: %ld triangles dropped on geometry %p\n",
                            t.dropped, static_cast<const void*>(g));
                t.logged = true;
            }
    if (tieLog)
        for (auto& [g, pt] : poseTie_)
            if (!pt.tie.logged && !pt.tie.appliedTriangles().empty()) {
                std::printf("[tie] gles: %zu triangles degenerate on POSED geometry %p\n",
                            pt.tie.appliedTriangles().size(), static_cast<const void*>(g));
                pt.tie.logged = true;
            }
}

const Surface& GlesRenderer::readback() {
    // IDEMPOTENT - the contract the Vulkan backend's comment explains: a
    // caller may read twice and write between (the mirror composite does).
    // On a Vita this is a PIPELINE STALL (the CPU waits for the GPU to finish
    // the frame), which is why the plan's step G6 is to stop needing it.
    if (!dirty_) return fb_;
    dirty_ = false;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const double t0 = glesClockMs();
    glReadPixels(0, 0, rw_, rh_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
    const double t1 = glesClockMs();
    g_glesMs[0] += t1 - t0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // THE SUPERSAMPLE RESOLVE, on the 8-bit side and BEFORE the one
    // quantisation - Vulkan's `readback` rule: the rounded mean of each
    // `ss x ss` block, then dithered once at the output pixel.
    if (ss_ > 1) {
        const int n = ss_ * ss_;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                int acc[3] = {0, 0, 0};
                for (int sy = 0; sy < ss_; ++sy) {
                    const unsigned char* row = rgba_.data() +
                        static_cast<std::size_t>(rh_ - 1 - (y * ss_ + sy)) * rw_ * 4;
                    for (int sx = 0; sx < ss_; ++sx) {
                        const unsigned char* p = row + static_cast<std::size_t>(x * ss_ + sx) * 4;
                        acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
                    }
                }
                const int ar = (acc[0] + n / 2) / n, ag = (acc[1] + n / 2) / n,
                          ab = (acc[2] + n / 2) / n;
                fb_.px[static_cast<std::size_t>(y) * w_ + x] =
                    dither_ ? quantise888Dither(ar, ag, ab, x, y)
                            : rgb565(static_cast<unsigned char>(ar), static_cast<unsigned char>(ag),
                                     static_cast<unsigned char>(ab));
            }
        g_glesMs[1] += glesClockMs() - t1;
        return fb_;
    }
    // GL's row 0 is the BOTTOM; the Surface's is the top. The 888 -> 565 rule
    // is `quantise888DitherRow`, the one the Vulkan readback calls, so the
    // quantisation stays in one place (`PORTING` A3).
    for (int y = 0; y < h_; ++y) {
        const unsigned char* src = rgba_.data() + static_cast<std::size_t>(h_ - 1 - y) * w_ * 4;
        std::uint16_t* dst = fb_.px.data() + static_cast<std::size_t>(y) * w_;
        if (dither_) {
            quantise888DitherRow(src, w_, y, dst);
        } else {
            for (int x = 0; x < w_; ++x) dst[x] = rgb565(src[4 * x], src[4 * x + 1], src[4 * x + 2]);
        }
    }
    g_glesMs[1] += glesClockMs() - t1;
    return fb_;
}

void GlesRenderer::destRect(int picW, int picH, int winW, int winH, float out[4]) const {
    // the largest rectangle of the picture's aspect, centred - or the whole
    // window when stretching was asked for. In NDC: x, y (bottom-left), w, h.
    float dw = static_cast<float>(winW), dh = static_cast<float>(winH);
    if (!stretch_) {
        const float a = static_cast<float>(picW) / static_cast<float>(picH);
        if (dw / dh > a) dw = dh * a; else dh = dw / a;
    }
    out[2] = 2.0f * dw / static_cast<float>(winW);
    out[3] = 2.0f * dh / static_cast<float>(winH);
    out[0] = -out[2] * 0.5f;
    out[1] = -out[3] * 0.5f;
}

void GlesRenderer::drawPresent(GLuint tex, int picW, int picH, int texW, int texH,
                               bool flipY, bool quantise, const float dst[4],
                               int winW, int winH) {
    glBindFramebuffer(GL_FRAMEBUFFER, windowFbo_);
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);   // the bands, and whatever the aspect leaves
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(present_);
    curProg_ = present_;
    glUniform4fv(pDst_, 1, dst);
    glUniform2f(pPicSize_, static_cast<float>(picW), static_cast<float>(picH));
    glUniform2f(pTexSize_, static_cast<float>(texW), static_cast<float>(texH));
    glUniform1i(pFlip_, flipY ? 1 : 0);
    glUniform1i(pDither_, dither_ ? 1 : 0);
    glUniform1i(pQuant_, quantise ? 1 : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bayer_);
    glUniform1i(pBayer_, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(pPic_, 0);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    for (GLuint a = 1; a <= kAttrPhase; ++a) glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

bool GlesRenderer::presentWorld(int vy, int vh, int frameW, int frameH, int winW, int winH) {
    notePresent();
    // The world target holds the picture in its top `vh` rows; the engine's
    // frame is `frameW x frameH` with the picture at row `vy` and black bands
    // around it. The frame's rectangle on the window is fitted first, then cut
    // to the picture's rows, so the bands are the window's clear colour and
    // never pixels of the target.
    (void)frameW;
    if (vh <= 0 || vh > h_ || vy < 0) return false;
    float frame[4];
    destRect(w_, frameH, winW, winH, frame);
    const float rowH = frame[3] / static_cast<float>(frameH);
    const float dst[4] = {frame[0], frame[1] + frame[3] - rowH * static_cast<float>(vy + vh),
                          frame[2], rowH * static_cast<float>(vh)};
    if (ss_ > 1) { drawPresentSS(vh, dst, winW, winH); return true; }
    drawPresent(colour_, w_, vh, w_, h_, true, true, dst, winW, winH);
    return true;
}

// The world through `kPresentSSFrag`: `drawPresent`'s state, the resolve's
// program.
void GlesRenderer::drawPresentSS(int vh, const float dst[4], int winW, int winH) {
    glBindFramebuffer(GL_FRAMEBUFFER, windowFbo_);
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(presentSS_);
    curProg_ = presentSS_;
    glUniform4fv(sDst_, 1, dst);
    glUniform2f(sPicSize_, static_cast<float>(w_), static_cast<float>(vh));
    glUniform2f(sTexSize_, static_cast<float>(rw_), static_cast<float>(rh_));
    glUniform1f(sSS_, static_cast<float>(ss_));
    glUniform1i(sDither_, dither_ ? 1 : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bayer_);
    glUniform1i(sBayer_, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colour_);
    glUniform1i(sPic_, 0);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    for (GLuint a = 1; a <= kAttrPhase; ++a) glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_DEPTH_TEST);
}

bool GlesRenderer::presentSurface(const Surface& s, int winW, int winH) {
    notePresent();
    // A frame the CPU composed - an interface screen, the HUD, a movie - is
    // already 565 and is uploaded as 565: `GL_UNSIGNED_SHORT_5_6_5` is core
    // GLES2, so no conversion touches the pixels on the way.
    if (!surfTex_) {
        glGenTextures(1, &surfTex_);
        glBindTexture(GL_TEXTURE_2D, surfTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    const double t0 = glesClockMs();
    glBindTexture(GL_TEXTURE_2D, surfTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    // ONLY THE ROWS THAT CHANGED, as `presentOverlay` sends its own - and on
    // the Vita, as there, written into the texture's own memory rather than
    // through `glTexSubImage2D`, which first copies the whole texture to a new
    // allocation when the GPU used it in the last frames. This sent the whole
    // surface every frame: 10-15 ms of a console's start-menu frame, and the
    // same for a screen nothing on is moving (the pause menu, a shop).
    //
    // A surface that is MOSTLY moving - the start menu's cloud changes seven
    // rows in ten, the pause screen every one - would pay the row hashes and
    // gain little, so once half the rows have changed the next fifteen frames
    // are copied whole without asking, the hashes thrown away; then one frame
    // takes them again and the next compares.
    static const bool check = std::getenv("OMK_PRESENT_CHECK") != nullptr;
    const std::size_t rowBytes = static_cast<std::size_t>(s.w) * 2u;
    const unsigned char* cur = reinterpret_cast<const unsigned char*>(s.px.data());
    const auto rowHash = [](const unsigned char* p, std::size_t n) {
        std::uint32_t h = 2166136261u;
        const std::uint32_t* q = reinterpret_cast<const std::uint32_t*>(p);
        for (std::size_t i = 0; i < n / 4; ++i) h = (h ^ q[i]) * 16777619u;
        if (n & 2u) h = (h ^ reinterpret_cast<const std::uint16_t*>(p)[n / 2 - 1]) * 16777619u;
        return h;
    };
#if defined(__vita__)
    unsigned char* texData = nullptr;
    const std::size_t stride = static_cast<std::size_t>((s.w + 7) & ~7) * 2u;
#endif
    const auto shadowRow = [&](int y) {
        if (check) std::memcpy(surfShadow_.data() + static_cast<std::size_t>(y) * s.w,
                               cur + y * rowBytes, rowBytes);
    };
    const auto sendRow = [&](int y) {
#if defined(__vita__)
        if (texData) std::memcpy(texData + y * stride, cur + y * rowBytes, rowBytes);
        else
#endif
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, s.w, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                        cur + y * rowBytes);
        shadowRow(y);
    };
    const auto sendAll = [&] {
#if defined(__vita__)
        if (texData) {
            for (int y = 0; y < s.h; ++y) std::memcpy(texData + y * stride, cur + y * rowBytes, rowBytes);
        } else
#endif
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s.w, s.h, GL_RGB,
                        GL_UNSIGNED_SHORT_5_6_5, s.px.data());
        for (int y = 0; check && y < s.h; ++y) shadowRow(y);
        surfRowsSent_ += s.h;
    };
    if (check && surfShadow_.size() != s.px.size()) surfShadow_.assign(s.px.size(), 0);
    if (s.w != surfW_ || s.h != surfH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, s.w, s.h, 0, GL_RGB,
                     GL_UNSIGNED_SHORT_5_6_5, s.px.data());
gpuTexture("interface", surfTex_, 2LL * s.w * s.h);
        for (int y = 0; check && y < s.h; ++y) shadowRow(y);
        surfW_ = s.w; surfH_ = s.h;
        lastOverlay_.clear();
        surfSkip_ = 0;
        surfRowsSent_ += s.h;
    } else {
#if defined(__vita__)
        texData = static_cast<unsigned char*>(vglGetTexDataPointer(GL_TEXTURE_2D));
#endif
        if (surfSkip_ > 0) {
            --surfSkip_;
            lastOverlay_.clear();
            sendAll();
        } else if (lastOverlay_.size() != static_cast<std::size_t>(s.h)) {
            lastOverlay_.resize(static_cast<std::size_t>(s.h));
            for (int y = 0; y < s.h; ++y) lastOverlay_[y] = rowHash(cur + y * rowBytes, rowBytes);
            sendAll();
        } else {
            // the rows that differ, found first: a desktop GL is handed one
            // upload when most of them do, not a call a row
            int changed = 0;
            surfFlagged_.assign(static_cast<std::size_t>(s.h), 0);
            for (int y = 0; y < s.h; ++y) {
                const std::uint32_t hsh = rowHash(cur + y * rowBytes, rowBytes);
                if (hsh == lastOverlay_[y]) continue;
                lastOverlay_[y] = hsh;
                surfFlagged_[y] = 1;
                ++changed;
            }
            bool whole = false;
#if !defined(__vita__)
            // (`OMK_PRESENT_ROWS`: the Vita's decision on a desktop - row by
            // row always - so the check above can see the path a console takes)
            static const bool rowsOnly = std::getenv("OMK_PRESENT_ROWS") != nullptr;
            whole = !rowsOnly && changed * 4 > s.h;
#endif
            if (whole) { sendAll(); surfRowsSent_ -= s.h; }
            else for (int y = 0; y < s.h; ++y) if (surfFlagged_[y]) sendRow(y);
            surfRowsSent_ += changed;
            surfRowsKept_ += s.h - changed;
            if (changed * 2 >= s.h) surfSkip_ = 15;
        }
    }
    // the overlay's own sync takes `surfFlagged_` as "was flagged last frame"
    surfFlagged_.assign(static_cast<std::size_t>(s.h), 1);
    if (check) {
        ++surfCheckFrames_;
        int bad = 0;
        for (int y = 0; y < s.h; ++y)
            bad += std::memcmp(surfShadow_.data() + static_cast<std::size_t>(y) * s.w,
                               cur + y * rowBytes, rowBytes) != 0;
        surfCheckBad_ += bad;
        if (bad || (surfCheckFrames_ % 60) == 0)
            std::printf("gles: present check - %ld frames, %ld rows that would show stale "
                        "(this frame %d); %ld rows sent, %ld kept\n",
                        surfCheckFrames_, surfCheckBad_, bad, surfRowsSent_, surfRowsKept_);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const double t1 = glesClockMs();
    g_glesMs[2] += t1 - t0;
    // row 0 of the Surface is its TOP and was uploaded as texture row 0, which
    // GL samples at t = 0 - so the present pass reads it with NO flip
    float dst[4];
    destRect(s.w, s.h, winW, winH, dst);
    drawPresent(surfTex_, s.w, s.h, s.w, s.h, false, false, dst, winW, winH);
    g_glesMs[3] += glesClockMs() - t1;
    return true;
}


bool GlesRenderer::presentOverlay(const Surface& s, const unsigned char* mask, const unsigned char* maskRows,
                                  const float fade[4], int vy, int vh, int winW, int winH) {
    notePresent();
    if (!maskTex_) {
        if (!overlay_) overlay_ = link(kPresentVert, kOverlayFrag, {{0, "aPos"}});
        if (!overlay_) return false;
        oDst_ = glGetUniformLocation(overlay_, "uDst");
        oPic_ = glGetUniformLocation(overlay_, "uPic");
        oPicSize_ = glGetUniformLocation(overlay_, "uPicSize");
        oMask_ = glGetUniformLocation(overlay_, "uMask");
        oFade_ = glGetUniformLocation(overlay_, "uFade");
        glGenTextures(1, &maskTex_);
        glBindTexture(GL_TEXTURE_2D, maskTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (!presentWorld(vy, vh, s.w, s.h, winW, winH)) return false;
    const double t0 = glesClockMs();
    if (!surfTex_) {
        glGenTextures(1, &surfTex_);
        glBindTexture(GL_TEXTURE_2D, surfTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, surfTex_);
    // (a texture `presentSurface` left at another size holds no row of this)
    if (s.w != surfW_ || s.h != surfH_) lastOverlay_.clear();
    // ONLY THE ROWS THAT CHANGED, and on the Vita not through GL at all.
    // vitaGL's `glTexSubImage2D` first COPIES THE WHOLE TEXTURE to a new
    // allocation when the GPU used it in the last frames (textures.c, "Copying
    // the texture in a new mem location") - 41 ms a frame on a console for the
    // two planes, whatever the size of the change. `vglGetTexDataPointer` is
    // the texture's own memory (565 and L8 are stored as they are, rows padded
    // to 8 pixels), so the changed rows are copied straight into it.
    // WHICH rows changed is found by reading the new plane ONCE: a hash a row
    // against the last frame's (comparing against a kept copy read 3 MB a
    // frame, 18 ms on a console with nothing changed). The mask is looked at
    // only on the rows `maskRows` flags - it is zero everywhere else.
    const auto rowHash = [](const unsigned char* p, std::size_t n) {
        std::uint32_t h = 2166136261u;
        const std::uint32_t* q = reinterpret_cast<const std::uint32_t*>(p);
        for (std::size_t i = 0; i < n / 4; ++i) h = (h ^ q[i]) * 16777619u;
        return h;
    };
    const auto sync = [&](GLuint tex, const unsigned char* cur, std::vector<std::uint32_t>& last,
                          int bpp, GLenum fmt, GLenum type, const unsigned char* rows,
                          std::vector<unsigned char>& wasFlagged) {
        glBindTexture(GL_TEXTURE_2D, tex);
        const std::size_t rowBytes = static_cast<std::size_t>(s.w) * bpp;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (last.size() != static_cast<std::size_t>(s.h)) {
            glTexImage2D(GL_TEXTURE_2D, 0, fmt, s.w, s.h, 0, fmt, type, cur);
gpuTexture("interface", tex, static_cast<long long>(bpp) * s.w * s.h);
            last.resize(static_cast<std::size_t>(s.h));
            for (int y = 0; y < s.h; ++y) last[y] = rowHash(cur + y * rowBytes, rowBytes);
            wasFlagged.assign(static_cast<std::size_t>(s.h), 1);
            overlayRows_ += s.h;
        } else {
#if defined(__vita__)
            unsigned char* texData = static_cast<unsigned char*>(vglGetTexDataPointer(GL_TEXTURE_2D));
            const std::size_t stride = static_cast<std::size_t>((s.w + 7) & ~7) * bpp;
#endif
            for (int y = 0; y < s.h; ++y) {
                if (rows && !rows[y] && !wasFlagged[y]) continue;
                wasFlagged[y] = rows ? rows[y] : 1;
                const std::uint32_t hsh = rowHash(cur + y * rowBytes, rowBytes);
                if (hsh == last[y]) continue;
                last[y] = hsh;
#if defined(__vita__)
                if (texData) std::memcpy(texData + y * stride, cur + y * rowBytes, rowBytes);
                else
#endif
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, s.w, 1, fmt, type, cur + y * rowBytes);
                ++overlayRows_;
            }
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    };
    sync(maskTex_, mask, lastMask_, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, maskRows, maskFlagged_);
    sync(surfTex_, reinterpret_cast<const unsigned char*>(s.px.data()), lastOverlay_, 2, GL_RGB,
         GL_UNSIGNED_SHORT_5_6_5, nullptr, surfFlagged_);
    surfW_ = s.w; surfH_ = s.h;
    const double t1 = glesClockMs();
    g_glesMs[2] += t1 - t0;
    float dst[4];
    destRect(s.w, s.h, winW, winH, dst);
    glBindFramebuffer(GL_FRAMEBUFFER, windowFbo_);
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_SRC_ALPHA);
    glUseProgram(overlay_);
    curProg_ = overlay_;
    glUniform4fv(oDst_, 1, dst);
    glUniform2f(oPicSize_, static_cast<float>(s.w), static_cast<float>(s.h));
    glUniform4fv(oFade_, 1, fade);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    glUniform1i(oMask_, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surfTex_);
    glUniform1i(oPic_, 0);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    for (GLuint a = 1; a <= kAttrPhase; ++a) glDisableVertexAttribArray(a);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    g_glesMs[3] += glesClockMs() - t1;
    return true;
}

// ---- the factory and the window-side calls, DECLARED by a caller rather
// than included, the pattern `play.cpp` uses for Vulkan: a frontend that
// links this file needs no GL header of its own to call them.
Renderer* makeGlesRenderer() { return new GlesRenderer(); }

bool glesPresentWorld(Renderer* r, int vy, int vh, int frameW, int frameH, int winW, int winH) {
    return static_cast<GlesRenderer*>(r)->presentWorld(vy, vh, frameW, frameH, winW, winH);
}
bool glesPresentOverlay(Renderer* r, const Surface& s, const unsigned char* mask, const unsigned char* maskRows,
                        const float fade[4], int vy, int vh, int winW, int winH) {
    return static_cast<GlesRenderer*>(r)->presentOverlay(s, mask, maskRows, fade, vy, vh, winW, winH);
}
long glesTakeOverlayRows(Renderer* r) { return static_cast<GlesRenderer*>(r)->takeOverlayRows(); }
// geometries released so far (todo/optimization.md step 26), and the vertex
// and posed buffers still held
void glesGeometryStats(Renderer* r, long out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (auto* g = dynamic_cast<GlesRenderer*>(r)) g->geometryStats(out);
}

// the anisotropy the context granted (1: none), for a probe
int glesAnisotropy(Renderer* r) {
    auto* g = dynamic_cast<GlesRenderer*>(r);
    return g ? g->anisotropy() : 1;
}

// the supersample factor the target was made at (1: off), for a probe
int glesSupersample(Renderer* r) {
    auto* g = dynamic_cast<GlesRenderer*>(r);
    return g ? g->supersample() : 1;
}

// the MSAA count the target was made with, and the context's limit
void glesSamples(Renderer* r, int* got, int* most) {
    auto* g = dynamic_cast<GlesRenderer*>(r);
    *got = g ? g->samples() : 1;
    *most = g ? g->maxSamples() : 1;
}

// per-pixel lighting and the mapped shadow asked for, BEFORE `init`
// (`setEnhancedLighting`)
void glesSetEnhancedLighting(Renderer* r, bool perPixel, bool shadowMap) {
    if (auto* g = dynamic_cast<GlesRenderer*>(r)) g->setEnhancedLighting(perPixel, shadowMap);
}

// the draw-state cache on or off, for a probe that compares the two
void glesSetStateCache(Renderer* r, bool on) {
    if (auto* g = dynamic_cast<GlesRenderer*>(r)) g->setStateCache(on);
}
bool glesPresentSurface(Renderer* r, const Surface& s, int winW, int winH) {
    return static_cast<GlesRenderer*>(r)->presentSurface(s, winW, winH);
}
void glesWindowPicture(Renderer* r, int w, int h, std::vector<unsigned char>& out) {
    static_cast<GlesRenderer*>(r)->windowPicture(w, h, out);
}
void glesSetStretch(Renderer* r, bool on) { static_cast<GlesRenderer*>(r)->setStretch(on); }
void glesSetDepthTie(Renderer* r, bool on) { static_cast<GlesRenderer*>(r)->setDepthTie(on); }
void glesSetWindowTarget(Renderer* r, unsigned fbo) {
    static_cast<GlesRenderer*>(r)->setWindowTarget(static_cast<GLuint>(fbo));
}

}  // namespace omk

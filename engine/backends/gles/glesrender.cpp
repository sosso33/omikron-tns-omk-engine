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
// WHAT IS NOT HERE, and each is an ENHANCEMENT or has a CPU fallback:
//   * the mapped shadow pass and the per-pixel lights (`View::lights`,
//     `Draw::lit`) - enhancements, off by default (`todo/enhancements.md`);
//     `lit` is ignored and the vertex colour stands, which is what the
//     default game draws;
//   * MSAA, texture filtering, anisotropy, supersampling - enhancements;
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

// The one call whose NAME differs: GLES has only the float form, desktop GL
// 2.1 only the double one.
#if defined(__APPLE__)
#  define OMK_GL_CLEAR_DEPTH(d) glClearDepth(d)
#else
#  define OMK_GL_CLEAR_DEPTH(d) glClearDepthf(d)
#endif

#include "o3de/depthtie.h"
#include "o3de/renderer.h"
#include "o3de/shimmer.h"
#include "ui/surface.h"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    float wave = 0.0;
    if (aPhase >= 0.0) {
        // ((int(clock) >> 2) + int(phase)) & 31, with no integer operators:
        // both are non-negative, so floor(x / 4) is the shift and mod the mask
        float i = mod(floor(floor(uShimmerClock) / 4.0) + floor(aPhase), 32.0);
        wave = waveAt(i);
    }
    vCol = aCol + vec3(wave);
    gl_Position = uMvp * vec4(aPos, 1.0);
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

GpuVert gpuVert(const omk::Corner& c) {
    return {c.x, c.y, c.z, c.u, c.v, c.r, c.g, c.b, c.phase};
}

constexpr GLuint kAttrPos = 0, kAttrUV = 1, kAttrCol = 2, kAttrPhase = 3;

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

namespace omk {

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
    long uploads = 0, uploadBytes = 0, patches = 0, patchedBuffers = 0, draws = 0;
} g_glesFrame;
double glesClockMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
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
    ~GlesRenderer() override;
    bool init(int w, int h) override;
    void setTextures(std::span<const Texture> t) override;
    void begin(const View& v) override;
    void submit(const Draw& d) override;
    void end() override;
    const Surface& readback() override;
    RasterStats stats() const override { return st_; }
    const char* name() const override { return "gles2"; }

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
    struct Vbo { GLuint id = 0; std::size_t n = 0; std::uint64_t rev = 0; };

    bool uploadGeometry(const Geometry* g);
    void resolveTies(const Draw& d, Vbo& vb);
    void destRect(int picW, int picH, int winW, int winH, float out[4]) const;
    // One picture onto the window: `picW x picH` pixels of a `texW x texH`
    // texture, into the NDC rectangle `dst`.
    void drawPresent(GLuint tex, int picW, int picH, int texW, int texH, bool flipY,
                     bool quantise, const float dst[4], int winW, int winH);

    int w_ = 0, h_ = 0;
    bool ready_ = false;
    GLuint prog_ = 0, present_ = 0;
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
public:
    long takeOverlayRows() { const long r = overlayRows_; overlayRows_ = 0; return r; }
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
    GLuint windowFbo_ = 0;

    View view_;
    bool fog_ = false, dither_ = true, stretch_ = false;
    float fogStart_ = 0, fogEnd_ = 0, fogColour_[3] = {0, 0, 0};
    bool recording_ = false, dirty_ = false;
    Surface fb_{1, 1, 0};
    std::vector<unsigned char> rgba_;
    RasterStats st_;
};

GlesRenderer::~GlesRenderer() {
    for (auto& [g, vb] : vbo_) glDeleteBuffers(1, &vb.id);
    for (auto& t : tex_) if (t.id) glDeleteTextures(1, &t.id);
    if (white_.id) glDeleteTextures(1, &white_.id);
    if (bayer_) glDeleteTextures(1, &bayer_);
    if (surfTex_) glDeleteTextures(1, &surfTex_);
    if (quad_) glDeleteBuffers(1, &quad_);
    if (colour_) glDeleteTextures(1, &colour_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (prog_) glDeleteProgram(prog_);
    if (present_) glDeleteProgram(present_);
}

bool GlesRenderer::init(int w, int h) {
    w_ = w; h_ = h;
    fb_ = Surface(w, h, 0);
    rgba_.assign(static_cast<std::size_t>(w) * h * 4, 0);
    if (ready_) {
        // a resize: only the target changes
        glBindTexture(GL_TEXTURE_2D, colour_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
        return true;
    }
    prog_ = link(kSceneVert, kSceneFrag, {{kAttrPos, "aPos"}, {kAttrUV, "aUV"},
                                          {kAttrCol, "aCol"}, {kAttrPhase, "aPhase"}});
    present_ = link(kPresentVert, kPresentFrag, {{0, "aPos"}});
    if (!prog_ || !present_) return false;
    // ALL THREE PROGRAMS AT START: a shader first linked mid-game would be
    // missing from a shader cache made by one short run (`todo/vita-port.md`,
    // the precompiled shaders). Its uniforms are still looked up on first use.
    overlay_ = link(kPresentVert, kOverlayFrag, {{0, "aPos"}});
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    const GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (fbs != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "gles: render target incomplete (0x%x)\n", fbs);
        return false;
    }

    // the white 1x1 a material with no texture binds - raster.cpp leaves the
    // texel at 255 and lets the vertex colour stand alone
    const unsigned char wpx[4] = {255, 255, 255, 255};
    glGenTextures(1, &white_.id);
    glBindTexture(GL_TEXTURE_2D, white_.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wpx);

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

    const float quad[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &quad_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    static const bool noTie = std::getenv("OMK_NO_TIE") != nullptr;
    if (noTie) tieOn_ = false;
    ready_ = true;
    return true;
}

void GlesRenderer::setTextures(std::span<const Texture> t) {
    for (auto& x : tex_) if (x.id) glDeleteTextures(1, &x.id);
    tex_.assign(t.size(), Tex{});
    std::vector<unsigned char> px;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const auto& s = t[i];
        if (s.width <= 0 || s.height <= 0 || s.rgb.empty() ||
            s.rgb.size() < static_cast<std::size_t>(s.width) * s.height * 3) continue;
        const int n = s.width * s.height;
        px.resize(static_cast<std::size_t>(n) * 4);
        const unsigned char* rgb = s.rgb.data();
        // alpha carries the COLOUR KEY, exactly as the Vulkan upload does:
        // 0 where the texel is black, 255 elsewhere
        for (int k = 0; k < n; ++k) {
            px[4 * k + 0] = rgb[3 * k + 0];
            px[4 * k + 1] = rgb[3 * k + 1];
            px[4 * k + 2] = rgb[3 * k + 2];
            px[4 * k + 3] = (rgb[3 * k] | rgb[3 * k + 1] | rgb[3 * k + 2]) ? 255 : 0;
        }
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
        auto& out = tex_[i];
        glGenTextures(1, &out.id);
        glBindTexture(GL_TEXTURE_2D, out.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.width, s.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, px.data());
        out.w = static_cast<float>(s.width);
        out.h = static_cast<float>(s.height);
    }
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

bool GlesRenderer::uploadGeometry(const Geometry* g) {
    // The Vulkan backend's contract, verbatim in intent: cached by POINTER
    // while the revision holds; the same object with new vertices refills in
    // place, and when the geometry names the corners that moved since the
    // revision this buffer holds, only those are written.
    auto it = vbo_.find(g);
    if (it != vbo_.end() && it->second.rev == g->revision) return true;
    if (g->corners.empty()) return false;
    std::vector<GpuVert>& v = up_;
    v.clear();
    ++g_glesFrame.uploads;
    g_glesFrame.uploadBytes += static_cast<long>(g->corners.size() * sizeof(GpuVert));
    if (it != vbo_.end() && it->second.n == g->corners.size()) {
        Vbo& vb = it->second;
        static const bool noDirty = std::getenv("OMK_NO_DIRTY") != nullptr;
        const bool partial = !noDirty && g->dirtyTo != 0 && g->dirtyTo == g->revision &&
                             vb.rev == g->dirtyFrom;
        glBindBuffer(GL_ARRAY_BUFFER, vb.id);
        if (partial) {
            // one call a corner would be thousands of driver calls for a
            // walker; the dirty list is sorted by construction, so coalesce
            // it into runs and send each run once
            const auto& dc = g->dirtyCorners;
            std::size_t i = 0;
            while (i < dc.size()) {
                std::size_t j = i;
                while (j + 1 < dc.size() && dc[j + 1] == dc[j] + 1) ++j;
                const std::uint32_t lo = dc[i], hi = dc[j];
                if (hi < g->corners.size()) {
                    v.resize(hi - lo + 1);
                    for (std::uint32_t k = lo; k <= hi; ++k) v[k - lo] = gpuVert(g->corners[k]);
                    patchArrayBuffer(lo * sizeof(GpuVert), v.size() * sizeof(GpuVert), v.data());
                }
                i = j + 1;
            }
        } else {
            v.resize(g->corners.size());
            for (std::size_t k = 0; k < v.size(); ++k) v[k] = gpuVert(g->corners[k]);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(v.size() * sizeof(GpuVert)), v.data());
            tie_[g].vboReplaced();
        }
        vb.rev = g->revision;
        return true;
    }
    Vbo vb;
    if (it != vbo_.end()) { vb.id = it->second.id; }
    else glGenBuffers(1, &vb.id);
    v.resize(g->corners.size());
    for (std::size_t k = 0; k < v.size(); ++k) v[k] = gpuVert(g->corners[k]);
    glBindBuffer(GL_ARRAY_BUFFER, vb.id);
    // DYNAMIC: a posed body rewrites its buffer every frame. What vitaGL does
    // with a buffer the GPU may still be reading is the open question the
    // device must answer (`todo/vita-port.md` G3).
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(GpuVert)),
                 v.data(), GL_DYNAMIC_DRAW);
    vb.n = v.size();
    vb.rev = g->revision;
    vbo_[g] = vb;
    tie_[g].vboReplaced();
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
    auto& t = tie_[g];
    std::vector<std::size_t>& losers = losers_;
    std::vector<std::size_t>& restore = restore_;
    losers.clear();
    restore.clear();
    t.resolve(*g, d.start, d.count, d.blend == Blend::Opaque, losers, restore);
    if (losers.empty() && restore.empty()) return;
    g_glesInTie = true;
    glBindBuffer(GL_ARRAY_BUFFER, vb.id);
    GpuVert tri[3];
    for (const std::size_t k : restore) {
        const std::size_t c = 3 * k;
        if (c + 2 >= vb.n || c + 2 >= g->corners.size()) continue;
        for (int j = 0; j < 3; ++j) tri[j] = gpuVert(g->corners[c + j]);
        patchArrayBuffer(c * sizeof(GpuVert), sizeof tri, tri);
    }
    for (const std::size_t k : losers) {
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

void GlesRenderer::begin(const View& view) {
    g_glesFrame = GlesFrameCounts{};
    st_ = RasterStats{};
    view_ = view;
    fog_ = view.fog;
    fogStart_ = view.fogStart;
    fogEnd_ = view.fogEnd;
    for (int i = 0; i < 3; ++i) fogColour_[i] = static_cast<float>(view.fogColour[i]) / 255.0f;
    dither_ = view.dither;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   // black, as the engine clears
    OMK_GL_CLEAR_DEPTH(1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // The letterbox is a VIEWPORT in the TOP-LEFT `vw x vh` of the target, as
    // the Vulkan backend places it. GL's window origin is bottom-left, so the
    // top-left rectangle starts at row `h - vh`.
    RCamera cam = view.cam;
    const int vw = view.letterboxed() ? view.vw : w_;
    const int vh = view.letterboxed() ? view.vh : h_;
    cam.w = vw; cam.h = vh;
    glViewport(0, h_ - vh, vw, vh);

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

    glUseProgram(prog_);
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp);
    glUniform1f(uClock_, view.shimmerClock);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    recording_ = true;
}

void GlesRenderer::submit(const Draw& d) {
    if (!recording_ || !d.geo || !d.count) return;
    const double fu0 = glesClockMs();
    const bool uploaded = uploadGeometry(d.geo);
    g_glesFrame.uploadMs += glesClockMs() - fu0;
    if (!uploaded) return;
    Vbo& vb = vbo_[d.geo];
    const double ft0 = glesClockMs();
    resolveTies(d, vb);
    g_glesFrame.tieMs += glesClockMs() - ft0;
    st_.triangles += static_cast<long>(d.count / 3);

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

    // The texture is the key's LOW SIX BITS and nothing else (ASSETS 4b).
    const std::size_t slot = d.bucketKey & 0x3Fu;
    const Tex& t = (slot < tex_.size() && tex_[slot].id) ? tex_[slot] : white_;
    glBindTexture(GL_TEXTURE_2D, t.id);
    glUniform2f(uTexSize_, t.w, t.h);
    glUniform1i(uCutout_, d.cutout ? 1 : 0);

    // THE FOG's two exclusions - the rule `renderer.cpp` applies for the
    // software loop and `vkrender.cpp` for Vulkan.
    float fs = 0.0f, fe = 0.0f;
    if (fog_ && !(d.bucketKey & 0x2080u)) {
        const float k = (d.bucketKey & 0x800u) ? 2.0f : 1.0f;
        fs = fogStart_ * k;
        fe = fogEnd_ * k;
    }
    glUniform1f(uFogStart_, fs);
    glUniform1f(uFogEnd_, fe);
    glUniform3f(uFogColour_, fogColour_[0], fogColour_[1], fogColour_[2]);

    glBindBuffer(GL_ARRAY_BUFFER, vb.id);
    const GLsizei st = sizeof(GpuVert);
    glEnableVertexAttribArray(kAttrPos);
    glEnableVertexAttribArray(kAttrUV);
    glEnableVertexAttribArray(kAttrCol);
    glEnableVertexAttribArray(kAttrPhase);
    glVertexAttribPointer(kAttrPos, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(0));
    glVertexAttribPointer(kAttrUV, 2, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(12));
    glVertexAttribPointer(kAttrCol, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(20));
    glVertexAttribPointer(kAttrPhase, 1, GL_FLOAT, GL_FALSE, st, reinterpret_cast<const void*>(32));
    const double fd0 = glesClockMs();
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(d.start), static_cast<GLsizei>(d.count));
    g_glesFrame.drawMs += glesClockMs() - fd0;
    ++g_glesFrame.draws;
    st_.drawn += static_cast<long>(d.count / 3);
}

void GlesRenderer::end() {
    if (!recording_) return;
    recording_ = false;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
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
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
    const double t1 = glesClockMs();
    g_glesMs[0] += t1 - t0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    drawPresent(colour_, w_, vh, w_, h_, true, true, dst, winW, winH);
    return true;
}

bool GlesRenderer::presentSurface(const Surface& s, int winW, int winH) {
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
    lastOverlay_.clear();   // the texture is about to hold something else
    glBindTexture(GL_TEXTURE_2D, surfTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (s.w != surfW_ || s.h != surfH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, s.w, s.h, 0, GL_RGB,
                     GL_UNSIGNED_SHORT_5_6_5, s.px.data());
        surfW_ = s.w; surfH_ = s.h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s.w, s.h, GL_RGB,
                        GL_UNSIGNED_SHORT_5_6_5, s.px.data());
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

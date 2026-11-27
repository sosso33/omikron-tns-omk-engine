#version 450
// The scene vertex shader. The transform is handed down as a finished matrix
// from `cameraBasis` - the CPU side owns the conventions (world up is (0,-1,0)
// because the game's Y points DOWN; hfov is HORIZONTAL) so that this file
// cannot disagree with the software rasterizer about them.
layout(push_constant) uniform Push {
    mat4  mvp;
    int   cutout;      // flag 0x800: a colour key on black, never alpha
    float fogStart;    // 0 = no fog for this batch; see renderer.h's View
    float fogEnd;
    int   caster;      // a caster does not receive; see scene.frag
    vec3  fogColour;   // 16-BYTE ALIGNED at offset 80 - see scene.frag
    int   lit;         // 92, and it must come AFTER the vec3
    int   cull;        // 96, the back-face cull - see scene.frag
    int   posed;       // 100, a body POSED here from set 2
    int   texScaled;   // 104, a TEXTURE PACK replacement - see scene.frag
    float texScaleU;   // 108
    float texScaleV;   // 112
    int   bloomMode;   // 116, the BLOOM's light map - see scene.frag
    int   water;       // 120, the WATER reflection - see scene.frag
    float waterStrength; // 124
} pc;

// THE POSE BUFFER (`vkrender.cpp` `posesBodies`, 2026-10-11 - as GLES, GL1 and
// the 3DS pose): a posed draw's corners are its REST corners and this block,
// at the body's offset - d[0] (lights, from black, base grey, affines), then
// kLights lights of two vec4 (direction x strength, colour bytes), then three
// vec4 an affine: world = R rest + t, the rows of a 3x4 (`renderer.h`), then
// one vec4 the body light's tint (rgb).
const int kLights = 32;   // vkrender.cpp's kPoseLights
layout(std430, set = 2, binding = 0) readonly buffer Pose { vec4 d[]; } pose;

layout(location = 0) in vec3 inPos;    // world position
layout(location = 1) in vec2 inUV;     // TEXEL units, as the shipped data stores them
layout(location = 2) in vec3 inCol;    // the baked light - a COLOUR, not a brightness
layout(location = 3) in vec3 inNrm;    // the vertex NORMAL, for row 7's lighting
layout(location = 4) in float inPhase; // the SHIMMER's phase, -1 = none
layout(location = 5) in float inMesh;  // the corner's mesh, -1 = none (a posed draw's affine)

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vCol;
// The VIEW-SPACE depth, in world units, for the fog. Row 3 of `mvp` is
// `f . (world - eye)` - the forward axis dotted with the offset from the eye -
// so `gl_Position.w` is exactly the `z` raster.cpp's inner loop fogs on.
layout(location = 2) out float vDepth;
// The WORLD position, which the fragment stage needs to look itself up in the
// shadow map. Carried rather than reconstructed: the map's matrix is a
// separate transform and there is no inverse of `mvp` here.
layout(location = 3) out vec3 vWorld;
layout(location = 4) out vec3 vNrm;
layout(location = 5) out float vPhase;

void main() {
    vec3 pos = inPos, nrm = inNrm, col = inCol;
    if (pc.posed != 0) {
        // PLACED by its mesh's affine (a corner no mesh owns stays at rest),
        // the normal turned and not moved
        vec4 h = pose.d[0];
        int m = int(floor(inMesh + 0.5));
        if (m >= 0 && float(m) < h.w) {
            int b = 1 + 2 * kLights + 3 * m;
            vec4 r0 = pose.d[b], r1 = pose.d[b + 1], r2 = pose.d[b + 2];
            pos = vec3(dot(r0.xyz, inPos) + r0.w, dot(r1.xyz, inPos) + r1.w, dot(r2.xyz, inPos) + r2.w);
            nrm = vec3(dot(r0.xyz, inNrm), dot(r1.xyz, inNrm), dot(r2.xyz, inNrm));
        }
        // ...and LIT, corner by corner as `applyLights` walks it: from the
        // scene's grey where the body starts black, else from its colour; t =
        // -(N.L) truncated toward zero and clamped to 0..255, the ramp
        // `(t * c) >> 8` (exact in float), each light added and clamped
        // ...from a start colour times THE BODY LIGHT's tint (the remaster's
        // A5, `o3de/lightprobe.h`), the vec4 after the affines - 1, 1, 1
        // unless it is on, which multiplies nothing
        vec3 tint = pose.d[1 + 2 * kLights + 3 * int(h.w + 0.5)].rgb;
        col = inCol * tint;
        int n = int(h.x);
        if (n > 0 || h.y > 0.5) {
            vec3 lit = (h.y > 0.5 ? vec3(h.z) : inCol) * tint;
            for (int i = 0; i < kLights; ++i) {
                if (i >= n) break;
                vec4 L = pose.d[1 + 2 * i], C = pose.d[2 + 2 * i];
                float t = -dot(nrm, L.xyz);
                float ti = clamp(t < 0.0 ? ceil(t) : floor(t), 0.0, 255.0);
                vec3 a = floor(ti * C.rgb / 256.0) / 255.0;
                lit = min(lit + a, vec3(1.0));
            }
            col = lit;
        }
    }
    vUV  = inUV;
    vCol = col;
    vWorld = pos;
    vNrm = nrm;
    // A TIE LOSER (`bakeDepthTie`) carries its phase moved down by 8192: -1
    // becomes -8193, a phase p becomes p - 8192, both decoded exactly here
    float ph = inPhase;
    float tie = 0.0;
    if (ph < -4096.0) { ph += 8192.0; tie = 1.0; }
    vPhase = ph;
    gl_Position = pc.mvp * vec4(pos, 1.0);
    vDepth = gl_Position.w;
    // ...drawn TWO 16-bit depth steps back (Vulkan's window depth IS the NDC
    // z), so the earlier coincident face keeps every pixel under LESS whatever
    // the noise between the two triangulations: the band a later face must
    // beat under the engine's strict test on its quantised buffer (ASSETS 4b)
    gl_Position.z += tie * (2.0 / 65535.0) * gl_Position.w;
}

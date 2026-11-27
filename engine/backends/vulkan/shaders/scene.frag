#version 450
// The scene fragment shader, and it is a transcription of `raster.cpp`'s inner
// loop rather than a rendering choice:
//
//   * the texel is the material's own pixel units WRAPPED, which is what lets
//     one atlas tile across a wall - so the UVs arrive unnormalised and are
//     divided by textureSize here, with the sampler set to REPEAT;
//   * the baked vertex value is a COLOUR and multiplies the texel (ASSETS 4c);
//     38.9% of set vertices are not grey, and reading one byte as a brightness
//     renders every set in monochrome, which was this repo's own bug;
//   * `cutout` is flag 0x800 - a COLOUR KEY on black, the engine's
//     SetRenderState(27, 1) arm - and never alpha;
//   * the fog is linear over the clip distance's range, the CPU having
//     already applied the bucket key's exclusions (`renderer.h`, the View).
layout(push_constant) uniform Push {
    mat4  mvp;
    int   cutout;      // flag 0x800: a colour key on black, never alpha
    float fogStart;    // 0 = no fog for this batch; see renderer.h's View
    float fogEnd;
    // A CASTER does not RECEIVE. The engine's shadow only ever darkens the
    // ground, and a map that also shadowed the caster would put its own
    // silhouette across a 1999 character's chest.
    //
    // **It sits HERE, before the vec3, and that is not a style choice.** A
    // `vec3` in a push-constant block is 16-BYTE ALIGNED, so `fogColour`
    // lands at offset 80 whatever precedes it; the C++ struct had it packed at
    // 76 and the two had disagreed since the fog landed. Nobody saw it because
    // the shipped fog colour is BLACK (ASSETS, "The fog"), so the four bytes
    // that arrived in the wrong place were zeros either way. Adding an int
    // after the vec3 is what made it visible: `caster` read the padding and
    // every character shadowed itself.
    int   caster;
    vec3  fogColour;
    // Row 7: this batch is LIT per pixel, so its baked vertex colour is not
    // its shading and the light starts from BLACK.
    //
    // **AFTER the vec3, and that is forced.** `fogColour` is 16-byte aligned,
    // so it sits at offset 80 and anything put in front of it pushes it to
    // 96 - which silently broke this the first time: `lit` was never 1 and the
    // whole enhancement drew nothing. The scalars before it fill 64..80
    // exactly; a new one goes here.
    int   lit;          // 92
    // The BACK-FACE CULL (`geom3do.h` kTwoSided): 0 draws both sides, +1
    // discards where !gl_FrontFacing, -1 where gl_FrontFacing - the engine's
    // software test in `Render_SubmitMesh`, which the device never sees.
    int   cull;         // 96
    int   posed;        // 100, a body posed in `scene.vert` (set 2)
    // THE TEXTURE PACK (`o3de/texpack.h`): the bound texture is a replacement
    // this many times the original's size, while the UVs are still in the
    // ORIGINAL's texels. 0 on every shipped texture, which then divides by
    // its own size exactly as before.
    int   texScaled;    // 104
    float texScaleU;    // 108
    float texScaleV;    // 112
    // THE BLOOM (`post.frag`): 0 off - alpha 1, as always; 1 on - the alpha
    // is this texel's LIGHT, the texture pack's light map (binding 1)
    int   bloomMode;    // 116
    // THE WATER REFLECTION (`todo/remaster.md` A3): 1 this draw is WATER and
    // adds the reflected scene (binding 2) at its own screen position, times
    // `waterStrength`; 2 this is the REFLECTION pass, and `waterStrength` is
    // the water's height - what lies below it is discarded; 3 a REFLECTING
    // FLOOR's draw, as 1 with its relief (`o3de/floorreflect.h`); 0 otherwise.
    // Those are the LOW FOUR BITS: above them a WET floor's darkening, 0..255
    // (`o3de/wetfloor.h`), and above that the RAIN's, 0..255 (`View::rain`, its
    // ripples) - `waterMode`, `wetFilm` and `rainNow` below; bits 20-22 flags
    int   water;          // 120
    float waterStrength;  // 124
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex;
// the texture's LIGHT MAP (`o3de/texpack.h`), black where the pack has none;
// read only with the bloom on
layout(set = 0, binding = 1) uniform sampler2D emis;
// the WATER pass's reflection, the scene through the camera reflected in the
// water plane (black unless a water pass made one); read only by water draws
layout(set = 0, binding = 2) uniform sampler2D refl;
#ifdef OMK_TILED
// THE NORMAL MAP (the pack's `<key>_n.png`, the remaster's B3) - the relief's;
// a flat one of ALPHA 0 where the texture has none
layout(set = 0, binding = 3) uniform sampler2D nmap;
#endif

// ---- the MAPPED shadow, `todo/enhancements.md` 6 - an ENHANCEMENT ----------
//
// Set 1 is per FRAME, not per texture: the light's transform and the depth
// map it filled. `strength` of 0 means the frame has no shadow light, which is
// the default and every classic and fitted frame.
layout(set = 1, binding = 0) uniform Shadow {
    mat4  lightMvp;
    float strength;    // 0 = no shadow this frame
    float texel;       // 1 / the map's side, for the PCF tap spacing
    float bias;        // depth slop, in the map's own 0..1 units
    float shimmer;     // THE SHIMMER's clock - see the note below
} sh;
layout(set = 1, binding = 1) uniform sampler2D shadowMap;

// ---- PER-PIXEL LIGHTING, `todo/enhancements.md` row 7 ----------------------
//
// `sub_493E40` transcribed, evaluated per fragment. Not a new law: the same
// squared-radius reach test, the same LINEAR falloff between the two radii,
// the same `k = intensity * 256 * fall`, the same `-(N.L)` and the same
// `(t * c) >> 8` ramp `o3de/vertexlight.cpp` runs per vertex.
struct GpuLight {
    vec4 posA;      // xyz position, w outer radius
    vec4 dirB;      // xyz direction, w inner radius
    vec4 colourI;   // rgb colour 0..1, a intensity
};
layout(set = 1, binding = 2) uniform Lights {
    GpuLight l[64];    // View::kMaxGpuLights
    int   count;
    float base;        // View::litBase - the scene's +416, what a lit batch starts from
    int   grey;        // View::grey - the greyscale bank, ops 150/151
    int   tiles;       // 1: the TILED point lights below are bound (`o3de/lighttiles.h`)
    vec4  eye;         // the camera, for the water's Fresnel (xyz); w the NIGHT, View::setDim
    vec4  fx;          // x: the RELIEF's strength (View::relief) - the tiled module's alone;
                       // y, z, w: the MIST's strength, ground and height (View::mist...), both
    vec4  dust;        // THE SAND'S DUST (View::dust): strength, the drift's x and z, the distance scale
    vec4  dustColour;  // ...its colour, lit as the scene (rgb)
    vec4  veil;        // THE RAIN'S VEILS (View::veil): strength, the fall's offset, the distance scale;
                       // w the PUDDLES' strength (View::puddles)
    vec4  veilColour;  // ...their colour, lit as the scene (rgb), and the gust's lean (w, `View::veilGust`)
    vec4  streakN;     // THE LIGHTS' STREAKS on the wet floors: x how many, y their strength (View::streaks)
    GpuLight streak[16];   // ...the lights (View::streakLights, kMaxStreakLights): posA, colourI
} lg;

// THE TILED POINT LIGHTS (`o3de/lighttiles.h`, the remaster's): every point
// light of the frame, and the screen's 32-pixel tiles cut in 16 depth slices -
// [0] across, [1] down, [2] slices, [3] the tile's size - and above its low
// 16 bits the pass's x ORIGIN, the second floor level's reflection drawn in
// the image's right half - two words a cluster (its list's offset, its
// count), then the lists
//
// COMPILED TWICE (the Makefile's `scene_tiled.frag.inc`, -DOMK_TILED): these
// buffers and the loop that reads them only in the TILED module, which the
// backend binds only on a frame whose tiles are on. Present but never run,
// they still changed a plain frame through MoltenVK - a cutout sign's keyed
// texels drew dark (4317 pixels in Qalisar, 2026-10-20) - so the default
// module is the one without them.
#ifdef OMK_TILED
layout(std430, set = 1, binding = 3) readonly buffer Points { GpuLight p[]; } pts;
layout(std430, set = 1, binding = 4) readonly buffer Tiles { uint t[]; } tl;
#endif
// (the fragment's VIEW depth, the fog's - declared here for the tiles' slice)
layout(location = 2) in float vDepth;

// one POINT light (a negative inner radius, `o3de/emitterlight.h`): from the
// light to the fragment, the falloff from |inner| squared, nothing at its own
// housing (30 to 90 units), 0.3 wrapped round the far side
vec3 pointLit(vec3 n, vec3 w, GpuLight L) {
    vec3  d  = w - L.posA.xyz;
    float d2 = dot(d, d);
    float rA = L.posA.w, rB = abs(L.dirB.w);
    if (d2 > rA * rA || rA <= rB) return vec3(0.0);
    float fall = min(1.0 - (sqrt(d2) - rB) / (rA - rB), 1.0);
    float k = L.colourI.a * 256.0 * fall;
    if (k <= 0.0 || d2 <= 0.0) return vec3(0.0);
    vec3 dir = d / sqrt(d2);
    float t = k * fall * clamp((sqrt(d2) - 30.0) / 60.0, 0.0, 1.0) * (0.3 + 0.7 * max(-dot(n, dir), 0.0));
    if (t <= 0.0) return vec3(0.0);
    return min(t * L.colourI.rgb / 256.0, vec3(1.0));
}

// `pointsOnly`: the remaster's mode 3 - a SET lit by the emitters' point
// lights alone (its own lights are already in its baked colour)
vec3 litColour(vec3 n, vec3 w, bool pointsOnly) {
    vec3 c = vec3(0.0);
    for (int i = 0; i < lg.count; ++i) {
        vec3  p  = lg.l[i].posA.xyz;
        float rA = lg.l[i].posA.w, rB = lg.l[i].dirB.w;
        vec3  d  = w - p;
        float d2 = dot(d, d);
        if (d2 > rA * rA) continue;      // the engine's own reach test
        // THE REMASTER'S POINT LIGHT (`o3de/emitterlight.h`): a negative
        // inner radius - no shipped light has one - shines from the light to
        // the fragment, the falloff from |inner|
        bool point = rB < 0.0;
        if (pointsOnly && !point) continue;
        rB = abs(rB);
        if (rA <= rB) continue;          // a degenerate pair lights nothing
        float fall = 1.0 - (sqrt(d2) - rB) / (rA - rB);
        fall = min(fall, 1.0);
        float k = lg.l[i].colourI.a * 256.0 * fall;
        if (k <= 0.0) continue;
        // `-(N.L)`, with the falloff folded into L exactly as the engine
        // folds `v46` in before the dot.
        vec3 dir = point ? (d2 > 0.0 ? d / sqrt(d2) : vec3(0.0)) : lg.l[i].dirB.xyz;
        float t = -dot(n, dir * k);
        // ...and a point light WRAPS 0.3 of itself round to the far side, as
        // the walls about a flame would bounce it
        // (and nothing at its own housing: 30 to 90 units, `o3de/emitterlight.h`)
        // ...and its falloff SQUARED - pools of light, dark between them
        if (point) t = k * fall * clamp((sqrt(d2) - 30.0) / 60.0, 0.0, 1.0) * (0.3 + 0.7 * max(-dot(n, dir), 0.0));
        if (t <= 0.0) continue;
        // ...and the ramp, `(t * c) >> 8` per channel, saturating.
        c += min(t * lg.l[i].colourI.rgb / 256.0, vec3(1.0));
    }
    // ...and the frame's TILED point lights, this fragment's tile's alone - in
    // the water's reflection pass too, its tiles binned for ITS camera (its
    // own set 1); the reflections were unlit by them before (2026-10-21)
#ifdef OMK_TILED
    if (lg.tiles != 0) {
        uint across = tl.t[0], down = tl.t[1];
        uvec2 fc = uvec2(gl_FragCoord.xy);
        fc.x -= min(tl.t[3] >> 16, fc.x);                          // the pass's x origin
        uvec2 tc = fc / max(tl.t[3] & 0xFFFFu, 1u);   // the tile's size, by resolution
        // the depth slice: `depthSlice` in `o3de/lighttiles.cpp`, the same
        // log spacing from 50 to 20000 units
        int sl = vDepth > 50.0 ? clamp(int(log(vDepth / 50.0) / log(400.0) * 16.0), 0, 15) : 0;
        if (tc.x < across && tc.y < down) {
            uint h = 4u + 2u * ((uint(sl) * down + tc.y) * across + tc.x);
            uint off = tl.t[h], cnt = tl.t[h + 1u];
            for (uint q = 0u; q < cnt; ++q) c += pointLit(n, w, pts.p[tl.t[off + q]]);
        }
    }
#endif
    return min(c, vec3(1.0));
}

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vCol;
layout(location = 3) in vec3 vWorld;
layout(location = 4) in vec3 vNrm;
layout(location = 5) in float vPhase;
layout(location = 0) out vec4 outColour;

// -> 1 fully lit, 0 fully shadowed. Outside the map's slab a fragment is LIT
// by definition, which is what keeps a characters-only shadow from darkening
// the far end of a street: the slab is fitted to the casters and nothing else.
float litness() {
    if (sh.strength <= 0.0 || pc.caster != 0) return 1.0;
    vec4 lp = sh.lightMvp * vec4(vWorld, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 p = lp.xyz / lp.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    if (p.z < 0.0 || p.z > 1.0) return 1.0;
    // 3x3 PCF, so the edge is not a staircase of map texels.
    float lit = 0.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            float d = texture(shadowMap, uv + vec2(i, j) * sh.texel).r;
            lit += (p.z - sh.bias <= d) ? 1.0 : 0.0;
        }
    return mix(1.0 - sh.strength, 1.0, lit / 9.0);
}

#ifdef OMK_TILED
// THE RELIEF (the remaster's B3, `[Remaster] relief = S`): the texture's own
// BRIGHTNESS read as height - four taps a texel apart, at the bound texture's
// size (the pack's 4x where there is one) - tilting the normal the lights see.
// The sets carry no tangents, so the frame is the COTANGENT frame of the
// surface's screen derivatives (world position and UV). Only the point lights
// see it (the painted light has no direction), and only on the SET (mode 3):
// a body's painted cloth and face read as grain. 0, the default, leaves `n`.
// `dp1`/`dp2`, `du1`/`du2`: the screen derivatives of `w` and `uv`, taken by
// `main` BEFORE its discards (`gradW`, `gradUV`) - see there.
vec3 relief(vec3 n, vec3 w, vec2 uv, vec3 dp1, vec3 dp2, vec2 du1, vec2 du2) {
    float s = lg.fx.x;
    if (s <= 0.0) return n;
    vec3 p2 = cross(dp2, n), p1 = cross(n, dp1);
    vec3 T = p2 * du1.x + p1 * du2.x, B = p2 * du1.y + p1 * du2.y;
    float m = max(dot(T, T), dot(B, B));
    if (m <= 0.0) return n;
    float inv = inversesqrt(m);
    // THE PACK'S NORMAL MAP where there is one (`o3de/normalmap.h`: OpenGL's
    // convention - R along +u, G along -v, the image's up): its slope, scaled
    // by the strength, along the frame's T and -B
    vec4 nm = textureGrad(nmap, uv, du1, du2);
    if (nm.a > 0.0) {
        vec3 t = nm.rgb * 2.0 - 1.0;
        vec2 sl = t.xy / max(t.z, 0.2) * s;
        return normalize(n + (T * inv) * sl.x - (B * inv) * sl.y);
    }
    // (explicit gradients: past the normal map's early return the quad is
    // not uniform either)
    vec2 px = 1.0 / vec2(textureSize(tex, 0));
    const vec3 Y = vec3(0.299, 0.587, 0.114);
    float hl = dot(textureGrad(tex, uv - vec2(px.x, 0.0), du1, du2).rgb, Y);
    float hr = dot(textureGrad(tex, uv + vec2(px.x, 0.0), du1, du2).rgb, Y);
    float hu = dot(textureGrad(tex, uv - vec2(0.0, px.y), du1, du2).rgb, Y);
    float hd = dot(textureGrad(tex, uv + vec2(0.0, px.y), du1, du2).rgb, Y);
    vec2 g = vec2(hr - hl, hd - hu) * s;
    // the slope a texel, x8: a brightness step of 0.1 tilts the normal ~0.8 at 1
    // (x4 was barely seen at 1 - the median pixel moved 8 of 255)
    return normalize(n - (T * inv) * g.x * 8.0 - (B * inv) * g.y * 8.0);
}
#endif

// THE RAIN'S RIPPLES on a wet floor (`o3de/wetfloor.h`, the remaster's): the
// floor in cells of 48 units, each with at most one drop - at a hashed place,
// and kept only where its hash is below the rain's strength, so a light shower
// has few - whose ring spreads to its own size (24..48 units) over 64 of the
// shimmer's clock (32 frames; the clock wraps at 256, a whole number of
// rings), ONE soft crest `(1 - x^2)^2` across 6 units, rising in its first
// tenth and fading as it goes. -> xy the rings' slope at `p` (the reflection's
// bend), z their crest (the light). GLES's twin is in
// `glesrender_remaster_shaders.inl` - keep the two in step.
// THE SAND'S DUST (the restored weather's, the remaster's - `View::dust`): a
// fog toward an ochre lit as the scene, thickening with the storm and with
// distance, its density broken into drifting clouds - a 3D value noise moved
// along the wind - so gusts pass. GLES's twin in glesrender_remaster_shaders.inl.
float dustHash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float dustNoise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(dustHash(i), dustHash(i + vec3(1, 0, 0)), f.x),
                   mix(dustHash(i + vec3(0, 1, 0)), dustHash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(dustHash(i + vec3(0, 0, 1)), dustHash(i + vec3(1, 0, 1)), f.x),
                   mix(dustHash(i + vec3(0, 1, 1)), dustHash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
// -> how much of the dust's colour a point `w` seen from `e` takes - with
// `kDustNear` of dust in front of everything, so it drifts by the player too
// (the reader: "make the moving fog go closer to the player")
// The clouds are STRETCHED along the wind (x, the arm's: long that way,
// narrow across), sampled near the eye too, and contrasted - gaps and thick
// gusts sweeping past (the reader: "make the fog movements more visible").
const float kDustNear = 300.0;
float dustCloud(vec3 p) {
    float g = dustNoise(p * vec3(0.0012, 0.004, 0.004));
    return g * g * 2.2;                                   // 0..2.2, most of it thin
}
float dustAmount(vec3 e, vec3 w, vec4 dust) {
    vec3 drift = vec3(dust.y, 0.0, dust.z);
    float d = length(w - e);
    vec3 near = e + (w - e) * min(1.0, 250.0 / max(d, 1.0));
    float n = 0.1 + (dustCloud(near + drift) + dustCloud(mix(e, w, 0.5) + drift) + dustCloud(w + drift)) / 3.0 * 1.6;
    return 1.0 - exp(-dust.x * n * (d + kDustNear) / dust.w);
}

// THE RAIN'S VEILS (the remaster's, `View::veil`): far rain reads as sheets,
// not streaks - long bands slanted by the wind and falling, sampled IN THE AIR
// along the ray (at 500, 1200 and 1900 units, those nearer than the surface),
// a light grey lit as the scene, thicker with distance. GLES's twin in
// glesrender_remaster_shaders.inl.
const float kVeilFirst = 500.0;         // the first sample in the air, units from the eye
const float kVeilStep = 700.0;          // ...and between the samples
const float kVeilSlant = 0.577;         // the bands' slant with the wind (30 degrees)
const float kVeilAcross = 0.008;        // the bands' noise across, per unit
const float kVeilAlong = 0.0007;        // ...and along the fall: long sheets
const float kVeilLow = 0.5;             // the noise where a sheet starts...
const float kVeilHigh = 0.85;           // ...and where it is full
const float kVeilMax = 0.85;            // the veils' strength at full, far off
float veilAmount(vec3 e, vec3 w, vec4 veil, float gust) {
    float d = length(w - e);
    if (d < kVeilFirst) return 0.0;
    vec3 dir = (w - e) / d;
    float sum = 0.0;
    for (int k = 0; k < 3; ++k) {
        float t = kVeilFirst + float(k) * kVeilStep;
        if (t > d) break;
        vec3 p = e + dir * t;
        // the gust leans the sheets further, sheared about the eye's height
        vec3 q = vec3((p.x + kVeilSlant * p.y + gust * (p.y - e.y)) * kVeilAcross, (p.y - veil.y) * kVeilAlong,
                      p.z * kVeilAcross);
        sum += smoothstep(kVeilLow, kVeilHigh, dustNoise(q));
    }
    return veil.x * (1.0 - exp(-d / veil.z)) * sum / 3.0 * kVeilMax;
}

// THE PUDDLES' MASK: 0 between them, 1 in one - a value noise of the ground's
// x and z (about 2.5 m patches), its edge soft over a fifth of its range.
// GLES's twin in glesrender_remaster_shaders.inl.
const float kPuddleBroad = 0.004;   // the patches' noise, a cycle in 250 units
const float kPuddleFine = 0.011;    // ...and their edges' detail
const float kPuddleFineShare = 0.3; // ...its share of the noise
const float kPuddleDry = 0.52;      // the noise where a puddle starts...
const float kPuddleFull = 0.68;     // ...and where it is full
// what a puddle does, as (between them, in one): the wet film's darkening,
// the reflection's strength, the ripples'; and the Fresnel's floor in one
const vec2 kPuddleFilm = vec2(0.5, 1.8);
const vec2 kPuddleMirror = vec2(0.5, 2.2);
const vec2 kPuddleRipples = vec2(0.6, 1.6);
const float kPuddleFresnel = 0.18;
// THE LIGHTS' STREAKS on a wet floor (the reader's "go with 1", 2026-10-28):
// a wet street draws each lamp and neon out into a long streak toward the
// eye - the light's highlight on the water with a slope distribution TIGHT
// across the view and LONG along it (an anisotropic Gaussian of the half
// vector, Ward's), the floor's normal up (Y is down). From the lights
// themselves, so on every wet floor - the reflection holds one plane a
// frame. GLES's twin in glesrender_remaster_shaders.inl.
const float kStreakLong = 0.30;     // the slope spread along the view: the streak's length
const float kStreakWide = 0.035;    // ...and across it: its width
const float kStreakGain = 1.2;      // its light at full wet
const float kStreakIMax = 1.5;      // a light's intensity, at most
const float kStreakFar = 4000.0;    // past this from the floor the light streaks no more (`o3de/renderer.h`'s)
const float kStreakFilmFull = 0.22; // a wet floor's film at full wet (`o3de/wetfloor.h` kWetDarken)
const vec2 kStreakPuddle = vec2(0.6, 1.4);   // ...between the puddles, in one
vec3 streakOf(vec3 w, vec3 e, vec4 posA, vec4 colourI) {
    vec3 l = posA.xyz - w;
    float dl = length(l);
    if (dl <= 1.0 || dl > kStreakFar || l.y >= 0.0) return vec3(0.0);   // a light above the floor
    l /= dl;
    vec3 v = normalize(e - w);
    vec2 tv = v.xz;
    float tl = length(tv);
    if (tl < 1e-3) return vec3(0.0);
    vec3 t = vec3(tv.x / tl, 0.0, tv.y / tl), b = vec3(-t.z, 0.0, t.x);
    vec3 h = normalize(l + v);
    float hn = -h.y;
    if (hn <= 1e-3) return vec3(0.0);
    float x = dot(h, t) / hn, y = dot(h, b) / hn;
    float s = exp(-(x * x / (kStreakLong * kStreakLong) + y * y / (kStreakWide * kStreakWide)));
    float fall = 1.0 - smoothstep(0.6 * kStreakFar, kStreakFar, dl);
    return colourI.rgb * min(colourI.a, kStreakIMax) * (s * fall);
}
float puddleMask(vec2 xz) {
    float n = dustNoise(vec3(xz.x * kPuddleBroad, 0.0, xz.y * kPuddleBroad)) * (1.0 - kPuddleFineShare) +
              dustNoise(vec3(xz.x * kPuddleFine, 3.7, xz.y * kPuddleFine)) * kPuddleFineShare;
    return smoothstep(kPuddleDry, kPuddleFull, n);
}

const float kRippleLight = 0.13;   // a ring's crest, added light (GLES: the same)
vec3 rippleHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}
vec3 ripples(vec2 p, float clock, float rain) {
    const float cell = 48.0;
    vec2 base = floor(p / cell);
    vec3 g = vec3(0.0);
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j) {
            vec2 cc = base + vec2(float(i), float(j));
            vec3 h = rippleHash(cc);
            if (h.z > rain) continue;
            float life = fract(clock / 64.0 + h.z * 4.0);
            vec2 d = p - (cc + 0.15 + 0.7 * h.xy) * cell;
            float r = length(d);
            float x = (r - life * (24.0 + 24.0 * fract(h.z * 13.0))) / 3.0;
            if (abs(x) < 1.0) {
                float a = (1.0 - life) * (1.0 - life) * smoothstep(0.0, 0.1, life);
                float u = 1.0 - x * x;
                g.z += u * u * a;
                g.xy += d / max(r, 0.001) * (-4.0 * x * u) * a * 0.5;
            }
        }
    return g;
}

// A WET BODY (`Draw::wetBody`) - GLES's twin in glesrender_remaster_shaders.inl
const float kWetBodyDark = 0.3;         // its darkening at full wet: wet cloth and skin go darker...
const float kWetBodySat = 0.35;         // ...and deeper in colour (the saturation's gain)
const float kWetBodySheen = 0.75;       // its SHEEN, the lit street mirrored by the water, at full wet
const float kWetBodyRim = 5.0;          // ...toward the grazing angles (the Fresnel's power)...
const float kWetBodyRimFilm = 0.25;     // ...faint where no bead sits: an even rim is a plastic film
const float kWetBodySky = 1.0;          // ...and where a bead faces up, mirroring the sky's glow
const float kWetBodySkyPower = 16.0;    // ...in a tight highlight (the power on the reflection's lift)
const float kWetBodyBeadScale = 48.0;   // the BEADS of water, a noise on the body's texture (it moves
const float kWetBodyBeadLo = 0.5;       // with the body, where the world's would slide over it)...
const float kWetBodyBeadHi = 0.8;       // ...thresholded to patches between these

// THE TRICKLES on a WET WALL (`o3de/wetfloor.h`, the reader's "wet walls and
// characters", 2026-10-28): thin lines of water down some columns of the
// wall - a bead running down each, a glint, its wet trail above it darker
// (-> trail, bead) - on the ripples'
// clock. GLES's twin in glesrender_remaster_shaders.inl.
const float kWetWallFilm = 0.6;       // a wet wall's film against a floor's: the water runs off
const float kTrickleAcross0 = 0.83;   // a trickle's columns run along this level direction...
const float kTrickleAcross1 = 0.56;   // ...(any wall but one rare heading)
const float kTrickleSpan = 5.0;       // ...one column every this many units
const float kTrickleShare = 0.45;     // ...this share of them running at the full shower
const float kTrickleWidth = 0.12;     // ...a line's half width, of its column
const float kTrickleLength = 40.0;    // ...a bead and its wet trail, units of fall
const float kTrickleRate = 0.0625;    // ...cycles a clock step: 16 in the clock's 256, so it wraps whole
const float kTrickleDark = 0.35;        // ...the wet trail above a bead, darker by this
const float kTrickleRuns = 0.5;       // ...a cycle's run there or not, this share running
const float kTrickleMeander = 0.04;   // ...wandering this much of its column either way
const float kTrickleWave = 0.07;      // ...over this many radians a unit of fall
const float kTrickleFar = 900.0;      // ...gone this far from the eye, finer than a pixel
const float kTrickleLight = 0.22;      // ...a bead's light, added
vec2 trickles(vec3 w, float clock, float rain) {
    float u = (w.x * kTrickleAcross0 + w.z * kTrickleAcross1) / kTrickleSpan;
    float col = floor(u);
    vec3 h = rippleHash(vec2(col, 17.0));
    if (h.z > kTrickleShare * rain) return vec2(0.0);
    float speed = kTrickleRate * (1.0 + floor(h.y * 2.0));
    float v = w.y / kTrickleLength - clock * speed + h.x * 7.0;
    // ...a run now and then down the column, not every cycle
    if (rippleHash(vec2(col, floor(v))).x > kTrickleRuns) return vec2(0.0);
    float f = fract(v);
    // ...meandering a little as it runs
    float centre = 0.3 + 0.4 * h.x + kTrickleMeander * sin(w.y * kTrickleWave + h.y * 6.28);
    float line = 1.0 - smoothstep(kTrickleWidth * 0.5, kTrickleWidth, abs(fract(u) - centre));
    float bead = smoothstep(0.82, 0.93, f) * (1.0 - smoothstep(0.93, 0.97, f));
    float trail = smoothstep(0.1, 0.93, f) * (1.0 - smoothstep(0.93, 0.97, f));
    return line * vec2(trail, bead);
}

void main() {
    int waterMode = pc.water & 15;                            // the water's mode (`Push`)
    float wetFilm = float((pc.water >> 4) & 255) / 255.0;     // a wet floor's darkening
    float rainNow = float((pc.water >> 12) & 255) / 255.0;    // ...and the rain on it
    bool snowLying = (pc.water & (1 << 20)) != 0;              // ...or snow lying on it, bit 20
    bool wetBody = (pc.water & (1 << 21)) != 0;                // ...or a BODY in the rain, bit 21
    bool level2 = (pc.water & (1 << 22)) != 0;                 // a floor of the SECOND level, bit 22
    // the open sky at this point, the corners' answers blended (`encodeOpenSky`)
    // - a WALL's (`-6 - open`) below -5.5, a floor's (`-2 - open`) below -1.5
    bool wetWall = vPhase <= -5.5;
    float openSky = wetWall ? clamp(-6.0 - vPhase, 0.0, 1.0) : vPhase <= -1.5 ? clamp(-2.0 - vPhase, 0.0, 1.0) : 1.0;
#ifdef OMK_LATE_DEPTH
    // THE CUTOUT'S DEPTH, decided AFTER the shader (the cutout pipelines'
    // module, -DOMK_LATE_DEPTH, bound for an opaque cutout draw alone): on
    // Apple's GPUs a module that can `discard` its keyed texels still let some
    // of them write depth once it grew past a size - the tiled module, the
    // relief's code - so the cutout's holes hid what lies behind them
    // DIFFERENTLY EACH RUN, the texture atlas's neighbours showing through
    // (Qalisar without MSAA, ~2000 pixels behind the pink sign; found
    // 2026-10-26). Writing the depth makes it the shader's: a discarded
    // fragment writes none. The value is the rasterised one, so nothing moves.
    // Not every draw: it costs the hidden-surface removal (5.4 ms of 13 at 4K
    // for the whole scene, measured).
    gl_FragDepth = gl_FragCoord.z;
#endif
    if ((pc.cull > 0 && !gl_FrontFacing) || (pc.cull < 0 && gl_FrontFacing)) discard;
    vec2 texels = vec2(textureSize(tex, 0));
    if (pc.texScaled != 0) texels /= vec2(pc.texScaleU, pc.texScaleV);
    // THE SCREEN DERIVATIVES the remaster's reads take, here - before the
    // water's and the cutout's DISCARDS (the cull's takes a whole primitive).
    // Past a discard that took a neighbour the quad is no longer whole:
    // `dFdx` and a `texture()`'s implicit LOD are UNDEFINED there. On the M1 that drew the relief, its normal map and the
    // glow's light map differently EACH RUN on a wall whose quads a cutout
    // edge split (`verify.py: engine: emitter light`'s run-to-run flip, found
    // 2026-10-25). The reads after the discards take these through
    // `textureGrad`; the colour read above them stays as the original's.
    vec2 gradUV[2] = vec2[2](dFdx(vUV / texels), dFdy(vUV / texels));
    vec3 gradW[2] = vec3[2](dFdx(vWorld), dFdy(vWorld));
    if (waterMode == 2 && vWorld.y > pc.waterStrength) discard;   // below the water (Y is down)
    // Alpha is the KEY, written at upload: 0 on a black texel, 1 elsewhere.
    // Under the nearest sampler (the original's) this is exactly the old
    // `rgb == 0` test on the same texel. Under the bilinear ENHANCEMENT the
    // key texels are (0,0,0,0), so the sample is premultiplied: a fragment
    // more than half key is discarded and the rest is un-premultiplied,
    // which is what keeps the cutout edge free of a dark fringe.
    vec4 t = texture(tex, vUV / texels);
    if (pc.cutout != 0) {
        if (t.a < 0.5) discard;
        t.rgb /= t.a;
    }
    // A LIT batch starts from black and takes its shading from the lights;
    // every other one keeps the colour baked into its vertices.
    // 1 from black, 2 added to the baked colour - see `renderer.h`'s Draw.
    // THE SHIMMER - mesh flag 0x8000000, and it is the GAME's, not an
    // enhancement. `sub_4947F0` adds one entry of a 32-byte signed table into
    // all three colour bytes; 233 set meshes carry the flag, all of them
    // distant scenery, and neighbouring vertices sit 1/16 of a cycle apart so
    // the wave travels. The table is duplicated here from `o3de/shimmer.h`
    // because a shader cannot include it, and `verify.py: shimmer table`
    // asserts the two against the executable.
    float wave = 0.0;
    if (vPhase >= 0.0) {
        int wi = ((int(sh.shimmer) >> 2) + int(vPhase)) & 31;
        float tbl[32] = float[32](
              0.0,  10.0,  13.0,  18.0,  28.0,  32.0,  48.0,  56.0,
             64.0,  56.0,  48.0,  32.0,  28.0,  18.0,  13.0,  10.0,
              0.0, -10.0, -13.0, -18.0, -28.0, -32.0, -48.0, -56.0,
            -64.0, -56.0, -48.0, -32.0, -28.0, -18.0, -13.0, -10.0);
        wave = tbl[wi] / 255.0;
    }
    // (the remaster's NIGHT, `lg.eye.w`, dims the painted and the start light
    // under the point lights; 1 - a multiply by one - everywhere else)
    float night = lg.eye.w;
    vec3 nL = normalize(vNrm);
#ifdef OMK_TILED
    if (pc.lit == 3) nL = relief(nL, vWorld, vUV / texels, gradW[0], gradW[1], gradUV[0], gradUV[1]);
#endif
    vec3 shade = pc.lit == 1 ? min(vec3(lg.base) * night + litColour(nL, vWorld, false), vec3(1.0))
               : pc.lit == 2 ? min(vCol * night + litColour(nL, vWorld, false), vec3(1.0))
               : pc.lit == 3 ? min((vCol + vec3(wave)) * night + litColour(nL, vWorld, true), vec3(1.0))
                             : vCol + vec3(wave);
    // THE GREYSCALE BANK (ops 150/151, `o3de/greybank.h`): `sub_42FF80`
    // writes each vertex colour as its luma, `(299 R + 587 G + 114 B) / 1000`
    // on the BYTES, truncated - here on the colour the batch ends with, after
    // its shimmer and lights. The texture came greyed (its palette); the fog
    // colour too. `+ 0.5` keeps the floor exact under an approximate divide.
    if (lg.grey != 0) {
        float y = floor((dot(floor(clamp(shade, 0.0, 1.0) * 255.0 + 0.5),
                             vec3(299.0, 587.0, 114.0)) + 0.5) / 1000.0);
        shade = vec3(y / 255.0);
    }
    vec3 c = clamp(t.rgb * shade * litness(), 0.0, 1.0);
    // SNOW LYING (`o3de/wetfloor.h` coverRuns): the floor turned white under
    // its own light - `c / texel`, the light alone - by the cover times the
    // open sky here, before the fog takes it like the rest
    // A BODY IN THE RAIN (`Draw::wetBody`): darker and deeper in colour, and
    // a SHEEN - the water mirroring the lit street around it, so lit by the
    // body's own light (`shade`), in BEADS on its texture (an even film read
    // as plastic: the reader, 2026-10-28): strongest at the grazing angles
    // (Fresnel) and where the reflected ray rises to the sky (Y is down: -y).
    // `reflect` takes either sign of the normal alike. GLES alike
    if (wetBody && wetFilm > 0.0) {
        float y = dot(c, vec3(0.299, 0.587, 0.114));
        c = max(mix(vec3(y), c, 1.0 + kWetBodySat * wetFilm), 0.0) * (1.0 - kWetBodyDark * wetFilm);
        vec3 v = normalize(lg.eye.xyz - vWorld);
        float bead = smoothstep(kWetBodyBeadLo, kWetBodyBeadHi, dustNoise(vec3(vUV * kWetBodyBeadScale, 0.5)));
        float rim = pow(1.0 - abs(dot(nL, v)), kWetBodyRim) * mix(kWetBodyRimFilm, 1.0, bead);
        float sky = pow(max(-reflect(-v, nL).y, 0.0), kWetBodySkyPower) * kWetBodySky * bead;
        c = clamp(c + kWetBodySheen * wetFilm * (rim + sky) * shade, 0.0, 1.0);
        wetFilm = 0.0;   // no floor's film, no ripples, on a body
    }
    if (snowLying && wetFilm > 0.0) {
        // ...the more the darker the texel: a snow texture keeps its own
        // drawing, stone and wood go white
        vec3 lightHere = c / max(t.rgb, vec3(0.04));
        float darkTexel = clamp((1.0 - dot(t.rgb, vec3(0.299, 0.587, 0.114))) * 1.4, 0.0, 1.0);
        c = mix(c, clamp(lightHere * 0.9, 0.0, 1.0), wetFilm * openSky * darkTexel);
        wetFilm = 0.0;   // no rain's film, no ripples, on snow
    }
    // THE FOG, and it is raster.cpp's line transcribed. Linear -
    // `FOGTABLEMODE` 3 at density 1.0, the only mode the engine sets - over
    // the range the clip distance sizes. The CPU side has already applied the
    // bucket key's two exclusions and any doubling, so a zero end means this
    // batch is not fogged.
    if (pc.fogEnd > pc.fogStart && vDepth > pc.fogStart) {
        float f = clamp((pc.fogEnd - vDepth) / (pc.fogEnd - pc.fogStart), 0.0, 1.0);
        c = mix(pc.fogColour, c, f);
    }
    // THE MIST (the remaster's A4, `View::mist`): a height fog in the same
    // colour, on what the game fogs - its density 1 at the set's GROUND and
    // falling as exp(-h / height) above it (Y is down: h = ground - y), the
    // ray's mean density in closed form, one unit of it the 15 m that takes
    // 63% of the light at strength 1 (40 m read as nothing over a street). Not in the water's reflection pass
    // (its eye is the frame's); 0 nothing. The batches it takes are the ones
    // the game fogs - or, its fog off, the ones the backend marks -1 / -1.
    if (lg.fx.y > 0.0 && (pc.fogEnd > pc.fogStart || pc.fogEnd < 0.0) && waterMode != 2) {
        float H = max(lg.fx.w, 1.0);
        float h0 = max(lg.fx.z - lg.eye.y, 0.0), h1 = max(lg.fx.z - vWorld.y, 0.0);
        float dist = length(vWorld - lg.eye.xyz);
        float mean = abs(h1 - h0) > 1.0 ? H * (exp(-h0 / H) - exp(-h1 / H)) / (h1 - h0) : exp(-h0 / H);
        float m = 1.0 - exp(-lg.fx.y * dist * mean / 1500.0);
        c = mix(c, pc.fogColour, m);
    }
    // ...and THE SAND'S DUST (`dustAmount` above), on the same batches
    if (lg.dust.x > 0.0 && (pc.fogEnd > pc.fogStart || pc.fogEnd < 0.0) && waterMode != 2)
        c = mix(c, lg.dustColour.rgb, dustAmount(lg.eye.xyz, vWorld, lg.dust));
    // THE FRESNEL (Schlick, F0 0.02 - water's): the water mirrors little
    // looked down into and nearly all at grazing, so `waterStrength` is the
    // GRAZING strength. The plane is level: cos theta is the eye ray's height.
    // A REFLECTING FLOOR (`o3de/floorreflect.h`) is mode 3: the same, its
    // lookup broken up by the floor's RELIEF where the pack has its normal
    // map (the tiled module's, as the relief reads it) - the slope shifts it, up to
    // ~1% of the frame's height. The water's mode 1 is as it was.
    // A WET floor (`o3de/wetfloor.h`): the rain's film darkens it, and its
    // reflection, if it has one, is added over the darker ground - all of it
    // times how open to the sky this point is, the corners' answers blended
    // (`encodeOpenSky`: `-2 - open` in the phase; any other phase is open)
    // PUDDLES (the reader's "more realistic rain", 2 of 3): patches of standing
    // water across the wet floor - a noise mask on the ground (`puddleMask`):
    // in one, the film darker, the reflection much stronger, the ripples more;
    // between them, the film and the reflection lighter
    // A WALL takes a lighter film and its TRICKLES instead - no puddle, no ripples
    float puddle = wetFilm > 0.0 && !wetWall ? puddleMask(vWorld.xz) * lg.veil.w : 0.0;
    float wetHere = wetFilm > 0.0 ? openSky * mix(kPuddleMirror.x, kPuddleMirror.y, puddle) : 1.0;
    c *= 1.0 - wetFilm * openSky * (wetWall ? kWetWallFilm : mix(kPuddleFilm.x, kPuddleFilm.y, puddle));
    // ...and while it RAINS, its RIPPLES (`ripples` above): a little light on
    // their crests, and bending the reflection below
    vec3 rip = vec3(0.0);
    if (wetFilm > 0.0 && rainNow > 0.0 && openSky > 0.0 && wetWall) {
        float near = 1.0 - smoothstep(kTrickleFar * 0.6, kTrickleFar, length(lg.eye.xyz - vWorld));
        vec2 tr = trickles(vWorld, sh.shimmer, rainNow) * openSky * near;
        c = c * (1.0 - kTrickleDark * tr.x) + vec3(kTrickleLight * tr.y);
    } else if (wetFilm > 0.0 && rainNow > 0.0 && openSky > 0.0) {
        rip = ripples(vWorld.xz, sh.shimmer, rainNow) * openSky * mix(kPuddleRipples.x, kPuddleRipples.y, puddle);
        c += vec3(kRippleLight * rip.z);
    }
    // ...and THE LIGHTS' STREAKS (`streakOf` above), on a wet floor
    if (wetFilm > 0.0 && !wetWall && openSky > 0.0 && lg.streakN.y > 0.0) {
        vec3 st = vec3(0.0);
        int ns = int(lg.streakN.x);
        for (int i = 0; i < ns; ++i) st += streakOf(vWorld, lg.eye.xyz, lg.streak[i].posA, lg.streak[i].colourI);
        c += st * (kStreakGain * lg.streakN.y * min(wetFilm / kStreakFilmFull, 1.0) * openSky *
                   mix(kStreakPuddle.x, kStreakPuddle.y, puddle));
    }
    if (waterMode == 1 || waterMode == 3) {
        float ct = abs(normalize(lg.eye.xyz - vWorld).y);
        float f = 0.02 + 0.98 * pow(1.0 - ct, 5.0);
        f = max(f, kPuddleFresnel * puddle);   // standing water mirrors, looked into too
        vec2 rs = vec2(textureSize(refl, 0));
        // (a floor's reflection is at HALF size, in the image's top-left quarter)
        float k = waterMode == 3 ? 0.5 : 1.0;
        vec2 at = gl_FragCoord.xy * k;
#ifdef OMK_TILED
        if (waterMode == 3) {
            vec4 nm = textureGrad(nmap, vUV / texels, gradUV[0], gradUV[1]);
            if (nm.a > 0.0) at += (nm.rg * 2.0 - 1.0) * rs.y * 0.012 * k;
        }
#endif
        if (waterMode == 3) at += rip.xy * rs.y * 0.02 * k;   // the ripples' slope
        if (waterMode == 3) at = clamp(at, vec2(0.5), rs * 0.5 - 0.5);   // inside its quarter
        // ...the SECOND level's the top-RIGHT one (`[Remaster] reflections = 2`),
        // from the render width's half rounded up (`reflHalfW`)
        if (waterMode == 3 && level2) {
            float x0 = floor((rs.x + 1.0) * 0.5);
            at.x = min(at.x + x0, rs.x - 0.5);
        }
        c += texture(refl, at / rs).rgb * pc.waterStrength * f * wetHere;
    }
    // THE RAIN'S VEILS (`veilAmount` above), on the fog's batches - LAST: rain
    // in the AIR between the eye and the surface, over the surface's whole
    // light (its wet film, ripples, streaks and reflection), as a fog is and
    // as the GLES screen pass lays it (2026-11-27; before, the film darkened
    // the veil and the reflection lay over it)
    if (lg.veil.x > 0.0 && (pc.fogEnd > pc.fogStart || pc.fogEnd < 0.0) && waterMode != 2)
        c = mix(c, lg.veilColour.rgb, veilAmount(lg.eye.xyz, vWorld, lg.veil, lg.veilColour.w));
    // THE ALPHA, read by the post-pass (`post.frag`, `ao.frag`): 0 on a BODY
    // (`caster`: a character, the shadow map's - posed here or on the CPU),
    // and on the set 1/255 plus its light map's value (the bloom's glow) in
    // the 254 steps above - so a body neither glows nor takes or casts the
    // ambient occlusion. A body marked by the top of the range would collide
    // with full glow: additive draws ADD their alpha.
    float glowV = pc.bloomMode != 0 ? textureGrad(emis, vUV / texels, gradUV[0], gradUV[1]).r : 0.0;
    outColour = vec4(c, pc.caster != 0 ? 0.0 : (1.0 + 254.0 * glowV) / 255.0);
}

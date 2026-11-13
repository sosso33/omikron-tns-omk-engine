// SPDX-License-Identifier: GPL-3.0-or-later
// THE REMASTER'S GLES SHADERS (`todo/remaster.md`) - the post pass (bloom,
// grade), the scene programs' variants (bloom, water, point lights, body
// tint) and FSR 1 (EASU, RCAS). Included by `glesrender.cpp` at namespace
// scope under OMK_REMASTER only, so a build without it carries none of them.
// Not compiled on its own.

// THE REMASTER'S POST-PASS (`todo/remaster.md` step 2) - `post.frag`'s four
// steps in GLSL ES 1.0: DOWN (the light, `src.rgb * src.a`, the alpha the
// light map the bloom variant of the scene program wrote), BLUR (21 taps,
// sigma 4), FINAL (the glow screen-blended, then the city grade from an
// N*N x N strip). Linked only when the bloom or a grade is asked for.
constexpr const char* kPostVert = R"(
attribute vec2 aPos;
varying vec2 vUv;
void main() {
    vUv = aPos;
    gl_Position = vec4(aPos * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kPostFrag = R"(
uniform sampler2D uSrc;
uniform sampler2D uGlow;
uniform sampler2D uLut;
uniform vec2  uSrcInv;
uniform vec2  uDir;
uniform float uStrength;
uniform int   uMode;
uniform float uLutN;
uniform float uHalo;
uniform sampler2D uHaloTex;   // the halos, made at the glow's size (mode 3)
varying vec2 vUv;
// THE HALOS in the rain, `post.frag`'s - keep the two in step
const float kHaloGain = 1.5;
const float kHaloRadius = 12.0;
vec3 haloOf(sampler2D g, vec2 uv, vec2 texel) {
    vec3 h = texture2D(g, uv).rgb;
    for (int i = 0; i < 8; ++i) {
        float a = (float(i) + 0.5) * 0.7853982;
        h += texture2D(g, uv + vec2(cos(a), sin(a)) * (0.5 * kHaloRadius) * texel).rgb;
    }
    for (int i = 0; i < 16; ++i) {
        float a = float(i) * 0.3926991;
        h += texture2D(g, uv + vec2(cos(a), sin(a)) * kHaloRadius * texel).rgb;
    }
    return h / 25.0;
}
vec3 graded(vec3 c) {
    float n = uLutN;
    c = clamp(c, 0.0, 1.0);
    float bs = c.b * (n - 1.0);
    float b0 = floor(bs);
    float b1 = min(b0 + 1.0, n - 1.0);
    float t = bs - b0;
    vec2 inv = vec2(1.0 / (n * n), 1.0 / n);
    vec2 rg = c.rg * (n - 1.0) + 0.5;
    vec3 a = texture2D(uLut, (vec2(b0 * n, 0.0) + rg) * inv).rgb;
    vec3 b = texture2D(uLut, (vec2(b1 * n, 0.0) + rg) * inv).rgb;
    return mix(a, b, t);
}
void main() {
    if (uMode == 0) {
        vec3 acc = vec3(0.0);
        for (int j = -1; j <= 1; j += 2)
            for (int i = -1; i <= 1; i += 2) {
                vec4 s = texture2D(uSrc, vUv + vec2(float(i), float(j)) * uSrcInv);
                acc += s.rgb * s.a;
            }
        gl_FragColor = vec4(acc * 0.25, 1.0);
    } else if (uMode == 1) {
        vec3 acc = vec3(0.0);
        float wsum = 0.0;
        for (int k = -10; k <= 10; ++k) {
            float w = exp(-float(k * k) / 32.0);
            acc += texture2D(uSrc, vUv + uDir * uSrcInv * float(k)).rgb * w;
            wsum += w;
        }
        gl_FragColor = vec4(acc / wsum, 1.0);
    } else if (uMode == 3) {
        // THE HALOS alone, at the glow's quarter size, once (`post.frag`'s
        // mode 3): the glow images are 8-bit here, so the halo - an average,
        // at most 1 - is kept apart from the glow rather than added to it,
        // and kept as its SQUARE ROOT: 8 bits of a faint halo banded into
        // rings, 8 bits of its root do not (squared back in the final pass)
        gl_FragColor = vec4(sqrt(haloOf(uSrc, vUv, 4.0 * uSrcInv)), 1.0);
    } else {
        vec3 c = texture2D(uSrc, vUv).rgb;
        if (uStrength > 0.0) {
            vec3 g = texture2D(uGlow, vUv).rgb;
            if (uHalo > 0.0) { vec3 h = texture2D(uHaloTex, vUv).rgb; g += h * h * (kHaloGain * uHalo); }
            g = min(g * uStrength, vec3(1.0));
            c = 1.0 - (1.0 - c) * (1.0 - g);
        }
        if (uLutN > 1.5) c = graded(c);
        gl_FragColor = vec4(c, 1.0);
    }
}
)";

// THE LIGHTS' STREAKS on a wet floor, `scene.frag`'s `streakOf` and its
// constants - keep the two in step
constexpr const char* kStreakGlsl =
    "const float kStreakLong = 0.30;\n"
    "const float kStreakWide = 0.035;\n"
    "const float kStreakGain = 1.2;\n"
    "const float kStreakIMax = 1.5;\n"
    "const float kStreakFar = 4000.0;\n"
    "const float kStreakFilmFull = 0.22;\n"
    "const vec2 kStreakPuddle = vec2(0.6, 1.4);\n"
    "vec3 streakOf(vec3 w, vec3 e, vec4 posA, vec4 colourI) {\n"
    "    vec3 l = posA.xyz - w;\n"
    "    float dl = length(l);\n"
    "    if (dl <= 1.0 || dl > kStreakFar || l.y >= 0.0) return vec3(0.0);\n"
    "    l /= dl;\n"
    "    vec3 v = normalize(e - w);\n"
    "    vec2 tv = v.xz;\n"
    "    float tl = length(tv);\n"
    "    if (tl < 1e-3) return vec3(0.0);\n"
    "    vec3 t = vec3(tv.x / tl, 0.0, tv.y / tl), b = vec3(-t.z, 0.0, t.x);\n"
    "    vec3 h = normalize(l + v);\n"
    "    float hn = -h.y;\n"
    "    if (hn <= 1e-3) return vec3(0.0);\n"
    "    float x = dot(h, t) / hn, y = dot(h, b) / hn;\n"
    "    float s = exp(-(x * x / (kStreakLong * kStreakLong) + y * y / (kStreakWide * kStreakWide)));\n"
    "    float fall = 1.0 - smoothstep(0.6 * kStreakFar, kStreakFar, dl);\n"
    "    return colourI.rgb * min(colourI.a, kStreakIMax) * (s * fall);\n"
    "}\n";

// A variant's ANCHOR not found: said, never silent - the shader text it
// patches changed and the variant would build and do nothing (the review of
// 2026-10-21)
void anchorMissed(const char* variant, const std::string& anchor) {
    std::fprintf(stderr, "gles: the %s variant's anchor is not in the shader - `%.60s` - it does nothing there\n",
                 variant, anchor.c_str());
}

// The BLOOM variant of a scene fragment program: the same source, with the
// alpha written as the texel's LIGHT (the pack's light map on unit 3). Made
// from the default text at `init`, so the default programs - and the Vita's
// shader cache, keyed by source - are untouched.
std::string bloomVariant(const char* frag) {
    std::string s = std::string("uniform sampler2D uEmis;\n") + frag;
    const std::string from = "gl_FragColor = vec4(c, 1.0);";
    const auto at = s.rfind(from);
    if (at != std::string::npos) s.replace(at, from.size(), "gl_FragColor = vec4(c, texture2D(uEmis, vUV).r);");
    else anchorMissed("bloom", from);
    return s;
}

// THE RAIN'S RIPPLES (`o3de/wetfloor.h`), `scene.frag`'s `ripples` - keep the
// two in step: cells of 48 units, a drop where the hash is below the rain, one
// soft ring to its own size over 64 of the shimmer's clock
// THE SAND'S DUST (the restored weather's), `scene.frag`'s `dustAmount` - keep
// the two in step
constexpr const char* kDustGlsl =
    "float dustHash(vec3 p) {\n"
    "    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));\n"
    "    p *= 17.0;\n"
    "    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"
    "}\n"
    "float dustNoise(vec3 x) {\n"
    "    vec3 i = floor(x), f = fract(x);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(mix(dustHash(i), dustHash(i + vec3(1.0, 0.0, 0.0)), f.x),\n"
    "                   mix(dustHash(i + vec3(0.0, 1.0, 0.0)), dustHash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),\n"
    "               mix(mix(dustHash(i + vec3(0.0, 0.0, 1.0)), dustHash(i + vec3(1.0, 0.0, 1.0)), f.x),\n"
    "                   mix(dustHash(i + vec3(0.0, 1.0, 1.0)), dustHash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);\n"
    "}\n"
    "float dustCloud(vec3 p) {\n"
    "    float g = dustNoise(p * vec3(0.0012, 0.004, 0.004));\n"
    "    return g * g * 2.2;\n"
    "}\n"
    "float dustAmount(vec3 e, vec3 w, vec4 dust) {\n"
    "    vec3 drift = vec3(dust.y, 0.0, dust.z);\n"
    "    float d = length(w - e);\n"
    "    vec3 near = e + (w - e) * min(1.0, 250.0 / max(d, 1.0));\n"
    "    float n = 0.1 + (dustCloud(near + drift) + dustCloud(mix(e, w, 0.5) + drift) + dustCloud(w + drift)) / 3.0 * 1.6;\n"
    "    return 1.0 - exp(-dust.x * n * (d + 300.0) / dust.w);\n"
    "}\n";

// THE RAIN'S VEILS (`View::veil`), `scene.frag`'s `veilAmount` - keep the two
// in step (after kDustGlsl, whose noise it takes)
constexpr const char* kVeilGlsl =
    "const float kVeilFirst = 500.0;\n"
    "const float kVeilStep = 700.0;\n"
    "const float kVeilSlant = 0.577;\n"
    "const float kVeilAcross = 0.008;\n"
    "const float kVeilAlong = 0.0007;\n"
    "const float kVeilLow = 0.5;\n"
    "const float kVeilHigh = 0.85;\n"
    "const float kVeilMax = 0.85;\n"
    "float veilAmount(vec3 e, vec3 w, vec4 veil, float gust) {\n"
    "    float d = length(w - e);\n"
    "    if (d < kVeilFirst) return 0.0;\n"
    "    vec3 dir = (w - e) / d;\n"
    "    float sum = 0.0;\n"
    "    for (int k = 0; k < 3; ++k) {\n"
    "        float t = kVeilFirst + float(k) * kVeilStep;\n"
    "        if (t > d) break;\n"
    "        vec3 p = e + dir * t;\n"
    "        vec3 q = vec3((p.x + kVeilSlant * p.y + gust * (p.y - e.y)) * kVeilAcross, (p.y - veil.y) * kVeilAlong,\n"
    "                      p.z * kVeilAcross);\n"
    "        sum += smoothstep(kVeilLow, kVeilHigh, dustNoise(q));\n"
    "    }\n"
    "    return veil.x * (1.0 - exp(-d / veil.z)) * sum / 3.0 * kVeilMax;\n"
    "}\n";

// THE PUDDLES' MASK, `scene.frag`'s `puddleMask` and its constants - keep the
// two in step (after kDustGlsl, whose noise it takes)
constexpr const char* kPuddleGlsl =
    "const float kPuddleBroad = 0.004;\n"
    "const float kPuddleFine = 0.011;\n"
    "const float kPuddleFineShare = 0.3;\n"
    "const float kPuddleDry = 0.52;\n"
    "const float kPuddleFull = 0.68;\n"
    "const vec2 kPuddleFilm = vec2(0.5, 1.8);\n"
    "const vec2 kPuddleMirror = vec2(0.5, 2.2);\n"
    "const vec2 kPuddleRipples = vec2(0.6, 1.6);\n"
    "const float kPuddleFresnel = 0.18;\n"
    "float puddleMask(vec2 xz) {\n"
    "    float n = dustNoise(vec3(xz.x * kPuddleBroad, 0.0, xz.y * kPuddleBroad)) * (1.0 - kPuddleFineShare) +\n"
    "              dustNoise(vec3(xz.x * kPuddleFine, 3.7, xz.y * kPuddleFine)) * kPuddleFineShare;\n"
    "    return smoothstep(kPuddleDry, kPuddleFull, n);\n"
    "}\n";

// THE TRICKLES on a wet wall, `scene.frag`'s `trickles` and its constants -
// keep the two in step (after kRipplesGlsl, whose hash it takes); and a WET
// BODY's constants, `scene.frag`'s too
constexpr const char* kTrickleGlsl =
    "const float kWetBodyDark = 0.3;\n"
    "const float kWetBodySat = 0.35;\n"
    "const float kWetBodySheen = 0.75;\n"
    "const float kWetBodyRim = 5.0;\n"
    "const float kWetBodyRimFilm = 0.25;\n"
    "const float kWetBodySky = 1.0;\n"
    "const float kWetBodySkyPower = 16.0;\n"
    "const float kWetBodyBeadScale = 48.0;\n"
    "const float kWetBodyBeadLo = 0.5;\n"
    "const float kWetBodyBeadHi = 0.8;\n"
    "const float kWetWallFilm = 0.6;\n"
    "const float kTrickleAcross0 = 0.83;\n"
    "const float kTrickleAcross1 = 0.56;\n"
    "const float kTrickleSpan = 5.0;\n"
    "const float kTrickleShare = 0.45;\n"
    "const float kTrickleWidth = 0.12;\n"
    "const float kTrickleLength = 40.0;\n"
    "const float kTrickleRate = 0.0625;\n"
    "const float kTrickleDark = 0.35;\n"
    "const float kTrickleRuns = 0.5;\n"
    "const float kTrickleMeander = 0.04;\n"
    "const float kTrickleWave = 0.07;\n"
    "const float kTrickleFar = 900.0;\n"
    "const float kTrickleLight = 0.22;\n"
    "vec2 trickles(vec3 w, float clock, float rain) {\n"
    "    float u = (w.x * kTrickleAcross0 + w.z * kTrickleAcross1) / kTrickleSpan;\n"
    "    float col = floor(u);\n"
    "    vec3 h = rippleHash(vec2(col, 17.0));\n"
    "    if (h.z > kTrickleShare * rain) return vec2(0.0);\n"
    "    float speed = kTrickleRate * (1.0 + floor(h.y * 2.0));\n"
    "    float v = w.y / kTrickleLength - clock * speed + h.x * 7.0;\n"
    "    // ...a run now and then down the column, not every cycle\n"
    "    if (rippleHash(vec2(col, floor(v))).x > kTrickleRuns) return vec2(0.0);\n"
    "    float f = fract(v);\n"
    "    // ...meandering a little as it runs\n"
    "    float centre = 0.3 + 0.4 * h.x + kTrickleMeander * sin(w.y * kTrickleWave + h.y * 6.28);\n"
    "    float line = 1.0 - smoothstep(kTrickleWidth * 0.5, kTrickleWidth, abs(fract(u) - centre));\n"
    "    float bead = smoothstep(0.82, 0.93, f) * (1.0 - smoothstep(0.93, 0.97, f));\n"
    "    float trail = smoothstep(0.1, 0.93, f) * (1.0 - smoothstep(0.93, 0.97, f));\n"
    "    return line * vec2(trail, bead);\n"
    "}\n";

constexpr const char* kRipplesGlsl =
    "const float kRippleLight = 0.13;   // a ring's crest, added light (GLES: the same)\n"
    "vec3 rippleHash(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
    "    p3 += dot(p3, p3.yxz + 33.33);\n"
    "    return fract((p3.xxy + p3.yzz) * p3.zyx);\n"
    "}\n"
    "vec3 ripples(vec2 p, float clock, float rain) {\n"
    "    const float cell = 48.0;\n"
    "    vec2 base = floor(p / cell);\n"
    "    vec3 g = vec3(0.0);\n"
    "    for (int i = -1; i <= 1; ++i)\n"
    "        for (int j = -1; j <= 1; ++j) {\n"
    "            vec2 cc = base + vec2(float(i), float(j));\n"
    "            vec3 h = rippleHash(cc);\n"
    "            if (h.z > rain) continue;\n"
    "            float life = fract(clock / 64.0 + h.z * 4.0);\n"
    "            vec2 d = p - (cc + 0.15 + 0.7 * h.xy) * cell;\n"
    "            float r = length(d);\n"
    "            float x = (r - life * (24.0 + 24.0 * fract(h.z * 13.0))) / 3.0;\n"
    "            if (abs(x) < 1.0) {\n"
    "                float a = (1.0 - life) * (1.0 - life) * smoothstep(0.0, 0.1, life);\n"
    "                float u = 1.0 - x * x;\n"
    "                g.z += u * u * a;\n"
    "                g.xy += d / max(r, 0.001) * (-4.0 * x * u) * a * 0.5;\n"
    "            }\n"
    "        }\n"
    "    return g;\n"
    "}\n";

// ...and the WATER variant (`todo/remaster.md` A3), over either: a water draw
// adds the reflection (unit 4) at its screen position times `uWater`, 0 on
// every other draw. Made at `init` only when the water is asked for.
std::string waterVariant(const std::string& frag) {
    std::string s = "uniform sampler2D uRefl;\nuniform float uWater;\nuniform vec2 uReflInv;\nuniform float uWet;\n"
                    "uniform vec2 uReflOff;\nuniform vec4 uReflBox;\n"
                    "uniform float uRain;\nuniform float uRippleClock;\nvarying vec2 vWxz;\nvarying float vOpen;\nvarying float vWall;\n"
                    "uniform float uSnow;\nuniform float uWetBody;\nuniform vec3 uMist;\nuniform vec4 uDust;\nuniform vec3 uDustColour;\n"
                    "uniform vec4 uVeil;\nuniform vec4 uVeilColour;\n"
                    "uniform vec4 uStreak[32];\nuniform vec2 uStreakN;\n"
                    "uniform int uClip;\nuniform float uClipY;\nvarying float vWy;\nvarying vec3 vToEye;\nvarying vec3 vBodyN;\n" +
                    std::string(kRipplesGlsl) + std::string(kDustGlsl) + std::string(kVeilGlsl) +
                    std::string(kPuddleGlsl) + std::string(kTrickleGlsl) + std::string(kStreakGlsl) + frag;
    // times the FRESNEL (Schlick, F0 0.02 - `scene.frag`'s): `uWater` is the
    // grazing strength, and the level plane's cos theta the eye ray's height
    const auto at = s.rfind("gl_FragColor = vec4(c,");
    if (at != std::string::npos)
        // ...over a WET floor's film first (`o3de/wetfloor.h`): the rain darkens
        // it, and while it rains its RIPPLES lighten their crests and bend the
        // reflection (`scene.frag`'s rule)
        // (all of it times the open sky at this point, `encodeOpenSky`'s)
        // (none of it on SNOW lying - `uSnow`, below)
        // ...after THE MIST (`View::mist`, `scene.frag`'s rule, on what the
        // game fogs or the -1 / -1 marks): a height fog in the fog's colour,
        // the ray's mean density in closed form from the eye (`vWy + vToEye.y`)
        s.insert(at, "if (uMist.x > 0.0 && (uFogEnd > uFogStart || uFogEnd < 0.0)) {\n"
                     "        float H = max(uMist.z, 1.0);\n"
                     "        float h0 = max(uMist.y - (vWy + vToEye.y), 0.0), h1 = max(uMist.y - vWy, 0.0);\n"
                     "        float dist = length(vToEye);\n"
                     "        float mean = abs(h1 - h0) > 1.0 ? H * (exp(-h0 / H) - exp(-h1 / H)) / (h1 - h0)\n"
                     "                                        : exp(-h0 / H);\n"
                     "        c = mix(c, uFogColour, 1.0 - exp(-uMist.x * dist * mean / 1500.0));\n"
                     "    }\n"
                     "    if (uDust.x > 0.0 && (uFogEnd > uFogStart || uFogEnd < 0.0)) {\n"
                     "        vec3 dw = vec3(vWxz.x, vWy, vWxz.y);\n"
                     "        c = mix(c, uDustColour, dustAmount(dw + vToEye, dw, uDust));\n"
                     "    }\n"
                     "    if (uVeil.x > 0.0 && (uFogEnd > uFogStart || uFogEnd < 0.0)) {\n"
                     "        vec3 vw = vec3(vWxz.x, vWy, vWxz.y);\n"
                     "        c = mix(c, uVeilColour.rgb, veilAmount(vw + vToEye, vw, uVeil, uVeilColour.w));\n"
                     "    }\n"
                     "    float wetF = uSnow > 0.5 || uWetBody > 0.5 ? 0.0 : uWet;\n"
                     "    float pud = 0.0;\n"   // the PUDDLES, `scene.frag`'s `puddleMask`
                     "    bool wall = vWall > 0.5;\n"   // a WALL: a lighter film and its trickles, `scene.frag`'s
                     "    if (wetF > 0.0 && !wall) pud = puddleMask(vWxz) * uVeil.w;\n"
                     "    float wetHere = wetF > 0.0 ? vOpen * mix(kPuddleMirror.x, kPuddleMirror.y, pud) : 1.0;\n"
                     "    c *= 1.0 - wetF * vOpen * (wall ? kWetWallFilm : mix(kPuddleFilm.x, kPuddleFilm.y, pud));\n"
                     "    vec3 rip = vec3(0.0);\n"
                     "    if (wetF > 0.0 && uRain > 0.0 && vOpen > 0.0 && wall) {\n"
                     "        float near = 1.0 - smoothstep(kTrickleFar * 0.6, kTrickleFar, length(vToEye));\n"
                     "        vec2 tr = trickles(vec3(vWxz.x, vWy, vWxz.y), uRippleClock, uRain) * vOpen * near;\n"
                     "        c = c * (1.0 - kTrickleDark * tr.x) + vec3(kTrickleLight * tr.y);\n"
                     "    } else if (wetF > 0.0 && uRain > 0.0 && vOpen > 0.0) {\n"
                     "        rip = ripples(vWxz, uRippleClock, uRain) * vOpen * mix(kPuddleRipples.x, kPuddleRipples.y, pud);\n"
                     "        c += vec3(kRippleLight * rip.z);\n"
                     "    }\n"
                     // ...and THE LIGHTS' STREAKS on a wet floor, `scene.frag`'s
                     "    if (wetF > 0.0 && !wall && vOpen > 0.0 && uStreakN.y > 0.0) {\n"
                     "        vec3 sw = vec3(vWxz.x, vWy, vWxz.y), st = vec3(0.0);\n"
                     "        for (int i = 0; i < 16; ++i) {\n"
                     "            if (float(i) >= uStreakN.x) break;\n"
                     "            st += streakOf(sw, sw + vToEye, uStreak[2 * i], uStreak[2 * i + 1]);\n"
                     "        }\n"
                     "        c += st * (kStreakGain * uStreakN.y * min(wetF / kStreakFilmFull, 1.0) * vOpen *\n"
                     "                   mix(kStreakPuddle.x, kStreakPuddle.y, pud));\n"
                     "    }\n"
                     "    if (uWater > 0.0) {\n"
                     "        float ct = abs(normalize(vToEye).y);\n"
                     "        float f = max(0.02 + 0.98 * pow(1.0 - ct, 5.0), kPuddleFresnel * pud);\n"
                     "        vec2 at = gl_FragCoord.xy + rip.xy * 0.02 * (0.5 / uReflInv.y);\n"
                     "        c += texture2D(uRefl, clamp(at * uReflInv + uReflOff, uReflBox.xy, uReflBox.zw)).rgb * uWater * f * wetHere;\n"
                     "    }\n    ");
    else anchorMissed("water", "gl_FragColor = vec4(c,");
    // SNOW LYING (`o3de/wetfloor.h` coverRuns, `scene.frag`'s rule): the floor
    // turned white under its own light - `c / texel`, the light alone - by the
    // cover (`uWet`) times the open sky, BEFORE the fog, which takes it as the rest
    const std::string lit = "vec3 c = clamp(t.rgb * shade";
    const auto lp = s.find(lit);
    const auto le = lp == std::string::npos ? lp : s.find(";\n", lp);
    // a BODY in the rain, `scene.frag`'s: darker and deeper in colour, and its
    // SHEEN on the body's normal (`vBodyN`, the vertex variant's - the plain
    // programs have no `vNrm`), in beads on its texture
    if (le != std::string::npos)
        s.insert(le + 2, "    if (uWetBody > 0.5 && uWet > 0.0) {\n"
                         "        float y = dot(c, vec3(0.299, 0.587, 0.114));\n"
                         "        c = max(mix(vec3(y), c, 1.0 + kWetBodySat * uWet), 0.0) * (1.0 - kWetBodyDark * uWet);\n"
                         "        vec3 nB = normalize(vBodyN), v = normalize(vToEye);\n"
                         "        float bead = smoothstep(kWetBodyBeadLo, kWetBodyBeadHi, dustNoise(vec3(vUV * kWetBodyBeadScale, 0.5)));\n"
                         "        float rim = pow(1.0 - abs(dot(nB, v)), kWetBodyRim) * mix(kWetBodyRimFilm, 1.0, bead);\n"
                         "        float sky = pow(max(-reflect(-v, nB).y, 0.0), kWetBodySkyPower) * kWetBodySky * bead;\n"
                         "        c = clamp(c + kWetBodySheen * uWet * (rim + sky) * shade, 0.0, 1.0);\n"
                         "    }\n"
                         "    if (uSnow > 0.5 && uWet > 0.0)\n"
                         "        c = mix(c, clamp(c / max(t.rgb, vec3(0.04)) * 0.9, 0.0, 1.0), uWet * vOpen *\n"
                         "                clamp((1.0 - dot(t.rgb, vec3(0.299, 0.587, 0.114))) * 1.4, 0.0, 1.0));\n");
    else anchorMissed("snow", lit);
    // the REFLECTION pass discards what lies below the water (Y is down) - a
    // posed body's place is the GPU's, so the CPU clip cannot do it
    const std::string m = "void main() {";
    const auto mp = s.find(m);
    if (mp != std::string::npos) s.insert(mp + m.size(), "\n    if (uClip != 0 && vWy > uClipY) discard;");
    else anchorMissed("water", m);
    return s;
}

// THE EMITTERS' LIGHT's variant of the ENHANCED fragment program
// (`o3de/emitterlight.h`, the remaster's A5b): a light whose inner radius is
// negative is a POINT light, shining from it to the fragment. Made at `init`
// only when asked - the enhanced program the Vita may cache is untouched.
std::string pointLightVariant(const std::string& frag) {
    std::string s = frag;
    const auto rep = [&](const std::string& from, const std::string& to, bool all) {
        int n = 0;
        for (auto at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
            s.replace(at, from.size(), to);
            ++n;
            if (!all) break;
        }
        if (n == 0) anchorMissed("emitters' light", from);
    };
    rep("vec3 litColour(vec3 n, vec3 w) {", "vec3 litColour(vec3 n, vec3 w, bool po) {", false);
    // ...and room for 64 lights, the set's 8 and the remaster's points
    rep("uniform vec4  uPL[24];", "uniform vec4  uPL[192];", false);
    rep("for (int i = 0; i < 8; ++i) {\n        if (float(i) >= uPLCount) break;",
        "for (int i = 0; i < 64; ++i) {\n        if (float(i) >= uPLCount) break;", false);
    rep("litColour(normalize(vNrm), vWorld)", "litColour(normalize(vNrm), vWorld, false)", true);
    rep("if (pa.w <= db.w) continue;          // a degenerate pair lights nothing\n"
        "        float fall = min(1.0 - (sqrt(d2) - db.w) / (pa.w - db.w), 1.0);",
        "if (po && db.w >= 0.0) continue;\n"
        "        float rB = abs(db.w);\n"
        "        if (pa.w <= rB) continue;\n"
        "        float fall = min(1.0 - (sqrt(d2) - rB) / (pa.w - rB), 1.0);", false);
    // a point light shines from it to the fragment, and WRAPS 0.3 round
    rep("float t = -dot(n, db.xyz * k);",
        "vec3 dir = db.w < 0.0 ? (d2 > 0.0 ? d / sqrt(d2) : vec3(0.0)) : db.xyz;\n"
        "        float t = db.w < 0.0 ? k * fall * clamp((sqrt(d2) - 30.0) / 60.0, 0.0, 1.0) * (0.3 + 0.7 * max(-dot(n, dir), 0.0))"
        " : -dot(n, db.xyz * k);", false);
    // ...and MODE 3, a set lit by the point lights alone; and the NIGHT,
    // `uSetDim` (View::setDim), on the painted and the start light of all three
    rep("vec3 litColour(vec3 n, vec3 w, bool po) {", "uniform float uSetDim;\nvec3 litColour(vec3 n, vec3 w, bool po) {", false);
    rep("min(vCol + litColour(normalize(vNrm), vWorld, false)", "min(vCol * uSetDim + litColour(normalize(vNrm), vWorld, false)", true);
    rep("min(vec3(uLitBase) + litColour(normalize(vNrm), vWorld, false)",
        "min(vec3(uLitBase) * uSetDim + litColour(normalize(vNrm), vWorld, false)", true);
    rep("if (uLit > 1.5) shade =",
        "if (uLit > 2.5) shade = min(vCol * uSetDim + litColour(normalize(vNrm), vWorld, true), vec3(1.0));\n"
        "    else if (uLit > 1.5) shade =", false);
    return s;
}

// THE BODY LIGHT's variant of a posing program (`o3de/lightprobe.h`, the
// remaster's A5): the start colour - the grey or the baked colour - times
// `uLightTint`. Made at `init` only when asked, as the water's is.
std::string tintVariant(const std::string& vert) {
    std::string s = "uniform vec3 uLightTint;\n" + vert;
    const std::string unlit = "vCol = aCol + vec3(wave);";
    const auto u = s.find(unlit);
    if (u != std::string::npos) s.replace(u, unlit.size(), "vCol = aCol * uLightTint + vec3(wave);");
    const std::string lit = "vec3 lit = uLightBlack > 0.5 ? vec3(uLightBase) : aCol;";
    const auto l = s.find(lit);
    if (l != std::string::npos)
        s.replace(l, lit.size(), "vec3 lit = (uLightBlack > 0.5 ? vec3(uLightBase) : aCol) * uLightTint;");
    // the plain posing program has the first, the enhanced one both
    if (u == std::string::npos && l == std::string::npos) anchorMissed("body light", unlit);
    return s;
}

// ...and its VERTEX side: the world height handed down as `vWy`, taken from
// whatever the program transforms - `aPos` for a set, `wp` for a posed body -
// and the ray to the eye as `vToEye`, made here where the floats are highp
std::string waterVertVariant(const char* vert) {
    // (a set's stage reads `aNormal` for a wet body's sheen - the plain one
    // declares it here - and a posing one hands down the normal it turned)
    const bool hasNormal = std::string(vert).find("attribute vec3  aNormal;") != std::string::npos;
    std::string s = std::string("uniform vec3 uEye;\nvarying float vWy;\nvarying vec3 vToEye;\nvarying vec2 vWxz;\n"
                                "varying float vOpen;\nvarying float vWall;\nvarying vec3 vBodyN;\n") +
                    (hasNormal ? "" : "attribute vec3 aNormal;\n") + vert;
    const bool posing = s.find("vec3 nrm = ") != std::string::npos;
    const std::string key = "gl_Position = uMvp * vec4(";
    const auto at = s.find(key);
    const auto end = at == std::string::npos ? at : s.find(", 1.0);", at);
    if (end == std::string::npos) { anchorMissed("water (vertex)", key); return s; }
    const std::string expr = s.substr(at + key.size(), end - at - key.size());
    // ...and the open sky a wet floor's corner carries in its phase
    // (`encodeOpenSky`: a floor's `-2 - open`, a wall's `-6 - open`, under the
    // depth tie's -8192 shift)
    s.insert(at, std::string("vBodyN = ") + (posing ? "nrm" : "aNormal") + ";\n    vWy = (" + expr + ").y;\n    vToEye = uEye - (" + expr + ");\n    vWxz = (" + expr +
                 ").xz;\n    float wph = aPhase < -4096.0 ? aPhase + 8192.0 : aPhase;\n"
                 "    vWall = wph <= -5.5 ? 1.0 : 0.0;\n"
                 "    vOpen = wph <= -5.5 ? clamp(-6.0 - wph, 0.0, 1.0) : wph <= -1.5 ? clamp(-2.0 - wph, 0.0, 1.0) : 1.0;\n    ");
    return s;
}

// ---- THE UPSCALE (`todo/remaster.md` A6) - AMD FSR 1 on GL ---------------
//
// `kEasuFrag` and `kRcasFrag` are AMD's 32-bit EASU and RCAS
// (`third_party/ffx_fsr1.h`, MIT - `FsrEasuF`, `FsrEasuSetF`, `FsrEasuTapF`,
// `FsrRcasF`) TRANSCRIBED into GLSL 1.00/1.20, which has no unsigned integers
// and no `textureGather`: the twelve texels are read one by one, and AMD's
// bit-trick reciprocals (`APrxLoRcpF1`, `APrxLoRsqF1`, `APrxMedRcpF1`) are
// exact ones - within their own error of the Vulkan pass, not bit for bit.
// Both run in TOP-ORIGIN pixel coordinates over a picture in the top rows of
// its texture (GL's row 0 the bottom), as the present pass reads them, and
// both CLAMP their reads to the drawn picture. EASU's constants are
// `FsrEasuCon`'s, in floats: `uIn` the drawn picture (`rs(w_) x rs(vh)`),
// `uOut` the output's (`w_ x vh`).
constexpr const char* kEasuFrag = R"(
uniform sampler2D uSrc;
uniform vec2 uSrcTex;      // the source texture's size
uniform vec2 uIn;          // the drawn picture, render pixels
uniform vec2 uOut;         // the output picture
uniform float uOutTexH;    // the output target's height
vec3 T(vec2 p) {
    p = clamp(p, vec2(0.0), uIn - 1.0);
    return texture2D(uSrc, vec2(p.x + 0.5, uSrcTex.y - 0.5 - p.y) / uSrcTex).rgb;
}
float L(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }
float rcp(float x) { return 1.0 / max(x, 1.0e-20); }
void setF(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float dc = lD - lC, cb = lC - lB;
    float lenX = rcp(max(abs(dc), abs(cb)));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    len += lenX * lenX * w;
    float ec = lE - lC, ca = lC - lA;
    float lenY = rcp(max(abs(ec), abs(ca)));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    len += lenY * lenY * w;
}
void tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}
void main() {
    vec2 ip = vec2(floor(gl_FragCoord.x), uOutTexH - 1.0 - floor(gl_FragCoord.y));
    vec2 pp = ip * (uIn / uOut) + (0.5 * uIn / uOut - 0.5);
    vec2 fp = floor(pp);
    pp -= fp;
    vec3 b = T(fp + vec2(0.0, -1.0)), c = T(fp + vec2(1.0, -1.0));
    vec3 e = T(fp + vec2(-1.0, 0.0)), f = T(fp), g = T(fp + vec2(1.0, 0.0)), h = T(fp + vec2(2.0, 0.0));
    vec3 i = T(fp + vec2(-1.0, 1.0)), j = T(fp + vec2(0.0, 1.0)), k = T(fp + vec2(1.0, 1.0)), l = T(fp + vec2(2.0, 1.0));
    vec3 n = T(fp + vec2(0.0, 2.0)), o = T(fp + vec2(1.0, 2.0));
    float bL = L(b), cL = L(c), eL = L(e), fL = L(f), gL = L(g), hL = L(h);
    float iL = L(i), jL = L(j), kL = L(k), lL = L(l), nL = L(n), oL = L(o);
    vec2 dir = vec2(0.0);
    float len = 0.0;
    setF(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    setF(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    setF(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    setF(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < 1.0 / 32768.0;
    dirR = zro ? 1.0 : inversesqrt(dirR);
    dir.x = zro ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) * rcp(max(abs(dir.x), abs(dir.y)));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = rcp(lob);
    vec3 min4 = min(min(f, g), min(j, k));
    vec3 max4 = max(max(f, g), max(j, k));
    vec3 aC = vec3(0.0);
    float aW = 0.0;
    tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    gl_FragColor = vec4(min(max4, max(min4, aC / aW)), 1.0);
}
)";

// RCAS over EASU's picture (`uPic` its size), the sharpness `uSharp` -
// `exp2(-stops)`, `FsrRcasCon`'s `con.x`. No denoise, as on Vulkan.
constexpr const char* kRcasFrag = R"(
uniform sampler2D uSrc;
uniform vec2 uSrcTex;
uniform vec2 uPic;
uniform float uSharp;
vec3 T(vec2 p) {
    p = clamp(p, vec2(0.0), uPic - 1.0);
    return texture2D(uSrc, vec2(p.x + 0.5, uSrcTex.y - 0.5 - p.y) / uSrcTex).rgb;
}
float mx3(float x, float y, float z) { return max(x, max(y, z)); }
float mn3(float x, float y, float z) { return min(x, min(y, z)); }
void main() {
    vec2 sp = vec2(floor(gl_FragCoord.x), uSrcTex.y - 1.0 - floor(gl_FragCoord.y));
    vec3 b = T(sp + vec2(0.0, -1.0)), d = T(sp + vec2(-1.0, 0.0)), e = T(sp);
    vec3 f = T(sp + vec2(1.0, 0.0)), h = T(sp + vec2(0.0, 1.0));
    vec3 mn4 = min(min(b, d), min(f, h));
    vec3 mx4 = max(max(b, d), max(f, h));
    // (black ring: 0 / 0, and white: 0 / -0 - AMD's leave a NaN for the GPU's
    // min and max to drop; GLSL promises nothing of NaN, so they are kept finite)
    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1.0e-20));
    vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1.0e-20));
    vec3 lobeC = max(-hitMin, hitMax);
    float lobe = max(-(0.25 - 1.0 / 16.0), min(mx3(lobeC.r, lobeC.g, lobeC.b), 0.0)) * uSharp;
    vec3 o = (lobe * b + lobe * d + lobe * h + lobe * f + e) / (4.0 * lobe + 1.0);
    gl_FragColor = vec4(o, 1.0);
}
)";


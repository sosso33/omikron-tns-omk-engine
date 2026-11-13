#version 450
// THE AMBIENT OCCLUSION (the remaster's A4, `todo/remaster.md`), at LOW
// strength: the sets' vertex colours already hold the artists' painted
// occlusion, so this only adds CONTACT - a crate against a wall, a step on
// the ground - and never doubles the painted dark. The SET's alone: a body
// neither takes it nor casts it (mode 3), its ground shadow is the shadow
// map's (`todo/enhancements.md` 5). Off unless asked
// (`[Remaster] ao = S`, `--ao S`). NOT IN THE ORIGINAL.
//
// It reads the frame's own DEPTH (kept for it - `makeTarget`), half size, in
// four modes of one shader:
//
//   mode 3  DEPTH: the half-size view z, from one full-size depth each (the
//           4K multisampled depth read 17 times a pixel at random cost 8 ms
//           of 23 on the M3 - every later mode reads this instead); a BODY's
//           pixel taken as the sky - the occlusion is the set's
//   mode 0  AO: the view-space point and its normal from the depths around
//           it, then 12 samples on a disk of `radius` game units, rotated per
//           pixel - the Alchemy estimator, sum max(0, v.n - bias) / (v.v + e),
//           each faded past the radius. 8 samples, the disk at most 4% of
//           the picture's width. Faded out with the game's own FOG
//           (what the fog hides the AO must not darken) and past `kFar`.
//   mode 1  BLUR along x, mode 2 along y: 9 taps weighted by depth - an edge
//           keeps its own side's occlusion.
//
// Compiled twice: plain, and `-DOMK_MS` where the depth is MULTISAMPLED
// (`--aa`, `--enhance-all`) - sample 0 read.
layout(push_constant) uniform Push {
    vec4  view;      // tanH, tanV, the picture's width and height in render pixels
    vec4  depth;     // A, B (d = A + B / z), fog start, fog end (fog end 0: no fog)
    float radius;    // game units (~cm)
    float strength;
    int   mode;
    int   pad;
    vec2  srcInv;    // 1 / the AO image's size (the blur's source)
} pc;

#ifdef OMK_MS
layout(set = 0, binding = 0) uniform sampler2DMS depthTex;
#else
layout(set = 0, binding = 0) uniform sampler2D depthTex;
#endif
layout(set = 0, binding = 1) uniform sampler2D aoTex;
layout(set = 0, binding = 2) uniform sampler2D zTex;   // mode 3's output, half size
layout(set = 0, binding = 3) uniform sampler2D colourTex;   // the frame's colour: a BODY's alpha is 0
layout(location = 0) out vec4 outAo;

const float kFar = 8000.0;   // no contact detail to add past ~80 m

float depthAt(ivec2 q) {
#ifdef OMK_MS
    q = clamp(q, ivec2(0), textureSize(depthTex) - 1);
    return texelFetch(depthTex, q, 0).r;   // sample 0
#else
    q = clamp(q, ivec2(0), textureSize(depthTex, 0) - 1);
    return texelFetch(depthTex, q, 0).r;
#endif
}

// the view-space z of a depth (1: the clear - the sky)
float viewZ(float d) { return pc.depth.y / (d - pc.depth.x); }

vec3 viewPos(vec2 px, float z) {
    vec2 ndc = px / pc.view.zw * 2.0 - 1.0;
    return vec3(ndc.x * pc.view.x * z, -ndc.y * pc.view.y * z, z);
}

// the half-size view z (1e9: the sky)
float zAt(ivec2 p) { return texelFetch(zTex, clamp(p, ivec2(0), textureSize(zTex, 0) - 1), 0).r; }

// a half-size pixel's view-space point
vec3 posAt(ivec2 p) { return viewPos(vec2(p) * 2.0 + 1.0, zAt(p)); }

void main() {
    ivec2 o = ivec2(gl_FragCoord.xy);
    if (pc.mode == 3) {
        ivec2 q = o * 2;   // the full-size pixel this half-size one stands for
        float d = depthAt(q);
        // a BODY is the sky here (`scene.frag`'s alpha 0): it neither takes the
        // occlusion nor casts it - a low-poly figure occluding itself smudged
        // chins, armpits and legs black, and haloed the wall behind it (the
        // reader, 2026-10-22: "too strong" on the characters)
        bool body = texelFetch(colourTex, clamp(q, ivec2(0), textureSize(colourTex, 0) - 1), 0).a < 0.5 / 255.0;
        outAo = vec4(d >= 1.0 || body || q.x >= int(pc.view.z) || q.y >= int(pc.view.w) ? 1e9 : viewZ(d));
    } else if (pc.mode == 0) {
        float z = zAt(o);
        if (z >= 1e8) { outAo = vec4(1.0); return; }
        float fade = 1.0 - smoothstep(kFar * 0.6, kFar, z);
        if (pc.depth.w > pc.depth.z)
            fade *= 1.0 - smoothstep(pc.depth.z, pc.depth.w, z);
        if (fade <= 0.0) { outAo = vec4(1.0); return; }
        vec3 P = posAt(o);
        // the normal from the nearer neighbour on each axis (an edge's far
        // side is another surface)
        vec3 l = posAt(o - ivec2(1, 0)), r = posAt(o + ivec2(1, 0));
        vec3 u = posAt(o - ivec2(0, 1)), b = posAt(o + ivec2(0, 1));
        vec3 dx = abs(r.z - z) < abs(z - l.z) ? r - P : P - l;
        vec3 dy = abs(b.z - z) < abs(z - u.z) ? b - P : P - u;
        vec3 n = normalize(cross(dx, dy));
        if (dot(n, P) > 0.0) n = -n;   // towards the eye
        float rpx = pc.radius / z / pc.view.x * pc.view.z * 0.25;   // the radius in HALF-size pixels
        if (rpx < 1.0) { outAo = vec4(1.0); return; }
        rpx = min(rpx, pc.view.z * 0.02);
        // interleaved gradient noise: the disk turned per pixel, the blur evens it
        float a0 = 6.2831853 * fract(52.9829189 * fract(dot(vec2(o), vec2(0.06711056, 0.00583715))));
        const int N = 8;
        float r2 = pc.radius * pc.radius, sum = 0.0;
        // the disk's 8 directions: i golden angles on from a0 - a0's rotation
        // of the constant ones (cos and sin of float(i) * 2.3999632, the float
        // the loop computed), two trigonometric calls a pixel, not sixteen
        const vec2 kDir[N] = vec2[N](vec2(1, 0), vec2(-0.737368818, 0.67549036), vec2(0.0874255469, -0.996171056), vec2(0.608439263, 0.793600443), vec2(-0.984713547, -0.174181599), vec2(0.843755183, -0.536728228), vec2(-0.259603327, 0.965715337), vec2(-0.460907791, -0.887448031));
        const float ca = cos(a0), sa = sin(a0);
        for (int i = 0; i < N; ++i) {
            float t = (float(i) + 0.5) / float(N);
            const vec2 d = kDir[i];
            vec2 off = vec2(ca * d.x - sa * d.y, sa * d.x + ca * d.y) * rpx * sqrt(t);
            vec3 S = posAt(o + ivec2(off));
            vec3 v = S - P;
            float vv = dot(v, v);
            float fall = max(0.0, 1.0 - vv / r2);
            sum += max(0.0, dot(v, n) - 0.01 * z) / (vv + 0.01 * r2) * fall;
        }
        float ao = max(0.0, 1.0 - 2.0 * pc.strength * sum / float(N) * pc.radius * 0.5);
        outAo = vec4(mix(1.0, ao, fade));
    } else {
        // the BLUR, weighted by depth
        vec2 dir = pc.mode == 1 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        float z0 = zAt(o);
        float acc = 0.0, wsum = 0.0;
        for (int k = -4; k <= 4; ++k) {
            ivec2 p = o + ivec2(dir) * k;
            float zk = zAt(p);
            float w = exp(-float(k * k) / 8.0) * exp(-abs(zk - z0) / (0.03 * z0 + 1.0));
            acc += texelFetch(aoTex, clamp(p, ivec2(0), textureSize(aoTex, 0) - 1), 0).r * w;
            wsum += w;
        }
        outAo = vec4(acc / wsum);
    }
}

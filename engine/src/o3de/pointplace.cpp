// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/pointplace.h"

#include <cstdlib>

// NO FUSED MULTIPLY-ADD in this file: the generic loop must compute exactly
// what the NEON loop computes (see the header).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define OMK_HAS_NEON 1
#else
#define OMK_HAS_NEON 0
#endif

namespace omk {

namespace {

// One point, `qrot`'s own expression order (actor/pose.cpp) - the order the
// NEON loop reproduces lane by lane.
inline void placeOne(const PointPlace& p, const float* in, float* out) {
    const float v0 = (in[0] - p.origin[0]) * p.scale[0];
    const float v1 = (in[1] - p.origin[1]) * p.scale[1];
    const float v2 = (in[2] - p.origin[2]) * p.scale[2];
    float r0 = v0, r1 = v1, r2 = v2;
    if (p.rotated) {
        const Quatf& q = p.q;
        const float tx = 2.0f * (q.y * v2 - q.z * v1);
        const float ty = 2.0f * (q.z * v0 - q.x * v2);
        const float tz = 2.0f * (q.x * v1 - q.y * v0);
        r0 = v0 + q.w * tx + (q.y * tz - q.z * ty);
        r1 = v1 + q.w * ty + (q.z * tx - q.x * tz);
        r2 = v2 + q.w * tz + (q.x * ty - q.y * tx);
    }
    out[0] = r0 + p.at[0];
    out[1] = r1 + p.at[1];
    out[2] = r2 + p.at[2];
}

}  // namespace

void placePointsGeneric(const PointPlace& p, const float* in, std::size_t inStride,
                        float* out, std::size_t outStride,
                        const std::uint32_t* idx, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t k = idx[i];
        float o[3];
        placeOne(p, in + k * inStride, o);
        float* w = out + k * outStride;
        w[0] = o[0]; w[1] = o[1]; w[2] = o[2];
    }
}

bool pointPlaceHasNeon() { return OMK_HAS_NEON != 0; }

#if OMK_HAS_NEON
void placePointsNeon(const PointPlace& p, const float* in, std::size_t inStride,
                     float* out, std::size_t outStride,
                     const std::uint32_t* idx, std::size_t n) {
    const float32x4_t ox = vdupq_n_f32(p.origin[0]), oy = vdupq_n_f32(p.origin[1]),
                      oz = vdupq_n_f32(p.origin[2]);
    const float32x4_t sx = vdupq_n_f32(p.scale[0]), sy = vdupq_n_f32(p.scale[1]),
                      sz = vdupq_n_f32(p.scale[2]);
    const float32x4_t ax = vdupq_n_f32(p.at[0]), ay = vdupq_n_f32(p.at[1]),
                      az = vdupq_n_f32(p.at[2]);
    const float32x4_t qw = vdupq_n_f32(p.q.w), qx = vdupq_n_f32(p.q.x),
                      qy = vdupq_n_f32(p.q.y), qz = vdupq_n_f32(p.q.z);
    const float32x4_t two = vdupq_n_f32(2.0f);
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        const std::size_t k0 = idx[i], k1 = idx[i + 1], k2 = idx[i + 2], k3 = idx[i + 3];
        float32x4_t x, y, z;
        if (inStride == 3 && k1 == k0 + 1 && k2 == k0 + 2 && k3 == k0 + 3) {
            // four consecutive packed points - a triangle soup's run
            const float32x4x3_t v = vld3q_f32(in + k0 * 3);
            x = v.val[0]; y = v.val[1]; z = v.val[2];
        } else {
            const float* a = in + k0 * inStride;
            const float* b = in + k1 * inStride;
            const float* c = in + k2 * inStride;
            const float* d = in + k3 * inStride;
            const float xs[4] = {a[0], b[0], c[0], d[0]};
            const float ys[4] = {a[1], b[1], c[1], d[1]};
            const float zs[4] = {a[2], b[2], c[2], d[2]};
            x = vld1q_f32(xs); y = vld1q_f32(ys); z = vld1q_f32(zs);
        }
        const float32x4_t v0 = vmulq_f32(vsubq_f32(x, ox), sx);
        const float32x4_t v1 = vmulq_f32(vsubq_f32(y, oy), sy);
        const float32x4_t v2 = vmulq_f32(vsubq_f32(z, oz), sz);
        float32x4_t r0 = v0, r1 = v1, r2 = v2;
        if (p.rotated) {
            // `placeOne`'s expressions, operation for operation
            const float32x4_t tx = vmulq_f32(two, vsubq_f32(vmulq_f32(qy, v2), vmulq_f32(qz, v1)));
            const float32x4_t ty = vmulq_f32(two, vsubq_f32(vmulq_f32(qz, v0), vmulq_f32(qx, v2)));
            const float32x4_t tz = vmulq_f32(two, vsubq_f32(vmulq_f32(qx, v1), vmulq_f32(qy, v0)));
            r0 = vaddq_f32(vaddq_f32(v0, vmulq_f32(qw, tx)),
                           vsubq_f32(vmulq_f32(qy, tz), vmulq_f32(qz, ty)));
            r1 = vaddq_f32(vaddq_f32(v1, vmulq_f32(qw, ty)),
                           vsubq_f32(vmulq_f32(qz, tx), vmulq_f32(qx, tz)));
            r2 = vaddq_f32(vaddq_f32(v2, vmulq_f32(qw, tz)),
                           vsubq_f32(vmulq_f32(qx, ty), vmulq_f32(qy, tx)));
        }
        const float32x4_t wx = vaddq_f32(r0, ax), wy = vaddq_f32(r1, ay), wz = vaddq_f32(r2, az);
        if (outStride == 3 && k1 == k0 + 1 && k2 == k0 + 2 && k3 == k0 + 3) {
            float32x4x3_t w;
            w.val[0] = wx; w.val[1] = wy; w.val[2] = wz;
            vst3q_f32(out + k0 * 3, w);
        } else {
            float xs[4], ys[4], zs[4];
            vst1q_f32(xs, wx); vst1q_f32(ys, wy); vst1q_f32(zs, wz);
            const std::size_t ks[4] = {k0, k1, k2, k3};
            for (int j = 0; j < 4; ++j) {
                float* w = out + ks[j] * outStride;
                w[0] = xs[j]; w[1] = ys[j]; w[2] = zs[j];
            }
        }
    }
    // the tail, one at a time - the same arithmetic
    if (i < n) placePointsGeneric(p, in, inStride, out, outStride, idx + i, n - i);
}
#else
void placePointsNeon(const PointPlace& p, const float* in, std::size_t inStride,
                     float* out, std::size_t outStride,
                     const std::uint32_t* idx, std::size_t n) {
    placePointsGeneric(p, in, inStride, out, outStride, idx, n);
}
#endif

void placePoints(const PointPlace& p, const float* in, std::size_t inStride,
                 float* out, std::size_t outStride,
                 const std::uint32_t* idx, std::size_t n) {
#if OMK_HAS_NEON
    // `OMK_NO_NEON` runs the generic loop in the same binary, for comparing
    static const bool noNeon = std::getenv("OMK_NO_NEON") != nullptr;
    if (noNeon) placePointsGeneric(p, in, inStride, out, outStride, idx, n);
    else placePointsNeon(p, in, inStride, out, outStride, idx, n);
#else
    placePointsGeneric(p, in, inStride, out, outStride, idx, n);
#endif
}

}  // namespace omk

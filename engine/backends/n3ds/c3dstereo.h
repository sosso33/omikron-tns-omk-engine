// SPDX-License-Identifier: GPL-3.0-or-later
// STEREOSCOPIC 3D on the top screen (`todo/3ds-port.md` step 8) - an
// ENHANCEMENT the original never had, OFF unless `sdmc:/omk/stereo3d` is on
// the card. The citro3d backend draws both eyes in ONE pass, each draw twice:
// each eye at the screen's own 400x224 (no 2x2 average), stacked in a 400x448
// target - so the GPU fills no more pixels than the mono 800x448 picture -
// through an OFF-AXIS projection: the eye moved along the camera's right
// vector, the picture sheared back so the convergence plane has no parallax.
//
// The CONVERGENCE plane is the camera's look-at point - the player, behind
// the follow camera - so the player sits at the screen and the city recedes
// behind it. The depth is the console's 3D slider (`osGet3DSliderState`); at 0
// the frame is drawn mono. The parallax at infinity is `kStereoParallax`
// pixels between the eyes at full slider - a first figure, to be judged by
// watching on the console (the plan: "a decision made by watching").
//
// The file may hold a number 0..1, which then stands for the slider - Azahar
// starts its slider at 0, and a run that measures the 3D must not depend on a
// person moving it.
#pragma once

#include <3ds.h>

#include <cstdio>

namespace omk::n3ds {

constexpr float kStereoParallax = 8.0f;    // pixels at infinity, slider at 1

struct StereoCard {
    bool on = false;
    float slider = -1.0f;                  // < 0: the console's own slider
    void read() {
        std::FILE* f = std::fopen("sdmc:/omk/stereo3d", "r");
        if (!f) return;
        on = true;
        float s = 0.0f;
        if (std::fscanf(f, "%f", &s) == 1) slider = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
        std::fclose(f);
    }
    float depth() const { return slider >= 0.0f ? slider : osGet3DSliderState(); }
};

// The half-separation, in world units, that puts `parallaxPx` pixels between
// the eyes at infinity on an eye picture `eyeW` wide, the convergence plane at
// view depth `c`: an eye moved by `o` along `s` and sheared back shifts a
// point at infinity by `o / (tanH c)` in NDC, so the two eyes are
// `2h / (tanH c)` apart, `h W / (tanH c)` pixels.
inline float stereoHalfSeparation(float parallaxPx, float tanH, float c, int eyeW) {
    return eyeW > 0 ? parallaxPx * tanH * c / static_cast<float>(eyeW) : 0.0f;
}

// Row 0 of the view-projection for an eye at `o` along the camera's right
// vector (left eye negative): the eye's own row 0, `row0 - (0,0,0, o/tanH)`,
// plus the shear `o / (tanH c) * row3` that brings depth `c` back to zero
// parallax.
inline void stereoEyeRow0(const float row0[4], const float row3[4], float o, float tanH, float c,
                          float out[4]) {
    const float k = o / (tanH * c);
    for (int i = 0; i < 4; ++i) out[i] = row0[i] + k * row3[i];
    out[3] -= o / tanH;
}

}  // namespace omk::n3ds

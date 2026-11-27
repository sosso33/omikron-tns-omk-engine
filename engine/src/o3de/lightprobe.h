// SPDX-License-Identifier: GPL-3.0-or-later
// THE BODIES LIT BY THEIR SURROUNDINGS - the remaster's A5 (`todo/remaster.md`).
// NOT IN THE ORIGINAL: a `[Remaster]` option (`bodylight = S`,
// `--body-light S`), off unless named.
//
// The original starts every lit body from ONE grey, the scene's `+416` (the
// set's ambient, `Draw::lightBase`), and adds the set's lights that reach it;
// the static set is shaded instead by a colour the artists BAKED into every
// vertex (ASSETS 4c). So a character under a neon sign is as grey as one in a
// dark alley. This reads the painted light: the set's own opaque surface,
// its corners' colours interpolated and summed by area into cells at load,
// and around a body the weighted mean of the cells near it - the colour the
// artists gave that place. Brighter than the set's mean colour, a body starts
// from the grey PLUS the departure, `grey + S (probe - mean)` per channel;
// darker, the grey SHADED in proportion, `grey (1 - S (1 - probe / mean))` -
// `mean` the probe's mean at body height over the set's floors - so the
// city's average character keeps the original's grey and only the
// DEPARTURES - a neon, a dark corner - carry over, in brightness and hue.
// (Brightening is added, not multiplied: a set's grey can be nearly black -
// Anekbah's is 10 of 255 - and its lamps outweigh any multiple of it.
// Darkening is multiplied: added, it took a body by a fire to black.) It
// reaches the renderer as a TINT of the grey, `Draw::lightTint`. No light is added; the set's
// lights still add on top. `--remaster DIR` sets it at 1.
//
// No oracle: the original has no such thing. Tier 6 - the rule's own
// invariants (a uniform set gives a tint of 1 everywhere; the tint is
// continuous as a body walks), checked by `verify.py: engine: body light`.
// Drawn by Vulkan and GLES (`Renderer::setBodyTint`); the software reference,
// GL1 and the 3DS refuse it.
#pragma once

#include "o3de/geom3do.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace omk {

class LightProbes {
public:
    // The set's OPAQUE surface (its additive water and glass, its multiply
    // shadows are not light) into cells of `kCell` units, by AREA - each
    // triangle cut into pieces no longer than a cell, its corners' colour
    // interpolated. -> false when the set has none.
    bool build(const Geometry& g);
    bool built() const { return n_ > 0; }

    // The painted colour around `p`: each cell within `kReach` weighted by
    // (1 - d^2 / R^2)^2 on its surface's centroid, so it moves smoothly as `p`
    // does. -> false (and `rgb` untouched) when no cell is in reach.
    bool sample(const float p[3], float rgb[3]) const;

    // The tint of `base` (the grey the body starts from) a body at `p` takes,
    // at strength `s` (0 = 1, 1, 1 exactly): `start / base`, the start
    // `base + s (probe - mean)` where the probe is brighter than the mean,
    // `base (1 - s (1 - probe / mean))` where darker, per channel, clamped to
    // 0..1. A point out of every cell's reach, or a base of 0, takes 1.
    void tint(const float p[3], float s, float base, float out[3]) const;

    // the brightness the tint divides by: the probe's mean at body height over
    // the set's floors, by area (a set with no level face: its surface's mean)
    float meanLuma() const { return meanLuma_; }
    const float* meanRgb() const { return meanRgb_; }

    // in world units, which are INCHES (`kInchesPerMetre`) - set by eye, the
    // metres beside them only what they come to
    static constexpr float kCell = 200.0f;        // ~5 m
    static constexpr float kReach = 600.0f;       // ~15 m
    static constexpr float kBodyHeight = 90.0f;   // a floor's probe ~2.3 m up (meant as the hips, read as cm)

private:
    struct Cell { double r = 0, g = 0, b = 0, x = 0, y = 0, z = 0, w = 0; };   // sums by area
    static std::uint64_t key(int x, int y, int z);
    std::unordered_map<std::uint64_t, Cell> cells_;
    // THE CELLS NEAR A CELL, kept: `sample` looked up all 7x7x7 keys of the
    // window round its point - 343 hash finds a body a frame, most of them
    // empty: the crowd's body light ~0.35 ms on the M1 for 34 walkers, ~3 on
    // the Switch (2026-11-27). Now each cell's window is read ONCE, the cells
    // that exist copied in the window's own z, y, x order, and a sample walks
    // that list with the same arithmetic in the same order - the same bits.
    // Shared by copies (built afresh by `build`), filled under its lock from
    // any thread (the crowd's bodies are posed on several).
    // ...each kept with its centre already divided out (`x / w` - the same
    // operation on the same values as the sample made, once)
    struct NearCell { double cx, cy, cz, w, r, g, b; };
    struct Near;
    std::shared_ptr<Near> near_;
    const std::vector<NearCell>& nearOf(int cx, int cy, int cz) const;
    long  n_ = 0;          // pieces
    float meanLuma_ = 0.0f;
    float meanRgb_[3] = {0.0f, 0.0f, 0.0f};   // ...and its colour
};

}  // namespace omk

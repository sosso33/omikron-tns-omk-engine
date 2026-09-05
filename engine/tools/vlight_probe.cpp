// SPDX-License-Identifier: GPL-3.0-or-later
// THE CROWD'S DYNAMIC LIGHTING, measured on known inputs.
//
//     vlight_probe
//
// A synthetic light and four vertices with known normals, so nothing of any
// model's geometry is in the answer. Prints one line per case:
//   <label> <r> <g> <b> <reached>
//
// `verify.py: engine vertex light`. See `engine/src/o3de/vertexlight.h`.
#include "o3de/vertexlight.h"

#include <cstdio>
#include <vector>

int main() {
    // a light 100 units up, shining DOWN (+Y is down in this game), warm
    // orange, radii 1000 outer / 200 inner, intensity 1.0
    omk::Light3do l;
    l.pos[0] = 0; l.pos[1] = -100; l.pos[2] = 0;
    // OBLIQUE on purpose. With the light shining straight down every
    // component of the direction but one is zero, and a probe like that cannot
    // tell `l.dir` from the on-disk `l.corner[0]` - which is how a first
    // version of this check passed the exact bug the port had made. Here
    // centre - pos = (60, 100, 80), so all three components are non-zero and
    // any one of them being wrong moves the answer.
    l.centre[0] = 60; l.centre[1] = 0; l.centre[2] = 80;
    const float dv[3] = {60.0f, 100.0f, 80.0f};
    const float dl = 141.42136f;                    // |(60,100,80)|
    l.dir[0] = dv[0] / dl; l.dir[1] = dv[1] / dl; l.dir[2] = dv[2] / dl;
    l.radiusA = 1000.0f; l.radiusB = 200.0f;
    l.f32 = 1.0f;
    l.colour = 0x00F4B168;                          // R 244  G 177  B 104
    // A REAL on-disk corner 0, which is a world-space point and NOT the
    // direction. Without this the probe cannot tell `l.dir` from
    // `l.corner[0]` - the light shines on an axis and an all-zero corner
    // gives the same answer as the right one, which is how a first version of
    // this check passed the exact bug the port had made.
    l.corner[0][0] = 5000.0f; l.corner[0][1] = 3.0f; l.corner[0][2] = -6000.0f;
    const std::vector<omk::Light3do> lights = {l};

    struct Case { const char* label; float n[3]; float body[3]; };
    const Case cases[] = {
        // a normal pointing UP (-Y) faces the downward light: t = -(N.L) > 0
        {"facing",   {0, -1, 0}, {0, 0, 0}},
        // pointing DOWN, away from it: no contribution
        {"away",     {0,  1, 0}, {0, 0, 0}},
        // side-on: N.L is 0
        {"side",     {1,  0, 0}, {0, 0, 0}},
        // an OBLIQUE normal, so all three of the direction's components are
        // in the answer
        {"oblique",  {-0.5773503f, -0.5773503f, -0.5773503f}, {0, 0, 0}},
        // A normal facing -X, which the direction's X component alone decides.
        // This is the case that separates `l.dir` from the on-disk corner:
        // right it is a fraction of the ramp, wrong it saturates.
        {"xfacing",  {-1, 0, 0}, {0, 0, 0}},
        // facing, but 900 units off - inside the outer radius, well past the
        // inner, so the linear falloff has most of the way to run
        {"far",      {0, -1, 0}, {890, 0, 0}},
        // facing and OUTSIDE the outer radius: the light does not reach
        {"beyond",   {0, -1, 0}, {2000, 0, 0}},
    };

    for (const auto& c : cases) {
        omk::Geometry g;
        omk::Corner v;
        v.x = c.body[0]; v.y = c.body[1]; v.z = c.body[2];
        v.u = v.v = 0; v.phase = -1.0f;
        v.r = v.g = v.b = 0.0f;                     // the BASE IS BLACK
        v.nx = c.n[0]; v.ny = c.n[1]; v.nz = c.n[2];
        g.corners.push_back(v);
        const int hit = omk::applyLights(g, 0, 1, c.body, lights);
        const omk::Corner& o = g.corners[0];
        std::printf("%s %d %d %d %d\n", c.label,
                    static_cast<int>(o.r * 255.0f + 0.5f),
                    static_cast<int>(o.g * 255.0f + 0.5f),
                    static_cast<int>(o.b * 255.0f + 0.5f), hit);
    }
    return 0;
}

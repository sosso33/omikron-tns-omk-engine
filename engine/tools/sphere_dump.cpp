// SPDX-License-Identifier: GPL-3.0-or-later
// A MODEL'S COLLISION SPHERES - the body the sweep actually moves.
//
//     sphere_dump <model.3DO>
//
// The descriptor's +244/+248 sphere list, in the model's frame, with each
// sphere's BOTTOM worked out: the controller hangs them off the feet, so a
// bottom equal to the pelvis-to-feet distance means the sphere rests on the
// ground and every riser in the world is inside it. That is what it turned out
// to be for Kay'l (41.81 against 41.8), and it is why the sweep had to start a
// step above the feet (`todo/next-tasks.md` 20, `actor/walk.cpp`).
#include "actor/spatial.h"
#include "platform/datafs.h"
#include <cstdio>
int main(int argc, char** argv) {
    const auto d = omk::DataFs::readPath(argv[1]);
    const auto sp = omk::modelSweepSpheres(d);
    std::printf("%zu spheres (offsets are from the model origin):\n", sp.size());
    for (const auto& s : sp)
        std::printf("   centre %7.2f %7.2f %7.2f   radius %6.2f   bottom %7.2f\n",
                    s.pos[0], s.pos[1], s.pos[2], s.radius, s.pos[1] + s.radius);
    return 0;
}

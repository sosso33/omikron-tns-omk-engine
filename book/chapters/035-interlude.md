<div class="pagebreak"></div>

# Interlude — one frame in Anekbah

Part III ends the description of the original engine. Before turning to how it
was read and rebuilt, it is worth putting the pieces together once, on the
running example. You are standing in Anekbah's main street. You press
*forward*. Here is what one frame of the game does with that, in order, with
the chapter that explains each step.

**The clock** (chapter 2). The message pump has nothing to do, so the frame
runs. The machine is keeping up, so `dt = 30 / fps` comes out at about 1.0:
one thirtieth of a second of game time.

**The input** (chapter 6). The keyboard is read through the adventure
scheme's bindings, and *forward* sets one of the fourteen bits in the input
word. Nothing else sees the key.

**The scripts** (chapter 4). The frame first runs every live script context.
In a quiet street almost all of them are parked, waiting for a zone, a screen
or a message, so almost nothing executes. The port measured it: about fifty
instructions in 150 frames. If your step carries you into a trigger zone, its
*enter* script is queued, and it will run from its first instruction.

**Your body** (chapter 6). Your state graph is in `H_STAND`. One of its edges
names the *forward* bit, so the channel takes it and the body starts the walk
clip. The walker then asks whether the step is possible: it probes the floor
under the new position, checks the slope is under 30° and any rise under 30
cm, and sweeps the body's spheres against the walls.

**The crowd** (chapter 6). Every walker spawned along the street's lanes takes
its own step along its route. One slows because another is ahead of it at a
shared junction. One brushes past you, and the push test nudges you aside.

**The scene** (chapter 7). Scene objects with running programs advance their
steps, and a crane's cargo swings along its path. The camera follows you,
since no editing is holding it.

**The draw** (chapter 8). The visible-set walk culls the set's meshes against
the frustum, and every triangle seen from behind is dropped. Every surviving
triangle goes into a bucket by its 14-bit key: the
street's opaque walls and floors into the low buckets texture by texture, the
lamp's additive glow near the top. The crowd and you are posed on the CPU, and
lit per vertex by the lights the set's model carries. Under each body, a soft
shadow quad is copied beneath its chest, and beneath its head, legs and arms
depending on the detail setting, faded by how far the floor is below each
bone. The far skyline's vertex colours shimmer on the frame clock.
`Render_FlushBuckets` walks the keys upward. On the shop sign on the wall, the
two sides of the panel share one position, but only the side facing you
survived the back-face test, so its advert is the one you see. The frame goes
out dithered to 16 bits.

**The sound** (chapter 9). The engine decides which of its voices play your
footsteps, triggered on a frame of the walk clip, and how loud the street's
ambience is from where you stand. DirectSound mixes.

**The interface** (chapter 10). No screen is open, so the 2D display list
has little to add; it is walked over the picture all the same, layer by layer.

Then the pump goes idle again, and in a thirtieth of a second it all happens
once more.

Every step in that list was once a question, answered by reading a function
in the executable. The next part is about how that reading was done, and
how anyone can tell whether it was done right.

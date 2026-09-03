#version 450
// The scene vertex shader. The transform is handed down as a finished matrix
// from `cameraBasis` - the CPU side owns the conventions (world up is (0,-1,0)
// because the game's Y points DOWN; hfov is HORIZONTAL) so that this file
// cannot disagree with the software rasterizer about them.
layout(push_constant) uniform Push {
    mat4 mvp;
    int  cutout;      // flag 0x800: a colour key on black, never alpha
    int  pad0, pad1, pad2;
} pc;

layout(location = 0) in vec3 inPos;    // world position
layout(location = 1) in vec2 inUV;     // TEXEL units, as the shipped data stores them
layout(location = 2) in vec3 inCol;    // the baked light - a COLOUR, not a brightness

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vCol;

void main() {
    vUV  = inUV;
    vCol = inCol;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
}

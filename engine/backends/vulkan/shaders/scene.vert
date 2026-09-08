#version 450
// The scene vertex shader. The transform is handed down as a finished matrix
// from `cameraBasis` - the CPU side owns the conventions (world up is (0,-1,0)
// because the game's Y points DOWN; hfov is HORIZONTAL) so that this file
// cannot disagree with the software rasterizer about them.
layout(push_constant) uniform Push {
    mat4  mvp;
    int   cutout;      // flag 0x800: a colour key on black, never alpha
    float fogStart;    // 0 = no fog for this batch; see renderer.h's View
    float fogEnd;
    int   caster;      // a caster does not receive; see scene.frag
    vec3  fogColour;   // 16-BYTE ALIGNED at offset 80 - see scene.frag
    int   lit;         // 92, and it must come AFTER the vec3
} pc;

layout(location = 0) in vec3 inPos;    // world position
layout(location = 1) in vec2 inUV;     // TEXEL units, as the shipped data stores them
layout(location = 2) in vec3 inCol;    // the baked light - a COLOUR, not a brightness
layout(location = 3) in vec3 inNrm;    // the vertex NORMAL, for row 7's lighting

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vCol;
// The VIEW-SPACE depth, in world units, for the fog. Row 3 of `mvp` is
// `f . (world - eye)` - the forward axis dotted with the offset from the eye -
// so `gl_Position.w` is exactly the `z` raster.cpp's inner loop fogs on.
layout(location = 2) out float vDepth;
// The WORLD position, which the fragment stage needs to look itself up in the
// shadow map. Carried rather than reconstructed: the map's matrix is a
// separate transform and there is no inverse of `mvp` here.
layout(location = 3) out vec3 vWorld;
layout(location = 4) out vec3 vNrm;

void main() {
    vUV  = inUV;
    vCol = inCol;
    vWorld = inPos;
    vNrm = inNrm;
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vDepth = gl_Position.w;
}

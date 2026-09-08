#version 450
// The DEPTH-ONLY pass of the mapped shadow (`todo/enhancements.md` 6).
//
// There is no fragment stage: the pass has a depth attachment and nothing
// else, so the pipeline writes gl_FragDepth implicitly and outputs no colour.
// The vertex format is the scene's, and only the position is read.
layout(push_constant) uniform Push { mat4 lightMvp; } pc;

layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = pc.lightMvp * vec4(inPos, 1.0);
}

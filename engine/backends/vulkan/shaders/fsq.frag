#version 450
// Writes no colour - the pipeline masks it off. It exists because a graphics
// pipeline needs a fragment stage in order to write depth.
layout(location = 0) out vec4 o;
void main() { o = vec4(0.0); }

#version 450
// A full-screen triangle with no vertex input - three vertices covering the
// viewport. Used only to RESET DEPTH inside the mirror's stencil area, which
// is the step that lets the reflection be drawn after the room without the
// room's own depth occluding it.
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    // z = 1.0: the far plane, which is what "nothing drawn here yet" means to
    // a LESS depth test.
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}

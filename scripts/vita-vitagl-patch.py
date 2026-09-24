#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""OMK's two patches to the pinned vitaGL.

    vita-vitagl-patch.py source/custom_shaders.c [source/vgl.c]

1. A CACHED SHADER NEEDS NO COMPILER (custom_shaders.c).

vitaGL's HAVE_SHADER_CACHE keeps each compiled shader as `<xxh3 of the
source>.gxp`, but `glCompileShader` starts Sony's runtime compiler
(`libshacccg.suprx`, which only comes extracted from a console) BEFORE it looks
in the cache, and gives up when it is missing - so a full cache still needed
the compiler. Patched: the start is not fatal, and only a cache MISS with no
compiler refuses, in `glCompileShader` and in `glLinkProgram`'s postponed
compile.

2. MEMORY LEFT FOR THE FILM DECODER (vgl.c). SDL's GL context calls
`vglInitExtended`, which passes a CDRAM and a PHYCONT threshold of 0 - vitaGL
takes EVERY byte of both at start-up. The console's log then says `free: main
11264 KB, CDRAM 0 KB, PHYCONT 0 KB` when the films open, and SceAvPlayer, whose
hardware decoder needs memory of its own beyond the frame buffers it asks the
game for, STOPS three times and never gets READY (todo/handoff-vita-port.md
3b). Patched: `vglInitExtended` leaves 16 MB of each to the system.

Every anchor is asserted; run by scripts/vita-vitagl.sh on a fresh checkout of
the pinned commit.
"""
import sys


def patch_memory(path):
    s = open(path).read()
    if "OMK: memory left for the film decoder" in s:
        return
    a = """	return vglInitWithCustomThreshold(pool_size, width, height, ram_threshold, 0, 0, SCE_KERNEL_MAX_MAIN_CDIALOG_MEM_SIZE, msaa);
"""
    assert s.count(a) == 1, "vglInitExtended's thresholds moved"
    s = s.replace(a, """	// OMK: memory left for the film decoder - 16 MB of CDRAM and of PHYCONT
	return vglInitWithCustomThreshold(pool_size, width, height, ram_threshold, 16 * 1024 * 1024, 16 * 1024 * 1024, SCE_KERNEL_MAX_MAIN_CDIALOG_MEM_SIZE, msaa);
""")
    open(path, "w").write(s)
    print("patched", path)


if len(sys.argv) > 2:
    patch_memory(sys.argv[2])

path = sys.argv[1]
s = open(path).read()
if "OMK: a cached shader needs no compiler" in s:
    sys.exit(0)

a = """	if (!is_shark_online && !start_shader_compiler()) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	}
	
	// Grabbing passed shader
	shader *s = &shaders[handle - 1];
"""
assert s.count(a) == 1, "glCompileShader's compiler start moved"
s = s.replace(a, """	// OMK: a cached shader needs no compiler - the start is not fatal, a MISS is
	if (!is_shark_online)
		start_shader_compiler();
	
	// Grabbing passed shader
	shader *s = &shaders[handle - 1];
""")

b = """	if (s->is_glsl) {
		glsl_translator_process(s);
	}
	vgl_compile_shader(s, GL_FALSE);
}
"""
assert s.count(b) == 1, "glCompileShader's compile tail moved"
s = s.replace(b, """	if (!is_shark_online) { // OMK: a miss, and nothing to compile it with
		SET_GL_ERROR(GL_INVALID_OPERATION)
	}
	if (s->is_glsl) {
		glsl_translator_process(s);
	}
	vgl_compile_shader(s, GL_FALSE);
}
""")

c = """		if (!p->vshader->prog || !p->fshader->prog) {
			if (p->vshader->is_glsl || p->fshader->is_glsl) {
				glsl_translator_set_process(p->vshader, p->fshader);
			}
"""
assert s.count(c) == 1, "glLinkProgram's postponed compile moved"
s = s.replace(c, """		if (!p->vshader->prog || !p->fshader->prog) {
			if (!is_shark_online && !start_shader_compiler()) { // OMK: a miss, no compiler
				glsl_sema_mode = VGL_MODE_POSTPONED;
				SET_GL_ERROR(GL_INVALID_OPERATION)
			}
			if (p->vshader->is_glsl || p->fshader->is_glsl) {
				glsl_translator_set_process(p->vshader, p->fshader);
			}
""")
open(path, "w").write(s)
print("patched", path)

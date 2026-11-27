# SPDX-License-Identifier: GPL-3.0-or-later
"""THE DEKO3D BACKEND'S SHADERS - Vulkan's GLSL, rewritten for `uam`.

`todo/deko3d.md` D0. The deko3d backend draws with the SAME shaders as the
Vulkan one (`backends/vulkan/shaders/`), so a fix to either reaches both
(`fix-all-backends`). `uam` (devkitPro's deko3d shader compiler, Mesa's GLSL
front end) takes GLSL for OpenGL, not for Vulkan, and differs from glslc in
five places - each a rewrite below, each ASSERTED to have applied the number
of times the source says it must (a rewrite that does not apply passes
silently otherwise - CLAUDE.md 1):

  1. `layout(push_constant) uniform X` - deko3d has no push constants; its
     `dkCmdBufPushConstants` writes a UNIFORM BUFFER inline in the command
     list, which is the same thing. So the block becomes
     `layout(std140, binding = 0) uniform X`. Vulkan lays a push block out by
     std430; every block here holds scalars, vec2/3/4, uvec4 and mat4 only,
     where std140 and std430 agree - an ARRAY or a nested struct is where they
     part, and is refused.
  2. `set = S, binding = B` - deko3d binds per STAGE and per KIND (16 uniform
     buffers, 16 storage buffers, 32 textures), with no sets. Flattened to
     `binding = S * 6 + B`; uniform-buffer binding 0 is the push block, so a
     set-0 uniform block is refused.
  3. `gl_VertexIndex` / `gl_InstanceIndex` are GL's `gl_VertexID` /
     `gl_InstanceID`.
  4. `#include "x"` (GL_GOOGLE_include_directive) - `uam` has no include
     path; the file is inlined, from the includes directories given.
  5. `-DNAME` - `uam` has no -D; the defines go after `#version`.

    python3 dkshaders.py <in.glsl> <out.glsl> [-I dir]... [-D NAME]...

Writes GLSL only, and never into the shipped data (`gamedata/` - CLAUDE.md 1).
"""
import argparse
import os
import re
import sys

STRIDE = 6   # bindings per set once flattened: set 2 binding 3 -> 15, under 16


def fail(path, why):
    sys.exit(f"dkshaders: {path}: {why}")


def inline_includes(src, path, dirs, seen):
    out = []
    for line in src.splitlines(keepends=True):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
        if not m:
            if re.match(r'\s*#\s*extension\s+GL_GOOGLE_include_directive', line):
                continue   # the directive's enable line goes with the includes
            out.append(line)
            continue
        name = m.group(1)
        for d in [os.path.dirname(path)] + dirs:
            cand = os.path.join(d, name)
            if os.path.isfile(cand):
                break
        else:
            fail(path, f'#include "{name}" not found in {dirs}')
        if cand in seen:
            fail(path, f"{name} included twice")
        seen.add(cand)
        with open(cand, encoding="utf-8") as f:
            body = f.read()
        out.append(f"// ---- {name} (inlined by dkshaders.py)\n")
        out.append(inline_includes(body, cand, dirs, seen))
        out.append(f"\n// ---- end of {name}\n")
    return "".join(out)


def check_push_block(src, path):
    """The push block's members: std140 must lay it out as std430 did."""
    m = re.search(r"layout\s*\(\s*push_constant\s*\)\s*uniform\s+\w+\s*\{(.*?)\}", src, re.S)
    if not m:
        return 0
    body = re.sub(r"//[^\n]*", "", m.group(1))
    for decl in filter(None, (d.strip() for d in body.split(";"))):
        words = decl.replace(",", " ").split()
        if words[0] not in ("int", "uint", "float", "vec2", "vec3", "vec4", "ivec2", "ivec4",
                            "uvec2", "uvec4", "mat4"):
            fail(path, f"push member '{decl}': only scalars, vectors and mat4 lay out alike")
        if "[" in decl:
            fail(path, f"push member '{decl}': an array differs between std140 and std430")
    return 1


def translate(src, path, dirs, defines):
    src = inline_includes(src, path, dirs, set())
    pushes = check_push_block(src, path)

    # 1. the push block -> uniform buffer 0
    src, n = re.subn(r"layout\s*\(\s*push_constant\s*\)\s*uniform",
                     "layout(std140, binding = 0) uniform", src)
    if n != pushes or n > 1:
        fail(path, f"{n} push blocks rewritten, {pushes} found (one at most)")

    # 2. the sets flattened; a set-0 UNIFORM BLOCK would take the push's slot
    for m in re.finditer(r"layout\s*\(([^)]*)\)\s*uniform\s+(\w+)\s*\{", src):
        q = m.group(1)
        sm = re.search(r"set\s*=\s*(\d+)", q)
        if sm and sm.group(1) == "0" and "push" not in q:
            fail(path, f"uniform block {m.group(2)} in set 0: binding 0 is the push block's")
    expected = len(re.findall(r"set\s*=\s*\d+\s*,\s*binding\s*=\s*\d+", src))
    src, n = re.subn(r"set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)",
                     lambda m: f"binding = {int(m.group(1)) * STRIDE + int(m.group(2))}", src)
    if n != expected:
        fail(path, f"{n} of {expected} set/binding pairs rewritten")
    for m in re.finditer(r"binding\s*=\s*(\d+)", src):
        if int(m.group(1)) >= 16 and "sampler" not in src[m.end():m.end() + 80]:
            fail(path, f"binding {m.group(1)} past deko3d's 16 buffers")
    if re.search(r"\bset\s*=", src):
        fail(path, "a `set =` left over, not followed by `, binding =`")

    # 3. the vertex and instance index
    src = re.sub(r"\bgl_VertexIndex\b", "gl_VertexID", src)
    src = re.sub(r"\bgl_InstanceIndex\b", "gl_InstanceID", src)

    # 5. the defines, after `#version`
    lines = src.splitlines(keepends=True)
    if not lines or not lines[0].startswith("#version"):
        fail(path, "the first line is not #version")
    head = "".join(f"#define {d} 1\n" for d in defines)
    return lines[0] + "// generated by backends/deko3d/dkshaders.py - do not edit\n" + head + "".join(lines[1:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("-I", dest="dirs", action="append", default=[])
    ap.add_argument("-D", dest="defines", action="append", default=[])
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    if not out.endswith(".glsl"):
        fail(a.out, "the output must be a .glsl")
    if "/gamedata/" in out.replace(os.sep, "/") + "/":
        fail(a.out, "refused: inside the shipped data")
    with open(a.src, encoding="utf-8") as f:
        text = translate(f.read(), a.src, a.dirs, a.defines)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)


if __name__ == "__main__":
    main()

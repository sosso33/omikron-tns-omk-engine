# SPDX-License-Identifier: GPL-3.0-or-later
NEW2 = {}

NEW2["00411EF0"] = r'''
/* malloc with the engine's allocation accounting, and no failure path.
 *
 * Every game allocation goes through here. On success it updates the running
 * byte total, the live-allocation count and the high-water mark. On failure it
 * puts up a retry dialog and loops, so this never returns null - callers rely
 * on that and none of them check.
 */
void *__cdecl Mem_Alloc(size_t size)
{
    void   *block;
    size_t  actual;
    HWND    owner;

    for (;;)
    {
        block = malloc(size);
        if (block)
        {
            actual = msize(block);
            if (g_MemTraceEnabled)
                Dbg_Printf("alloue %d octets\n", actual);

            g_MemBytesAllocated += actual;
            ++g_MemAllocCount;
            if (g_MemBytesAllocated > g_MemPeakBytes)
                g_MemPeakBytes = g_MemBytesAllocated;

            return block;
        }

        /* Out of memory: ask the player to free some and try again. */
        sub_4338D0();
        owner = GetActiveWindow();
        if (MessageBoxA(owner, aPlusDeMemoire, lpCaption, 0x35u) != IDRETRY)
            break;
    }
    return block;
}
'''

NEW2["00412060"] = r'''
/* free() with the matching accounting. Null is accepted and ignored.
 *
 * msize() is read before the block is released, which is why the two updates
 * are not folded together. */
void __cdecl Mem_Free(void *block)
{
    size_t size;

    if (!block)
        return;

    if (g_MemTraceEnabled)
    {
        size = msize(block);
        Dbg_Printf("libere %d octets\n", size);
    }

    g_MemBytesAllocated -= msize(block);
    --g_MemAllocCount;
    free(block);
}
'''

NEW2["00412100"] = r'''
/* Milliseconds since the engine's time base, or the frozen value while paused.
 *
 * g_TimePaused is a flag, not a timestamp: when set, g_TimeBaseMs is returned
 * verbatim, so callers see a clock that has stopped. */
DWORD Sys_GetTimeMs(void)
{
    if (g_TimePaused)
        return g_TimeBaseMs;

    return timeGetTime() - g_TimeBaseMs;
}
'''

NEW2["0041E040"] = r'''
/* Put a line of subtitle text on screen and work out where to draw it.
 *
 * The text is laid out across the full screen width, inset 16 pixels each side,
 * and parked against the bottom. Text_DrawBlock reports the height the text
 * needs, so the y it should sit at is (screen height - text height - 16).
 *
 * It also starts a display timer: roughly 80ms per character, floored at two
 * seconds, so short lines still stay up long enough to read.
 */
int __cdecl Subtitle_Show(const char *text)
{
    signed int durationMs;
    int        params[16];

    g_SubtitleText = (int)text;

    durationMs = 80 * strlen(text);
    if (durationMs < 2000)
        durationMs = 2000;
    g_SubtitleExpiryMs = durationMs + Sys_GetTimeMs();

    /* params[0] is the "which fields are present" mask: 0x20 says params[2] is
     * meaningful, 0x40 is a mode flag Text_DrawBlock forwards to the renderer.
     * The rest of the array is left uninitialised because the mask does not
     * claim it. */
    params[0] = 0x20 | 0x40;
    params[2] = 86;

    g_SubtitleY = SCREEN_H
                - Text_DrawBlock(16, 0, SCREEN_W - 16, SCREEN_H,
                                 (uint8_t *)g_SubtitleText, params)
                - 16;
    return 1;
}
'''

NEW2["00441EB0"] = r'''
/* Build a 3x3 rotation matrix from three Euler angles, in radians.
 *
 * Written row-major into `m` as nine consecutive floats:
 *
 *      m[0] m[1] m[2]        +0  +4  +8
 *      m[3] m[4] m[5]       +12 +16 +20
 *      m[6] m[7] m[8]       +24 +28 +32
 *
 * The terms below are exactly what the original computes; naming the angles
 * ax/ay/az follows the order Camera_ResetForActor passes them in (the camera's
 * +416, +420, +424). Which axis each one physically turns about is not settled
 * by anything in this closure, so the names claim ordering only.
 *
 * The original hoisted every sin/cos into its own temporary because the x87
 * stack is only eight deep; expressing it as a matrix costs nothing here.
 */
int __cdecl Matrix3x3_FromEulerAngles(float ax, float ay, float az, int m)
{
    float sx = sin(ax), cx = cos(ax);
    float sy = sin(ay), cy = cos(ay);
    float sz = sin(az), cz = cos(az);

    f32(m,  0) = cz * cy;
    f32(m,  4) = -(sz * cy);
    f32(m,  8) = sy;

    f32(m, 12) = sy * sx * cz + sz * cx;
    f32(m, 16) = cz * cx - sx * sz * sy;
    f32(m, 20) = -(sx * cy);

    f32(m, 24) = sz * sx - sy * cz * cx;
    f32(m, 28) = cx * sz * sy + sx * cz;
    f32(m, 32) = cy * cx;

    return m;
}
'''

NEW2["0041B280"] = r'''
/* Point the camera at an actor and reset the per-shot camera state.
 *
 * Called at the end of Dialog_Load with the speaking actor. What it does,
 * in order:
 *   - resolve actorIndex, falling back to sub_457030() (dword_4C8898) for -1,
 *   - hand the camera to sub_468DE0, which resets its render state,
 *   - clear a block of camera fields, including the first Euler angle, so the
 *     shot starts square-on while keeping the other two angles,
 *   - rebuild the camera's rotation matrix from those angles,
 *   - flag the actor's node so it is included in the shot,
 *   - remember the actor and mode for whoever runs the camera next.
 *
 * `mode` is only stored; nothing in this closure reads it back.
 *
 * The actor table is a flat array of 328-byte records at unk_9106A0; the entry
 * at +12 is the node sub_4372D0 flags. Only the stride is certain here.
 */
int __cdecl Camera_ResetForActor(int actorIndex, int mode)
{
    int       actor;
    int       camera;
    uint32_t *actorNode;
    float     radX, radY, radZ;

    actor = (actorIndex == -1) ? sub_457030() : actorIndex;

    camera = LODWORD(g_Camera);          /* g_Camera is a pointer the
                                          * decompiler mistyped as float */
    sub_468DE0((int *)camera);

    u32(camera, CAM_ANGLE_X) = 0;        /* square up; Y and Z are kept */
    u32(camera, 216) = 0;
    u32(camera, 220) = 0;
    u32(camera, 224) = 0;
    u8 (camera, 1304) = 0;
    u32(camera, 280) = 0;
    u32(camera, 284) = 0;
    dword_6A52CC = 0;

    /* 0.0174532925199433 is pi/180: the angles are stored in degrees. */
    radZ = f32(camera, CAM_ANGLE_Z) * 0.0174532925199433;
    radX = f32(camera, CAM_ANGLE_X) * 0.0174532925199433;
    radY = f32(camera, CAM_ANGLE_Y) * 0.0174532925199433;
    Matrix3x3_FromEulerAngles(radX, radY, radZ, camera + CAM_ROTMATRIX);

    sub_440C80(*(uint32_t **)(camera + 8));

    actorNode = (uint32_t *)u32i(&unk_9106A0 + 328 * actor, 3);
    if (actorNode)
        sub_4372D0(actorNode, 1, 8);

    dword_4C2BBC = actor;
    dword_4C2BC0 = mode;
    dword_9103DC = 1;
    return 1;
}
'''

NEW2["004372D0"] = r'''
/* Translate a compact 11-bit flag word into the engine's 32-bit node flags and
 * either clear them on one node or apply them down a subtree.
 *
 * The original is eleven consecutive `if` statements, several of them written
 * through BYTE1()/LOBYTE() so the constants are not obvious; the table makes
 * the mapping readable and is a straight transcription of them.
 *
 * op 4 clears the bits on this node only; op 8 walks the subtree via
 * sub_48D080, applying them through the sub_437370 callback. Any other op does
 * nothing - and still returns 1, like the original.
 */
int __cdecl sub_4372D0(uint32_t *node, int16_t flags, int op)
{
    /* bit n of `flags` -> this mask in the node's flag word */
    static const uint32_t kFlagMap[11] = {
        0x00000080u,  /* 0x001 */
        0x00000800u,  /* 0x002  was BYTE1(v) |= 0x08 */
        0x00001000u,  /* 0x004  was BYTE1(v) |= 0x10 */
        0x00002000u,  /* 0x008  was BYTE1(v) |= 0x20 */
        0x00004000u,  /* 0x010  was BYTE1(v) |= 0x40 */
        0x00008000u,  /* 0x020  was BYTE1(v) |= 0x80 */
        0x00010000u,  /* 0x040 */
        0x00020000u,  /* 0x080 */
        0x08000000u,  /* 0x100 */
        0x00000008u,  /* 0x200  was LOBYTE(v) |= 0x08 */
        0x04000000u,  /* 0x400 */
    };
    uint32_t mask = 0;
    int      i;

    for (i = 0; i < 11; i++)
    {
        if (flags & (1 << i))
            mask |= kFlagMap[i];
    }

    if (op == 4)
        u32(*node, 0) &= ~mask;
    else if (op == 8)
        sub_48D080(node, mask, (void(__cdecl *)(uint32_t *, int))sub_437370);

    return 1;
}
'''

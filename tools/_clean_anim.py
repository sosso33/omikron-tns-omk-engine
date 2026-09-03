# SPDX-License-Identifier: GPL-3.0-or-later
NEW = {}

NEW["00434010"] = r'''
/* Clear the runtime state of every clip in the loaded library.
 *
 * Not a function in the binary - the original inlines this loop at both of
 * Anim_Load's exits, verbatim. It is factored out here because it is the same
 * eight lines twice; nothing else calls it. */
static void anim_release_clips(void)
{
    int *group;
    int  n, clip;
    uint32_t *slot;

    if (g_AnimGroupCount <= 0) return;
    group = (int *)(g_AnimGroups + 4);
    for (n = g_AnimGroupCount; n; --n, group += 6)
    {
        for (clip = *group; clip; clip = u32(clip, 24))
        {
            slot = *(uint32_t **)(clip + 8);
            slot[0] = 0; slot[1] = 0; slot[2] = 0; slot[3] = 0;
        }
    }
}

/* Load a body-animation library from ANIMS\ and relocate it in place.
 *
 * The file (magic "3.0V") is read whole and its internal offsets are turned
 * into pointers where it sits, so the layout in memory is the layout on disk.
 * See docs/ASSETS.md section 7:
 *
 *   +8   group[groupCount], 24 bytes; +4 is the offset of its first clip
 *   clip node, 36 bytes: +8 descriptor offset, +24 next node, +28 name[8]
 *
 * Each clip's descriptor is handed to Anim_RegisterClip, which copies it into
 * one of 512 global slots and returns the slot - so +8 stops being a file
 * offset and becomes that pointer.
 *
 * Any previously loaded library is released first, and its clips' runtime
 * state cleared before the memory goes.
 *
 * Returns 1 on success, 0 if the file is missing or not a "3.0V" library.
 */
int __cdecl Anim_Load(const char *name)
{
    uint32_t *file;
    char     *node;
    char     *next;
    int       groups, table, i, off, head;
    char      path[260];

    if (g_AnimLibrary)
    {
        anim_release_clips();
        Mem_Free(g_AnimLibrary);
        g_AnimLibrary = 0;
    }

    sprintf(path, "ANIMS\\%s", name);
    file = sub_411940(path, 0, 1);
    g_AnimLibrary = file;
    if (!file || *file != ANIM_MAGIC_30V)        /* "3.0V" */
    {
        if (file)
        {
            anim_release_clips();
            Mem_Free(file);
            g_AnimLibrary = 0;
        }
        return 0;
    }

    groups = file[1];
    table  = (int)(file + 2);
    g_AnimGroupCount = groups;
    g_AnimGroups     = table;

    for (i = 0, off = 0; i < groups; i++, off += 24)
    {
        head = u32(table + off, 4);
        if (!head) continue;

        u32(table + off, 4) = (char *)file + head;   /* group -> first clip */
        node = (char *)file + head;
        for (;;)
        {
            /* +8: descriptor offset -> the global slot it is copied into */
            u32i(node, 2) = Anim_RegisterClip(
                                (uint32_t *)((char *)file + u32i(node, 2)));
            next = (char *)(uintptr_t)u32i(node, 6);   /* +24: next node */
            file = g_AnimLibrary;
            if (!next) break;
            next = (char *)file + (uintptr_t)next;
            u32i(node, 6) = next;
            node = next;
        }
        groups = g_AnimGroupCount;
        table  = g_AnimGroups;
    }
    return 1;
}
'''

NEW["0042BD90"] = r'''
/* Set the morph player's audio format: sample rate, channels, frame rate.
 *
 * Morph_Play calls this with (0x5640, 1, 30) - 22080 Hz, mono, 30 fps - which
 * is where a .3DM's 368 bytes of ADPCM per frame comes from: 736 samples at
 * 22050 Hz is one thirtieth of a second.
 *
 * Refused while playback is running, above 0xAC44 (44100 Hz), or outside
 * mono/stereo.
 */
int __cdecl Morph_SetAudioFormat(unsigned int rate, unsigned int channels, int fps)
{
    if (g_MorphPlaying || !channels || channels > 2 || rate > 0xAC44)
        return 0;
    g_MorphChannels = channels;
    g_MorphFps      = fps;
    g_MorphRate     = rate;
    return 1;
}
'''

NEW["0042BCA0"] = r'''
/* Prepare the morph player's track tables for a new clip.
 *
 * Two arrays are cleared. The first, from unk_4EA906, is 8-byte entries whose
 * first field is set to -1 for "empty". The second, from unk_4EB140, is the
 * bone-track table: 40 bytes per entry, id set to -1 and the rest zeroed. The
 * per-frame update fills in each entry's node id and its two-key quaternion
 * track, so this is what makes an unused slot recognisable.
 *
 * `node` and `mesh` are the scene node and the mesh whose vertices the morph
 * will replace; the three coordinates are the position the animation plays at.
 */
int __cdecl Morph_ResetTracks(int node, int mesh, int x, int y, int z)
{
    char     *p;
    uint32_t *t;

    if (g_MorphPlaying)
        return 0;

    g_MorphNode      = node;
    g_MorphMesh      = mesh;
    g_MorphRootTrack = -2;
    g_MorphLastFrame = -1082130432;              /* -1.0f */

    for (p = (char *)&unk_4EA906; (int)p < (int)&unk_4EB106; p += 8)
    {
        u16i(p, -1) = -1;
        u16(p, 0)   = 0;
        u32(p, 2)   = 0;
    }

    g_MorphTrackA = 0;
    g_MorphTrackB = 0;
    g_MorphTracks = (int)&unk_4EB140;
    for (t = &unk_4EB158; (int)t < (int)&dword_4EB608; t += 10)
    {
        *(t - 6) = -1;                            /* entry id: empty */
        t[0] = 0; t[1] = 0; t[2] = 0; t[3] = 0;
    }

    g_MorphOriginX = x;
    g_MorphOriginY = y;
    g_MorphOriginZ = z;
    dword_4EA8A4 = 0;
    dword_4EA8A8 = 0;
    dword_4EA8AC = 0;
    return 1;
}
'''

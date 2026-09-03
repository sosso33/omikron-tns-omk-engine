# SPDX-License-Identifier: GPL-3.0-or-later
NEW = {}

NEW["00401800"] = r'''
/* Load one conversation out of the IAM\DIALOG archive and hand control to the
 * dialogue camera.
 *
 * The archive chunk is a single self-contained block: a small header, an array
 * of DialogNode, an array of DialogCamera, then the dialogue text. Two things
 * have to happen before the rest of the engine can use it - the file-relative
 * offsets inside each node have to become real pointers, and the camera records
 * have to be converted out of the units the authoring tool wrote them in.
 *
 * Named after the string it loads with; see types.h for how the layout and the
 * unit conversions were established (they were checked against gamedata/IAM/DIALOG).
 */
int __cdecl Dialog_Load(int dialogIndex)
{
    uint8_t      *blob;
    DialogNode   *nodes;
    DialogCamera *cameras;
    int           nodeCount, cameraCount;
    int           i, k;

    blob = (uint8_t *)Archive_ReadChunk(aIamDialog, dialogIndex, 0, 0);
    g_DialogBlob = (int)blob;

    /* Header word 0 is the id of the object doing the talking, scene-local;
     * resolve it once here so the accessors can work with a plain index.
     * dword_69BC48[4 * dword_69BC60] is the currently loaded scene. */
    g_DialogSpeakerObj = Scene_FindObjectIndexById(i16(blob, 0),
                                                   dword_69BC48[4 * dword_69BC60]);

    nodeCount   = i16(blob, 2);
    cameraCount = i16(blob, 4);

    nodes   = (DialogNode *)(blob + 8);
    cameras = (DialogCamera *)(nodes + nodeCount);

    g_DialogNodeCount   = nodeCount;
    g_DialogCameraCount = cameraCount;
    g_DialogNodes       = (int)nodes;
    g_DialogCameras     = (int)cameras;

    /* Relocation. On disk every pointer in a node is an offset from the start
     * of the block, with 0 meaning "none"; turn the non-zero ones into real
     * pointers. The original unrolled all nine slots by hand. */
    for (i = 0; i < nodeCount; i++)
    {
        for (k = 0; k < 9; k++)
        {
            if (nodes[i].ptr[k])
                nodes[i].ptr[k] = (char *)(blob + (uintptr_t)nodes[i].ptr[k]);
        }
    }

    /* Unit conversion, done once at load so nothing downstream sees file units.
     * See DIALOG_POS_SCALE / DIALOG_ANGLE_SCALE in types.h for the derivation.
     * The `100 *` is deliberately left in 32-bit integer arithmetic, matching
     * the `lea/lea/shl` the compiler emitted - it can overflow, and did in the
     * original too. */
    for (i = 0; i < cameraCount; i++)
    {
        for (k = 0; k < 6; k++)
        {
            int32_t scaled = (int32_t)(100u * (uint32_t)cameras[i].pos[k]);
            cameras[i].pos[k] = (int32_t)((double)scaled * DIALOG_POS_SCALE
                                          - DIALOG_POS_BIAS);
        }
        cameras[i].angle[0] = (int16_t)(int32_t)((double)cameras[i].angle[0]
                                                 * DIALOG_ANGLE_SCALE);
        cameras[i].angle[1] = (int16_t)(int32_t)((double)cameras[i].angle[1]
                                                 * DIALOG_ANGLE_SCALE);
    }

    g_DialogIndex = dialogIndex;
    g_DialogState = 3;

    /* g_DialogEmptyText is {0,0,0,0}, so this clears any subtitle still up. */
    Subtitle_Show(g_DialogEmptyText);

    return Camera_ResetForActor(g_DialogSpeakerObj, 0);
}
'''

NEW["0040FF90"] = r'''
/* Read one chunk out of one of the game's flat archives.
 *
 * An archive is a directory interleaved with its payload. Entries are grouped
 * 256 at a time; group g starts at byte g * 2048 and holds 256 pairs of
 * (offset, size), which is exactly 2048 bytes. So chunkIndex splits into
 * (chunkIndex >> 8) to pick the group and the low byte to pick the slot.
 *
 * Confirmed by parsing gamedata/IAM/DIALOG with exactly this rule: every chunk it
 * yields starts with a well-formed dialogue header.
 *
 * When explicitSize > 0 the caller already knows where the data is and the
 * directory is skipped entirely; offsetOrIndex is then a chunk stride.
 *
 * The returned block is owned by the caller. Note the file-scope `Stream`:
 * this is not reentrant, which is why the whole read happens in one go.
 */
void *__cdecl Archive_ReadChunk(char *path, int offsetOrIndex, int explicitOffset,
                                int explicitSize)
{
    void *directory;
    void *block;
    int   dataOffset;
    int   dataSize;

    directory = 0;
    Stream = Res_OpenFile(path, 1, 0);

    if (explicitSize > 0)
    {
        /* Caller supplied the layout: chunk n of a fixed-size run. */
        dataOffset = offsetOrIndex * explicitSize;
        dataSize   = explicitOffset;
    }
    else
    {
        /* Read the 2048-byte directory group this index falls in, then take
         * the (offset, size) pair at the slot the low byte selects. */
        fseek(Stream, (unsigned int)offsetOrIndex >> 8 << 11, SEEK_SET);
        directory = Mem_Alloc(0x800u);
        fread(directory, 1u, 0x800u, Stream);

        dataOffset = u32i(directory, 2 * (uint8_t)offsetOrIndex);
        dataSize   = u32i(directory + 2 * (uint8_t)offsetOrIndex, 1);
    }

    if (directory)
        Mem_Free(directory);

    fseek(Stream, dataOffset, SEEK_SET);
    block = Mem_Alloc(dataSize);
    fread(block, 1u, dataSize, Stream);
    fclose(Stream);

    Stream = 0;
    return block;
}
'''

NEW["004128F0"] = r'''
/* Open a game file, resolving it against the install directory.
 *
 * A path containing ':' is already absolute (a drive letter - this is a 1999
 * Windows game), so it is used as-is; anything else is appended to `Source`,
 * the install root. `Mode` is the file-scope string "rb".
 *
 * `unused` is passed 1 by every caller in this closure and never read.
 * `resolvedPathOut`, when non-null, receives the path that was actually opened.
 */
FILE *__cdecl Res_OpenFile(char *path, int unused, int resolvedPathOut)
{
    FILE *fp;
    char  fullPath[260];

    fullPath[0] = 0;
    if (!strchr(path, ':'))
        strcpy(fullPath, &Source);
    strcat(fullPath, path);

    fp = fopen(fullPath, Mode);
    if (fp && resolvedPathOut)
        strcpy((char *)resolvedPathOut, fullPath);

    return fp;
}
'''

NEW["0040D760"] = r'''
/* Resolve a scene-local object id to the index the engine addresses it by.
 *
 * Three places are searched, in order, and the first hit wins:
 *   1. the object table of the scene `sceneId` names,
 *   2. the object table of that scene's parent, found by indirecting through
 *      dword_4E6D94 (a per-scene lookup keyed by scene id),
 *   3. a flat fallback table, word_69BC80 .. unk_69BD48, whose index is the
 *      answer rather than a stored value.
 *
 * Both object tables are arrays of 20-byte records - the original walks them
 * with `int16 *` in steps of 10 - holding the index at +0 and the id at +2.
 *
 * Returns -1 when the id is unknown. The scene lookups pick between two
 * candidate slots by comparing against dword_69BC48[0] / dword_69BC58; that
 * pattern is a two-entry scene cache, but nothing here proves which is which,
 * so the names below stay descriptive rather than committal.
 */
int __cdecl Scene_FindObjectIndexById(int objectId, int sceneId)
{
    int16_t *entry;
    int      scene;
    int      parentSceneId;
    int      parentScene;
    int      i;
    int      count;

    if (sceneId != -1)
    {
        /* --- 1. the scene's own object table, at scene + 40 --- */
        if (sceneId == dword_69BC48[0])
            scene = dword_69BC40[0];
        else
            scene = (sceneId != dword_69BC58) ? 0 : dword_69BC50;

        entry = *(int16_t **)(scene + 40);
        count = i16(scene, 72);
        for (i = 0; i < count; i++, entry += 10)
        {
            if (entry[1] == objectId)
                return entry[0];
        }

        /* --- 2. the parent scene's object table, at parent + 8 --- */
        parentSceneId = i16(u32(dword_4E6D94, 12), 2 * sceneId);
        if (parentSceneId != -1)
        {
            if (parentSceneId == dword_69BC4C[0])
                parentScene = dword_69BC44[0];
            else
                parentScene = (parentSceneId != dword_69BC5C) ? 0 : dword_69BC54;

            entry = *(int16_t **)(parentScene + 8);
            count = i16(parentScene, 40);
            for (i = 0; i < count; i++, entry += 10)
            {
                if (entry[1] == objectId)
                    return entry[0];
            }
        }
    }

    /* --- 3. the global fallback table: position in it is the index --- */
    if (objectId == -1)
        return -1;

    for (i = 0, entry = word_69BC80; ; i++, entry++)
    {
        if ((int)entry >= (int)&unk_69BD48)
            return -1;
        if (*entry == objectId)
            return i;
    }
}
'''

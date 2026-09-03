# SPDX-License-Identifier: GPL-3.0-or-later
NEW3 = {}

NEW3["0043F180"] = r'''
/* Lay out and draw a block of text, returning the height it occupied.
 *
 * Callers use the return value two ways: 0x0041E040 measures once to place a
 * subtitle against the bottom of the screen, and 0x0042xxxx measures then draws
 * again lower down. So this both draws and reports.
 *
 * Almost all of the body is unpacking `params`, an optional block whose first
 * int is a mask saying which slots are present (see TEXTP_* in types.h). Each
 * present slot is copied into one of the renderer's globals; sub_43F3E0 then
 * does the actual work. The globals keep their address names because nothing
 * here shows what they mean.
 */
int __cdecl Text_DrawBlock(int left, int top, int right, int bottom,
                           uint8_t *text, int *params)
{
    unsigned int style;
    int          flagA;
    int          flagB;
    int          mask;

    /* Defaults, applied whether or not `params` is supplied. */
    byte_9079FC = -1;   byte_9079FD = -1;   byte_9079FE = -1;
    byte_907940 = -1;   byte_907941 = -1;   byte_907942 = -1;

    dword_907A14 = left;
    dword_907A18 = top;
    dword_907A08 = right;
    dword_907A1C = bottom;

    dword_907A10 = 74;
    dword_907A0C = 74;
    dword_9079F4 = 0;
    dword_9079F8 = 0;
    dword_907948 = 11;
    dword_907A00 = 2;
    dword_907A04 = 2;
    dword_907A24 = -1;
    dword_907A28 = 0;
    dword_907A2C = 0;

    flagA = 0;
    flagB = 0;
    style = 2;

    if (params)
    {
        mask = params[0];

        if (mask & TEXTP_COLOUR)
        {
            /* Three colour bytes at +4,+5,+6, stored in reverse order. */
            byte_9079FE = u8i(params, 4);
            byte_9079FD = u8i(params, 5);
            byte_9079FC = u8i(params, 6);
        }

        /* Exactly one of the four alignment bits selects a value; if none is
         * set the global keeps its default rather than being rewritten. */
        if      (mask & TEXTP_ALIGN_2)  style = 2;
        else if (mask & TEXTP_ALIGN_4)  style = 4;
        else if (mask & TEXTP_ALIGN_16) style = 16;
        else if (mask & TEXTP_ALIGN_8)  style = 8;
        if (mask & TEXTP_ALIGN_MASK)
            dword_907A00 = style;

        /* The style bits replace each other rather than combining, so the
         * existing ones are cleared before the new one goes in. */
        if      (mask & TEXTP_STYLE_400)  style = (style & ~TEXTP_STYLE_MASK) | TEXTP_STYLE_400;
        else if (mask & TEXTP_STYLE_800)  style = (style & ~TEXTP_STYLE_MASK) | TEXTP_STYLE_800;
        else if (mask & TEXTP_STYLE_1000) style = (style & ~TEXTP_STYLE_MASK) | TEXTP_STYLE_1000;
        if (mask & TEXTP_STYLE_MASK)
            dword_907A00 = style;

        if (mask & TEXTP_SLOT10)  dword_907948 = params[10];
        if (mask & TEXTP_SLOT2)   dword_907A10 = params[2];
        if (mask & TEXTP_FLAG_A)  flagA = 1;

        if (mask & TEXTP_ORIGIN)
        {
            dword_9079F4 = params[3];
            dword_9079F8 = params[4];
        }

        if (mask & TEXTP_EXTENDED)
        {
            dword_907A24 = params[11];
            byte_907941  = u8i(params, 48);
            byte_907942  = u8i(params, 49);
            byte_907940  = u8i(params, 50);
            dword_907A0C = params[13];
            dword_907A04 = params[14];
            /* 0x1E is the same four-bit alignment group as above; if the
             * caller cleared all of them, fall back to 2. */
            if ((dword_907A04 & TEXTP_ALIGN_MASK) == 0)
                dword_907A04 |= 2u;
            dword_907A2C = params[15];
        }

        if (mask & TEXTP_SLOT5)      dword_907A28 = params[5];
        if (mask & TEXTP_STYLE_4000) dword_907A00 = style | TEXTP_STYLE_4000;
        if (mask & TEXTP_FLAG_B)     flagB = 1;
    }

    /* Below 640x480 the two size fields step up from 74 to 76. */
    if (SCREEN_W < 640 || SCREEN_H < 480)
    {
        dword_907A10 = 76;
        dword_907A0C = 76;
    }

    return sub_43F3E0(text, flagA, flagB);
}
'''

NEW3["00440C80"] = r'''
/* Walk a node and its children, applying sub_4942A0 to each.
 *
 * sub_48CFE0 is the engine's generic subtree traversal: (depth, root, callback).
 * Camera_ResetForActor calls this on the camera's child node at +8 after
 * rebuilding the rotation matrix, so sub_4942A0 is very likely the "recompute
 * my world transform from my parent's" step - but only the traversal shape is
 * actually visible from here, so the name says only that.
 */
int __cdecl sub_440C80(uint32_t *root)
{
    return sub_48CFE0(0, root, (void(__cdecl *)(int, uint32_t *))sub_4942A0);
}
'''

NEW3["00457030"] = r'''
/* The actor index used when a caller passes -1 for "whoever is current".
 *
 * dword_4C8898 is initialised to -1 and set elsewhere; Camera_ResetForActor is
 * the only caller in this closure, using it as the fallback subject.
 */
int sub_457030(void)
{
    return dword_4C8898;
}
'''

NEW3["00468DE0"] = r'''
/* Reset the camera's render state before a new shot.
 *
 * Called first thing by Camera_ResetForActor. `cam` is the camera viewed as an
 * int array; the slots it touches are cam[99], cam[45], cam[101] and cam[102]
 * (byte offsets 396, 180, 404, 408).
 *
 * cam[101] looks like a state enum being advanced: 9 becomes 17, anything else
 * is saved into cam[102] and replaced by 16. What those numbers mean is not
 * visible from here, so nothing is renamed beyond the parameter.
 *
 * The original reads `v1` before assigning it - the decompiler flagged it, and
 * the call really does pass whatever happens to be in that register. Kept
 * as-is because changing it would change behaviour.
 */
int __cdecl sub_468DE0(int *cam)
{
    void     *uninitialised;
    int       surface;
    uint32_t *buffer;
    int       state;

    dword_90EF8C = 10;
    sub_43E4F0(uninitialised);            /* reads an uninitialised value */

    surface = sub_45ACB0(cam[99]);
    dword_53AE28 = surface;
    sub_45A3E0(cam[99], 0);

    buffer = sub_46ACE0(cam[45], 400);
    sub_45A630(cam[99], (int)buffer);

    state = cam[101];
    if (state == 9)
    {
        cam[101] = 17;
    }
    else
    {
        cam[102] = state;
        cam[101] = 16;
    }

    if (cam[102] == 4)
        return sub_45A470(cam[99], 1);

    return state;
}
'''

NEW3["00437E00"] = r'''
/* Return a pointer to object `index` of `scene`.
 *
 * Objects hang off the scene handle as a flat array of 184-byte records; the
 * count lives at +224 in the descriptor that scene[0] points to.
 *
 * Quirk preserved from the original: when the index is out of range this only
 * *reports* the error - it still falls through and returns the out-of-range
 * pointer. Callers are expected to have checked the count themselves.
 */
void *__cdecl o3de_GetObjectByIndex(uint32_t *scene, int index)
{
    if (index < 0 || scene == NULL)
        return NULL;

    if ((uint32_t)index >= u32(scene[0], O3DE_DESC_OBJECTCOUNT))
        Err_SetMessage(aO3deGetobjectb);   /* "...index too big !" */

    return (void *)(scene[7] + O3DE_OBJECT_SIZE * index);
}
'''

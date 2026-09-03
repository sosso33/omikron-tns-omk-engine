# SPDX-License-Identifier: GPL-3.0-or-later
NEW = {}

_FIND = """/* Every accessor below finds its node the same way: a linear scan of
 * g_DialogNodes for a matching id, yielding 0 when the id is absent - which
 * the callers then happily dereference at offset 0. The original repeated the
 * loop verbatim in each function; it is written once here as Dialog_FindNode
 * and the shape of each accessor is what is left. */"""

NEW["00401000"] = r'''
/* Release the loaded conversation and forget it. */
int Dialog_Unload(void)
{
    Mem_Free(g_DialogBlob);
    g_DialogBlob        = 0;
    g_DialogNodeCount   = 0;
    g_DialogCameras     = 0;
    g_DialogCameraCount = 0;
    g_DialogSpeakerObj  = -1;
    return 0;
}
'''

NEW["00401070"] = r'''
/* Find the node with this id, or NULL.
 *
 * Every accessor in this group opens with this scan. The original inlined it
 * in each one; it is written once here and the others call it. Note what the
 * original does when the id is absent: it carries on with a null node and
 * reads through it, so the accessors return whatever sits at offset 0 rather
 * than failing. That behaviour is preserved - callers rely on the id existing.
 */
DialogNode *Dialog_FindNode(int16_t id)
{
    DialogNode *n = (DialogNode *)g_DialogNodes;
    int i;

    for (i = 0; i < g_DialogNodeCount; i++, n++)
        if (n->id == id)
            return n;
    return 0;
}

/* The node's string pool: a packed run of NUL-terminated strings. */
int __cdecl Dialog_GetStrings(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    return (int)u32(n, 32);          /* ptr[8] */
}
'''

NEW["004010A0"] = r'''
/* The node's asset id, e.g. "0C64BF" - the stem of its MORPH/*.3DM. */
char *__cdecl Dialog_GetAssetName(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    return (char *)n + 46;
}
'''
NEW["004010D0"] = r'''
int __cdecl Dialog_GetField60(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    return i16(n, 60);               /* role unknown; see types.h */
}
'''
NEW["00401110"] = r'''
int __cdecl Dialog_GetField62(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    return i16(n, 62);               /* role unknown */
}
'''
NEW["00401150"] = r'''
int __cdecl Dialog_GetField56(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    return i16(n, 56);               /* role unknown */
}
'''
NEW["00401190"] = r'''
int __cdecl Dialog_GetField58(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    return i16(n, 58);               /* role unknown */
}
'''

NEW["004011D0"] = r'''
/* The text of one reply.
 *
 * The pool is a packed run of NUL-terminated strings; walk `which + 1` of them
 * and return where that lands. `which == -1` therefore returns the first
 * string, which is the line the character speaks; 0..3 are the four replies.
 *
 * The walk is the original's, including its quirk: it reads the first byte and
 * advances past it, then skips strlen+1 only if that byte was non-zero. That
 * steps over an empty string by exactly one byte, which is what makes unused
 * reply slots skippable.
 */
const char *__cdecl Dialog_GetText(int16_t id, int which)
{
    DialogNode *n = Dialog_FindNode(id);
    const char *s = *(const char **)((char *)n + 32);   /* ptr[8] */
    int remaining = which + 1;

    if (which != -1)
    {
        do
        {
            if (*s++)
                s += strlen(s) + 1;
            --remaining;
        }
        while (remaining);
    }
    return s;
}
'''

NEW["00401220"] = r'''
/* String 5 of the node's pool: the line the *player* speaks to open the
 * exchange. 1072 nodes in the shipped file use only string 0 (the character's
 * line), 9 use only this one, and 19 use both. */
const char *__cdecl Dialog_GetPlayerLine(int16_t id)
{
    DialogNode *n = Dialog_FindNode(id);
    const char *s = *(const char **)((char *)n + 32);
    int i = 5;

    do
    {
        if (*s++)
            s += strlen(s) + 1;
        --i;
    }
    while (i);
    return s;
}
'''

NEW["004012B0"] = r'''
/* ptr[4 + branch] - the action script attached to one reply. Handed back
 * unevaluated, unlike the condition, which is why these are read as the
 * actions and ptr[0..3] as the conditions. */
int __cdecl Dialog_GetBranchAction(int16_t id, int branch)
{
    DialogNode *n = Dialog_FindNode(id);
    return (int)u32((char *)n + 4 * branch, 16);
}
'''

NEW["004012F0"] = r'''
/* Run a reply's condition script and return what it evaluates to.
 *
 * ptr[0..3] hold the four conditions. An absent script means the reply is
 * always offered, hence the 1 when the pointer is null. Otherwise a throwaway
 * VM context is built around the bytecode, run, and its top-of-stack read
 * back; see docs/SCRIPT_VM.md.
 */
int __cdecl Dialog_EvalBranchCondition(int16_t id, int branch)
{
    DialogNode *n = Dialog_FindNode(id);
    uint32_t   *ctx;
    int         script, result;

    script = (int)u32(n, 4 * branch);
    if (!script)
        return 1;

    ctx = sub_406290(dword_69BC60, 0, 0, 0);
    ctx[3] = script;                 /* ctx+12 = the program counter */
    u16i(ctx, 11) = 1;               /* ctx+22 = run flag */
    sub_406460((int)ctx);
    result = Script_Pop((int)ctx);
    sub_406390(ctx);
    return result;
}
'''

NEW["00401370"] = r'''
/* param[branch] - the node this reply leads to, or -1 to end the
 * conversation. All 1452 used values in the shipped file are valid indices
 * into their own chunk's node array. */
int __cdecl Dialog_GetBranchTarget(int16_t id, int branch)
{
    DialogNode *n = Dialog_FindNode(id);
    return i16((char *)n + 2 * branch, 36);
}
'''

# SPDX-License-Identifier: GPL-3.0-or-later
NEW = {}

NEW["00401A80"] = r'''
/* Pop the script VM's stack. ctx+16 is the stack, ctx+20 the 16-bit stack
 * pointer, which addresses one past the top. */
int __cdecl Script_Pop(int ctx)
{
    return u32(u32(ctx, 16), 4 * (uint16_t)--u16(ctx, 20));
}
'''

NEW["00401AA0"] = r'''
/* Fetch the 16-bit operand at the program counter and advance past it.
 *
 * The two bytes are read separately, low then high. 0xFFFF means "none". If
 * bit 0x4000 is set the bit is cleared and what remains indexes the fixup
 * table at ctx+36 instead of being the value itself - an indirection layer,
 * presumably so the authoring tool could patch operands after compiling. No
 * operand in any shipped dialogue script actually uses it: of the 754 16-bit
 * operands in the corpus, none has the bit set.
 */
int __cdecl Script_FetchOperand(int ctx)
{
    uint8_t *pc = *(uint8_t **)(ctx + 12);
    int      value;

    value = pc[0] | (pc[1] << 8);
    u32(ctx, 12) = pc + 2;

    if ((uint16_t)value != 0xFFFF && (value & 0x4000))
    {
        value &= ~0x4000;
        return i16(u32(ctx, 36) + 2 * value, 2);
    }
    return value;
}
'''

NEW["00401C50"] = r'''
/* Opcode 4: unconditional jump. Fetches a signed 16-bit displacement and adds
 * it to the program counter, which by then points past the operand. */
uint8_t *__cdecl Script_OpJump(int ctx)
{
    uint8_t *pc = *(uint8_t **)(ctx + 12);
    uint8_t *after;
    int      value;

    value = pc[0] | (pc[1] << 8);
    after = pc + 2;
    u32(ctx, 12) = after;

    if ((uint16_t)value != 0xFFFF && (value & 0x4000))
    {
        value &= ~0x4000;
        value = u16(u32(ctx, 36) + 2 * value, 2);
    }

    after += (int16_t)value;
    u32(ctx, 12) = after;
    return after;
}
'''

NEW["00401B40"] = r'''
/* Opcode 1: dump 64 bytes of bytecode from the program counter as hex, eight
 * per line. A debugging aid; nothing in the shipped scripts uses it. */
int __cdecl Script_OpDumpCode(int ctx)
{
    uint8_t *pc = *(uint8_t **)(ctx + 12);
    int      i, result = 0;

    for (i = 0; i < 64; i++)
    {
        result = printf("%02x ", *pc++);
        if ((i & 7) == 7)
            result = printf("\n");
    }
    return result;
}
'''

NEW["004060B0"] = r'''
/* Run a dialogue script to completion.
 *
 * One byte of opcode, dispatched through the table at 0x004C0140 whose entries
 * are { handler, operandBytes }; each handler consumes its own operands and
 * advances ctx->pc itself. Opcode 3 ends the script. Bit 0x10 of ctx+40 aborts
 * the run, which is how the engine stops a script mid-way.
 *
 * ctx+4 is the script as passed in and ctx+12 the program counter; the entry
 * value of the latter is saved and restored, so running a script leaves the
 * caller's position alone.
 *
 * g_ScriptDryRun is set here: this entry point EVALUATES, and the opcodes that
 * write game variables check the flag and skip the write. Side effects happen
 * through sub_406460, which clears it. See docs/SCRIPT_VM.md.
 *
 * Returns 1 if the run was aborted, 0 if it reached `end`.
 */
int __cdecl Script_Run(int ctx)
{
    uint8_t *pc;
    int      saved_pc;
    int      op;

    g_ScriptDryRun = 1;
    pc = *(uint8_t **)(ctx + 4);
    saved_pc = u32(ctx, 12);

    op = *pc;
    u32(ctx, 12) = pc + 1;
    if (op == 3)
    {
        u32(ctx, 12) = saved_pc;
        return 0;
    }

    while ((u8(ctx, 40) & 0x10) == 0)
    {
        (*(&off_4C0140 + 2 * op))(ctx);          /* handler[op](ctx) */
        pc = *(uint8_t **)(ctx + 12);
        op = *pc;
        u32(ctx, 12) = pc + 1;
        if (op == 3)
        {
            u32(ctx, 12) = saved_pc;
            return 0;
        }
    }
    u32(ctx, 12) = saved_pc;
    return 1;                                    /* aborted */
}
'''

NEW["00406120"] = r'''
/* The same interpreter, stopping at opcode 75 instead of watching the abort
 * flag. Also an evaluate-only run - it sets g_ScriptDryRun. */
int __cdecl Script_RunToOpcode75(int ctx)
{
    uint8_t *pc = *(uint8_t **)(ctx + 4);
    int      saved_pc = u32(ctx, 12);
    int      op;

    g_ScriptDryRun = 1;
    op = *pc;
    u32(ctx, 12) = pc + 1;
    if (op == 3)
    {
        u32(ctx, 12) = saved_pc;
        return 0;
    }

    while (op != 75)
    {
        (*(&off_4C0140 + 2 * op))(ctx);
        pc = *(uint8_t **)(ctx + 12);
        op = *pc;
        u32(ctx, 12) = pc + 1;
        if (op == 3)
        {
            u32(ctx, 12) = saved_pc;
            return 0;
        }
    }
    u32(ctx, 12) = saved_pc;
    return 1;
}
'''

NEW["0040E530"] = r'''
/* Read a game variable. g_GameDB+8 is the array; IAM\VARIABLES.TAG names the
 * indices, so the game's persistent state is readable: 'OE Table Corresp',
 * '1 Section 1 Finie', 'Inventaire', 'Vie'. */
int __cdecl Var_Get(int index)
{
    return u32(u32(g_GameDB, GAMEDB_VARS), 4 * index);
}
'''

NEW["0040E510"] = r'''
/* Write a game variable, returning the index. This is how a conversation
 * remembers what you did: about twenty VM opcodes call it, most of them paired
 * with Var_Get for the compound assignments. */
int __cdecl Var_Set(int index, int value)
{
    u32(u32(g_GameDB, GAMEDB_VARS), 4 * index) = value;
    return index;
}
'''

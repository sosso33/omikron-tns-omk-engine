# SPDX-License-Identifier: GPL-3.0-or-later
NEWM = {}
NEWM["0041AFC0"] = r'''
/* Start a talking-head animation on an actor: MORPH/<name>.3DM.
 *
 * A .3DM is one recorded line - per-frame face vertices, per-frame skeleton
 * quaternions and the ADPCM voice, all interleaved at 30 fps. See
 * docs/FILE_FORMATS.md section 5.
 *
 * `path` is used verbatim if it already contains a backslash, otherwise it is
 * resolved under MORPH\. The open is retried five times, which is what a 1999
 * game does when the file may be on a CD that is still spinning up.
 *
 * The camera is pointed at the actor before playback starts: sub_442D70
 * rotates the -Z axis by the actor's node transform, and atan2 of the result
 * gives the heading the morph camera should sit at.
 */
int __cdecl Morph_Play(int actorIndex, char *path)
{
    int    actor;
    char  *rec;
    int    state;
    FILE  *probe;
    int    attempt;
    int    morphObj;
    float  heading;
    int    facing;
    int    dirX;   /* out-param */
    int    dirZ;   /* out-param */
    int    dirY;   /* out-param */
    char   fullPath[260];

    actor = (actorIndex == -1) ? sub_457030() : actorIndex;
    rec   = (char *)&g_Actors + ACTOR_STRIDE * actor;

    sub_42CA50();

    /* State 4 means a previous morph is still loaded - release it first. */
    state = u32i(rec, ACTOR_STATE);
    if (state && state == 4)
    {
        if (sub_44B460(*((uint32_t **)rec + 43)))
            u32i(rec, 44) = u32i(rec, 43);
        else
            u32i(rec, 44) = 0;
        sub_44AA20(u32i(rec, 43));
        u32i(rec, 43) = 0;
        u32i(rec, ACTOR_STATE) = 0;
    }

    sub_42BC10(byte_910316 ? 1 : 0);

    if (strrchr(path, '\\'))
    {
        fullPath[0] = 0;
        strncat(fullPath, path, 0x103u);
    }
    else
    {
        sprintf(fullPath, "MORPH\\%s", path);

        /* Probe that the file is there, retrying while the CD spins up. */
        for (attempt = 0; ; ++attempt)
        {
            probe = sub_412A90(fullPath, 1, 0);
            if (probe) break;
            if ((unsigned int)(attempt + 1) >= 5)
            {
                sub_42BC10(1);
                goto configure;
            }
        }
        fclose(probe);
        sub_42CC30(0);
    }

configure:
    sub_42BCA0(u32i(rec, ACTOR_NODE), u32i(rec, 3),
               u32i(rec, 61), u32i(rec, 62), u32i(rec, 63));
    sub_42BD90(0x5640u, 1u, 30);      /* 22080 Hz, mono, 30 fps */

    sub_440C80(*((uint32_t **)rec + ACTOR_NODE));

    /* Rotate (0, 0, -1) by the actor's transform to get the way it faces,
     * then turn that into a heading in degrees. 57.29577951 is 180/pi. */
    sub_442D70(0.0, 0.0, -1.0, u32i(rec, ACTOR_NODE) + 92,
               (int)&dirX, (int)&dirY, (int)&dirZ);
    facing  = i32i(rec, ACTOR_FACING);
    heading = atan2(as_f32(dirZ), as_f32(dirX)) * 57.29577951308232
              - -90.0 - as_f32(facing);
    sub_42BE00(heading);

    morphObj = u32i(rec, ACTOR_MORPHOBJ);
    if (morphObj)
    {
        sub_42BDD0(morphObj, u32i(rec, 47), facing);
        sub_42BE30(f32(**((uint32_t **)rec + ACTOR_NODE), 36),
                   f32(**((uint32_t **)rec + ACTOR_NODE), 40),
                   f32(**((uint32_t **)rec + ACTOR_NODE), 44));
        sub_42BE10(1, strstr(path, a02e19a_1) == 0);
    }
    else
    {
        sub_42BE10(0, 0);
    }

    Morph_Start(fullPath, 0);
    u32i(rec, ACTOR_STATE) = 5;       /* playing */
    return 1;
}
'''

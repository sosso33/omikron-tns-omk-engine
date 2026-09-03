# Processed functions

557 of 2177 functions have been worked out, in three states:

| status | meaning | count |
|---|---|---|
| `CLEAN` | body rewritten by hand | 64 |
| `NAMED` | read and named from evidence, body left as generated | 481 |
| `READ` | read, then deliberately left alone - the banner says why | 12 |

Everything else is `@status RAW`: untouched decompiler output. A RAW function
may still carry a real name - the automatic recovery pass lifted around 50 out
of the binary's own debug strings - but nobody has read it. `clean/src/` always
holds the untouched original of every function whatever its status.

`READ` is the one that is easy to lose. A function can be read closely, supply
the fact that was wanted, and still be the wrong thing to rename or rewrite -
usually because a name would be a guess. Without a record of that, the next
pass re-reads it from scratch and risks inventing the name that was rejected
the first time. The reason is written into the banner comment, so it travels
with the code.

Renames are applied as **real renames across the whole tree** by
`tools/rename.py`, not as `#define` aliases - a function renamed where it is
defined reads the same at every call site in the other modules, in `decls.h`,
and in its `@func` banner. `tools/renames.json` is the map (638 entries).

`tools/rename.py` promotes RAW to NAMED automatically for anything in the map,
so the trace cannot drift out of step with the renames.

Regenerate this file with `python3 tools/index.py`.


## Dialogue

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x00401000` | `Dialog_Unload` |  | 01_file.c | 2 | Release the loaded conversation and forget it. |
| `0x00401070` | `Dialog_GetStrings` |  | 01_file.c | 1 | The node lookup shared by every accessor in this group, and the first of them. |
| `0x004010A0` | `Dialog_GetAssetName` |  | 01_file.c | 1 | The node's asset id - a 6-character stem naming its MORPH/*.3DM, which holds the voice recording and the facial animation for this line. |
| `0x004010D0` | `Dialog_GetLineCamera` |  | 01_file.c | 1 | the camera for the line this node speaks. |
| `0x00401110` | `Dialog_GetLineCamera2` |  | 01_file.c | 1 | the second camera id command 53 loads, alongside field60. |
| `0x00401150` | `Dialog_GetReplyCamera` |  | 01_file.c | 1 | the camera for the player's reply. |
| `0x00401190` | `Dialog_GetReplyCamera2` |  | 01_file.c | 1 | the second camera id command 57 loads, alongside field56. |
| `0x004011D0` | `Dialog_GetText` |  | 01_file.c | 1 | The text of one reply. |
| `0x00401220` | `Dialog_GetPlayerLine` |  | 01_file.c | 1 | String 5 of the node's pool: the line the *player* speaks to open the exchange. |
| `0x004012B0` | `Dialog_GetBranchAction` |  | 01_file.c | 1 | ptr[4 + branch] - the action script attached to one reply. |
| `0x004012F0` | `Dialog_EvalBranchCondition` |  | 01_file.c | 1 | Run a reply's condition script and return what it evaluates to. |
| `0x00401370` | `Dialog_GetBranchTarget` |  | 01_file.c | 1 | param[branch] - the node this reply leads to, or -1 to end the conversation. |
| `0x004013B0` | `Dialog_ApplyLineCameras` |  | 01_file.c | 1 | Point the camera at whoever is talking, and start it moving. |
| `0x00401800` | `Dialog_Load` |  | 01_file.c | 1 | Load one conversation out of the IAM\DIALOG archive and hand control to the dialogue camera. |
| `0x0041B280` | `Dialog_SetSubjectActor` |  | 04_sys.c | 1 | Hand a conversation its subject: settle the player, then mark the actor the shot is on. |
| `0x0041B390` | `Dialog_ClearSubjectActor` | named | 04_sys.c | 2 | Undo Dialog_SetSubjectActor: take the actor back off the shot. |
| `0x0046A200` | `Dialog_TickUI` | named | 21_d3d.c | 7 | name established, body still as generated |

## Script VM

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x00401A80` | `Script_Pop` |  | 01_file.c | 1 | Pop the script VM's stack. |
| `0x00401AA0` | `Script_FetchOperand` |  | 01_file.c | 0 | Fetch the 16-bit operand at the program counter and advance past it. |
| `0x00401AE0` | `Script_StoreVar` | named | 01_file.c | 0 | name established, body still as generated |
| `0x00401B40` | `Script_OpDumpCode` |  | 01_file.c | 0 | Opcode 1: dump 64 bytes of bytecode from the program counter as hex, eight per line. |
| `0x00401C50` | `Script_OpJump` |  | 01_file.c | 0 | Opcode 4: unconditional jump. |
| `0x004060B0` | `Script_Run` |  | 01_file.c | 1 | Run a dialogue script to completion. |
| `0x00406120` | `Script_RunToOpcode75` |  | 01_file.c | 1 | The same interpreter, stopping at opcode 75 instead of watching the abort flag. |
| `0x00406290` | `Script_NewContext` | named | 01_file.c | 6 | name established, body still as generated |
| `0x00406390` | `Script_FreeContext` | named | 01_file.c | 12 | name established, body still as generated |
| `0x004063D0` | `Script_QueueAction` | named | 01_file.c | 8 | name established, body still as generated |
| `0x00406460` | `Script_Execute` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00407DC0` | `Script_Pump` |  | 01_file.c | 3 | The world-script pump. |
| `0x00408220` | `Script_ProcessActions` |  | 01_file.c | 1 | Drain one queued action from a script context, before Script_Execute runs it (Script_Pump calls both, every frame, for every live context). |
| `0x0040E510` | `Var_Set` |  | 01_file.c | 2 | Write a game variable, returning the index. |
| `0x0040E530` | `Var_Get` |  | 01_file.c | 1 | Read a game variable. |
| `0x0044A070` | `Script_FunctionsIndexesToAdresses` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x0044A7E0` | `Script_StartScript` | named | 16_o3de.c | 4 | name established, body still as generated |
| `0x0044B460` | `Script_ObjectRunsForever` | named | 16_o3de.c | 3 | Does this scene object's program never end by itself? Two ways to run forever, and it tests both against the layout in FILE_FORMATS "the object… |
| `0x0044C680` | `Script_GetParamInt` | named | 17_script.c | 88 | name established, body still as generated |
| `0x0044C6B0` | `Script_GetParamFloatB` | named | 17_script.c | 12 | name established, body still as generated |
| `0x0044C6E0` | `Script_GetParamFloat` | named | 17_script.c | 37 | name established, body still as generated |
| `0x0044C6F0` | `Script_SetParamFloat` | named | 17_script.c | 55 | name established, body still as generated |
| `0x0044C710` | `Script_GetParamFloatC` | named | 17_script.c | 10 | name established, body still as generated |
| `0x0044C7A0` | `Script_SetCurrentScene` | named | 17_script.c | 4 | name established, body still as generated |
| `0x0044C7C0` | `Script_GetCurrentScene` | named | 17_script.c | 30 | name established, body still as generated |
| `0x0044C7E0` | `Script_SetFrameTime` | named | 17_script.c | 1 | name established, body still as generated |
| `0x0044C7F0` | `Script_GetFrameTime` | named | 17_script.c | 16 | name established, body still as generated |
| `0x0044C840` | `Script_SyncChainTail` | named | 17_script.c | 4 | name established, body still as generated |
| `0x0044C860` | `Script_PlayScript` |  | 17_script.c | 2 | Run one scene object's program for one frame. |
| `0x0044CC50` | `Script_PlayAllScripts` | named | 17_script.c | 1 | name established, body still as generated |
| `0x0049EAB0` | `Script_ClearError` | named | 26_ole.c | 19 | name established, body still as generated |
| `0x0049EB70` | `Script_GetError` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x0049EB90` | `Script_GetErrorCode` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x0049EBC0` | `Script_SetErrorf` | named | 27_sys.c | 3 | name established, body still as generated |
| `0x0049F230` | `Script_LinkCamEditing` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x004A0D30` | `Script_InstanceTemplate` | named | 28_script.c | 3 | name established, body still as generated |
| `0x004A0EE0` | `Script_Wait` | named | 28_script.c | 1 | name established, body still as generated |
| `0x004A12D0` | `Script_PlaySound` | named | 28_script.c | 2 | name established, body still as generated |
| `0x004A4580` | `Script_SelectCamera` | named | 28_script.c | 1 | name established, body still as generated |

## Morph playback and animation

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x0040B220` | `Camera_FindWorld` | named | 01_file.c | 1 | name established, body still as generated |
| `0x004137D0` | `Camera_SetFlag` | named | 04_sys.c | 7 | name established, body still as generated |
| `0x004146C0` | `Camera_LoadParams` | named | 04_sys.c | 2 | Load a CameraParam into the live camera block. |
| `0x004147F0` | `Camera_RequestChanged` |  | 04_sys.c | 1 | Would this request actually change the camera? Non-zero if so. |
| `0x00414BF0` | `Camera_Request` |  | 04_sys.c | 23 | Put the camera into `mode`, using the parameters in `request`. |
| `0x00414DB0` | `Camera_SetShake` | named | 04_sys.c | 1 | name established, body still as generated |
| `0x0041AFC0` | `Morph_Play` |  | 04_sys.c | 1 | Start a talking-head animation on an actor: MORPH/<name>.3DM. A .3DM is one recorded line - per-frame face vertices, per-frame skeleton… |
| `0x0042BCA0` | `Morph_ResetTracks` |  | 07_thread.c | 2 | Prepare the morph player's track tables for a new clip. |
| `0x0042BD90` | `Morph_SetAudioFormat` |  | 07_thread.c | 2 | Set the morph player's audio format: sample rate, channels, frame rate. |
| `0x0042C300` | `Morph_Open` | named | 08_wave.c | 2 | name established, body still as generated |
| `0x0042C870` | `Morph_Start` | named | 08_wave.c | 2 | name established, body still as generated |
| `0x0042CA50` | `Morph_Stop` | named | 08_wave.c | 6 | name established, body still as generated |
| `0x0042CBD0` | `Morph_IsDone` | named | 08_wave.c | 2 | name established, body still as generated |
| `0x00434010` | `Anim_Load` |  | 09_ddraw.c | 2 | Clear the runtime state of every clip in the loaded library. |
| `0x00446BC0` | `Camera_GetPosition` | named | 16_o3de.c | 4 | name established, body still as generated |
| `0x0044F370` | `Anim_LoadClipSfx` | named | 18_d3d.c | 2 | name established, body still as generated |
| `0x0046E5F0` | `Anim_RegisterClip` | named | 22_dsound.c | 2 | name established, body still as generated |
| `0x00470FE0` | `Anim_BindNodeTrack` | named | 23_script.c | 1 | name established, body still as generated |
| `0x00471040` | `Anim_BindToHierarchy` | named | 23_script.c | 10 | name established, body still as generated |
| `0x00471160` | `Anim_SnapRootToStart` | named | 23_script.c | 1 | name established, body still as generated |
| `0x004711D0` | `Anim_RootDelta` | named | 23_script.c | 8 | name established, body still as generated |
| `0x004715B0` | `Anim_SetFrame` | named | 23_script.c | 9 | name established, body still as generated |
| `0x00471690` | `Anim_ApplyNodeFrame` | named | 23_script.c | 0 | name established, body still as generated |
| `0x004721B0` | `Anim_Frames` | named | 23_script.c | 55 | name established, body still as generated |
| `0x00483200` | `Adpcm_DecodeMono` |  | 25_sys.c | 2 | Decode mono OTNS ADPCM into 16-bit samples. |
| `0x00483340` | `Adpcm_DecodeStereo` |  | 25_sys.c | 2 | Decode stereo OTNS ADPCM. One byte carries one sample for each channel: the high nibble is the left channel, the low nibble the right, so there is… |
| `0x004A4130` | `Anim_TickClipSfx` | named | 28_script.c | 2 | name established, body still as generated |

## Files and archives

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x0040FF90` | `Archive_ReadChunk` |  | 02_file.c | 6 | Read one chunk out of one of the game's flat archives. |
| `0x004128F0` | `Res_OpenFile` |  | 04_sys.c | 7 | Open a game file, resolving it against the install directory. |

## Scene and geometry

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x00409FC0` | `Scene_LoadProps` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040B160` | `Scene_Block` | named | 01_file.c | 7 | name established, body still as generated |
| `0x0040C120` | `Scene_Load` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040D6A0` | `Scene_FindObjectRecord` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040D760` | `Scene_FindObjectIndexById` |  | 01_file.c | 1 | Resolve an object or character id to the index the engine addresses it by. |
| `0x00436C40` | `o3de_FindNodeByName` | named | 10_dsound.c | 37 | name established, body still as generated |
| `0x00436D90` | `o3de_FindMeshByName` | named | 10_dsound.c | 19 | name established, body still as generated |
| `0x004370A0` | `o3de_SetNodePos` | named | 10_dsound.c | 44 | name established, body still as generated |
| `0x00437110` | `o3de_MoveNodeBy` | named | 10_dsound.c | 59 | name established, body still as generated |
| `0x00437400` | `o3de_UnlinkObject` | named | 10_dsound.c | 48 | name established, body still as generated |
| `0x004374E0` | `o3de_LinkObjectToParent` | named | 10_dsound.c | 50 | name established, body still as generated |
| `0x00437E00` | `o3de_GetObjectByIndex` |  | 11_o3de.c | 1 | Return a pointer to object `index` of `scene`. |
| `0x00441A00` | `o3de_FreeScene` | named | 16_o3de.c | 27 | name established, body still as generated |
| `0x00441EB0` | `Matrix3x3_FromEulerAngles` |  | 16_o3de.c | 33 | Build a 3x3 rotation matrix from three Euler angles, in radians. |
| `0x00442A00` | `Matrix3x3_FromQuaternion` | named | 16_o3de.c | 10 | name established, body still as generated |
| `0x00442D70` | `Matrix3x3_RotateVector` | named | 16_o3de.c | 76 | name established, body still as generated |
| `0x00442DF0` | `Matrix3x3_RotateVectorT` | named | 16_o3de.c | 40 | name established, body still as generated |
| `0x004430A0` | `o3de_ForEachMeshInBox` | named | 16_o3de.c | 7 | name established, body still as generated |
| `0x00446BA0` | `Scene_SetActiveCamera` | named | 16_o3de.c | 5 | name established, body still as generated |
| `0x00446BB0` | `Scene_GetActiveCamera` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x00449380` | `Scene_FullPath` | named | 16_o3de.c | 22 | name established, body still as generated |
| `0x00449750` | `Scene_LoadSCX` |  | 16_o3de.c | 4 | Load one SCPTDATA/*.SCX scene script. |
| `0x0044AA20` | `Scene_ResetObjectState` | named | 16_o3de.c | 14 | name established, body still as generated |
| `0x0044AAC0` | `Scene_ReleaseResources` | named | 16_o3de.c | 6 | name established, body still as generated |
| `0x0044B1B0` | `Scene_FindObjectByName` | named | 16_o3de.c | 2 | name established, body still as generated |
| `0x0044B280` | `Scene_LinkObjectPair` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x0044B300` | `Scene_MakeObjectActive` | named | 16_o3de.c | 2 | name established, body still as generated |
| `0x0044B7D0` | `Scene_ObjectTables` | named | 16_o3de.c | 106 | name established, body still as generated |
| `0x0044CD10` | `Scene_FindScriptObject` | named | 17_script.c | 4 | Find a script object in a loaded scene by its id. |
| `0x0044EA10` | `Scene_Load3DO` | named | 18_d3d.c | 7 | name established, body still as generated |
| `0x0044EBF0` | `Scene_InitAndLoadTextures` | named | 18_d3d.c | 0 | name established, body still as generated |
| `0x0044EC80` | `Scene_Load3DOStream` | named | 18_d3d.c | 2 | name established, body still as generated |
| `0x0048CC80` | `Scene_FindSoundIndex` | named | 25_sys.c | 9 | name established, body still as generated |
| `0x0048CF90` | `o3de_TraverseNodes` | named | 25_sys.c | 3 | name established, body still as generated |
| `0x0048CFE0` | `o3de_Traverse` | named | 25_sys.c | 26 | name established, body still as generated |
| `0x0048D080` | `o3de_TraverseWithArg` | named | 25_sys.c | 6 | name established, body still as generated |
| `0x0048EE10` | `o3de_SetSpriteMorphPalette` | named | 25_sys.c | 1 | name established, body still as generated |
| `0x0049F210` | `Scene_FindCamEditingById` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x004A0DA0` | `Scene_PreloadObjectSprites` | named | 28_script.c | 1 | name established, body still as generated |
| `0x004A5650` | `Scene_SpriteIsLoaded` | named | 28_script.c | 7 | name established, body still as generated |
| `0x004A5D70` | `Scene_AnimClip` | named | 28_script.c | 8 | name established, body still as generated |
| `0x004A5EA0` | `Scene_AnimRecord` | named | 28_script.c | 6 | name established, body still as generated |

## System

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x0040EB60` | `Dbg_LogLine` | named | 02_file.c | 1 | name established, body still as generated |
| `0x0040EC70` | `Dbg_LogTagged` | named | 02_file.c | 0 | name established, body still as generated |
| `0x00411EF0` | `Mem_Alloc` |  | 03_win32.c | 48 | malloc with the engine's allocation accounting. |
| `0x00412060` | `Mem_Free` |  | 03_win32.c | 222 | free() with the matching accounting. |
| `0x00412100` | `Sys_GetTimeMs` |  | 04_sys.c | 46 | Milliseconds since the engine's time base, or the frozen value while paused. |
| `0x0041E040` | `Subtitle_Show` |  | 05_sys.c | 3 | Put a line of subtitle text on screen and work out where to draw it. |
| `0x0043C420` | `Dbg_LogError` | named | 14_mci.c | 44 | name established, body still as generated |
| `0x0043EA10` | `Text_DrawRun` | named | 15_dinput.c | 1 | Draw one run of same-styled characters onto the text surface. |
| `0x0043EEF0` | `Text_GlyphAdvance` | named | 15_dinput.c | 2 | name established, body still as generated |
| `0x0043EF30` | `Text_PackColour` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x0043F180` | `Text_DrawBlock` |  | 15_dinput.c | 20 | Lay out and draw a block of text, returning the height it occupied. |
| `0x0043F3E0` | `Text_LayOutBlock` | named | 15_dinput.c | 1 | Lay out a block of text: parse the markup, wrap it, and emit styled runs. |

## Other

| address | name | | file | used | what it does |
|---|---|---|---|---|---|
| `0x00402B70` | `Area_LoadIntoSlot` | named | 01_file.c | 2 | name established, body still as generated |
| `0x00406560` | `Zones_RegisterAll` |  | 01_file.c | 3 | Rebuild the runtime zone index from both resident areas, and prune dead script contexts. |
| `0x00406760` | `Zone_FindScriptsById` | named | 01_file.c | 4 | name established, body still as generated |
| `0x004067D0` | `Game_HandleEvent` | named | 01_file.c | 2 | The engine's central event switch - everything routes through here (162 Game_RaiseEvent sites). |
| `0x004083F0` | `Game_RaiseEvent` | named | 01_file.c | 162 | name established, body still as generated |
| `0x00408530` | `Area_Transition` | named | 01_file.c | 3 | name established, body still as generated |
| `0x00408A10` | `SaveDir_Build` | named | 01_file.c | 3 | Build the save-slot directory `SaveDir_*` walks, out of `IAM\GAMES`. |
| `0x00408B50` | `SaveDir_CountProfiles` | named | 01_file.c | 3 | name established, body still as generated |
| `0x00408BF0` | `SaveDir_NameAt` | named | 01_file.c | 3 | name established, body still as generated |
| `0x00408CE0` | `SaveDir_CountByName` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00408D20` | `SaveDir_RecordAt` | named | 01_file.c | 1 | name established, body still as generated |
| `0x00408E00` | `GameDB_Alloc` | named | 01_file.c | 2 | name established, body still as generated |
| `0x00408E40` | `GameDB_Free` | named | 01_file.c | 1 | name established, body still as generated |
| `0x00408E60` | `GameDB_Ptr` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00408EF0` | `Game_WriteSave` | named | 01_file.c | 0 | name established, body still as generated |
| `0x00408FC0` | `Game_LoadSave` | named | 01_file.c | 1 | name established, body still as generated |
| `0x004090A0` | `SaveDir_ClearSlot` | named | 01_file.c | 0 | name established, body still as generated |
| `0x00409100` | `SaveDir_Delete` | named | 01_file.c | 0 | name established, body still as generated |
| `0x00409200` | `SaveDir_Load` | named | 01_file.c | 2 | name established, body still as generated |
| `0x00409370` | `HighScore_Insert` | named | 01_file.c | 0 | name established, body still as generated |
| `0x00409420` | `Message_RunHandlers` |  | 01_file.c | 2 | Run the script subscribed to a message (FILE_FORMATS 5b3). |
| `0x004095D0` | `ObjectSlot_Alloc` | named | 01_file.c | 3 | name established, body still as generated |
| `0x00409620` | `ObjectSlot_Free` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00409630` | `ObjectSlot_Id` | named | 01_file.c | 3 | name established, body still as generated |
| `0x004096B0` | `ObjectList_Count` | named | 01_file.c | 9 | name established, body still as generated |
| `0x004096C0` | `ObjectList_Capacity` | named | 01_file.c | 0 | name established, body still as generated |
| `0x004096D0` | `ObjectList_IsFull` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00409700` | `ObjectList_Find` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00409780` | `Object_ApplyEffect` | named | 01_file.c | 3 | name established, body still as generated |
| `0x004098E0` | `Inventory_Insert` | named | 01_file.c | 3 | name established, body still as generated |
| `0x00409AE0` | `ObjectRecord_Read` | named | 01_file.c | 3 | name established, body still as generated |
| `0x00409B00` | `ObjectList_SetCapacity` | named | 01_file.c | 6 | name established, body still as generated |
| `0x00409B40` | `ObjectList_Load` | named | 01_file.c | 5 | name established, body still as generated |
| `0x00409C10` | `ObjectList_Free` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00409C70` | `ObjectList_DisplayName` | named | 01_file.c | 4 | name established, body still as generated |
| `0x00409C90` | `ObjectList_Header` | named | 01_file.c | 12 | name established, body still as generated |
| `0x00409CB0` | `ObjectList_InsertFront` | read | 01_file.c | 5 | Read, not changed: this is what reads IAM\OBJECT, and reading it is how that archive's layout was established. |
| `0x00409DC0` | `ObjectList_RemoveAt` | named | 01_file.c | 5 | name established, body still as generated |
| `0x00409E50` | `ObjectList_RemoveById` | named | 01_file.c | 2 | name established, body still as generated |
| `0x00409F50` | `PropAsset_Find` | named | 01_file.c | 3 | name established, body still as generated |
| `0x0040AEE0` | `Actor_IdBySlot` | named | 01_file.c | 3 | name established, body still as generated |
| `0x0040AF30` | `State_SetBit` | named | 01_file.c | 0 | name established, body still as generated |
| `0x0040AFC0` | `ObjectState_Set` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040B010` | `ObjectState_Get` | named | 01_file.c | 7 | name established, body still as generated |
| `0x0040B060` | `Area_Block` | named | 01_file.c | 9 | name established, body still as generated |
| `0x0040B090` | `Address_SetEnabled` | named | 01_file.c | 0 | name established, body still as generated |
| `0x0040B120` | `Area_SetLoadedScene` | named | 01_file.c | 0 | name established, body still as generated |
| `0x0040B140` | `Area_GetLoadedScene` | named | 01_file.c | 3 | name established, body still as generated |
| `0x0040B190` | `Actor_FindById` | named | 01_file.c | 59 | name established, body still as generated |
| `0x0040B2B0` | `Actor_CtlSlotName` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040B360` | `Actor_GetProperty` | named | 01_file.c | 2 | name established, body still as generated |
| `0x0040B8D0` | `Actor_SetProperty` | named | 01_file.c | 2 | name established, body still as generated |
| `0x0040BAF0` | `Object_ModelPath` | named | 01_file.c | 5 | name established, body still as generated |
| `0x0040BB90` | `Actors_SpawnFromTables` | named | 01_file.c | 1 | Spawn every character an area and a scene place, and stand them where the data says. |
| `0x0040C7E0` | `Area_TickLoad` | named | 01_file.c | 2 | name established, body still as generated |
| `0x0040CC90` | `Area_Load` | named | 01_file.c | 3 | name established, body still as generated |
| `0x0040D500` | `Zone_StateBit` | named | 01_file.c | 3 | name established, body still as generated |
| `0x0040D540` | `Zone_SetStateBit` |  | 01_file.c | 0 | Set or clear one zone's save-game bit in the game DB's +28 bitmap - what VM opcodes 64 `zone.enable` (value 1) and 65 `zone.disable` (value 0)… |
| `0x0040D950` | `State_Save` | named | 01_file.c | 1 | The serializer: the live game DB -> the 8192 bytes a save slot holds. |
| `0x0040DB00` | `State_Apply` | named | 01_file.c | 3 | Take an 8192-byte game DB image - IAM\START for a new game, a save slot otherwise - and make it the live state. |
| `0x0040DE60` | `Global_Load` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040E060` | `Game_NewGame` | named | 01_file.c | 1 | Reset the session, load IAM\START over a freshly zeroed game DB and apply it. |
| `0x0040E140` | `Game_FreeState` | named | 01_file.c | 1 | name established, body still as generated |
| `0x0040E5E0` | `Address_Find` | named | 01_file.c | 0 | name established, body still as generated |
| `0x0040EA50` | `Weapon_SlotForObject` | named | 01_file.c | 0 | name established, body still as generated |
| `0x0040EA80` | `Weapon_ObjectForSlot` | named | 01_file.c | 2 | name established, body still as generated |
| `0x00411940` | `File_LoadWhole` | named | 03_win32.c | 20 | name established, body still as generated |
| `0x00419740` | `Area_LoadSet` | named | 04_sys.c | 2 | name established, body still as generated |
| `0x00419CB0` | `Actor_LoadBankList` | named | 04_sys.c | 4 | name established, body still as generated |
| `0x00419E00` | `Actor_Player` | named | 04_sys.c | 27 | The actor index the player is currently controlling. |
| `0x00419E10` | `Player_SetActor` | named | 04_sys.c | 7 | name established, body still as generated |
| `0x0041A0A0` | `Actor_HoldObject` | named | 04_sys.c | 0 | name established, body still as generated |
| `0x0041A140` | `Actor_ReleaseObject` | named | 04_sys.c | 2 | name established, body still as generated |
| `0x0041A350` | `Actor_HeldObjectSlot` | named | 04_sys.c | 7 | name established, body still as generated |
| `0x0041A3B0` | `Fight_Engage` | named | 04_sys.c | 2 | name established, body still as generated |
| `0x0041A730` | `Actor_LoadModel` | named | 04_sys.c | 7 | name established, body still as generated |
| `0x0041B420` | `Area_LoadSliderTrack` | named | 04_sys.c | 2 | Load the area's slider circuit: `trajectoires\\<stem>.OPT`. |
| `0x0041B4E0` | `Area_LoadScx` | named | 04_sys.c | 2 | name established, body still as generated |
| `0x0041B5A0` | `Game_Start` | named | 04_sys.c | 7 | name established, body still as generated |
| `0x0041B6F0` | `Player_GoToMove` | named | 04_sys.c | 0 | Put the player into one of the moves in its animation bank. |
| `0x0041BA80` | `ScriptObject_StartOnActor` | named | 04_sys.c | 0 | name established, body still as generated |
| `0x0041BD10` | `Actor_HoldAnimationById` | named | 04_sys.c | 0 | name established, body still as generated |
| `0x0041BDF0` | `Actor_SetPlacement` | named | 04_sys.c | 4 | Put an actor at a position and facing. |
| `0x0041BF50` | `Actor_MoveToAddress` | named | 04_sys.c | 0 | Teleport an actor onto an ADDRESSES entry - the worker behind VM opcode 73, `actor.goto_address`. |
| `0x0041C270` | `Actor_GetPosAndFacing` | named | 04_sys.c | 48 | name established, body still as generated |
| `0x0041C300` | `Actor_FromNode` | named | 04_sys.c | 6 | name established, body still as generated |
| `0x0041C330` | `Actor_ByIndex` | named | 04_sys.c | 49 | name established, body still as generated |
| `0x0041C350` | `Actor_Index` | named | 04_sys.c | 63 | name established, body still as generated |
| `0x0041C980` | `Object_Load` | named | 04_sys.c | 5 | name established, body still as generated |
| `0x0041CBE0` | `Object_ShowInScene` | named | 04_sys.c | 2 | name established, body still as generated |
| `0x0041CC20` | `Object_HideFromScene` | named | 04_sys.c | 0 | name established, body still as generated |
| `0x0041CC60` | `Actor_SetLookAt` | named | 04_sys.c | 0 | name established, body still as generated |
| `0x0041CCA0` | `Actor_Attach` | named | 04_sys.c | 7 | name established, body still as generated |
| `0x0041CDD0` | `Actor_Detach` | named | 04_sys.c | 1 | name established, body still as generated |
| `0x0041CE50` | `Hud_ShowValue` | named | 04_sys.c | 1 | name established, body still as generated |
| `0x0041CED0` | `Actor_SetHidden` | named | 04_sys.c | 0 | name established, body still as generated |
| `0x0041CF50` | `Object_SetPlacement` | named | 04_sys.c | 3 | name established, body still as generated |
| `0x0041D1A0` | `Area_LoadMiscModel` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041D6B0` | `Random_NoRepeat` | named | 05_sys.c | 0 | A random integer in [lo, hi] that is never the one drawn last time. |
| `0x0041D9C0` | `ScriptObject_HasCamEditing` | named | 05_sys.c | 0 | Does this scene script object have a camera editing linked to it? Returns 1 if any of the four bytes at +94..97 of the object record is non-zero. |
| `0x0041DC10` | `ScriptObject_Start` | named | 05_sys.c | 6 | Start a scene script object running. |
| `0x0041DEF0` | `UI_ScreenName` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041DF30` | `UI_OpenScreen` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041DFA0` | `Image_Show` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041E000` | `Screen_FadeToColor` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041E020` | `Screen_FadeFromColor` | named | 05_sys.c | 2 | name established, body still as generated |
| `0x0041E0E0` | `Music_SetVolumeRamp` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041E110` | `Music_PlayTrack` | named | 05_sys.c | 4 | name established, body still as generated |
| `0x0041E1B0` | `Screen_Fade` | named | 05_sys.c | 6 | name established, body still as generated |
| `0x0041E270` | `Timer_SetValue` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041E290` | `Timer_SetMode` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041E300` | `Timer_Format` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041E430` | `Timer_Elapsed` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x0041E600` | `Clock_Tick` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041E670` | `Clock_SetDay` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x0041E680` | `Clock_GetDay` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041E690` | `Clock_FormatDate` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041E6E0` | `Clock_FormatTime` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041E730` | `Clock_GetTimeOfDay` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041E740` | `Clock_GetTime` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x0041E750` | `Clock_SetTime` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x0041EFF0` | `Music_SetFadeMode` | named | 05_sys.c | 6 | name established, body still as generated |
| `0x0041F740` | `Game_Frame` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x0041FA00` | `Game_Init` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x00420000` | `Game_Shutdown` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x004200F0` | `Game_Tick` | named | 05_sys.c | 1 | The per-frame game tick, called by Game_Frame with the dt already in flt_4C30D8 (30 fps units; 0 while paused). |
| `0x00420AB0` | `Shoot_Think` | named | 05_sys.c | 4 | name established, body still as generated |
| `0x00421FB0` | `Shoot_InitWeapon` | named | 05_sys.c | 5 | name established, body still as generated |
| `0x004222D0` | `Shoot_Enter` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x00422730` | `Shoot_Leave` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x00422C10` | `Shoot_ActorEnter` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x00423170` | `Shoot_ActorAction` | named | 05_sys.c | 19 | name established, body still as generated |
| `0x00424880` | `Shoot_SyncHudHealth` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x004272B0` | `sub_4272B0` | read | 05_sys.c | 3 | Read, not changed: this is the behaviour selector - a 304-line switch that picks an animation for whatever the character is doing next. |
| `0x004279C0` | `Shoot_TickNpc` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x00427AC0` | `Shoot_TickPlayer` | named | 05_sys.c | 2 | name established, body still as generated |
| `0x004280D0` | `I2D_Shutdown` | named | 05_sys.c | 2 | name established, body still as generated |
| `0x00428430` | `I2D_DrawLine` | named | 05_sys.c | 5 | name established, body still as generated |
| `0x004284B0` | `I2D_Enqueue` | named | 05_sys.c | 9 | name established, body still as generated |
| `0x00428560` | `I2D_DrawTriangle` | named | 05_sys.c | 8 | name established, body still as generated |
| `0x004285E0` | `I2D_SubmitQuad` | named | 05_sys.c | 30 | name established, body still as generated |
| `0x00428780` | `I2D_BlitFullScreen` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x004287A0` | `I2D_BlitBitmap` | named | 05_sys.c | 13 | name established, body still as generated |
| `0x00428850` | `I2D_BlitSurface` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x00428900` | `I2D_Submit3DView` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x004289D0` | `I2D_GetBitmapSize` | named | 05_sys.c | 6 | name established, body still as generated |
| `0x00428A20` | `I2D_LoadBitmap` | named | 05_sys.c | 13 | name established, body still as generated |
| `0x00428A90` | `I2D_FreeBitmap` | named | 05_sys.c | 10 | name established, body still as generated |
| `0x00428B00` | `I2D_Flush` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x00428D80` | `I2D_ReloadBitmaps` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x00428DB0` | `I2D_CreateSurfaceFromBmp` | named | 05_sys.c | 2 | name established, body still as generated |
| `0x00428EA0` | `I2D_FlagBank` | named | 05_sys.c | 17 | name established, body still as generated |
| `0x00428EF0` | `Ui_ListSelectedItem` | named | 05_sys.c | 28 | name established, body still as generated |
| `0x00428F30` | `Ui_PanelCurrentList` | named | 05_sys.c | 4 | name established, body still as generated |
| `0x00428F50` | `Ui_PanelSelectedItem` | named | 05_sys.c | 0 | name established, body still as generated |
| `0x00428F90` | `I2D_TestFlag` | named | 05_sys.c | 70 | name established, body still as generated |
| `0x00428FF0` | `I2D_SetFlag` | named | 05_sys.c | 66 | name established, body still as generated |
| `0x00429080` | `Ui_TestListFlag` | named | 05_sys.c | 4 | name established, body still as generated |
| `0x004290D0` | `Ui_SetListFlag` | named | 05_sys.c | 33 | name established, body still as generated |
| `0x00429140` | `I2D_SetFlagOnAllRows` | named | 05_sys.c | 6 | name established, body still as generated |
| `0x004291E0` | `Ui_TestPanelFlag` | named | 05_sys.c | 14 | name established, body still as generated |
| `0x00429230` | `Ui_SetPanelFlag` | named | 05_sys.c | 6 | name established, body still as generated |
| `0x00429320` | `UI_ScreenBySlot` | named | 05_sys.c | 1 | name established, body still as generated |
| `0x00429340` | `UI_FindScreen` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x004293B0` | `UI_CountOpenScreens` | named | 05_sys.c | 2 | name established, body still as generated |
| `0x004293D0` | `UI_FindOpenScreen` | named | 05_sys.c | 3 | name established, body still as generated |
| `0x00429400` | `UI_TestScreenFlag` | named | 05_sys.c | 9 | name established, body still as generated |
| `0x00429450` | `UI_SetScreenFlag` | named | 05_sys.c | 13 | name established, body still as generated |
| `0x00429700` | `I2D_ScaleX` | named | 06_sys.c | 51 | name established, body still as generated |
| `0x00429730` | `I2D_ScaleY` | named | 06_sys.c | 55 | name established, body still as generated |
| `0x004297C0` | `Ui_ItemScreenX` | named | 06_sys.c | 9 | name established, body still as generated |
| `0x004297E0` | `Ui_ItemScreenY` | named | 06_sys.c | 9 | name established, body still as generated |
| `0x00429800` | `UI_ResetScreenSlots` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x00429880` | `UI_LoadScreenText` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x00429950` | `UI_BindScreenSounds` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x00429B40` | `UI_FocusScreen` | named | 06_sys.c | 3 | name established, body still as generated |
| `0x00429BB0` | `UI_LoadScreen` | named | 06_sys.c | 5 | Open one of the 37 interface screens. |
| `0x00429F10` | `UI_CloseScreen` | named | 06_sys.c | 4 | name established, body still as generated |
| `0x00429F40` | `UI_TickScreens` | named | 06_sys.c | 1 | The interface state machine: one tick over the three screen slots. |
| `0x0042A050` | `Ui_BeginScreen` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x0042A0F0` | `Ui_ScreenInput` | named | 06_sys.c | 0 | The interface's input callback - and it is the SAME one for every screen. |
| `0x0042A150` | `Ui_CloseScreenDefault` | named | 06_sys.c | 1 | The generic screen close - and the end of the ui.open loop. |
| `0x0042A340` | `UI_CloseAllScreens` | named | 06_sys.c | 4 | name established, body still as generated |
| `0x0042A370` | `Ui_GoToPanel` | named | 06_sys.c | 6 | name established, body still as generated |
| `0x0042A430` | `Ui_DispatchInput` | named | 06_sys.c | 1 | Dispatch one frame of input down the widget tree. |
| `0x0042A5C0` | `Ui_MoveBetweenLists` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x0042A710` | `Ui_MoveListsLeftRight` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x0042A750` | `Ui_ConfirmSelection` | named | 06_sys.c | 4 | Confirm: activate the selected item. |
| `0x0042A7E0` | `Ui_MoveSelection` | named | 06_sys.c | 3 | The default list input: move the selection, or fall through to confirm. |
| `0x0042A910` | `Ui_MoveSelectionVertical` | named | 06_sys.c | 2 | name established, body still as generated |
| `0x0042A930` | `Ui_MoveSelectionHorizontal` | named | 06_sys.c | 1 | name established, body still as generated |
| `0x0042B560` | `UI_SendAnswer` | named | 06_sys.c | 1 | Close the open screen by handing its answer back to the waiting script. |
| `0x0042B5E0` | `Ui_Oscillator` | named | 06_sys.c | 13 | name established, body still as generated |
| `0x0042B5F0` | `Ui_OscillatorFlags` | named | 06_sys.c | 4 | name established, body still as generated |
| `0x0042B820` | `Ui_StartOscillator` | named | 06_sys.c | 7 | name established, body still as generated |
| `0x0042BE60` | `Music_SetVolume` | named | 07_thread.c | 1 | name established, body still as generated |
| `0x0042EE70` | `Ambience_Load` | named | 08_wave.c | 1 | name established, body still as generated |
| `0x0042FF80` | `sub_42FF80` | read | 08_wave.c | 0 | The SECOND bucket walk - entry 1 of the renderer vtable (off_4C4918 = { Render_FlushBuckets, sub_42FF80 }), and it is NEVER INSTALLED in the… |
| `0x004316C0` | `Zones_Clear` | named | 08_wave.c | 3 | name established, body still as generated |
| `0x004317C0` | `Zone_Add` | named | 08_wave.c | 2 | name established, body still as generated |
| `0x004345E0` | `List_PickRandomByType` | read | 09_ddraw.c | 103 | Read, not changed: the engine never asks for an animation by name - this collects every clip whose type (node+0) matches and returns a random one,… |
| `0x00434630` | `sub_434630` | read | 09_ddraw.c | 14 | Read, not changed: a by-id lookup over the same clip list, matching the field at +4 rather than the type at +0. |
| `0x00434E30` | `Map2D_Load` | named | 10_dsound.c | 1 | name established, body still as generated |
| `0x004372D0` | `sub_4372D0` |  | 10_dsound.c | 14 | Translate a compact 11-bit flag word into the engine's 32-bit node flags and either clear them on one node or apply them down a subtree. |
| `0x00438740` | `Window_Create` | named | 12_win32.c | 1 | name established, body still as generated |
| `0x00439310` | `Game_RunLoop` | named | 12_win32.c | 1 | name established, body still as generated |
| `0x00439470` | `Game_Main` | named | 12_win32.c | 0 | name established, body still as generated |
| `0x0043B4E0` | `Movie_Play` | named | 13_ddraw.c | 4 | name established, body still as generated |
| `0x0043E0D0` | `Input_Poll` | named | 15_dinput.c | 4 | name established, body still as generated |
| `0x0043E360` | `Input_ReadOneControl` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x0043E830` | `Input_SetUiKeyBinding` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x0043E840` | `Input_SetUiKeyBinding2` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x0043E850` | `Input_SetUiKeyBinding3` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x0043FFC0` | `Font_LoadAll` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x00440080` | `Font_Find` | named | 15_dinput.c | 4 | name established, body still as generated |
| `0x004406B0` | `SetMaterialsMemory` | named | 15_dinput.c | 1 | name established, body still as generated |
| `0x00440AF0` | `Color_Sum` | named | 16_o3de.c | 29 | name established, body still as generated |
| `0x00440C80` | `sub_440C80` |  | 16_o3de.c | 33 | Walk a node and its children, applying sub_4942A0 to each. |
| `0x00441030` | `Render_Frame` | named | 16_o3de.c | 2 | name established, body still as generated |
| `0x00441840` | `Materials_ReleaseSlots` | named | 16_o3de.c | 2 | name established, body still as generated |
| `0x004433B0` | `World_ProbePoint` | named | 16_o3de.c | 34 | name established, body still as generated |
| `0x00444360` | `Collision_BodySphere` | named | 16_o3de.c | 3 | name established, body still as generated |
| `0x00444F60` | `World_Raycast` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x00445160` | `Fight_UpdateHealthBars` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x004455B0` | `Fight_Begin` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x00446500` | `Fight_TickCamera` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x00447A50` | `Hud_ScaleX` | named | 16_o3de.c | 67 | name established, body still as generated |
| `0x00447AB0` | `Hud_ScaleY` | named | 16_o3de.c | 73 | name established, body still as generated |
| `0x00447B10` | `Hud_DrawBar` | named | 16_o3de.c | 8 | name established, body still as generated |
| `0x00448FA0` | `Hud_Refresh` | named | 16_o3de.c | 4 | name established, body still as generated |
| `0x004490D0` | `Hud_LoadResources` | named | 16_o3de.c | 1 | name established, body still as generated |
| `0x0044A0F0` | `sub_44A0F0` | read | 16_o3de.c | 3 | Read, not changed: it walks a scene's object array (stride 100 from +12, count at +8), which is what confirmed those two Scene fields while… |
| `0x0044AAA0` | `ScriptObject_IsBusy` | named | 16_o3de.c | 2 | name established, body still as generated |
| `0x0044B270` | `ScriptObject_Scene` | named | 16_o3de.c | 61 | name established, body still as generated |
| `0x0044B850` | `ObjectTable_Name` | named | 16_o3de.c | 31 | name established, body still as generated |
| `0x0044B8B0` | `ObjectTable_Cached` | named | 16_o3de.c | 31 | name established, body still as generated |
| `0x0044B8D0` | `ObjectTable_SetCached` | named | 16_o3de.c | 28 | name established, body still as generated |
| `0x0044B960` | `ObjectTable_CameraName` | named | 16_o3de.c | 5 | name established, body still as generated |
| `0x0044B9C0` | `ObjectTable_CachedCamera` | named | 16_o3de.c | 5 | name established, body still as generated |
| `0x0044B9E0` | `ObjectTable_SetCachedCamera` | named | 16_o3de.c | 5 | name established, body still as generated |
| `0x0044CD40` | `ScriptObject_Id` | named | 17_script.c | 2 | The id of a script object - the int16 at +26 that Scene_FindScriptObject matches on. |
| `0x0044D110` | `Actor_TickProjectiles` | named | 17_script.c | 3 | name established, body still as generated |
| `0x0044D930` | `Projectiles_Tick` | named | 17_script.c | 1 | name established, body still as generated |
| `0x0044DF10` | `Read3DO_Init` | named | 18_d3d.c | 3 | name established, body still as generated |
| `0x0044F630` | `Sfx_LoadFile` | named | 18_d3d.c | 1 | name established, body still as generated |
| `0x0044F840` | `Sfx_BindAmbientEffects` | named | 18_d3d.c | 1 | name established, body still as generated |
| `0x0044FC30` | `Sound_TickEmitters` | named | 18_d3d.c | 1 | name established, body still as generated |
| `0x00450070` | `SetPiece_Find` | named | 18_d3d.c | 1 | name established, body still as generated |
| `0x00451340` | `SetPiece_Show` | named | 18_d3d.c | 5 | name established, body still as generated |
| `0x00451DC0` | `Screen_StartColorFade` | named | 18_d3d.c | 2 | name established, body still as generated |
| `0x00453450` | `Slider_Init` | named | 18_d3d.c | 1 | name established, body still as generated |
| `0x00454BB0` | `Sliders_Tick` | named | 18_d3d.c | 1 | name established, body still as generated |
| `0x00457030` | `sub_457030` |  | 19_dsound.c | 2 | The actor index used when a caller passes -1 for "whoever is current". |
| `0x00458150` | `Slider_TickRide` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045A3E0` | `Perso_SetInputEnabled` | named | 19_dsound.c | 3 | name established, body still as generated |
| `0x0045A470` | `Perso_SetNoPlayback` | named | 19_dsound.c | 3 | name established, body still as generated |
| `0x0045A510` | `SetPersoBank` | named | 19_dsound.c | 16 | name established, body still as generated |
| `0x0045A630` | `SetPersoBankGroup` | named | 19_dsound.c | 29 | name established, body still as generated |
| `0x0045A700` | `SetPersoBankList` | named | 19_dsound.c | 3 | name established, body still as generated |
| `0x0045A9F0` | `Perso_InjectInput` | named | 19_dsound.c | 20 | name established, body still as generated |
| `0x0045ACB0` | `Perso_GetInputEnabled` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045ADB0` | `Effects_SetScene` | named | 19_dsound.c | 2 | name established, body still as generated |
| `0x0045ADE0` | `Cef_SetEffectDt` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045ADF0` | `Cef_TickEffects` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045B120` | `Actor_AttachPoint` | named | 19_dsound.c | 2 | name established, body still as generated |
| `0x0045B260` | `Cef_UpdateStateEffects` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045B3B0` | `Cef_SpawnEffect` | named | 19_dsound.c | 2 | name established, body still as generated |
| `0x0045B5A0` | `Cef_ClearEffects` | named | 19_dsound.c | 9 | name established, body still as generated |
| `0x0045BFF0` | `Input_InstallScheme` | named | 19_dsound.c | 11 | name established, body still as generated |
| `0x0045C1B0` | `Cef_ApplyTurn` | named | 19_dsound.c | 2 | name established, body still as generated |
| `0x0045C2F0` | `Cef_ApplyRootShift` | named | 19_dsound.c | 3 | name established, body still as generated |
| `0x0045C3B0` | `Actor_PlayClip` | named | 19_dsound.c | 3 | name established, body still as generated |
| `0x0045C510` | `Actor_BlendToClip` | named | 19_dsound.c | 2 | name established, body still as generated |
| `0x0045D0E0` | `Cef_QueueSpecialMove` | named | 19_dsound.c | 3 | name established, body still as generated |
| `0x0045D1F0` | `Cef_LoadClip` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045D220` | `Actor_ClipFrames` | named | 19_dsound.c | 4 | name established, body still as generated |
| `0x0045D270` | `InitCEFFile` |  | 19_dsound.c | 1 | Load an ANIMS/*.CTL animation state machine. |
| `0x0045D970` | `LoadBankList` | named | 19_dsound.c | 2 | name established, body still as generated |
| `0x0045DCB0` | `Fight_FindAiProfile` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045DD00` | `Log_Error` | named | 19_dsound.c | 18 | name established, body still as generated |
| `0x0045DEB0` | `Cef_PushStateHistory` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045E110` | `SpatialIndex_Update` | named | 19_dsound.c | 9 | name established, body still as generated |
| `0x0045E190` | `SpatialIndex_Query` | named | 19_dsound.c | 1 | name established, body still as generated |
| `0x0045F450` | `D3D_AllocTextureSlots` | named | 20_ddraw.c | 2 | name established, body still as generated |
| `0x00460060` | `Render_FlushBuckets` | named | 20_ddraw.c | 0 | name established, body still as generated |
| `0x00460B80` | `Raster_DrawTriangles` | named | 20_ddraw.c | 2 | name established, body still as generated |
| `0x00461DB0` | `DDraw_ErrorString` | named | 21_d3d.c | 58 | name established, body still as generated |
| `0x004632A0` | `D3D_Fatal` | named | 21_d3d.c | 77 | name established, body still as generated |
| `0x00464760` | `D3D_SetRenderState` | named | 21_d3d.c | 50 | name established, body still as generated |
| `0x004647C0` | `D3D_SetTextureStageState` | named | 21_d3d.c | 29 | name established, body still as generated |
| `0x00464830` | `Fight_TickAI` | named | 21_d3d.c | 1 | The fight AI: pick a move and press it. |
| `0x004652B0` | `Fight_SelectAiProfile` | named | 21_d3d.c | 1 | name established, body still as generated |
| `0x00465460` | `Walk_GroundResponse` | named | 21_d3d.c | 1 | name established, body still as generated |
| `0x00466580` | `Actor_TickNpc` |  | 21_d3d.c | 2 | The ordinary actor's frame - states 1 and the scripted/hit family (11..14), dispatched by Actors_TickAll. |
| `0x00466710` | `Actor_TickPlayerAndOpponent` |  | 21_d3d.c | 1 | ACTOR_STATE 2 - melee. |
| `0x00466840` | `Actor_TickShoot` | named | 21_d3d.c | 1 | name established, body still as generated |
| `0x00466950` | `Actor_TickDialogue` | named | 21_d3d.c | 1 | name established, body still as generated |
| `0x00466990` | `Actor_TickScxDriven` |  | 21_d3d.c | 1 | ACTOR_STATE 4: an SCX scene object owns this actor's body (slot [43] = +172 - "perso has no script assigned !"). |
| `0x00466A60` | `Actor_StartPendingScx` |  | 21_d3d.c | 2 | ACTOR_STATE 5: resume the scene object a spoken line interrupted. |
| `0x00466B00` | `Actor_TickChannelOnly` | named | 21_d3d.c | 1 | name established, body still as generated |
| `0x00466CC0` | `Actor_TickUiHeld` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00467030` | `Walk_ProbeGround` | named | 21_d3d.c | 4 | name established, body still as generated |
| `0x004672D0` | `Actor_ApplyMotion` |  | 21_d3d.c | 3 | Integrate one frame of an actor's motion and put him on the ground. |
| `0x00467770` | `Actor_ScanZones` |  | 21_d3d.c | 9 | Test the actor against the trigger zones at his position and raise the game events - the ZONES.TAG runtime. |
| `0x004681C0` | `Actors_TickAll` |  | 21_d3d.c | 1 | The per-frame update of every live actor - the heart of the character system. |
| `0x00468B50` | `Actor_SetHeadLook` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00468DA0` | `Actor_HoldAnimation` | named | 21_d3d.c | 5 | Hold an actor's animation at rest, or let it run again. |
| `0x00468DE0` | `Actor_EnterDialogueMode` |  | 21_d3d.c | 1 | Put an actor - in practice always the player, via Dialog_Begin - into dialogue mode (FILE_FORMATS 5b2). |
| `0x00468E80` | `Actor_LeaveDialogueMode` |  | 21_d3d.c | 1 | Undo Actor_EnterDialogueMode: the channel back to group 100 (locomotion), input re-enabled if it was on before, the parked state restored - except… |
| `0x004693E0` | `Actor_SetClip` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00469400` | `Actor_SetClipFrame` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00469420` | `Actor_SetEuler` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00469450` | `Actor_MoveBy` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00469500` | `Actor_SetPosition` | named | 21_d3d.c | 2 | name established, body still as generated |
| `0x00469580` | `Actor_Move` | named | 21_d3d.c | 9 | name established, body still as generated |
| `0x0046A020` | `Walk_ClampNormal` | named | 21_d3d.c | 3 | name established, body still as generated |
| `0x0046ACE0` | `Cef_FindGroupById` | named | 21_d3d.c | 33 | name established, body still as generated |
| `0x0046AD90` | `Cef_DefaultGroup` | named | 21_d3d.c | 4 | name established, body still as generated |
| `0x0046ADF0` | `Sneak_Start` | named | 21_d3d.c | 0 | name established, body still as generated |
| `0x0046CDC0` | `Sound_Play3D` | named | 22_dsound.c | 18 | name established, body still as generated |
| `0x0046DB80` | `Sfx_TickAmbient` | named | 22_dsound.c | 1 | name established, body still as generated |
| `0x0046E3A0` | `Sfx_RegisterEmitter` | named | 22_dsound.c | 7 | name established, body still as generated |
| `0x00475A50` | `Ui_DrawScreen` | named | 24_sys.c | 0 | Draw one interface screen: its panel, then every list on that panel. |
| `0x00476040` | `Ui_DrawPanelBack` | named | 24_sys.c | 2 | Draw a panel's background, either as one stretched bitmap or as a tile map. |
| `0x00476290` | `Ui_DrawPanelDim` | named | 24_sys.c | 2 | name established, body still as generated |
| `0x00476340` | `Ui_DrawList` | named | 24_sys.c | 2 | Draw one list: walk its items, maintain focus, and hand each to Ui_DrawItem. |
| `0x004764A0` | `Ui_DrawItem` | named | 24_sys.c | 1 | Draw one item: its text, then whichever decorations its flags ask for. |
| `0x004767E0` | `Ui_ScreenString` | named | 24_sys.c | 12 | name established, body still as generated |
| `0x00476860` | `Ui_ItemStringDefault` | named | 24_sys.c | 0 | name established, body still as generated |
| `0x004769A0` | `Ui_ItemTextStyle` | named | 24_sys.c | 2 | Turn an item's flags into a Text_DrawBlock parameter block. |
| `0x00476D60` | `Ui_ClipBlitRects` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x00476E60` | `Ui_DrawItemSprite` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x00476FE0` | `Ui_DrawItemFill` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x00477290` | `Ui_DrawItemArrows` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x004775C0` | `Ui_DrawItemMarker` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x004777A0` | `I2D_DrawRectOutline` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x00478400` | `Ui_MoveItemStraight` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x004784A0` | `Ui_MoveItemPerAxis` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x00478AE0` | `Ui_StartPanelSlide` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x00478BC0` | `Ui_SlidePanelFrom` | named | 24_sys.c | 19 | name established, body still as generated |
| `0x00478C40` | `I2D_PackColour` | named | 24_sys.c | 3 | name established, body still as generated |
| `0x00479920` | `Ui_DrawItemCursor` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x0047A6D0` | `Ui_BuildLoadPanel` | named | 24_sys.c | 0 | Build the start menu's "Charger une partie" panel - and the SAVE panel. |
| `0x0047ABA0` | `Ui_LoadPanelInput` | named | 24_sys.c | 0 | name established, body still as generated |
| `0x0047BEF0` | `Shoot_StartTargetScripts` | named | 24_sys.c | 1 | name established, body still as generated |
| `0x0047DB90` | `Cef_FindState` | named | 24_sys.c | 3 | name established, body still as generated |
| `0x0047DC40` | `Cef_FindEntryByCode` | named | 24_sys.c | 5 | name established, body still as generated |
| `0x0047DD00` | `Cef_DefaultClip` | named | 24_sys.c | 4 | name established, body still as generated |
| `0x0047DD40` | `Cef_DefaultEntry` | named | 24_sys.c | 4 | name established, body still as generated |
| `0x0047DE40` | `Cef_FindEntryByCodeGlobal` | named | 24_sys.c | 6 | name established, body still as generated |
| `0x00482F30` | `UI_CacheScreenSounds` | named | 25_sys.c | 1 | name established, body still as generated |
| `0x00484180` | `sub_484180` | read | 25_sys.c | 1 | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00485350` | `sub_485350` | read | 25_sys.c | 1 | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00486570` | `sub_486570` | read | 25_sys.c | 1 | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00487900` | `sub_487900` | read | 25_sys.c | 1 | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00488CD0` | `sub_488CD0` | read | 25_sys.c | 1 | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x0048A470` | `sub_48A470` | read | 25_sys.c | 1 | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x0048C790` | `Zone_Bounds` | named | 25_sys.c | 1 | name established, body still as generated |
| `0x0048C880` | `Zone_ContainsPoint` | named | 25_sys.c | 1 | name established, body still as generated |
| `0x0048EB80` | `Sprite_AllocPool` | named | 25_sys.c | 1 | name established, body still as generated |
| `0x0048EBF0` | `Sprite_SpawnInstance` | named | 25_sys.c | 3 | name established, body still as generated |
| `0x0048EC90` | `Sprite_ReleaseInstance` | named | 25_sys.c | 7 | name established, body still as generated |
| `0x0048ECE0` | `Sprite_LinkToScene` | named | 25_sys.c | 2 | name established, body still as generated |
| `0x0048ED30` | `Sprite_UnlinkFromScene` | named | 25_sys.c | 7 | name established, body still as generated |
| `0x0048EF10` | `Sprite_SetFrame` | named | 25_sys.c | 1 | name established, body still as generated |
| `0x0048FA50` | `Opt_ApplyResolution` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FA90` | `Opt_ReadResolution` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FB10` | `Opt_ReadClipDistance` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FBB0` | `Opt_ReadSkyDisplay` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FC10` | `Opt_ReadShadowDisplay` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FC70` | `Opt_ReadStreetActivity` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FCD0` | `Opt_ReadDetailLevel` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FD00` | `Opt_ApplyAccel3D` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0048FDE0` | `Opt_ReadAccel3D` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00490380` | `Opt_ApplyMouseSensitivity` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x004906A0` | `Opt_MarkRowChanged` | named | 26_ole.c | 9 | name established, body still as generated |
| `0x00490F90` | `Opt_BindRow` | named | 26_ole.c | 176 | Bind one of the options menu's sixteen row widgets to an item, and to the page that item leads to. |
| `0x004910B0` | `Opt_LayOutPage` | named | 26_ole.c | 10 | Space a page's non-empty rows down the screen, writing each one's y at +2. |
| `0x004913F0` | `Opt_PageVideo` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00491640` | `Opt_PageAudio` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00491810` | `Opt_PageOptions` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x004919E0` | `Opt_PageControls` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00491BB0` | `Opt_PageControlScheme` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00491DA0` | `Opt_PageKeysAdventure` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00492000` | `Opt_PageKeysSwim` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00492240` | `Opt_PageKeysShoot` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00492480` | `Opt_PageKeysFight` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x004926C0` | `Opt_PageMouse` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00492A60` | `Opt_SavePageSelection` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x00492B50` | `Opt_RebindKey` | named | 26_ole.c | 4 | Read a key or button for a keybinding row, and take it off whatever other row already had it. |
| `0x00492DA0` | `Opt_RowInput` | named | 26_ole.c | 0 | The options menu's row input - the live page's `+4` hook. |
| `0x004951C0` | `Render_SubmitMesh` | named | 26_ole.c | 3 | name established, body still as generated |
| `0x004969C0` | `Render_SubmitSprites` | named | 26_ole.c | 1 | name established, body still as generated |
| `0x00499FE0` | `Sweep_Point` | named | 26_ole.c | 1 | name established, body still as generated |
| `0x0049A550` | `Fight_FaceOpponent` | named | 26_ole.c | 5 | name established, body still as generated |
| `0x0049A610` | `Fight_KeepSeparation` | named | 26_ole.c | 1 | name established, body still as generated |
| `0x0049A960` | `Fight_ResolveHit` |  | 26_ole.c | 2 | Decide whether the attacker's current move hits the defender, and apply it. |
| `0x0049B1F0` | `Fight_ResolveBoth` | named | 26_ole.c | 1 | name established, body still as generated |
| `0x0049B220` | `Fight_RecordFrame` | named | 26_ole.c | 2 | name established, body still as generated |
| `0x0049B400` | `Ui_OpenSneakFamily` | named | 26_ole.c | 0 | name established, body still as generated |
| `0x0049B610` | `Ui_CloseSneakFamily` | named | 26_ole.c | 0 | The sneak family's close - the one close in the game that can REFUSE. `screen+4` distinguishes the three screens sharing this pair: 0 VIDEOPHONE,… |
| `0x0049ECE0` | `Cam_PlayEditing` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x0049EEF0` | `Cam_LoadCameraFile` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x0049FCA0` | `Path_Read3DP` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x004A0040` | `Path_ReadEmbedded` | named | 27_sys.c | 1 | name established, body still as generated |
| `0x004A6CF0` | `Tex3DT_BindMaterials` | named | 28_script.c | 2 | name established, body still as generated |
| `0x004A7970` | `Tex3DT_Load` | named | 28_script.c | 2 | name established, body still as generated |
| `0x004A7B80` | `GoToMove` |  | 28_script.c | 8 | Route a perso channel from state `from` into state `to`, starting the new clip at `startFrame`. |
| `0x004A8160` | `Cef_TickChannel` | named | 29_win32.c | 5 | name established, body still as generated |
| `0x004A8AD0` | `Cef_InputMatches` | named | 29_win32.c | 3 | name established, body still as generated |
| `0x004A8BD0` | `Cef_FindTransition` |  | 29_win32.c | 6 | Find the transition out of state `from` that the input `code` triggers. |
| `0x004A9AB0` | `Sweep_MeshFaces` | named | 29_win32.c | 1 | The broad phase INSIDE one mesh: which of its faces the swept sphere could possibly touch. |
| `0x004A9D30` | `Sweep_PolygonKernel` | named | 29_win32.c | 3 | The swept-sphere-against-polygon test itself - the earliest hit fraction into +136 and the surface normal into +260, which is what Sweep_ActorMove… |
| `0x004AD360` | `Sweep_ActorMove` | named | 29_win32.c | 1 | name established, body still as generated |
| `0x004AD460` | `Sweep_MeshTest` | named | 29_win32.c | 0 | name established, body still as generated |
| `0x004B00D0` | `UI_GridMenuInput` | named | 29_win32.c | 0 | The 7-slot menu widget: move a selection, and on confirm hand back an answer. |
| `0x004B0C60` | `Path_Duration` | named | 29_win32.c | 3 | name established, body still as generated |
| `0x004B0C70` | `Path_Sample` | named | 29_win32.c | 10 | name established, body still as generated |

## Processed but still carrying their address name

Nothing in the code establishes what these are for, and a wrong name is worse than none. The `status` column separates the ones whose bodies were rewritten from the ones that were only read.

| address | status | file | what it does |
|---|---|---|---|
| `0x004272B0` | read | 05_sys.c | Read, not changed: this is the behaviour selector - a 304-line switch that picks an animation for whatever the character is doing next. |
| `0x0042FF80` | read | 08_wave.c | The SECOND bucket walk - entry 1 of the renderer vtable (off_4C4918 = { Render_FlushBuckets, sub_42FF80 }), and it is NEVER INSTALLED in the… |
| `0x00434630` | read | 09_ddraw.c | Read, not changed: a by-id lookup over the same clip list, matching the field at +4 rather than the type at +0. |
| `0x004372D0` | clean | 10_dsound.c | Translate a compact 11-bit flag word into the engine's 32-bit node flags and either clear them on one node or apply them down a subtree. |
| `0x00440C80` | clean | 16_o3de.c | Walk a node and its children, applying sub_4942A0 to each. |
| `0x0044A0F0` | read | 16_o3de.c | Read, not changed: it walks a scene's object array (stride 100 from +12, count at +8), which is what confirmed those two Scene fields while… |
| `0x00457030` | clean | 19_dsound.c | The actor index used when a caller passes -1 for "whoever is current". |
| `0x00484180` | read | 25_sys.c | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00485350` | read | 25_sys.c | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00486570` | read | 25_sys.c | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00487900` | read | 25_sys.c | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x00488CD0` | read | 25_sys.c | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |
| `0x0048A470` | read | 25_sys.c | One of the six software-rasterizer triangle variants Raster_DrawTriangles (0x00460B80) dispatches to when the renderer runs without 3D hardware… |


## Where the names came from

* **Recovered from the binary's own debug strings** - the game prints messages
  naming its functions, e.g. `o3de_GetObjectByIndex Error : index too big !`.
  See `clean/SYMBOLS.md`.
* **Read out of the code** - `Adpcm_DecodeMono` from the step table and nibble
  handling, `Script_Run` from the opcode dispatch loop, `Var_Set` from the
  array it writes.
* **Inferred from use, and marked as such** in the function's comment where the
  evidence is thin.


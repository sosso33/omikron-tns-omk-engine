// SPDX-License-Identifier: GPL-3.0-or-later
// STAGING A CONVERSATION'S SPEAKER - where the character the cameras are
// looking at actually stands.
//
// A `DialogCamera` names an eye and an aim, and every camera of a line aims at
// the speaker. So the speaker is where those rays CONVERGE, and the repo's own
// `/dialog` viewer has staged conversations that way since 2026-08-27
// (`tools/omkdata.speaker_positions`): a least-squares closest point to the
// line cameras' rays, then dropped onto the walkable floor beneath it.
//
// **The anchor matters and it has bitten before.** CLAUDE.md 6 records the
// dialogue-staging saga at length: a camera solve names a spot on the GROUND,
// so the model's FEET go there - not its pelvis, and not its centre. Staging
// two speakers on one flat floor looks right under almost any rule, and it was
// a restaurant with a bench under one of them that finally showed which rule
// was correct. Nothing here is settled by a still frame.
//
// **What is NOT this.** `Script_SelectBodyAnimation` and its relative variant
// place a character from a scene clip's root key or an authored `.3DP` path,
// and those are the AUTHORED positions where a scene object drives the
// character. This is the fallback the viewer uses when nothing else names one,
// and for conversation 272 - whose speaker is shown by `character.show 310`
// rather than driven by a path - it is what there is.
//
// TIER 6, reconstructed and labelled: the convergence is arithmetic over the
// engine's own camera records, but which point of the model goes there is a
// reading, and `verify.py: engine speaker` differences it against
// `tools/omkdata.speaker_positions` rather than asserting it alone.
#pragma once

#include "o3de/collision.h"
#include "script/dialogue.h"

#include <optional>
#include <vector>

namespace omk {

struct SpeakerStage {
    float pos[3] = {0, 0, 0};   // where the FEET go
    float converge[3] = {0, 0, 0};   // where the rays met, before the drop
    int   rays = 0;
    float scatter = 0;          // mean distance from the rays, in units
    bool  onFloor = false;      // whether a walkable triangle was found
    bool  valid = false;
};

// One camera's ray, so the solve does not care which table it came from: a
// conversation's own cameras during a line, the WORLD cameras the script has
// set during the beat before one. Both aim at the character.
struct CameraRay { float eye[3]; float at[3]; };

// The least-squares closest point to a set of camera rays, dropped onto the
// walkable floor beneath it when a soup is supplied.
SpeakerStage stageRays(const std::vector<CameraRay>& rays,
                       const TriangleSoup* walkable);

SpeakerStage stageSpeaker(const std::vector<DialogCamera>& cams,
                          const std::vector<int>& ids,
                          const TriangleSoup* walkable);

// Every LINE camera a conversation's nodes name, which is the ray set the
// viewer solves over.
std::vector<int> lineCameraIds(const Conversation& c);

}  // namespace omk

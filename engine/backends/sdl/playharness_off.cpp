// SPDX-License-Identifier: GPL-3.0-or-later
// THE INSTRUMENTS, NOT BUILT - `playharness.cpp`'s methods as empty stubs,
// linked instead of it by a build made with `INSTRUMENTS=0`
// (`todo/play-split.md` S6). The harness flags still parse; here they do
// nothing, and the first setup step says so once if any was given.
#include "playframe.h"

void PlayState::harnessStateWrites() {
    if (moneyArg >= 0 || ringsArg >= 0 || clockArg >= 0 || !giveList.empty() || !varList.empty() || newWorld ||
        !zoneEnable.empty() || !zoneDisable.empty() || !sceneLoads.empty() || bankReject ||
        rideArg || saveSlotArg >= 0 || boardArg || fightArg >= 0 || callDialog >= 0 ||
        animHoldHarness || shootEndAt >= 0 || shootHealth >= 0 || aimAtSet || playerAtFrame >= 0 || playerAt2Frame >= 0 || astarothSoulsAt >= 0 || astarothHealth >= 0 || gandharHealth >= 0 || fightHealth >= 0 || foeAtSet ||
        !scxPlay.empty() || hideShow[0] >= 0 || gameRestartAt >= 0 || !opAt.empty() ||
        !flickerDir.empty() ||
        !snapsDir.empty())
        std::printf("instruments: not built (INSTRUMENTS=0) - the harness flags given are "
                    "parsed and ignored\n");
}
void PlayState::harnessNewWorld() {}
void PlayState::harnessBankReject() {}
void PlayState::harnessScriptForcing() {}
void PlayState::harnessRide() {}
void PlayState::harnessSaveSlot() {}
void PlayState::harnessHoldAndCall() {}
void PlayState::harnessShootEnd() {}
void PlayState::harnessBoard(float (&)[3], float (&)[3]) {}
void PlayState::harnessFight() {}
void PlayState::harnessShootHealth(std::int32_t&) {}
void PlayState::harnessAimAt(omk::RecordShot&) {}
void PlayState::harnessAstaroth() {}
void PlayState::harnessFoeAt(const float *&) {}
void PlayState::harnessFightHealth(omk::FightStats&) {}
void PlayState::harnessScxPlay() {}
void PlayState::harnessHideShow() {}
void PlayState::harnessGameRestart() {}
void PlayState::harnessFlickerNote(std::size_t&, std::size_t&, std::size_t&) {}
void PlayState::harnessSnaps() {}
void PlayState::screensFlicker() {}

// which of the two files this build linked - the run's first `build:` line
bool omkInstrumentsBuilt() { return false; }

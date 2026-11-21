#include <TiltedOnlinePCH.h>
#include "DialogueMovement.h"
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Misc/MenuTopicManager.h>
#include <common/TimedReferenceSet.h>
#include <cmath>

namespace DialogueMovement
{
namespace
{
TimedReferenceSet s_holds;
}

bool IsSupported() noexcept { return VersionDb::Get().GetLoadedVersionString() == "1.7.104.0"; }
bool Hold(uint32_t aFormId, uint32_t aDurationMs) noexcept
{
    if (!IsSupported() || aDurationMs == 0 || aDurationMs > 3000)
        return false;
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    return s_holds.Set(aFormId, now + aDurationMs, now);
}
void Release(uint32_t aFormId) noexcept { s_holds.Remove(aFormId); }
void Reset() noexcept { s_holds.Clear(); }

void StopIfHeld(Actor* apActor, float aDelta) noexcept
{
    if (!s_holds.Contains(apActor->formID, static_cast<uint32_t>(GetTickCount64())) ||
        !std::isfinite(aDelta) || aDelta <= 0.f || aDelta > 1.f || !apActor->currentProcess ||
        apActor->GetExtension()->IsPlayer() || apActor->GetExtension()->IsRemote() ||
        (apActor->actorState.flags1 & 0x1E00000u) != 0 || apActor->IsInCombat() || MenuTopicManager::IsPlayerDialogueSpeaker(apActor))
        return;
    // MCP: 1.7.104 ID37817 / 1406a6050 takes float in XMM1, stops the
    // movement agent and clears bMoving. Do not use SetRestrained: that changes
    // the persisted life state. The caller still executes native movement,
    // collision and animation updates; this is not a position/AI freeze.
    TP_THIS_FUNCTION(TStopMoving, void, Actor, float);
    POINTER_SKYRIMSE(TStopMoving, stopMoving, 37817);
    TiltedPhoques::ThisCall(stopMoving, apActor, aDelta);
}
}

#include <TiltedOnlinePCH.h>
#include "NativeActorPose.h"
#include <Actor.h>
#include <AI/AIProcess.h>
#include <Misc/MiddleProcess.h>
#include <ScopedReference.h>
#include <World.h>
#include <Games/ActorExtension.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <PlayerCharacter.h>
#include <atomic>

namespace NativeActorPose
{
namespace { uint32_t s_furnitureBudget{}; std::atomic<uint32_t> s_runner{}; std::atomic<bool> s_threadFailed{}; }
bool OnRunner() noexcept { return !s_threadFailed.load(std::memory_order_acquire) && s_runner.load(std::memory_order_acquire) == GetCurrentThreadId(); }
void BeginRunnerFrame() noexcept
{
    const uint32_t thread = GetCurrentThreadId();
    uint32_t expected{};
    if (!s_runner.compare_exchange_strong(expected, thread) && expected != thread) { s_threadFailed.store(true, std::memory_order_release); return; }
    if (!s_threadFailed.load(std::memory_order_acquire)) s_furnitureBudget = 1;
}
uint32_t GrabbedHandle() noexcept
{
    if (VersionDb::Get().GetLoadedVersionString() != "1.7.104.0" || !OnRunner()) return 0;
    auto* player = PlayerCharacter::Get();
    // MCP 140752630 writes/resolves the reference HANDLE at Player+8D8.
    // +8D0 is the spring-array count on this runtime, not a grabbed handle.
    return player ? *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(player) + 0x8D8) : 0;
}
bool IsRagdoll(Actor* apActor) noexcept
{
    if (!OnRunner() || !apActor || VersionDb::Get().GetLoadedVersionString() != "1.7.104.0") return false;
    // MCP 140687C20 / ID37491: native predicate includes controller/actor flags,
    // life and knock states. Do not substitute IsDead or guess a single bit.
    TP_THIS_FUNCTION(TIsRagdoll, bool, Actor);
    POINTER_SKYRIMSE(TIsRagdoll, isRagdoll, 37491);
    return TiltedPhoques::ThisCall(isRagdoll, apActor);
}
ActorPoseState Capture(Actor* apActor) noexcept
{
    ActorPoseState state;
    if (!OnRunner() || !apActor || VersionDb::Get().GetLoadedVersionString() != "1.7.104.0") return state;
    const uint32_t flags = apActor->actorState.flags1;
    // MCP GetSitState/GetSleepState registration and exact callback bytes at
    // 140A01200/140A01260 verify Actor+C8 and mask 3C000. Native ragdoll
    // predicate 140687C20 verifies life 1E00000 and knock E000000.
    state.Known = true; state.Ragdoll = IsRagdoll(apActor);
    state.SitSleep = (flags >> 14) & 15; state.Knock = (flags >> 25) & 7; state.Life = (flags >> 21) & 15;
    if (state.SitSleep && apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        const auto* process = apActor->currentProcess->middleProcess;
        // MCP 14071FEC0 reads/clears this handle and marker while releasing the
        // reservation. Capture only; never assign these fields to fake a bind.
        ScopedReference furniture(process->occupiedFurniture.handle.iBits);
        if (furniture && !furniture.Get()->IsTemporary() &&
            World::Get().GetModSystem().GetServerModId(furniture.Get()->formID, state.Furniture))
            state.FurnitureMarker = process->furnitureMarker;
    }
    return state.IsValid() ? state : ActorPoseState{};
}
FurnitureResult ApplyFurniture(Actor* apActor, const ActorPoseState& acState) noexcept
{
    if (!OnRunner() || !acState.Known || !acState.IsValid() || !apActor || VersionDb::Get().GetLoadedVersionString() != "1.7.104.0") return FurnitureResult::Deferred;
    if (!acState.SitSleep && !(apActor->actorState.flags1 & 0x3C000u)) return FurnitureResult::Matched;
    if (
        !apActor->GetExtension()->IsRemote() || acState.Ragdoll || acState.Knock ||
        acState.Life != 0 ||
        (acState.SitSleep != 0 && acState.SitSleep != 3 && acState.SitSleep != 7) ||
        !apActor->currentProcess || !apActor->currentProcess->middleProcess || !apActor->animationGraphHolder.IsReady() ||
        (apActor->flags1 & Actor::HAS_SCENE_DATA) || apActor->IsInCombat() || IsRagdoll(apActor)) return FurnitureResult::Deferred;
    const auto current = Capture(apActor);
    if (current.SitSleep == acState.SitSleep && current.Furniture == acState.Furniture && current.FurnitureMarker == acState.FurnitureMarker) return FurnitureResult::Matched;
    if (!current.SitSleep && !acState.SitSleep) return FurnitureResult::Matched;
    uint32_t handle{};
    // Static forms resolve through the mod mapping. Never use a FormID as a handle.
    TESObjectREFR* reference = acState.Furniture ? Cast<TESObjectREFR>(TESForm::GetById(World::Get().GetModSystem().GetGameId(acState.Furniture))) : nullptr;
    if (acState.SitSleep)
    {
        // MCP 1402FFA60 checks REFR+40, base type 28h before furniture flags.
        if (!reference || !reference->baseForm || reference->baseForm->formType != FormType::Furniture ||
            reference->IsDeleted() || !reference->GetNiNode() || acState.FurnitureMarker == UINT32_MAX ||
            !apActor->parentCell || !reference->parentCell || apActor->GetWorldSpace() != reference->GetWorldSpace() ||
            (!apActor->GetWorldSpace() && apActor->parentCell != reference->parentCell)) return FurnitureResult::Deferred;
        using TMarker = const void*(void*, uint32_t);
        POINTER_SKYRIMSE(TMarker, marker, 17408);
        // MCP 14026EB30 bounds-checks BSFurnitureMarker's real NIF array.
        if (!marker.Get()(reference->GetNiNode(), acState.FurnitureMarker)) return FurnitureResult::Deferred;
        handle = reference->GetHandle().handle.iBits;
        if (!handle) return FurnitureResult::Deferred;
    }
    if (!s_furnitureBudget) return FurnitureResult::Throttled;
    --s_furnitureBudget;
    if (reference && acState.SitSleep)
    {
        TP_THIS_FUNCTION(TReserve, bool, TESObjectREFR, uint32_t, Actor*, bool, bool);
        POINTER_SKYRIMSE(TReserve, reserve, 20159);
        // MCP 1402FF270: respect another occupant/reservation; never evict the
        // local player or another actor just to match a received seat ID.
        if (!TiltedPhoques::ThisCall(reserve, reference, acState.FurnitureMarker, apActor, true, false)) return FurnitureResult::Deferred;
    }
    TP_THIS_FUNCTION(TSetSitSleep, void, AIProcess, Actor*, uint32_t, uint32_t*, uint32_t);
    POINTER_SKYRIMSE(TSetSitSleep, setSitSleep, 39912);
    if (current.SitSleep && acState.SitSleep &&
        (current.Furniture != acState.Furniture || current.FurnitureMarker != acState.FurnitureMarker))
    {
        uint32_t empty{};
        // The native function branches on a changed sit state. Leaving the old
        // seat first releases its reservation; merely replacing a handle would
        // leak it when both old and new modes happen to be Sitting.
        TiltedPhoques::ThisCall(setSitSleep, apActor->currentProcess, apActor, uint32_t{0}, &empty, UINT32_MAX);
    }
    // MCP 1407246A0 performs native reservation, controller, animation and
    // sit/sleep transitions. It copies the handle; it does not retain this pointer.
    TiltedPhoques::ThisCall(setSitSleep, apActor->currentProcess, apActor, uint32_t(acState.SitSleep), &handle, acState.FurnitureMarker);
    const auto applied = Capture(apActor);
    return applied.SitSleep == acState.SitSleep && (!acState.SitSleep ||
        (applied.Furniture == acState.Furniture && applied.FurnitureMarker == acState.FurnitureMarker)) ? FurnitureResult::Matched : FurnitureResult::Deferred;
}
FurnitureResult BeginGetUp(Actor* apActor, const ActorPoseState& acState) noexcept
{
    if (!OnRunner() || !apActor || !acState.Known || !acState.IsValid() || acState.IsDriven() || acState.Life ||
        VersionDb::Get().GetLoadedVersionString() != "1.7.104.0" || !apActor->GetExtension()->IsRemote() ||
        !apActor->currentProcess || apActor->currentProcess->movementType || !apActor->currentProcess->middleProcess ||
        (apActor->flags1 & Actor::HAS_SCENE_DATA) || !apActor->animationGraphHolder.IsReady() ||
        GrabbedHandle() == apActor->GetHandle().handle.iBits) return FurnitureResult::Deferred;
    const auto local = Capture(apActor);
    if (!local.Known || local.Life || local.SitSleep) return FurnitureResult::Deferred;
    if (!local.Knock || local.Knock == 6) return FurnitureResult::Matched;
    if (!s_furnitureBudget) return FurnitureResult::Throttled;
    --s_furnitureBudget;
    // MCP 140691370 / ID37569, Actor/Character vtable DE: build the native
    // get-up package, retaining its own package/state checks. Do not clear
    // knock bits or call the destructive B0 ragdoll-reset path (14067D3A0).
    TP_THIS_FUNCTION(TGetUp, void, Actor);
    POINTER_SKYRIMSE(TGetUp, getUp, 37569);
    TiltedPhoques::ThisCall(getUp, apActor);
    return FurnitureResult::Deferred; // scheduling is not proof it stood up
}
}

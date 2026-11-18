#include <TiltedOnlinePCH.h>

#include <Systems/AnimationSystem.h>

#include <Games/Animation/TESActionData.h>
#include <Games/Animation/ActorMediator.h>

#include <Games/References.h>

#include <Forms/BGSAction.h>
#include <AI/AIProcess.h>
#include <Camera/PlayerCamera.h>
#include <Misc/MiddleProcess.h>
#include <AI/NativeActorPose.h>
#include <chrono>

#include <Messages/RequestActionEvents.h>
#include <Structs/Movement.h>

#include <Components.h>
#include <World.h>

#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

namespace
{
using AnimationClock = std::chrono::steady_clock;
AnimationClock::duration s_frameSpent{};
size_t s_frameActions{};
}
void AnimationSystem::BeginRunnerFrame() noexcept
{
    if (NativeActorPose::OnRunner()) { s_frameSpent = {}; s_frameActions = 0; }
}
bool AnimationSystem::HasBudget() noexcept
{
    return NativeActorPose::OnRunner() && s_frameActions < 16 && s_frameSpent < std::chrono::microseconds(1000);
}
void AnimationSystem::Update(World& aWorld, Actor* apActor, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    if (!apActor || !HasBudget()) return;
    auto& actions = aAnimationComponent.TimePoints;

    const auto started = std::chrono::steady_clock::now();
    struct Account
    {
        AnimationClock::time_point Started;
        ~Account() { s_frameSpent += AnimationClock::now() - Started; }
    } account{started};
    for (size_t processed = 0; processed < 4; ++processed)
    {
        const auto it = std::begin(actions);
        if (it == std::end(actions) || it->Tick > aTick || s_frameActions == 16 ||
            s_frameSpent + (AnimationClock::now() - started) >= std::chrono::microseconds(1000) ||
            (processed && std::chrono::steady_clock::now() - started >= std::chrono::microseconds(300))) break;
        // Check if animation graph is ready before attempting to play animations
        if (!apActor->animationGraphHolder.IsReady())
        {
            // Animation graph not ready, keep the action in queue and try again later
            return;
        }
        if (aAnimationComponent.ReplayCount > 0 && aAnimationComponent.ResetAnimationGraphForReplay)
        {
            apActor->animationGraphHolder.RevertAnimationGraphManager();
            aAnimationComponent.ResetAnimationGraphForReplay = false;
        }

        const auto& first = *it;

        const auto actionId = first.ActionId;
        const auto targetId = first.TargetId;

        const auto pAction = Cast<BGSAction>(TESForm::GetById(actionId));
        const auto pTarget = Cast<TESObjectREFR>(TESForm::GetById(targetId));

        // MCP-verified native life/knock and furniture bits must be changed by
        // the actual death/physics/furniture path. Replaying an older animation
        // must not revive a corpse, cancel get-up or fake an occupied seat.
        constexpr uint32_t managed = 0x1E00000u | 0xE000000u | 0x3C000u;
        if (VersionDb::Get().GetLoadedVersionString() == "1.7.104.0")
        {
            apActor->actorState.flags1 = (first.State1 & ~managed) | (apActor->actorState.flags1 & managed);
            apActor->actorState.flags2 = (first.State2 & ~0x10u) | (apActor->actorState.flags2 & 0x10u);
        }
        else { apActor->actorState.flags1 = first.State1; apActor->actorState.flags2 = first.State2; }

        if (!NativeActorPose::IsRagdoll(apActor)) apActor->LoadAnimationVariables(first.Variables);

        aAnimationComponent.LastRanAction = first;

        // Play the animation
        TESActionData actionData(first.Type & 0x3, apActor, pAction, pTarget);
        actionData.eventName = BSFixedString(first.EventName.c_str());
        actionData.idleForm = Cast<TESIdleForm>(TESForm::GetById(first.IdleId));
        actionData.someFlag = ((first.Type & 0x4) != 0) ? 1 : 0;

        const auto result = ActorMediator::Get()->ForceAction(&actionData);
        ++s_frameActions;

        if (aAnimationComponent.ReplayCount > 0)
            aAnimationComponent.ReplayCount--;

        actions.pop_front();
    }
}

void AnimationSystem::Setup(World& aWorld, const entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::Clean(World& aWorld, const entt::entity aEntity) noexcept
{
    if (aWorld.all_of<RemoteAnimationComponent>(aEntity))
        aWorld.remove<RemoteAnimationComponent>(aEntity);
}

void AnimationSystem::AddActionsForReplay(RemoteAnimationComponent& aAnimationComponent, const ActionReplayChain& acReplay) noexcept
{
    // A replay rebuilds past state, so it is due right away rather than when playback reaches the original ticks
    for (auto action : acReplay.Actions)
    {
        action.Tick = 0;
        aAnimationComponent.TimePoints.push_back(std::move(action));
    }

    aAnimationComponent.ReplayCount = acReplay.Actions.size();
    aAnimationComponent.ResetAnimationGraphForReplay = acReplay.ResetAnimationGraph;
}

void AnimationSystem::AddAction(RemoteAnimationComponent& aAnimationComponent, const std::string& acActionDiff) noexcept
{
    auto itor = std::begin(aAnimationComponent.TimePoints);
    const auto end = std::cend(aAnimationComponent.TimePoints);

    auto& lastProcessedAction = aAnimationComponent.LastProcessedAction;

    TiltedPhoques::ViewBuffer buffer((uint8_t*)acActionDiff.data(), acActionDiff.size());
    Buffer::Reader reader(&buffer);

    lastProcessedAction.ApplyDifferential(reader);

    aAnimationComponent.TimePoints.push_back(lastProcessedAction);
}

void AnimationSystem::SerializeMovement(World& aWorld, Actor* apActor, Movement& aMovement) noexcept
{
    if (const auto pCell = apActor->parentCell)
        aWorld.GetModSystem().GetServerModId(pCell->formID, aMovement.CellId.ModId, aMovement.CellId.BaseId);

    if (const auto pWorldSpace = apActor->GetWorldSpace())
        aWorld.GetModSystem().GetServerModId(pWorldSpace->formID, aMovement.WorldSpaceId.ModId, aMovement.WorldSpaceId.BaseId);

    aMovement.Position = apActor->position;
    aMovement.Pose = NativeActorPose::Capture(apActor);

    aMovement.Rotation.x = apActor->rotation.x;
    aMovement.Rotation.y = apActor->rotation.z;

    apActor->SaveAnimationVariables(aMovement.Variables);

    if (apActor->currentProcess && apActor->currentProcess->middleProcess)
    {
        aMovement.Direction = apActor->currentProcess->middleProcess->direction;

        static BSFixedString s_targetLocation("TargetLocation");
        NiPoint3 target{};

        if (apActor == PlayerCharacter::Get() && apActor->currentProcess->middleProcess->lookAt && PlayerCamera::Get()->IsThirdPerson() &&
            apActor->animationGraphHolder.GetVariableNiPoint3(&s_targetLocation, &target))
        {
            aMovement.HasLookAt = true;
            aMovement.LookOffset = target - apActor->GetLookingAtLocation();
        }
    }
}

void AnimationSystem::SerializeActions(RequestActionEvents& aMessage, LocalComponent& localComponent, LocalAnimationComponent& animationComponent) noexcept
{
    if (animationComponent.Actions.empty())
        return;

    localComponent.CurrentAction = animationComponent.Actions.back();

    aMessage.OwnershipEpochs[localComponent.Id] = localComponent.OwnershipEpoch;
    aMessage.Actions[localComponent.Id] = std::move(animationComponent.Actions);
    animationComponent.Actions.clear();
}

bool AnimationSystem::Serialize(World& aWorld, const ActionEvent& aActionEvent, const ActionEvent& aLastProcessedAction, std::string* apData)
{
    uint32_t actionBaseId = 0;
    uint32_t actionModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.ActionId, actionModId, actionBaseId))
        return false;

    uint32_t targetBaseId = 0;
    uint32_t targetModId = 0;
    if (!aWorld.GetModSystem().GetServerModId(aActionEvent.TargetId, targetModId, targetBaseId))
        return false;

    uint8_t scratch[1 << 14];
    TiltedPhoques::ViewBuffer buffer(scratch, std::size(scratch));
    Buffer::Writer writer(&buffer);
    aActionEvent.GenerateDifferential(aLastProcessedAction, writer);

    apData->assign(buffer.GetData(), buffer.GetData() + writer.Size());

    return true;
}

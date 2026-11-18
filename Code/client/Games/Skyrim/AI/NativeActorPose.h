#pragma once
#include <Structs/ActorPoseState.h>
struct Actor;
namespace NativeActorPose
{
bool IsRagdoll(Actor* apActor) noexcept;
uint32_t GrabbedHandle() noexcept;
ActorPoseState Capture(Actor* apActor) noexcept;
void BeginRunnerFrame() noexcept;
bool OnRunner() noexcept;
enum class FurnitureResult { Matched, Deferred, Throttled };
FurnitureResult ApplyFurniture(Actor* apActor, const ActorPoseState& acState) noexcept;
FurnitureResult BeginGetUp(Actor* apActor, const ActorPoseState& acState) noexcept;
}

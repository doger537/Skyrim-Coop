#pragma once
#include <array>
#include <cstdint>
#include <Structs/RigidBodyPose.h>
#include <Structs/GameId.h>

// Main-runner API for loose objects with one rigid body. No engine pointer is
// retained in a request/result. Ragdolls need a separate validated body graph.
namespace PhysicsReplication
{
using Pose = RigidBodyPose;
enum class Status : uint8_t { Captured, Applied, MissingReference, NotLoaded, UnsupportedBody, InvalidPose, FixedBody, Expired };
struct Result
{
    uint64_t Cookie{};
    uint32_t ReferenceHandle{};
    Pose Transform{};
    uint8_t PreviousMotionType{};
    Status State{Status::NotLoaded};
};
struct RigResult
{
    uint64_t Cookie{};
    uint64_t CapturedAt{}; // GetTickCount64, copied when capture actually ran
    uint32_t ReferenceHandle{};
    RigidBodyRig Rig;
    uint32_t Entity{};
    uint32_t OwnershipEpoch{};
    GameId Cell{};
    GameId WorldSpace{};
    bool Interaction{};
    Status State{Status::NotLoaded};
};

void BeginRunnerFrame() noexcept;
bool QueueCapture(uint32_t aReferenceHandle, uint64_t aCookie) noexcept;
bool QueueApply(uint32_t aReferenceHandle, uint64_t aCookie, const Pose& acPose, uint8_t aMotionType = 4) noexcept;
bool Poll(Result& aResult) noexcept;
bool QueueRigCapture(uint32_t aReferenceHandle, uint64_t aCookie, uint32_t aEntity, uint32_t aEpoch) noexcept;
bool QueueRigApply(uint32_t aReferenceHandle, uint64_t aCookie, const RigidBodyRig& acRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell, GameId aWorld) noexcept;
bool QueueRigInteractionCapture(uint32_t aReferenceHandle, uint64_t aCookie, uint32_t aEntity, uint32_t aEpoch) noexcept;
bool QueueRigAuthoritativeInput(uint32_t aReferenceHandle, uint64_t aCookie, const RigidBodyRig& acRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell, GameId aWorld) noexcept;
bool PollRig(RigResult& aResult) noexcept;
void Invalidate() noexcept; // atomic-only native load barrier
void Reset() noexcept;
} // namespace PhysicsReplication

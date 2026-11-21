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
enum class Status : uint8_t
{
    Captured,
    Applied,
    MissingReference,
    NotLoaded,
    UnsupportedBody,
    InvalidPose,
    FixedBody,
    Expired,
    ReadbackMismatch
};
struct Result
{
    uint64_t Cookie{};
    uint64_t CapturedAt{};
    uint32_t ReferenceHandle{};
    Pose Transform{};
    uint8_t PreviousMotionType{};
    Status State{Status::NotLoaded};
};
struct RigResult
{
    uint8_t Channel{}; // 0 actor playback, 1 placed objects, 2 paused checkpoint
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
using Validator = bool (*)(uint32_t, uint64_t) noexcept;
bool QueueCapture(uint32_t aReferenceHandle, uint64_t aCookie, Validator aValidate = nullptr) noexcept;
// Motion type zero preserves the native dynamic body's mode.
bool QueueApply(uint32_t aReferenceHandle, uint64_t aCookie, const Pose& acPose, uint8_t aMotionType = 4, Validator aValidate = nullptr) noexcept;
bool Poll(Result& aResult) noexcept;
bool QueueRigCapture(uint32_t aReferenceHandle, uint64_t aCookie, uint32_t aEntity, uint32_t aEpoch) noexcept;
bool QueueRigApply(uint32_t aReferenceHandle, uint64_t aCookie, const RigidBodyRig& acRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell, GameId aWorld) noexcept;
bool QueueRigInteractionCapture(uint32_t aReferenceHandle, uint64_t aCookie, uint32_t aEntity, uint32_t aEpoch) noexcept;
bool QueueRigAuthoritativeInput(uint32_t aReferenceHandle, uint64_t aCookie, const RigidBodyRig& acRig, uint32_t aEntity, uint32_t aEpoch, GameId aCell, GameId aWorld) noexcept;
bool PollRig(RigResult& aResult, uint8_t aChannel = 0) noexcept;
bool QueueObjectCapture(uint32_t aHandle, uint64_t aCookie, uint8_t aChannel, Validator aValidate) noexcept;
bool QueueObjectApply(uint32_t aHandle, uint64_t aCookie, uint8_t aChannel, const RigidBodyRig& acRig, GameId aCell, GameId aWorld, Validator aValidate) noexcept;
// Only while both checkpoint clients are paused. Exact native units, with
// checked readback; one native TryLockForWrite attempt, never sleep/spin.
void PumpPaused(GameId aCell) noexcept;
void Invalidate() noexcept; // atomic-only native load barrier
void Reset() noexcept;
} // namespace PhysicsReplication

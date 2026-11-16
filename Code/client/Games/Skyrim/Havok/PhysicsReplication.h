#pragma once
#include <array>
#include <cstdint>

// Main-runner API for loose objects with one rigid body. No engine pointer is
// retained in a request/result. Ragdolls need a separate validated body graph.
namespace PhysicsReplication
{
struct Pose
{
    std::array<float, 3> Position{};
    std::array<float, 4> Rotation{1.f, 0.f, 0.f, 0.f}; // WXYZ, world space
    std::array<float, 3> LinearVelocity{};             // Skyrim units / second
    std::array<float, 3> AngularVelocity{};            // radians / second
};
enum class Status : uint8_t { Captured, Applied, MissingReference, NotLoaded, UnsupportedBody, InvalidPose, FixedBody, Expired };
struct Result
{
    uint64_t Cookie{};
    uint32_t ReferenceHandle{};
    Pose Transform{};
    uint8_t PreviousMotionType{};
    Status State{Status::NotLoaded};
};

void BeginRunnerFrame() noexcept;
bool QueueCapture(uint32_t aReferenceHandle, uint64_t aCookie) noexcept;
bool QueueApply(uint32_t aReferenceHandle, uint64_t aCookie, const Pose& acPose, uint8_t aMotionType = 4) noexcept;
bool Poll(Result& aResult) noexcept;
void Reset() noexcept;
} // namespace PhysicsReplication

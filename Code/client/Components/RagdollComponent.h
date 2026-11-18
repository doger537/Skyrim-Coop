#pragma once
#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif
#include <Structs/RagdollSnapshot.h>
#include <Systems/PlaybackClock.h>
#include <optional>

struct RagdollComponent
{
    uint32_t OwnershipEpoch{};
    uint64_t CaptureCookie{}, ApplyCookie{}, CaptureDeadline{}, ApplyDeadline{}, NextCapture{}, NextApply{}, ReceivedAt{};
    uint64_t Sequence{}, Tick{};
    PlaybackClock Clock;
    std::array<RagdollSnapshot, 4> Frames{};
    size_t Count{};
    std::optional<RagdollSnapshot> Input;
    uint64_t InputAt{}, InputTick{};
};

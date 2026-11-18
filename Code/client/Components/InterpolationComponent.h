#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/AnimationVariables.h>
#include <Systems/PlaybackClock.h>
#include <Structs/ActorPoseState.h>

struct InterpolationComponent
{
    struct TimePoint
    {
        // The owner's capture tick
        uint64_t Tick{};
        uint64_t Cell{};
        uint64_t WorldSpace{};
        glm::vec3 Position{};
        glm::vec3 Rotation{};
        AnimationVariables Variables{};
        float Direction{};
        bool HasLookAt{};
        glm::vec3 LookOffset{};
        ActorPoseState Pose{};

        TimePoint() = default;
    };

    List<TimePoint> TimePoints;
    // Last displayed position
    glm::vec3 Position{};

    PlaybackClock Clock;

    // Left over from correcting an extrapolation, decays over a few frames instead of popping the actor into place
    glm::vec3 PositionError{};
    bool IsExtrapolating{false};
    bool HasNewTimePoint{false};
    uint64_t FurnitureRetryAt{};
    uint64_t RecoveryRetryAt{}, RecoveryStableTick{};
};

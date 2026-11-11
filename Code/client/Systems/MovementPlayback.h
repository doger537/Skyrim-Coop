#pragma once

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Engine-independent position playback. Limits apply only to prediction, never to an owner's received position.
namespace MovementPlayback
{
constexpr double cMaxExtrapolation = 150.0;
constexpr double cMaxPredictionDistance = 256.0;
constexpr double cTeleportDistance = 1024.0;

inline bool IsFinite(const glm::vec3& acValue) noexcept
{
    return std::isfinite(acValue.x) && std::isfinite(acValue.y) && std::isfinite(acValue.z);
}

// Adjacent exterior cells share coordinates; interiors and different worlds do not.
inline bool IsLocationDiscontinuity(uint64_t aFirstWorld, uint64_t aFirstCell, uint64_t aSecondWorld, uint64_t aSecondCell) noexcept
{
    return aFirstWorld != aSecondWorld || (aSecondWorld == 0 && aFirstCell != aSecondCell);
}

struct Result
{
    glm::vec3 Position{};
    float Delta{1.f};
    bool IsTeleport{};
    bool IsExtrapolating{};
};

inline Result Sample(const glm::vec3& acFirst, const glm::vec3& acSecond, uint64_t aFirstTick, uint64_t aSecondTick, double aRenderTick, bool aDiscontinuity = false) noexcept
{
    Result result{acSecond};
    if (!IsFinite(acFirst) || !IsFinite(acSecond) || !std::isfinite(aRenderTick))
        return {IsFinite(acSecond) ? acSecond : glm::vec3{}};

    const double dx = static_cast<double>(acSecond.x) - acFirst.x;
    const double dy = static_cast<double>(acSecond.y) - acFirst.y;
    const double dz = static_cast<double>(acSecond.z) - acFirst.z;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    result.IsTeleport = aDiscontinuity || distance > cTeleportDistance;

    // Compare before subtracting unsigned ticks: reversed or equal ticks must not create a huge velocity.
    if (aSecondTick <= aFirstTick)
        return result;
    const double duration = static_cast<double>(aSecondTick - aFirstTick);
    if (aRenderTick <= static_cast<double>(aSecondTick))
    {
        result.Delta = static_cast<float>(std::clamp((aRenderTick - static_cast<double>(aFirstTick)) / duration, 0.0, 1.0));
        if (result.IsTeleport && result.Delta < 1.f)
            result.Delta = 0.f;
        // Use doubles for the intermediate subtraction so finite float coordinates cannot overflow it.
        result.Position = {acFirst.x + dx * result.Delta, acFirst.y + dy * result.Delta, acFirst.z + dz * result.Delta};
    }
    else if (!result.IsTeleport && distance > 0.0)
    {
        const double extraTime = std::min(aRenderTick - static_cast<double>(aSecondTick), cMaxExtrapolation);
        // A 1 ms pair must not launch a model hundreds of times the observed distance into the air.
        const double scale = std::min({extraTime / duration, 1.0, cMaxPredictionDistance / distance});
        result.Position = {acSecond.x + dx * scale, acSecond.y + dy * scale, acSecond.z + dz * scale};
        result.IsExtrapolating = scale > 0.0;
    }
    return result;
}
} // namespace MovementPlayback

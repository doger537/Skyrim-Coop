#include "WorldItemTimeline.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace
{
float DistanceSquared(const WorldItemMotion& acLeft, const WorldItemMotion& acRight) noexcept
{
    const glm::vec3 difference = acLeft.Position - acRight.Position;
    return glm::dot(difference, difference);
}
}

void WorldItemTimeline::Track::Rebase(const WorldItemMotion& acMotion) noexcept
{
    Current = acMotion;
    Start = 0;
    Count = 1;
    Samples[0] = acMotion;
}

void WorldItemTimeline::Track::Append(const WorldItemMotion& acMotion) noexcept
{
    if (Count == 0 || acMotion.Tick == Current.Tick || acMotion.CellId != Current.CellId ||
        acMotion.WorldSpaceId != Current.WorldSpaceId || acMotion.Mode != Current.Mode ||
        DistanceSquared(acMotion, Current) > 512.f * 512.f)
    {
        Rebase(acMotion);
        return;
    }
    if (Count == kHistory)
    {
        Samples[Start] = acMotion;
        Start = (Start + 1) % kHistory;
    }
    else
        Samples[(Start + Count++) % kHistory] = acMotion;
    Current = acMotion;
}

void WorldItemTimeline::Reset(uint64_t aWorldEpoch, uint64_t aHostEpoch) noexcept
{
    m_items.clear();
    m_worldEpoch = aWorldEpoch;
    m_hostEpoch = aHostEpoch;
}

bool WorldItemTimeline::CurrentEpoch(const WorldItemMotion& acMotion) const noexcept
{
    return m_worldEpoch != 0 && m_hostEpoch != 0 && acMotion.WorldEpoch == m_worldEpoch && acMotion.HostEpoch == m_hostEpoch;
}

WorldItemTimeline::Result WorldItemTimeline::Lifecycle(const WorldItemState& acState) noexcept
{
    if (!acState.IsValid())
        return Result::Invalid;
    if (!CurrentEpoch(acState))
        return Result::Stale;
    auto it = m_items.find(acState.Id);
    if (it == m_items.end())
    {
        if (m_items.size() >= kMaxItems)
            return Result::Capacity;
        Track track;
        track.Rebase(acState);
        track.Deleted = acState.Deleted;
        m_items.emplace(acState.Id, std::move(track));
        return acState.Deleted ? Result::Removed : Result::Created;
    }
    auto& track = it.value();
    if (acState.Revision < track.Current.Revision || (track.Deleted && !acState.Deleted))
        return Result::Stale;
    if (acState.Revision == track.Current.Revision)
    {
        if (track.Deleted != acState.Deleted)
            return Result::Invalid;
        if (track.Deleted || acState.Sequence <= track.Current.Sequence)
            return Result::Duplicate;
        return Motion(acState);
    }
    track.Rebase(acState);
    track.Deleted = acState.Deleted;
    return acState.Deleted ? Result::Removed : Result::Applied;
}

WorldItemTimeline::Result WorldItemTimeline::Motion(const WorldItemMotion& acMotion) noexcept
{
    if (!acMotion.IsValid())
        return Result::Invalid;
    if (!CurrentEpoch(acMotion))
        return Result::Stale;
    const auto it = m_items.find(acMotion.Id);
    if (it == m_items.end())
        return Result::Unknown;
    auto& track = it.value();
    if (track.Deleted || acMotion.Revision != track.Current.Revision || acMotion.Sequence <= track.Current.Sequence || acMotion.Tick < track.Current.Tick)
        return Result::Stale;
    track.Append(acMotion);
    return Result::Applied;
}

const WorldItemMotion* WorldItemTimeline::Latest(uint64_t aId) const noexcept
{
    const auto it = m_items.find(aId);
    return it == m_items.end() || it->second.Deleted ? nullptr : &it->second.Current;
}

bool WorldItemTimeline::Sample(uint64_t aId, uint64_t aRenderTick, WorldItemMotion& aMotion) const noexcept
{
    const auto it = m_items.find(aId);
    if (it == m_items.end() || it->second.Deleted)
        return false;
    const auto& track = it->second;
    aMotion = track.Current;
    if (aRenderTick >= track.Current.Tick)
    {
        if (track.Current.Mode != WorldItemMotion::Motion::Sleeping)
        {
            const float seconds = static_cast<float>(std::min(aRenderTick - track.Current.Tick, kMaxExtrapolationMs)) / 1000.f;
            aMotion.Position = track.Current.Position + glm::vec3(track.Current.LinearVelocity[0], track.Current.LinearVelocity[1], track.Current.LinearVelocity[2]) * seconds;
            if (!aMotion.IsValid())
                aMotion = track.Current;
        }
        return true;
    }
    aMotion = track.At(0);
    for (size_t i = 1; i < track.Count; ++i)
    {
        const auto& left = track.At(i - 1);
        const auto& right = track.At(i);
        if (aRenderTick > right.Tick)
            continue;
        if (aRenderTick < left.Tick)
            return true;
        const float alpha = static_cast<float>(aRenderTick - left.Tick) / static_cast<float>(right.Tick - left.Tick);
        aMotion = right;
        aMotion.Position = glm::mix(static_cast<const glm::vec3&>(left.Position), static_cast<const glm::vec3&>(right.Position), alpha);
        const auto& lr = left.Rotation;
        const auto& rr = right.Rotation;
        const glm::quat rotation = glm::normalize(glm::slerp(glm::normalize(glm::quat(lr[0], lr[1], lr[2], lr[3])), glm::normalize(glm::quat(rr[0], rr[1], rr[2], rr[3])), alpha));
        aMotion.Rotation = {rotation.w, rotation.x, rotation.y, rotation.z};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            aMotion.LinearVelocity[axis] = std::lerp(left.LinearVelocity[axis], right.LinearVelocity[axis], alpha);
            aMotion.AngularVelocity[axis] = std::lerp(left.AngularVelocity[axis], right.AngularVelocity[axis], alpha);
        }
        return true;
    }
    return true;
}

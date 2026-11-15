#pragma once

#include <Messages/NotifyActorTeleport.h>
#include <limits>
#include <optional>

namespace ActorTeleportAuthority
{
struct Target
{
    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    GameId FormId{};
    bool IsLocalOwner{};
};

// Like other owner deltas, a teleport can reveal a missed ownership epoch.
// It cannot introduce an actor, change its identity, or move our local owner copy.
inline bool CanApply(const NotifyActorTeleport& acMessage, const std::optional<Target>& acTarget) noexcept
{
    return acTarget && !acTarget->IsLocalOwner && acTarget->OwnershipEpoch != 0 && acMessage.OwnershipEpoch != 0 &&
           acMessage.OwnershipEpoch >= acTarget->OwnershipEpoch && acMessage.ServerId == acTarget->ServerId && acMessage.FormId != GameId{} &&
           acMessage.FormId == acTarget->FormId;
}

// Static references can outlive their network entity after leaving the active
// cells. Preserve their final placement, but never bypass a currently tracked
// local or remote incarnation of the same form. No old-epoch history remains
// for fully untracked references, so this fallback makes no ordering guarantee.
inline bool CanApplyUntracked(const NotifyActorTeleport& acMessage, bool aHasTrackedAuthorityForForm) noexcept
{
    return !aHasTrackedAuthorityForForm && acMessage.OwnershipEpoch != 0 && acMessage.FormId != GameId{} &&
           acMessage.FormId.ModId != std::numeric_limits<uint32_t>::max() && !(acMessage.FormId.ModId == 0 && acMessage.FormId.BaseId == 0x14);
}
} // namespace ActorTeleportAuthority

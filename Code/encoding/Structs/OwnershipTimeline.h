#pragma once
#include <cstdint>

namespace OwnershipTimeline
{
// Deltas may reveal a new epoch before its full transfer snapshot arrives.
// Receiving that snapshot once is still necessary, even if the epoch is already known.
inline bool ShouldApplyTransfer(uint32_t aCurrentEpoch, uint32_t aLastTransferEpoch, uint32_t aIncomingEpoch) noexcept
{
    return aIncomingEpoch != 0 && aIncomingEpoch >= aCurrentEpoch && aIncomingEpoch > aLastTransferEpoch;
}
} // namespace OwnershipTimeline

#pragma once

#include <cstdint>

namespace QuestSyncPolicy
{
// MQ101DragonAttack is a local scene controller. Its internal stages depend
// on the player's route through Helgen.
inline constexpr uint32_t kHelgenDragonAttack = 0x000D0593;
inline constexpr uint32_t kHelgenPrologue = 0x0003372B; // MQ101
inline constexpr uint16_t kHelgenEscapeStage = 900;
inline constexpr uint16_t kHelgenCompletionStage = 1000;

inline constexpr bool IsLocalScene(uint32_t aModId, uint32_t aBaseId) noexcept
{
    // The validated content manifest reserves server mod zero for Skyrim.esm.
    return aModId == 0 && aBaseId == kHelgenDragonAttack;
}
} // namespace QuestSyncPolicy

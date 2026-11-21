#pragma once

#include "QuestJournal.h"

namespace QuestSyncPolicy
{
enum class Category : uint8_t
{
    Local,
    Story,
    Radiant,
    Shared
};

struct Policy
{
    Category Type;
    uint16_t PreparationThrough{};
};

inline constexpr uint32_t kHelgenDragonAttack = 0x000D0593; // MQ101DragonAttack
inline constexpr uint32_t kHelgenPrologue = 0x0003372B; // MQ101
inline constexpr uint16_t kHelgenEscapeStage = 900;
inline constexpr uint16_t kHelgenCompletionStage = 1000;
inline constexpr uint32_t kHadvarRoute = 0x0002BF9C; // MQ102A
inline constexpr uint32_t kRalofRoute = 0x0002610A; // MQ102B
inline constexpr uint16_t kAlduinCleanupStage = 3;
inline constexpr uint16_t kRiverwoodSceneStage = 10;

Policy Classify(const GameId& acId, uint8_t aType) noexcept;
const char* CategoryName(Category aCategory) noexcept;
bool IsShared(Policy aPolicy, uint8_t aType, bool aAllowMisc) noexcept;
bool IsPreparation(Policy aPolicy, const TiltedPhoques::Vector<uint16_t>& acStages) noexcept;
bool IsPreparedImport(const QuestJournal& acJournal) noexcept;
bool IsLocalOnly(const QuestJournal& acJournal) noexcept;
bool IsPreparationEvent(Policy aPolicy, uint8_t aStatus, uint16_t aStage) noexcept;
bool IsHistoricalStage(const QuestJournal& acJournal, bool aLocalStopped, const TiltedPhoques::Vector<uint16_t>& acReached, uint16_t aStage) noexcept;
} // namespace QuestSyncPolicy

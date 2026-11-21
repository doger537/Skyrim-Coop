#include "QuestSyncPolicy.h"

#include <algorithm>
#include <iterator>

namespace QuestSyncPolicy
{
namespace
{
constexpr uint32_t kLocalQuests[]{
    kHelgenDragonAttack,
    0x0003AC44, // MS13BleakFallsBarrowLeverScene
    0x0002BA16, // PlayerWerewolfQuest
    0x000F2593, // WISkillIncrease02
};

// These quests share the CompanionsRadiantQuest.Setup -> stage 1
// preparation path. Stage 10 is the first player-facing objective.
constexpr uint32_t kCompanionsOffers[]{
    0x0001CEEE, 0x00025185, 0x00025231, 0x0002522F, 0x000C18E1, 0x00025250, 0x00025251, // CR01, CR02, CR04-CR08
};

// Audited MQ101 tutorial and scene history. Route selectors 8/9 and
// any stage outside this list remain exact even after Helgen.
constexpr uint16_t kHelgenHistory[]{258, 280, 315, 317, 318, 320, 340, 368, 380, 395, 470, 550, 560, 630, 700, 720, 725, 730, 750};

template <class T, size_t N>
bool Contains(const T (&acValues)[N], T aValue) noexcept
{
    return std::find(std::begin(acValues), std::end(acValues), aValue) != std::end(acValues);
}

bool BothReached(const QuestJournal& acJournal, const TiltedPhoques::Vector<uint16_t>& acReached, uint16_t aStage) noexcept
{
    return std::find(acReached.begin(), acReached.end(), aStage) != acReached.end() &&
           std::any_of(acJournal.Events.begin(), acJournal.Events.end(),
                       [aStage](const auto& event) { return event.Status == 0 && event.Stage == aStage; });
}
} // namespace

Policy Classify(const GameId& acId, uint8_t aType) noexcept
{
    // The validated manifest reserves server slot zero for Skyrim.esm.
    if (acId.ModId == 0)
    {
        if (Contains(kLocalQuests, acId.BaseId))
            return {Category::Local};
        if (aType == 5 && Contains(kCompanionsOffers, acId.BaseId))
            return {Category::Radiant, 1};
    }
    if (aType == 1 || aType == 9 || aType == 10 || aType == 11)
        return {Category::Story};
    return {Category::Shared};
}

const char* CategoryName(Category aCategory) noexcept
{
    switch (aCategory)
    {
    case Category::Local: return "local";
    case Category::Story: return "story";
    case Category::Radiant: return "radiant";
    case Category::Shared: return "shared";
    }
    return "unknown";
}

bool IsShared(Policy aPolicy, uint8_t aType, bool aAllowMisc) noexcept
{
    return aPolicy.Type != Category::Local && ((aType != 0 && aType != 6) || aAllowMisc);
}

bool IsPreparation(Policy aPolicy, const TiltedPhoques::Vector<uint16_t>& acStages) noexcept
{
    return aPolicy.Type == Category::Radiant &&
           std::all_of(acStages.begin(), acStages.end(), [&](uint16_t aStage) { return aStage <= aPolicy.PreparationThrough; });
}

bool IsPreparedImport(const QuestJournal& acJournal) noexcept
{
    const auto policy = Classify(acJournal.Id, acJournal.Type);
    return policy.Type == Category::Radiant && acJournal.Epoch == 1 && acJournal.Started &&
           acJournal.Decisions.empty() && !acJournal.Events.empty() &&
           std::all_of(acJournal.Events.begin(), acJournal.Events.end(),
                       [&](const auto& event) { return event.Bootstrap && event.Stage <= policy.PreparationThrough; });
}

bool IsLocalOnly(const QuestJournal& acJournal) noexcept
{
    return Classify(acJournal.Id, acJournal.Type).Type == Category::Local || IsPreparedImport(acJournal);
}

bool IsPreparationEvent(Policy aPolicy, uint8_t aStatus, uint16_t aStage) noexcept
{
    return aPolicy.Type == Category::Radiant && (aStatus == 1 || aStage <= aPolicy.PreparationThrough);
}

bool IsHistoricalStage(const QuestJournal& acJournal, bool aLocalStopped, const TiltedPhoques::Vector<uint16_t>& acReached, uint16_t aStage) noexcept
{
    if (acJournal.Id.ModId != 0 || acJournal.Type != 1 || acJournal.Epoch != 1 || !acJournal.Started ||
        acJournal.Stopped != aLocalStopped || !acJournal.Decisions.empty())
        return false;

    switch (acJournal.Id.BaseId)
    {
    case kHelgenPrologue:
        return aLocalStopped && Contains(kHelgenHistory, aStage) &&
               BothReached(acJournal, acReached, kHelgenEscapeStage) && BothReached(acJournal, acReached, kHelgenCompletionStage) &&
               std::all_of(acJournal.Events.begin(), acJournal.Events.end(), [](const auto& event) { return event.Bootstrap; });
    case kHadvarRoute:
    case kRalofRoute:
        // MQ101 sets this stage while leaving the bear cave. It cleans up
        // the intro Alduin reference; stage 10 records the actual route.
        return aStage == kAlduinCleanupStage && BothReached(acJournal, acReached, kRiverwoodSceneStage) &&
               std::none_of(acJournal.Events.begin(), acJournal.Events.end(), [&](const auto& event)
                            { return event.Stage == aStage && (event.Status != 0 || !event.Bootstrap); });
    default:
        return false;
    }
}
} // namespace QuestSyncPolicy

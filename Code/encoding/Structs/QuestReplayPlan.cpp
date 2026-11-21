#include "QuestReplayPlan.h"
#include "QuestSyncPolicy.h"
#include <algorithm>
#include <iterator>
#include <set>

bool NeedsSharedJournal(const GameId& acId, uint8_t aType, const LocalQuestState& acLocal) noexcept
{
    const auto policy = QuestSyncPolicy::Classify(acId, aType);
    return acLocal.Exists && (!acLocal.Stopped || !acLocal.Reached.empty()) &&
           policy.Type != QuestSyncPolicy::Category::Local && !QuestSyncPolicy::IsPreparation(policy, acLocal.Reached);
}

bool PlanQuestReplay(
    const QuestJournal& acJournal, const TiltedPhoques::Vector<uint16_t>& acReplayStages, bool aReplayLifecycle, const LocalQuestState& acLocal, QuestReplayPlan& aPlan) noexcept
{
    aPlan = {};
    const auto fail = [&](const char* apReason)
    {
        aPlan.Apply.clear();
        aPlan.Error = apReason;
        return false;
    };
    if (!acJournal.IsValid() || !acLocal.Exists)
        return fail("Shared quest is invalid or missing from the game");
    using namespace QuestSyncPolicy;
    const auto policy = Classify(acJournal.Id, acJournal.Type);
    if (policy.Type == Category::Local)
        return true;
    const bool localPreparation = IsPreparation(policy, acLocal.Reached);
    if (IsPreparedImport(acJournal) && localPreparation)
        return true;

    const bool radiantProgress = policy.Type == Category::Radiant && std::any_of(
        acJournal.Events.begin(), acJournal.Events.end(),
        [&](const auto& event) { return event.Status == 0 && event.Stage > policy.PreparationThrough; });
    if (radiantProgress && localPreparation)
        return fail("The campaign has an accepted radiant quest; the local save has no accepted progress");
    if (policy.Type == Category::Radiant)
        aReplayLifecycle = false;

    const auto localHistory = [&](uint16_t aStage)
    {
        return (radiantProgress && aStage <= policy.PreparationThrough) ||
               IsHistoricalStage(acJournal, acLocal.Stopped, acLocal.Reached, aStage);
    };
    std::set<uint16_t> expected;
    std::set<uint16_t> reached;
    for (uint16_t stage : acLocal.Reached)
        if (!localHistory(stage))
            reached.insert(stage);
    for (const auto& event : acJournal.Events)
        if (event.Status != 2 && !localHistory(event.Stage))
            expected.insert(event.Stage);
    for (const auto& decision : acJournal.Decisions)
        if (!expected.contains(decision.Stage) || !reached.contains(decision.Stage))
            return fail("A committed quest decision is missing from the local save");
    for (uint16_t stage : reached)
        if (!expected.contains(stage))
            return fail("Local quest contains progress outside the shared campaign; load a matching campaign save");
    const auto canReplay = [&](const auto& event)
    {
        return policy.Type != Category::Radiant && !event.Bootstrap && std::find(acReplayStages.begin(), acReplayStages.end(), event.Stage) != acReplayStages.end();
    };
    bool running = !acLocal.Stopped;
    const bool hasMissing = std::any_of(
        acJournal.Events.begin(), acJournal.Events.end(),
        [&](const auto& event) { return event.Status != 2 && !localHistory(event.Stage) && !(event.Status == 1 && event.Stage == 0) && !reached.contains(event.Stage); });
    if (!running && (!acJournal.Stopped || hasMissing))
    {
        if (!aReplayLifecycle || acJournal.Epoch != 1)
            return fail("Quest lifecycle differs; automatic start/restart is not approved for this quest");
        const auto start = std::find_if(acJournal.Events.begin(), acJournal.Events.end(), [](const auto& event) { return event.Status == 1; });
        if (start == acJournal.Events.end() || !canReplay(*start))
            return fail("Imported or unapproved quest start cannot be replayed");
        aPlan.Apply.push_back(*start);
        reached.insert(start->Stage);
        running = true;
    }
    for (const auto& event : acJournal.Events)
    {
        if (event.Status == 2 || localHistory(event.Stage) || reached.contains(event.Stage) || (event.Status == 1 && event.Stage == 0))
            continue;
        if (!canReplay(event))
            return fail("Missing quest stage is not approved for replay; load a matching campaign save");
        aPlan.Apply.push_back({0, event.Stage, false});
        reached.insert(event.Stage);
    }
    if (acJournal.Stopped && running)
    {
        const auto stop = std::find_if(acJournal.Events.rbegin(), acJournal.Events.rend(), [](const auto& event) { return event.Status == 2; });
        if (!aReplayLifecycle || acJournal.Epoch != 1 || stop == acJournal.Events.rend() || stop->Bootstrap)
            return fail("Quest stop differs and its lifecycle replay is not approved");
        aPlan.Apply.push_back(*stop);
    }
    return true;
}

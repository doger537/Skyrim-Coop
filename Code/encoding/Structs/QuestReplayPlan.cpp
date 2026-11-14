#include "QuestReplayPlan.h"
#include <algorithm>
#include <set>

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
    std::set<uint16_t> expected;
    std::set<uint16_t> reached(acLocal.Reached.begin(), acLocal.Reached.end());
    for (const auto& event : acJournal.Events)
        if (event.Status != 2)
            expected.insert(event.Stage);
    for (uint16_t stage : reached)
        if (!expected.contains(stage))
            return fail("Local quest contains progress outside the shared campaign; load a matching campaign save");
    const auto canReplay = [&](const auto& event)
    {
        return !event.Bootstrap && std::find(acReplayStages.begin(), acReplayStages.end(), event.Stage) != acReplayStages.end();
    };
    bool running = !acLocal.Stopped;
    const bool hasMissing = std::any_of(
        acJournal.Events.begin(), acJournal.Events.end(),
        [&](const auto& event) { return event.Status != 2 && !(event.Status == 1 && event.Stage == 0) && !reached.contains(event.Stage); });
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
        if (event.Status == 2 || reached.contains(event.Stage) || (event.Status == 1 && event.Stage == 0))
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

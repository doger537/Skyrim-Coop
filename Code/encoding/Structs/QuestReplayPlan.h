#pragma once
#include "QuestJournal.h"

struct LocalQuestState
{
    bool Exists{};
    bool Stopped{};
    TiltedPhoques::Vector<uint16_t> Reached;
};

struct QuestReplayPlan
{
    TiltedPhoques::Vector<QuestJournal::Event> Apply;
    TiltedPhoques::String Error;
};

// Make the full plan before invoking any engine functions. Imported completed-stage
// sets are observations, not an inferred Papyrus execution order.
bool PlanQuestReplay(const QuestJournal& acJournal, const TiltedPhoques::Vector<uint16_t>& acReplayStages, bool aReplayLifecycle,
                     const LocalQuestState& acLocal, QuestReplayPlan& aPlan) noexcept;

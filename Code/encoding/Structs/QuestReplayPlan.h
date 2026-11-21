#pragma once
#include "QuestAdmissionDiff.h"

struct QuestReplayPlan
{
    TiltedPhoques::Vector<QuestJournal::Event> Apply;
    QuestAdmissionProblem Problem{};
};

bool NeedsSharedJournal(const GameId& acId, uint8_t aType, const LocalQuestState& acLocal) noexcept;

// Make the full plan before invoking any engine functions. Imported completed-stage
// sets are observations, not an inferred Papyrus execution order.
bool PlanQuestReplay(const QuestJournal& acJournal, const TiltedPhoques::Vector<uint16_t>& acReplayStages, bool aReplayLifecycle,
                     const LocalQuestState& acLocal, QuestReplayPlan& aPlan, bool aNormalizeHistory = true) noexcept;

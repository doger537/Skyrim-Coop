#include "QuestScenePolicy.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace QuestScenePolicy
{
bool IsSupported(Comparison aOperation, float aValue) noexcept
{
    return aOperation <= Comparison::LessOrEqual && std::isfinite(aValue);
}

bool Compare(Comparison aOperation, float aLeft, float aRight) noexcept
{
    switch (aOperation)
    {
    case Comparison::Equal: return aLeft == aRight;
    case Comparison::NotEqual: return aLeft != aRight;
    case Comparison::Greater: return aLeft > aRight;
    case Comparison::GreaterOrEqual: return aLeft >= aRight;
    case Comparison::Less: return aLeft < aRight;
    case Comparison::LessOrEqual: return aLeft <= aRight;
    default: return false;
    }
}

size_t ClosingPrefix(std::span<const Condition> acConditions, uint16_t aCurrentStage, std::span<const uint16_t> acQueuedStages) noexcept
{
    if (acConditions.empty() || acConditions.size() > kMaxConditions || acConditions.back().OrWithNext || acQueuedStages.empty())
        return 0;

    // Resolve each GetStageDone term once. Later passes compare against the
    // first position that satisfies it, without rescanning the prefix.
    std::array<size_t, kMaxConditions> doneAt{};
    for (size_t i = 0; i < acConditions.size(); ++i)
    {
        const auto& condition = acConditions[i];
        if (condition.Source == Operand::CompletedStage && !condition.WasDone)
        {
            const auto found = std::find(acQueuedStages.begin(), acQueuedStages.end(), condition.Stage);
            doneAt[i] = static_cast<size_t>(found - acQueuedStages.begin()) + 1;
        }
    }

    enum class Truth : uint8_t { False, Unknown, True };
    const auto evaluate = [&](uint16_t aStage, size_t aPrefix)
    {
        Truth conditionList = Truth::True;
        Truth group = Truth::False;
        for (size_t i = 0; i < acConditions.size(); ++i)
        {
            const auto& condition = acConditions[i];
            Truth term = Truth::Unknown;
            if ((condition.Source == Operand::CurrentStage || condition.Source == Operand::CompletedStage) && IsSupported(condition.Operation, condition.Value))
            {
                const float value = condition.Source == Operand::CurrentStage ? static_cast<float>(aStage) : (doneAt[i] <= aPrefix ? 1.f : 0.f);
                term = Compare(condition.Operation, value, condition.Value) ? Truth::True : Truth::False;
            }

            group = std::max(group, term);
            if (!condition.OrWithNext)
            {
                conditionList = std::min(conditionList, group);
                if (conditionList == Truth::False)
                    break;
                group = Truth::False;
            }
        }
        return conditionList;
    };

    if (evaluate(aCurrentStage, 0) == Truth::True)
        return 0;

    for (size_t i = 0; i < acQueuedStages.size(); ++i)
    {
        aCurrentStage = std::max(aCurrentStage, acQueuedStages[i]);
        if (evaluate(aCurrentStage, i + 1) == Truth::True)
            return i + 1;
    }
    return 0;
}
} // namespace QuestScenePolicy

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace QuestScenePolicy
{
enum class Comparison : uint8_t
{
    Equal,
    NotEqual,
    Greater,
    GreaterOrEqual,
    Less,
    LessOrEqual
};

enum class Operand : uint8_t
{
    Unknown,
    CurrentStage,
    CompletedStage
};

struct Condition
{
    Operand Source{};
    Comparison Operation{};
    float Value{};
    uint16_t Stage{}; // Operand::CompletedStage only.
    bool WasDone{};
    bool OrWithNext{};
};

inline constexpr size_t kMaxConditions = 128;

bool IsSupported(Comparison aOperation, float aValue) noexcept;
bool Compare(Comparison aOperation, float aLeft, float aRight) noexcept;

// Conditions form a conjunction of OR groups, where the OR flag links the
// current term to the next. An unknown term can be covered by a true term in
// its group. Return zero when there is no proven reason for an early release.
// Positive results give the shortest ordered prefix that satisfies the phase.
size_t ClosingPrefix(std::span<const Condition> acConditions, uint16_t aCurrentStage, std::span<const uint16_t> acQueuedStages) noexcept;
} // namespace QuestScenePolicy

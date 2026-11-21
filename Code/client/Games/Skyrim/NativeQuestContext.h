#pragma once
#include <Events/QuestItemScriptOrigin.h>
#include <optional>

namespace NativeQuestContext
{
bool AuditEnabled() noexcept;
// Arm only on the runner after campaign admission. Other threads and offline
// gameplay pass through unchanged, without VM inspection.
void SetEnabled(bool aEnabled) noexcept;
std::optional<QuestItemScriptOrigin> Current() noexcept;
}

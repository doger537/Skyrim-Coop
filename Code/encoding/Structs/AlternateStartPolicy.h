#pragma once

#include <cstdint>
#include <string_view>

namespace AlternateStartPolicy
{
inline constexpr std::string_view cPlugin = "alternate start - live another life.esp";
inline constexpr std::string_view cDependency = "unofficial skyrim special edition patch.esp";

// Personal start selection must never be replayed onto another player's save.
// Only records originating in this plugin are private; overridden Skyrim quests retain their original IDs.
inline bool OwnsForm(uint32_t aFormId, uint16_t aSlot, bool aLite) noexcept
{
    if ((aFormId >> 24) == 0xFF)
        return false;
    return aLite ? (aFormId >> 24) == 0xFE && ((aFormId >> 12) & 0xFFF) == aSlot : (aFormId >> 24) == aSlot;
}
} // namespace AlternateStartPolicy

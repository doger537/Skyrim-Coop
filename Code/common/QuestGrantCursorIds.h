#pragma once
#include <cstdint>

namespace QuestGrantCursorIds
{
inline constexpr const char* kPlugin = "TrueCoopWorldState.esp";
inline constexpr uint32_t kFirstLocalId = 0x800;
inline constexpr uint32_t kWorldWords = 8;
inline constexpr uint32_t kCursorWords = 4;
inline constexpr uint32_t kVersionWord = kWorldWords + kCursorWords;
inline constexpr uint32_t kCount = kVersionWord + 1;
inline constexpr uint16_t kVersion = 1;
}

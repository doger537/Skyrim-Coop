#pragma once
#include <cstdint>
namespace WorldItemRegistryIds
{
inline constexpr const char* kPlugin = "TrueCoopWorldState.esp";
inline constexpr const char* kQuestEditorId = "TrueCoopWorldItemRegistry";
inline constexpr uint32_t kQuestLocalId = 0x900;
inline constexpr uint32_t kSlots = 64;
inline constexpr uint32_t kWords = 4;
inline constexpr uint32_t kFirstIdLocalId = 0xA00;
inline constexpr uint32_t kReceiptFirstLocalId = 0xB00;
inline constexpr uint32_t kReceiptVersionLocalId = 0xB04;
inline constexpr uint16_t kVersion = 1;
}

#pragma once
#include <cstdint>

namespace CharacterIdentityIds
{
inline constexpr uint32_t kFirstPartLocalId = 0x4DF6;
inline constexpr uint32_t kVersionLocalId = 0x4DFE;
inline constexpr uint32_t kVersion = 1;
static_assert(kVersionLocalId == kFirstPartLocalId + 8);
}

#pragma once
#include <cstdint>

struct CoopSleepIntentEvent
{
    uint64_t MenuSession{};
    uint64_t ConnectionGeneration{};
    uint8_t Hours{};
    bool Sleeping{};
};

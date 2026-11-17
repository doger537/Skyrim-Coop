#pragma once
#include <cstdint>

struct WorldCheckpointIntent
{
    bool Load{};
    uint64_t Id{};
    uint64_t ConnectionGeneration{};
};

#pragma once
#include <cstdint>
struct Actor;

namespace DialogueMovement
{
bool IsSupported() noexcept;
bool Hold(uint32_t aFormId, uint32_t aDurationMs) noexcept;
void Release(uint32_t aFormId) noexcept;
void Reset() noexcept;
void StopIfHeld(Actor* apActor, float aDelta) noexcept;
}

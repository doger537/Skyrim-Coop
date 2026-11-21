#pragma once
#include <cstdint>
namespace NativeGrantDelivery
{
// Atomics only: safe to inspect from native save/load callbacks. A loaded ESS
// starts a new generation so late disconnect cleanup cannot taint that world.
bool Begin(uint64_t& aGeneration) noexcept;
void Complete(uint64_t aGeneration) noexcept;
void Abort(uint64_t aGeneration, bool aMutated) noexcept;
void AfterLoad() noexcept;
bool HasPending() noexcept;
bool IsActive() noexcept;
uint64_t Generation() noexcept;
}

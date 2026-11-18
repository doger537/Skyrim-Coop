#include <TiltedOnlinePCH.h>
#include "NativeGrantDelivery.h"
#include <atomic>
namespace NativeGrantDelivery
{
namespace { std::atomic<uint64_t> s_state{}; }
bool Begin(uint64_t& aGeneration) noexcept
{
    uint64_t state = s_state.load(std::memory_order_acquire);
    if (state & 3) return false;
    aGeneration = state;
    return s_state.compare_exchange_strong(state, state | 1, std::memory_order_acq_rel);
}
void Complete(uint64_t aGeneration) noexcept
{
    uint64_t expected = aGeneration | 1;
    s_state.compare_exchange_strong(expected, aGeneration, std::memory_order_acq_rel);
}
void Abort(uint64_t aGeneration, bool aMutated) noexcept
{
    uint64_t expected = aGeneration | 1;
    s_state.compare_exchange_strong(expected, aGeneration | (aMutated ? 2 : 0), std::memory_order_acq_rel);
}
void AfterLoad() noexcept
{
    uint64_t state = s_state.load(std::memory_order_acquire);
    while (!s_state.compare_exchange_weak(state, (state & ~uint64_t{3}) + 4, std::memory_order_acq_rel)) {}
}
bool HasPending() noexcept { return (s_state.load(std::memory_order_acquire) & 3) != 0; }
bool IsActive() noexcept { return (s_state.load(std::memory_order_acquire) & 1) != 0; }
uint64_t Generation() noexcept { return s_state.load(std::memory_order_acquire) & ~uint64_t{3}; }
}

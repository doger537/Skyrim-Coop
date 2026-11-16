#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

// One runner writer, any number of engine readers. References are numeric IDs,
// never pointers. A reader takes one coherent ID/deadline snapshot without waiting.
class TimedReferenceSet
{
public:
    static constexpr std::size_t kCapacity = 8;
    static bool Pending(uint32_t aDeadline, uint32_t aNow) noexcept
    {
        const uint32_t remaining = aDeadline - aNow;
        return remaining != 0 && remaining < 0x80000000u;
    }
    bool Contains(uint32_t aId, uint32_t aNow) const noexcept
    {
        if (aId == 0)
            return false;
        for (const auto& entry : m_entries)
        {
            const uint64_t value = entry.load(std::memory_order_relaxed);
            if (static_cast<uint32_t>(value) == aId && Pending(static_cast<uint32_t>(value >> 32), aNow))
                return true;
        }
        return false;
    }
    bool Set(uint32_t aId, uint32_t aDeadline, uint32_t aNow) noexcept
    {
        if (aId == 0 || !Pending(aDeadline, aNow))
            return false;
        auto* slot = static_cast<std::atomic<uint64_t>*>(nullptr);
        for (auto& entry : m_entries)
        {
            const uint64_t value = entry.load(std::memory_order_relaxed);
            if (static_cast<uint32_t>(value) == aId)
            {
                slot = &entry;
                break;
            }
            if (!slot && (value == 0 || !Pending(static_cast<uint32_t>(value >> 32), aNow)))
                slot = &entry;
        }
        if (!slot)
            return false;
        slot->store((uint64_t(aDeadline) << 32) | aId, std::memory_order_relaxed);
        return true;
    }
    void Remove(uint32_t aId) noexcept
    {
        for (auto& entry : m_entries)
            if (static_cast<uint32_t>(entry.load(std::memory_order_relaxed)) == aId)
                entry.store(0, std::memory_order_relaxed);
    }
    void Clear() noexcept
    {
        for (auto& entry : m_entries)
            entry.store(0, std::memory_order_relaxed);
    }
private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::array<std::atomic<uint64_t>, kCapacity> m_entries{};
};

#pragma once

#include <array>
#include <cstdint>
#include <limits>

// Standard-plugin lookup is explicitly partial. An unknown slot is never Skyrim.esm (server id 0).
struct StandardModMapping
{
    StandardModMapping() noexcept { Reset(); }
    void Reset() noexcept
    {
        m_ids.fill(0);
        m_known.fill(false);
    }
    bool Add(uint32_t aSlot, uint32_t aServerId) noexcept
    {
        if (aSlot >= 0xFE || aServerId == std::numeric_limits<uint32_t>::max())
            return false;
        m_ids[aSlot] = aServerId;
        m_known[aSlot] = true;
        return true;
    }
    bool Resolve(uint32_t aSlot, uint32_t& aServerId) const noexcept
    {
        if (aSlot == 0xFF)
        {
            aServerId = std::numeric_limits<uint32_t>::max();
            return true;
        }
        if (aSlot >= 0xFE || !m_known[aSlot])
            return false;
        aServerId = m_ids[aSlot];
        return true;
    }

private:
    std::array<uint32_t, 0xFE> m_ids{};
    std::array<bool, 0xFE> m_known{};
};

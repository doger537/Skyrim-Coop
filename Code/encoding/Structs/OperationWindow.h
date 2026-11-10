#pragma once
#include <cstdint>

// Per authenticated connection: accept each operation once, including reordered operations within the window.
struct OperationWindow
{
    bool Accept(uint64_t aId) noexcept
    {
        if (aId == 0)
            return false;
        if (aId > m_latest)
        {
            const uint64_t gap = aId - m_latest;
            m_seen = gap >= 64 ? 1 : (m_seen << gap) | 1;
            m_latest = aId;
            return true;
        }
        const uint64_t distance = m_latest - aId;
        if (distance >= 64 || (m_seen & (uint64_t{1} << distance)))
            return false;
        m_seen |= uint64_t{1} << distance;
        return true;
    }

private:
    uint64_t m_latest{};
    uint64_t m_seen{};
};

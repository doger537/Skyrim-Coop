#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ReplicationClock
{
// Network ticks belong to one connection. A new snapshot or connection must
// establish its own baseline, even when its first tick is zero.
class TickBaseline
{
public:
    void Reset() noexcept { m_hasBaseline = false; }
    void Rebase(uint64_t aTick) noexcept
    {
        m_lastTick = aTick;
        m_hasBaseline = true;
    }
    uint64_t Sample(uint64_t aTick) noexcept
    {
        const uint64_t delta = m_hasBaseline && aTick >= m_lastTick ? aTick - m_lastTick : 0;
        Rebase(aTick);
        return delta;
    }

private:
    uint64_t m_lastTick = 0;
    bool m_hasBaseline = false;
};

// One refresh at most per update: a delayed frame never drains a backlog of
// redundant reliable packets. Subtraction avoids overflow on large tick deltas.
class Cadence
{
public:
    explicit Cadence(uint64_t aInterval) noexcept : m_interval(aInterval) {}
    void Reset() noexcept { m_elapsed = 0; }
    bool Advance(uint64_t aDelta) noexcept
    {
        if (m_interval == 0)
            return false;
        const uint64_t remaining = m_interval - m_elapsed;
        if (aDelta < remaining)
        {
            m_elapsed += aDelta;
            return false;
        }
        m_elapsed = (aDelta - remaining) % m_interval;
        return true;
    }

private:
    uint64_t m_interval;
    uint64_t m_elapsed = 0;
};

inline float WrapHour(float aHour) noexcept
{
    const float wrapped = std::fmod(aHour, 24.f);
    return wrapped < 0.f ? wrapped + 24.f : wrapped;
}

inline float BlendHour(float aFrom, float aTo, float aProgress) noexcept
{
    float delta = WrapHour(aTo - aFrom);
    if (delta > 12.f)
        delta -= 24.f;
    return WrapHour(aFrom + delta * std::clamp(aProgress, 0.f, 1.f));
}
}

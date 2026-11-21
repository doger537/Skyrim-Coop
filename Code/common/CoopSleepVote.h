#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstddef>

// Data only. The server owns this state; no cell/range dependency and no engine
// calls. Two recent votes and two prepare acknowledgements are required.
class CoopSleepVote
{
public:
    enum class Phase : uint8_t { Idle, Waiting, Preparing, Committed, Cancelled };
    enum class Action : uint8_t { Vote, Ready, Cancel };
    static constexpr uint64_t kWindowMs = 30000;
    static constexpr uint64_t kMaxIntentAgeMs = 15000;

    bool Configure(uint64_t aEpoch, uint32_t aHost, uint32_t aGuest) noexcept
    {
        const std::array<uint32_t, 2> members{aHost, aGuest};
        if (m_epoch == aEpoch && m_members == members)
            return false;
        m_epoch = aEpoch;
        m_members = members;
        m_sequences = {};
        m_hours = {};
        m_ready = {};
        m_phase = Phase::Cancelled;
        return true;
    }

    bool Submit(uint32_t aMember, uint64_t aSequence, uint64_t aRound, Action aAction,
                uint8_t aHours, uint64_t aIntentTick, uint64_t aNow) noexcept
    {
        if (m_epoch == 0 || m_members[0] == 0 || m_members[1] == 0 || m_members[0] == m_members[1])
            return false;
        const size_t index = aMember == m_members[0] ? 0 : 1;
        if (m_members[index] != aMember || aSequence == 0 || aSequence <= m_sequences[index] || aNow > std::numeric_limits<uint64_t>::max() - kWindowMs ||
            (aIntentTick > aNow && aIntentTick - aNow > 2000) || (aNow > aIntentTick && aNow - aIntentTick > kMaxIntentAgeMs))
            return false;
        m_sequences[index] = aSequence;
        Expire(aNow);
        if (aAction == Action::Vote)
        {
            if (aHours < 1 || aHours > 24)
                return false;
            if (m_phase != Phase::Waiting)
            {
                if (aRound != 0 || m_phase == Phase::Preparing || m_round == std::numeric_limits<uint64_t>::max())
                    return false;
                ++m_round;
                m_phase = Phase::Waiting;
                m_hours = {};
                m_ready = {};
                m_deadline = aNow + kWindowMs;
            }
            else if (aRound != 0 && aRound != m_round)
                return false;
            m_hours[index] = aHours;
            if (m_hours[0] != 0 && m_hours[1] != 0)
                m_phase = Phase::Preparing;
            return true;
        }
        if (aRound != m_round || (m_phase != Phase::Waiting && m_phase != Phase::Preparing))
            return false;
        if (aAction == Action::Cancel)
        {
            m_phase = Phase::Cancelled;
            return true;
        }
        if (aAction != Action::Ready || m_phase != Phase::Preparing || m_ready[index])
            return false;
        m_ready[index] = true;
        if (m_ready[0] && m_ready[1])
            m_phase = Phase::Committed;
        return true;
    }

    bool Expire(uint64_t aNow) noexcept
    {
        if ((m_phase == Phase::Waiting || m_phase == Phase::Preparing) && aNow >= m_deadline)
        {
            m_phase = Phase::Cancelled;
            return true;
        }
        return false;
    }

    Phase GetPhase() const noexcept { return m_phase; }
    void Abort() noexcept { m_phase = Phase::Cancelled; }
    void SetRoundFloor(uint64_t aFloor) noexcept
    {
        if (m_phase != Phase::Waiting && m_phase != Phase::Preparing)
            m_round = std::max(m_round, aFloor);
    }
    uint64_t Round() const noexcept { return m_round; }
    uint64_t Deadline() const noexcept { return m_deadline; }
    uint8_t Hours() const noexcept { return std::min(m_hours[0], m_hours[1]); }
    uint8_t Votes() const noexcept { return (m_hours[0] ? 1 : 0) | (m_hours[1] ? 2 : 0); }
    const std::array<uint32_t, 2>& Members() const noexcept { return m_members; }

private:
    std::array<uint32_t, 2> m_members{};
    std::array<uint64_t, 2> m_sequences{};
    std::array<uint8_t, 2> m_hours{};
    std::array<bool, 2> m_ready{};
    uint64_t m_epoch{};
    uint64_t m_round{};
    uint64_t m_deadline{};
    Phase m_phase{Phase::Idle};
};

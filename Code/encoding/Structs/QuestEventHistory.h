#pragma once
#include <cstdint>
#include <set>

// Deduplicate the existing party relay. This is not a durable quest journal or a
// story-choice resolver. Never infer quest ordering from the numeric value of a stage.
struct QuestEventHistory
{
    enum Event : uint8_t
    {
        StageUpdate,
        Started,
        Stopped
    };

    bool Accept(uint8_t aEvent, uint16_t aStage) noexcept
    {
        switch (aEvent)
        {
        case StageUpdate: return m_stages.insert(aStage).second;
        case Started:
            if (m_started && !m_stopped)
                return false;
            if (m_stopped)
                m_stages.clear();
            m_started = true;
            m_stopped = false;
            return true;
        case Stopped:
            if (m_stopped)
                return false;
            m_stopped = true;
            return true;
        default: return false;
        }
    }

private:
    std::set<uint16_t> m_stages;
    bool m_started{};
    bool m_stopped{};
};

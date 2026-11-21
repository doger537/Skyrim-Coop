#pragma once

#include <cstdint>

struct QuestApplyConfirmation
{
    enum class Next
    {
        Invoke,
        Wait,
        Confirm,
        Failed
    };

    static constexpr uint64_t kTimeoutMs = 5000;
    uint64_t Deadline{};
    uint32_t PendingFragments{};

    Next Observe(uint64_t aNow, bool aReached, bool aSceneBlocked = false) noexcept
    {
        if (aReached)
            return PendingFragments ? Next::Wait : Next::Confirm;
        if (!Deadline)
        {
            if (aSceneBlocked)
                return Next::Wait;
            Deadline = aNow + kTimeoutMs;
            return Next::Invoke;
        }
        return aNow < Deadline ? Next::Wait : Next::Failed;
    }
};

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

    Next Observe(uint64_t aNow, bool aReached) noexcept
    {
        if (aReached)
            return Next::Confirm;
        if (!Deadline)
        {
            Deadline = aNow + kTimeoutMs;
            return Next::Invoke;
        }
        return aNow < Deadline ? Next::Wait : Next::Failed;
    }
};

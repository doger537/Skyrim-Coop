#pragma once

#include <cstdint>
#include <encoding/Structs/TimeModel.h>

struct DateTime
{
    [[nodiscard]] static uint32_t GetNumberOfDaysByMonthIndex(int index);

    DateTime() = default;
    DateTime(TimeModel aTimeModel)
        : m_timeModel(aTimeModel)
    {}

    bool operator==(const DateTime& other) const;
    void Update(uint64_t aDeltaTick);
    bool AdvanceHours(double aHours) noexcept;
    float GetDeltaTime(uint64_t aDeltaTick) const noexcept;
    float GetTimeInDays() const noexcept;

    TimeModel m_timeModel;

private:
    // Preserve the fraction lost when publishing a float GameHour, rather than
    // accumulating a different rounding error at each client's frame rate.
    double m_hourRemainder{};
    float m_lastPublishedHour{12.f};
};

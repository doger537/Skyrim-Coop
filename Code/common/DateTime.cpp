#include "DateTime.h"
#include <cmath>

bool DateTime::operator==(const DateTime& other) const
{
    return m_timeModel == other.m_timeModel;
}

const uint32_t cDayLengthArray[12] = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

uint32_t DateTime::GetNumberOfDaysByMonthIndex(int aIndex)
{
    return cDayLengthArray[aIndex % 12];
}

void DateTime::Update(uint64_t aDeltaTick)
{
    AdvanceHours(static_cast<double>(aDeltaTick) / 1000.0 * m_timeModel.TimeScale / 3600.0);
}

bool DateTime::AdvanceHours(double aHours) noexcept
{
    if (!m_timeModel.IsValid() || !std::isfinite(aHours) || aHours < 0.0 || aHours > 24.0 * 366.0 ||
        m_timeModel.DaysPassed + aHours / 24.0 > 365250.0)
        return false;
    TimeModel next = m_timeModel;
    const double remainder = m_lastPublishedHour == next.Time ? m_hourRemainder : 0.0;
    const double hours = next.Time + remainder + aHours;
    next.Day += static_cast<uint32_t>(hours / 24.0);
    const double exactHour = std::fmod(hours, 24.0);
    next.Time = static_cast<float>(exactHour);
    // Do not enter tomorrow early merely because float rounds up to 24.
    if (next.Time >= 24.f)
        next.Time = std::nextafter(24.f, 0.f);
    next.DaysPassed += aHours / 24.0;

    while (next.Day > GetNumberOfDaysByMonthIndex(next.Month))
    {
        next.Day -= GetNumberOfDaysByMonthIndex(next.Month);
        if (++next.Month == 12)
        {
            next.Month = 0;
            ++next.Year;
        }
    }
    if (!next.IsValid())
        return false;
    m_timeModel = next;
    m_hourRemainder = exactHour - next.Time;
    m_lastPublishedHour = next.Time;
    return true;
}

float DateTime::GetDeltaTime(uint64_t aDeltaTick) const noexcept
{
    float deltaSeconds = static_cast<float>(aDeltaTick) / 1000.f;
    return (deltaSeconds * (m_timeModel.TimeScale * 0.00027777778f));
}

float DateTime::GetTimeInDays() const noexcept
{
    float totalDays = static_cast<float>(m_timeModel.Year) * 365.f;
    for (uint32_t i = 0u; i < m_timeModel.Month; i++)
        totalDays += GetNumberOfDaysByMonthIndex(i);
    totalDays += m_timeModel.Day;
    totalDays += (m_timeModel.Time / 24.f);
    return totalDays;
}

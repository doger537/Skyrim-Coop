#include <Structs/TimeModel.h>
#include <TiltedCore/Serialization.hpp>
#include <bit>
#include <cmath>

using TiltedPhoques::Serialization;

bool TimeModel::IsValid() const noexcept
{
    constexpr uint32_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return Decoded && std::isfinite(TimeScale) && TimeScale >= 0.f && TimeScale <= 1000.f &&
           std::isfinite(Time) && Time >= 0.f && Time < 24.f && Month < 12 && Day >= 1 && Day <= kDays[Month] && Year <= 999 &&
           std::isfinite(DaysPassed) && DaysPassed >= 0.0 && DaysPassed <= 365250.0;
}

void TimeModel::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsValid(), 1);
    if (!IsValid())
        return;
    aWriter.WriteBits(std::bit_cast<uint32_t>(TimeScale), 32);
    aWriter.WriteBits(std::bit_cast<uint32_t>(Time), 32);
    aWriter.WriteBits(std::bit_cast<uint64_t>(DaysPassed), 64);
    aWriter.WriteBits(Year, 16);
    aWriter.WriteBits(Month, 8);
    aWriter.WriteBits(Day, 8);
}

void TimeModel::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t value{};
    if (!aReader.ReadBits(value, 1) || value == 0 || !aReader.ReadBits(value, 32))
        return;
    TimeScale = std::bit_cast<float>(static_cast<uint32_t>(value));
    if (!aReader.ReadBits(value, 32))
        return;
    Time = std::bit_cast<float>(static_cast<uint32_t>(value));
    if (!aReader.ReadBits(value, 64))
        return;
    DaysPassed = std::bit_cast<double>(value);
    if (!aReader.ReadBits(value, 16))
        return;
    Year = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(value, 8))
        return;
    Month = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(value, 8))
        return;
    Day = static_cast<uint32_t>(value);
    Decoded = true;
    Decoded = IsValid();
}

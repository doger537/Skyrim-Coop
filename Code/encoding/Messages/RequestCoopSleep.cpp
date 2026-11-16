#include "RequestCoopSleep.h"

bool RequestCoopSleep::IsValid() const noexcept
{
    return Decoded && Sequence != 0 && Action <= CoopSleepVote::Action::Cancel &&
           (Action == CoopSleepVote::Action::Vote ? Hours >= 1 && Hours <= 24 : Hours == 0 && Round != 0);
}

void RequestCoopSleep::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsValid(), 1);
    if (!IsValid())
        return;
    aWriter.WriteBits(static_cast<uint8_t>(Action), 8);
    aWriter.WriteBits(Sequence, 64);
    aWriter.WriteBits(Round, 64);
    aWriter.WriteBits(Tick, 64);
    aWriter.WriteBits(Hours, 8);
}

void RequestCoopSleep::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t value{};
    if (!aReader.ReadBits(value, 1) || value == 0 || !aReader.ReadBits(value, 8))
        return;
    Action = static_cast<CoopSleepVote::Action>(value);
    if (!aReader.ReadBits(Sequence, 64) || !aReader.ReadBits(Round, 64) || !aReader.ReadBits(Tick, 64) || !aReader.ReadBits(value, 8))
        return;
    Hours = static_cast<uint8_t>(value);
    Decoded = true;
    Decoded = IsValid();
}

#include "NotifyCoopSleep.h"

bool NotifyCoopSleep::IsValid() const noexcept
{
    return Decoded && Round != 0 && Members[0] != 0 && Members[1] != 0 && Members[0] != Members[1] &&
           Phase >= CoopSleepVote::Phase::Waiting && Phase <= CoopSleepVote::Phase::Cancelled && Votes <= 3 && Hours <= 24 &&
           ((Phase != CoopSleepVote::Phase::Preparing && Phase != CoopSleepVote::Phase::Committed) || (Votes == 3 && Hours >= 1)) &&
           (Phase != CoopSleepVote::Phase::Committed || Time.IsValid());
}

void NotifyCoopSleep::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsValid(), 1);
    if (!IsValid())
        return;
    aWriter.WriteBits(static_cast<uint8_t>(Phase), 8);
    for (uint32_t member : Members)
        aWriter.WriteBits(member, 32);
    aWriter.WriteBits(Round, 64);
    aWriter.WriteBits(Deadline, 64);
    aWriter.WriteBits(Tick, 64);
    aWriter.WriteBits(Hours, 8);
    aWriter.WriteBits(Votes, 8);
    if (Phase == CoopSleepVote::Phase::Committed)
        Time.Serialize(aWriter);
}

void NotifyCoopSleep::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t value{};
    if (!aReader.ReadBits(value, 1) || value == 0 || !aReader.ReadBits(value, 8))
        return;
    Phase = static_cast<CoopSleepVote::Phase>(value);
    for (uint32_t& member : Members)
    {
        if (!aReader.ReadBits(value, 32))
            return;
        member = static_cast<uint32_t>(value);
    }
    if (!aReader.ReadBits(Round, 64) || !aReader.ReadBits(Deadline, 64) || !aReader.ReadBits(Tick, 64) || !aReader.ReadBits(value, 8))
        return;
    Hours = static_cast<uint8_t>(value);
    if (!aReader.ReadBits(value, 8))
        return;
    Votes = static_cast<uint8_t>(value);
    if (Phase == CoopSleepVote::Phase::Committed)
        Time.Deserialize(aReader);
    Decoded = true;
    Decoded = IsValid();
}

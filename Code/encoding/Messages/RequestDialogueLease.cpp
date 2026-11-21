#include "RequestDialogueLease.h"

void RequestDialogueLease::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(IsValid(), 1);
    if (!IsValid())
        return;
    aWriter.WriteBits(ServerId, 32);
    aWriter.WriteBits(OwnershipEpoch, 32);
    aWriter.WriteBits(Sequence, 64);
    aWriter.WriteBits(Tick, 64);
    aWriter.WriteBits(Active, 1);
}

void RequestDialogueLease::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    Decoded = false;
    uint64_t value{};
    if (!aReader.ReadBits(value, 1) || value == 0 || !aReader.ReadBits(value, 32))
        return;
    ServerId = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(value, 32))
        return;
    OwnershipEpoch = static_cast<uint32_t>(value);
    if (!aReader.ReadBits(Sequence, 64) || !aReader.ReadBits(Tick, 64) || !aReader.ReadBits(value, 1))
        return;
    Active = value != 0;
    Decoded = true;
    Decoded = IsValid();
}
